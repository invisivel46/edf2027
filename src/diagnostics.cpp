// EDF2027 - bug-report plumbing (diagnostics.h).
#include "diagnostics.h"

#include "native_graphics/native_disk_cache.h"  // NativeModuleIdentity
#include "native_graphics/native_ffx.h"
#include "native_graphics/native_fsr.h"         // NativeFsrLibrary
#include "version.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <fmt/format.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <dxgi1_6.h>
#include <intrin.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <exception>
#include <map>
#include <mutex>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

REXCVAR_DEFINE_INT32(edf_crash_test, 0, "EDF2027",
                     "Development: crash on purpose once the game is up, to check crash reporting: 0 off, 1 access "
                     "violation, 2 uncaught C++ exception on a thread, 3 abort()")
    .range(0, 3)
    .debug_only();

namespace edf::diag {
namespace {

// ---- State the crash path reads ---------------------------------------------------------
// Fixed buffers, filled before anything can crash: the crash path must not depend on a
// heap or a lock that the crashing thread may be holding.
constexpr size_t kPathChars = 1024;
wchar_t g_log_dir[kPathChars];     // where dumps go
wchar_t g_log_path[kPathChars];    // this run's log file
wchar_t g_run_stem[64] = L"edf2027";  // "edf2027_012": the dump is named after the log
char g_terminate_reason[1024];     // what std::terminate / abort / purecall was about
std::atomic<bool> g_crashing{false};
std::mutex g_paths_mutex;
std::filesystem::path g_log_path_fs, g_log_dir_fs;

void CopyPath(wchar_t* out, const std::filesystem::path& path) {
  const std::wstring text = path.wstring();
  const size_t n = (std::min)(text.size(), kPathChars - 1);
  std::wmemcpy(out, text.data(), n);
  out[n] = 0;
}

void SetCrashDirectory(const std::filesystem::path& directory, const std::filesystem::path& log_file) {
  std::lock_guard lock(g_paths_mutex);
  g_log_dir_fs = directory;
  g_log_path_fs = log_file;
  CopyPath(g_log_dir, directory);
  CopyPath(g_log_path, log_file);
  std::wstring stem = log_file.empty() ? std::wstring(L"edf2027") : log_file.stem().wstring();
  const size_t n = (std::min)(stem.size(), std::size(g_run_stem) - 1);
  std::wmemcpy(g_run_stem, stem.data(), n);
  g_run_stem[n] = 0;
}

std::filesystem::path ExecutableFolder() {
  wchar_t buffer[kPathChars];
  const DWORD length = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
  if (!length || length >= std::size(buffer)) return std::filesystem::current_path();
  return std::filesystem::path(std::wstring(buffer, length)).parent_path();
}

// ---- Crash reporting ---------------------------------------------------------------------
struct DumpJob {
  EXCEPTION_POINTERS* pointers = nullptr;
  DWORD thread_id = 0;
  wchar_t dump_path[kPathChars] = {};
  wchar_t text_path[kPathChars] = {};
  char summary[2048] = {};
  bool dump_written = false;
  bool text_written = false;
  bool log = true;  // also log the summary and flush (may block: runs on the helper thread)
};

// Module-relative address: "edf2027.exe+0x1234ab".
void DescribeAddress(const void* address, char* out, size_t capacity) {
  HMODULE module = nullptr;
  if (address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    static_cast<LPCWSTR>(address), &module) && module) {
    char name[MAX_PATH];
    const DWORD length = GetModuleFileNameA(module, name, DWORD(std::size(name)));
    const char* base = name;
    for (DWORD i = 0; i < length; ++i)
      if (name[i] == '\\' || name[i] == '/') base = name + i + 1;
    std::snprintf(out, capacity, "%s+0x%llx", length ? base : "?",
                  static_cast<unsigned long long>(static_cast<const char*>(address) - reinterpret_cast<const char*>(module)));
  } else {
    std::snprintf(out, capacity, "0x%p", address);
  }
}

// An MSVC C++ exception (0xE06D7363) carries the thrown object and its type list; when one
// of the types is std::exception, its what() is the most useful line in the report.
// x64 layout: ExceptionInformation = {magic, object, ThrowInfo*, image base}, with RVAs.
constexpr DWORD kCppException = 0xE06D7363;
const char* CppExceptionWhat(const EXCEPTION_RECORD* record) {
  if (!record || record->ExceptionCode != kCppException || record->NumberParameters < 4) return nullptr;
  const ULONG_PTR magic = record->ExceptionInformation[0];
  if (magic != 0x19930520 && magic != 0x19930521 && magic != 0x19930522) return nullptr;
  auto* object = reinterpret_cast<const char*>(record->ExceptionInformation[1]);
  auto* throw_info = reinterpret_cast<const int32_t*>(record->ExceptionInformation[2]);
  auto* image = reinterpret_cast<const char*>(record->ExceptionInformation[3]);
  if (!object || !throw_info || !image) return nullptr;
  // ThrowInfo: attributes, pmfnUnwind, pForwardCompat, pCatchableTypeArray.
  auto* types = reinterpret_cast<const int32_t*>(image + throw_info[3]);
  const int32_t count = types[0];
  for (int32_t i = 0; i < count && i < 64; ++i) {
    // CatchableType: properties, pType, PMD{mdisp,pdisp,vdisp}, sizeOrOffset, copyFunction.
    auto* catchable = reinterpret_cast<const int32_t*>(image + types[1 + i]);
    // TypeDescriptor: vftable pointer, spare pointer, then the decorated name.
    const char* name = image + catchable[1] + 2 * sizeof(void*);
    if (std::strcmp(name, ".?AVexception@std@@") == 0) {
      auto* as_exception = reinterpret_cast<const std::exception*>(object + catchable[2]);
      return as_exception->what();
    }
  }
  return nullptr;
}

const char* ExceptionName(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page error (a file or the page file could not be read)";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
    case EXCEPTION_BREAKPOINT: return "breakpoint";
    case kCppException: return "unhandled C++ exception";
    case 0xE0ED2027: return "fatal error";
    default: return "exception";
  }
}

void BuildSummary(DumpJob& job) {
  const EXCEPTION_RECORD* record = job.pointers ? job.pointers->ExceptionRecord : nullptr;
  char where[512] = "?";
  DescribeAddress(record ? record->ExceptionAddress : nullptr, where, sizeof(where));
  char detail[1400] = "";
  if (record && record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
    const ULONG_PTR op = record->ExceptionInformation[0];
    std::snprintf(detail, sizeof(detail), " (%s of address 0x%llx)", op == 0 ? "read" : op == 1 ? "write" : "execute",
                  static_cast<unsigned long long>(record->ExceptionInformation[1]));
  } else if (const char* what = CppExceptionWhat(record)) {
    std::snprintf(detail, sizeof(detail), ": %s", what);
  } else if (g_terminate_reason[0]) {
    std::snprintf(detail, sizeof(detail), ": %s", g_terminate_reason);
  }
  std::snprintf(job.summary, sizeof(job.summary), "EDF2027 %s crashed: %s 0x%08lX at %s on thread %lu%s",
                edf::version::Full(), ExceptionName(record ? record->ExceptionCode : 0),
                record ? static_cast<unsigned long>(record->ExceptionCode) : 0ul, where,
                static_cast<unsigned long>(job.thread_id), detail);
}

DWORD WINAPI DumpThread(void* parameter) {
  auto& job = *static_cast<DumpJob*>(parameter);
  // The dump first: it is what a developer needs, and the log flush below can block.
  HANDLE file = CreateFileW(job.dump_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION exception{job.thread_id, job.pointers, FALSE};
    // Stacks, thread and module lists, and the memory the stacks point at: enough to
    // symbolize every thread with the release's PDB, a few MB rather than the guest's
    // whole address space.
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                                 MiniDumpWithUnloadedModules | MiniDumpWithHandleData |
                                                 MiniDumpIgnoreInaccessibleMemory);
    job.dump_written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                         job.pointers ? &exception : nullptr, nullptr, nullptr) != FALSE;
    CloseHandle(file);
    if (!job.dump_written) DeleteFileW(job.dump_path);
  }
  // The same facts in words, beside the dump, for whoever opens the folder.
  HANDLE text = CreateFileW(job.text_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (text != INVALID_HANDLE_VALUE) {
    char body[4096];
    char log_utf8[kPathChars * 3] = "", dump_utf8[kPathChars * 3] = "";
    WideCharToMultiByte(CP_UTF8, 0, g_log_path, -1, log_utf8, int(sizeof(log_utf8)), nullptr, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, job.dump_path, -1, dump_utf8, int(sizeof(dump_utf8)), nullptr, nullptr);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const int length = std::snprintf(body, sizeof(body),
                                     "%s\r\n\r\nTime: %04d-%02d-%02d %02d:%02d:%02d\r\nVersion: %s\r\nLog: %s\r\nDump: %s\r\n",
                                     job.summary, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
                                     edf::version::Full(), log_utf8, job.dump_written ? dump_utf8 : "(could not be written)");
    DWORD written = 0;
    job.text_written = length > 0 && WriteFile(text, body, DWORD((std::min)(length, int(sizeof(body) - 1))), &written, nullptr);
    CloseHandle(text);
  }
  if (job.log) {
    REXLOG_CRITICAL("{}", job.summary);
    if (job.dump_written) {
      char dump_utf8[kPathChars * 3] = "";
      WideCharToMultiByte(CP_UTF8, 0, job.dump_path, -1, dump_utf8, int(sizeof(dump_utf8)), nullptr, nullptr);
      REXLOG_CRITICAL("Crash dump: {}", dump_utf8);
    }
    rex::FlushLogging();
  }
  return 0;
}

// Runs the dump on a thread of its own: the crashing thread may have overflowed its
// stack, and MiniDumpWriteDump walks the calling thread poorly. Bounded, so a log lock
// held by the crashed thread cannot turn a crash into a hang.
void WriteReport(DumpJob& job, const wchar_t* suffix) {
  std::swprintf(job.dump_path, kPathChars, L"%ls\\%ls_%ls.dmp", g_log_dir, g_run_stem, suffix);
  std::swprintf(job.text_path, kPathChars, L"%ls\\%ls_%ls.txt", g_log_dir, g_run_stem, suffix);
  CreateDirectoryW(g_log_dir, nullptr);
  HANDLE thread = CreateThread(nullptr, 256 * 1024, DumpThread, &job, 0, nullptr);
  if (thread) {
    WaitForSingleObject(thread, 30000);
    CloseHandle(thread);
  }
}

HWND FindGameWindow() {
  struct Search { DWORD process; HWND found; } search{GetCurrentProcessId(), nullptr};
  EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
    auto& s = *reinterpret_cast<Search*>(parameter);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process == s.process && IsWindowVisible(window) && !GetWindow(window, GW_OWNER)) { s.found = window; return FALSE; }
    return TRUE;
  }, reinterpret_cast<LPARAM>(&search));
  return search.found;
}

void OpenFolderSelecting(const wchar_t* path) {
  wchar_t command[kPathChars + 64];
  std::swprintf(command, std::size(command), L"explorer.exe /select,\"%ls\"", path);
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION process{};
  if (CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
  }
}

// The box: out of a borderless-fullscreen window's way (asynchronously, so a UI thread that
// is blocked cannot hang the crash path), topmost, with an offer to open the folder.
void ShowReportBox(const wchar_t* title, const wchar_t* text, const wchar_t* select) {
  if (HWND window = FindGameWindow()) ShowWindowAsync(window, SW_MINIMIZE);
  const UINT buttons = select && select[0] ? MB_YESNO : MB_OK;
  const int choice = MessageBoxW(nullptr, text, title, buttons | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST | MB_TASKMODAL);
  if (choice == IDYES) OpenFolderSelecting(select);
}

[[noreturn]] void CrashAndExit(EXCEPTION_POINTERS* pointers) {
  static DumpJob job;  // static: no stack needed on an overflowed thread
  job.pointers = pointers;
  job.thread_id = GetCurrentThreadId();
  BuildSummary(job);
  WriteReport(job, L"crash");

  static wchar_t text[6144];
  wchar_t summary[2048];
  MultiByteToWideChar(CP_UTF8, 0, job.summary, -1, summary, int(std::size(summary)));
  std::swprintf(text, std::size(text),
                L"%ls\n\n"
                L"%ls%ls%ls"
                L"Log:\n%ls\n\n"
                L"Please attach %ls to your bug report, with the log. They contain no personal files, only the game's state "
                L"at the moment of the crash.\n\nOpen the folder now?",
                summary, job.dump_written ? L"Crash dump:\n" : L"(The crash dump could not be written.)\n\n",
                job.dump_written ? job.dump_path : L"", job.dump_written ? L"\n\n" : L"",
                g_log_path[0] ? g_log_path : L"(no log file)", job.dump_written ? L"the .dmp file" : L"the log");
  ShowReportBox(L"EDF2027 has crashed", text, job.dump_written ? job.dump_path : g_log_path);
  TerminateProcess(GetCurrentProcess(), pointers && pointers->ExceptionRecord ? pointers->ExceptionRecord->ExceptionCode : 3);
  for (;;) Sleep(INFINITE);
}

LONG WINAPI UnhandledException(EXCEPTION_POINTERS* pointers) {
  // A second crash (another thread, or the report itself) waits for the first report.
  if (g_crashing.exchange(true)) {
    Sleep(INFINITE);
    return EXCEPTION_CONTINUE_SEARCH;
  }
  CrashAndExit(pointers);
}

// std::terminate, abort, purecall and invalid-parameter reports have no exception record
// of their own; give the dump one, with this thread's context.
[[noreturn]] void CrashHere(const char* reason) {
  if (g_crashing.exchange(true)) for (;;) Sleep(INFINITE);
  if (reason && !g_terminate_reason[0]) std::snprintf(g_terminate_reason, sizeof(g_terminate_reason), "%s", reason);
  static CONTEXT context;
  static EXCEPTION_RECORD record;
  static EXCEPTION_POINTERS pointers;
  RtlCaptureContext(&context);
  record = {};
  record.ExceptionCode = 0xE0ED2027;
  record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
  record.ExceptionAddress = reinterpret_cast<void*>(context.Rip);
  pointers = {&record, &context};
  CrashAndExit(&pointers);
}

// The last C++ exception thrown on each thread, noted as it is thrown (a first-chance
// vectored handler; throws are rare and this only copies a string). An exception escaping
// a noexcept boundary reaches terminate/abort with no current exception to ask, and this
// is then the only record of what it was.
thread_local char t_last_throw[512];
LONG WINAPI NoteThrow(EXCEPTION_POINTERS* pointers) {
  if (pointers && pointers->ExceptionRecord && pointers->ExceptionRecord->ExceptionCode == kCppException)
    if (const char* what = CppExceptionWhat(pointers->ExceptionRecord))
      std::snprintf(t_last_throw, sizeof(t_last_throw), "%s", what);
  return EXCEPTION_CONTINUE_SEARCH;
}

// The exception in flight, if any, as the reason. The MSVC runtime keeps the terminate
// handler per thread, so a thread the game did not start reaches abort() instead of
// OnTerminate; both ask.
void DescribeCurrentException(const char* context) {
  if (auto current = std::current_exception()) {
    try {
      std::rethrow_exception(current);
    } catch (const std::exception& error) {
      std::snprintf(g_terminate_reason, sizeof(g_terminate_reason), "%s: uncaught exception: %s", context, error.what());
    } catch (...) {
      std::snprintf(g_terminate_reason, sizeof(g_terminate_reason), "%s: uncaught non-standard exception", context);
    }
  }
}

void OnTerminate() {
  DescribeCurrentException("std::terminate");
  if (!g_terminate_reason[0] && t_last_throw[0])
    std::snprintf(g_terminate_reason, sizeof(g_terminate_reason),
                  "std::terminate; the last C++ exception thrown on this thread: %s", t_last_throw);
  CrashHere("std::terminate (an exception escaped a thread or a noexcept function)");
}

void OnAbortSignal(int) {
  DescribeCurrentException("abort()");
  if (!g_terminate_reason[0] && t_last_throw[0])
    std::snprintf(g_terminate_reason, sizeof(g_terminate_reason),
                  "abort() was called; the last C++ exception thrown on this thread: %s", t_last_throw);
  CrashHere("abort() was called");
}
void OnPureCall() { CrashHere("pure virtual function call"); }
void OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
  CrashHere("C runtime invalid parameter");
}

// ---- Log files ---------------------------------------------------------------------------
bool WritableDirectory(const std::filesystem::path& directory) {
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (!std::filesystem::is_directory(directory, error)) return false;
  const auto probe = directory / L".edf2027-write-test";
  HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  CloseHandle(file);
  return true;
}

// "edf2027_012.log", "edf2027_012.1.log", "edf2027_012_crash.dmp" -> 12.
std::optional<int> RunNumber(const std::wstring& name) {
  constexpr std::wstring_view kPrefix = L"edf2027_";
  if (!name.starts_with(kPrefix)) return std::nullopt;
  size_t at = kPrefix.size(), end = at;
  while (end < name.size() && iswdigit(name[end])) ++end;
  if (end == at || end - at > 9 || end == name.size() || (name[end] != L'.' && name[end] != L'_')) return std::nullopt;
  return std::stoi(name.substr(at, end - at));
}

}  // namespace

void InstallCrashHandler() {
  static std::once_flag once;
  std::call_once(once, [] {
    if (!g_log_dir[0]) SetCrashDirectory(ExecutableFolder() / "logs", {});
    SetUnhandledExceptionFilter(UnhandledException);
    AddVectoredExceptionHandler(0, NoteThrow);  // last in line: observes, never handles
    std::set_terminate(OnTerminate);
    std::signal(SIGABRT, OnAbortSignal);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_purecall_handler(OnPureCall);
    _set_invalid_parameter_handler(OnInvalidParameter);
    // Make room for the handler on an overflowed stack.
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
  });
}

void PrepareLogFile(const std::filesystem::path& fallback_root) {
  const std::string explicit_log = rex::cvar::GetFlagByName("log_file");
  if (!explicit_log.empty()) {
    const std::filesystem::path path(explicit_log);
    SetCrashDirectory(path.has_parent_path() ? path.parent_path() : std::filesystem::current_path(), path);
    return;
  }
  std::filesystem::path directory = ExecutableFolder() / "logs";
  if (!WritableDirectory(directory)) {
    directory = fallback_root.empty() ? std::filesystem::path() : fallback_root / "logs";
    if (directory.empty() || !WritableDirectory(directory)) {
      std::error_code error;
      directory = std::filesystem::temp_directory_path(error) / "edf2027" / "logs";
      if (!WritableDirectory(directory)) return;  // the SDK's own default, and its own failure
    }
  }
  // Group every file by run number; keep the newest kKeptRuns - 1 runs, plus this one.
  std::map<int, std::vector<std::filesystem::path>> runs;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
    if (!entry.is_regular_file(error)) continue;
    if (auto run = RunNumber(entry.path().filename().wstring())) runs[*run].push_back(entry.path());
  }
  const int next = runs.empty() ? 1 : runs.rbegin()->first + 1;
  while (runs.size() > size_t(kKeptRuns - 1)) {
    for (const auto& file : runs.begin()->second) std::filesystem::remove(file, error);
    runs.erase(runs.begin());
  }
  const auto path = directory / fmt::format("edf2027_{:03d}.log", next);
  rex::cvar::SetFlagByName("log_file", path.generic_string());
  SetCrashDirectory(directory, path);
}

std::filesystem::path LogPath() {
  std::lock_guard lock(g_paths_mutex);
  return g_log_path_fs;
}
std::filesystem::path LogDirectory() {
  std::lock_guard lock(g_paths_mutex);
  return g_log_dir_fs;
}

// ---- Startup report ----------------------------------------------------------------------
namespace {
std::wstring Widen(std::string_view text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
  std::wstring out(size_t(size > 0 ? size : 0), L'\0');
  if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size);
  return out;
}

std::string Narrow(std::wstring_view text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
  std::string out(size_t(size > 0 ? size : 0), '\0');
  if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size, nullptr, nullptr);
  return out;
}

std::string RegistryString(const wchar_t* key, const wchar_t* value) {
  wchar_t buffer[256];
  DWORD bytes = sizeof(buffer);
  if (RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, nullptr, buffer, &bytes) != ERROR_SUCCESS) return {};
  return Narrow(buffer);
}

std::string OperatingSystem() {
  using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOEXW*);
  OSVERSIONINFOEXW info{sizeof(info)};
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (auto get = ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr) get(&info);
  constexpr const wchar_t* kKey = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
  DWORD ubr = 0, bytes = sizeof(ubr);
  RegGetValueW(HKEY_LOCAL_MACHINE, kKey, L"UBR", RRF_RT_REG_DWORD, nullptr, &ubr, &bytes);
  std::string text = fmt::format("{} {} ({}.{}.{}.{})", RegistryString(kKey, L"ProductName"),
                                 RegistryString(kKey, L"DisplayVersion"), info.dwMajorVersion, info.dwMinorVersion,
                                 info.dwBuildNumber, ubr);
  // Wine and Proton report a Windows version too; say what is really underneath.
  using WineVersionFn = const char*(__cdecl*)();
  using WineHostFn = void(__cdecl*)(const char**, const char**);
  if (auto wine = ntdll ? reinterpret_cast<WineVersionFn>(GetProcAddress(ntdll, "wine_get_version")) : nullptr) {
    text += fmt::format(", Wine {}", wine());
    if (auto host = reinterpret_cast<WineHostFn>(GetProcAddress(ntdll, "wine_get_host_version"))) {
      const char *system = nullptr, *release = nullptr;
      host(&system, &release);
      text += fmt::format(" on {} {}", system ? system : "?", release ? release : "?");
    }
  }
  return text;
}

std::string Processor() {
  int regs[4] = {};
  char brand[49] = {};
  __cpuid(regs, int(0x80000000));
  if (unsigned(regs[0]) >= 0x80000004u) {
    for (int leaf = 0; leaf < 3; ++leaf) {
      __cpuid(regs, int(0x80000002 + leaf));
      std::memcpy(brand + leaf * 16, regs, 16);
    }
  }
  std::string name(brand);
  name.erase(0, name.find_first_not_of(' '));
  SYSTEM_INFO system{};
  GetNativeSystemInfo(&system);
  MEMORYSTATUSEX memory{sizeof(memory)};
  GlobalMemoryStatusEx(&memory);
  return fmt::format("{}, {} logical processors, {:.1f} GB RAM", name.empty() ? "unknown CPU" : name,
                     system.dwNumberOfProcessors, double(memory.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0));
}

void LogAdapters() {
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    REXLOG_WARN("  GPU: DXGI factory creation failed");
    return;
  }
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  for (UINT index = 0; factory->EnumAdapters1(index, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++index) {
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc))) continue;
    std::string driver = "unknown";
    LARGE_INTEGER umd{};
    if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd)))
      driver = fmt::format("{}.{}.{}.{}", HIWORD(umd.HighPart), LOWORD(umd.HighPart), HIWORD(umd.LowPart), LOWORD(umd.LowPart));
    REXLOG_INFO("  GPU {}: {} (vendor {:04x}, device {:04x}), {} MB dedicated VRAM, driver {}{}", index,
                Narrow(desc.Description), desc.VendorId, desc.DeviceId, desc.DedicatedVideoMemory / (1024 * 1024), driver,
                (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? " [software]" : "");
  }
}

void LogDisplays() {
  EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM) -> BOOL {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return TRUE;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode)) return TRUE;
    REXLOG_INFO("  Display {}: {}x{} @ {} Hz{}", Narrow(info.szDevice), mode.dmPelsWidth, mode.dmPelsHeight,
                mode.dmDisplayFrequency, (info.dwFlags & MONITORINFOF_PRIMARY) ? " (primary)" : "");
    return TRUE;
  }, 0);
}

void LogModule(const wchar_t* name) {
  HMODULE module = GetModuleHandleW(name);
  if (!module) {
    REXLOG_INFO("  {}: not loaded", Narrow(name));
    return;
  }
  wchar_t path[kPathChars];
  const DWORD length = GetModuleFileNameW(module, path, DWORD(std::size(path)));
  const std::string identity = edf::native::NativeModuleIdentity(name);
  REXLOG_INFO("  {}: {} (version|bytes {})", Narrow(name), Narrow(std::wstring_view(path, length)), identity);
}
}  // namespace

void LogStartupReport(const std::filesystem::path& user_data_root, const std::filesystem::path& config_path) {
  REXLOG_INFO("==== EDF2027 {} ====", edf::version::Full());
  REXLOG_INFO("  Executable: {}", Narrow((ExecutableFolder() / "edf2027.exe").wstring()));
  REXLOG_INFO("  OS: {}", OperatingSystem());
  REXLOG_INFO("  CPU: {}", Processor());
  LogAdapters();
  LogDisplays();
  REXLOG_INFO("  User data: {}", user_data_root.string());
  REXLOG_INFO("  Config: {}{}", config_path.string(), std::filesystem::exists(config_path) ? "" : " (not written yet)");
  REXLOG_INFO("  Log: {}", LogPath().string());
  // The settings a report needs, as the game will use them (config and command line applied).
  std::string settings;
  for (const char* name : {"edf_display_mode", "window_width", "window_height", "edf_aspect", "edf_native_vsync",
                           "edf_low_latency", "edf_fps_cap", "edf_native_unlock_framerate", "edf_native_renderer",
                           "edf_native_backend", "edf_native_scene_backend", "edf_native_render_width",
                           "edf_native_render_height", "edf_native_msaa", "edf_native_fsr", "edf_native_fsr_sharpness",
                           "edf_native_anisotropic_filtering", "edf_present_filter", "edf_kbm", "edf_native_cache_dir",
                           "log_level"}) {
    if (!settings.empty()) settings += ", ";
    settings += fmt::format("{}={}", name, rex::cvar::GetFlagByName(name));
  }
  REXLOG_INFO("  Settings: {}", settings);
  LogModule(L"rexruntime.dll");
  LogModule(L"d3dcompiler_47.dll");
  LogModule(L"amd_fidelityfx_dx12.dll");
  REXLOG_INFO("  FidelityFX: {}", edf::native::NativeFsrLibrary().Describe());
  REXLOG_INFO("==== end of startup report ====");
}

void RunCrashTest() {
  switch (REXCVAR_GET(edf_crash_test)) {
    case 1: {
      REXLOG_WARN("edf_crash_test=1: access violation");
      volatile int* nowhere = nullptr;
      *nowhere = 2027;
      break;
    }
    case 2: {
      REXLOG_WARN("edf_crash_test=2: uncaught exception on a thread");
      std::thread([] { throw std::runtime_error("edf_crash_test: an exception nobody catches"); }).detach();
      Sleep(INFINITE);
      break;
    }
    case 3:
      REXLOG_WARN("edf_crash_test=3: abort()");
      std::abort();
    default: break;
  }
}

void ReportFatalError(const std::string& title, const std::string& message) {
  REXLOG_ERROR("{}: {}", title, message);
  rex::FlushLogging();
  static std::mutex once;
  std::lock_guard lock(once);
  DumpJob job;
  job.thread_id = GetCurrentThreadId();
  std::snprintf(job.summary, sizeof(job.summary), "EDF2027 %s: %s: %s", edf::version::Full(), title.c_str(), message.c_str());
  job.log = false;  // already logged, from this thread
  WriteReport(job, L"error");
  const std::wstring body = Widen(message);
  wchar_t text[6144];
  std::swprintf(text, std::size(text),
                L"%ls\n\nLog:\n%ls\n%ls%ls\n\nPlease attach the log to your bug report.\n\nOpen the folder now?",
                body.c_str(), g_log_path[0] ? g_log_path : L"(no log file)",
                job.dump_written ? L"\nDiagnostic dump:\n" : L"", job.dump_written ? job.dump_path : L"");
  ShowReportBox(Widen(title).c_str(), text, g_log_path[0] ? g_log_path : nullptr);
}

}  // namespace edf::diag

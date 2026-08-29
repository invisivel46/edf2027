// EDF 2017 PC - ISO extraction via the bundled extract-xiso (BSD) run as a subprocess (SDL3 process API).
#pragma once
#include <SDL3/SDL.h>
#include <rex/logging.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace edf {

class IsoExtractor {
 public:
  enum class State { kIdle, kListing, kExtracting, kDone, kFailed, kCancelled };

  ~IsoExtractor() { Cancel(); if (thread_.joinable()) thread_.join(); }

  static std::filesystem::path ToolPath() {
    const char* base = SDL_GetBasePath();
    std::filesystem::path dir = base ? std::filesystem::path(base) : std::filesystem::current_path();
#if defined(_WIN32)
    return dir / "extract-xiso.exe";
#else
    return dir / "extract-xiso";
#endif
  }

  void Start(std::filesystem::path iso, std::filesystem::path dest) {
    if (thread_.joinable()) thread_.join();
    iso_ = std::move(iso); dest_ = std::move(dest);
    cancel_ = false; progress_ = 0.f; done_bytes_ = 0; total_bytes_ = 0; state_ = State::kListing; error_.clear();
    thread_ = std::thread([this] { Run(); });
  }
  void Cancel() { cancel_ = true; }
  void Reset() { if (thread_.joinable()) thread_.join(); state_ = State::kIdle; }

  State state() const { return state_; }
  float progress() const { return progress_; }
  uint64_t done_bytes() const { return done_bytes_; }
  uint64_t total_bytes() const { return total_bytes_; }
  std::string error() const { std::lock_guard<std::mutex> lk(m_); return error_; }
  const std::filesystem::path& dest() const { return dest_; }

 private:
  // GUI apps have no console std handles; SDL_CreateProcess() would try to inherit
  // them and fail (DuplicateHandle), so set stdio explicitly.
  static SDL_Process* Spawn(const std::vector<std::string>& argv, bool capture_stdout) {
    std::vector<const char*> args; for (auto& a : argv) args.push_back(a.c_str()); args.push_back(nullptr);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)args.data());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, capture_stdout ? SDL_PROCESS_STDIO_APP : SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_Process* p = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    return p;
  }

  static std::string RunAndCapture(const std::vector<std::string>& argv, int* exit_code) {
    SDL_Process* p = Spawn(argv, true);
    if (!p) { *exit_code = -1; return std::string("cannot start ") + argv[0] + ": " + SDL_GetError(); }
    size_t n = 0; void* out = SDL_ReadProcess(p, &n, exit_code);
    std::string s = out ? std::string((const char*)out, n) : std::string();
    if (out) SDL_free(out);
    SDL_DestroyProcess(p);
    return s;
  }

  static uint64_t DirSize(const std::filesystem::path& d) {
    uint64_t total = 0; std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(d, ec); !ec && it != std::filesystem::end(it); it.increment(ec))
      if (it->is_regular_file(ec)) total += it->file_size(ec);
    return total;
  }

  void Fail(std::string msg) { REXLOG_ERROR("IsoExtractor: {}", msg); { std::lock_guard<std::mutex> lk(m_); error_ = std::move(msg); } state_ = State::kFailed; }

  void Run() {
    const std::string tool = ToolPath().string();
    if (!std::filesystem::exists(tool)) return Fail("extract-xiso not found next to the executable: " + tool);
    // 1) listing: sum "(N bytes)" to know the total.
    int code = 0;
    std::string listing = RunAndCapture({tool, "-l", iso_.string()}, &code);
    uint64_t total = 0; size_t pos = 0;
    while ((pos = listing.find(" bytes)", pos)) != std::string::npos) {
      size_t b = listing.rfind('(', pos); if (b != std::string::npos) total += strtoull(listing.c_str() + b + 1, nullptr, 10);
      pos += 7;
    }
    if (total == 0) {
      std::string tail = listing.size() > 200 ? listing.substr(listing.size() - 200) : listing;
      return Fail("not a readable Xbox 360 disc image (extract-xiso -l exit " + std::to_string(code) + "): " + tail);
    }
    total_bytes_ = total; state_ = State::kExtracting;
    std::error_code ec; std::filesystem::create_directories(dest_, ec);
    // 2) extraction, polled for progress by destination size.
    std::vector<std::string> argv{tool, "-d", dest_.string(), "-x", iso_.string()};
    SDL_Process* p = Spawn(argv, false);
    if (!p) return Fail(std::string("cannot start extract-xiso: ") + SDL_GetError());
    int exit_code = 0;
    while (!SDL_WaitProcess(p, false, &exit_code)) {
      if (cancel_) { SDL_KillProcess(p, true); SDL_WaitProcess(p, true, &exit_code); SDL_DestroyProcess(p); state_ = State::kCancelled; return; }
      done_bytes_ = DirSize(dest_); progress_ = (float)((double)done_bytes_ / (double)total_bytes_);
      SDL_Delay(400);
    }
    SDL_DestroyProcess(p);
    done_bytes_ = DirSize(dest_); progress_ = 1.f;
    if (exit_code != 0) return Fail("extract-xiso exited with code " + std::to_string(exit_code));
    std::filesystem::remove_all(dest_ / "$SystemUpdate", ec);  // dashboard update, never needed
    state_ = State::kDone;
  }

  std::filesystem::path iso_, dest_;
  std::thread thread_;
  std::atomic<bool> cancel_{false};
  std::atomic<State> state_{State::kIdle};
  std::atomic<float> progress_{0.f};
  std::atomic<uint64_t> done_bytes_{0}, total_bytes_{0};
  mutable std::mutex m_; std::string error_;
};

}  // namespace edf

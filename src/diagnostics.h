// EDF2027 - bug-report plumbing: where the log goes, the startup report that makes a log
// useful on its own, crash dumps, and the message box for an error the game cannot
// continue from. docs/release.md describes what a player finds where.
#pragma once
#include <filesystem>
#include <string>

namespace edf::diag {

// Unhandled SEH exceptions (access violations, stack overflows, an uncaught C++ exception
// on a thread that lets it escape), std::terminate, abort(), pure virtual calls and CRT
// invalid-parameter reports: each writes <log dir>/edf2027_NNN_crash.dmp (a minidump) and
// edf2027_NNN_crash.txt (what happened, in words), logs a line, and shows a message box
// naming both files and the log before the process exits. Call as early as possible; safe
// to call twice.
void InstallCrashHandler();

// Chooses this run's log file before the SDK opens it: <exe folder>/logs/edf2027_NNN.log,
// or <fallback_root>/logs when the exe folder cannot be written (an install under
// Program Files), and keeps only the newest kKeptRuns runs' logs and dumps there. Sets
// the log_file cvar, which the config never saves (core_logic.h IsTransientConfigLine).
// An explicit --log_file is left alone. Crash dumps go to the same folder.
inline constexpr int kKeptRuns = 10;
void PrepareLogFile(const std::filesystem::path& fallback_root);
std::filesystem::path LogPath();       // this run's log file; empty before PrepareLogFile
std::filesystem::path LogDirectory();  // where the logs and crash dumps are

// Version, OS, CPU, memory, GPUs and drivers, displays, the runtime DLLs actually loaded,
// the folders in use and the settings that matter for a bug report, as one block at the
// top of the log.
void LogStartupReport(const std::filesystem::path& user_data_root, const std::filesystem::path& config_path);

// An error the game cannot continue from: logs it, flushes the log, writes a minidump
// (so a hang or device loss can be looked at like a crash) and shows a message box with
// `message`, the log's path and the dump's. Returns when the box is closed; the caller
// decides how to exit.
void ReportFatalError(const std::string& title, const std::string& message);

// edf_crash_test (development, off by default): crash on purpose, to check the above.
void RunCrashTest();

}  // namespace edf::diag

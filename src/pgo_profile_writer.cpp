// Built only with EDF_PGO=generate (cmake/edf_optimization.cmake).
//
// The profile runtime writes its counters from an atexit handler, which a
// process that ends through ExitProcess/TerminateProcess or a crash never
// reaches. Rewrite the raw profile periodically instead. The file name has no
// %m, so each write replaces the file with the cumulative counters and the
// last write (periodic or at exit) holds the whole session.
//
// The path is the EDF_PGO_RAW_FILE environment variable when set (tools/
// pgo-train.ps1 gives each training scenario its own file), else the
// EDF_PGO_RAW_FILE cache path compiled in. The runtime writes to <path>.tmp,
// and each periodic write is then renamed over <path>, so a process killed
// mid-write (training runs are stopped with TerminateProcess) still leaves the
// previous complete profile at <path>. A clean exit's final write stays at
// <path>.tmp; tools/pgo-train.ps1 takes the newer of the two that
// llvm-profdata accepts.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

extern "C" int __llvm_profile_write_file(void);
extern "C" void __llvm_profile_set_filename(const char*);

namespace {

struct PgoProfileWriter {
  PgoProfileWriter() {
    std::string path = EDF_PGO_RAW_FILE;
    if (const char* value = std::getenv("EDF_PGO_RAW_FILE"); value && *value) {
      path = value;
    }
    int seconds = 30;
    if (const char* value = std::getenv("EDF_PGO_DUMP_SECONDS")) {
      seconds = std::max(5, std::atoi(value));
    }
    const std::string temp = path + ".tmp";
    // The runtime copies the name. Switching names truncates the new file, so
    // the name stays the temporary one for the whole session.
    __llvm_profile_set_filename(temp.c_str());
    std::thread([seconds, path, temp] {
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        if (__llvm_profile_write_file() == 0) {
          MoveFileExA(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
      }
    }).detach();
  }
};

PgoProfileWriter writer;

}  // namespace

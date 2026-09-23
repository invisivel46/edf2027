// Built only with EDF_PGO=generate (cmake/edf_optimization.cmake).
//
// The profile runtime writes its counters from an atexit handler, which a
// process that ends through ExitProcess/TerminateProcess or a crash never
// reaches. Rewrite the raw profile periodically instead. The file name has no
// %m, so each write replaces the file with the cumulative counters and the
// last write (periodic or at exit) holds the whole session.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>

extern "C" int __llvm_profile_write_file(void);
extern "C" void __llvm_profile_set_filename(const char*);

namespace {

struct PgoProfileWriter {
  PgoProfileWriter() {
    __llvm_profile_set_filename(EDF_PGO_RAW_FILE);
    int seconds = 30;
    if (const char* value = std::getenv("EDF_PGO_DUMP_SECONDS")) {
      seconds = std::max(5, std::atoi(value));
    }
    std::thread([seconds] {
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        __llvm_profile_write_file();
      }
    }).detach();
  }
};

PgoProfileWriter writer;

}  // namespace

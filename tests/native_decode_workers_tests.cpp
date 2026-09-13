// The decode workers exist to run guest conversion ahead of the draw that
// needs it. Their whole contract is "this ticket is finished", and getting
// that wrong means a draw binds constants that were never written - which
// looks like one material flickering, not like a synchronisation bug. So it
// is tested here, with contention, and not against a GPU.
#include "native_graphics/native_decode_workers.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {
int failures = 0;
void Check(bool ok, const std::string& message) {
  if (ok) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}
}  // namespace

int main() {
  using edf::native::NativeDecodeWorkers;
  std::cout << std::unitbuf;
  try {
    {
      // Zero workers must still be a working queue, because that is how
      // threading is turned off: the same code path, not a special case.
      NativeDecodeWorkers workers(0);
      int value = 0;
      const auto ticket = workers.Submit([&] { value = 7; });
      workers.Wait(ticket);
      Check(value == 7, "an inline job did not run");
      Check(workers.inline_jobs() == 1, "the inline job was not counted");
      workers.Drain();
    }
    {
      // The property that matters: when Wait returns, that job has finished.
      // Each job writes its own slot and the waiter reads it immediately, so
      // a premature return is a wrong value rather than a silent pass.
      constexpr int kJobs = 4000;
      NativeDecodeWorkers workers(4);
      std::vector<int> written(kJobs, 0);
      std::vector<uint64_t> tickets(kJobs, 0);
      std::mt19937 random(20260913);
      for (int index = 0; index < kJobs; ++index) {
        const int spin = static_cast<int>(random() % 200);
        tickets[index] = workers.Submit([&written, index, spin] {
          volatile int sink = 0;
          for (int i = 0; i < spin; ++i) sink += i;
          written[index] = index + 1;
        });
      }
      int unfinished = 0;
      for (int index = 0; index < kJobs; ++index) {
        workers.Wait(tickets[index]);
        if (written[index] != index + 1) ++unfinished;
      }
      Check(unfinished == 0, "Wait returned before the job had finished, " +
                                 std::to_string(unfinished) + " times");
      workers.Drain();
      Check(workers.submitted() == kJobs, "not every job was counted");
      std::cout << "decode workers: " << kJobs << " jobs, " << workers.waits()
                << " waits that actually blocked\n";
    }
    {
      // Out-of-order completion must not let a later job's finish release an
      // earlier waiter. The first job is slow and every later one is instant,
      // so a naive "highest finished ticket" would report the first as done.
      NativeDecodeWorkers workers(4);
      std::atomic<bool> slow_done{false};
      const auto slow = workers.Submit([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        slow_done.store(true, std::memory_order_release);
      });
      for (int index = 0; index < 64; ++index) workers.Submit([] {});
      workers.Wait(slow);
      Check(slow_done.load(std::memory_order_acquire),
            "waiting on an early job returned once a later one finished");
      workers.Drain();
    }
    {
      // Drain means everything, including work still queued behind a slow job.
      NativeDecodeWorkers workers(2);
      std::atomic<int> done{0};
      for (int index = 0; index < 500; ++index) workers.Submit([&] {
        done.fetch_add(1, std::memory_order_relaxed);
      });
      workers.Drain();
      Check(done.load(std::memory_order_relaxed) == 500,
            "Drain returned with work outstanding: " + std::to_string(done.load()));
    }
    {
      // Destruction with work still queued must not hang or drop the process;
      // it is what happens when the game exits mid-frame.
      NativeDecodeWorkers workers(3);
      for (int index = 0; index < 200; ++index) workers.Submit([] {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
      });
    }
  } catch (const std::exception& error) {
    std::cerr << "unexpected: " << error.what() << '\n';
    return 1;
  }
  std::cout << "native decode worker tests: " << failures << " failures\n";
  return failures ? 1 : 0;
}

// The upload ring's whole job is to never hand out memory a queued frame is
// still reading. That failure cannot be tested against a GPU, because reusing
// live memory usually looks correct - so it is tested here, exhaustively, with
// no device involved.
#include "native_graphics/native_upload_ring.h"
#include <iostream>
#include <algorithm>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int failures = 0;
void Check(bool ok, const std::string& message) {
  if (ok) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}
bool Throws(void (*body)()) {
  try { body(); } catch (const std::runtime_error&) { return true; }
  return false;
}
}  // namespace

int main() {
  using edf::native::NativeUploadRing;
  using Status = NativeUploadRing::Status;
  try {
    {
      NativeUploadRing ring(1024);
      ring.BeginFrame(1);
      const auto first = ring.Allocate(100, 256);
      Check(first && first.offset == 0, "first allocation is not at zero");
      const auto second = ring.Allocate(100, 256);
      Check(second && second.offset == 256, "allocation did not honour 256-byte alignment");
      Check(ring.in_flight_bytes() == 356, "in-flight accounting is wrong");
      Check(ring.Allocate(0, 256).status == Status::Ok, "a zero-byte allocation was refused");
      ring.EndFrame();
    }
    {
      // An allocation larger than the ring must be distinguishable from
      // transient pressure. Reporting Full would send the caller off to wait
      // for a GPU that can never free enough, which is a hang, not a stall.
      NativeUploadRing ring(1024);
      ring.BeginFrame(1);
      Check(ring.Allocate(2048, 256).status == Status::TooLarge,
            "an oversized allocation was reported as merely Full");
      Check(ring.Allocate(1024, 256).status == Status::Ok, "an exactly-capacity allocation was refused");
      Check(ring.Allocate(1, 256).status == Status::Full, "a full ring kept allocating");
      ring.EndFrame();
      // Nothing is free until the GPU says so.
      Check(ring.Allocate(1, 1).status == Status::Full, "the ring freed memory before the fence completed");
      ring.Retire(1);
      ring.BeginFrame(2);
      Check(ring.Allocate(1, 1).status == Status::Ok, "retiring a completed frame did not free its memory");
      ring.EndFrame();
    }
    {
      // A constant buffer view has to be contiguous, so no allocation may
      // straddle the physical end of the buffer.
      NativeUploadRing ring(1024);
      ring.BeginFrame(1);
      Check(ring.Allocate(768, 256).status == Status::Ok, "setup allocation failed");
      ring.EndFrame();
      ring.Retire(1);
      ring.BeginFrame(2);
      const auto wrapped = ring.Allocate(512, 256);
      Check(wrapped.status == Status::Ok, "the ring could not wrap into freed space");
      Check(wrapped.offset % 1024 + 512 <= 1024, "an allocation straddled the end of the buffer");
      ring.EndFrame();
    }
    {
      NativeUploadRing ring(1024);
      ring.BeginFrame(5);
      ring.EndFrame();
      Check(Throws([] { NativeUploadRing r(1024); r.BeginFrame(5); r.EndFrame(); r.BeginFrame(5); }),
            "a repeated fence value was accepted, which would retire live memory");
      Check(Throws([] { NativeUploadRing r(1024); r.BeginFrame(5); r.BeginFrame(6); }),
            "a second frame was opened while one was still open");
      Check(Throws([] { NativeUploadRing r(1024); r.EndFrame(); }), "EndFrame without a frame was accepted");
      Check(Throws([] { NativeUploadRing r(0); }), "a zero-capacity ring was accepted");
      Check(Throws([] { NativeUploadRing r(1024); r.BeginFrame(1); r.Allocate(16, 3); }),
            "a non-power-of-two alignment was accepted");
      // A caller that never retires must fail loudly rather than grow a queue.
      Check(Throws([] {
              NativeUploadRing r(1 << 20);
              for (uint64_t frame = 1; frame <= 32; ++frame) { r.BeginFrame(frame); r.EndFrame(); }
            }),
            "an unbounded number of un-retired frames was accepted");
    }
    {
      // The safety property, checked directly: while a frame is un-retired, no
      // later allocation may overlap any byte it was given. Randomised sizes
      // and retire timings, so it is not just the shapes I thought to write.
      struct Live { uint64_t fence, first, last; };
      constexpr uint64_t kCapacity = 64 * 1024;
      NativeUploadRing ring(kCapacity);
      std::vector<Live> live;
      std::mt19937 random(20260913);
      uint64_t completed = 0, overlaps = 0, fulls = 0;
      // Retiring must be monotonic and must drop this test's own record of the
      // frame at the same moment the ring drops it, or the test accuses the
      // ring of reusing memory it was entitled to reuse.
      const auto retire_to = [&](uint64_t fence) {
        if (fence <= completed) return;
        completed = fence;
        ring.Retire(completed);
        live.erase(std::remove_if(live.begin(), live.end(),
                                  [&](const Live& entry) { return entry.fence <= completed; }),
                   live.end());
      };
      for (uint64_t frame = 1; frame <= 4000; ++frame) {
        ring.BeginFrame(frame);
        const int draws = static_cast<int>(random() % 40);
        for (int draw = 0; draw < draws; ++draw) {
          const uint64_t bytes = 16 + random() % 4096;
          auto allocation = ring.Allocate(bytes, 256);
          if (allocation.status == Status::Full) {
            ++fulls;
            // What a real caller does: wait for the GPU to finish one more
            // frame, retire it, retry. Only an ended frame can be retired.
            if (completed + 1 < frame) retire_to(completed + 1);
            allocation = ring.Allocate(bytes, 256);
          }
          if (!allocation) continue;
          const uint64_t first = allocation.offset, last = allocation.offset + bytes - 1;
          for (const auto& other : live) {
            // Compare in buffer space, which is where a real overwrite would
            // happen; monotonic offsets a whole capacity apart are the same
            // bytes. No allocation straddles the end, so each is one interval.
            const uint64_t a0 = first % kCapacity, a1 = a0 + (last - first);
            const uint64_t b0 = other.first % kCapacity, b1 = b0 + (other.last - other.first);
            if (a0 <= b1 && b0 <= a1) ++overlaps;
          }
          live.push_back({frame, first, last});
        }
        ring.EndFrame();
        // The GPU runs a few frames behind, as it does in practice.
        if (frame > 3) retire_to(frame - 3);
      }
      Check(overlaps == 0, "the ring handed out memory a queued frame was still reading: " +
                               std::to_string(overlaps) + " overlaps");
      Check(fulls > 0, "the randomised run never filled the ring, so back-pressure went untested");
      Check(ring.high_water() <= kCapacity, "high water exceeded the ring capacity");
      Check(ring.allocations() > 10000, "the randomised run was too small to mean anything");
      std::cout << "ring simulation: " << ring.allocations() << " allocations, " << fulls
                << " back-pressure events, high water " << ring.high_water() << " of " << kCapacity << '\n';
    }
  } catch (const std::exception& error) {
    std::cerr << "unexpected: " << error.what() << '\n';
    return 1;
  }
  std::cout << "native upload ring tests: " << failures << " failures\n";
  return failures ? 1 : 0;
}

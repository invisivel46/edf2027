#pragma once
#include "native_render_backend.h"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace edf::native {
// GPU time per full-frame pass (edf_native_gpu_timings), from timestamps the
// scene recorder writes at each pass boundary.
//
// A ring of frames, each owning a fixed range of one timestamp set. A frame
// writes its markers in recorded order, resolves its range at the end, and is
// read back by a later BeginFrame once the GPU has passed that resolve - never
// waited for. When every ring entry is still in flight the frame is skipped
// rather than stalled, and counted, so a skipped count other than zero says the
// ring is too short for the GPU's lag, not that a frame was slow.
//
// Spans are named; a name that occurs more than once in a frame (a view pass
// with two views) adds up within the frame. Two spans are implicit:
//   frame     the first BeginFrame marker to the EndFrame marker;
//   interval  one frame's begin marker to the next frame's, when both were
//             read: the GPU's whole frame period, idle time included, so on a
//             GPU-bound run it is the GPU frame time and on a CPU-bound one it
//             is the present interval.
// Not thread-safe: the caller serializes every call (the bridge mutex).
struct NativeGpuPassTiming {
  std::string name;
  uint64_t frames=0;  // Frames that had this span.
  double total_ms=0,max_ms=0;
  double average_ms() const { return frames?total_ms/double(frames):0; }
};
struct NativeGpuPassTimingWindow {
  uint64_t frames=0;         // Frames read back in this window.
  uint64_t skipped=0;        // Frames not timed because the ring was full.
  uint64_t dropped_spans=0;  // Spans past a frame's slot budget.
  uint64_t invalid_spans=0;  // Spans whose end preceded their begin, or never closed.
  std::vector<NativeGpuPassTiming> passes;  // First-seen order.
};
class NativeGpuPassTimings {
 public:
  struct Options {
    uint32_t ring=8;              // Frames that may be in flight at once.
    uint32_t slots_per_frame=64;  // Two per span, two for the frame itself.
    uint32_t report_frames=600;   // Frames read per reported window.
  };
  NativeGpuPassTimings() : NativeGpuPassTimings(Options{}) {}
  explicit NativeGpuPassTimings(Options options);
  ~NativeGpuPassTimings();
  NativeGpuPassTimings(const NativeGpuPassTimings&)=delete;
  NativeGpuPassTimings& operator=(const NativeGpuPassTimings&)=delete;

  // Reads finished frames, then opens a timed frame with its begin marker on
  // `recorder`. False when the frame is not timed: the backend has no
  // timestamps (supported() turns false) or the ring is full.
  bool BeginFrame(NativeRenderBackend& backend,NativeBackendRecorder& recorder);
  // A named span inside the open frame; returns its handle, or -1 when no
  // frame is open or the frame's slots are used up.
  int BeginSpan(std::string_view name,NativeBackendRecorder& recorder);
  void EndSpan(int span,NativeBackendRecorder& recorder);
  // Closes the frame: its end marker, any span left open (reported invalid),
  // and the resolve of its range. No-op without an open frame.
  void EndFrame(NativeBackendRecorder& recorder);
  // Reads every frame the GPU has finished with, oldest first. Returns how many.
  size_t Poll(NativeRenderBackend& backend);
  // Moves the window into `out` and starts the next once report_frames frames
  // have been read (or, with `force`, any have); false otherwise.
  bool TakeReport(NativeGpuPassTimingWindow& out,bool force=false);

  bool supported() const { return !unsupported_; }
  bool frame_open() const { return open_>=0; }
  uint64_t frequency() const { return frequency_; }
  uint64_t frames_read() const { return frames_read_; }
  uint64_t pending() const;

 private:
  struct Span { uint32_t name=0,begin=0,end=0; bool closed=false; };
  struct Entry {
    enum class State { Free, Recording, Pending } state=State::Free;
    uint64_t sequence=0;
    uint32_t used=0;
    std::vector<Span> spans;
  };
  uint32_t Base(size_t entry) const { return uint32_t(entry)*options_.slots_per_frame; }
  uint32_t Intern(std::string_view name);
  void Read(Entry& entry,const std::vector<uint64_t>& ticks);
  double Milliseconds(uint64_t ticks) const { return double(ticks)*1000.0/double(frequency_); }

  Options options_;
  NativeRenderBackend* backend_=nullptr;
  std::unique_ptr<NativeBackendTimestamps> timestamps_;
  uint64_t frequency_=0;
  bool unsupported_=false;
  std::vector<Entry> entries_;
  int open_=-1;
  uint64_t next_sequence_=0,frames_read_=0;
  // The last frame read, for the interval span.
  uint64_t last_sequence_=UINT64_MAX,last_begin_=0;
  std::vector<std::string> names_;
  uint32_t frame_name_=0,interval_name_=0;
  NativeGpuPassTimingWindow window_;
  std::vector<double> frame_sums_;
  std::vector<uint64_t> ticks_;
};
}  // namespace edf::native

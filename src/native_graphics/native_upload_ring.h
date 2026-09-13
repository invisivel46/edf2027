#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace edf::native {
// Fenced ring allocator for per-frame upload memory.
//
// D3D11 hid this: Map(WRITE_DISCARD) let the driver rename the buffer whenever
// the previous contents were still being read. D3D12 and Vulkan do not rename
// anything, so writing over bytes a queued frame has not finished reading is a
// silent corruption that shows up as one frame of wrong constants - the hardest
// class of bug to catch by looking at a screenshot. This type owns that rule.
//
// The arithmetic is deliberately free of any graphics API so it can be tested
// without a device, because the failure it prevents cannot be tested with one:
// reusing live memory usually looks fine.
//
// Offsets are monotonic (never wrapped) so "is this range still in flight" is a
// subtraction rather than a case analysis on whether head passed tail. The
// caller maps a monotonic offset onto the buffer with offset % capacity.
class NativeUploadRing {
 public:
  enum class Status : uint32_t {
    Ok,        // Allocated; offset is valid.
    Full,      // No room right now. Wait for the GPU, Retire(), and retry.
    TooLarge,  // Larger than the whole ring. Retrying can never help.
  };
  struct Allocation {
    Status status=Status::Full;
    uint64_t offset=0;  // Monotonic; the byte in the buffer is offset % capacity.
    explicit operator bool() const { return status==Status::Ok; }
  };

  explicit NativeUploadRing(uint64_t capacity) : capacity_(capacity) {
    if(!capacity) throw std::runtime_error("upload ring capacity must be non-zero");
  }

  // Alignment must be a power of two; D3D12 wants 256 for constant buffers and
  // 512 for texture upload rows, and Vulkan reports its own minimum.
  Allocation Allocate(uint64_t bytes, uint64_t alignment) {
    if(!alignment || (alignment&(alignment-1))) throw std::runtime_error("upload ring alignment must be a power of two");
    if(!bytes) return {Status::Ok,head_};
    uint64_t start=(head_+alignment-1)&~(alignment-1);
    // A constant buffer view must be contiguous, so an allocation that would
    // straddle the physical end of the buffer is pushed to the start instead
    // and the tail of the buffer is left unused.
    const uint64_t offset_in_buffer=start%capacity_;
    if(offset_in_buffer+bytes>capacity_) start+=capacity_-offset_in_buffer;
    // Compare against capacity before comparing against free space: an
    // allocation bigger than the ring must not look like transient pressure,
    // or the caller waits for a GPU that will never free enough and hangs.
    if(bytes>capacity_) return {Status::TooLarge,0};
    if(start+bytes-tail_>capacity_) { ++full_events_; return {Status::Full,0}; }
    head_=start+bytes;
    const uint64_t live=head_-tail_;
    if(live>high_water_) high_water_=live;
    ++allocations_;
    return {Status::Ok,start};
  }

  // Everything allocated from now until EndFrame belongs to this fence value.
  // Fence values must increase; a repeated or lower value would retire memory
  // the GPU is still reading.
  void BeginFrame(uint64_t fence_value) {
    if(open_) throw std::runtime_error("upload ring frame already open");
    if(frames_.full()) throw std::runtime_error("upload ring has more frames in flight than it can track");
    if(fence_value<=last_fence_) throw std::runtime_error("upload ring fence values must increase");
    last_fence_=fence_value;
    open_=true;
    open_fence_=fence_value;
  }
  void EndFrame() {
    if(!open_) throw std::runtime_error("upload ring has no open frame");
    open_=false;
    frames_.push({open_fence_,head_});
  }

  // Free every frame the GPU has finished. Called with the fence value the GPU
  // has actually reached, never with the value that was merely submitted.
  void Retire(uint64_t completed_fence) {
    while(!frames_.empty() && frames_.front().fence<=completed_fence) {
      tail_=frames_.front().head;
      frames_.pop();
    }
  }

  uint64_t capacity() const { return capacity_; }
  uint64_t in_flight_bytes() const { return head_-tail_; }
  // Diagnostics, in the same spirit as the rest of this renderer: a ring that
  // is too small shows up as full_events, not as a frame-rate mystery.
  uint64_t high_water() const { return high_water_; }
  uint64_t full_events() const { return full_events_; }
  uint64_t allocations() const { return allocations_; }
  uint32_t frames_in_flight() const { return frames_.size(); }

 private:
  struct Frame { uint64_t fence=0,head=0; };
  // Fixed capacity: the number of frames the GPU may be behind is bounded by
  // design, and an unbounded queue here would hide a missing Retire as growth
  // rather than as a loud failure.
  static constexpr uint32_t kMaxFrames=16;
  class FrameQueue {
   public:
    bool empty() const { return count_==0; }
    bool full() const { return count_==kMaxFrames; }
    uint32_t size() const { return count_; }
    const Frame& front() const { return entries_[first_]; }
    void push(const Frame& frame) { entries_[(first_+count_)%kMaxFrames]=frame; ++count_; }
    void pop() { first_=(first_+1)%kMaxFrames; --count_; }
   private:
    Frame entries_[kMaxFrames]{};
    uint32_t first_=0,count_=0;
  };

  uint64_t capacity_,head_=0,tail_=0,high_water_=0,full_events_=0,allocations_=0;
  uint64_t last_fence_=0,open_fence_=0;
  bool open_=false;
  FrameQueue frames_;
};
}  // namespace edf::native

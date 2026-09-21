#pragma once
#include <d3d11.h>
#include "native_render_backend.h"
#include <wrl/client.h>
#include <cstdint>
#include <deque>
#include <vector>
#include <optional>
#include <stdexcept>

namespace edf::native {
struct NativeSignal {
  uint32_t callback,argument,cpu_mask;
  bool operator==(const NativeSignal&) const = default;
};
// CPU-only, single guest publication slot. External delivery lock serializes
// all operations; never acquire the renderer lock from a Deliver callback.
class NativeSignalDelivery {
 public:
  void BeginSubmission() {
    if(depth_==SIZE_MAX) throw std::runtime_error("native signal submission depth exhausted");
    ++depth_;
  }
  void EndSubmission() {
    if(!depth_) throw std::runtime_error("native signal submission depth underflow");
    --depth_;
  }
  bool submitting() const { return depth_!=0; }
  bool pending() const { return signal_.has_value(); }
  void Enqueue(NativeSignal signal) {
    if(signal_ || !signal.callback || !signal.cpu_mask || (signal.cpu_mask&~63u))
      throw std::runtime_error("invalid native CPU signal handoff");
    signal_=signal; remaining_=signal.cpu_mask;
  }
  template<class Wake> bool Deliver(Wake wake) {
    if(depth_ || !signal_) return false;
    for(uint32_t cpu=0;cpu<6;++cpu) if(remaining_&(1u<<cpu)) {
      wake(*signal_,cpu);
      remaining_&=~(1u<<cpu); // Completed CPUs are never repeated after a later failure.
    }
    signal_.reset(); return true;
  }
 private:
  size_t depth_=0;
  std::optional<NativeSignal> signal_;
  uint32_t remaining_=0;
};
inline uint32_t NativeSignalCpuMask(uint32_t flags) {
  const auto mask=(flags>>24)&63;
  return mask?mask:4;
}
// Same address normalization used by the recompiled command submission callers.
// This is boundary address translation, not GPU packet processing.
inline uint32_t NativeSignalCommandAddress(uint32_t address) {
  return (address&0x1fffffffu)+(((address>>20)+512)&0x1000u);
}
// Capture is not submission. Associate signal metadata with its command byte
// range; arm a D3D event only when the enclosing submission covers that range.
// Caller serializes immediate-context access and dispatches returned signals
// outside that lock. No guest writes, callbacks, or packet interpretation here.
class NativeSignalQueue {
 public:
  NativeSignalQueue(ID3D11Device& device,ID3D11DeviceContext& context,size_t capacity=4096);
  explicit NativeSignalQueue(NativeRenderBackend& backend,size_t capacity=4096);
  NativeSignalQueue(const NativeSignalQueue&)=delete;
  NativeSignalQueue& operator=(const NativeSignalQueue&)=delete;
  void Capture(uint32_t begin,uint32_t bytes,NativeSignal signal);
  size_t SubmitRange(uint32_t begin,uint32_t bytes);
  // A limited poll retains the remainder of completed batches for backpressure.
  // Active waits allow driver flushing; passive observers remain non-flushing.
  // Neither mode submits captured CPU ranges or bypasses publication limits.
  std::vector<NativeSignal> Poll(size_t limit=SIZE_MAX,bool allow_flush=false);
  // Two-phase delivery: retain completed signals until explicitly acknowledged.
  // Callers must serialize selection/acknowledgement and retain this queue's
  // lifetime. Pending includes completed but unacknowledged signals.
  std::vector<NativeSignal> PeekCompleted(size_t limit=SIZE_MAX,bool allow_flush=false);
  void AcknowledgeCompleted(size_t count);
  size_t completed() const { return completed_.size(); }
  size_t pending() const { return count_; }
  size_t unsubmitted() const { return captured_.size(); }
 private:
  struct Captured { uint32_t begin,bytes; NativeSignal signal; };
  struct Submitted { std::vector<NativeSignal> signals; Microsoft::WRL::ComPtr<ID3D11Query> query; std::shared_ptr<NativeBackendCompletion> completion; };
  NativeRenderBackend* backend_=nullptr;
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  size_t capacity_,count_=0;
  std::vector<Captured> captured_;
  std::deque<Submitted> submitted_;
  std::vector<NativeSignal> completed_;
};
}

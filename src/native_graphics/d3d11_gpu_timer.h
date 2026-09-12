#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <deque>
#include <optional>

namespace edf::native {
struct NativeGpuTiming {
  uint64_t tag=0,begin=0,end=0,frequency=0;
  bool reliable=false;
  std::optional<uint64_t> middle;
  std::optional<double> FractionAfterMiddle() const {
    if(!reliable || !frequency || !middle || end<=begin || *middle<begin || *middle>end)
      return std::nullopt;
    return double(end-*middle)/double(end-begin);
  }
  std::optional<double> Milliseconds() const {
    if(!reliable || !frequency || end<begin) return std::nullopt;
    return double(end-begin)*1000.0/double(frequency);
  }
};
// Owns real GPU timestamp/disjoint queries, not guest packets or CPU timings.
// One non-overlapping span per frame or less; caller serializes all immediate
// context access, including cancellation/destruction, with other GPU producers.
class NativeGpuTimer {
 public:
  NativeGpuTimer(ID3D11Device& device,ID3D11DeviceContext& context,size_t capacity=8);
  ~NativeGpuTimer();
  NativeGpuTimer(const NativeGpuTimer&)=delete;
  NativeGpuTimer& operator=(const NativeGpuTimer&)=delete;
  void Begin(uint64_t tag);
  void MarkMiddle();
  void End();
  void Cancel();
  std::optional<NativeGpuTiming> Poll(bool allow_flush=false);
  size_t pending() const { return submitted_.size(); }
  bool active() const { return active_.has_value(); }
 private:
  struct Span {
    uint64_t tag=0;
    Microsoft::WRL::ComPtr<ID3D11Query> disjoint,begin,middle,end;
  };
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  size_t capacity_;
  std::optional<Span> active_;
  std::deque<Span> submitted_;
};
// The first pair has no previous end. Later pairs span previous end -> start
// -> end, all within one disjoint interval. Tags are the guest producer index.
class NativePresentProfiler {
 public:
  NativePresentProfiler(ID3D11Device& device,ID3D11DeviceContext& context)
      :timer_(device,context,4) {}
  void Start(uint32_t sequence);
  void Finish(uint32_t sequence);
  std::optional<NativeGpuTiming> Poll() { return timer_.Poll(true); }
 private:
  NativeGpuTimer timer_;
  std::optional<uint32_t> expected_;
  bool started_=false;
};
}

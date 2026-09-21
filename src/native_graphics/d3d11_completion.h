#pragma once
#include "guest_fence.h"
#include "native_render_backend.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <deque>
#include <optional>
#include <cstdint>
#include <chrono>
#include <stop_token>

namespace edf::native {
// Serialized immediate-context use only. Values belong to one device lifetime;
// recreate this queue on guest device initialization/reset. No guest writes.
class NativeCompletionQueue {
 public:
  NativeCompletionQueue(ID3D11Device& device,ID3D11DeviceContext& context,size_t capacity=4096);
  explicit NativeCompletionQueue(NativeRenderBackend& backend,size_t capacity=4096);
  NativeCompletionQueue(const NativeCompletionQueue&)=delete;
  NativeCompletionQueue& operator=(const NativeCompletionQueue&)=delete;
  void Submit(uint32_t value,uint32_t cursor=0);
  void Capture(uint32_t begin,uint32_t bytes,uint32_t value,uint32_t cursor);
  size_t SubmitRange(uint32_t begin,uint32_t bytes);
  // Passive observers avoid driver flushes; an active waiter must allow them
  // so completion does not depend on another render/present call progressing.
  std::optional<uint32_t> Poll(bool allow_flush=false);
  NativeWaitResult WaitUntil(uint32_t issued,uint32_t target,
      std::chrono::steady_clock::time_point deadline,std::stop_token stop={});
  std::optional<uint32_t> completed() const { return completed_; }
  std::optional<uint32_t> completed_cursor() const { return completed_cursor_; }
  size_t pending() const { return entries_.size(); }
  size_t unsubmitted() const;
 private:
  struct Entry { uint32_t value,cursor; Microsoft::WRL::ComPtr<ID3D11Query> query; uint32_t begin=0,bytes=0; std::shared_ptr<NativeBackendCompletion> completion;
    bool armed() const { return bool(query) || bool(completion); } };
  NativeRenderBackend* backend_=nullptr;
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  size_t capacity_;
  std::deque<Entry> entries_;
  std::optional<uint32_t> issued_,submitted_,completed_;
  std::optional<uint32_t> completed_cursor_;
};
}  // namespace edf::native

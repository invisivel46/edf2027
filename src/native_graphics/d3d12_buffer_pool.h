#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <memory>

namespace edf::native {
// Placed buffers suballocated from a few large heaps, instead of one committed
// resource (one kernel allocation, one page-table update) per buffer.
//
// Measured on an RTX 4070 Ti SUPER, 415 buffers totalling 10.5 MB - the static
// world a mission publishes in its first frame: CreateCommittedResource took
// 66-100 ms for them (~170 us each, and ~80 us each again to release), while
// CreatePlacedResource into an existing heap took 2.2-2.7 ms (0.5 ms to
// release). That burst was the whole of the mission-start frame's 78 ms.
//
// Placement changes where a buffer lives, not what it holds or how it is used:
// each placed buffer is still its own resource with its own state, created in
// COMMON exactly as a committed one is, and it is only ever handed out for a
// buffer whose creation writes every byte of it (the backend's rule), so the
// memory's previous contents are never observable. Buffers are aligned to
// 64 KB inside a heap, which is exactly the granularity a committed buffer
// already occupies, so this costs no memory a committed buffer would not.
//
// Lifetime: the backend retires a placed buffer on the frame fence like any
// other resource (NativeD3D12Device::Retire). Its range goes back to the pool
// only when the resource itself is destroyed - the pool attaches an object to
// it as private data, and D3D12 releases private data when the resource dies -
// so a range is never reused while a queued command list can still read it.
class NativeD3D12BufferPool {
 public:
  // heap_bytes: size of each heap. max_buffer_bytes: larger buffers are not
  // pooled. reserve_heaps: heaps created now, at construction, so the first
  // burst does not pay for a heap either; they are also never released.
  NativeD3D12BufferPool(ID3D12Device& device,uint64_t heap_bytes,uint64_t max_buffer_bytes,uint32_t reserve_heaps);
  ~NativeD3D12BufferPool();
  NativeD3D12BufferPool(const NativeD3D12BufferPool&)=delete;
  NativeD3D12BufferPool& operator=(const NativeD3D12BufferPool&)=delete;

  // A placed buffer of `bytes` in D3D12_RESOURCE_STATE_COMMON, or null when it
  // is too large to pool or the device refused a heap; the caller then creates
  // a committed buffer as before.
  Microsoft::WRL::ComPtr<ID3D12Resource> Create(uint64_t bytes);

  struct Statistics {
    uint64_t placed=0,placed_bytes=0;  // Buffers handed out, ever.
    uint64_t live=0,live_bytes=0;      // Ranges currently held (64 KB granularity).
    uint64_t heaps=0,heap_bytes=0;     // Heaps currently held.
    uint64_t heaps_created=0,heaps_released=0;
    uint64_t refused=0;                // Create calls answered with null.
  };
  Statistics statistics() const;

  static constexpr uint64_t kAlignment=D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

  // Shared with each placed buffer's allocation record, which may outlive the
  // pool object itself by the frames a retired buffer waits for its fence.
  struct State;

 private:
  std::shared_ptr<State> state_;
};
}  // namespace edf::native

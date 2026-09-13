#pragma once
#include "native_upload_ring.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace edf::native {
// Shader-visible descriptors, which D3D11 did not have in any form the caller
// could see. Two kinds, because the hardware limits force two different
// strategies, and mixing them up is the sort of thing that works until the
// scene gets busy:
//
//   Views (textures) are ring-allocated per frame, on exactly the same fenced
//   rule as upload memory - the GPU must be finished with a descriptor before
//   its slot is reused, and the ring already owns that rule and is tested.
//
//   Samplers cannot be. A shader-visible sampler heap holds at most 2,048
//   descriptors, and one frame of this game wants 2,370 draws x 7 = 16,590.
//   So sampler tables are cached by combination and live for the run.
class NativeD3D12DescriptorRing {
 public:
  NativeD3D12DescriptorRing(ID3D12Device& device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t descriptors);

  struct Table {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};  // Where to write the descriptors.
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};  // What to hand the root signature.
  };
  // Contiguous, because a descriptor table has to be. Returns the ring's status
  // rather than throwing: only the device knows how to wait for the GPU, so the
  // decision to stall belongs there and not here.
  struct Result { NativeUploadRing::Status status=NativeUploadRing::Status::Full; Table table; };
  Result TryAllocate(uint32_t count);

  void BeginFrame(uint64_t fence) { ring_.BeginFrame(fence); }
  void EndFrame() { ring_.EndFrame(); }
  void Retire(uint64_t completed) { ring_.Retire(completed); }

  ID3D12DescriptorHeap* heap() const { return heap_.Get(); }
  const NativeUploadRing& ring() const { return ring_; }
  uint32_t increment() const { return increment_; }

 private:
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
  NativeUploadRing ring_;
  D3D12_CPU_DESCRIPTOR_HANDLE cpu_start_{};
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_start_{};
  uint32_t increment_=0,capacity_=0;
};

// One contiguous run of sampler descriptors per distinct combination a shader
// asks for, kept for the life of the run. The hit path is a map lookup, which
// is far cheaper than writing seven descriptors per draw would have been, and
// it is the only option that fits the 2,048-descriptor limit.
class NativeD3D12SamplerCache {
 public:
  // slots_per_table is the root signature's sampler table width; the whole
  // width is reserved per combination so the table can be bound as one handle.
  NativeD3D12SamplerCache(ID3D12Device& device, uint32_t slots_per_table, uint32_t max_tables);

  // Throws when the heap is exhausted, naming how many combinations fit. A
  // silent fallback would bind the wrong filtering rather than report a limit,
  // and the symptom - one surface filtered wrongly - is nearly unfindable.
  D3D12_GPU_DESCRIPTOR_HANDLE Table(std::span<const D3D12_SAMPLER_DESC> samplers);

  ID3D12DescriptorHeap* heap() const { return heap_.Get(); }
  uint32_t tables() const { return static_cast<uint32_t>(tables_.size()); }
  uint32_t capacity() const { return max_tables_; }
  uint64_t hits() const { return hits_; }
  uint64_t misses() const { return misses_; }

 private:
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
  ID3D12Device* device_=nullptr;
  // Keyed on the raw descriptor bytes: two sampler descriptions that compare
  // equal byte for byte are the same sampler, and nothing else is.
  std::map<std::string,D3D12_GPU_DESCRIPTOR_HANDLE> tables_;
  D3D12_CPU_DESCRIPTOR_HANDLE cpu_start_{};
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_start_{};
  uint32_t increment_=0,slots_per_table_=0,max_tables_=0;
  uint64_t hits_=0,misses_=0;
};
}  // namespace edf::native

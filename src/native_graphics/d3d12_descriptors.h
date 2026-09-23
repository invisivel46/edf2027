#pragma once
#include <mutex>
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
  // A window onto an existing heap, so several recorders can each own a slice
  // of one heap and allocate from it without a lock. Sharing a heap matters:
  // a command list binds whole heaps, and one per recorder would mean every
  // list re-binding them.
  NativeD3D12DescriptorRing(ID3D12Device& device, ID3D12DescriptorHeap& heap,
                            D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t first, uint32_t count);

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
  uint32_t increment_=0,capacity_=0,first_=0;
};

// CPU-side descriptors that live as long as the resource they describe. Every
// texture needs one, because a shader-visible table is filled by copying from
// somewhere, and D3D11's "the view is the object" model has no equivalent.
// Freed slots are reused, so a run that creates and destroys textures does not
// grow the heap forever.
class NativeD3D12CpuDescriptorHeap {
 public:
  NativeD3D12CpuDescriptorHeap(ID3D12Device& device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t descriptors);
  // Throws when exhausted, naming the capacity; silently handing back a slot
  // already in use would make two textures alias.
  D3D12_CPU_DESCRIPTOR_HANDLE Allocate();
  void Free(D3D12_CPU_DESCRIPTOR_HANDLE handle);
  uint32_t live() const { return live_; }
  uint32_t capacity() const { return capacity_; }

 private:
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
  std::vector<uint32_t> free_;
  D3D12_CPU_DESCRIPTOR_HANDLE start_{};
  uint32_t increment_=0,capacity_=0,next_=0,live_=0;
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

  // A table for this combination, reusing one the GPU has finished with when
  // the heap is full.
  //
  // The heap cannot simply be made bigger: 2,048 descriptors is the whole
  // shader-visible sampler heap D3D12 allows, which at eight slots a table is
  // 256 combinations, and a mission uses more than that over its lifetime
  // while using very few at once. So a combination not seen since a frame the
  // GPU has finished gives up its slots. `used` is the frame value to stamp
  // this table with and `completed` what the GPU has reached; a table stamped
  // above `completed` is still being read and is never taken.
  //
  // Throws only when every table is in flight, which is a real shape problem
  // and not a busy moment. A silent fallback would bind the wrong filtering
  // rather than report a limit, and the symptom - one surface filtered wrongly
  // - is nearly unfindable.
  D3D12_GPU_DESCRIPTOR_HANDLE Table(std::span<const D3D12_SAMPLER_DESC> samplers,
                                    uint64_t used,uint64_t completed);

  // Writes a table for a combination an earlier run used, before any draw
  // asks for it, so the first draw that does is a hit. Stamped as never used,
  // so it is the first to give its slots back if the heap fills. Returns false
  // when the combination is already present, is too wide, or would need an
  // eviction (a warm start never displaces a table this run has used).
  bool Prewarm(std::span<const D3D12_SAMPLER_DESC> samplers);
  // Every combination currently held, for the persistent manifest.
  std::vector<std::vector<D3D12_SAMPLER_DESC>> Combinations();
  uint64_t prewarmed() const { return prewarmed_; }

  ID3D12DescriptorHeap* heap() const { return heap_.Get(); }
  uint32_t tables() const { return static_cast<uint32_t>(tables_.size()); }
  uint32_t capacity() const { return max_tables_; }
  uint64_t hits() const { return hits_; }
  uint64_t misses() const { return misses_; }
  // Tables taken back from a combination the GPU had finished with. A number
  // that climbs every frame means the working set really is over capacity and
  // the cache is thrashing, which is worth seeing rather than inferring.
  uint64_t evictions() const { return evictions_; }

 private:
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
  ID3D12Device* device_=nullptr;
  // Keyed on the raw descriptor bytes: two sampler descriptions that compare
  // equal byte for byte are the same sampler, and nothing else is.
  struct Entry {
    D3D12_GPU_DESCRIPTOR_HANDLE table{};
    uint32_t index=0;      // First slot, so an evicted table's slots can be reused.
    uint64_t used=0;       // Frame this was last handed out for.
  };
  std::mutex mutex_;
  std::map<std::string,Entry> tables_;
  D3D12_CPU_DESCRIPTOR_HANDLE cpu_start_{};
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_start_{};
  uint32_t increment_=0,slots_per_table_=0,max_tables_=0;
  uint64_t hits_=0,misses_=0,evictions_=0,prewarmed_=0;
  void Write(std::span<const D3D12_SAMPLER_DESC> samplers,uint32_t index);
};
}  // namespace edf::native

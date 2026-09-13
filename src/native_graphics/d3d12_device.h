#pragma once
#include "d3d12_descriptors.h"
#include "native_upload_ring.h"
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace edf::native {
// Device, queue and frame lifecycle for the D3D12 backend.
//
// This is the part with no D3D11 counterpart. D3D11 gave us a device and an
// immediate context and handled the rest: allocator recycling, buffer renaming,
// knowing when the GPU was finished with something. Here all of that is ours,
// and getting it wrong does not fail - it corrupts one frame in a hundred. So
// the rule is concentrated in this one type rather than spread across callers.
//
// Frames in flight is the only real tuning knob. One means the CPU waits for
// the GPU every frame, which throws away the whole reason for the migration.
// Three is the usual answer and is the default.
struct NativeD3D12Options {
  // WARP is how this gets tested: the suite must run on a build machine with no
  // usable GPU, exactly as the D3D11 tests already do.
  bool prefer_warp=false;
  bool debug_layer=false;
  uint32_t frames_in_flight=3;
  // 16 MB. At the measured 44,917 activations a second that is roughly 750 per
  // frame; the ring reports its high water so this can be set from evidence
  // rather than from this guess.
  uint64_t upload_bytes=16u<<20;
  // ~2,370 draws a frame at 7 textures each is ~16,600 view descriptors per
  // frame; three frames in flight and headroom for the UI and post passes.
  uint32_t view_descriptors=65536;
  // The root signature's sampler table width, and how many distinct
  // combinations the cache may hold. 8 x 192 = 1,536 of the 2,048 descriptors
  // a shader-visible sampler heap is allowed.
  uint32_t sampler_slots=8,sampler_tables=192;
  // How many threads may record at once. One is D3D11's shape and the default;
  // more is the entire reason this backend exists, and each recorder gets its
  // own command allocator, its own slice of the upload ring and its own slice
  // of the descriptor heap, so recording needs no lock.
  uint32_t recorders=1;
};

class NativeD3D12Device {
 public:
  explicit NativeD3D12Device(const NativeD3D12Options& options={});
  ~NativeD3D12Device();
  NativeD3D12Device(const NativeD3D12Device&)=delete;
  NativeD3D12Device& operator=(const NativeD3D12Device&)=delete;

  ID3D12Device* device() const { return device_.Get(); }
  ID3D12CommandQueue* queue() const { return queue_.Get(); }
  IDXGIFactory4* factory() const { return factory_.Get(); }
  // Valid only between BeginFrame and EndFrame.
  ID3D12GraphicsCommandList* commands(uint32_t recorder=0) const { return lists_.at(recorder).Get(); }
  uint32_t recorders() const { return static_cast<uint32_t>(lists_.size()); }
  const std::string& adapter_name() const { return adapter_name_; }
  bool is_warp() const { return is_warp_; }
  // Whether the debug layer is actually validating. Asking for it and not
  // getting it (the Graphics Tools feature is not installed) must not read as
  // success: a missing barrier is silent, and believing it is being checked
  // when it is not is worse than knowing it is not.
  bool debug_layer_active() const { return debug_layer_active_; }
  // Validation messages go to the debugger, where an automated run never looks.
  // Pulling them out is what turns "the debug layer is on" into "a missing
  // barrier fails a test" - which is the only version of consequence 5 that is
  // worth anything, since the corruption itself is invisible on hardware that
  // tolerates it. Returns errors and corruption only; clears what it returns.
  std::vector<std::string> DrainValidationErrors();

  // Waits until the frame slot this frame will reuse is finished on the GPU,
  // recycles its allocator, and frees the upload memory it was holding.
  void BeginFrame();
  // Closes and submits the list, then signals the fence this frame owns.
  void EndFrame();
  // Blocks until the GPU has finished everything submitted so far. For
  // shutdown and for tests; never on a frame path.
  void WaitIdle();

  // A slice of the upload ring, already mapped. The pointer is valid until the
  // frame that allocated it has completed on the GPU - which is the entire
  // contract, and why this returns a fence-bound view rather than a buffer.
  struct Upload {
    uint8_t* cpu=nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu=0;
    ID3D12Resource* resource=nullptr;
    uint64_t offset=0;
  };
  // Throws rather than returning a null slice: every caller would have to
  // handle the failure identically and none could continue meaningfully.
  Upload Allocate(uint64_t bytes, uint64_t alignment=D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
                  uint32_t recorder=0);

  // A contiguous run of shader-visible view descriptors for this frame, on the
  // same fenced rule and with the same stall-and-retry as upload memory.
  NativeD3D12DescriptorRing::Table AllocateViews(uint32_t count, uint32_t recorder=0);
  NativeD3D12DescriptorRing& views(uint32_t recorder=0) { return *views_.at(recorder); }
  ID3D12DescriptorHeap* view_heap() const { return view_heap_.Get(); }
  NativeD3D12SamplerCache& samplers() { return *samplers_; }

  const NativeUploadRing& upload_ring(uint32_t recorder=0) const { return rings_.at(recorder); }
  uint64_t frames_submitted() const { return frame_counter_; }
  // How many times a frame had to stall waiting for upload memory. Zero is the
  // expected reading; anything else means upload_bytes is too small, and it
  // should be visible as a number rather than as an unexplained stutter.
  uint64_t upload_stalls() const { return upload_stalls_; }
  uint64_t descriptor_stalls() const { return descriptor_stalls_; }

 private:
  void WaitForFence(uint64_t value);
  // Waits for the oldest frame still on the GPU. Returns false when nothing is
  // in flight, which is how a caller knows waiting again cannot help.
  bool WaitForOldestFrame();

  struct Frame {
    // One allocator per recorder: two threads writing one allocator is
    // undefined, and it is the first thing that breaks when recording is
    // parallelised.
    std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> allocators;
    uint64_t fence=0;  // Value signalled after this frame's work; 0 = never used.
  };

  Microsoft::WRL::ComPtr<IDXGIFactory4> factory_;
  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
  std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>> lists_;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages_;
  Microsoft::WRL::ComPtr<ID3D12Resource> upload_;
  std::vector<Frame> frames_;
  void* fence_event_=nullptr;
  uint8_t* upload_cpu_=nullptr;
  D3D12_GPU_VIRTUAL_ADDRESS upload_gpu_=0;
  // One ring per recorder over its own region of one upload buffer, so no two
  // threads touch the same allocator state.
  std::vector<NativeUploadRing> rings_;
  std::vector<uint64_t> ring_bases_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> view_heap_;
  std::vector<std::unique_ptr<NativeD3D12DescriptorRing>> views_;
  std::unique_ptr<NativeD3D12SamplerCache> samplers_;
  std::string adapter_name_;
  uint64_t next_fence_=0,frame_counter_=0,upload_stalls_=0,descriptor_stalls_=0;
  uint32_t open_frame_=0;
  bool open_=false,is_warp_=false,debug_layer_active_=false;
};
}  // namespace edf::native

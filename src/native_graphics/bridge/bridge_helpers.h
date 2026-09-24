#pragma once
// What the bridge shares with the full frame (edf/full_frame): the guest memory reader, the active targets,
// the post's guest memory, the output capture, the render helper's thread state, and declarations of the
// bridge functions and thread-locals the full frame calls. Moved from guest_shader_bridge.cpp unchanged, out
// of its anonymous namespaces: the types and templates are here, and the functions and variables are still
// defined in guest_shader_bridge.cpp, where the bridge's own calls stay in one translation unit.
#include "bridge_state.h"
#include "../guest_block.h"
#include "../guest_sdk_readable_range.h"
#include "../native_full_frame_post.h"
#include "../native_scene_cpu_window.h"
#include "../native_scene_pass_inputs.h"
#include "../native_full_frame_effects.h"
#include "../native_full_frame_models.h"
#include "../native_model_pass.h"
#include <rex/memory.h>
#include <rex/system/kernel_state.h>
#include <windows.h>
#include <bit>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace edf::native {
// Only consume ranges the guest mapped and made readable. The source loader
// gives a pointer, not an allocation length; never manufacture a 16 MiB span.
class GuestReader {
 public:
  explicit GuestReader(uint8_t* base) : memory_(REX_KERNEL_MEMORY()) {
    if (!memory_ || memory_->virtual_membase()!=base) throw std::runtime_error("native bridge guest memory mismatch");
  }
  const uint8_t* Bytes(uint32_t address, size_t size) const {
    if (!address || size > 0x100000000ull - address) throw std::runtime_error("invalid guest shader address");
    auto* start = memory_->TranslateVirtual(address);
    if (size && memory_->TranslateVirtual(address+static_cast<uint32_t>(size-1))!=start+size-1)
      throw std::runtime_error("native bridge range crosses noncontiguous guest heaps");
    if(size && REXCVAR_GET(edf_native_guest_heap_reads)) {
      const uint32_t last=address+static_cast<uint32_t>(size-1);
      auto* heap=memory_->LookupHeap(address);
      // QueryRangeAccess alone ignores commitment (Decommit retains protect).
      // The heap's page entries hold both state and access; read per page
      // touched, lock-free, never cached (guest_sdk_readable_range.h).
      // Require containment; retain TranslateVirtual's alias offset.
      // Loader/untracked/cross-heap ranges keep the original OS validation.
      if(heap && heap==memory_->LookupHeap(last) && address>=heap->heap_base() &&
         uint64_t(last)-heap->heap_base()<heap->heap_size()) {
        const bool readable=GuestHeapCommittedReadable(*heap,address,size);
        if(readable) {
          static thread_local bool reported=false;
          if(!reported) {
            reported=true;
            REXLOG_INFO("Native guest read: SDK committed-region validation active, heap={:#x}",heap->heap_base());
          }
          return start;
        }
      }
    }
    auto* cursor = start;
    const auto* end = cursor + size;
    while (cursor < end) {
      MEMORY_BASIC_INFORMATION region{};
      if (!VirtualQuery(cursor, &region, sizeof(region)) || region.State != MEM_COMMIT ||
          (region.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        throw std::runtime_error("unreadable guest shader memory");
      auto* region_end = static_cast<uint8_t*>(region.BaseAddress) + region.RegionSize;
      if (region_end <= cursor) throw std::runtime_error("invalid guest memory extent");
      cursor = region_end < end ? region_end : const_cast<uint8_t*>(end);
    }
    return start;
  }
  uint32_t Word(uint32_t address) const {
    const auto* p = Bytes(address, 4);
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  void StoreWord(uint32_t address,uint32_t value) const {
    const auto* data=WritableBytes(address,4,4);
    // Permission lookup is optimized, not publication semantics.
    InterlockedExchange(reinterpret_cast<volatile LONG*>(const_cast<uint8_t*>(data)),
                        static_cast<LONG>(std::byteswap(value)));
  }
  // Atomically replaces the big-endian word at `address` with `desired` when
  // it holds `expected`; true when it did. The guest word is byte-swapped, as
  // StoreWord's interlocked exchange stores it.
  bool CompareExchangeWord(uint32_t address,uint32_t expected,uint32_t desired) const {
    const auto* data=WritableBytes(address,4,4);
    const auto want=static_cast<LONG>(std::byteswap(expected));
    return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(const_cast<uint8_t*>(data)),
                                      static_cast<LONG>(std::byteswap(desired)),want)==want;
  }
  void StoreByte(uint32_t address,uint8_t value) const {
    const auto aligned=address&~3u;
    auto* data=const_cast<uint8_t*>(WritableBytes(aligned,4,4));
    data[address-aligned]=value;
  }
  template<size_t N>
  void StoreCpuWords(uint32_t address,const std::array<uint32_t,N>& words) const {
    static_assert(N>0);
    // CPU-owned render state, as in the original PPC ordinary stores. Never
    // use this for completion publication, worker signals or shared fences.
    // Validate the whole write before mutation; do not use one interlocked
    // exchange per word for viewport/transform data.
    const auto* data=WritableBytes(address,N*4,4);
    StoreGuestCpuWords(std::span<uint8_t>{const_cast<uint8_t*>(data),N*4},words);
  }
  const uint8_t* WritableBytes(uint32_t address,size_t size,size_t alignment) const {
    if(!size || (alignment!=4 && alignment!=8))
      throw std::runtime_error("invalid native guest write extent/alignment");
    // The SDK writable proof (committed, read and write access) implies the
    // readable one Bytes would check first: one page-table probe, not two.
    // Anything the proof does not cover takes the full path below, which
    // throws as before.
    if(REXCVAR_GET(edf_native_guest_heap_reads) && address && size<=0x100000000ull-address && !(address&(alignment-1))) {
      const auto last=address+uint32_t(size-1);
      auto* heap=memory_->LookupHeap(address);
      auto* start=memory_->TranslateVirtual(address);
      if(heap && heap==memory_->LookupHeap(last) && memory_->TranslateVirtual(last)==start+size-1 &&
         !(reinterpret_cast<uintptr_t>(start)&(alignment-1)) && GuestVirtualHeapCommittedWritable(*heap,address,size))
        return start;
    }
    const auto* data=Bytes(address,size);
    if((address&(alignment-1)) || (reinterpret_cast<uintptr_t>(data)&(alignment-1)))
      throw std::runtime_error("native guest write destination is not aligned");
    bool sdk_writable=false;
    if(REXCVAR_GET(edf_native_guest_heap_reads)) {
      auto* heap=memory_->LookupHeap(address);
      sdk_writable=heap && heap==memory_->LookupHeap(address+uint32_t(size-1)) &&
        GuestVirtualHeapCommittedWritable(*heap,address,size);
    }
    const auto* cursor=sdk_writable?data+size:data;
    const auto* end=data+size;
    while(cursor<end) {
      MEMORY_BASIC_INFORMATION region{};
      if(!VirtualQuery(cursor,&region,sizeof(region)) || region.State!=MEM_COMMIT ||
         (region.Protect&PAGE_GUARD) ||
         !(region.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))
        throw std::runtime_error("native guest write destination is not writable");
      const auto* region_end=static_cast<const uint8_t*>(region.BaseAddress)+region.RegionSize;
      if(region_end<=cursor) throw std::runtime_error("invalid native CPU state extent");
      cursor=(std::min)(region_end,end);
    }
    return data;
  }
  uint64_t DoubleWord(uint32_t address) const {
    uint64_t value;
    std::memcpy(&value,Bytes(address,8),8);
    return std::byteswap(value);
  }
  void StoreDoubleWord(uint32_t address,uint64_t value) const {
    const auto* data=WritableBytes(address,8,8);
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(const_cast<uint8_t*>(data)),
                          static_cast<LONG64>(std::byteswap(value)));
  }
  uint32_t Add(uint32_t address, uint32_t offset) const {
    if (offset > UINT32_MAX - address) throw std::runtime_error("guest shader reference overflow");
    return address + offset;
  }
  std::string String(uint32_t address, size_t limit) const {
    std::string value;
    // Query at page boundaries, not once per character.
    while (value.size() < limit) {
      const auto at = Add(address, static_cast<uint32_t>(value.size()));
      const size_t count = (std::min)(limit - value.size(), size_t(4096 - (at & 4095)));
      const auto* p = Bytes(at, count);
      const auto* zero = static_cast<const uint8_t*>(std::memchr(p, 0, count));
      value.append(reinterpret_cast<const char*>(p), zero ? size_t(zero - p) : count);
      if (zero) return value;
    }
    throw std::runtime_error("unterminated guest shader string");
  }
 private:
  rex::memory::Memory* memory_;
};
// The targets a draw goes into, as backend handles, plus the formats a
// pipeline has to declare to match them.
//
// Mirrors BindActiveTarget below, which answers the same question for the
// context. The two must agree: a pipeline built for one set of formats and
// bound while another is set does not draw, it fails to create or draws
// nothing, and neither says why.
struct ActiveTargets {
  std::array<edf::native::NativeBackendRenderTarget*,8> colors{};
  uint32_t count=0;
  edf::native::NativeBackendRenderTarget* depth=nullptr;
  std::array<uint32_t,8> rtv_format{};
  uint32_t dsv_format=0,samples=1;
  uint32_t width=0,height=0;
  bool operator==(const ActiveTargets&) const=default;
};
// Defined in guest_shader_bridge.cpp, as are the other functions declared here.
ActiveTargets ActiveTargetsLocked(Bridge& state);
void BindActiveTarget(Bridge& state);
// Defined below, next to the selection it mirrors; declared here because
// the texture hook, further up, is the first thing to create a scene resource.
edf::native::NativeRenderBackend& EnsureSceneBackendLocked(Bridge& state);
// The scene backend's recorder, with a frame open.
edf::native::NativeBackendRecorder& SceneRecorderLocked(Bridge& state);
// Records a copy of a finished frame into the scene's shared surface. Does
// nothing when the scene is on D3D11, whose frames reach the window through
// the compositor, or when the backend cannot share.
void PublishSceneSharedLocked(Bridge& state,const NativeRenderTarget& output,
                             NativeFrameKind kind=NativeFrameKind::PartialScene);
// A scene's ordinary output as a BMP, by whichever route its backend allows.
// The D3D11 one reads the surface directly; anything else has to close the
// open frame first, because the readback waits for the GPU and waiting on work
// that has not been submitted never returns.
template <typename Scene>
std::vector<uint8_t> CaptureOutputBmp(Bridge& state,Scene& scene);
// Closes the frame if one is open. Safe to call when none is.
void SubmitSceneFrameLocked(Bridge& state);
template <typename Scene>
std::vector<uint8_t> CaptureOutputBmp(Bridge& state,Scene& scene) {
  if(scene.output.surface)
    return edf::native::CaptureNativeHdrBmp(*state.context.Get(),*scene.output.surface.Get());
  if(!scene.output.backend_surface)
    throw std::runtime_error("this scene has no output surface to capture");
  SubmitSceneFrameLocked(state);
  return edf::native::CaptureNativeBmp(EnsureSceneBackendLocked(state),
                                       *scene.output.backend_surface,scene.output.format);
}
// The scene pass inputs of this thread's render helper call and its FSR jitter (defined in
// guest_shader_bridge.cpp), and the camera the effect activations draw with (inline: every pass calls it).
// constinit (here and on the definitions): a constant-initialized, trivially destructible thread-local is read
// from another file without the TLS guard check an extern thread_local otherwise costs on every access.
extern thread_local std::shared_ptr<const NativeScenePublication> native_scene_publication;
extern thread_local constinit std::optional<NativeScenePassCamera> native_scene_pass_camera;
extern thread_local std::shared_ptr<const NativeScenePassCameras> native_scene_pass_cameras;
extern thread_local constinit std::optional<NativeScenePassAnimation> native_scene_pass_animation;
extern thread_local std::shared_ptr<const NativeSceneAdapter::WorldAnimations> native_scene_pass_animations;
extern thread_local constinit uint32_t native_scene_animation_owner;
extern thread_local constinit std::optional<NativeScenePassCamera> native_scene_draw_camera;
extern thread_local constinit NativeFsrJitter native_scene_view_jitter;
inline const NativeScenePassCamera& NativeSceneDrawCamera() {
  return native_scene_draw_camera?*native_scene_draw_camera:*native_scene_pass_camera;
}
bool ArmNativeFsrFrameLocked(Bridge& state,uint32_t renderer,NativeScene& scene,uint64_t helper_frame);
void DisarmNativeFsrLocked(Bridge& state);
class GuestPostMemory final:public PostGuestMemory {
 public:
  explicit GuestPostMemory(const GuestReader& reader):reader_(reader) {}
  const uint8_t* Bytes(uint32_t address,size_t size) const override { return reader_.Bytes(address,size); }
 private:
  const GuestReader& reader_;
};
// The model pass caches (defined with RenderNativeModelPass), shared with the
// full frame's Models and Sky passes.
std::shared_ptr<const NativeSceneGroupMaterial> NativeModelPassProgramLocked(Bridge& state,const NativeSceneCpuWindow<GuestReader>& window,
  uint32_t pass,bool skip_palette,const std::function<void(const std::string&)>& report);
NativeSceneGeometrySource NativeModelGeometrySource(const GuestReader& reader,const NativeModelBatchLayout& batch,uint32_t pass);
std::shared_ptr<const NativeIndexedMesh::RetainedDraw> NativeModelGeometryLocked(Bridge& state,const GuestReader& reader,
  const NativeSceneGeometrySource& source,const NativeModelBatchLayout& batch);
// Full-frame effect draws through the immediate path (defined after
// RecordNativeSceneImmediate), recorded back to back in order, with the
// activation (program, bindings, render state) done once per run of adjacent
// draws that share it (NativeEffectDrawsShareActivation). Returns the draws
// recorded; each failure goes to failed (the draw's error) and the next draw
// activates again.
uint64_t RecordNativeFullFrameEffectsLocked(Bridge& state,const GuestReader& reader,const NativeSceneCpuWindow<GuestReader>& window,
  std::span<const NativeEffectDraw> draws,const NativeScenePassCamera& camera,const NativeViewportState& viewport,
  const NativeFullFramePassTargets& formats,const std::function<void(const std::exception&)>& failed);
}  // namespace edf::native

// The render helper's thread state (the 821A5080 hook and its guest routes; defined in guest_shader_bridge.cpp).
// The heartbeat and step dispatcher run on the engine thread. Rendering work
// may run on a helper, so this budget must not be shared with that thread.
struct NativeLoopBudget {
  bool unlocked=false;
  uint32_t steps=0;
  uint64_t tick=0;
  float fraction=0;
  uint32_t divisor=1;
};
extern thread_local constinit NativeLoopBudget native_render_budget;
extern thread_local constinit uint64_t native_render_publication;
extern thread_local constinit bool native_render_tick_frame;
// The shadow render's guest side (native_shadow_guest, with its definition).
struct NativeShadowGuest {
  uint64_t tone_holds=0;      // PS_Downsample_Tone draws skipped (the immediate draw hook)
  uint64_t lifetime_holds=0;  // clEffectEtc02 +612 put back (the 8217C4A0 hook)
};
extern thread_local constinit NativeShadowGuest* native_shadow_guest;
extern std::atomic<uint64_t> native_render_frames;
extern std::atomic<uint64_t> native_guest_slot4_frames;

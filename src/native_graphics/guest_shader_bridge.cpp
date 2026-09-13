#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "guest_shader_bridge.h"
#include "d3d11_backend.h"
#include "d3d12_backend.h"
#include "native_render_backend.h"
#include "native_decode_workers.h"
#include "native_d3d12_preview.h"
#include "native_host_surface.h"
#include "guest_instance_parameters.h"
#include "guest_parameter_records.h"
#include "guest_draw_state.h"
#include "guest_readable_range.h"
#include "guest_sdk_readable_range.h"
#include "guest_mesh_watch_audit.h"
#include "guest_audio_output_ranges.h"
#include "native_model_buffers.h"
#include "native_pool_backings.h"
#include "native_cache_flush.h"
#include "native_model_header.h"
#include "native_model_cleanup.h"
#include "native_index_binding.h"
#include "native_stream_binding.h"
#include "native_shader_state.h"
#include "native_render_state_snapshot.h"
#include "native_declarations.h"
#include "native_material_parameters.h"
#include "native_font_bindings.h"
#include "native_xui_bindings.h"
#include "native_movie_bindings.h"
#include "native_generated_indices.h"
#include "native_physical_write_notify.h"
#include "native_contract_ledger.h"
#include "utility_layout.h"
#include "triangle_strip.h"
#include "guest_fence.h"
#include "native_fence_poll.h"
#include "native_fence_records.h"
#include "native_pix_monitor.h"
#include "native_allocator_wait.h"
#include "native_submission_flush.h"
#include "native_descriptor_submit.h"
#include "native_submission_observers.h"
#include "native_submission_dispatch.h"
#include "native_ring_cursor.h"
#include "native_submission_cursors.h"
#include "native_buffer_write_frame.h"
#include "native_pacing.h"
#include "native_profile_result.h"
#include "native_capture_policy.h"
#include "native_constant_ownership.h"
#include "immediate_mesh_key.h"
#include "d3d11_bindings.h"
#include "d3d11_texture.h"
#include "d3d11_quads.h"
#include "d3d11_mesh.h"
#include "d3d11_completion.h"
#include "d3d11_signals.h"
#include "d3d11_gpu_timer.h"
#include "d3d11_sampler.h"
#include "d3d11_render_state.h"
#include "movie_effect.h"
#include "xui_effect.h"
#include "font_effect.h"
#include <rex/cvar.h>
#include <rex/chrono/clock.h>
#include <rex/ppc/func.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/memory.h>
#include <rex/thread/mutex.h>
#include <cstring>
#include <format>
#include <limits>
#include <bit>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <fstream>
#include <set>
#include <cmath>
#include <chrono>
#include <thread>
#include <optional>

REXCVAR_DEFINE_BOOL(edf_native_shader_bridge, false, "EDF2027",
                   "Validate native D3D11 shader resources against live guest loads (development)");
REXCVAR_DEFINE_BOOL(edf_native_render_state_audit,false,"EDF2027",
                   "Compare setter-owned render-state snapshots with live draw state (diagnostic; does not bypass reads)");
REXCVAR_DEFINE_BOOL(edf_native_worker_callback_audit,false,"EDF2027",
                   "Log up to 256 render-worker callback registrations for writer provenance (diagnostic)");
REXCVAR_DEFINE_BOOL(edf_native_retirement_audit,false,"EDF2027",
                   "Sample completed native setter retirement branches and descriptor identity (diagnostic)");
REXCVAR_DEFINE_INT32(edf_native_geometry_verify_interval,256,"EDF2027",
                    "Sample the guest geometry byte comparison once per N snapshot observations of an owner once its revision proves the retained candidate; 0 compares every draw. A detected miss permanently restores full comparison");
REXCVAR_DEFINE_INT32(edf_native_geometry_verify_initial,8,"EDF2027",
                    "Snapshot observations of each subscription lifetime that always compare guest geometry bytes before the sampled schedule applies (0..65536)");
REXCVAR_DEFINE_INT32(edf_native_wait_stall_ms,5000,"EDF2027",
                    "Native wait no-progress deadline in host milliseconds (1..600000); failure does not fake GPU completion");
REXCVAR_DEFINE_BOOL(edf_native_owned_render_state,true,"EDF2027",
                   "Use native setter-owned render words, scissor enable and blend factors; false enables diagnostic legacy reads");
REXCVAR_DEFINE_INT32(edf_native_anisotropic_filtering, -1, "EDF2027",
                    "Native material filtering: -1 game default, 0 off, 1..5 for 1x..16x; preserves point/base-only sampling");
REXCVAR_DEFINE_INT32(edf_native_render_width, 0, "EDF2027",
                    "Native render width at startup; 0 preserves the original scene size (restart required)");
REXCVAR_DEFINE_INT32(edf_native_msaa, 0, "EDF2027",
                    "Native scene samples: 0 game default, 1 off, 2 or 4 MSAA (restart required)");
REXCVAR_DEFINE_INT32(edf_native_render_height, 0, "EDF2027",
                    "Native render height at startup; paired with render width (restart required)");
REXCVAR_DEFINE_STRING(edf_native_scene_capture, "", "EDF2027",
                     "Optional prefix for partial scene, font/movie and output-frame BMP diagnostics (not presentation)");
REXCVAR_DEFINE_INT32(edf_native_output_capture_interval,0,"EDF2027",
                    "Additional output capture interval in indexed frames; 0 disables periodic captures (development)");
REXCVAR_DEFINE_INT32(edf_native_output_capture_limit,32,"EDF2027",
                    "Maximum diagnostic output BMPs per run, clamped to 0..128; requires scene capture prefix (development)");
REXCVAR_DEFINE_INT32(edf_native_output_capture_start_frame,0,"EDF2027",
                    "Positive indexed frame starts interval captures and disables startup milestones; 0 preserves defaults (development)");
REXCVAR_DEFINE_BOOL(edf_native_output_capture_scene_color,false,"EDF2027",
                   "Also capture scene color at each selected output frame to diagnose post-processing differences (development)");
REXCVAR_DEFINE_BOOL(edf_native_pixel_centers,true,"EDF2027",
                   "Apply the guest PA_SU_VTX_CNTL half-pixel offset to the audited retail post passes; false restores the unshifted viewport for regression diagnosis");
REXCVAR_DEFINE_BOOL(edf_native_capture_indexed_state,false,"EDF2027",
                   "Trace up to 256 indexed draw states per selected capture frame; requires scene capture prefix (development)");
REXCVAR_DEFINE_INT32(edf_native_probe_x, -1, "EDF2027", "Optional native scene invalid-RGB probe pixel X");
REXCVAR_DEFINE_INT32(edf_native_probe_y, -1, "EDF2027", "Optional native scene invalid-RGB probe pixel Y");
REXCVAR_DEFINE_INT32(edf_native_probe_frame, 0, "EDF2027",
                    "First indexed output candidate to probe; 0 retains first-scene diagnostics");
REXCVAR_DEFINE_INT32(edf_native_probe_width, 1, "EDF2027", "Invalid-RGB diagnostic region width");
REXCVAR_DEFINE_INT32(edf_native_probe_height, 1, "EDF2027", "Invalid-RGB diagnostic region height");
REXCVAR_DEFINE_INT32(edf_native_probe_draw_limit, 4096, "EDF2027", "Maximum invalid-RGB diagnostic draws, capped at 65536");
REXCVAR_DEFINE_BOOL(edf_native_probe_negative, false, "EDF2027",
                   "Also stop the invalid-RGB probe on a scene channel at or below -1; the tone curve maps a large negative to white");
REXCVAR_DEFINE_STRING(edf_native_backend, "d3d12", "EDF2027",
                     "Backend used by everything that draws through the renderer's backend interface: d3d12 (default), d3d12-warp, d3d11, d3d11-warp, or empty for none. Built on first use, so selecting one costs nothing until something draws through it. An unknown name is refused rather than silently falling back");
REXCVAR_DEFINE_STRING(edf_native_scene_backend, "d3d11", "EDF2027",
                     "Backend that owns the scene's own resources - its textures, meshes and targets - while the draw paths are being moved onto the backend interface one at a time. Must stay d3d11 until the last of them has moved: a ported path and an unported one have to share the same targets, and only the adopted d3d11 backend is this renderer's own device. Setting it to d3d12 before then gives the unported paths nothing to bind");
REXCVAR_DEFINE_BOOL(edf_native_backend_present, true, "EDF2027",
                   "Let the selected backend present the game's window from its own device. False keeps the D3D11 presenter, which is the control for measuring what the backend path costs or saves");
REXCVAR_DEFINE_BOOL(edf_native_reuse_material, true, "EDF2027",
                   "Skip re-binding the shader pair, textures and samplers when the previous indexed draw already bound the same ones and nothing has bound since. Set false if repeated objects ever show another material's textures; that is what a wrong guard here looks like");
REXCVAR_DEFINE_INT32(edf_native_shader_workers, -1, "EDF2027",
                    "Threads used to compile a shader registration's entries: -1 picks one per core up to eight, 0 compiles inline on the calling thread. Compilation is the load cost worth threading - the entries are a real batch and each takes milliseconds, unlike the per-draw work, which has neither property");
REXCVAR_DEFINE_BOOL(edf_native_backend_preview, false, "EDF2027",
                   "Open a second window drawn and presented entirely by the selected backend. Needs --edf_native_backend and --edf_native_publish_frames. The renderer's own window is untouched");
REXCVAR_DEFINE_BOOL(edf_native_batch_audit, false, "EDF2027",
                   "Measure runs of consecutive indexed draws that differ only in per-instance constants; the mean run length is the draw-call reduction instancing would give");
REXCVAR_DEFINE_INT32(edf_native_contract_limit, 4096, "EDF2027",
                    "Distinct draw contracts the coverage ledger retains (1..1048576); reaching it is counted, never silently dropped");
REXCVAR_DEFINE_STRING(edf_native_contract_export, "", "EDF2027",
                     "Write the captured draw-contract catalog to this path; the offline geometry check replays it");
REXCVAR_DEFINE_BOOL(edf_native_contract_coverage, false, "EDF2027",
                   "Also record every submitted draw contract, so a run can enumerate what the content exercises; costs a set lookup per draw");
REXCVAR_DEFINE_INT32(edf_native_shared_constant_audit, 0, "EDF2027",
                    "Audit Common.fx globals a stage's native shader consumes but the material never lists for that stage, for this many activations; 0 disables (development)");
REXCVAR_DEFINE_BOOL(edf_native_hook_timings, false, "EDF2027",
                   "Log inclusive CPU wall times for native/original graphics hook phases (development)");
REXCVAR_DEFINE_BOOL(edf_native_loading_trace, false, "EDF2027",
                   "Sample end-frame publication eligibility and cumulative UI draws; does not capture pixels (development)");
REXCVAR_DEFINE_BOOL(edf_native_load_timings, false, "EDF2027",
                   "Log each texture/shader load phase CPU duration, including nested work (development)");
REXCVAR_DEFINE_BOOL(edf_native_mesh_watch_audit,false,"EDF2027",
                   "Audit physical mesh write versions against exact bytes; never skips validation (development)");
REXCVAR_DEFINE_BOOL(edf_native_untiled_scene, true, "EDF2027",
                   "Deprecated compatibility setting; native untiled scene lifecycle is always used");
REXCVAR_DEFINE_BOOL(edf_native_guest_heap_reads, true, "EDF2027",
                   "Validate committed readable SDK regions; Windows validation remains fallback (false forces OS checks)");
REXCVAR_DEFINE_BOOL(edf_native_fence_probe, false, "EDF2027",
                   "Observe native event-query completion at guest fence boundaries; never writes guest counters");
REXCVAR_DEFINE_BOOL(edf_native_validate_wait, false, "EDF2027",
                   "Development: await native completion after original guest waits; requires native bridge and fence probe");
REXCVAR_DEFINE_BOOL(edf_native_publish_frames, false, "EDF2027",
                   "Publish native movie/partial scene GPU snapshots for host presentation; requires native bridge");
REXCVAR_DEFINE_BOOL(edf_native_preview_window, false, "EDF2027",
                   "Show native frames in a separate development window; requires frame publication");
REXCVAR_DECLARE(bool, edf_native_host);
REX_EXTERN(__imp__KeSetEvent);

namespace edf::native {
// Capture at renderer initialization. Saving a new F1 choice must not change
// live UI scaling while the current render targets still have the old size.
const std::array<int32_t,2>& NativeRenderDimensions() {
  static const std::array<int32_t,2> dimensions{
    REXCVAR_GET(edf_native_render_width),REXCVAR_GET(edf_native_render_height)};
  return dimensions;
}
namespace {
// Opt-in CPU wall-clock diagnostics, not GPU timestamps. Include lock waits and
// any nested work; phases from different hooks must not be added as exclusive
// frame costs. Per-thread buckets avoid adding contention to the draw path.
enum class HookPhase { ActivationGuest, ActivationNative, InstanceGuest, InstanceNative,
                       IndexedNative, IndexedGuest, ImmediateNative, ImmediateGuest,
                       SwapGpuWait, SwapRefreshWait, EngineWait, CompletionPoll,
                       SceneSetup, WorkerService, TilingBegin, TilingEnd,
                       FenceWait, SubmissionFlush, DescriptorSubmit,
                       SceneSetupGuest, SceneSetupNative, SceneSetupLock, SceneClear,
                       ColorTarget, DepthTarget, ViewportHook, ViewportLock,
                       ViewportRead, ViewportWrite, ViewportGuest,
                       IndexedMesh, IndexedBindings, MeshRanges, MeshAcquire, MeshDrawRange,
                       IndexedSubmissionWait, IndexedContextWait, ImmediateSubmissionWait,
                       ImmediateContextWait, PresentationContextWait,
                       ActivationLock, ActivationResolve, ActivationVertexParams, ActivationPixelParams,
                       ActivationTextures, ActivationBind,
                       XuiNative, XuiDecode, XuiBind, XuiDraw,
                       TextureSnapshot, TextureOriginal, TextureLock, TextureCreate,
                       ShaderRegistration, ShaderLock, ShaderEntry,
                       TextureAllocate, TextureUpload2D, TextureUploadVolume, TexturePrepare,
                       ResourceOneShot, ResourceCoordinator, ResourceHelper, ResourceTransition, Count };
thread_local uint32_t texture_loader_depth=0;
class HookTiming {
 public:
  explicit HookTiming(HookPhase phase,bool active=true) : phase_(phase), enabled_(active && (phase>=HookPhase::TextureSnapshot ?
      REXCVAR_GET(edf_native_load_timings) : REXCVAR_GET(edf_native_hook_timings))) {
    if(enabled_) start_=Clock::now();
  }
  ~HookTiming() { Finish(); }
  void Finish() {
    if(!enabled_) return;
    enabled_=false;
    const auto now=Clock::now();
    const double ms=std::chrono::duration<double,std::milli>(now-start_).count();
    struct Bucket { uint64_t count=0; double total=0,maximum=0; Clock::time_point reported{}; };
    static thread_local std::array<Bucket,static_cast<size_t>(HookPhase::Count)> buckets{};
    static constexpr const char* names[]{"activation.original","activation.native",
      "instance.original","instance.native","indexed.native","indexed.original",
      "immediate.native","immediate.original","swap.gpu_wait","swap.refresh_wait",
      "engine.wait","completion.poll","scene.setup","worker.service","tiling.begin","tiling.end",
      "fence.wait","submission.flush","descriptor.submit","scene.setup.original","scene.setup.native",
      "scene.setup.lock","scene.clear","target.color","target.depth","viewport.hook",
      "viewport.lock","viewport.read","viewport.write","viewport.original",
      "indexed.mesh","indexed.bindings","mesh.ranges","mesh.acquire","mesh.draw_range",
      "indexed.submission_wait","indexed.context_wait","immediate.submission_wait",
      "immediate.context_wait","presentation.context_wait",
      "activation.lock","activation.resolve","activation.params_vs","activation.params_ps",
      "activation.textures","activation.bind",
      "xui.native","xui.decode","xui.bind","xui.draw",
      "load.texture.snapshot","load.texture.original","load.texture.lock","load.texture.create",
      "load.shader.registration","load.shader.lock","load.shader.entry",
      "load.texture.allocate","load.texture.upload2d","load.texture.upload_volume","load.texture.prepare",
      "load.resource.oneshot","load.resource.coordinator","load.resource.helper","load.resource.transition"};
    static_assert(std::size(names)==static_cast<size_t>(HookPhase::Count));
    const auto index=static_cast<size_t>(phase_);
    auto& bucket=buckets[index];
    ++bucket.count; bucket.total+=ms; bucket.maximum=(std::max)(bucket.maximum,ms);
    if(phase_>=HookPhase::TextureSnapshot ||
       (bucket.count>=256 && (bucket.reported==Clock::time_point{} || now-bucket.reported>=std::chrono::seconds(5)))) {
      REXLOG_INFO("Native hook timing: phase={} calls={} total_ms={} max_ms={} (inclusive CPU wall time)",
        names[index],bucket.count,bucket.total,bucket.maximum);
      bucket={};
      bucket.reported=now;
    }
  }
 private:
  using Clock=std::chrono::steady_clock;
  HookPhase phase_;
  bool enabled_;
  Clock::time_point start_{};
};
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
      // QueryRegionInfo checks both state and access under the heap mutex.
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

NativeViewportState DecodeDrawViewport(const GuestViewportWords& snapshot) {
  const auto& words=snapshot.words;
  return MakeNativeDrawViewport(words[0],words[1],words[2],words[3],
    std::bit_cast<float>(words[4]),std::bit_cast<float>(words[5]),snapshot.scissor_enabled,
    {std::bit_cast<int32_t>(words[6]),std::bit_cast<int32_t>(words[7]),
     std::bit_cast<int32_t>(words[8]),std::bit_cast<int32_t>(words[9])});
}
template<class Reader>
NativeViewportState ReadDrawViewport(const Reader& reader,uint32_t device) {
  return DecodeDrawViewport(ReadViewportWords(reader,device));
}
std::array<float,4> ResolveBlendFactorForDraw(uint32_t device,
    const NativeRenderStateSnapshots::BlendWords* live);
template<class Reader>
void BindGuestRenderState(const NativeRenderState& state,ID3D11DeviceContext& context,
                          const Reader& reader,uint32_t device,uint64_t* generation=nullptr) {
  // Every path that binds render state comes through here, so this is where a
  // skip elsewhere has to be invalidated from. Same for BindActiveTarget.
  // Passed in rather than fetched, because this is a template defined before
  // the bridge state is.
  if(generation) ++*generation;
  if(!state.requires_blend_factor) { state.Bind(context); return; }
  if(!REXCVAR_GET(edf_native_owned_render_state) && !REXCVAR_GET(edf_native_render_state_audit)) {
    state.Bind(context,ReadBlendFactor(reader,device)); return;
  }
  NativeRenderStateSnapshots::BlendWords live{};
  const bool audit=REXCVAR_GET(edf_native_render_state_audit);
  if(audit) live=ReadGuestWords<4>(reader,reader.Add(device,10336));
  state.Bind(context,ResolveBlendFactorForDraw(device,audit?&live:nullptr));
}
struct VertexParameterRange {
  std::string name;
  uint32_t first,count;
  ShaderBindings::FloatRegisterBinding normal,reversed;
};
struct RegisteredShader {
  uint32_t owner;
  std::unique_ptr<ShaderBindings> bindings;
  std::unique_ptr<QuadStream> quads;
  std::unique_ptr<ShaderBindings> reversed_bindings;
  std::unique_ptr<QuadStream> reversed_quads;
  uint64_t source_fingerprint=0;
  struct ParameterBinding {
    ShaderBindings::FloatRegisterBinding binding;
    bool canvas_xy=false;
  };
  struct ParameterPlan {
    std::array<std::vector<ParameterBinding>,4> groups;
    std::array<bool,4> ready{};
    std::array<std::vector<ShaderBindings::ResourceBinding>,2> textures;
    std::array<bool,2> textures_ready{};
    std::shared_ptr<const std::vector<VertexParameterRange>> vertex_ranges;
  };
  using ParameterOwner=std::weak_ptr<const NativeMaterialParameters::Groups>;
  std::array<std::map<ParameterOwner,ParameterPlan,std::owner_less<ParameterOwner>>,2> parameter_plans;
  ParameterPlan& MaterialPlan(const std::shared_ptr<const NativeMaterialParameters::Groups>& material,bool reverse) {
    auto& cache=parameter_plans[reverse?1:0];
    auto found=cache.find(ParameterOwner(material));
    if(found==cache.end()) {
      std::erase_if(cache,[](const auto& entry){return entry.first.expired();});
      found=cache.try_emplace(ParameterOwner(material)).first;
    }
    return found->second;
  }
  const std::vector<ShaderBindings::ResourceBinding>& ResolveTextures(
      const std::shared_ptr<const NativeMaterialParameters::Groups>& material,size_t group) {
    auto& plan=MaterialPlan(material,false);
    if(!plan.textures_ready[group]) {
      std::vector<ShaderBindings::ResourceBinding> fresh;
      fresh.reserve(material->textures[group].size());
      for(const auto& texture:material->textures[group]) fresh.push_back(bindings->ResolveResource(texture.name));
      plan.textures[group]=std::move(fresh); plan.textures_ready[group]=true;
    }
    return plan.textures[group];
  }
  const std::vector<ParameterBinding>& ResolveParameters(
      const std::shared_ptr<const NativeMaterialParameters::Groups>& material,size_t group,bool reverse) {
    auto& plan=MaterialPlan(material,reverse);
    if(!plan.ready[group]) {
      std::vector<ParameterBinding> fresh;
      fresh.reserve((*material)[group].size());
      const auto& destination=reverse?*reversed_bindings:*bindings;
      for(const auto& parameter:(*material)[group])
        fresh.push_back({destination.ResolveFloatRegisters(parameter.name),
          IsNativeCanvasXY(destination.shader().source_fingerprint,destination.shader().entry.name,parameter.name,group)});
      plan.groups[group]=std::move(fresh); plan.ready[group]=true;
    }
    return plan.groups[group];
  }
  std::shared_ptr<const std::vector<VertexParameterRange>> ResolveVertexRanges(
      const std::shared_ptr<const NativeMaterialParameters::Groups>& material) {
    auto& plan=MaterialPlan(material,false);
    if(!plan.vertex_ranges) {
      if(!reversed_bindings) throw std::runtime_error("vertex parameter has no reversed binding plan");
      auto fresh=std::make_shared<std::vector<VertexParameterRange>>();
      for(size_t group=0;group<2;++group) {
        const auto& normal=ResolveParameters(material,group,false);
        const auto& reversed=ResolveParameters(material,group,true);
        size_t index=0;
        for(const auto& parameter:(*material)[group]) {
          if(parameter.registers) {
            if(parameter.first>256 || parameter.registers>256-parameter.first)
              throw std::runtime_error("invalid vertex parameter register range");
            fresh->push_back({parameter.name,parameter.first,parameter.registers,normal[index].binding,reversed[index].binding});
          }
          ++index;
        }
      }
      // Publish only a complete plan. The active shared owner survives material
      // retirement; binding tokens still reject a replaced shader generation.
      plan.vertex_ranges=std::move(fresh);
    }
    return plan.vertex_ranges;
  }
};
struct TextureCreation {
  uint32_t width, height, depth, levels, usage, format, pool, type, caller;
};
struct RegisteredTarget {
  uint32_t texture_handle, surface_handle;
  NativeRenderTarget native;
};
NativeBufferWrites& BufferWrites() {
  static NativeBufferWrites writes;
  return writes;
}
struct GuestStream {
  uint32_t resource, offset, stride;
};
struct NativeScene {
  NativeRenderTarget color;
  NativeDepthTarget depth;
  NativeRenderTarget output;
  uint32_t output_surface=0;
  uint32_t color_surface=0;
  uint32_t samples=1;
  bool frame_complete=false;
  std::unordered_map<uint32_t,NativeRenderTarget> direct_outputs;
};
struct SurfaceCreation { uint32_t width,height,format,msaa; };
struct EmbeddedShader { uint32_t source; bool pixel; };
struct MoviePlaneLock { uint32_t texture,pitch,pixels; };
struct MovieDecodeLocks { std::vector<MoviePlaneLock> planes; bool failed=false; };
thread_local MovieDecodeLocks* active_movie_decode=nullptr;
struct DrawVisibility {
  Microsoft::WRL::ComPtr<ID3D11Query> query;
  std::array<uint32_t,4> key; // VS, PS, raster state, depth state.
};
struct NativePacingState {
  // Separate from the rendering mutex: sleeping must not block host UI paints.
  std::mutex mutex;
  NativePacingClock clock;
  uint64_t calls=0;
};
NativePacingState& PacingState() {
  static NativePacingState state;
  return state;
}
struct Bridge {
  // Game command ordering is separate from immediate-context access. A swap
  // may hold this gate while releasing mutex between polls so the host can paint.
  std::recursive_mutex submissions;
  std::mutex mutex;
  std::filesystem::path root;
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  std::unique_ptr<NativeFrameHandoff> presentation_frames;
  std::weak_ptr<GuestMeshWatchAudit> mesh_watch_audit;
  std::optional<NativeDisplayGamma> display_gamma;
  uint32_t display_gamma_device=0;
  std::map<uint32_t,uint32_t> native_published_completions;
  NativeSubmissionCursors submission_cursors;
  struct SwapClock { NativePacingClock clock; uint64_t sampled=0; };
  std::map<uint32_t,SwapClock> swap_clocks;
  std::unordered_map<uint32_t, RegisteredShader> shaders;
  std::shared_ptr<const std::vector<VertexParameterRange>> active_vertex_parameters;
  uint64_t instance_parameter_updates=0, instance_parameter_errors=0;
  std::unordered_map<uint32_t, EmbeddedShader> embedded_shaders;
  std::unordered_map<uint32_t, NativeTexture> textures;
  std::unordered_map<uint32_t, TextureCreation> texture_creations;
  std::unordered_map<uint32_t, RegisteredTarget> render_targets;
  std::unordered_map<uint32_t, NativeDepthTarget> depth_targets;
  std::unordered_map<uint32_t, SurfaceCreation> surface_creations;
  std::unordered_map<uint32_t, NativeScene> scenes;
  uint32_t active_scene = 0;
  std::set<uint32_t> untiled_devices;
  std::unique_ptr<NativeGpuTimer> scene_gpu_timer;
  uint32_t scene_gpu_timer_owner=0;
  bool scene_gpu_timer_resolved=false;
  uint32_t active_output = 0, output_captures = 0;
  uint64_t loading_trace_frames=0;
  std::array<uint64_t,4> loading_trace_routes{};
  std::chrono::steady_clock::time_point loading_trace_reported{};
  std::chrono::steady_clock::time_point bloom_parameters_reported{};
  uint64_t output_draws=0;
  uint64_t output_unhandled=0;
  // Sentinel distinguishes "not observed" from a real zero register word.
  uint64_t vertex_center_word=UINT64_MAX;
  uint64_t shared_constant_activations=0,shared_constant_unsupplied=0,shared_constant_split_storage=0;
  uint64_t shared_constant_both_supplied=0;
  uint32_t last_activation_instance=0,last_activation_vertex=0,last_activation_pixel=0;
  uint64_t repeat_activations=0;
  // Created when --edf_native_backend names one. The renderer still draws
  // through its direct D3D11 path; this exists so the backend can be created
  // and reported inside the real process, which is where device creation
  // actually fails, and so paths can be moved onto it one at a time.
  std::unique_ptr<edf::native::NativeRenderBackend> backend;
  // The scene's resources, separate from the selection above because the two
  // answer different questions while the port is under way: --edf_native_backend
  // is what the player picked, --edf_native_scene_backend is what the half-ported
  // scene can actually share targets with. See the cvar.
  std::unique_ptr<edf::native::NativeRenderBackend> scene_backend;
  // Whether the scene backend has a frame open. Opened lazily by the first
  // thing that records into it and closed at the guest's swap barrier, which
  // is the only point in the frame where the renderer already knows the frame
  // is over. A backend that records has to be told where a frame ends: D3D11
  // did not, which is why nothing needed this until now.
  bool scene_frame_open=false;
  uint64_t scene_frames=0;
  // Declared after the backend so it is destroyed before it: the preview
  // thread uses the backend on every tick and must be stopped first.
  std::unique_ptr<edf::native::NativeD3D12Preview> backend_preview;
  // Run-length accounting for the XUI path, the same question the indexed
  // audit answered: how many consecutive draws differ only in things a batch
  // would carry per-item, and how many change state that a batch cannot.
  // Bumped by every path that binds a target or render state, so the indexed
  // path can tell whether anything has bound since it last did. Without this a
  // skip would compare against its own cache and miss that another path had
  // replaced the state underneath it.
  uint64_t bind_generation=0,indexed_bind_generation=0;
  edf::native::RenderStateWords indexed_bind_key{};
  uint32_t indexed_bind_target=0,indexed_bind_scene=0,indexed_bind_output=0;
  uint32_t indexed_bind_vertex=0,indexed_bind_pixel=0;
  bool indexed_bind_reversed=false;
  uint64_t indexed_materials_reused=0;
  bool indexed_bind_valid=false;
  uint64_t indexed_binds_skipped=0,indexed_binds_bound=0;
  uint64_t xui_batch_draws=0,xui_batch_runs=0,xui_batch_run=0,xui_batch_longest=0;
  uint64_t xui_batch_collapsible=0,xui_last_state=0,xui_last_constants=0;
  uint64_t xui_constants_differ=0;
  std::array<uint32_t,12> last_batch_key{};
  uint64_t batch_draws=0,batch_runs=0,batch_run=0,batch_run_total=0,batch_longest=0,batch_collapsible=0;
  uint64_t instance_shape=0,last_instance_shape=0,batch_shape_breaks=0;

  std::set<std::array<uint32_t,2>> shared_constant_pairs;
  std::set<std::array<uint32_t,3>> shared_constant_storage_reported;
  std::set<std::array<uint32_t,3>> shared_constant_reported;
  uint64_t movie_uploads=0, movie_upload_errors=0;
  uint64_t movie_draws=0, movie_draw_errors=0;
  std::unique_ptr<ShaderBindings> movie_vertex,movie_pixel,movie_pixel_sd;
  std::array<std::optional<NativeMovieBindings>,2> movie_bindings;
  std::unique_ptr<QuadStream> movie_vertices;
  std::unique_ptr<ShaderBindings> xui_vertex,xui_pixel;
  std::unique_ptr<ShaderBindings> xui_reversed_vertex;
  std::optional<NativeXuiVertexBindings> xui_vertex_bindings,xui_reversed_vertex_bindings;
  std::array<std::optional<NativeXuiPixelBindings>,3> xui_pixel_bindings;
  std::unique_ptr<ShaderBindings> xui_solid_pixel,xui_mask_pixel;
  std::unique_ptr<PositionTriangleStream> xui_vertices;
  uint64_t xui_draws=0,xui_errors=0;
  std::unique_ptr<ShaderBindings> font_vertex,font_pixel;
  std::optional<NativeFontBindings> font_bindings;
  std::unique_ptr<QuadStream> font_vertices;
  uint64_t font_draws=0,font_errors=0;
  uint64_t utility_draws=0,utility_errors=0;
  uint64_t utility_3d_draws=0,utility_3d_errors=0;
  std::array<bool,6> scene_immediate_variants_reported{};
  std::array<bool,4> utility_variants_reported{};
  std::set<std::array<uint32_t,4>> unsupported_output_pairs;
  uint64_t immediate_requests=0,immediate_empty=0,immediate_submitted=0,immediate_unsubmitted=0;
  std::set<std::array<uint32_t,5>> immediate_unsubmitted_paths;
  std::chrono::steady_clock::time_point immediate_coverage_reported{};
  uint64_t scene_begins = 0, scene_ends = 0;
  uint64_t scene_resolves = 0,scene_resolve_errors = 0;
  uint64_t scene_indexed_start = 0;
  uint32_t scene_captures = 0;
  uint64_t indexed_output_frames = 0;
  uint64_t indexed_trace_frame = 0;
  uint64_t post_input_capture_frame = 0;
  uint32_t post_input_capture_pass = 0;
  uint32_t indexed_trace_draws = 0;
  std::vector<DrawVisibility> visibility;
  std::set<std::array<uint32_t,4>> clip_probes;
  uint32_t color_probe_draws=0;
  bool color_probe_done=false;
  uint64_t depth_clears = 0, depth_clear_skips = 0, depth_errors = 0;
  std::map<SamplerStateWords, Microsoft::WRL::ComPtr<ID3D11SamplerState>> samplers;
  std::map<RenderStateWords,NativeRenderState> render_states;
  std::map<std::pair<uint32_t,uint32_t>,GuestStream> streams;
  std::map<uint32_t,uint32_t> index_bindings;
  std::map<uint32_t,uint32_t> declaration_bindings;
  NativeShaderState shader_bindings;
  NativeRenderStateSnapshots render_state_snapshots;
  NativeDeclarations declarations;
  NativeMaterialParameters material_parameters;
  NativeGeneratedIndexCache generated_indices;
  NativeModelBuffers model_buffers{&BufferWrites()};
  uint64_t buffer_update_notifications=0;
  uint64_t buffer_write_batches=0;
  uint64_t buffer_write_all_batches=0;
  uint64_t reported_mesh_mismatches=0;
  NativeMeshCache meshes;
  // Immediate guest buffers may be reused or mutated every draw. This cache
  // compares all bytes and stays separate from long-lived scene geometry.
  NativeMeshCache immediate_meshes{4*1024*1024,256,true};
  std::map<uint32_t,std::unique_ptr<NativeCompletionQueue>> completion_queues;
  std::map<uint32_t,std::unique_ptr<NativePresentProfiler>> present_profilers;
  std::set<uint32_t> completion_faults;
  uint64_t completion_submits=0;
  std::unordered_map<uint32_t,std::unique_ptr<NativeSignalQueue>> signal_queues;
  // Registry/context -> delivery, or SDK global -> delivery. Never delivery ->
  // registry/context/global. CPU delivery must not depend on renderer progress.
  std::mutex signal_delivery_mutex;
  std::unordered_map<uint32_t,NativeSignalDelivery> signal_deliveries;
  uint64_t completion_waits=0,completion_waits_native_pending=0;
  uint64_t completion_validated_waits=0;
  uint64_t indexed_draws = 0, indexed_uploads = 0, indexed_errors = 0, indexed_submitted = 0;
  std::array<uint64_t,4> indexed_ownership{}; // Bit0: registered VB; bit1: registered IB.
  uint64_t indexed_ownership_attempts=0,indexed_physical_pairs=0;
  uint64_t indexed_empty_requests=0,indexed_nonempty_submitted=0,indexed_unsubmitted_requests=0;
  std::set<std::array<uint32_t,5>> indexed_unsubmitted_paths;
  // Identity is the contract, not the caller: see native_contract_ledger.h.
  edf::native::NativeContractLedger contracts;
  std::chrono::steady_clock::time_point indexed_coverage_reported{};
  uint64_t indexed_outside_scene = 0;
  uint64_t sampler_bindings = 0;
  std::vector<std::pair<uint32_t,uint32_t>> target_stack;
  uint32_t active_target = 0;
  uint64_t target_begins = 0, target_resolves = 0, target_unwritten = 0;
  uint64_t immediate_draws = 0;
  uint64_t native_quad_draws = 0, native_quad_errors = 0;
  uint64_t color_clears = 0, color_clear_skips = 0;
  uint32_t active_vertex = 0, linked_vertex = 0, linked_pixel = 0;
  uint64_t texture_loads = 0, texture_errors = 0;
  uint64_t texture_bindings = 0, texture_missing = 0, texture_binding_errors = 0;
  uint64_t activations = 0, misses = 0, parameter_uploads = 0, optimized_out = 0, parameter_errors = 0;
};
Bridge& State() { static Bridge state; return state; }
// Defined below, next to the selection it mirrors; declared here because
// the texture hook, further up, is the first thing to create a scene resource.
edf::native::NativeRenderBackend& EnsureSceneBackendLocked(Bridge& state);
// The scene backend's recorder, with a frame open.
edf::native::NativeBackendRecorder& SceneRecorderLocked(Bridge& state);
// Closes the frame if one is open. Safe to call when none is.
void SubmitSceneFrameLocked(Bridge& state);
// Called under the registry lock at consumption, never from a writer callback.
void AuditGeneratedWrites(Bridge& state,const NativeBufferWrites::Batch& batch) {
  for(size_t i=0;i<batch.writer_hit_count;++i) {
    const auto& hit=batch.writer_hits[i];
    REXLOG_INFO("Native geometry writer site: file={}, line={}, provider={:#x}, caller={:#x}, owner={:#x}, lifetime={}, calls={}, first_physical={:#x}, first_bytes={} (owner at notification; caller is pre-call LR; observed workload only)",
      hit.site.file?hit.site.file:"",hit.site.line,hit.site.provider,hit.site.caller,
      hit.owner,hit.lifetime,hit.calls,hit.first_range.address,hit.first_range.bytes);
  }
  if(batch.writer_hits_omitted)
    REXLOG_WARN("Native geometry writer inventory incomplete: omitted_hits={} (64 site/owner/lifetime keys per batch)",batch.writer_hits_omitted);
  if(!batch.generated_calls && !batch.provider_exact_owner_hits) return;
  static uint64_t batches=0,reported_overlaps=0;
  static uint64_t exact_batches=0,total_generated_hits=0,total_provider_hits=0;
  total_generated_hits+=batch.generated_exact_owner_hits;
  total_provider_hits+=batch.provider_exact_owner_hits;
  const bool exact_hit=batch.generated_exact_owner_hits || batch.provider_exact_owner_hits;
  if(exact_hit) ++exact_batches;
  size_t overlaps=0;
  for(size_t i=0;i<batch.subscribed_sample_count;++i) {
    const auto& sample=batch.subscribed_samples[i];
    state.model_buffers.VisitPhysicalOverlaps(sample.address,sample.bytes,[&](uint32_t owner,const auto& buffer) {
      ++overlaps;
      if(reported_overlaps++<16)
        REXLOG_INFO("Native generated write overlap: physical={:#x}, bytes={}, owner={:#x}, generation={}, kind={} (sampled; owner at drain, not writer PC)",
          sample.address,sample.bytes,owner,buffer.generation,buffer.kind==NativeModelBuffers::Kind::Vertex?"vertex":"index");
    });
  }
  ++batches;
  if(batches<=8 || (batches & (batches-1))==0)
    REXLOG_INFO("Native generated write audit: batch={}, generated_calls={}, provider_calls={}, samples={}, omitted={}, sample_owner_overlaps={}, all={}, pages={} (bounded samples; no completeness claim)",
      batches,batch.generated_calls,batch.provider_calls,batch.generated_sample_count,
      batch.generated_calls-batch.generated_sample_count,overlaps,batch.all,batch.pages?batch.pages->count():0);
  if(batches<=8 || (batches & (batches-1))==0)
    REXLOG_INFO("Native subscribed write audit: batch={}, generated_page_hits={}, retained={}, omitted={}, unknown={} (page subscriptions at notification; exact owners queried at drain)",
      batches,batch.generated_subscribed_calls,batch.subscribed_sample_count,
      batch.generated_subscribed_calls-batch.subscribed_sample_count,batch.subscriptions_unknown);
  if((exact_hit && (exact_batches<=16 || (exact_batches & (exact_batches-1))==0)) || batches<=8 || (batches & (batches-1))==0)
    REXLOG_INFO("Native exact write versions: batch={}, generated_owner_hits={}, provider_owner_hits={}, total_generated_hits={}, total_provider_hits={} (unsampled overlap counts at notification, not writer PCs)",
      batches,batch.generated_exact_owner_hits,batch.provider_exact_owner_hits,total_generated_hits,total_provider_hits);
}
void CaptureScene(Bridge& state,uint32_t owner) {
  const auto prefix=REXCVAR_GET(edf_native_scene_capture);
  if (prefix.empty() || state.scene_captures>=3 || state.indexed_submitted==state.scene_indexed_start) return;
  const auto scene=state.scenes.find(owner);
  if (scene==state.scenes.end()) return;
  const auto number=++state.scene_captures;
  try {
    const auto path=std::filesystem::path(prefix+"."+std::to_string(number)+".bmp");
    if (std::filesystem::exists(path)) throw std::runtime_error("native capture path already exists");
    const auto bmp=CaptureNativeHdrBmp(*state.context.Get(),*scene->second.color.surface.Get());
    std::ofstream output(path,std::ios::binary);
    output.write(reinterpret_cast<const char*>(bmp.data()),bmp.size());
    output.close();
    if (!output) throw std::runtime_error("cannot write native scene capture");
    REXLOG_INFO("Native partial scene capture: {}, indexed_draws={}, initialized={}, frame_complete={}, linear RGB clamped; not final tone mapping",
      path.string(),state.indexed_submitted-state.scene_indexed_start,scene->second.color.content_valid,scene->second.frame_complete);
    if(scene->second.samples==1) {
      const auto depth=InspectNativeDepth(*state.context.Get(),*scene->second.depth.surface.Get(),0);
      REXLOG_INFO("Native scene depth diagnostic: changed_pixels={}, top/middle/bottom={}/{}/{}, min={}, max={}, nonfinite={}",
        depth.changed_pixels,depth.vertical_bands[0],depth.vertical_bands[1],depth.vertical_bands[2],
        depth.minimum,depth.maximum,depth.nonfinite_pixels);
    } else REXLOG_INFO("Native scene depth diagnostic unavailable for {} samples; no depth-coverage claim",scene->second.samples);
    // Surface readback above has completed prior GPU work. Never busy-wait
    // here: unavailable queries are reported, not interpreted as zero samples.
    struct Counts { uint64_t draws=0,empty=0,samples=0; };
    std::map<std::array<uint32_t,4>,Counts> groups;
    size_t unavailable=0;
    for (const auto& draw:state.visibility) {
      uint64_t samples=0;
      if (state.context->GetData(draw.query.Get(),&samples,sizeof(samples),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK) {
        ++unavailable; continue;
      }
      auto& counts=groups[draw.key]; ++counts.draws; counts.empty+=samples==0; counts.samples+=samples;
    }
    for (const auto& [key,counts]:groups)
      REXLOG_INFO("Native visibility: VS={:#x}, PS={:#x}, raster={:#x}, depth={:#x}, draws={}, zero_sample_draws={}, passed_samples={}",
        key[0],key[1],key[2],key[3],counts.draws,counts.empty,counts.samples);
    REXLOG_INFO("Native visibility coverage: queries={}, unavailable={}, total_indexed={}, outside_scene={}",
      state.visibility.size(),unavailable,state.indexed_draws,state.indexed_outside_scene);
  } catch (const std::exception& error) { REXLOG_ERROR("Native scene capture: {}",error.what()); }
}
void ResolveScene(const GuestReader& reader,Bridge& state,uint32_t owner) {
  try {
    auto& scene=state.scenes.at(owner);
    const auto handle=reader.Word(reader.Add(owner,104));
    const auto creation=state.texture_creations.find(handle);
    if (!handle || creation==state.texture_creations.end() || creation->second.format!=0x1a22ab60 ||
        creation->second.width!=scene.color.sampled.width || creation->second.height!=scene.color.sampled.height)
      throw std::runtime_error("scene resolve destination is not the expected full-frame HDR texture");
    ID3D11ShaderResourceView* empty[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
    state.context->PSSetShaderResources(0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,empty);
    state.context->VSSetShaderResources(0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,empty);
    ResolveNativeRenderTarget(*state.context.Get(),scene.color);
    state.textures.insert_or_assign(handle,scene.color.sampled);
    if (++state.scene_resolves<=5 || state.scene_resolves%1000==0)
      REXLOG_INFO("Native HDR scene resolve: count={}, texture={:#x}, initialized={}, frame_complete={}",
        state.scene_resolves,handle,scene.color.sampled.content_valid,scene.frame_complete);
  } catch (const std::exception& error) {
    if (++state.scene_resolve_errors<=10) REXLOG_ERROR("Native HDR scene resolve: {}",error.what());
  }
}

void ForgetOwner(uint32_t owner) {
  auto& state = State();
  // Registry/cache retirement does not submit commands. The registry mutex
  // excludes native users; bound GPU resources retain their D3D references.
  std::lock_guard lock(state.mutex);
  const auto erased = std::erase_if(state.shaders, [owner](const auto& item) { return item.second.owner == owner; });
  state.active_vertex = state.linked_vertex = state.linked_pixel = 0;
  state.active_vertex_parameters.reset();
  if (erased) REXLOG_INFO("Native shader bridge: released {} shaders for owner={:#x}", erased, owner);
  if (erased) { state.meshes.Clear(); state.immediate_meshes.Clear(); }
}

template<class Original>
void ImportTexture(PPCContext& ctx, uint8_t* base, Original original) {
  if (!REXCVAR_GET(edf_native_shader_bridge)) { original(ctx, base); return; }
  const GuestReader reader(base);
  uint32_t output = 0;
  std::vector<uint8_t> image;
  try {
    HookTiming snapshot_timing(HookPhase::TextureSnapshot);
    // Shared D3DX image loader sub_82201458, reached by both engine loaders
    // and direct callers. The output parameter is at entry SP+148; the
    // function's 1520-byte frame accesses it at SP+1668.
    output = reader.Word(reader.Add(ctx.r1.u32, 148));
    const auto address = ctx.r4.u32;
    const auto size = ctx.r5.u32;
    if (size > 256 * 1024 * 1024) throw std::runtime_error("texture source exceeds size limit");
    const auto* bytes = reader.Bytes(address, size);
    image.assign(bytes, bytes + size);
  } catch (const std::exception& error) {
    REXLOG_ERROR("Native texture bridge: snapshot failed: {}", error.what());
  }
  {
    struct LoaderScope {
      LoaderScope() { ++texture_loader_depth; }
      ~LoaderScope() { --texture_loader_depth; }
    } scope;
    HookTiming original_timing(HookPhase::TextureOriginal);
    original(ctx, base);
  }
  if (image.empty() || ctx.r3.s32 < 0) return;
  auto& state = State();
  HookTiming lock_timing(HookPhase::TextureLock);
  // CreateNativeDdsTexture uses device resource creation with initial data,
  // not immediate-context uploads. Registry publication is serialized below;
  // it must not wait for the rendering submission barrier's refresh sleeps.
  std::lock_guard lock(state.mutex);
  lock_timing.Finish();
  try {
    const auto handle = reader.Word(output);
    if (!handle) throw std::runtime_error("guest texture creation returned null");
    // Erase first: a failed replacement must not leave an old native image
    // associated with an address the guest has reused for a new resource.
    state.textures.erase(handle);
    if (!state.device) throw std::runtime_error("native texture bridge not initialized");
    HookTiming create_timing(HookPhase::TextureCreate);
    auto native = CreateNativeDdsTexture(EnsureSceneBackendLocked(state), image);
    create_timing.Finish();
    ++state.texture_loads;
    REXLOG_INFO("Native texture bridge: handle={:#x}, {}x{}, mips={}, cube={}, loads={}",
                handle, native.width, native.height, native.mip_count, native.cube, state.texture_loads);
    state.textures.emplace(handle, std::move(native));
  } catch (const std::exception& error) {
    ++state.texture_errors;
    REXLOG_ERROR("Native texture bridge: {} (errors={})", error.what(), state.texture_errors);
  }
}

void RecordEmbeddedShader(const GuestReader& reader,uint32_t output,uint32_t source,bool pixel,uint32_t caller) {
  const auto handle=reader.Word(output);
  if (!handle) throw std::runtime_error("middleware shader creation returned null");
  reader.Bytes(source,4);
  auto& state=State();
  std::lock_guard lock(state.mutex);
  state.embedded_shaders.insert_or_assign(handle,EmbeddedShader{source,pixel});
  REXLOG_INFO("Native middleware shader source: handle={:#x}, source={:#x}, pixel={}, caller={:#x}",handle,source,pixel,caller);
}
Effect SnapshotEffect(const GuestReader& reader, uint32_t data) {
  Effect result;
  result.source = reader.String(reader.Add(data, reader.Word(reader.Add(data, 4))), 16 * 1024 * 1024);
  const auto count = reader.Word(reader.Add(data, 16));
  if (count > 4096) throw std::runtime_error("invalid guest shader count");
  const auto table = reader.Add(data, reader.Word(reader.Add(data, 20)));
  reader.Bytes(table, size_t(count) * 12);
  for (uint32_t i = 0; i < count; ++i) {
    const auto record = reader.Add(table, i * 12);
    const auto stage = reader.Word(record);
    if (stage > 1) throw std::runtime_error("invalid guest shader stage");
    result.entries.push_back({stage == 1,
      reader.String(reader.Add(record, reader.Word(reader.Add(record, 4))), 256),
      reader.String(reader.Add(record, reader.Word(reader.Add(record, 8))), 64)});
  }
  return result;
}

// One pool for the run. Created on first registration so a run that loads no
// shaders never starts a thread.
NativeDecodeWorkers& ShaderWorkers() {
  static NativeDecodeWorkers workers([] {
    const auto requested=REXCVAR_GET(edf_native_shader_workers);
    if(requested==0) return uint32_t(0);
    if(requested>0) return uint32_t((std::min)(requested,64));
    // One per core, less the one doing the loading, capped: compilation is
    // memory-bound enough that more threads stop helping well before the core
    // count on a large machine.
    const auto cores=std::thread::hardware_concurrency();
    return uint32_t((std::min)(cores>1?cores-1:1u,8u));
  }());
  return workers;
}

void RegisterShaders(const GuestReader& reader, uint32_t owner, const Effect& effect) {
  HookTiming registration_timing(HookPhase::ShaderRegistration);
  auto& state = State();
  HookTiming lock_timing(HookPhase::ShaderLock);
  // Compilation, device-only creation and registry replacement issue no
  // immediate-context commands. Keep registry serialization, not refresh waits.
  std::lock_guard lock(state.mutex);
  lock_timing.Finish();
  if (!state.device) throw std::runtime_error("native shader bridge not initialized");
  const auto count = reader.Word(reader.Add(owner, 8));
  if (count != effect.entries.size()) throw std::runtime_error("guest compiled shader count mismatch");
  const auto records = reader.Word(owner);
  reader.Bytes(records, size_t(count) * 72);
  // Guest memory is read here, on the calling thread, before anything is
  // handed to a worker: the reads must happen while the game is inside this
  // call, and a worker touching guest memory would be reading it at a time the
  // game never agreed to.
  std::vector<uint32_t> handles(count);
  for (uint32_t i = 0; i < count; ++i) {
    const auto& entry = effect.entries[i];
    const auto record = reader.Add(records, i * 72);
    if (reader.Word(reader.Add(record, 68)) != uint32_t(entry.pixel))
      throw std::runtime_error("guest compiled shader stage mismatch");
    handles[i] = reader.Word(reader.Add(record, entry.pixel ? 4 : 0));
    if (!handles[i]) throw std::runtime_error("guest compiled shader handle is null");
  }

  // Compilation is the part worth threading: each entry costs milliseconds and
  // a registration brings several, which is the batch the per-draw path does
  // not have. D3DCompile is thread-safe and ID3D11Device resource creation is
  // free-threaded; nothing below touches the immediate context or the bridge
  // state, both of which are not.
  std::vector<RegisteredShader> built(count);
  std::vector<std::exception_ptr> failures(count);
  auto& workers = ShaderWorkers();
  std::vector<uint64_t> tickets;
  tickets.reserve(count);
  const auto source_path = state.root / "Shader" / "guest.fx";
  auto* device = state.device.Get();
  for (uint32_t i = 0; i < count; ++i)
    tickets.push_back(workers.Submit([&, i] {
      HookTiming entry_timing(HookPhase::ShaderEntry);
      try {
        const auto& entry = effect.entries[i];
        auto shader = CompileNativeShader(*device, effect, entry, source_path);
        built[i] = RegisteredShader{owner, std::make_unique<ShaderBindings>(*device, std::move(shader))};
        built[i].source_fingerprint = EffectSourceFingerprint(effect.source);
        if (!entry.pixel)
          built[i].reversed_bindings = std::make_unique<ShaderBindings>(*device,
            CompileNativeShader(*device, effect, entry, source_path, true));
      } catch (...) {
        failures[i] = std::current_exception();
      }
    }));
  for (const auto ticket : tickets) workers.Wait(ticket);
  // Rethrown in entry order, so which entry is blamed does not depend on which
  // worker happened to finish first.
  for (uint32_t i = 0; i < count; ++i) if (failures[i]) std::rethrow_exception(failures[i]);

  std::unordered_map<uint32_t, RegisteredShader> fresh;
  for (uint32_t i = 0; i < count; ++i)
    if (!fresh.emplace(handles[i], std::move(built[i])).second)
      throw std::runtime_error("duplicate guest shader handle");
  // Handle equality is not shader-generation equality. The linked pair/active
  // ranges must not survive a replacement at the same guest address. Invalidate
  // before mutating the registry, including a handle reused by another owner.
  const bool replacing=std::any_of(state.shaders.begin(),state.shaders.end(),
    [&](const auto& item){return item.second.owner==owner || fresh.contains(item.first);});
  if(replacing) {
    state.active_vertex=state.linked_vertex=state.linked_pixel=0;
    state.active_vertex_parameters.reset();
    // Mesh caches independently compare owned bytecode identity on acquisition.
  }
  // Replace the entire owner on reload so address reuse cannot select old code.
  std::erase_if(state.shaders, [owner](const auto& item) { return item.second.owner == owner; });
  for (auto& [handle, shader] : fresh) state.shaders.insert_or_assign(handle, std::move(shader));
  REXLOG_INFO("Native shader bridge: owner={:#x}, {} guest shaders registered, {} resident",
              owner, count, state.shaders.size());
}
namespace {
// Fill in only what is resolved: a rejection before shader or declaration
// lookup still names a real contract, just a coarser one.
edf::native::NativeContract MakeNativeContract(Bridge& state,edf::native::NativeContractPath path,
    uint32_t vertex,uint32_t pixel,uint32_t declaration,uint32_t topology,
    uint32_t stride=0,uint32_t index_width=0) {
  edf::native::NativeContract contract;
  contract.path=path; contract.topology=topology;
  contract.stride=stride; contract.index_width=index_width;
  if(const auto found=state.shaders.find(vertex);found!=state.shaders.end() && found->second.bindings)
    contract.vertex_source=found->second.bindings->shader().source_fingerprint;
  if(const auto found=state.shaders.find(pixel);found!=state.shaders.end() && found->second.bindings)
    contract.pixel_source=found->second.bindings->shader().source_fingerprint;
  if(declaration) try {
    const auto owned=state.declarations.Get(declaration);
    contract.declaration=edf::native::HashNativeDeclaration(owned->bytes());
    contract.elements=owned->count();
    // Keep the bytes, not only the identity: the disc cannot supply them.
    state.contracts.RetainDeclaration(contract.declaration,owned->bytes());
  } catch(const std::exception&) { /* Unpublished declaration stays unresolved. */ }
  return contract;
}
void ReportNativeContractRejection(Bridge& state,const edf::native::NativeContract& contract,
    std::string_view reason) {
  state.contracts.set_limit(size_t(std::clamp(REXCVAR_GET(edf_native_contract_limit),1,1<<20)));
  if(!state.contracts.RecordRejected(contract,reason)) return;
  REXLOG_WARN("Native contract rejected: path={}, VS_source={:#x}, PS_source={:#x}, declaration={:#x}, elements={}, topology={}, stride={}, index_width={}, reason={} (this draw is missing from the frame)",
    edf::native::NativeContractPathName(contract.path),contract.vertex_source,contract.pixel_source,
    contract.declaration,contract.elements,contract.topology,contract.stride,contract.index_width,reason);
}
}
template<class Reader>
void UploadParameters(const Reader& reader, uint32_t instance, uint32_t stage_offset,
                      ShaderBindings& bindings, Bridge& state, ShaderBindings* alternate=nullptr) {
  // sub_821BBAD8 constructs each stage's 36-byte group. Local named records:
  // [name, data, register_count, register_index]. Globals:
  // [value_vector, name, register_count, register_index]. The activation path
  // sub_821B8E48 dereferences value_vector+0 to obtain the live global data.
  const auto owned=state.material_parameters.Get(instance);
  auto& shader=state.shaders.at(stage_offset?state.linked_pixel:state.linked_vertex);
  for (bool global : {false, true}) {
    const size_t group=(stage_offset?2:0)+(global?1:0);
    const auto& resolved=shader.ResolveParameters(owned,group,false);
    const auto* alternate_resolved=alternate?&shader.ResolveParameters(owned,group,true):nullptr;
    // One validated read covers every record in this group, so the per-record
    // reads inside it cost pointer arithmetic instead of a heap lookup each.
    // Payload pointers still fall through to the backing reader's validation:
    // they are scattered, and that check belongs exactly where it is.
    const auto record_base=owned->record_base[group];
    const auto record_bytes=owned->record_bytes[group];
    const auto upload=[&](const auto& source) {
    size_t parameter_index=0;
    for(const auto& parameter:(*owned)[group]) {
      const std::array<const ShaderBindings::FloatRegisterBinding*,2> targets{
        &resolved[parameter_index].binding,alternate_resolved?&(*alternate_resolved)[parameter_index].binding:nullptr};
      const bool canvas_xy=resolved[parameter_index].canvas_xy;
      ++parameter_index;
      const auto value=parameter.ReadValue(source,global);
      const auto& name=parameter.name;
      const auto registers=parameter.registers;
      std::array<ShaderBindings*,2> destinations{&bindings,alternate};
      std::array<size_t,2> sizes{};
      size_t maximum=0;
      for(size_t index=0;index<destinations.size();++index) {
        if(!destinations[index]) continue;
        size_t bytes=size_t(registers)*16;
        if(global) {
          // 821A1CA8 sizes the global float4 vector separately from the Xbox
          // compiler's per-material register footprint. +8 is its logical
          // register count. A native float4x4 may retain a fourth vector which
          // the Xbox compiler omitted; read owned source data, never pad it.
          const auto available=value.available;
          const auto required=targets[index]->bytes();
          if (!required) { ++state.optimized_out; destinations[index]=nullptr; continue; }
          if (available>4096 || required>size_t(available)*16)
            throw std::runtime_error("native global constant exceeds owned vector: "+name);
          bytes=required;
        }
        sizes[index]=bytes;
        maximum=(std::max)(maximum,bytes);
      }
      if(!destinations[0] && !destinations[1]) continue;
      const auto* data=source.Bytes(value.data,maximum);
      std::array<uint8_t,16> canvas_data{};
      if(canvas_xy && NativeRenderDimensions()[0]>0 && maximum==16) {
        canvas_data=ScaleNativeCanvasXY({data,16},float(NativeRenderDimensions()[0])/1280.0f,
          float(NativeRenderDimensions()[1])/720.0f);
        data=canvas_data.data();
      }
      for(size_t index=0;index<destinations.size();++index) if(destinations[index]) {
        if(destinations[index]->SetGuestFloatRegisters(*targets[index],{data,sizes[index]})) ++state.parameter_uploads;
        else ++state.optimized_out;
      }
    }
    };
    // Windowing the scattered payload reads as well was measured and did not
    // pay: the pixel stage improved 14% but the vertex stage lost 5% to the
    // extra pass, and activation.native did not move outside the 5% run-to-run
    // drift of the untouched phases. Not worth a second pass and a fallback in
    // this routine.
    if(record_base && record_bytes)
      upload(edf::native::GuestReadWindow(reader,record_base,record_bytes));
    else upload(reader);
  }
}
template<class Reader>
void UploadTextures(const Reader& reader, uint32_t instance, uint32_t device, ShaderBindings& bindings, Bridge& state) {
  bindings.ClearTextures();
  bindings.ClearSamplers();
  const auto owned=state.material_parameters.Get(instance);
  for (bool global : {false, true}) {
    const auto& resolved=state.shaders.at(state.linked_pixel).ResolveTextures(owned,global?1:0);
    size_t index=0;
    for(const auto& parameter:owned->textures[global?1:0]) {
      const auto& target=resolved[index++];
      const auto value=parameter.ReadValue(reader,global);
      const auto& name=parameter.name;
      const auto handle=value.handle;
      const auto found = state.textures.find(handle);
      auto* view = found == state.textures.end() || !found->second.content_valid ? nullptr : found->second.view.Get();
      if (!bindings.TrySetTexture(target, view)) {
        // Guest material records describe Xbox compiler usage. A native entry
        // can optimize a combined sampler out; it then has neither binding.
        // Do not let an unused record erase the material's remaining textures.
        if (bindings.TrySetSampler(target,nullptr))
          throw std::runtime_error("native combined sampler has no texture binding: " + name);
        continue;
      }
      // Read after the original activation has applied named filtering and
      // texture mip limits. Inline engine address changes are already present.
      const auto key=NativeFilteringKey(ReadSamplerWords(reader,device,value.slot),
                                        REXCVAR_GET(edf_native_anisotropic_filtering));
      auto cached = state.samplers.find(key);
      if (cached == state.samplers.end()) {
        const auto desc = DecodeNativeSampler(key);
        Microsoft::WRL::ComPtr<ID3D11SamplerState> native;
        if (FAILED(state.device->CreateSamplerState(&desc,&native))) throw std::runtime_error("native sampler creation failed");
        cached = state.samplers.emplace(key,std::move(native)).first;
        REXLOG_INFO("Native sampler: cached={}, address={}/{}/{}, filter={:#x}, lod={}..{}, bias={}",
                    state.samplers.size(),uint32_t(desc.AddressU),uint32_t(desc.AddressV),uint32_t(desc.AddressW),uint32_t(desc.Filter),desc.MinLOD,desc.MaxLOD,desc.MipLODBias);
      }
      if(!bindings.TrySetSampler(target,cached->second.Get()))
        throw std::runtime_error("unknown sampler: "+name);
      ++state.sampler_bindings;
      if (handle && !view) {
        ++state.texture_missing;
        if (state.texture_missing <= 10)
          REXLOG_INFO("Native texture bridge: pending resource name={}, handle={:#x}", name, handle);
        if (state.texture_missing <= 10) {
          const auto creation = state.texture_creations.find(handle);
          if (creation != state.texture_creations.end()) {
            const auto& c = creation->second;
            REXLOG_INFO("Native texture bridge: pending allocation {}x{}x{}, levels={}, usage={:#x}, format={:#x}, pool={}, type={}, caller={:#x}",
                        c.width, c.height, c.depth, c.levels, c.usage, c.format, c.pool, c.type, c.caller);
          }
        }
      } else ++state.texture_bindings;
    }
  }
}
void RegisterRenderTarget(const GuestReader& reader, uint32_t owner) {
  const auto format = reader.Word(reader.Add(owner,16));
  const auto surface_format = reader.Word(reader.Add(owner,20));
  // Verified post-processing pairs in sub_820B1028. Other pairs need their
  // component swizzles and resolve conversions recovered before enabling them.
  const bool luminance=format==0x2da2ab5e && surface_format==0x2da2aba4;
  const bool bloom=format==0x18280186 && surface_format==0x1a2201bf;
  const bool rgba16=(format==0x1a22ab60 || format==0x1a22ab5d) && surface_format==0x1a2201bf;
  if (!luminance && !bloom && !rgba16) return;
  auto& state = State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  const auto width = reader.Word(reader.Add(owner,24));
  const auto height = reader.Word(reader.Add(owner,28));
  const auto texture = reader.Word(reader.Add(owner,4));
  const auto surface = reader.Word(reader.Add(owner,12));
  if (!texture || !surface || !state.device) throw std::runtime_error("invalid native render target registration");
  auto native = luminance ? CreateNativeLuminanceTarget(*state.device.Get(),width,height) :
    bloom ? CreateNativeBloomTarget(*state.device.Get(),width,height) :
    CreateNativeRenderTarget(*state.device.Get(),width,height,DXGI_FORMAT_R16G16B16A16_FLOAT);
  if (luminance && width==1 && height==1) {
    // 8213B730 inserts the allocation address in the header's upper 20 bits
    // at +32. Snapshot at creation, before any GPU writes; never reread stale
    // CPU backing memory as if it were a later rendered history value.
    const auto allocation=reader.Word(reader.Add(texture,32))&0xfffff000u;
    try {
      const bool imported=ImportZeroLuminanceHistory(*state.context.Get(),native,
        {reader.Bytes(allocation,4096),4096});
      REXLOG_INFO("Native tone initial allocation: texture={:#x}, page={:#x}, uniform_zero_import={}",texture,allocation,imported);
    } catch (const std::exception& error) {
      REXLOG_INFO("Native tone initial allocation not imported: {}",error.what());
    }
  }
  state.textures.insert_or_assign(texture,native.sampled);
  state.render_targets.insert_or_assign(owner,RegisteredTarget{texture,surface,std::move(native)});
  REXLOG_INFO("Native render target: owner={:#x}, texture={:#x}, surface={:#x}, {}x{}",owner,texture,surface,width,height);
}
void BindActiveTarget(Bridge& state) {
  ++state.bind_generation;
  const auto found = state.render_targets.find(state.active_target);
  if (found == state.render_targets.end()) {
    const auto scene=state.scenes.find(state.active_scene);
    // An unsupported nested post-process scope must not fall through and
    // overwrite the scene. Only the outer scene scope may bind this fallback.
    if (!state.active_target && scene!=state.scenes.end()) {
      auto* view=scene->second.color.target.Get();
      state.context->OMSetRenderTargets(1,&view,scene->second.depth.target.Get());
    } else state.context->OMSetRenderTargets(0,nullptr,nullptr);
    if (!state.active_target && !state.active_scene && state.scenes.contains(state.active_output)) {
      auto* output=state.scenes.at(state.active_output).output.target.Get();
      state.context->OMSetRenderTargets(1,&output,nullptr);
    }
    return;
  }
  auto& target = found->second.native;
  auto* view = target.target.Get();
  state.context->OMSetRenderTargets(1,&view,nullptr);
  const D3D11_VIEWPORT viewport{0,0,float(target.sampled.width),float(target.sampled.height),0,1};
  state.context->RSSetViewports(1,&viewport);
}
void BeginRenderTarget(uint32_t owner) {
  auto& state = State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  state.target_stack.emplace_back(owner,state.active_target);
  state.active_target = owner;
  BindActiveTarget(state);
  ++state.target_begins;
}
void EndRenderTarget(uint32_t owner) {
  auto& state = State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  if (state.target_stack.empty() || state.target_stack.back().first != owner)
    throw std::runtime_error("unbalanced native render target scope");
  auto found = state.render_targets.find(owner);
  if (found != state.render_targets.end()) {
    // Unbind SRVs before writing the resolved resource. The next material
    // activation rebinds its own resources by name.
    ID3D11ShaderResourceView* empty[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
    state.context->PSSetShaderResources(0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,empty);
    state.context->VSSetShaderResources(0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,empty);
    auto& target = found->second;
    ResolveNativeRenderTarget(*state.context.Get(),target.native);
    state.textures.insert_or_assign(target.texture_handle,target.native.sampled);
    if (target.native.sampled.content_valid) ++state.target_resolves;
    else ++state.target_unwritten;
  }
  state.active_target = state.target_stack.back().second;
  state.target_stack.pop_back();
  BindActiveTarget(state);
  if (state.target_begins <= 5 || state.target_begins % 1000 == 0)
    REXLOG_INFO("Native render target: begins={}, valid_resolves={}, unwritten={}",
                state.target_begins,state.target_resolves,state.target_unwritten);
}
void ObserveActivation(const GuestReader& backing, uint32_t instance, uint32_t device) {
  // Per-stage vector headers, texture vectors and pass pointer share this
  // material object. Validate once, retaining no window beyond activation.
  const GuestReadWindow reader(backing,instance,112);
  const auto pass = reader.Word(reader.Add(instance, 108));
  const auto vertex = reader.Word(reader.Word(pass));
  const auto pixel = reader.Word(reader.Add(reader.Word(reader.Add(pass, 4)), 4));
  auto& state = State();
  // Sub-phases: activation is the most expensive per-call hook in gameplay and
  // nobody has measured which part of it that is. Lock wait is separated from
  // work because both scale differently with thread contention.
  edf::native::HookTiming lock_timing(edf::native::HookPhase::ActivationLock);
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  lock_timing.Finish();
  edf::native::HookTiming resolve_timing(edf::native::HookPhase::ActivationResolve);
  ++state.activations;
  // Repeat activations of one material with unchanged shaders are the cheapest
  // thing to skip if they dominate, so count them before optimising anything.
  if(instance==state.last_activation_instance && vertex==state.last_activation_vertex &&
     pixel==state.last_activation_pixel) ++state.repeat_activations;
  state.last_activation_instance=instance;
  state.last_activation_vertex=vertex; state.last_activation_pixel=pixel;
  if(state.activations%250000==0)
    REXLOG_INFO("Native activation repeats: activations={}, repeats_of_previous={} ({:.1f}% identical instance and shader pair as the immediately preceding activation)",
      state.activations,state.repeat_activations,100.0*double(state.repeat_activations)/double(state.activations));
  const bool found = state.shaders.contains(vertex) && state.shaders.contains(pixel);
  state.active_vertex = 0;
  state.active_vertex_parameters.reset();
  if (!found) ++state.misses;
  if (found) {
    try {
      auto& vs = *state.shaders.at(vertex).bindings;
      auto& reversed = *state.shaders.at(vertex).reversed_bindings;
      auto& ps = *state.shaders.at(pixel).bindings;
      if (state.linked_vertex != vertex || state.linked_pixel != pixel) {
        ValidateNativeShaderLink(vs.shader(),ps.shader());
        ValidateNativeShaderLink(reversed.shader(),ps.shader());
        state.linked_vertex = vertex; state.linked_pixel = pixel;
      }
      const auto vertex_ranges=state.shaders.at(vertex).ResolveVertexRanges(state.material_parameters.Get(instance));
      // A stage whose native shader consumes a Common.fx global that this
      // material never lists for that stage keeps the value our own HLSL
      // compilation baked in. Distinguish that from an upload the bridge drops:
      // the guest's own records decide a stage's parameter list.
      // Audit the first activation of each distinct shader pair only. The
      // per-stage parameter list belongs to the material's shaders, not to the
      // draw, so one sample per pair is complete, and gameplay activates
      // hundreds of thousands of times a second.
      if(const auto audit=REXCVAR_GET(edf_native_shared_constant_audit);
         audit>0 && state.shared_constant_pairs.size()<uint64_t(audit) &&
         state.shared_constant_pairs.insert({vertex,pixel}).second) {
        ++state.shared_constant_activations;
        const auto owned=state.material_parameters.Get(instance);
        const auto supplied=[&](size_t stage,const std::string& name) {
          for(size_t group=stage*2;group<stage*2+2;++group)
            for(const auto& parameter:(*owned)[group]) if(parameter.name==name) return true;
          return false;
        };
        // Where both stages are supplied, compare the storage each one names.
        // Identical source bytes with different shader values would be an
        // upload defect; different source addresses are the guest's own layout.
        const auto global_source=[&](size_t stage,const std::string& name)
            ->std::optional<std::array<uint32_t,3>> {
          for(const auto& parameter:(*owned)[stage*2+1]) {
            if(parameter.name!=name) continue;
            const auto node=GuestBlockWord(reader.Bytes(parameter.record,4));
            const auto value=parameter.ReadValue(reader,true);
            return std::array<uint32_t,3>{parameter.record,node,value.data};
          }
          return std::nullopt;
        };
        for(const auto* name:{"g_LightVector","g_LightDiffuse","g_HemiSphereVector",
            "g_HemiSphereColor1","g_HemiSphereColor2","g_FogParam","g_FogColor"}) {
          // Declaring a constant is not reading it: the reflection lists every
          // Common.fx global in the buffer, so a binding's existence says
          // nothing about whether this stage's pixels depend on the value.
          const bool vertex_used=vs.ConsumesConstant(name);
          const bool pixel_used=ps.ConsumesConstant(name);
          const bool vertex_supplied=supplied(0,name),pixel_supplied=supplied(1,name);
          if(vertex_supplied || pixel_supplied) {
            const auto vertex_source=global_source(0,name),pixel_source=global_source(1,name);
            if(vertex_used && pixel_used && vertex_supplied && pixel_supplied)
              ++state.shared_constant_both_supplied;
            if(vertex_source || pixel_source) {
              const std::array<uint32_t,3> storage_identity{vertex,pixel,uint32_t(name[2])*7+uint32_t(name[3])};
              if(state.shared_constant_storage_reported.size()<64 &&
                 state.shared_constant_storage_reported.insert(storage_identity).second) {
                const auto first=[&](uint32_t data)->float {
                  return data ? std::bit_cast<float>(GuestBlockWord(reader.Bytes(data,4))) : 0.f;
                };
                const auto vertex_data=vertex_source?(*vertex_source)[2]:0u;
                const auto pixel_data=pixel_source?(*pixel_source)[2]:0u;
                REXLOG_INFO("Native shared constant storage: name={}, VS={:#x} {} uses={} data={:#x} first={}, PS={:#x} {} uses={} data={:#x} first={}, same_data={}",
                  name,vertex,vs.shader().entry.name,vertex_used,vertex_data,first(vertex_data),
                  pixel,ps.shader().entry.name,pixel_used,pixel_data,first(pixel_data),
                  vertex_data && pixel_data && vertex_data==pixel_data);
              }
              if(vertex_source && pixel_source && (*vertex_source)[2]!=(*pixel_source)[2])
                ++state.shared_constant_split_storage;
            }
          }
          if((vertex_used && !vertex_supplied)||(pixel_used && !pixel_supplied)) {
            ++state.shared_constant_unsupplied;
            const std::array<uint32_t,3> identity{vertex,pixel,uint32_t(std::string_view(name).size()*131+name[2])};
            if(state.shared_constant_reported.size()<64 &&
               state.shared_constant_reported.insert(identity).second)
              REXLOG_WARN("Native shared constant unsupplied: name={}, VS={:#x} {} uses={} supplied={}, PS={:#x} {} uses={} supplied={}, source={:#x} (an unsupplied stage keeps the compiled source default)",
                name,vertex,vs.shader().entry.name,vertex_used,vertex_supplied,
                pixel,ps.shader().entry.name,pixel_used,pixel_supplied,
                vs.shader().source_fingerprint);
          }
        }
        const auto seen=state.shared_constant_activations;
        if(seen<=4 || (seen&(seen-1))==0)
          REXLOG_INFO("Native shared constant audit: pairs={}, unsupplied_uses={}, distinct_unsupplied={}, both_supplied={}, split_storage={} (one sample per distinct shader pair; split_storage counts pairs whose two stages name different guest value storage)",
            seen,state.shared_constant_unsupplied,state.shared_constant_reported.size(),
            state.shared_constant_both_supplied,state.shared_constant_split_storage);
      }
      resolve_timing.Finish();
      { edf::native::HookTiming vertex_params(edf::native::HookPhase::ActivationVertexParams);
        UploadParameters(reader, instance, 0, vs, state, &reversed); }
      { edf::native::HookTiming pixel_params(edf::native::HookPhase::ActivationPixelParams);
        UploadParameters(reader, instance, 36, ps, state); }
      edf::native::HookTiming texture_timing(edf::native::HookPhase::ActivationTextures);
      try { UploadTextures(reader, instance, device, ps, state); }
      catch (const std::exception& error) {
        ++state.texture_binding_errors;
        if (state.texture_binding_errors <= 10)
          REXLOG_ERROR("Native texture binding: {} (instance={:#x})", error.what(), instance);
        ps.ClearTextures();
        ps.ClearSamplers();
      }
      texture_timing.Finish();
      { edf::native::HookTiming bind_timing(edf::native::HookPhase::ActivationBind);
        vs.Bind(*state.context.Get());
        ps.Bind(*state.context.Get()); }
      state.active_vertex_parameters=vertex_ranges;
      state.active_vertex = vertex;
    } catch (const std::exception& error) {
      ++state.parameter_errors;
      if (state.parameter_errors <= 10)
        REXLOG_ERROR("Native parameter bridge: {} (instance={:#x})", error.what(), instance);
    }
  }
  if (state.activations <= 5 || state.activations % 10000 == 0)
    REXLOG_INFO("Native shader bridge: activation={}, VS={:#x}, PS={:#x}, resolved={}, misses={}, uploads={}, optimized_out={}, parameter_errors={}, texture_bindings={}, texture_missing={}, texture_binding_errors={}, sampler_bindings={}, sampler_states={}",
                state.activations, vertex, pixel, found, state.misses, state.parameter_uploads,
                state.optimized_out, state.parameter_errors, state.texture_bindings,
                state.texture_missing, state.texture_binding_errors,state.sampler_bindings,state.samplers.size());
}
}
void SetNativeMeshWatchAudit(std::weak_ptr<GuestMeshWatchAudit> audit) {
  auto& state=State();
  std::lock_guard lock(state.mutex);
  state.mesh_watch_audit=std::move(audit);
}
namespace {
std::array<float,4> ResolveBlendFactorForDraw(uint32_t device,
    const NativeRenderStateSnapshots::BlendWords* live) {
  auto& snapshots=State().render_state_snapshots;
  if(live) {
    const auto result=snapshots.CheckBlend(device,*live);
    const auto& count=snapshots.blend_counters();
    if(count.checks<=8 || !(count.checks&(count.checks-1)) ||
        (result!=NativeRenderStateSnapshots::Result::Match && count.missing+count.mismatches<=8))
      REXLOG_INFO("Native blend-factor audit: device={:#x}, checks={}, missing={}, mismatches={}",
        device,count.checks,count.missing,count.mismatches);
    if(REXCVAR_GET(edf_native_owned_render_state) && result!=NativeRenderStateSnapshots::Result::Match)
      throw std::runtime_error("native blend-factor audit mismatch");
  }
  const auto words=REXCVAR_GET(edf_native_owned_render_state)?snapshots.RequireBlend(device):*live;
  return {std::bit_cast<float>(words[0]),std::bit_cast<float>(words[1]),
          std::bit_cast<float>(words[2]),std::bit_cast<float>(words[3])};
}
}
void PublishNativeRenderState(uint8_t* base,uint32_t device,uint32_t producer) {
  if((!REXCVAR_GET(edf_native_render_state_audit) && !REXCVAR_GET(edf_native_owned_render_state)) ||
      !REXCVAR_GET(edf_native_shader_bridge)) return;
  auto& state=State();
  std::lock_guard lock(state.mutex);
  try {
    const auto fields=NativeRenderStateProducerFields(producer);
    const GuestReader reader(base);
    constexpr std::array<uint32_t,6> offsets{10424,10420,10440,10428,10332,11584};
    NativeRenderStateSnapshots::Words words{};
    for(size_t field=0;field<words.size();++field) if(fields&(1u<<field)) {
      auto word=reader.Word(reader.Add(device,offsets[field]));
      if(field==4) word&=15;
      if(field==5) word=uint32_t(word!=0);
      words[field]=word;
    }
    state.render_state_snapshots.Publish(device,words,producer,fields);
  } catch(const std::exception& error) {
    state.render_state_snapshots.Retire(device);
    REXLOG_ERROR("Native render-state publication failed: device={:#x}, producer={:#x}, error={}",device,producer,error.what());
  }
}
// Draw consumers already hold the bridge state lock. This diagnostic never
// repairs ownership; opt-in native consumption additionally rejects mismatches.
void AuditNativeRenderState(uint32_t device,const NativeRenderStateSnapshots::Words& live) {
  if(!REXCVAR_GET(edf_native_render_state_audit)) return;
  auto& snapshots=State().render_state_snapshots;
  const auto result=snapshots.Check(device,live);
  const auto& count=snapshots.counters();
  if(count.checks<=8 || !(count.checks&(count.checks-1)) ||
      (result==NativeRenderStateSnapshots::Result::Mismatch && count.mismatches<=8)) {
    const auto* saved=snapshots.Find(device);
    REXLOG_INFO("Native render-state ownership audit: device={:#x}, publications={}, checks={}, missing={}, mismatches={}, producer={:#x}, revision={}",
      device,count.publications,count.checks,count.missing,count.mismatches,saved?saved->producer:0,saved?saved->revision:0);
    if(result==NativeRenderStateSnapshots::Result::Mismatch && count.mismatches<=8)
      for(size_t field=0;field<live.size();++field) if(live[field]!=saved->words[field])
        REXLOG_INFO("Native render-state mismatch: field={}, owned={:#x}, live={:#x}, producer={:#x}",field,saved->words[field],live[field],saved->producers[field]);
    if(result==NativeRenderStateSnapshots::Result::Missing)
      REXLOG_INFO("Native render-state missing fields: valid_mask={:#x}",saved?saved->valid_fields:0);
  }
}
template <typename Reader>
NativeRenderStateSnapshots::Words ReadAuditedRenderStateWords(const Reader& reader,uint32_t device) {
  return ResolveNativeRenderState(State().render_state_snapshots,device,
    REXCVAR_GET(edf_native_owned_render_state),REXCVAR_GET(edf_native_render_state_audit),
    [&]{return ReadRenderStateWords(reader,device);},
    [&](const auto& live){AuditNativeRenderState(device,live);});
}
template <typename Reader>
GuestXuiDeviceWords ReadAuditedXuiDeviceWords(const Reader& reader,uint32_t device) {
  if(REXCVAR_GET(edf_native_owned_render_state))
    return ReadXuiDeviceWords(reader,device,ReadAuditedRenderStateWords(reader,device));
  auto snapshot=ReadXuiDeviceWords(reader,device);
  AuditNativeRenderState(device,snapshot.render);
  return snapshot;
}
template <typename Reader>
NativeViewportState ReadNativeDrawViewport(const Reader& reader,uint32_t device) {
  if(!REXCVAR_GET(edf_native_owned_render_state)) return ReadDrawViewport(reader,device);
  const auto render=ReadAuditedRenderStateWords(reader,device);
  return DecodeDrawViewport(ReadViewportWords(reader,device,render[5]!=0));
}
namespace {
// Builds the backend named by --edf_native_backend, once, with the state lock
// already held. One place knows how to do this; the public accessor below only
// adds the lock.
//
// Refused on an unknown name rather than falling back to whatever exists: an
// A/B run silently comparing a backend against itself would be worse than a
// failure to start.
edf::native::NativeRenderBackend& EnsureBackendLocked(Bridge& state) {
  if(state.backend) return *state.backend;
  const std::string name=REXCVAR_GET(edf_native_backend);
  if(name.empty()) throw std::runtime_error("a render backend was asked for but --edf_native_backend is empty");
  if(!state.device) throw std::runtime_error("a render backend was asked for before the renderer had a device");
  edf::native::RegisterNativeD3D11Backend();
  edf::native::RegisterNativeD3D12Backend();
  try {
    // "d3d11" means the device this renderer already has, not a second one: a
    // separate device could not share a texture or a target with the paths
    // that still draw through D3D11 directly, which is what lets the port
    // proceed one path at a time.
    state.backend=name=="d3d11"
      ? edf::native::AdoptNativeD3D11Backend(*state.device.Get(),*state.context.Get())
      : edf::native::CreateNativeRenderBackend(name);
  } catch(const std::exception& error) {
    std::string known;
    for(const auto& candidate:edf::native::NativeRenderBackendNames())
      known+=(known.empty()?"":", ")+candidate;
    REXLOG_ERROR("Native render backend: --edf_native_backend={} was refused: {}. Available: {}",
      name,error.what(),known.empty()?std::string("none"):known);
    throw;
  }
  REXLOG_INFO("Native render backend ready: name={}, recorders={}, parallel_recording={}; selected by --edf_native_backend={}. {}",
    std::string(state.backend->name()),state.backend->RecorderCount(),
    state.backend->SupportsParallelRecording(),name,
    name=="d3d11"?"Sharing this renderer's own device, so ported and unported paths draw into the same targets."
                 :"On its own device, so it cannot share targets with the paths that still draw through D3D11 directly.");
  for(const auto& message:state.backend->DrainValidationMessages())
    REXLOG_WARN("Native render backend validation: {}",message);
  return *state.backend;
}
// The backend the scene's own resources are created on.
//
// Separate from EnsureBackendLocked because during the port these are not the
// same backend: a texture created on a second device cannot be sampled by the
// draw paths that are still direct D3D11, so the scene's resources stay on the
// adopted backend - this renderer's own device - until the last path has moved.
edf::native::NativeRenderBackend& EnsureSceneBackendLocked(Bridge& state) {
  if(state.scene_backend) return *state.scene_backend;
  const std::string name=REXCVAR_GET(edf_native_scene_backend);
  if(name.empty()) throw std::runtime_error("the scene's resources were asked for but --edf_native_scene_backend is empty");
  if(!state.device) throw std::runtime_error("the scene's resources were asked for before the renderer had a device");
  edf::native::RegisterNativeD3D11Backend();
  edf::native::RegisterNativeD3D12Backend();
  state.scene_backend=name=="d3d11"
    ? edf::native::AdoptNativeD3D11Backend(*state.device.Get(),*state.context.Get())
    : edf::native::CreateNativeRenderBackend(name);
  REXLOG_INFO("Native scene backend ready: name={}; selected by --edf_native_scene_backend={}. {}",
    std::string(state.scene_backend->name()),name,
    name=="d3d11"?"This renderer's own device, so ported and unported draw paths share the same resources."
                 :"A separate device: every draw path that samples a scene resource must already be ported, or it will have nothing to bind.");
  for(const auto& message:state.scene_backend->DrainValidationMessages())
    REXLOG_WARN("Native scene backend validation: {}",message);
  return *state.scene_backend;
}
edf::native::NativeBackendRecorder& SceneRecorderLocked(Bridge& state) {
  auto& backend=EnsureSceneBackendLocked(state);
  if(!state.scene_frame_open) {
    backend.BeginFrame();
    state.scene_frame_open=true;
    ++state.scene_frames;
  }
  // Recorder 0: this renderer records the scene from one thread. Parallel
  // recording is a later question and a different one - it needs the draws to
  // be independent first, which is what moving them here is for.
  return backend.Recorder(0);
}
void SubmitSceneFrameLocked(Bridge& state) {
  if(!state.scene_frame_open) return;
  state.scene_frame_open=false;
  try {
    state.scene_backend->Submit();
  } catch(const std::exception& error) {
    // A frame that cannot be submitted is lost either way; what must not
    // happen is the next frame finding one still open and refusing to start.
    REXLOG_ERROR("Native scene frame submit: {} (frame {})",error.what(),state.scene_frames);
  }
  for(const auto& message:state.scene_backend->DrainValidationMessages())
    REXLOG_WARN("Native scene backend validation: {}",message);
}
}  // namespace

void InitializeGuestShaderBridge(const std::filesystem::path& game_root) {
  REXLOG_INFO("Native render-state consumption: owned={}, audit={}",
    REXCVAR_GET(edf_native_owned_render_state),REXCVAR_GET(edf_native_render_state_audit));
  if(REXCVAR_GET(edf_native_preview_window) && !REXCVAR_GET(edf_native_publish_frames))
    throw std::runtime_error("native preview requires native frame publication");
  if(REXCVAR_GET(edf_native_publish_frames) && !REXCVAR_GET(edf_native_shader_bridge))
    throw std::runtime_error("native frame publication requires native shader bridge");
  if(REXCVAR_GET(edf_native_validate_wait) &&
     (!REXCVAR_GET(edf_native_shader_bridge) || !REXCVAR_GET(edf_native_fence_probe)))
    throw std::runtime_error("native wait validation requires native shader bridge and fence probe");
  if (!REXCVAR_GET(edf_native_shader_bridge)) return;
  auto& state = State();
  std::lock_guard lock(state.mutex);
  state.root = game_root;
  // Native overlays may initialize the device before a game path/runtime
  // exists. The later OnPostSetup call supplies the root without replacing
  // a device already referenced by the host window and font textures.
  if(state.device) return;
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1,
                              D3D11_SDK_VERSION, &state.device, nullptr, &state.context)))
    throw std::runtime_error("native shader bridge: D3D11 device creation failed");
  REXLOG_INFO("Native shader bridge: initialized hardware D3D11 device");
  // The host window asks for a backend through this rather than calling into
  // the bridge, so it can still be built standalone by its own test.
  // A backend of its own for presentation, never the bridge's. The bridge's
  // may be the adopted D3D11 one, which shares this renderer's immediate
  // context - and presentation runs on the window thread outside the context
  // lock, so sharing it would drive that context from two threads.
  //
  // The flag is read here rather than in the host surface, which is also
  // built standalone by its own test and cannot see the bridge's cvars.
  edf::native::SetNativeHostBackendFactory([]() -> std::unique_ptr<edf::native::NativeRenderBackend> {
    if(!REXCVAR_GET(edf_native_backend_present)) return nullptr;
    const std::string name=REXCVAR_GET(edf_native_backend);
    if(name.empty()) return nullptr;
    edf::native::RegisterNativeD3D11Backend();
    edf::native::RegisterNativeD3D12Backend();
    return edf::native::CreateNativeRenderBackend(name);
  });
  REXLOG_INFO("Native render backend: --edf_native_backend={}; built on first use, so selecting one costs nothing until something draws through it. The renderer's own draw path is still direct D3D11 and does not use it yet",
    REXCVAR_GET(edf_native_backend).empty()?std::string("none"):REXCVAR_GET(edf_native_backend));
  if(REXCVAR_GET(edf_native_backend_preview)) {
    if(!REXCVAR_GET(edf_native_publish_frames))
      throw std::runtime_error("--edf_native_backend_preview needs --edf_native_publish_frames: it draws the frames the renderer publishes");
    // Built here, under the lock already held, through the same creator the
    // lazy accessor uses - so there is one place that knows how to build the
    // selected backend rather than two that can drift.
    edf::native::RegisterNativeD3D11Backend();
    edf::native::RegisterNativeD3D12Backend();
    state.backend_preview=std::make_unique<edf::native::NativeD3D12Preview>(
      REXCVAR_GET(edf_native_backend));
  }
  if(REXCVAR_GET(edf_native_publish_frames))
    state.presentation_frames=std::make_unique<NativeFrameHandoff>(*state.device.Get(),*state.context.Get());
}
NativeRenderBackend* EnsureNativeRenderBackend() {
  auto& state=State();
  std::lock_guard lock(state.mutex);
  if(state.backend) return state.backend.get();
  if(REXCVAR_GET(edf_native_backend).empty() || !state.device) return nullptr;
  return &EnsureBackendLocked(state);
}

bool VisitNativePresentationSharedFrame(NativeFrameHandoff::SharedFrame& shared,uint64_t& sequence) {
  auto& state=State();
  std::lock_guard lock(state.mutex);
  if(!state.presentation_frames) return false;
  shared=state.presentation_frames->Shared();
  sequence=state.presentation_frames->SharedSequence();
  return bool(shared);
}

bool VisitNativePresentationFrame(const NativeFrameHandoff::Consumer& consumer) {
  auto& state=State();
  HookTiming wait(HookPhase::PresentationContextWait);
  std::lock_guard lock(state.mutex);
  wait.Finish();
  return state.presentation_frames && state.presentation_frames->Visit(consumer);
}
bool VisitNativePresentationContext(
    const std::function<void(ID3D11Device&,ID3D11DeviceContext&)>& consumer) {
  auto& state=State();
  HookTiming wait(HookPhase::PresentationContextWait);
  std::lock_guard lock(state.mutex);
  wait.Finish();
  if(!state.presentation_frames) return false;
  state.presentation_frames->VisitContext(consumer);
  return true;
}
// Native mode is the only path permitted to publish guest GPU completion.
// Cursor and fence come from the same completed native event, never from the
// current issued value or an unsubmitted target. Caller holds the bridge lock.
void PublishNativeCompletion(const GuestReader& reader,Bridge& state,uint32_t device,bool allow_flush=false) {
  const auto found=state.completion_queues.find(device);
  if(found==state.completion_queues.end() || !found->second)
    throw std::runtime_error("native completion has no submitted queue");
  const auto completed=found->second->Poll(allow_flush);
  if(!completed) return;
  const auto published=state.native_published_completions.find(device);
  if(published!=state.native_published_completions.end() && published->second==*completed) return;
  const auto writeback=reader.Word(reader.Add(device,10768));
  reader.StoreWord(reader.Add(writeback,4),found->second->completed_cursor().value());
  reader.StoreWord(writeback,*completed);
  state.native_published_completions.insert_or_assign(device,*completed);
}
// Queries may finish while C410 is returning into C868, before the latter's
// busy increment. Defer dispatch across the entire enclosing CPU transaction.
class NativeSignalSubmissionScope {
 public:
  explicit NativeSignalSubmissionScope(uint32_t device):device_(device) {
    auto global=rex::thread::global_critical_region::AcquireDirect();
    auto& state=State();
    std::lock_guard lock(state.signal_delivery_mutex);
    state.signal_deliveries[device_].BeginSubmission();
  }
  ~NativeSignalSubmissionScope() {
    auto& state=State();
    std::lock_guard lock(state.signal_delivery_mutex);
    state.signal_deliveries.at(device_).EndSubmission();
  }
  NativeSignalSubmissionScope(const NativeSignalSubmissionScope&)=delete;
  NativeSignalSubmissionScope& operator=(const NativeSignalSubmissionScope&)=delete;
 private:
  uint32_t device_;
};
void PollNativeWorkerSignals(PPCContext& ctx,uint8_t* base,uint32_t device,bool allow_flush=false) {
  // Match interrupt-side serialization, without entering the Xbox GPU ISR or
  // borrowing/modifying a guest thread's PCR CPU/TLS state. Only the audited
  // EBA0 event contract is implemented here, not arbitrary guest callbacks.
  auto& state=State();
  bool needs_gpu_poll=true;
  {
    std::lock_guard delivery_lock(state.signal_delivery_mutex);
    const auto found=state.signal_deliveries.find(device);
    if(found!=state.signal_deliveries.end()) {
      if(found->second.submitting()) return;
      needs_gpu_poll=!found->second.pending();
    }
  }
  // An occupied CPU delivery slot needs no immediate-context access. Release
  // its lock before acquiring the renderer lock on the GPU polling path.
  if(needs_gpu_poll) {
    std::lock_guard lock(state.mutex);
    std::lock_guard delivery_lock(state.signal_delivery_mutex);
    auto& delivery=state.signal_deliveries[device];
    if(delivery.submitting()) return;
    if(!delivery.pending()) {
      const auto found=state.signal_queues.find(device);
      if(found==state.signal_queues.end()) return;
      const auto ready=found->second->PeekCompleted(1,allow_flush);
      if(ready.empty()) return;
      delivery.Enqueue(ready.front()); // Nonallocating handoff before retiring GPU ownership.
      found->second->AcknowledgeCompleted(1);
    }
  }
  // No renderer lock is held while acquiring the SDK lock or waking workers.
  auto global=rex::thread::global_critical_region::AcquireDirect();
  std::lock_guard delivery_lock(state.signal_delivery_mutex);
  auto& delivery=state.signal_deliveries.at(device);
  if(delivery.submitting() || !delivery.pending()) return;
  const GuestReader reader(base);
  // Keep ownership if the guest's single publication slot is occupied.
  if(reader.Word(reader.Add(device,10900))) return;
  if(reader.Word(reader.Word(0x82000720))!=device)
    throw std::runtime_error("native worker signal device no longer matches callback global");
  NativeSignal delivered{};
  const bool completed=delivery.Deliver([&](const NativeSignal& signal,uint32_t cpu) {
    if(signal.callback!=0x8214EBA0) throw std::runtime_error("unsupported native worker callback");
      // EBA0 publishes its command-list argument before waking the selected
      // worker. The worker, not this producer, executes jobs and retires busy.
      reader.StoreWord(reader.Add(device,10900),signal.argument);
      auto event_context=ctx;
      event_context.r3.u64=reader.Add(device,11228+cpu*56);
      event_context.r4.u64=1;
      event_context.r5.u64=0;
      __imp__KeSetEvent(event_context,base);
      delivered=signal;
  });
  if(completed) {
    static std::atomic<uint64_t> dispatched{0};
    const auto count=dispatched.fetch_add(1,std::memory_order_relaxed)+1;
    if(count<=8 || !(count&(count-1)))
      REXLOG_INFO("Native worker signal completed: count={}, device={:#x}, argument={:#x}, CPUs={:#x}, busy={}",
        count,device,delivered.argument,delivered.cpu_mask,reader.Word(reader.Add(device,10868)));
  }
}
}

namespace {
void ObserveAudioOutputPages(uint8_t* base,uint32_t address,bool indirect,const char* phase) {
  if(!REXCVAR_GET(edf_native_mesh_watch_audit)) return;
  auto& state=edf::native::State();
  std::lock_guard lock(state.mutex);
  auto audit=state.mesh_watch_audit.lock();
  if(!audit) return;
  try {
    const edf::native::GuestReader reader(base);
    const auto owner=indirect ? reader.Word(address) : address;
    const auto ranges=edf::native::VisitGuestAudioOutputRanges(reader,owner,
      [&](uint32_t physical,uint32_t length) { audit->ExcludePhysical(physical,length); });
    static uint64_t observed=0;
    if(++observed<=3 || (observed&(observed-1))==0)
      REXLOG_INFO("Native mesh watch audio exclusion: observations={}, ranges={}, phase={} (conservative shadow-only pages)",observed,ranges,phase);
  } catch(const std::exception& error) {
    audit->Disable();
    REXLOG_ERROR("Native mesh watch audit disabled after audio exclusion failure: {}",error.what());
  }
}
}
// Capture registration inputs rather than reading callback fields after the
// call: another worker can replace them immediately. This is workload evidence,
// not an ownership proof, and never changes the guest registration or arguments.
REX_EXTERN(__imp__sub_8243A000);
REX_HOOK_RAW(sub_8243A000) {
  if(!REXCVAR_GET(edf_native_worker_callback_audit)) {
    __imp__sub_8243A000(ctx,base);
    return;
  }
  const uint32_t worker=ctx.r3.u32, mode=ctx.r4.u32;
  const uint32_t callback=ctx.r5.u32, context=ctx.r6.u32;
  const uint32_t return_address=static_cast<uint32_t>(ctx.lr);
  __imp__sub_8243A000(ctx,base);
  static std::atomic<uint64_t> registrations{0};
  const auto sequence=registrations.fetch_add(1,std::memory_order_relaxed)+1;
  if(sequence<=256) {
    REXLOG_INFO("Native worker callback registration: sequence={}, worker={:#x}, mode={}, callback={:#x}, context={:#x}, caller_return={:#x} (inputs; observed workload only)",
                sequence,worker,mode,callback,context,return_address);
  } else if(sequence==257) {
    REXLOG_WARN("Native worker callback audit truncated: registration limit 256 reached; later registrations are not logged");
  }
}

// Retain construction coverage for deferred contexts, and observe every pending
// and live output before 1F88 copies records / signals the decoder. No guest
// registers or original audio behavior are changed by these shadow hooks.
REX_EXTERN(__imp__sub_823C1108);
REX_HOOK_RAW(sub_823C1108) {
  const uint32_t output=ctx.r6.u32;
  __imp__sub_823C1108(ctx,base);
  if(!ctx.r3.u32) ObserveAudioOutputPages(base,output,true,"constructed");
}
REX_EXTERN(__imp__sub_823C1F88);
REX_HOOK_RAW(sub_823C1F88) {
  ObserveAudioOutputPages(base,ctx.r3.u32,false,"before_decode");
  __imp__sub_823C1F88(ctx,base);
}

// Observational resource-worker boundaries. The persistent coordinator/helper
// are not BeginLoading/EndLoading transactions; preserve their original calls.
namespace {
class ScriptLoadTiming {
 public:
  ScriptLoadTiming(const char* api,uint32_t mode,uint32_t index,bool active=true)
      : api_(api),mode_(mode),index_(index),enabled_(active && REXCVAR_GET(edf_native_load_timings)) {
    if(enabled_) start_=std::chrono::steady_clock::now();
  }
  ~ScriptLoadTiming() {
    if(!enabled_) return;
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start_).count();
    if(ms>=1.0) REXLOG_INFO("Native script timing: api={} mode={} index={} total_ms={} (inclusive, calls below 1ms omitted)",
                            api_,mode_,index_,ms);
  }
 private:
  const char* api_;
  uint32_t mode_,index_;
  bool enabled_;
  std::chrono::steady_clock::time_point start_{};
};
thread_local uint32_t map_load_depth=0;
}
REX_EXTERN(__imp__sub_820CBD28);
REX_HOOK_RAW(sub_820CBD28) {
  struct Scope {
    Scope() { ++map_load_depth; }
    ~Scope() { --map_load_depth; }
  } scope;
  __imp__sub_820CBD28(ctx,base);
}
// Inclusive nested timings, restricted to this thread's LoadMap call chain.
// Address labels avoid assigning unverified semantic names to guest helpers.
#define EDF_MAP_TIMED_HOOK(address) \
  REX_EXTERN(__imp__sub_##address); \
  REX_HOOK_RAW(sub_##address) { \
    ScriptLoadTiming timing("map." #address,0,0,map_load_depth!=0); \
    __imp__sub_##address(ctx,base); \
  }
EDF_MAP_TIMED_HOOK(821B8090)
EDF_MAP_TIMED_HOOK(820BE628)
EDF_MAP_TIMED_HOOK(820B4D58)
EDF_MAP_TIMED_HOOK(820B3B00)
EDF_MAP_TIMED_HOOK(821A6158)
EDF_MAP_TIMED_HOOK(821B7278)
EDF_MAP_TIMED_HOOK(821B2850)
EDF_MAP_TIMED_HOOK(821ACBD0)
EDF_MAP_TIMED_HOOK(820ADD28)
EDF_MAP_TIMED_HOOK(821B24C0)
EDF_MAP_TIMED_HOOK(820B77A0)
EDF_MAP_TIMED_HOOK(820B6458)
EDF_MAP_TIMED_HOOK(820B6FA8)
EDF_MAP_TIMED_HOOK(821A6278)
EDF_MAP_TIMED_HOOK(820B4858)
EDF_MAP_TIMED_HOOK(821C7B20)
EDF_MAP_TIMED_HOOK(821C7D80)
EDF_MAP_TIMED_HOOK(821C7E30)
EDF_MAP_TIMED_HOOK(821ACAD8)
EDF_MAP_TIMED_HOOK(821AB708)
EDF_MAP_TIMED_HOOK(821D6448)
EDF_MAP_TIMED_HOOK(821D6AE0)
EDF_MAP_TIMED_HOOK(821D6C20)
EDF_MAP_TIMED_HOOK(821AA130)
EDF_MAP_TIMED_HOOK(821AAB70)
EDF_MAP_TIMED_HOOK(821B5568)
EDF_MAP_TIMED_HOOK(821AB520)
EDF_MAP_TIMED_HOOK(821CB6A0)
EDF_MAP_TIMED_HOOK(821CB550)
EDF_MAP_TIMED_HOOK(821D88C0)
EDF_MAP_TIMED_HOOK(821DB008)
EDF_MAP_TIMED_HOOK(821D85B8)
EDF_MAP_TIMED_HOOK(821D7C18)
EDF_MAP_TIMED_HOOK(821D7850)
EDF_MAP_TIMED_HOOK(821D79D8)
EDF_MAP_TIMED_HOOK(821D8770)
EDF_MAP_TIMED_HOOK(821B3C98)
#undef EDF_MAP_TIMED_HOOK
REX_EXTERN(__imp__sub_820C7220);
REX_HOOK_RAW(sub_820C7220) {
  ScriptLoadTiming timing("menu",ctx.r6.u32,ctx.r7.u32);
  __imp__sub_820C7220(ctx,base);
}
REX_EXTERN(__imp__sub_820D1518);
REX_HOOK_RAW(sub_820D1518) {
  ScriptLoadTiming timing("mission",ctx.r6.u32,ctx.r7.u32);
  __imp__sub_820D1518(ctx,base);
}
REX_EXTERN(__imp__sub_821A4170);
REX_HOOK_RAW(sub_821A4170) {
  const bool trace=REXCVAR_GET(edf_native_load_timings);
  const uint32_t manager=ctx.r3.u32,mode=ctx.r4.u32;
  if(trace) REXLOG_INFO("Native load request: begin manager={:#x} mode={} caller={:#x}",manager,mode,ctx.lr);
  __imp__sub_821A4170(ctx,base);
  if(trace) REXLOG_INFO("Native load request: armed manager={:#x}",manager);
}
REX_EXTERN(__imp__sub_821A41E8);
REX_HOOK_RAW(sub_821A41E8) {
  const bool trace=REXCVAR_GET(edf_native_load_timings);
  const uint32_t manager=ctx.r3.u32;
  if(trace) REXLOG_INFO("Native load request: finish manager={:#x} caller={:#x}",manager,ctx.lr);
  __imp__sub_821A41E8(ctx,base);
  if(trace) REXLOG_INFO("Native load request: disarmed manager={:#x}",manager);
}
REX_EXTERN(__imp__sub_821A4BA0);
REX_HOOK_RAW(sub_821A4BA0) {
  edf::native::HookTiming timing(edf::native::HookPhase::ResourceCoordinator);
  __imp__sub_821A4BA0(ctx,base);
}
REX_EXTERN(__imp__sub_821A5080);
REX_HOOK_RAW(sub_821A5080) {
  edf::native::HookTiming timing(edf::native::HookPhase::ResourceHelper);
  __imp__sub_821A5080(ctx,base);
}
REX_EXTERN(__imp__sub_821A4FC0);
REX_HOOK_RAW(sub_821A4FC0) {
  const bool trace=REXCVAR_GET(edf_native_load_timings);
  const uint32_t manager=ctx.r3.u32;
  if(trace) REXLOG_INFO("Native resource worker: begin manager={:#x}",manager);
  if(trace) {
    // Snapshot identities only; do not invoke callbacks or replace the original
    // traversal. r4 owns the list, while r3 supplies the slot-6 argument.
    // The bounded snapshot is diagnostic, not an ownership certificate.
    try {
      const edf::native::GuestReader reader(base);
      const uint32_t list_owner=ctx.r4.u32;
      const uint32_t owner=reader.Word(reader.Add(list_owner,132));
      const uint32_t owner_vtable=reader.Word(owner);
      REXLOG_INFO("Native resource callbacks: manager={:#x} owner={:#x} enter={:#x} leave={:#x}",
        manager,owner,reader.Word(reader.Add(owner_vtable,32)),reader.Word(reader.Add(owner_vtable,36)));
      const uint32_t sentinel=reader.Word(reader.Add(list_owner,2232));
      uint32_t node=reader.Word(sentinel);
      uint32_t count=0;
      for(;node!=sentinel && count<256;++count) {
        const uint32_t object=reader.Word(reader.Add(node,12));
        const uint32_t vtable=reader.Word(object);
        REXLOG_INFO("Native resource callback: manager={:#x} index={} object={:#x} vtable={:#x} slot6={:#x}",
          manager,count,object,vtable,reader.Word(reader.Add(vtable,24)));
        node=reader.Word(node);
      }
      if(node!=sentinel) REXLOG_WARN("Native resource callback snapshot truncated at {} records",count);
    } catch(const std::exception& error) {
      REXLOG_WARN("Native resource callback snapshot unavailable: {}",error.what());
    }
  }
  edf::native::HookTiming timing(edf::native::HookPhase::ResourceOneShot);
  __imp__sub_821A4FC0(ctx,base);
  timing.Finish();
  if(trace) REXLOG_INFO("Native resource worker: end manager={:#x}",manager);
}
REX_EXTERN(__imp__sub_821A4DE8);
REX_HOOK_RAW(sub_821A4DE8) {
  const bool trace=REXCVAR_GET(edf_native_load_timings);
  const uint32_t manager=ctx.r3.u32;
  // These are the same valid manager fields immediately read by the original.
  // Snapshot before the call: desired/actual are not changed by instrumentation.
  uint32_t actual=0,desired=0;
  if(trace) {
    try {
      const edf::native::GuestReader reader(base);
      const auto* fields=reader.Bytes(manager+2261,2);
      actual=fields[0]; desired=fields[1];
    } catch(const std::exception& error) {
      REXLOG_WARN("Native resource transition snapshot unavailable: {}",error.what());
    }
  }
  const bool edge=trace && actual!=desired;
  if(edge) REXLOG_INFO("Native resource transition: begin manager={:#x} actual={} desired={}",
                      manager,actual,desired);
  edf::native::HookTiming timing(edf::native::HookPhase::ResourceTransition,edge);
  __imp__sub_821A4DE8(ctx,base);
  timing.Finish();
  if(edge) REXLOG_INFO("Native resource transition: end manager={:#x}",manager);
}

REX_EXTERN(__imp__sub_82142050);
REX_EXTERN(__imp__sub_82142130);
REX_EXTERN(__imp__edf_native_gamma_table_cpu_tail);
REX_EXTERN(__imp__edf_native_gamma_pwl_cpu_tail);
namespace {
void CaptureNativeDisplayGamma(uint8_t* base,uint32_t device,uint32_t address,
    edf::native::NativeDisplayGamma::Mode mode) {
  const edf::native::GuestReader reader(base);
  auto gamma=edf::native::NativeDisplayGamma::Decode({reader.Bytes(address,1536),1536},mode);
  auto& state=edf::native::State();
  std::lock_guard lock(state.mutex);
  // Device construction may upload its default ramp before publishing the
  // SDK global device pointer. Track explicit ownership, cleared by reset.
  if(state.display_gamma_device && state.display_gamma_device!=device)
    throw std::runtime_error("native gamma update changed display device without reset");
  state.display_gamma=std::move(gamma);
  state.display_gamma_device=device;
  static std::atomic<uint64_t> updates{0};
  const auto count=updates.fetch_add(1)+1;
  if(count<=4 || !(count&(count-1)))
    REXLOG_INFO("Native display gamma captured: count={}, device={:#x}, mode={}",count,device,int(mode));
}
}
REX_HOOK_RAW(sub_82142050) {
  if(REXCVAR_GET(edf_native_host)) {
    CaptureNativeDisplayGamma(base,ctx.r3.u32,ctx.r4.u32,edf::native::NativeDisplayGamma::Mode::Table256);
    __imp__edf_native_gamma_table_cpu_tail(ctx,base);
    return;
  }
  __imp__sub_82142050(ctx,base);
}
REX_HOOK_RAW(sub_82142130) {
  if(REXCVAR_GET(edf_native_host)) {
    CaptureNativeDisplayGamma(base,ctx.r3.u32,ctx.r4.u32,edf::native::NativeDisplayGamma::Mode::Piecewise128);
    __imp__edf_native_gamma_pwl_cpu_tail(ctx,base);
    return;
  }
  __imp__sub_82142130(ctx,base);
}
REX_EXTERN(__imp__sub_821B6880);
REX_HOOK_RAW(sub_821B6880) {
  if (!REXCVAR_GET(edf_native_shader_bridge)) { __imp__sub_821B6880(ctx, base); return; }
  const uint32_t owner = ctx.r3.u32;
  edf::native::ForgetOwner(owner);
  edf::native::Effect effect;
  bool captured = false;
  try {
    effect = edf::native::SnapshotEffect(edf::native::GuestReader(base), ctx.r4.u32);
    captured = true;
  } catch (const std::exception& error) { REXLOG_ERROR("Native shader bridge: {}", error.what()); }
  __imp__sub_821B6880(ctx, base);
  if (captured && ctx.r3.u8) {
    try { edf::native::RegisterShaders(edf::native::GuestReader(base), owner, effect); }
    catch (const std::exception& error) { REXLOG_ERROR("Native shader bridge: {}", error.what()); }
  }
}
REX_EXTERN(__imp__sub_821B8E48);
REX_HOOK_RAW(sub_821B8E48) {
  const auto instance = ctx.r3.u32, device = ctx.r4.u32;
  {
    edf::native::HookTiming timing(edf::native::HookPhase::ActivationGuest);
    __imp__sub_821B8E48(ctx, base);
  }
  if (REXCVAR_GET(edf_native_shader_bridge)) {
    edf::native::HookTiming timing(edf::native::HookPhase::ActivationNative);
    try { edf::native::ObserveActivation(edf::native::GuestReader(base), instance, device); }
    catch (const std::exception& error) { REXLOG_ERROR("Native shader bridge: {}", error.what()); }
  }
}

// The instanced mesh loop at 821D97C4 uploads these overrides after material
// activation, immediately before each indexed draw. Preserve that ordering.
REX_EXTERN(__imp__sub_821D9600);
REX_HOOK_RAW(sub_821D9600) {
  const auto list=ctx.r3.u32, device=ctx.r4.u32;
  {
    edf::native::HookTiming timing(edf::native::HookPhase::InstanceGuest);
    __imp__sub_821D9600(ctx,base);
  }
  if(!REXCVAR_GET(edf_native_shader_bridge)) return;
  edf::native::HookTiming timing(edf::native::HookPhase::InstanceNative);
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  try {
    const edf::native::GuestReader reader(base);
    if(!state.active_vertex || !state.active_vertex_parameters || state.shader_bindings.Vertex(device)!=state.active_vertex) return;
    auto& shader=state.shaders.at(state.active_vertex);
    // A single instanced shader variant can only serve a run whose instances
    // all patch the same register range. Fingerprint the shape (not the data)
    // so the indexed hook can tell whether a collapsible run is also uniform.
    uint64_t shape=1469598103934665603ull;
    for(const auto& entry:edf::native::ReadInstanceParameters(reader,list)) {
      for(const auto word:{entry.first,entry.count}) {
        shape^=word; shape*=1099511628211ull;
      }
    }
    state.instance_shape=shape;
    for(const auto& [data,first,count]:edf::native::ReadInstanceParameters(reader,list)) {
      for(const auto& parameter:*state.active_vertex_parameters) {
        const auto low=(std::max)(first,parameter.first),high=(std::min)(first+count,parameter.first+parameter.count);
        if(low>=high) continue;
        const size_t bytes=size_t(high-low)*16;
        const auto* source=reader.Bytes(reader.Add(data,(low-first)*16),bytes);
        shader.bindings->PatchGuestFloatRegisters(parameter.normal,low-parameter.first,{source,bytes});
        shader.reversed_bindings->PatchGuestFloatRegisters(parameter.reversed,low-parameter.first,{source,bytes});
        ++state.instance_parameter_updates;
        if(state.instance_parameter_updates<=12)
          REXLOG_INFO("Native instance constant: name={}, register={}, count={}",parameter.name,low,high-low);
      }
    }
  } catch(const std::exception& error) {
    ++state.instance_parameter_errors;
    state.active_vertex=0; // Never submit a draw with partially applied overrides.
    state.active_vertex_parameters.reset();
    if(state.instance_parameter_errors<=10) REXLOG_ERROR("Native instance constant: {}",error.what());
  }
}

// Sideband observation only: the original producer still owns guest counters.
// Each event follows the native draws already submitted at this guest boundary.
REX_EXTERN(__imp__sub_8213C788);
REX_HOOK_RAW(sub_8213C788) {
  const auto device=ctx.r3.u32;
  if(REXCVAR_GET(edf_native_host)) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    const edf::native::GuestReader reader(base);
    const auto issued=reader.Word(reader.Add(device,10780));
    const auto cursor=reader.Word(reader.Add(device,40));
    const auto flags=reader.Word(reader.Add(device,13500));
    const auto next=reader.Add(ctx.r4.u32,40);
    auto& queue=state.completion_queues[device];
    if(!queue) queue=std::make_unique<edf::native::NativeCompletionQueue>(*state.device.Get(),*state.context.Get());
    edf::native::PublishNativeCompletion(reader,state,device);
    queue->Capture(edf::native::NativeSignalCommandAddress(ctx.r4.u32),40,issued,cursor|(flags&3));
    // Preserve CPU-side bookkeeping and reservation size while the remaining
    // command-buffer callers are migrated. No PM4 packet or premature guest
    // completion write from the retail producer is retained in native mode.
    reader.StoreWord(reader.Add(device,12956),flags);
    reader.StoreWord(reader.Add(device,12952),cursor);
    reader.StoreWord(reader.Add(device,10780),issued+2);
    ctx.r3.u64=next;
    if(++state.completion_submits<=8)
      REXLOG_INFO("Native fence ownership: captured={}, cursor={:#x}, device={:#x}; awaits actual submission, no GPU packet emitted",issued,cursor|(flags&3),device);
    return;
  }
  const bool probe=REXCVAR_GET(edf_native_shader_bridge) && REXCVAR_GET(edf_native_fence_probe);
  std::optional<uint32_t> issued;
  if(probe) {
    try {
      const edf::native::GuestReader reader(base);
      issued=reader.Word(reader.Add(device,10780));
    }
    catch(const std::exception& error) { REXLOG_ERROR("Native fence probe input: {}",error.what()); }
  }
  __imp__sub_8213C788(ctx,base);
  if(!probe || !issued) return;
  auto& state=edf::native::State();
  std::lock_guard lock(state.mutex);
  if(state.completion_faults.contains(device)) return;
  try {
    const edf::native::GuestReader reader(base);
    if(reader.Word(reader.Add(device,10780))!=uint32_t(*issued+2))
      throw std::runtime_error("guest fence producer did not advance by two");
    auto& queue=state.completion_queues[device];
    if(!queue) queue=std::make_unique<edf::native::NativeCompletionQueue>(*state.device.Get(),*state.context.Get());
    const auto completed=queue->Poll();
    queue->Submit(*issued);
    if(++state.completion_submits<=4 || state.completion_submits%128==0)
      REXLOG_INFO("Native fence probe: device={:#x}, submitted={}, completed_valid={}, completed={}, pending={}; guest counters unchanged",
        device,*issued,completed.has_value(),completed.value_or(0),queue->pending());
  } catch(const std::exception& error) {
    state.completion_faults.insert(device);
    REXLOG_ERROR("Native fence probe disabled for device {:#x}: {}",device,error.what());
  }
}
REX_EXTERN(__imp__sub_8213D298);
REX_EXTERN(__imp__edf_native_swap_wait_cpu_tail);
REX_HOOK_RAW(sub_821512D8) {
  __imp__edf_native_swap_wait_cpu_tail(ctx,base);
}
REX_EXTERN(edf_native_swap_wait) {
  using namespace edf::native;
  const GuestReader reader(base);
  const auto device=ctx.r3.u32;
  const auto mode=reader.Word(reader.Add(device,13220));
  if(mode!=0 && mode!=1 && mode!=2 && mode!=4)
    throw std::runtime_error("unsupported native swap interval");
  const uint32_t interval=mode==4?3:mode==2?2:1;
  const uint32_t phase_limit=(reader.Word(reader.Add(device,11580))>>23)&127;
  auto& state=State();
  // Keep later game commands behind this barrier, but let host presentation
  // and CPU worker bookkeeping take the context lock between short polls.
  std::lock_guard submission(state.submissions);
  std::unique_lock lock(state.mutex);
  if(!state.device || !state.context)
    throw std::runtime_error("native swap requires an initialized native device");
  if(reader.Word(reader.Add(device,15120)))
    throw std::runtime_error("native swap has an unaudited vblank callback");
  // Everything this frame recorded into the scene backend goes now, before the
  // barrier below waits for the GPU: submitting after the wait would put this
  // frame's work behind the wait that was meant to cover it.
  SubmitSceneFrameLocked(state);
  auto [it,inserted]=state.swap_clocks.try_emplace(device);
  auto& timing=it->second;
  if(inserted) timing.clock.Reset(NativePacingClock::Clock::now(),0);
  NativeCompletionQueue barrier(*state.device.Get(),*state.context.Get(),1);
  HookTiming gpu_timing(HookPhase::SwapGpuWait);
  barrier.Submit(2);
  const auto gpu_deadline=NativePacingClock::Clock::now()+std::chrono::seconds(10);
  while(barrier.Poll(true)!=2) {
    if(NativePacingClock::Clock::now()>=gpu_deadline)
      throw std::runtime_error("native swap GPU completion timed out");
    lock.unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    lock.lock();
  }
  gpu_timing.Finish();
  HookTiming refresh_timing(HookPhase::SwapRefreshWait);
  const bool audit_pacing=REXCVAR_GET(edf_native_hook_timings);
  double pacing_sleep_ms=0, pacing_lock_ms=0;
  uint32_t pacing_polls=0;
  NativeSwapPacingState pacing{
    reader.Word(reader.Add(device,15124)),reader.Word(reader.Add(device,15128)),
    reader.Word(reader.Add(device,15132)),reader.Word(reader.Add(device,15136))};
  auto now=NativePacingClock::Clock::now();
  auto ticks=timing.clock.Sample(now);
  pacing.Advance(ticks-timing.sampled);
  timing.sampled=ticks;
  const auto entry_phase=timing.clock.PhasePercent(now);
  const auto entry_ticks=pacing.ticks, entry_ack=pacing.acknowledged;
  bool released=pacing.CompleteNative(interval);
  // Publish the callback increment once, before sleeping. A statistics reader
  // may reset this counter during the refresh wait; do not overwrite its reset.
  reader.StoreWord(reader.Add(device,15136),pacing.callbacks);
  const auto pacing_deadline=now+std::chrono::seconds(1);
  while(!released) {
    lock.unlock();
    const auto sleep_start=audit_pacing?NativePacingClock::Clock::now():NativePacingClock::Clock::time_point{};
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto sleep_end=audit_pacing?NativePacingClock::Clock::now():NativePacingClock::Clock::time_point{};
    lock.lock();
    now=NativePacingClock::Clock::now();
    if(audit_pacing) {
      pacing_sleep_ms+=std::chrono::duration<double,std::milli>(sleep_end-sleep_start).count();
      pacing_lock_ms+=std::chrono::duration<double,std::milli>(now-sleep_end).count();
      ++pacing_polls;
    }
    ticks=timing.clock.Sample(now);
    released=pacing.Advance(ticks-timing.sampled);
    timing.sampled=ticks;
    if(!released && now>=pacing_deadline)
      throw std::runtime_error("native swap pacing state failed to reach its deadline");
  }
  reader.StoreWord(reader.Add(device,15124),pacing.ticks);
  reader.StoreWord(reader.Add(device,15128),pacing.acknowledged);
  reader.StoreWord(reader.Add(device,15132),pacing.pending);
  // No GPU writeback address, callback packet or register acknowledgement.
  static std::atomic<uint64_t> calls{};
  const auto count=calls.fetch_add(1,std::memory_order_relaxed)+1;
  if(audit_pacing && count%256==0)
    REXLOG_INFO("Native swap pacing detail: count={}, mode={}, interval={}, phase_limit={}, entry_phase={}, entry_ticks={}, entry_ack={}, exit_ticks={}, polls={}, sleep_ms={}, lock_ms={}",
      count,mode,interval,phase_limit,entry_phase,entry_ticks,entry_ack,pacing.ticks,pacing_polls,pacing_sleep_ms,pacing_lock_ms);
  if(count<=4 || !(count&(count-1)))
    REXLOG_INFO("Native swap barrier: count={}, interval={}, ticks={}, acknowledged={}",
      count,interval,pacing.ticks,pacing.acknowledged);
}
REX_EXTERN(__imp__sub_8213C9F0);
REX_EXTERN(__imp__edf_native_worker_signal_cpu_tail);
REX_HOOK_RAW(sub_8213C9F0) {
  const auto device=ctx.r3.u32,begin=ctx.r4.u32,callback=ctx.r6.u32,argument=ctx.r7.u32,flags=ctx.r5.u32;
  // The port has no Xbox command consumer. Unknown callbacks must not silently
  // emit a GPU interrupt packet and leave their CPU waiter permanently pending.
  // The former 51248 caller is replaced at 512D8 by native swap pacing.
  if(REXCVAR_GET(edf_native_host) && callback!=0x8214EBA0) {
    REXLOG_ERROR("Unsupported native signal callback: caller={:#x}, callback={:#x}, device={:#x}, argument={:#x}; no Xbox packet fallback",
      uint32_t(ctx.lr),callback,device,argument);
    throw std::runtime_error("native signal callback has no CPU completion implementation");
  }
  if(REXCVAR_GET(edf_native_host) && REXCVAR_GET(edf_native_hook_timings)) {
    const auto caller=uint32_t(ctx.lr);
    const size_t bucket=caller==0x8213D288?0:caller==0x8214ED64?1:caller==0x821513F4?2:3;
    static std::array<std::atomic<uint64_t>,4> calls{};
    const auto count=calls[bucket].fetch_add(1,std::memory_order_relaxed)+1;
    if(count<=8 || !(count&(count-1))) {
      // Observe the CPU callback contract before replacing its GPU signal packet.
      // Never execute the callback here: the enclosing submit has not run yet.
      REXLOG_INFO("Native pending signal audit: calls={}, caller={:#x}, device={:#x}, flags={:#x}, callback={:#x}, argument={:#x}, guest_tls={:#x}",
        count,uint32_t(ctx.lr),ctx.r3.u32,ctx.r5.u32,ctx.r6.u32,ctx.r7.u32,ctx.r13.u32);
    }
  }
  if(REXCVAR_GET(edf_native_host) && callback==0x8214EBA0)
    __imp__edf_native_worker_signal_cpu_tail(ctx,base);
  else __imp__sub_8213C9F0(ctx,base);
  if(REXCVAR_GET(edf_native_host) && callback==0x8214EBA0) {
    const uint32_t bytes=ctx.r3.u32-begin;
    const auto physical=edf::native::NativeSignalCommandAddress(begin+4);
    if(uint64_t(begin)+bytes+4>(uint64_t(1)<<32) || !bytes)
      throw std::runtime_error("invalid native worker signal packet span");
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    auto& queue=state.signal_queues[device];
    if(!queue) queue=std::make_unique<edf::native::NativeSignalQueue>(*state.device.Get(),*state.context.Get());
    std::lock_guard delivery_lock(state.signal_delivery_mutex);
    const auto delivery=state.signal_deliveries.find(device);
    if(queue->pending()+size_t(delivery!=state.signal_deliveries.end() && delivery->second.pending())>=4096)
      throw std::runtime_error("native signal capacity includes undelivered CPU work");
    try {
      queue->Capture(physical,bytes,{callback,argument,edf::native::NativeSignalCpuMask(flags)});
    } catch(const std::exception& error) {
      REXLOG_ERROR("Native signal capture failed: begin={:#x}, bytes={}, argument={:#x}, pending={}, unsubmitted={}: {}",
        physical,bytes,argument,queue->pending(),queue->unsubmitted(),error.what());
      throw;
    }
  }
}
REX_EXTERN(__imp__sub_8213C868);
REX_EXTERN(__imp__sub_8214E8E0);
REX_EXTERN(__imp__edf_native_worker_audit);
REX_EXTERN(__imp__edf_native_worker_commands_audit);
REX_HOOK_RAW(sub_8214E640) {
  __imp__edf_native_worker_commands_audit(ctx,base);
}
namespace {
struct NativeWorkerCommandTrace { uint32_t worker,cursor,word,continuation,list; };
thread_local std::array<NativeWorkerCommandTrace,32> native_worker_commands{};
thread_local uint64_t native_worker_command_count=0;
void DumpNativeWorkerCommands() {
  const auto first=native_worker_command_count>32?native_worker_command_count-32:0;
  for(auto i=first;i<native_worker_command_count;++i) {
    const auto& entry=native_worker_commands[i%32];
    REXLOG_ERROR("Native worker history: step={}, worker={:#x}, cursor={:#x}, word={:#x}, continuation={:#x}, list={:#x}",
      i,entry.worker,entry.cursor,entry.word,entry.continuation,entry.list);
  }
}
}
void edf_native_audit_worker_command(PPCContext& ctx,uint8_t* base,uint32_t worker,uint32_t cursor) {
  if(!REXCVAR_GET(edf_native_hook_timings)) return;
  const edf::native::GuestReader reader(base);
  if(cursor<0x10000) {
    DumpNativeWorkerCommands();
    REXLOG_ERROR("Native invalid worker cursor: LR={:#x}, worker={:#x}, cursor={:#x}, continuation={:#x}, list={:#x}",
      uint32_t(ctx.lr),worker,cursor,reader.Word(worker+80),reader.Word(worker+84));
    return; // Preserve the original failing read; diagnostics must not skip work.
  }
  const auto word=reader.Word(cursor);
  native_worker_commands[native_worker_command_count++%32]={worker,cursor,word,reader.Word(worker+80),reader.Word(worker+84)};
  // Count actual consumer visits, not static producer sites. Tiling commands
  // remain in retail bodies that native entry hooks may already bypass.
  const auto opcode=word>>24;
  const unsigned kind=!(word&0x80000000u)?0:
    word==0xc0000000u?14:opcode>=0x80 && opcode<=0x8c?1+opcode-0x80:15;
  static std::array<std::atomic<uint64_t>,16> visits{};
  const auto count=visits[kind].fetch_add(1,std::memory_order_relaxed)+1;
  if(count<=8 || !(count&(count-1)))
    REXLOG_INFO("Native worker command coverage: kind={}, visits={}, worker={:#x}, cursor={:#x}, word={:#x}",
      kind,count,worker,cursor,word);
}
REX_EXTERN(edf_native_null_worker_job) {
  DumpNativeWorkerCommands();
  const edf::native::GuestReader reader(base);
  const auto worker=ctx.r31.u32;
  REXLOG_ERROR("Native null worker job: LR={:#x}, worker={:#x}, callback={:#x}, job_argument={:#x}, index={}, count={}, data={:#x}, cursor={:#x}, continuation={:#x}, list={:#x}, published={:#x}",
    uint32_t(ctx.lr),worker,reader.Word(worker+16),reader.Word(worker+20),reader.Word(worker+24),reader.Word(worker+28),
    reader.Word(worker+32),reader.Word(worker+36),reader.Word(worker+80),reader.Word(worker+84),reader.Word(worker+88));
  for(const auto offset:{32u,36u,80u,84u}) {
    const auto address=reader.Word(worker+offset);
    if(!address) continue;
    for(uint32_t i=0;i<8;++i)
      REXLOG_ERROR("Native null worker memory: field={}, address={:#x}, word={:#x}",offset,address+i*4,reader.Word(reader.Add(address,i*4)));
  }
}
REX_HOOK_RAW(sub_8214E8E0) {
  const auto worker_arg=ctx.r3.u32;
  const bool audit=REXCVAR_GET(edf_native_host) && REXCVAR_GET(edf_native_hook_timings);
  static std::atomic<uint64_t> calls{0};
  const auto count=audit?calls.fetch_add(1,std::memory_order_relaxed)+1:0;
  const bool sample=count && (count<=8 || !(count&(count-1)));
  const edf::native::GuestReader reader(base);
  const auto worker=sample?reader.Word(worker_arg):0;
  if(sample)
    REXLOG_INFO("Native CPU worker entered: count={}, worker={:#x}, CPU={}, busy={}, nesting={}, argument={:#x}, continuation={:#x}",
      count,worker,reader.Word(worker_arg+4),reader.Word(worker+56),reader.Word(worker+48),reader.Word(worker+88),reader.Word(worker+80));
  if(REXCVAR_GET(edf_native_host)) __imp__edf_native_worker_audit(ctx,base);
  else __imp__sub_8214E8E0(ctx,base);
  if(REXCVAR_GET(edf_native_host)) {
    const auto actual_worker=reader.Word(worker_arg);
    edf::native::HookTiming service_timing(edf::native::HookPhase::WorkerService);
    const auto device=actual_worker-10812;
    // A worker may itself submit the query that resumes its saved continuation.
    // Service that native completion before it returns to an indefinite event
    // wait. Never execute the continuation here or change its busy counter.
    for(;;) {
      edf::native::PollNativeWorkerSignals(ctx,base,device,true);
      if(reader.Word(actual_worker+88)) break;
      bool in_flight=false;
      {
        auto& state=edf::native::State();
        std::lock_guard lock(state.mutex);
        const auto found=state.signal_queues.find(device);
        in_flight=found!=state.signal_queues.end() && found->second->pending()>found->second->unsubmitted();
        std::lock_guard delivery_lock(state.signal_delivery_mutex);
        const auto delivery=state.signal_deliveries.find(device);
        in_flight|=delivery!=state.signal_deliveries.end() && delivery->second.pending();
      }
      if(!in_flight) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  if(sample)
    REXLOG_INFO("Native CPU worker returned: count={}, worker={:#x}, busy={}, nesting={}, argument={:#x}",
      count,worker,reader.Word(worker+56),reader.Word(worker+48),reader.Word(worker+88));
}
namespace {
void SubmitOwnedNativeDescriptors(PPCContext& ctx,uint8_t* base,uint32_t device,
    std::span<const edf::native::NativeSubmissionDescriptor> descriptors);
}
REX_EXTERN(__imp__KfAcquireSpinLock);
REX_EXTERN(__imp__KfReleaseSpinLock);
REX_EXTERN(sub_8213C018);
REX_HOOK_RAW(sub_8213C868) {
  const auto device=ctx.r3.u32;
  if(REXCVAR_GET(edf_native_host) && REXCVAR_GET(edf_native_hook_timings)) {
    const auto caller=uint32_t(ctx.lr);
    const size_t path=caller==0x8213CEF8?0:caller==0x8213CFD8?1:caller==0x8214EDF8?2:caller==0x8214EE44?3:4;
    static std::array<std::atomic<uint64_t>,10> calls{};
    const auto count=calls[path*2+unsigned(ctx.r7.u32!=0)].fetch_add(1,std::memory_order_relaxed)+1;
    if(count<=8 || !(count&(count-1))) {
      try {
        const edf::native::GuestReader reader(base);
        const auto device=ctx.r3.u32;
        REXLOG_INFO("Native pending list audit: calls={}, caller={:#x}, device={:#x}, busy={}, increment={}, words={}, list={:#x}, recording={}, mode={:#x}",
          count,uint32_t(ctx.lr),device,reader.Word(reader.Add(device,10868)),ctx.r7.u32,ctx.r6.u32,
          ctx.r8.u32,reader.Word(reader.Add(device,12944)),reader.Bytes(reader.Add(device,10809),1)[0]);
      } catch(const std::exception& error) {
        REXLOG_WARN("Native pending list audit read failed: {}",error.what());
      }
    }
  }
  if(REXCVAR_GET(edf_native_host)) {
    {
      edf::native::NativeSignalSubmissionScope submission(device);
      const edf::native::GuestReader reader(base);
      const auto cursor=ctx.r4.u64,list=ctx.r8.u64;
      const auto increment=ctx.r7.u32;
      const edf::native::NativeSubmissionDescriptor descriptor{ctx.r6.u32,ctx.r5.u32};
      auto work=ctx;
      if(work.r1.u32<176) throw std::runtime_error("invalid native submission dispatch stack");
      work.r1.u64=work.r1.u32-176u;
      ctx.r3.u64=edf::native::DispatchNativeSubmission(reader,device,cursor,descriptor,increment,
        [&] {
          work.r3.u64=reader.Add(device,10872); work.lr=0x8213C8A8;
          __imp__KfAcquireSpinLock(work,base); return work.r3.u64;
        },
        [&](uint64_t token) {
          work.r3.u64=reader.Add(device,10872); work.r4.u64=token; work.lr=0x8213C8EC;
          __imp__KfReleaseSpinLock(work,base);
        },
        [&](uint64_t next,edf::native::NativeSubmissionDescriptor range) {
          work.r3.u64=list; work.r4.u64=next; work.r5.u64=range.words; work.r6.u64=range.address;
          work.lr=0x8213C8CC; sub_8213C018(work,base); return work.r3.u64;
        },
        [&](const edf::native::NativeSubmissionDescriptor& range) {
          work.r3.u64=device; work.lr=0x8213C918;
          SubmitOwnedNativeDescriptors(work,base,device,std::span(&range,1));
        });
    }
    // Enqueue is not submission: only the owned submission service arms ranges.
    edf::native::PollNativeWorkerSignals(ctx,base,device);
  } else {
    __imp__sub_8213C868(ctx,base);
  }
}
REX_HOOK_RAW(sub_8213BD90) {
  const edf::native::GuestReader reader(base);
  const auto device=ctx.r3.u32,words=ctx.r5.u32;
  auto& state=edf::native::State();
  std::lock_guard order(state.submissions);
  std::lock_guard lock(state.mutex);
  const auto snapshot=state.submission_cursors.Get(device);
  const auto cursor=snapshot.cursor,mask=snapshot.mask;
  const auto next=edf::native::AdvanceNativeRingCursor(cursor,words,mask);
  state.submission_cursors.Publish(device,snapshot,next);
  reader.StoreWord(reader.Add(device,10820),next);
  // The extracted native tail returned the omitted reservation's masked end,
  // which can differ from the repeated-mask cursor for non-contiguous masks.
  ctx.r3.u64=(cursor+words)&mask;
}
REX_HOOK_RAW(sub_8213C410) {
  {
    std::lock_guard command_order(edf::native::State().submissions);
    const auto device=ctx.r3.u32,descriptors=ctx.r4.u32,count=ctx.r5.u32;
    edf::native::NativeSignalSubmissionScope snapshot_scope(device);
    const edf::native::GuestReader reader(base);
    // C410 accepts CPU descriptors, not Xbox packets: [word count, address].
    // Snapshot before its observer callbacks can touch the descriptor storage.
    std::vector<edf::native::NativeSubmissionDescriptor> owned_descriptors;
    if(count>UINT32_MAX/8) throw std::runtime_error("native signal descriptor count overflow");
    owned_descriptors.reserve(count);
    for(uint32_t i=0;i<count;++i) {
      const auto descriptor=reader.Add(descriptors,i*8);
      const auto words=reader.Word(descriptor);
      if(words>UINT32_MAX/4) throw std::runtime_error("native signal submission range overflow");
      const auto address=reader.Word(reader.Add(descriptor,4));
      owned_descriptors.push_back({words,address});
    }
    SubmitOwnedNativeDescriptors(ctx,base,device,owned_descriptors);
  }
}
namespace {
void SubmitOwnedNativeDescriptors(PPCContext& ctx,uint8_t* base,uint32_t device,
    std::span<const edf::native::NativeSubmissionDescriptor> owned_descriptors) {
    std::lock_guard command_order(edf::native::State().submissions);
    edf::native::NativeSignalSubmissionScope submission(device);
    const edf::native::GuestReader reader(base);
    for(const auto& range:owned_descriptors)
      if(range.words>UINT32_MAX/4) throw std::runtime_error("native signal submission range overflow");
    auto work=ctx;
    if(work.r1.u32<176) throw std::runtime_error("invalid native submission observer stack");
    work.r1.u64=work.r1.u32-176u;
    auto& state=edf::native::State();
    edf::native::NativeSubmissionCursors::Snapshot snapshot;
    { std::lock_guard lock(state.mutex); snapshot=state.submission_cursors.Get(device); }
    const edf::native::NativeSubmissionCursorAccess owned_reader(reader,device,snapshot,[&](uint32_t cursor) {
      std::lock_guard lock(state.mutex);
      state.submission_cursors.Publish(device,snapshot,cursor);
      reader.StoreWord(reader.Add(device,10820),cursor);
    });
    edf::native::SubmitNativeObservers(owned_reader,device,owned_descriptors,
      [&](uint32_t target,uint32_t slot,uint64_t address,uint32_t words,uint32_t kind,uint32_t caller) {
        const auto function=slot?reader.Word(reader.Add(reader.Word(target),slot)):target;
        work.r3.u64=slot?target:kind;
        if(slot!=28) {
          // Retail subtracts in 64 bits; preserve the negative observer address.
          work.r4.u64=kind==2?0:address;
          work.r5.u64=words; work.r6.u64=kind==2?0:1;
        }
        work.lr=caller; work.ctr.u64=function; work.last_indirect_target=function;
        rex::runtime::ResolveIndirectFunction(function)(work,base);
        // The submission gate is recursive: an observer can reset this device.
        // Reject before reading its replacement ABI state or invoking another observer.
        std::lock_guard lock(state.mutex);
        state.submission_cursors.Validate(device,snapshot);
      });
    ctx.r3=work.r3;
    std::lock_guard lock(state.mutex);
    // Special-mode submission need not publish a cursor. Validate every path
    // under the same lock as queue lookup, before arming any completion ranges.
    state.submission_cursors.Validate(device,snapshot);
    const auto found=state.signal_queues.find(device);
    if(found!=state.signal_queues.end())
      for(const auto& range:owned_descriptors) if(range.words) found->second->SubmitRange(range.address,range.words*4);
    const auto fences=state.completion_queues.find(device);
    if(fences!=state.completion_queues.end())
      for(const auto& range:owned_descriptors) if(range.words) fences->second->SubmitRange(range.address,range.words*4);
    return;
}
}
REX_EXTERN(__imp__sub_8213C5F0);
REX_EXTERN(__imp__edf_native_cache_range_cpu_tail);
REX_EXTERN(__imp__edf_native_cache_reservation_cpu_tail);
REX_HOOK_RAW(sub_8213C5F0) {
  // CF60 skips only the cache packet enqueue when word count is zero; its
  // following CDC0 native fence submission and optional CPU wait still run.
  // ECD8 consumes the reserved address as fallback CPU storage: retain its
  // reservation/failure contract, but do not encode or enqueue cache packets.
  if(REXCVAR_GET(edf_native_host) && uint32_t(ctx.lr)==0x8213CF9C) {
    __imp__edf_native_cache_range_cpu_tail(ctx,base); return;
  }
  if(REXCVAR_GET(edf_native_host) && uint32_t(ctx.lr)==0x8214ED04) {
    __imp__edf_native_cache_reservation_cpu_tail(ctx,base); return;
  }
  if(REXCVAR_GET(edf_native_host)) {
    REXLOG_ERROR("Unsupported native cache-range caller={:#x}; no Xbox packet fallback",uint32_t(ctx.lr));
    throw std::runtime_error("native cache range caller has no CPU reservation contract");
  }
  __imp__sub_8213C5F0(ctx,base);
}
REX_EXTERN(__imp__edf_native_device_reset);
REX_EXTERN(__imp__sub_8213D1C8);
REX_EXTERN(__imp__edf_native_device_drain);
REX_HOOK_RAW(sub_8213D1C8) {
  if(REXCVAR_GET(edf_native_host)) {
    __imp__edf_native_device_drain(ctx,base); return;
  }
  __imp__sub_8213D1C8(ctx,base);
}
REX_EXTERN(edf_native_reset_completion_tracking) {
  // D298 has drained old work, but has not yet freed/replaced writeback storage.
  // Its saved r31 still holds the device. Do not touch guest registers/memory.
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  const auto signals=state.signal_queues.find(ctx.r31.u32);
  std::lock_guard delivery_lock(state.signal_delivery_mutex);
  const auto delivery=state.signal_deliveries.find(ctx.r31.u32);
  if(delivery!=state.signal_deliveries.end() && delivery->second.pending())
    throw std::runtime_error("native reset attempted to discard undelivered worker signal");
  if(signals!=state.signal_queues.end() && signals->second->pending())
    throw std::runtime_error("native reset attempted to discard pending worker signals");
  state.signal_queues.erase(ctx.r31.u32);
  const auto fences=state.completion_queues.find(ctx.r31.u32);
  if(fences!=state.completion_queues.end() && fences->second->unsubmitted())
    throw std::runtime_error("native reset attempted to discard unsubmitted fences");
  state.completion_queues.erase(ctx.r31.u32);
  state.swap_clocks.erase(ctx.r31.u32);
  // D298 replaces command/completion storage, not the device's render words.
  // Keep their ownership; allocation/final Release delimit that lifetime.
  state.native_published_completions.erase(ctx.r31.u32);
  state.submission_cursors.Retire(ctx.r31.u32);
  state.completion_faults.erase(ctx.r31.u32);
  if(state.display_gamma_device==ctx.r31.u32) {
    state.display_gamma.reset(); state.display_gamma_device=0;
    if(state.presentation_frames) state.presentation_frames->Invalidate();
  }
  REXLOG_INFO("Native device reset: retired old completion tracking after drain, before storage replacement");
}
REX_EXTERN(edf_native_drain_worker_signals) {
  const auto device=ctx.r31.u32;
  for(;;) {
    edf::native::PollNativeWorkerSignals(ctx,base,device,true);
    size_t pending=0,unsubmitted=0;
    {
      auto& state=edf::native::State();
      std::lock_guard lock(state.mutex);
      const auto found=state.signal_queues.find(device);
      if(found!=state.signal_queues.end()) {
        pending=found->second->pending(); unsubmitted=found->second->unsubmitted();
      }
      std::lock_guard delivery_lock(state.signal_delivery_mutex);
      const auto delivery=state.signal_deliveries.find(device);
      pending+=size_t(delivery!=state.signal_deliveries.end() && delivery->second.pending());
    }
    if(unsubmitted) throw std::runtime_error("native drain has unsubmitted worker signals");
    if(!pending) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const edf::native::GuestReader reader(base);
  if(reader.Word(reader.Add(device,10868))) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
REX_HOOK_RAW(sub_8213D298) {
  if(REXCVAR_GET(edf_native_host)) {
    const auto device=ctx.r3.u32,configuration=ctx.r4.u32;
    __imp__edf_native_device_reset(ctx,base);
    if(configuration && ctx.r3.u32==0) {
      auto& state=edf::native::State();
      std::lock_guard order(state.submissions);
      std::lock_guard lock(state.mutex);
      const edf::native::GuestReader reader(base);
      state.submission_cursors.Initialize(device,reader.Word(reader.Add(device,13480)));
    }
    return;
  }
  if(REXCVAR_GET(edf_native_shader_bridge) && REXCVAR_GET(edf_native_fence_probe)) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    state.completion_queues.erase(ctx.r3.u32);
    state.native_published_completions.erase(ctx.r3.u32);
    state.completion_faults.erase(ctx.r3.u32);
  }
  __imp__sub_8213D298(ctx,base);
}

REX_EXTERN(__imp__edf_native_worker_init);
REX_HOOK_RAW(sub_8214EE50) {
  const edf::native::GuestReader reader(base);
  const auto flags=reader.Word(reader.Add(ctx.r3.u32,20416));
  __imp__edf_native_worker_init(ctx,base);
  REXLOG_INFO("Native worker initialization: retained CPU/thread setup; omitted Xbox setup packets; result={}, flags={:#x}, requested_mask={:#x}, skipped={}",
    ctx.r3.u32,flags,(flags>>24)&63,bool(flags&0x100));
}

REX_EXTERN(__imp__edf_native_device_defaults);
REX_HOOK_RAW(sub_8214EFF8) {
  const auto device=ctx.r3.u32;
  __imp__edf_native_device_defaults(ctx,base);
  edf::native::PublishNativeRenderState(base,device,0x8214eff8);
  REXLOG_INFO("Native device defaults: retained CPU descriptors/state; omitted Xbox setup packet tail");
}

REX_EXTERN(__imp__sub_82139638);
REX_HOOK_RAW(sub_82139638) {
  // 82139A40 allocates and zero-fills the device before either initializer.
  // Publish the known zero result here, not a later draw-time memory snapshot.
  const bool device_allocation=uint32_t(ctx.lr)==0x82139a70 &&
    ctx.r3.u32==20480 && ctx.r4.u32==128;
  __imp__sub_82139638(ctx,base);
  if(device_allocation && ctx.r3.u32 &&
      (REXCVAR_GET(edf_native_render_state_audit) || REXCVAR_GET(edf_native_owned_render_state)) &&
      REXCVAR_GET(edf_native_shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    state.render_state_snapshots.Retire(ctx.r3.u32);
    state.render_state_snapshots.Publish(ctx.r3.u32,{},0x82139638);
    state.render_state_snapshots.PublishBlend(ctx.r3.u32,{},0x82139638);
    REXLOG_INFO("Native render-state zeroed device: device={:#x}",ctx.r3.u32);
  }
}
REX_EXTERN(__imp__sub_82139760);
REX_HOOK_RAW(sub_82139760) {
  if((REXCVAR_GET(edf_native_render_state_audit) || REXCVAR_GET(edf_native_owned_render_state)) &&
      REXCVAR_GET(edf_native_shader_bridge)) {
    const edf::native::GuestReader reader(base);
    // Final Release invokes destruction/free; non-final Release keeps ownership.
    if(reader.Word(reader.Add(ctx.r3.u32,52))==1) {
      auto& state=edf::native::State();
      std::lock_guard lock(state.mutex);
      state.render_state_snapshots.Retire(ctx.r3.u32);
    }
  }
  __imp__sub_82139760(ctx,base);
}

REX_EXTERN(__imp__sub_82135418);
REX_HOOK_RAW(sub_82135418) {
  const auto device=ctx.r3.u32;
  __imp__sub_82135418(ctx,base);
  if((REXCVAR_GET(edf_native_owned_render_state) || REXCVAR_GET(edf_native_render_state_audit)) &&
      REXCVAR_GET(edf_native_shader_bridge)) {
    const edf::native::GuestReader reader(base);
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    state.render_state_snapshots.PublishBlend(device,
      edf::native::ReadGuestWords<4>(reader,reader.Add(device,10336)),0x82135418);
  }
}

// Preserve retail setter/dirty-mask/return semantics, then publish completed CPU
// state. Diagnostic-only until indirect, reset and concurrent writers are proven.
#define EDF_RENDER_STATE_SETTER(name,pc) \
  REX_EXTERN(__imp__##name); \
  REX_HOOK_RAW(name) { \
    const auto device=ctx.r3.u32; \
    __imp__##name(ctx,base); \
    edf::native::PublishNativeRenderState(base,device,pc); \
  }
EDF_RENDER_STATE_SETTER(sub_82134EE8,0x82134ee8)
EDF_RENDER_STATE_SETTER(sub_82134EB8,0x82134eb8)
EDF_RENDER_STATE_SETTER(sub_82134F18,0x82134f18)
EDF_RENDER_STATE_SETTER(sub_82134F58,0x82134f58)
EDF_RENDER_STATE_SETTER(sub_82134FE8,0x82134fe8)
EDF_RENDER_STATE_SETTER(sub_82135078,0x82135078)
EDF_RENDER_STATE_SETTER(sub_82135108,0x82135108)
EDF_RENDER_STATE_SETTER(sub_82135198,0x82135198)
EDF_RENDER_STATE_SETTER(sub_82135208,0x82135208)
EDF_RENDER_STATE_SETTER(sub_82135278,0x82135278)
EDF_RENDER_STATE_SETTER(sub_821352E8,0x821352e8)
EDF_RENDER_STATE_SETTER(sub_821353E8,0x821353e8)
EDF_RENDER_STATE_SETTER(sub_82135530,0x82135530)
EDF_RENDER_STATE_SETTER(sub_82135578,0x82135578)
EDF_RENDER_STATE_SETTER(sub_821355A8,0x821355a8)
EDF_RENDER_STATE_SETTER(sub_821355E8,0x821355e8)
EDF_RENDER_STATE_SETTER(sub_82135630,0x82135630)
EDF_RENDER_STATE_SETTER(sub_82135670,0x82135670)
EDF_RENDER_STATE_SETTER(sub_821356A0,0x821356a0)
EDF_RENDER_STATE_SETTER(sub_821356E0,0x821356e0)
EDF_RENDER_STATE_SETTER(sub_82135720,0x82135720)
EDF_RENDER_STATE_SETTER(sub_82135750,0x82135750)
EDF_RENDER_STATE_SETTER(sub_82135780,0x82135780)
EDF_RENDER_STATE_SETTER(sub_821357C0,0x821357c0)
EDF_RENDER_STATE_SETTER(sub_82135800,0x82135800)
EDF_RENDER_STATE_SETTER(sub_82135948,0x82135948)
EDF_RENDER_STATE_SETTER(sub_82135A10,0x82135a10)
EDF_RENDER_STATE_SETTER(sub_82135AB8,0x82135ab8)
EDF_RENDER_STATE_SETTER(sub_82135B08,0x82135b08)
EDF_RENDER_STATE_SETTER(sub_82135B40,0x82135b40)
EDF_RENDER_STATE_SETTER(sub_82135B78,0x82135b78)
EDF_RENDER_STATE_SETTER(sub_82135BB0,0x82135bb0)
EDF_RENDER_STATE_SETTER(sub_82136478,0x82136478)
EDF_RENDER_STATE_SETTER(sub_821364C8,0x821364c8)
EDF_RENDER_STATE_SETTER(sub_821364F8,0x821364f8)
EDF_RENDER_STATE_SETTER(sub_82137978,0x82137978)
EDF_RENDER_STATE_SETTER(sub_82137988,0x82137988)
#undef EDF_RENDER_STATE_SETTER

// Replace the two present profiling packets with native timestamp markers.
REX_EXTERN(__imp__sub_821390B8);
REX_HOOK_RAW(sub_821390B8) {
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_821390B8(ctx,base); return; }
  const bool finish=ctx.lr==0x8215162C;
  if(!finish && ctx.lr!=0x82151544) throw std::runtime_error("unknown native profiling marker caller");
  const edf::native::GuestReader reader(base);
  const auto device=ctx.r3.u32,sequence=reader.Word(reader.Add(device,20052));
  const auto writeback=reader.Word(reader.Add(device,10768));
  if(ctx.r4.u32!=reader.Add(writeback,64+4*((sequence+uint32_t(finish))&7)))
    throw std::runtime_error("native profiling marker slot mismatch");
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  auto& profiler=state.present_profilers[device];
  if(!profiler) profiler=std::make_unique<edf::native::NativePresentProfiler>(*state.device.Get(),*state.context.Get());
  if(finish) profiler->Finish(sequence); else profiler->Start(sequence);
}
REX_EXTERN(__imp__sub_82138158);
REX_HOOK_RAW(sub_82138158) {
  if(!REXCVAR_GET(edf_native_host) || ctx.r4.u32!=6) { __imp__sub_82138158(ctx,base); return; }
  // Retail's 0x7fffffff float is its unavailable-sample sentinel, not zero.
  ctx.f1.f64=edf::native::NativeProfileResult(std::nullopt,0,0);
  const edf::native::GuestReader reader(base);
  const auto device=ctx.r3.u32;
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  const auto found=state.present_profilers.find(device);
  if(found==state.present_profilers.end()) return;
  const auto sample=found->second->Poll();
  if(!sample) return;
  const auto consumer=reader.Word(reader.Add(device,20048));
  if(sample->tag!=consumer) throw std::runtime_error("native profiling consumer sequence mismatch");
  reader.StoreWord(reader.Add(device,20048),consumer+2);
  const auto ratio=sample->FractionAfterMiddle();
  if(!ratio) return; // First interval, disjoint data or zero-duration interval.
  const auto scale=std::bit_cast<float>(reader.Word(0x82003e3c));
  const auto minimum=std::bit_cast<float>(reader.Word(0x820009a4));
  ctx.f1.f64=edf::native::NativeProfileResult(ratio,scale,minimum);
}
REX_EXTERN(__imp__sub_82138E00);
REX_HOOK_RAW(sub_82138E00) {
  uint32_t reset_device=0;
  if(REXCVAR_GET(edf_native_host) && (ctx.r3.u32==0 ||
      ((ctx.r3.u32==16 || ctx.r3.u32==17) && ctx.r4.u32==6))) {
    const edf::native::GuestReader reader(base);
    const auto device=reader.Word(reader.Word(0x82000720));
    if(device && reader.Word(reader.Add(device,52)) && reader.DoubleWord(reader.Add(device,10752)))
      reset_device=device;
  }
  __imp__sub_82138E00(ctx,base);
  if(reset_device) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    state.present_profilers.erase(reset_device);
  }
}

namespace {
thread_local edf::native::NativeFenceRecords native_fence_records;
template<class Reader>
void AuditNativeFenceRecord(const Reader& reader,uint32_t record,
    uint32_t tls,uint32_t caller,unsigned phase) noexcept {
  if(!REXCVAR_GET(edf_native_retirement_audit)) return;
  static std::atomic<uint64_t> counts[3]{};
  const auto count=counts[phase].fetch_add(1,std::memory_order_relaxed)+1;
  if(count>8 && (count&(count-1))) return;
  try {
    const auto device=reader.Word(record);
    const auto now=reader.Word(reader.Add(reader.Word(reader.Add(tls,256)),88));
    REXLOG_INFO("Native fence record: phase={} count={} record={:08X} device={:08X} kind={} kernel_start={} kernel_now={} callback={:08X} caller={:08X}",
      phase,count,record,device,reader.Word(reader.Add(record,4)),reader.Word(reader.Add(record,16)),now,
      reader.Word(reader.Add(device,13068)),caller);
  } catch(const std::exception& error) {
    REXLOG_WARN("Native fence record audit failed: {}",error.what());
  }
}
}
REX_EXTERN(__imp__sub_821394D8);
REX_HOOK_RAW(sub_821394D8) {
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_821394D8(ctx,base); return; }
  const edf::native::GuestReader guest(base);
  edf::native::NativeFenceRecord owned{};
  const edf::native::NativeFenceRecordAccess reader(guest,ctx.r3.u32,owned);
  edf::native::BeginNativeFenceRecord(reader,ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r13.u32,
    [] { return rex::chrono::Clock::QueryGuestTickCount(); });
  native_fence_records.Publish(ctx.r13.u32,ctx.r3.u32,owned);
  AuditNativeFenceRecord(reader,ctx.r3.u32,ctx.r13.u32,uint32_t(ctx.lr),0);
}
edf::native::NativeBufferWrites::WriterScope edf_native_begin_fence_counter_write(uint8_t*,uint32_t);
void edf_native_complete_fence_counter_write(uint8_t*,uint32_t);
REX_EXTERN(__imp__sub_82139508);
REX_HOOK_RAW(sub_82139508) {
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_82139508(ctx,base); return; }
  const edf::native::GuestReader guest(base);
  const auto record=ctx.r3.u32;
  // Detach before accounting/callbacks, allowing reentrant reuse without a
  // dangling map reference. Exceptions cannot leave a completed record active.
  auto owned=native_fence_records.Take(ctx.r13.u32,record);
  const edf::native::NativeFenceRecordAccess reader(guest,record,owned);
  edf::native::EndNativeFenceRecord(reader,ctx.r3.u32,ctx.r13.u32,
    [] { return rex::chrono::Clock::QueryGuestTickCount(); },
    [&](uint32_t callback,uint32_t device,uint32_t kind,uint32_t ticks,uint64_t elapsed) {
      auto work=ctx;
      if(work.r1.u32<96) throw std::runtime_error("invalid native fence accounting stack");
      work.r1.u64=work.r1.u32-96u;
      work.r3.u64=0; work.r4.u64=kind; work.r6.u64=elapsed;
      PPCRegister scale{},units{};
      work.fpscr.disableFlushMode();
      scale.u32=reader.Word(reader.Add(device,20020)); units.u32=reader.Word(0x82002170);
      // Match the two separately rounded retail single-precision products.
      const float scaled=float(double(scale.f32)*double(float(ticks)));
      work.f1.f64=double(float(double(scaled)*double(units.f32)));
      work.lr=0x821395B8;
      work.ctr.u64=callback; work.last_indirect_target=callback;
      rex::runtime::ResolveIndirectFunction(callback)(work,base);
      ctx.r3=work.r3;
    },[&](uint32_t total,uint32_t ticks) {
      const auto writer=edf_native_begin_fence_counter_write(base,total);
      reader.StoreDoubleWord(total,reader.DoubleWord(total)+ticks);
      edf_native_complete_fence_counter_write(base,total);
    });
  AuditNativeFenceRecord(reader,record,ctx.r13.u32,uint32_t(ctx.lr),2);
}

// Native stall policy uses host steady time. Failure unwinds, never fabricates
// guest completion through the retail device-error handler.
REX_EXTERN(__imp__sub_82139688);
REX_HOOK_RAW(sub_82139688) {
  edf::native::HookTiming poll_timing(edf::native::HookPhase::CompletionPoll);
  const bool native=REXCVAR_GET(edf_native_host);
  if(native) {
    const auto record_token=ctx.r3.u32,tls=ctx.r13.u32;
    try {
    const edf::native::GuestReader guest(base);
    auto& owned=native_fence_records.Find(ctx.r13.u32,ctx.r3.u32);
    const edf::native::NativeFenceRecordAccess reader(guest,ctx.r3.u32,owned);
    const auto device=reader.Word(ctx.r3.u32);
    AuditNativeFenceRecord(reader,ctx.r3.u32,ctx.r13.u32,uint32_t(ctx.lr),1);
    // A queued fence may depend on a CPU worker that has not consumed its
    // completed wake signal yet. Keep both completion domains progressing.
    edf::native::PollNativeWorkerSignals(ctx,base,device,true);
    {
      auto& state=edf::native::State();
      std::lock_guard lock(state.mutex);
      edf::native::PublishNativeCompletion(guest,state,device,true);
    }
    const auto limit=REXCVAR_GET(edf_native_wait_stall_ms);
    if(limit<1 || limit>600000) throw std::runtime_error("invalid native wait stall deadline");
    const bool again=edf::native::PollHostFenceProgress(reader,record_token,tls,owned.progress,
      edf::native::NativeWaitProgress::Clock::now(),std::chrono::milliseconds(limit),
      [&](uint32_t failed_device,bool stalled) {
        REXLOG_ERROR("Native wait failed: device={:08X} record={:08X} stalled={} limit_ms={}; no completion fabricated",
          failed_device,record_token,stalled,limit);
        throw std::runtime_error(stalled?"native GPU/worker wait made no progress":"native wait encountered device failure");
      });
    ctx.r3.u64=again?1:0;
    if(again) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return;
    } catch(...) {
      native_fence_records.Discard(tls,record_token);
      throw;
    }
  }
  __imp__sub_82139688(ctx,base);
}

// Native engine timing replaces the engine's vblank registration and spin loop,
// not the retail GPU interrupt handler (which reads Xenos MMIO).
REX_EXTERN(__imp__sub_821BEBF0);
REX_HOOK_RAW(sub_821BEBF0) {
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_821BEBF0(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  const auto object=ctx.r3.u32, divisor=ctx.r4.u32;
  (void)edf::native::NativePacingSteps(0,0,divisor);
  auto& state=edf::native::PacingState();
  std::lock_guard lock(state.mutex);
  reader.StoreWord(object,0x82019918);
  reader.StoreWord(reader.Add(object,4),divisor);
  reader.StoreDoubleWord(reader.Add(object,16),1);
  state.clock.Reset(edf::native::NativePacingClock::Clock::now(),reader.DoubleWord(0x8257C300));
  state.calls=0;
  REXLOG_INFO("Native engine pacing: 60 Hz clock, divisor={}, no vblank callback registered",divisor);
}

REX_EXTERN(__imp__sub_821BEAB0);
REX_EXTERN(sub_821FAC28);
REX_HOOK_RAW(sub_821BEAB0) {
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_821BEAB0(ctx,base); return; }
  edf::native::HookTiming engine_timing(edf::native::HookPhase::EngineWait);
  const edf::native::GuestReader reader(base);
  const auto object=ctx.r3.u32;
  const auto divisor=reader.Word(reader.Add(object,4));
  auto& state=edf::native::PacingState();
  std::lock_guard lock(state.mutex);
  const auto previous=reader.DoubleWord(0x8257C308);
  auto current=state.clock.Sample(edf::native::NativePacingClock::Clock::now());
  auto steps=edf::native::NativePacingSteps(current,previous,divisor);
  while(edf::native::NativePacingPending(steps)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    current=state.clock.Sample(edf::native::NativePacingClock::Clock::now());
    steps=edf::native::NativePacingSteps(current,previous,divisor);
  }
  reader.StoreDoubleWord(0x8257C300,current);
  reader.StoreDoubleWord(0x8257C308,current);
  ctx.r3.u64=reader.Add(object,8);
  sub_821FAC28(ctx,base);
  ctx.r3.u64=edf::native::NativePacingResult(steps);
  if(++state.calls<=3)
    REXLOG_INFO("Native engine pacing: tick={}, steps={}, result={}",current,steps,ctx.r3.u32);
}

REX_EXTERN(__imp__sub_821BEB38);
REX_HOOK_RAW(sub_821BEB38) {
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_821BEB38(ctx,base); return; }
  auto& state=edf::native::PacingState();
  std::lock_guard lock(state.mutex);
  const edf::native::GuestReader reader(base);
  reader.StoreDoubleWord(0x8257C300,state.clock.Sample(edf::native::NativePacingClock::Clock::now()));
  __imp__sub_821BEB38(ctx,base);
}

// Observe the retained recording/direct-submission transitions independently
// of scene setup. No packet, queue, fence or scheduling behavior is changed.
REX_EXTERN(__imp__sub_8213CF60);
REX_EXTERN(sub_8213C5F0);
REX_EXTERN(sub_8213CDC0);
REX_EXTERN(sub_8213C928);
REX_HOOK_RAW(sub_8213CF60) {
  edf::native::HookTiming timing(edf::native::HookPhase::SubmissionFlush);
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_8213CF60(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  const auto device=ctx.r3.u32;
  auto work=ctx;
  if(work.r1.u32<112) throw std::runtime_error("invalid native submission flush stack");
  work.r1.u64=work.r1.u32-112u;
  ctx.r3.u64=edf::native::FlushNativeSubmission(reader,device,
    [&] {
      work.r3.u64=device; work.r4.u64=reader.Add(work.r1.u32,84); work.r5.u64=reader.Add(work.r1.u32,80);
      work.lr=0x8213CF9C; sub_8213C5F0(work,base);
      if(reader.Word(reader.Add(work.r1.u32,80))) throw std::runtime_error("native cache collection returned GPU packet words");
    },
    [&] { work.r3.u64=device; work.lr=0x8213CFE0; sub_8213CDC0(work,base); },
    [&] { return reader.Word(0x82578D08)!=0; },
    [&](uint32_t target) {
      work.r3.u64=device; work.r4.u64=target; work.r5.u64=0; work.r6.u64=0;
      work.lr=0x8213D020; sub_8213C928(work,base);
    });
}
REX_EXTERN(__imp__sub_8213CDC0);
REX_EXTERN(sub_82141500);
REX_EXTERN(sub_8213CB30);
REX_EXTERN(sub_8213C788);
REX_EXTERN(sub_8213C868);
REX_EXTERN(sub_8213CC20);
REX_HOOK_RAW(sub_8213CDC0) {
  edf::native::HookTiming timing(edf::native::HookPhase::DescriptorSubmit);
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_8213CDC0(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  const auto device=ctx.r3.u32;
  auto work=ctx;
  if(work.r1.u32<128) throw std::runtime_error("invalid native descriptor submission stack");
  work.r1.u64=work.r1.u32-128u;
  edf::native::SubmitNativeDescriptor(reader,device,
    [&] { work.r3.u64=device; work.lr=0x8213CE44; sub_82141500(work,base); return work.r3.u32; },
    [&] { work.r3.u64=reader.Add(device,13000); work.lr=0x8213CEA0; sub_8213CB30(work,base); return work.r3.u32; },
    [&](uint32_t next) {
      work.r3.u64=device; work.r4.u64=next; work.lr=0x8213CEC8;
      sub_8213C788(work,base); return work.r3.u32;
    },
    [&](uint32_t captured,uint32_t address,uint32_t words) {
      work.r3.u64=device; work.r4.u64=captured; work.r5.u64=address; work.r6.s64=int32_t(words);
      work.r7.u64=0; work.r8.u64=reader.Add(device,13000); work.lr=0x8213CEF8;
      sub_8213C868(work,base); return work.r3.u32;
    },
    [&] { work.r3.u64=device; work.r4.u64=0; work.lr=0x8213CF24; sub_8213CC20(work,base); });
  ctx.r3=work.r3;
}
REX_EXTERN(sub_821394D8);
REX_EXTERN(sub_82139688);
REX_EXTERN(sub_82139508);
namespace {
void RunNativeAllocatorWait(PPCContext& ctx,uint8_t* base,bool generation) {
  const edf::native::GuestReader reader(base);
  const auto device=ctx.r3.u32,first=ctx.r4.u32,second=ctx.r5.u32;
  auto work=ctx;
  if(work.r1.u32<144) throw std::runtime_error("invalid native allocator wait stack");
  work.r1.u64=work.r1.u32-144u;
  const auto token=reader.Add(work.r1.u32,80);
  auto begin=[&] {
    work.r3.u64=token; work.r4.u64=device; work.r5.u64=generation?2:1;
    work.lr=generation?0x8213BC98:0x8213BD3C; sub_821394D8(work,base);
  };
  auto poll=[&] {
    work.r3.u64=token; work.lr=generation?0x8213BCA0:0x8213BD44;
    sub_82139688(work,base); return work.r3.u32!=0;
  };
  auto end=[&] {
    work.r3.u64=token; work.lr=generation?0x8213BCD8:0x8213BD80; sub_82139508(work,base);
  };
  if(generation) {
    edf::native::WaitNativeAllocationGeneration(reader,device,first,second,begin,poll,end);
    ctx.r3=work.r3;
  } else {
    ctx.r3.u64=edf::native::WaitNativeRingRange(reader,device,first,second,begin,poll,end);
  }
}
}
REX_EXTERN(__imp__sub_8213BC48);
REX_HOOK_RAW(sub_8213BC48) {
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_8213BC48(ctx,base); return; }
  RunNativeAllocatorWait(ctx,base,true);
}
REX_EXTERN(__imp__sub_8213BCE0);
REX_HOOK_RAW(sub_8213BCE0) {
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_8213BCE0(ctx,base); return; }
  RunNativeAllocatorWait(ctx,base,false);
}
REX_EXTERN(__imp__sub_8213C928);
REX_EXTERN(__imp__sub_8214E5B8);
REX_HOOK_RAW(sub_8214E5B8) {
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_8214E5B8(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  const auto device=reader.Word(reader.Word(0x82000720));
  auto work=ctx;
  if(work.r1.u32<144) throw std::runtime_error("invalid native worker wait stack");
  work.r1.u64=work.r1.u32-144u;
  const auto token=reader.Add(work.r1.u32,80);
  edf::native::WaitNativeWorkerSlots(reader,device,
    [&] { work.r3.u64=token; work.r4.u64=device; work.r5.u64=0;
      work.lr=0x8214E600; sub_821394D8(work,base); },
    [&] { work.r3.u64=token; work.lr=0x8214E608; sub_82139688(work,base); return work.r3.u32!=0; },
    [&] { work.r3.u64=token; work.lr=0x8214E628; sub_82139508(work,base); });
  ctx.r3=work.r3;
}
// Native mode owns wait control flow. Submission, wait-record accounting and
// error policy remain retained services; completion polling publishes native
// D3D results. Optional diagnostics below run only after this boundary returns.
REX_EXTERN(sub_8213CF60);
REX_EXTERN(sub_821394D8);
REX_EXTERN(sub_82139688);
REX_EXTERN(sub_82139508);
REX_HOOK_RAW(sub_8213C928) {
  edf::native::HookTiming wait_timing(edf::native::HookPhase::FenceWait);
  const auto device=ctx.r3.u32,target=ctx.r4.u32;
  if(REXCVAR_GET(edf_native_host)) {
    const edf::native::GuestReader reader(base);
    const auto wait_device=ctx.r3.u32,target=ctx.r4.u32,kind=ctx.r5.u32;
    auto work=ctx;
    // Preserve the caller frame for retained submission/error services. The
    // old record address is now an opaque key; its fields live in native storage.
    if(work.r1.u32<144) throw std::runtime_error("invalid native fence wait stack");
    work.r1.u64=work.r1.u32-144u;
    const auto record=reader.Add(work.r1.u32,80);
    edf::native::WaitNativeResourceFence(reader,wait_device,target,
      [&] { work.r3.u64=wait_device; work.lr=0x8213C97C; sub_8213CF60(work,base); },
      [&] { work.r3.u64=record; work.r4.u64=wait_device; work.r5.u64=kind;
        work.lr=0x8213C9A8; sub_821394D8(work,base); },
      [&] { work.r3.u64=record; work.lr=0x8213C9B4; sub_82139688(work,base); return work.r3.u32!=0; },
      [&] { work.r3.u64=record; work.lr=0x8213C9E8; sub_82139508(work,base); });
    ctx.r3=work.r3;
  } else {
    __imp__sub_8213C928(ctx,base);
  }
  wait_timing.Finish(); // Exclude optional post-wait diagnostic validation.
  if(!REXCVAR_GET(edf_native_shader_bridge) || !REXCVAR_GET(edf_native_fence_probe)) return;
  auto& state=edf::native::State();
  std::lock_guard lock(state.mutex);
  if(state.completion_faults.contains(device)) return;
  try {
    const edf::native::GuestReader reader(base);
    const auto issued=reader.Word(reader.Add(device,10780));
    const auto guest_completed=reader.Word(reader.Word(reader.Add(device,10768)));
    std::optional<uint32_t> native_completed;
    const auto queue=state.completion_queues.find(device);
    if(queue!=state.completion_queues.end() && queue->second) native_completed=queue->second->Poll();
    const bool native_pending=native_completed && edf::native::GuestFencePending(issued,target,*native_completed);
    ++state.completion_waits;
    if(native_pending) ++state.completion_waits_native_pending;
    if(state.completion_waits<=16 || state.completion_waits%1000==0 ||
       (native_pending && state.completion_waits_native_pending<=8))
      REXLOG_INFO("Native fence wait probe: device={:#x}, target={}, issued={}, guest_completed={}, guest_pending={}, native_known={}, native_completed={}, native_pending={}, waits={}, native_pending_waits={}; observational only",
        device,target,issued,guest_completed,edf::native::GuestFencePending(issued,target,guest_completed),
        native_completed.has_value(),native_completed.value_or(0),native_pending,state.completion_waits,
        state.completion_waits_native_pending);
    if(REXCVAR_GET(edf_native_validate_wait) && target) {
      if(queue==state.completion_queues.end() || !queue->second)
        throw std::runtime_error("native wait validation has no submitted event queue");
      const auto started=std::chrono::steady_clock::now();
      // A diagnostic deadline, not a guessed conversion of the retail tick
      // timeout. This does not replace guest error handling or its counters.
      const auto result=queue->second->WaitUntil(issued,target,started+std::chrono::seconds(2));
      if(result!=edf::native::NativeWaitResult::Complete)
        throw std::runtime_error(result==edf::native::NativeWaitResult::Unsubmitted ?
          "native wait validation target was not submitted" :
          result==edf::native::NativeWaitResult::TimedOut ? "native wait validation timed out" :
          "native wait validation cancelled");
      if(++state.completion_validated_waits<=8 || state.completion_validated_waits%1000==0)
        REXLOG_INFO("Native wait validated: device={:#x}, target={}, completed={}, elapsed_ms={}, count={}; guest counters unchanged",
          device,target,queue->second->completed().value_or(0),
          std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count(),
          state.completion_validated_waits);
    }
  } catch(const std::exception& error) {
    state.completion_faults.insert(device);
    REXLOG_ERROR("Native fence wait probe disabled for device {:#x}: {}",device,error.what());
  }
}

// sub_820AB2F8 destroys the 72-byte shader-record array and clears its owner
// pointer/count/capacity. Native resources must not outlive that ownership.
REX_EXTERN(__imp__sub_820AB2F8);
REX_HOOK_RAW(sub_820AB2F8) {
  if (REXCVAR_GET(edf_native_shader_bridge)) edf::native::ForgetOwner(ctx.r3.u32);
  __imp__sub_820AB2F8(ctx, base);
}

REX_EXTERN(__imp__sub_82201458);
REX_HOOK_RAW(sub_82201458) { edf::native::ImportTexture(ctx, base, __imp__sub_82201458); }
// Attribution only: retain every original call and limit nested measurements
// to this thread's active image-loader scope. These phases overlap its total.
REX_EXTERN(__imp__sub_821FFB38);
REX_HOOK_RAW(sub_821FFB38) {
  edf::native::HookTiming timing(edf::native::HookPhase::TextureUpload2D,edf::native::texture_loader_depth!=0);
  __imp__sub_821FFB38(ctx,base);
}
REX_EXTERN(__imp__sub_822001E0);
REX_HOOK_RAW(sub_822001E0) {
  edf::native::HookTiming timing(edf::native::HookPhase::TextureUploadVolume,edf::native::texture_loader_depth!=0);
  __imp__sub_822001E0(ctx,base);
}
REX_EXTERN(__imp__sub_822009B0);
REX_HOOK_RAW(sub_822009B0) {
  edf::native::HookTiming timing(edf::native::HookPhase::TexturePrepare,edf::native::texture_loader_depth!=0);
  __imp__sub_822009B0(ctx,base);
}

// Final resource destruction, reached after sub_821347C0 decrements the
// reference count to zero. Remove the mapping before the address can be reused.
REX_EXTERN(__imp__sub_82134220);
REX_HOOK_RAW(sub_82134220) {
  if (REXCVAR_GET(edf_native_shader_bridge)) {
    auto& state = edf::native::State();
    // Registry retirement issues no context commands. Submitted D3D work owns
    // its resource references; registry users are serialized by state.mutex.
    // Do not wait behind the swap's refresh-pacing submission barrier.
    std::lock_guard lock(state.mutex);
    state.texture_creations.erase(ctx.r3.u32);
    for(auto& [owner,scene]:state.scenes) scene.direct_outputs.erase(ctx.r3.u32);
    state.meshes.Invalidate(ctx.r3.u32);
    state.model_buffers.Retire(ctx.r3.u32);
    state.declarations.Retire(ctx.r3.u32);
    state.depth_targets.erase(ctx.r3.u32);
    state.surface_creations.erase(ctx.r3.u32);
    state.embedded_shaders.erase(ctx.r3.u32);
    for (auto& [owner,scene]:state.scenes) if (scene.output_surface==ctx.r3.u32) {
      scene.output={}; scene.output_surface=0;
      if (state.active_output==owner) state.active_output=0;
    }
    std::erase_if(state.render_targets,[handle=ctx.r3.u32](const auto& item) {
      return item.second.texture_handle == handle || item.second.surface_handle == handle;
    });
    if (state.textures.erase(ctx.r3.u32))
      REXLOG_INFO("Native texture bridge: released handle={:#x}, resident={}", ctx.r3.u32, state.textures.size());
  }
  __imp__sub_82134220(ctx, base);
}

// Model-owned buffers are embedded resources, not necessarily reference-counted
// objects destroyed by 82134220. These cleanup routines also run before their
// creators reuse the same owner. Retire native meshes before freeing CPU storage;
// do not hold bridge locks across guest calls (cleanup can unbind a stream).
REX_EXTERN(__imp__sub_821D7468);
REX_EXTERN(__imp__sub_821D75F8);
REX_EXTERN(sub_82137410);
REX_EXTERN(sub_821375C0);
REX_EXTERN(sub_821D3EA0);
namespace {
void RetireNativeModelBuffer(uint32_t resource) {
  if (!REXCVAR_GET(edf_native_shader_bridge)) return;
  auto& state=edf::native::State();
  // Registry/cache retirement has no immediate-context work. Keep it atomic
  // with draws using the state lock, without waiting for refresh pacing.
  std::lock_guard lock(state.mutex);
  // Preserve aliases before erasing this owner: the later pool-release hook
  // cannot recover its physical extent after normal cleanup retires metadata.
  if(!state.model_buffers.RetireBackingAliases(resource,
       [&](uint32_t owner) { state.meshes.Invalidate(owner); }))
    state.meshes.Invalidate(resource);
}
void CleanupNativeModelBuffer(PPCContext& ctx,uint8_t* base,bool index) {
  const edf::native::GuestReader reader(base);
  auto work=ctx;
  bool unbound=false,released=false;
  edf::native::CleanupNativeModelResource(reader,ctx.r3.u32,index,
    [&] { return reader.Word(reader.Word(0x82000720)); },
    [&](uint32_t device,bool ib) {
      work.r3.u64=device; work.r4.u64=0;
      if(ib) { work.lr=0x821D7640; sub_821375C0(work,base); }
      else {
        work.r5.u64=0; work.r6.u64=0; work.r7.u64=0; work.r8.u64=0x1000;
        work.lr=0x821D74C8; sub_82137410(work,base);
      }
      unbound=true;
    },
    [&](uint32_t allocation) {
      work.r3.u64=allocation; work.lr=index?0x821D7648u:0x821D74D0u;
      sub_821D3EA0(work,base);
      released=true;
    });
  if(released) {
    static std::atomic<uint64_t> releases{0};
    const auto sequence=releases.fetch_add(1,std::memory_order_relaxed)+1;
    if(sequence<=8 || (sequence&(sequence-1))==0)
      REXLOG_INFO("Native model cleanup completed: owner={:#x}, index={}, unbound={}, releases={} (sampled)",
        ctx.r3.u32,index,unbound,sequence);
  }
}
}
REX_HOOK_RAW(sub_821D7468) {
  RetireNativeModelBuffer(ctx.r3.u32);
  if(REXCVAR_GET(edf_native_shader_bridge)) { CleanupNativeModelBuffer(ctx,base,false); return; }
  __imp__sub_821D7468(ctx,base);
}
REX_HOOK_RAW(sub_821D75F8) {
  RetireNativeModelBuffer(ctx.r3.u32);
  if(REXCVAR_GET(edf_native_shader_bridge)) { CleanupNativeModelBuffer(ctx,base,true); return; }
  __imp__sub_821D75F8(ctx,base);
}

// Generic pool release receives the allocation record in r4. Observe before
// its own guest critical section and before the pool makes these bytes reusable.
REX_EXTERN(__imp__sub_821D3DC8);
REX_HOOK_RAW(sub_821D3DC8) {
  if(REXCVAR_GET(edf_native_shader_bridge)) {
    auto& state=edf::native::State();
    // Allocation ownership retirement is metadata-only, as above.
    std::lock_guard lock(state.mutex);
    state.model_buffers.RetireAllocationAliases(ctx.r4.u32,
      [&](uint32_t owner) { state.meshes.Invalidate(owner); });
  }
  __imp__sub_821D3DC8(ctx,base);
}

REX_EXTERN(__imp__sub_821D3748);
REX_HOOK_RAW(sub_821D3748) {
  std::optional<edf::native::NativeBufferWrites::WriterScope> release_scope;
  // This audited edge runs under the guest pool lock. Retire by actual block
  // extent even if its original model wrapper has already left the registry.
  // Keep pool -> renderer ordering; no renderer lock spans the guest helper.
  if(REXCVAR_GET(edf_native_shader_bridge) && uint32_t(ctx.lr)==0x821D3E30) {
    const auto block=edf::native::ReadNativePoolBlock(edf::native::GuestReader(base),ctx.r3.u32,ctx.r4.u64);
    auto* memory=REX_KERNEL_MEMORY();
    if(!memory || memory->virtual_membase()!=base)
      throw std::runtime_error("native pool block release memory mismatch");
    const auto extent=edf::native::MapCompletedNativePhysicalWrite(*memory,block.address,block.bytes);
    if(block.bytes && (!extent || extent->all))
      throw std::runtime_error("invalid native pool block physical extent");
    if(extent) {
      // Exclude overlapping guarded acquisitions through the original helper,
      // including owners published after the registry retirement below.
      // Scope entry/exit takes only the queue lock, never across guest code.
      release_scope.emplace(&edf::native::BufferWrites(),
        edf::native::NativeBufferWrites::Range{extent->address,extent->bytes},
        edf::native::NativeBufferWrites::WriterKind::AllocationRelease);
      auto& state=edf::native::State();
      std::lock_guard lock(state.mutex);
      size_t retired=0;
      state.model_buffers.RetirePhysicalRange(extent->address,extent->bytes,
        [&](uint32_t owner) { state.meshes.Invalidate(owner); ++retired; });
      static uint64_t releases=0;
      const auto sequence=++releases;
      if(retired || sequence<=8 || (sequence&(sequence-1))==0)
        REXLOG_INFO("Native pool block retirement: physical={:#x}, bytes={}, owners={}, sequence={} (before block reuse; completion not implied)",
          extent->address,extent->bytes,retired,sequence);
    }
  }
  __imp__sub_821D3748(ctx,base);
}

// Whole-pool destruction has no per-owner release callbacks. Resolve all
// backing allocations before taking the registry lock, and retire before the
// original routine detaches/frees its nodes. Never hold bridge locks over free.
namespace {
[[nodiscard]] edf::native::NativeBufferWrites::ReleaseScopes RetireNativePoolBackings(uint8_t* base,std::span<const uint32_t> backings) {
    auto* memory=REX_KERNEL_MEMORY();
    if(!memory || memory->virtual_membase()!=base)
      throw std::runtime_error("native pool release memory mismatch");
    std::vector<edf::native::NativePhysicalWriteExtent> extents;
    extents.reserve(backings.size());
    for(const auto address:backings) {
      auto* heap=memory->LookupHeap(address);
      rex::memory::HeapAllocationInfo allocation{};
      if(!heap || heap->heap_type()!=rex::memory::HeapType::kGuestPhysical ||
         !heap->QueryRegionInfo(address,&allocation) || allocation.allocation_base!=address ||
         !allocation.state || !allocation.allocation_size)
        throw std::runtime_error("invalid native pool backing allocation");
      const auto extent=edf::native::MapCompletedNativePhysicalWrite(*memory,address,allocation.allocation_size);
      if(!extent || extent->all) throw std::runtime_error("invalid native pool backing physical extent");
      extents.push_back(*extent);
    }
    std::vector<edf::native::NativeBufferWrites::Range> ranges;
    ranges.reserve(extents.size());
    for(const auto extent:extents) ranges.push_back({extent.address,extent.bytes});
    auto release_scopes=edf::native::BufferWrites().BeginReleaseSet(ranges);
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    size_t retired=0;
    for(const auto extent:extents)
      state.model_buffers.RetirePhysicalRange(extent.address,extent.bytes,
        [&](uint32_t owner) { state.meshes.Invalidate(owner); ++retired; });
    static uint64_t releases=0;
    const auto sequence=++releases;
    if(sequence<=8 || (sequence&(sequence-1))==0 || retired)
      REXLOG_INFO("Native physical backing retirement: allocations={}, model_owners={}, sequence={} (before guest free; successful completion not implied)",
        extents.size(),retired,sequence);
    return release_scopes;
}
}
REX_EXTERN(__imp__sub_821D3A40);
REX_HOOK_RAW(sub_821D3A40) {
  edf::native::NativeBufferWrites::ReleaseScopes release_scopes;
  if(REXCVAR_GET(edf_native_shader_bridge)) {
    const auto backings=edf::native::ReadNativePoolBackings(edf::native::GuestReader(base),ctx.r3.u32);
    release_scopes=RetireNativePoolBackings(base,backings);
  }
  __imp__sub_821D3A40(ctx,base);
}
REX_EXTERN(__imp__sub_8212FC28);
REX_HOOK_RAW(sub_8212FC28) {
  edf::native::NativeBufferWrites::ReleaseScopes release_scopes;
  // 821D3B68 has already selected/unlinked the node and loaded its backing
  // address into r3. Observe the actual free argument, not a predicted iterator
  // target. 821D1890/821D18E0 likewise load their owned physical buffer into
  // r3 before destruction/reallocation. 821D4380's conditional temporary
  // cleanup also loads the actual nonnull backing; do not assume that branch
  // unreachable from the temporary's initial zero value. Other callers keep
  // their prior policy.
  const auto caller=uint32_t(ctx.lr);
  if(REXCVAR_GET(edf_native_shader_bridge) &&
     (caller==0x821D3BEC || caller==0x821D18B4 || caller==0x821D190C || caller==0x821D4404)) {
    const std::array<uint32_t,1> backings{ctx.r3.u32};
    release_scopes=RetireNativePoolBackings(base,backings);
  }
  __imp__sub_8212FC28(ctx,base);
}

REX_EXTERN(__imp__sub_8212F4B8);
REX_HOOK_RAW(sub_8212F4B8) {
  edf::native::NativeBufferWrites::ReleaseScopes release_scopes;
  // Preserve the retail selector: only low-word bit31 denotes physical
  // memory, and its null case is a no-op. CPU-heap releases are untouched.
  // Resolve/retire before forwarding, with no bridge lock spanning guest code.
  if(REXCVAR_GET(edf_native_shader_bridge) && (ctx.r4.u32&0x80000000u) && ctx.r3.u32) {
    const std::array<uint32_t,1> backings{ctx.r3.u32};
    release_scopes=RetireNativePoolBackings(base,backings);
  }
  __imp__sub_8212F4B8(ctx,base);
}

// The model loader copies into owner+48 storage before associating it with
// its embedded resource. Publish after the complete creator, not mid-copy.
namespace {
void PublishNativeModelBuffer(uint8_t* base,uint32_t owner,edf::native::NativeModelBuffers::Kind kind,
                              uint32_t stride,uint32_t count) {
  if(!REXCVAR_GET(edf_native_shader_bridge)) return;
  auto& state=edf::native::State();
  // Drain/apply write notifications and publish the generation atomically.
  // NativeIndexBuffer creates an immutable device resource with initial data;
  // neither it nor cache invalidation submits immediate-context commands.
  std::lock_guard lock(state.mutex);
  const edf::native::GuestReader reader(base);
  const auto address=reader.Word(reader.Add(owner,48));
  std::optional<uint32_t> physical;
  auto* memory=REX_KERNEL_MEMORY();
  const auto bytes=uint64_t(stride)*count;
  auto* heap=memory->LookupHeap(address);
  if(bytes && bytes<=0x03fffffcu && uint64_t(address)+bytes<=0x100000000ull && heap &&
     heap->heap_type()==rex::memory::HeapType::kGuestPhysical &&
     heap==memory->LookupHeap(uint32_t(address+bytes-1))) {
    const auto start=memory->GetPhysicalAddress(address);
    if(uint64_t(start)+bytes<=0x20000000ull && memory->GetPhysicalAddress(uint32_t(address+bytes-1))==start+bytes-1)
      physical=start;
  }
  // The constructor's bulk copy is already queued. Apply older notifications
  // before publishing this completed generation, otherwise its first draw would
  // immediately retire the just-created storage for its own initialization.
  if(edf::native::BufferWrites().Pending()) {
    const auto writes=edf::native::BufferWrites().Drain();
    edf::native::AuditGeneratedWrites(state,writes);
    size_t affected=0;
    state.model_buffers.ApplyWrites(writes,[&](uint32_t resource) { state.meshes.Invalidate(resource); ++affected; });
    if(writes.all) ++state.buffer_write_all_batches;
    const bool report_all=writes.all && (state.buffer_write_all_batches & (state.buffer_write_all_batches-1))==0;
    if(++state.buffer_write_batches<=8 || report_all || writes.pages)
      REXLOG_INFO("Native buffer write batch: ranges={}, all={}, affected={}, all_batches={}, pages={} (before model publication)",
        writes.count,writes.all,affected,state.buffer_write_all_batches,writes.pages?writes.pages->count():0);
  }
  // Register before constructing the host snapshot so completed writes during
  // construction can reject attachment. First-use byte validation stays enabled.
  state.model_buffers.Publish(owner,kind,address,stride,count,physical);
  if(count) {
    const auto generation=state.model_buffers.Find(owner,kind)->generation;
    const std::span<const uint8_t> source{reader.Bytes(address,size_t(bytes)),size_t(bytes)};
    std::optional<edf::native::NativeBufferWrites::ObservedVersion> version;
    std::shared_ptr<const std::vector<uint8_t>> contents;
    if(physical) {
      if(auto snapshot=edf::native::BufferWrites().CopyObserved(owner,*physical,source)) {
        version=snapshot->version; contents=std::move(snapshot->contents);
      }
      // Busy/unknown owners retain metadata only. Do not fall back to an
      // unguarded publication read; first-use validation still handles them.
    } else if(kind==edf::native::NativeModelBuffers::Kind::Vertex) {
      contents=std::make_shared<const std::vector<uint8_t>>(source.begin(),source.end());
    }
    if(kind==edf::native::NativeModelBuffers::Kind::Vertex && contents)
      state.model_buffers.RetainVertexContents(owner,generation,std::move(contents),version);
    if(kind==edf::native::NativeModelBuffers::Kind::Index && (!physical || contents)) {
      if(contents && version) state.model_buffers.RetainIndexContents(owner,generation,contents,*version);
      // GPU construction is outside the queue lock and reads the owned copy for
      // physical buffers. Later writes can still reject registry attachment.
      auto index_storage=std::make_shared<const edf::native::NativeIndexBuffer>(EnsureSceneBackendLocked(state),
        contents?std::span<const uint8_t>(*contents):source,stride,contents);
      if(version) state.model_buffers.CommitObservedIndex(owner,generation,*version,std::move(index_storage));
      else state.model_buffers.RetainIndexStorage(owner,generation,std::move(index_storage));
    }
  }
  if(kind==edf::native::NativeModelBuffers::Kind::Index && count) {
    // Summing all retained payloads at every publication makes model loading
    // quadratic. Sample this diagnostic; indexed-draw telemetry still reports
    // current ownership totals independently. Serialized by state.mutex above.
    static uint64_t publications=0;
    const auto publication=++publications;
    if(publication>8 && (publication & (publication-1))!=0) return;
    size_t retained=0,storage_bytes=0;
    state.model_buffers.VisitIndexStorage([&](uint32_t,const edf::native::NativeIndexBuffer& storage) {
      ++retained; storage_bytes+=storage.StorageBytes();
    });
    REXLOG_INFO("Native model index publication: owner={:#x}, indices={}, retained={}, storage_bytes={}, publication={} (sampled; CPU snapshots plus GPU payload; shared with meshes)",
      owner,count,retained,storage_bytes,publication);
  }
}
}
REX_EXTERN(__imp__sub_821D7530);
REX_EXTERN(sub_821D4700);
REX_EXTERN(sub_821E8320);
namespace {
void ConstructNativeModelBuffer(PPCContext& ctx,uint8_t* base,bool index) {
  const auto owner=ctx.r3.u32,source=ctx.r4.u32;
  const auto stride=index?2u:ctx.r5.u32,count=index?ctx.r5.u32:ctx.r6.u32;
  static std::atomic<uint64_t> constructions{0};
  const bool trace=constructions.fetch_add(1,std::memory_order_relaxed)<8;
  if(trace) REXLOG_INFO("Native model constructor: owner={:#x}, source={:#x}, index={}, stride={}, count={}",owner,source,index,stride,count);
  const uint64_t bytes=uint64_t(stride)*count;
  if(!owner || (index?false:(!stride || stride>2048 || stride%4)) || bytes>0x03fffffcu)
    throw std::runtime_error("invalid native model construction extent");
  const edf::native::GuestReader reader(base);
  // Keep the engine allocator and its CPU-visible payload until all consumers
  // have migrated. Native orchestration removes the Xbox header-builder and
  // generic relocation calls; it does not certify payload immutability.
  auto work=ctx;
  work.lr=index?0x821D76C4u:0x821D7550u;
  if(index) sub_821D75F8(work,base); else sub_821D7468(work,base);
  if(trace) REXLOG_INFO("Native model constructor: cleanup complete owner={:#x}",owner);
  work.r3.u64=reader.Add(owner,32); work.r4.u64=bytes;
  work.lr=index?0x821D76D4u:0x821D7560u;
  sub_821D4700(work,base);
  // The pool may return failure after releasing the previous allocation.
  // Do not copy through an empty/stale record or publish a successful owner.
  if(!work.r3.u32) throw std::runtime_error("native model allocation failed");
  const auto address=reader.Word(reader.Add(owner,48));
  if((bytes && (!address || address%4)) || uint64_t(address)+bytes>0x100000000ull)
    throw std::runtime_error("invalid native model allocation extent");
  if(trace) REXLOG_INFO("Native model constructor: allocation complete owner={:#x}, address={:#x}",owner,address);
  work.r3.u64=address; work.r4.u64=source; work.r5.u64=bytes;
  work.lr=index?0x821D76E8u:0x821D7574u;
  sub_821E8320(work,base);
  if(trace) REXLOG_INFO("Native model constructor: copy complete owner={:#x}",owner);
  const auto header=edf::native::NativeModelHeader(index,address,uint32_t(bytes));
  reader.StoreCpuWords(owner,header);
  reader.StoreWord(reader.Add(owner,56),index?count:stride);
  if(!index) {
    reader.StoreWord(reader.Add(owner,60),count);
    reader.StoreWord(reader.Add(owner,64),uint32_t(bytes));
  }
  *const_cast<uint8_t*>(reader.WritableBytes(reader.Add(owner,52),1,4))=1;
  PublishNativeModelBuffer(base,owner,index?edf::native::NativeModelBuffers::Kind::Index:
    edf::native::NativeModelBuffers::Kind::Vertex,stride,count);
  ctx.r3.u64=1;
}
}
REX_HOOK_RAW(sub_821D7530) {
  if(REXCVAR_GET(edf_native_shader_bridge)) { ConstructNativeModelBuffer(ctx,base,false); return; }
  const auto owner=ctx.r3.u32,stride=ctx.r5.u32,count=ctx.r6.u32;
  __imp__sub_821D7530(ctx,base);
  if(ctx.r3.u32) PublishNativeModelBuffer(base,owner,edf::native::NativeModelBuffers::Kind::Vertex,stride,count);
}
REX_EXTERN(__imp__sub_821D76A8);
REX_HOOK_RAW(sub_821D76A8) {
  if(REXCVAR_GET(edf_native_shader_bridge)) { ConstructNativeModelBuffer(ctx,base,true); return; }
  const auto owner=ctx.r3.u32,count=ctx.r5.u32;
  __imp__sub_821D76A8(ctx,base);
  if(ctx.r3.u32) PublishNativeModelBuffer(base,owner,edf::native::NativeModelBuffers::Kind::Index,2,count);
}

// Bulk writes can run under a native submission lock. Queue completed physical
// writes without entering renderer state; the next indexed draw retires affected
// storage. Scalar/inline stores and other native providers are not covered here.
namespace {
edf::native::NativeBufferWrites* NativeBufferWriteQueue(uint8_t* base,uint32_t destination,uint32_t bytes,
    std::optional<edf::native::NativeBufferWrites::Range>* range=nullptr) {
  if(range) range->reset();
  auto* memory=REX_KERNEL_MEMORY();
  if(!REXCVAR_GET(edf_native_shader_bridge) || !memory || memory->virtual_membase()!=base) return nullptr;
  const auto extent=edf::native::MapCompletedNativePhysicalWrite(*memory,destination,bytes);
  if(!extent) return nullptr;
  if(range && !extent->all) *range=edf::native::NativeBufferWrites::Range{extent->address,extent->bytes};
  return &edf::native::BufferWrites();
}
edf::native::NativeBufferWrites::WriterScope BeginNativeBufferWrite(uint8_t* base,uint32_t destination,uint32_t bytes,bool exact=false,
    edf::native::NativeBufferWrites::WriterKind kind=edf::native::NativeBufferWrites::WriterKind::Bulk) {
  std::optional<edf::native::NativeBufferWrites::Range> range;
  auto* queue=NativeBufferWriteQueue(base,destination,bytes,exact?&range:nullptr);
  return edf::native::NativeBufferWrites::WriterScope(queue,range,kind);
}
void NotifyCompletedNativeBufferWrite(uint8_t* base,uint32_t destination,uint32_t bytes,bool notify_versions=false,bool generated=false,
    edf::native::NativeBufferWrites::WriterSite site={nullptr,0}) {
  if(!REXCVAR_GET(edf_native_shader_bridge) || !bytes || destination<0xa0000000u) return;
  auto* memory=REX_KERNEL_MEMORY();
  if(!memory || memory->virtual_membase()!=base) return;
  const auto extent=edf::native::MapCompletedNativePhysicalWrite(*memory,destination,bytes);
  if(!extent) return;
  if(site.provider && !REXCVAR_GET(edf_native_retirement_audit)) site={};
  edf::native::BufferWrites().Record(extent->address,extent->bytes,generated,site);
  if(!extent->all && notify_versions && REXCVAR_GET(edf_native_mesh_watch_audit))
    edf::native::NotifyPhysicalProviderWrite(*memory,extent->address,extent->bytes);
}
}
REX_EXTERN(__imp__sub_821E8320);
// Called by the hash-gated native lock tail only after retained fence/range
// services return. The following suffix writes header +0, +0x14 or +0x18.
edf::native::NativeBufferWrites::WriterScope edf_native_begin_lock_header_write(uint8_t* base,uint32_t owner) {
  return BeginNativeBufferWrite(base,owner,28,true);
}
void edf_native_complete_lock_header_write(uint8_t* base,uint32_t owner) {
  NotifyCompletedNativeBufferWrite(base,owner,28);
}
edf::native::NativeBufferWrites::WriterScope edf_native_begin_fence_counter_write(uint8_t* base,uint32_t total) {
  return BeginNativeBufferWrite(base,total,8,true);
}
void edf_native_complete_fence_counter_write(uint8_t* base,uint32_t total) {
  NotifyCompletedNativeBufferWrite(base,total,8);
}
edf::native::NativeBufferWrites::WriterScope edf_native_begin_counter_reset_write(uint8_t* base,uint32_t device) {
  return BeginNativeBufferWrite(base,edf::native::GuestReader(base).Add(device,20000),48,true);
}
void edf_native_complete_counter_reset_write(uint8_t* base,uint32_t device) {
  NotifyCompletedNativeBufferWrite(base,edf::native::GuestReader(base).Add(device,20000),48);
}
void edf_native_counter_store_word(uint8_t* base,uint32_t address,uint32_t value) {
  const auto writer=BeginNativeBufferWrite(base,address,4,true);
  edf::native::GuestReader(base).StoreWord(address,value);
  NotifyCompletedNativeBufferWrite(base,address,4);
}
REX_EXTERN(__imp__sub_82139228);
REX_EXTERN(__imp__sub_82138858);
REX_HOOK_RAW(sub_82138858) {
  if(REXCVAR_GET(edf_native_host)) {
    const edf::native::GuestReader reader(base);
    if(edf::native::RunNativePixIdle(ctx,reader,[&](uint32_t callback,auto& work) {
      rex::runtime::ResolveIndirectFunction(callback)(work,base);
    })) return;
  }
  __imp__sub_82138858(ctx,base);
}
REX_EXTERN(__imp__edf_native_counter_reset_cpu_tail);
REX_HOOK_RAW(sub_82139228) {
  if(REXCVAR_GET(edf_native_host)) __imp__edf_native_counter_reset_cpu_tail(ctx,base);
  else __imp__sub_82139228(ctx,base);
}
// Optional generated-store endpoint (scalar, SIMD, atomic and inline zero).
// Uncovered SDK providers remain outside it; source comparisons stay enabled.
extern "C" void edf_native_observe_guest_scalar_store(uint8_t* base,uint32_t address,uint32_t bytes,const char* file,uint32_t line) {
  NotifyCompletedNativeBufferWrite(base,address,bytes,false,true,{file,line});
}
REX_HOOK_RAW(sub_821E8320) {
  const auto destination=ctx.r3.u32,bytes=ctx.r5.u32;
  const auto caller=uint32_t(ctx.lr);
  // Mark before entering the provider, including writes to not-yet-published
  // owners. Nesting and exceptional returns release the active-write count.
  const auto writer=BeginNativeBufferWrite(base,destination,bytes,true);
  __imp__sub_821E8320(ctx,base);
  NotifyCompletedNativeBufferWrite(base,destination,bytes,false,false,{nullptr,0,0x821e8320,caller});
}
// Off-thread dirty-range accumulation atomically updates one packed min/max
// pair, including the retry-path conditional store. No nested guest services.
REX_EXTERN(__imp__sub_8213BDF8);
REX_HOOK_RAW(sub_8213BDF8) {
  const auto destination=ctx.r3.u32;
  const auto writer=BeginNativeBufferWrite(base,destination,8,true);
  __imp__sub_8213BDF8(ctx,base);
  NotifyCompletedNativeBufferWrite(base,destination,8);
}

// Independent forward-copy implementation (aligned word loop plus byte tails).
// It does not call 821E8320, and mutates r5 while copying the leading bytes.
// Capture the original extent and notify only after the guest copy completes.
REX_EXTERN(__imp__sub_821E8740);
REX_HOOK_RAW(sub_821E8740) {
  const auto destination=ctx.r3.u32,bytes=ctx.r5.u32;
  const auto caller=uint32_t(ctx.lr);
  const auto writer=BeginNativeBufferWrite(base,destination,bytes,true);
  __imp__sub_821E8740(ctx,base);
  NotifyCompletedNativeBufferWrite(base,destination,bytes,false,false,{nullptr,0,0x821e8740,caller});
}

// A320's backward memmove path writes inline and consumes r5. The forward
// branch tail-calls the already tracked 8320; equal addresses perform no write.
// Match the original signed address comparison when selecting the notification.
REX_EXTERN(__imp__sub_821EA320);
REX_HOOK_RAW(sub_821EA320) {
  const auto destination=ctx.r3.u32,bytes=ctx.r5.u32;
  const auto caller=uint32_t(ctx.lr);
  const bool backward=ctx.r3.s32>ctx.r4.s32;
  const auto writer=BeginNativeBufferWrite(base,destination,backward?bytes:0,true);
  __imp__sub_821EA320(ctx,base);
  if(backward) NotifyCompletedNativeBufferWrite(base,destination,bytes,false,false,{nullptr,0,0x821ea320,caller});
}

// Bulk fill writes its byte prefix, 16-byte blocks and tails inline. Save the
// original extent because alignment consumes r5 before the routine returns.
REX_EXTERN(__imp__sub_821E9BA0);
REX_HOOK_RAW(sub_821E9BA0) {
  const auto destination=ctx.r3.u32,bytes=ctx.r5.u32;
  const auto caller=uint32_t(ctx.lr);
  const auto writer=BeginNativeBufferWrite(base,destination,bytes,true);
  __imp__sub_821E9BA0(ctx,base);
  NotifyCompletedNativeBufferWrite(base,destination,bytes,false,false,{nullptr,0,0x821e9ba0,caller});
}

// The current SDK NtReadFile implementation completes XFile::Read synchronously
// before returning (even when reporting STATUS_PENDING for asynchronous handles).
// XFile bypasses virtual write protection for physical destinations and only
// emits its own callbacks on success. Notify the requested extent on all return
// statuses: failed/short reads must not leave a potentially changed native owner
// reusable. This is conservative and is not synchronization with a concurrent draw.
REX_EXTERN(__imp__NtReadFile);
REX_HOOK_RAW(edf_native_NtReadFile) {
  const auto destination=ctx.r8.u32,bytes=ctx.r9.u32,status=ctx.r7.u32;
  // SDK queues an APC only with a non-low-bit routine and nonnull context.
  // Without that path, audited guest outputs are data and the status block;
  // event/completion-port and memory-invalidation work changes host metadata.
  // APC-capable calls retain global exclusion for their guest queue writes.
  const bool exact=!((ctx.r5.u32&~1u) && ctx.r6.u32);
  const auto writer=BeginNativeBufferWrite(base,destination,bytes,exact,edf::native::NativeBufferWrites::WriterKind::FileRead);
  const auto status_writer=BeginNativeBufferWrite(base,status,status?8u:0u,exact,edf::native::NativeBufferWrites::WriterKind::FileRead);
  __imp__NtReadFile(ctx,base);
  NotifyCompletedNativeBufferWrite(base,destination,bytes,true);
  if(status) NotifyCompletedNativeBufferWrite(base,status,8,true);
}

// SDK bulk fill bypasses generated store instrumentation. Preserve the original
// provider, including floor(length/4) words and its register/return behavior.
REX_EXTERN(__imp__RtlFillMemoryUlong);
REX_HOOK_RAW(edf_native_RtlFillMemoryUlong) {
  const auto destination=ctx.r3.u32,bytes=ctx.r4.u32&~3u;
  // Audited SDK body only stores floor(length/4) words to this destination;
  // no secondary guest outputs or callbacks require global exclusion.
  const auto writer=BeginNativeBufferWrite(base,destination,bytes,true,edf::native::NativeBufferWrites::WriterKind::WordFill);
  __imp__RtlFillMemoryUlong(ctx,base);
  NotifyCompletedNativeBufferWrite(base,destination,bytes,true);
}

// Explicit VB/IB Unlock boundaries. Their callers finish CPU writes before
// entering these wrappers; preserve original coherency/wait behavior first.
// Other writers may bypass Lock/Unlock, so source comparisons remain required.
REX_EXTERN(__imp__sub_82134408);
REX_EXTERN(__imp__edf_native_buffer_lock_cpu_tail);
REX_HOOK_RAW(sub_82134408) {
  // AD70 is the audited texture subresource lock wrapper. Its access code is
  // 14; the extracted helper preserves parent fences, both dirty ranges and
  // the returned CPU alias, omitting only the Xbox cache packet block.
  const bool texture_lock=ctx.r4.u32==14 && uint32_t(ctx.lr)==0x8213ae24;
  if(REXCVAR_GET(edf_native_shader_bridge) && (ctx.r4.u32==10 || ctx.r4.u32==12 || texture_lock)) {
    __imp__edf_native_buffer_lock_cpu_tail(ctx,base);
    if(texture_lock) {
      static thread_local uint64_t completed=0;
      if(++completed<=3) REXLOG_INFO("Native texture lock: preserved CPU lock/fence state; omitted Xbox cache packets");
    }
  } else __imp__sub_82134408(ctx,base);
}
namespace {
void NotifyNativeBufferUpdate(uint8_t* base,uint32_t owner,bool index_buffer) {
  if(!REXCVAR_GET(edf_native_shader_bridge)) return;
  auto& state=edf::native::State();
  // Invalidate CPU ownership/cache snapshots under the registry lock. No GPU
  // commands are issued, so completed CPU writes need not wait for refresh.
  std::lock_guard lock(state.mutex);
  const bool tracked=state.model_buffers.NotifyUpdateAliases(owner,
    [&](uint32_t affected) { state.meshes.Invalidate(affected); });
  if(!tracked) {
    state.meshes.Invalidate(owner);
    // A generic (non-model) resource header can alias a published model's
    // payload. Owner-only invalidation misses that relationship. Queue its
    // physical extent through the same checked mapper as other CPU writers;
    // the next indexed consumer drains it before consulting retained storage.
    const edf::native::GuestReader reader(base);
    const auto extent=edf::native::DecodeNativeBufferUpdateExtent(
      reader.Word(reader.Add(owner,24)),reader.Word(reader.Add(owner,28)),index_buffer);
    NotifyCompletedNativeBufferWrite(base,extent.address,extent.bytes);
  }
  if(++state.buffer_update_notifications<=8)
    REXLOG_INFO("Native buffer update: owner={:#x}, model_tracked={}, notification={}",
      owner,tracked,state.buffer_update_notifications);
}
}
REX_EXTERN(__imp__sub_82141AB8);
REX_HOOK_RAW(sub_82141AB8) {
  if(REXCVAR_GET(edf_native_shader_bridge)) {
    edf::native::NativeCacheFlushCpu(ctx,edf::native::GuestReader(base));
    return;
  }
  __imp__sub_82141AB8(ctx,base);
}
REX_EXTERN(__imp__sub_821349B8);
REX_EXTERN(__imp__edf_native_unlock_821349B8);
REX_HOOK_RAW(sub_821349B8) {
  const auto owner=ctx.r3.u32;
  {
    // VB unlock passes r5=0 to 82134640, so its non-stack outputs are
    // header word 0 and conditional word +20, never word +24. Payload
    // copies have their own producer scopes; release this before state.mutex.
    const auto bytes=REXCVAR_GET(edf_native_shader_bridge)?24u:0u;
    const auto header_writer=BeginNativeBufferWrite(base,owner,bytes,true);
    if(bytes) __imp__edf_native_unlock_821349B8(ctx,base);
    else __imp__sub_821349B8(ctx,base);
    if(bytes) NotifyCompletedNativeBufferWrite(base,owner,bytes);
  }
  NotifyNativeBufferUpdate(base,owner,false);
}
REX_EXTERN(__imp__sub_82134AD8);
REX_EXTERN(__imp__edf_native_unlock_82134AD8);
REX_HOOK_RAW(sub_82134AD8) {
  const auto owner=ctx.r3.u32;
  const bool inline_indices=REXCVAR_GET(edf_native_shader_bridge) && uint32_t(ctx.lr)==0x8242D3DC;
  if(inline_indices) edf::native::NativeBufferWriteFrame::Current().RequireOwner(owner);
  {
    // Index unlock tail-calls 82134640 with r5=0: header word 0 and
    // conditional word +0x14 are its guest header outputs, not +0x18.
    // This header contract holds for every caller, independently of the
    // special twelve-byte inline payload and its physical mapping.
    const auto bytes=REXCVAR_GET(edf_native_shader_bridge)?24u:0u;
    const auto header_writer=BeginNativeBufferWrite(base,owner,bytes,true,
      edf::native::NativeBufferWrites::WriterKind::InlineIndices);
    if(bytes) __imp__edf_native_unlock_82134AD8(ctx,base);
    else __imp__sub_82134AD8(ctx,base);
    if(bytes) NotifyCompletedNativeBufferWrite(base,owner,bytes);
  }
  if(inline_indices) edf::native::NativeBufferWriteFrame::Current().Finish(owner,[&](uint32_t destination) {
    NotifyCompletedNativeBufferWrite(base,destination,12);
  });
  // No inline payload scope may remain active while this takes state.mutex.
  // The queued exact write already protects a draw that gets that lock first.
  NotifyNativeBufferUpdate(base,owner,true);
}
// Ghidra/assembly: this producer writes six uint16 indices at 8242D3B0..D3D0
// between the lock return D3A8 and unlock return D3DC. Do not generalize this
// extent to arbitrary index locks, saved aliases, or other producer callbacks.
REX_EXTERN(__imp__sub_8242D2B0);
REX_HOOK_RAW(sub_8242D2B0) {
  if(!REXCVAR_GET(edf_native_shader_bridge)) { __imp__sub_8242D2B0(ctx,base); return; }
  edf::native::NativeBufferWriteFrame frame;
  __imp__sub_8242D2B0(ctx,base);
  frame.RequireFinished();
}
REX_EXTERN(__imp__sub_82134A78);
REX_EXTERN(__imp__edf_native_lock_82134A78);
REX_EXTERN(__imp__sub_82134958);
REX_EXTERN(__imp__edf_native_lock_82134958);
REX_HOOK_RAW(sub_82134958) {
  if(REXCVAR_GET(edf_native_shader_bridge)) __imp__edf_native_lock_82134958(ctx,base);
  else __imp__sub_82134958(ctx,base);
}
REX_HOOK_RAW(sub_82134A78) {
  const auto owner=ctx.r3.u32;
  const bool inline_indices=REXCVAR_GET(edf_native_shader_bridge) && uint32_t(ctx.lr)==0x8242D3A8;
  if(REXCVAR_GET(edf_native_shader_bridge)) __imp__edf_native_lock_82134A78(ctx,base);
  else __imp__sub_82134A78(ctx,base);
  if(inline_indices) {
    // Fence callbacks and lock-entry writes have already returned. The six
    // payload stores follow; unlock header writes have an independent guard.
    std::optional<edf::native::NativeBufferWrites::Range> range;
    auto* queue=NativeBufferWriteQueue(base,ctx.r3.u32,12,&range);
    edf::native::NativeBufferWriteFrame::Current().Begin(owner,queue,ctx.r3.u32,range);
  }
}

// Surface creation uses width/height/format/MSAA in r3-r6. Floating depth
// uses host D32S8 in this development bridge; this does not reproduce the
// guest's 24-bit floating quantization or establish frame equivalence.
REX_EXTERN(__imp__sub_8213B850);
REX_HOOK_RAW(sub_8213B850) {
  const auto width=ctx.r3.u32,height=ctx.r4.u32,format=ctx.r5.u32,msaa=ctx.r6.u32;
  __imp__sub_8213B850(ctx,base);
  if (REXCVAR_GET(edf_native_shader_bridge) && ctx.r3.u32) {
    auto& state=edf::native::State();
    // Device-only creation/registry replacement; no context submission here.
    std::lock_guard lock(state.mutex);
    state.surface_creations.insert_or_assign(ctx.r3.u32,edf::native::SurfaceCreation{width,height,format,msaa});
    state.depth_targets.erase(ctx.r3.u32);
    if (format==0x1a220197) {
      try {
        if (msaa) {
          REXLOG_INFO("Native depth allocation: {}x{}, MSAA={} not supported",width,height,msaa);
        } else {
          auto target=edf::native::CreateNativeDepthTarget(*state.device.Get(),width,height,DXGI_FORMAT_D32_FLOAT_S8X24_UINT);
          state.depth_targets.insert_or_assign(ctx.r3.u32,std::move(target));
          REXLOG_INFO("Native depth allocation: handle={:#x}, {}x{}, host=D32S8 (development)",ctx.r3.u32,width,height);
        }
      } catch (const std::exception& error) { REXLOG_ERROR("Native depth allocation: {}",error.what()); }
    }
  }
}

// Clear's native flags are low four color-target bits, bit 4 depth, bit 5
// stencil (verified in 821334E8), not desktop D3D9's 1/2/4 flag values.
REX_EXTERN(__imp__sub_821340D0);
REX_HOOK_RAW(sub_821340D0) {
  if (REXCVAR_GET(edf_native_shader_bridge) && (ctx.r6.u32&15)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    try {
      const edf::native::GuestReader reader(base);
      auto word=[&](uint32_t offset) { return reader.Word(reader.Add(ctx.r3.u32,offset)); };
      // 82133D20 reads the four actual color surfaces at device+12168..12180.
      // Match resource identity, not the last high-level Begin scope. Never
      // interpret r7 as an address: 821340D0 unpacks that packed ARGB value.
      for (uint32_t slot=0;slot<4;++slot) {
        if (!(ctx.r6.u32&(1u<<slot))) continue;
        const auto surface=word(12168+slot*4);
        for (auto& [owner,registered]:state.render_targets) {
          if (!surface || registered.surface_handle!=surface) continue;
          auto& target=registered.native;
          // Partial/tiled clears need their own rectangle mapping. Retain no
          // claim of current initialized contents when that write is skipped.
          if (ctx.r5.u32 || word(11584) || word(12376) || word(12380) ||
              word(12384)!=target.sampled.width || word(12388)!=target.sampled.height ||
              (*reader.Bytes(reader.Add(ctx.r3.u32,10808),1)&0x30)) {
            target.content_valid=false;
            if (++state.color_clear_skips<=5)
              REXLOG_INFO("Native color clear skipped: surface={:#x}, rectangles={:#x}, viewport={}x{}, target={}x{}",
                surface,ctx.r5.u32,word(12384),word(12388),target.sampled.width,target.sampled.height);
            continue;
          }
          edf::native::ClearNativeColorTarget(*state.context.Get(),target,ctx.r7.u32);
          if (++state.color_clears<=5)
            REXLOG_INFO("Native color clear: surface={:#x}, slot={}, ARGB={:#x}, initialized=true",surface,slot,ctx.r7.u32);
        }
      }
    } catch (const std::exception& error) {
      REXLOG_ERROR("Native color clear: {}",error.what());
    }
  }
  if (REXCVAR_GET(edf_native_shader_bridge) && (ctx.r6.u32&0x30)) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    try {
      const edf::native::GuestReader reader(base);
      auto word=[&](uint32_t offset) { return reader.Word(reader.Add(ctx.r3.u32,offset)); };
      const auto found=state.depth_targets.find(word(12184));
      if (found!=state.depth_targets.end()) {
        auto& target=found->second;
        // Guest clears intersect viewport/scissor and may carry explicit
        // rectangles. D3D11 ClearDepthStencilView clears the whole resource.
        // Never expand an unimplemented partial clear into a whole-surface one.
        if (ctx.r5.u32 || word(11584) || word(12376) || word(12380) ||
            word(12384)!=target.width || word(12388)!=target.height) {
          ++state.depth_clear_skips;
          if (ctx.r6.u32&0x10) target.depth_valid=false;
          if (ctx.r6.u32&0x20) target.stencil_valid=false;
        } else {
          edf::native::ClearNativeDepthTarget(*state.context.Get(),target,
            (ctx.r6.u32&0x10)!=0,(ctx.r6.u32&0x20)!=0,float(ctx.f1.f64),uint8_t(ctx.r9.u32));
          ++state.depth_clears;
          if (state.depth_clears<=5 || state.depth_clears%1000==0)
            REXLOG_INFO("Native depth clear: cleared={}, skipped={}, flags={:#x}, depth={}, stencil={}",
              state.depth_clears,state.depth_clear_skips,ctx.r6.u32,ctx.f1.f64,ctx.r9.u32);
        }
      }
    } catch (const std::exception& error) {
      if (++state.depth_errors<=10) REXLOG_ERROR("Native depth clear: {}",error.what());
    }
  }
  __imp__sub_821340D0(ctx,base);
}

// Engine scene begin: original sets up tile replay, then a full-frame viewport.
// Native graphics uses one full-resolution HDR surface, not the eDRAM tile
// dimensions. Sample count comes from the actual guest color surface; host
// multisample storage/resolves do not reproduce the Xbox eDRAM tile layout.
// Replace the audited full-scene viewport's CPU state in untiled mode. Other
// callers retain their original setter, with bounded clamp diagnostics.
REX_EXTERN(__imp__sub_821371D0);
REX_HOOK_RAW(sub_821371D0) {
  edf::native::HookTiming hook_timing(edf::native::HookPhase::ViewportHook);
  const auto device=ctx.r3.u32;
  const auto caller=uint32_t(ctx.lr);
  const bool audit=REXCVAR_GET(edf_native_host);
  std::array<uint32_t,6> requested{};
  if(audit) requested=edf::native::ReadGuestWords<6>(edf::native::GuestReader(base),ctx.r4.u32);
  if(audit) {
    const edf::native::GuestReader reader(base);
    auto& state=edf::native::State();
    edf::native::HookTiming lock_timing(edf::native::HookPhase::ViewportLock);
    std::lock_guard lock(state.mutex);
    lock_timing.Finish();
    // Initial caller retains its owner in r31. Later setters must target the
    // active native scene's color surface, not a smaller post-process target.
    const bool initial=caller==0x8219C828;
    const auto owner=initial?ctx.r31.u32:state.active_scene;
    const bool native_scene=initial || (owner && state.untiled_devices.contains(device) &&
      reader.Word(reader.Add(owner,8))==device &&
      reader.Word(reader.Add(device,12168))==state.scenes.at(owner).color_surface);
    if(native_scene) {
    edf::native::HookTiming read_timing(edf::native::HookPhase::ViewportRead);
    if(!state.untiled_devices.contains(device) || reader.Word(reader.Add(owner,8))!=device)
      throw std::runtime_error("native scene viewport without matching scene begin");
    const auto raw=edf::native::ReadViewportWords(reader,device);
    const auto replacement=edf::native::MakeNativeSceneViewportCpuState(requested,
      reader.Word(reader.Add(owner,84)),reader.Word(reader.Add(owner,88)),
      {raw.words[6],raw.words[7],raw.words[8],raw.words[9]},raw.scissor_enabled,
      edf::native::ReadGuestWords<2>(reader,reader.Add(device,10308)),initial);
    read_timing.Finish();
    edf::native::HookTiming write_timing(edf::native::HookPhase::ViewportWrite);
    std::array<uint32_t,6> transform_words{};
    for(uint32_t i=0;i<6;++i) transform_words[i]=std::bit_cast<uint32_t>(replacement.transform[i]);
    const auto dirty=reader.DoubleWord(reader.Add(device,24))|0xfcu;
    reader.StoreCpuWords(reader.Add(device,12376),replacement.viewport);
    reader.StoreCpuWords(reader.Add(device,10376),transform_words);
    reader.StoreCpuWords(reader.Add(device,10308),replacement.packed_scissor);
    reader.StoreCpuWords(reader.Add(device,24),std::array<uint32_t,2>{uint32_t(dirty>>32),uint32_t(dirty)});
    write_timing.Finish();
    static thread_local std::set<uint32_t> native_callers;
    if(native_callers.size()<16 && native_callers.insert(caller).second)
      REXLOG_INFO("Native untiled scene viewport: caller={:#x}, requested={}x{}, stored={}x{}, CPU transform/scissor updated without tile packets",
        caller,requested[2],requested[3],replacement.viewport[2],replacement.viewport[3]);
    return;
    }
  }
  {
    edf::native::HookTiming original_timing(edf::native::HookPhase::ViewportGuest);
    __imp__sub_821371D0(ctx,base);
  }
  if(audit) {
    const edf::native::GuestReader reader(base);
    const auto actual=edf::native::ReadViewportWords(reader,device);
    static thread_local uint32_t reports=0;
    if(reports<12 && (requested[2]!=actual.words[2] || requested[3]!=actual.words[3])) {
      ++reports;
      const auto surface=reader.Word(reader.Add(device,12168));
      REXLOG_INFO("Untiled viewport clamp: caller={:#x}, requested={}x{} at {},{}, stored={}x{} at {},{}, surface={:#x}, descriptor={:#x}, scissor={},{},{},{} enabled={}",
        caller,requested[2],requested[3],requested[0],requested[1],actual.words[2],actual.words[3],actual.words[0],actual.words[1],
        surface,surface?reader.Word(reader.Add(surface,36)):0,
        actual.words[6],actual.words[7],actual.words[8],actual.words[9],actual.scissor_enabled);
    }
  }
}
REX_EXTERN(__imp__sub_82137F98);
REX_HOOK_RAW(sub_82137F98) {
  edf::native::HookTiming timing(edf::native::HookPhase::ColorTarget);
  __imp__sub_82137F98(ctx,base);
}
REX_EXTERN(__imp__sub_82137CB8);
REX_HOOK_RAW(sub_82137CB8) {
  edf::native::HookTiming timing(edf::native::HookPhase::DepthTarget);
  const auto device=ctx.r3.u32;
  __imp__sub_82137CB8(ctx,base);
  edf::native::PublishNativeRenderState(base,device,0x82137cb8);
}
REX_HOOK_RAW(sub_821409A0) {
  edf::native::HookTiming timing(edf::native::HookPhase::TilingBegin);
  {
    if(uint32_t(ctx.lr)!=0x8219C654)
      throw std::runtime_error("untiled scene encountered an unaudited begin caller");
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    if(!state.untiled_devices.insert(ctx.r3.u32).second)
      throw std::runtime_error("untiled scene began before prior scene ended");
    // The engine caller already bound its color/depth surfaces and computed
    // the authored clear color. C7A8's native hook performs the actual clear.
    // No Xbox recording descriptors, tile replay state or tile packets.
    ctx.r3.u64=0;
    return;
  }
}
REX_HOOK_RAW(sub_82140E98) {
  edf::native::HookTiming timing(edf::native::HookPhase::TilingEnd);
  {
    if(uint32_t(ctx.lr)!=0x8219C990 && uint32_t(ctx.lr)!=0x8219C6B8)
      throw std::runtime_error("untiled scene encountered an unaudited end caller");
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    if(!state.untiled_devices.erase(ctx.r3.u32))
      throw std::runtime_error("untiled scene ended without a matching begin");
    // The enclosing native C930/C840 hooks own HDR resolve/publication. The
    // enclosing engine function restores ordinary target bindings afterward.
    ctx.r3.u64=0;
    return;
  }
}
// The engine ignores video-mode dimensions and initializes its renderer to
// 1280x720/960. Replace that choice before device/resource initialization, so
// all consumers (scene, depth, post pyramid and UI) see one consistent extent.
// Audited call: 8219E3B8 -> 82139A40 at LR 8219E4D4; r7 is presentation
// parameters, r8 is &renderer.device at renderer+8. Later consumers reload
// renderer+84/+88 rather than retaining the fixed dimensions in registers.
REX_EXTERN(__imp__sub_82139A40);
REX_HOOK_RAW(sub_82139A40) {
  if (uint32_t(ctx.lr)==0x8219E4D4u) {
    const auto width=edf::native::NativeRenderDimensions()[0];
    const auto height=edf::native::NativeRenderDimensions()[1];
    if(width || height) {
      if(width<640 || width>4095 || height<480 || height>4095)
        throw std::runtime_error("native render dimensions require width 640..4095 and height 480..4095");
      const edf::native::GuestReader reader(base);
      if(ctx.r8.u32<8 || reader.Word(ctx.r7.u32)!=1280 || reader.Word(reader.Add(ctx.r8.u32,76))!=1280)
        throw std::runtime_error("native resolution initialization contract changed");
      const std::array<uint32_t,2> dimensions{uint32_t(width),uint32_t(height)};
      reader.StoreCpuWords(ctx.r7.u32,dimensions);
      reader.StoreCpuWords(reader.Add(ctx.r8.u32,76),dimensions);
      REXLOG_INFO("Native render resolution selected: {}x{} (engine resource initialization)",width,height);
    }
  }
  __imp__sub_82139A40(ctx,base);
}
REX_EXTERN(__imp__sub_8219C7A8);
REX_HOOK_RAW(sub_8219C7A8) {
  edf::native::HookTiming setup_timing(edf::native::HookPhase::SceneSetup);
  const auto owner=ctx.r3.u32;
  {
    edf::native::HookTiming original_timing(edf::native::HookPhase::SceneSetupGuest);
    __imp__sub_8219C7A8(ctx,base);
  }
  edf::native::HookTiming native_timing(edf::native::HookPhase::SceneSetupNative);
  if (REXCVAR_GET(edf_native_shader_bridge) && ctx.r3.u32) {
    auto& state=edf::native::State();
    edf::native::HookTiming lock_timing(edf::native::HookPhase::SceneSetupLock);
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    lock_timing.Finish();
    try {
      const edf::native::GuestReader reader(base);
      const auto width=reader.Word(reader.Add(owner,84)),height=reader.Word(reader.Add(owner,88));
      const auto color_surface=reader.Word(reader.Add(reader.Word(reader.Add(owner,8)),12168));
      const auto& creation=state.surface_creations.at(color_surface);
      // Pin the choice for this renderer lifetime, including later scene recreation.
      static const int32_t sample_override=REXCVAR_GET(edf_native_msaa);
      const uint32_t samples=edf::native::NativeSceneSamples(creation.msaa,sample_override);
      auto found=state.scenes.find(owner);
      if (found==state.scenes.end() || found->second.color.sampled.width!=width || found->second.color.sampled.height!=height || found->second.samples!=samples) {
        edf::native::NativeScene scene{
          edf::native::CreateNativeRenderTarget(*state.device.Get(),width,height,DXGI_FORMAT_R16G16B16A16_FLOAT,samples),
          edf::native::CreateNativeDepthTarget(*state.device.Get(),width,height,DXGI_FORMAT_D32_FLOAT_S8X24_UINT,samples)};
        scene.samples=samples;
        found=state.scenes.insert_or_assign(owner,std::move(scene)).first;
        REXLOG_INFO("Native full scene allocated: owner={:#x}, {}x{}, samples={}, guest_surface={:#x}",owner,width,height,samples,color_surface);
      }
      state.active_scene=owner;
      if(REXCVAR_GET(edf_native_hook_timings)) {
        // Sparse diagnostic samples; never wait for query readiness or invent
        // a GPU duration from CPU submission time. Includes GPU idle gaps.
        try {
          if(!state.scene_gpu_timer)
            state.scene_gpu_timer=std::make_unique<edf::native::NativeGpuTimer>(*state.device.Get(),*state.context.Get());
          auto& timer=*state.scene_gpu_timer;
          if(timer.active()) timer.Cancel(); // Previous scene took an alternate exit.
          state.scene_gpu_timer_owner=0;
          state.scene_gpu_timer_resolved=false;
          while(const auto sample=timer.Poll()) {
            const auto duration=sample->Milliseconds();
            if(duration && sample->middle) REXLOG_INFO("Native frame GPU span: scene={}, scene_ms={}, post_ms={}, total_ms={} (includes submission gaps; excludes presentation)",sample->tag,
              double(*sample->middle-sample->begin)*1000.0/double(sample->frequency),
              double(sample->end-*sample->middle)*1000.0/double(sample->frequency),*duration);
            else REXLOG_INFO("Native scene GPU span unreliable: scene={}",sample->tag);
          }
          if(state.scene_begins%60==0 && timer.pending()<8) {
            timer.Begin(state.scene_begins+1);
            state.scene_gpu_timer_owner=owner;
          }
        } catch(const std::exception& error) {
          state.scene_gpu_timer.reset(); state.scene_gpu_timer_owner=0;
          REXLOG_ERROR("Native scene GPU timing: {}",error.what());
        }
      }
      state.active_output=0;
      state.scene_indexed_start=state.indexed_submitted;
      state.visibility.clear();
      auto& scene=found->second;
      scene.color_surface=color_surface;
      // 8219C5A8 computes the float clear color at 8257BFC0 and passes it to
      // BeginTiling. Use those actual values rather than owner color guesses.
      float color[4];
      for (uint32_t i=0;i<4;++i) color[i]=std::bit_cast<float>(reader.Word(0x8257bfc0+i*4));
      {
        edf::native::HookTiming clear_timing(edf::native::HookPhase::SceneClear);
        state.context->ClearRenderTargetView(scene.color.target.Get(),color);
        edf::native::ClearNativeDepthTarget(*state.context.Get(),scene.depth,true,true,
          std::bit_cast<float>(reader.Word(0x820009a4)),0);
      }
      // The whole surface now contains defined pixels. Native post-processing
      // may sample its explicit resolve, but scene completeness is independent.
      scene.color.content_valid=true;
      scene.frame_complete=false; // Remaining producers/presentation are not covered.
      edf::native::BindActiveTarget(state);
      edf::native::ReadNativeDrawViewport(reader,reader.Word(reader.Add(owner,8))).Bind(*state.context.Get());
      if (++state.scene_begins<=5 || state.scene_begins%1000==0)
        REXLOG_INFO("Native full scene begin: count={}, clear_depth={}, color={},{},{},{}",state.scene_begins,
          std::bit_cast<float>(reader.Word(0x820009a4)),color[0],color[1],color[2],color[3]);
    } catch (const std::exception& error) { state.active_scene=0; REXLOG_ERROR("Native full scene begin: {}",error.what()); }
  }
}
// Ends the tiled pass and switches to the ordinary surface pair. r4==1
// resolves scene HDR to owner+104; other modes discard the tiled contents.
REX_EXTERN(__imp__sub_8219C930);
REX_HOOK_RAW(sub_8219C930) {
  const auto output_owner=ctx.r3.u32;
  if (REXCVAR_GET(edf_native_shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    if (state.active_scene==ctx.r3.u32) {
      state.active_scene=0;
      edf::native::BindActiveTarget(state);
      // Only mode1 resolves to owner+104; discard mode must retain old sampled
      // contents. Do not synthesize the separate end-frame/backbuffer resolve.
      if (ctx.r4.u32==1) edf::native::ResolveScene(edf::native::GuestReader(base),state,ctx.r3.u32);
      if(state.scene_gpu_timer_owner==ctx.r3.u32 && state.scene_gpu_timer) {
        try {
          if(ctx.r4.u32==1) {
            state.scene_gpu_timer->MarkMiddle();
            state.scene_gpu_timer_resolved=true;
          } else {
            state.scene_gpu_timer->Cancel();
            state.scene_gpu_timer_owner=0;
          }
        } catch(const std::exception& error) {
          state.scene_gpu_timer.reset();
          state.scene_gpu_timer_owner=0;
          REXLOG_ERROR("Native scene GPU timing end: {}",error.what());
        }
      }
      edf::native::CaptureScene(state,ctx.r3.u32);
      if (++state.scene_ends<=5 || state.scene_ends%1000==0)
        REXLOG_INFO("Native full scene end: count={}, resolve_mode={}, frame_complete=false",state.scene_ends,ctx.r4.u32);
    }
  }
  __imp__sub_8219C930(ctx,base);
  if (REXCVAR_GET(edf_native_shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    state.active_output=0;
    try {
      const edf::native::GuestReader reader(base);
      auto& scene=state.scenes.at(output_owner);
      const auto surface=reader.Word(reader.Add(output_owner,112));
      const auto& creation=state.surface_creations.at(surface);
      if (state.scene_ends<=5)
        REXLOG_INFO("Native ordinary output contract: owner={:#x}, surface={:#x}, {}x{}, format={:#x}, MSAA={}",
          output_owner,surface,creation.width,creation.height,creation.format,creation.msaa);
      if (creation.msaa || creation.width!=scene.color.sampled.width || creation.height!=scene.color.sampled.height ||
          (creation.format!=0x1a220186 && creation.format!=0x18280186))
        throw std::runtime_error("unsupported ordinary output surface contract");
      if (scene.output_surface!=surface || !scene.output.target) {
        scene.output=edf::native::CreateNativeRenderTarget(*state.device.Get(),creation.width,creation.height,DXGI_FORMAT_R8G8B8A8_UNORM);
        scene.output_surface=surface;
      }
      scene.output.content_valid=false;
      state.active_output=output_owner;
      edf::native::BindActiveTarget(state);
    } catch (const std::exception& error) {
      if (state.scene_ends<=5) REXLOG_INFO("Native ordinary output pending: {}",error.what());
    }
  }
}
// End-frame can finish the tiled pass directly without the C930 intermediate
// resolve. Close that native scope too, before any following UI/frame work.
REX_EXTERN(__imp__sub_8219C840);
REX_HOOK_RAW(sub_8219C840) {
  if (REXCVAR_GET(edf_native_shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    if(REXCVAR_GET(edf_native_loading_trace)) {
      // Observe before either scope is closed. These are eligibility counts,
      // not successful publications or proof that loading UI pixels are visible.
      const auto owner=ctx.r3.u32;
      const bool output=state.active_output==owner;
      const auto found=state.scenes.find(owner);
      const bool valid=output && found!=state.scenes.end() && found->second.output.content_valid;
      const size_t route=output ? (valid?0:1) : (state.active_scene==owner?2:3);
      ++state.loading_trace_routes[route];
      const auto now=std::chrono::steady_clock::now();
      if(++state.loading_trace_frames<=8 || now-state.loading_trace_reported>=std::chrono::seconds(1)) {
        state.loading_trace_reported=now;
        REXLOG_INFO("Native loading frame: frames={}, owner={:#x}, scene={:#x}, output={:#x}, output_valid={}, handoff={}, eligible={}, invalid_output={}, direct_scene={}, no_scope={}, xui_total={}, font_total={}, movie_total={} (cumulative eligibility, not publication success or per-frame UI attribution)",
          state.loading_trace_frames,owner,state.active_scene,state.active_output,valid,
          state.presentation_frames!=nullptr,state.loading_trace_routes[0],state.loading_trace_routes[1],
          state.loading_trace_routes[2],state.loading_trace_routes[3],state.xui_draws,state.font_draws,state.movie_draws);
      }
    }
    if (state.active_output==ctx.r3.u32) {
      auto& scene=state.scenes.at(state.active_output);
      if(state.presentation_frames) {
        state.presentation_frames->Invalidate();
        if(scene.output.content_valid) {
          try { state.presentation_frames->Publish(*scene.output.surface.Get(),edf::native::NativeFrameKind::PartialScene,
            state.display_gamma?&*state.display_gamma:nullptr); }
          catch(const std::exception& error) { REXLOG_ERROR("Native frame publication: {}",error.what()); }
        }
      }
      const auto prefix=REXCVAR_GET(edf_native_scene_capture);
      const bool indexed_output=scene.output.content_valid && state.indexed_submitted>state.scene_indexed_start;
      if(indexed_output) ++state.indexed_output_frames;
      // Retail exposure adapts per frame. The first three outputs alone cannot
      // distinguish startup overexposure from a persistently broken post chain.
      const auto capture_interval=REXCVAR_GET(edf_native_output_capture_interval);
      if (!prefix.empty() && indexed_output && edf::native::ShouldCaptureNativeOutput(
          state.indexed_output_frames,state.output_captures,REXCVAR_GET(edf_native_output_capture_limit),
          capture_interval,REXCVAR_GET(edf_native_output_capture_start_frame))) {
        try {
          ++state.output_captures;
          const auto path=std::filesystem::path(prefix+".output."+std::to_string(state.indexed_output_frames)+".bmp");
          if (std::filesystem::exists(path)) throw std::runtime_error("native output capture already exists");
          const auto bmp=edf::native::CaptureNativeHdrBmp(*state.context.Get(),*scene.output.surface.Get());
          std::ofstream output(path,std::ios::binary);
          output.write(reinterpret_cast<const char*>(bmp.data()),bmp.size()); output.close();
          if (!output) throw std::runtime_error("native output capture write failed");
          REXLOG_INFO("Native bloom output capture: {}, draws={}, frame_complete=false (UI and scene coverage incomplete)",path.string(),state.output_draws);
          if(REXCVAR_GET(edf_native_output_capture_scene_color)) {
            if(!scene.color.content_valid || !scene.color.surface)
              throw std::runtime_error("paired scene-color capture has no valid scene surface");
            const auto scene_path=std::filesystem::path(prefix+".scene-color."+std::to_string(state.indexed_output_frames)+".bmp");
            if(std::filesystem::exists(scene_path)) throw std::runtime_error("paired scene-color capture already exists");
            const auto scene_bmp=edf::native::CaptureNativeHdrBmp(*state.context.Get(),*scene.color.surface.Get());
            std::ofstream scene_file(scene_path,std::ios::binary);
            scene_file.write(reinterpret_cast<const char*>(scene_bmp.data()),scene_bmp.size()); scene_file.close();
            if(!scene_file) throw std::runtime_error("paired scene-color capture write failed");
            REXLOG_INFO("Native paired scene color capture: {}, output={}, same indexed frame; HDR BMP is diagnostic, not display reference",
              scene_path.string(),path.string());
          }
          for (const auto& [owner,target]:state.render_targets) {
            const auto& sample=target.native.sampled;
            if (!sample.content_valid || sample.width>40 || sample.height>22) continue;
            const auto value=edf::native::ReadNativeColorPixel(*state.context.Get(),*sample.resource.Get(),sample.width/2,sample.height/2);
            REXLOG_INFO("Native post pixel: texture={:#x}, {}x{}, center={},{},{},{}",target.texture_handle,sample.width,sample.height,value[0],value[1],value[2],value[3]);
          }
        } catch (const std::exception& error) { REXLOG_ERROR("Native output capture: {}",error.what()); }
      }
      state.active_output=0;
      edf::native::BindActiveTarget(state);
    }
    if (state.active_scene==ctx.r3.u32) {
      try {
        const edf::native::GuestReader reader(base);
        const auto owner=ctx.r3.u32;
        auto& scene=state.scenes.at(owner);
        // C840 -> C678 resolves directly to owner[(31 + buffer_index) * 4].
        // This path has no ordinary-output/post pass and must still present UI.
        const auto index=reader.Word(reader.Add(owner,140));
        if(index>=4) throw std::runtime_error("unsupported direct frame buffer index");
        const auto handle=reader.Word(reader.Add(owner,124+index*4));
        const auto& creation=state.texture_creations.at(handle);
        if(creation.width!=scene.color.sampled.width || creation.height!=scene.color.sampled.height ||
           creation.format!=0x28280106)
          throw std::runtime_error("unsupported direct frame destination format/dimensions: "+std::to_string(creation.format));
        auto& direct=scene.direct_outputs[handle];
        if(!direct.surface)
          direct=edf::native::CreateNativeOpaqueFrameTarget(*state.device.Get(),creation.width,creation.height);
        edf::native::ResolveNativeRgba8Frame(*state.context.Get(),scene.color,direct);
        state.textures.insert_or_assign(handle,direct.sampled);
        if(state.presentation_frames) {
          state.presentation_frames->Invalidate();
          if(direct.sampled.content_valid)
            state.presentation_frames->Publish(*direct.sampled.resource.Get(),edf::native::NativeFrameKind::PartialScene,
              state.display_gamma?&*state.display_gamma:nullptr);
        }
      } catch(const std::exception& error) {
        static uint64_t failures=0;
        if(++failures<=8 || !(failures&(failures-1)))
          REXLOG_ERROR("Native direct frame resolve: {} (failures={})",error.what(),failures);
      }
      state.active_scene=0;
      edf::native::BindActiveTarget(state);
      edf::native::CaptureScene(state,ctx.r3.u32);
      ++state.scene_ends;
    }
  }
  __imp__sub_8219C840(ctx,base);
}
// Renderer teardown owns the full-frame native scene, unlike individual guest
// tile resources. Clear the active scope before releasing its attachments.
REX_EXTERN(__imp__sub_8219E140);
REX_HOOK_RAW(sub_8219E140) {
  if (REXCVAR_GET(edf_native_shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    if (state.active_scene==ctx.r3.u32) state.active_scene=0;
    if (state.active_output==ctx.r3.u32) state.active_output=0;
    if (state.scenes.erase(ctx.r3.u32)) edf::native::BindActiveTarget(state);
  }
  __imp__sub_8219E140(ctx,base);
}

REX_EXTERN(__imp__sub_821B8C30);
REX_HOOK_RAW(sub_821B8C30) {
  const auto owner = ctx.r3.u32;
  __imp__sub_821B8C30(ctx,base);
  if (REXCVAR_GET(edf_native_shader_bridge) && ctx.r3.u8) {
    try { edf::native::RegisterRenderTarget(edf::native::GuestReader(base),owner); }
    catch (const std::exception& error) { REXLOG_ERROR("Native render target: {}",error.what()); }
  }
}
REX_EXTERN(__imp__sub_821B8828);
REX_HOOK_RAW(sub_821B8828) {
  const auto owner = ctx.r3.u32;
  __imp__sub_821B8828(ctx,base);
  if (REXCVAR_GET(edf_native_shader_bridge)) {
    try { edf::native::BeginRenderTarget(owner); }
    catch (const std::exception& error) { REXLOG_ERROR("Native render target begin: {}",error.what()); }
  }
}
REX_EXTERN(__imp__sub_821B88B0);
REX_HOOK_RAW(sub_821B88B0) {
  if (REXCVAR_GET(edf_native_shader_bridge)) {
    try {
      const edf::native::GuestReader reader(base);
      if (*reader.Bytes(reader.Add(ctx.r3.u32,40),1)) edf::native::EndRenderTarget(ctx.r3.u32);
    } catch (const std::exception& error) { REXLOG_ERROR("Native render target end: {}",error.what()); }
  }
  __imp__sub_821B88B0(ctx,base);
}

// D3D texture allocation: dimensions in r3-r5, mip count r6, usage r7,
// format r8, pool r9, resource type r10; returns the new resource pointer.
REX_EXTERN(__imp__sub_8213B730);
REX_HOOK_RAW(sub_8213B730) {
  if (!REXCVAR_GET(edf_native_shader_bridge)) { __imp__sub_8213B730(ctx, base); return; }
  const edf::native::TextureCreation creation{ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,
    ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32, uint32_t(ctx.lr)};
  {
    edf::native::HookTiming timing(edf::native::HookPhase::TextureAllocate,edf::native::texture_loader_depth!=0);
    __imp__sub_8213B730(ctx, base);
  }
  if (ctx.r3.u32) {
    auto& state = edf::native::State();
    // Metadata publication only: no native upload/draw or context operation.
    // Keep address-reuse invalidation atomic without joining frame pacing.
    std::lock_guard lock(state.mutex);
    state.textures.erase(ctx.r3.u32);
    state.texture_creations.insert_or_assign(ctx.r3.u32, creation);
  }
}

// CPU SetStreamSource: retain the original byte offset and full stride. The
// device's fetch descriptor encodes these and cannot serve as a native API.
namespace {
void AuditNativeRetirement(const edf::native::GuestReader& reader,uint32_t device,bool index,
    edf::native::NativeRetirementPath path,uint32_t previous,uint32_t value,uint32_t cursor) noexcept {
  if(!REXCVAR_GET(edf_native_retirement_audit)) return;
  static std::atomic<uint64_t> counts[2][4]{};
  const auto branch=static_cast<unsigned>(path);
  const auto count=counts[index?1:0][branch].fetch_add(1,std::memory_order_relaxed)+1;
  if(count>8 && (count&(count-1))) return;
  try {
    const auto descriptor=reader.Word(reader.Add(device,13140));
    REXLOG_INFO("Native retirement: index={} branch={} count={} device={:08X} previous={:08X} value={:08X} cursor={:08X} descriptor={:08X}",
      index,branch,count,device,previous,value,cursor,descriptor);
  } catch(const std::exception& error) {
    REXLOG_WARN("Native retirement audit read failed: {}",error.what());
  }
}
}
REX_EXTERN(__imp__sub_82137410);
REX_EXTERN(sub_82141440);
REX_HOOK_RAW(sub_82137410) {
  const auto device=ctx.r3.u32, stream=ctx.r4.u32;
  const edf::native::GuestStream binding{ctx.r5.u32,ctx.r6.u32,ctx.r7.u32};
  if(!REXCVAR_GET(edf_native_shader_bridge)) { __imp__sub_82137410(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  edf::native::SetNativeStreamResource(reader,device,stream,binding.resource,binding.offset,binding.stride,ctx.r8.u64,
    [&] {
      auto work=ctx;
      if(work.r1.u32<144) throw std::runtime_error("invalid stream setter guest stack");
      work.r1.u64=work.r1.u32-144u; work.r3.u64=device; work.lr=0x821374D0;
      sub_82141440(work,base);
      return work.r3.u32;
    },
    [&] {
      if(ctx.r1.u32<64) throw std::runtime_error("invalid stream setter tag address");
      return reader.Word(ctx.r1.u32-64u);
    },[&](auto path,uint32_t previous,uint32_t value,uint32_t cursor) {
      AuditNativeRetirement(reader,device,false,path,previous,value,cursor);
    });
  if (REXCVAR_GET(edf_native_shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    state.streams.insert_or_assign({device,stream},binding);
  }
}

// CPU SetIndices: own the binding at its producer, rather than re-reading the
// guest device slot for each native draw. Keep the original old-resource fence
// and deferred-retirement bookkeeping; a null binding is an explicit unbind.
REX_EXTERN(__imp__sub_821375C0);
REX_EXTERN(sub_82141440);
REX_HOOK_RAW(sub_821375C0) {
  const auto device=ctx.r3.u32,resource=ctx.r4.u32;
  if(!REXCVAR_GET(edf_native_shader_bridge)) { __imp__sub_821375C0(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  edf::native::SetNativeIndexResource(reader,device,resource,
    [&] {
      auto work=ctx;
      // Match the original caller frame while invoking the retained queue
      // allocator. In particular, do not overwrite the legacy tag at SP-48.
      if(work.r1.u32<128) throw std::runtime_error("invalid index setter guest stack");
      work.r1.u64=work.r1.u32-128u; work.r3.u64=device; work.lr=0x8213761C;
      sub_82141440(work,base);
      return work.r3.u32;
    },
    [&] {
      if(ctx.r1.u32<48) throw std::runtime_error("invalid index setter tag address");
      return reader.Word(ctx.r1.u32-48u);
    },[&](auto path,uint32_t previous,uint32_t value,uint32_t cursor) {
      AuditNativeRetirement(reader,device,true,path,previous,value,cursor);
    });
  if(REXCVAR_GET(edf_native_shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    state.index_bindings.insert_or_assign(device,resource);
  }
}

namespace {
void PublishNativeShaderBinding(uint32_t device,uint32_t shader,bool pixel) {
  if(!REXCVAR_GET(edf_native_shader_bridge)) return;
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  state.shader_bindings.Set(device,shader,pixel);
}
void PublishNativeMaterialParameters(uint8_t* base,uint32_t instance) {
  if(!REXCVAR_GET(edf_native_shader_bridge)) return;
  auto& state=edf::native::State();
  // CPU-owned metadata publication is serialized by the registry mutex.
  std::lock_guard lock(state.mutex);
  state.material_parameters.Publish(edf::native::GuestReader(base),instance);
}
void RetireNativeMaterialParameters(uint8_t* base,uint32_t instance,bool array) {
  if(!REXCVAR_GET(edf_native_shader_bridge)) return;
  auto& state=edf::native::State();
  // Retained metadata snapshots keep their lifetime independently of this map.
  std::lock_guard lock(state.mutex);
  if(array) {
    if(instance<4) throw std::runtime_error("invalid material array header");
    const edf::native::GuestReader reader(base);
    state.material_parameters.RetireArray(instance,reader.Word(instance-4));
  } else state.material_parameters.Retire(instance);
}
void PublishNativeDeclarationContents(uint8_t* base,uint32_t handle) {
  if(!REXCVAR_GET(edf_native_shader_bridge) || !handle) return;
  auto& state=edf::native::State();
  // Immutable declaration snapshots issue no immediate-context commands.
  std::lock_guard lock(state.mutex);
  const edf::native::GuestReader reader(base);
  const auto count=reader.Word(reader.Add(handle,24));
  if(count>64) throw std::runtime_error("invalid published declaration count");
  const size_t bytes=size_t(count)*12;
  state.declarations.Publish(handle,bytes?std::span<const uint8_t>{reader.Bytes(reader.Add(handle,52),bytes),bytes}:
    std::span<const uint8_t>{});
}
void PublishNativeDeclarationBinding(uint32_t device,uint32_t declaration) {
  if(!REXCVAR_GET(edf_native_shader_bridge)) return;
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  state.declaration_bindings.insert_or_assign(device,declaration);
}
}
REX_EXTERN(__imp__sub_82149AB0);
REX_EXTERN(__imp__sub_821BC230);
REX_HOOK_RAW(sub_821BC230) {
  const auto instance=ctx.r3.u32;
  RetireNativeMaterialParameters(base,instance,false);
  __imp__sub_821BC230(ctx,base);
  if(ctx.r3.u32&255) PublishNativeMaterialParameters(base,instance);
}
REX_EXTERN(__imp__sub_821BD110);
REX_HOOK_RAW(sub_821BD110) {
  const auto instance=ctx.r3.u32;
  RetireNativeMaterialParameters(base,instance,false);
  __imp__sub_821BD110(ctx,base);
  PublishNativeMaterialParameters(base,instance);
}
REX_EXTERN(__imp__sub_820ABE88);
REX_HOOK_RAW(sub_820ABE88) {
  RetireNativeMaterialParameters(base,ctx.r3.u32,false);
  __imp__sub_820ABE88(ctx,base);
}
REX_EXTERN(__imp__sub_820ABF20);
REX_HOOK_RAW(sub_820ABF20) {
  RetireNativeMaterialParameters(base,ctx.r3.u32,(ctx.r4.u32&2)!=0);
  __imp__sub_820ABF20(ctx,base);
}
REX_HOOK_RAW(sub_82149AB0) {
  __imp__sub_82149AB0(ctx,base);
  PublishNativeDeclarationContents(base,ctx.r3.u32);
}
REX_EXTERN(__imp__sub_82147BA0);
REX_HOOK_RAW(sub_82147BA0) {
  const auto handle=ctx.r4.u32;
  __imp__sub_82147BA0(ctx,base);
  PublishNativeDeclarationContents(base,handle);
}
REX_EXTERN(__imp__sub_821498C8);
REX_HOOK_RAW(sub_821498C8) {
  const auto device=ctx.r3.u32,shader=ctx.r4.u32;
  __imp__sub_821498C8(ctx,base);
  PublishNativeShaderBinding(device,shader,false);
}
REX_EXTERN(__imp__sub_82149608);
REX_HOOK_RAW(sub_82149608) {
  const auto device=ctx.r3.u32,shader=ctx.r4.u32;
  __imp__sub_82149608(ctx,base);
  PublishNativeShaderBinding(device,shader,true);
}
// Both retail writers of device+11536: the explicit declaration setter and
// the FVF setter. Keep CPU dirty flags and FVF conversion; draws consume the
// native binding. B0A0 returns the converted declaration in volatile r4, not
// its input FVF value (verified by its 47BA0 call and final store).
REX_EXTERN(__imp__sub_82149A90);
REX_HOOK_RAW(sub_82149A90) {
  const auto device=ctx.r3.u32,declaration=ctx.r4.u32;
  __imp__sub_82149A90(ctx,base);
  PublishNativeDeclarationBinding(device,declaration);
}
REX_EXTERN(__imp__sub_8214B0A0);
REX_HOOK_RAW(sub_8214B0A0) {
  const auto device=ctx.r3.u32;
  __imp__sub_8214B0A0(ctx,base);
  PublishNativeDeclarationBinding(device,ctx.r4.u32);
}

// DrawIndexedVertices(device, primitive, baseVertex, firstIndex, indexCount).
// The model producer 821B2C28 uses triangle list (4). Inspect actual bound
// buffers before the original helper emits packets. Submit only inside the
// verified full-frame scene scope, never into an unrelated post-process target.
REX_EXTERN(__imp__sub_821FE358);
REX_EXTERN(__imp__edf_native_indexed_cpu_tail);
REX_HOOK_RAW(sub_821FE358) {
  bool native_submitted=false;
  bool native_geometry_rejected=false;
  // The stride and index width belong to the draw, not the declaration, and the
  // coverage catalog needs both to replay a layout at the size it was actually
  // drawn with. Resolved inside the draw, reported after it.
  uint32_t drawn_stride=0,drawn_index_width=0;
  edf::native::HookTiming native_timing(edf::native::HookPhase::IndexedNative);
  if (REXCVAR_GET(edf_native_shader_bridge)) {
    auto& state=edf::native::State();
    edf::native::HookTiming submission_wait(edf::native::HookPhase::IndexedSubmissionWait);
    std::lock_guard submission(state.submissions);
    submission_wait.Finish();
    edf::native::HookTiming context_wait(edf::native::HookPhase::IndexedContextWait);
    std::lock_guard lock(state.mutex);
    context_wait.Finish();
    ++state.indexed_draws;
    if(edf::native::BufferWrites().Pending()) {
      const auto writes=edf::native::BufferWrites().Drain();
      edf::native::AuditGeneratedWrites(state,writes);
      size_t affected=0;
      state.model_buffers.ApplyWrites(writes,[&](uint32_t owner) { state.meshes.Invalidate(owner); ++affected; });
      if(writes.all) ++state.buffer_write_all_batches;
      const bool report_all=writes.all && (state.buffer_write_all_batches & (state.buffer_write_all_batches-1))==0;
      if(++state.buffer_write_batches<=8 || report_all || writes.pages)
        REXLOG_INFO("Native buffer write batch: ranges={}, all={}, affected={}, all_batches={}, pages={} (deferred notifications)",
          writes.count,writes.all,affected,state.buffer_write_all_batches,writes.pages?writes.pages->count():0);
    }
    const bool scene_draw=state.active_scene && !state.active_target && state.scenes.contains(state.active_scene);
    if (!scene_draw) {
      if (++state.indexed_outside_scene<=10)
        REXLOG_INFO("Native indexed outside scene: draw={}, scene={:#x}, target={:#x}, caller={:#x}, primitive={}, count={}",
          state.indexed_draws,state.active_scene,state.active_target,uint32_t(ctx.lr),ctx.r4.u32,ctx.r7.u32);
    }
    if (state.indexed_draws<=20 || scene_draw) {
      try {
        const edf::native::GuestReader backing(base);
        const edf::native::GuestReadWindow reader(backing,backing.Add(ctx.r3.u32,1024),12416-1024);
        // Material activation isn't necessarily the last shader change. Consult
        // native setter-owned identity before reusing its prepared parameters.
        const auto guest_shaders=state.shader_bindings.Pair(ctx.r3.u32);
        const auto guest_vertex=guest_shaders.vertex;
        const auto guest_pixel=guest_shaders.pixel;
        if (guest_vertex!=state.active_vertex || guest_pixel!=state.linked_pixel)
          throw std::runtime_error("indexed shader changed outside material activation: guestVS="+
            std::to_string(guest_vertex)+" nativeVS="+std::to_string(state.active_vertex)+
            " guestPS="+std::to_string(guest_pixel)+" nativePS="+std::to_string(state.linked_pixel));
        const auto stream=state.streams.at({ctx.r3.u32,0});
        const auto decl=state.declaration_bindings.at(ctx.r3.u32);
        const auto native_declaration=state.declarations.Get(decl);
        const auto elements=uint32_t(native_declaration->bytes().size()/12);
        const auto ib=state.index_bindings.at(ctx.r3.u32);
        const auto* native_vb=state.model_buffers.Find(stream.resource,edf::native::NativeModelBuffers::Kind::Vertex);
        const auto* native_ib=state.model_buffers.Find(ib,edf::native::NativeModelBuffers::Kind::Index);
        const unsigned ownership=(native_vb?1u:0u)|(native_ib?2u:0u);
        const auto ownership_count=++state.indexed_ownership[ownership];
        const auto ownership_attempt=++state.indexed_ownership_attempts;
        if(native_vb && native_ib && native_vb->physical && native_ib->physical) ++state.indexed_physical_pairs;
        if(ownership!=3 && (ownership_count<=8 || (ownership_count&(ownership_count-1))==0))
          REXLOG_INFO("Native indexed guest-header fallback: registered_vb={}, registered_ib={}, VB={:#x}, IB={:#x}, caller={:#x}, VS={:#x}, category_count={} (before header read; missing creator/lifetime ownership)",
            native_vb!=nullptr,native_ib!=nullptr,stream.resource,ib,uint32_t(ctx.lr),state.active_vertex,ownership_count);
        if(ownership_attempt<=8 || (ownership_attempt&(ownership_attempt-1))==0)
          REXLOG_INFO("Native indexed ownership coverage: attempts={}, both={}, vb_only={}, ib_only={}, neither={}, physical_pairs={} (resolved indexed attempts, not submitted draws; registration is not writer completeness)",
            ownership_attempt,state.indexed_ownership[3],state.indexed_ownership[1],state.indexed_ownership[2],
            state.indexed_ownership[0],state.indexed_physical_pairs);
        uint32_t vertex_address,vertex_bytes,index_address,index_bytes,index_width;
        if(native_vb) { vertex_address=native_vb->address; vertex_bytes=native_vb->bytes; }
        else {
          const auto header=edf::native::ReadGuestWords<2>(reader,reader.Add(stream.resource,24));
          vertex_address=header[0]&~3u; vertex_bytes=header[1]&0x03fffffcu;
        }
        if(native_ib) { index_address=native_ib->address; index_bytes=native_ib->bytes; index_width=native_ib->stride; }
        else {
          const auto header=edf::native::ReadGuestWords<8>(reader,ib);
          index_address=header[6]; index_bytes=header[7]; index_width=(header[0]&0x80000000u)?4:2;
        }
        drawn_stride=stream.stride; drawn_index_width=index_width;
        if (state.indexed_draws<=20) REXLOG_INFO("Native indexed input: draw={}, primitive={}, base={}, first={}, count={}, stride={}, vertex_bytes={}, index_bytes={}, index_width={}, elements={}, target={:#x}, native_vb={}, native_ib={}",
          state.indexed_draws,ctx.r4.u32,ctx.r5.s32,ctx.r6.u32,ctx.r7.u32,stream.stride,
          vertex_bytes,index_bytes,index_width,elements,state.active_target,native_vb!=nullptr,native_ib!=nullptr);
        if (ctx.r4.u32!=4 || !elements || elements>64 || stream.offset>=vertex_bytes ||
            vertex_bytes>128*1024*1024 || index_bytes>128*1024*1024)
          throw std::runtime_error("unsupported indexed geometry bounds/topology");
        auto& shader=state.shaders.at(state.active_vertex);
        const auto viewport=edf::native::ReadNativeDrawViewport(reader,ctx.r3.u32);
        auto& bindings=viewport.reverse_depth ? *shader.reversed_bindings : *shader.bindings;
        edf::native::HookTiming mesh_timing(edf::native::HookPhase::IndexedMesh);
        edf::native::HookTiming ranges_timing(edf::native::HookPhase::MeshRanges);
        const auto declaration_bytes=native_declaration->bytes();
        std::span<const uint8_t> vertices_bytes{reader.Bytes(reader.Add(vertex_address,stream.offset),vertex_bytes-stream.offset),vertex_bytes-stream.offset};
        std::span<const uint8_t> indices_bytes{reader.Bytes(index_address,index_bytes),index_bytes};
        ranges_timing.Finish();
        auto mesh_watch_audit=state.mesh_watch_audit.lock();
        edf::native::HookTiming acquire_timing(edf::native::HookPhase::MeshAcquire);
        std::optional<edf::native::NativeBufferWrites::ObservedVersion> vertex_version,index_version;
        using GeometrySnapshots=std::array<edf::native::NativeBufferWrites::ObservedSnapshot,2>;
        std::optional<GeometrySnapshots> observed_geometry;
        auto vertex_contents=native_vb?native_vb->vertex_contents:nullptr;
        if(native_vb && native_ib && native_vb->physical && native_ib->physical) {
          using Source=edf::native::NativeBufferWrites::SnapshotSource;
          edf::native::NativeBufferWrites::SnapshotFailure failure;
          const bool revision_audit=REXCVAR_GET(edf_native_retirement_audit);
          const std::span<const uint8_t> full_vertices{reader.Bytes(vertex_address,vertex_bytes),vertex_bytes};
          std::optional<edf::native::GuestMeshWatchAudit::Observation> vertex_watch,index_watch;
          if(mesh_watch_audit) {
            vertex_watch=mesh_watch_audit->Begin(reader.Add(vertex_address,stream.offset),vertices_bytes.size());
            index_watch=mesh_watch_audit->Begin(index_address,indices_bytes.size());
          }
          // Revision/writer-watch audits are the comparison's own oracles, so
          // they always read the source. Otherwise the comparison is sampled:
          // see NativeBufferWrites::SnapshotPolicy for the fail-closed rules.
          edf::native::NativeBufferWrites::SnapshotPolicy policy{};
          policy.audit_revisions=revision_audit;
          if(!revision_audit && !mesh_watch_audit) {
            policy.verify_interval=uint64_t(std::clamp(REXCVAR_GET(edf_native_geometry_verify_interval),0,1<<20));
            policy.verify_initial=uint64_t(std::clamp(REXCVAR_GET(edf_native_geometry_verify_initial),0,1<<16));
          }
          observed_geometry=edf::native::BufferWrites().CopyObservedSet(std::array<Source,2>{{
            {stream.resource,*native_vb->physical,full_vertices,vertex_contents},
            {ib,*native_ib->physical,indices_bytes,native_ib->index_contents?native_ib->index_contents:
              (native_ib->index_storage?native_ib->index_storage->SourceSnapshot():nullptr)}}},&failure,
            policy);
          if(mesh_watch_audit && observed_geometry) {
            if(vertex_watch) mesh_watch_audit->Finish(*vertex_watch,
              std::span<const uint8_t>(*(*observed_geometry)[0].contents).subspan(stream.offset));
            if(index_watch) mesh_watch_audit->Finish(*index_watch,*(*observed_geometry)[1].contents);
          }
          static std::array<uint64_t,2> revision_checked{},revision_without_baseline{},revision_missed{};
          if(revision_audit && observed_geometry) for(size_t slot=0;slot<2;++slot) {
            const auto& observation=(*observed_geometry)[slot];
            if(observation.revision_audited) ++revision_checked[slot];
            else ++revision_without_baseline[slot];
            if(observation.unreported_change) ++revision_missed[slot];
          }
          if(observed_geometry) for(size_t slot=0;slot<2;++slot) if((*observed_geometry)[slot].unreported_change)
            REXLOG_WARN("Native geometry revision audit: unreported change, owner={:#x}, index={}, lifetime={}, revision={} (live comparison repaired snapshot; sampled verification is now permanently disabled for this run)",
              slot?ib:stream.resource,slot==1,(*observed_geometry)[slot].version.lifetime,(*observed_geometry)[slot].version.revision);
          static uint64_t guarded=0,unavailable=0;
          static std::array<uint64_t,5> rejections{};
          if(observed_geometry) {
            ++guarded;
            vertex_contents=(*observed_geometry)[0].contents;
            vertices_bytes=std::span<const uint8_t>(*vertex_contents).subspan(stream.offset);
            indices_bytes=*(*observed_geometry)[1].contents;
            vertex_version=(*observed_geometry)[0].version; index_version=(*observed_geometry)[1].version;
          } else {
            ++unavailable; ++rejections.at(size_t(failure.reason));
            if(unavailable<=8 || (unavailable & (unavailable-1))==0)
              REXLOG_INFO("Native geometry snapshot rejection: unavailable={}, unknown={}, active={}, missing={}, extent={}, owner={:#x}, active_writers={} (classification at acquisition; no writer completeness claim)",
                unavailable,rejections[1],rejections[2],rejections[3],rejections[4],failure.owner,failure.active_writers);
            if(unavailable<=8 || (unavailable & (unavailable-1))==0)
              REXLOG_INFO("Native geometry active write ranges: overlapping={}, unknown={}, same_thread={}, allocation_release={} (same-thread scopes cannot be waited out by this draw)",
                failure.overlapping_writers,failure.unknown_writers,failure.same_thread_writers,failure.releasing_writers);
            if(unavailable<=8 || (unavailable & (unavailable-1))==0)
              REXLOG_INFO("Native geometry unknown writer providers: unspecified={}, bulk={}, file_read={}, word_fill={}, inline_indices={}, allocation_release={} (active scope counts at sampled rejection)",
                failure.unknown_by_kind[0],failure.unknown_by_kind[1],failure.unknown_by_kind[2],failure.unknown_by_kind[3],failure.unknown_by_kind[4],failure.unknown_by_kind[5]);
            // Failed guarded acquisition never authorizes an unguarded read.
            // Neither active payload writers nor lost tracking/ownership are
            // repaired by comparing live bytes. Do not wait under these locks;
            // a producer may need the registry lock before it can complete.
            // The outer handler records an unsubmitted draw, not a safe retry.
            native_geometry_rejected=true;
            throw std::runtime_error("native geometry guarded acquisition failed; live fallback rejected");
          }
          const auto attempts=guarded+unavailable;
          if(attempts<=8 || (attempts & (attempts-1))==0) {
            REXLOG_INFO("Native geometry snapshot acquisition: guarded={}, unavailable={} (failed guarded acquisitions reject the draw)",guarded,unavailable);
            const auto trust=edf::native::BufferWrites().Trust();
            REXLOG_INFO("Native geometry comparison schedule: compared={}, trusted={}, unreported_changes={}, revoked={}, interval={}, initial={} (trusted observations reused a revision-proven snapshot without reading guest bytes)",
              trust.verified,trust.trusted,trust.unreported_changes,trust.revoked,
              policy.verify_interval,policy.verify_initial);
          }
          if(revision_audit && (attempts<=8 || (attempts & (attempts-1))==0))
            REXLOG_INFO("Native geometry revision audit coverage: checked_vb={}, checked_ib={}, without_baseline_vb={}, without_baseline_ib={}, missed_vb={}, missed_ib={} (successful snapshots only; checked requires same candidate and unchanged revision; not writer completeness)",
              revision_checked[0],revision_checked[1],revision_without_baseline[0],revision_without_baseline[1],revision_missed[0],revision_missed[1]);
        }
        if(mesh_watch_audit && state.indexed_draws%10000==0) {
          const auto& c=mesh_watch_audit->counters();
          REXLOG_INFO("Native mesh watch audit: checked={}, unsupported={}, stable={}, invalidated={}, missed={}, resets={}, excluded={}, foreign={} (shadow only; checked counts begun observations, rejected snapshots not compared)",
            c.checked,c.unsupported,c.stable,c.invalidated,c.missed,c.resets,c.excluded,c.foreign);
        }
        auto before_snapshot=[&] {
          if(observed_geometry) return; // Never label an earlier copy with a later writer epoch.
          if(native_vb && native_vb->physical) vertex_version=edf::native::BufferWrites().Version(stream.resource);
          if(native_ib && native_ib->physical) index_version=edf::native::BufferWrites().Version(ib);
        };
        auto& mesh=state.meshes.Acquire(EnsureSceneBackendLocked(state),bindings.shader(),
          {stream.resource,ib,decl,state.active_vertex,uint32_t(viewport.reverse_depth)},
          declaration_bytes,stream.stride,vertices_bytes,indices_bytes,index_width,native_declaration,{},
          native_ib?native_ib->index_storage:nullptr,native_vb?native_vb->vertex_storage:nullptr,
          {&before_snapshot,[](void* context) {
            (*static_cast<decltype(before_snapshot)*>(context))();
          }},vertex_contents,stream.offset,observed_geometry?(*observed_geometry)[1].contents:nullptr);
        // The registry lock protects lifetimes; the queue handshake additionally
        // rejects completed notified writes during construction. Unnotified raw
        // writes still require the existing live source comparisons.
        if(state.indexed_draws<=5) {
          const auto& source=*mesh.IndexStorage()->SourceSnapshot();
          if(source.size()>=12) {
            const auto word=[&](size_t at) {
              uint32_t value=0;
              for(size_t lane=0;lane<4;++lane) value=(value<<8)|source[at+lane];
              return value;
            };
            REXLOG_INFO("Native indexed resources: VB={:#x} data={:#x} offset={}, IB={:#x} data={:#x}, index_width={}, first_words={:#x}/{:#x}/{:#x} (native draw snapshot)",
              stream.resource,vertex_address,stream.offset,ib,index_address,index_width,word(0),word(4),word(8));
          }
        }
        if(observed_geometry) {
          // Both source observations came from one guarded acquisition. Keep
          // attachment indivisible too, including a full VB with stream offset.
          if(native_vb->vertex_storage!=mesh.VertexStorage() ||
             native_vb->vertex_contents!=vertex_contents || native_ib->index_storage!=mesh.IndexStorage() ||
             native_ib->index_contents!=(*observed_geometry)[1].contents)
            state.model_buffers.CommitObservedGeometry(
              {stream.resource,native_vb->generation,*vertex_version},
              {ib,native_ib->generation,*index_version},mesh.VertexStorage(),vertex_contents,mesh.IndexStorage(),
              (*observed_geometry)[1].contents);
        } else {
          if(native_vb && native_vb->vertex_storage!=mesh.VertexStorage()) {
            if(vertex_version) state.model_buffers.CommitObservedVertex(stream.resource,native_vb->generation,*vertex_version,mesh.VertexStorage());
            else if(!native_vb->physical) state.model_buffers.RetainVertexStorage(stream.resource,native_vb->generation,mesh.VertexStorage());
          }
          if(native_vb && !stream.offset && !mesh.VertexStorage()->SourceOffset() &&
             mesh.VertexStorage()->SourceBytes()==mesh.VertexStorage()->SourceSnapshot()->size() &&
             native_vb->vertex_contents!=mesh.VertexStorage()->SourceSnapshot()) {
            state.model_buffers.RetainVertexContents(stream.resource,native_vb->generation,
              mesh.VertexStorage()->SourceSnapshot(),vertex_version);
          }
          if(native_ib && native_ib->index_storage!=mesh.IndexStorage()) {
            if(index_version) state.model_buffers.CommitObservedIndex(ib,native_ib->generation,*index_version,mesh.IndexStorage());
            else if(!native_ib->physical) state.model_buffers.RetainIndexStorage(ib,native_ib->generation,mesh.IndexStorage());
          }
        }
        acquire_timing.Finish();
        edf::native::HookTiming draw_range_timing(edf::native::HookPhase::MeshDrawRange);
        mesh.ValidateDraw(ctx.r6.u32,ctx.r7.u32,ctx.r5.s32);
        draw_range_timing.Finish();
        mesh_timing.Finish();
        ++state.indexed_uploads;
        if (scene_draw) {
          edf::native::HookTiming binding_timing(edf::native::HookPhase::IndexedBindings);
          const auto key=edf::native::ReadAuditedRenderStateWords(reader,ctx.r3.u32);
          auto found=state.render_states.find(key);
          if (found==state.render_states.end()) found=state.render_states.emplace(key,
            edf::native::CreateNativeRenderState(*state.device.Get(),key)).first;
          // 77.4% of this game's indexed draws repeat the one before them in
          // mesh, material and state, differing only in the constants an
          // activation patches between them. The targets and render state do
          // not change across such a run, so binding them again per draw is
          // pure repetition - which is the batch, without needing the draws
          // themselves to merge.
          //
          // Skipped only when nothing else has bound since we did: the
          // generation catches another path replacing the state underneath us,
          // which a comparison against our own cache never would. A state that
          // needs a constant blend factor is never skipped, because the factor
          // can change while the state words do not.
          const bool same_binding=state.indexed_bind_valid &&
            state.bind_generation==state.indexed_bind_generation &&
            state.indexed_bind_key==key &&
            state.indexed_bind_target==state.active_target &&
            state.indexed_bind_scene==state.active_scene &&
            state.indexed_bind_output==state.active_output &&
            !found->second.requires_blend_factor;
          // The same guard extended to the material. When the previous draw
          // left this very shader pair bound with these textures and samplers,
          // the only thing this draw changes is its constants, which an
          // activation has just patched. Re-sending the shader, the texture
          // runs and the sampler runs is the same calls with the same
          // arguments, once per draw, for 77.4% of them.
          //
          // The reversed-depth variant is a different shader object under the
          // same handle, so it has to be part of the comparison: a draw that
          // flips depth direction would otherwise reuse the wrong one.
          const bool same_material=same_binding &&
            REXCVAR_GET(edf_native_reuse_material) &&
            state.indexed_bind_vertex==state.active_vertex &&
            state.indexed_bind_pixel==state.linked_pixel &&
            state.indexed_bind_reversed==viewport.reverse_depth;
          if(same_binding) ++state.indexed_binds_skipped;
          else {
            edf::native::BindActiveTarget(state);
            edf::native::BindGuestRenderState(found->second,*state.context.Get(),reader,ctx.r3.u32,
                                             &state.bind_generation);
            state.indexed_bind_valid=true;
            state.indexed_bind_key=key;
            state.indexed_bind_target=state.active_target;
            state.indexed_bind_scene=state.active_scene;
            state.indexed_bind_output=state.active_output;
            state.indexed_bind_generation=state.bind_generation;
            ++state.indexed_binds_bound;
          }
          viewport.Bind(*state.context.Get());
          if(same_material) {
            bindings.BindConstants(*state.context.Get());
            state.shaders.at(state.linked_pixel).bindings->BindConstants(*state.context.Get());
            ++state.indexed_materials_reused;
          } else {
            bindings.Bind(*state.context.Get());
            state.shaders.at(state.linked_pixel).bindings->Bind(*state.context.Get());
            state.indexed_bind_vertex=state.active_vertex;
            state.indexed_bind_pixel=state.linked_pixel;
            state.indexed_bind_reversed=viewport.reverse_depth;
          }
          binding_timing.Finish();
          if(REXCVAR_GET(edf_native_capture_indexed_state) &&
             !REXCVAR_GET(edf_native_scene_capture).empty()) {
            // This scene is a candidate for the next indexed output. If output
            // fails, the index may be reused; keep the cap across those retries.
            const auto frame=state.indexed_output_frames+1;
            if(state.indexed_trace_frame!=frame) {
              state.indexed_trace_frame=frame;
              state.indexed_trace_draws=0;
            }
            if(state.indexed_trace_draws<256 && edf::native::ShouldCaptureNativeOutput(
                frame,state.output_captures,REXCVAR_GET(edf_native_output_capture_limit),
                REXCVAR_GET(edf_native_output_capture_interval),REXCVAR_GET(edf_native_output_capture_start_frame))) {
              ++state.indexed_trace_draws;
              REXLOG_INFO("Native indexed capture state: next_output_candidate={}, sample={}/256, scene={:#x}, draw={}, VS={:#x}, PS={:#x}, VB={:#x}, IB={:#x}, declaration={:#x}, first={}, count={}, base={}, blend={:#x}, depth={:#x}, raster={:#x}, alpha={:#x}, mask={}, scissor={}, depth_range={}..{}, reversed={}; state before draw, not proof of output",
                frame,state.indexed_trace_draws,state.active_scene,state.indexed_draws,
                state.active_vertex,state.linked_pixel,stream.resource,ib,decl,
                ctx.r6.u32,ctx.r7.u32,ctx.r5.s32,key[0],key[1],key[2],key[3],key[4],key[5],
                viewport.viewport.MinDepth,viewport.viewport.MaxDepth,viewport.reverse_depth);
              REXLOG_INFO("Native indexed capture viewport: draw={}, xy={},{} extent={}x{}, scene_extent={}x{}",
                state.indexed_draws,viewport.viewport.TopLeftX,viewport.viewport.TopLeftY,
                viewport.viewport.Width,viewport.viewport.Height,
                state.scenes.at(state.active_scene).color.sampled.width,
                state.scenes.at(state.active_scene).color.sampled.height);
            }
          }
          const std::array<uint32_t,4> probe_key{state.active_vertex,state.linked_pixel,key[2],key[1]};
          if (!REXCVAR_GET(edf_native_scene_capture).empty() && state.clip_probes.size()<24 &&
              state.clip_probes.insert(probe_key).second) {
            try {
              const auto positions=mesh.CaptureClipPositions(*state.context.Get(),bindings.shader(),
                ctx.r6.u32,(std::min)(ctx.r7.u32,9u),ctx.r5.s32);
              REXLOG_INFO("Native clip probe: VS={:#x} {}, PS={:#x} {}, raster={:#x}, depth={:#x}, first={}, base={}, vertices={}",
                state.active_vertex,bindings.shader().entry.name,state.linked_pixel,
                state.shaders.at(state.linked_pixel).bindings->shader().entry.name,key[2],key[1],ctx.r6.u32,ctx.r5.s32,positions.size());
              for(const auto* name:{"g_mWorld","g_mView","g_mProjection","g_mViewProjection"}) {
                if(const auto matrix=bindings.ReadFloat4x4(name)) for(size_t row=0;row<4;++row)
                  REXLOG_INFO("Native clip matrix: name={}, row={}, values={},{},{},{}",name,row,
                    (*matrix)[row*4],(*matrix)[row*4+1],(*matrix)[row*4+2],(*matrix)[row*4+3]);
              }
              // Log the original POSITION0 float3 for the same indexed samples.
              // This is diagnostic-only; other declaration formats still render.
              for(uint32_t element=0;element<elements;++element) {
                const auto declaration_word=[&](size_t at) {
                  uint32_t value=0;
                  for(size_t lane=0;lane<4;++lane) value=(value<<8)|declaration_bytes[at+lane];
                  return value;
                };
                const auto at=size_t(element)*12;
                if(declaration_word(at+8)!=0 || declaration_word(at+4)!=0x2a23b9) continue;
                const auto position_offset=declaration_word(at);
                if(position_offset>=stream.stride || stream.stride-position_offset<12) continue;
                const auto inputs=mesh.CaptureSourceFloat3(ctx.r6.u32,uint32_t(positions.size()),ctx.r5.s32,position_offset);
                for(size_t i=0;i<inputs.size();++i) {
                  REXLOG_INFO("Native clip input: index={}, xyz={},{},{} (native draw snapshot)",i,
                    inputs[i][0],inputs[i][1],inputs[i][2]);
                }
                break;
              }
              for(size_t i=0;i<positions.size();++i) {
                const auto& p=positions[i];
                REXLOG_INFO("Native clip vertex: index={}, xyzw={},{},{},{}",i,p[0],p[1],p[2],p[3]);
              }
            } catch(const std::exception& error) { REXLOG_ERROR("Native clip probe: {}",error.what()); }
          }
          Microsoft::WRL::ComPtr<ID3D11Query> visibility;
          if (!REXCVAR_GET(edf_native_scene_capture).empty() && state.scene_captures<3 && state.visibility.size()<2048) {
            const D3D11_QUERY_DESC query_desc{D3D11_QUERY_OCCLUSION,0};
            if (SUCCEEDED(state.device->CreateQuery(&query_desc,&visibility))) state.context->Begin(visibility.Get());
          }
          // Batching precondition. A run of draws that share mesh, declaration,
          // shader pair, render state and index range, and differ only in the
          // per-instance constants patched by 821D9600, is exactly what one
          // DrawIndexedInstanced replaces. Measure the run lengths before
          // building that: the mean run length is the draw-call reduction, and
          // a mean near 1 would mean there is nothing to collapse.
          if(REXCVAR_GET(edf_native_batch_audit) && state.indexed_draws%1000000==0)
            REXLOG_INFO("Native indexed binding reuse: bound={}, skipped={} ({:.1f}% of draws bound no target or render state, because the draw before them had already bound the same)",
              state.indexed_binds_bound,state.indexed_binds_skipped,
              (state.indexed_binds_bound+state.indexed_binds_skipped)
                ?100.0*double(state.indexed_binds_skipped)/double(state.indexed_binds_bound+state.indexed_binds_skipped):0.0);
          if(REXCVAR_GET(edf_native_batch_audit) && state.indexed_draws%1000000==0)
            REXLOG_INFO("Native indexed material reuse: draws={}, constants_only={} ({:.1f}% re-sent only their constants, keeping the shader pair, textures and samplers the previous draw bound)",
              state.indexed_draws,state.indexed_materials_reused,
              state.indexed_draws?100.0*double(state.indexed_materials_reused)/double(state.indexed_draws):0.0);
          if(REXCVAR_GET(edf_native_batch_audit)) {
            const std::array<uint32_t,12> batch_key{stream.resource,ib,decl,
              state.active_vertex,state.linked_pixel,ctx.r6.u32,ctx.r7.u32,uint32_t(ctx.r5.s32),
              key[0],key[1],key[2],key[3]};
            ++state.batch_draws;
            if(batch_key==state.last_batch_key && state.instance_shape!=state.last_instance_shape)
              ++state.batch_shape_breaks;
            if(batch_key==state.last_batch_key) ++state.batch_run;
            else {
              if(state.batch_run) {
                state.batch_longest=(std::max)(state.batch_longest,state.batch_run);
                state.batch_run_total+=state.batch_run;
                ++state.batch_runs;
                if(state.batch_run>=2) state.batch_collapsible+=state.batch_run-1;
              }
              state.batch_run=1; state.last_batch_key=batch_key;
            }
            state.last_instance_shape=state.instance_shape;
            if(state.batch_draws%1000000==0) {
              REXLOG_INFO("Native batch audit: draws={}, runs={}, mean_run={:.2f}, longest_run={}, collapsible_draws={} ({:.1f}% of draws could be folded into a preceding instanced draw)",
                state.batch_draws,state.batch_runs,
                state.batch_runs?double(state.batch_run_total)/double(state.batch_runs):0.0,
                state.batch_longest,state.batch_collapsible,
                100.0*double(state.batch_collapsible)/double(state.batch_draws));
              REXLOG_INFO("Native batch shape: collapsible={}, register_shape_breaks={} ({:.2f}% of collapsible draws patch a different register range than the draw before, so cannot share one instanced variant)",
                state.batch_collapsible,state.batch_shape_breaks,
                state.batch_collapsible?100.0*double(state.batch_shape_breaks)/double(state.batch_collapsible):0.0);
            }
          }
          try { mesh.Draw(*state.context.Get(),ctx.r6.u32,ctx.r7.u32,ctx.r5.s32); }
          catch (...) { if (visibility) state.context->End(visibility.Get()); throw; }
          if (visibility) {
            state.context->End(visibility.Get());
            state.visibility.push_back({std::move(visibility),{state.active_vertex,state.linked_pixel,key[2],key[1]}});
          }
          ++state.indexed_submitted;
          native_submitted=true;
          const auto probe_x=REXCVAR_GET(edf_native_probe_x),probe_y=REXCVAR_GET(edf_native_probe_y);
          const auto probe_frame=REXCVAR_GET(edf_native_probe_frame);
          const bool probe_window=probe_frame>0 ? state.indexed_output_frames+1>=uint64_t(probe_frame) : state.scene_captures==0;
          if(probe_x>=0 && probe_y>=0 && !state.color_probe_done && probe_window) {
            try {
              const auto& scene=state.scenes.at(state.active_scene);
              uint32_t sample_x=uint32_t(probe_x),sample_y=uint32_t(probe_y);
              const auto width=REXCVAR_GET(edf_native_probe_width),height=REXCVAR_GET(edf_native_probe_height);
              if(width<=0 || height<=0) throw std::runtime_error("invalid diagnostic region size");
              std::array<float,4> rgba{};
              const bool probe_negative=REXCVAR_GET(edf_native_probe_negative);
              if((width==1 && height==1) || edf::native::FindNativeInvalidColorPixel(
                  *state.context.Get(),*scene.color.surface.Get(),sample_x,sample_y,
                  uint32_t(width),uint32_t(height),sample_x,sample_y,probe_negative))
                rgba=edf::native::ReadNativeColorPixel(*state.context.Get(),*scene.color.surface.Get(),sample_x,sample_y);
              ++state.color_probe_draws;
              const bool nonfinite=!std::isfinite(rgba[0]) || !std::isfinite(rgba[1]) || !std::isfinite(rgba[2]);
              const bool negative=probe_negative &&
                (rgba[0]<=-1.f || rgba[1]<=-1.f || rgba[2]<=-1.f);
              if(nonfinite || negative) {
                state.color_probe_done=true;
                // Scans since the probe window opened. A hit on the first scan
                // means the pixel was already bad before the blamed draw ran,
                // so the draw is only "the first one scanned", not the producer.
                REXLOG_INFO("Native invalid RGB frame: next_output_candidate={}, kind={}, scans={} (scans==1 means the value predates this draw)",
                  state.indexed_output_frames+1,nonfinite?"nonfinite":"negative",state.color_probe_draws);
                REXLOG_INFO("Native invalid RGB origin: pixel={},{} draw={} VS={:#x} {} source={:#x} PS={:#x} {} source={:#x} rgba={},{},{},{}",
                  sample_x,sample_y,state.indexed_submitted,state.active_vertex,bindings.shader().entry.name,
                  state.shaders.at(state.active_vertex).source_fingerprint,state.linked_pixel,
                  state.shaders.at(state.linked_pixel).bindings->shader().entry.name,
                  state.shaders.at(state.linked_pixel).source_fingerprint,rgba[0],rgba[1],rgba[2],rgba[3]);
                // m_tD_trans is alpha blended and its lighting cannot go
                // negative, so the decoded blend operation is the first thing
                // to rule out: a SUBTRACT or REV_SUBTRACT where the guest asked
                // for ADD drives the target below zero from positive inputs.
                // Everything inside the shader is provably non-negative for the
                // logged constants, so the remaining candidate is the geometry
                // reaching the rasteriser. A vertex at w<=0, or a triangle
                // spanning the eye plane, makes perspective-correct
                // interpolation extrapolate the colour varyings far outside the
                // range the vertex shader can emit.
                try {
                  const auto clip=mesh.CaptureClipPositions(*state.context.Get(),bindings.shader(),
                    ctx.r6.u32,(std::min)(ctx.r7.u32,24u),ctx.r5.s32);
                  float min_w=std::numeric_limits<float>::infinity(),max_w=-min_w;
                  size_t nonpositive=0,nonfinite_w=0;
                  for(const auto& position:clip) {
                    if(!std::isfinite(position[3])) { ++nonfinite_w; continue; }
                    min_w=(std::min)(min_w,position[3]); max_w=(std::max)(max_w,position[3]);
                    if(position[3]<=0) ++nonpositive;
                  }
                  REXLOG_INFO("Native invalid RGB clip: sampled={}, min_w={}, max_w={}, nonpositive_w={}, nonfinite_w={} (mixed-sign or near-zero w extrapolates interpolated colour)",
                    clip.size(),min_w,max_w,nonpositive,nonfinite_w);
                  for(size_t i=0;i<clip.size() && i<8;++i)
                    REXLOG_INFO("Native invalid RGB clip vertex: index={}, xyzw={},{},{},{}",
                      i,clip[i][0],clip[i][1],clip[i][2],clip[i][3]);
                } catch(const std::exception& error) {
                  REXLOG_INFO("Native invalid RGB clip capture: {}",error.what());
                }
                // Color = Dtex * In.Color + In.Specular, and the lighting terms
                // are provably non-negative for the logged constants, so the
                // sampled texel is the remaining candidate. A UNORM/BC format
                // cannot be negative; a signed or float one can.
                for(const auto* name:{"m_DiffuseTexture0_Sampler","m_ParameterTexture0_Sampler",
                    "m_NormalTexture0_Sampler","m_CubeTexture0_Sampler"}) {
                  const auto view=state.shaders.at(state.linked_pixel).bindings->ReadTexture(name);
                  if(!view) continue;
                  D3D11_SHADER_RESOURCE_VIEW_DESC view_desc{}; view->GetDesc(&view_desc);
                  Microsoft::WRL::ComPtr<ID3D11Resource> resource; view->GetResource(&resource);
                  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
                  D3D11_TEXTURE2D_DESC desc{};
                  if(SUCCEEDED(resource.As(&texture))) texture->GetDesc(&desc);
                  REXLOG_INFO("Native invalid RGB texture: name={}, view_format={}, resource_format={}, {}x{}, mips={}",
                    name,uint32_t(view_desc.Format),uint32_t(desc.Format),desc.Width,desc.Height,desc.MipLevels);
                }
                REXLOG_INFO("Native invalid RGB state: blend={:#x}, depth={:#x}, raster={:#x}, alpha={:#x}, write_mask={}, scissor={}",
                  key[0],key[1],key[2],key[3],key[4],key[5]);
                if(const auto found=state.render_states.find(key);
                   found!=state.render_states.end() && found->second.blend) {
                  D3D11_BLEND_DESC blend{};
                  found->second.blend->GetDesc(&blend);
                  const auto& target=blend.RenderTarget[0];
                  REXLOG_INFO("Native invalid RGB blend: enabled={}, src={}, dest={}, op={}, src_alpha={}, dest_alpha={}, alpha_op={}, mask={}, needs_factor={}, replicate_alpha={} (D3D11_BLEND_OP: 1=ADD 2=SUBTRACT 3=REV_SUBTRACT 4=MIN 5=MAX)",
                    target.BlendEnable!=FALSE,uint32_t(target.SrcBlend),uint32_t(target.DestBlend),
                    uint32_t(target.BlendOp),uint32_t(target.SrcBlendAlpha),uint32_t(target.DestBlendAlpha),
                    uint32_t(target.BlendOpAlpha),uint32_t(target.RenderTargetWriteMask),
                    found->second.requires_blend_factor,found->second.replicate_blend_alpha);
                }
                for(const auto* stage:{&bindings,state.shaders.at(state.linked_pixel).bindings.get()}) {
                  const auto* stage_name=stage->shader().entry.pixel?"PS":"VS";
                  for(const auto* name:{"m_MaterialDiffuse","m_MaterialSpecularColor","m_MaterialSpecularPower",
                      "m_MaterialBumpHeight","m_RefrectionRate","g_LightVector","g_LightDiffuse",
                      "g_LightSpecular","g_LightAmbient","g_HemiSphereVector","g_HemiSphereColor1",
                      "g_HemiSphereColor2","g_FogParam","g_FogColor"}) {
                    const auto values=stage->ReadFloatVector(name);
                    for(size_t lane=0;lane<values.size();++lane)
                      REXLOG_INFO("Native invalid RGB constant: stage={} name={} lane={} value={}",stage_name,name,lane,values[lane]);
                  }
                  for(const auto* name:{"g_mWorld","g_mView","g_mProjection","g_mViewTranspose"}) {
                    if(const auto matrix=stage->ReadFloat4x4(name)) for(size_t row=0;row<4;++row)
                      REXLOG_INFO("Native invalid RGB matrix: stage={} name={} row={} values={},{},{},{}",stage_name,name,row,
                        (*matrix)[row*4],(*matrix)[row*4+1],(*matrix)[row*4+2],(*matrix)[row*4+3]);
                  }
                }
                REXLOG_INFO("Native invalid RGB mesh: declaration={:#x} stride={} first={} base={} count={}",
                  decl,stream.stride,ctx.r6.u32,ctx.r5.s32,ctx.r7.u32);
                const auto declaration_word=[&](size_t at) {
                  uint32_t value=0;
                  for(size_t lane=0;lane<4;++lane) value=(value<<8)|declaration_bytes[at+lane];
                  return value;
                };
                for(uint32_t element=0;element<elements;++element) {
                  const auto at=element*12,offset=declaration_word(at),type=declaration_word(at+4);
                  const auto semantic=declaration_word(at+8);
                  REXLOG_INFO("Native invalid RGB attribute: offset={} type={:#x} semantic={:#x}",offset,type,semantic);
                  const auto usage=(semantic>>16)&255,index=(semantic>>8)&255;
                  if(type!=0x2a23b9 || (usage!=3 && usage!=6) || index!=0) continue;
                  if(offset>stream.stride || stream.stride-offset<12) continue;
                  uint32_t zero=0,invalid=0,samples=(std::min)(ctx.r7.u32,65536u);
                  const auto values=mesh.CaptureSourceFloat3(ctx.r6.u32,samples,ctx.r5.s32,offset);
                  for(uint32_t i=0;i<samples;++i) {
                    const auto& value=values[i];
                    if(value[0]==0 && value[1]==0 && value[2]==0) ++zero;
                    if(!std::isfinite(value[0]) || !std::isfinite(value[1]) || !std::isfinite(value[2])) ++invalid;
                    if(i<3) REXLOG_INFO("Native invalid RGB attribute sample: usage={} index_position={} xyz={},{},{} (native draw snapshot)",
                      usage,ctx.r6.u32+i,value[0],value[1],value[2]);
                  }
                  REXLOG_INFO("Native invalid RGB attribute summary: usage={} indexed_samples={} zero={} nonfinite={}",
                    usage,samples,zero,invalid);
                }
              }
              if(state.color_probe_draws>=uint64_t(std::clamp(REXCVAR_GET(edf_native_probe_draw_limit),1,65536))) {
                state.color_probe_done=true;
                REXLOG_INFO("Native invalid RGB probe exhausted: draws={}, next_output_candidate={}",state.color_probe_draws,state.indexed_output_frames+1);
              }
            } catch(const std::exception& error) {
              state.color_probe_done=true;
              REXLOG_ERROR("Native invalid RGB probe: {}",error.what());
            }
          }
          state.scenes.at(state.active_scene).frame_complete=false;
        }
        if (state.indexed_uploads<=20 || state.indexed_submitted%1000==0) {
          REXLOG_INFO("Native indexed upload: uploaded={}, errors={}, submitted={}, reversed_depth={}, mesh_builds={}, mesh_hits={}, mesh_bytes={}, mesh_entries={}, entry_evictions={}, budget_evictions={}, vertex_mismatches={}, index_mismatches={}",
            state.indexed_uploads,state.indexed_errors,state.indexed_submitted,viewport.reverse_depth,
            state.meshes.builds(),state.meshes.hits(),state.meshes.bytes(),state.meshes.entries(),
            state.meshes.entry_evictions(),state.meshes.budget_evictions(),state.meshes.vertex_mismatches(),state.meshes.index_mismatches());
          if(state.indexed_submitted%100000==0) {
            const auto& checks=state.meshes.source_checks();
            REXLOG_INFO("Native mesh source checks: vertex_checks={}, index_checks={}, vertex_candidate_bytes={}, index_candidate_bytes={} (cache-hit checks; not measured memory traffic)",
              checks.vertex_checks,checks.index_checks,checks.vertex_candidate_bytes,checks.index_candidate_bytes);
            REXLOG_INFO("Native published index consumption: reused_builds={}, rejected_generations={} (mesh hits excluded)",
              state.meshes.published_index_reuses(),state.meshes.published_index_rejections());
            size_t vertices=0,vertex_bytes=0,indices=0,index_bytes=0;
            state.model_buffers.VisitVertexStorage([&](uint32_t,const edf::native::NativeVertexBuffer& storage) {
              ++vertices; vertex_bytes+=storage.StorageBytes();
            });
            state.model_buffers.VisitIndexStorage([&](uint32_t,const edf::native::NativeIndexBuffer& storage) {
              ++indices; index_bytes+=storage.StorageBytes();
            });
            REXLOG_INFO("Native model storage: vertices={}, vertex_bytes={}, indices={}, index_bytes={}, vertex_reused_builds={}, vertex_replaced_builds={} (CPU/GPU payload shared with meshes; cache hits excluded)",
              vertices,vertex_bytes,indices,index_bytes,state.meshes.retained_vertex_reuses(),state.meshes.retained_vertex_replacements());
          }
          const auto mismatches=state.meshes.vertex_mismatches()+state.meshes.index_mismatches();
          if(mismatches!=state.reported_mesh_mismatches) {
            state.reported_mesh_mismatches=mismatches;
            const auto& v=state.meshes.last_vertex_mismatch();
            const auto& i=state.meshes.last_index_mismatch();
            REXLOG_INFO("Native geometry mismatch identities: vertex_count={}, last_vertex=(VB={:#x}, IB={:#x}, declaration={:#x}, VS={:#x}, variant={}), index_count={}, last_index=(VB={:#x}, IB={:#x}, declaration={:#x}, VS={:#x}, variant={}) (last detected consumers, not writer PCs)",
              state.meshes.vertex_mismatches(),v[0],v[1],v[2],v[3],v[4],
              state.meshes.index_mismatches(),i[0],i[1],i[2],i[3],i[4]);
          }
        }
      } catch (const std::exception& error) {
        ++state.indexed_errors;
        if (state.indexed_errors<=10) REXLOG_ERROR("Native indexed upload: {} (VS={:#x}, PS={:#x}, caller={:#x})",
          error.what(),state.active_vertex,state.linked_pixel,uint32_t(ctx.lr));
      }
    }
    if(!ctx.r7.u32) ++state.indexed_empty_requests;
    else if(native_submitted) {
      ++state.indexed_nonempty_submitted;
      // Opt-in: enumerating what the content exercises costs a set lookup on
      // every draw, and gameplay submits over a hundred thousand a second.
      if(REXCVAR_GET(edf_native_contract_coverage)) try {
        const edf::native::GuestReader reader(base);
        state.contracts.RecordSubmitted(edf::native::MakeNativeContract(state,
          edf::native::NativeContractPath::Indexed,state.active_vertex,state.linked_pixel,
          reader.Word(reader.Add(ctx.r3.u32,11536)),ctx.r4.u32,drawn_stride,drawn_index_width));
      } catch(const std::exception&) { /* Accounting must never fail a drawn frame. */ }
    }
    else {
      ++state.indexed_unsubmitted_requests;
      try {
        const edf::native::GuestReader reader(base);
        const auto declaration=reader.Word(reader.Add(ctx.r3.u32,11536));
        edf::native::ReportNativeContractRejection(state,edf::native::MakeNativeContract(state,
          edf::native::NativeContractPath::Indexed,state.active_vertex,state.linked_pixel,
          declaration,ctx.r4.u32),"indexed draw not submitted natively");
      } catch(const std::exception&) {
        edf::native::ReportNativeContractRejection(state,edf::native::MakeNativeContract(state,
          edf::native::NativeContractPath::Indexed,state.active_vertex,state.linked_pixel,
          0,ctx.r4.u32),"indexed draw not submitted natively; device unreadable");
      }
      const std::array<uint32_t,5> signature{uint32_t(ctx.lr),ctx.r4.u32,state.active_vertex,state.linked_pixel,state.active_target};
      if(state.indexed_unsubmitted_paths.size()<64 && state.indexed_unsubmitted_paths.insert(signature).second) {
        REXLOG_INFO("Native indexed unsubmitted path: caller={:#x}, primitive={}, count={}, scene={:#x}, target={:#x}, active_VS={:#x}, active_PS={:#x}",
          uint32_t(ctx.lr),ctx.r4.u32,ctx.r7.u32,state.active_scene,state.active_target,state.active_vertex,state.linked_pixel);
        try {
          const edf::native::GuestReader reader(base);
          const auto pair=edf::native::ReadShaderPair(reader,ctx.r3.u32);
          REXLOG_INFO("Native indexed unsubmitted bindings: VS={:#x}, PS={:#x}, surface={:#x}, declaration={:#x}, index_buffer={:#x}",
            pair.vertex,pair.pixel,reader.Word(reader.Add(ctx.r3.u32,12168)),
            reader.Word(reader.Add(ctx.r3.u32,11536)),reader.Word(reader.Add(ctx.r3.u32,12164)));
        } catch(const std::exception& error) {
          REXLOG_INFO("Native indexed unsubmitted inspection: {}",error.what());
        }
      }
    }
    const auto now=std::chrono::steady_clock::now();
    if(state.indexed_draws>=1024 && now-state.indexed_coverage_reported>=std::chrono::seconds(10)) {
      state.indexed_coverage_reported=now;
      REXLOG_INFO("Native indexed coverage: requests={}, empty={}, submitted={}, unsubmitted={}, sampled_paths={} (64-path cap; not visual completeness)",
        state.indexed_draws,state.indexed_empty_requests,state.indexed_nonempty_submitted,
        state.indexed_unsubmitted_requests,state.indexed_unsubmitted_paths.size());
      const auto& coverage=state.contracts.counters();
      REXLOG_INFO("Native contract coverage: rejected_draws={}, distinct_rejected={}, omitted={}, errors={}, clean={}, submitted_contracts={} (clean requires zero rejected and zero omitted)",
        coverage.rejected,coverage.distinct_rejected,coverage.omitted_rejected,
        state.indexed_errors,state.contracts.clean(),coverage.distinct_submitted);
      if(const auto path=REXCVAR_GET(edf_native_contract_export);!path.empty()) {
        // Rewritten whole each time: a partial capture from a killed run is
        // still a valid catalog of everything seen up to that point.
        std::ofstream file(path,std::ios::binary|std::ios::trunc);
        const auto text=state.contracts.Export();
        file.write(text.data(),std::streamsize(text.size()));
        if(!file) REXLOG_ERROR("Native contract export failed: {}",path);
        else REXLOG_INFO("Native contract export: {}, declarations={}, bytes={}",
          path,state.contracts.declarations().size(),text.size());
      }
      for(const auto& rejection:state.contracts.Rejections())
        REXLOG_INFO("Native contract gap: path={}, VS_source={:#x}, PS_source={:#x}, declaration={:#x}, elements={}, topology={}, draws={}, reason={}",
          edf::native::NativeContractPathName(rejection.contract.path),rejection.contract.vertex_source,
          rejection.contract.pixel_source,rejection.contract.declaration,rejection.contract.elements,
          rejection.contract.topology,rejection.draws,rejection.reason);
    }
  }
  native_timing.Finish();
  edf::native::HookTiming guest_timing(edf::native::HookPhase::IndexedGuest);
  // The native CPU chain calls native implementations directly; it needs no
  // thread-local packet-routing scope. Mask inherited ownership only for legacy.
  const bool native_host=REXCVAR_GET(edf_native_host);
  std::optional<edf::native::NativeConstantOwnership> legacy_scope;
  if(!native_host) legacy_scope.emplace(0);
  if(native_host) {
    // Generated from the unchanged retail CPU-state prefix and original ABI
    // epilogue. Native submission replaces the subsequent inline GPU draw loop.
    // Native-host mode has no GPU emulator to consume retail draw packets.
    // Unsupported, empty and failed draws retain CPU updates too, without
    // rereading the index header. Missing
    // native submissions remain explicit coverage failures, not fallback draws.
    __imp__edf_native_indexed_cpu_tail(ctx,base);
    static thread_local uint64_t omitted=0;
    if(++omitted<=3) REXLOG_INFO("Native indexed routing: retained CPU state; omitted Xbox draw packets (submitted={}, geometry_rejected={})",
      native_submitted,native_geometry_rejected);
  } else __imp__sub_821FE358(ctx,base);
}

// XUI's device wrapper stores raw created shader handles directly through r5.
// These identities are diagnostic until the embedded programs are recovered;
// never register an arbitrary game effect as a substitute for an XUI shader.
REX_EXTERN(__imp__sub_8241E180);
REX_HOOK_RAW(sub_8241E180) {
  const auto source=ctx.r4.u32,output=ctx.r5.u32,caller=uint32_t(ctx.lr);
  __imp__sub_8241E180(ctx,base);
  if (REXCVAR_GET(edf_native_shader_bridge) && ctx.r3.s32>=0) {
    try { edf::native::RecordEmbeddedShader(edf::native::GuestReader(base),output,source,false,caller); }
    catch (const std::exception& error) { REXLOG_ERROR("Native middleware VS identity: {}",error.what()); }
  }
}
REX_EXTERN(__imp__sub_8241E1D0);
REX_HOOK_RAW(sub_8241E1D0) {
  const auto source=ctx.r4.u32,output=ctx.r5.u32,caller=uint32_t(ctx.lr);
  __imp__sub_8241E1D0(ctx,base);
  if (REXCVAR_GET(edf_native_shader_bridge) && ctx.r3.s32>=0) {
    try { edf::native::RecordEmbeddedShader(edf::native::GuestReader(base),output,source,true,caller); }
    catch (const std::exception& error) { REXLOG_ERROR("Native middleware PS identity: {}",error.what()); }
  }
}
// The movie decoder calls LockRect directly, not through the XUI wrapper.
// Capture only locks nested in its decode call; ordinary resource locks do not
// establish video identity. 8213AE38 writes {row_pitch,pixel_address} to r5.
REX_EXTERN(__imp__sub_8213B5A0);
REX_HOOK_RAW(sub_8213B5A0) {
  const auto texture=ctx.r3.u32,level=ctx.r4.u32,output=ctx.r5.u32,rect=ctx.r6.u32;
  __imp__sub_8213B5A0(ctx,base);
  if (auto* capture=edf::native::active_movie_decode) {
    try {
      if (level || rect || capture->planes.size()>=3) throw std::runtime_error("unexpected movie plane lock");
      const edf::native::GuestReader reader(base);
      capture->planes.push_back({texture,reader.Word(output),reader.Word(reader.Add(output,4))});
    } catch (...) { capture->failed=true; }
  }
}
REX_EXTERN(__imp__sub_8242AF30);
REX_HOOK_RAW(sub_8242AF30) {
  if (!REXCVAR_GET(edf_native_shader_bridge)) { __imp__sub_8242AF30(ctx,base); return; }
  const auto owner=ctx.r3.u32;
  edf::native::MovieDecodeLocks capture;
  struct CaptureScope {
    edf::native::MovieDecodeLocks* previous;
    explicit CaptureScope(edf::native::MovieDecodeLocks& current)
      : previous(edf::native::active_movie_decode) { edf::native::active_movie_decode=&current; }
    ~CaptureScope() { edf::native::active_movie_decode=previous; }
  };
  { CaptureScope scope(capture); __imp__sub_8242AF30(ctx,base); }
  // Only S_OK publishes the newly decoded buffer at owner+52. Other positive
  // statuses include end-of-stream, and must not upload unwritten pixels.
  if (ctx.r3.s32!=0) return;
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  try {
    if (capture.failed || capture.planes.size()!=3) throw std::runtime_error("incomplete movie decode locks");
    const edf::native::GuestReader reader(base);
    const auto buffer=reader.Word(reader.Add(owner,52));
    const auto width=reader.Word(reader.Add(owner,56)),height=reader.Word(reader.Add(owner,60));
    if (buffer>1 || !width || !height || width>4096 || height>4096 || (width&1) || (height&1))
      throw std::runtime_error("invalid movie buffer dimensions/index");
    for (size_t i=0;i<3;++i) {
      const auto& plane=capture.planes[i];
      const auto w=i ? width/2 : width,h=i ? height/2 : height;
      if (plane.texture!=reader.Word(reader.Add(owner,20+uint32_t(i)*8+buffer*4)) || plane.pitch<w || plane.pitch>16384)
        throw std::runtime_error("movie lock does not match published YUV buffer");
      const auto creation=state.texture_creations.find(plane.texture);
      if (creation==state.texture_creations.end() || creation->second.format!=0x28000002 ||
          creation->second.width!=w || creation->second.height!=h)
        throw std::runtime_error("movie plane is not the verified linear 8-bit allocation");
      const auto* pixels=reader.Bytes(plane.pixels,size_t(plane.pitch)*(h-1)+w);
      auto& native=state.textures[plane.texture];
      native.content_valid=false;
      if (!native.resource || native.width!=w || native.height!=h) {
        edf::native::NativeTexture replacement;
        replacement.content_valid=false;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=w; desc.Height=h; desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R8_UNORM; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(state.device->CreateTexture2D(&desc,nullptr,&replacement.resource)) ||
            FAILED(state.device->CreateShaderResourceView(replacement.resource.Get(),nullptr,&replacement.view)))
          throw std::runtime_error("native movie plane allocation failed");
        replacement.width=w; replacement.height=h; replacement.mip_count=1;
        native=std::move(replacement);
      }
      state.context->UpdateSubresource(native.resource.Get(),0,nullptr,pixels,plane.pitch,0);
      native.content_valid=true;
      if (state.movie_uploads<3) REXLOG_INFO("Native movie plane upload: owner={:#x}, buffer={}, plane={}, texture={:#x}, {}x{}, pitch={}, pixels={:#x}",
        owner,buffer,i,plane.texture,w,h,plane.pitch,plane.pixels);
    }
    ++state.movie_uploads;
  } catch (const std::exception& error) {
    for (const auto& plane:capture.planes) if (const auto found=state.textures.find(plane.texture);found!=state.textures.end())
      found->second.content_valid=false;
    if (++state.movie_upload_errors<=8) REXLOG_ERROR("Native movie upload: {}",error.what());
  }
}
REX_EXTERN(__imp__sub_821FD8F8);
REX_EXTERN(__imp__edf_native_immediate_cpu_tail);
REX_HOOK_RAW(sub_821FD8F8) {
  edf::native::HookTiming native_timing(edf::native::HookPhase::ImmediateNative);
  bool native_submitted=false;
  if (REXCVAR_GET(edf_native_shader_bridge)) {
    auto& state = edf::native::State();
    edf::native::HookTiming submission_wait(edf::native::HookPhase::ImmediateSubmissionWait);
    std::lock_guard submission(state.submissions);
    submission_wait.Finish();
    edf::native::HookTiming context_wait(edf::native::HookPhase::ImmediateContextWait);
    std::lock_guard lock(state.mutex);
    context_wait.Finish();
    bool movie_draw=false;
    // Setters publish identity independently of material activation. Retain a
    // per-invocation copy while this hook holds the submission/state locks.
    std::optional<edf::native::GuestShaderPair> draw_shaders;
    auto bound_shaders=[&]() -> const edf::native::GuestShaderPair& {
      if(!draw_shaders)
        draw_shaders=state.shader_bindings.Pair(ctx.r3.u32);
      return *draw_shaders;
    };
    if (!state.active_target && state.scenes.contains(state.active_output)) try {
      const auto& pair=bound_shaders();
      const auto vs=state.embedded_shaders.find(pair.vertex),ps=state.embedded_shaders.find(pair.pixel);
      movie_draw=vs!=state.embedded_shaders.end() && ps!=state.embedded_shaders.end() &&
        !vs->second.pixel && vs->second.source==0x82060B70 && ps->second.pixel &&
        (ps->second.source==0x82064428 || ps->second.source==0x820641F0);
      if (movie_draw) {
        const edf::native::GuestReader backing(base);
        const edf::native::GuestReadWindow reader(backing,backing.Add(ctx.r3.u32,1024),12416-1024);
        auto word=[&](uint32_t offset) {return reader.Word(reader.Add(ctx.r3.u32,offset));};
        auto& scene=state.scenes.at(state.active_output);
        if (word(12168)!=scene.output_surface || ctx.r4.u32!=5 || ctx.r5.u32!=4 || ctx.r7.u32!=16)
          throw std::runtime_error("unsupported movie output/topology");
        const auto declaration=state.declarations.Get(state.declaration_bindings.at(ctx.r3.u32));
        if (declaration->count()!=2 ||
            declaration->Words<6>()!=std::array<uint32_t,6>{0,0x2c23a5,0,8,0x2c23a5,0x50000})
          throw std::runtime_error("unsupported movie vertex declaration");
        const auto viewport=edf::native::ReadNativeDrawViewport(reader,ctx.r3.u32);
        if (viewport.reverse_depth) throw std::runtime_error("unimplemented reversed movie projection");
        if (!state.movie_vertex) {
          const auto effect=edf::native::MakeNativeMovieEffect();
          auto vertex=std::make_unique<edf::native::ShaderBindings>(*state.device.Get(),
            edf::native::CompileNativeShader(*state.device.Get(),effect,effect.entries[0],"native_movie.fx"));
          auto pixel=std::make_unique<edf::native::ShaderBindings>(*state.device.Get(),
            edf::native::CompileNativeShader(*state.device.Get(),effect,effect.entries[1],"native_movie.fx"));
          auto pixel_sd=std::make_unique<edf::native::ShaderBindings>(*state.device.Get(),
            edf::native::CompileNativeShader(*state.device.Get(),effect,effect.entries[2],"native_movie.fx"));
          edf::native::ValidateNativeShaderLink(vertex->shader(),pixel->shader());
          edf::native::ValidateNativeShaderLink(vertex->shader(),pixel_sd->shader());
          auto vertices=std::make_unique<edf::native::QuadStream>(*state.device.Get(),vertex->shader());
          state.movie_bindings[0].emplace(*vertex,*pixel);
          state.movie_bindings[1].emplace(*vertex,*pixel_sd);
          state.movie_vertex=std::move(vertex); state.movie_pixel=std::move(pixel);
          state.movie_pixel_sd=std::move(pixel_sd); state.movie_vertices=std::move(vertices);
        }
        // Choose the program actually bound by the game, not a heuristic based
        // on the output resolution (SD movies may fill a 1280x720 target).
        auto& movie_pixel=ps->second.source==0x820641F0 ? *state.movie_pixel_sd : *state.movie_pixel;
        // CPU constant setters 82149248/82149358 copy float4 rows to
        // device+(112+register)*16 / device+(368+register)*16 respectively.
        auto registers=[&](uint32_t offset,size_t bytes) {return std::span<const uint8_t>{reader.Bytes(reader.Add(ctx.r3.u32,offset),bytes),bytes};};
        const auto& movie_plan=*state.movie_bindings[ps->second.source==0x820641F0?1:0];
        movie_plan.SetConstants(*state.movie_vertex,movie_pixel,registers(1792,160),registers(5888,16));
        movie_pixel.ClearTextures(); movie_pixel.ClearSamplers();
        for(uint32_t i=0;i<3;++i) {
          // 8213BA98 stores the raw SetTexture handle at (3068+slot)*4.
          const auto handle=word(12272+i*4);
          const auto texture=state.textures.find(handle);
          const auto creation=state.texture_creations.find(handle);
          if (texture==state.textures.end() || !texture->second.content_valid ||
              creation==state.texture_creations.end() || creation->second.format!=0x28000002)
            throw std::runtime_error("movie draw missing decoded native plane");
          movie_plan.SetTexture(movie_pixel,i,texture->second.view.Get());
          const auto offset=1024+i*24;
          const auto key=edf::native::SamplerStateKey({word(offset),word(offset+12),word(offset+16),word(offset+20)});
          auto cached=state.samplers.find(key);
          if(cached==state.samplers.end()) {
            const auto desc=edf::native::DecodeNativeSampler(key);
            Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
            if(FAILED(state.device->CreateSamplerState(&desc,&sampler))) throw std::runtime_error("movie sampler creation failed");
            cached=state.samplers.emplace(key,std::move(sampler)).first;
          }
          movie_plan.SetSampler(movie_pixel,i,cached->second.Get());
        }
        const auto key=edf::native::ReadAuditedRenderStateWords(reader,ctx.r3.u32);
        if ((key[1]&3)!=0) throw std::runtime_error("movie requires an unbound depth/stencil surface");
        auto render=state.render_states.find(key);
        if(render==state.render_states.end()) render=state.render_states.emplace(key,edf::native::CreateNativeRenderState(*state.device.Get(),key)).first;
        edf::native::BindActiveTarget(state);
        edf::native::BindGuestRenderState(render->second,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation); viewport.Bind(*state.context.Get());
        state.movie_vertex->Bind(*state.context.Get()); movie_pixel.Bind(*state.context.Get());
        state.movie_vertices->Draw(*state.context.Get(),{reader.Bytes(ctx.r6.u32,64),64});
        native_submitted=true;
        scene.frame_complete=false;
        ++state.movie_draws;
        if(state.presentation_frames && scene.output.content_valid)
          state.presentation_frames->Publish(*scene.output.surface.Get(),edf::native::NativeFrameKind::Movie,
            state.display_gamma?&*state.display_gamma:nullptr);
        if(state.movie_draws<=3 || state.movie_draws==30 || state.movie_draws==60 || state.movie_draws==120) {
          REXLOG_INFO("Native movie draw: submitted={}, PS={}, output_initialized={}, frame_complete=false",
            state.movie_draws,movie_pixel.shader().entry.name,scene.output.content_valid);
          const auto prefix=REXCVAR_GET(edf_native_scene_capture);
          if(!prefix.empty() && scene.output.content_valid) {
            const auto path=std::filesystem::path(prefix+".movie."+std::to_string(state.movie_draws)+".bmp");
            if(std::filesystem::exists(path)) throw std::runtime_error("native movie capture path already exists");
            const auto bytes=edf::native::CaptureNativeHdrBmp(*state.context.Get(),*scene.output.surface.Get());
            std::ofstream output(path,std::ios::binary);
            output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
            output.close();
            if(!output) throw std::runtime_error("native movie capture write failed");
          }
        }
      }
    } catch(const std::exception& error) {
      if(++state.movie_draw_errors<=10) REXLOG_ERROR("Native movie draw: {}",error.what());
    }
    bool xui_draw=false;
    const auto xui_scene_owner=state.active_scene?state.active_scene:state.active_output;
    if(!movie_draw && !state.active_target && state.scenes.contains(xui_scene_owner)) try {
      const auto& pair=bound_shaders();
      const auto vs=state.embedded_shaders.find(pair.vertex),ps=state.embedded_shaders.find(pair.pixel);
      xui_draw=vs!=state.embedded_shaders.end() && ps!=state.embedded_shaders.end() &&
        !vs->second.pixel && vs->second.source==0x820608B0 && ps->second.pixel &&
        (ps->second.source==0x82060EC8 || ps->second.source==0x82060DB0 || ps->second.source==0x82061848);
      if(xui_draw) {
        const edf::native::GuestReader backing(base);
        const auto device=ctx.r3.u32;
        const edf::native::GuestReadWindow reader(backing,backing.Add(device,1024),12416-1024);
        const auto snapshot=edf::native::ReadAuditedXuiDeviceWords(reader,device);
        auto& scene=state.scenes.at(xui_scene_owner);
        const bool scene_draw=state.active_scene!=0;
        const auto expected_surface=scene_draw?scene.color_surface:scene.output_surface;
        auto& target=scene_draw?scene.color:scene.output;
        if(!expected_surface || snapshot.surface!=expected_surface || ctx.r4.u32!=4 || ctx.r7.u32!=8 ||
           !ctx.r5.u32 || ctx.r5.u32%3 || ctx.r5.u32>16384)
          throw std::runtime_error("unsupported XUI textured output/topology");
        const auto declaration=state.declarations.Get(state.declaration_bindings.at(device));
        if(declaration->count()!=1 ||
           declaration->Words<3>()!=std::array<uint32_t,3>{0,0x2c23a5,0})
          throw std::runtime_error("unsupported XUI position declaration");
        auto viewport=edf::native::DecodeDrawViewport(snapshot.viewport);
        if(edf::native::NativeRenderDimensions()[0]>0)
          viewport=edf::native::ScaleNativeCanvasScissor(viewport,float(target.sampled.width)/1280,
            float(target.sampled.height)/720,false);
        const auto key=snapshot.render;
        if(!scene_draw && (viewport.reverse_depth || (key[1]&3)))
          throw std::runtime_error("unimplemented XUI depth contract");
        const bool solid=ps->second.source==0x82060DB0;
        const bool mask=ps->second.source==0x82061848;
        const auto texture=state.textures.find(snapshot.texture);
        if(!solid && (texture==state.textures.end() || !texture->second.content_valid || !texture->second.view))
          throw std::runtime_error("XUI textured brush has no native texture");
        if(!solid) {
          D3D11_SHADER_RESOURCE_VIEW_DESC view_desc{}; texture->second.view->GetDesc(&view_desc);
          if(view_desc.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D ||
             texture->second.resource.Get()==target.surface.Get())
            throw std::runtime_error("unsupported or aliased XUI brush texture");
        }
        if(!state.xui_vertex) {
          const auto effect=edf::native::MakeNativeXuiTextureEffect();
          auto vertex=std::make_unique<edf::native::ShaderBindings>(*state.device.Get(),
            edf::native::CompileNativeShader(*state.device.Get(),effect,effect.entries[0],"native_xui.fx"));
          auto pixel=std::make_unique<edf::native::ShaderBindings>(*state.device.Get(),
            edf::native::CompileNativeShader(*state.device.Get(),effect,effect.entries[1],"native_xui.fx"));
          auto solid_pixel=std::make_unique<edf::native::ShaderBindings>(*state.device.Get(),
            edf::native::CompileNativeShader(*state.device.Get(),effect,effect.entries[2],"native_xui.fx"));
          auto mask_pixel=std::make_unique<edf::native::ShaderBindings>(*state.device.Get(),
            edf::native::CompileNativeShader(*state.device.Get(),effect,effect.entries[3],"native_xui.fx"));
          edf::native::ValidateNativeShaderLink(vertex->shader(),pixel->shader());
          edf::native::ValidateNativeShaderLink(vertex->shader(),solid_pixel->shader());
          edf::native::ValidateNativeShaderLink(vertex->shader(),mask_pixel->shader());
          auto vertices=std::make_unique<edf::native::PositionTriangleStream>(*state.device.Get(),vertex->shader());
          state.xui_vertex_bindings.emplace(*vertex);
          state.xui_pixel_bindings[0].emplace(*pixel,false);
          state.xui_pixel_bindings[1].emplace(*solid_pixel,true);
          state.xui_pixel_bindings[2].emplace(*mask_pixel,false);
          state.xui_vertex=std::move(vertex); state.xui_pixel=std::move(pixel); state.xui_vertices=std::move(vertices);
          state.xui_solid_pixel=std::move(solid_pixel); state.xui_mask_pixel=std::move(mask_pixel);
        }
        if(viewport.reverse_depth && !state.xui_reversed_vertex) {
          const auto effect=edf::native::MakeNativeXuiTextureEffect();
          auto reversed=std::make_unique<edf::native::ShaderBindings>(*state.device.Get(),
            edf::native::CompileNativeShader(*state.device.Get(),effect,effect.entries[0],"native_xui.fx",true));
          state.xui_reversed_vertex_bindings.emplace(*reversed);
          state.xui_reversed_vertex=std::move(reversed);
        }
        auto& vertex=viewport.reverse_depth?*state.xui_reversed_vertex:*state.xui_vertex;
        auto& pixel=solid ? *state.xui_solid_pixel : mask ? *state.xui_mask_pixel : *state.xui_pixel;
        // Split into the three parts a batch would separate: converting guest
        // data, binding, and the draw itself. 2.87 million of these went
        // through here with no timing at all, so what they cost - and which
        // part of them is worth batching - was unknown.
        edf::native::HookTiming xui_total(edf::native::HookPhase::XuiNative);
        edf::native::HookTiming xui_decode(edf::native::HookPhase::XuiDecode);
        auto registers=[&](uint32_t offset,size_t bytes){
          return std::span<const uint8_t>{reader.Bytes(reader.Add(device,offset),bytes),bytes};};
        const auto vertex_registers=registers(1792,192);
        if(edf::native::NativeRenderDimensions()[0]>0 && state.xui_draws<5) {
          REXLOG_INFO("Native XUI resolution trace: viewport={},{} {}x{}, scissor={},{},{},{}, target={}x{}",
            viewport.viewport.TopLeftX,viewport.viewport.TopLeftY,viewport.viewport.Width,viewport.viewport.Height,
            viewport.scissor.left,viewport.scissor.top,viewport.scissor.right,viewport.scissor.bottom,
            target.sampled.width,target.sampled.height);
          for(uint32_t row=0;row<8;++row) {
            const auto offset=device+1792+row*16;
            REXLOG_INFO("Native XUI resolution matrix: row={}, values={},{},{},{}",row,
              std::bit_cast<float>(reader.Word(offset)),std::bit_cast<float>(reader.Word(offset+4)),
              std::bit_cast<float>(reader.Word(offset+8)),std::bit_cast<float>(reader.Word(offset+12)));
          }
        }
        const auto& vertex_plan=viewport.reverse_depth?*state.xui_reversed_vertex_bindings:*state.xui_vertex_bindings;
        const bool native_canvas=edf::native::NativeRenderDimensions()[0]>0;
        vertex_plan.SetConstants(vertex,vertex_registers,registers(5872,16),
          native_canvas?float(target.sampled.width)/1280.0f:1.0f,
          native_canvas?float(target.sampled.height)/720.0f:1.0f);
        const auto& pixel_plan=*state.xui_pixel_bindings[solid?1:mask?2:0];
        pixel_plan.SetConstants(pixel,registers(5904,16),solid?registers(5888,16):std::span<const uint8_t>{});
        if(!solid) {
        pixel_plan.SetTexture(pixel,texture->second.view.Get());
        const auto sampler_key=edf::native::SamplerStateKey(edf::native::ReadSamplerWords(reader,device,0));
        auto sampler=state.samplers.find(sampler_key);
        if(sampler==state.samplers.end()) {
          const auto desc=edf::native::DecodeNativeSampler(sampler_key);
          Microsoft::WRL::ComPtr<ID3D11SamplerState> native;
          if(FAILED(state.device->CreateSamplerState(&desc,&native))) throw std::runtime_error("XUI sampler creation failed");
          sampler=state.samplers.emplace(sampler_key,std::move(native)).first;
        }
        pixel_plan.SetSampler(pixel,sampler->second.Get());
        }
        if(REXCVAR_GET(edf_native_batch_audit)) {
          // State a batch must share, and constants it would have to carry per
          // draw, hashed apart - so the answer says not just "could these
          // merge" but what a merge would have to do about their differences.
          const auto mix=[](uint64_t hash,uint64_t value) {
            hash^=value; return hash*1099511628211ull;
          };
          uint64_t shape=1469598103934665603ull;
          for(const auto word:key) shape=mix(shape,word);
          shape=mix(shape,uint64_t(solid?1:mask?2:0));
          shape=mix(shape,reinterpret_cast<uintptr_t>(texture==state.textures.end()?nullptr:texture->second.view.Get()));
          shape=mix(shape,uint64_t(viewport.reverse_depth));
          shape=mix(shape,uint64_t(viewport.viewport.Width)*8191+uint64_t(viewport.viewport.Height));
          uint64_t constants=1469598103934665603ull;
          for(const auto byte:vertex_registers) constants=mix(constants,byte);
          ++state.xui_batch_draws;
          if(shape==state.xui_last_state && state.xui_batch_draws>1) {
            ++state.xui_batch_run;
            ++state.xui_batch_collapsible;
            if(constants!=state.xui_last_constants) ++state.xui_constants_differ;
          } else {
            state.xui_batch_longest=(std::max)(state.xui_batch_longest,state.xui_batch_run);
            if(state.xui_batch_run) ++state.xui_batch_runs;
            state.xui_batch_run=1;
          }
          state.xui_last_state=shape;
          state.xui_last_constants=constants;
          if(state.xui_batch_draws%500000==0)
            REXLOG_INFO("Native XUI batch audit: draws={}, runs={}, longest_run={}, collapsible={} ({:.1f}% share the state of the draw before), of those {} also change constants ({:.1f}%)",
              state.xui_batch_draws,state.xui_batch_runs,state.xui_batch_longest,
              state.xui_batch_collapsible,
              100.0*double(state.xui_batch_collapsible)/double(state.xui_batch_draws),
              state.xui_constants_differ,
              state.xui_batch_collapsible?100.0*double(state.xui_constants_differ)/double(state.xui_batch_collapsible):0.0);
        }
        xui_decode.Finish();
        edf::native::HookTiming xui_bind(edf::native::HookPhase::XuiBind);
        auto render=state.render_states.find(key);
        if(render==state.render_states.end())
          render=state.render_states.emplace(key,edf::native::CreateNativeRenderState(*state.device.Get(),key)).first;
        edf::native::BindActiveTarget(state);
        edf::native::BindGuestRenderState(render->second,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation); viewport.Bind(*state.context.Get());
        vertex.Bind(*state.context.Get()); pixel.Bind(*state.context.Get());
        xui_bind.Finish();
        edf::native::HookTiming xui_draw(edf::native::HookPhase::XuiDraw);
        const size_t bytes=size_t(ctx.r5.u32)*8;
        state.xui_vertices->Draw(*state.context.Get(),{reader.Bytes(ctx.r6.u32,bytes),bytes});
        xui_draw.Finish();
        native_submitted=true;
        // Partial alpha geometry cannot establish initialized full-frame pixels.
        // Preserve the preceding clear/post pass's initialization state.
        scene.frame_complete=false;
        if(++state.xui_draws<=5 || state.xui_draws%10000==0)
          REXLOG_INFO("Native XUI brush draw: submitted={}, PS={}, vertices={}, target_initialized={}, scene={}, reverse_depth={}",
            state.xui_draws,pixel.shader().entry.name,ctx.r5.u32,target.content_valid,scene_draw,viewport.reverse_depth);
      }
    } catch(const std::exception& error) {
      if(++state.xui_errors<=10) REXLOG_ERROR("Native XUI textured draw: {}",error.what());
    }
    bool font_draw=false;
    if(!movie_draw && !xui_draw && !state.active_target && state.scenes.contains(state.active_output)) try {
      const edf::native::GuestReader font_backing(base);
      const auto& pair=bound_shaders();
      // Shared declaration/VS/PS owned by retail font setup821AD3B8. Read
      // current handles rather than retaining identities across font releases.
      const auto font=edf::native::ReadGuestWords<3>(font_backing,0x8257C06C);
      font_draw=font[0] && font[1] && font[2] && pair.vertex==font[1] && pair.pixel==font[2];
      if(font_draw) {
        const auto device=ctx.r3.u32;
        const edf::native::GuestReadWindow reader(font_backing,font_backing.Add(device,1024),12416-1024);
        if(reader.Word(0x82556148)!=0x820179A8)
          throw std::runtime_error("unsupported font source pointer");
        const auto snapshot=edf::native::ReadAuditedXuiDeviceWords(reader,device);
        auto& scene=state.scenes.at(state.active_output);
        if(state.declaration_bindings.at(device)!=font[0] || snapshot.surface!=scene.output_surface ||
           ctx.r4.u32!=13 || ctx.r7.u32!=16 || !ctx.r5.u32 || ctx.r5.u32%4 || ctx.r5.u32>16384)
          throw std::runtime_error("unsupported font output/declaration/topology");
        const auto declaration=state.declarations.Get(font[0]);
        if(declaration->count()!=2 || !edf::native::IsFontVertexDeclaration(declaration->count(),
           declaration->Words<6>()))
          throw std::runtime_error("unsupported font vertex elements");
        auto viewport=edf::native::DecodeDrawViewport(snapshot.viewport);
        if(edf::native::NativeRenderDimensions()[0]>0)
          viewport=edf::native::ScaleNativeCanvasScissor(viewport,float(scene.output.sampled.width)/1280,
            float(scene.output.sampled.height)/720,true);
        const auto key=snapshot.render;
        if(viewport.reverse_depth || (key[1]&3))
          throw std::runtime_error("unimplemented font depth contract");
        const auto texture=state.textures.find(snapshot.texture);
        if(texture==state.textures.end() || !texture->second.content_valid || !texture->second.view)
          throw std::runtime_error("font atlas has no native texture");
        D3D11_SHADER_RESOURCE_VIEW_DESC view_desc{}; texture->second.view->GetDesc(&view_desc);
        if(view_desc.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D ||
           texture->second.resource.Get()==scene.output.surface.Get())
          throw std::runtime_error("unsupported or aliased font atlas");
        if(!state.font_vertex) {
          const auto effect=edf::native::MakeNativeFontEffect();
          auto vertex=std::make_unique<edf::native::ShaderBindings>(*state.device.Get(),
            edf::native::CompileNativeShader(*state.device.Get(),effect,effect.entries[0],"native_font.fx"));
          auto pixel=std::make_unique<edf::native::ShaderBindings>(*state.device.Get(),
            edf::native::CompileNativeShader(*state.device.Get(),effect,effect.entries[1],"native_font.fx"));
          edf::native::ValidateNativeShaderLink(vertex->shader(),pixel->shader());
          auto vertices=std::make_unique<edf::native::QuadStream>(*state.device.Get(),vertex->shader());
          const edf::native::NativeFontBindings plan(*vertex,*pixel);
          state.font_bindings=plan;
          state.font_vertex=std::move(vertex); state.font_pixel=std::move(pixel);
          state.font_vertices=std::move(vertices);
        }
        // Font producer821ADCE8 writes c1 directly for each glyph run. The
        // native cbuffer packs float2 fields, unlike the guest's register slots.
        const std::span<const uint8_t> vs{reader.Bytes(reader.Add(device,1808),64),64};
        const std::span<const uint8_t> ps{reader.Bytes(reader.Add(device,5888),32),32};
        const bool font_canvas=edf::native::NativeRenderDimensions()[0]>0;
        if(font_canvas && state.font_draws<5) {
          REXLOG_INFO("Native font canvas trace: viewport={},{} {}x{}, scissor_enabled={}, scissor={},{},{},{}, offset={},{} scale={},{}",
            viewport.viewport.TopLeftX,viewport.viewport.TopLeftY,viewport.viewport.Width,viewport.viewport.Height,
            snapshot.viewport.scissor_enabled,viewport.scissor.left,viewport.scissor.top,viewport.scissor.right,viewport.scissor.bottom,
            std::bit_cast<float>(reader.Word(device+1840)),std::bit_cast<float>(reader.Word(device+1844)),
            std::bit_cast<float>(reader.Word(device+1856)),std::bit_cast<float>(reader.Word(device+1860)));
        }
        state.font_bindings->SetConstants(*state.font_vertex,*state.font_pixel,vs,ps,
          font_canvas?float(scene.output.sampled.width)/1280.0f:1.0f,
          font_canvas?float(scene.output.sampled.height)/720.0f:1.0f);
        state.font_bindings->SetTexture(*state.font_pixel,texture->second.view.Get());
        const auto sampler_key=edf::native::SamplerStateKey(edf::native::ReadSamplerWords(reader,device,0));
        auto sampler=state.samplers.find(sampler_key);
        if(sampler==state.samplers.end()) {
          const auto desc=edf::native::DecodeNativeSampler(sampler_key);
          Microsoft::WRL::ComPtr<ID3D11SamplerState> native;
          if(FAILED(state.device->CreateSamplerState(&desc,&native))) throw std::runtime_error("font sampler creation failed");
          sampler=state.samplers.emplace(sampler_key,std::move(native)).first;
        }
        state.font_bindings->SetSampler(*state.font_pixel,sampler->second.Get());
        auto render=state.render_states.find(key);
        if(render==state.render_states.end())
          render=state.render_states.emplace(key,edf::native::CreateNativeRenderState(*state.device.Get(),key)).first;
        edf::native::BindActiveTarget(state);
        edf::native::BindGuestRenderState(render->second,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation); viewport.Bind(*state.context.Get());
        state.font_vertex->Bind(*state.context.Get()); state.font_pixel->Bind(*state.context.Get());
        const size_t bytes=size_t(ctx.r5.u32)*16;
        state.font_vertices->Draw(*state.context.Get(),{reader.Bytes(ctx.r6.u32,bytes),bytes});
        native_submitted=true;
        scene.frame_complete=false;
        if(++state.font_draws<=5 || state.font_draws%10000==0)
          REXLOG_INFO("Native font draw: submitted={}, vertices={}, output_initialized={}",
            state.font_draws,ctx.r5.u32,scene.output.content_valid);
        const auto prefix=REXCVAR_GET(edf_native_scene_capture);
        if(!prefix.empty() && scene.output.content_valid &&
           (state.font_draws==1 || state.font_draws==100 || state.font_draws==1000)) {
          const auto path=std::filesystem::path(prefix+".font."+std::to_string(state.font_draws)+".bmp");
          if(std::filesystem::exists(path)) throw std::runtime_error("native font capture path already exists");
          const auto bytes=edf::native::CaptureNativeHdrBmp(*state.context.Get(),*scene.output.surface.Get());
          std::ofstream output(path,std::ios::binary);
          output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
          output.close();
          if(!output) throw std::runtime_error("native font capture write failed");
        }
      }
    } catch(const std::exception& error) {
      if(++state.font_errors<=10) REXLOG_ERROR("Native font draw: {}",error.what());
    }
    bool utility_3d_draw=false;
    if(!movie_draw && !xui_draw && !font_draw && state.active_scene && !state.active_target) try {
      const auto& pair=bound_shaders();
      auto vertex=state.shaders.find(pair.vertex),pixel=state.shaders.find(pair.pixel);
      if(vertex!=state.shaders.end() && pixel!=state.shaders.end()) {
        const auto& vs=vertex->second.bindings->shader();
        auto& ps=*pixel->second.bindings;
        const bool solid=vs.source_fingerprint==0xc885203e230fe745ull &&
          ps.shader().source_fingerprint==0xc885203e230fe745ull &&
          vs.entry.name=="VS_3D" && ps.shader().entry.name=="PS_Main";
        const bool textured=vs.source_fingerprint==0xc885203e230fe745ull &&
          ps.shader().source_fingerprint==0xc885203e230fe745ull &&
          vs.entry.name=="VS_3DTex" && ps.shader().entry.name=="PS_Tex";
        const bool particle=vs.source_fingerprint==0x777f4cf51fb1b019ull &&
          ps.shader().source_fingerprint==0x777f4cf51fb1b019ull &&
          vs.entry.name=="Vs_Particle" &&
          (ps.shader().entry.name=="Ps_Particle" || ps.shader().entry.name=="Ps_ZParticle");
        utility_3d_draw=solid || textured || particle;
        if(utility_3d_draw) {
          const edf::native::GuestReader backing(base);
          const edf::native::GuestReadWindow reader(backing,backing.Add(ctx.r3.u32,1024),12416-1024);
          const uint32_t stride=solid?16:particle?44:36;
          const uint32_t element_count=solid?2:particle?4:3;
          const bool strip=ctx.r4.u32==6;
          if(pair.vertex!=state.active_vertex || pair.pixel!=state.linked_pixel ||
             (solid?!strip:particle?ctx.r4.u32!=13:(!strip && ctx.r4.u32!=13)) ||
             ctx.r7.u32!=stride || ctx.r5.u32<3 || ctx.r5.u32>16384 || (!strip && ctx.r5.u32%4))
            throw std::runtime_error("unsupported native scene immediate contract");
          auto& scene=state.scenes.at(state.active_scene);
          if(!scene.color_surface || reader.Word(reader.Add(ctx.r3.u32,12168))!=scene.color_surface)
            throw std::runtime_error("native scene immediate surface mismatch");
          const auto declaration=state.declaration_bindings.at(ctx.r3.u32);
          const auto owned_declaration=state.declarations.Get(declaration);
          if(owned_declaration->count()!=element_count)
            throw std::runtime_error("unsupported Utility 3D declaration count");
          const auto* elements=owned_declaration->bytes().data();
          auto word=[&](size_t at){return edf::native::GuestBlockWord(elements+at);};
          const size_t color=(element_count-1)*12;
          if(word(0)!=0 || word(4)!=0x2a23b9 || (word(8)&0xffffff00)!=0 ||
             word(color)!=(solid?12u:particle?28u:20u) || word(color+4)!=(solid?0x182886u:0x1a23a6u) ||
             (word(color+8)&0xffffff00)!=0xa0000 ||
             (!solid && (word(12)!=12 || word(16)!=0x2c23a5 || (word(20)&0xffffff00)!=0x50000)) ||
             (particle && (word(24)!=20 || word(28)!=0x2c23a5 || (word(32)&0xffffff00)!=0x50100)))
            throw std::runtime_error("unsupported Utility 3D vertex layout");
          const auto viewport=edf::native::ReadNativeDrawViewport(reader,ctx.r3.u32);
          auto& bindings=viewport.reverse_depth?*vertex->second.reversed_bindings:*vertex->second.bindings;
          if(!bindings.HasAllTextureInputs() || !ps.HasAllTextureInputs())
            throw std::runtime_error("Utility 3D missing texture inputs");
          if(bindings.UsesTextureResource(*scene.color.surface.Get()) || ps.UsesTextureResource(*scene.color.surface.Get()))
            throw std::runtime_error("native scene immediate samples its target");
          const auto owned_indices=state.generated_indices.Get(strip?edf::native::NativeIndexPattern::Strip:
            edf::native::NativeIndexPattern::Quads,ctx.r5.u32);
          const auto indices=owned_indices->bytes();
          const size_t bytes=size_t(ctx.r5.u32)*stride;
          const std::span<const uint8_t> vertices{reader.Bytes(ctx.r6.u32,bytes),bytes};
          auto& mesh=state.immediate_meshes.Acquire(EnsureSceneBackendLocked(state),bindings.shader(),
            edf::native::ImmediateStreamKey(vertices.size(),declaration,pair.vertex,ctx.r4.u32,viewport.reverse_depth),
            {elements,element_count*12},stride,
            vertices,indices,2,owned_declaration,owned_indices);
          const auto key=edf::native::ReadAuditedRenderStateWords(reader,ctx.r3.u32);
          auto render=state.render_states.find(key);
          if(render==state.render_states.end()) render=state.render_states.emplace(key,
            edf::native::CreateNativeRenderState(*state.device.Get(),key)).first;
          edf::native::BindActiveTarget(state);
          edf::native::BindGuestRenderState(render->second,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation); viewport.Bind(*state.context.Get());
          bindings.Bind(*state.context.Get()); ps.Bind(*state.context.Get());
          mesh.Draw(*state.context.Get(),0,uint32_t(indices.size()/2));
          native_submitted=true; scene.frame_complete=false;
          auto& reported=state.scene_immediate_variants_reported[solid?0:textured?(strip?1:2):ps.shader().entry.name=="Ps_ZParticle"?4:3];
          if(++state.utility_3d_draws<=5 || !reported || state.utility_3d_draws%10000==0)
            REXLOG_INFO("Native scene immediate: submitted={}, vertices={}, reverse_depth={}, scene={:#x}, VS={}, PS={}, stride={}",
              state.utility_3d_draws,ctx.r5.u32,viewport.reverse_depth,state.active_scene,
              vs.entry.name,ps.shader().entry.name,stride);
          reported=true;
        }
      }
    } catch(const std::exception& error) {
      if(++state.utility_3d_errors<=10) REXLOG_ERROR("Native Utility 3D strip: {}",error.what());
    }
    bool utility_draw=false;
    const auto utility_scene_owner=state.active_scene?state.active_scene:state.active_output;
    if(!movie_draw && !xui_draw && !font_draw && !state.active_target && state.scenes.contains(utility_scene_owner)) try {
      const auto& pair=bound_shaders();
      const auto vertex=state.shaders.find(pair.vertex),pixel=state.shaders.find(pair.pixel);
      if(vertex!=state.shaders.end() && pixel!=state.shaders.end()) {
        auto& vs=*vertex->second.bindings; auto& ps=*pixel->second.bindings;
        constexpr uint64_t utility_source=0xc885203e230fe745ull;
        const bool textured=vs.shader().entry.name=="VS_2DTex" && ps.shader().entry.name=="PS_Tex";
        utility_draw=vs.shader().source_fingerprint==utility_source && ps.shader().source_fingerprint==utility_source &&
          (textured || (vs.shader().entry.name=="VS_2D" && ps.shader().entry.name=="PS_Main"));
        if(utility_draw) {
          const edf::native::GuestReader backing(base);
          const edf::native::GuestReadWindow reader(backing,backing.Add(ctx.r3.u32,1024),12416-1024);
          const auto snapshot=edf::native::ReadAuditedXuiDeviceWords(reader,ctx.r3.u32);
          auto& scene=state.scenes.at(utility_scene_owner);
          const bool scene_draw=state.active_scene!=0;
          const auto expected_surface=scene_draw?scene.color_surface:scene.output_surface;
          auto& target=scene_draw?scene.color:scene.output;
          const uint32_t stride=textured?20:12,element_count=textured?3:2;
          const bool lines=ctx.r4.u32==2;
          if(pair.vertex!=state.active_vertex || pair.pixel!=state.linked_pixel ||
             !expected_surface || snapshot.surface!=expected_surface ||
             (!lines && ctx.r4.u32!=13) || ctx.r7.u32!=stride || !ctx.r5.u32 || ctx.r5.u32%(lines?2:4) || ctx.r5.u32>16384)
            throw std::runtime_error("unsupported Utility output/binding/topology");
          const auto declaration_handle=state.declaration_bindings.at(ctx.r3.u32);
          const auto owned_declaration=state.declarations.Get(declaration_handle);
          if(owned_declaration->count()!=element_count)
            throw std::runtime_error("unsupported Utility declaration count");
          const auto* declaration=owned_declaration->bytes().data();
          auto word=[&](size_t offset){return edf::native::GuestBlockWord(declaration+offset);};
          std::shared_ptr<const edf::native::NativeDeclaration> native_declaration;
          try { native_declaration=owned_declaration->Utility2D(textured); }
          catch(const std::exception&) {
            if(state.utility_errors<10) for(uint32_t element=0;element<element_count;++element)
              REXLOG_INFO("Native Utility element: stride={}, element={}, offset={:#x}, type={:#x}, semantic={:#x}",
                stride,element,word(element*12),word(element*12+4),word(element*12+8));
            throw std::runtime_error("unsupported Utility vertex elements");
          }
          auto viewport=edf::native::DecodeDrawViewport(snapshot.viewport);
          if(edf::native::NativeRenderDimensions()[0]>0)
            viewport=edf::native::ScaleNativeCanvasScissor(viewport,float(edf::native::NativeRenderDimensions()[0])/1280,
              float(edf::native::NativeRenderDimensions()[1])/720,true);
          // Ordinary output has no depth attachment. The HDR scene does, and
          // uses the same reversed-clip shader contract as indexed scene draws.
          if(!scene_draw && (viewport.reverse_depth || (snapshot.render[1]&3)))
            throw std::runtime_error("unsupported Utility output depth contract");
          auto& bindings=viewport.reverse_depth?*vertex->second.reversed_bindings:vs;
          if(!bindings.HasAllTextureInputs()) throw std::runtime_error("Utility vertex shader has missing native texture inputs");
          if(!ps.HasAllTextureInputs()) throw std::runtime_error("Utility has missing native texture inputs");
          if(ps.UsesTextureResource(*target.surface.Get()) || bindings.UsesTextureResource(*target.surface.Get()))
            throw std::runtime_error("Utility samples its active surface");
          const auto owned_indices=state.generated_indices.Get(lines?edf::native::NativeIndexPattern::Lines:
            edf::native::NativeIndexPattern::Quads,ctx.r5.u32);
          const auto indices=owned_indices->bytes();
          const size_t bytes=size_t(ctx.r5.u32)*stride;
          const std::span<const uint8_t> vertices{reader.Bytes(ctx.r6.u32,bytes),bytes};
          auto& mesh=state.immediate_meshes.Acquire(EnsureSceneBackendLocked(state),bindings.shader(),
            edf::native::ImmediateStreamKey(vertices.size(),declaration_handle,pair.vertex,ctx.r4.u32,viewport.reverse_depth),
            native_declaration->bytes(),stride,
            vertices,indices,2,native_declaration,owned_indices);
          auto render=state.render_states.find(snapshot.render);
          if(render==state.render_states.end())
            render=state.render_states.emplace(snapshot.render,edf::native::CreateNativeRenderState(*state.device.Get(),snapshot.render)).first;
          edf::native::BindActiveTarget(state);
          edf::native::BindGuestRenderState(render->second,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation); viewport.Bind(*state.context.Get());
          bindings.Bind(*state.context.Get()); ps.Bind(*state.context.Get());
          if(lines) mesh.DrawLines(*state.context.Get(),0,uint32_t(indices.size()/2));
          else mesh.Draw(*state.context.Get(),0,uint32_t(indices.size()/2));
          native_submitted=true; scene.frame_complete=false;
          auto& variant_reported=state.utility_variants_reported[(textured?2:0)+(lines?1:0)];
          if(++state.utility_draws<=5 || !variant_reported)
            REXLOG_INFO("Native Utility draw: submitted={}, VS={}, PS={}, vertices={}, lines={}, scene={}, reverse_depth={}",
              state.utility_draws,vs.shader().entry.name,ps.shader().entry.name,ctx.r5.u32,lines,scene_draw,viewport.reverse_depth);
          variant_reported=true;
        }
      }
    } catch(const std::exception& error) {
      if(++state.utility_errors<=10) REXLOG_ERROR("Native Utility quad: {}",error.what());
    }
    const bool output_candidate=!movie_draw && !xui_draw && !font_draw && !utility_draw && !state.active_target && state.scenes.contains(state.active_output) &&
      state.shaders.contains(state.linked_pixel) &&
      state.shaders.at(state.linked_pixel).bindings->shader().entry.name=="PS_Bloom";
    bool output_draw=false;
    if (output_candidate) {
      try {
        const edf::native::GuestReader reader(base);
        const auto& pair=bound_shaders();
        output_draw=pair.vertex==state.active_vertex &&
          pair.pixel==state.linked_pixel &&
          ctx.r4.u32==13 && ctx.r7.u32==16;
        if (!output_draw && ++state.output_unhandled<=5) {
          const auto actual_vs=pair.vertex;
          const auto actual_ps=pair.pixel;
          REXLOG_INFO("Native ordinary output unhandled draw: caller={:#x}, primitive={}, stride={}, VS={:#x}, PS={:#x}; UI coverage incomplete",
            uint32_t(ctx.lr),ctx.r4.u32,ctx.r7.u32,actual_vs,actual_ps);
          for (const auto handle:{actual_vs,actual_ps}) if (state.embedded_shaders.contains(handle)) {
            const auto identity=state.embedded_shaders.at(handle);
            REXLOG_INFO("Native embedded shader identity: handle={:#x}, source={:#x}, pixel={}",handle,identity.source,identity.pixel);
          }
          for(const auto handle:{actual_vs,actual_ps}) if(state.shaders.contains(handle)) {
            const auto& shader=state.shaders.at(handle).bindings->shader();
            REXLOG_INFO("Native source shader identity: handle={:#x}, entry={}, pixel={}, source_bytes={}, fingerprint={:#x}",
              handle,shader.entry.name,shader.entry.pixel,shader.source_bytes,shader.source_fingerprint);
          }
        }
      } catch (const std::exception& error) { REXLOG_ERROR("Native ordinary output selection: {}",error.what()); }
    }
    // Observe every unsupported output pair, not just draws following Bloom.
    // Otherwise an unimplemented menu shader can disappear without evidence.
    if(!movie_draw && !xui_draw && !font_draw && !utility_draw && !output_draw && !state.active_target &&
       state.scenes.contains(state.active_output) && state.unsupported_output_pairs.size()<64) try {
      const auto& pair=bound_shaders();
      const std::array<uint32_t,4> key{pair.vertex,pair.pixel,ctx.r4.u32,ctx.r7.u32};
      edf::native::ReportNativeContractRejection(state,edf::native::MakeNativeContract(state,
        edf::native::NativeContractPath::Output,pair.vertex,pair.pixel,0,ctx.r4.u32,ctx.r7.u32),
        "no native implementation for this output shader pair");
      if(state.unsupported_output_pairs.insert(key).second) {
        REXLOG_INFO("Native unsupported output shader pair: caller={:#x}, VS={:#x}, PS={:#x}, primitive={}, stride={}, vertices={}",
          uint32_t(ctx.lr),pair.vertex,pair.pixel,ctx.r4.u32,ctx.r7.u32,ctx.r5.u32);
        for(const auto handle:{pair.vertex,pair.pixel}) {
          if(const auto found=state.embedded_shaders.find(handle);found!=state.embedded_shaders.end())
            REXLOG_INFO("Native unsupported embedded source: handle={:#x}, source={:#x}, pixel={}",
              handle,found->second.source,found->second.pixel);
          if(const auto found=state.shaders.find(handle);found!=state.shaders.end()) {
            const auto& shader=found->second.bindings->shader();
            REXLOG_INFO("Native unsupported material source: handle={:#x}, entry={}, fingerprint={:#x}",
              handle,shader.entry.name,shader.source_fingerprint);
          }
        }
      }
    } catch(const std::exception& error) {
      if(++state.output_unhandled<=5) REXLOG_ERROR("Native unsupported output inspection: {}",error.what());
    }
    if (state.render_targets.contains(state.active_target) || output_draw) {
      ++state.immediate_draws;
      try {
        const edf::native::GuestReader backing(base);
        const edf::native::GuestReadWindow reader(backing,backing.Add(ctx.r3.u32,1024),12416-1024);
        if (output_draw && reader.Word(reader.Add(ctx.r3.u32,12168))!=state.scenes.at(state.active_output).output_surface)
          throw std::runtime_error("ordinary output does not match guest bound surface");
        const auto declaration = state.declaration_bindings.at(ctx.r3.u32);
        const auto owned_declaration=state.declarations.Get(declaration);
        const auto count = owned_declaration->count();
        if (state.immediate_draws <= 5) REXLOG_INFO("Native immediate draw: primitive={}, vertices={}, stride={}, declaration={:#x}, elements={}",
                    ctx.r4.u32,ctx.r5.u32,ctx.r7.u32,declaration,count);
        if(state.immediate_draws<=5) for (uint32_t i = 0; i < count; ++i) {
          const auto element = owned_declaration->Words<3>(i*12);
          REXLOG_INFO("Native immediate element: stream_offset={:#x}, type={:#x}, method_usage_index={:#x}",
                      element[0],element[1],element[2]);
        }
        if (ctx.r4.u32 != 13 || ctx.r7.u32 != 16 || !ctx.r5.u32 || ctx.r5.u32 % 4 || ctx.r5.u32 > 16384 || count != 2 ||
            owned_declaration->Words<6>()!=std::array<uint32_t,6>{0,0x2c23a5,0,8,0x2c23a5,0x50000})
          throw std::runtime_error("unsupported native immediate vertex declaration/topology");
        const auto found = state.shaders.find(state.active_vertex);
        if (found == state.shaders.end()) throw std::runtime_error("missing native immediate vertex shader");
        const auto& pair=bound_shaders();
        if (pair.vertex!=state.active_vertex || pair.pixel!=state.linked_pixel)
          throw std::runtime_error("native immediate shader pair differs from guest device");
        auto& shader = found->second;
        const size_t bytes = size_t(ctx.r5.u32)*16;
        const auto render_key=edf::native::ReadAuditedRenderStateWords(reader,ctx.r3.u32);
        auto render_state = state.render_states.find(render_key);
        if (render_state == state.render_states.end()) {
          auto native = edf::native::CreateNativeRenderState(*state.device.Get(),render_key);
          render_state = state.render_states.emplace(render_key,std::move(native)).first;
          REXLOG_INFO("Native render state: cached={}, blend={:#x}, depth={:#x}, raster={:#x}, alpha={:#x}, write_mask={}, scissor={}",
                      state.render_states.size(),render_key[0],render_key[1],render_key[2],render_key[3],render_key[4],render_key[5]);
        }
        edf::native::BindGuestRenderState(render_state->second,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation);
        const auto viewport=edf::native::ReadNativeDrawViewport(reader,ctx.r3.u32);
        if (state.immediate_draws<=15) {
          REXLOG_INFO("Native post pass: VS={}, PS={}, viewport={},{},{}x{}, depth={}..{}, reverse={}",
            shader.bindings->shader().entry.name,state.shaders.at(state.linked_pixel).bindings->shader().entry.name,
            viewport.viewport.TopLeftX,viewport.viewport.TopLeftY,viewport.viewport.Width,viewport.viewport.Height,
            viewport.viewport.MinDepth,viewport.viewport.MaxDepth,viewport.reverse_depth);
          for (uint32_t i=0;i<4;++i) {
            const auto vertex=reader.Add(ctx.r6.u32,i*16);
            REXLOG_INFO("Native post vertex: index={}, position={},{} UV={},{}",i,
              std::bit_cast<float>(reader.Word(vertex)),std::bit_cast<float>(reader.Word(reader.Add(vertex,4))),
              std::bit_cast<float>(reader.Word(reader.Add(vertex,8))),std::bit_cast<float>(reader.Word(reader.Add(vertex,12))));
          }
        }
        viewport.Bind(*state.context.Get());
        auto& bindings=viewport.reverse_depth ? *shader.reversed_bindings : *shader.bindings;
        auto& quads=viewport.reverse_depth ? shader.reversed_quads : shader.quads;
        if (!quads) quads=std::make_unique<edf::native::QuadStream>(*state.device.Get(),bindings.shader());
        bindings.Bind(*state.context.Get());
        const std::span<const uint8_t> vertices{reader.Bytes(ctx.r6.u32,bytes),bytes};
        auto& pixel=*state.shaders.at(state.linked_pixel).bindings;
        pixel.Bind(*state.context.Get());
        auto& target=output_draw ? state.scenes.at(state.active_output).output : state.render_targets.at(state.active_target).native;
        // Read the actual post-pass views before drawing, not a guessed target
        // from the resource registry after later passes may have changed it.
        const auto post_prefix=REXCVAR_GET(edf_native_output_capture_scene_color)
          ? REXCVAR_GET(edf_native_scene_capture) : std::string{};
        const auto post_frame=state.indexed_output_frames+1;
        if((output_draw || state.active_target) && pixel.shader().source_fingerprint==0x6b7926f9747c6933ull &&
           REXCVAR_GET(edf_native_output_capture_scene_color) && !post_prefix.empty() &&
           state.indexed_submitted>state.scene_indexed_start &&
           edf::native::ShouldCaptureNativeOutput(post_frame,state.output_captures,
             REXCVAR_GET(edf_native_output_capture_limit),REXCVAR_GET(edf_native_output_capture_interval),
             REXCVAR_GET(edf_native_output_capture_start_frame)) &&
           (state.post_input_capture_frame!=post_frame || state.post_input_capture_pass<32)) {
          if(state.post_input_capture_frame!=post_frame) {
            state.post_input_capture_frame=post_frame; state.post_input_capture_pass=0;
          }
          const auto pass=++state.post_input_capture_pass;
          try {
            REXLOG_INFO("Native post chain pass: frame={}, pass={}, shader={}, target={}x{}",
              post_frame,pass,pixel.shader().entry.name,target.sampled.width,target.sampled.height);
            for(const auto* name:{"m_DiffuseTexture0_Sampler","m_DiffuseTexture1_Sampler","m_Tone_Sampler","m_OldTone_Sampler"}) {
              const auto view=pixel.ReadTexture(name);
              if(!view) continue; // A pass only reflects the inputs it consumes.
              const auto sampler=pixel.ReadSampler(name);
              if(sampler) {
                D3D11_SAMPLER_DESC desc{}; sampler->GetDesc(&desc);
                REXLOG_INFO("Native exact post sampler: frame={}, pass={}, name={}, filter={}, address={}/{}/{}, lod={}..{}, bias={}",
                  post_frame,pass,name,uint32_t(desc.Filter),uint32_t(desc.AddressU),uint32_t(desc.AddressV),uint32_t(desc.AddressW),desc.MinLOD,desc.MaxLOD,desc.MipLODBias);
              }
              D3D11_SHADER_RESOURCE_VIEW_DESC view_desc{}; view->GetDesc(&view_desc);
              if(view_desc.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D || view_desc.Texture2D.MostDetailedMip!=0)
                throw std::runtime_error("unsupported post diagnostic view");
              Microsoft::WRL::ComPtr<ID3D11Resource> resource; view->GetResource(&resource);
              Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
              if(FAILED(resource.As(&texture))) throw std::runtime_error("post diagnostic input is not 2D");
              D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
              const auto value=edf::native::ReadNativeColorPixel(*state.context.Get(),*texture.Get(),desc.Width/2,desc.Height/2);
              REXLOG_INFO("Native exact post input: frame={}, pass={}, name={}, {}x{}, view_format={}, center={},{},{},{}",
                post_frame,pass,name,desc.Width,desc.Height,uint32_t(view_desc.Format),value[0],value[1],value[2],value[3]);
              // The saved BMP clamps to [0,1]; the tone curve's behaviour depends
              // on whether anything actually exceeds 1. Report the real range.
              const auto hdr=edf::native::InspectNativeHdrColor(*state.context.Get(),*texture.Get());
              REXLOG_INFO("Native exact post input range: frame={}, pass={}, name={}, pixels={}, nonfinite={}, min={},{},{}, max={},{},{}, mean={},{},{}, above_1={}, above_2={}, above_4={}, above_8={}",
                post_frame,pass,name,hdr.pixels,hdr.nonfinite_pixels,
                hdr.minimum[0],hdr.minimum[1],hdr.minimum[2],hdr.maximum[0],hdr.maximum[1],hdr.maximum[2],
                hdr.mean[0],hdr.mean[1],hdr.mean[2],hdr.above[0],hdr.above[1],hdr.above[2],hdr.above[3]);
              if(hdr.negative_pixels)
                REXLOG_WARN("Native exact post input negatives: frame={}, pass={}, name={}, negative_pixels={} of {}, worst_at={},{} (negative radiance; the tone curve maps a large negative to white)",
                  post_frame,pass,name,hdr.negative_pixels,hdr.pixels,hdr.worst_x,hdr.worst_y);
              const auto path=std::filesystem::path(post_prefix+".pass."+std::to_string(pass)+"."+pixel.shader().entry.name+".input."+name+"."+std::to_string(post_frame)+".bmp");
              if(std::filesystem::exists(path)) throw std::runtime_error("post input capture already exists");
              const auto bmp=edf::native::CaptureNativeHdrBmp(*state.context.Get(),*texture.Get());
              std::ofstream file(path,std::ios::binary);
              file.write(reinterpret_cast<const char*>(bmp.data()),bmp.size()); file.close();
              if(!file) throw std::runtime_error("post input capture write failed");
            }
            for(const auto* name:{"g_PostEffect_MiddleGray","g_PostEffect_LuminanceWhite",
                                  "g_PostEffect_ToneMap"}) {
              const auto value=pixel.ReadFloatVector(name);
              if(!value.empty()) REXLOG_INFO("Native exact post constant: frame={}, pass={}, {}={}",post_frame,pass,name,value[0]);
            }
            // The sampling geometry decides whether a reduction averages the
            // intended footprint and whether a blur preserves its input's mean.
            // Report the authored values, not an assumed normalized kernel.
            if(const auto offsets=pixel.ReadFloatArray("m_DownsampleUVOffset");!offsets.empty()) {
              std::string text;
              for(size_t i=0;i+1<offsets.size();i+=2)
                text+=std::format("{}({},{})",text.empty()?"":" ",offsets[i],offsets[i+1]);
              REXLOG_INFO("Native exact post downsample offsets: frame={}, pass={}, count={}, uv={}",
                post_frame,pass,offsets.size()/2,text);
            }
            if(const auto taps=pixel.ReadFloatArray("m_GaussBlurUVOffset");!taps.empty()) {
              float weight_sum=0;
              std::string text;
              for(size_t i=0;i+2<taps.size();i+=3) {
                weight_sum+=taps[i+2];
                text+=std::format("{}({},{})*{}",text.empty()?"":" ",taps[i],taps[i+1],taps[i+2]);
              }
              REXLOG_INFO("Native exact post blur kernel: frame={}, pass={}, taps={}, weight_sum={}, offsets={}",
                post_frame,pass,taps.size()/3,weight_sum,text);
            }
          } catch(const std::exception& error) {
            // Optional diagnostics must never suppress the real draw.
            REXLOG_ERROR("Native post input capture: {}",error.what());
          }
        }
        const bool initialized=edf::native::CanInitializeReductionTarget(bindings.shader(),pixel.shader(),
          vertices,viewport,render_key,target.sampled.width,target.sampled.height,pixel.HasAllTextureInputs());
        // The offset follows the guest's live PA_SU_VTX_CNTL rather than a
        // fixed assumption, exactly as the SDK's own GPU path selects it. Only
        // the audited full-screen post passes consume it so far: indexed world
        // geometry, font, movie and the remaining immediate draws each need
        // their own validation before the same offset is extended to them.
        const auto centers_word=edf::native::ReadVertexCenterWord(reader,ctx.r3.u32);
        if(state.vertex_center_word!=centers_word) {
          state.vertex_center_word=centers_word;
          const auto centers=edf::native::DecodeGuestVertexCenters(centers_word);
          REXLOG_INFO("Native guest vertex centers: PA_SU_VTX_CNTL={:#x}, integer_centers={}, rounding={}, quantization={}, enabled={}",
            centers_word,centers.integer_centers,centers.rounding,centers.quantization,
            REXCVAR_GET(edf_native_pixel_centers));
        }
        const float center_offset=REXCVAR_GET(edf_native_pixel_centers)
          ? edf::native::GuestPixelCenterOffset(centers_word) : 0.f;
        const bool shift_centers=initialized && center_offset!=0.f;
        if(shift_centers) {
          auto shifted=viewport.viewport;
          shifted.TopLeftX+=center_offset; shifted.TopLeftY+=center_offset;
          state.context->RSSetViewports(1,&shifted);
        }
        try { quads->Draw(*state.context.Get(),vertices); }
        catch(...) { if(shift_centers) viewport.Bind(*state.context.Get()); throw; }
        if(shift_centers) viewport.Bind(*state.context.Get());
        if(output_draw && state.scene_gpu_timer && state.scene_gpu_timer_resolved &&
           state.scene_gpu_timer_owner==state.active_output) {
          try { state.scene_gpu_timer->End(); }
          catch(const std::exception& error) {
            state.scene_gpu_timer.reset();
            REXLOG_ERROR("Native frame GPU timing end: {}",error.what());
          }
          state.scene_gpu_timer_owner=0;
          state.scene_gpu_timer_resolved=false;
        }
        if(state.scene_gpu_timer_owner && state.scene_gpu_timer_resolved &&
           pixel.shader().entry.name=="PS_Downsample_Tone") {
          for(const auto* name:{"g_PostEffect_MiddleGray","g_PostEffect_ToneMap"}) {
            const auto value=pixel.ReadFloatVector(name);
            if(!value.empty()) REXLOG_INFO("Native tone history constant: {}={}, scene={}",name,value[0],state.scene_begins);
          }
        }
        native_submitted=true;
        ++state.native_quad_draws;
        target.content_valid=initialized;
        if (output_draw && ++state.output_draws<=5)
          REXLOG_INFO("Native final bloom: draws={}, initialized={}, frame_complete=false",state.output_draws,initialized);
        const auto bloom_now=std::chrono::steady_clock::now();
        if (output_draw && (state.output_draws<=5 ||
            (!REXCVAR_GET(edf_native_scene_capture).empty() &&
             bloom_now-state.bloom_parameters_reported>=std::chrono::seconds(10)))) {
          state.bloom_parameters_reported=bloom_now;
          for (const auto* name:{"g_PostEffect_MiddleGray","g_PostEffect_LuminanceWhite"}) {
            const auto value=pixel.ReadFloatVector(name);
            if (!value.empty()) REXLOG_INFO("Native bloom constant: {}={}, indexed_frame={}, output_draw={}",
              name,value[0],state.indexed_output_frames,state.output_draws);
          }
        }
        if (state.native_quad_draws <= 15 || state.native_quad_draws % 1000 == 0)
          REXLOG_INFO("Native immediate draw: submitted={}, errors={}, contents_valid={}",state.native_quad_draws,state.native_quad_errors,initialized);
      } catch (const std::exception& error) {
        ++state.native_quad_errors;
        if (output_draw) state.scenes.at(state.active_output).output.content_valid=false;
        else state.render_targets.at(state.active_target).native.content_valid=false;
        if (state.native_quad_errors <= 10) REXLOG_ERROR("Native immediate draw: {}",error.what());
      }
    }
    ++state.immediate_requests;
    if(!ctx.r5.u32) ++state.immediate_empty;
    else if(native_submitted) ++state.immediate_submitted;
    else {
      ++state.immediate_unsubmitted;
      try {
        const edf::native::GuestReader reader(base);
        edf::native::ReportNativeContractRejection(state,edf::native::MakeNativeContract(state,
          edf::native::NativeContractPath::Immediate,state.active_vertex,state.linked_pixel,
          reader.Word(reader.Add(ctx.r3.u32,11536)),ctx.r4.u32,ctx.r7.u32),
          "immediate draw not submitted natively");
      } catch(const std::exception&) {
        edf::native::ReportNativeContractRejection(state,edf::native::MakeNativeContract(state,
          edf::native::NativeContractPath::Immediate,state.active_vertex,state.linked_pixel,
          0,ctx.r4.u32,ctx.r7.u32),"immediate draw not submitted natively; device unreadable");
      }
      // This is evidence of routing coverage, not automatically a pixel bug:
      // the guest may issue draws outside a valid target or during teardown.
      if(state.immediate_unsubmitted_paths.size()<64) {
        const std::array<uint32_t,5> key{uint32_t(ctx.lr),ctx.r4.u32,ctx.r7.u32,state.active_vertex,state.linked_pixel};
        if(state.immediate_unsubmitted_paths.insert(key).second) {
          REXLOG_INFO("Native immediate unsubmitted path: caller={:#x}, primitive={}, stride={}, vertices={}, active_VS={:#x}, active_PS={:#x}, target={:#x}, output={:#x}",
            key[0],key[1],key[2],ctx.r5.u32,key[3],key[4],state.active_target,state.active_output);
          try {
            const edf::native::GuestReader reader(base);
            const auto surface=reader.Word(reader.Add(ctx.r3.u32,12168));
            uint32_t scene_owner=0;
            for(const auto& [owner,scene]:state.scenes)
              if(scene.output_surface==surface) {scene_owner=owner; break;}
            REXLOG_INFO("Native unsubmitted target: guest_surface={:#x}, registered_surface={}, known_output_owner={:#x}, scene_count={}",
              surface,state.surface_creations.contains(surface),scene_owner,state.scenes.size());
            REXLOG_INFO("Native unsubmitted scene: active_scene={:#x}",state.active_scene);
            const auto pair=edf::native::ReadShaderPair(reader,ctx.r3.u32);
            const auto draw_view=edf::native::ReadNativeDrawViewport(reader,ctx.r3.u32);
            const auto render_words=edf::native::ReadAuditedRenderStateWords(reader,ctx.r3.u32);
            REXLOG_INFO("Native unsubmitted bindings: VS={:#x}, PS={:#x}, reverse_depth={}, depth_state={:#x}",
              pair.vertex,pair.pixel,draw_view.reverse_depth,render_words[1]);
            for(auto handle:{pair.vertex,pair.pixel}) {
              const auto embedded=state.embedded_shaders.find(handle);
              if(embedded!=state.embedded_shaders.end())
                REXLOG_INFO("Native unsubmitted embedded shader: handle={:#x}, source={:#x}, pixel={}",
                  handle,embedded->second.source,embedded->second.pixel);
              const auto found=state.shaders.find(handle);
              if(found!=state.shaders.end()) {
                const auto& shader=found->second.bindings->shader();
                REXLOG_INFO("Native unsubmitted shader: handle={:#x}, entry={}, fingerprint={:#x}",
                  handle,shader.entry.name,shader.source_fingerprint);
              }
            }
            const auto declaration=reader.Word(reader.Add(ctx.r3.u32,11536));
            const auto count=reader.Word(reader.Add(declaration,24));
            if(count<=16) for(uint32_t element=0;element<count;++element) {
              const auto words=edf::native::ReadGuestWords<3>(reader,reader.Add(declaration,52+element*12));
              REXLOG_INFO("Native unsubmitted element: element={}, offset={:#x}, type={:#x}, semantic={:#x}",
                element,words[0],words[1],words[2]);
            }
          } catch(const std::exception& error) {
            REXLOG_INFO("Native unsubmitted target inspection: {}",error.what());
          }
        }
      }
    }
    const auto now=std::chrono::steady_clock::now();
    if(state.immediate_requests>=1024 && now-state.immediate_coverage_reported>=std::chrono::seconds(10)) {
      state.immediate_coverage_reported=now;
      REXLOG_INFO("Native immediate coverage: requests={}, empty={}, submitted={}, unsubmitted={}, sampled_paths={} (64-path cap; not visual completeness)",
        state.immediate_requests,state.immediate_empty,state.immediate_submitted,state.immediate_unsubmitted,state.immediate_unsubmitted_paths.size());
      REXLOG_INFO("Native immediate mesh cache: builds={}, hits={}, updates={}, bytes={}, entries={}, entry_evictions={}, budget_evictions={}",
        state.immediate_meshes.builds(),state.immediate_meshes.hits(),state.immediate_meshes.updates(),state.immediate_meshes.bytes(),
        state.immediate_meshes.entries(),state.immediate_meshes.entry_evictions(),state.immediate_meshes.budget_evictions());
    }
  }
  const bool native_host=REXCVAR_GET(edf_native_host);
  std::optional<edf::native::NativeConstantOwnership> legacy_scope;
  if(!native_host) legacy_scope.emplace(0);
  native_timing.Finish();
  edf::native::HookTiming guest_timing(edf::native::HookPhase::ImmediateGuest);
  if(native_host) __imp__edf_native_immediate_cpu_tail(ctx,base);
  else __imp__sub_821FD8F8(ctx,base);
}

REX_EXTERN(__imp__sub_8213C328);
REX_EXTERN(__imp__sub_8213ECB0);
REX_EXTERN(__imp__edf_native_main_state_cpu_tail);
REX_HOOK_RAW(sub_8213ECB0) {
  if(!edf::native::NativeConstantOwnership::OwnsMainStatePackets(ctx.r3.u32,uint32_t(ctx.lr))) {
    __imp__sub_8213ECB0(ctx,base); return;
  }
  __imp__edf_native_main_state_cpu_tail(ctx,base);
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native main-state ownership: retained CPU updates/dirty mask; omitted inline Xbox packets");
}
REX_EXTERN(__imp__sub_8213E950);
REX_EXTERN(__imp__edf_native_shader_upload_cpu_tail);
REX_HOOK_RAW(sub_8213E950) {
  if(!edf::native::NativeConstantOwnership::OwnsShaderUpload(ctx.r3.u32,uint32_t(ctx.lr))) {
    __imp__sub_8213E950(ctx,base); return;
  }
  __imp__edf_native_shader_upload_cpu_tail(ctx,base);
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native shader-upload ownership: retained CPU metadata; omitted Xbox allocation/copy/packets");
}
REX_EXTERN(__imp__sub_8213E800);
REX_EXTERN(__imp__edf_native_shader_output_cpu_tail);
REX_HOOK_RAW(sub_8213E800) {
  if(!edf::native::NativeConstantOwnership::OwnsShaderOutputPatch(ctx.r29.u32,uint32_t(ctx.lr))) {
    __imp__sub_8213E800(ctx,base); return;
  }
  // E950 keeps its device in r29 at this exact call. Preserve E800's CPU output
  // through r6, but do not rewrite Xbox instructions via E678/E748.
  __imp__edf_native_shader_output_cpu_tail(ctx,base);
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native shader-output ownership: retained CPU output; omitted Xbox instruction patches");
}
REX_EXTERN(__imp__sub_8213E070);
REX_HOOK_RAW(sub_8213E070) {
  if(!edf::native::NativeConstantOwnership::OwnsShaderMicrocodePatch(ctx.r6.u32,uint32_t(ctx.lr))) {
    __imp__sub_8213E070(ctx,base); return;
  }
  // Recovered native HLSL/input layouts replace the Xbox instruction patcher.
  // Keep EB68/E950's surrounding shader cache/stride metadata updates intact.
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native shader-patch ownership: omitted Xbox microcode rewrite");
}
REX_EXTERN(__imp__sub_8213D750);
REX_EXTERN(__imp__edf_native_derived_cpu_tail);
REX_HOOK_RAW(sub_8213D750) {
  if(!edf::native::NativeConstantOwnership::OwnsDerivedStatePackets(ctx.r3.u32,uint32_t(ctx.lr))) {
    __imp__sub_8213D750(ctx,base); return;
  }
  // Keep the exact CPU state calculation, flag transitions and dirty-mask return.
  // Only the command-buffer rollover and packet write are removed by extraction.
  __imp__edf_native_derived_cpu_tail(ctx,base);
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native derived-state ownership: preserved CPU updates; omitted Xbox packet block");
}
REX_EXTERN(__imp__sub_8213EAB0);
REX_HOOK_RAW(sub_8213EAB0) {
  if(!edf::native::NativeConstantOwnership::OwnsShaderLoadPackets(ctx.r3.u32,uint32_t(ctx.lr))) {
    __imp__sub_8213EAB0(ctx,base); return;
  }
  // Native-host tails retain ECB0's CPU updates regardless of draw success, but
  // don't copy the guest program relocation table into Xenos load packets.
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native shader-load ownership: omitted Xbox program-load packets");
}

REX_EXTERN(__imp__sub_8213D938);
REX_HOOK_RAW(sub_8213D938) {
  if(!edf::native::NativeConstantOwnership::OwnsSpecialRenderPacket(
      ctx.r3.u32,uint32_t(ctx.lr),ctx.r4.u64,ctx.r6.u32)) {
    __imp__sub_8213D938(ctx,base); return;
  }
  // Both simple and tiled branches write only packets/cursor40. Preserve the
  // exact dirty-mask return used by the CPU prefix's subsequent bank dispatch.
  ctx.r3.u64=ctx.r4.u64 & ~uint64_t(0x100);
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native special-render ownership: omitted Xbox packets; preserved dirty-mask return");
}

REX_HOOK_RAW(sub_8213C328) {
  if(!edf::native::NativeConstantOwnership::OwnsImmediateAllocation(
      ctx.r3.u32,uint32_t(ctx.lr),ctx.r4.u32,ctx.r5.u32)) {
    __imp__sub_8213C328(ctx,base);
    return;
  }
  // FD428 has already applied derived CPU state, cleared dirty banks and
  // restored its temporary stride byte. Returning no guest storage takes its
  // existing no-buffer exit, preserving cursor40 while bypassing GPU packets.
  // FD8F8 then skips its redundant vertex memcpy and cursor13076 commit.
  // The native draw already succeeded. Do NOT run the allocator's failure path
  // or set device+10809's allocation-failure bit; no allocation was attempted.
  // GPU-only restore bit4096 and scratch fields13076/13080/13088 remain untouched.
  ctx.r3.u64=0;
  static thread_local uint64_t removed=0;
  if(++removed<=3)
    REXLOG_INFO("Native immediate ownership: omitted Xbox vertex allocation/copy and draw packets");
}

REX_EXTERN(__imp__sub_8213DF00);
REX_HOOK_RAW(sub_8213DF00) {
  if(!edf::native::NativeConstantOwnership::Owns(ctx.r3.u32,ctx.r5.u32,ctx.r6.u32)) {
    __imp__sub_8213DF00(ctx,base);
    return;
  }
  // The original groups dirty float4 registers into GPU packet payloads and
  // advances cursor40. It never updates the source constants. Native-host
  // mode has no GPU emulator consuming this packet allocation/copy.
  // The caller still owns its CPU dirty-mask clear and all other derived state.
  static thread_local uint64_t removed=0;
  if(++removed<=3)
    REXLOG_INFO("Native constant ownership: omitted Xbox float-constant packet encoding, bank={:#x}",ctx.r5.u32);
}

REX_EXTERN(__imp__sub_8213DB60);
REX_HOOK_RAW(sub_8213DB60) {
  if(!edf::native::NativeConstantOwnership::OwnsRenderWords(ctx.r3.u32,ctx.r5.u32,ctx.r6.u32,ctx.r4.u64)) {
    __imp__sub_8213DB60(ctx,base);
    return;
  }
  // Pure packet copy in the audited native CPU-tail render-state path.
  // Preserve all CPU source words; the original caller clears its dirty mask.
  // Unknown banks/extents and all nonnative draws continue through the encoder.
  static thread_local uint64_t removed=0;
  if(++removed<=3)
    REXLOG_INFO("Native render ownership: omitted Xbox state packet encoding, dirty={:#x}",ctx.r4.u64);
}

REX_EXTERN(__imp__sub_8213DDA0);
REX_EXTERN(__imp__sub_8213DC20);
REX_HOOK_RAW(sub_8213DC20) {
  if(!edf::native::NativeConstantOwnership::OwnsVectorStatePackets(ctx.r3.u32,ctx.r4.u64)) {
    __imp__sub_8213DC20(ctx,base); return;
  }
  // Preserve CPU plane/control records and the caller's dirty clear. This
  // removes only the redundant GPU packet copy, not native clipping behavior.
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native vector-state ownership: omitted Xbox packet encoding");
}

REX_HOOK_RAW(sub_8213DDA0) {
  if(!edf::native::NativeConstantOwnership::OwnsFetchWords(ctx.r3.u32,ctx.r4.u64)) {
    __imp__sub_8213DDA0(ctx,base);
    return;
  }
  // DDA0 reads device+1024 descriptors and writes only command packets/cursor40.
  // Its caller retains descriptor updates and the device+16 dirty-mask clear.
  // Native-host mode consumes neither the packet payloads nor GPU cache packets,
  // including when native submission failed and was recorded as unsubmitted.
  static thread_local uint64_t removed=0;
  if(++removed<=3)
    REXLOG_INFO("Native fetch ownership: omitted Xbox descriptor packet encoding, dirty={:#x}",ctx.r4.u64);
}

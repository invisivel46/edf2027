#pragma once
// What the bridge and the hook files (edf/hooks) share: the types, templates, constants and small helpers more
// than one of them uses, moved from guest_shader_bridge.cpp unchanged (out of their anonymous namespaces; the
// helpers the bridge inlined keep being inlined: `inline`), and declarations of the functions and variables
// that stay defined where they were (guest_shader_bridge.cpp, or the hook file that uses them most).
#include "bridge_helpers.h"
#include "../guest_shader_bridge.h"
#include "../native_renderer_preset.h"
#include "../../pause_menu.h"
#include "../../input_latency.h"
#include "../native_backend_frame_queue.h"
#include "../d3d11_backend.h"
#include "../d3d12_backend.h"
#include "../native_constant_cache.h"
#include "../native_scene_sources.h"
#include "../native_scene_adapter.h"
#include "../native_scene_cpu_window.h"
#include "../native_recorded_reads.h"
#include "../native_scene_geometry_install.h"
#include "../native_scene_execution.h"
#include "../native_scene_world_restore.h"
#include "../native_static_group_eligibility.h"
#include "../native_static_world_resolve.h"
#include "../native_static_world_pass.h"
#include "../native_static_world_cache.h"
#include "../guest_draw_state.h"
#include "../guest_sdk_readable_range.h"
#include "../native_model_buffers.h"
#include "../native_full_frame_static_world.h"
#include "../native_full_frame_models.h"
#include "../native_render_state_snapshot.h"
#include "../native_font_bindings.h"
#include "../native_physical_write_notify.h"
#include "../native_load_trace.h"
#include "../native_post_finish_plan.h"
#include "../native_fsr.h"
#include "../d3d11_texture.h"
#include "../d3d11_completion.h"
#include "../native_transient_batching.h"
#include "native_cvars.h"
#include "bridge_state.h"
#include "../edf/full_frame/host.h"
#include <rex/hook.h>
#include <rex/cvar.h>
#include <rex/ppc/func.h>
#include <rex/logging.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Launcher cvars (launcher_cvars.cpp) and the SDK's window size, for the
// render size and the 2D canvas layout (native_display_layout.h).
REXCVAR_DECLARE(std::string, edf_aspect);
// Transient batching (edf_native_transient_batching), off whenever reuse is
// (native_reuse.h): a batched run is recorded as its separate draws.
static bool NativeTransientBatchingEnabled() {
  return REXCVAR_GET(edf_native_transient_batching) && edf::native::NativeReuseAllowed();
}
REXCVAR_DECLARE(bool, edf_native_host);
// edf_native_load_trace (native_load_trace.h): phase events and 250 ms ticks.
// Everything here runs only with the cvar on; off, each hook pays one cvar read.
struct NativeLoadTraceState {
  using Clock=std::chrono::steady_clock;
  std::mutex mutex;
  std::atomic<bool> loading{false};   // BeginLoading request .. presenter exit
  std::atomic<int64_t> next_tick{0};  // steady_clock ticks
  std::atomic<HANDLE> engine{nullptr};
  Clock::time_point origin{},last_event{},last_tick{},window_start{};
  edf::native::LoadTraceSample at_event{},at_tick{},at_window{};
  uint64_t engine_cpu_event=0,engine_cpu_tick=0,engine_cpu_window=0;
  std::atomic<uint64_t> presenter_frames{0};
  uint64_t presenter_frames_event=0,presenter_frames_tick=0,presenter_frames_window=0;
  bool started=false;
  // Mission script natives (dispatcher 820D1518, by native number): which
  // script calls the loading window's time went to. Reported at window close.
  static constexpr size_t kNatives=2048;
  std::array<std::atomic<uint64_t>,kNatives> native_calls{},native_nanos{};
  std::array<uint64_t,kNatives> native_calls_window{},native_nanos_window{};
};
inline NativeLoadTraceState& LoadTraceState() { static auto* state=new NativeLoadTraceState; return *state; }
void LoadTraceEvent(const char* event,uint32_t detail,bool open=false,bool close=false);
void LoadTraceTick();
extern thread_local constinit bool native_load_trace_presenter_thread;
namespace edf::native {
inline NativeSceneTreePublications& TreePublications() {
  static NativeSceneTreePublications publications;
  return publications;
}
std::array<int32_t,2> NativeDisplaySize();
// Resolved once, at renderer initialization (native_display_layout.h): the
// engine allocates its targets, post pyramid, 2D canvas and camera viewports
// from this size and none of them follows a later change, so saving a new F1
// choice or resizing the window must not change live canvas scaling while
// those still have the old size. {0,0} is the engine's own 1280x720: no
// override and no canvas mapping, byte for byte the unmodified path.
inline const std::array<int32_t,2>& NativeRenderDimensions() {
  static const std::array<int32_t,2> dimensions=[] {
    const auto display=NativeDisplaySize();
    // FSR upscaling at startup: the engine renders (and FSR outputs) at the
    // window's size, the scene at a fraction of it (NativeRenderRequest). The
    // validation runs that keep FSR off keep the request.
    const auto fsr=ParseNativeFsrMode(std::string(REXCVAR_GET(edf_native_fsr)));
    const bool upscaling=fsr && NativeFsrUpscales(*fsr) &&
      !NativeFsrExcludedBy({REXCVAR_GET(edf_native_ab_alternate)>0,REXCVAR_GET(edf_native_reuse_off_alternate)>0,
                            REXCVAR_GET(edf_native_shadow_render)>0});
    const auto request=NativeRenderRequest(REXCVAR_GET(edf_native_render_width),REXCVAR_GET(edf_native_render_height),upscaling);
    const auto size=ResolveNativeRenderSize(request[0],request[1],display[0],display[1],ParseNativeAspectMode(REXCVAR_GET(edf_aspect)));
    if(upscaling) {
      const auto scene=NativeFsrRenderSizeFor(*fsr,uint32_t(size.width),uint32_t(size.height));
      REXLOG_INFO("Native render size: FSR {} draws the scene at {}x{} and upscales it to the output size below; "
        "edf_native_render_width/height ({}x{}) are not used while it upscales",NativeFsrModeName(*fsr),scene.width,scene.height,
        REXCVAR_GET(edf_native_render_width),REXCVAR_GET(edf_native_render_height));
    }
    REXLOG_INFO("Native render size: {}x{}{} (request {}x{}, display {}x{}, aspect {}){}",size.width,size.height,
      size.original?" (engine original)":"",request[0],request[1],
      display[0],display[1],std::string(REXCVAR_GET(edf_aspect)),size.clamped?"; scaled to fit the render size limits":"");
    return size.original?std::array<int32_t,2>{0,0}:std::array<int32_t,2>{size.width,size.height};
  }();
  return dimensions;
}
// Coarse wait totals are sampled once per swap. They include all participating
// threads, so they locate waits but must not be summed as a CPU-time partition.
enum class FrameWaitKind { Engine,GuestFence,SharedSlot };
inline std::array<std::atomic<uint64_t>,3>& FrameWaitTotals() {
  static std::array<std::atomic<uint64_t>,3> totals{};
  return totals;
}
class NativeFrameWaitTrace {
 public:
  explicit NativeFrameWaitTrace(FrameWaitKind kind,bool active=true)
      : kind_(kind),enabled_(active && !REXCVAR_GET(edf_native_frame_trace).empty()) {
    if(enabled_) start_=Clock::now();
  }
  ~NativeFrameWaitTrace() { Finish(); }
  void Finish() {
    if(!enabled_) return;
    enabled_=false;
    const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start_).count();
    FrameWaitTotals()[size_t(kind_)].fetch_add(uint64_t(ns),std::memory_order_relaxed);
  }
 private:
  using Clock=std::chrono::steady_clock;
  FrameWaitKind kind_;
  bool enabled_;
  Clock::time_point start_{};
};
namespace {
NativeViewportState DecodeDrawViewport(const GuestViewportWords& snapshot) {
  const auto& words=snapshot.words;
  return MakeNativeDrawViewport(words[0],words[1],words[2],words[3],
    std::bit_cast<float>(words[4]),std::bit_cast<float>(words[5]),snapshot.scissor_enabled,
    {std::bit_cast<int32_t>(words[6]),std::bit_cast<int32_t>(words[7]),
     std::bit_cast<int32_t>(words[8]),std::bit_cast<int32_t>(words[9])});
}
}
std::array<float,4> ResolveBlendFactorForDraw(uint32_t device,
    const NativeRenderStateSnapshots::BlendWords* live);
namespace {
template <typename Reader>
std::array<float,4> GuestBlendFactorForDraw(const Reader& reader,uint32_t device) {
  if(!REXCVAR_GET(edf_native_owned_render_state) && !REXCVAR_GET(edf_native_render_state_audit))
    return ReadBlendFactor(reader,device);
  NativeRenderStateSnapshots::BlendWords live{};
  const bool audit=REXCVAR_GET(edf_native_render_state_audit);
  if(audit) live=ReadGuestWords<4>(reader,reader.Add(device,10336));
  return ResolveBlendFactorForDraw(device,audit?&live:nullptr);
}
}
void AuditGeneratedWrites(Bridge& state,const NativeBufferWrites::Batch& batch);
void ResolveScene(const GuestReader& reader,Bridge& state,uint32_t owner);
struct NativeScenePassCursorState {
  uint32_t device=0;
  NativeSceneMaterialPassState material;
  std::optional<GuestViewportWords> viewport;
  ActiveTargets targets;
};
using NativeMaterialPassCursor=std::optional<NativeScenePassCursorState>;
extern thread_local constinit NativeMaterialPassCursor* native_material_pass_cursor;
extern thread_local constinit NativeSceneQueues* native_scene_queues;
NativeFsrMode NativeFsrRequestedMode();
namespace {
NativeFsrExclusions NativeFsrCurrentExclusions() {
  return {REXCVAR_GET(edf_native_ab_alternate)>0,REXCVAR_GET(edf_native_reuse_off_alternate)>0,
          REXCVAR_GET(edf_native_shadow_render)>0};
}
}
const NativeSceneSources& NativeSceneSourcesForPass(const Bridge& state);
// Everything a recorded draw needs before its geometry.
//
// The D3D11 paths spell this out as four separate bindings - target, render
// state, viewport, shader pair - because D3D11 keeps those apart. One pipeline
// carries most of it here, and the rest is recorder state, so every draw path
// that used those four calls uses this one instead. Written once because five
// paths need it and five copies would drift.
struct RecordedDraw {
  ShaderBindings& vertex;
  ShaderBindings& pixel;
  const NativeViewportState& viewport;
  const RenderStateWords& state;
  std::span<const edf::native::NativeBackendInputElement> layout;
  uint64_t layout_id=0;
  // The guest handles, plus whatever else makes two compiled shaders under one
  // handle different - the reversed-depth variant being the one that does.
  uint64_t vertex_id=0,pixel_id=0;
  edf::native::NativeBackendTopology topology=edf::native::NativeBackendTopology::TriangleList;
  bool world_instancing=false;
};
namespace {
template <typename Reader>
edf::native::NativeBackendRecorder& RecordDrawSetup(Bridge& state,const Reader& reader,
                                                    uint32_t device,const RecordedDraw& draw) {
  auto& backend=EnsureSceneBackendLocked(state);
  auto& recorder=SceneRecorderLocked(state);
  recorder.SetWorldInstancing(draw.world_instancing,REXCVAR_GET(edf_native_world_constant_reuse));
  recorder.SetTransientBatching(NativeTransientBatchingEnabled());
  const auto targets=ActiveTargetsLocked(state);
  if(!targets.count) throw std::runtime_error("a recorded draw has no colour target to record into");
  ++state.recorded_draws;
  auto& last=state.recorded;
  // Nothing the recorder already holds is re-sent. The comparison is against
  // what this code last sent, not against device state, because neither target
  // API has device state to ask - which is also why a direct bind on the shared
  // context has to invalidate it explicitly.
  const bool same_frame=last.valid && last.frame==state.scene_frames &&
                        last.bind_generation==state.bind_generation;
  bool same_targets=same_frame && last.color_count==targets.count && last.depth==targets.depth;
  for(uint32_t index=0;same_targets && index<targets.count;++index)
    same_targets=last.colors[index]==targets.colors[index];
  if(!same_targets) {
    recorder.SetRenderTargets({targets.colors.data(),targets.count},targets.depth);
    last.color_count=targets.count; last.colors=targets.colors; last.depth=targets.depth;
  }
  // FSR upscaling (native_fsr.h): a draw into the scene while an upscaling
  // view records lands in the render-size corner of the targets.
  auto view=draw.viewport.viewport;
  auto scissor=draw.viewport.scissor;
  if(state.fsr.view_scale.active() && state.active_scene && !state.active_target) {
    const auto scaled=ScaleNativeViewport({view.TopLeftX,view.TopLeftY,view.Width,view.Height,view.MinDepth,view.MaxDepth},
      state.fsr.view_scale);
    view.TopLeftX=scaled.x; view.TopLeftY=scaled.y; view.Width=scaled.width; view.Height=scaled.height;
    const auto rect=ScaleNativeScissor({int32_t(scissor.left),int32_t(scissor.top),int32_t(scissor.right),int32_t(scissor.bottom)},
      state.fsr.view_scale);
    scissor.left=rect.left; scissor.top=rect.top; scissor.right=rect.right; scissor.bottom=rect.bottom;
  }
  const bool scissor_enabled=draw.state[5]!=0;
  if(!same_frame || std::memcmp(&last.viewport,&view,sizeof(view))!=0) {
    recorder.SetViewport({view.TopLeftX,view.TopLeftY,view.Width,view.Height,
                          view.MinDepth,view.MaxDepth});
    last.viewport=view;
  }
  if(!same_frame || last.scissor_enabled!=scissor_enabled ||
     std::memcmp(&last.scissor,&scissor,sizeof(scissor))!=0) {
    recorder.SetScissor({scissor.left,scissor.top,scissor.right,scissor.bottom},scissor_enabled);
    last.scissor=scissor; last.scissor_enabled=scissor_enabled;
  }
  // Pixel-stage resources only, which is what every shader in this game and
  // this renderer uses. A vertex shader that sampled something would have it
  // silently unbound, so it is refused instead: no backend root signature
  // declares vertex-stage textures, and this is where that would be noticed.
  if(draw.vertex.BindsResources())
    throw std::runtime_error("a vertex shader with textures or samplers cannot be recorded: "
                             "no backend root signature declares them");
  const bool same_pipeline=same_frame && last.pipeline &&
    last.vertex_id==draw.vertex_id && last.pixel_id==draw.pixel_id &&
    last.layout_id==draw.layout_id && last.state==draw.state && last.topology==draw.topology &&
    last.render_targets==targets.count && last.sample_count==targets.samples &&
    last.dsv_format==targets.dsv_format && last.rtv_format==targets.rtv_format;
  if(same_pipeline) ++state.recorded_pipeline_skips;
  else {
    edf::native::NativeBackendPipelineDesc desc{};
    auto* vertex_code=draw.vertex.shader().bytecode.Get();
    auto* pixel_code=draw.pixel.shader().bytecode.Get();
    desc.vertex={static_cast<const uint8_t*>(vertex_code->GetBufferPointer()),vertex_code->GetBufferSize()};
    desc.pixel={static_cast<const uint8_t*>(pixel_code->GetBufferPointer()),pixel_code->GetBufferSize()};
    desc.vertex_id=draw.vertex_id;
    desc.pixel_id=draw.pixel_id;
    desc.input_layout=draw.layout;
    desc.input_layout_id=draw.layout_id;
    desc.state=draw.state;
    desc.topology=draw.topology;
    desc.render_targets=targets.count;
    desc.rtv_format=targets.rtv_format;
    desc.dsv_format=targets.dsv_format;
    desc.sample_count=targets.samples;
    auto& pipeline=backend.CreatePipeline(desc);
    // A property of the two shaders and the layout, which is what the cached
    // pipeline is keyed on, so it is decided once per pipeline: reflecting
    // on every pipeline switch would cost more than the switch.
    if(!pipeline.transient_batchable_known) {
      pipeline.transient_batchable=NativePipelineTransientBatchable(draw.layout,
        draw.vertex.shader().reflection.Get(),draw.pixel.shader().reflection.Get());
      pipeline.transient_batchable_known=true;
    }
    if(auto* code=draw.vertex.shader().instanced_bytecode.Get(); code && !pipeline.world_instanced &&
       draw.layout.size()<=28 && std::none_of(draw.layout.begin(),draw.layout.end(),
         [](const auto& element) { return element.slot==15 || element.per_instance; })) {
      edf::native::NativeOwnedInputLayout instanced_layout;
      for(const auto& element:draw.layout) instanced_layout.Add(element.semantic,element.semantic_index,
        element.format,element.slot,element.offset,element.per_instance,element.step_rate);
      for(uint32_t row=0;row<4;++row)
        instanced_layout.Add("EDFINSTANCE",row,DXGI_FORMAT_R32G32B32A32_FLOAT,15,row*16,true,1);
      desc.vertex={static_cast<const uint8_t*>(code->GetBufferPointer()),code->GetBufferSize()};
      desc.vertex_id|=uint64_t(1)<<63;
      desc.input_layout=instanced_layout.elements();
      desc.input_layout_id=instanced_layout.fingerprint();
      pipeline.world_instanced=&backend.CreatePipeline(desc);
      pipeline.instance_world_slot=draw.vertex.shader().instance_world_slot;
      pipeline.instance_world_offset=draw.vertex.shader().instance_world_offset;
    }
    recorder.SetPipeline(pipeline);
    last.pipeline=&pipeline;
    last.vertex_id=draw.vertex_id; last.pixel_id=draw.pixel_id; last.layout_id=draw.layout_id;
    last.state=draw.state; last.topology=draw.topology;
    last.render_targets=targets.count; last.sample_count=targets.samples;
    last.dsv_format=targets.dsv_format; last.rtv_format=targets.rtv_format;
    // A blend factor belongs to the pipeline that was bound with it; a new
    // pipeline has not been given one.
    last.blend_factor_needed=false;
  }
  if(last.pipeline->requires_blend_factor()) {
    const auto factor=GuestBlendFactorForDraw(reader,device);
    if(!last.blend_factor_needed || factor!=last.blend_factor) {
      recorder.SetBlendFactor(factor);
      last.blend_factor=factor; last.blend_factor_needed=true;
    }
  }
  // Constants are re-sent when they have changed. An activation patches the
  // vertex constants between draws, which is the whole reason a run of
  // otherwise identical draws exists - but the pixel constants usually do not
  // move, and every re-send stages a copy in the backend's upload ring. The
  // bindings count their own changes, so this is an integer comparison per
  // register rather than the memcmp and the private copy it used to be.
  const auto send=[&](edf::native::NativeBackendStage stage,
                      edf::native::NativeConstantCache& sent,
                      const edf::native::ShaderBindings& bindings) {
    if(same_frame && sent.MatchesComplete(&bindings,bindings.constant_generation())) {
      state.recorded_constant_skips+=bindings.ConstantImages().size();
      return;
    }
    for(const auto& image:bindings.ConstantImages()) {
      // Reflection order is not a binding slot. Different shaders can put
      // identical bytes in different registers; each register must be bound.
      if(same_frame && sent.Matches(image.slot,&bindings,*image.version)) {
        ++state.recorded_constant_skips;
        continue;
      }
      recorder.SetConstants(stage,image.slot,image.bytes);
      sent.Store(image.slot,&bindings,*image.version);
    }
    sent.StoreComplete(&bindings,bindings.constant_generation());
  };
  send(edf::native::NativeBackendStage::Vertex,last.vertex_constants,draw.vertex);
  send(edf::native::NativeBackendStage::Pixel,last.pixel_constants,draw.pixel);
  if(&draw.pixel!=last.pixel || draw.pixel.resource_generation()!=last.pixel_resources || !same_frame) {
    for(const auto& image:draw.pixel.TextureImages())
      recorder.SetTexture(edf::native::NativeBackendStage::Pixel,image.slot,image.texture);
    for(const auto& image:draw.pixel.SamplerImages())
      recorder.SetSampler(edf::native::NativeBackendStage::Pixel,image.slot,image.sampler);
    last.pixel=&draw.pixel;
    last.pixel_resources=draw.pixel.resource_generation();
  } else ++state.recorded_material_skips;
  last.valid=true;
  last.frame=state.scene_frames;
  last.bind_generation=state.bind_generation;
  return recorder;
}
}
NativeBackendSampler* SamplerLocked(Bridge& state,const SamplerStateWords& key);
// make_reader supplies what the words are read through, inside the lock and
// the try, as the plain overload always built its GuestReader: a caller that
// already holds a validated window over the device (the material activation,
// once per state override) reads them through it instead.
template<class MakeReader>
void PublishNativeRenderStateWith(uint32_t device,uint32_t producer,MakeReader&& make_reader) {
  if((!REXCVAR_GET(edf_native_render_state_audit) && !REXCVAR_GET(edf_native_owned_render_state)) ||
      !EDF_NATIVE_FLAG(shader_bridge)) return;
  auto& state=State();
  std::lock_guard lock(state.mutex);
  try {
    const auto fields=NativeRenderStateProducerFields(producer);
    decltype(auto) reader=make_reader();
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
void PublishNativeRenderState(uint8_t* base,uint32_t device,uint32_t producer);
void AuditNativeRenderState(uint32_t device,const NativeRenderStateSnapshots::Words& live);
template <typename Reader>
NativeRenderStateSnapshots::Words ReadAuditedRenderStateWords(const Reader& reader,uint32_t device) {
  return ResolveNativeRenderState(State().render_state_snapshots,device,
    REXCVAR_GET(edf_native_owned_render_state),REXCVAR_GET(edf_native_render_state_audit),
    [&]{return ReadRenderStateWords(reader,device);},
    [&](const auto& live){AuditNativeRenderState(device,live);});
}
template <typename Reader>
GuestViewportWords ReadNativeDrawViewportWords(const Reader& reader,uint32_t device) {
  if(!REXCVAR_GET(edf_native_owned_render_state)) return ReadViewportWords(reader,device);
  const auto render=ReadAuditedRenderStateWords(reader,device);
  return ReadViewportWords(reader,device,render[5]!=0);
}
template <typename Reader>
NativeViewportState ReadNativeDrawViewport(const Reader& reader,uint32_t device) {
  return DecodeDrawViewport(ReadNativeDrawViewportWords(reader,device));
}
void PublishSceneSharedLocked(Bridge& state,const NativeRenderTarget& output,NativeFrameKind kind);
void PublishNativeCompletion(const GuestReader& reader,Bridge& state,uint32_t device,bool allow_flush=false);
}
extern thread_local constinit NativeLoopBudget native_loop_budget;
struct NativeModelMotionState {
  struct Source {
    uint32_t vector=0,owner=0,node=0;
    uint64_t publication=0;
    edf::native::NativeModelPoseHistory history;
    size_t history_bytes=0;
    bool trace=false;
    uint64_t samples=0,source_changes=0,rendered_changes=0,blended=0,trace_publication=0;
    uint64_t source_hash=0,rendered_hash=0;
  };
  std::mutex mutex;
  NativeLoopBudget published;
  uint64_t publication=0;
  uint32_t trace_sources=0;
  std::unordered_map<uint32_t,Source> sources;
  size_t history_bytes=0;
  void Clear() { sources.clear(); history_bytes=0; }
  void Erase(uint32_t address) {
    const auto found=sources.find(address);
    if(found==sources.end()) return;
    history_bytes-=found->second.history_bytes;
    sources.erase(found);
  }
};
inline NativeModelMotionState& ModelMotionState() { static NativeModelMotionState value; return value; }
edf::native::NativeModelPublications& ModelPublications();
extern thread_local constinit std::vector<uint32_t>* native_model_dirty_poses;
extern thread_local std::vector<uint32_t> native_step_trees;
namespace edf::native {
// Guest-thread only: the post chain, its setters and its quad draws all run
// synchronously inside the hooked call.
struct PostFinishRecorder {
  uint32_t self=0;
  std::set<uint32_t> targets,techniques;
  PostFinishObservation seen;
  uint64_t errors=0;
  std::string first_error;
  bool Effect(uint32_t effect) const { return effect>=self && effect-self<PostFinishLayout::kEffectSpan; }
  void Fail(const std::exception& error) { if(!errors++) first_error=error.what(); }
};
extern thread_local constinit PostFinishRecorder* post_finish_recorder;
bool NativeStaticWorldPassEnabled();
void RenderNativeStaticWorldPass(PPCContext& ctx,uint8_t* base,uint32_t owner);
bool NativeModelPassEnabled();
bool RenderNativeModelPass(PPCContext& ctx,uint8_t* base,const std::vector<NativePoseMatrix>* interpolated,
  bool interpolating,bool render_dependent);
namespace {
// Retains one descriptor's observed VB/IB snapshots as native indexed geometry
// and commits them as the buffers' observed storage; throws if either buffer
// changed generation or version meanwhile. Shared by the static preload and
// the model pass, which draw the same 148-byte descriptor shape.
std::shared_ptr<const NativeIndexedMesh::RetainedDraw> RetainNativeSceneGeometryLocked(Bridge& state,
    const NativeSceneGeometrySource& input,const auto& vb,const auto& ib,const auto& declaration,const auto& shader,
    const std::array<NativeBufferWrites::ObservedSnapshot,2>& observed) {
  auto& backend=EnsureSceneBackendLocked(state);
  auto& mesh=state.meshes.Acquire(backend,shader,
    {input.vertex,input.index,input.declaration,input.shader,0},declaration->bytes(),input.stride,
    *observed[0].contents,*observed[1].contents,ib.stride,declaration,{},
    ib.index_storage,vb.vertex_storage,{},observed[0].contents,0,observed[1].contents);
  auto geometry=state.scene_adapter.RetainGeometry(state.scene_backend,mesh,0,input.count);
  if(!state.model_buffers.CommitObservedGeometry(
      {input.vertex,vb.generation,observed[0].version},
      {input.index,ib.generation,observed[1].version},
      mesh.VertexStorage(),observed[0].contents,mesh.IndexStorage(),observed[1].contents))
    throw std::runtime_error("native scene geometry changed before publication");
  return geometry;
}
}
bool NativeSceneMaterialHostCurrent(Bridge& state,const NativeSceneMaterialProgram& program,uint32_t material,
    const std::shared_ptr<const NativeMaterialParameters::Groups>& schema);
// One material (a 112-byte pass record: +96/+104 state operations, +108 the
// shader pair) as an owned program plus its constant layout and values.
// Program inputs go through `recorder` (the change signal); constant values
// through `reader`, unrecorded, for the per-use refresh. The previous program
// is kept when every program input and host identity is unchanged.
struct NativeSceneMaterialBuild {
  std::shared_ptr<const NativeMaterialParameters::Groups> schema;
  NativeSceneMaterialConstantLayout layout;
  std::shared_ptr<const NativeSceneMaterialProgram> program;
  std::vector<NativeSceneMaterialInputs::Constant> constants;
  bool reused=false;
};
namespace {
template<class Recorder,class Reader>
NativeSceneMaterialBuild BuildNativeSceneMaterialLocked(Bridge& state,const Recorder& recorder,const Reader& reader,
    uint32_t material,const NativeSceneMaterialProgram* previous) {
  const auto pass=recorder.Word(recorder.Add(material,108));
  const auto vertex=recorder.Word(recorder.Word(pass));
  const auto pixel=recorder.Word(recorder.Add(recorder.Word(recorder.Add(pass,4)),4));
  const auto& vs=state.shaders.at(vertex);
  const auto& ps=state.shaders.at(pixel);
  if(!vs.reversed_bindings) throw std::runtime_error("native material has no vertex variants");
  NativeSceneMaterialBuild result;
  result.schema=state.material_parameters.Get(material);
  const auto& schema=*result.schema;
  result.layout=ResolveNativeSceneMaterialConstants(schema,[&](bool pixel_stage,const std::string& name) {
    return pixel_stage?ps.bindings->GuestFloatRegisterBytes(name):std::max(
      vs.bindings->GuestFloatRegisterBytes(name),vs.reversed_bindings->GuestFloatRegisterBytes(name));
  });
  auto definition=ReadNativeSceneMaterialDefinition(recorder,material,schema,
    [&](const std::string& name) { return ps.bindings->ResolveResource(name).used(); });
  auto sampler_operations=ReadNativeMaterialSamplerOperations(recorder,schema);
  result.constants=ReadNativeSceneMaterialConstants(reader,schema,result.layout);
  std::vector<std::shared_ptr<NativeBackendTexture>> textures;
  for(const auto& input:definition.textures) {
    if(!input.handle) { textures.emplace_back(); continue; }
    const auto texture=state.textures.find(input.handle);
    if(texture==state.textures.end() || !texture->second.content_valid || !texture->second.backend)
      throw std::runtime_error("native material texture is not ready");
    textures.push_back(texture->second.backend);
  }
  if(previous && previous->inputs==definition && previous->textures==textures &&
     previous->sampler_operations==sampler_operations && previous->backend==state.scene_backend &&
     previous->vertex.bytecode==vs.bindings->shader().bytecode &&
     previous->reversed_vertex.bytecode==vs.reversed_bindings->shader().bytecode &&
     previous->pixel.bytecode==ps.bindings->shader().bytecode) {
    result.reused=true;
    return result;
  }
  auto program=std::make_shared<NativeSceneMaterialProgram>();
  program->backend=state.scene_backend; program->inputs=std::move(definition); program->textures=std::move(textures);
  program->sampler_operations=std::move(sampler_operations);
  program->vertex=vs.bindings->shader(); program->reversed_vertex=vs.reversed_bindings->shader();
  program->pixel=ps.bindings->shader();
  result.program=std::move(program);
  return result;
}
}
}
edf::native::NativeBufferWrites* NativeBufferWriteQueue(uint8_t* base,uint32_t destination,uint32_t bytes,
    std::optional<edf::native::NativeBufferWrites::Range>* range=nullptr);
namespace {
edf::native::NativeBufferWrites::WriterScope BeginNativeBufferWrite(uint8_t* base,uint32_t destination,uint32_t bytes,bool exact=false,
    edf::native::NativeBufferWrites::WriterKind kind=edf::native::NativeBufferWrites::WriterKind::Bulk) {
  std::optional<edf::native::NativeBufferWrites::Range> range;
  auto* queue=NativeBufferWriteQueue(base,destination,bytes,exact?&range:nullptr);
  return edf::native::NativeBufferWrites::WriterScope(queue,range,kind);
}
}
void NotifyCompletedNativeBufferWrite(uint8_t* base,uint32_t destination,uint32_t bytes,bool notify_versions=false,bool generated=false,
    edf::native::NativeBufferWrites::WriterSite site={nullptr,0});

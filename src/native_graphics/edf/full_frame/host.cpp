// The full frame's host, NativeFullFrameHost, with what only it uses: the GPU pass timing markers,
// FSR's motion input and the shadow frame (NativeShadowFrame); and RunNativeFullFrame, the 821A5080 hook's
// full-frame route from the host on. Moved from guest_shader_bridge.cpp unchanged (see host.h).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "host.h"
#include "../../native_ab_alternate.h"
#include "../../native_frame_times.h"
#include "../../native_fsr.h"
#include "../../native_full_frame_post.h"
#include "../../native_gpu_pass_timings.h"
#include "../../native_motion_vector_pass.h"
#include "../../native_motion_vectors.h"
#include "../../native_render_registry.h"
#include "../../native_shadow_render.h"
#include "../../native_view_globals.h"
#include "../../native_frame_dispatch.h"
#include "../../native_renderer_preset.h"
#include "../../../frame_stats.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
// NativeFullFrame's view of the bridge for one render helper call. Native
// work reads guest memory and writes only owner+136 and the guest frame
// context; the remaining guest calls (kNativeFrameRemainingGuestCalls) go
// through guest_, with the frame context at context_ as DispatchNativeFrame
// passes it. See native_full_frame.h for where the frame sits between
// 8219C7A8 and 8219C840.
// edf_native_gpu_timings: timestamps at the full frame's pass boundaries on the
// scene recorder (native_gpu_pass_timings.h), so each span brackets exactly
// the GPU work its pass recorded. Each call takes the bridge locks, as every
// recorder use does, and only when the cvar is on; off, it is one cvar read.
// The frame's markers are resolved at its end and read by a later frame's
// begin, which also logs the window every 600 frames read.
edf::native::NativeGpuPassTimings& GpuPassTimings() {
  static edf::native::NativeGpuPassTimings timings;
  return timings;
}
template<class Body> void WithGpuPassTimings(Body&& body) {
  if(!REXCVAR_GET(edf_native_gpu_timings)) return;
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  auto& timings=GpuPassTimings();
  if(!timings.supported() || !state.scene_backend) return;
  try { body(timings,*state.scene_backend,state); }
  catch(const std::exception& error) {
    static std::atomic<bool> reported=false;
    if(!reported.exchange(true)) REXLOG_WARN("Native GPU timing marker failed: {} (logged once)",error.what());
  }
}
void GpuPassFrameBegin() {
  WithGpuPassTimings([](auto& timings,auto& backend,auto& state) {
    timings.BeginFrame(backend,edf::native::SceneRecorderLocked(state));
    if(!timings.supported()) {
      REXLOG_WARN("Native GPU timing unavailable: scene backend {} has no timestamps",std::string(backend.name()));
      return;
    }
    edf::native::NativeGpuPassTimingWindow window;
    if(!timings.TakeReport(window)) return;
    REXLOG_INFO("Native GPU timing window: frames={} skipped={} dropped_spans={} invalid_spans={} ticks_per_s={} (GPU timestamps; frame is the helper's first to last marker, interval one frame's begin to the next)",
      window.frames,window.skipped,window.dropped_spans,window.invalid_spans,timings.frequency());
    for(const auto& pass:window.passes) {
      REXLOG_INFO("Native GPU timing: pass={} frames={} total_ms={:.3f} avg_ms={:.3f} max_ms={:.3f}",
        pass.name,pass.frames,pass.total_ms,pass.average_ms(),pass.max_ms);
      // The performance overlay's GPU number (frame_stats.h).
      if(pass.name=="frame") {
        edf::CurrentFrameStats().gpu_ms.store(float(pass.average_ms()),std::memory_order_relaxed);
        edf::CurrentFrameStats().gpu_windows.fetch_add(1,std::memory_order_relaxed);
      }
    }
  });
}
int GpuPassSpanBegin(std::string_view name) {
  int span=-1;
  WithGpuPassTimings([&](auto& timings,auto&,auto& state) {
    if(timings.frame_open()) span=timings.BeginSpan(name,edf::native::SceneRecorderLocked(state));
  });
  return span;
}
void GpuPassSpanEnd(int span) {
  if(span<0) return;
  WithGpuPassTimings([&](auto& timings,auto&,auto& state) {
    if(timings.frame_open()) timings.EndSpan(span,edf::native::SceneRecorderLocked(state));
  });
}
void GpuPassFrameEnd() {
  WithGpuPassTimings([](auto& timings,auto&,auto& state) {
    if(timings.frame_open()) timings.EndFrame(edf::native::SceneRecorderLocked(state));
  });
}
struct GpuPassSpan {
  explicit GpuPassSpan(std::string_view name):span(GpuPassSpanBegin(name)) {}
  ~GpuPassSpan() { GpuPassSpanEnd(span); }
  GpuPassSpan(const GpuPassSpan&)=delete;
  GpuPassSpan& operator=(const GpuPassSpan&)=delete;
  int span;
};
// edf_native_shadow_render: one shadow frame (native_shadow_render.h has the
// design and the side-effect handling). Begin (the 821A5080 hook, before the
// native frame) arms the draw-list tap on the scene backend; the host's
// Label names the native pass recording; Run (NativeFullFrameHost::Phases,
// after the native post, before the HUD) captures the native image, renders
// the guest route into the shadow's own targets, captures that, puts every
// guest word and bridge field back and writes the files. The destructor
// removes the tap on every exit.
}
namespace edf::native {
namespace {
class NativeShadowFrame {
 public:
  using GuestCall=std::function<void(uint32_t function,uint32_t object,uint32_t argument,uint32_t index,uint32_t lr)>;
  static std::unique_ptr<NativeShadowFrame> Begin(uint8_t* base,uint32_t owner) {
    const NativeShadowSchedule schedule{REXCVAR_GET(edf_native_shadow_render),REXCVAR_GET(edf_native_shadow_render_start_frame),
      uint64_t((std::max)(0,REXCVAR_GET(edf_native_shadow_render_limit)))};
    static uint64_t taken=0,last_frame=0;  // 821A6508 joins each helper call before the next.
    auto& state=State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    const auto frame=state.indexed_output_frames+1;
    if(frame==last_frame || !NativeShadowRenderDue(frame,schedule,taken)) return nullptr;
    last_frame=frame; ++taken;
    std::unique_ptr<NativeShadowFrame> shadow(new NativeShadowFrame(base,owner,frame));
    try {
      auto& backend=EnsureSceneBackendLocked(state);
      if(backend.recorder_tap) throw std::runtime_error("the scene backend already has a recorder tap");
      shadow->tap_.Arm("native.begin");
      shadow->tap_.RecordConstantBytes(REXCVAR_GET(edf_native_shadow_render_constants));
      backend.recorder_tap=&shadow->tap_;
      shadow->backend_=&backend;
    } catch(const std::exception& error) {
      REXLOG_ERROR("Native shadow render frame={}: not armed: {}",frame,error.what());
      return nullptr;
    }
    REXLOG_INFO("Native shadow render: frame={} armed (every {} from {}, {} of {})",frame,schedule.period,schedule.start,taken,schedule.limit);
    return shadow;
  }
  ~NativeShadowFrame() { Disarm(); }
  NativeShadowFrame(const NativeShadowFrame&)=delete;
  NativeShadowFrame& operator=(const NativeShadowFrame&)=delete;
  void Label(std::string label) { tap_.SetLabel(std::move(label)); }
  // After the native finish stage, before the HUD: `context` is the helper's
  // guest frame context (the host's), `views` the views BeginView accepted.
  void Run(const NativeFrameContext& frame,uint32_t context,uint32_t views,const GuestCall& guest);

 private:
  NativeShadowFrame(uint8_t* base,uint32_t owner,uint64_t frame)
    :base_(base),reader_(base),owner_(owner),frame_(frame),serial_before_(reader_.Word(reader_.Add(owner,136))) {}
  // The shadow's own scene color, depth and ordinary output, matching the
  // native scene's so the guest scene begin and 8219C930 reuse them. Leaked,
  // as other backend-lifetime caches here, so no exit order can free them late.
  struct Targets {
    NativeRenderBackend* backend=nullptr;
    uint32_t width=0,height=0,samples=0,output_format=0;
    NativeRenderTarget color,output;
    NativeDepthTarget depth;
  };
  static Targets& ShadowTargets() { static auto* targets=new Targets; return *targets; }
  void Disarm() {
    if(!backend_) return;
    auto& state=State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    if(state.scene_backend.get()==backend_ && backend_->recorder_tap==&tap_) backend_->recorder_tap=nullptr;
    backend_=nullptr;
  }
  std::string GuestLabel(uint32_t lr,uint32_t object) const {
    switch(lr) {
      case 0x821A515C: return "guest.view_begin";
      case 0x821A51DC: {
        uint32_t vtable=0;
        try { vtable=reader_.Word(object); } catch(const std::exception&) {}
        return std::format("guest.world:{:08x}",vtable);
      }
      case 0x821A520C: return "guest.buckets";
      case 0x821A5268: return "guest.overlays";
      case 0x821A5294: return "guest.view16";
      case 0x821A52AC: return "guest.view_end";
      case 0x821A52E8: return "guest.finish";
      case 0x821A52FC: return "guest.finish16";
      default: return std::format("guest.{:08x}",lr);
    }
  }
  void CaptureGuestWords(NativeGuestSnapshot& snapshot,const NativeFrameInputs& inputs) const {
    const auto capture=[&](std::string name,uint32_t address,uint32_t words) {
      try { snapshot.Capture(reader_,std::move(name),address,words); }
      catch(const std::exception& error) { REXLOG_WARN("Native shadow render: {} at {:#x} not snapshot: {}",name,address,error.what()); }
    };
    capture("serial",reader_.Add(owner_,136),1);
    capture("buckets",reader_.Add(owner_,168),512);  // owner+168..+2215: heads and the drain's buckets
    if(const auto pool=reader_.Word(NativeViewGlobalsLayout::kPoolGlobal)) {
      capture("pool.copies",reader_.Add(pool,NativeViewGlobalsLayout::kProjectionCopy),36);  // +64..+207
      for(uint32_t offset=32;offset<=56;offset+=4) {
        const auto record=reader_.Word(reader_.Add(pool,offset));
        if(!record) continue;
        const auto data=reader_.Word(reader_.Add(record,NativeViewGlobalsLayout::kValueData));
        const auto count=reader_.Word(reader_.Add(record,NativeViewGlobalsLayout::kValueCount));
        if(data && count && count<=1024) capture(std::format("pool.record+{}",offset),data,count*4);
      }
    }
    if(inputs.registry) for(const auto& entry:inputs.registry->entries) {
      try {
        if(!entry || reader_.Word(entry->object)!=NativeBrokenObject::vtable) continue;
      } catch(const std::exception&) { continue; }
      capture(std::format("broken+712:{:08x}",entry->object),reader_.Add(entry->object,NativeBrokenObject::drawn),1);
    }
  }
  void Write(const std::string& suffix,std::string_view bytes) const {
    const auto path=std::filesystem::path(prefix_+"."+std::to_string(frame_)+suffix);
    std::ofstream file(path,std::ios::binary|std::ios::trunc);
    file.write(bytes.data(),std::streamsize(bytes.size()));
    file.close();
    if(!file) throw std::runtime_error("shadow render write failed: "+path.string());
  }

  uint8_t* base_;
  const GuestReader reader_;
  uint32_t owner_;
  uint64_t frame_;
  uint32_t serial_before_;
  std::string prefix_;
  NativeRenderBackend* backend_=nullptr;
  NativeDrawListRecorder tap_;
};
void NativeShadowFrame::Run(const NativeFrameContext& frame,uint32_t context,uint32_t views,const GuestCall& guest) {
  auto& state=State();
  prefix_=REXCVAR_GET(edf_native_shadow_render_prefix);
  if(prefix_.empty()) prefix_="native-shadow/shadow";
  const auto renderer=reader_.Word(0x8257bfb4);
  std::vector<NativeDrawRecord> native_draws,guest_draws;
  std::vector<uint8_t> native_bmp,guest_bmp;
  // The resolved HDR scene each post read (clamped to 0..1 in the BMP): the
  // pre-post images, so a post difference can be told from a scene one.
  std::vector<uint8_t> native_scene_bmp,guest_scene_bmp;
  std::string scene_error;
  const auto capture_scene=[&](NativeScene& scene,const char* side) {
    try {
      auto& sampled=scene.color.sampled;
      if(!sampled.backend || !sampled.content_valid) throw std::runtime_error("no resolved scene color");
      SubmitSceneFrameLocked(state);
      return CaptureNativeBmp(EnsureSceneBackendLocked(state),*sampled.backend,sampled.format);
    } catch(const std::exception& error) {
      scene_error+=std::string(scene_error.empty()?"":"; ")+side+": "+error.what();
      return std::vector<uint8_t>{};
    }
  };
  std::string skipped;
  // 1. The native frame as the post left it (pre-HUD), and its draw list.
  {
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    native_draws=tap_.Take();
    const auto scene=renderer?state.scenes.find(renderer):state.scenes.end();
    if(!frame.output_ready) skipped="the native post left no output";
    else if(!views) skipped="no view was rendered";
    else if(scene==state.scenes.end() || state.active_output!=renderer || !scene->second.output.content_valid)
      skipped="the native output is not active and valid";
    else if(state.scene_backend.get()!=backend_) skipped="the scene backend changed";
    else {
      try { native_bmp=CaptureOutputBmp(state,scene->second); }
      catch(const std::exception& error) { skipped=std::string("native capture: ")+error.what(); }
      if(skipped.empty()) native_scene_bmp=capture_scene(scene->second,"native");
      tap_.ResetState();
    }
  }
  if(!skipped.empty()) {
    REXLOG_WARN("Native shadow render frame={}: skipped: {}",frame_,skipped);
    Disarm();
    return;
  }
  // 2. Guest words the guest route writes and game state keeps.
  NativeGuestSnapshot snapshot;
  CaptureGuestWords(snapshot,frame.inputs);
  // 3. The shadow's targets in place of the native scene's, and the bridge
  // fields the guest scene begin and finish rewrite.
  struct Saved {
    uint32_t active_scene=0,active_output=0,active_target=0,timer_owner=0,color_surface=0;
    bool full_frame=false,timer_resolved=false,frame_complete=false;
    uint64_t indexed_start=0;
    std::vector<std::pair<uint32_t,uint32_t>> target_stack;
    std::vector<DrawVisibility> visibility;
    uint32_t resolved_handle=0;
    std::optional<NativeTexture> resolved;
    // Every registered shader's sampler slots. The guest route's activations
    // decode the guest's sampler words into these shared bindings, and the
    // full-frame post reads its samplers back from them (BridgePostSink::Draw):
    // left in place, every later native frame would post with the guest's
    // samplers instead of its own, and every later shadow frame would compare
    // that instead of the native renderer.
    struct Samplers { uint32_t shader=0; bool reversed=false; ShaderBindings* bindings=nullptr; std::map<UINT,NativeBackendSampler*> values; };
    std::vector<Samplers> samplers;
  } saved;
  size_t samplers_restored=0;
  auto& targets=ShadowTargets();
  const auto swap_targets=[&](NativeScene& scene) {
    std::swap(scene.color,targets.color); std::swap(scene.depth,targets.depth); std::swap(scene.output,targets.output);
  };
  {
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    auto& scene=state.scenes.at(renderer);
    const auto width=scene.color.sampled.width,height=scene.color.sampled.height;
    if(targets.backend!=backend_ || targets.width!=width || targets.height!=height || targets.samples!=scene.samples ||
       targets.output_format!=scene.output.format) {
      targets.color=CreateNativeRenderTarget(*backend_,width,height,DXGI_FORMAT_R16G16B16A16_FLOAT,scene.samples);
      targets.depth=CreateNativeDepthTarget(*backend_,width,height,DXGI_FORMAT_D32_FLOAT_S8X24_UINT,scene.samples,0.0f);
      targets.output=CreateNativeRenderTarget(*backend_,scene.output.sampled.width,scene.output.sampled.height,DXGI_FORMAT(scene.output.format));
      targets.backend=backend_; targets.width=width; targets.height=height; targets.samples=scene.samples;
      targets.output_format=scene.output.format;
      REXLOG_INFO("Native shadow render targets: {}x{} samples={} output_format={}",width,height,scene.samples,scene.output.format);
    }
    saved.active_scene=state.active_scene; saved.active_output=state.active_output; saved.active_target=state.active_target;
    saved.timer_owner=state.scene_gpu_timer_owner; saved.timer_resolved=state.scene_gpu_timer_resolved;
    saved.full_frame=state.scene_full_frame; saved.indexed_start=state.scene_indexed_start;
    saved.target_stack=state.target_stack; saved.visibility=std::move(state.visibility); state.visibility.clear();
    saved.color_surface=scene.color_surface; saved.frame_complete=scene.frame_complete;
    saved.resolved_handle=reader_.Word(reader_.Add(renderer,104));
    if(const auto found=state.textures.find(saved.resolved_handle);found!=state.textures.end()) saved.resolved=found->second;
    for(auto& [handle,shader]:state.shaders)
      for(const bool reversed:{false,true})
        if(auto* bindings=(reversed?shader.reversed_bindings:shader.bindings).get(); bindings && bindings->BindsResources())
          saved.samplers.push_back({handle,reversed,bindings,bindings->SamplerValues()});
    swap_targets(scene);
    targets.output.content_valid=false;  // The shadow output starts undefined, as a new frame's does.
    ++state.bind_generation; state.recorded={};
  }
  // 4. The guest route for the same state: scene begin (clSgsCoreRender +0),
  // the view loop and the finish stage, no phase loop.
  NativeShadowGuest held;
  std::exception_ptr failure;
  {
    const NativeAbSideLatch guest_side(false);
    struct Scope {
      bool tick_frame=native_render_tick_frame;
      NativeShadowGuest* shadow=native_shadow_guest;
      uint32_t animation_owner=native_scene_animation_owner;
      std::optional<NativeScenePassCamera> camera=native_scene_pass_camera;
      std::optional<NativeScenePassAnimation> animation=native_scene_pass_animation;
      ~Scope() {
        native_render_tick_frame=tick_frame; native_shadow_guest=shadow; native_scene_animation_owner=animation_owner;
        native_scene_pass_camera=std::move(camera); native_scene_pass_animation=std::move(animation);
      }
    } scope;
    native_render_tick_frame=false;  // The 8217C4A0 hook puts clEffectEtc02 +612 back.
    native_shadow_guest=&held;       // The immediate draw hook holds PS_Downsample_Tone.
    native_scene_animation_owner=0; native_scene_pass_camera.reset(); native_scene_pass_animation.reset();
    {
      std::lock_guard submission(state.submissions);
      std::lock_guard lock(state.mutex);
      tap_.Arm("guest.scene_begin");
    }
    try {
      reader_.StoreWord(reader_.Add(owner_,136),serial_before_);  // The native views' serials.
      const auto core=reader_.Word(reader_.Add(owner_,132));
      guest(reader_.Word(reader_.Word(core)),core,0,0,0x821A6508);
      {
        std::lock_guard submission(state.submissions);
        std::lock_guard lock(state.mutex);
        if(state.active_scene!=renderer) throw std::runtime_error("the guest scene begin did not open the scene");
      }
      DispatchNativeFrame(reader_,owner_,context,[&](uint32_t function,uint32_t object,uint32_t argument,uint32_t index,uint32_t lr) {
        tap_.SetLabel(GuestLabel(lr,object));
        guest(function,object,argument,index,lr);
      },false);
    } catch(...) { failure=std::current_exception(); }
  }
  // 5. The guest image, then the native scene back and every bridge field.
  std::string guest_error;
  {
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    guest_draws=tap_.Take();
    auto& scene=state.scenes.at(renderer);
    if(failure) guest_error="the guest route threw";
    else if(state.active_output!=renderer || !scene.output.content_valid) guest_error="the guest post left no valid output";
    else {
      try { guest_bmp=CaptureOutputBmp(state,scene); }
      catch(const std::exception& error) { guest_error=std::string("guest capture: ")+error.what(); }
      if(guest_error.empty()) guest_scene_bmp=capture_scene(scene,"guest");
    }
    swap_targets(scene);
    scene.color_surface=saved.color_surface; scene.frame_complete=saved.frame_complete;
    state.active_scene=saved.active_scene; state.active_output=saved.active_output; state.active_target=saved.active_target;
    state.scene_gpu_timer_owner=saved.timer_owner; state.scene_gpu_timer_resolved=saved.timer_resolved;
    state.scene_full_frame=saved.full_frame; state.scene_indexed_start=saved.indexed_start;
    state.target_stack=std::move(saved.target_stack); state.visibility=std::move(saved.visibility);
    if(saved.resolved) state.textures.insert_or_assign(saved.resolved_handle,*saved.resolved);
    // Only bindings still owned by the same registration: one the guest route
    // replaced is not the object that was saved.
    for(const auto& entry:saved.samplers) {
      const auto found=state.shaders.find(entry.shader);
      if(found==state.shaders.end()) continue;
      auto* bindings=(entry.reversed?found->second.reversed_bindings:found->second.bindings).get();
      if(bindings==entry.bindings) samplers_restored+=bindings->RestoreSamplerValues(entry.values);
    }
    ++state.bind_generation; state.recorded={};
    tap_.ResetState();
    BindActiveTarget(state);
  }
  Disarm();
  // 6. Every guest word back as the native frame left it.
  const auto restored=snapshot.Restore(reader_);
  // 7. The files.
  uint32_t restored_words=0;
  std::string ranges;
  for(const auto& range:restored) {
    restored_words+=range.changed;
    ranges+=std::format("{}{{\"name\":{},\"address\":\"{:08x}\",\"words\":{},\"changed\":{}}}",ranges.empty()?"":",",
      NativeShadowJsonString(range.name),range.address,range.words,range.changed);
  }
  std::string excluded;
  for(const auto& item:kNativeShadowExclusions) excluded+=(excluded.empty()?"":",")+NativeShadowJsonString(item);
  const auto& motion=frame.inputs.motion;
  const auto stem=std::filesystem::path(prefix_+"."+std::to_string(frame_)).filename().string();
  const auto meta=std::format("{{\"format\":\"edf-shadow-frame\",\"version\":1,\"frame\":{},\"owner\":\"{:08x}\",\"renderer\":\"{:08x}\","
    "\"views\":{},\"serial\":{},\"motion\":{{\"tick\":{},\"fraction\":{},\"steps\":{},\"unlocked\":{},\"interpolate\":{}}},\"tick_frame\":{},"
    "\"native\":{{\"image\":{},\"draws\":{},\"count\":{}}},\"guest\":{{\"image\":{},\"draws\":{},\"count\":{},\"error\":{}}},"
    "\"held\":{{\"tone\":{},\"lifetime\":{}}},\"restored_words\":{},\"restored\":[{}],\"samplers_restored\":{},"
    "\"scene\":{{\"native\":{},\"guest\":{},\"error\":{}}},\"excluded\":[{}]}}\n",
    frame_,owner_,renderer,views,serial_before_,motion.tick,NativeShadowJsonFloat(motion.fraction),motion.steps,motion.unlocked,
    motion.interpolate,frame.inputs.tick_frame,
    NativeShadowJsonString(stem+".native.bmp"),NativeShadowJsonString(stem+".native.draws.jsonl"),native_draws.size(),
    guest_bmp.empty()?std::string("null"):NativeShadowJsonString(stem+".guest.bmp"),NativeShadowJsonString(stem+".guest.draws.jsonl"),
    guest_draws.size(),guest_error.empty()?std::string("null"):NativeShadowJsonString(guest_error),
    held.tone_holds,held.lifetime_holds,restored_words,ranges,samplers_restored,
    native_scene_bmp.empty()?std::string("null"):NativeShadowJsonString(stem+".native.scene.bmp"),
    guest_scene_bmp.empty()?std::string("null"):NativeShadowJsonString(stem+".guest.scene.bmp"),
    scene_error.empty()?std::string("null"):NativeShadowJsonString(scene_error),excluded);
  try {
    if(const auto directory=std::filesystem::path(prefix_).parent_path();!directory.empty())
      std::filesystem::create_directories(directory);
    Write(".native.bmp",{reinterpret_cast<const char*>(native_bmp.data()),native_bmp.size()});
    if(!guest_bmp.empty()) Write(".guest.bmp",{reinterpret_cast<const char*>(guest_bmp.data()),guest_bmp.size()});
    if(!native_scene_bmp.empty()) Write(".native.scene.bmp",{reinterpret_cast<const char*>(native_scene_bmp.data()),native_scene_bmp.size()});
    if(!guest_scene_bmp.empty()) Write(".guest.scene.bmp",{reinterpret_cast<const char*>(guest_scene_bmp.data()),guest_scene_bmp.size()});
    Write(".native.draws.jsonl",SerializeNativeDrawList("native",frame_,native_draws));
    Write(".guest.draws.jsonl",SerializeNativeDrawList("guest",frame_,guest_draws));
    Write(".shadow.json",meta);
  } catch(const std::exception& error) { REXLOG_ERROR("Native shadow render frame={}: {}",frame_,error.what()); }
  REXLOG_INFO("Native shadow render: frame={} prefix={} native_draws={} guest_draws={} tone_holds={} lifetime_holds={} restored_words={} samplers_restored={} guest_error={}",
    frame_,prefix_,native_draws.size(),guest_draws.size(),held.tone_holds,held.lifetime_holds,restored_words,samplers_restored,
    guest_error.empty()?"none":guest_error);
  if(failure) std::rethrow_exception(failure);
}
}
}
namespace {
// FSR's motion vectors: workstream B's (native_motion_vectors.h), which
// NativeFullFrame::Run hands the frame context as the last accepted view's
// (null texture and reset when edf_native_motion_vectors recorded none;
// PlanNativeFsrMotion turns that into zero motion and a history reset). The
// one place FSR takes them from.
edf::native::NativeMotionVectorOutput NativeFsrMotionInput(const edf::native::NativeFrameContext& context) {
  return context.motion;
}
class NativeFullFrameHost final : public edf::native::NativeFrameHost {
 public:
  using GuestCall=std::function<void(uint32_t function,uint32_t object,uint32_t argument,uint32_t index,uint32_t lr)>;
  NativeFullFrameHost(uint8_t* base,uint32_t owner,uint32_t context,GuestCall guest)
    :base_(base),reader_(base),owner_(owner),context_(context),guest_(std::move(guest)) {}
  // A pass that threw leaves no jitter behind for the guest routes or the HUD.
  ~NativeFullFrameHost() { EndFsrView(); }
  NativeFullFrameHost(const NativeFullFrameHost&)=delete;
  NativeFullFrameHost& operator=(const NativeFullFrameHost&)=delete;
  // edf_native_shadow_render: this frame's shadow (null on every other frame).
  void SetShadow(edf::native::NativeShadowFrame* shadow) { shadow_=shadow; }
  // The models pass's per-view handoff, for the velocity draws.
  void SetModels(std::shared_ptr<edf::native::NativeFullFrameModelsShared> models) { models_=std::move(models); }
  // (a) One generation of publication, published cameras and world animations
  // under the producer lock, as the hook acquires them; the motion budget and
  // its publication were already acquired by the hook, which restores all of
  // them on exit. The thread-locals are the ones existing native pass code reads.
  //
  // Every input is an immutable generation (shared_ptr to const) taken once
  // here, so all views and passes of the frame see one consistent step: the
  // simulation publishes a new generation instead of mutating one a frame
  // holds. The bridge mutex alone guards the swap of the adapter's pointers.
  edf::native::NativeFrameInputs AcquireInputs() override {
    auto& state=edf::native::State();
    std::shared_ptr<const edf::native::NativeRenderRegistrySnapshot> registry;
    {
      std::lock_guard lock(state.mutex);
      // A frame armed for FSR and never resolved is not this one's.
      edf::native::DisarmNativeFsrLocked(state);
      edf::native::native_scene_publication=state.scene_adapter.AcquirePublication();
      edf::native::native_scene_pass_cameras=EDF_NATIVE_FLAG(scene_camera_owned)?state.scene_adapter.AcquireCameras():nullptr;
      edf::native::native_scene_pass_animations=state.scene_adapter.AcquireWorldAnimations();
    }
    // The renderable registry's latest tick (edf_native_render_registry, and
    // always with the full frame); null before its first tick.
    registry=edf::native::RenderRegistry().AcquireSnapshot();
    // The render budget the hook took from the 821A4DE8 publication, as the
    // camera's 821CDDF8 interpolation sampled it: the models pass blends the
    // registry's poses with it under the guest 821C9C20 hook's condition.
    const auto motion=edf::native::MakeNativeFrameMotion(native_render_budget.unlocked,native_render_budget.divisor,
      native_render_budget.tick,native_render_budget.fraction,native_render_budget.steps,REXCVAR_GET(edf_native_model_interpolation));
    return {edf::native::native_scene_publication,edf::native::native_scene_pass_cameras,
      edf::native::native_scene_pass_animations,native_render_publication,std::move(registry),motion};
  }
  // The helper's view loop condition and list (owner+0..owner+12, view at
  // node+8), and the frame context it initializes before the loop.
  std::vector<uint32_t> Views() override {
    const auto flag=[&](uint32_t offset) { return *reader_.Bytes(reader_.Add(owner_,offset),1)!=0; };
    std::vector<uint32_t> views;
    if(flag(2261) || flag(2262) || !(flag(2216) || flag(2217))) return views;
    const auto zero=reader_.Word(0x820009a4),one=reader_.Word(0x820008cc);
    for(const auto offset:{0u,4u,32u,36u,40u}) reader_.StoreWord(reader_.Add(context_,offset),zero);
    reader_.StoreWord(reader_.Add(context_,44),one);
    reader_.StoreWord(reader_.Add(context_,12),0);
    reader_.StoreWord(reader_.Add(context_,16),0);
    const auto end=Word(12);
    for(auto node=Word(0);node!=end;node=reader_.Word(node)) {
      if(views.size()>=256) throw std::runtime_error("native full frame view list is cyclic or excessive");
      views.push_back(reader_.Word(reader_.Add(node,8)));
    }
    return views;
  }
  // Helper side effects kept per view, as 821A5080 and DispatchNativeFrame
  // write them: the frame context the view's guest listeners read (near, far,
  // view+400, serial, view), the frame serial owner+136, and the bucket heads
  // owner+168..+2215 cleared (nothing refills them: the world callbacks and
  // 821A3BA0 are replaced by native passes).
  uint32_t AdvanceSerial(uint32_t view) override {
    const auto serial=Word(136);
    reader_.StoreWord(context_,reader_.Word(0x8201711c));
    reader_.StoreWord(reader_.Add(context_,4),reader_.Word(0x82017120));
    reader_.StoreWord(reader_.Add(context_,8),reader_.Word(reader_.Add(view,400)));
    reader_.StoreWord(reader_.Add(context_,12),serial);
    reader_.StoreWord(reader_.Add(context_,16),view);
    reader_.StoreWord(reader_.Add(owner_,136),serial+1);
    for(uint32_t i=0;i<512;++i) reader_.StoreWord(reader_.Add(owner_,168+i*4),0);
    return serial;
  }
  // (b) The native half of 821BE8D0 (clSgsCoreRender +4) on the scene 8219C7A8
  // opened: pass camera (published, else read as the 821BE8D0 hook does),
  // viewport from scene+488..+500 with the retail depth range words 820008CC and
  // 820009A4 (reversed), and the depth/stencil clear it issues for every view
  // but the first (8219C7A8 already cleared color and depth). The guest
  // view/projection globals 821BE8D0 ends with (821A17F8/821A19F0 on the effect
  // pool [8257C02C]: g_mProjection, g_mViewTranspose, g_mViewInverseTranspose,
  // g_mView, g_mViewProjection and the pool's +64/+128/+192 copies) are written
  // natively from the pass camera (native_view_globals.h), for every view as
  // the helper calls +4 for every view, before any pass: the view's guest
  // listeners (ViewOverlays: clItem01 through 8216DA80, clPlayerCamera
  // 820D3FD0) draw with them. The native effect and wire builders derive the
  // same eye from the pass camera themselves (NativeEffectEyeFromView), so
  // their stale_guest_eye diagnostic now counts only a failed write. Not
  // replicated here: native passes take context.viewport and their own depth
  // state; 82135530(device,1), the depth test the guest listeners draw with,
  // is replayed by ViewOverlays.
  bool BeginView(edf::native::NativeFrameContext& context) override {
    edf::native::HookTiming timing(edf::native::HookPhase::FrameNativeBegin);
    const auto scene=context.view.scene;
    const auto& cameras=context.inputs.cameras;
    const auto found=cameras?cameras->find(scene):edf::native::NativeScenePassCameras::const_iterator{};
    if(cameras && found!=cameras->end()) {
      edf::native::native_scene_pass_camera=found->second;
      // The native culls (models, static world, effects, wires, grass) read
      // scene+96/+288 live; they are the pass camera's only while nothing
      // wrote the scene since the publication (see BuildNativeElectricWireDraws).
      if(REXCVAR_GET(edf_native_scene_transform_audit) &&
         found->second!=edf::native::ReadNativeScenePassCamera(reader_,scene)) {
        static std::atomic<uint64_t> mismatches=0;
        if(const auto count=++mismatches;count<=4 || !(count&(count-1)))
          REXLOG_ERROR("Native full frame camera mismatch: scene={:#x} changed after publication; culls and draws disagree (count={})",
            scene,count);
      }
    }
    else edf::native::native_scene_pass_camera=edf::native::ReadNativeScenePassCamera(reader_,scene);
    // 821BE8D0's 821A17F8(pool, scene+32) and 821A19F0(pool, scene+96), from
    // the same camera the native passes draw with.
    if(!edf::native::WriteNativeViewGlobals(reader_,edf::native::native_scene_pass_camera->projection,
         edf::native::native_scene_pass_camera->view)) {
      static std::atomic<uint64_t> missing=0;
      if(const auto count=++missing;count<=4 || !(count&(count-1)))
        REXLOG_WARN("Native full frame view: no effect pool at 8257C02C; view globals not written (count={})",count);
    }
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    const auto renderer=reader_.Word(kRenderer);
    const auto native_scene=state.scenes.find(renderer);
    if(!renderer || state.active_scene!=renderer || native_scene==state.scenes.end()) {
      static uint64_t skipped=0;
      if(++skipped<=4 || !(skipped&(skipped-1)))
        REXLOG_INFO("Native full frame view skipped: renderer={:#x} active_scene={:#x} skipped={} (no scene from 8219C7A8)",
          renderer,state.active_scene,skipped);
      // No view pass runs: everything the guest would draw in the view is dropped.
      if(edf::native::NativeCoverageCensusOn())
        edf::native::CoverageCensus().Add(edf::native::NativeCoverageStatus::Uncovered,0,"frame","view_skipped");
      return false;
    }
    context.renderer=renderer;
    state.scene_full_frame=true;  // Counts as the scene's indexed draws for output frames and captures.
    context.owner=owner_; context.guest_context=context_;
    ++views_begun_;
    const auto width=reader_.Word(reader_.Add(renderer,84)),height=reader_.Word(reader_.Add(renderer,88));
    // 821BE8D0 copies six words from scene+480 and converts the last four,
    // +488/+492/+496/+500, to the viewport x/y/width/height (fctidz); +480 is
    // the vertical field of view 821CDDF8 consumes and +484 its companion.
    // Reading the rectangle at +480 took the angle for x and x for width, so
    // every view fell back to the whole surface (split views included).
    const auto rect=edf::native::ReadGuestWords<4>(reader_,reader_.Add(scene,488));
    const auto extent=[](uint32_t word,uint32_t limit) {
      const float value=std::bit_cast<float>(word);  // fctidz truncates; clamp to the surface.
      return value>0?uint32_t((std::min)(double(value),double(limit))):0u;
    };
    auto& viewport=context.viewport;
    viewport.x=extent(rect[0],width); viewport.y=extent(rect[1],height);
    viewport.width=(std::min)(extent(rect[2],width),width-viewport.x);
    viewport.height=(std::min)(extent(rect[3],height),height-viewport.y);
    viewport.min_depth=std::bit_cast<float>(reader_.Word(0x820008cc));
    viewport.max_depth=std::bit_cast<float>(reader_.Word(0x820009a4));
    // An empty rectangle would clip every native draw away; the scene's
    // viewport is the full surface in practice (8219C828's contract).
    if(!viewport.width || !viewport.height) {
      static std::atomic<uint64_t> empty=0;
      if(const auto count=++empty;count<=4 || !(count&(count-1)))
        REXLOG_WARN("Native full frame view viewport empty: scene={:#x} rect={:#x},{:#x},{:#x},{:#x}; using {}x{} (count={})",
          scene,rect[0],rect[1],rect[2],rect[3],width,height,count);
      viewport.x=0; viewport.y=0; viewport.width=width; viewport.height=height;
    }
    static std::atomic<uint64_t> views=0;
    if(const auto count=++views;count<=4 || count%10000==0)
      REXLOG_INFO("Native full frame view: scene={:#x} viewport={},{} {}x{} depth={}..{} surface={}x{} index={} (count={})",
        scene,viewport.x,viewport.y,viewport.width,viewport.height,viewport.min_depth,viewport.max_depth,width,height,
        context.view.index,count);
    if(context.view.index) {
      edf::native::HookTiming clear_timing(edf::native::HookPhase::SceneClear);
      edf::native::ClearNativeDepthTarget(edf::native::SceneRecorderLocked(state),native_scene->second.depth,true,true,
        viewport.max_depth,0);
    }
    // FSR (native_fsr.h): the frame's first accepted view arms it and picks
    // the jitter; every view of an armed frame draws jittered. The pass camera
    // and the view globals written above stay unjittered.
    if(!fsr_arm_tried_) {
      fsr_arm_tried_=true;
      edf::native::ArmNativeFsrFrameLocked(state,renderer,native_scene->second,native_render_frames.load(std::memory_order_relaxed));
    }
    edf::native::native_scene_draw_camera.reset();
    edf::native::native_scene_view_jitter={};
    edf::native::native_scene_mip_bias_steps=0;
    state.fsr.view_scale={};
    if(state.fsr.frame && state.fsr.owner==renderer) {
      const auto& jitter=state.fsr.jitter;
      edf::native::native_scene_view_jitter=jitter;
      edf::native::native_scene_draw_camera=edf::native::NativeFsrJitterCamera(*edf::native::native_scene_pass_camera,jitter);
      state.scene_renderer.SetClipJitter(jitter.clip_x,jitter.clip_y);
      state.fsr.camera=edf::native::NativeFsrCameraFromProjectionWords(edf::native::native_scene_pass_camera->projection);
      // Upscaling (native_fsr.h): the view's draws land in the render-size
      // corner of the scene targets - the renderer's views and the recorded
      // draws (RecordDrawSetup) are mapped there, context.viewport and every
      // cache key keep the output's rectangle - and the scene's materials
      // sample with the mip bias. EndView (EndFsrView) takes both off.
      if(const auto& scale=state.fsr.scale;scale.active()) {
        state.fsr.view_scale=scale;
        state.scene_renderer.SetRenderScale(scale);
        edf::native::native_scene_mip_bias_steps=edf::native::NativeFsrMipBiasSteps(scale.render_width,scale.display_width);
      }
    }
    edf::native::BindActiveTarget(state);
    if(state.context) edf::native::MakeNativeDrawViewport(viewport.x,viewport.y,viewport.width,viewport.height,
      viewport.min_depth,viewport.max_depth,false,{}).Bind(*state.context.Get());
    return true;
  }
  // (c) Per-pass timing: frame.native.<name>, in kNativeFramePassOrder order.
  void RunPass(size_t index,edf::native::NativeFramePass& pass,edf::native::NativeFrameContext& context) override {
    constexpr auto first=size_t(edf::native::HookPhase::FrameNativeSky);
    constexpr auto count=size_t(edf::native::HookPhase::FrameNativeEnd)-first;
    static_assert(count==std::size(edf::native::kNativeFramePassOrder));
    edf::native::HookTiming timing(index<count?edf::native::HookPhase(first+index):edf::native::HookPhase::FrameNative,index<count);
    const GpuPassSpan gpu(pass.name());
    if(shadow_) shadow_->Label(std::string("native.")+pass.name());
    pass.Record(context);
    // FSR: the opaque-only colour (sky, opaque models, static world), after
    // the last of them and before the effects and transparent passes, for the
    // reactive mask.
    if(edf::native::native_scene_draw_camera && std::string_view(pass.name())=="static_world") CopyFsrOpaque(context);
  }
  void CopyFsrOpaque(const edf::native::NativeFrameContext& context) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    auto& fsr=state.fsr;
    const auto scene=state.scenes.find(context.renderer);
    auto* opaque=fsr.upscaler.opaque_texture();
    if(!fsr.frame || fsr.owner!=context.renderer || state.active_scene!=context.renderer || scene==state.scenes.end() ||
       !opaque || !scene->second.color.backend_surface) return;
    edf::native::SceneRecorderLocked(state).ResolveTarget(*opaque,*scene->second.color.backend_surface);
    ++state.bind_generation;
    state.recorded={};
    fsr.opaque=true;
  }
  // REMAINING GUEST CALLS, per view, in the helper's order: the overlay
  // listeners' +12 (clSatoCallback 8216DA80 -> 8217A728 -> 82122640: the
  // [8257BF88] instance list's slot 3s, i.e. clItem01 8218F3D8, the pickups)
  // with the frame context, then the view's +16 (clPlayerCamera 820D3FD0:
  // follow/talk icons and the trajectory ribbon). Their draws reach the native
  // scene through the per-draw hooks, with the view globals BeginView wrote.
  // Those hooks take the depth test from the device mirrors, so the guest
  // view's depth bracket is replayed around them: clSgsCoreRender +4 ends
  // with 82135530(device,1) (depth test on, 821BE9CC) and +8 (821BE9D8) is
  // 82135530(device,0), after the view's +16. Without the first, the
  // listeners inherited the previous frame's 82135530(device,0) (BindOutput,
  // for the HUD) and the pickups drew over every model and wall in front.
  void ViewOverlays(edf::native::NativeFrameContext& context) override {
    // The guest listeners' per-render state follows this frame's tick gate
    // (native_render_tick_frame; the 821A5080 hook restores it on exit).
    native_render_tick_frame=context.inputs.tick_frame;
    edf::native::HookTiming timing(edf::native::HookPhase::FrameNativeOverlays);
    const GpuPassSpan gpu("view_overlays");
    if(shadow_) shadow_->Label("native.overlays");
    // FSR: the listeners' draws take their camera from the view globals, so
    // for this call those carry the jitter too (the draw camera's projection;
    // the view, hence the eye pool+192, is unchanged). Put back unjittered
    // when the call returns or throws; the view's jitter ends at EndView.
    struct FsrViewGlobals {
      NativeFullFrameHost& host;
      ~FsrViewGlobals() { host.RestoreFsrViewGlobals(); }
    } fsr_view_globals{*this};
    if(const auto& draw=edf::native::native_scene_draw_camera)
      edf::native::WriteNativeViewGlobals(reader_,draw->projection,draw->view);
    const auto renderer=reader_.Word(kRenderer);
    const auto device=renderer?reader_.Word(reader_.Add(renderer,8)):0u;
    if(device) guest_(0x82135530,device,1,0,0x821BE9CC);
    RemainingGuestCall(0);
    const auto sentinel=[&] { return Word(2232); };
    for(auto node=reader_.Word(sentinel());node!=sentinel();) {
      Virtual(reader_.Word(reader_.Add(node,12)),12,context_,0,0x821A5268);
      if(node==sentinel()) throw std::runtime_error("native full frame overlay iterator invalidated");
      node=reader_.Word(node);
    }
    RemainingGuestCall(1);
    Virtual(context.view.scene,16,0,0,0x821A5294);
    if(device) guest_(0x82135530,device,0,0,0x821A52AC);
  }
  // The view globals back to the unjittered pass camera after the jittered
  // ViewOverlays call. Nothing when the view is not jittered. Never throws.
  void RestoreFsrViewGlobals() noexcept {
    if(!edf::native::native_scene_draw_camera) return;
    try {
      if(const auto& camera=edf::native::native_scene_pass_camera)
        edf::native::WriteNativeViewGlobals(reader_,camera->projection,camera->view);
    } catch(...) {}
  }
  // The end of a view's jittered drawing (EndView, or the host's destruction
  // when a pass threw): no renderer jitter, no draw camera. Never throws.
  void EndFsrView() noexcept {
    if(!edf::native::native_scene_draw_camera) return;
    RestoreFsrViewGlobals();
    edf::native::native_scene_draw_camera.reset();
    edf::native::native_scene_view_jitter={};
    edf::native::native_scene_mip_bias_steps=0;
    try {
      auto& state=edf::native::State();
      std::lock_guard submission(state.submissions);
      std::lock_guard lock(state.mutex);
      state.scene_renderer.SetClipJitter(0,0);
      state.scene_renderer.SetRenderScale({});
      state.fsr.view_scale={};
    } catch(...) {}
  }
  void EndView(edf::native::NativeFrameContext&) override { EndFsrView(); }
  // edf_native_motion_vectors: the view's motion vectors, after its passes
  // and guest overlays, before post (RecordNativeMotionVectors), into
  // context.motion. Camera reprojection from the scene depth (the scene is
  // made 1x with a sampled depth while the cvar is on, restart required) and
  // the models pass's velocity draws. The pass camera is the one every pass
  // of the view drew with; jitter (workstream C) must stay out of it or be
  // taken off here. Everything the record bound is forgotten afterwards and
  // the scene's targets are bound again.
  void MotionVectors(edf::native::NativeFrameContext& context) override {
    // FSR implies them: a jittered (FSR-armed) view records them too.
    if((!REXCVAR_GET(edf_native_motion_vectors) && !edf::native::native_scene_draw_camera) ||
       !context.renderer || !edf::native::native_scene_pass_camera) return;
    const GpuPassSpan gpu("motion_vectors");
    const auto velocity=models_?models_->velocity:nullptr;
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    const auto scene=state.scenes.find(context.renderer);
    if(state.active_scene!=context.renderer || scene==state.scenes.end() || !state.scene_backend) return;
    auto& motion=MotionVectorState();
    edf::native::NativeMotionVectorContext record;
    record.backend=state.scene_backend.get();
    record.recorder=&edf::native::SceneRecorderLocked(state);
    record.depth=scene->second.depth.backend_target.get();
    record.depth_format=scene->second.depth.format;
    record.width=scene->second.color.sampled.width; record.height=scene->second.color.sampled.height;
    const auto& v=context.viewport;
    // Upscaling: the view drew into the render-size corner; the vectors are
    // written there too, in UV over that rectangle (FSR scales them by the
    // render size).
    record.viewport=edf::native::ScaleNativeViewport({float(v.x),float(v.y),float(v.width),float(v.height),0,1},
      state.fsr.view_scale);
    record.scene=context.view.scene;
    record.frame=context.inputs.frame;
    record.camera=&*edf::native::native_scene_pass_camera;
    record.velocity=velocity.get();
    try {
      context.motion=edf::native::RecordNativeMotionVectors(motion,record);
    } catch(...) {
      ++state.bind_generation; state.recorded={};
      edf::native::BindActiveTarget(state);
      throw;
    }
    // The velocity draws' geometry stays until the recording is submitted.
    if(velocity) state.scene_recorded_frames.push_back(velocity);
    ++state.bind_generation; state.recorded={};
    edf::native::BindActiveTarget(state);
    if(!context.motion.motion) {
      static std::atomic<bool> reported=false;
      if(!reported.exchange(true))
        REXLOG_WARN("Native motion vectors declined: the scene depth has no SRV (samples={}); they need a 1x scene depth made sampled (logged once)",
          scene->second.samples);
      return;
    }
    const auto& stats=motion.statistics();
    if(stats.records<=4 || stats.records%1000==0)
      REXLOG_INFO("Native motion vectors: records={} resets={} last_reset={} declined={} velocity_draws={} instances={} skipped={} "
        "this_view_velocity={} scenes={} reasons first/gap/viewport/size/cut/invalid={}/{}/{}/{}/{}/{}",
        stats.records,stats.resets,edf::native::NativeMotionResetName(motion.last_reset()),stats.declined,stats.velocity_draws,
        stats.velocity_instances,stats.velocity_skipped,velocity?velocity->size():size_t(0),motion.history().size(),
        stats.reasons[1],stats.reasons[2],stats.reasons[3],stats.reasons[4],stats.reasons[5],stats.reasons[6]);
  }
  // HOOK POINT: helper side effects other code relies on, to be filled from
  // the ongoing side-effect research. Known and not replicated: the world
  // callbacks (owner+44 list, vtable +8) and the bucket drain 821A3BA0, which
  // the native passes replace; clSgsCoreRender +16 (820AFEE8) is an empty
  // body (branch to the blr at 8252B718) and needs nothing.
  void SideEffects(const edf::native::NativeFrameInputs&) override {}
  // The finish stage (clSgsCoreRender +12, 820B0B80): the native post with
  // zero guest calls, then the output bind (BindOutput); the guest stage only
  // when the post reports an error, and then as a remaining guest call.
  // 820B0B80 resolves the scene (mode 1) only while byte 2260 of [8257C030] is
  // clear; the native post takes the same. Returns whether the output is bound
  // as the HUD's per-draw hooks require it (NativeOutputBound): the bridge's
  // active output and the guest device's bound color surface. The native post
  // sets the first and BindOutput the second; the guest stage's 8219C930 both.
  bool Finish(edf::native::NativeFrameContext& context) override {
    const auto post=Word(132);
    const auto renderer=reader_.Word(kRenderer);
    const bool resolve_scene=*reader_.Bytes(reader_.Add(reader_.Word(0x8257c030),2260),1)==0;
    std::string error;
    // An unlocked render-only frame holds the tone history (NativePostHistory):
    // its 0.025-per-draw blend stays once per simulation tick.
    const auto history=context.inputs.tick_frame?edf::native::NativePostHistory::Advance:edf::native::NativePostHistory::Hold;
    // FSR dispatches inside the scene's resolve (either route below) with the
    // frame's motion vectors (the last accepted view's).
    {
      auto& state=edf::native::State();
      std::lock_guard submission(state.submissions);
      std::lock_guard lock(state.mutex);
      if(state.fsr.frame) state.fsr.motion=NativeFsrMotionInput(context);
    }
    if(edf::native::RecordNativeFullFramePost(base_,post,resolve_scene,&error,history)) {
      BindOutput(renderer,resolve_scene);
      MotionVectorDebug(renderer,context.motion);
    } else {
      edf::native::FrameEventCounters().post_fallbacks.fetch_add(1,std::memory_order_relaxed);
      static std::atomic<bool> reported=false;
      if(!reported.exchange(true))
        REXLOG_WARN("Native full frame post failed, guest finish stage 820B0B80 used: {} (logged once)",error);
      RemainingGuestCall(2);
      // The guest stage's native chain holds the tone history by the same
      // gate (a view-less frame never reached ViewOverlays, which sets it).
      native_render_tick_frame=context.inputs.tick_frame;
      Virtual(post,12,0,0,0x821A52E8);
    }
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    const auto scene=renderer?state.scenes.find(renderer):state.scenes.end();
    const edf::native::NativeOutputBinding binding{state.active_output,state.active_target,state.active_scene,
      scene!=state.scenes.end()?scene->second.output_surface:0u,
      renderer?reader_.Word(reader_.Add(reader_.Word(reader_.Add(renderer,8)),12168)):0u};
    const bool bound=edf::native::NativeOutputBound(renderer,binding);
    if(!bound && renderer && binding.active_output==renderer) {
      static std::atomic<uint64_t> unbound=0;
      if(const auto count=++unbound;count<=4 || !(count&(count-1)))
        REXLOG_WARN("Native full frame output not bound on the guest device: output_surface={:#x} device_surface={:#x} target={:#x} scene={:#x} (HUD draws are refused; count={})",
          binding.output_surface,binding.device_surface,binding.active_target,binding.active_scene,count);
    }
    return bound;
  }
  // edf_native_motion_vectors_debug: the frame's motion vectors (1: as colour,
  // 2: the history-valid mask) over the native post's output, before the HUD.
  void MotionVectorDebug(uint32_t renderer,const edf::native::NativeMotionVectorOutput& motion) {
    const auto mode=REXCVAR_GET(edf_native_motion_vectors_debug);
    if(!mode || !motion.motion || !renderer) return;
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    const auto scene=state.scenes.find(renderer);
    if(scene==state.scenes.end() || state.active_output!=renderer || !scene->second.output.backend_surface || !state.scene_backend) return;
    auto& output=scene->second.output;
    // An upscaled frame's vectors fill the render-size corner of their target.
    const auto& scale=state.fsr.scale;
    const std::array<uint32_t,2> extent=scale.active()?std::array<uint32_t,2>{scale.render_width,scale.render_height}:
      std::array<uint32_t,2>{};
    MotionVectorState().RecordDebug(*state.scene_backend,edf::native::SceneRecorderLocked(state),*output.backend_surface,
      output.format,mode,motion,extent);
    ++state.bind_generation; state.recorded={};
    edf::native::BindActiveTarget(state);
  }
  // The motion vectors' state (texture, per-scene camera history, pipelines),
  // for the scene backend. Leaked, as the other backend-lifetime caches here.
  static edf::native::NativeMotionVectors& MotionVectorState() {
    static auto* state=new edf::native::NativeMotionVectors;
    return *state;
  }
  // What 820B0B80 does around its post chain that the native post does not,
  // and what the HUD phase loop inherits from it in the guest. 8219C930's tail
  // binds the ordinary output owner+112 as color target 0 (82137F98: the
  // device+12168 mirror every HUD per-draw hook compares with the output) and
  // owner+120 as depth (82137CB8, whose hook publishes render state), sets
  // owner+96 so 8219C840 takes its ordinary-output path (8213FAF8) as in guest
  // mode, and ends the untiled scope 821409A0 opened (82140E98; without this,
  // 8219C840 -> 8219C678 ends it). 82135530(device,0) then turns the depth
  // test off, which the Utility hook requires of output draws. The 2D scope
  // 821A7270/821A73F8 is a balanced state push/pop that leaves nothing, and
  // the HUD's 821A71F0 wrap sets its own 2D viewport, so neither is repeated.
  //
  // Minimal guest calls rather than written mirrors: 82137F98 also keeps the
  // surface-info and dirty words the guest device derives with the surface,
  // and both calls go through their hooks as 820B0B80's own would, with its
  // return addresses. The 8219C930 hook finds the scene already closed and
  // resolved by the native post, so it only re-derives the same output
  // (created by the post) and makes it active; its reset of content_valid,
  // meant for a guest post still to draw, is undone, since the native post
  // has already drawn this frame's output.
  void BindOutput(uint32_t renderer,bool resolve_scene) {
    if(!renderer) return;
    auto& state=edf::native::State();
    bool valid=false;
    {
      std::lock_guard submission(state.submissions);
      std::lock_guard lock(state.mutex);
      if(state.active_output!=renderer || state.active_scene) return;
      valid=state.scenes.at(renderer).output.content_valid;
    }
    RemainingGuestCall(4);
    guest_(0x8219C930,renderer,resolve_scene?1:0,0,0x820B0BC0);
    guest_(0x82135530,reader_.Word(reader_.Add(renderer,8)),0,0,0x820B0BD0);
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    if(state.active_output==renderer) state.scenes.at(renderer).output.content_valid=valid;
  }
  // REMAINING GUEST CALLS: the phase loop, which draws the HUD/XUI onto the
  // output (clNoguchiCallback 820A4DD0, clSatoCallback 8216E630); each draw
  // is translated by the per-draw hooks (821FD8F8). The output is bound first.
  void Phases(edf::native::NativeFrameContext& context) override {
    // edf_native_shadow_render: the guest route for this frame's state, after
    // the native post and before the HUD, into the shadow's own targets.
    if(shadow_) shadow_->Run(context,context_,views_begun_,guest_);
    // The HUD's two draw-counted advances (clGaugeRader 82176708, the window
    // cursor fade 8218ED68) follow this frame's tick gate; see their hooks.
    native_render_tick_frame=context.inputs.tick_frame;
    edf::native::HookTiming timing(edf::native::HookPhase::FrameNativePhases);
    {
      auto& state=edf::native::State();
      std::lock_guard submission(state.submissions);
      std::lock_guard lock(state.mutex);
      edf::native::BindActiveTarget(state);
    }
    RemainingGuestCall(3);
    const GpuPassSpan gpu("hud");
    const bool gpu_phases=REXCVAR_GET(edf_native_gpu_timings);
    const auto sentinel=[&] { return Word(2232); };
    for(auto phase=Word(140);phase!=Word(144);++phase) {
      // One span per HUD phase, by phase number (the first eight only).
      const auto gpu_phase=gpu_phases && phase-Word(140)<8 ? GpuPassSpanBegin("hud.phase"+std::to_string(phase)) : -1;
      for(auto node=reader_.Word(sentinel());node!=sentinel();) {
        Virtual(reader_.Word(reader_.Add(node,12)),16,phase,0,0x821A536C);
        if(node==sentinel()) throw std::runtime_error("native full frame phase iterator invalidated");
        node=reader_.Word(node);
      }
      GpuPassSpanEnd(gpu_phase);
    }
  }
  // (d) After this helper returns, 821A6508 calls clSgsCoreRender +20
  // (821BE9F0 -> 8219C840). With post output (output_ready) its hook publishes
  // the active ordinary output, post and HUD included, and captures it as an
  // indexed output frame; with the scene still open (no post) it resolves the
  // scene to the frame buffer and publishes that. The next +24 (821BEA00 ->
  // 8219C1F8 -> 82151460) reaches edf_native_swap_wait, which submits the
  // scene recorder and paces. Nothing here publishes.
  void EndScene(const edf::native::NativeFrameInputs&,bool output_ready) override {
    edf::native::HookTiming timing(edf::native::HookPhase::FrameNativeEnd);
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    // An FSR frame the scene's resolve did not consume (a direct frame).
    edf::native::DisarmNativeFsrLocked(state);
    const auto renderer=reader_.Word(kRenderer);
    const bool output=renderer && state.active_output==renderer;
    const bool open=renderer && state.active_scene==renderer;
    static uint64_t frames=0,unpublished=0;
    if(!output && !open) ++unpublished;
    if(++frames<=4 || frames%1000==0 || (!output && !open && unpublished<=4))
      REXLOG_INFO("Native full frame end: frames={} renderer={:#x} output_ready={} output={} scene_open={} unpublished={} (8219C840 publishes, swap presents)",
        frames,renderer,output_ready,output,open,unpublished);
  }
  void Unimplemented(const char* pass) override {
    static std::mutex mutex;
    static std::set<std::string> logged;
    std::lock_guard lock(mutex);
    if(logged.emplace(pass).second) REXLOG_WARN("Native full frame pass unimplemented: {} (records nothing)",pass);
  }
 private:
  static constexpr uint32_t kRenderer=0x8257bfb4;  // Renderer global: +8 device, +84/+88 extent.
  uint32_t Word(uint32_t offset) const { return reader_.Word(reader_.Add(owner_,offset)); }
  void Virtual(uint32_t object,uint32_t method,uint32_t argument,uint32_t index,uint32_t lr) {
    guest_(reader_.Word(reader_.Add(reader_.Word(object),method)),object,argument,index,lr);
  }
  static void RemainingGuestCall(size_t which) {
    static std::array<std::atomic<bool>,std::size(edf::native::kNativeFrameRemainingGuestCalls)> logged{};
    if(!logged[which].exchange(true))
      REXLOG_INFO("Native full frame remaining guest call: {}",edf::native::kNativeFrameRemainingGuestCalls[which]);
  }
  uint8_t* base_;
  const edf::native::GuestReader reader_;
  uint32_t owner_,context_;
  GuestCall guest_;
  edf::native::NativeShadowFrame* shadow_=nullptr;
  std::shared_ptr<edf::native::NativeFullFrameModelsShared> models_;
  uint32_t views_begun_=0;
  bool fsr_arm_tried_=false;  // this frame's first accepted view has armed FSR (or declined to)
};
}

namespace edf::native {
// The 821A5080 hook's full-frame route from the host on: the host over this helper call (owner, the
// frame context at context, guest for the remaining guest calls) with the models' handoff, the shadow
// frame, the frame's run with its GPU timing markers, and the census and dispatch reports. Moved from
// the hook unchanged; the host and the shadow end with this call, as they ended with the hook's route.
void RunNativeFullFrame(NativeFullFrame& full_frame,uint8_t* base,uint32_t owner,uint32_t context,
    const std::shared_ptr<NativeFullFrameModelsShared>& models,NativeFullFrameGuestCall guest) {
  NativeFullFrameHost host(base,owner,context,std::move(guest));
  host.SetModels(models);
  // edf_native_shadow_render (native_shadow_render.h): null unless this
  // frame takes a shadow render; the tap it arms is removed on every exit.
  std::unique_ptr<edf::native::NativeShadowFrame> shadow;
  if(REXCVAR_GET(edf_native_shadow_render)>0) {
    shadow=edf::native::NativeShadowFrame::Begin(base,owner);
    host.SetShadow(shadow.get());
  }
  {
    edf::native::HookTiming frame_timing(edf::native::HookPhase::FrameNative);
    GpuPassFrameBegin();
    full_frame.Run(host);
    GpuPassFrameEnd();
  }
  if(NativeCoverageCensusOn()) {
    auto& census=edf::native::CoverageCensus();
    const auto now=NativeCoverageNow();
    census.EndFrame(now);
    for(const auto& line:census.Poll(now,double(REXCVAR_GET(edf_native_coverage_census_interval)))) REXLOG_INFO("{}",line);
  }
  const auto frames=full_frame.frames();
  if(frames<=4 || frames%1000==0)
    REXLOG_INFO("Native full frame dispatch: frames={} view_passes={} frame_passes={} (guest helper not called; view listeners, finish fallback and HUD phases remain guest)",
      frames,full_frame.view_passes().size(),full_frame.frame_passes().size());
}
}  // namespace edf::native

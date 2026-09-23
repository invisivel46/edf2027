#pragma once
#include "native_render_backend.h"
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace edf::native {
// Shadow render (edf_native_shadow_render=N): every Nth indexed output frame
// the full frame renders natively as usual, and then, in the same render
// helper call and from the same simulation state, the guest render helper
// path renders the frame again into separate offscreen targets that are never
// presented. Both images and both draw lists are written to disk, so a native
// frame is compared with the guest frame of the SAME state instead of the
// neighbouring frame A/B alternation (edf_native_ab_alternate) has to use.
//
// Where it runs (guest_shader_bridge.cpp, the sub_821A5080 hook and
// NativeFullFrameHost::Phases): after the native post (the finish stage) and
// before the HUD phase loop. The engine loop joins the helper before the next
// transition, so the simulation is held for the extra render by the join the
// engine already has; nothing else waits.
//
// The guest side is the guest helper's own route with the A/B latch on the
// guest side (NativeAbSideLatch(false), so every native pass hook takes its
// guest branch): scene begin (clSgsCoreRender slot +0 -> 8219C7A8), then
// DispatchNativeFrame's view loop (+4, the world callbacks, the bucket drain
// 821A3BA0, the overlay listeners, view +16, +8) and finish (+12 820B0B80,
// +16), without the phase loop. The native scene color, depth and ordinary
// output are swapped out for the shadow's own targets for the duration, so
// the guest scene begin, draws and post write only those.
//
// Guest side effects of that route, and how each is kept from changing game
// state (the helper's other effects are per-render scratch the next render
// rewrites before reading):
// - owner+136 render serial: set back to the value the native frame started
//   from, so the guest views get the native views' serials, and put back to
//   the native frame's result afterwards (snapshot range "serial").
// - owner+168..+2215 bucket heads and drain buckets: snapshot and restored.
// - effect pool [8257C02C] view globals (+64 projection, +128 view, +192 eye)
//   and the float4 data of its shared records +32..+56 (g_mWorld,
//   g_mWorldArray, g_mView, g_mViewTranspose, g_mViewInverseTranspose,
//   g_mViewProjection, g_mProjection): snapshot and restored.
// - clEffectEtc02 +612 lifetime (8217C4A0): the guest route runs with
//   native_render_tick_frame false, so the existing hook puts the word back
//   after each call (NativeRenderStepOncePerTick). The slot's draw does not
//   read +612, so the image is unaffected.
// - clBrokenObject +712 = +708 (8211FAA8): idempotent (the native models pass
//   stored the same value this frame); +712 of every registry entry of the
//   class is still snapshot and restored, and a change is reported.
// - post tone history (PS_Downsample_Tone's 0.025-per-draw blend, a GPU
//   history): the guest post's PS_Downsample_Tone draw is skipped (its target
//   already holds this frame's value, resolved by the native post), which is
//   what NativePostHistory::Hold does; the Tone and Bloom passes then read the
//   same history the native post read.
// - the HUD phase loop (XUI clock via the Sato phases, the timers, the radar
//   shake 82176708, the cursor fade 8218ED68): not run in the shadow. The HUD
//   runs once, natively, after the shadow; both images are captured before it.
// - model pose history (the 821C9C20 hook, NativeModelPoseHistory): sampled
//   again with the same tick and input, which leaves it unchanged.
// - owner stamps object+48 = serial (820B4038's visited stamp): only ever
//   compared for equality with the current pass's serial, which only grows.
// - the shared shader bindings' samplers: the guest route's activations
//   decode the guest's sampler words into them, and the full-frame post reads
//   its samplers back from them (it binds fallbacks only where they are
//   empty). Every registered shader's sampler slots are saved and put back
//   ("samplers_restored" in the metadata), so the native frames after a shadow
//   post exactly as before it and later shadow frames compare the native post.
// Nothing the native frame left in the bridge is lost: the active scene,
// output and target, the scene counters and the owner+104 texture entry are
// restored, and the native output is swapped back before the HUD draws.
struct NativeShadowSchedule {
  int64_t period=0,start=0;
  uint64_t limit=0;
};
// Whether indexed output frame `frame` (the one this helper call produces)
// takes a shadow render, `taken` shadow frames having been written already.
constexpr bool NativeShadowRenderDue(uint64_t frame,const NativeShadowSchedule& schedule,uint64_t taken) {
  if(schedule.period<=0 || taken>=schedule.limit) return false;
  const auto start=schedule.start<0?0:uint64_t(schedule.start);
  return frame>=start && (frame-start)%uint64_t(schedule.period)==0;
}
// What the comparison leaves out, written into every shadow frame's metadata.
inline constexpr std::string_view kNativeShadowExclusions[]{
  "hud: the phase loop (owner+140..+144 x listener +16) runs once, natively, after both captures; images are pre-HUD",
  "tone: the guest post's PS_Downsample_Tone draw is held (not drawn); its history is the native post's",
  "presentation: 8219C840 publishes and captures the native output only",
  "guest immediate rings and device command state advance for the extra render (per-render scratch, not restored)"};

// Guest words saved before the shadow and written back after it. Memory:
// Word/StoreWord on guest addresses (GuestReader).
struct NativeGuestRange {
  std::string name;
  uint32_t address=0;
  std::vector<uint32_t> words;
};
struct NativeGuestRestore {
  std::string name;
  uint32_t address=0,words=0,changed=0;  // changed: words the shadow had changed and were put back
};
class NativeGuestSnapshot {
 public:
  template<class Memory>
  void Capture(const Memory& memory,std::string name,uint32_t address,uint32_t words) {
    NativeGuestRange range{std::move(name),address,{}};
    range.words.reserve(words);
    for(uint32_t i=0;i<words;++i) range.words.push_back(memory.Word(address+i*4));
    ranges_.push_back(std::move(range));
  }
  // Writes back every word that differs, last range first (so an overlap
  // ends with the earliest capture), and reports what each range had changed.
  template<class Memory>
  std::vector<NativeGuestRestore> Restore(const Memory& memory) const {
    std::vector<NativeGuestRestore> out(ranges_.size());
    for(size_t r=ranges_.size();r-->0;) {
      const auto& range=ranges_[r];
      auto& report=out[r];
      report.name=range.name; report.address=range.address; report.words=uint32_t(range.words.size());
      for(uint32_t i=0;i<range.words.size();++i) {
        const auto address=range.address+i*4;
        if(memory.Word(address)==range.words[i]) continue;
        memory.StoreWord(address,range.words[i]);
        ++report.changed;
      }
    }
    return out;
  }
  const std::vector<NativeGuestRange>& ranges() const { return ranges_; }
  size_t words() const {
    size_t total=0;
    for(const auto& range:ranges_) total+=range.words.size();
    return total;
  }
 private:
  std::vector<NativeGuestRange> ranges_;
};

// One draw as a recorder saw it, for the draw-list diff (tools/shadow-diff.py).
inline uint64_t NativeShadowHash(const void* data,size_t size,uint64_t hash=14695981039346656037ull) {
  const auto* bytes=static_cast<const uint8_t*>(data);
  for(size_t i=0;i<size;++i) { hash^=bytes[i]; hash*=1099511628211ull; }
  return hash;
}
struct NativeDrawTexture {
  uint32_t stage=0,slot=0;
  uint64_t id=0;  // Object identity within the run (both lists share the process's textures).
  uint32_t width=0,height=0;
  bool operator==(const NativeDrawTexture&) const=default;
};
struct NativeDrawSampler {
  uint32_t stage=0,slot=0;
  uint64_t id=0;  // The backend's sampler object: deduplicated on its description, so equal ids are equal states.
  bool operator==(const NativeDrawSampler&) const=default;
};
struct NativeDrawConstant {
  uint32_t stage=0,slot=0,bytes=0;
  uint64_t hash=0;
  // The bytes themselves, only when the tap records them
  // (edf_native_shadow_render_constants): which registers differ.
  std::vector<uint8_t> data;
  bool operator==(const NativeDrawConstant&) const=default;
};
enum class NativeDrawKind : uint32_t { Draw, Indexed, Instanced };
struct NativeDrawRecord {
  uint32_t index=0;
  std::string label;  // What was recording it: a native pass or the guest callback.
  uint64_t pipeline=0,vertex_shader=0,pixel_shader=0,layout=0;
  uint32_t topology=0;
  NativeDrawKind kind=NativeDrawKind::Draw;
  uint32_t count=0,first=0,instances=1,first_instance=0;
  int32_t base=0;
  std::vector<NativeDrawTexture> textures;
  std::vector<NativeDrawSampler> samplers;
  std::vector<NativeDrawConstant> constants;
  uint64_t constant_hash=0;  // Over every bound constant's (stage, slot, hash).
  std::optional<std::array<float,16>> world;  // The pipeline's g_mWorld registers, when it declares them.
  std::vector<uint64_t> targets;
  uint64_t depth=0;
  std::array<float,6> viewport{};
  std::optional<std::array<float,4>> blend;
  std::string geometry;  // "t:<hash>:<bytes>" transient vertices, "b:<id>+<offset>" a buffer, per slot.
  // The pipeline's stamped description (NativeBackendPipeline::identity_state
  // and identity_format), when it has one: two paths that build the same GPU
  // state from different guest words get different pipeline identities, and
  // these say which part differs.
  std::optional<RenderStateWords> state;
  std::array<uint32_t,5> format{};  // render targets, RTV 0, DSV, samples, topology
  std::optional<NativeBackendScissor> scissor;  // Set and enabled for the draw; nullopt when disabled.
  // Textures, samplers and constants are only the slots the pipeline's shaders
  // read (its reflection); otherwise every bound slot.
  bool reflected=false;
};
// The decoded render state as one canonical text (tools/shadow-diff.py splits
// it on spaces): equal texts are equal GPU state even where the guest words
// differ in bits the decoder ignores.
inline std::string NativeShadowStateText(const RenderStateWords& words) {
  try {
    const auto d=DecodeNativeRenderState(words);
    return std::format("blend={}:{}/{}/{}:{}/{}/{} mask={} depth={}/{}/{} raster={}/{}/{}/{}",
      int(d.blend_enable),d.src_color,d.dst_color,d.color_op,d.src_alpha,d.dst_alpha,d.alpha_op,uint32_t(d.write_mask),
      int(d.depth_enable),int(d.depth_write),d.depth_func,d.fill,d.cull,int(d.front_counter_clockwise),int(d.depth_clip));
  } catch(const std::exception&) {
    return "undecodable";
  }
}

// JSON Lines: a header object, then one object per draw in record order.
inline std::string NativeShadowJsonString(std::string_view text) {
  std::string out="\"";
  for(const char c:text) {
    switch(c) {
      case '"': out+="\\\""; break;
      case '\\': out+="\\\\"; break;
      case '\n': out+="\\n"; break;
      case '\r': out+="\\r"; break;
      case '\t': out+="\\t"; break;
      default:
        if(uint8_t(c)<0x20) out+=std::format("\\u{:04x}",uint32_t(uint8_t(c)));
        else out+=c;
    }
  }
  return out+"\"";
}
inline std::string NativeShadowJsonFloat(float value) {
  if(std::isnan(value)) return "\"nan\"";
  if(std::isinf(value)) return value>0?"\"inf\"":"\"-inf\"";
  return std::format("{}",value);
}
inline std::string NativeShadowHex(uint64_t value) { return std::format("\"{:016x}\"",value); }
inline std::string SerializeNativeDrawRecord(const NativeDrawRecord& draw) {
  static constexpr const char* kinds[]{"draw","indexed","instanced"};
  std::string out=std::format("{{\"i\":{},\"label\":{},\"pipeline\":{},\"vs\":{},\"ps\":{},\"layout\":{},\"topology\":{},"
    "\"kind\":\"{}\",\"count\":{},\"first\":{},\"base\":{},\"instances\":{},\"first_instance\":{},\"textures\":[",
    draw.index,NativeShadowJsonString(draw.label),NativeShadowHex(draw.pipeline),NativeShadowHex(draw.vertex_shader),
    NativeShadowHex(draw.pixel_shader),NativeShadowHex(draw.layout),draw.topology,kinds[uint32_t(draw.kind)%3],
    draw.count,draw.first,draw.base,draw.instances,draw.first_instance);
  for(size_t i=0;i<draw.textures.size();++i) {
    const auto& t=draw.textures[i];
    out+=std::format("{}{{\"stage\":{},\"slot\":{},\"id\":{},\"w\":{},\"h\":{}}}",i?",":"",t.stage,t.slot,NativeShadowHex(t.id),t.width,t.height);
  }
  out+="],\"samplers\":[";
  for(size_t i=0;i<draw.samplers.size();++i) {
    const auto& s=draw.samplers[i];
    out+=std::format("{}{{\"stage\":{},\"slot\":{},\"id\":{}}}",i?",":"",s.stage,s.slot,NativeShadowHex(s.id));
  }
  out+="],\"constants\":[";
  for(size_t i=0;i<draw.constants.size();++i) {
    const auto& c=draw.constants[i];
    out+=std::format("{}{{\"stage\":{},\"slot\":{},\"bytes\":{},\"hash\":{}",i?",":"",c.stage,c.slot,c.bytes,NativeShadowHex(c.hash));
    if(!c.data.empty()) {
      out+=",\"data\":\"";
      static constexpr char digits[]="0123456789abcdef";
      for(const auto byte:c.data) { out+=digits[byte>>4]; out+=digits[byte&15]; }
      out+="\"";
    }
    out+="}";
  }
  out+=std::format("],\"constant_hash\":{},\"world\":",NativeShadowHex(draw.constant_hash));
  if(draw.world) {
    out+="[";
    for(size_t i=0;i<16;++i) out+=(i?",":"")+NativeShadowJsonFloat((*draw.world)[i]);
    out+="]";
  } else out+="null";
  out+=",\"targets\":[";
  for(size_t i=0;i<draw.targets.size();++i) out+=(i?",":"")+NativeShadowHex(draw.targets[i]);
  out+=std::format("],\"depth\":{},\"viewport\":[",NativeShadowHex(draw.depth));
  for(size_t i=0;i<6;++i) out+=(i?",":"")+NativeShadowJsonFloat(draw.viewport[i]);
  out+="],\"blend\":";
  if(draw.blend) {
    out+="[";
    for(size_t i=0;i<4;++i) out+=(i?",":"")+NativeShadowJsonFloat((*draw.blend)[i]);
    out+="]";
  } else out+="null";
  out+=std::format(",\"geometry\":{}",NativeShadowJsonString(draw.geometry));
  if(draw.state) {
    const auto& w=*draw.state;
    out+=std::format(",\"state\":[\"{:08x}\",\"{:08x}\",\"{:08x}\",\"{:08x}\",\"{:08x}\",\"{:08x}\"],\"decoded\":{}",
      w[0],w[1],w[2],w[3],w[4],w[5],NativeShadowJsonString(NativeShadowStateText(w)));
    out+=std::format(",\"format\":[{},{},{},{},{}]",draw.format[0],draw.format[1],draw.format[2],draw.format[3],draw.format[4]);
  }
  if(draw.reflected) out+=",\"reflected\":true";
  out+=",\"scissor\":";
  if(draw.scissor) out+=std::format("[{},{},{},{}]",draw.scissor->left,draw.scissor->top,draw.scissor->right,draw.scissor->bottom);
  else out+="null";
  out+="}";
  return out;
}
inline std::string SerializeNativeDrawList(std::string_view side,uint64_t frame,const std::vector<NativeDrawRecord>& draws) {
  std::string out=std::format("{{\"format\":\"edf-shadow-draws\",\"version\":1,\"side\":{},\"frame\":{},\"draws\":{}}}\n",
    NativeShadowJsonString(side),frame,draws.size());
  for(const auto& draw:draws) { out+=SerializeNativeDrawRecord(draw); out+='\n'; }
  return out;
}

// The tap: forwards every call to the recorder it wraps and, on the thread
// that armed it, records each draw with the state bound for it. Everything
// else passes through untouched, so the frame renders exactly as without it.
// Bindings are tracked from every thread (the recorder is one producer under
// the bridge lock); only draws are filtered by thread and pause.
class NativeDrawListRecorder final : public NativeBackendRecorderTap {
 public:
  // Starts a list (and the state tracking) on the calling thread.
  void Arm(std::string label={}) {
    thread_=std::this_thread::get_id(); armed_=true; label_=std::move(label);
    draws_.clear(); state_={};
  }
  // Stops recording (calls still forward) and hands the list over.
  std::vector<NativeDrawRecord> Take() { armed_=false; return std::exchange(draws_,{}); }
  void Pause(bool paused) { paused_=paused; }
  // Forgets the tracked bindings, as a recorder does at a frame boundary (a
  // capture's submit closes the frame; the bridge binds everything again).
  void ResetState() { state_={}; stack_.clear(); }
  void SetLabel(std::string label) { label_=std::move(label); }
  // Also keep every draw's constant bytes (large: a diagnostic for which
  // registers differ, not for every run).
  void RecordConstantBytes(bool enabled) { record_constant_bytes_=enabled; }
  const std::string& label() const { return label_; }
  bool recording() const { return armed_ && !paused_ && std::this_thread::get_id()==thread_; }
  size_t size() const { return draws_.size(); }

  void Wrap(NativeBackendRecorder& inner) override { inner_=&inner; }
  void SetPipeline(NativeBackendPipeline& pipeline) override { state_.pipeline=&pipeline; Inner().SetPipeline(pipeline); }
  void SetWorldInstancing(bool enabled,bool reuse_constants=true) override { Inner().SetWorldInstancing(enabled,reuse_constants); }
  void SetTransientBatching(bool enabled) override { Inner().SetTransientBatching(enabled); }
  void SetVertexBuffer(uint32_t slot,NativeBackendBuffer& buffer,uint32_t stride,uint32_t offset) override {
    if(slot<kSlots) state_.geometry[slot]=std::format("b:{:x}+{}/{}",uint64_t(reinterpret_cast<uintptr_t>(&buffer)),offset,stride);
    Inner().SetVertexBuffer(slot,buffer,stride,offset);
  }
  void SetIndexBuffer(NativeBackendBuffer& buffer,NativeBackendIndexFormat format,uint32_t offset) override {
    state_.indices=std::format("i:{:x}+{}/{}",uint64_t(reinterpret_cast<uintptr_t>(&buffer)),offset,uint32_t(format));
    Inner().SetIndexBuffer(buffer,format,offset);
  }
  void SetTransientVertices(uint32_t slot,std::span<const uint8_t> bytes,uint32_t stride) override {
    NoteTransient(slot,bytes,stride);
    Inner().SetTransientVertices(slot,bytes,stride);
  }
  void SetTransientVerticesOwned(uint32_t slot,std::vector<uint8_t>& bytes,uint32_t stride) override {
    NoteTransient(slot,bytes,stride);
    Inner().SetTransientVerticesOwned(slot,bytes,stride);
  }
  void SetTopology(NativeBackendTopology topology) override { state_.topology=uint32_t(topology); Inner().SetTopology(topology); }
  void SetBlendFactor(const std::array<float,4>& factor) override { state_.blend=factor; Inner().SetBlendFactor(factor); }
  void SetConstants(NativeBackendStage stage,uint32_t slot,std::span<const uint8_t> bytes) override {
    if(armed_ && uint32_t(stage)<kStages && slot<kSlots) {
      auto& constant=state_.constants[uint32_t(stage)][slot];
      constant.bound=true;
      constant.hash=NativeShadowHash(bytes.data(),bytes.size());
      constant.bytes.assign(bytes.begin(),bytes.end());
    }
    Inner().SetConstants(stage,slot,bytes);
  }
  void SetTexture(NativeBackendStage stage,uint32_t slot,NativeBackendTexture* texture) override {
    if(uint32_t(stage)<kStages && slot<kSlots) state_.textures[uint32_t(stage)][slot]=texture;
    Inner().SetTexture(stage,slot,texture);
  }
  void SetSampler(NativeBackendStage stage,uint32_t slot,NativeBackendSampler* sampler) override {
    if(uint32_t(stage)<kStages && slot<kSlots) state_.samplers[uint32_t(stage)][slot]=sampler;
    Inner().SetSampler(stage,slot,sampler);
  }
  void SetRenderTargets(std::span<NativeBackendRenderTarget* const> colors,NativeBackendRenderTarget* depth) override {
    state_.targets.clear();
    for(auto* color:colors) state_.targets.push_back(uint64_t(reinterpret_cast<uintptr_t>(color)));
    state_.depth=uint64_t(reinterpret_cast<uintptr_t>(depth));
    Inner().SetRenderTargets(colors,depth);
  }
  void SetViewport(const NativeBackendViewport& viewport) override {
    state_.viewport={viewport.x,viewport.y,viewport.width,viewport.height,viewport.min_depth,viewport.max_depth};
    Inner().SetViewport(viewport);
  }
  void SetScissor(const NativeBackendScissor& scissor,bool enabled) override {
    state_.scissor=enabled?std::optional(scissor):std::nullopt;
    Inner().SetScissor(scissor,enabled);
  }
  void ClearColor(NativeBackendRenderTarget& target,const std::array<float,4>& color) override { Inner().ClearColor(target,color); }
  void ClearDepthStencil(NativeBackendRenderTarget& target,bool depth,bool stencil,float value,uint8_t stencil_value) override {
    Inner().ClearDepthStencil(target,depth,stencil,value,stencil_value);
  }
  void Draw(uint32_t vertices,uint32_t first_vertex) override {
    Note(NativeDrawKind::Draw,vertices,first_vertex,0,1,0);
    Inner().Draw(vertices,first_vertex);
  }
  void DrawIndexed(uint32_t indices,uint32_t first_index,int32_t base_vertex) override {
    Note(NativeDrawKind::Indexed,indices,first_index,base_vertex,1,0);
    Inner().DrawIndexed(indices,first_index,base_vertex);
  }
  void DrawIndexedInstanced(uint32_t indices,uint32_t instances,uint32_t first_index,int32_t base_vertex,uint32_t first_instance) override {
    Note(NativeDrawKind::Instanced,indices,first_index,base_vertex,instances,first_instance);
    Inner().DrawIndexedInstanced(indices,instances,first_index,base_vertex,first_instance);
  }
  void CopyTexture(NativeBackendTexture& destination,NativeBackendTexture& source) override { Inner().CopyTexture(destination,source); }
  void ReleaseSharedTexture(NativeBackendTexture& texture) override { Inner().ReleaseSharedTexture(texture); }
  void CopyToShared(NativeBackendSharedSurface& destination,NativeBackendRenderTarget& source) override { Inner().CopyToShared(destination,source); }
  void ResolveTarget(NativeBackendTexture& destination,NativeBackendRenderTarget& source) override { Inner().ResolveTarget(destination,source); }
  void UpdateBuffer(NativeBackendBuffer& buffer,uint32_t offset,std::span<const uint8_t> bytes) override { Inner().UpdateBuffer(buffer,offset,bytes); }
  void UpdateTexture(NativeBackendTexture& texture,std::span<const uint8_t> bytes) override { Inner().UpdateTexture(texture,bytes); }
  void BeginQuery(NativeBackendQuery& query) override { Inner().BeginQuery(query); }
  void EndQuery(NativeBackendQuery& query) override { Inner().EndQuery(query); }
  void WriteTimestamp(NativeBackendTimestamps& timestamps,uint32_t slot) override { Inner().WriteTimestamp(timestamps,slot); }
  void ResolveTimestamps(NativeBackendTimestamps& timestamps,uint32_t first,uint32_t count) override { Inner().ResolveTimestamps(timestamps,first,count); }
  // The recorder's own state stack: a foreign pass that pushes, binds and pops
  // leaves the tracked state as it found it, as it does the real recorder's.
  void PushState() override { stack_.push_back(state_); Inner().PushState(); }
  void PopState() override {
    if(!stack_.empty()) { state_=std::move(stack_.back()); stack_.pop_back(); }
    Inner().PopState();
  }
  // Raw passes are not draws: nothing to list, only the ordering to keep.
  NativeBackendRecorder& BeginExternal() override { return Inner().BeginExternal(); }
  void EndExternal() override { Inner().EndExternal(); }

 private:
  static constexpr uint32_t kStages=3,kSlots=16;
  struct Constant { bool bound=false; uint64_t hash=0; std::vector<uint8_t> bytes; };
  struct State {
    NativeBackendPipeline* pipeline=nullptr;
    uint32_t topology=0;
    std::array<std::array<Constant,kSlots>,kStages> constants{};
    std::array<std::array<NativeBackendTexture*,kSlots>,kStages> textures{};
    std::array<std::array<NativeBackendSampler*,kSlots>,kStages> samplers{};
    std::optional<NativeBackendScissor> scissor;
    std::array<std::string,kSlots> geometry{};
    std::string indices;
    std::vector<uint64_t> targets;
    uint64_t depth=0;
    std::array<float,6> viewport{};
    std::optional<std::array<float,4>> blend;
  };
  NativeBackendRecorder& Inner() {
    if(!inner_) throw std::runtime_error("native draw list tap has no recorder to forward to");
    return *inner_;
  }
  void NoteTransient(uint32_t slot,std::span<const uint8_t> bytes,uint32_t stride) {
    if(slot<kSlots && armed_)
      state_.geometry[slot]=std::format("t:{:016x}:{}/{}",NativeShadowHash(bytes.data(),bytes.size()),bytes.size(),stride);
  }
  void Note(NativeDrawKind kind,uint32_t count,uint32_t first,int32_t base,uint32_t instances,uint32_t first_instance) {
    if(!recording()) return;
    NativeDrawRecord draw;
    draw.index=uint32_t(draws_.size());
    draw.label=label_;
    if(const auto* pipeline=state_.pipeline) {
      draw.pipeline=pipeline->identity?pipeline->identity:uint64_t(reinterpret_cast<uintptr_t>(pipeline));
      draw.vertex_shader=pipeline->identity_vertex; draw.pixel_shader=pipeline->identity_pixel;
      draw.layout=pipeline->identity_layout;
      if(pipeline->identity) { draw.state=pipeline->identity_state; draw.format=pipeline->identity_format; }
      if(pipeline->world_instanced && pipeline->instance_world_slot<kSlots) {
        const auto& world=state_.constants[uint32_t(NativeBackendStage::Vertex)][pipeline->instance_world_slot];
        if(world.bound && pipeline->instance_world_offset+64<=world.bytes.size()) {
          std::array<float,16> matrix{};
          std::memcpy(matrix.data(),world.bytes.data()+pipeline->instance_world_offset,64);
          draw.world=matrix;
        }
      }
    }
    draw.topology=state_.topology; draw.kind=kind;
    draw.count=count; draw.first=first; draw.base=base; draw.instances=instances; draw.first_instance=first_instance;
    // Only the slots the pipeline's shaders read, where the backend reflected
    // them: a slot an earlier draw left bound is not this draw's input.
    const auto* used=state_.pipeline && state_.pipeline->used_slots_known?state_.pipeline:nullptr;
    draw.reflected=used!=nullptr;
    enum class Binding { Texture, Sampler, Constants };
    const auto reads=[used](uint32_t stage,uint32_t slot,Binding binding) {
      if(!used) return true;
      const auto bit=1u<<slot;
      if(stage==uint32_t(NativeBackendStage::Vertex)) return binding==Binding::Constants && (used->used_vertex_constants&bit)!=0;
      if(stage!=uint32_t(NativeBackendStage::Pixel)) return false;
      const auto mask=binding==Binding::Texture?used->used_pixel_textures:
                      binding==Binding::Sampler?used->used_pixel_samplers:used->used_pixel_constants;
      return (mask&bit)!=0;
    };
    uint64_t combined=14695981039346656037ull;
    for(uint32_t stage=0;stage<kStages;++stage) for(uint32_t slot=0;slot<kSlots;++slot) {
      if(auto* texture=state_.textures[stage][slot]; texture && reads(stage,slot,Binding::Texture))
        draw.textures.push_back({stage,slot,uint64_t(reinterpret_cast<uintptr_t>(texture)),texture->width(),texture->height()});
      if(auto* sampler=state_.samplers[stage][slot]; sampler && reads(stage,slot,Binding::Sampler))
        draw.samplers.push_back({stage,slot,uint64_t(reinterpret_cast<uintptr_t>(sampler))});
      const auto& constant=state_.constants[stage][slot];
      if(!constant.bound || !reads(stage,slot,Binding::Constants)) continue;
      draw.constants.push_back({stage,slot,uint32_t(constant.bytes.size()),constant.hash,
                                record_constant_bytes_?constant.bytes:std::vector<uint8_t>{}});
      const uint64_t key[3]{stage,slot,constant.hash};
      combined=NativeShadowHash(key,sizeof(key),combined);
    }
    draw.constant_hash=combined;
    for(uint32_t slot=0;slot<kSlots;++slot) if(!state_.geometry[slot].empty())
      draw.geometry+=std::format("{}{}={}",draw.geometry.empty()?"":" ",slot,state_.geometry[slot]);
    if(kind!=NativeDrawKind::Draw && !state_.indices.empty()) draw.geometry+=(draw.geometry.empty()?"":" ")+state_.indices;
    draw.targets=state_.targets; draw.depth=state_.depth; draw.viewport=state_.viewport; draw.blend=state_.blend;
    draw.scissor=state_.scissor;
    draws_.push_back(std::move(draw));
  }
  NativeBackendRecorder* inner_=nullptr;
  std::thread::id thread_{};
  // Read by every thread that records while the tap is installed; the thread
  // id and label only by the arming thread once armed.
  std::atomic<bool> armed_=false,paused_=false;
  bool record_constant_bytes_=false;
  std::string label_;
  State state_;
  std::vector<State> stack_;
  std::vector<NativeDrawRecord> draws_;
};
}

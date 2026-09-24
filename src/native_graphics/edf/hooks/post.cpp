// Post and finish: the native finish of 820B0B80, its seams and observers, and the full frame's post sink.
// Moved from guest_shader_bridge.cpp unchanged (stage 3 of its split); what this file shares with the bridge
// and the other hook files is in bridge/bridge_shared.h.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../bridge/bridge_shared.h"
#include "../../guest_shader_bridge.h"
#include "../../native_renderer_preset.h"
#include "../../../pause_menu.h"
#include "../../d3d12_backend.h"
#include "../../native_scene_adapter.h"
#include "../../native_static_world_resolve.h"
#include "../../guest_draw_state.h"
#include "../../guest_sdk_readable_range.h"
#include "../../native_full_frame_static_world.h"
#include "../../native_post_finish_plan.h"
#include "../../native_full_frame_post.h"
#include "../../d3d11_texture.h"
#include "../../d3d11_quads.h"
#include "../../bridge/native_cvars.h"
#include "../../bridge/bridge_state.h"
#include "../../bridge/bridge_helpers.h"
#include <rex/hook.h>
#include <rex/cvar.h>
#include <rex/ppc/func.h>
#include <rex/logging.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace edf::native {
namespace {
void BeginRenderTarget(uint32_t owner) {
  auto& state = State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  state.target_stack.emplace_back(owner,state.active_target);
  state.active_target = owner;
  BindActiveTarget(state);
  ++state.target_begins;
}
}
}
// Finish/post stage 820B0B80. Steps 1-2: the plan derives what its passes will
// set; the audit compares that plan with the setters, targets and quads the
// guest body actually issues. Step 3 (edf_native_post_finish): the post chain
// 820B09B0 is replaced by a native loop issuing the plan, and the bloom quad
// of 821A8F20 with it; every failure gives that frame back to the original.
//
// What stays guest in a native frame, and why:
// - 8219C930 resolve, 82135530, the 2D scope 821A7270/821A73F8: kept as the
//   task requires; the 820B0B80 body still runs around the replaced calls.
// - Target begin/end 821B8828/821B88B0 per pass: they write the device's
//   surface, viewport and resolve mirrors the native draw path reads back
//   (ReadNativeDrawViewport, render-state words, 12168) and that later guest
//   code inherits; their hooks push/pop the native target registry.
// - The setters 821BCD58/821BCE98/821BCF28, called with the plan's values: they
//   write the effects' parameter records, which the activation reads and which
//   persist in the post owner to the next frame. Their names are laid out in
//   the loop's own stack frame, so no 820A62E8/820B2510 allocation happens.
// - Activation 821B94E8 per pass, and the bloom's 821B94E8(this+580) with the
//   this+564 setters before it: they write the device's shader, constant,
//   sampler and render-state mirrors (and the native bindings through the
//   821B8E48 hook); later guest draws inherit every state the bloom does not
//   set itself, so replicating them would mean replicating the material
//   program. They are the guest=true steps of BuildPostFinishIssue.
// - 821A79B8 is the draw call the loop issues: it keeps the 2D object's
//   declaration 82149A90 and the 821FD8F8 CPU tail (dirty words, stride and
//   main-state mirrors), and its 821FD8F8 hook is the native post/quad
//   recording path (QuadStream seam, CanInitializeReductionTarget). What is
//   native: the chain's control flow, targets, sizes, offsets, blur kernel and
//   quads (the plan), the preflight against the native target registry, and
//   the bloom quad 821A8F20 would build.
REX_EXTERN(sub_821B8828);
REX_EXTERN(sub_821B88B0);
REX_EXTERN(sub_821BCD58);
REX_EXTERN(sub_821BCE98);
REX_EXTERN(sub_821BCF28);
REX_EXTERN(sub_821B94E8);
REX_EXTERN(sub_821A79B8);
namespace edf::native {
namespace {
struct PostFinishStats {
  uint64_t planned=0,rejected=0,audited=0,clean=0,mismatches=0,unobserved=0,recorder_errors=0;
  std::optional<std::array<float,3>> tone; // last values the native bindings reflected
  // Step 3.
  bool last_audit_clean=false,last_frame_native=false;
  uint64_t native_frames=0,native_bloom=0,ab_guest=0,uninitialized=0,self_audited=0,self_clean=0,self_mismatches=0;
  // Held renders: DownsampleTone dropped (tone_held), or drawn anyway because
  // its record had no resolved history yet (tone_unheld).
  uint64_t tone_held=0,tone_unheld=0;
  std::array<uint64_t,size_t(PostFinishFallback::Count)> fallbacks{};
};
PostFinishStats& FinishStats() { static PostFinishStats stats; return stats; }
// 820A62E8/820A6908 strings: +4 inline buffer or pointer, +20 size, +24 capacity.
std::string ReadGuestStdString(const GuestReader& reader,uint32_t address) {
  const auto size=reader.Word(reader.Add(address,20)),capacity=reader.Word(reader.Add(address,24));
  if(size>256 || size>capacity) throw std::runtime_error("unsupported guest parameter name string");
  const auto data=capacity>=16 ? reader.Word(reader.Add(address,4)) : reader.Add(address,4);
  if(!size) return {};
  return std::string(reinterpret_cast<const char*>(reader.Bytes(data,size)),size);
}
PostFinishInput ReadPostFinishInput(const GuestReader& reader,uint32_t self,const std::optional<std::array<float,3>>& tone) {
  using L=PostFinishLayout;
  if(!self) throw std::runtime_error("no post owner");
  PostFinishInput in; in.self=self;
  const auto owner=reader.Word(L::kOwnerGlobal);
  in.screen_width=std::bit_cast<int32_t>(reader.Word(reader.Add(owner,L::kOwnerWidth)));
  in.screen_height=std::bit_cast<int32_t>(reader.Word(reader.Add(owner,L::kOwnerHeight)));
  in.scene_texture=reader.Word(reader.Add(owner,L::kOwnerSceneTexture));
  const auto record=[&](uint32_t address) {
    PostFinishRecord r; r.address=address;
    r.texture=reader.Word(reader.Add(address,L::kRecordTexture));
    r.width=std::bit_cast<int32_t>(reader.Word(reader.Add(address,L::kRecordWidth)));
    r.height=std::bit_cast<int32_t>(reader.Word(reader.Add(address,L::kRecordHeight)));
    r.texel_x=std::bit_cast<float>(reader.Word(reader.Add(address,L::kRecordTexelX)));
    r.texel_y=std::bit_cast<float>(reader.Word(reader.Add(address,L::kRecordTexelY)));
    return r;
  };
  for(uint32_t k=0;k<L::kFirstPyramidCount;++k) in.first[k]=record(reader.Add(self,L::kFirstPyramid+k*L::kRecordStride));
  const auto records=reader.Word(reader.Add(self,L::kSecondPyramid));
  const auto count=reader.Word(reader.Add(self,L::kSecondCount));
  if(!records || !count || count>L::kMaxSecondCount)
    throw std::runtime_error(std::format("second pyramid {:#x} count {} unsupported",records,count));
  for(uint32_t i=0;i<count;++i) in.second.push_back(record(reader.Add(records,i*L::kRecordStride)));
  in.blur=record(reader.Add(self,L::kBlurTarget));
  in.blur_vertical=record(reader.Add(self,L::kBlurTargetVertical));
  const auto technique=[&](uint32_t effect) { return reader.Word(reader.Add(self,effect+L::kTechniqueOffset)); };
  in.mono_technique=technique(L::kMonoEffect); in.downsample_tone_technique=technique(L::kDownsampleToneEffect);
  in.downsample_technique=technique(L::kDownsampleEffect); in.tone_technique=technique(L::kToneEffect);
  in.blur_technique=technique(L::kBlurEffect); in.bloom_technique=technique(L::kBloomEffect);
  in.tone=tone; in.tone_source=tone?PostToneSource::LivePreviousFrame:PostToneSource::None;
  return in;
}
// Step 3: one native frame. Set only while 820B0B80's guest body runs.
struct PostFinishNativeRun {
  uint32_t self=0;
  PostFinishPlan plan;
  std::vector<PostIssueStep> steps;
  bool chain_attempted=false,chain_native=false,bloom_attempted=false,bloom_native=false;
  // hold_tone: this render holds the tone history (unlocked, no simulation
  // step: native_render_tick_frame false). tone_held: the loop dropped the
  // DownsampleTone pass because its record already held a resolve
  // (PostIssueWithoutPass); the pass is plan index held_pass.
  bool hold_tone=false,tone_held=false;
  size_t held_pass=0;
};
thread_local PostFinishNativeRun* post_finish_native_run=nullptr;
struct PostFinishRefusal:std::runtime_error {
  PostFinishFallback reason;
  PostFinishRefusal(PostFinishFallback why,const std::string& what):std::runtime_error(what),reason(why) {}
};
void CountPostFallback(PostFinishFallback reason,const std::string& detail) {
  auto& stats=FinishStats();
  const auto count=++stats.fallbacks[size_t(reason)];
  if(ShouldLogPostFinish(count))
    REXLOG_INFO("Native post finish fallback: reason={}, {} (count={}, native_frames={})",
      PostFinishFallbackName(reason),detail,count,stats.native_frames);
}
// The loop's guest frame, below the caller's: back chain at +0, one setter
// name record (+80, 32 bytes, the 820A62E8 layout) and its text (+176), the
// quad (+112, four big-endian x,y,u,v) and the setter vectors (+256, up to
// fifteen float4). Callees build their frames below it.
constexpr uint32_t kPostFrameBytes=512,kPostNameRecord=80,kPostQuad=112,kPostNameText=176,kPostVectors=256;
constexpr size_t kPostMaxName=63,kPostMaxVectors=60;
// lis -32168 / -16332: the 2D object 820B01E8, 820B04B8 and 820B0B80 draw through.
constexpr uint32_t kPost2DGlobal=0x8257C034u;
constexpr uint32_t kPostBloomCaller=0x820B0DD0u; // 820B0B80's return from 821A8F20
struct PostIssueFrame {
  uint32_t stack=0;
  size_t end=0;
  std::vector<uint32_t> links; // guest return address of each chain step's original call site
};
// The return addresses the original call sites leave in LR, per step.
uint32_t PostIssueLink(PostPassKind pass,PostIssueKind step,size_t setter) {
  using K=PostPassKind; using S=PostIssueKind;
  const auto pick=[&](uint32_t begin,std::initializer_list<uint32_t> setters,uint32_t activate,uint32_t draw,uint32_t end) {
    switch(step) {
      case S::BeginTarget: return begin;
      case S::Setter:
        if(setter>=setters.size()) throw std::runtime_error(std::format("{} has no setter {}",PostPassName(pass),setter));
        return *(setters.begin()+setter);
      case S::Activate: return activate;
      case S::Draw: return draw;
      case S::EndTarget: return end;
    }
    return 0u;
  };
  switch(pass) {
    case K::Downsample: return pick(0x820B02F4u,{0x820B0454u,0x820B047Cu},0x820B048Cu,0x820B04A4u,0x820B04ACu);
    case K::Mono: return pick(0x820B02F4u,{0x820B0400u,0x820B0428u},0x820B048Cu,0x820B04A4u,0x820B04ACu);
    case K::DownsampleTone: return pick(0x820B02F4u,{0x820B0324u,0x820B036Cu,0x820B03B0u},0x820B048Cu,0x820B04A4u,0x820B04ACu);
    case K::Tone: return pick(0x820B0578u,{0x820B059Cu,0x820B05E4u,0x820B062Cu},0x820B0654u,0x820B0668u,0x820B0670u);
    case K::BlurHorizontal: return pick(0x820B0780u,{0x820B07BCu,0x820B0814u},0x820B0888u,0x820B089Cu,0x820B08A4u);
    case K::BlurVertical: return pick(0x820B08D8u,{0x820B0908u,0x820B0958u},0x820B0980u,0x820B0994u,0x820B099Cu);
    case K::Bloom: return 0x821A8FE4u; // only its quad is issued, from inside 821A8F20
  }
  return 0u;
}
// 821A79B8 traps (twi) when the 2D object has no vertex record or its
// declaration record is the list's end. Checked before issuing anything.
void CheckPost2DObject(const GuestReader& reader) {
  const auto object=reader.Word(kPost2DGlobal);
  if(!object) throw PostFinishRefusal(PostFinishFallback::Draw2D,"no 2D object at 8257C034");
  const auto list=reader.Word(reader.Add(object,28));
  if(!list) throw PostFinishRefusal(PostFinishFallback::Draw2D,std::format("2D object {:#x} has no vertex record",object));
  const auto current=reader.Word(reader.Add(object,32));
  if(current==reader.Word(reader.Add(list,4)))
    throw PostFinishRefusal(PostFinishFallback::Draw2D,std::format("2D object {:#x} declaration record is its list's end",object));
  reader.Word(reader.Add(current,28));
}
// The history a held render keeps (both post routes; locks held): the
// DownsampleTone record `target` still registered with the planned texture
// and extent, and that texture's last resolve initialized and the one the
// record's own sampled texture holds (not a stale or replaced one).
bool NativeToneHistoryResolved(const Bridge& state,uint32_t target,uint32_t texture,int64_t width,int64_t height) {
  const auto found=state.render_targets.find(target);
  if(found==state.render_targets.end() || found->second.texture_handle!=texture) return false;
  const auto& sampled=found->second.native.sampled;
  if(int64_t(sampled.width)!=width || int64_t(sampled.height)!=height) return false;
  const auto resolved=state.textures.find(texture);
  return resolved!=state.textures.end() && resolved->second.content_valid && resolved->second.backend &&
    resolved->second.backend==sampled.backend && sampled.content_valid;
}
// Everything a native frame needs, checked before the first guest call: the
// plan rebuilt where 820B09B0 itself reads the records (after the resolve),
// every chain target registered natively at the planned extent and texture,
// every technique's shader pair registered, the 2D object and the stack.
PostIssueFrame PreparePostIssue(const GuestReader& reader,const PPCContext& ctx,PostFinishNativeRun& run) {
  using F=PostFinishFallback;
  if(ctx.r3.u32!=run.self) throw PostFinishRefusal(F::Owner,std::format("chain owner {:#x}, planned {:#x}",ctx.r3.u32,run.self));
  try {
    run.plan=BuildPostFinishPlan(ReadPostFinishInput(reader,run.self,run.plan.tone));
    run.steps=BuildPostFinishIssue(run.plan);
  } catch(const std::exception& error) { throw PostFinishRefusal(F::Plan,error.what()); }
  PostIssueFrame frame;
  const auto chain=[&] {
    frame.end=PostIssueChainEnd(run.plan,run.steps);
    frame.links.clear();
    for(size_t i=0;i<frame.end;++i) {
      const auto& step=run.steps[i];
      const auto& pass=run.plan.passes[step.pass];
      if(step.guest) throw PostFinishRefusal(F::Plan,"guest step inside the chain");
      if(step.kind==PostIssueKind::Setter) {
        const auto& call=pass.setters[step.setter];
        if(call.name.empty() || call.name.size()>kPostMaxName ||
           (call.kind==PostSetterKind::Vectors && (call.values.empty() || call.values.size()%4 || call.values.size()>kPostMaxVectors)) ||
           (call.kind==PostSetterKind::Sampler && call.values.size()!=1))
          throw PostFinishRefusal(F::Plan,std::format("{} setter {} does not fit the loop's frame",PostPassName(pass.kind),call.name));
      }
      frame.links.push_back(PostIssueLink(pass.kind,step.kind,step.setter));
    }
  };
  chain();
  CheckPost2DObject(reader);
  // Technique -> pass (+108) -> shader handles, as ObserveActivation reads them.
  std::vector<std::array<uint32_t,3>> programs;
  for(size_t p=0;p+1<run.plan.passes.size();++p) {
    const auto technique=run.plan.passes[p].technique;
    const auto record=reader.Word(reader.Add(technique,108));
    programs.push_back({technique,reader.Word(reader.Word(record)),reader.Word(reader.Add(reader.Word(reader.Add(record,4)),4))});
  }
  if(ctx.r1.u32<kPostFrameBytes+4096) throw PostFinishRefusal(F::Stack,std::format("stack {:#x}",ctx.r1.u32));
  frame.stack=(ctx.r1.u32-kPostFrameBytes)&~15u;
  try { reader.WritableBytes(frame.stack,kPostFrameBytes,8); }
  catch(const std::exception& error) { throw PostFinishRefusal(F::Stack,error.what()); }
  auto& state=State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  for(size_t p=0;p+1<run.plan.passes.size();++p) {
    const auto& pass=run.plan.passes[p];
    const auto found=state.render_targets.find(pass.target);
    if(found==state.render_targets.end())
      throw PostFinishRefusal(F::Target,std::format("pass {} ({}) target {:#x} is not a registered native target",p,PostPassName(pass.kind),pass.target));
    const auto& sampled=found->second.native.sampled;
    if(found->second.texture_handle!=pass.target_texture || int64_t(sampled.width)!=pass.target_width || int64_t(sampled.height)!=pass.target_height)
      throw PostFinishRefusal(F::Target,std::format("pass {} ({}) target {:#x} is native {}x{} texture {:#x}, planned {}x{} texture {:#x}",
        p,PostPassName(pass.kind),pass.target,sampled.width,sampled.height,found->second.texture_handle,
        pass.target_width,pass.target_height,pass.target_texture));
  }
  for(const auto& [technique,vertex,pixel]:programs)
    if(!state.shaders.contains(vertex) || !state.shaders.contains(pixel))
      throw PostFinishRefusal(F::Shaders,std::format("technique {:#x} shaders {:#x}/{:#x} are not registered natively",technique,vertex,pixel));
  // A held render drops the DownsampleTone pass when its record already
  // holds a resolve to keep (the full frame's NativePostHistory::Hold, on the
  // same registry); without one it draws, as the first frame must.
  run.tone_held=false;
  if(run.hold_tone) {
    std::optional<size_t> history;
    try { history=PostToneHistoryPass(run.plan); }
    catch(const std::exception& error) { throw PostFinishRefusal(F::Plan,error.what()); }
    if(history) {
      const auto& pass=run.plan.passes[*history];
      if(NativeToneHistoryResolved(state,pass.target,pass.target_texture,pass.target_width,pass.target_height)) {
        try { run.steps=PostIssueWithoutPass(run.plan,run.steps,*history); }
        catch(const std::exception& error) { throw PostFinishRefusal(F::Plan,error.what()); }
        chain();
        run.tone_held=true; run.held_pass=*history;
      } else ++FinishStats().tone_unheld;
    }
  }
  return frame;
}
// The 820A62E8 layout: 16+ byte names point at a copy in the loop's frame.
void StorePostName(const GuestReader& reader,uint32_t record,uint32_t text,const std::string& name) {
  const auto size=uint32_t(name.size());
  for(uint32_t i=0;i<8;++i) reader.StoreWord(record+i*4,0);
  const auto data=size>=16 ? text : record+4;
  for(uint32_t i=0;i<size;++i) reader.StoreByte(data+i,uint8_t(name[i]));
  reader.StoreByte(data+size,0);
  if(size>=16) reader.StoreWord(record+4,text);
  reader.StoreWord(record+20,size);
  reader.StoreWord(record+24,size>=16?size:15);
}
void IssuePostSetter(const GuestReader& reader,PPCContext& work,uint8_t* base,uint32_t stack,const PostSetterCall& call) {
  StorePostName(reader,stack+kPostNameRecord,stack+kPostNameText,call.name);
  work.r3.u64=call.effect; work.r4.u64=stack+kPostNameRecord;
  switch(call.kind) {
    case PostSetterKind::Vectors:
      for(size_t i=0;i<call.values.size();++i)
        reader.StoreWord(stack+kPostVectors+uint32_t(i)*4,std::bit_cast<uint32_t>(call.values[i]));
      work.r5.u64=stack+kPostVectors; work.r6.u64=call.values.size()/4;
      sub_821BCD58(work,base); break;
    case PostSetterKind::Texture:
      work.r5.u64=call.texture; sub_821BCE98(work,base); break;
    case PostSetterKind::Sampler:
      work.f1.f64=double(call.values[0]);
      work.r5.u64=call.words[0]; work.r6.u64=call.words[1]; work.r7.u64=call.words[2];
      sub_821BCF28(work,base); break;
  }
}
// 821A79B8(2D object,13 = quad list,vertices,1), exactly as the chain calls it.
// `entered` is set just before the guest call, after every write that can throw.
void IssuePostQuad(const GuestReader& reader,PPCContext& work,uint8_t* base,uint32_t stack,const PostQuad& quad,bool* entered=nullptr) {
  for(uint32_t i=0;i<16;++i) reader.StoreWord(stack+kPostQuad+i*4,std::bit_cast<uint32_t>(quad[i]));
  work.r3.u64=reader.Word(kPost2DGlobal); work.r4.u64=13; work.r5.u64=stack+kPostQuad; work.r6.u64=1;
  if(entered) *entered=true;
  sub_821A79B8(work,base);
}
// After the draw, before the end resolves it: CanInitializeReductionTarget's
// verdict as the native post path left it on the target.
void CountPostTargetInitialized(uint32_t target) {
  auto& state=State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  const auto found=state.render_targets.find(target);
  if(found==state.render_targets.end() || !found->second.native.content_valid) ++FinishStats().uninitialized;
}
// The 820B09B0 replacement. False: nothing of the chain remains issued and
// the caller runs the original.
bool RunNativePostChain(PPCContext& ctx,uint8_t* base,PostFinishNativeRun& run) {
  using F=PostFinishFallback;
  PostIssueFrame frame;
  try { frame=PreparePostIssue(GuestReader(base),ctx,run); }
  catch(const PostFinishRefusal& refusal) { CountPostFallback(refusal.reason,refusal.what()); return false; }
  catch(const std::exception& error) { CountPostFallback(F::Exception,error.what()); return false; }
  const GuestReader reader(base);
  auto work=ctx;
  uint32_t open=0;
  size_t draws=0;
  try {
    reader.StoreWord(frame.stack,ctx.r1.u32);
    work.r1.u64=frame.stack;
    for(size_t i=0;i<frame.end;++i) {
      const auto& step=run.steps[i];
      const auto& pass=run.plan.passes[step.pass];
      work.lr=frame.links[i];
      switch(step.kind) {
        case PostIssueKind::BeginTarget:
          work.r3.u64=pass.target; open=pass.target; sub_821B8828(work,base); break;
        case PostIssueKind::Setter:
          IssuePostSetter(reader,work,base,frame.stack,pass.setters[step.setter]); break;
        case PostIssueKind::Activate:
          work.r3.u64=pass.technique; sub_821B94E8(work,base); break;
        case PostIssueKind::Draw:
          IssuePostQuad(reader,work,base,frame.stack,*pass.quad); ++draws;
          CountPostTargetInitialized(pass.target); break;
        case PostIssueKind::EndTarget:
          open=0; work.r3.u64=pass.target; sub_821B88B0(work,base); break;
      }
    }
  } catch(const std::exception& error) {
    // Close the open scope so the guest and native target stacks stay
    // balanced; the original then reruns the whole chain for this frame.
    if(open) try {
      work.r1.u64=frame.stack; work.r3.u64=open; work.lr=0x820B04ACu; sub_821B88B0(work,base);
    } catch(const std::exception& close) { REXLOG_ERROR("Native post finish: closing target {:#x}: {}",open,close.what()); }
    CountPostFallback(F::Midway,std::format("{} after {} native draw(s)",error.what(),draws));
    run.tone_held=false;  // the original reruns the whole chain, DownsampleTone included
    return false;
  }
  run.chain_native=true;
  if(run.tone_held) ++FinishStats().tone_held;
  return true;
}
// The bloom quad 821A8F20 would draw, issued from the plan. False before any
// guest call: the caller runs the original 821A8F20.
bool IssueNativePostBloom(PPCContext& ctx,uint8_t* base,PostFinishNativeRun& run) {
  using L=PostFinishLayout;
  bool issued=false;
  try {
    const GuestReader reader(base);
    const auto& bloom=run.plan.passes.back();
    const auto owner=reader.Word(L::kOwnerGlobal);
    const auto width=std::bit_cast<int32_t>(reader.Word(reader.Add(owner,L::kOwnerWidth)));
    const auto height=std::bit_cast<int32_t>(reader.Word(reader.Add(owner,L::kOwnerHeight)));
    if(bloom.kind!=PostPassKind::Bloom || !bloom.quad || width!=bloom.target_width || height!=bloom.target_height)
      throw PostFinishRefusal(PostFinishFallback::Bloom,std::format("screen {}x{}, planned {}x{}",width,height,bloom.target_width,bloom.target_height));
    if(ctx.r3.u32!=reader.Word(kPost2DGlobal)) throw PostFinishRefusal(PostFinishFallback::Bloom,"821A8F20 called for another 2D object");
    CheckPost2DObject(reader);
    if(ctx.r1.u32<kPostFrameBytes+4096) throw PostFinishRefusal(PostFinishFallback::Bloom,"stack");
    const auto stack=(ctx.r1.u32-kPostFrameBytes)&~15u;
    reader.WritableBytes(stack,kPostFrameBytes,8);
    auto work=ctx;
    reader.StoreWord(stack,ctx.r1.u32);
    work.r1.u64=stack; work.lr=PostIssueLink(PostPassKind::Bloom,PostIssueKind::Draw,0);
    IssuePostQuad(reader,work,base,stack,*bloom.quad,&issued);
  } catch(const std::exception& error) {
    CountPostFallback(PostFinishFallback::Bloom,error.what());
    return issued; // once 821A79B8 has been entered, the original must not draw again
  }
  run.bloom_native=true;
  return true;
}
}
}
REX_EXTERN(__imp__sub_820B0B80);
REX_HOOK_RAW(sub_820B0B80) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderFinish);
  const bool audit=REXCVAR_GET(edf_native_post_finish_audit),native=EDF_NATIVE_FLAG(post_finish);
  if((!native && !audit) || !EDF_NATIVE_FLAG(host) ||
     !EDF_NATIVE_FLAG(shader_bridge) || !EDF_NATIVE_FLAG(seam_draws) ||
     edf::native::post_finish_recorder || edf::native::post_finish_native_run) {
    __imp__sub_820B0B80(ctx,base); return;
  }
  auto& stats=edf::native::FinishStats();
  const auto self=ctx.r3.u32;
  std::optional<edf::native::PostFinishPlan> plan;
  try {
    const edf::native::GuestReader reader(base);
    const auto input=edf::native::ReadPostFinishInput(reader,self,stats.tone);
    plan=edf::native::BuildPostFinishPlan(input);
    if(++stats.planned<=2) {
      std::string first,second;
      for(const auto& r:input.first) first+=std::format(" {}x{}/texel={},{}",r.width,r.height,r.texel_x,r.texel_y);
      for(const auto& r:input.second) second+=std::format(" {}x{}",r.width,r.height);
      REXLOG_INFO("Native post finish plan: owner={:#x}, passes={}, screen={}x{}, first=[{} ], second=[{} ], blur={}x{}, tone_source={}",
        self,plan->passes.size(),input.screen_width,input.screen_height,first,second,input.blur.width,input.blur.height,
        input.tone?"live-previous-frame":"none");
    }
  } catch(const std::exception& error) {
    plan.reset();
    if(edf::native::ShouldLogPostFinish(++stats.rejected))
      REXLOG_INFO("Native post finish fallback: {} (rejected={}, planned={})",error.what(),stats.rejected,stats.planned);
    if(native) edf::native::CountPostFallback(edf::native::PostFinishFallback::Plan,error.what());
  }
  const bool ab_native_side=edf::native::NativeAbNativeSide();
  const auto mode=edf::native::ChoosePostFinishMode({native,audit,ab_native_side,plan.has_value(),
    stats.last_audit_clean,stats.last_frame_native});
  if(plan && native && mode!=edf::native::PostFinishMode::Native) {
    if(!ab_native_side) ++stats.ab_guest;
    else if(audit && !stats.last_audit_clean && !stats.last_frame_native)
      edf::native::CountPostFallback(edf::native::PostFinishFallback::AuditUnclean,
        stats.audited?"latest audited guest frame had mismatches":"no audited guest frame yet");
  }
  if(mode==edf::native::PostFinishMode::Native) {
    edf::native::PostFinishNativeRun run;
    run.self=self; run.plan=*plan;
    // The guest-helper and frame-dispatch routes' tick gate (the 821A5080
    // hook; the full frame's host sets it before its fallback call): a held
    // render keeps the tone history, as the full-frame post does.
    run.hold_tone=!native_render_tick_frame;
    // Native frames with the audit on are recorded by the same observers:
    // a self-audit that the loop issued the plan, never counted as the guest
    // audit that gates the next native frame.
    std::optional<edf::native::PostFinishRecorder> recorder;
    if(audit) {
      recorder.emplace(); recorder->self=self;
      for(const auto& pass:plan->passes) {
        if(pass.target) recorder->targets.insert(pass.target);
        recorder->techniques.insert(pass.technique);
      }
    }
    {
      struct Scope { ~Scope() { edf::native::post_finish_native_run=nullptr; edf::native::post_finish_recorder=nullptr; } } scope;
      edf::native::post_finish_native_run=&run;
      if(recorder) edf::native::post_finish_recorder=&*recorder;
      __imp__sub_820B0B80(ctx,base);
    }
    if(!run.chain_attempted)
      edf::native::CountPostFallback(edf::native::PostFinishFallback::Exception,"820B0B80 did not reach 820B09B0");
    stats.last_frame_native=run.chain_native;
    if(run.chain_native) {
      ++stats.native_frames;
      if(run.bloom_native) ++stats.native_bloom;
      if(edf::native::ShouldLogPostFinish(stats.native_frames)) {
        std::string reasons;
        for(size_t r=0;r<stats.fallbacks.size();++r) if(stats.fallbacks[r])
          reasons+=std::format(" {}={}",edf::native::PostFinishFallbackName(edf::native::PostFinishFallback(r)),stats.fallbacks[r]);
        REXLOG_INFO("Native post finish: native_frames={}, native_bloom={}, passes={}, uninitialized_targets={}, ab_guest={}, tone_held={}, tone_unheld={}, fallbacks=[{} ]",
          stats.native_frames,stats.native_bloom,run.plan.passes.size(),stats.uninitialized,stats.ab_guest,
          stats.tone_held,stats.tone_unheld,reasons);
      }
    }
    if(recorder && run.chain_native) try {
      // A held render issued the plan without its DownsampleTone pass.
      const auto result=edf::native::ComparePostFinish(
        run.tone_held?edf::native::PostPlanWithoutPass(run.plan,run.held_pass):run.plan,recorder->seen);
      ++stats.self_audited;
      if(result.mismatches.empty() && !recorder->errors) ++stats.self_clean;
      for(const auto& mismatch:result.mismatches)
        if(edf::native::ShouldLogPostFinish(++stats.self_mismatches))
          REXLOG_WARN("Native post finish self-audit mismatch: frame={}, pass={}, {} (mismatches={})",
            stats.self_audited,mismatch.pass,mismatch.what,stats.self_mismatches);
      if(edf::native::ShouldLogPostFinish(stats.self_audited))
        REXLOG_INFO("Native post finish self-audit: native_frames={}, clean={}, mismatches={}, recorder_errors={}",
          stats.self_audited,stats.self_clean,stats.self_mismatches,recorder->errors);
    } catch(const std::exception& error) {
      REXLOG_ERROR("Native post finish self-audit: {}",error.what());
    }
    return;
  }
  stats.last_frame_native=false;
  if(mode!=edf::native::PostFinishMode::Audit) { __imp__sub_820B0B80(ctx,base); return; }
  edf::native::PostFinishRecorder recorder;
  recorder.self=self;
  for(const auto& pass:plan->passes) {
    if(pass.target) recorder.targets.insert(pass.target);
    recorder.techniques.insert(pass.technique);
  }
  {
    struct Scope { ~Scope() { edf::native::post_finish_recorder=nullptr; } } scope;
    edf::native::post_finish_recorder=&recorder;
    __imp__sub_820B0B80(ctx,base);
  }
  stats.last_audit_clean=false;
  try {
    const auto result=edf::native::ComparePostFinish(*plan,recorder.seen);
    ++stats.audited;
    stats.last_audit_clean=result.mismatches.empty() && !recorder.errors;
    if(stats.last_audit_clean) ++stats.clean;
    stats.unobserved+=result.native_unobserved;
    if(recorder.errors && edf::native::ShouldLogPostFinish(++stats.recorder_errors))
      REXLOG_WARN("Native post finish audit recorder: frame={}, errors={}, first={}",stats.audited,recorder.errors,recorder.first_error);
    for(const auto& mismatch:result.mismatches)
      if(edf::native::ShouldLogPostFinish(++stats.mismatches))
        REXLOG_WARN("Native post finish mismatch: frame={}, pass={}, {} (mismatches={})",
          stats.audited,mismatch.pass,mismatch.what,stats.mismatches);
    if(edf::native::ShouldLogPostFinish(stats.audited))
      REXLOG_INFO("Native post finish audit: frames={}, clean={}, mismatches={}, native_unobserved_passes={}, tone_compared={}, tone_source={}, recorder_errors={}",
        stats.audited,stats.clean,stats.mismatches,stats.unobserved,result.tone_compared,
        plan->tone?"live-previous-frame":"none",stats.recorder_errors);
    if(const auto tone=edf::native::PostObservedTone(*plan,recorder.seen)) stats.tone=tone;
  } catch(const std::exception& error) {
    REXLOG_ERROR("Native post finish audit: {}",error.what());
  }
}
// Step 3 seams. Each acts only inside a native 820B0B80 frame on this thread;
// otherwise, and on any refusal before issuing, the original runs.
REX_EXTERN(__imp__sub_820B09B0);
REX_HOOK_RAW(sub_820B09B0) {
  auto* run=edf::native::post_finish_native_run;
  if(!run || run->chain_attempted) { __imp__sub_820B09B0(ctx,base); return; }
  run->chain_attempted=true;
  if(!edf::native::RunNativePostChain(ctx,base,*run)) __imp__sub_820B09B0(ctx,base);
}
REX_EXTERN(__imp__sub_821A8F20);
REX_HOOK_RAW(sub_821A8F20) {
  auto* run=edf::native::post_finish_native_run;
  if(!run || !run->chain_native || run->bloom_attempted || uint32_t(ctx.lr)!=edf::native::kPostBloomCaller) {
    __imp__sub_821A8F20(ctx,base); return;
  }
  run->bloom_attempted=true;
  if(!edf::native::IssueNativePostBloom(ctx,base,*run)) __imp__sub_821A8F20(ctx,base);
}
namespace edf::native {
namespace {
// Full-frame post: the finish stage 820B0B80 with zero guest calls. The pure
// half (tone source, plan -> draws) is native_full_frame_post.cpp; this is its
// sink. Guest memory is only read here: no guest function runs, no device
// mirror is written, and the target scopes 821B8828/821B88B0 and activations
// 821B94E8 are replaced by the native target stack and named bindings.
struct FullFramePostStats { uint64_t frames=0,draws=0,failures=0,uninitialized=0,history_held=0; };
FullFramePostStats& FullPostStats() { static FullFramePostStats stats; return stats; }
// The engine's big-endian float4 register stream, as the setters leave it.
std::vector<uint8_t> GuestFloatBytes(std::span<const float> values) {
  std::vector<uint8_t> bytes(values.size()*4);
  for(size_t i=0;i<values.size();++i) {
    const auto bits=std::bit_cast<uint32_t>(values[i]);
    for(size_t b=0;b<4;++b) bytes[i*4+b]=uint8_t(bits>>(24-b*8));
  }
  return bytes;
}
// Called with the submission and registry locks held.
class BridgePostSink final:public NativePostSink {
 public:
  BridgePostSink(Bridge& state,const GuestReader& reader,uint32_t owner,uint32_t device,float center)
    : state_(state),reader_(reader),owner_(owner),device_(device),center_(center) {}
  // The history a held frame keeps: the DownsampleTone record still registered
  // with the planned texture, and that texture's last resolve initialized and
  // the one the record's own sampled texture holds (not a stale or replaced one).
  bool HasToneHistory(const NativePostDraw& draw) const override {
    if(draw.output || draw.kind!=PostPassKind::DownsampleTone) return false;
    return NativeToneHistoryResolved(state_,draw.target,draw.target_texture,draw.width,draw.height);
  }
  void Draw(const NativePostDraw& draw) override {
    auto& state=state_;
    // Technique -> pass (+108) -> shader handles, as ObserveActivation reads them.
    const auto pass=reader_.Word(reader_.Add(draw.technique,108));
    const auto vertex=reader_.Word(reader_.Word(pass));
    const auto pixel=reader_.Word(reader_.Add(reader_.Word(reader_.Add(pass,4)),4));
    const auto vs_found=state.shaders.find(vertex),ps_found=state.shaders.find(pixel);
    if(vs_found==state.shaders.end() || ps_found==state.shaders.end())
      throw std::runtime_error(std::format("{} technique {:#x} shaders {:#x}/{:#x} are not registered natively",
        PostPassName(draw.kind),draw.technique,vertex,pixel));
    auto& vs=VertexBindingsForDraw(vs_found->second,false);
    auto& ps=*ps_found->second.bindings;
    NativeRenderTarget* target=nullptr;
    if(draw.output) target=&state.scenes.at(owner_).output;
    else {
      const auto found=state.render_targets.find(draw.target);
      if(found==state.render_targets.end())
        throw std::runtime_error(std::format("pass {} ({}) target {:#x} is not a registered native target",draw.pass,PostPassName(draw.kind),draw.target));
      if(found->second.texture_handle!=draw.target_texture)
        throw std::runtime_error(std::format("pass {} ({}) target {:#x} texture {:#x}, planned {:#x}",draw.pass,PostPassName(draw.kind),
          draw.target,found->second.texture_handle,draw.target_texture));
      target=&found->second.native;
    }
    if(int64_t(target->sampled.width)!=draw.width || int64_t(target->sampled.height)!=draw.height)
      throw std::runtime_error(std::format("pass {} ({}) target is {}x{}, planned {}x{}",draw.pass,PostPassName(draw.kind),
        target->sampled.width,target->sampled.height,draw.width,draw.height));
    // Constants by name: a name the native compiler optimized out is skipped.
    for(const auto& constant:draw.constants) ps.SetGuestFloatRegisters(constant.name,GuestFloatBytes(constant.values));
    // Textures and samplers by name. Each sampler is the device state the
    // guest's activation leaves in the record's slot (ResolveNativePostSamplers),
    // decoded as the guest route decodes it, never read back from the shared
    // bindings: the post samples with the game's states from its first frame,
    // whatever ran before it.
    std::vector<std::pair<const NativePostTexture*,NativeBackendSampler*>> inputs;
    for(const auto& texture:draw.textures) {
      const auto* sampler=FindPostSamplerState(draw,texture.name);
      if(!sampler)
        throw std::runtime_error(std::format("{} technique {:#x} has no texture record for {}",
          PostPassName(draw.kind),draw.technique,texture.name));
      inputs.emplace_back(&texture,SamplerLocked(state,NativeFilteringKey(sampler->words,
        REXCVAR_GET(edf_native_anisotropic_filtering))));
    }
    ps.BeginResourceUpdate();
    try {
      for(const auto& [texture,sampler]:inputs) {
        // Unresolved history (the first frame's m_OldTone) binds nothing, and
        // CanInitializeReductionTarget then leaves the target invalid.
        const auto found=state.textures.find(texture->handle);
        const std::shared_ptr<NativeBackendTexture> missing;
        const auto& view=found==state.textures.end() || !found->second.content_valid ? missing : found->second.backend;
        if(!ps.TrySetTexture(texture->name,view)) continue;
        if(!ps.TrySetSampler(texture->name,sampler)) throw std::runtime_error("native post sampler has no binding: "+texture->name);
      }
      ps.EndResourceUpdate();
    } catch(...) { ps.ClearTextures(); ps.ClearSamplers(); throw; }
    // Reading the target's own RESOLVED texture is the guest's order for the
    // tone history and BlurH (NativePostOwnResolveAllowed; BuildNativePostFrame
    // refuses any other). The surface being drawn is never sampled.
    if(target->backend_surface)
      if(auto* surface=target->backend_surface->texture(); surface && ps.UsesTexture(*surface))
        throw std::runtime_error(std::format("pass {} ({}) samples the surface it draws into",draw.pass,PostPassName(draw.kind)));
    if(target->sampled.backend && ps.UsesTexture(*target->sampled.backend) && !NativePostOwnResolveAllowed(draw))
      throw std::runtime_error(std::format("pass {} ({}) samples its own target",draw.pass,PostPassName(draw.kind)));
    // The target scope 821B8828 would open, on the native stack only.
    struct Scope {
      Bridge& state; bool open=false; uint32_t target=0;
      ~Scope() {
        if(!open) return;
        state.active_target=state.target_stack.back().second; state.target_stack.pop_back();
        BindActiveTarget(state);
      }
    } scope{state};
    if(draw.output) { state.active_target=0; state.active_scene=0; state.active_output=owner_; }
    else {
      state.target_stack.emplace_back(draw.target,state.active_target); state.active_target=draw.target;
      scope.open=true; scope.target=draw.target;
    }
    BindActiveTarget(state);
    const auto vertices=GuestFloatBytes(draw.quad);
    const auto viewport=MakeNativeViewport(0,0,uint32_t(draw.width),uint32_t(draw.height),0,1,false,{0,0,0,0});
    const bool initialized=CanInitializeReductionTarget(vs.shader(),ps.shader(),vertices,viewport,kNativeOpaqueCopyState,
      uint32_t(draw.width),uint32_t(draw.height),ps.HasAllTextureInputs());
    auto shifted=viewport;
    if(initialized && center_!=0.f) { shifted.viewport.TopLeftX+=center_; shifted.viewport.TopLeftY+=center_; }
    auto& quads=vs_found->second.quads;
    if(!quads) quads=std::make_unique<QuadStream>(state.device.Get(),vs.shader());
    auto& recorder=RecordDrawSetup(state,reader_,device_,{vs,ps,shifted,kNativeOpaqueCopyState,
      QuadStream::Layout(),kNativeQuadLayoutId,uint64_t(vertex)<<1,pixel,NativeBackendTopology::TriangleList});
    quads->Draw(EnsureSceneBackendLocked(state),recorder,vertices);
    target->content_valid=initialized;
    if(!initialized) ++FullPostStats().uninitialized;
    ++FullPostStats().draws;
    if(draw.output) return;
    // The resolve 821B88B0 would perform: EndRenderTarget's, on the recorder.
    if(state.context) {
      ID3D11ShaderResourceView* empty[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
      state.context->PSSetShaderResources(0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,empty);
      state.context->VSSetShaderResources(0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,empty);
    }
    auto& registered=state.render_targets.at(draw.target);
    ResolveNativeRenderTarget(SceneRecorderLocked(state),registered.native);
    ++state.bind_generation;
    state.recorded={};
    state.textures.insert_or_assign(registered.texture_handle,registered.native.sampled);
  }
 private:
  Bridge& state_;
  const GuestReader& reader_;
  uint32_t owner_,device_;
  float center_;
};
}
bool RecordNativeFullFramePost(uint8_t* base,uint32_t self,bool resolve_scene,std::string* error,NativePostHistory history) {
  auto& stats=FullPostStats();
  try {
    if(!EDF_NATIVE_FLAG(shader_bridge) || !EDF_NATIVE_FLAG(seam_draws))
      throw std::runtime_error("the full-frame post needs the shader bridge and recorded (seam) draws");
    const GuestReader reader(base);
    const GuestPostMemory memory(reader);
    const auto owner=reader.Word(PostFinishLayout::kOwnerGlobal);
    auto& state=State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    const auto found=state.scenes.find(owner);
    if(found==state.scenes.end()) throw std::runtime_error(std::format("screen owner {:#x} has no native scene",owner));
    auto& scene=found->second;
    // The 8219C930 hook, mode 1: close the scene and resolve it to owner+104.
    if(resolve_scene) {
      if(state.active_scene==owner) { state.active_scene=0; BindActiveTarget(state); }
      ResolveScene(reader,state,owner);
    }
    const auto input=reader.Word(reader.Add(owner,PostFinishLayout::kOwnerSceneTexture));
    if(const auto resolved=state.textures.find(input);resolved==state.textures.end() || !resolved->second.content_valid)
      throw std::runtime_error(std::format("HDR scene texture {:#x} is not resolved",input));
    // Its ordinary output, as the hook's tail creates it: owner+112.
    const auto surface=reader.Word(reader.Add(owner,112));
    const auto& creation=state.surface_creations.at(surface);
    if(creation.msaa || creation.width!=scene.color.sampled.width || creation.height!=scene.color.sampled.height ||
       (creation.format!=0x1a220186 && creation.format!=0x18280186))
      throw std::runtime_error("unsupported ordinary output surface contract");
    if(scene.output_surface!=surface || !scene.output.backend_surface) {
      scene.output=CreateNativeRenderTarget(EnsureSceneBackendLocked(state),creation.width,creation.height,DXGI_FORMAT_R8G8B8A8_UNORM);
      scene.output_surface=surface;
    }
    scene.output.content_valid=false;
    const auto device=reader.Word(reader.Add(owner,8));
    const float center=REXCVAR_GET(edf_native_pixel_centers) ? GuestPixelCenterOffset(ReadVertexCenterWord(reader,device)) : 0.f;
    // The image defaults the post's sampler base derives from, once.
    static bool sampler_image_checked=false;
    if(!sampler_image_checked) { CheckNativePostSamplerImage(memory); sampler_image_checked=true; }
    BridgePostSink sink(state,reader,owner,device,center);
    const auto frame=RecordNativePost(sink,memory,self,history);
    if(frame.history_held) ++stats.history_held;
    // The derived sampler table, once: what the confirming shadow run compares.
    if(!stats.frames) for(const auto& draw:frame.draws) for(const auto& sampler:draw.sampler_states) {
      const auto desc=DecodeNativeGuestSampler(NativeFilteringKey(sampler.words,REXCVAR_GET(edf_native_anisotropic_filtering)));
      REXLOG_INFO("Native full-frame post sampler: pass={} ({}), name={}, slot={}, words={:#x},{:#x},{:#x},{:#x}, "
        "filter={}/{}/{}, address={}/{}/{}, lod={}..{}, bias={}",draw.pass,PostPassName(draw.kind),sampler.name,sampler.slot,
        sampler.words[0],sampler.words[1],sampler.words[2],sampler.words[3],uint32_t(desc.min),uint32_t(desc.mag),uint32_t(desc.mip),
        uint32_t(desc.u),uint32_t(desc.v),uint32_t(desc.w),desc.min_lod,desc.max_lod,desc.mip_lod_bias);
    }
    // Leave the composite as the active ordinary output: the end-frame
    // publication (8219C840 hook) presents it.
    state.active_target=0; state.active_scene=0; state.active_output=owner;
    BindActiveTarget(state);
    if(ShouldLogPostFinish(++stats.frames))
      REXLOG_INFO("Native full-frame post: frames={}, draws={}, owner={:#x}, tone={},{},{}, uninitialized={}, failures={}, history_held={}",
        stats.frames,frame.draws.size(),owner,frame.tone.middle_gray[0],frame.tone.luminance_white[0],frame.tone.tone_map[0],
        stats.uninitialized,stats.failures,stats.history_held);
    return true;
  } catch(const std::exception& failure) {
    if(ShouldLogPostFinish(++stats.failures)) REXLOG_ERROR("Native full-frame post: {} (failures={})",failure.what(),stats.failures);
    if(error) *error=failure.what();
    return false;
  }
}
}
// Observers for the finish audit. Each records only while an audited 820B0B80
// is on this thread's stack, and only for the post owner's own effects,
// targets and techniques; the original always runs, and a failed read is
// counted, never thrown into guest code.
#define EDF_POST_FINISH_OBSERVER(address,...) \
  REX_EXTERN(__imp__sub_##address); \
  REX_HOOK_RAW(sub_##address) { \
    if(auto* recorder=edf::native::post_finish_recorder) { \
      try { __VA_ARGS__ } catch(const std::exception& error) { recorder->Fail(error); } \
    } \
    __imp__sub_##address(ctx,base); \
  }
// 821BCD58(effect,name,float4*,count)
EDF_POST_FINISH_OBSERVER(821BCD58,
  if(recorder->Effect(ctx.r3.u32)) {
    const edf::native::GuestReader reader(base);
    edf::native::PostSetterCall call;
    call.effect=ctx.r3.u32; call.name=edf::native::ReadGuestStdString(reader,ctx.r4.u32);
    call.kind=edf::native::PostSetterKind::Vectors;
    if(ctx.r6.u32>64) throw std::runtime_error("post vector setter count");
    reader.Bytes(ctx.r5.u32,size_t(ctx.r6.u32)*16);
    for(uint32_t i=0;i<ctx.r6.u32*4;++i) call.values.push_back(std::bit_cast<float>(reader.Word(ctx.r5.u32+i*4)));
    recorder->seen.Setter(std::move(call));
  })
// 821BCE98(effect,name,texture)
EDF_POST_FINISH_OBSERVER(821BCE98,
  if(recorder->Effect(ctx.r3.u32)) {
    const edf::native::GuestReader reader(base);
    edf::native::PostSetterCall call;
    call.effect=ctx.r3.u32; call.name=edf::native::ReadGuestStdString(reader,ctx.r4.u32);
    call.kind=edf::native::PostSetterKind::Texture; call.texture=ctx.r5.u32;
    recorder->seen.Setter(std::move(call));
  })
// 821BCF28(effect,name,f1,r5,r6,r7)
EDF_POST_FINISH_OBSERVER(821BCF28,
  if(recorder->Effect(ctx.r3.u32)) {
    const edf::native::GuestReader reader(base);
    edf::native::PostSetterCall call;
    call.effect=ctx.r3.u32; call.name=edf::native::ReadGuestStdString(reader,ctx.r4.u32);
    call.kind=edf::native::PostSetterKind::Sampler;
    call.values={float(ctx.f1.f64)}; call.words={ctx.r5.u32,ctx.r6.u32,ctx.r7.u32};
    recorder->seen.Setter(std::move(call));
  })
// 821B94E8(technique): closes a pass. Unrelated activations inside the scope
// (no pending setters or target) are not passes of this chain.
EDF_POST_FINISH_OBSERVER(821B94E8,
  auto& pending=recorder->seen.pending;
  if(recorder->techniques.contains(ctx.r3.u32) && (pending.target || !pending.setters.empty()))
    recorder->seen.Activate(ctx.r3.u32);)
// 821A79B8(device,primitive,vertices,count): the pass quad, four (x,y,u,v).
EDF_POST_FINISH_OBSERVER(821A79B8,
  if(recorder->seen.quad_armed) {
    const edf::native::GuestReader reader(base);
    reader.Bytes(ctx.r5.u32,64);
    edf::native::PostQuad quad{};
    for(uint32_t i=0;i<16;++i) quad[i]=std::bit_cast<float>(reader.Word(ctx.r5.u32+i*4));
    recorder->seen.Quad(quad);
  })
#undef EDF_POST_FINISH_OBSERVER
REX_EXTERN(sub_821B94E8);
REX_EXTERN(__imp__sub_821B8828);
REX_HOOK_RAW(sub_821B8828) {
  const auto owner = ctx.r3.u32;
  if(auto* recorder=edf::native::post_finish_recorder; recorder && recorder->targets.contains(owner))
    recorder->seen.BeginTarget(owner);
  __imp__sub_821B8828(ctx,base);
  if (EDF_NATIVE_FLAG(shader_bridge)) {
    try { edf::native::BeginRenderTarget(owner); }
    catch (const std::exception& error) { REXLOG_ERROR("Native render target begin: {}",error.what()); }
  }
}

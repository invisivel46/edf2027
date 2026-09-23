#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// Native plan of the finish/post stage 820B0B80: what its passes will set,
// derived from the post owner's records before the guest body runs.
// 820B0B80: resolve, 2D scope, post chain 820B09B0, bloom inputs on this+564.
// 820B09B0: five downsamples (820B01E8 mode 0) over this+188..396, Mono (mode 1)
// into the second pyramid's first record, then its remaining records (mode 0,
// the last mode 2 with tone history), then 820B04B8: Tone, horizontal and
// vertical GaussBlur. Pure: the caller reads guest memory, this only derives.
namespace edf::native {
struct PostFinishLayout {
  static constexpr uint32_t kOwnerGlobal=0x8257BFB4u; // [..]+84/+88 screen ints, +104 scene texture
  static constexpr uint32_t kOwnerWidth=84,kOwnerHeight=88,kOwnerSceneTexture=104;
  static constexpr uint32_t kRecordStride=52,kFirstPyramid=188,kFirstPyramidCount=5;
  static constexpr uint32_t kSecondPyramid=552,kSecondCount=560,kMaxSecondCount=16;
  static constexpr uint32_t kBlurTarget=500,kBlurTargetVertical=448;
  static constexpr uint32_t kRecordTexture=4,kRecordWidth=24,kRecordHeight=28,kRecordTexelX=32,kRecordTexelY=36;
  // 820B01E8/820B04B8/820B0B80: each effect's technique word sits at effect+16.
  static constexpr uint32_t kMonoEffect=48,kDownsampleToneEffect=76,kDownsampleEffect=104;
  static constexpr uint32_t kToneEffect=132,kBlurEffect=160,kBloomEffect=564,kTechniqueOffset=16;
  static constexpr uint32_t kEffectSpan=kBloomEffect+kTechniqueOffset+4;
  static constexpr int32_t kMaxExtent=8192;
};
struct PostFinishRecord {
  uint32_t address=0,texture=0;
  int32_t width=0,height=0;
  float texel_x=0,texel_y=0; // +32/+36; 820B04B8 uses +32 as the blur step
};
// Where the three tone constants came from. No setter in 820B0B80's call tree
// writes MiddleGray/LuminanceWhite/ToneMap: they are shared parameters of the
// effect pool [8257C02C], set by 820B1028 (defaults) and 820B5718 (environment
// presets). The audit supplies the values the previous audited frame observed
// on the live native effect bindings; the full-frame path reads the pool
// itself each frame (native_full_frame_post.h).
enum class PostToneSource:uint8_t { None, LivePreviousFrame, SharedPool };
struct PostFinishInput {
  uint32_t self=0;
  int32_t screen_width=0,screen_height=0;
  uint32_t scene_texture=0;
  std::array<PostFinishRecord,PostFinishLayout::kFirstPyramidCount> first{};
  std::vector<PostFinishRecord> second;
  PostFinishRecord blur{},blur_vertical{}; // this+500, this+448
  uint32_t mono_technique=0,downsample_tone_technique=0,downsample_technique=0;
  uint32_t tone_technique=0,blur_technique=0,bloom_technique=0;
  std::optional<std::array<float,3>> tone; // MiddleGray, LuminanceWhite, ToneMap
  PostToneSource tone_source=PostToneSource::None;
};
enum class PostSetterKind:uint8_t { Vectors, Texture, Sampler };
// 821BCD58(effect,name,float4*,count), 821BCE98(effect,name,texture),
// 821BCF28(effect,name,f1,r5,r6,r7).
struct PostSetterCall {
  uint32_t effect=0;
  std::string name;
  PostSetterKind kind=PostSetterKind::Vectors;
  std::vector<float> values; // Vectors: count*4 floats; Sampler: {f1}
  uint32_t texture=0;
  std::array<uint32_t,3> words{};
};
enum class PostPassKind:uint8_t { Downsample, Mono, DownsampleTone, Tone, BlurHorizontal, BlurVertical, Bloom };
inline const char* PostPassName(PostPassKind kind) {
  switch(kind) {
    case PostPassKind::Downsample: return "Downsample";
    case PostPassKind::Mono: return "Mono";
    case PostPassKind::DownsampleTone: return "DownsampleTone";
    case PostPassKind::Tone: return "Tone";
    case PostPassKind::BlurHorizontal: return "BlurH";
    case PostPassKind::BlurVertical: return "BlurV";
    case PostPassKind::Bloom: return "Bloom";
  }
  return "?";
}
using PostQuad=std::array<float,16>; // four (x,y,u,v), guest order
struct PostPassPlan {
  PostPassKind kind=PostPassKind::Downsample;
  uint32_t target=0; // record begun by 821B8828; 0 for the bloom (ordinary output)
  uint32_t target_texture=0; // the record's +4, which the native target registry is keyed by
  int32_t target_width=0,target_height=0;
  uint32_t technique=0;
  std::vector<PostSetterCall> setters;
  std::optional<PostQuad> quad; // the bloom's is the one 821A8F20 builds from the screen size
  bool uses_tone=false;
};
struct PostFinishPlan {
  std::vector<PostPassPlan> passes;
  std::optional<std::array<float,3>> tone;
  PostToneSource tone_source=PostToneSource::None;
};

inline constexpr const char* kPostDiffuse0="m_DiffuseTexture0_Sampler";
inline constexpr const char* kPostDiffuse1="m_DiffuseTexture1_Sampler";
inline constexpr const char* kPostOldTone="m_OldTone_Sampler";
inline constexpr const char* kPostTone="m_Tone_Sampler";
inline constexpr const char* kPostDownsampleOffset="m_DownsampleUVOffset";
inline constexpr const char* kPostBlurOffset="m_GaussBlurUVOffset";

// 820B01E8/820B04B8 quad: (-1,1),(1,1),(1,-1),(-1,-1) with UV shifted by half.
inline PostQuad PostQuadVertices(float half_u,float half_v) {
  const float u1=half_u+1.0f,v1=half_v+1.0f;
  return {-1,1,half_u,half_v, 1,1,u1,half_v, 1,-1,u1,v1, -1,-1,half_u,v1};
}
// 820B01E8: f9=1/w, f8=1/h (fdivs), UV half = f*0.5f.
struct PostSourceTexel { float x,y; };
inline PostSourceTexel PostSourceTexelOf(float width,float height) { return {1.0f/width,1.0f/height}; }
inline PostQuad PostDownsampleQuad(float width,float height) {
  const auto texel=PostSourceTexelOf(width,height);
  return PostQuadVertices(texel.x*0.5f,texel.y*0.5f);
}
// 820B01E8 stack+112: four float4 (0,0,0,1),(1/w,0,0,1),(1/w,1/h,0,1),(0,1/h,0,1).
inline std::array<float,16> PostDownsampleOffsets(float width,float height) {
  const auto t=PostSourceTexelOf(width,height);
  return {0,0,0,1, t.x,0,0,1, t.x,t.y,0,1, 0,t.y,0,1};
}
// 820B04B8 stack+416: fifteen float4 (x,y,weight,1). The kernel constant is the
// double at 82001EB8 (float(18*pi) widened), fsqrts'd; the exponent step is
// the single at 82001EA8 (-1/18); gain 1.25 at 82001EAC. Taps 1..7 step +i*texel,
// 8..14 copy 1..7 with x negated. Not normalized: the weights sum to ~1.235.
inline constexpr uint64_t kPostBlurDenominatorBits=0x404C463ACC000000ull;
inline constexpr uint32_t kPostBlurExponentBits=0xBD638E39u;
inline constexpr float kPostBlurGain=1.25f;
inline float PostBlurWeight(int tap) {
  const float root=float(std::sqrt(std::bit_cast<double>(kPostBlurDenominatorBits))); // fsqrts
  const float inverse=1.0f/root; // fdivs
  const float i=float(tap);
  const float exponent=tap ? (i*i)*std::bit_cast<float>(kPostBlurExponentBits) : 0.0f;
  // Tap 0 passes the double -0 at 82001EB0; exp(-0)=1 either way.
  const float gauss=float(std::exp(double(exponent))); // sub_821E88D8, then frsp
  return (gauss*inverse)*kPostBlurGain;
}
inline std::array<float,60> PostBlurTaps(float texel,bool vertical) {
  std::array<float,60> taps{};
  taps[2]=PostBlurWeight(0); taps[3]=1;
  for(int i=1;i<8;++i) {
    auto* tap=&taps[size_t(i)*4];
    tap[0]=float(i)*texel; tap[1]=0; tap[2]=PostBlurWeight(i); tap[3]=1;
    auto* mirror=&taps[size_t(i+7)*4];
    mirror[0]=-tap[0]; mirror[1]=0; mirror[2]=tap[2]; mirror[3]=1;
  }
  // 820B08AC swaps each tap's x and y in place before the vertical pass.
  if(vertical) for(size_t i=0;i<15;++i) std::swap(taps[i*4],taps[i*4+1]);
  return taps;
}

// Record/format checks that must pass before a plan exists. Returns the reason.
inline std::optional<std::string> ValidatePostFinishInput(const PostFinishInput& in) {
  using L=PostFinishLayout;
  const auto extent=[](int32_t v) { return v>0 && v<=L::kMaxExtent; };
  if(!in.self) return "no post owner";
  if(!extent(in.screen_width) || !extent(in.screen_height))
    return std::format("screen {}x{} outside 1..{}",in.screen_width,in.screen_height,L::kMaxExtent);
  if(!in.scene_texture) return "no scene texture at owner+104";
  const auto record=[&](const PostFinishRecord& r,const char* what,size_t index)->std::optional<std::string> {
    if(!r.address || !r.texture) return std::format("{}[{}] at {:#x} has no texture",what,index,r.address);
    if(!extent(r.width) || !extent(r.height))
      return std::format("{}[{}] at {:#x} is {}x{}",what,index,r.address,r.width,r.height);
    return std::nullopt;
  };
  for(size_t i=0;i<in.first.size();++i) if(auto e=record(in.first[i],"first",i)) return e;
  if(in.second.empty() || in.second.size()>L::kMaxSecondCount)
    return std::format("second pyramid count {} outside 1..{}",in.second.size(),L::kMaxSecondCount);
  for(size_t i=0;i<in.second.size();++i) if(auto e=record(in.second[i],"second",i)) return e;
  if(auto e=record(in.blur,"blur",0)) return e;
  if(auto e=record(in.blur_vertical,"blur_vertical",0)) return e;
  const auto& last=in.first.back();
  if(!std::isfinite(last.texel_x) || !std::isfinite(last.texel_y) || last.texel_x<=0 || last.texel_y<=0)
    return std::format("first[4] texel {},{} is not a positive finite step",last.texel_x,last.texel_y);
  for(const auto t:{in.mono_technique,in.downsample_tone_technique,in.downsample_technique,
                    in.tone_technique,in.blur_technique,in.bloom_technique})
    if(!t) return "missing post technique";
  if(in.tone && !std::all_of(in.tone->begin(),in.tone->end(),[](float v) { return std::isfinite(v); }))
    return "nonfinite live tone constants";
  return std::nullopt;
}

inline PostFinishPlan BuildPostFinishPlan(const PostFinishInput& in) {
  using L=PostFinishLayout;
  if(auto error=ValidatePostFinishInput(in)) throw std::runtime_error("post finish input: "+*error);
  PostFinishPlan plan;
  plan.tone=in.tone; plan.tone_source=in.tone?in.tone_source:PostToneSource::None;
  const auto vectors=[](uint32_t effect,const char* name,std::span<const float> values) {
    PostSetterCall call; call.effect=effect; call.name=name; call.kind=PostSetterKind::Vectors;
    call.values.assign(values.begin(),values.end()); return call;
  };
  const auto texture=[](uint32_t effect,const char* name,uint32_t handle) {
    PostSetterCall call; call.effect=effect; call.name=name; call.kind=PostSetterKind::Texture;
    call.texture=handle; return call;
  };
  const auto sampler=[](uint32_t effect,const char* name) {
    // 820B0600/820B0CBC: f1 = [820009A4] = 0.0, r5..r7 = 0.
    PostSetterCall call; call.effect=effect; call.name=name; call.kind=PostSetterKind::Sampler;
    call.values={0.0f}; return call;
  };
  // 820B01E8(this, target record, &size, source texture, mode).
  const auto reduce=[&](PostPassKind kind,const PostFinishRecord& target,float width,float height,uint32_t source) {
    PostPassPlan pass;
    pass.kind=kind; pass.target=target.address; pass.target_texture=target.texture;
    pass.target_width=target.width; pass.target_height=target.height;
    const auto effect=in.self+(kind==PostPassKind::Mono ? L::kMonoEffect :
      kind==PostPassKind::DownsampleTone ? L::kDownsampleToneEffect : L::kDownsampleEffect);
    pass.technique=kind==PostPassKind::Mono ? in.mono_technique :
      kind==PostPassKind::DownsampleTone ? in.downsample_tone_technique : in.downsample_technique;
    const auto offsets=PostDownsampleOffsets(width,height);
    pass.setters.push_back(vectors(effect,kPostDownsampleOffset,offsets));
    pass.setters.push_back(texture(effect,kPostDiffuse0,source));
    if(kind==PostPassKind::DownsampleTone) {
      pass.setters.push_back(texture(effect,kPostOldTone,target.texture)); // its own history
      pass.uses_tone=true;
    }
    pass.quad=PostDownsampleQuad(width,height);
    plan.passes.push_back(std::move(pass));
  };
  // 820B09B0: sizes are the SOURCE extents, halved in single precision.
  float width=float(in.screen_width),height=float(in.screen_height);
  for(size_t k=0;k<in.first.size();++k) {
    if(k) { width*=0.5f; height*=0.5f; }
    reduce(PostPassKind::Downsample,in.first[k],width,height,k?in.first[k-1].texture:in.scene_texture);
  }
  const auto& mono_source=in.first.back(); // this+396
  reduce(PostPassKind::Mono,in.second[0],float(mono_source.width),float(mono_source.height),mono_source.texture);
  for(size_t i=1;i<in.second.size();++i) {
    const auto& source=in.second[i-1];
    reduce(i+1==in.second.size()?PostPassKind::DownsampleTone:PostPassKind::Downsample,
           in.second[i],float(source.width),float(source.height),source.texture);
  }
  // 820B04B8(this, this+396, last second record): quad from +32/+36.
  const auto quad=PostQuadVertices(mono_source.texel_x*0.5f,mono_source.texel_y*0.5f);
  const auto& history=in.second.back();
  {
    PostPassPlan pass; pass.kind=PostPassKind::Tone; pass.uses_tone=true;
    pass.target=in.blur.address; pass.target_texture=in.blur.texture;
    pass.target_width=in.blur.width; pass.target_height=in.blur.height;
    pass.technique=in.tone_technique;
    const auto effect=in.self+L::kToneEffect;
    pass.setters={texture(effect,kPostDiffuse0,mono_source.texture),texture(effect,kPostTone,history.texture),
                  sampler(effect,kPostTone)};
    pass.quad=quad; plan.passes.push_back(std::move(pass));
  }
  for(const bool vertical:{false,true}) {
    PostPassPlan pass; pass.kind=vertical?PostPassKind::BlurVertical:PostPassKind::BlurHorizontal;
    const auto& target=vertical?in.blur_vertical:in.blur;
    pass.target=target.address; pass.target_texture=target.texture;
    pass.target_width=target.width; pass.target_height=target.height;
    pass.technique=in.blur_technique;
    const auto effect=in.self+L::kBlurEffect;
    const auto taps=PostBlurTaps(mono_source.texel_x,vertical);
    // Both passes read this+504, the Tone target's texture (history exists).
    pass.setters={vectors(effect,kPostBlurOffset,taps),texture(effect,kPostDiffuse0,in.blur.texture)};
    pass.quad=quad; plan.passes.push_back(std::move(pass));
  }
  {
    PostPassPlan pass; pass.kind=PostPassKind::Bloom; pass.uses_tone=true;
    pass.target_width=in.screen_width; pass.target_height=in.screen_height;
    pass.technique=in.bloom_technique;
    const auto effect=in.self+L::kBloomEffect;
    pass.setters={texture(effect,kPostDiffuse0,in.scene_texture),texture(effect,kPostDiffuse1,in.blur_vertical.texture),
                  sampler(effect,kPostDiffuse0),texture(effect,kPostTone,history.texture),sampler(effect,kPostTone)};
    // 821A8F20: the same quad as 820B01E8's, from float(owner+84/+88) with the
    // same 1.0/-1.0/0.5 constants (5084/2252/2260), drawn through 821A79B8.
    pass.quad=PostDownsampleQuad(float(in.screen_width),float(in.screen_height));
    plan.passes.push_back(std::move(pass));
  }
  return plan;
}

// What the guest actually did during the original call, segmented by technique
// activation (821B94E8): setters and a begun target precede it, the quad follows.
struct PostObservedPass {
  uint32_t target=0,technique=0;
  std::vector<PostSetterCall> setters;
  std::optional<PostQuad> quad;
  std::optional<std::array<int32_t,2>> native_target;
  std::array<float,3> tone{NAN,NAN,NAN}; // native bindings at the draw; NaN where not reflected
};
struct PostFinishObservation {
  std::vector<PostObservedPass> passes;
  PostObservedPass pending;
  bool quad_armed=false,native_armed=false;
  void BeginTarget(uint32_t target) { pending.target=target; }
  void Setter(PostSetterCall call) { pending.setters.push_back(std::move(call)); }
  void Activate(uint32_t technique) {
    pending.technique=technique;
    passes.push_back(std::move(pending)); pending={};
    quad_armed=native_armed=true;
  }
  void Quad(const PostQuad& quad) {
    if(!quad_armed || passes.empty()) return;
    passes.back().quad=quad; quad_armed=false;
  }
  void NativeDraw(int32_t width,int32_t height,const std::array<float,3>& tone) {
    if(!native_armed || passes.empty()) return;
    passes.back().native_target=std::array<int32_t,2>{width,height};
    passes.back().tone=tone; native_armed=false;
  }
};

inline bool PostFloatMatches(float expected,float actual) {
  if(std::bit_cast<uint32_t>(expected)==std::bit_cast<uint32_t>(actual)) return true;
  if(!std::isfinite(expected) || !std::isfinite(actual)) return false;
  // The guest's exp (sub_821E88D8) is its own polynomial; allow a few ulps.
  return std::abs(expected-actual)<=std::max(std::abs(expected),std::abs(actual))*4e-7f+1e-12f;
}
struct PostFinishMismatch { size_t pass=0; std::string what; };
struct PostFinishComparison {
  std::vector<PostFinishMismatch> mismatches;
  size_t native_unobserved=0; // passes whose draw did not reach the native post path
  size_t tone_compared=0;
};
inline PostFinishComparison ComparePostFinish(const PostFinishPlan& plan,const PostFinishObservation& seen) {
  PostFinishComparison out;
  auto add=[&](size_t pass,std::string what) { out.mismatches.push_back({pass,std::move(what)}); };
  if(plan.passes.size()!=seen.passes.size())
    add(0,std::format("pass count planned={} observed={}",plan.passes.size(),seen.passes.size()));
  if(!seen.pending.setters.empty())
    add(seen.passes.size(),std::format("{} setter(s) after the last activation",seen.pending.setters.size()));
  const auto count=std::min(plan.passes.size(),seen.passes.size());
  for(size_t p=0;p<count;++p) {
    const auto& want=plan.passes[p];
    const auto& got=seen.passes[p];
    const auto* name=PostPassName(want.kind);
    if(want.technique!=got.technique) add(p,std::format("{} technique planned={:#x} observed={:#x}",name,want.technique,got.technique));
    if(want.target!=got.target) add(p,std::format("{} target planned={:#x} observed={:#x}",name,want.target,got.target));
    if(want.setters.size()!=got.setters.size())
      add(p,std::format("{} setter count planned={} observed={}",name,want.setters.size(),got.setters.size()));
    for(size_t s=0;s<std::min(want.setters.size(),got.setters.size());++s) {
      const auto& a=want.setters[s];
      const auto& b=got.setters[s];
      if(a.effect!=b.effect || a.name!=b.name || a.kind!=b.kind) {
        add(p,std::format("{} setter {} planned={:#x}:{} observed={:#x}:{}",name,s,a.effect,a.name,b.effect,b.name));
        continue;
      }
      if(a.texture!=b.texture) add(p,std::format("{} {} texture planned={:#x} observed={:#x}",name,a.name,a.texture,b.texture));
      if(a.words!=b.words) add(p,std::format("{} {} words planned={},{},{} observed={},{},{}",name,a.name,
                                           a.words[0],a.words[1],a.words[2],b.words[0],b.words[1],b.words[2]));
      if(a.values.size()!=b.values.size()) { add(p,std::format("{} {} value count planned={} observed={}",name,a.name,a.values.size(),b.values.size())); continue; }
      for(size_t v=0;v<a.values.size();++v) if(!PostFloatMatches(a.values[v],b.values[v])) {
        add(p,std::format("{} {}[{}] planned={} observed={}",name,a.name,v,a.values[v],b.values[v])); break;
      }
    }
    if(want.quad) {
      if(!got.quad) add(p,std::format("{} quad not observed",name));
      else for(size_t v=0;v<16;++v) if(!PostFloatMatches((*want.quad)[v],(*got.quad)[v])) {
        add(p,std::format("{} quad[{}] planned={} observed={}",name,v,(*want.quad)[v],(*got.quad)[v])); break;
      }
    }
    if(!got.native_target) ++out.native_unobserved;
    else if((*got.native_target)[0]!=want.target_width || (*got.native_target)[1]!=want.target_height)
      add(p,std::format("{} native target planned={}x{} observed={}x{}",name,want.target_width,want.target_height,
                        (*got.native_target)[0],(*got.native_target)[1]));
    if(want.uses_tone && plan.tone) for(size_t c=0;c<3;++c) {
      if(std::isnan(got.tone[c])) continue;
      ++out.tone_compared;
      if(!PostFloatMatches((*plan.tone)[c],got.tone[c]))
        add(p,std::format("{} live tone[{}] previous={} observed={}",name,c,(*plan.tone)[c],got.tone[c]));
    }
  }
  return out;
}
// First values reflected on the native bindings at tone-using draws, in pass order.
inline std::optional<std::array<float,3>> PostObservedTone(const PostFinishPlan& plan,const PostFinishObservation& seen) {
  std::array<float,3> tone{NAN,NAN,NAN};
  for(size_t p=0;p<std::min(plan.passes.size(),seen.passes.size());++p) {
    if(!plan.passes[p].uses_tone) continue;
    for(size_t c=0;c<3;++c) if(std::isnan(tone[c])) tone[c]=seen.passes[p].tone[c];
  }
  if(std::any_of(tone.begin(),tone.end(),[](float v) { return std::isnan(v); })) return std::nullopt;
  return tone;
}
// First eight, then every 1000th.
inline bool ShouldLogPostFinish(uint64_t count) { return count && (count<=8 || count%1000==0); }

// Step 3: the order the native finish loop issues the plan in. Each chain pass
// (the 820B09B0 replacement) is issued whole, as 820B01E8/820B04B8 order it:
// begin target 821B8828, the plan's setters, activation 821B94E8, the quad
// through 821A79B8, end 821B88B0. The bloom's setters and activation stay in
// the kept guest body of 820B0B80 (guest=true); the loop issues only its quad,
// in place of 821A8F20's.
enum class PostIssueKind:uint8_t { BeginTarget, Setter, Activate, Draw, EndTarget };
inline const char* PostIssueName(PostIssueKind kind) {
  switch(kind) {
    case PostIssueKind::BeginTarget: return "begin";
    case PostIssueKind::Setter: return "setter";
    case PostIssueKind::Activate: return "activate";
    case PostIssueKind::Draw: return "draw";
    case PostIssueKind::EndTarget: return "end";
  }
  return "?";
}
struct PostIssueStep {
  PostIssueKind kind=PostIssueKind::BeginTarget;
  size_t pass=0,setter=0;
  bool guest=false; // performed by the kept guest body, not by the loop
};
inline std::vector<PostIssueStep> BuildPostFinishIssue(const PostFinishPlan& plan) {
  if(plan.passes.empty() || plan.passes.back().kind!=PostPassKind::Bloom)
    throw std::runtime_error("post finish issue: the plan does not end with the bloom");
  std::vector<PostIssueStep> steps;
  for(size_t p=0;p<plan.passes.size();++p) {
    const auto& pass=plan.passes[p];
    const bool bloom=p+1==plan.passes.size();
    if(bloom==(pass.target!=0) || !pass.quad || !pass.technique)
      throw std::runtime_error(std::format("post finish issue: pass {} ({}) has no target, quad or technique",p,PostPassName(pass.kind)));
    if(!bloom) steps.push_back({PostIssueKind::BeginTarget,p,0,false});
    for(size_t s=0;s<pass.setters.size();++s) steps.push_back({PostIssueKind::Setter,p,s,bloom});
    steps.push_back({PostIssueKind::Activate,p,0,bloom});
    steps.push_back({PostIssueKind::Draw,p,0,false});
    if(!bloom) steps.push_back({PostIssueKind::EndTarget,p,0,false});
  }
  return steps;
}
// Steps the 820B09B0 replacement issues: every one before the bloom's.
inline size_t PostIssueChainEnd(const PostFinishPlan& plan,const std::vector<PostIssueStep>& steps) {
  size_t end=0;
  while(end<steps.size() && steps[end].pass+1<plan.passes.size()) ++end;
  return end;
}

// The tone history on the guest route. PS_Downsample_Tone blends its own
// previous resolve (m_OldTone) by a fixed 0.025 per DRAW (NativePostHistory in
// native_full_frame_post.h), so an unlocked render that dispatched no
// simulation step (native_render_tick_frame false) would adapt the eye again.
// A held render drops that pass whole: begin 821B8828, its setters,
// activation, quad and end 821B88B0 (the resolve). Its record then keeps the
// tick frame's resolve, which the Tone pass (m_Tone_Sampler) and the bloom
// read. The pass's setters write only the DownsampleTone effect (this+76),
// which nothing else draws, and the tone constants are pool values that no
// setter of the chain writes. Begin and end go together, so the target stack
// stays balanced, and every later pass begins, sets and activates its own.
// The index of the plan's DownsampleTone pass, if it has one.
inline std::optional<size_t> PostToneHistoryPass(const PostFinishPlan& plan) {
  std::optional<size_t> found;
  for(size_t p=0;p<plan.passes.size();++p) {
    if(plan.passes[p].kind!=PostPassKind::DownsampleTone) continue;
    if(found) throw std::runtime_error("post finish issue: more than one DownsampleTone pass");
    found=p;
  }
  return found;
}
// `steps` without the steps of plan pass `pass` (a chain pass: none of its
// steps is the kept guest body's). Step pass indexes still refer to `plan`.
inline std::vector<PostIssueStep> PostIssueWithoutPass(const PostFinishPlan& plan,const std::vector<PostIssueStep>& steps,size_t pass) {
  if(pass+1>=plan.passes.size()) throw std::runtime_error("post finish issue: only a chain pass can be held");
  std::vector<PostIssueStep> kept;
  kept.reserve(steps.size());
  for(const auto& step:steps) {
    if(step.pass!=pass) { kept.push_back(step); continue; }
    if(step.guest) throw std::runtime_error("post finish issue: a held pass has a guest step");
  }
  return kept;
}
// What the guest route observes on a held render: the plan without `pass`,
// for the self-audit's ComparePostFinish.
inline PostFinishPlan PostPlanWithoutPass(PostFinishPlan plan,size_t pass) {
  if(pass>=plan.passes.size()) throw std::runtime_error("post finish plan: no such pass");
  plan.passes.erase(plan.passes.begin()+std::ptrdiff_t(pass));
  return plan;
}

// Why a frame's native loop gave the frame back to the original. Counted per
// reason; each is decided before anything is issued except Midway and Bloom.
enum class PostFinishFallback:uint8_t {
  Plan,          // records or formats did not validate (at 820B0B80 or again at 820B09B0)
  AuditUnclean,  // the audit is on and its latest guest frame had mismatches
  Owner,         // 820B09B0 was called for another post owner
  Target,        // a pass target is not a registered native target of the planned extent
  Shaders,       // a technique's shader pair has no native registration
  Draw2D,        // the 2D object 821A79B8 draws through would trap
  Stack,         // no writable guest stack for the loop's frame
  Exception,     // preflight threw
  Midway,        // a step threw after issuing began; the open target is closed and the original reruns
  Bloom,         // the bloom quad was left to the original 821A8F20
  Count
};
inline const char* PostFinishFallbackName(PostFinishFallback reason) {
  switch(reason) {
    case PostFinishFallback::Plan: return "plan";
    case PostFinishFallback::AuditUnclean: return "audit-unclean";
    case PostFinishFallback::Owner: return "owner";
    case PostFinishFallback::Target: return "target";
    case PostFinishFallback::Shaders: return "shaders";
    case PostFinishFallback::Draw2D: return "draw2d";
    case PostFinishFallback::Stack: return "stack";
    case PostFinishFallback::Exception: return "exception";
    case PostFinishFallback::Midway: return "midway";
    case PostFinishFallback::Bloom: return "bloom";
    case PostFinishFallback::Count: break;
  }
  return "?";
}
// Which body a frame runs. Native needs the cvar, the A/B native side and a
// valid plan; with the audit on it also needs the latest frame to have been an
// audited guest frame that was clean, so native and audited frames alternate.
enum class PostFinishMode:uint8_t { Guest, Audit, Native };
struct PostFinishGate {
  bool native=false,audit=false,ab_native_side=true,plan=false;
  bool last_audit_clean=false,last_frame_native=false;
};
inline PostFinishMode ChoosePostFinishMode(const PostFinishGate& gate) {
  if(!gate.plan) return PostFinishMode::Guest;
  if(gate.native && gate.ab_native_side && (!gate.audit || (gate.last_audit_clean && !gate.last_frame_native)))
    return PostFinishMode::Native;
  return gate.audit ? PostFinishMode::Audit : PostFinishMode::Guest;
}
}

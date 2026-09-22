#include "native_graphics/native_post_finish_plan.h"
#include <cmath>
#include <iostream>
#include <numbers>

namespace {
int failures=0;
void Check(bool condition,const char* expression,int line) {
  if(!condition) { std::cerr<<"line "<<line<<": CHECK("<<expression<<") failed\n"; ++failures; }
}
#define CHECK(value) Check(static_cast<bool>(value),#value,__LINE__)
using namespace edf::native;
using L=PostFinishLayout;

constexpr uint32_t kSelf=0x40010000u;
PostFinishRecord Record(uint32_t address,uint32_t texture,int32_t width,int32_t height) {
  PostFinishRecord record;
  record.address=address; record.texture=texture; record.width=width; record.height=height;
  record.texel_x=1.0f/float(width); record.texel_y=1.0f/float(height);
  return record;
}
PostFinishInput Input(size_t second_count=5) {
  PostFinishInput in;
  in.self=kSelf; in.screen_width=1280; in.screen_height=720; in.scene_texture=0x50000000u;
  // 1280x720 halved and floored: 640x360 .. 40x22 (the 80x45 -> 40x22 stage).
  for(uint32_t k=0;k<L::kFirstPyramidCount;++k)
    in.first[k]=Record(kSelf+L::kFirstPyramid+k*L::kRecordStride,0x51000000u+k,640>>k,int32_t(360>>k));
  CHECK(in.first[4].width==40 && in.first[4].height==22);
  const std::array<int32_t,5> sides{32,16,8,4,1};
  for(size_t i=0;i<second_count;++i)
    in.second.push_back(Record(0x42000000u+uint32_t(i)*L::kRecordStride,0x52000000u+uint32_t(i),sides[i%5],sides[i%5]));
  in.blur=Record(kSelf+L::kBlurTarget,0x53000000u,40,22);
  in.blur_vertical=Record(kSelf+L::kBlurTargetVertical,0x53000001u,40,22);
  in.mono_technique=0x60000001u; in.downsample_tone_technique=0x60000002u; in.downsample_technique=0x60000003u;
  in.tone_technique=0x60000004u; in.blur_technique=0x60000005u; in.bloom_technique=0x60000006u;
  return in;
}
PostFinishObservation Replay(const PostFinishPlan& plan,bool native=true) {
  PostFinishObservation seen;
  for(const auto& pass:plan.passes) {
    if(pass.target) seen.BeginTarget(pass.target);
    for(const auto& setter:pass.setters) seen.Setter(setter);
    seen.Activate(pass.technique);
    if(pass.quad) seen.Quad(*pass.quad);
    if(native) seen.NativeDraw(pass.target_width,pass.target_height,
      plan.tone && pass.uses_tone ? *plan.tone : std::array<float,3>{NAN,NAN,NAN});
  }
  return seen;
}
const PostSetterCall* Find(const PostPassPlan& pass,const char* name) {
  for(const auto& setter:pass.setters) if(setter.name==name) return &setter;
  return nullptr;
}

void TestBlurWeights() {
  // Independent reference: 1.25*exp(-i^2/18)/sqrt(18*pi), sigma 3, in double.
  float sum=0;
  for(int i=0;i<8;++i) {
    const double reference=1.25*std::exp(-double(i*i)/18.0)/std::sqrt(18.0*std::numbers::pi);
    CHECK(std::abs(PostBlurWeight(i)-reference)<=reference*2e-6);
    sum+=PostBlurWeight(i)*(i?2.0f:1.0f);
  }
  CHECK(std::abs(PostBlurWeight(0)-0.16622594f)<=1e-7f);
  CHECK(std::abs(PostBlurWeight(1)-0.15724300f)<=1e-7f);
  CHECK(std::abs(PostBlurWeight(7)-0.010925785f)<=1e-8f);
  CHECK(std::abs(sum-1.2349775f)<=2e-6f); // not normalized
  const float texel=1.0f/40.0f;
  const auto h=PostBlurTaps(texel,false),v=PostBlurTaps(texel,true);
  CHECK(h[0]==0 && h[1]==0 && h[2]==PostBlurWeight(0) && h[3]==1);
  for(int i=1;i<8;++i) {
    CHECK(h[i*4]==float(i)*texel && h[i*4+1]==0 && h[i*4+2]==PostBlurWeight(i) && h[i*4+3]==1);
    CHECK(h[(i+7)*4]==-float(i)*texel && h[(i+7)*4+2]==h[i*4+2]);
  }
  for(size_t i=0;i<15;++i) CHECK(v[i*4]==h[i*4+1] && v[i*4+1]==h[i*4] && v[i*4+2]==h[i*4+2] && v[i*4+3]==1);
}

void TestOffsetsAndQuad() {
  const auto offsets=PostDownsampleOffsets(1280,720);
  const std::array<float,16> expected{0,0,0,1, 1.0f/1280,0,0,1, 1.0f/1280,1.0f/720,0,1, 0,1.0f/720,0,1};
  CHECK(offsets==expected);
  const auto quad=PostDownsampleQuad(80,45);
  CHECK(quad[0]==-1 && quad[1]==1 && quad[2]==(1.0f/80)*0.5f && quad[3]==(1.0f/45)*0.5f);
  CHECK(quad[4]==1 && quad[5]==1 && quad[6]==quad[2]+1.0f && quad[7]==quad[3]);
  CHECK(quad[8]==1 && quad[9]==-1 && quad[10]==quad[6] && quad[11]==quad[3]+1.0f);
  CHECK(quad[12]==-1 && quad[13]==-1 && quad[14]==quad[2] && quad[15]==quad[11]);
  CHECK(PostQuadVertices(0.0f,0.0f)==(PostQuad{-1,1,0,0, 1,1,1,0, 1,-1,1,1, -1,-1,0,1}));
}

void TestPlan() {
  const auto in=Input();
  const auto plan=BuildPostFinishPlan(in);
  CHECK(plan.passes.size()==14);
  const std::array<PostPassKind,14> kinds{PostPassKind::Downsample,PostPassKind::Downsample,PostPassKind::Downsample,
    PostPassKind::Downsample,PostPassKind::Downsample,PostPassKind::Mono,PostPassKind::Downsample,PostPassKind::Downsample,
    PostPassKind::Downsample,PostPassKind::DownsampleTone,PostPassKind::Tone,PostPassKind::BlurHorizontal,
    PostPassKind::BlurVertical,PostPassKind::Bloom};
  for(size_t p=0;p<std::min(kinds.size(),plan.passes.size());++p) CHECK(plan.passes[p].kind==kinds[p]);
  // Sources: the screen halved per stage; targets: the records' own extents.
  const std::array<float,5> widths{1280,640,320,160,80},heights{720,360,180,90,45};
  for(size_t k=0;k<5;++k) {
    const auto& pass=plan.passes[k];
    CHECK(pass.target==kSelf+188+52*k && pass.target_width==(640>>k) && pass.technique==in.downsample_technique);
    const auto* offsets=Find(pass,kPostDownsampleOffset);
    CHECK(offsets && offsets->effect==kSelf+104 && offsets->values.size()==16);
    if(offsets) CHECK(offsets->values[4]==1.0f/widths[k] && offsets->values[9]==1.0f/heights[k]);
    const auto* source=Find(pass,kPostDiffuse0);
    CHECK(source && source->texture==(k?in.first[k-1].texture:in.scene_texture));
    CHECK(pass.quad && *pass.quad==PostDownsampleQuad(widths[k],heights[k]));
  }
  const auto& mono=plan.passes[5];
  CHECK(mono.target==in.second[0].address && mono.technique==in.mono_technique);
  CHECK(Find(mono,kPostDownsampleOffset)->effect==kSelf+48);
  CHECK(Find(mono,kPostDownsampleOffset)->values[4]==1.0f/40 && Find(mono,kPostDownsampleOffset)->values[9]==1.0f/22);
  CHECK(Find(mono,kPostDiffuse0)->texture==in.first[4].texture);
  const auto& history=plan.passes[9];
  CHECK(history.setters.size()==3 && history.uses_tone && history.technique==in.downsample_tone_technique);
  CHECK(Find(history,kPostOldTone)->texture==in.second[4].texture && Find(history,kPostOldTone)->effect==kSelf+76);
  CHECK(Find(history,kPostDiffuse0)->texture==in.second[3].texture);
  const auto& tone=plan.passes[10];
  CHECK(tone.target==kSelf+500 && tone.setters.size()==3 && tone.setters[2].kind==PostSetterKind::Sampler);
  CHECK(tone.setters[0].texture==in.first[4].texture && tone.setters[1].texture==in.second[4].texture);
  CHECK(tone.quad && (*tone.quad)[2]==in.first[4].texel_x*0.5f && (*tone.quad)[3]==in.first[4].texel_y*0.5f);
  const auto& blur_h=plan.passes[11];
  const auto& blur_v=plan.passes[12];
  CHECK(blur_h.target==kSelf+500 && blur_v.target==kSelf+448);
  CHECK(Find(blur_h,kPostBlurOffset)->values.size()==60 && Find(blur_h,kPostBlurOffset)->effect==kSelf+160);
  CHECK(Find(blur_h,kPostDiffuse0)->texture==in.blur.texture && Find(blur_v,kPostDiffuse0)->texture==in.blur.texture);
  CHECK(Find(blur_v,kPostBlurOffset)->values[5]==1.0f/40); // tap 1 y after the swap
  const auto& bloom=plan.passes[13];
  CHECK(!bloom.target && bloom.target_width==1280 && bloom.technique==in.bloom_technique);
  CHECK(bloom.quad && *bloom.quad==PostDownsampleQuad(1280,720)); // 821A8F20's
  for(size_t p=0;p+1<plan.passes.size();++p) CHECK(plan.passes[p].target_texture!=0);
  CHECK(plan.passes[0].target_texture==in.first[0].texture && plan.passes[9].target_texture==in.second[4].texture &&
        plan.passes[10].target_texture==in.blur.texture && plan.passes[12].target_texture==in.blur_vertical.texture);
  CHECK(bloom.setters.size()==5 && bloom.setters[0].texture==in.scene_texture &&
        bloom.setters[1].texture==in.blur_vertical.texture && bloom.setters[3].texture==in.second[4].texture);
  for(const auto& setter:bloom.setters) CHECK(setter.effect==kSelf+564);
  // One-record second pyramid: no history reduction, the Tone reads record 0.
  const auto single=BuildPostFinishPlan(Input(1));
  CHECK(single.passes.size()==10);
  for(const auto& pass:single.passes) CHECK(pass.kind!=PostPassKind::DownsampleTone);
}

void TestValidation() {
  CHECK(!ValidatePostFinishInput(Input()));
  auto in=Input(); in.second.clear(); CHECK(ValidatePostFinishInput(in));
  in=Input(); { const auto copy=in.second[0]; in.second.resize(17,copy); } CHECK(ValidatePostFinishInput(in));
  in=Input(); in.first[2].texture=0; CHECK(ValidatePostFinishInput(in));
  in=Input(); in.screen_width=0; CHECK(ValidatePostFinishInput(in));
  in=Input(); in.blur.height=-4; CHECK(ValidatePostFinishInput(in));
  in=Input(); in.first[4].texel_x=NAN; CHECK(ValidatePostFinishInput(in));
  in=Input(); in.bloom_technique=0; CHECK(ValidatePostFinishInput(in));
  bool threw=false;
  try { in=Input(); in.scene_texture=0; BuildPostFinishPlan(in); } catch(const std::runtime_error&) { threw=true; }
  CHECK(threw);
}

void TestComparison() {
  auto in=Input();
  in.tone=std::array<float,3>{.8f,1.5f,.8f}; in.tone_source=PostToneSource::LivePreviousFrame;
  const auto plan=BuildPostFinishPlan(in);
  auto result=ComparePostFinish(plan,Replay(plan));
  CHECK(result.mismatches.empty() && !result.native_unobserved && result.tone_compared==9);
  CHECK(PostObservedTone(plan,Replay(plan))==in.tone);
  CHECK(ComparePostFinish(plan,Replay(plan,false)).native_unobserved==14);
  CHECK(!PostObservedTone(plan,Replay(plan,false)));
  auto seen=Replay(plan);
  seen.passes[11].setters[0].values[2]=std::nextafter(seen.passes[11].setters[0].values[2],1.0f); // one ulp
  CHECK(ComparePostFinish(plan,seen).mismatches.empty());
  seen.passes[11].setters[0].values[2]*=1.01f;
  CHECK(ComparePostFinish(plan,seen).mismatches.size()==1);
  seen=Replay(plan); seen.passes[3].native_target=std::array<int32_t,2>{81,45};
  CHECK(ComparePostFinish(plan,seen).mismatches.size()==1);
  seen=Replay(plan); seen.passes[13].tone[0]=.9f;
  CHECK(ComparePostFinish(plan,seen).mismatches.size()==1);
  seen=Replay(plan); seen.passes[0].setters[1].texture^=1;
  CHECK(ComparePostFinish(plan,seen).mismatches.size()==1);
  seen=Replay(plan); seen.passes.pop_back();
  result=ComparePostFinish(plan,seen);
  CHECK(result.mismatches.size()==1 && result.mismatches[0].what.starts_with("pass count"));
  // A quad without an activation before it is not attributed to any pass.
  PostFinishObservation stray; stray.Quad(PostQuad{}); CHECK(stray.passes.empty());
}

// What the observer hooks record while the native loop runs the issue steps:
// 821B8828 begins, the three setters, 821B94E8, the 821A79B8 quad and the
// native post path's draw extent. Guest steps are what 820B0B80 still does.
struct IssueTrace {
  PostFinishObservation seen;
  std::vector<uint32_t> draw_targets;
  std::vector<std::string> order;
};
IssueTrace Execute(const PostFinishPlan& plan,const std::vector<PostIssueStep>& steps) {
  IssueTrace trace;
  uint32_t open=0;
  for(const auto& step:steps) {
    const auto& pass=plan.passes[step.pass];
    trace.order.push_back(std::format("{}:{}{}",step.pass,PostIssueName(step.kind),step.guest?"(guest)":""));
    switch(step.kind) {
      case PostIssueKind::BeginTarget: CHECK(!open); open=pass.target; trace.seen.BeginTarget(pass.target); break;
      case PostIssueKind::Setter: trace.seen.Setter(pass.setters[step.setter]); break;
      case PostIssueKind::Activate: trace.seen.Activate(pass.technique); break;
      case PostIssueKind::Draw:
        CHECK(open==pass.target);
        trace.draw_targets.push_back(open);
        trace.seen.Quad(*pass.quad);
        trace.seen.NativeDraw(pass.target_width,pass.target_height,
          plan.tone && pass.uses_tone ? *plan.tone : std::array<float,3>{NAN,NAN,NAN});
        break;
      case PostIssueKind::EndTarget: CHECK(open==pass.target); open=0; break;
    }
  }
  CHECK(!open);
  return trace;
}

void TestIssue() {
  auto in=Input();
  in.tone=std::array<float,3>{.8f,1.5f,.8f}; in.tone_source=PostToneSource::LivePreviousFrame;
  const auto plan=BuildPostFinishPlan(in);
  const auto steps=BuildPostFinishIssue(plan);
  // 13 chain passes of begin/setters/activate/draw/end, then the bloom's five
  // guest setters, its guest activation and the native quad.
  size_t setters=0;
  for(const auto& pass:plan.passes) setters+=pass.setters.size();
  CHECK(steps.size()==13*4+setters+2);
  const auto end=PostIssueChainEnd(plan,steps);
  CHECK(end==steps.size()-7);
  for(size_t i=0;i<steps.size();++i) CHECK(steps[i].guest==(i>=end && steps[i].kind!=PostIssueKind::Draw));
  CHECK(steps.back().kind==PostIssueKind::Draw && steps.back().pass==13 && !steps.back().guest);
  // Per pass: begin, the plan's setters in plan order, activate, draw, end.
  for(size_t i=0,p=0;p<13;++p) {
    CHECK(steps[i].kind==PostIssueKind::BeginTarget && steps[i].pass==p); ++i;
    for(size_t s=0;s<plan.passes[p].setters.size();++s,++i)
      CHECK(steps[i].kind==PostIssueKind::Setter && steps[i].pass==p && steps[i].setter==s);
    CHECK(steps[i].kind==PostIssueKind::Activate && steps[i].pass==p); ++i;
    CHECK(steps[i].kind==PostIssueKind::Draw && steps[i].pass==p); ++i;
    CHECK(steps[i].kind==PostIssueKind::EndTarget && steps[i].pass==p); ++i;
  }
  // The recorded sequence compares clean with the plan: targets, setter
  // constants, techniques, quads and native extents, in draw order.
  const auto trace=Execute(plan,steps);
  const auto result=ComparePostFinish(plan,trace.seen);
  CHECK(result.mismatches.empty() && !result.native_unobserved && result.tone_compared==9);
  for(const auto& mismatch:result.mismatches) std::cerr<<"  pass "<<mismatch.pass<<": "<<mismatch.what<<"
";
  std::vector<uint32_t> expected;
  for(const auto& record:in.first) expected.push_back(record.address);
  for(const auto& record:in.second) expected.push_back(record.address);
  expected.push_back(in.blur.address); expected.push_back(in.blur.address); expected.push_back(in.blur_vertical.address);
  expected.push_back(0);
  CHECK(trace.draw_targets==expected);
  CHECK(trace.order.front()=="0:begin" && trace.order[1]=="0:setter" && trace.order.back()=="13:draw");
  CHECK(trace.order[trace.order.size()-2]=="13:activate(guest)");
  CHECK(trace.seen.passes[11].setters[0].values==plan.passes[11].setters[0].values); // the blur kernel as issued
  CHECK(trace.seen.passes[12].setters[0].values[5]==1.0f/40);
  // An issue order that swaps two passes, or drops a setter, is caught.
  auto swapped=steps;
  std::swap_ranges(swapped.begin(),swapped.begin()+6,swapped.begin()+6);
  CHECK(!ComparePostFinish(plan,Execute(plan,swapped).seen).mismatches.empty());
  auto dropped=steps;
  dropped.erase(dropped.begin()+1);
  CHECK(!ComparePostFinish(plan,Execute(plan,dropped).seen).mismatches.empty());
  // A one-record second pyramid issues 9 chain passes.
  const auto single=BuildPostFinishPlan(Input(1));
  const auto single_steps=BuildPostFinishIssue(single);
  CHECK(single_steps[PostIssueChainEnd(single,single_steps)-1].pass==8);
  CHECK(ComparePostFinish(single,Execute(single,single_steps).seen).mismatches.empty());
  // Plans the loop cannot issue are refused.
  auto broken=plan; broken.passes[3].quad.reset();
  bool threw=false;
  try { BuildPostFinishIssue(broken); } catch(const std::runtime_error&) { threw=true; }
  CHECK(threw);
  broken=plan; broken.passes.pop_back(); threw=false;
  try { BuildPostFinishIssue(broken); } catch(const std::runtime_error&) { threw=true; }
  CHECK(threw);
  for(size_t r=0;r<size_t(PostFinishFallback::Count);++r) CHECK(std::string(PostFinishFallbackName(PostFinishFallback(r)))!="?");
}

void TestMode() {
  using M=PostFinishMode;
  const auto mode=[](bool native,bool audit,bool side,bool plan,bool clean,bool last_native) {
    return ChoosePostFinishMode({native,audit,side,plan,clean,last_native});
  };
  CHECK(mode(true,false,true,true,false,false)==M::Native);    // audit off: a valid plan suffices
  CHECK(mode(true,false,true,false,false,false)==M::Guest);    // no plan
  CHECK(mode(true,false,false,true,false,false)==M::Guest);    // A/B guest side
  CHECK(mode(true,true,true,true,true,false)==M::Native);      // after a clean audited frame
  CHECK(mode(true,true,true,true,true,true)==M::Audit);        // after a native frame: audit again
  CHECK(mode(true,true,true,true,false,false)==M::Audit);      // latest audit unclean
  CHECK(mode(true,true,false,true,true,false)==M::Audit);      // A/B guest side still audits
  CHECK(mode(false,true,true,true,true,false)==M::Audit);
  CHECK(mode(false,false,true,true,true,false)==M::Guest);
  // Alternation with the audit on: audit, native, audit, native.
  bool clean=false,last_native=false;
  std::vector<M> frames;
  for(int f=0;f<4;++f) {
    const auto m=mode(true,true,true,true,clean,last_native);
    frames.push_back(m);
    if(m==M::Audit) clean=true;
    last_native=m==M::Native;
  }
  CHECK((frames==std::vector<M>{M::Audit,M::Native,M::Audit,M::Native}));
}

void TestLogPolicy() {
  CHECK(!ShouldLogPostFinish(0));
  for(uint64_t n=1;n<=8;++n) CHECK(ShouldLogPostFinish(n));
  CHECK(!ShouldLogPostFinish(9) && !ShouldLogPostFinish(999) && ShouldLogPostFinish(1000) && ShouldLogPostFinish(2000));
}
}

int main() {
  TestBlurWeights();
  TestOffsetsAndQuad();
  TestPlan();
  TestValidation();
  TestComparison();
  TestIssue();
  TestMode();
  TestLogPolicy();
  if(failures) std::cerr<<failures<<" post finish plan failures\n";
  else std::cout<<"post finish plan tests passed\n";
  return failures?1:0;
}

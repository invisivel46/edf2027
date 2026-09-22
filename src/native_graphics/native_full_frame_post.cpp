#include "native_graphics/native_full_frame_post.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <stdexcept>

namespace edf::native {
uint32_t PostGuestMemory::Word(uint32_t address) const {
  const auto* p=Bytes(address,4);
  return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
}
float PostGuestMemory::Float(uint32_t address) const { return std::bit_cast<float>(Word(address)); }

namespace {
using T=PostToneLayout;
uint32_t Add(uint32_t address,uint32_t offset) {
  if(!address || offset>0xFFFFFFFFu-address) throw std::runtime_error(std::format("guest address {:#x}+{} overflows",address,offset));
  return address+offset;
}
// The 820A62E8 layout at `string`: 16+ capacity points at the text.
std::string ReadKey(const PostGuestMemory& memory,uint32_t string) {
  const auto size=memory.Word(Add(string,T::kStringSize)),capacity=memory.Word(Add(string,T::kStringCapacity));
  if(size>capacity || size>T::kMaxKey) throw std::runtime_error(std::format("pool key at {:#x} has size {} capacity {}",string,size,capacity));
  const auto data=capacity>=T::kStringInline ? memory.Word(Add(string,T::kStringBuffer)) : Add(string,T::kStringBuffer);
  if(!size) return {};
  return std::string(reinterpret_cast<const char*>(memory.Bytes(data,size)),size);
}
// 821A1868(this,0,this.size,ptr,count): unsigned bytes over the shorter
// length, then the shorter string first.
int CompareKey(const std::string& left,const std::string& right) {
  const auto count=std::min(left.size(),right.size());
  for(size_t i=0;i<count;++i)
    if(const int d=int(uint8_t(left[i]))-int(uint8_t(right[i]))) return d;
  return left.size()<right.size() ? -1 : left.size()!=right.size() ? 1 : 0;
}
std::array<float,4> ReadVector(const PostGuestMemory& memory,uint32_t address) {
  return {memory.Float(address),memory.Float(Add(address,4)),memory.Float(Add(address,8)),memory.Float(Add(address,12))};
}
std::array<float,4> Scalar(float value) { return {value,0,0,1}; } // 820B5718 stores (v,0,0,1)
void RequireFinite(const NativePostTone& tone,const char* what) {
  for(const auto* vector:{&tone.middle_gray,&tone.luminance_white,&tone.tone_map,&tone.luminance_vector})
    if(!std::all_of(vector->begin(),vector->end(),[](float v) { return std::isfinite(v); }))
      throw std::runtime_error(std::format("{} tone constants are not finite",what));
}
std::vector<float> Floats(const std::array<float,4>& value) { return {value.begin(),value.end()}; }
}

std::optional<std::array<float,4>> ReadPostPoolVector(const PostGuestMemory& memory,const char* name) {
  const std::string wanted=name;
  const auto pool=memory.Word(T::kPoolGlobal);
  if(!pool) throw std::runtime_error("no shared effect pool at 8257C02C");
  const auto head=memory.Word(Add(pool,T::kPoolHead));
  // 821A1D80: lower_bound from the root [head+4].
  auto result=head,node=memory.Word(Add(head,T::kNodeParent));
  for(size_t depth=0;!memory.Byte(Add(node,T::kNodeIsNil));++depth) {
    if(depth>=T::kMaxDepth) throw std::runtime_error("shared effect pool map is deeper than expected");
    if(CompareKey(ReadKey(memory,Add(node,T::kNodeKey)),wanted)<0) node=memory.Word(Add(node,T::kNodeRight));
    else { result=node; node=memory.Word(Add(node,T::kNodeLeft)); }
  }
  // 821A1EB0: end, or a key the name sorts before, is not found.
  if(result==head || CompareKey(wanted,ReadKey(memory,Add(result,T::kNodeKey)))<0) return std::nullopt;
  const auto value=Add(result,T::kNodeValue);
  const auto data=memory.Word(Add(value,T::kValueData)),count=memory.Word(Add(value,T::kValueCount));
  if(!data || !count) throw std::runtime_error(std::format("shared parameter {} has no value ({:#x}, count {})",wanted,data,count));
  return ReadVector(memory,data);
}

NativePostTone ReadNativePostTone(const PostGuestMemory& memory) {
  NativePostTone tone; tone.origin=NativePostToneOrigin::Pool;
  const auto read=[&](const char* name,std::array<float,4>& out) {
    const auto value=ReadPostPoolVector(memory,name);
    if(!value) throw std::runtime_error(std::format("shared parameter {} is not registered",name));
    out=*value;
  };
  read(kPostMiddleGray,tone.middle_gray); read(kPostLuminanceWhite,tone.luminance_white);
  read(kPostToneMap,tone.tone_map); read(kPostLuminanceVector,tone.luminance_vector);
  RequireFinite(tone,"pool");
  return tone;
}

NativePostTone ReadNativePostTonePreset(const PostGuestMemory& memory,uint32_t index) {
  const auto manager=memory.Word(T::kManagerGlobal);
  if(!manager) throw std::runtime_error("no environment manager at 82578678");
  const auto table=memory.Word(Add(manager,T::kManagerTable));
  if(!table) throw std::runtime_error("environment manager has no preset table");
  const auto count=memory.Word(Add(table,T::kTableCount));
  if(int32_t(index)<0 || int32_t(index)>=int32_t(count)) throw std::runtime_error(std::format("preset {} outside 0..{}",index,count));
  // 820B5790: r31 = [table+28] + index*172 + table (wrapping adds).
  const auto preset=memory.Word(Add(table,T::kTableBase))+index*T::kPresetStride+table;
  NativePostTone tone; tone.origin=NativePostToneOrigin::Preset;
  tone.middle_gray=Scalar(memory.Float(Add(preset,T::kPresetMiddleGray)));
  tone.luminance_white=Scalar(memory.Float(Add(preset,T::kPresetLuminanceWhite)));
  tone.tone_map=Scalar(memory.Float(Add(preset,T::kPresetToneMap)));
  const auto v=ReadVector(memory,Add(preset,T::kPresetLuminanceVector));
  tone.luminance_vector={v[0],v[1],v[2],1.0f};
  RequireFinite(tone,"preset");
  return tone;
}

PostFinishInput ReadNativePostInput(const PostGuestMemory& memory,uint32_t self,const NativePostTone& tone) {
  using L=PostFinishLayout;
  if(!self) throw std::runtime_error("no post owner");
  PostFinishInput in; in.self=self;
  const auto owner=memory.Word(L::kOwnerGlobal);
  in.screen_width=std::bit_cast<int32_t>(memory.Word(Add(owner,L::kOwnerWidth)));
  in.screen_height=std::bit_cast<int32_t>(memory.Word(Add(owner,L::kOwnerHeight)));
  in.scene_texture=memory.Word(Add(owner,L::kOwnerSceneTexture));
  const auto record=[&](uint32_t address) {
    PostFinishRecord r; r.address=address;
    r.texture=memory.Word(Add(address,L::kRecordTexture));
    r.width=std::bit_cast<int32_t>(memory.Word(Add(address,L::kRecordWidth)));
    r.height=std::bit_cast<int32_t>(memory.Word(Add(address,L::kRecordHeight)));
    r.texel_x=memory.Float(Add(address,L::kRecordTexelX));
    r.texel_y=memory.Float(Add(address,L::kRecordTexelY));
    return r;
  };
  for(uint32_t k=0;k<L::kFirstPyramidCount;++k) in.first[k]=record(Add(self,L::kFirstPyramid+k*L::kRecordStride));
  const auto records=memory.Word(Add(self,L::kSecondPyramid));
  const auto count=memory.Word(Add(self,L::kSecondCount));
  if(!records || !count || count>L::kMaxSecondCount)
    throw std::runtime_error(std::format("second pyramid {:#x} count {} unsupported",records,count));
  for(uint32_t i=0;i<count;++i) in.second.push_back(record(Add(records,i*L::kRecordStride)));
  in.blur=record(Add(self,L::kBlurTarget));
  in.blur_vertical=record(Add(self,L::kBlurTargetVertical));
  const auto technique=[&](uint32_t effect) { return memory.Word(Add(self,effect+L::kTechniqueOffset)); };
  in.mono_technique=technique(L::kMonoEffect); in.downsample_tone_technique=technique(L::kDownsampleToneEffect);
  in.downsample_technique=technique(L::kDownsampleEffect); in.tone_technique=technique(L::kToneEffect);
  in.blur_technique=technique(L::kBlurEffect); in.bloom_technique=technique(L::kBloomEffect);
  in.tone=tone.Scalars(); in.tone_source=PostToneSource::SharedPool;
  return in;
}

NativePostFrame BuildNativePostFrame(const PostFinishPlan& plan,const NativePostTone& tone) {
  if(plan.passes.size()<PostFinishLayout::kFirstPyramidCount+5 || plan.passes.back().kind!=PostPassKind::Bloom)
    throw std::runtime_error(std::format("native post: {} planned passes do not end with the bloom",plan.passes.size()));
  RequireFinite(tone,"native post");
  NativePostFrame frame; frame.plan=plan; frame.tone=tone;
  for(size_t p=0;p<plan.passes.size();++p) {
    const auto& pass=plan.passes[p];
    const bool bloom=p+1==plan.passes.size();
    if(bloom!=(pass.kind==PostPassKind::Bloom) || bloom==(pass.target!=0) || !pass.quad || !pass.technique ||
       pass.target_width<=0 || pass.target_height<=0)
      throw std::runtime_error(std::format("native post: pass {} ({}) has no target, extent, quad or technique",p,PostPassName(pass.kind)));
    NativePostDraw draw;
    draw.kind=pass.kind; draw.pass=p; draw.target=pass.target; draw.target_texture=pass.target_texture;
    draw.width=pass.target_width; draw.height=pass.target_height; draw.output=bloom;
    draw.technique=pass.technique; draw.quad=*pass.quad;
    for(const auto& setter:pass.setters) switch(setter.kind) {
      case PostSetterKind::Vectors:
        if(setter.values.empty() || setter.values.size()%4)
          throw std::runtime_error(std::format("native post: {} {} is not whole float4s",PostPassName(pass.kind),setter.name));
        draw.constants.push_back({setter.name,setter.values}); break;
      case PostSetterKind::Texture: {
        // A later setter of the same name replaces the value, as the parameter record does.
        auto found=std::find_if(draw.textures.begin(),draw.textures.end(),[&](const auto& t) { return t.name==setter.name; });
        if(found!=draw.textures.end()) found->handle=setter.texture;
        else draw.textures.push_back({setter.name,setter.texture});
        break;
      }
      case PostSetterKind::Sampler:
        if(setter.values.size()!=1) throw std::runtime_error(std::format("native post: sampler {} has no value",setter.name));
        draw.samplers.push_back({setter.name,setter.words,setter.values[0]}); break;
    }
    for(const auto& [name,value]:{std::pair{kPostMiddleGray,&tone.middle_gray},std::pair{kPostLuminanceWhite,&tone.luminance_white},
                                  std::pair{kPostToneMap,&tone.tone_map},std::pair{kPostLuminanceVector,&tone.luminance_vector}})
      draw.constants.push_back({name,Floats(*value)});
    if(!NativePostOwnResolveAllowed(draw))
      throw std::runtime_error(std::format("native post: pass {} ({}) reads its own target's texture {:#x}",p,PostPassName(pass.kind),draw.target_texture));
    frame.draws.push_back(std::move(draw));
  }
  return frame;
}
const NativePostConstant* FindPostConstant(const NativePostDraw& draw,const char* name) {
  for(const auto& constant:draw.constants) if(constant.name==name) return &constant;
  return nullptr;
}
const NativePostTexture* FindPostTexture(const NativePostDraw& draw,const char* name) {
  for(const auto& texture:draw.textures) if(texture.name==name) return &texture;
  return nullptr;
}

bool NativePostReadsOwnResolve(const NativePostDraw& draw) {
  return !draw.output && draw.target_texture &&
    std::any_of(draw.textures.begin(),draw.textures.end(),[&](const auto& t) { return t.handle==draw.target_texture; });
}
bool NativePostOwnResolveAllowed(const NativePostDraw& draw) {
  return !NativePostReadsOwnResolve(draw) ||
    draw.kind==PostPassKind::DownsampleTone || draw.kind==PostPassKind::BlurHorizontal;
}

NativePostFrame RecordNativePost(NativePostSink& sink,const PostFinishInput& input,const NativePostTone& tone) {
  auto in=input; in.tone=tone.Scalars(); in.tone_source=PostToneSource::SharedPool;
  auto frame=BuildNativePostFrame(BuildPostFinishPlan(in),tone);
  for(const auto& draw:frame.draws) sink.Draw(draw);
  return frame;
}
NativePostFrame RecordNativePost(NativePostSink& sink,const PostGuestMemory& memory,uint32_t self) {
  const auto tone=ReadNativePostTone(memory);
  return RecordNativePost(sink,ReadNativePostInput(memory,self,tone),tone);
}
}

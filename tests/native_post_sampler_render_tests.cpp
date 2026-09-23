// The full-frame post's sampler state on WARP: one retail PS_Downsample pass
// drawn with the sampler the guest's sequence leaves in its slot (point, U/V
// clamp; ResolveNativePostSamplers) and with the fallback the post used to bind
// when the shared bindings were empty (linear clamp). The reduction is a
// non-integer one (10x10 -> 4x4, as 80x45 -> 40x22 and 40x22 -> 16x16 are in
// the game), where the four taps fall between texels: point sampling returns
// whole source texels and the fallback blends neighbours - the bloom/exposure
// difference the shadow render measured.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "native_graphics/d3d12_backend.h"
#include "native_graphics/native_full_frame_post.h"
#include "native_graphics/native_render_backend.h"
#include "native_graphics/native_sampler_decode.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace edf::native;

namespace {
int failures=0;
void Check(bool ok,const std::string& message) {
  if(ok) return;
  ++failures;
  std::cerr<<"FAIL: "<<message<<'\n';
}
ComPtr<ID3DBlob> Compile(const char* source,const char* entry,const char* profile) {
  ComPtr<ID3DBlob> code,errors;
  if(FAILED(D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,entry,profile,0,0,&code,&errors)))
    throw std::runtime_error(std::string("shader compile failed: ")+
      (errors?static_cast<const char*>(errors->GetBufferPointer()):"no detail"));
  return code;
}
std::span<const uint8_t> Bytes(ID3DBlob& blob) {
  return {static_cast<const uint8_t*>(blob.GetBufferPointer()),blob.GetBufferSize()};
}
// PS_Downsample of the retail PostEffect.dxsl, as the native compiler sees it:
// four taps at In.UV0 + m_DownsampleUVOffset[i], averaged. VS_Main copies XY
// and the UV.
constexpr const char* kDownsample=R"(
cbuffer Post : register(b0) { float4 m_DownsampleUVOffset[4]; };
Texture2D m_DiffuseTexture0 : register(t0);
SamplerState m_DiffuseTexture0_Sampler : register(s0);
struct Varying { float4 Pos : SV_POSITION; float2 UV0 : TEXCOORD0; };
Varying VS_Main(float2 Pos : POSITION, float2 UV0 : TEXCOORD0) {
  Varying Out; Out.Pos=float4(Pos,0,1); Out.UV0=UV0; return Out;
}
float4 PS_Downsample(Varying In) : SV_TARGET {
  float4 C=m_DiffuseTexture0.Sample(m_DiffuseTexture0_Sampler,In.UV0+m_DownsampleUVOffset[0].xy);
  C+=m_DiffuseTexture0.Sample(m_DiffuseTexture0_Sampler,In.UV0+m_DownsampleUVOffset[1].xy);
  C+=m_DiffuseTexture0.Sample(m_DiffuseTexture0_Sampler,In.UV0+m_DownsampleUVOffset[2].xy);
  C+=m_DiffuseTexture0.Sample(m_DiffuseTexture0_Sampler,In.UV0+m_DownsampleUVOffset[3].xy);
  return C*0.25f;
}
)";
constexpr uint32_t kSource=10,kTarget=4;
uint8_t SourceTexel(uint32_t x,uint32_t y) { return uint8_t((x*37u+y*101u)%251u); }
// The sampler the guest's device holds in slot 0 for a Downsample draw: the
// post's base (reset + 2D scope) and the activation of the record 820B1028
// set with 821BCF28(0.0,0,0,0), bound to a one-level texture.
NativeBackendSamplerDesc GuestDownsampleSampler() {
  auto device=NativePostSamplerBase();
  NativeMaterialSamplerOperation record;
  record.slot=0; record.settings={0,0,0,0}; record.texture_lod=0;
  device[0]=ApplyNativeMaterialSampler(device[0],record);
  return DecodeNativeGuestSampler(NativeFilteringKey(device[0].words,-1));
}
// The removed FullPostSampler fallback for a record without a 821BCF28 setter.
NativeBackendSamplerDesc FallbackSampler() {
  NativeBackendSamplerDesc desc{};
  desc.min=desc.mag=NativeBackendFilter::Linear; desc.mip=NativeBackendFilter::Point;
  desc.u=desc.v=desc.w=NativeBackendAddress::Clamp; desc.max_lod=0;
  return desc;
}
// Point sampling on the CPU: the texel containing the coordinate, clamped.
float PointSample(float u,float v) {
  const auto texel=[](float c) { return std::clamp(int(std::floor(c*float(kSource))),0,int(kSource)-1); };
  return float(SourceTexel(uint32_t(texel(u)),uint32_t(texel(v))));
}
std::vector<uint8_t> Draw(NativeRenderBackend& backend,const NativeBackendSamplerDesc& sampler_desc) {
  NativeBackendTextureDesc source_desc{};
  source_desc.width=source_desc.height=kSource; source_desc.format=DXGI_FORMAT_R8G8B8A8_UNORM;
  std::vector<uint8_t> pixels(kSource*kSource*4);
  for(uint32_t y=0;y<kSource;++y) for(uint32_t x=0;x<kSource;++x) {
    auto* p=&pixels[(y*kSource+x)*4];
    p[0]=p[1]=p[2]=SourceTexel(x,y); p[3]=255;
  }
  const auto source=backend.CreateTexture(source_desc,pixels);
  NativeBackendTextureDesc target_desc{};
  target_desc.width=target_desc.height=kTarget; target_desc.format=DXGI_FORMAT_R8G8B8A8_UNORM; target_desc.render_target=true;
  const auto target=backend.CreateRenderTarget(target_desc);
  // 820B01E8's quad and offsets for a kSource x kSource source, as the plan builds them.
  const auto quad=PostDownsampleQuad(float(kSource),float(kSource));
  const auto offsets=PostDownsampleOffsets(float(kSource),float(kSource));
  std::vector<float> vertices;
  for(const int corner:{0,1,2,0,2,3}) vertices.insert(vertices.end(),quad.begin()+corner*4,quad.begin()+corner*4+4);
  NativeBackendBufferDesc buffer_desc{}; buffer_desc.bytes=vertices.size()*4; buffer_desc.vertex=true;
  const auto buffer=backend.CreateBuffer(buffer_desc,{reinterpret_cast<const uint8_t*>(vertices.data()),vertices.size()*4});
  static const auto vs=Compile(kDownsample,"VS_Main","vs_5_0");
  static const auto ps=Compile(kDownsample,"PS_Downsample","ps_5_0");
  const NativeBackendInputElement layout[]{
    {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,false,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,false,0}};
  NativeBackendPipelineDesc pipeline_desc{};
  pipeline_desc.vertex=Bytes(*vs.Get()); pipeline_desc.pixel=Bytes(*ps.Get());
  pipeline_desc.vertex_id=0x5001; pipeline_desc.pixel_id=0x5002;
  pipeline_desc.input_layout=layout; pipeline_desc.input_layout_id=0x5003;
  pipeline_desc.state={0x10001,0,0,0,15,0};
  pipeline_desc.topology=NativeBackendTopology::TriangleList;
  pipeline_desc.render_targets=1; pipeline_desc.rtv_format[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
  auto& pipeline=backend.CreatePipeline(pipeline_desc);
  auto& sampler=backend.CreateSampler(sampler_desc);
  NativeBackendRenderTarget* colors[]{target.get()};
  backend.BeginFrame();
  auto& recorder=backend.Recorder();
  recorder.SetRenderTargets(colors,nullptr);
  recorder.SetViewport({0,0,float(kTarget),float(kTarget),0,1});
  recorder.ClearColor(*target,{0,0,0,0});
  recorder.SetPipeline(pipeline);
  recorder.SetVertexBuffer(0,*buffer,16,0);
  recorder.SetConstants(NativeBackendStage::Pixel,0,{reinterpret_cast<const uint8_t*>(offsets.data()),offsets.size()*4});
  recorder.SetTexture(NativeBackendStage::Pixel,0,source.get());
  recorder.SetSampler(NativeBackendStage::Pixel,0,&sampler);
  recorder.Draw(6,0);
  backend.Submit();
  for(const auto& message:backend.DrainValidationMessages()) Check(false,"D3D12 validation: "+message);
  return backend.ReadRenderTarget(*target);
}
}

int main() {
  std::cout<<std::unitbuf;
  try {
    // The derived state: point everywhere, U/V clamp, W wrap, base level only.
    const auto guest=GuestDownsampleSampler();
    Check(guest.min==NativeBackendFilter::Point && guest.mag==NativeBackendFilter::Point && guest.mip==NativeBackendFilter::Point,
      "the guest's Downsample sampler is not point");
    Check(guest.u==NativeBackendAddress::Clamp && guest.v==NativeBackendAddress::Clamp && guest.w==NativeBackendAddress::Wrap,
      "the guest's Downsample sampler is not U/V clamp, W wrap");
    Check(guest.min_lod==0 && guest.max_lod==0 && guest.mip_lod_bias==0 && guest.max_anisotropy==1,
      "the guest's Downsample sampler LOD/bias/anisotropy");
    RegisterNativeD3D12Backend();
    auto backend=CreateNativeRenderBackend("d3d12-warp");
    if(!backend) throw std::runtime_error("no WARP backend");
    const auto with_guest=Draw(*backend,guest);
    const auto with_fallback=Draw(*backend,FallbackSampler());
    Check(with_guest.size()==kTarget*kTarget*4 && with_fallback.size()==with_guest.size(),"readback size");
    // Point: each output is the mean of four whole source texels, at the taps
    // the quad and offsets name (pixel centres (x+0.5)/4, half-texel UV bias).
    const float texel=1.0f/float(kSource);
    int worst_guest=0,worst_fallback=0,differing=0;
    for(uint32_t y=0;y<kTarget;++y) for(uint32_t x=0;x<kTarget;++x) {
      const float u=(float(x)+0.5f)/float(kTarget)+texel*0.5f,v=(float(y)+0.5f)/float(kTarget)+texel*0.5f;
      const float expected=(PointSample(u,v)+PointSample(u+texel,v)+PointSample(u+texel,v+texel)+PointSample(u,v+texel))*0.25f;
      const int g=with_guest[(y*kTarget+x)*4],f=with_fallback[(y*kTarget+x)*4];
      worst_guest=std::max(worst_guest,int(std::lround(std::abs(float(g)-expected))));
      worst_fallback=std::max(worst_fallback,int(std::lround(std::abs(float(f)-expected))));
      differing+=g!=f;
    }
    Check(worst_guest<=1,"the guest sampler's reduction is not the point-sampled mean: off by "+std::to_string(worst_guest));
    Check(worst_fallback>=8 && differing>=kTarget*kTarget/2,
      "the fallback sampler should blend neighbouring texels here: worst "+std::to_string(worst_fallback)+
      ", "+std::to_string(differing)+" pixels differ");
    std::cout<<"post sampler render: guest within "<<worst_guest<<", fallback off by up to "<<worst_fallback
             <<" in "<<differing<<"/"<<kTarget*kTarget<<" pixels\n";
    backend.reset();
  } catch(const std::exception& error) {
    std::cerr<<"unexpected: "<<error.what()<<'\n';
    return 1;
  }
  std::cout<<"native post sampler render tests: "<<failures<<" failures\n";
  return failures?1:0;
}

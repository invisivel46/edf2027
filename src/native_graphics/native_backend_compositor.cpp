#include "native_backend_compositor.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace edf::native {
namespace {
const char* kSource=R"SHADER(
struct Varying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Varying VS(uint index : SV_VertexID) {
  Varying v;
  v.uv=float2((index<<1)&2,index&2);
  v.position=float4(v.uv.x*2-1,1-v.uv.y*2,0,1);
  return v;
}
Texture2D<float4> frame : register(t0);
SamplerState frame_sampler : register(s0);
cbuffer DisplayGamma : register(b0) { uint4 gamma_mode; float4 gamma_values[1024]; };
float4 GammaTexel(int2 at,uint2 size) {
  float4 c=frame.Load(int3(clamp(at,int2(0,0),int2(size)-1),0));
  uint3 code=(uint3)(saturate(c.rgb)*(gamma_mode.x==1?255.0:1023.0)+0.5);
  return float4(gamma_values[code.r].r,gamma_values[code.g].g,gamma_values[code.b].b,c.a);
}
float4 PS(Varying v) : SV_TARGET {
  if(gamma_mode.x==0) return frame.SampleLevel(frame_sampler,v.uv,0);
  uint2 size; frame.GetDimensions(size.x,size.y);
  float2 p=v.uv*size-0.5;
  int2 at=(int2)floor(p); float2 f=frac(p);
  // Gamma belongs to source scanout pixels, before host window scaling.
  return lerp(lerp(GammaTexel(at,size),GammaTexel(at+int2(1,0),size),f.x),
              lerp(GammaTexel(at+int2(0,1),size),GammaTexel(at+int2(1,1),size),f.x),f.y);
}
)SHADER";
Microsoft::WRL::ComPtr<ID3DBlob> Compile(const char* entry,const char* profile) {
  Microsoft::WRL::ComPtr<ID3DBlob> code,errors;
  if(FAILED(D3DCompile(kSource,std::strlen(kSource),"native_backend_compositor",nullptr,nullptr,
                      entry,profile,0,0,&code,&errors)))
    throw std::runtime_error(errors?static_cast<const char*>(errors->GetBufferPointer()):
                             "backend compositor shader compilation failed");
  return code;
}
}
NativeBackendCompositor::NativeBackendCompositor(NativeRenderBackend& backend) {
  const auto vertex=Compile("VS","vs_5_0"),pixel=Compile("PS","ps_5_0");
  NativeBackendPipelineDesc desc{};
  desc.vertex={static_cast<const uint8_t*>(vertex->GetBufferPointer()),vertex->GetBufferSize()};
  desc.pixel={static_cast<const uint8_t*>(pixel->GetBufferPointer()),pixel->GetBufferSize()};
  desc.vertex_id=0x434f4d505653; desc.pixel_id=0x434f4d505053;
  desc.state={0x10001,0,0,0,15,0};
  desc.topology=NativeBackendTopology::TriangleList;
  desc.render_targets=1; desc.rtv_format[0]=28;
  pipeline_=&backend.CreatePipeline(desc);
  NativeBackendSamplerDesc sampler{};
  sampler.u=sampler.v=sampler.w=NativeBackendAddress::Clamp;
  sampler.mip=NativeBackendFilter::Point;
  sampler_=&backend.CreateSampler(sampler);
}
void NativeBackendCompositor::Draw(NativeBackendRecorder& recorder,NativeBackendTexture& source,
    NativeBackendRenderTarget& destination,bool preserve_aspect,const NativeDisplayGamma* gamma) {
  if(!source.width() || !source.height() || !destination.width() || !destination.height() ||
     destination.texture()==&source)
    throw std::runtime_error("invalid backend compositor source/target");
  struct GammaConstants { uint32_t mode[4]{}; float values[1024][4]{}; } constants;
  if(gamma) {
    const bool table=gamma->mode()==NativeDisplayGamma::Mode::Table256;
    constants.mode[0]=table?1:2;
    for(uint32_t code=0;code<(table?256u:1024u);++code)
      for(uint32_t channel=0;channel<3;++channel)
        constants.values[code][channel]=gamma->EvaluateNormalizedCode(channel,code);
  }
  const float scale=(std::min)(float(destination.width())/source.width(),
                               float(destination.height())/source.height());
  const float width=preserve_aspect?source.width()*scale:float(destination.width());
  const float height=preserve_aspect?source.height()*scale:float(destination.height());
  NativeBackendRenderTarget* targets[]={&destination};
  recorder.SetRenderTargets(targets,nullptr);
  recorder.ClearColor(destination,{0,0,0,1});
  recorder.SetViewport({(destination.width()-width)*.5f,(destination.height()-height)*.5f,width,height,0,1});
  recorder.SetScissor({},false);
  recorder.SetPipeline(*pipeline_);
  recorder.SetConstants(NativeBackendStage::Pixel,0,
    {reinterpret_cast<const uint8_t*>(&constants),sizeof(constants)});
  recorder.SetTexture(NativeBackendStage::Pixel,0,&source);
  recorder.SetSampler(NativeBackendStage::Pixel,0,sampler_);
  recorder.Draw(3,0);
  recorder.SetTexture(NativeBackendStage::Pixel,0,nullptr);
}
}

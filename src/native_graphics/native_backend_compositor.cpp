#include "native_backend_compositor.h"
#include "native_display_layout.h"
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
// gamma_mode: x gamma (0 none, 1 table256, 2 piecewise), y filter (0 bilinear,
// 1 nearest, 2 area), z area taps per axis. fit_filter.xy: one window pixel in uv.
cbuffer DisplayGamma : register(b0) { uint4 gamma_mode; float4 gamma_values[1024]; float4 fit_filter; };
float4 GammaTexel(int2 at,uint2 size) {
  float4 c=frame.Load(int3(clamp(at,int2(0,0),int2(size)-1),0));
  uint3 code=(uint3)(saturate(c.rgb)*(gamma_mode.x==1?255.0:1023.0)+0.5);
  return float4(gamma_values[code.r].r,gamma_values[code.g].g,gamma_values[code.b].b,c.a);
}
float4 Bilinear(float2 uv) {
  if(gamma_mode.x==0) return frame.SampleLevel(frame_sampler,uv,0);
  uint2 size; frame.GetDimensions(size.x,size.y);
  float2 p=uv*size-0.5;
  int2 at=(int2)floor(p); float2 f=frac(p);
  // Gamma belongs to source scanout pixels, before host window scaling.
  return lerp(lerp(GammaTexel(at,size),GammaTexel(at+int2(1,0),size),f.x),
              lerp(GammaTexel(at+int2(0,1),size),GammaTexel(at+int2(1,1),size),f.x),f.y);
}
float4 PS(Varying v) : SV_TARGET {
  if(gamma_mode.y==1) {
    // Whole-factor magnification: each source pixel becomes an exact block.
    uint2 size; frame.GetDimensions(size.x,size.y);
    int2 at=(int2)floor(v.uv*size);
    if(gamma_mode.x==0) return frame.Load(int3(clamp(at,int2(0,0),int2(size)-1),0));
    return GammaTexel(at,size);
  }
  if(gamma_mode.y==2) {
    // Minification: average taps x taps bilinear samples spread over the
    // window pixel's footprint, at most one source texel apart, instead of
    // one bilinear sample that skips source pixels.
    uint taps=max(gamma_mode.z,1u);
    float4 sum=0;
    for(uint y=0;y<taps;++y)
      for(uint x=0;x<taps;++x)
        sum+=Bilinear(v.uv+((float2(x,y)+0.5)/taps-0.5)*fit_filter.xy);
    return sum/float(taps*taps);
  }
  return Bilinear(v.uv);
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
    NativeBackendRenderTarget& destination,bool preserve_aspect,const NativeDisplayGamma* gamma,bool smart_filter) {
  if(!source.width() || !source.height() || !destination.width() || !destination.height() ||
     destination.texture()==&source)
    throw std::runtime_error("invalid backend compositor source/target");
  struct GammaConstants { uint32_t mode[4]{}; float values[1024][4]{}; float filter[4]{}; } constants;
  if(gamma) {
    const bool table=gamma->mode()==NativeDisplayGamma::Mode::Table256;
    constants.mode[0]=table?1:2;
    for(uint32_t code=0;code<(table?256u:1024u);++code)
      for(uint32_t channel=0;channel<3;++channel)
        constants.values[code][channel]=gamma->EvaluateNormalizedCode(channel,code);
  }
  float x=0,y=0,width=0,height=0;
  if(smart_filter) {
    const auto fit=ComputeNativePresentFit(source.width(),source.height(),destination.width(),destination.height(),
                                           preserve_aspect,true);
    x=fit.x; y=fit.y; width=fit.width; height=fit.height;
    constants.mode[1]=uint32_t(fit.filter); constants.mode[2]=fit.taps;
    if(width>0 && height>0) { constants.filter[0]=1.0f/width; constants.filter[1]=1.0f/height; }
  } else {
    const float scale=(std::min)(float(destination.width())/source.width(),
                                 float(destination.height())/source.height());
    width=preserve_aspect?source.width()*scale:float(destination.width());
    height=preserve_aspect?source.height()*scale:float(destination.height());
    x=(destination.width()-width)*.5f; y=(destination.height()-height)*.5f;
  }
  NativeBackendRenderTarget* targets[]={&destination};
  recorder.SetRenderTargets(targets,nullptr);
  recorder.ClearColor(destination,{0,0,0,1});
  recorder.SetViewport({x,y,width,height,0,1});
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

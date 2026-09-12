#include "d3d11_frame_compositor.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace edf::native {
NativeFrameCompositor::NativeFrameCompositor(ID3D11Device& device) : device_(&device) {
  Effect effect;
  effect.source=R"(
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
)";
  vertex_=CompileNativeShader(device,effect,{false,"VS","vs_5_0"},"native_frame_compositor.fx");
  pixel_=CompileNativeShader(device,effect,{true,"PS","ps_5_0"},"native_frame_compositor.fx");
  ValidateNativeShaderLink(vertex_,pixel_);
  D3D11_SAMPLER_DESC sampler{};
  sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler.MaxLOD=D3D11_FLOAT32_MAX; sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
  if(FAILED(device.CreateSamplerState(&sampler,&sampler_)))
    throw std::runtime_error("native frame sampler creation failed");
  D3D11_RASTERIZER_DESC raster{};
  raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
  if(FAILED(device.CreateRasterizerState(&raster,&raster_)))
    throw std::runtime_error("native frame rasterizer creation failed");
  D3D11_BUFFER_DESC constants{};
  constants.ByteWidth=16+1024*16;
  constants.Usage=D3D11_USAGE_DYNAMIC; constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
  constants.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  if(FAILED(device.CreateBuffer(&constants,nullptr,&gamma_constants_)))
    throw std::runtime_error("native gamma constant buffer creation failed");
}
void NativeFrameCompositor::Draw(ID3D11DeviceContext& context, ID3D11ShaderResourceView& source,
    ID3D11RenderTargetView& destination, bool preserve_aspect,const NativeDisplayGamma* gamma) {
  Microsoft::WRL::ComPtr<ID3D11Device> owner;
  context.GetDevice(&owner);
  if(owner.Get()!=device_.Get() || context.GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)
    throw std::runtime_error("native frame compositor context mismatch");
  source.GetDevice(owner.ReleaseAndGetAddressOf());
  if(owner.Get()!=device_.Get()) throw std::runtime_error("native frame source device mismatch");
  destination.GetDevice(owner.ReleaseAndGetAddressOf());
  if(owner.Get()!=device_.Get()) throw std::runtime_error("native frame target device mismatch");
  D3D11_SHADER_RESOURCE_VIEW_DESC source_view{}; source.GetDesc(&source_view);
  D3D11_RENDER_TARGET_VIEW_DESC target_view{}; destination.GetDesc(&target_view);
  if(source_view.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D ||
     target_view.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D ||
     source_view.Format!=DXGI_FORMAT_R8G8B8A8_UNORM || target_view.Format!=DXGI_FORMAT_R8G8B8A8_UNORM)
    throw std::runtime_error("native frame compositor requires tone-mapped RGBA8 2D views");
  Microsoft::WRL::ComPtr<ID3D11Resource> input,output;
  source.GetResource(&input); destination.GetResource(&output);
  if(input.Get()==output.Get()) throw std::runtime_error("native frame source aliases destination");
  Microsoft::WRL::ComPtr<ID3D11Texture2D> input_texture,output_texture;
  if(FAILED(input.As(&input_texture)) || FAILED(output.As(&output_texture)))
    throw std::runtime_error("native frame compositor requires textures");
  D3D11_TEXTURE2D_DESC in{},out{};
  input_texture->GetDesc(&in); output_texture->GetDesc(&out);
  const UINT source_width=(std::max)(1u,in.Width>>source_view.Texture2D.MostDetailedMip);
  const UINT source_height=(std::max)(1u,in.Height>>source_view.Texture2D.MostDetailedMip);
  const UINT target_width=(std::max)(1u,out.Width>>target_view.Texture2D.MipSlice);
  const UINT target_height=(std::max)(1u,out.Height>>target_view.Texture2D.MipSlice);
  const float scale=(std::min)(float(target_width)/source_width,float(target_height)/source_height);
  const float width=preserve_aspect?source_width*scale:float(target_width);
  const float height=preserve_aspect?source_height*scale:float(target_height);
  const D3D11_VIEWPORT viewport{(target_width-width)*0.5f,(target_height-height)*0.5f,width,height,0,1};
  struct GammaConstants { uint32_t mode[4]{}; float values[1024][4]{}; } constants;
  if(gamma) {
    const bool table=gamma->mode()==NativeDisplayGamma::Mode::Table256;
    constants.mode[0]=table?1:2;
    for(uint32_t code=0;code<(table?256u:1024u);++code)
      for(uint32_t channel=0;channel<3;++channel)
        constants.values[code][channel]=gamma->EvaluateNormalizedCode(channel,code);
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if(FAILED(context.Map(gamma_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))
    throw std::runtime_error("native gamma upload failed");
  std::memcpy(mapped.pData,&constants,sizeof(constants));
  context.Unmap(gamma_constants_.Get(),0);
  // Clear all shader stages/UAVs/SO/predication too, not just the state this
  // pass happens to set. A previous scene pass must not affect presentation.
  context.ClearState();
  const float black[]{0,0,0,1}; context.ClearRenderTargetView(&destination,black);
  auto* target=&destination; context.OMSetRenderTargets(1,&target,nullptr);
  context.RSSetState(raster_.Get()); context.RSSetViewports(1,&viewport);
  context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context.VSSetShader(vertex_.vertex.Get(),nullptr,0);
  context.PSSetShader(pixel_.pixel.Get(),nullptr,0);
  auto* gamma_buffer=gamma_constants_.Get(); context.PSSetConstantBuffers(0,1,&gamma_buffer);
  auto* view=&source; context.PSSetShaderResources(0,1,&view);
  auto* sampler=sampler_.Get(); context.PSSetSamplers(0,1,&sampler);
  context.Draw(3,0);
  view=nullptr; context.PSSetShaderResources(0,1,&view);
}
}

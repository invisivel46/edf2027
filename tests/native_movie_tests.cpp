#include "native_graphics/d3d11_backend.h"
#include "native_graphics/movie_effect.h"
#include "native_graphics/native_movie_bindings.h"
#include "native_graphics/d3d11_bindings.h"
#include "native_graphics/d3d11_quads.h"
#include "native_graphics/d3d11_texture.h"
#include <bit>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace edf::native;
using Microsoft::WRL::ComPtr;
void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<size_t N> std::span<const uint8_t> Bytes(const std::array<float,N>& data) {
  return {reinterpret_cast<const uint8_t*>(data.data()),sizeof(data)};
}
int main() {
  try {
    for (size_t pixel_entry:{size_t(1),size_t(2)}) {
    const std::array<uint32_t,4> coefficients=pixel_entry==1
      ? std::array<uint32_t,4>{0x3fe575a2,0x400731db,0xbf0872f2,0xbe5a5b23}
      : std::array<uint32_t,4>{0x3fcc4aa0,0x40010a0c,0xbf503a5e,0xbec960c5};
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    Require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
      D3D11_SDK_VERSION,&device,nullptr,&context)),"movie device creation");
    // Render targets are created through the backend now; adopting this device
    // keeps them usable by the direct D3D11 calls the rest of this test makes.
    auto backend=AdoptNativeD3D11Backend(*device.Get(),*context.Get());
    Require(bool(backend),"adopted backend");
    const auto effect=MakeNativeMovieEffect();
    ShaderBindings vs(*device.Get(),CompileNativeShader(*device.Get(),effect,effect.entries[0],"movie.fx"));
    ShaderBindings ps(*device.Get(),CompileNativeShader(*device.Get(),effect,effect.entries[pixel_entry],"movie.fx"));
    ValidateNativeShaderLink(vs.shader(),ps.shader());
    QuadStream vertices(*device.Get(),vs.shader());
    std::array<float,16> transform{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const std::array<float,16> projection{.5f,0,0,-1, 0,-1,0,1, 0,0,1,0, 0,0,0,1};
    const std::array<float,4> factor{.5f,.75f,1,.75f};
    NativeMovieBindings plan(vs,ps);
    std::array<uint8_t,160> registers{};
    std::array<uint8_t,16> color{};
    auto pack=[](std::span<uint8_t> bytes,std::span<const float> values) {
      for(size_t lane=0;lane<values.size();++lane) {
        const auto word=std::bit_cast<uint32_t>(values[lane]);
        for(size_t b=0;b<4;++b) bytes[lane*4+b]=uint8_t(word>>(24-b*8));
      }
    };
    auto update_constants=[&] {
      pack(registers,transform); pack(std::span(registers).subspan(64),projection); pack(color,factor);
      plan.SetConstants(vs,ps,registers,color);
    };
    update_constants();
    std::array<ComPtr<ID3D11Texture2D>,3> planes;
    std::array<ComPtr<ID3D11ShaderResourceView>,3> views;
    D3D11_TEXTURE2D_DESC plane_desc{};
    plane_desc.Width=plane_desc.Height=plane_desc.MipLevels=plane_desc.ArraySize=plane_desc.SampleDesc.Count=1;
    plane_desc.Format=DXGI_FORMAT_R32_FLOAT; plane_desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    for (size_t i=0;i<3;++i) {
      Require(SUCCEEDED(device->CreateTexture2D(&plane_desc,nullptr,&planes[i])) &&
              SUCCEEDED(device->CreateShaderResourceView(planes[i].Get(),nullptr,&views[i])),"movie plane creation");
    }
    D3D11_SAMPLER_DESC sampler_desc{}; sampler_desc.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU=sampler_desc.AddressV=sampler_desc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    ComPtr<ID3D11SamplerState> sampler;
    Require(SUCCEEDED(device->CreateSamplerState(&sampler_desc,&sampler)),"movie sampler creation");
    for (size_t i=0;i<3;++i) { plan.SetTexture(ps,i,views[i].Get()); plan.SetSampler(ps,i,sampler.Get()); }
    auto target=CreateNativeRenderTarget(*backend,4,2,DXGI_FORMAT_R16G16B16A16_FLOAT);
    auto* rtv=target.target.Get(); context->OMSetRenderTargets(1,&rtv,nullptr);
    CreateNativeRenderState(*device.Get(),{0x10001,0,0,0,15,0}).Bind(*context.Get());
    MakeNativeViewport(0,0,4,2,0,1,false,{}).Bind(*context.Get());
    // 8242B988 supplies TL,TR,BR,BL as primitive 5 (triangle fan).
    // The four-corner quad adapter expands the exact same two triangles.
    const std::array<float,16> fan{0,0,0,0, 4,0,1,0, 4,2,1,1, 0,2,0,1};
    std::vector<uint8_t> guest;
    for (float value:fan) { const auto word=std::bit_cast<uint32_t>(value); for(int b=24;b>=0;b-=8) guest.push_back(uint8_t(word>>b)); }
    auto draw=[&](std::array<float,3> yuv) {
      for(size_t i=0;i<3;++i) context->UpdateSubresource(planes[i].Get(),0,nullptr,&yuv[i],4,0);
      vs.Bind(*context.Get()); ps.Bind(*context.Get()); vertices.Draw(*context.Get(),guest);
      return ReadNativeColorPixel(*context.Get(),*target.surface.Get(),3,0);
    };
    auto black=draw({.0625f,.5f,.5f});
    Require(black==std::array<float,4>{0,0,0,.75f},"movie neutral black/alpha mismatch");
    const auto colored=draw({.5f,.25f,.75f});
    const auto coefficient=[](uint32_t bits){ return std::bit_cast<float>(bits); };
    const float luminance=(.5f-.0625f)*coefficient(0x3f950a7f);
    const std::array<float,4> expected{(luminance+.25f*coefficient(coefficients[0]))*.5f,
      (luminance+.25f*coefficient(coefficients[2])-.25f*coefficient(coefficients[3]))*.75f,
      luminance-.25f*coefficient(coefficients[1]),.75f};
    for(size_t i=0;i<4;++i) Require(std::abs(colored[i]-expected[i])<.001f,"movie chroma/coefficient/tint mismatch");
    transform[3]=2; update_constants();
    const float clear[]{-2,-2,-2,-2}; context->ClearRenderTargetView(rtv,clear);
    draw({.0625f,.5f,.5f});
    Require(ReadNativeColorPixel(*context.Get(),*target.surface.Get(),0,0)==std::array<float,4>{-2,-2,-2,-2},"movie transform failed to move left edge");
    Require(ReadNativeColorPixel(*context.Get(),*target.surface.Get(),3,0)==black,"movie transformed projection mismatch");
    // A spatial pattern catches swapped planes, flipped UVs and incorrect
    // interpolation that constant 1x1 inputs cannot expose.
    transform[3]=0; update_constants();
    ps.ClearTextures();
    plane_desc.Width=4; plane_desc.Height=2;
    const std::array<std::array<float,8>,3> pattern{{
      {.0625f,.25f,.5f,.75f, .875f,.625f,.375f,.125f},
      {.5f,.375f,.625f,.25f, .75f,.5f,.375f,.625f},
      {.5f,.625f,.375f,.75f, .25f,.375f,.625f,.5f}
    }};
    for(size_t i=0;i<3;++i) {
      views[i].Reset(); planes[i].Reset();
      D3D11_SUBRESOURCE_DATA initial{};
      initial.pSysMem=pattern[i].data(); initial.SysMemPitch=4*sizeof(float);
      Require(SUCCEEDED(device->CreateTexture2D(&plane_desc,&initial,&planes[i])) &&
              SUCCEEDED(device->CreateShaderResourceView(planes[i].Get(),nullptr,&views[i])),"patterned movie plane creation");
      plan.SetTexture(ps,i,views[i].Get());
      plan.SetSampler(ps,i,sampler.Get());
    }
    vs.Bind(*context.Get()); ps.Bind(*context.Get());
    vertices.Draw(*context.Get(),guest);
    for(uint32_t y=0;y<2;++y) for(uint32_t x=0;x<4;++x) {
      const size_t index=y*4+x;
      const float l=(pattern[0][index]-.0625f)*coefficient(0x3f950a7f);
      const float u=pattern[1][index]-.5f, v=pattern[2][index]-.5f;
      const std::array<float,4> wanted{
        (l+v*coefficient(coefficients[0]))*factor[0],
        (l+v*coefficient(coefficients[2])+u*coefficient(coefficients[3]))*factor[1],
        (l+u*coefficient(coefficients[1]))*factor[2], factor[3]};
      const auto actual=ReadNativeColorPixel(*context.Get(),*target.surface.Get(),x,y);
      for(size_t i=0;i<4;++i)
        Require(std::abs(actual[i]-wanted[i])<.001f,"movie patterned YUV sampling mismatch");
    }
    auto reject=[](auto&& action) {
      bool rejected=false; try { action(); } catch(const std::runtime_error&) { rejected=true; }
      Require(rejected,"invalid movie binding input accepted");
    };
    const auto before=vs.ReadFloatVector("Params");
    reject([&]{plan.SetConstants(vs,ps,std::span(registers).first(159),color);});
    ShaderBindings foreign_ps(*device.Get(),CompileNativeShader(*device.Get(),effect,effect.entries[pixel_entry],"movie.fx"));
    registers[128]=0x3f;
    reject([&]{plan.SetConstants(vs,foreign_ps,registers,color);});
    Require(vs.ReadFloatVector("Params")==before,"invalid movie binding mutated vertex constants");
    reject([&]{plan.SetTexture(ps,3,views[0].Get());});
    reject([&]{plan.SetSampler(ps,SIZE_MAX,sampler.Get());});
    reject([&]{plan.SetTexture(foreign_ps,0,views[0].Get());});
    reject([&]{plan.SetSampler(foreign_ps,0,sampler.Get());});
    std::cout<<"Native recovered XUI movie "<<(pixel_entry==1 ? "HD" : "SD")
             <<" transform, YUV planes, tint and alpha passed\n";
    }
  } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}

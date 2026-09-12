#pragma once
#include "native_graphics/d3d11_quads.h"
#include "native_graphics/d3d11_texture.h"
#include <bit>
#include <cmath>

// Disc-source arithmetic check, not an Xbox compiler equivalence claim.
inline void CheckRetailPostArithmetic(ID3D11Device& device,const std::filesystem::path& root) {
  using namespace edf::native;
  const auto path=root/"PostEffect.dxsl";
  const auto effect=ParseEffect(ReadSourceAsset(path));
  if(EffectSourceFingerprint(effect.source)!=0x6b7926f9747c6933ull)
    throw std::runtime_error("post arithmetic oracle requires the audited retail source");
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context; device.GetImmediateContext(&context);
  auto entry=[&](const std::string& name)->ShaderEntry {
    for(const auto& candidate:effect.entries) if(candidate.name==name) return candidate;
    throw std::runtime_error("missing retail post entry: "+name);
  };
  ShaderBindings vertex(device,CompileNativeShader(device,effect,entry("VS_Main"),path));
  QuadStream quad(device,vertex.shader());
  auto guest=[](std::span<const float> values) {
    std::vector<uint8_t> bytes(values.size()*4);
    for(size_t i=0;i<values.size();++i) {
      const auto bits=std::bit_cast<uint32_t>(values[i]);
      for(size_t b=0;b<4;++b) bytes[i*4+b]=uint8_t(bits>>(24-b*8));
    }
    return bytes;
  };
  const std::array<float,16> corners{-1,1,0,0, 1,1,1,0, 1,-1,1,1, -1,-1,0,1};
  const auto vertices=guest(corners);
  D3D11_SAMPLER_DESC sampler_desc{};
  sampler_desc.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU=sampler_desc.AddressV=sampler_desc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.MaxAnisotropy=1; sampler_desc.ComparisonFunc=D3D11_COMPARISON_ALWAYS;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
  if(FAILED(device.CreateSamplerState(&sampler_desc,&sampler))) throw std::runtime_error("post oracle sampler");
  auto input=[&](const std::array<float,4>& value) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=desc.Height=desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.Usage=D3D11_USAGE_IMMUTABLE; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{value.data(),16,0};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    if(FAILED(device.CreateTexture2D(&desc,&data,&texture)) ||
       FAILED(device.CreateShaderResourceView(texture.Get(),nullptr,&view)))
      throw std::runtime_error("post oracle input");
    return view;
  };
  auto target=CreateNativeRenderTarget(device,1,1,DXGI_FORMAT_R16G16B16A16_FLOAT);
  D3D11_VIEWPORT viewport{0,0,1,1,0,1}; context->RSSetViewports(1,&viewport);
  D3D11_RASTERIZER_DESC raster_desc{}; raster_desc.FillMode=D3D11_FILL_SOLID;
  raster_desc.CullMode=D3D11_CULL_NONE; raster_desc.DepthClipEnable=true;
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster;
  if(FAILED(device.CreateRasterizerState(&raster_desc,&raster))) throw std::runtime_error("post oracle raster");
  context->RSSetState(raster.Get());
  const std::array<float,4> luminance{.6154f,.7154f,.0721f,0};
  size_t checked=0;
  for(const auto* name:{"PS_Bloom","PS_Tone","PS_Downsample_Tone"}) {
    ShaderBindings pixel(device,CompileNativeShader(device,effect,entry(name),path));
    auto set=[&](const char* key,std::array<float,4> value) { pixel.SetGuestFloatRegisters(key,guest(value)); };
    set("g_PostEffect_MiddleGray",{.8f,0,0,0});
    set("g_PostEffect_LuminanceWhite",{1.5f,0,0,0});
    set("g_PostEffect_ToneMap",{.8f,0,0,0});
    set("g_PostEffect_LuminanceVector",luminance);
    const std::array<float,16> zero_offsets{};
    pixel.SetGuestFloatRegisters("m_DownsampleUVOffset",guest(zero_offsets));
    for(const float tone:{0.01f,.2861328f,.5751953f,1.0f}) {
      const std::array<float,4> scene{.5019531f,.5419922f,1.25f,.75f};
      const std::array<float,4> bloom{.41568628f,.42745098f,.44313726f,1};
      const std::array<float,4> history{tone,1,1,1};
      const auto scene_view=input(scene),bloom_view=input(bloom),tone_view=input(history);
      pixel.TrySetTexture("m_DiffuseTexture0_Sampler",scene_view.Get());
      pixel.TrySetTexture("m_DiffuseTexture1_Sampler",bloom_view.Get());
      pixel.TrySetTexture("m_Tone_Sampler",tone_view.Get());
      pixel.TrySetTexture("m_OldTone_Sampler",tone_view.Get());
      for(const auto* binding:{"m_DiffuseTexture0_Sampler","m_DiffuseTexture1_Sampler","m_Tone_Sampler","m_OldTone_Sampler"})
        pixel.TrySetSampler(binding,sampler.Get());
      if(!pixel.HasAllTextureInputs()) throw std::runtime_error("post oracle unbound input");
      auto* rtv=target.target.Get(); context->OMSetRenderTargets(1,&rtv,nullptr);
      vertex.Bind(*context.Get()); pixel.Bind(*context.Get()); quad.Draw(*context.Get(),vertices);
      const auto actual=ReadNativeColorPixel(*context.Get(),*target.surface.Get(),0,0);
      auto expected=scene;
      if(std::string_view(name)=="PS_Downsample_Tone") {
        expected[0]=.8f+(expected[0]-.8f)*.8f;
        for(size_t c=0;c<4;++c) expected[c]=history[c]+(expected[c]-history[c])*.025f;
      } else {
        for(size_t c=0;c<3;++c) {
          expected[c]*=.8f/(tone+.001f);
          expected[c]*=1+expected[c]/1.5f;
          expected[c]/=1+expected[c];
        }
        if(std::string_view(name)=="PS_Bloom") {
          for(size_t c=0;c<3;++c) expected[c]+=bloom[c];
          expected[3]=1;
        } else {
          for(auto& c:expected) c*=c;
          float illuminance=0;
          for(size_t c=0;c<3;++c) illuminance+=expected[c]*luminance[c];
          for(auto& c:expected) c*=illuminance;
        }
      }
      for(size_t c=0;c<4;++c)
        if(!std::isfinite(actual[c]) || std::abs(actual[c]-expected[c])>0.002f+std::abs(expected[c])*.002f)
          throw std::runtime_error(std::string("retail post arithmetic mismatch: ")+name+" channel="+std::to_string(c)+
            " actual="+std::to_string(actual[c])+" expected="+std::to_string(expected[c]));
      ++checked;
    }
  }
  // Spatial control: retail 820B01E8 adds half a SOURCE texel to the quad UVs
  // and uses offsets (0,0),(1/w,0),(1/w,1/h),(0,1/h). Integer-center rasterization
  // should average each disjoint 2x2 block. Check both host-center conventions.
  {
    ShaderBindings pixel(device,CompileNativeShader(device,effect,entry("PS_Downsample"),path));
    std::array<float,64> texels{};
    for(size_t p=0;p<16;++p) for(size_t c=0;c<4;++c) texels[p*4+c]=float(p)/16;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=desc.Height=4; desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.Usage=D3D11_USAGE_IMMUTABLE; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{texels.data(),64,0};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    if(FAILED(device.CreateTexture2D(&desc,&data,&texture)) || FAILED(device.CreateShaderResourceView(texture.Get(),nullptr,&view)))
      throw std::runtime_error("spatial oracle texture");
    pixel.SetTexture("m_DiffuseTexture0_Sampler",view.Get());
    pixel.SetSampler("m_DiffuseTexture0_Sampler",sampler.Get());
    const std::array<float,16> offsets{0,0,0,0, .25f,0,0,0, .25f,.25f,0,0, 0,.25f,0,0};
    pixel.SetGuestFloatRegisters("m_DownsampleUVOffset",guest(offsets));
    auto shifted_uv=corners;
    for(size_t v=0;v<4;++v) { shifted_uv[v*4+2]+=.125f; shifted_uv[v*4+3]+=.125f; }
    const auto spatial_vertices=guest(shifted_uv);
    auto spatial_target=CreateNativeRenderTarget(device,2,2,DXGI_FORMAT_R16G16B16A16_FLOAT);
    const std::array<float,4> expected{.15625f,.28125f,.65625f,.78125f};
    bool unadjusted_differs=false;
    for(const float shift:{0.0f,.5f}) {
      D3D11_VIEWPORT spatial_view{shift,shift,2,2,0,1}; context->RSSetViewports(1,&spatial_view);
      auto* rtv=spatial_target.target.Get(); context->OMSetRenderTargets(1,&rtv,nullptr);
      vertex.Bind(*context.Get()); pixel.Bind(*context.Get()); quad.Draw(*context.Get(),spatial_vertices);
      for(uint32_t y=0;y<2;++y) for(uint32_t x=0;x<2;++x) {
        const auto actual=ReadNativeColorPixel(*context.Get(),*spatial_target.surface.Get(),x,y);
        if(!std::isfinite(actual[0])) throw std::runtime_error("nonfinite spatial oracle result");
        if(shift==0) unadjusted_differs|=std::abs(actual[0]-expected[y*2+x])>.001f;
        else if(std::abs(actual[0]-expected[y*2+x])>.001f)
          throw std::runtime_error("integer-center spatial oracle mismatch");
      }
    }
    if(!unadjusted_differs) throw std::runtime_error("spatial negative control did not distinguish pixel centers");
    // Positive sentinel over the entire target catches omitted first/last rows
    // and columns at fractional viewport boundaries, including the 1x1 history.
    const std::array<float,4> constant{.375f,.5f,.625f,.75f};
    const auto constant_view=input(constant);
    pixel.SetTexture("m_DiffuseTexture0_Sampler",constant_view.Get());
    for(const uint32_t extent:{1u,2u,3u,8u}) {
      auto coverage=CreateNativeRenderTarget(device,extent,extent,DXGI_FORMAT_R16G16B16A16_FLOAT);
      const float clear[]{0,0,0,0}; context->ClearRenderTargetView(coverage.target.Get(),clear);
      auto* rtv=coverage.target.Get(); context->OMSetRenderTargets(1,&rtv,nullptr);
      D3D11_VIEWPORT coverage_view{.5f,.5f,float(extent),float(extent),0,1};
      context->RSSetViewports(1,&coverage_view);
      vertex.Bind(*context.Get()); pixel.Bind(*context.Get()); quad.Draw(*context.Get(),spatial_vertices);
      for(uint32_t y=0;y<extent;++y) for(uint32_t x=0;x<extent;++x) {
        const auto actual=ReadNativeColorPixel(*context.Get(),*coverage.surface.Get(),x,y);
        for(size_t c=0;c<4;++c) if(!std::isfinite(actual[c]) || std::abs(actual[c]-constant[c])>.001f)
          throw std::runtime_error("half-pixel viewport lost full target coverage");
      }
    }
    std::cout<<"Spatial control: unchanged retail UVs mis-sample on D3D11; +0.5 viewport shift restores four expected block averages\n";
    // Rectangular/odd-source reductions include the actual 80x45 -> 40x22
    // stage. Compute integer-center point-sampling footprints independently.
    for(const auto dimensions:{std::array<UINT,4>{6,4,3,2}, {4,6,2,3}, {2,8,1,4}, {8,2,4,1}, {80,45,40,22}}) {
      const auto sw=dimensions[0],sh=dimensions[1],dw=dimensions[2],dh=dimensions[3];
      std::vector<float> pixels(size_t(sw)*sh*4);
      for(UINT y=0;y<sh;++y) for(UINT x=0;x<sw;++x) {
        const size_t at=(size_t(y)*sw+x)*4;
        pixels[at]=float(x)/128; pixels[at+1]=float(y)/64;
        pixels[at+2]=float((x+y)%3)/4; pixels[at+3]=1;
      }
      auto source_desc=desc; source_desc.Width=sw; source_desc.Height=sh;
      D3D11_SUBRESOURCE_DATA source_data{pixels.data(),sw*16,0};
      Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> source_view;
      if(FAILED(device.CreateTexture2D(&source_desc,&source_data,&source)) || FAILED(device.CreateShaderResourceView(source.Get(),nullptr,&source_view)))
        throw std::runtime_error("rectangular oracle texture");
      pixel.SetTexture("m_DiffuseTexture0_Sampler",source_view.Get());
      const std::array<float,16> sample_offsets{0,0,0,0, 1.f/sw,0,0,0, 1.f/sw,1.f/sh,0,0, 0,1.f/sh,0,0};
      pixel.SetGuestFloatRegisters("m_DownsampleUVOffset",guest(sample_offsets));
      auto uv=corners;
      for(size_t v=0;v<4;++v) { uv[v*4+2]+=.5f/sw; uv[v*4+3]+=.5f/sh; }
      const auto rect_vertices=guest(uv);
      auto output=CreateNativeRenderTarget(device,dw,dh,DXGI_FORMAT_R16G16B16A16_FLOAT);
      const float clear[]{-1,-1,-1,-1}; context->ClearRenderTargetView(output.target.Get(),clear);
      auto* rtv=output.target.Get(); context->OMSetRenderTargets(1,&rtv,nullptr);
      D3D11_VIEWPORT rect_view{.5f,.5f,float(dw),float(dh),0,1}; context->RSSetViewports(1,&rect_view);
      vertex.Bind(*context.Get()); pixel.Bind(*context.Get()); quad.Draw(*context.Get(),rect_vertices);
      for(UINT y=0;y<dh;++y) for(UINT x=0;x<dw;++x) {
        const auto actual=ReadNativeColorPixel(*context.Get(),*output.surface.Get(),x,y);
        const double px=.5+double(x)*sw/dw,py=.5+double(y)*sh/dh;
        // At an exact ideal texel boundary, interpolating the float UV endpoints
        // can place the sample infinitesimally on either side. Accept either
        // coherent footprint only there, never arbitrary channel tolerances.
        const bool tie_x=std::abs(px-std::round(px))<1e-9;
        const bool tie_y=std::abs(py-std::round(py))<1e-9;
        bool matched=false;
        for(int by=0;by<=(tie_y?1:0);++by) for(int bx=0;bx<=(tie_x?1:0);++bx) {
          std::array<float,4> reference{};
          for(int oy=0;oy<2;++oy) for(int ox=0;ox<2;++ox) {
            const auto sx=UINT(std::clamp(int(std::floor(px))+ox-bx,0,int(sw)-1));
            const auto sy=UINT(std::clamp(int(std::floor(py))+oy-by,0,int(sh)-1));
            for(size_t c=0;c<4;++c) reference[c]+=pixels[(size_t(sy)*sw+sx)*4+c]*.25f;
          }
          bool footprint_matches=true;
          for(size_t c=0;c<4;++c) footprint_matches&=std::isfinite(actual[c]) && std::abs(actual[c]-reference[c])<=.001f;
          matched|=footprint_matches;
        }
        if(!matched)
          throw std::runtime_error("rectangular point footprint mismatch "+std::to_string(sw)+"x"+std::to_string(sh)+
            " at "+std::to_string(x)+","+std::to_string(y));
      }
    }
    std::cout<<"Five rectangular spatial cases passed, including 80x45 -> 40x22\n";
  }
  context->ClearState();
  std::cout<<checked<<" retail post GPU arithmetic cases passed (native compiler; not Xbox equivalence)\n";
}

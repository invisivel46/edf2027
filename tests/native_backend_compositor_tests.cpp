#include "native_graphics/native_backend_compositor.h"
#include "native_graphics/d3d11_backend.h"
#include "native_graphics/d3d12_backend.h"
#include "native_graphics/d3d11_bindings.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace edf::native;
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void CheckD3D12FrameTransfer() {
  NativeD3D12Options options{}; options.prefer_warp=true; options.debug_layer=true;
  auto producer=CreateNativeD3D12Backend(options),consumer=CreateNativeD3D12Backend(options);
  NativeBackendTextureDesc desc{}; desc.width=1280; desc.height=720; desc.format=28;
  auto copied=consumer->CreateSharedSurface(desc);
  // Replace the source, including its fence, and exercise repeated imported
  // reads. The consumer retains its snapshot after the producer overwrites.
  for(unsigned generation=0;generation<3;++generation) {
    desc.width=1280+generation;
    auto shared=producer->CreateSharedSurface(desc);
    auto imported=consumer->OpenSharedTexture(shared->texture_handle(),desc);
    auto snapshot=consumer->CreateTexture(desc,{});
    auto target_desc=desc; target_desc.render_target=true;
    auto target=producer->CreateRenderTarget(target_desc);
    for(unsigned frame=0;frame<8;++frame) {
      producer->BeginFrame();
      producer->Recorder().ClearColor(*target,{1,0,0,1});
      producer->Recorder().CopyToShared(*shared,*target);
      producer->Submit(); producer->SignalShared(*shared);
      Require(consumer->WaitSharedFence(shared->fence_handle(),shared->value()),"D3D12 producer fence import failed");
      consumer->BeginFrame();
      consumer->Recorder().CopyTexture(*snapshot,*imported);
      consumer->Recorder().ReleaseSharedTexture(*imported);
      consumer->Submit(); consumer->SignalShared(*copied);
      Require(producer->WaitSharedFence(copied->fence_handle(),copied->value()),"D3D12 consumer fence import failed");
      producer->BeginFrame();
      producer->Recorder().ClearColor(*target,{0,1,0,1});
      producer->Recorder().CopyToShared(*shared,*target);
      producer->Submit();
      const auto pixels=consumer->ReadTexture(*snapshot);
      Require(pixels.size()==size_t(desc.width)*desc.height*4,"D3D12 snapshot dimensions mismatch");
      for(size_t at=0;at<pixels.size();at+=4)
        Require(pixels[at]==255 && pixels[at+1]==0 && pixels[at+2]==0 && pixels[at+3]==255,
                "D3D12 owned snapshot changed after producer overwrite");
    }
  }
  Require(producer->DrainValidationMessages().empty() && consumer->DrainValidationMessages().empty(),
          "D3D12 frame transfer validation failed");
}
std::vector<uint8_t> Render(NativeRenderBackend& backend,const NativeDisplayGamma* gamma,bool fit) {
  NativeBackendCompositor compositor(backend);
  NativeBackendTextureDesc input{}; input.width=2; input.height=1; input.format=28;
  const uint8_t pixels[]{32,64,128,255,192,128,64,255};
  auto texture=backend.CreateTexture(input,pixels);
  auto output=input; output.width=8; output.height=8; output.render_target=true;
  auto target=backend.CreateRenderTarget(output);
  backend.BeginFrame();
  compositor.Draw(backend.Recorder(),*texture,*target,fit,gamma);
  backend.Submit();
  auto result=backend.ReadRenderTarget(*target);
  Require(result.size()==256,"wrong compositor output size");
  if(fit) Require(result[0]==0 && result[1]==0 && result[2]==0 && result[3]==255,
                  "compositor letterbox is not opaque black");
  const auto at=(4*8)*4;
  for(unsigned c=0;c<3;++c) {
    const int expected=gamma?int(std::lround(gamma->EvaluateNormalizedCode(c,
      gamma->mode()==NativeDisplayGamma::Mode::Table256?pixels[c]:
      uint32_t(std::lround(pixels[c]*1023.0/255.0)))*255)):pixels[c];
    Require(std::abs(int(result[at+c])-expected)<=1,"compositor gamma or source sampling differs");
  }
  Require(backend.DrainValidationMessages().empty(),"compositor GPU validation failed");
  return result;
}
int main() {
  try {
    Effect effect;
    effect.source="cbuffer Values : register(b3) { float4 tint; }; float4 PS() : SV_TARGET { return tint; }";
    auto shader=CompileNativeShader(static_cast<ID3D11Device*>(nullptr),effect,{true,"PS","ps_5_0"},"cpu_bindings.fx");
    Require(shader.bytecode && shader.reflection && !shader.pixel && !shader.vertex,
            "CPU shader compilation allocated a D3D11 executable shader");
    ShaderBindings bindings(static_cast<ID3D11Device*>(nullptr),std::move(shader));
    const float tint[]{.25f,.5f,.75f,1};
    bindings.SetConstant("tint",{reinterpret_cast<const uint8_t*>(tint),sizeof(tint)});
    const auto constants=bindings.ConstantImages();
    Require(constants.size()==1 && constants[0].slot==3 && constants[0].bytes.size()==sizeof(tint),
            "CPU shader binding reflection lost the constant register");
    auto d11=CreateNativeD3D11Backend({true,true});
    NativeD3D12Options options{}; options.prefer_warp=true; options.debug_layer=true;
    auto d12=CreateNativeD3D12Backend(options);
    uint8_t table[1536]{};
    for(unsigned c=0;c<3;++c) for(unsigned i=0;i<256;++i) {
      const auto value=uint16_t(((i*i*1023)/(255*255))<<6);
      table[(c*256+i)*2]=uint8_t(value>>8); table[(c*256+i)*2+1]=uint8_t(value);
    }
    const auto gamma=NativeDisplayGamma::Decode(table,NativeDisplayGamma::Mode::Table256);
    const auto piecewise=NativeDisplayGamma::Decode(table,NativeDisplayGamma::Mode::Piecewise128);
    for(auto* curve:{static_cast<const NativeDisplayGamma*>(nullptr),&gamma,&piecewise})
      for(bool fit:{false,true}) {
        auto a=Render(*d11,curve,fit),b=Render(*d12,curve,fit);
        Require(a.size()==b.size(),"compositor sizes differ");
        for(size_t i=0;i<a.size();++i)
          Require(std::abs(int(a[i])-int(b[i]))<=1,"D3D11/D3D12 composition differs");
      }
    std::cout << "Backend compositor tests passed\n";
    CheckD3D12FrameTransfer();
    std::cout << "Full-size D3D12 frame transfer tests passed\n";
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}

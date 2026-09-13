// Deterministic native D3D11 draw/readback via WARP. The separate disc checker
// uses hardware. Neither path loads ReXGlue or emulates Xbox GPU commands.
#include "native_graphics/d3d11_bindings.h"
#include "native_graphics/d3d11_texture.h"
#include "native_graphics/d3d11_backend.h"
#include "native_graphics/d3d11_sampler.h"
#include "native_graphics/d3d11_render_state.h"
#include "native_graphics/d3d11_completion.h"
#include "native_graphics/d3d11_signals.h"
#include "native_graphics/d3d11_gpu_timer.h"
#include <array>
#include <cstring>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <thread>
#include <type_traits>
#include <bit>

using Microsoft::WRL::ComPtr;
using namespace edf::native;
static_assert(!std::is_copy_constructible_v<NativeCompletionQueue>);
static_assert(!std::is_copy_assignable_v<NativeCompletionQueue>);
static_assert(!std::is_copy_constructible_v<NativeGpuTimer>);
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class T> std::span<const uint8_t> Bytes(const T& value) {
  return {reinterpret_cast<const uint8_t*>(&value), sizeof(value)};
}
std::vector<uint8_t> GuestRegisters(std::span<const float> values) {
  std::vector<uint8_t> bytes((values.size() + 3) / 4 * 16, 0);
  const auto* source = reinterpret_cast<const uint8_t*>(values.data());
  for (size_t i = 0; i < values.size(); ++i)
    for (size_t b = 0; b < 4; ++b) bytes[i * 4 + b] = source[i * 4 + 3 - b];
  return bytes;
}
void DdsWord(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<uint8_t>(value >> (i * 8));
}
int main() {
  try {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1,
                                       D3D11_SDK_VERSION, &device, nullptr, &context)), "device creation");
    // Textures are created through the backend now; adopting this device keeps
    // them usable by the direct D3D11 draws the rest of this test makes.
    auto backend = AdoptNativeD3D11Backend(*device.Get(), *context.Get());
    Require(bool(backend), "adopted backend");
    Effect effect;
    effect.source = R"(
cbuffer VertexData : register(b7) { float2 offset; };
cbuffer PixelData : register(b4) { float gain; float3 bias; float4 tint; };
cbuffer PackedData : register(b6) {
  column_major float2x3 columnM;
  row_major float2x3 rowM;
  float2 vectorArr[2];
  float scalarArr[2];
};
Texture2D image : register(t3);
SamplerState filtering : register(s2);
Texture2D mipImage : register(t6);
cbuffer SamplerProbe : register(b5) { float2 probe; float probeLod; };
float4 SamplerPS() : SV_TARGET { return mipImage.SampleLevel(filtering,probe,probeLod); }
float4 VS(uint id : SV_VertexID) : SV_POSITION {
  float2 p = float2((id << 1) & 2, id & 2);
  return float4(p * float2(2,-2) + float2(-1,1) + offset, 0, 1);
}
float4 PS() : SV_TARGET {
  return image.Sample(filtering, float2(0.5,0.5)) * tint * gain + float4(bias,0);
}
float4 PackingPS() : SV_TARGET {
  return float4(columnM[0][2], rowM[1][0], vectorArr[1].y, scalarArr[1]);
}
)";
    ShaderBindings vs(*device.Get(), CompileNativeShader(*device.Get(), effect, {false,"VS","vs_5_0"}, "test.fx"));
    ShaderBindings ps(*device.Get(), CompileNativeShader(*device.Get(), effect, {true,"PS","ps_5_0"}, "test.fx"));
    const std::array<float,2> offset{0,0};
    const std::array<float,3> bias{0,0,0};
    const float gain = 1;
    Require(vs.SetGuestFloatRegisters("offset", GuestRegisters(offset)), "guest vertex parameter");
    Require(ps.SetGuestFloatRegisters("gain", GuestRegisters({&gain, 1})), "guest scalar parameter");
    Require(ps.SetGuestFloatRegisters("bias", GuestRegisters(bias)), "guest vector parameter");
    Require(ps.ReadFloatVector("gain")==std::vector<float>{gain},"scalar diagnostic constant differs");
    Require(ps.ReadFloatVector("bias")==std::vector<float>(bias.begin(),bias.end()),"vector diagnostic constant differs");
    Require(ps.ReadFloatVector("absent").empty(),"absent diagnostic constant invented");
    {
      // The per-draw constant image must be the same bytes Bind would upload.
      // A draw that carries its own copy of these is independent of every other
      // draw on this shader, which is what lets draws be built off the submit
      // thread; a copy that did not match would put the wrong material on
      // screen only once the restructuring happened, which is far too late to
      // find out.
      const auto images = ps.ConstantImages();
      Require(!images.empty(), "no constant image for a shader with constants");
      bool found_gain = false;
      for (const auto& image : images) {
        // The reflected accessors are already verified above, so they are what
        // the image is checked against rather than a hand-computed offset.
        const auto expected = ps.ReadFloatVector("gain");
        if (expected.empty() || image.bytes.size() < sizeof(float)) continue;
        for (size_t at = 0; at + sizeof(float) <= image.bytes.size(); at += sizeof(float)) {
          float value = 0;
          std::memcpy(&value, image.bytes.data() + at, sizeof(value));
          if (value == expected.front()) { found_gain = true; break; }
        }
      }
      Require(found_gain, "the constant image does not contain the value the shader was given");
      // Reading the image must not disturb the binding: the next Bind still has
      // to upload, or a draw built from an image would clear the dirty flag for
      // a draw that never got the bytes.
      Require(ps.ReadFloatVector("gain")==std::vector<float>{gain},
              "reading the constant image changed the binding");
    }
    Require(!ps.SetGuestFloatRegisters("not_in_native_shader", {}), "optimized-out guest parameter");
    const auto resolved_gain=ps.ResolveFloatRegisters("gain");
    Require(resolved_gain.bytes()==16 && ps.Owns(resolved_gain),"resolved scalar ownership/extent");
    Require(ps.SetGuestFloatRegisters(resolved_gain,GuestRegisters({&gain,1})),"resolved scalar upload");
    const auto missing_binding=ps.ResolveFloatRegisters("not_in_native_shader");
    Require(missing_binding.bytes()==0 && !ps.SetGuestFloatRegisters(missing_binding,{}),
      "resolved optimized-out constant");
    bool foreign_rejected=false;
    try { vs.SetGuestFloatRegisters(resolved_gain,GuestRegisters({&gain,1})); }
    catch(const std::runtime_error&) { foreign_rejected=true; }
    Require(foreign_rejected,"foreign shader constant token rejected");
    ShaderBindings::FloatRegisterBinding retired;
    {
      ShaderBindings temporary(*device.Get(),CompileNativeShader(*device.Get(),effect,{true,"PS","ps_5_0"},"test.fx"));
      retired=temporary.ResolveFloatRegisters("gain");
    }
    foreign_rejected=false;
    try { ps.SetGuestFloatRegisters(retired,GuestRegisters({&gain,1})); }
    catch(const std::runtime_error&) { foreign_rejected=true; }
    Require(foreign_rejected,"retired shader generation token rejected");
    foreign_rejected=false;
    try { ps.SetGuestFloatRegisters(ShaderBindings::FloatRegisterBinding{},{}); }
    catch(const std::runtime_error&) { foreign_rejected=true; }
    Require(foreign_rejected,"default constant token rejected");
    foreign_rejected=false;
    try { ps.SetGuestFloatRegisters(resolved_gain,Bytes(gain)); }
    catch(const std::runtime_error&) { foreign_rejected=true; }
    Require(foreign_rejected && ps.ReadFloatVector("gain")==std::vector<float>{gain},
      "resolved size mismatch rejected before mutation");
    bool rejected = false;
    try { ps.SetConstant("tint", Bytes(gain)); } catch (const std::runtime_error&) { rejected = true; }
    Require(rejected, "reject incorrect constant size");
    rejected = false;
    try { ps.SetGuestFloatRegisters("tint", Bytes(gain)); } catch (const std::runtime_error&) { rejected = true; }
    Require(rejected, "reject incomplete guest register");
    rejected = false;
    try { ps.SetConstant("missing", Bytes(gain)); } catch (const std::runtime_error&) { rejected = true; }
    Require(rejected, "reject unknown constant");
    D3D11_TEXTURE2D_DESC texture_desc{};
    texture_desc.Width = texture_desc.Height = texture_desc.MipLevels = texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const std::array<uint8_t,4> texel{255,128,64,255};
    std::vector<uint8_t> dds(132, 0);
    DdsWord(dds,0,0x20534444); DdsWord(dds,4,124); DdsWord(dds,12,1); DdsWord(dds,16,1);
    DdsWord(dds,76,32); DdsWord(dds,80,0x41); DdsWord(dds,88,32);
    DdsWord(dds,92,0xff); DdsWord(dds,96,0xff00); DdsWord(dds,100,0xff0000); DdsWord(dds,104,0xff000000);
    std::copy(texel.begin(), texel.end(), dds.begin()+128);
    auto texture = CreateNativeDdsTexture(*backend, dds);
    auto view = texture.view;
    rejected = false;
    try { CreateNativeDdsTexture(*backend, std::span(dds).first(131)); }
    catch (const std::runtime_error&) { rejected = true; }
    Require(rejected, "truncated DDS payload accepted");
    const uint32_t clamp = (2u<<10)|(2u<<13)|(2u<<16);
    auto sampler_desc = DecodeNativeSampler({clamp,0,15u<<6,0});
    Require(sampler_desc.AddressU == D3D11_TEXTURE_ADDRESS_CLAMP &&
            sampler_desc.AddressV == D3D11_TEXTURE_ADDRESS_CLAMP &&
            sampler_desc.Filter == D3D11_FILTER_MIN_MAG_MIP_POINT,"native point/clamp sampler conversion");
    for (uint32_t min = 0; min < 2; ++min) for (uint32_t mag = 0; mag < 2; ++mag)
      for (uint32_t mip = 0; mip < 2; ++mip) {
        const auto state = DecodeNativeSampler({0,(min<<21)|(mag<<19)|(mip<<23),15u<<6,0});
        Require(state.Filter == D3D11_ENCODE_BASIC_FILTER(min,mag,mip,D3D11_FILTER_REDUCTION_TYPE_STANDARD),
                "native filter combination");
      }
    const auto biased = DecodeNativeSampler({0,5u<<25,(1000u<<12)|(12u<<6)|(2u<<2),0});
    Require(biased.Filter == D3D11_FILTER_ANISOTROPIC && biased.MaxAnisotropy == 16 &&
            biased.MipLODBias == -.75f && biased.MinLOD == 2 && biased.MaxLOD == 12,"native LOD/aniso conversion");
    const auto base_only = DecodeNativeSampler({0,2u<<23,15u<<6,0});
    const SamplerStateWords linear_words{clamp,(1u<<19)|(1u<<21)|(1u<<23),15u<<6,0};
    Require(NativeFilteringKey(linear_words,-1)==linear_words,"default filtering changed");
    for(int mode=0;mode<=5;++mode) {
      const auto key=NativeFilteringKey(linear_words,mode);
      const auto desc=DecodeNativeSampler(key);
      Require(desc.MaxAnisotropy==(mode ? 1u<<(mode-1) : 1u),"native filtering override level");
      Require(desc.Filter==(mode ? D3D11_FILTER_ANISOTROPIC : D3D11_FILTER_MIN_MAG_MIP_LINEAR),"native filtering override filter");
      Require(NativeFilteringKey(key,0)==linear_words,"native filtering override reset");
      ComPtr<ID3D11SamplerState> override_sampler;
      Require(SUCCEEDED(device->CreateSamplerState(&desc,&override_sampler)),"native override sampler creation");
    }
    const SamplerStateWords point_words{clamp,0,15u<<6,0};
    const SamplerStateWords base_words{clamp,(1u<<19)|(1u<<21)|(2u<<23),15u<<6,0};
    Require(NativeFilteringKey(point_words,5)==point_words,"AF changed point sampling");
    Require(NativeFilteringKey(base_words,5)==base_words,"AF changed base-only sampling");
    Require(NativeFilteringKey(linear_words,6)==linear_words,"invalid AF override changed sampling");
    Require(base_only.MinLOD == 0 && base_only.MaxLOD == 0,"native base-only sampling");
    rejected = false;
    try { DecodeNativeSampler({4u<<10,0,0,0}); }
    catch (const std::runtime_error&) { rejected = true; }
    Require(rejected,"unsupported sampler address mode accepted");
    Require(SamplerStateKey({0xfff803ff,0x7ffff,3,0xfffffe00}) == SamplerStateWords{},
            "sampler cache retained resource-specific fields");
    // A backend sampler, because that is what a binding now holds. The decode
    // above still produces the D3D11 desc the D3D11 path wants; this is the
    // neutral one the seam takes.
    NativeBackendSamplerDesc neutral_sampler{};
    neutral_sampler.min=neutral_sampler.mag=neutral_sampler.mip=NativeBackendFilter::Point;
    auto* sampler=&backend->CreateSampler(neutral_sampler);
    Require(!ps.HasAllTextureInputs(),"unbound texture inputs accepted");
    Require(!ps.ReadTexture("image") && !ps.ReadTexture("absent"),"unbound diagnostic texture read");
    Require(!ps.ReadSampler("filtering") && !ps.ReadSampler("absent"),"unbound diagnostic sampler read");
    const auto resolved_image=ps.ResolveResource("image");
    const auto resolved_filter=ps.ResolveResource("filtering");
    Require(ps.TrySetTexture(resolved_image,texture.backend),"resolved texture upload");
    auto* diagnostic_view=ps.ReadTexture("image");
    Require(diagnostic_view==texture.backend.get() && !ps.ReadTexture("filtering"),"diagnostic view snapshot mismatch");
    Require(!ps.HasAllTextureInputs(),"unbound sampler accepted");
    Require(ps.TrySetSampler(resolved_filter,sampler),"resolved sampler upload");
    auto* diagnostic_sampler=ps.ReadSampler("filtering");
    Require(diagnostic_sampler==sampler && !ps.ReadSampler("image"),"diagnostic sampler snapshot mismatch");
    Require(!ps.TrySetSampler(resolved_image,nullptr) && !ps.TrySetTexture(resolved_filter,nullptr),
      "separate texture and sampler slots preserved");
    for(const auto& target:{ShaderBindings::ResourceBinding{},vs.ResolveResource("image")}) {
      bool rejected_texture=false,rejected_sampler=false;
      try { ps.TrySetTexture(target,nullptr); } catch(const std::runtime_error&) { rejected_texture=true; }
      try { ps.TrySetSampler(target,nullptr); } catch(const std::runtime_error&) { rejected_sampler=true; }
      Require(rejected_texture && rejected_sampler,"foreign/default resource tokens rejected");
    }
    ShaderBindings::ResourceBinding retired_resource;
    {
      ShaderBindings temporary(*device.Get(),CompileNativeShader(*device.Get(),effect,{true,"PS","ps_5_0"},"test.fx"));
      retired_resource=temporary.ResolveResource("image");
    }
    bool retired_resource_rejected=false;
    try { ps.TrySetTexture(retired_resource,nullptr); }
    catch(const std::runtime_error&) { retired_resource_rejected=true; }
    Require(retired_resource_rejected && ps.HasAllTextureInputs(),"retired resource token leaves live bindings intact");
    Require(ps.HasAllTextureInputs(),"bound texture inputs missing");
    texture_desc.Width = texture_desc.Height = 4;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target, staging;
    ComPtr<ID3D11RenderTargetView> rtv;
    Require(SUCCEEDED(device->CreateTexture2D(&texture_desc, nullptr, &target)), "render target");
    Require(SUCCEEDED(device->CreateRenderTargetView(target.Get(), nullptr, &rtv)), "render target view");
    texture_desc.BindFlags = 0;
    texture_desc.Usage = D3D11_USAGE_STAGING;
    texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Require(SUCCEEDED(device->CreateTexture2D(&texture_desc, nullptr, &staging)), "readback target");
    auto* output = rtv.Get();
    context->OMSetRenderTargets(1, &output, nullptr);
    D3D11_VIEWPORT viewport{0,0,4,4,0,1};
    context->RSSetViewports(1, &viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto draw = [&](ShaderBindings& pixels, std::array<uint8_t,4> expected) {
      vs.Bind(*context.Get()); pixels.Bind(*context.Get());
      const float clear[]{0,0,0,0};
      context->ClearRenderTargetView(rtv.Get(), clear);
      context->Draw(3,0);
      context->CopyResource(staging.Get(), target.Get());
      D3D11_MAPPED_SUBRESOURCE mapped{};
      Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "readback mapping");
      bool correct = true;
      for (size_t y = 0; y < 4; ++y) for (size_t x = 0; x < 4; ++x) for (size_t c = 0; c < 4; ++c) {
        const auto actual = static_cast<const uint8_t*>(mapped.pData)[y * mapped.RowPitch + x * 4 + c];
        if (std::abs(int(actual) - int(expected[c])) > 1) correct = false;
      }
      context->Unmap(staging.Get(), 0);
      Require(correct, "native draw pixels differ from expected constant/texture values");
    };
    const std::array<float,4> tint1{.25f,.5f,1,.5f}, tint2{.5f,1,.5f,1};
    Require(ps.SetGuestFloatRegisters("tint", GuestRegisters(tint1)), "guest tint upload");
    draw(ps, {64,64,64,128});
    auto source_over = CreateNativeRenderState(*device.Get(),{6u|(7u<<8)|(1u<<16)|(7u<<24),0,0,0,15,0});
    source_over.Bind(*context.Get());
    draw(ps,{32,32,32,128}); // Separate RGB/alpha factors, over cleared black.
    auto red_alpha = CreateNativeRenderState(*device.Get(),{0x10001,0,0,0,9,0});
    red_alpha.Bind(*context.Get()); draw(ps,{64,0,0,128});
    auto no_color = CreateNativeRenderState(*device.Get(),{0x10001,0,0,0,0,0});
    no_color.Bind(*context.Get()); draw(ps,{0,0,0,0});
    auto additive = CreateNativeRenderState(*device.Get(),{1u|(1u<<8)|(1u<<16)|(1u<<24),0,0,0,15,0});
    D3D11_BLEND_DESC additive_desc{}; additive.blend->GetDesc(&additive_desc);
    Require(additive_desc.RenderTarget[0].SrcBlend == D3D11_BLEND_ONE &&
            additive_desc.RenderTarget[0].DestBlend == D3D11_BLEND_ONE,"native additive blend conversion");
    auto depth_cull = CreateNativeRenderState(*device.Get(),{0x10001,2u|4u|(3u<<4),2u|4u,0,15,0});
    D3D11_DEPTH_STENCIL_DESC depth_desc{}; depth_cull.depth->GetDesc(&depth_desc);
    D3D11_RASTERIZER_DESC raster_desc{}; depth_cull.raster->GetDesc(&raster_desc);
    Require(depth_desc.DepthEnable && depth_desc.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL &&
            depth_desc.DepthFunc == D3D11_COMPARISON_LESS_EQUAL,"native depth conversion");
    Require(raster_desc.CullMode == D3D11_CULL_BACK && !raster_desc.FrontCounterClockwise,"native cull conversion");
    for(uint32_t cull=0;cull<3;++cull) for(uint32_t front=0;front<8;++front)
      for(uint32_t back=0;back<8;++back) {
        const auto visible=cull==1?back:front;
        const bool supported=(cull || front==back) && (visible==1 || visible==2);
        bool failed=false;
        D3D11_RASTERIZER_DESC polygon_desc{};
        try {
          auto polygon=CreateNativeRenderState(*device.Get(),
            {0x10001,0,cull|8|(front<<5)|(back<<8),0,15,0});
          polygon.raster->GetDesc(&polygon_desc);
        } catch(const std::runtime_error&) { failed=true; }
        Require(failed!=supported,"native per-face polygon acceptance mismatch");
        if(supported) Require(polygon_desc.FillMode==(visible==1?D3D11_FILL_WIREFRAME:D3D11_FILL_SOLID),
                             "native polygon fill conversion");
        auto inactive=CreateNativeRenderState(*device.Get(),
          {0x10001,0,cull|(front<<5)|(back<<8),0,15,0});
        D3D11_RASTERIZER_DESC desc{}; inactive.raster->GetDesc(&desc);
        Require(desc.FillMode==D3D11_FILL_SOLID,"inactive polygon fields changed fill mode");
      }
    rejected = false;
    try { CreateNativeRenderState(*device.Get(),{0x10001,0,0,8,15,0}); }
    catch (const std::runtime_error&) { rejected = true; }
    Require(rejected,"unimplemented alpha test accepted");
    auto opaque = CreateNativeRenderState(*device.Get(),{0x10001,0,0,0,15,0});
    {
      Effect polygon_effect;
      polygon_effect.source=R"(
float4 TriangleVS(uint id:SV_VertexID):SV_POSITION {
  const float2 p[3]={float2(-.75,-.75),float2(0,.75),float2(.75,-.75)};
  return float4(p[id],.5,1);
}
float4 ReversedVS(uint id:SV_VertexID):SV_POSITION { return TriangleVS(2-id); }
float4 WhitePS():SV_TARGET { return 1; }
)";
      ShaderBindings triangle(*device.Get(),CompileNativeShader(*device.Get(),polygon_effect,{false,"TriangleVS","vs_5_0"},"polygon.fx"));
      ShaderBindings reversed(*device.Get(),CompileNativeShader(*device.Get(),polygon_effect,{false,"ReversedVS","vs_5_0"},"polygon.fx"));
      ShaderBindings white(*device.Get(),CompileNativeShader(*device.Get(),polygon_effect,{true,"WhitePS","ps_5_0"},"polygon.fx"));
      auto polygon_target=CreateNativeRenderTarget(*backend,32,32,DXGI_FORMAT_R8G8B8A8_UNORM);
      D3D11_TEXTURE2D_DESC desc{}; polygon_target.surface->GetDesc(&desc);
      desc.BindFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
      ComPtr<ID3D11Texture2D> pixels;
      Require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&pixels)),"polygon readback allocation");
      auto* polygon_output=polygon_target.target.Get();
      context->OMSetRenderTargets(1,&polygon_output,nullptr);
      const D3D11_VIEWPORT polygon_viewport{0,0,32,32,0,1};
      context->RSSetViewports(1,&polygon_viewport);
      unsigned coverage[2][2][2][3]{};
      for(unsigned winding=0;winding<2;++winding) for(unsigned face=0;face<2;++face)
        for(unsigned wire=0;wire<2;++wire) for(unsigned cull=0;cull<3;++cull) {
          const auto type=wire?1u:2u;
          auto state=CreateNativeRenderState(*device.Get(),
            {0x10001,0,cull|(face<<2)|8|(type<<5)|(type<<8),0,15,0});
          state.Bind(*context.Get());
          (winding?reversed:triangle).Bind(*context.Get()); white.Bind(*context.Get());
          const float clear[4]{}; context->ClearRenderTargetView(polygon_output,clear);
          context->Draw(3,0);
          context->CopyResource(pixels.Get(),polygon_target.surface.Get());
          D3D11_MAPPED_SUBRESOURCE mapped{};
          Require(SUCCEEDED(context->Map(pixels.Get(),0,D3D11_MAP_READ,0,&mapped)),"polygon readback map");
          unsigned count=0;
          for(unsigned y=0;y<32;++y) for(unsigned x=0;x<32;++x)
            count+=static_cast<const uint8_t*>(mapped.pData)[y*mapped.RowPitch+x*4]!=0;
          const auto center=static_cast<const uint8_t*>(mapped.pData)[16*mapped.RowPitch+16*4];
          context->Unmap(pixels.Get(),0);
          coverage[winding][face][wire][cull]=count;
          if(!cull) Require(wire?center==0:center==255,"polygon interior coverage differs");
        }
      for(unsigned winding=0;winding<2;++winding) for(unsigned face=0;face<2;++face) {
        Require(coverage[winding][face][1][0]>0 &&
                coverage[winding][face][1][0]<coverage[winding][face][0][0],"wireframe did not reduce coverage");
        for(unsigned wire=0;wire<2;++wire) {
          const auto* counts=coverage[winding][face][wire];
          Require((counts[1]==0)!=(counts[2]==0),"front/back culling not complementary");
          Require(counts[1]+counts[2]==counts[0],"culling changed surviving polygon coverage");
          Require(counts[1]==coverage[winding^1][face][wire][2] &&
                  counts[1]==coverage[winding][face^1][wire][2],"winding/face reversal did not reverse culling");
        }
      }
      context->OMSetRenderTargets(1,&output,nullptr);
      context->RSSetViewports(1,&viewport);
    }
    opaque.Bind(*context.Get());
    Require(ps.SetGuestFloatRegisters("tint", GuestRegisters(tint2)), "guest tint update");
    draw(ps, {128,128,32,255});
    for(uint32_t factor:{12u,13u,14u,15u}) {
      auto constant=CreateNativeRenderState(*device.Get(),{factor|(factor<<16),0,0,0,15,0});
      rejected=false;
      try { constant.Bind(*context.Get()); } catch(const std::runtime_error&) { rejected=true; }
      Require(rejected,"missing dynamic blend constant accepted");
      constant.Bind(*context.Get(),{.25f,.5f,.75f,.5f});
      if(factor==12) draw(ps,{32,64,24,128});
      else if(factor==13) draw(ps,{96,64,8,128});
      else draw(ps,{64,64,16,128});
      constant.Bind(*context.Get(),{1,1,1,1});
      draw(ps,(factor&1)?std::array<uint8_t,4>{0,0,0,0}:std::array<uint8_t,4>{128,128,32,255});
    }
    rejected=false;
    try { CreateNativeRenderState(*device.Get(),{12u|(14u<<8),0,0,0,15,0}); }
    catch(const std::runtime_error&) { rejected=true; }
    Require(rejected,"mixed RGB constant color/alpha factors accepted");
    opaque.Bind(*context.Get());
    ShaderBindings packing(*device.Get(), CompileNativeShader(*device.Get(), effect,
                           {true,"PackingPS","ps_5_0"}, "test.fx"));
    // Each register is a float4. Deliberate nonzero padding catches accidental
    // tightly-packed arrays/matrices and column/row-major transposition.
    const std::array<float,12> columns{.1f,.2f,9,9, .3f,.4f,9,9, .25f,.6f,9,9};
    const std::array<float,8> rows{.1f,.2f,.3f,9, .5f,.6f,.7f,9};
    const std::array<float,8> vectors{.1f,.2f,9,9, .3f,.75f,9,9};
    const std::array<float,8> scalars{.1f,9,9,9, 1,9,9,9};
    Require(packing.GuestFloatRegisterBytes("columnM")==48,"column matrix register requirement");
    Require(packing.GuestFloatRegisterBytes("rowM")==32,"row matrix register requirement");
    Require(packing.GuestFloatRegisterBytes("vectorArr")==32,"vector array register requirement");
    Require(packing.GuestFloatRegisterBytes("scalarArr")==32,"scalar array register requirement");
    Require(packing.GuestFloatRegisterBytes("unused")==0,"optimized-out register requirement");
    Require(packing.SetGuestFloatRegisters("columnM", GuestRegisters(columns)), "column matrix import");
    Require(packing.SetGuestFloatRegisters("rowM", GuestRegisters(rows)), "row matrix import");
    Require(packing.SetGuestFloatRegisters("vectorArr", GuestRegisters(vectors)), "vector array import");
    Require(packing.SetGuestFloatRegisters("scalarArr", GuestRegisters(scalars)), "scalar array import");
    // Flattened array reads must skip each element's 16-byte padding and must
    // not accept a matrix, a plain vector, or be accepted by the vector reader.
    Require(packing.ReadFloatArray("vectorArr")==std::vector<float>{.1f,.2f,.3f,.75f},
      "float2 array read kept element padding or transposed elements");
    Require(packing.ReadFloatArray("scalarArr")==std::vector<float>{.1f,1.f},
      "scalar array read used the wrong element stride");
    Require(packing.ReadFloatArray("absent").empty(),"absent diagnostic array invented");
    for(const auto* rejected_name:{"columnM","rowM"}) {
      bool refused=false;
      try { packing.ReadFloatArray(rejected_name); } catch(const std::runtime_error&) { refused=true; }
      Require(refused,"matrix accepted as a diagnostic float array");
    }
    bool vector_as_array_refused=false;
    try { ps.ReadFloatArray("bias"); } catch(const std::runtime_error&) { vector_as_array_refused=true; }
    Require(vector_as_array_refused,"non-array vector accepted as a diagnostic float array");
    bool array_as_vector_refused=false;
    try { packing.ReadFloatVector("vectorArr"); } catch(const std::runtime_error&) { array_as_vector_refused=true; }
    Require(array_as_vector_refused,"array accepted as a single diagnostic vector");
    draw(packing, {64,128,191,255});
    for(const auto& [name,source]:std::array<std::pair<const char*,std::vector<uint8_t>>,4>{{
        {"columnM",GuestRegisters(columns)},{"rowM",GuestRegisters(rows)},
        {"vectorArr",GuestRegisters(vectors)},{"scalarArr",GuestRegisters(scalars)}}}) {
      auto* reflected=packing.shader().reflection->GetConstantBufferByName("PackedData")->GetVariableByName(name);
      D3D11_SHADER_VARIABLE_DESC variable{};
      D3D11_SHADER_TYPE_DESC type{};
      Require(SUCCEEDED(reflected->GetDesc(&variable)) && SUCCEEDED(reflected->GetType()->GetDesc(&type)),
        "packing fixture reflection");
      std::vector<uint8_t> poison(variable.Size,0xcd),expected(variable.Size,0);
      packing.SetConstant(name,poison);
      const auto resolved=packing.ResolveFloatRegisters(name);
      Require(resolved.bytes()==source.size(),"resolved packing extent");
      packing.SetGuestFloatRegisters(resolved,source);
      const size_t components=type.Class==D3D_SVC_SCALAR?1:
        (type.Class==D3D_SVC_MATRIX_COLUMNS?type.Rows:type.Columns);
      for(size_t slot=0;slot<source.size()/16;++slot)
        for(size_t lane=0;lane<components;++lane) for(size_t byte=0;byte<4;++byte)
          expected.at(slot*16+lane*4+byte)=source.at(slot*16+lane*4+3-byte);
      packing.Bind(*context.Get());
      ComPtr<ID3D11Buffer> gpu,readback;
      context->PSGetConstantBuffers(6,1,&gpu);
      Require(gpu!=nullptr,"packing fixture buffer not bound");
      D3D11_BUFFER_DESC desc{}; gpu->GetDesc(&desc);
      desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
      Require(SUCCEEDED(device->CreateBuffer(&desc,nullptr,&readback)),"packing readback creation");
      context->CopyResource(readback.Get(),gpu.Get());
      D3D11_MAPPED_SUBRESOURCE mapped{};
      Require(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"packing readback map");
      const auto* actual=static_cast<const uint8_t*>(mapped.pData)+variable.StartOffset;
      const bool equal=std::equal(expected.begin(),expected.end(),actual);
      context->Unmap(readback.Get(),0);
      Require(equal,"full upload failed to replace poisoned data/padding exactly");
    }
    // Repeated full uploads must also preserve output and reject malformed
    // register extents before updating any earlier lanes.
    Require(packing.SetGuestFloatRegisters("columnM",GuestRegisters(columns)),"repeated full import");
    rejected=false;
    try { packing.SetGuestFloatRegisters("columnM",GuestRegisters(rows)); }
    catch(const std::runtime_error&) { rejected=true; }
    Require(rejected,"short full register upload accepted");
    draw(packing,{64,128,191,255});
    const std::array<float,4> patched_column{.75f,.6f,91,92};
    const std::array<float,4> patched_row{.25f,.6f,.7f,93};
    const std::array<float,4> patched_vector{.3f,.5f,94,95};
    const std::array<float,4> patched_scalar{.5f,96,97,98};
    const auto patch_column=packing.ResolveFloatRegisters("columnM");
    const auto patch_row=packing.ResolveFloatRegisters("rowM");
    const auto patch_vector=packing.ResolveFloatRegisters("vectorArr");
    const auto patch_scalar=packing.ResolveFloatRegisters("scalarArr");
    Require(packing.PatchGuestFloatRegisters(patch_column,2,GuestRegisters(patched_column)) &&
      packing.PatchGuestFloatRegisters(patch_row,1,GuestRegisters(patched_row)) &&
      packing.PatchGuestFloatRegisters(patch_vector,1,GuestRegisters(patched_vector)) &&
      packing.PatchGuestFloatRegisters(patch_scalar,1,GuestRegisters(patched_scalar)),
      "partial matrix/vector/scalar register patch");
    draw(packing,{191,64,128,128});
    Require(packing.PatchGuestFloatRegisters("scalarArr",1,GuestRegisters(patched_scalar)),
      "unchanged patch rejected");
    Require(packing.PatchGuestFloatRegisters(patch_column,3,{}),"empty end patch rejected");
    Require(!packing.PatchGuestFloatRegisters(packing.ResolveFloatRegisters("unused"),0,{}),"optimized-out patch unexpectedly resolved");
    for(const auto& invalid:{ShaderBindings::FloatRegisterBinding{},ps.ResolveFloatRegisters("tint"),retired}) {
      rejected=false;
      try { packing.PatchGuestFloatRegisters(invalid,0,GuestRegisters(patched_column)); }
      catch(const std::runtime_error&) { rejected=true; }
      Require(rejected,"foreign/default/retired patch token accepted");
    }
    // Bounds checks must happen before writing any component, including empty
    // patches beyond the end and malformed non-register-sized payloads.
    const auto patch_bytes=GuestRegisters(patched_column);
    for(const auto first:{size_t(4),~size_t(0)}) {
      rejected=false;
      try { packing.PatchGuestFloatRegisters(patch_column,first,{}); }
      catch(const std::runtime_error&) { rejected=true; }
      Require(rejected,"out-of-range empty token patch accepted");
    }
    rejected=false;
    try { packing.PatchGuestFloatRegisters(patch_column,0,std::span<const uint8_t>(patch_bytes).first(15)); }
    catch(const std::runtime_error&) { rejected=true; }
    Require(rejected,"unaligned token patch accepted");
    rejected=false;
    try { packing.PatchGuestFloatRegisters(patch_column,2,GuestRegisters(rows)); }
    catch(const std::runtime_error&) { rejected=true; }
    Require(rejected,"overflowing partial patch accepted");
    draw(packing,{191,64,128,128}); // Failed patch must not partially update.
    const std::array<uint32_t,4> special_bits{0x80000000u,0x7fc12345u,0xff800000u,0x3f800000u};
    std::array<float,4> special_values{};
    for(size_t i=0;i<4;++i) special_values[i]=std::bit_cast<float>(special_bits[i]);
    Require(ps.PatchGuestFloatRegisters(ps.ResolveFloatRegisters("tint"),0,GuestRegisters(special_values)),"bit-exact resolved vector patch");
    const auto special_readback=ps.ReadFloatVector("tint");
    for(size_t i=0;i<4;++i)
      Require(std::bit_cast<uint32_t>(special_readback.at(i))==special_bits[i],
        "register patch changed signed zero, NaN payload or infinity bits");
    ps.SetGuestFloatRegisters("tint",GuestRegisters(tint2));
    Require(ps.SetGuestFloatRegisters("tint",GuestRegisters(special_values)),"bit-exact full upload");
    const auto full_readback=ps.ReadFloatVector("tint");
    for(size_t i=0;i<4;++i)
      Require(std::bit_cast<uint32_t>(full_readback.at(i))==special_bits[i],
        "full upload changed signed zero, NaN payload or infinity bits");
    ps.SetGuestFloatRegisters("tint",GuestRegisters(tint2));
    // Sampling readback distinguishes addressing, filtering and LOD behavior,
    // rather than merely checking the D3D11 descriptor fields.
    auto mip_dds = dds; mip_dds.resize(148);
    DdsWord(mip_dds,12,2); DdsWord(mip_dds,16,2); DdsWord(mip_dds,28,2);
    const std::array<uint8_t,20> mip_pixels{
      255,0,0,255, 0,255,0,255, 255,0,0,255, 0,255,0,255, 0,0,255,255};
    std::copy(mip_pixels.begin(),mip_pixels.end(),mip_dds.begin()+128);
    auto mip_texture = CreateNativeDdsTexture(*backend,mip_dds);
    ShaderBindings probe_shader(*device.Get(),CompileNativeShader(*device.Get(),effect,{true,"SamplerPS","ps_5_0"},"test.fx"));
    probe_shader.SetTexture("mipImage",mip_texture.backend);
    auto sample = [&](uint32_t address, uint32_t filter, uint32_t lod_range,
                      float u, float lod, std::array<uint8_t,4> expected) {
      probe_shader.SetSampler("filtering",
        &backend->CreateSampler(DecodeNativeGuestSampler({address<<10,filter,lod_range,0})));
      const std::array<float,2> uv{u,.25f};
      probe_shader.SetConstant("probe",Bytes(uv)); probe_shader.SetConstant("probeLod",Bytes(lod));
      draw(probe_shader,expected);
    };
    sample(0,0,1u<<6,1.25f,0,{255,0,0,255}); // Wrap to first texel.
    sample(2,0,1u<<6,1.25f,0,{0,255,0,255}); // Clamp to last texel.
    sample(1,0,1u<<6,1.25f,0,{0,255,0,255}); // Mirror to .75.
    sample(3,0,1u<<6,-1.25f,0,{0,255,0,255}); // Mirror once, then clamp.
    sample(2,(1u<<19)|(1u<<21),1u<<6,.5f,0,{128,128,0,255});
    sample(2,0,1u<<6,.25f,1,{0,0,255,255}); // Authored blue mip.
    sample(2,0,0,.25f,1,{255,0,0,255}); // Maximum LOD clamps to base.
    // Rebinding must not retain resources from the previous material.
    Require(!ps.TrySetTexture("not_in_native_shader", texture.backend), "unknown texture accepted");
    ps.ClearTextures(); ps.ClearSamplers(); ps.Bind(*context.Get());
    Require(!ps.ReadTexture("image") && diagnostic_view==texture.backend.get(),"diagnostic snapshot lost retained view or stale current binding");
    Require(!ps.ReadSampler("filtering") && diagnostic_sampler==sampler,"diagnostic sampler snapshot lost retention or stale current binding");
    Require(!ps.HasAllTextureInputs(),"cleared texture inputs accepted");
    ComPtr<ID3D11ShaderResourceView> bound_texture;
    ComPtr<ID3D11SamplerState> bound_sampler;
    context->PSGetShaderResources(3, 1, &bound_texture);
    context->PSGetSamplers(2, 1, &bound_sampler);
    Require(!bound_texture && !bound_sampler, "stale material resources remain bound");
    {
      Effect sparse;
      sparse.source=R"(
Texture2D a:register(t0); Texture2D b:register(t1); Texture2D c:register(t3);
SamplerState sa:register(s0); SamplerState sb:register(s1); SamplerState sc:register(s3);
float4 PS(float2 uv:TEXCOORD0):SV_TARGET {
  return a.Sample(sa,uv)+b.Sample(sb,uv)+c.Sample(sc,uv);
})";
      ShaderBindings batch(*device.Get(),CompileNativeShader(*device.Get(),sparse,{true,"PS","ps_5_0"},"batch.fx"));
      auto* sentinel=&backend->CreateSampler(DecodeNativeGuestSampler({0,0,0,0}));
      auto* view=mip_texture.view.Get();
      auto* sampler=NativeD3D11SamplerState(*sentinel);
      context->PSSetShaderResources(2,1,&view); context->PSSetSamplers(2,1,&sampler);
      batch.SetTexture("a",mip_texture.backend); batch.SetTexture("b",nullptr); batch.SetTexture("c",mip_texture.backend);
      batch.SetSampler("sa",sentinel); batch.SetSampler("sb",nullptr); batch.SetSampler("sc",sentinel);
      batch.Bind(*context.Get());
      for(UINT slot=0;slot<4;++slot) {
        context->PSGetShaderResources(slot,1,bound_texture.ReleaseAndGetAddressOf());
        context->PSGetSamplers(slot,1,bound_sampler.ReleaseAndGetAddressOf());
        Require(bound_texture.Get()==(slot==1?nullptr:view) &&
                bound_sampler.Get()==(slot==1?nullptr:sampler),"batched sparse/null binding differs");
      }
      batch.ClearTextures(); batch.ClearSamplers(); batch.Bind(*context.Get());
      for(UINT slot=0;slot<4;++slot) {
        context->PSGetShaderResources(slot,1,bound_texture.ReleaseAndGetAddressOf());
        context->PSGetSamplers(slot,1,bound_sampler.ReleaseAndGetAddressOf());
        Require(bound_texture.Get()==(slot==2?view:nullptr) &&
                bound_sampler.Get()==(slot==2?sampler:nullptr),"precomputed slots retain cleared resources");
      }
      batch.SetTexture("b",mip_texture.backend); batch.SetSampler("sb",sentinel);
      batch.Bind(*context.Get());
      context->PSGetShaderResources(1,1,bound_texture.ReleaseAndGetAddressOf());
      context->PSGetSamplers(1,1,bound_sampler.ReleaseAndGetAddressOf());
      Require(bound_texture.Get()==view && bound_sampler.Get()==sampler,"precomputed slots miss changed bindings");
      // External context changes must not invalidate the binding object's data.
      ID3D11ShaderResourceView* external_null=nullptr;
      context->PSSetShaderResources(1,1,&external_null); batch.Bind(*context.Get());
      context->PSGetShaderResources(1,1,bound_texture.ReleaseAndGetAddressOf());
      Require(bound_texture.Get()==view,"precomputed binding incorrectly caches context state");
    }
    // Legacy combined samplers become separate native texture/sampler bindings
    // with the same name. Resolve each independently, never by guest register.
    Effect legacy;
    legacy.source = R"(
sampler2D combined : register(s5);
sampler2D unusedShadow : register(s6);
float4 LegacyPS(float2 uv : TEXCOORD0) : COLOR0 {
  return tex2D(combined, uv) + 0 * tex2D(unusedShadow, uv);
}
)";
    ShaderBindings combined(*device.Get(), CompileNativeShader(*device.Get(), legacy,
                            {true,"LegacyPS","ps_3_0"}, "legacy.fx"));
    D3D11_SHADER_INPUT_BIND_DESC reflected_texture{}, reflected_sampler{};
    auto* reflection = combined.shader().reflection.Get();
    D3D11_SHADER_DESC description{};
    Require(SUCCEEDED(reflection->GetDesc(&description)), "legacy reflection");
    bool has_texture = false, has_sampler = false;
    for (UINT i = 0; i < description.BoundResources; ++i) {
      D3D11_SHADER_INPUT_BIND_DESC binding{};
      Require(SUCCEEDED(reflection->GetResourceBindingDesc(i,&binding)), "legacy binding reflection");
      if (std::string(binding.Name) != "combined") continue;
      if (binding.Type == D3D_SIT_TEXTURE) { reflected_texture = binding; has_texture = true; }
      if (binding.Type == D3D_SIT_SAMPLER) { reflected_sampler = binding; has_sampler = true; }
    }
    Require(has_texture && has_sampler, "legacy combined resource names");
    const auto combined_resource=combined.ResolveResource("combined");
    Require(combined.TrySetTexture(combined_resource,texture.backend), "resolved legacy texture lookup");
    Require(combined.TrySetSampler(combined_resource,sampler),"resolved legacy sampler lookup");
    combined.Bind(*context.Get());
    const auto absent_resource=combined.ResolveResource("unusedShadow");
    Require(!combined.TrySetTexture(absent_resource,nullptr) && !combined.TrySetSampler(absent_resource,nullptr),
      "resolved optimized-out resource");
    Require(!combined.TrySetTexture("unusedShadow",nullptr),"dead legacy texture not optimized out");
    Require(!combined.TrySetSampler("unusedShadow",nullptr),"dead legacy sampler not optimized out");
    combined.Bind(*context.Get()); // Skipping dead records must retain live bindings.
    context->PSGetShaderResources(reflected_texture.BindPoint,1,bound_texture.ReleaseAndGetAddressOf());
    context->PSGetSamplers(reflected_sampler.BindPoint,1,bound_sampler.ReleaseAndGetAddressOf());
    Require(bound_texture.Get() == NativeD3D11TextureView(*texture.backend) &&
            bound_sampler.Get() == NativeD3D11SamplerState(*sampler), "legacy native binding slots");
    // A defined HDR scene can feed native post-processing without implying
    // complete gameplay coverage. Preserve the explicit resolve boundary.
    Effect scene_effect;
    scene_effect.source=R"(
Texture2D sceneImage; SamplerState sceneFilter;
float4 ScenePS() : SV_TARGET { return sceneImage.Sample(sceneFilter,float2(.5,.5)); }
)";
    ShaderBindings scene_ps(*device.Get(),CompileNativeShader(*device.Get(),scene_effect,
      {true,"ScenePS","ps_5_0"},"scene.fx"));
    auto scene=CreateNativeRenderTarget(*backend,4,4,DXGI_FORMAT_R16G16B16A16_FLOAT);
    const float scene_first[]{.25f,.5f,.75f,1},scene_next[]{.75f,.25f,.5f,1};
    context->ClearRenderTargetView(scene.target.Get(),scene_first); scene.content_valid=true;
    ResolveNativeRenderTarget(*context.Get(),scene);
    scene_ps.SetTexture("sceneImage",scene.sampled.backend);
    scene_ps.SetSampler("sceneFilter",sampler);
    draw(scene_ps,{64,128,191,255});
    context->ClearRenderTargetView(scene.target.Get(),scene_next);
    draw(scene_ps,{64,128,191,255}); // No resolve: previous sampled contents.
    scene_ps.ClearTextures(); scene_ps.Bind(*context.Get());
    ResolveNativeRenderTarget(*context.Get(),scene);
    scene_ps.SetTexture("sceneImage",scene.sampled.backend);
    draw(scene_ps,{191,64,128,255});
    NativeCompletionQueue completion(*device.Get(),*context.Get(),2);
    Require(!completion.Poll() && !completion.pending(),"empty completion queue published a value");
    Require(!completion.completed_cursor(),"empty completion queue published a cursor");
    completion.Submit(0xfffffffdu,0x1001);
    context->ClearRenderTargetView(scene.target.Get(),scene_first);
    completion.Submit(0xffffffffu,0x2002);
    Require(!completion.completed() && completion.pending()==2,"completion published before polling");
    Require(!completion.completed_cursor(),"cursor published before native completion");
    bool rejected_completion=false;
    try { completion.Submit(1); } catch(const std::runtime_error&) { rejected_completion=true; }
    Require(rejected_completion && completion.pending()==2,"completion overflow changed the queue");
    auto await_completion=[&](uint32_t expected) {
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      while(completion.Poll()!=expected) {
        Require(std::chrono::steady_clock::now()<deadline,"native completion timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      Require(completion.pending()==0,"ordered completion left earlier entries pending");
    };
    await_completion(0xffffffffu);
    Require(completion.completed_cursor()==0x2002,"native event cursor/value association lost");
    const auto completed_pixel=ReadNativeColorPixel(*context.Get(),*scene.surface.Get(),0,0);
    Require(std::abs(completed_pixel[0]-.25f)<.001f && std::abs(completed_pixel[2]-.75f)<.001f,
      "native completion did not cover preceding rendering");
    completion.Submit(1,0x3003); await_completion(1); // uint32 wraparound, +2 sequence
    Require(completion.completed_cursor()==0x3003,"wrapped completion cursor association lost");
    rejected_completion=false;
    try { completion.Submit(4); } catch(const std::runtime_error&) { rejected_completion=true; }
    Require(rejected_completion && completion.completed()==1,"invalid fence sequence changed completion");
    rejected_completion=false;
    try { NativeCompletionQueue invalid(*device.Get(),*context.Get(),0); }
    catch(const std::runtime_error&) { rejected_completion=true; }
    Require(rejected_completion,"zero completion capacity accepted");
    ComPtr<ID3D11DeviceContext> deferred;
    Require(SUCCEEDED(device->CreateDeferredContext(0,&deferred)),"create deferred test context");
    rejected_completion=false;
    try { NativeCompletionQueue invalid(*device.Get(),*deferred.Get()); }
    catch(const std::runtime_error&) { rejected_completion=true; }
    Require(rejected_completion,"deferred completion context accepted");
    {
      NativeCompletionQueue staged(*device.Get(),*context.Get(),3);
      staged.Capture(0x1000,40,0xffffffffu,0x1001);
      staged.Capture(0x2000,40,1,0x2002);
      Require(!staged.Poll() && staged.unsubmitted()==2,"fence construction released command storage");
      Require(!staged.Poll(true) && staged.unsubmitted()==2,
        "active flush polling armed an unsubmitted fence");
      bool rejected=false;
      try { staged.SubmitRange(0x1000,36); } catch(const std::runtime_error&) { rejected=true; }
      Require(rejected && staged.unsubmitted()==2,"partial fence submission mutated lifetime tracking");
      rejected=false;
      try { staged.Capture(0x1004,40,3,0x3003); } catch(const std::runtime_error&) { rejected=true; }
      Require(rejected && staged.pending()==2,"unconsumed fence storage reuse was accepted");
      Require(staged.SubmitRange(0x3000,40)==0 && !staged.Poll(),"unrelated submission released storage");
      Require(staged.SubmitRange(0x2000,40)==1,"later fence not armed");
      // Even a completed later query cannot release an earlier CPU-list range.
      const auto barrier=ReadNativeColorPixel(*context.Get(),*scene.surface.Get(),0,0);
      (void)barrier;
      Require(!staged.Poll() && !staged.completed_cursor(),"later fence bypassed unconsumed CPU work");
      Require(!staged.Poll(true) && !staged.completed_cursor(),
        "active flush polling bypassed an unconsumed CPU range");
      Require(staged.SubmitRange(0x1000,40)==1,"earlier fence not armed");
      staged.Capture(0x1000,40,3,0x3003); // Submitted storage may be reused.
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      while(staged.Poll()!=1) {
        Require(std::chrono::steady_clock::now()<deadline,"staged fence timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      Require(staged.completed_cursor()==0x2002 && staged.pending()==1 && staged.unsubmitted()==1,
        "staged fence cursor association or pending lifetime lost");
      staged.SubmitRange(0x1000,40);
      while(staged.Poll()!=3) {
        Require(std::chrono::steady_clock::now()<deadline,"reused staged fence timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      Require(staged.completed_cursor()==0x3003 && !staged.pending(),"reused staged fence not completed exactly once");
    }
    NativeCompletionQueue native_wait(*device.Get(),*context.Get());
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    Require(native_wait.WaitUntil(7,0,deadline)==NativeWaitResult::Complete,"empty zero native wait");
    Require(native_wait.WaitUntil(7,5,deadline)==NativeWaitResult::Unsubmitted,"unsubmitted native wait accepted");
    native_wait.Submit(5);
    std::stop_source cancel;
    cancel.request_stop();
    Require(native_wait.WaitUntil(7,5,deadline,cancel.get_token())==NativeWaitResult::Cancelled &&
      !native_wait.completed(),"cancelled native wait published completion");
    Require(native_wait.WaitUntil(7,5,deadline)==NativeWaitResult::Complete && native_wait.completed()==5,
      "native wait did not complete its event");
    {
      Require(NativeGpuTiming{1,100,160,60000,true}.Milliseconds()==1.0,
        "native GPU time conversion ignored its frequency");
      Require(!NativeGpuTiming{1,100,160,60000,false}.Milliseconds(),"disjoint GPU sample accepted");
      Require(!NativeGpuTiming{1,100,160,0,true}.Milliseconds(),"zero GPU frequency accepted");
      Require(!NativeGpuTiming{1,160,100,60000,true}.Milliseconds(),"backwards GPU clock accepted");
      Require(NativeGpuTiming{1,100,100,60000,true}.Milliseconds()==0.0,"valid zero GPU duration lost");
      NativeGpuTimer timer(*device.Get(),*context.Get(),1);
      Require(NativeGpuTiming{1,100,200,60000,true,125}.FractionAfterMiddle()==.75,
              "GPU three-point ratio differs");
      for(auto sample:{NativeGpuTiming{1,100,100,60000,true,100},
                       NativeGpuTiming{1,100,200,60000,true,99},
                       NativeGpuTiming{1,100,200,60000,true,201},
                       NativeGpuTiming{1,100,200,60000,false,150},
                       NativeGpuTiming{1,100,200,0,true,150},
                       NativeGpuTiming{1,100,200,60000,true}})
        Require(!sample.FractionAfterMiddle(),"invalid three-point GPU sample accepted");
      Require(!timer.active() && !timer.pending() && !timer.Poll(true),"empty GPU timer produced a sample");
      bool rejected=false;
      try { timer.End(); } catch(const std::logic_error&) { rejected=true; }
      Require(rejected,"GPU timer accepted end without begin");
      rejected=false;
      try { timer.MarkMiddle(); } catch(const std::logic_error&) { rejected=true; }
      Require(rejected,"GPU middle marker accepted without begin");
      timer.Begin(41);
      rejected=false;
      try { timer.Begin(42); } catch(const std::logic_error&) { rejected=true; }
      Require(rejected && timer.active() && !timer.Poll(true),"nested timer changed active ownership");
      context->ClearRenderTargetView(scene.target.Get(),scene_first);
      timer.MarkMiddle();
      rejected=false;
      try { timer.MarkMiddle(); } catch(const std::logic_error&) { rejected=true; }
      Require(rejected,"duplicate GPU middle marker accepted");
      context->ClearRenderTargetView(scene.target.Get(),scene_first);
      timer.End();
      Require(!timer.active() && timer.pending()==1,"GPU timer lost submitted ownership");
      rejected=false;
      try { timer.Begin(42); } catch(const std::runtime_error&) { rejected=true; }
      Require(rejected && !timer.active() && timer.pending()==1,"GPU timer capacity changed pending work");
      const auto timeout=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      std::optional<NativeGpuTiming> measured;
      while(!(measured=timer.Poll(true))) {
        Require(std::chrono::steady_clock::now()<timeout,"native GPU timer timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      Require(measured->tag==41 && measured->Milliseconds().has_value() && !timer.pending(),
        "WARP GPU timestamp span invalid or retired incorrectly");
      Require(measured->middle && *measured->middle>=measured->begin && *measured->middle<=measured->end,
              "WARP middle timestamp absent or unordered");
      timer.Begin(42); timer.Cancel();
      Require(!timer.active() && !timer.pending() && !timer.Poll(true),"cancelled timer published a sample");
      timer.Begin(43); // Destructor must close, not leave a disjoint query open.
    }
    std::cout << "Native event-query completion, ordering, capacity and fence wraparound passed\n";
    {
      NativePresentProfiler profiler(*device.Get(),*context.Get());
      bool failed=false;
      try { profiler.Finish(0xfffffffeu); } catch(const std::logic_error&) { failed=true; }
      Require(failed && !profiler.Poll(),"present profiler accepted orphan finish");
      for(uint32_t sequence:{0xfffffffeu,0u,2u}) {
        profiler.Start(sequence);
        failed=false;
        try { profiler.Start(sequence); } catch(const std::logic_error&) { failed=true; }
        Require(failed,"present profiler accepted duplicate start");
        context->ClearRenderTargetView(scene.target.Get(),scene_first);
        failed=false;
        try { profiler.Finish(sequence+2); } catch(const std::logic_error&) { failed=true; }
        Require(failed,"present profiler accepted wrong finish sequence");
        profiler.Finish(sequence);
      }
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      for(uint32_t sequence:{0xfffffffeu,0u,2u}) {
        std::optional<NativeGpuTiming> result;
        while(!(result=profiler.Poll())) {
          Require(std::chrono::steady_clock::now()<deadline,"present profiler query timeout");
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Require(result->tag==sequence && result->reliable,"present profiler lost ordered sample");
        Require(result->middle.has_value()==(sequence!=0xfffffffeu),"present profiler previous-end ownership differs");
      }
      Require(!profiler.Poll(),"present profiler duplicated a sample");
      failed=false;
      try { profiler.Start(6); } catch(const std::logic_error&) { failed=true; }
      Require(failed,"present profiler accepted a skipped producer index");
      profiler.Start(4); profiler.Finish(4);
      // Destruction retires ownership of pending results and the open next span.
    }
    {
      NativeSignalQueue signals(*device.Get(),*context.Get(),3);
      NativeSignalDelivery delivery;
      const NativeSignal multi{0x8214eba0,0x9876,7};
      delivery.Enqueue(multi);
      delivery.BeginSubmission(); delivery.BeginSubmission();
      size_t wakes=0;
      Require(!delivery.Deliver([&](const NativeSignal&,uint32_t) { ++wakes; }) && !wakes,
        "CPU signal escaped active submission");
      delivery.EndSubmission();
      Require(delivery.submitting() && delivery.pending(),"nested submission lost pending delivery");
      delivery.EndSubmission();
      std::vector<uint32_t> cpus;
      bool interrupted=false;
      try { delivery.Deliver([&](const NativeSignal& value,uint32_t cpu) {
        Require(value==multi,"CPU delivery changed signal payload");
        if(cpu==1) throw std::runtime_error("injected wake failure");
        cpus.push_back(cpu);
      }); } catch(const std::runtime_error&) { interrupted=true; }
      Require(interrupted && delivery.pending() && cpus==std::vector<uint32_t>{0},
        "CPU delivery lost partially published signal");
      Require(delivery.Deliver([&](const NativeSignal&,uint32_t cpu) { cpus.push_back(cpu); }) &&
        !delivery.pending() && cpus==std::vector<uint32_t>{0,1,2},
        "CPU delivery repeated a completed CPU or lost a remaining CPU");
      Require(!delivery.Deliver([&](const NativeSignal&,uint32_t) { ++wakes; }) && !wakes,
        "acknowledged CPU signal delivered twice");
      const NativeSignal first{0x8214eba0,0x1234,NativeSignalCpuMask(0x02000000)};
      const NativeSignal second{0x8214eba0,0x5678,NativeSignalCpuMask(0)};
      Require(first.cpu_mask==2 && second.cpu_mask==4 && NativeSignalCpuMask(0x3f000001)==63,
        "native signal CPU mask decoding differs");
      Require(NativeSignalCommandAddress(0xbf001000)==0x1f001000 &&
        NativeSignalCommandAddress(0xc0001000)==0x1000 && NativeSignalCommandAddress(0x1000)==0x1000,
        "native signal command address normalization differs");
      signals.Capture(0x1040,16,second); signals.Capture(0x1000,16,first);
      Require(signals.Poll().empty() && signals.pending()==2 && signals.unsubmitted()==2,
        "captured signal completed before submission");
      bool rejected=false;
      try { signals.Capture(0x1008,16,first); } catch(const std::runtime_error&) { rejected=true; }
      Require(rejected && signals.pending()==2,"overlapping unsubmitted signal accepted");
      rejected=false;
      try { signals.SubmitRange(0x1004,4); } catch(const std::runtime_error&) { rejected=true; }
      Require(rejected && signals.unsubmitted()==2,"partial signal submission mutated capture state");
      Require(signals.SubmitRange(0x2000,16)==0 && signals.Poll().empty(),"unrelated range released signals");
      Require(signals.Poll(1,true).empty() && signals.unsubmitted()==2,
        "active signal polling submitted an uncaptured range");
      context->ClearRenderTargetView(scene.target.Get(),scene_first);
      Require(signals.SubmitRange(0x1000,0x80)==2 && !signals.unsubmitted(),"signal range did not arm both callbacks");
      // Reuse submitted storage: callback payloads must have independent lifetime.
      signals.Capture(0x1000,16,second);
      rejected=false;
      try { signals.Capture(0x3000,16,first); } catch(const std::runtime_error&) { rejected=true; }
      Require(rejected && signals.pending()==3,"signal capacity did not include in-flight work");
      Require(signals.SubmitRange(0x1000,16)==1,"reused signal range not submitted");
      std::vector<NativeSignal> delivered;
      Require(signals.Poll(0,true).empty() && signals.pending()==3,
        "zero delivery budget consumed native signals");
      const auto signal_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      while(delivered.size()<3) {
        // Submission flushed the event: worker polling must make progress
        // without flushing later, unrelated commands on every GetData call.
        auto ready=signals.PeekCompleted(1);
        Require(ready.size()<=1,"native signal delivery exceeded worker slot capacity");
        Require(signals.pending()==3-delivered.size(),"completion retired signal before acknowledgement");
        if(!ready.empty()) {
          Require(signals.PeekCompleted(1)==ready,"repeated completion peek lost undelivered signal");
          bool bad_ack=false;
          try { signals.AcknowledgeCompleted(signals.completed()+1); }
          catch(const std::runtime_error&) { bad_ack=true; }
          Require(bad_ack && signals.PeekCompleted(1)==ready,"bad acknowledgement changed delivery ownership");
          delivery.Enqueue(ready.front());
          signals.AcknowledgeCompleted(ready.size());
          Require(signals.pending()+size_t(delivery.pending())==3-delivered.size(),
            "GPU-to-CPU handoff lost outstanding delivery count");
          bool occupied=false;
          try { delivery.Enqueue(second); } catch(const std::runtime_error&) { occupied=true; }
          Require(occupied && delivery.pending(),"CPU handoff overwrote occupied publication slot");
          delivery.BeginSubmission();
          Require(!delivery.Deliver([&](const NativeSignal&,uint32_t) {
            throw std::runtime_error("delivery escaped submission barrier");
          }),"completed GPU signal escaped CPU submission barrier");
          delivery.EndSubmission();
          Require(delivery.Deliver([&](const NativeSignal& signal,uint32_t cpu) {
            Require(signal==ready.front() && signal.cpu_mask==(1u<<cpu),"handoff changed signal/CPU identity");
            delivered.push_back(signal);
          }),"CPU handoff did not deliver completed GPU signal");
        }
        Require(signals.pending()==3-delivered.size(),"limited poll discarded undelivered batch signals");
        Require(std::chrono::steady_clock::now()<signal_deadline,"native signal completion timed out");
        if(delivered.size()<3) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      Require(delivered==std::vector<NativeSignal>{first,second,second} && !signals.pending() && signals.Poll().empty(),
        "native signal submission/byte ordering or exactly-once delivery differs");
      const auto signal_pixel=ReadNativeColorPixel(*context.Get(),*scene.surface.Get(),0,0);
      Require(std::abs(signal_pixel[0]-.25f)<.001f,"signal completion preceded covered GPU rendering");
      for(auto range:std::array<std::array<uint32_t,2>,3>{{{1,4},{0x1000,0},{0xfffffffc,8}}}) {
        rejected=false;
        try { signals.Capture(range[0],range[1],first); } catch(const std::invalid_argument&) { rejected=true; }
        Require(rejected && !signals.pending(),"invalid native signal range accepted");
      }
    }
    std::cout << "Native signal capture/submission separation and ordered GPU completion passed\n";
    std::cout << "Native D3D11 draw/readback, constant updates and resource clearing passed\n";
    std::cout << "Native HDR scene resolve, retained history and downstream sampling passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

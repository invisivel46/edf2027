#include "native_graphics/d3d11_texture.h"
#include "native_graphics/d3d11_backend.h"
#include "native_graphics/d3d11_bindings.h"
#include "native_graphics/d3d11_render_state.h"
#include <cmath>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace edf::native;
using Microsoft::WRL::ComPtr;
namespace {
void Require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
void Word(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) bytes.at(at+i) = static_cast<uint8_t>(value >> (8*i));
}
std::vector<uint8_t> Header(uint32_t width, uint32_t height, uint32_t mips) {
  std::vector<uint8_t> bytes(128);
  Word(bytes,0,0x20534444); Word(bytes,4,124); Word(bytes,76,32);
  Word(bytes,12,height); Word(bytes,16,width); Word(bytes,28,mips);
  return bytes;
}
void Reject(NativeRenderBackend& backend, const std::vector<uint8_t>& bytes) {
  bool rejected = false;
  try { CreateNativeDdsTexture(backend, bytes); }
  catch (const std::runtime_error&) { rejected = true; }
  Require(rejected, "invalid DDS accepted");
}
}
int main() {
  try {
    for(uint32_t mode:{0u,1u,2u}) {
      Require(NativeSceneSamples(mode,0)==(1u<<mode),"MSAA game default");
      for(int32_t choice:{1,2,4})
        Require(NativeSceneSamples(mode,choice)==uint32_t(choice),"MSAA scene override");
    }
    for(int32_t choice:{-1,3,8}) {
      bool rejected=false;
      try { NativeSceneSamples(1,choice); } catch(const std::runtime_error&) { rejected=true; }
      Require(rejected,"invalid MSAA override accepted");
    }
    bool rejected_mode=false;
    try { NativeSceneSamples(3,2); } catch(const std::runtime_error&) { rejected_mode=true; }
    Require(rejected_mode,"invalid guest MSAA mode accepted");
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
      D3D11_SDK_VERSION,&device,nullptr,&context)), "device creation");
    // Through the backend, on this device, so the readbacks below check what
    // the seam actually created - including face-major subresource order,
    // which a texture interface describing one 2D slice could not express.
    auto backend=AdoptNativeD3D11Backend(*device.Get(),*context.Get());
    Require(bool(backend),"adopted backend");
    // A frame stays open for the rest of this test. On the adopted D3D11
    // backend the recorder issues straight to the immediate context, so an
    // open frame is bookkeeping and the ordering against the direct calls
    // below is unchanged.
    backend->BeginFrame();
    // Read each compressed subresource back byte-for-byte. Unique face/mip
    // values detect truncated chains, incorrect block rounding and face order.
    for (uint32_t fourcc : {0x31545844u,0x33545844u,0x35545844u}) {
      const uint32_t block = fourcc == 0x31545844 ? 8 : 16;
      auto bytes = Header(8,8,4);
      Word(bytes,80,4); Word(bytes,84,fourcc); Word(bytes,112,0xfe00);
      for (unsigned face = 0; face < 6; ++face)
        for (unsigned mip = 0; mip < 4; ++mip)
          bytes.insert(bytes.end(), (mip == 0 ? 4 : 1)*block, uint8_t(face*4+mip+1));
      auto native = CreateNativeDdsTexture(*backend,bytes);
      Require(native.cube && native.mip_count == 4, "cube metadata");
      D3D11_SHADER_RESOURCE_VIEW_DESC view{};
      native.view->GetDesc(&view);
      Require(view.ViewDimension == D3D11_SRV_DIMENSION_TEXTURECUBE && view.TextureCube.MipLevels == 4,
              "cube view metadata");
      D3D11_TEXTURE2D_DESC desc{};
      native.resource->GetDesc(&desc);
      desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = desc.MiscFlags = 0;
      desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      ComPtr<ID3D11Texture2D> staging;
      Require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)), "staging creation");
      context->CopyResource(staging.Get(),native.resource.Get());
      for (unsigned face = 0; face < 6; ++face) for (unsigned mip = 0; mip < 4; ++mip) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const auto index = D3D11CalcSubresource(mip,face,4);
        Require(SUCCEEDED(context->Map(staging.Get(),index,D3D11_MAP_READ,0,&mapped)), "mip readback");
        const unsigned rows = mip == 0 ? 2 : 1, pitch = rows*block;
        bool correct = true;
        for (unsigned y = 0; y < rows; ++y) for (unsigned x = 0; x < pitch; ++x)
          correct &= static_cast<const uint8_t*>(mapped.pData)[y*mapped.RowPitch+x] == face*4+mip+1;
        context->Unmap(staging.Get(),index);
        Require(correct,"compressed face/mip payload changed");
      }
      bytes.pop_back(); Reject(*backend,bytes);
      Word(bytes,112,0x600); Reject(*backend,bytes);
    }
    // Map shadow textures are alpha-only A8. Check all channels, padded top
    // rows and the tightly packed next mip so alpha cannot turn into red.
    auto alpha=Header(2,2,2);
    Word(alpha,80,2); Word(alpha,88,8); Word(alpha,104,255);
    Word(alpha,8,8); Word(alpha,20,4);
    alpha.insert(alpha.end(),{0,85,0xee,0xee,170,255,0xee,0xee,123});
    auto alpha_texture=CreateNativeDdsTexture(*backend,alpha);
    D3D11_TEXTURE2D_DESC alpha_desc{}; alpha_texture.resource->GetDesc(&alpha_desc);
    Require(alpha_desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM,"A8 expanded format");
    alpha_desc.Usage=D3D11_USAGE_STAGING; alpha_desc.BindFlags=0;
    alpha_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> alpha_readback;
    Require(SUCCEEDED(device->CreateTexture2D(&alpha_desc,nullptr,&alpha_readback)),"A8 staging creation");
    context->CopyResource(alpha_readback.Get(),alpha_texture.resource.Get());
    for (UINT mip=0;mip<2;++mip) {
      D3D11_MAPPED_SUBRESOURCE mapped{};
      Require(SUCCEEDED(context->Map(alpha_readback.Get(),mip,D3D11_MAP_READ,0,&mapped)),"A8 mip map");
      bool correct=true;
      const UINT width=mip?1:2;
      for (UINT y=0;y<width;++y) for (UINT x=0;x<width;++x) {
        const auto* pixel=static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch+x*4;
        correct &= !pixel[0] && !pixel[1] && !pixel[2] && pixel[3]==(mip?123:(y*2+x)*85);
      }
      context->Unmap(alpha_readback.Get(),mip);
      Require(correct,"A8 shadow channel/pitch/mip conversion");
    }
    auto bad_alpha=alpha; bad_alpha.pop_back(); Reject(*backend,bad_alpha);
    bad_alpha=alpha; Word(bad_alpha,104,0); Reject(*backend,bad_alpha);
    bad_alpha=alpha; Word(bad_alpha,92,255); Reject(*backend,bad_alpha);
    auto invalid = Header(4,4,4); // Four levels cannot fit a 4x4 image.
    Reject(*backend,invalid);
    invalid = Header(0,4,1); Reject(*backend,invalid);
    invalid = Header(4,4,1); Word(invalid,80,4); Word(invalid,84,0x30315844);
    Reject(*backend,invalid); // DX10 is deliberately not silently misread.
    auto capture_target=CreateNativeRenderTarget(*backend,3,2,DXGI_FORMAT_R16G16B16A16_FLOAT);
    const std::array<uint16_t,24> capture_pixels{
      0x3c00,0,0,0x3c00, 0,0x3800,0,0x3c00, 0,0,0x4000,0x3c00,
      0xbc00,0,0,0x3c00, 1,1,1,0x3c00, 0x7e00,0,0,0x3c00};
    context->UpdateSubresource(capture_target.surface.Get(),0,nullptr,capture_pixels.data(),3*8,0);
    Require(ReadNativeColorPixel(*context.Get(),*capture_target.surface.Get(),2,0)==std::array<float,4>{0,0,2,1},"HDR pixel readback clamped values");
    Require(ReadNativeColorPixel(*context.Get(),*capture_target.surface.Get(),0,1)==std::array<float,4>{-1,0,0,1},"HDR pixel readback lost sign/row");
    bool rejected_pixel=false;
    try { ReadNativeColorPixel(*context.Get(),*capture_target.surface.Get(),3,0); }
    catch (const std::runtime_error&) { rejected_pixel=true; }
    Require(rejected_pixel,"out-of-bounds pixel accepted");
    const auto bmp=CaptureNativeHdrBmp(*context.Get(),*capture_target.surface.Get());
    Require(bmp.size()==78 && bmp[0]=='B' && bmp[1]=='M' && bmp[10]==54 && bmp[18]==3 &&
      bmp[22]==254 && bmp[23]==255 && bmp[26]==1 && bmp[28]==24,"native BMP header/dimensions");
    const std::array<uint8_t,24> expected_bgr{
      0,0,255, 0,128,0, 255,0,0, 0,0,0,
      0,0,0, 0,0,0, 255,0,255, 0,0,0};
    Require(std::equal(expected_bgr.begin(),expected_bgr.end(),bmp.begin()+54),"native BMP orientation/channel/clamp/padding");
    Require(!capture_target.content_valid && !capture_target.sampled.content_valid,"diagnostic capture published partial target");
    auto output_capture=CreateNativeRenderTarget(*backend,3,2,DXGI_FORMAT_R8G8B8A8_UNORM);
    const std::array<uint8_t,24> output_pixels{1,2,3,4, 5,6,7,8, 9,10,11,12,
                                             13,14,15,16, 17,18,19,20, 21,22,23,24};
    context->UpdateSubresource(output_capture.surface.Get(),0,nullptr,output_pixels.data(),12,0);
    Require(ReadNativeColorPixel(*context.Get(),*output_capture.surface.Get(),2,1)==std::array<float,4>{21/255.0f,22/255.0f,23/255.0f,24/255.0f},"RGBA8 pixel readback normalization differs");
    const auto output_bmp=CaptureNativeHdrBmp(*context.Get(),*output_capture.surface.Get());
    const std::array<uint8_t,24> output_bgr{3,2,1, 7,6,5, 11,10,9, 0,0,0,
                                         15,14,13, 19,18,17, 23,22,21, 0,0,0};
    Require(output_bmp.size()==78 && std::equal(output_bgr.begin(),output_bgr.end(),output_bmp.begin()+54),"native RGBA8 capture changed channels/rows/padding");
    Require(!output_capture.content_valid && !output_capture.sampled.content_valid,"output capture initialized partial frame");
    for(uint32_t samples:{2u,4u}) {
      auto msaa=CreateNativeRenderTarget(*backend,4,2,DXGI_FORMAT_R16G16B16A16_FLOAT,samples);
      auto depth=CreateNativeDepthTarget(*backend,4,2,DXGI_FORMAT_D32_FLOAT_S8X24_UINT,samples);
      D3D11_TEXTURE2D_DESC color_desc{},depth_desc{},sample_desc{};
      msaa.surface->GetDesc(&color_desc); depth.surface->GetDesc(&depth_desc);
      msaa.sampled.resource->GetDesc(&sample_desc);
      Require(color_desc.SampleDesc.Count==samples && depth_desc.SampleDesc.Count==samples &&
        sample_desc.SampleDesc.Count==1,"MSAA storage/resolve sample counts");
      ResolveNativeRenderTarget(backend->Recorder(),msaa);
      Require(!msaa.sampled.content_valid,"unwritten MSAA resolve marked valid");
      ClearNativeDepthTarget(*context.Get(),depth,true,true,.25f,7);
      Require(depth.depth_valid && depth.stencil_valid,"MSAA depth/stencil clear validity");
      const float hdr[]{2,.5f,-1,1}; context->ClearRenderTargetView(msaa.target.Get(),hdr);
      msaa.content_valid=true; ResolveNativeRenderTarget(backend->Recorder(),msaa);
      Require(msaa.sampled.content_valid,"MSAA resolve missing validity");
      for(uint32_t y=0;y<2;++y) for(uint32_t x=0;x<4;++x)
        Require(ReadNativeColorPixel(*context.Get(),*msaa.sampled.resource.Get(),x,y)==
          std::array<float,4>{2,.5f,-1,1},"MSAA HDR resolve pixels");
      // Write red into exactly sample0 over a blue clear. A real resolve must
      // average samples, not copy one sample or silently use single sampling.
      Effect sample_effect;
      sample_effect.source=R"(
float4 VS(uint id:SV_VertexID):SV_POSITION {
  return float4(id==2?3:-1,id==1?3:-1,0,1);
}
float4 PS():SV_TARGET { return float4(1,0,0,1); }
)";
      ShaderBindings sample_vs(*device.Get(),CompileNativeShader(*device.Get(),sample_effect,
        {false,"VS","vs_5_0"},"msaa_test.fx"));
      ShaderBindings sample_ps(*device.Get(),CompileNativeShader(*device.Get(),sample_effect,
        {true,"PS","ps_5_0"},"msaa_test.fx"));
      D3D11_RASTERIZER_DESC raster{}; raster.FillMode=D3D11_FILL_SOLID;
      raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=true; raster.MultisampleEnable=true;
      ComPtr<ID3D11RasterizerState> raster_state;
      Require(SUCCEEDED(device->CreateRasterizerState(&raster,&raster_state)),"MSAA raster state");
      auto* rtv=msaa.target.Get(); context->OMSetRenderTargets(1,&rtv,nullptr);
      const D3D11_VIEWPORT viewport{0,0,4,2,0,1}; context->RSSetViewports(1,&viewport);
      context->RSSetState(raster_state.Get()); context->OMSetBlendState(nullptr,nullptr,1);
      context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      const float blue[]{0,0,1,0}; context->ClearRenderTargetView(rtv,blue);
      sample_vs.Bind(*context.Get()); sample_ps.Bind(*context.Get()); context->Draw(3,0);
      context->OMSetRenderTargets(0,nullptr,nullptr); context->OMSetBlendState(nullptr,nullptr,UINT32_MAX);
      ResolveNativeRenderTarget(backend->Recorder(),msaa);
      const float fraction=1.f/samples;
      Require(CaptureNativeHdrBmp(*context.Get(),*msaa.surface.Get())==
        CaptureNativeHdrBmp(*context.Get(),*msaa.sampled.resource.Get()),"MSAA diagnostic capture differs from explicit resolve");
      for(uint32_t y=0;y<2;++y) for(uint32_t x=0;x<4;++x) {
        const auto pixel=ReadNativeColorPixel(*context.Get(),*msaa.sampled.resource.Get(),x,y);
        Require(ReadNativeColorPixel(*context.Get(),*msaa.surface.Get(),x,y)==pixel,
          "MSAA diagnostic pixel differs from explicit resolve");
        Require(std::abs(pixel[0]-fraction)<.001f && pixel[1]==0 &&
          std::abs(pixel[2]-(1-fraction))<.001f && std::abs(pixel[3]-fraction)<.001f,
          "MSAA per-sample averaging mismatch");
      }
      // First write depth0 only into sample0. On a second LESS draw, that
      // sample must reject while every other sample (still depth1) passes.
      context->OMSetRenderTargets(1,&rtv,depth.target.Get());
      auto depth_write=CreateNativeRenderState(*device.Get(),{0x10001,0x16,0,0,15,0});
      depth_write.Bind(*context.Get()); context->RSSetState(raster_state.Get());
      ClearNativeDepthTarget(*context.Get(),depth,true,true,1,0);
      context->OMSetBlendState(nullptr,nullptr,1);
      context->Draw(3,0);
      context->ClearRenderTargetView(rtv,blue);
      context->OMSetBlendState(nullptr,nullptr,UINT32_MAX);
      context->Draw(3,0);
      context->OMSetRenderTargets(0,nullptr,nullptr);
      ResolveNativeRenderTarget(backend->Recorder(),msaa);
      for(uint32_t y=0;y<2;++y) for(uint32_t x=0;x<4;++x) {
        const auto pixel=ReadNativeColorPixel(*context.Get(),*msaa.sampled.resource.Get(),x,y);
        Require(std::abs(pixel[0]-(1-fraction))<.001f && pixel[1]==0 &&
          std::abs(pixel[2]-fraction)<.001f && std::abs(pixel[3]-(1-fraction))<.001f,
          "MSAA per-sample depth write/rejection mismatch");
      }
      context->OMSetDepthStencilState(nullptr,0);
    }
    for(uint32_t samples:{0u,3u,8u,UINT32_MAX}) {
      bool color_rejected=false,depth_rejected=false;
      try { CreateNativeRenderTarget(*backend,4,2,DXGI_FORMAT_R16G16B16A16_FLOAT,samples); }
      catch(const std::runtime_error&) {color_rejected=true;}
      try { CreateNativeDepthTarget(*backend,4,2,DXGI_FORMAT_D32_FLOAT,samples); }
      catch(const std::runtime_error&) {depth_rejected=true;}
      Require(color_rejected && depth_rejected,"unsupported sample count accepted");
    }
    auto target = CreateNativeRenderTarget(*backend,4,2,DXGI_FORMAT_R16G16B16A16_FLOAT);
    Require(!target.content_valid && !target.sampled.content_valid,"allocated target marked initialized");
    ResolveNativeRenderTarget(backend->Recorder(),target);
    Require(!target.sampled.content_valid,"unwritten resolve marked initialized");
    const float first[]{2,.5f,-1,1}, second[]{.25f,4,0,1};
    context->ClearRenderTargetView(target.target.Get(),first);
    target.content_valid = true;
    ResolveNativeRenderTarget(backend->Recorder(),target);
    Require(target.sampled.content_valid,"written resolve not initialized");
    D3D11_TEXTURE2D_DESC readback_desc{};
    target.sampled.resource->GetDesc(&readback_desc);
    readback_desc.Usage = D3D11_USAGE_STAGING; readback_desc.BindFlags = 0;
    readback_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    Require(SUCCEEDED(device->CreateTexture2D(&readback_desc,nullptr,&readback)),"resolve readback creation");
    auto verify_resolved = [&](std::array<uint16_t,4> expected) {
      context->CopyResource(readback.Get(),target.sampled.resource.Get());
      D3D11_MAPPED_SUBRESOURCE mapped{};
      Require(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"resolve readback map");
      bool correct = true;
      for (unsigned y = 0; y < 2; ++y) {
        const auto* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch);
        for (unsigned x = 0; x < 4; ++x) for (unsigned c = 0; c < 4; ++c)
          correct &= row[x*4+c] == expected[c];
      }
      context->Unmap(readback.Get(),0);
      Require(correct,"resolved HDR contents differ");
    };
    verify_resolved({0x4000,0x3800,0xbc00,0x3c00});
    context->ClearRenderTargetView(target.target.Get(),second);
    verify_resolved({0x4000,0x3800,0xbc00,0x3c00}); // No implicit resolve.
    ResolveNativeRenderTarget(backend->Recorder(),target);
    verify_resolved({0x3400,0x4400,0,0x3c00});
    ClearNativeColorTarget(*context.Get(),target,0x00ff00ff);
    Require(target.content_valid,"color clear did not initialize surface");
    verify_resolved({0x3400,0x4400,0,0x3c00}); // Clear is not a resolve.
    ResolveNativeRenderTarget(backend->Recorder(),target);
    verify_resolved({0x3c00,0,0x3c00,0}); // ARGB -> RGBA, including zero alpha.
    ClearNativeColorTarget(*context.Get(),target,0xff008000);
    ResolveNativeRenderTarget(backend->Recorder(),target);
    verify_resolved({0,0x3804,0,0x3c00}); // 128/255, not 128/256.
    // Sampling the last resolve while rendering the next frame must not make
    // D3D11 silently null an SRV. Exercise both binding orders repeatedly.
    Require(target.surface.Get()!=target.sampled.resource.Get(),"render/resolve resources alias");
    for(unsigned frame=0;frame<32;++frame) {
      auto* srv=target.sampled.view.Get();
      auto* rtv=target.target.Get();
      if(frame&1) context->OMSetRenderTargets(1,&rtv,nullptr);
      context->PSSetShaderResources(3,1,&srv);
      context->VSSetShaderResources(5,1,&srv);
      if(!(frame&1)) context->OMSetRenderTargets(1,&rtv,nullptr);
      ComPtr<ID3D11ShaderResourceView> pixel_view,vertex_view;
      ComPtr<ID3D11RenderTargetView> active_target;
      context->PSGetShaderResources(3,1,&pixel_view);
      context->VSGetShaderResources(5,1,&vertex_view);
      context->OMGetRenderTargets(1,&active_target,nullptr);
      Require(pixel_view.Get()==srv && vertex_view.Get()==srv && active_target.Get()==rtv,
              "render/resolve binding hazard nulled a view");
      const auto before=ReadNativeColorPixel(*context.Get(),*target.sampled.resource.Get(),0,0);
      context->ClearRenderTargetView(rtv,(frame&1)?first:second);
      Require(ReadNativeColorPixel(*context.Get(),*target.sampled.resource.Get(),0,0)==before,
              "unresolved render changed sampled history");
      srv=nullptr;
      context->PSSetShaderResources(3,1,&srv);
      context->VSSetShaderResources(5,1,&srv);
      ResolveNativeRenderTarget(backend->Recorder(),target);
      if(frame&1) verify_resolved({0x4000,0x3800,0xbc00,0x3c00});
      else verify_resolved({0x3400,0x4400,0,0x3c00});
      context->OMSetRenderTargets(0,nullptr,nullptr);
    }
    NativeRenderTarget absent;
    bool rejected_color_clear=false;
    try { ClearNativeColorTarget(*context.Get(),absent,0); }
    catch (const std::runtime_error&) { rejected_color_clear=true; }
    Require(rejected_color_clear && !absent.content_valid,"invalid color clear initialized target");
    bool invalid_target = false;
    try { CreateNativeRenderTarget(*backend,0,1,DXGI_FORMAT_R16_FLOAT); }
    catch (const std::runtime_error&) { invalid_target = true; }
    Require(invalid_target,"zero-sized target accepted");
    {
      auto history=CreateNativeLuminanceTarget(*backend,1,1);
      std::array<uint8_t,4096> initial_page{};
      initial_page.back()=1;
      Require(!ImportZeroLuminanceHistory(*context.Get(),history,initial_page),"nonuniform initial history accepted");
      initial_page.back()=0;
      Require(!ImportZeroLuminanceHistory(*context.Get(),history,std::span(initial_page).first(4095)),"truncated initial page accepted");
      Require(ImportZeroLuminanceHistory(*context.Get(),history,initial_page),"zero initial history rejected");
      Require(history.sampled.content_valid && !history.content_valid,"history import cleared render surface");
      D3D11_TEXTURE2D_DESC history_desc{}; history.sampled.resource->GetDesc(&history_desc);
      history_desc.Usage=D3D11_USAGE_STAGING; history_desc.BindFlags=0; history_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
      ComPtr<ID3D11Texture2D> history_readback;
      Require(SUCCEEDED(device->CreateTexture2D(&history_desc,nullptr,&history_readback)),"history readback creation");
      context->CopyResource(history_readback.Get(),history.sampled.resource.Get());
      D3D11_MAPPED_SUBRESOURCE history_map{};
      Require(SUCCEEDED(context->Map(history_readback.Get(),0,D3D11_MAP_READ,0,&history_map)),"history readback mapping");
      const auto* history_pixel=static_cast<const uint16_t*>(history_map.pData);
      const bool history_correct=history_pixel[0]==0 && history_pixel[1]==0x3c00 &&
        history_pixel[2]==0x3c00 && history_pixel[3]==0x3c00;
      context->Unmap(history_readback.Get(),0);
      Require(history_correct,"initial history R111 pixels differ");
      Require(!ImportZeroLuminanceHistory(*context.Get(),history,initial_page),"history import overwrote existing data");
      auto invalid_history=CreateNativeLuminanceTarget(*backend,2,1);
      Require(!ImportZeroLuminanceHistory(*context.Get(),invalid_history,initial_page),"non-1x1 history accepted");
      auto luminance=CreateNativeLuminanceTarget(*backend,9,3);
      Require(!luminance.content_valid && !luminance.sampled.content_valid,"luminance allocation initialized");
      ResolveNativeRenderTarget(backend->Recorder(),luminance);
      Require(!luminance.sampled.content_valid,"unwritten luminance resolve initialized");
      D3D11_TEXTURE2D_DESC surface_desc{}; luminance.surface->GetDesc(&surface_desc);
      Require(surface_desc.Format==DXGI_FORMAT_R32_FLOAT,"luminance surface lost float32 precision");
      const std::array<float,4> values{.5f,-2,1.0003f,65504};
      const std::array<uint16_t,4> halves{0x3800,0xc000,0x3c00,0x7bff};
      std::array<float,27> pixels{};
      for (size_t i=0;i<pixels.size();++i) pixels[i]=values[i%4];
      context->UpdateSubresource(luminance.surface.Get(),0,nullptr,pixels.data(),9*4,0);
      luminance.content_valid=true;
      auto* luminance_rtv=luminance.target.Get();
      context->OMSetRenderTargets(1,&luminance_rtv,nullptr);
      ResolveNativeRenderTarget(backend->Recorder(),luminance);
      Require(luminance.sampled.content_valid,"luminance conversion not published");
      // The conversion is a draw now, so it leaves its own target bound. That
      // is the contract - a recorder has no getters to restore from - and the
      // check is that it really did change, so a caller cannot go on assuming
      // otherwise the way the compute pass let it.
      ComPtr<ID3D11RenderTargetView> after;
      context->OMGetRenderTargets(1,&after,nullptr);
      Require(after.Get()!=luminance_rtv,"conversion did not bind its own target");
      context->OMSetRenderTargets(1,&luminance_rtv,nullptr);
      D3D11_TEXTURE2D_DESC sample_desc{}; luminance.sampled.resource->GetDesc(&sample_desc);
      sample_desc.Usage=D3D11_USAGE_STAGING; sample_desc.BindFlags=0; sample_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
      ComPtr<ID3D11Texture2D> converted;
      Require(SUCCEEDED(device->CreateTexture2D(&sample_desc,nullptr,&converted)),"luminance readback creation");
      auto verify_luminance=[&] {
        context->CopyResource(converted.Get(),luminance.sampled.resource.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Require(SUCCEEDED(context->Map(converted.Get(),0,D3D11_MAP_READ,0,&mapped)),"luminance readback map");
        bool correct=true;
        for (size_t y=0;y<3;++y) {
          const auto* row=reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch);
          for (size_t x=0;x<9;++x) correct &= row[x*4]==halves[(y*9+x)%4] &&
            row[x*4+1]==0x3c00 && row[x*4+2]==0x3c00 && row[x*4+3]==0x3c00;
        }
        context->Unmap(converted.Get(),0);
        Require(correct,"luminance half conversion/R111/channel/pitch mismatch");
      };
      verify_luminance();
      ClearNativeColorTarget(*context.Get(),luminance,0);
      verify_luminance(); // Clear changes only the R32F surface, not old history.
      context->OMSetRenderTargets(0,nullptr,nullptr);
    }
    {
      auto bloom=CreateNativeBloomTarget(*backend,9,3);
      const std::array<uint16_t,4> hdr{0xbc00,0x3800,0x4000,0x3400};
      std::array<uint16_t,108> pixels{};
      for (size_t i=0;i<pixels.size();++i) pixels[i]=hdr[i%4];
      context->UpdateSubresource(bloom.surface.Get(),0,nullptr,pixels.data(),9*8,0);
      bloom.content_valid=true;
      ResolveNativeRenderTarget(backend->Recorder(),bloom);
      Require(bloom.sampled.content_valid,"bloom conversion not initialized");
      D3D11_TEXTURE2D_DESC desc{}; bloom.sampled.resource->GetDesc(&desc);
      Require(desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM,"bloom format not quantized RGBA8");
      desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
      ComPtr<ID3D11Texture2D> readback;
      Require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&readback)),"bloom readback creation");
      auto verify_bloom=[&] {
        context->CopyResource(readback.Get(),bloom.sampled.resource.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Require(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"bloom readback mapping");
        bool correct=true;
        for (size_t y=0;y<3;++y) {
          const auto* row=static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch;
          for (size_t x=0;x<9;++x) correct &= row[x*4]==0 && row[x*4+1]==128 && row[x*4+2]==255 && row[x*4+3]==64;
        }
        context->Unmap(readback.Get(),0);
        Require(correct,"bloom clamp/quantization/channel mapping differs");
      };
      verify_bloom();
      for(uint32_t samples:{1u,2u,4u}) {
        auto direct=CreateNativeRenderTarget(*backend,9,3,DXGI_FORMAT_R16G16B16A16_FLOAT,samples);
        const float clear[]{-1,.5f,2,.25f};
        context->ClearRenderTargetView(direct.target.Get(),clear);
        direct.content_valid=true;
        ResolveNativeRgba8Frame(backend->Recorder(),direct,bloom);
        Require(bloom.sampled.content_valid,"direct frame resolve not valid");
        verify_bloom();
        direct.content_valid=false;
        ResolveNativeRgba8Frame(backend->Recorder(),direct,bloom);
        Require(!bloom.sampled.content_valid,"unwritten direct frame published");
        verify_bloom(); // Invalidating must not overwrite old sampled bytes.
        direct.content_valid=true;
        auto opaque=CreateNativeOpaqueFrameTarget(*backend,9,3);
        ResolveNativeRgba8Frame(backend->Recorder(),direct,opaque);
        Require(opaque.sampled.content_valid,"opaque direct resolve not initialized");
        for(uint32_t y=0;y<3;++y) for(uint32_t x=0;x<9;++x) {
          const auto pixel=ReadNativeColorPixel(*context.Get(),*opaque.sampled.resource.Get(),x,y);
          Require(pixel[0]==0 && std::abs(pixel[1]-128.0f/255)<.00001f && pixel[2]==1 && pixel[3]==1,
                  "opaque direct resolve changed RGB or retained source alpha");
        }
      }
      {
        auto wrong=CreateNativeRenderTarget(*backend,8,3,DXGI_FORMAT_R16G16B16A16_FLOAT);
        bool rejected=false;
        try { ResolveNativeRgba8Frame(backend->Recorder(),wrong,bloom); }
        catch(const std::exception&) { rejected=true; }
        Require(rejected && !bloom.sampled.content_valid,"mismatched direct frame accepted");
        rejected=false;
        try { ResolveNativeRgba8Frame(backend->Recorder(),bloom,bloom); }
        catch(const std::exception&) { rejected=true; }
        Require(rejected,"aliased direct frame accepted");
      }
      ClearNativeColorTarget(*context.Get(),bloom,0xffffffff);
      verify_bloom(); // Retain previous resolved blur until the explicit copy.
      auto not_history=CreateNativeBloomTarget(*backend,1,1);
      std::array<uint8_t,4096> zero{};
      Require(!ImportZeroLuminanceHistory(*context.Get(),not_history,zero),"RGBA8 target accepted as luminance history");
    }
    auto depth=CreateNativeDepthTarget(*backend,4,2,DXGI_FORMAT_D24_UNORM_S8_UINT);
    D3D11_TEXTURE2D_DESC depth_desc{}; depth.surface->GetDesc(&depth_desc);
    depth_desc.Usage=D3D11_USAGE_STAGING; depth_desc.BindFlags=0;
    depth_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> depth_readback;
    Require(SUCCEEDED(device->CreateTexture2D(&depth_desc,nullptr,&depth_readback)),"depth readback creation");
    auto verify_depth=[&](uint32_t expected) {
      context->CopyResource(depth_readback.Get(),depth.surface.Get());
      D3D11_MAPPED_SUBRESOURCE mapped{};
      Require(SUCCEEDED(context->Map(depth_readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"depth readback map");
      bool correct=true;
      for (unsigned y=0;y<2;++y) {
        const auto* row=reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch);
        for (unsigned x=0;x<4;++x) correct &= row[x]==expected;
      }
      context->Unmap(depth_readback.Get(),0);
      Require(correct,"selective depth/stencil clear damaged retained channel");
    };
    ClearNativeDepthTarget(*context.Get(),depth,true,true,1,0x53); verify_depth(0x53ffffff);
    ClearNativeDepthTarget(*context.Get(),depth,true,false,0,0); verify_depth(0x53000000);
    ClearNativeDepthTarget(*context.Get(),depth,false,true,0,0xa6); verify_depth(0xa6000000);
    Require(depth.depth_valid && depth.stencil_valid,"clears not recorded");
    bool rejected_clear=false;
    try { ClearNativeDepthTarget(*context.Get(),depth,true,false,2,0); }
    catch (const std::runtime_error&) { rejected_clear=true; }
    Require(rejected_clear,"out-of-range depth accepted"); verify_depth(0xa6000000);
    auto depth32=CreateNativeDepthTarget(*backend,4,2,DXGI_FORMAT_D32_FLOAT);
    rejected_clear=false;
    try { ClearNativeDepthTarget(*context.Get(),depth32,false,true,0,1); }
    catch (const std::runtime_error&) { rejected_clear=true; }
    Require(rejected_clear && !depth32.stencil_valid,"stencil clear accepted without stencil storage");
    for (auto format : {DXGI_FORMAT_D32_FLOAT,DXGI_FORMAT_D32_FLOAT_S8X24_UINT}) {
      auto inspected=CreateNativeDepthTarget(*backend,4,3,format);
      ClearNativeDepthTarget(*context.Get(),inspected,true,false,0,0);
      auto coverage=InspectNativeDepth(*context.Get(),*inspected.surface.Get(),0);
      Require(!coverage.changed_pixels && !coverage.nonfinite_pixels && coverage.minimum==0 && coverage.maximum==0,
        "clear depth misreported as geometry");
      ClearNativeDepthTarget(*context.Get(),inspected,true,false,.5f,0);
      coverage=InspectNativeDepth(*context.Get(),*inspected.surface.Get(),0);
      Require(coverage.changed_pixels==12 && coverage.minimum==.5f && coverage.maximum==.5f &&
        coverage.vertical_bands==std::array<uint64_t,3>{4,4,4},"depth diagnostic stride/bands/range");
    }
    {
      auto region=CreateNativeRenderTarget(*backend,3,2,DXGI_FORMAT_R16G16B16A16_FLOAT);
      std::array<uint16_t,24> data{};
      // Finite magenta must not collide with the diagnostic's BMP marker.
      for(size_t p=0;p<6;++p) {data[p*4]=0x3c00;data[p*4+2]=0x3c00;data[p*4+3]=0x7c00;}
      context->UpdateSubresource(region.surface.Get(),0,nullptr,data.data(),24,0);
      uint32_t x=99,y=99;
      Require(!FindNativeInvalidColorPixel(*context.Get(),*region.surface.Get(),0,0,3,2,x,y) && x==99 && y==99,
        "finite magenta or nonfinite alpha triggered RGB region probe");
      for(uint32_t channel=0;channel<3;++channel) for(uint16_t value:{uint16_t(0x7c00),uint16_t(0xfc00),uint16_t(0x7e00)}) {
        auto changed=data; changed[5*4+channel]=value;
        context->UpdateSubresource(region.surface.Get(),0,nullptr,changed.data(),24,0);
        Require(FindNativeInvalidColorPixel(*context.Get(),*region.surface.Get(),1,1,2,1,x,y) && x==2 && y==1,
          "region probe missed nonfinite RGB or returned relative coordinates");
        Require(!FindNativeInvalidColorPixel(*context.Get(),*region.surface.Get(),0,0,3,1,x,y),
          "region probe escaped requested rectangle");
        const auto pixel=ReadNativeColorPixel(*context.Get(),*region.surface.Get(),2,1);
        Require(!std::isfinite(pixel[channel]) && !region.content_valid,"region probe modified source or validity");
      }
      bool rejected=false;
      try {FindNativeInvalidColorPixel(*context.Get(),*region.surface.Get(),2,1,UINT32_MAX,1,x,y);}
      catch(const std::runtime_error&) {rejected=true;}
      Require(rejected,"overflowing diagnostic region accepted");
    }
    std::cout << "Native BC1/BC2/BC3 cube mip readback and malformed DDS tests passed\n";
    std::cout << "Native A8 shadow alpha, padded rows and mip readback passed\n";
    std::cout << "Native HDR diagnostic BMP orientation, clamping, nonfinite markers and validity isolation passed\n";
    std::cout << "Native HDR render target, explicit resolve and retained history passed\n";
    std::cout << "Native depth/stencil selective clears and retained channel readback passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

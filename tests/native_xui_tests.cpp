#include "native_graphics/xui_effect.h"
#include "native_graphics/font_effect.h"
#include "native_graphics/native_font_bindings.h"
#include "native_graphics/native_xui_bindings.h"
#include "native_graphics/d3d11_bindings.h"
#include "native_graphics/d3d11_quads.h"
#include "native_graphics/d3d11_texture.h"
#include "native_graphics/d3d11_render_state.h"
#include <bit>
#include <cmath>
#include <iostream>
using namespace edf::native;
using Microsoft::WRL::ComPtr;
void Require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
template<size_t N> std::span<const uint8_t> Bytes(const std::array<float,N>& data) {
  return {reinterpret_cast<const uint8_t*>(data.data()),sizeof(data)};
}
int main() {
  try {
    for(size_t group=0;group<4;++group)
      for(const auto entry:{"VS_2D","VS_2DTex","PS_2D","VS_3D"})
        for(const auto parameter:{"_g_DX2DScale","_g_DX2DOffset","_g_Color"}) {
          const bool expected=group<2 && std::string_view(entry)!="PS_2D" &&
            std::string_view(entry)!="VS_3D" && std::string_view(parameter)!="_g_Color";
          Require(IsNativeCanvasXY(0xc885203e230fe745ull,entry,parameter,group)==expected,
            "canvas parameter classification");
          Require(!IsNativeCanvasXY(0,entry,parameter,group),"unrelated shader canvas classification");
        }
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    Require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
      D3D11_SDK_VERSION,&device,nullptr,&context)),"XUI device");
    const auto effect=MakeNativeXuiTextureEffect();
    auto clip=MakeNativeViewport(0,0,1920,1080,0,1,true,{784,668,854,680});
    const auto scaled_clip=ScaleNativeCanvasScissor(clip,1.5f,1.5f,true);
    Require(scaled_clip.scissor.left==696 && scaled_clip.scissor.top==732 &&
      scaled_clip.scissor.right==801 && scaled_clip.scissor.bottom==750,"centered font clip scaling");
    const auto original_clip=ScaleNativeCanvasScissor(clip,1,1,true);
    Require(original_clip.scissor.left==784 && original_clip.scissor.bottom==680,"default clip changed");
    clip.scissor={0,0,1920,1080};
    const auto full_clip=ScaleNativeCanvasScissor(clip,1.5f,1.5f,true);
    Require(full_clip.scissor.left==0 && full_clip.scissor.right==1920 && full_clip.scissor.bottom==1080,"full clip lost coverage");
    clip.scissor={1,1,1,1};
    const auto empty_clip=ScaleNativeCanvasScissor(clip,1.5f,1.5f,false);
    Require(empty_clip.scissor.left==empty_clip.scissor.right && empty_clip.scissor.top==empty_clip.scissor.bottom,"empty scaled clip became visible");
    ShaderBindings vs(*device.Get(),CompileNativeShader(*device.Get(),effect,effect.entries[0],"xui.fx"));
    ShaderBindings ps(*device.Get(),CompileNativeShader(*device.Get(),effect,effect.entries[1],"xui.fx"));
    ValidateNativeShaderLink(vs.shader(),ps.shader());
    PositionTriangleStream vertices(*device.Get(),vs.shader());
    const std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const std::array<float,16> projection{.5f,0,0,-1,0,-1,0,1,0,0,1,0,0,0,0,1};
    vs.SetConstant("TransformRows",Bytes(identity)); vs.SetConstant("ProjectionRows",Bytes(projection));
    vs.SetConstant("Params",Bytes(std::array<float,4>{0,0,0,0}));
    vs.SetConstant("ArithmeticBias",Bytes(std::array<float,4>{0,0,0,0}));
    vs.SetConstant("TextureRows",Bytes(std::array<float,8>{.25f,0,0,0,0,.5f,0,0}));
    vs.SetConstant("TextureOffset",Bytes(std::array<float,4>{0,0,0,0}));
    ps.SetConstant("ColorFactor",Bytes(std::array<float,4>{.5f,.25f,.75f,.5f}));
    const std::array<float,16> pixels{1,0,0,1, 0,1,0,.5f, 0,0,1,.25f, 1,1,1,0};
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=desc.Height=2;
    desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{pixels.data(),32,0};
    ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11ShaderResourceView> view;
    Require(SUCCEEDED(device->CreateTexture2D(&desc,&initial,&texture)),"XUI texture");
    Require(SUCCEEDED(device->CreateShaderResourceView(texture.Get(),nullptr,&view)),"XUI texture view");
    D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    ComPtr<ID3D11SamplerState> sampler;
    Require(SUCCEEDED(device->CreateSamplerState(&sd,&sampler)),"XUI sampler");
    ps.SetTexture("BrushTexture",view.Get()); ps.SetSampler("BrushSampler",sampler.Get());
    auto target=CreateNativeRenderTarget(*device.Get(),4,2,DXGI_FORMAT_R16G16B16A16_FLOAT);
    auto* rtv=target.target.Get(); context->OMSetRenderTargets(1,&rtv,nullptr);
    const D3D11_VIEWPORT viewport{0,0,4,2,0,1}; context->RSSetViewports(1,&viewport);
    std::vector<uint8_t> guest;
    // The retail brush supplies no UV stream: derive it from position/constants.
    for(float f:std::array<float,12>{0,0,4,0,4,2,0,0,4,2,0,2}) {
      const auto word=std::bit_cast<uint32_t>(f);
      for(int shift=24;shift>=0;shift-=8) guest.push_back(uint8_t(word>>shift));
    }
    auto reject=[&](auto&& action) {
      bool rejected=false;
      try { action(); } catch(const std::runtime_error&) { rejected=true; }
      Require(rejected,"invalid position triangle input accepted");
    };
    reject([&]{vertices.Draw(*context.Get(),{});});
    reject([&]{vertices.Draw(*context.Get(),std::span(guest).first(16));});
    reject([&]{vertices.Draw(*context.Get(),std::span(guest).first(47));});
    const std::vector<uint8_t> oversized(16386*8);
    reject([&]{vertices.Draw(*context.Get(),oversized);});
    auto invalid=guest; invalid[0]=0x7f; invalid[1]=0xc0; invalid[2]=invalid[3]=0;
    reject([&]{vertices.Draw(*context.Get(),invalid);});
    invalid[1]=0x80;
    reject([&]{vertices.Draw(*context.Get(),invalid);});
    ComPtr<ID3D11DeviceContext> deferred;
    Require(SUCCEEDED(device->CreateDeferredContext(0,&deferred)),"XUI deferred fixture");
    reject([&]{vertices.Draw(*deferred.Get(),guest);});
    ComPtr<ID3D11Device> foreign_device; ComPtr<ID3D11DeviceContext> foreign_context;
    Require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
      D3D11_SDK_VERSION,&foreign_device,nullptr,&foreign_context)),"foreign XUI device");
    reject([&]{vertices.Draw(*foreign_context.Get(),guest);});
    for(int offset=0;offset<2;++offset) {
      vs.SetConstant("TextureOffset",Bytes(std::array<float,4>{offset*.5f,0,0,0}));
      vs.Bind(*context.Get()); ps.Bind(*context.Get()); vertices.Draw(*context.Get(),guest);
      for(uint32_t y=0;y<2;++y) for(uint32_t x=0;x<4;++x) {
        const auto actual=ReadNativeColorPixel(*context.Get(),*target.surface.Get(),x,y);
        const auto index=(y*2+(x/2+offset)%2)*4;
        const std::array<float,4> factor{.5f,.25f,.75f,.5f};
        for(size_t c=0;c<4;++c)
          Require(std::abs(actual[c]-pixels[index+c]*factor[c])<.0001f,"XUI UV/tint/alpha mismatch");
      }
    }
    // Reuse the ordinary position stream with the reversed vertex shader, as
    // scene-targeted XUI does. A depth attachment must accept/reject correctly.
    for(bool reversed:{false,true}) {
      ShaderBindings scene_vs(*device.Get(),CompileNativeShader(*device.Get(),effect,effect.entries[0],"xui.fx",reversed));
      NativeXuiVertexBindings scene_plan(scene_vs);
      std::array<uint8_t,192> scene_registers{};
      std::array<uint8_t,16> scene_bias{};
      auto store_registers=[&](size_t offset,std::span<const float> values) {
        for(size_t lane=0;lane<values.size();++lane) {
          const auto word=std::bit_cast<uint32_t>(values[lane]);
          for(size_t b=0;b<4;++b) scene_registers[offset+lane*4+b]=uint8_t(word>>(24-b*8));
        }
      };
      store_registers(0,identity); store_registers(64,projection);
      const auto scaled=NativeXuiCanvasProjection(std::span(scene_registers).subspan(64,64),1.5f,1.5f);
      Require(scaled[0]==.75f && scaled[5]==-1.5f && scaled[3]==-1 && scaled[7]==1,
              "XUI canvas scale lost top-left anchoring");
      for(size_t i=8;i<16;++i) Require(scaled[i]==projection[i],"XUI canvas changed depth/homogeneous row");
      Require(NativeXuiCanvasProjection(std::span(scene_registers).subspan(64,64),1,1)==projection,
              "XUI default projection changed");
      reject([&]{NativeXuiCanvasProjection(std::span(scene_registers).subspan(64,64),0,1);});
      reject([&]{NativeXuiCanvasProjection(std::span(scene_registers).subspan(64,63),1,1);});
      store_registers(128,std::array<float,4>{.25f,0,0,0});
      store_registers(160,std::array<float,8>{.25f,0,0,0,0,.5f,0,0});
      scene_plan.SetConstants(scene_vs,scene_registers,scene_bias);
      const auto prior_params=scene_vs.ReadFloatVector("Params");
      reject([&]{scene_plan.SetConstants(scene_vs,std::span(scene_registers).first(191),scene_bias);});
      reject([&]{scene_plan.SetConstants(scene_vs,scene_registers,std::span(scene_bias).first(15));});
      reject([&]{scene_plan.SetConstants(vs,scene_registers,scene_bias);});
      Require(scene_vs.ReadFloatVector("Params")==prior_params,"rejected XUI plan changed constants");
      auto depth=CreateNativeDepthTarget(*device.Get(),4,2,DXGI_FORMAT_D32_FLOAT_S8X24_UINT);
      context->OMSetRenderTargets(1,&rtv,depth.target.Get());
      for(unsigned mode=0;mode<3;++mode) {
        const bool visible=mode!=0;
        const bool greater=reversed==visible;
        // Mode2 is the actual loading-screen scene depth word captured live.
        const uint32_t depth_word=mode==2?0x700764u:greater?0x42u:0x12u;
        auto state=CreateNativeRenderState(*device.Get(),{0x10001,depth_word,0,0,15,0});
        state.Bind(*context.Get());
        ClearNativeDepthTarget(*context.Get(),depth,true,false,.5f,0);
        const float blank[]{-2,-2,-2,-2}; context->ClearRenderTargetView(rtv,blank);
        scene_vs.Bind(*context.Get()); ps.Bind(*context.Get()); vertices.Draw(*context.Get(),guest);
        for(uint32_t y=0;y<2;++y) for(uint32_t x=0;x<4;++x) {
          const auto actual=ReadNativeColorPixel(*context.Get(),*target.surface.Get(),x,y);
          const std::array<float,4> factor{.5f,.25f,.75f,.5f};
          for(size_t c=0;c<4;++c)
            Require(std::abs(actual[c]-(visible?pixels[(y*2+x/2)*4+c]*factor[c]:-2.f))<.0001f,
              "XUI scene reversed shader/depth mismatch");
        }
      }
    }
    context->OMSetRenderTargets(1,&rtv,nullptr);
    auto restored_state=CreateNativeRenderState(*device.Get(),{0x10001,0,0,0,15,0});
    restored_state.Bind(*context.Get());
    auto translated=identity; translated[3]=2;
    vs.SetConstant("TransformRows",Bytes(translated));
    const float clear[]{-2,-2,-2,-2}; context->ClearRenderTargetView(rtv,clear);
    vs.Bind(*context.Get()); ps.Bind(*context.Get()); vertices.Draw(*context.Get(),guest);
    Require(ReadNativeColorPixel(*context.Get(),*target.surface.Get(),0,0)==
      std::array<float,4>{-2,-2,-2,-2},"XUI transform did not move geometry");
    Require(ReadNativeColorPixel(*context.Get(),*target.surface.Get(),3,0)==
      std::array<float,4>{0,.25f,0,.25f},"XUI transform changed local texture coordinates");
    vs.SetConstant("TransformRows",Bytes(identity));
    vs.SetConstant("TextureOffset",Bytes(std::array<float,4>{0,0,0,0}));
    auto guest_float4=[](std::array<float,4> values) {
      std::array<uint8_t,16> bytes{};
      for(size_t lane=0;lane<4;++lane) {
        const auto word=std::bit_cast<uint32_t>(values[lane]);
        for(size_t b=0;b<4;++b) bytes[lane*4+b]=uint8_t(word>>(24-b*8));
      }
      return bytes;
    };
    for(size_t entry:{size_t(1),size_t(2),size_t(3)}) {
      const auto canvas_input=guest_float4({.25f,-.5f,3,4});
      const auto expected_canvas=guest_float4({.375f,-1,3,4});
      Require(ScaleNativeCanvasXY(canvas_input,1.5f,2)==expected_canvas,"native canvas XY scale");
      Require(ScaleNativeCanvasXY(canvas_input,1,1)==canvas_input,"native canvas default bytes");
      reject([&]{ScaleNativeCanvasXY(canvas_input,0,1);});
      reject([&]{ScaleNativeCanvasXY(std::span(canvas_input).first(15),1,1);});
      ShaderBindings variant(*device.Get(),CompileNativeShader(*device.Get(),effect,effect.entries[entry],"xui.fx"));
      ValidateNativeShaderLink(vs.shader(),variant.shader());
      NativeXuiPixelBindings pixel_plan(variant,entry==2);
      const auto factor=guest_float4({.5f,.25f,.75f,.5f});
      const auto brush=guest_float4({.25f,.5f,1,.75f});
      pixel_plan.SetConstants(variant,factor,entry==2?std::span<const uint8_t>(brush):std::span<const uint8_t>{});
      const auto prior_factor=variant.ReadFloatVector("ColorFactor");
      reject([&]{pixel_plan.SetConstants(variant,std::span(factor).first(15));});
      reject([&]{pixel_plan.SetConstants(ps,factor,entry==2?std::span<const uint8_t>(brush):std::span<const uint8_t>{});});
      Require(variant.ReadFloatVector("ColorFactor")==prior_factor,"rejected XUI pixel plan mutated constants");
      if(entry==2) {
        reject([&]{pixel_plan.SetConstants(variant,factor);});
        reject([&]{pixel_plan.SetTexture(variant,view.Get());});
        reject([&]{pixel_plan.SetSampler(variant,sampler.Get());});
      } else {
        pixel_plan.SetTexture(variant,view.Get()); pixel_plan.SetSampler(variant,sampler.Get());
        reject([&]{pixel_plan.SetTexture(ps,view.Get());});
        reject([&]{pixel_plan.SetSampler(ps,sampler.Get());});
      }
      context->ClearRenderTargetView(rtv,clear);
      vs.Bind(*context.Get()); variant.Bind(*context.Get()); vertices.Draw(*context.Get(),guest);
      for(uint32_t y=0;y<2;++y) for(uint32_t x=0;x<4;++x) {
        const auto actual=ReadNativeColorPixel(*context.Get(),*target.surface.Get(),x,y);
        auto expected=entry==2 ? std::array<float,4>{.125f,.125f,.75f,.375f} :
          std::array<float,4>{.5f,.25f,.75f,pixels[(y*2+x/2)*4+3]*.5f};
        if(entry==1) {
          const std::array<float,4> tint{.5f,.25f,.75f,.5f};
          for(size_t c=0;c<4;++c) expected[c]=pixels[(y*2+x/2)*4+c]*tint[c];
        }
        Require(actual==expected,"XUI textured/solid/alpha-mask RGBA mismatch");
      }
    }
    const auto font=MakeNativeFontEffect();
    ShaderBindings font_vs(*device.Get(),CompileNativeShader(*device.Get(),font,font.entries[0],"font.fx"));
    ShaderBindings font_ps(*device.Get(),CompileNativeShader(*device.Get(),font,font.entries[1],"font.fx"));
    ValidateNativeShaderLink(font_vs.shader(),font_ps.shader());
    QuadStream glyph(*device.Get(),font_vs.shader());
    NativeFontBindings font_plan(font_vs,font_ps);
    std::array<uint8_t,64> font_vertex_registers{};
    std::array<uint8_t,32> font_pixel_registers{};
    auto upload_font_register=[](std::span<uint8_t> bytes,size_t slot,std::array<float,4> values) {
      for(size_t lane=0;lane<4;++lane) {
        const auto word=std::bit_cast<uint32_t>(values[lane]);
        for(size_t b=0;b<4;++b) bytes[slot*16+lane*4+b]=uint8_t(word>>(24-b*8));
      }
    };
    upload_font_register(font_vertex_registers,0,{.5f,.25f,.75f,.5f});
    // Nonzero unused lanes must not corrupt adjacent packed float2 fields.
    upload_font_register(font_vertex_registers,3,{.5f,-1,123,456});
    upload_font_register(font_vertex_registers,2,{-1,1,789,123});
    upload_font_register(font_vertex_registers,1,{.5f,.5f,456,789});
    font_plan.SetTexture(font_ps,view.Get()); font_plan.SetSampler(font_ps,sampler.Get());
    std::vector<uint8_t> glyph_data;
    for(float f:std::array<float,16>{0,0,0,0,4,0,2,0,4,2,2,2,0,2,0,2}) {
      const auto word=std::bit_cast<uint32_t>(f);
      for(int shift=24;shift>=0;shift-=8) glyph_data.push_back(uint8_t(word>>shift));
    }
    // Source alpha values are1,.5,.25,0: both branches and exact threshold.
    upload_font_register(font_pixel_registers,0,{0,0,0,1});
    for(const auto mask_value:{0.f,.5f,1.f}) {
      upload_font_register(font_pixel_registers,1,{mask_value,mask_value,mask_value,mask_value});
      font_plan.SetConstants(font_vs,font_ps,font_vertex_registers,font_pixel_registers);
      font_vs.Bind(*context.Get()); font_ps.Bind(*context.Get()); glyph.Draw(*context.Get(),glyph_data);
      for(uint32_t y=0;y<2;++y) for(uint32_t x=0;x<4;++x) {
        const auto actual=ReadNativeColorPixel(*context.Get(),*target.surface.Get(),x,y);
        const auto index=(y*2+x/2)*4;
        const auto value=pixels[index+3];
        const auto rgb=value>.5f ? 2*value-1 : 0;
        const std::array<float,4> decoded{rgb,rgb,rgb,2*(value>.5f ? 1:value)};
        const std::array<float,4> tint{.5f,.25f,.75f,.5f};
        for(size_t c=0;c<4;++c)
          Require(actual[c]==(decoded[c]+mask_value*(pixels[index+c]-decoded[c]))*tint[c],
            "font channel decode, threshold, mask or tint mismatch");
      }
    }
    const auto prior_color=font_vs.ReadFloatVector("VertexColor");
    upload_font_register(font_vertex_registers,0,{1,1,1,1});
    ShaderBindings replacement_ps(*device.Get(),CompileNativeShader(*device.Get(),font,font.entries[1],"font.fx"));
    bool stale=false;
    try { font_plan.SetConstants(font_vs,replacement_ps,font_vertex_registers,font_pixel_registers); }
    catch(const std::runtime_error&) { stale=true; }
    Require(stale && font_vs.ReadFloatVector("VertexColor")==prior_color,"stale pixel generation partially changed font vertex constants");
    bool malformed=false;
    try { font_plan.SetConstants(font_vs,font_ps,std::span(font_vertex_registers).first(63),font_pixel_registers); }
    catch(const std::runtime_error&) { malformed=true; }
    Require(malformed && font_vs.ReadFloatVector("VertexColor")==prior_color,"malformed font block mutated constants");
    std::cout << "Native XUI and recovered font pixel tests passed\n";
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}

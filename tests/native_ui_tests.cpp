#include "native_graphics/d3d11_backend.h"
#include "native_graphics/d3d11_ui.h"
#include "native_graphics/d3d11_texture.h"
#include <array>
#include <cmath>
#include <limits>
#include <iostream>
#include <stdexcept>

using namespace edf::native;
using Microsoft::WRL::ComPtr;
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
template<class F> void Reject(F operation,const char* message) {
  bool rejected=false; try { operation(); } catch(const std::runtime_error&) { rejected=true; }
  Require(rejected,message);
}
int main() {
  try {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    Require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
      D3D11_SDK_VERSION,&device,nullptr,&context)),"UI WARP creation");
    // Render targets are created through the backend now; adopting this device
    // keeps them usable by the direct D3D11 calls the rest of this test makes.
    auto backend=AdoptNativeD3D11Backend(*device.Get(),*context.Get());
    Require(bool(backend),"adopted backend");
    NativeUiRenderer renderer(*device.Get());
    auto target=CreateNativeRenderTarget(*backend,4,4,DXGI_FORMAT_R8G8B8A8_UNORM);
    std::array<NativeUiVertex,4> vertices{{{0,0,0,0,0x800000ff},{2,0,1,0,0x800000ff},
      {2,2,1,1,0x800000ff},{0,2,0,1,0x800000ff}}};
    const uint16_t indices[]{0,1,2,0,2,3};
    renderer.Upload(*context.Get(),vertices,indices);
    NativeUiDraw draw; draw.count=6; draw.scissor={1,1,3,3};
    const auto render=[&] { renderer.Draw(*context.Get(),*target.target.Get(),4,4,2,2,draw); };
    const auto check_pixel=[&](UINT x,UINT y,std::array<float,4> expected,float tolerance=0.005f) {
      const auto actual=ReadNativeColorPixel(*context.Get(),*target.surface.Get(),x,y);
      for(size_t i=0;i<4;++i) Require(std::abs(actual[i]-expected[i])<=tolerance,"UI pixel mismatch");
    };
    ClearNativeColorTarget(*context.Get(),target,0xff0000ff);
    render();
    for(UINT y=0;y<4;++y) for(UINT x=0;x<4;++x)
      check_pixel(x,y,x>=1 && x<3 && y>=1 && y<3?
        std::array<float,4>{128/255.f,0,127/255.f,1}:std::array<float,4>{0,0,1,1});
    ComPtr<ID3D11ShaderResourceView> bound;
    context->PSGetShaderResources(0,1,&bound);
    Require(!bound,"UI source binding retained");
    const uint8_t rgba[]{255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
    auto texture=CreateNativeUiTexture(*device.Get(),2,2,rgba,false,false);
    for(auto& vertex:vertices) vertex.color=0xffffffff;
    renderer.Upload(*context.Get(),vertices,indices);
    draw.texture=&texture; draw.scissor={0,0,4,4}; render();
    check_pixel(0,0,{1,0,0,1}); check_pixel(3,0,{0,1,0,1});
    check_pixel(0,3,{0,0,1,1}); check_pixel(3,3,{1,1,1,1});
    // All four texels contribute at the texture center with linear sampling.
    auto linear=CreateNativeUiTexture(*device.Get(),2,2,rgba,true,false);
    for(auto& vertex:vertices) vertex.u=vertex.v=0.5f;
    renderer.Upload(*context.Get(),vertices,indices); draw.texture=&linear; render();
    check_pixel(2,2,{0.5f,0.5f,0.5f,1});
    auto repeated=CreateNativeUiTexture(*device.Get(),2,2,rgba,false,true);
    for(auto& vertex:vertices) { vertex.u=1.25f; vertex.v=0.25f; }
    renderer.Upload(*context.Get(),vertices,indices); draw.texture=&repeated; render();
    check_pixel(2,2,{1,0,0,1});
    draw.texture=&texture; render(); // Clamp instead of wrapping to the red texel.
    check_pixel(2,2,{0,1,0,1});
    const uint16_t shifted[]{1,2,3,1,3,4};
    renderer.Upload(*context.Get(),vertices,shifted); draw.base_vertex=-1; render();
    check_pixel(2,2,{0,1,0,1});
    draw.base_vertex=-2; Reject(render,"negative indexed vertex accepted");
    draw.base_vertex=0; Reject(render,"oversized indexed vertex accepted");
    draw.base_vertex=-1; draw.index_offset=1; Reject(render,"index span overflow accepted");
    draw.index_offset=0; draw.count=5; Reject(render,"incomplete triangle accepted");
    draw.count=6;
    // Nonindexed draw and base-vertex offsets.
    std::array<NativeUiVertex,7> expanded{};
    for(size_t i=0;i<6;++i) expanded[i+1]=vertices[indices[i]];
    renderer.Upload(*context.Get(),expanded,{}); draw.base_vertex=1; render();
    check_pixel(2,2,{0,1,0,1});
    draw.base_vertex=2; Reject(render,"nonindexed span overflow accepted");
    draw.base_vertex=1; draw.scissor={3,3,1,1};
    ClearNativeColorTarget(*context.Get(),target,0xff0000ff); render(); check_pixel(2,2,{0,0,1,1});
    // Line list coverage without triangle reinterpretation.
    const NativeUiVertex line[]{{0.25f,0.75f,0,0,0xff00ff00},{1.75f,0.75f,0,0,0xff00ff00}};
    renderer.Upload(*context.Get(),line,{}); draw={}; draw.count=2; draw.lines=true; draw.scissor={0,0,4,4};
    ClearNativeColorTarget(*context.Get(),target,0xff000000); render();
    bool green=false;
    for(UINT y=0;y<4;++y) for(UINT x=0;x<4;++x) {
      const auto value=ReadNativeColorPixel(*context.Get(),*target.surface.Get(),x,y);
      green |= value[1]>0.9f;
      Require(value[0]==0 && value[2]==0,"line color corrupted");
    }
    Require(green,"UI line rendered no pixels");
    Reject([&]{renderer.Draw(*context.Get(),*target.target.Get(),3,4,2,2,draw);},"wrong target size accepted");
    Reject([&]{renderer.Draw(*context.Get(),*target.target.Get(),4,4,std::numeric_limits<float>::denorm_min(),2,draw);},
           "overflowing UI projection accepted");
    NativeUiTexture missing;
    draw.texture=&missing; Reject(render,"incomplete texture accepted"); draw.texture=nullptr;
    ComPtr<ID3D11Device> foreign_device; ComPtr<ID3D11DeviceContext> foreign_context;
    Require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
      D3D11_SDK_VERSION,&foreign_device,nullptr,&foreign_context)),"foreign UI device");
    auto foreign_texture=CreateNativeUiTexture(*foreign_device.Get(),2,2,rgba,false,false);
    draw.texture=&foreign_texture; Reject(render,"foreign UI texture accepted"); draw.texture=nullptr;
    NativeUiTexture alias; alias.sampler=texture.sampler;
    D3D11_TEXTURE2D_DESC alias_desc{}; target.surface->GetDesc(&alias_desc);
    alias_desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    Require(SUCCEEDED(device->CreateTexture2D(&alias_desc,nullptr,&alias.resource)),"UI alias texture");
    Require(SUCCEEDED(device->CreateShaderResourceView(alias.resource.Get(),nullptr,&alias.view)),"UI alias SRV");
    ComPtr<ID3D11RenderTargetView> alias_target;
    Require(SUCCEEDED(device->CreateRenderTargetView(alias.resource.Get(),nullptr,&alias_target)),"UI alias RTV");
    draw.texture=&alias;
    Reject([&]{renderer.Draw(*context.Get(),*alias_target.Get(),4,4,2,2,draw);},"UI feedback loop accepted");
    draw.texture=nullptr;
    auto invalid=vertices; invalid[0].x=std::numeric_limits<float>::quiet_NaN();
    Reject([&]{renderer.Upload(*context.Get(),invalid,indices);},"nonfinite UI vertex accepted");
    Reject(render,"failed upload left stale batch available");
    Reject([&]{CreateNativeUiTexture(*device.Get(),2,2,std::span(rgba).first(15),false,false);},"short UI texture accepted");
    Reject([&]{CreateNativeUiTexture(*device.Get(),0,2,{},false,false);},"zero UI texture accepted");
    ComPtr<ID3D11DeviceContext> deferred;
    Require(SUCCEEDED(device->CreateDeferredContext(0,&deferred)),"UI deferred context creation");
    Reject([&]{renderer.Upload(*deferred.Get(),vertices,indices);},"UI deferred context accepted");
    std::cout << "native UI tests passed\n";
    return 0;
  } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

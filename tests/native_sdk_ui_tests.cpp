#include "native_graphics/native_immediate_drawer.h"
#include "native_graphics/d3d11_texture.h"
#include <array>
#include <iostream>
#include <stdexcept>

using namespace edf::native;
using namespace rex::ui;
using Microsoft::WRL::ComPtr;
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
template<class F> void Reject(F operation,const char* message) {
  bool rejected=false; try { operation(); } catch(const std::runtime_error&) { rejected=true; }
  Require(rejected,message);
}
struct ForeignTexture : ImmediateTexture { ForeignTexture():ImmediateTexture(1,1) {} };
int main() {
  try {
    Require(GetModuleHandleW(L"rexgpu-xenos.dll")==nullptr,"SDK UI test loaded Xenos before rendering");
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    Require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
      D3D11_SDK_VERSION,&device,nullptr,&context)),"SDK UI WARP device");
    NativeImmediateDrawer drawer(*device.Get(),*context.Get());
    auto target=CreateNativeRenderTarget(*device.Get(),4,4,DXGI_FORMAT_R8G8B8A8_UNORM);
    AppUIDrawContext ui_context(4,4);
    Reject([&]{drawer.Begin(ui_context,2,2);},"SDK UI accepted absent target");
    auto* rtv=target.target.Get(); context->OMSetRenderTargets(1,&rtv,nullptr);
    ImmediateDraw draw; draw.count=6;
    Reject([&]{drawer.Draw(draw);},"SDK UI accepted draw outside Begin");
    drawer.Begin(ui_context,2,2);
    Reject([&]{drawer.Begin(ui_context,2,2);},"SDK UI nested Begin accepted");
    std::array<ImmediateVertex,4> vertices{{{0,0,0,0,0xffffffff},{2,0,1,0,0xffffffff},
      {2,2,1,1,0xffffffff},{0,2,0,1,0xffffffff}}};
    const uint16_t indices[]{0,1,2,0,2,3};
    ImmediateDrawBatch batch; batch.vertices=vertices.data(); batch.vertex_count=4;
    batch.indices=indices; batch.index_count=6;
    auto bad=batch; bad.vertex_count=-1;
    Reject([&]{drawer.BeginDrawBatch(bad);},"SDK UI negative vertex count accepted");
    Reject([&]{drawer.Draw(draw);},"failed SDK UI batch allowed stale draw");
    bad=batch; bad.vertices=nullptr;
    Reject([&]{drawer.BeginDrawBatch(bad);},"SDK UI null vertex buffer accepted");
    const uint8_t green[]{0,255,0,255};
    auto texture=drawer.CreateTexture(1,1,ImmediateTextureFilter::kNearest,false,green);
    Require(texture->width==1 && texture->height==1,"SDK UI texture metadata");
    Reject([&]{drawer.CreateTexture(1,1,ImmediateTextureFilter::kNearest,false,nullptr);},"SDK UI null texture accepted");
    drawer.BeginDrawBatch(batch);
    Reject([&]{drawer.BeginDrawBatch(batch);},"SDK UI nested batch accepted");
    // The adapter must not retain pointers to caller-owned batch memory.
    for(auto& v:vertices) v.color=0xff0000ff;
    draw.texture=texture.get(); draw.scissor=true;
    draw.scissor_left=draw.scissor_top=0.5f; draw.scissor_right=draw.scissor_bottom=1.5f;
    ClearNativeColorTarget(*context.Get(),target,0xff000000);
    drawer.Draw(draw);
    for(UINT y=0;y<4;++y) for(UINT x=0;x<4;++x) {
      const auto pixel=ReadNativeColorPixel(*context.Get(),*target.surface.Get(),x,y);
      const bool inside=x>=1 && x<3 && y>=1 && y<3;
      Require(pixel[0]==0 && pixel[1]==(inside?1.f:0.f) && pixel[2]==0 && pixel[3]==1,
              "SDK UI scaled scissor/texture/batch snapshot mismatch");
    }
    ForeignTexture foreign; draw.texture=&foreign;
    Reject([&]{drawer.Draw(draw);},"SDK UI foreign texture accepted");
    draw.texture=nullptr; draw.count=-1;
    Reject([&]{drawer.Draw(draw);},"SDK UI negative count accepted"); draw.count=6;
    drawer.EndDrawBatch();
    Reject([&]{drawer.Draw(draw);},"SDK UI drew ended batch");
    Reject([&]{drawer.EndDrawBatch();},"SDK UI duplicate batch end accepted");
    drawer.End();
    // Pixel-coordinate fallback, solid vertex colors, and exception cleanup.
    drawer.Begin(ui_context,0,-1); drawer.BeginDrawBatch(batch);
    draw.scissor=false;
    ClearNativeColorTarget(*context.Get(),target,0xff000000); drawer.Draw(draw);
    auto pixel=ReadNativeColorPixel(*context.Get(),*target.surface.Get(),0,0);
    Require(pixel[0]==1 && pixel[1]==0 && pixel[2]==0,"SDK UI pixel coordinate fallback");
    pixel=ReadNativeColorPixel(*context.Get(),*target.surface.Get(),3,3);
    Require(pixel[0]==0 && pixel[1]==0 && pixel[2]==0,"SDK UI fallback drew outside quad");
    drawer.End(); // Can unwind a failed/incomplete batch without retaining target.
    drawer.Begin(ui_context,2,2); drawer.BeginDrawBatch(batch); drawer.EndDrawBatch(); drawer.End();
    Require(GetModuleHandleW(L"rexgpu-xenos.dll")==nullptr,"SDK UI rendering loaded Xenos");
    std::cout << "native SDK UI tests passed\n";
    return 0;
  } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

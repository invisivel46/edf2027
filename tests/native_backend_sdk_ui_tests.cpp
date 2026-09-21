#include "native_graphics/native_backend_immediate_drawer.h"
#include "native_graphics/d3d12_backend.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <d3d11.h>
#include <rex/cvar.h>
REXCVAR_DEFINE_STRING(edf_native_scene_backend,"d3d12","Test","test");
using namespace edf::native;
using namespace rex::ui;
void Require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
template<class F> void Reject(F operation) {
  bool rejected=false; try {operation();} catch(const std::runtime_error&) {rejected=true;}
  Require(rejected,"invalid SDK UI operation accepted");
}
int main() {
  try {
    Reject([]{ D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
                                D3D11_SDK_VERSION,nullptr,nullptr,nullptr); });
    NativeD3D12Options options{}; options.prefer_warp=true; options.debug_layer=true;
    auto backend=std::shared_ptr<NativeRenderBackend>(CreateNativeD3D12Backend(options));
    NativeBackendImmediateDrawer drawer(backend);
    NativeBackendTextureDesc desc{}; desc.width=desc.height=4; desc.format=28; desc.render_target=true;
    auto target=backend->CreateRenderTarget(desc);
    AppUIDrawContext context(4,4);
    Reject([&]{drawer.Begin(context,2,2);});
    drawer.SetTarget(target.get());
    backend->BeginFrame(); backend->Recorder().ClearColor(*target,{0,0,1,1});
    drawer.Begin(context,2,2);
    Reject([&]{drawer.SetTarget(nullptr);});
    Reject([&]{drawer.Begin(context,2,2);});
    std::array<ImmediateVertex,4> vertices{{{0,0,0,0,0xffffffff},{2,0,1,0,0xffffffff},
      {2,2,1,1,0xffffffff},{0,2,0,1,0xffffffff}}};
    const uint16_t indices[]{0,1,2,0,2,3};
    ImmediateDrawBatch batch{}; batch.vertices=vertices.data(); batch.vertex_count=4;
    batch.indices=indices; batch.index_count=6;
    auto invalid=batch; invalid.vertex_count=-1;
    Reject([&]{drawer.BeginDrawBatch(invalid);});
    const uint8_t green[]{0,255,0,255};
    // ImGui can create a font/texture during an open host frame. Its upload
    // must be usable by that frame rather than deferred to the next one.
    auto texture=drawer.CreateTexture(1,1,ImmediateTextureFilter::kNearest,false,green);
    drawer.BeginDrawBatch(batch);
    for(auto& vertex:vertices) vertex.color=0xff0000ff;
    ImmediateDraw draw{}; draw.count=6; draw.texture=texture.get(); draw.scissor=true;
    draw.scissor_left=draw.scissor_top=.5f; draw.scissor_right=draw.scissor_bottom=1.5f;
    drawer.Draw(draw); drawer.EndDrawBatch(); drawer.End(); drawer.SetTarget(nullptr);
    backend->Submit();
    const auto pixels=backend->ReadRenderTarget(*target);
    for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
      const auto at=(y*4+x)*4; const bool inside=x>=1 && x<3 && y>=1 && y<3;
      Require(pixels[at]==0 && pixels[at+1]==(inside?255:0) &&
        pixels[at+2]==(inside?0:255) && pixels[at+3]==255,"SDK UI texture/scissor/batch snapshot mismatch");
    }
    drawer.End(); // Exception cleanup is idempotent.
    Require(backend->DrainValidationMessages().empty(),"SDK UI GPU validation failed");
    std::cout<<"D3D12 SDK UI tests passed\n";
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}

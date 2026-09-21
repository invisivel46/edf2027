#include "native_graphics/native_backend_ui.h"
#include "native_graphics/d3d11_backend.h"
#include "native_graphics/d3d12_backend.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace edf::native;
void Require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
std::vector<uint8_t> Render(NativeRenderBackend& backend) {
  NativeBackendUiRenderer ui(backend);
  NativeBackendTextureDesc desc{}; desc.width=4; desc.height=4; desc.format=28; desc.render_target=true;
  auto first=backend.CreateRenderTarget(desc),second=backend.CreateRenderTarget(desc);
  NativeBackendUiVertex vertices[]{{0,0,0,0,0x800000ff},{2,0,1,0,0x800000ff},
    {2,2,1,1,0x800000ff},{0,2,0,1,0x800000ff}};
  const uint16_t indices[]{0,1,2,0,2,3};
  const uint8_t pixels[]{255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
  auto texture=ui.CreateTexture(2,2,pixels,false,false);
  backend.BeginFrame(); auto& recorder=backend.Recorder();
  recorder.ClearColor(*first,{0,0,1,1}); recorder.ClearColor(*second,{0,0,0,1});
  ui.Upload(recorder,vertices,indices);
  NativeBackendUiDraw draw{}; draw.count=6; draw.scissor={1,1,3,3};
  ui.Draw(recorder,*first,2,2,draw);
  // Reuse the same upload allocation before submission. The first draw must
  // retain its red translucent vertices, not this later opaque batch.
  for(auto& v:vertices) v.color=0xffffffff;
  ui.Upload(recorder,vertices,indices); draw.texture=&texture; draw.scissor={0,0,4,4};
  ui.Draw(recorder,*second,2,2,draw);
  draw.base_vertex=-1;
  bool rejected=false;
  try { ui.Draw(recorder,*second,2,2,draw); } catch(const std::runtime_error&) { rejected=true; }
  Require(rejected,"UI accepted invalid indexed vertex");
  backend.Submit();
  auto a=backend.ReadRenderTarget(*first),b=backend.ReadRenderTarget(*second);
  for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
    const bool inside=x>=1 && x<3 && y>=1 && y<3;
    const auto at=(y*4+x)*4;
    Require(std::abs(int(a[at])-(inside?128:0))<=1 && a[at+1]==0 &&
      std::abs(int(a[at+2])-(inside?127:255))<=1 && a[at+3]==255,"UI blend/scissor/batch lifetime failed");
    const auto texel=((y/2)*2+x/2)*4;
    for(unsigned c=0;c<4;++c) Require(b[at+c]==pixels[texel+c],"UI texture/projection mismatch");
  }
  Require(backend.DrainValidationMessages().empty(),"UI GPU validation failed");
  a.insert(a.end(),b.begin(),b.end()); return a;
}
int main() {
  try {
    auto d11=CreateNativeD3D11Backend({true,true});
    NativeD3D12Options options{}; options.prefer_warp=true; options.debug_layer=true;
    auto d12=CreateNativeD3D12Backend(options);
    Require(Render(*d11)==Render(*d12),"D3D11/D3D12 UI mismatch");
    std::cout<<"Backend UI tests passed\n";
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}

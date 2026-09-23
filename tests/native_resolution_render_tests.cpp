// WARP render tests at sizes other than 1280x720 (native_display_layout.h):
// the 2D canvas layout and its scissor rasterized through the Utility and XUI
// constant forms, and the presenter's fit and filters, at 1366x768, 2560x1080,
// 1024x768 and 1920x1080 on the D3D12 backend.
#include "native_graphics/native_backend_compositor.h"
#include "native_graphics/native_canvas_constants.h"
#include "native_graphics/native_display_layout.h"
#include "native_graphics/d3d12_backend.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace edf::native;
namespace {
void Require(bool value,const std::string& message) { if(!value) throw std::runtime_error(message); }

// The Utility.dxsl 2D contract (clip = p*_g_DX2DScale + _g_DX2DOffset) and the
// XUI one (clip = ProjectionRows . (x, y, 1) in .xyw), for one quad whose
// corners come from the constants, drawn as two triangles.
const char* kSource=R"(
cbuffer Canvas : register(b0) {
  float4 corners;        // x0, y0, x1, y1 in canvas units
  float2 _g_DX2DScale; float2 _g_DX2DOffset;
  float4 ProjectionRows[4];
  float4 color;
  uint4 mode;            // x: 0 Utility, 1 XUI
};
struct V { float4 position:SV_POSITION; };
V VS(uint id:SV_VertexID) {
  const uint corner[6]={0,1,2,2,1,3};
  uint c=corner[id];
  float2 p=float2((c&1)?corners.z:corners.x,(c&2)?corners.w:corners.y);
  V v;
  if(mode.x==0) v.position=float4(p*_g_DX2DScale+_g_DX2DOffset,0,1);
  else {
    float3 t=float3(p,1);
    v.position=float4(dot(t,ProjectionRows[0].xyw),dot(t,ProjectionRows[1].xyw),
                      dot(t,ProjectionRows[2].xyw),dot(t,ProjectionRows[3].xyw));
  }
  return v;
}
float4 PS(V v):SV_TARGET { return color; }
)";
Microsoft::WRL::ComPtr<ID3DBlob> Compile(const char* entry,const char* profile) {
  Microsoft::WRL::ComPtr<ID3DBlob> code,errors;
  if(FAILED(D3DCompile(kSource,std::strlen(kSource),"resolution_tests",nullptr,nullptr,entry,profile,0,0,&code,&errors)))
    throw std::runtime_error(errors?static_cast<const char*>(errors->GetBufferPointer()):"compile failed");
  return code;
}
struct Constants {
  float corners[4];
  float scale[2],offset[2];
  float projection[16];
  float color[4];
  uint32_t mode[4];
};
struct Canvas {
  NativeRenderBackend& backend;
  NativeBackendPipeline* pipeline;
  Microsoft::WRL::ComPtr<ID3DBlob> vs,ps;
  explicit Canvas(NativeRenderBackend& b):backend(b),pipeline(nullptr),vs(Compile("VS","vs_5_0")),ps(Compile("PS","ps_5_0")) {
    NativeBackendPipelineDesc desc{};
    desc.vertex={static_cast<const uint8_t*>(vs->GetBufferPointer()),vs->GetBufferSize()};
    desc.pixel={static_cast<const uint8_t*>(ps->GetBufferPointer()),ps->GetBufferSize()};
    desc.vertex_id=0x5245534f4c5653; desc.pixel_id=0x5245534f4c5053;
    desc.state={0x10001,0,0,0,15,0};
    desc.topology=NativeBackendTopology::TriangleList;
    desc.render_targets=1; desc.rtv_format[0]=28;
    pipeline=&backend.CreatePipeline(desc);
  }
};
struct Image {
  uint32_t width=0,height=0;
  std::vector<uint8_t> rgba;
  bool Lit(uint32_t x,uint32_t y) const { return rgba[(size_t(y)*width+x)*4]>127; }
};
// Draws `draws` (constants, optional scissor) on a black width x height target.
Image Render(Canvas& canvas,uint32_t width,uint32_t height,
             const std::vector<std::pair<Constants,std::optional<std::array<int32_t,4>>>>& draws) {
  NativeBackendTextureDesc desc{}; desc.width=width; desc.height=height; desc.format=28; desc.render_target=true;
  auto target=canvas.backend.CreateRenderTarget(desc);
  canvas.backend.BeginFrame();
  auto& recorder=canvas.backend.Recorder();
  NativeBackendRenderTarget* targets[]={target.get()};
  recorder.SetRenderTargets(targets,nullptr);
  recorder.ClearColor(*target,{0,0,0,1});
  recorder.SetViewport({0,0,float(width),float(height),0,1});
  recorder.SetPipeline(*canvas.pipeline);
  for(const auto& [constants,scissor]:draws) {
    if(scissor) recorder.SetScissor({(*scissor)[0],(*scissor)[1],(*scissor)[2],(*scissor)[3]},true);
    else recorder.SetScissor({},false);
    recorder.SetConstants(NativeBackendStage::Vertex,0,{reinterpret_cast<const uint8_t*>(&constants),sizeof(constants)});
    recorder.SetConstants(NativeBackendStage::Pixel,0,{reinterpret_cast<const uint8_t*>(&constants),sizeof(constants)});
    recorder.Draw(6,0);
  }
  canvas.backend.Submit();
  Image image{width,height,canvas.backend.ReadRenderTarget(*target)};
  Require(image.rgba.size()==size_t(width)*height*4,"render target size");
  Require(canvas.backend.DrainValidationMessages().empty(),"GPU validation messages");
  return image;
}
// The lit region must be exactly [x0,x1) x [y0,y1) (pixel centres inside).
void RequireRect(const Image& image,const std::array<int32_t,4>& rect,const std::string& what) {
  size_t wrong=0;
  for(uint32_t y=0;y<image.height;++y)
    for(uint32_t x=0;x<image.width;++x) {
      const bool inside=int32_t(x)>=rect[0] && int32_t(x)<rect[2] && int32_t(y)>=rect[1] && int32_t(y)<rect[3];
      if(image.Lit(x,y)!=inside) ++wrong;
    }
  Require(wrong==0,what+": "+std::to_string(wrong)+" pixels outside the expected "+std::to_string(rect[0])+","+
          std::to_string(rect[1])+"-"+std::to_string(rect[2])+","+std::to_string(rect[3]));
}
std::array<int32_t,4> PixelRect(const NativeCanvasRect& r) {
  return {int32_t(r.x),int32_t(r.y),int32_t(r.x+r.width),int32_t(r.y+r.height)};
}
// The Utility constants the engine uploads for a width x height 2D object
// (2/W, -2/H, centre origin), through MapNativeCanvasXY as the bridge does.
Constants Utility(uint32_t width,uint32_t height,const NativeClipAffine& affine,std::array<float,4> corners) {
  std::array<uint8_t,16> scale{},offset{};
  StoreNativeGuestFloat(scale,0,2.0f/float(width)); StoreNativeGuestFloat(scale,4,-2.0f/float(height));
  const float kx=float(width)/1280.0f,ky=float(height)/720.0f;
  const auto s=MapNativeCanvasXY(scale,kx,ky,affine,false),o=MapNativeCanvasXY(offset,kx,ky,affine,true);
  Constants c{};
  std::memcpy(c.corners,corners.data(),16);
  c.scale[0]=NativeGuestFloatAt(s,0); c.scale[1]=NativeGuestFloatAt(s,4);
  c.offset[0]=NativeGuestFloatAt(o,0); c.offset[1]=NativeGuestFloatAt(o,4);
  c.color[0]=c.color[1]=c.color[2]=c.color[3]=1;
  return c;
}
// XUI: the viewport ortho 82415330 builds (top-left pixels), through
// NativeXuiCanvasProjection as the XUI vertex bindings do.
Constants Xui(uint32_t width,uint32_t height,const NativeClipAffine& affine,std::array<float,4> corners) {
  std::array<uint8_t,64> rows{};
  const float ortho[16]{2.0f/float(width),0,0,-1, 0,-2.0f/float(height),0,1, 0,0,1,0, 0,0,0,1};
  for(size_t i=0;i<16;++i) StoreNativeGuestFloat(rows,i*4,ortho[i]);
  const auto m=NativeXuiCanvasProjection(rows,float(width)/1280.0f,float(height)/720.0f,affine);
  Constants c{};
  std::memcpy(c.corners,corners.data(),16);
  std::memcpy(c.projection,m.data(),64);
  c.color[0]=c.color[1]=c.color[2]=c.color[3]=1;
  c.mode[0]=1;
  return c;
}

void CanvasTests(NativeRenderBackend& backend) {
  Canvas canvas(backend);
  const std::pair<uint32_t,uint32_t> sizes[]{{1366,768},{2560,1080},{1024,768},{1920,1080},{1706,720}};
  for(const auto [width,height]:sizes) {
    const std::string at=std::to_string(width)+"x"+std::to_string(height);
    for(const auto area:{NativeHudSafeArea::Console,NativeHudSafeArea::Full}) {
      const auto rect=NativeHudCanvasRect(width,height,area);
      const auto affine=NativeHudClipAffine(width,height,area);
      const std::string mode=area==NativeHudSafeArea::Console?" 16:9":" full";
      // The whole canvas, a textured-style draw: exactly the layout rectangle.
      RequireRect(Render(canvas,width,height,{{Utility(width,height,affine,{-640,-360,640,360}),{}}}),
                  PixelRect(rect),"Utility canvas "+at+mode);
      RequireRect(Render(canvas,width,height,{{Xui(width,height,affine,{0,0,1280,720}),{}}}),
                  PixelRect(rect),"XUI canvas "+at+mode);
      // A flat full-canvas fade covers the whole target in either mode.
      const auto fade=NativeCanvasDrawAffine(affine,{-1,1,-1,1,true},true);
      RequireRect(Render(canvas,width,height,{{Utility(width,height,fade,{-640,-360,640,360}),{}}}),
                  {0,0,int32_t(width),int32_t(height)},"Utility fade "+at+mode);
      // A top-left element (the canvas' first quarter) stays inside the layout
      // rectangle's first quarter.
      const auto quarter=Render(canvas,width,height,{{Xui(width,height,affine,{0,0,640,360}),{}}});
      // Pixels whose centre is left of / above the edge (an edge through a
      // centre, 682.5 at 1366, excludes it: the top-left rule).
      const auto end=[](double edge) { return int32_t(std::ceil(edge-0.5)); };
      RequireRect(quarter,{int32_t(rect.x),int32_t(rect.y),end(rect.x+rect.width*0.5),end(rect.y+rect.height*0.5)},
                  "XUI quarter "+at+mode);
      // The legacy full-canvas scissor, mapped like the draw, clips a quad
      // that overflows the canvas to exactly the rectangle.
      const auto scissor=MapNativeCanvasPixelRect({0,0,int32_t(width),int32_t(height)},affine,width,height);
      RequireRect(Render(canvas,width,height,{{Utility(width,height,{},{-2000,-2000,2000,2000}),scissor}}),
                  PixelRect(rect),"Utility scissor "+at+mode);
    }
  }
  std::cout<<"Canvas layout WARP tests passed\n";
}

// ---- Presenter ---------------------------------------------------------------------------
std::vector<uint8_t> Composite(NativeRenderBackend& backend,uint32_t sw,uint32_t sh,const std::vector<uint8_t>& pixels,
                               uint32_t dw,uint32_t dh,bool smart,bool preserve=true) {
  NativeBackendCompositor compositor(backend);
  NativeBackendTextureDesc input{}; input.width=sw; input.height=sh; input.format=28;
  auto texture=backend.CreateTexture(input,pixels);
  NativeBackendTextureDesc output{}; output.width=dw; output.height=dh; output.format=28; output.render_target=true;
  auto target=backend.CreateRenderTarget(output);
  backend.BeginFrame();
  compositor.Draw(backend.Recorder(),*texture,*target,preserve,nullptr,smart);
  backend.Submit();
  auto result=backend.ReadRenderTarget(*target);
  Require(result.size()==size_t(dw)*dh*4,"composite size");
  Require(backend.DrainValidationMessages().empty(),"compositor GPU validation");
  return result;
}
std::vector<uint8_t> Pattern(uint32_t width,uint32_t height,auto value) {
  std::vector<uint8_t> pixels(size_t(width)*height*4);
  for(uint32_t y=0;y<height;++y) for(uint32_t x=0;x<width;++x) {
    const uint8_t v=value(x,y);
    auto* p=&pixels[(size_t(y)*width+x)*4];
    p[0]=p[1]=p[2]=v; p[3]=255;
  }
  return pixels;
}
void PresenterTests(NativeRenderBackend& backend) {
  // 1:1 with the automatic filter is the original image, byte for byte.
  {
    const auto source=Pattern(1280,720,[](uint32_t x,uint32_t y) { return uint8_t((x*7+y*13)&255); });
    const auto smart=Composite(backend,1280,720,source,1280,720,true);
    const auto legacy=Composite(backend,1280,720,source,1280,720,false);
    Require(smart==legacy && smart==source,"1:1 presentation changed the image");
  }
  // Whole factors replicate exactly: 2x of a 2-pixel image is two 2x2 blocks.
  {
    const auto source=Pattern(64,36,[](uint32_t x,uint32_t) { return uint8_t(x&1?255:0); });
    const auto out=Composite(backend,64,36,source,128,72,true);
    for(uint32_t x=0;x<128;++x)
      Require(out[(10*128+x)*4]==((x/2)&1?255:0),"2x nearest replication at column "+std::to_string(x));
    const auto blurred=Composite(backend,64,36,source,128,72,false);
    Require(blurred!=out,"bilinear 2x should blend the columns the nearest filter keeps");
  }
  // Supersampling (render above the window): a [255,0,0] pattern at 3:1
  // averages to 85 with the area filter; one bilinear tap reads the zeros.
  {
    const auto source=Pattern(384,216,[](uint32_t x,uint32_t) { return uint8_t(x%3==0?255:0); });
    const auto area=Composite(backend,384,216,source,128,72,true);
    for(uint32_t x=0;x<128;++x)
      Require(std::abs(int(area[(36*128+x)*4])-85)<=2,"3:1 area average at column "+std::to_string(x)+" is "+
              std::to_string(area[(36*128+x)*4]));
    const auto point=Composite(backend,384,216,source,128,72,false);
    Require(std::abs(int(point[(36*128+64)*4])-85)>20,"one bilinear tap should miss the 3:1 average");
  }
  // Letterbox on whole pixels at odd sizes: 16:9 in 1366x768 leaves column
  // 1365 black and fills 0..1364; 16:9 in 2560x1080 pillarboxes at 320.
  {
    const auto white=Pattern(1280,720,[](uint32_t,uint32_t) { return uint8_t(255); });
    const auto odd=Composite(backend,1280,720,white,1366,768,true);
    for(uint32_t y:{0u,383u,767u}) {
      Require(odd[(size_t(y)*1366+1364)*4]==255 && odd[(size_t(y)*1366+1365)*4]==0 && odd[(size_t(y)*1366)*4]==255,
              "1366x768 letterbox edge");
    }
    const auto wide=Composite(backend,1280,720,white,2560,1080,true);
    Require(wide[(540*2560+319)*4]==0 && wide[(540*2560+320)*4]==255 && wide[(540*2560+2239)*4]==255 &&
            wide[(540*2560+2240)*4]==0,"2560x1080 pillarbox edges");
    // 16:9 in 4:3 letterboxes: rows 0..95 black, 96..671 image.
    const auto tall=Composite(backend,1280,720,white,1024,768,true);
    Require(tall[(95*1024+512)*4]==0 && tall[(96*1024+512)*4]==255 && tall[(671*1024+512)*4]==255 &&
            tall[(672*1024+512)*4]==0,"1024x768 letterbox edges");
    // Stretch fills the window.
    const auto stretched=Composite(backend,1280,720,white,2560,1080,true,false);
    Require(stretched[(540*2560)*4]==255 && stretched[(540*2560+2559)*4]==255,"stretch fills the window");
  }
  std::cout<<"Presenter fit WARP tests passed\n";
}
}  // namespace

int main() {
  try {
    NativeD3D12Options options{}; options.prefer_warp=true; options.debug_layer=true;
    auto backend=CreateNativeD3D12Backend(options);
    CanvasTests(*backend);
    PresenterTests(*backend);
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
  return 0;
}

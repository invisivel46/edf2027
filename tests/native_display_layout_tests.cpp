// Render size, field of view, 2D canvas layout, movie framing, presentation fit
// and the post pyramid at sizes other than 1280x720 (native_display_layout.h,
// native_canvas_constants.h, native_post_finish_plan.h). Pure arithmetic.
#include "native_graphics/native_display_layout.h"
#include "native_graphics/native_canvas_constants.h"
#include "native_graphics/native_post_finish_plan.h"
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numbers>
#include <vector>

namespace {
int failures=0;
void Check(bool condition,const char* expression,int line) {
  if(!condition) { std::cerr<<"line "<<line<<": CHECK("<<expression<<") failed\n"; ++failures; }
}
#define CHECK(value) Check(static_cast<bool>(value),#value,__LINE__)
using namespace edf::native;

bool Size(const NativeRenderSize& size,int32_t width,int32_t height) {
  if(size.width==width && size.height==height) return true;
  std::cerr<<"  resolved "<<size.width<<"x"<<size.height<<", expected "<<width<<"x"<<height<<"\n";
  return false;
}
std::array<uint8_t,16> Guest4(float x,float y,float z=0,float w=0) {
  std::array<uint8_t,16> out{};
  const float v[4]{x,y,z,w};
  for(size_t i=0;i<4;++i) StoreNativeGuestFloat(out,i*4,v[i]);
  return out;
}
std::vector<uint8_t> GuestFloats(std::initializer_list<float> values) {
  std::vector<uint8_t> out(values.size()*4);
  size_t i=0;
  for(const float v:values) StoreNativeGuestFloat(out,4*i++,v);
  return out;
}
bool Near(double a,double b,double tolerance=1e-4) { return std::abs(a-b)<=tolerance; }

// ---- Render size ------------------------------------------------------------------
void TestRenderSize() {
  const auto native=NativeAspectMode::Native;
  // The default request on a 16:9 window is the engine's own size: no override.
  CHECK(Size(ResolveNativeRenderSize(0,0,1280,720,native),1280,720));
  CHECK(ResolveNativeRenderSize(0,0,1280,720,native).original);
  CHECK(ResolveNativeRenderSize(0,0,1920,1080,native).original);
  CHECK(ResolveNativeRenderSize(0,0,3840,2160,native).original);
  // 720 lines in the window's shape; derived widths are even.
  CHECK(Size(ResolveNativeRenderSize(0,0,2560,1080,native),1706,720));
  CHECK(Size(ResolveNativeRenderSize(0,0,3440,1440,native),1720,720));
  CHECK(Size(ResolveNativeRenderSize(0,0,5120,1440,native),2560,720));
  CHECK(Size(ResolveNativeRenderSize(0,0,1920,1200,native),1152,720));
  CHECK(Size(ResolveNativeRenderSize(0,0,1024,768,native),960,720));
  // 1366x768 is 16:9 to within 0.05%; its 720 lines round to the engine's size.
  CHECK(ResolveNativeRenderSize(0,0,1366,768,native).original);
  // A line count equal to the window's height takes the window's width exactly.
  CHECK(Size(ResolveNativeRenderSize(0,1080,2560,1080,native),2560,1080));
  CHECK(Size(ResolveNativeRenderSize(0,768,1366,768,native),1366,768));
  CHECK(Size(ResolveNativeRenderSize(0,1440,1920,1080,native),2560,1440));
  CHECK(Size(ResolveNativeRenderSize(0,2160,2560,1440,native),3840,2160));
  // Match window.
  CHECK(Size(ResolveNativeRenderSize(0,-1,3840,2160,native),3840,2160));
  CHECK(Size(ResolveNativeRenderSize(0,-1,5120,1440,native),5120,1440));
  CHECK(Size(ResolveNativeRenderSize(0,-1,720,1280,native),720,1280));
  CHECK(!ResolveNativeRenderSize(0,-1,5120,1440,native).clamped);
  // 8K is beyond the guest memory budget (4096*4096 pixels): scaled, shape kept.
  const auto eight=ResolveNativeRenderSize(0,-1,7680,4320,native);
  CHECK(eight.clamped && Size(eight,5460,3072));
  CHECK(int64_t(eight.width)*eight.height<=int64_t(4096)*4096);
  CHECK(Near(double(eight.width)/eight.height,16.0/9.0,2e-3));
  // A portrait window's 720 lines are 405 wide: scaled up to the 640 minimum.
  const auto portrait=ResolveNativeRenderSize(0,0,1080,1920,native);
  CHECK(portrait.clamped && Size(portrait,640,1135));
  // The 16:9 modes render 16:9 whatever the window.
  for(const auto mode:{NativeAspectMode::Letterbox,NativeAspectMode::Stretch}) {
    CHECK(ResolveNativeRenderSize(0,0,2560,1080,mode).original);
    CHECK(Size(ResolveNativeRenderSize(0,1080,2560,1080,mode),1920,1080));
    CHECK(Size(ResolveNativeRenderSize(1920,1200,2560,1080,mode),2134,1200));
    CHECK(Size(ResolveNativeRenderSize(0,-1,1920,1200,mode),2134,1200));
  }
  // Hor+ only renders the same size as native; only the field of view differs.
  CHECK(Size(ResolveNativeRenderSize(0,0,1024,768,NativeAspectMode::Ultrawide),960,720));
  // A fixed size is kept, whatever the window.
  CHECK(Size(ResolveNativeRenderSize(2560,1440,2560,1080,native),2560,1440));
  CHECK(Size(ResolveNativeRenderSize(1366,768,1920,1080,native),1366,768));
  // No window size known: the console shape.
  CHECK(ResolveNativeRenderSize(0,0,0,0,native).original);
  CHECK(Size(ResolveNativeRenderSize(0,1080,0,0,native),1920,1080));
  // The request validator used by the F1 menu and the 82139A40 hook.
  CHECK(ValidNativeRenderRequest(0,0) && ValidNativeRenderRequest(0,-1) && ValidNativeRenderRequest(0,720));
  CHECK(ValidNativeRenderRequest(640,480) && ValidNativeRenderRequest(5120,1440) && ValidNativeRenderRequest(5120,2880));
  CHECK(ValidNativeRenderRequest(4095,4095) && ValidNativeRenderRequest(8192,2048));
  CHECK(!ValidNativeRenderRequest(7680,4320) && !ValidNativeRenderRequest(8193,480));
  CHECK(!ValidNativeRenderRequest(639,480) && !ValidNativeRenderRequest(640,479) && !ValidNativeRenderRequest(1280,0));
  CHECK(!ValidNativeRenderRequest(0,479) && !ValidNativeRenderRequest(0,-2));
  CHECK(ParseNativeAspectMode("ultrawide")==NativeAspectMode::Ultrawide);
  CHECK(ParseNativeAspectMode("bad")==NativeAspectMode::Native);
}

// ---- Field of view ------------------------------------------------------------------
void TestFieldOfView() {
  const float vertical=std::bit_cast<float>(0x3f32b8c2u);  // 0.698 rad, the fov a live run logged
  const auto native=NativeAspectMode::Native;
  const double console_h=NativeHorizontalFov(vertical,16.0/9.0);
  // Wider than 16:9: the engine is Hor+ already (aspect from the viewport).
  CHECK(NativeVerticalFovScale(2560,1080,native)==1.0f);
  CHECK(NativeVerticalFovScale(5120,1440,native)==1.0f);
  CHECK(NativeHorizontalFov(vertical,2560.0/1080.0)>console_h);
  CHECK(NativeHorizontalFov(vertical,5120.0/1440.0)>NativeHorizontalFov(vertical,2560.0/1080.0));
  // 16:9: untouched, bit for bit.
  CHECK(NativeVerticalFovScale(1920,1080,native)==1.0f);
  CHECK(std::bit_cast<uint32_t>(NativeAdjustedVerticalFov(vertical,1.0f))==0x3f32b8c2u);
  // Narrower: Vert+ keeps the 16:9 horizontal angle and opens the vertical one.
  for(const auto [w,h]:{std::pair{1920.0,1200.0},{1024.0,768.0},{1680.0,1050.0}}) {
    const float scale=NativeVerticalFovScale(w,h,native);
    CHECK(scale>1.0f);
    const float adjusted=NativeAdjustedVerticalFov(vertical,scale);
    CHECK(adjusted>vertical);
    CHECK(Near(NativeHorizontalFov(adjusted,w/h),console_h,1e-5));
  }
  CHECK(Near(NativeVerticalFovScale(1920,1200,native),(16.0/9.0)/1.6,1e-6));
  // Below 4:3 (portrait) the extension stops at the 4:3 amount.
  CHECK(Near(NativeVerticalFovScale(720,1280,native),(16.0/9.0)/(4.0/3.0),1e-6));
  CHECK(NativeAdjustedVerticalFov(3.0f,4.0f)<=3.0f);
  // Hor+ only and the 16:9 modes never change the vertical angle.
  for(const auto mode:{NativeAspectMode::Ultrawide,NativeAspectMode::Letterbox,NativeAspectMode::Stretch})
    CHECK(NativeVerticalFovScale(1024,768,mode)==1.0f);
  // Bad input stays untouched.
  CHECK(NativeVerticalFovScale(0,768,native)==1.0f);
  CHECK(std::isnan(NativeAdjustedVerticalFov(NAN,1.2f)));
}

// ---- 2D canvas -----------------------------------------------------------------------
// Where a canvas point lands in target pixels after the Utility path's
// constants: the engine's 2D object gives _g_DX2DScale = (2/W, -2/H) and a
// centre origin; the legacy correction multiplies by W/1280, H/720 and the
// layout maps the result.
std::array<double,2> UtilityPixel(float px,float py,uint32_t width,uint32_t height,const NativeClipAffine& affine) {
  const auto scale=Guest4(2.0f/float(width),-2.0f/float(height),0,0);
  const auto offset=Guest4(0,0,0,0);
  const float kx=float(width)/1280.0f,ky=float(height)/720.0f;
  const auto s=MapNativeCanvasXY(scale,kx,ky,affine,false);
  const auto o=MapNativeCanvasXY(offset,kx,ky,affine,true);
  const double cx=px*NativeGuestFloatAt(s,0)+NativeGuestFloatAt(o,0);
  const double cy=py*NativeGuestFloatAt(s,4)+NativeGuestFloatAt(o,4);
  return {(cx+1)*0.5*width,(1-cy)*0.5*height};
}
// XUI: a top-left canvas point through the engine's viewport ortho (2/W,
// -2/H, -1, +1), the legacy top-left correction and the layout.
std::array<double,2> XuiPixel(float px,float py,uint32_t width,uint32_t height,const NativeClipAffine& affine) {
  std::array<uint8_t,64> projection{};
  const float rows[16]{2.0f/float(width),0,0,-1, 0,-2.0f/float(height),0,1, 0,0,1,0, 0,0,0,1};
  for(size_t i=0;i<16;++i) StoreNativeGuestFloat(projection,i*4,rows[i]);
  const auto m=NativeXuiCanvasProjection(projection,float(width)/1280.0f,float(height)/720.0f,affine);
  const double x=m[0]*px+m[1]*py+m[3],y=m[4]*px+m[5]*py+m[7],w=m[12]*px+m[13]*py+m[15];
  return {(x/w+1)*0.5*width,(1-y/w)*0.5*height};
}
void TestCanvasLayout() {
  struct Case { uint32_t width,height; NativeCanvasRect console; };
  const Case cases[]{
    {1280,720,{0,0,1280,720}},{1920,1080,{0,0,1920,1080}},{3840,2160,{0,0,3840,2160}},
    {2560,1080,{320,0,1920,1080}},{3440,1440,{440,0,2560,1440}},{5120,1440,{1280,0,2560,1440}},
    {1920,1200,{0,60,1920,1080}},{1024,768,{0,96,1024,576}},{1366,768,{0,0,1365,768}},
    {1706,720,{213,0,1280,720}},{960,720,{0,90,960,540}},{640,1135,{0,387,640,360}}};
  for(const auto& c:cases) {
    const auto rect=NativeHudCanvasRect(c.width,c.height,NativeHudSafeArea::Console);
    CHECK(rect.x==c.console.x && rect.y==c.console.y && rect.width==c.console.width && rect.height==c.console.height);
    if(!(rect.x==c.console.x && rect.y==c.console.y && rect.width==c.console.width && rect.height==c.console.height))
      std::cerr<<"  "<<c.width<<"x"<<c.height<<": "<<rect.x<<","<<rect.y<<" "<<rect.width<<"x"<<rect.height<<"\n";
    // The canvas keeps its shape (within a pixel of rounding).
    CHECK(Near(rect.width/rect.height,16.0/9.0,2.5/rect.height));
    const auto full=NativeHudCanvasRect(c.width,c.height,NativeHudSafeArea::Full);
    CHECK(full.x==0 && full.y==0 && full.width==float(c.width) && full.height==float(c.height));
    CHECK(NativeHudClipAffine(c.width,c.height,NativeHudSafeArea::Full).identity());
    const auto affine=NativeHudClipAffine(c.width,c.height,NativeHudSafeArea::Console);
    CHECK(affine.identity()==IsConsoleAspect(c.width,c.height));
    // Utility (centre origin) and XUI (top-left origin) canvas points land in
    // the rectangle: corners on its corners, the centre on its centre.
    for(const auto [u,v]:{std::pair{0.0,0.0},{1.0,0.0},{0.0,1.0},{1.0,1.0},{0.5,0.5},{0.25,0.75}}) {
      const double ex=rect.x+u*rect.width,ey=rect.y+v*rect.height;
      const auto utility=UtilityPixel(float(-640+u*1280),float(-360+v*720),c.width,c.height,affine);
      CHECK(Near(utility[0],ex,2e-3*c.width) && Near(utility[1],ey,2e-3*c.height));
      const auto xui=XuiPixel(float(u*1280),float(v*720),c.width,c.height,affine);
      CHECK(Near(xui[0],ex,2e-3*c.width) && Near(xui[1],ey,2e-3*c.height));
      // Full frame: the legacy stretch.
      const auto stretched=UtilityPixel(float(-640+u*1280),float(-360+v*720),c.width,c.height,{});
      CHECK(Near(stretched[0],u*c.width,2e-3*c.width) && Near(stretched[1],v*c.height,2e-3*c.height));
    }
  }
  // Identity layout returns exactly the legacy bytes (16:9 byte-identical).
  const auto raw=Guest4(0.0015625f,-0.0027777778f,0.5f,1.0f);
  CHECK(MapNativeCanvasXY(raw,1.5f,1.5f,{},false)==ScaleNativeCanvasXY(raw,1.5f,1.5f));
  CHECK(MapNativeCanvasXY(raw,1,1,{},true)==raw);
  // Scale lanes take a*v, offsets a*v+b; Z/W untouched.
  const NativeClipAffine a{0.75f,0.25f,1,0};
  const auto scaled=MapNativeCanvasXY(raw,1,1,a,false),offset=MapNativeCanvasXY(raw,1,1,a,true);
  CHECK(NativeGuestFloatAt(scaled,0)==0.0015625f*0.75f && NativeGuestFloatAt(offset,0)==0.0015625f*0.75f+0.25f);
  CHECK(std::memcmp(scaled.data()+8,raw.data()+8,8)==0 && std::memcmp(offset.data()+8,raw.data()+8,8)==0);
  CHECK(ClassifyNativeCanvasParameter(0xc885203e230fe745ull,"VS_2D","_g_DX2DOffset",0)==NativeCanvasParameter::Offset);
  CHECK(ClassifyNativeCanvasParameter(0xc885203e230fe745ull,"VS_2DTex","_g_DX2DScale",1)==NativeCanvasParameter::Scale);
  CHECK(ClassifyNativeCanvasParameter(0xc885203e230fe745ull,"VS_2D","_g_DX2DScale",2)==NativeCanvasParameter::None);
  // XUI byte identity with an identity layout.
  {
    std::array<uint8_t,64> projection{};
    const float rows[16]{2.0f/1920,0,0,-1, 0,-2.0f/1080,0,1, 0,0,1,0, 0,0,0,1};
    for(size_t i=0;i<16;++i) StoreNativeGuestFloat(projection,i*4,rows[i]);
    CHECK(NativeXuiCanvasProjection(projection,1.5f,1.5f,{})==NativeXuiCanvasProjection(projection,1.5f,1.5f));
  }
}
void TestCanvasDrawAndScissor() {
  const auto layout=NativeHudClipAffine(2560,1080,NativeHudSafeArea::Console);
  CHECK(Near(layout.ax,0.75) && Near(layout.bx,0) && layout.ay==1 && layout.by==0);
  const NativeClipBounds full{-1,1,-1,1,true},band{-1,1,0.8f,1,true},element{-0.9f,-0.6f,-0.9f,-0.5f,true};
  // A flat full-screen fade still covers the frame; a picture is pillarboxed.
  CHECK(NativeCanvasDrawAffine(layout,full,true).identity());
  CHECK(NativeCanvasDrawAffine(layout,full,false)==layout);
  // A flat band keeps reaching the sides; an element keeps its shape.
  const auto banded=NativeCanvasDrawAffine(layout,band,true);
  CHECK(banded.ax==1 && banded.bx==0 && banded.ay==layout.ay);
  CHECK(NativeCanvasDrawAffine(layout,element,true)==layout);
  CHECK(NativeCanvasDrawAffine(layout,{},true)==layout);
  CHECK(NativeCanvasDrawAffine({},full,true).identity());
  // Tall: 4:3 letterboxes vertically.
  const auto tall=NativeHudClipAffine(1024,768,NativeHudSafeArea::Console);
  CHECK(tall.ax==1 && Near(tall.ay,0.75) && tall.by==0);
  const auto tall_fade=NativeCanvasDrawAffine(tall,full,true);
  CHECK(tall_fade.identity());
  // Scissor rectangles follow the geometry: the full canvas clip becomes the
  // 16:9 rectangle, empty stays empty, identity is untouched.
  const auto whole=MapNativeCanvasPixelRect({0,0,2560,1080},layout,2560,1080);
  CHECK(whole==(std::array<int32_t,4>{320,0,2240,1080}));
  CHECK(MapNativeCanvasPixelRect({1280,540,1280,600},layout,2560,1080)[2]==1280);
  CHECK(MapNativeCanvasPixelRect({10,20,30,40},{},2560,1080)==(std::array<int32_t,4>{10,20,30,40}));
  // The observed 1080p font clip (696,732)-(801,750), as it would sit on a
  // 2560x1080 target whose legacy canvas is 2560 wide: mapped into the
  // rectangle, rounded outward.
  const auto font=MapNativeCanvasPixelRect({928,732,1068,750},layout,2560,1080);
  CHECK(font[0]==1016 && font[2]==1121 && font[1]==732 && font[3]==750);
  const auto tall_clip=MapNativeCanvasPixelRect({0,0,1024,768},tall,1024,768);
  CHECK(tall_clip==(std::array<int32_t,4>{0,96,1024,672}));
  // Utility vertex bounds: big-endian float2 positions at each vertex start.
  const auto vertices=GuestFloats({-640,360,0, 640,360,0, 640,-360,0, -640,-360,0});
  const auto scale=Guest4(2.0f/1280,2.0f/720),offset=Guest4(0,0);
  const auto bounds=NativeCanvasVertexBounds(vertices,12,scale,offset);
  CHECK(bounds.valid && Near(bounds.min_x,-1) && Near(bounds.max_x,1) && Near(bounds.min_y,-1) && Near(bounds.max_y,1));
  CHECK(!NativeCanvasVertexBounds({},12,scale,offset).valid);
}

// ---- Movies ----------------------------------------------------------------------------
std::vector<uint8_t> MovieRegisters(uint32_t width,uint32_t height,bool canvas) {
  // TransformRows identity (x,y,1 in .xyw), ProjectionRows the viewport ortho
  // of the target (full target) or of 1280x720 (canvas pixels).
  const float w=canvas?float(width):2.0f,h=canvas?float(height):2.0f;
  const float ox=canvas?-1.0f:0.0f,oy=canvas?1.0f:0.0f;
  const float sy=canvas?-2.0f/h:1.0f;
  return GuestFloats({1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1,
                      canvas?2.0f/w:1.0f,0,0,ox, 0,sy,0,oy, 0,0,1,0, 0,0,0,1,
                      0,0,0,0, 0,0,0,0});
}
std::vector<uint8_t> MovieQuad(float x0,float y0,float x1,float y1) {
  return GuestFloats({x0,y0,0,0, x1,y0,1,0, x0,y1,0,1, x1,y1,1,1});
}
void TestMovieFraming() {
  // A full-target quad on 21:9 is pillarboxed into 1920x1080 at x=320.
  {
    const auto registers=MovieRegisters(2560,1080,false);
    const auto quad=MovieQuad(-1,1,1,-1);
    const auto layout=MapNativeMovieRegisters(registers,quad,2560,1080);
    CHECK(layout.framing==NativeMovieFraming::FullTarget);
    const auto bounds=NativeMovieClipBounds(layout.registers,quad);
    CHECK(Near(bounds.min_x,-0.75) && Near(bounds.max_x,0.75) && Near(bounds.min_y,-1) && Near(bounds.max_y,1));
  }
  // The same on 16:9 is untouched, byte for byte.
  {
    const auto registers=MovieRegisters(1920,1080,false);
    const auto layout=MapNativeMovieRegisters(registers,MovieQuad(-1,1,1,-1),1920,1080);
    CHECK(layout.framing==NativeMovieFraming::Unchanged);
    CHECK(std::memcmp(layout.registers.data(),registers.data(),160)==0);
  }
  // A canvas-pixel quad (0..1280, 0..720) on 4:3 is letterboxed.
  {
    const auto registers=MovieRegisters(1024,768,true);
    const auto quad=MovieQuad(0,0,1280,720);
    const auto layout=MapNativeMovieRegisters(registers,quad,1024,768);
    CHECK(layout.framing==NativeMovieFraming::Canvas);
    const auto bounds=NativeMovieClipBounds(layout.registers,quad);
    CHECK(Near(bounds.min_x,-1) && Near(bounds.max_x,1) && Near(bounds.min_y,-0.75) && Near(bounds.max_y,0.75));
  }
  // A quad that is neither is left alone.
  {
    const auto registers=MovieRegisters(2560,1080,false);
    const auto layout=MapNativeMovieRegisters(registers,MovieQuad(-0.5f,0.5f,0.5f,-0.5f),2560,1080);
    CHECK(layout.framing==NativeMovieFraming::Unchanged);
  }
}

// ---- Presentation ----------------------------------------------------------------------
void TestPresentFit() {
  // 1:1 is bilinear on whole pixels: the unscaled frame is unchanged.
  auto fit=ComputeNativePresentFit(1280,720,1280,720,true,true);
  CHECK(fit.x==0 && fit.y==0 && fit.width==1280 && fit.height==720 && fit.filter==NativePresentFilter::Bilinear);
  // Whole factors replicate exactly.
  fit=ComputeNativePresentFit(1280,720,2560,1440,true,true);
  CHECK(fit.width==2560 && fit.height==1440 && fit.filter==NativePresentFilter::Nearest);
  fit=ComputeNativePresentFit(1280,720,3840,2160,true,true);
  CHECK(fit.filter==NativePresentFilter::Nearest);
  // Supersampling averages the footprint.
  fit=ComputeNativePresentFit(3840,2160,1920,1080,true,true);
  CHECK(fit.filter==NativePresentFilter::Area && fit.taps==2 && fit.width==1920);
  fit=ComputeNativePresentFit(5120,2880,1280,720,true,true);
  CHECK(fit.filter==NativePresentFilter::Area && fit.taps==4);
  // Letterbox on whole pixels: 16:9 into 21:9 and into 4:3, and the 0.3-pixel
  // bars of 1280x720 in 1366x768 collapse onto a pixel edge.
  fit=ComputeNativePresentFit(1920,1080,2560,1080,true,true);
  CHECK(fit.x==320 && fit.y==0 && fit.width==1920 && fit.height==1080 && fit.filter==NativePresentFilter::Bilinear);
  fit=ComputeNativePresentFit(1280,720,1024,768,true,true);
  CHECK(fit.x==0 && fit.y==96 && fit.width==1024 && fit.height==576 && fit.filter==NativePresentFilter::Area);
  fit=ComputeNativePresentFit(1280,720,1366,768,true,true);
  CHECK(fit.x==0 && fit.width==1365 && fit.height==768 && fit.filter==NativePresentFilter::Bilinear);
  // Stretch fills the window.
  fit=ComputeNativePresentFit(1920,1080,2560,1080,false,true);
  CHECK(fit.x==0 && fit.width==2560 && fit.height==1080);
  // The original placement: fractional, bilinear.
  fit=ComputeNativePresentFit(1280,720,1366,768,true,false);
  CHECK(fit.filter==NativePresentFilter::Bilinear && Near(fit.width,1280*768.0/720,1e-3) && fit.x>0 && fit.x<1);
  CHECK(ComputeNativePresentFit(0,720,1280,720,true,true).width==0);
}

// ---- Post pyramid at odd sizes -----------------------------------------------------------
// The engine halves the screen into five records (floor), then the luminance
// chain; 820B09B0 feeds each downsample the SOURCE extent halved in single
// precision, not the record's integer size. At odd sizes the two drift by
// less than a texel; the native plan reproduces the float extents exactly.
PostFinishRecord Record(uint32_t address,uint32_t texture,int32_t width,int32_t height) {
  PostFinishRecord record;
  record.address=address; record.texture=texture; record.width=width; record.height=height;
  record.texel_x=1.0f/float(width); record.texel_y=1.0f/float(height);
  return record;
}
void TestPostPyramid() {
  using L=PostFinishLayout;
  for(const auto [width,height]:{std::pair{1366,768},{2560,1080},{1706,720},{1024,768},{1920,1200},
                                   {5120,1440},{3440,1440},{641,481},{960,720},{5460,3072}}) {
    PostFinishInput in;
    in.self=0x40010000u; in.screen_width=width; in.screen_height=height; in.scene_texture=0x50000000u;
    for(uint32_t k=0;k<L::kFirstPyramidCount;++k)
      in.first[k]=Record(in.self+L::kFirstPyramid+k*L::kRecordStride,0x51000000u+k,
                         std::max(1,width>>(k+1)),std::max(1,height>>(k+1)));
    for(const int32_t side:{32,16,8,4,1})
      in.second.push_back(Record(0x42000000u+uint32_t(in.second.size())*L::kRecordStride,
                                 0x52000000u+uint32_t(in.second.size()),side,side));
    in.blur=Record(in.self+L::kBlurTarget,0x53000000u,in.first[4].width,in.first[4].height);
    in.blur_vertical=Record(in.self+L::kBlurTargetVertical,0x53000001u,in.first[4].width,in.first[4].height);
    in.mono_technique=1; in.downsample_tone_technique=2; in.downsample_technique=3;
    in.tone_technique=4; in.blur_technique=5; in.bloom_technique=6;
    CHECK(!ValidatePostFinishInput(in));
    const auto plan=BuildPostFinishPlan(in);
    CHECK(plan.passes.size()==in.first.size()+in.second.size()+4);
    float source_w=float(width),source_h=float(height);
    for(size_t k=0;k<in.first.size();++k) {
      if(k) { source_w*=0.5f; source_h*=0.5f; }
      const auto& pass=plan.passes[k];
      CHECK(pass.target_width==in.first[k].width && pass.target_height==in.first[k].height);
      CHECK(pass.target_width>=1 && pass.target_height>=1);
      // The float source extent and the source texture's integer size are
      // within one texel of each other.
      const double actual_source_w=k?in.first[k-1].width:width,actual_source_h=k?in.first[k-1].height:height;
      CHECK(std::abs(source_w-actual_source_w)<1.0 && std::abs(source_h-actual_source_h)<1.0);
      // The offsets are the source texel from those float extents.
      const auto& offsets=pass.setters.front().values;
      CHECK(offsets.size()==16 && offsets[4]==1.0f/source_w && offsets[9]==1.0f/source_h);
      CHECK(pass.quad && (*pass.quad)[2]==0.5f/source_w && (*pass.quad)[3]==0.5f/source_h);
      // A 2:1 box: the target never samples past its source.
      CHECK(2*pass.target_width<=actual_source_w+1 && 2*pass.target_height<=actual_source_h+1);
    }
    // The bloom composites at the full render size.
    CHECK(plan.passes.back().kind==PostPassKind::Bloom && plan.passes.back().target_width==width &&
          plan.passes.back().target_height==height);
  }
}
}  // namespace

// ---- Guest EDRAM budget (82139BC0 tiles, 8213B914 limit) ------------------------------
void TestGuestEdramBudget() {
  const auto back=kGuestBackBufferFormat;
  // The limit: 2048 tiles of 5120 bytes, the 10 MiB of EDRAM.
  CHECK(kGuestEdramTiles==2048 && kGuestEdramTileBytes==5120);
  CHECK(uint64_t(kGuestEdramTiles)*kGuestEdramTileBytes==10u*1024*1024);
  // The tile count, 80-sample columns by 16-row groups, 4 bytes per sample.
  CHECK(GuestEdramSurfaceTiles(1280,720,back,0)==720);
  CHECK(GuestEdramSurfaceTiles(1920,1080,back,0)==1632);
  CHECK(GuestEdramSurfaceTiles(1920,1200,back,0)==1800);
  CHECK(GuestEdramSurfaceTiles(2133,1200,back,0)==2025);
  CHECK(GuestEdramSurfaceTiles(1365,768,back,0)==864);
  CHECK(GuestEdramSurfaceTiles(2560,1080,back,0)==2176);
  CHECK(GuestEdramSurfaceTiles(2560,1440,back,0)==2880);
  CHECK(GuestEdramSurfaceTiles(3440,1440,back,0)==3870);
  CHECK(GuestEdramSurfaceTiles(3840,2160,back,0)==6480);
  CHECK(GuestEdramSurfaceTiles(5120,1440,back,0)==5760);
  // MSAA 1 doubles the rows (the tiled depth the log reports as 640x736,
  // MSAA=1), 2 also the columns; 64-bit formats (21, 32, 37) take twice the bytes.
  CHECK(GuestEdramSurfaceTiles(640,736,0x1a220197u,1)==736);
  CHECK(GuestEdramSurfaceTiles(1280,720,back,2)==2880);
  CHECK(GuestEdramSurfaceTiles(1280,720,0x00000020u,0)==1440);
  CHECK(GuestEdramSurfaceTiles(1280,720,0x00000015u,0)==1440);
  CHECK(GuestEdramSurfaceTiles(1280,720,0x00000025u,0)==1440);
  // Exactly at the limit, and one row group past it.
  CHECK(GuestEdramSurfaceFits(2560,1024,back,0) && GuestEdramSurfaceTiles(2560,1024,back,0)==2048);
  CHECK(!GuestEdramSurfaceFits(2560,1025,back,0));
  CHECK(GuestEdramSurfaceFits(2720,960,back,0) && !GuestEdramSurfaceFits(2721,960,back,0));
  // The matrix: sizes that started fit the allocator; the five that crashed at
  // startup do not, and they are the ones the 8213B850 hook places itself.
  const auto native=NativeAspectMode::Native;
  for(const auto [w,h]:{std::pair{1280,720},{1920,1080},{1920,1200},{2133,1200},{1365,768}}) {
    const auto size=ResolveNativeRenderSize(w,h,w,h,native);
    CHECK(Size(size,w,h) && !size.clamped && ValidNativeRenderRequest(w,h));
    CHECK(GuestEdramSurfaceFits(uint32_t(size.width),uint32_t(size.height),back,0));
  }
  for(const auto [w,h]:{std::pair{2560,1440},{3840,2160},{2560,1080},{3440,1440},{5120,1440}}) {
    const auto size=ResolveNativeRenderSize(w,h,w,h,native);
    CHECK(Size(size,w,h) && !size.clamped && ValidNativeRenderRequest(w,h));
    CHECK(!GuestEdramSurfaceFits(uint32_t(size.width),uint32_t(size.height),back,0));
    // "Match window" and a line count resolve the same sizes on those displays.
    CHECK(Size(ResolveNativeRenderSize(0,-1,w,h,native),w,h));
    CHECK(Size(ResolveNativeRenderSize(0,h,w,h,native),w,h));
  }
  // The render size ceiling (8192 per axis, 4096*4096 pixels) stays in 32-bit
  // guest arithmetic and inside the descriptor fields 8213B280 packs: the
  // sample-aligned width shifted by two in 16 bits, the 80-aligned pitch in 14.
  for(const auto [w,h]:{std::pair{8192,2048},{2048,8192},{4096,4096},{5460,3072}}) {
    CHECK(ValidNativeRenderRequest(w,h));
    const uint32_t tiles=GuestEdramSurfaceTiles(uint32_t(w),uint32_t(h),back,0);
    CHECK(tiles>kGuestEdramTiles && uint64_t(tiles)*kGuestEdramTileBytes<=0xffffffffu);
    CHECK(((uint32_t(w)+31u)&~31u)<<2<=0xffffu);
    CHECK((uint32_t(w)+79u)/80u*80u<=0x3fffu);
  }
  CHECK(GuestEdramSurfaceTiles(8192,2048,back,0)==13184);
}

int main() {
  TestRenderSize();
  TestGuestEdramBudget();
  TestFieldOfView();
  TestCanvasLayout();
  TestCanvasDrawAndScissor();
  TestMovieFraming();
  TestPresentFit();
  TestPostPyramid();
  if(failures) { std::cerr<<failures<<" display layout check(s) failed\n"; return 1; }
  std::cout<<"Display layout tests passed\n";
  return 0;
}

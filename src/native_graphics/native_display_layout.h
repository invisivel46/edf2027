#pragma once
// Render size, field of view and 2D canvas layout for any window size and
// aspect ratio. Pure arithmetic: no guest memory, no cvars, no D3D, so the
// launcher, the bridge and the tests share one definition.
//
// What the game gives us (retail image):
// - 8219E3B8 fixes the renderer to 1280x720; the 82139A40 hook replaces it with
//   the size resolved here. Every scene, depth, post and 2D extent follows it.
// - clSgsCoreRender::slot7 (821BEA10) copies renderer+84/+88 into each camera's
//   viewport +496/+500, and 821CDDF8 builds the projection (821C82C0) and the
//   frustum (821C26C0) from the VERTICAL field of view at camera+480 and that
//   viewport's aspect. So the engine is Hor+ by construction: a wider render
//   widens the view and keeps the vertical angle. Narrower than 16:9 it would
//   crop the sides; NativeVerticalFovScale is the Vert+ correction for that.
// - The HUD, menus and text are laid out in a fixed 1280x720 canvas (the 2D
//   object 821A7440 takes 2/W, 2/H from the renderer, the XUI projection
//   82415330 takes 2/width of the viewport; neither layout reads the size).
//   The bridge maps that canvas onto the target: the legacy mapping fills the
//   whole target (stretched when it is not 16:9); the console safe area maps it
//   uniformly into the centred 16:9 rectangle.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace edf::native {

inline constexpr int32_t kNativeCanvasWidth=1280,kNativeCanvasHeight=720;

inline bool IsConsoleAspect(int64_t width,int64_t height) {
  return width>0 && height>0 && width*9==height*16;
}

// edf_aspect.
//  native     fill the window; wider than 16:9 is Hor+, narrower is Vert+
//             (the 16:9 view is never cropped).
//  ultrawide  fill the window; pure Hor+ at every aspect (the vertical angle
//             is fixed, so narrower windows lose the sides).
//  letterbox  render 16:9, black bars in the window.
//  stretch    render 16:9, stretched to the window.
enum class NativeAspectMode:uint8_t { Native,Ultrawide,Letterbox,Stretch };
inline NativeAspectMode ParseNativeAspectMode(std::string_view value) {
  return value=="ultrawide"?NativeAspectMode::Ultrawide:value=="letterbox"?NativeAspectMode::Letterbox:
         value=="stretch"?NativeAspectMode::Stretch:NativeAspectMode::Native;
}
inline bool NativeAspectFillsWindow(NativeAspectMode mode) {
  return mode==NativeAspectMode::Native || mode==NativeAspectMode::Ultrawide;
}

// ---- Render size -----------------------------------------------------------
// The render size request is the pair (edf_native_render_width, _height):
//   (0, 0)    original: 720 lines, the width from the display's shape
//             (exactly 1280x720 on a 16:9 display, the engine's own size).
//   (0, h)    h lines, the width from the display's shape.
//   (0, -1)   the display's size exactly ("match window").
//   (w, h)    exactly w x h (the display's shape is then not followed; the
//             presenter fits the image into the window).
// Letterbox and stretch always render 16:9.
struct NativeRenderSizeLimits {
  int32_t min_width=640,min_height=480;
  // 8192 is the Xenos 2D texture and render-target pitch ceiling the guest's
  // D3D descriptors can encode.
  int32_t max_extent=8192;
  // Guest memory holds the engine's resolve and post textures at this size,
  // and no larger footprint was ever accepted: the old validator allowed
  // 4095x4095. So the budget is 4096*4096 pixels (5120x2880 fits, 8K does not).
  int64_t max_pixels=int64_t(4096)*4096;
};
struct NativeRenderSize {
  int32_t width=0,height=0;
  bool original=false;  // 1280x720: the engine's own size, no override
  bool clamped=false;   // the request did not fit the limits and was scaled
};
inline bool ValidNativeRenderRequest(int32_t width,int32_t height,const NativeRenderSizeLimits& limits={}) {
  if(width==0) return height==0 || height==-1 || (height>=limits.min_height && height<=limits.max_extent);
  return width>=limits.min_width && width<=limits.max_extent && height>=limits.min_height &&
         height<=limits.max_extent && int64_t(width)*height<=limits.max_pixels;
}
// Nearest even integer: derived widths stay even so the post pyramid's 2:1
// halvings stay exact one level deeper and FSR gets an even extent.
inline int32_t NativeEvenRound(double value) {
  return int32_t(std::llround(value*0.5))*2;
}
inline NativeRenderSize ResolveNativeRenderSize(int32_t request_width,int32_t request_height,
    int32_t display_width,int32_t display_height,NativeAspectMode mode,const NativeRenderSizeLimits& limits={}) {
  const bool display=display_width>0 && display_height>0;
  const bool console=!NativeAspectFillsWindow(mode) || !display;
  const double aspect=console?16.0/9.0:double(display_width)/double(display_height);
  NativeRenderSize size;
  if(request_width>0 && request_height>0) {
    size.width=request_width; size.height=request_height;
    if(!NativeAspectFillsWindow(mode) && !IsConsoleAspect(size.width,size.height))
      size.width=NativeEvenRound(size.height*16.0/9.0);
  } else if(request_height==-1 && !console) {
    size.width=display_width; size.height=display_height;
  } else {
    size.height=request_height>0?request_height:request_height==-1 && display?display_height:kNativeCanvasHeight;
    if(!console && size.height==display_height) size.width=display_width;
    else size.width=NativeEvenRound(size.height*aspect);
  }
  // Too small: scale up keeping the shape (portrait windows).
  if(size.width<limits.min_width || size.height<limits.min_height) {
    const double up=(std::max)(double(limits.min_width)/(std::max)(size.width,1),
                             double(limits.min_height)/(std::max)(size.height,1));
    size.width=(std::max)(limits.min_width,int32_t(std::lround(size.width*up)));
    size.height=(std::max)(limits.min_height,int32_t(std::lround(size.height*up)));
    size.clamped=true;
  }
  // Too large: scale down keeping the shape.
  const int64_t pixels=int64_t(size.width)*size.height;
  if(size.width>limits.max_extent || size.height>limits.max_extent || pixels>limits.max_pixels) {
    const double down=(std::min)({double(limits.max_extent)/size.width,double(limits.max_extent)/size.height,
                                std::sqrt(double(limits.max_pixels)/double(pixels))});
    size.width=(std::max)(limits.min_width,int32_t(std::floor(size.width*down*0.5))*2);
    size.height=(std::max)(limits.min_height,int32_t(std::floor(size.height*down)));
    while(int64_t(size.width)*size.height>limits.max_pixels) --size.height;
    size.clamped=true;
  }
  size.original=size.width==kNativeCanvasWidth && size.height==kNativeCanvasHeight;
  return size;
}

// ---- Field of view -----------------------------------------------------------
// The factor to apply to tan(vertical fov / 2) for a view of width x height.
// Wider than 16:9 the engine is already Hor+ (1). Narrower, native mode keeps
// the 16:9 horizontal angle and extends the vertical one (Vert+), down to 4:3;
// below 4:3 (portrait) the vertical extension stops at the 4:3 amount and the
// horizontal angle narrows, so a 9:16 window does not get a 120-degree view.
inline float NativeVerticalFovScale(double width,double height,NativeAspectMode mode) {
  if(!(width>0) || !(height>0) || mode!=NativeAspectMode::Native) return 1.0f;
  const double aspect=width/height,console=16.0/9.0;
  if(aspect>=console-1e-9) return 1.0f;
  return float(console/(std::max)(aspect,4.0/3.0));
}
// fov: the full vertical angle in radians, as 821C82C0 consumes it (it halves
// it and takes the tangent). Returns the input bits untouched when scale is 1.
inline float NativeAdjustedVerticalFov(float fov,float scale) {
  if(scale==1.0f || !std::isfinite(fov) || !(fov>0.0f) || !std::isfinite(scale) || !(scale>0.0f)) return fov;
  constexpr double kMaximum=3.0;  // ~172 degrees: keep the projection finite
  const double adjusted=2.0*std::atan(std::tan(double(fov)*0.5)*double(scale));
  return float((std::min)(adjusted,kMaximum));
}
// Horizontal angle (radians) a vertical angle gives at an aspect; for tests
// and logs.
inline double NativeHorizontalFov(double vertical,double aspect) {
  return 2.0*std::atan(std::tan(vertical*0.5)*aspect);
}

// ---- 2D canvas ---------------------------------------------------------------
// edf_hud_safe_area.
//  full   the 1280x720 canvas spans the whole target (legacy; stretched when
//         the target is not 16:9, and the only mapping under which a 2D
//         element placed from a projected 3D position lines up with the view).
//  16:9   the canvas is mapped uniformly into the centred 16:9 rectangle:
//         HUD, menus, text and loading screens keep their shape; wide targets
//         get side bars, tall ones top/bottom bars. Untextured draws covering
//         the whole canvas (fades, flashes) still cover the whole target.
enum class NativeHudSafeArea:uint8_t { Full,Console };
inline NativeHudSafeArea ParseNativeHudSafeArea(std::string_view value) {
  return value=="16:9"||value=="console"||value=="safe"?NativeHudSafeArea::Console:NativeHudSafeArea::Full;
}
struct NativeCanvasRect { float x=0,y=0,width=0,height=0; };
// The target rectangle the canvas occupies, in whole pixels.
inline NativeCanvasRect NativeHudCanvasRect(uint32_t width,uint32_t height,NativeHudSafeArea area) {
  if(!width || !height || area==NativeHudSafeArea::Full || IsConsoleAspect(width,height))
    return {0,0,float(width),float(height)};
  const double scale=(std::min)(double(width)/kNativeCanvasWidth,double(height)/kNativeCanvasHeight);
  const double w=(std::min)(double(width),std::round(kNativeCanvasWidth*scale));
  const double h=(std::min)(double(height),std::round(kNativeCanvasHeight*scale));
  return {float(std::floor((width-w)*0.5)),float(std::floor((height-h)*0.5)),float(w),float(h)};
}
// A clip-space map x' = ax*x + bx, y' = ay*y + by applied after the legacy
// mapping (which puts the canvas on the whole target, clip [-1,1]).
struct NativeClipAffine {
  float ax=1,bx=0,ay=1,by=0;
  bool identity() const { return ax==1 && bx==0 && ay==1 && by==0; }
  bool operator==(const NativeClipAffine&) const=default;
};
inline NativeClipAffine NativeCanvasClipAffine(const NativeCanvasRect& rect,uint32_t width,uint32_t height) {
  if(!width || !height || (rect.x==0 && rect.y==0 && rect.width==float(width) && rect.height==float(height)))
    return {};
  NativeClipAffine affine;
  affine.ax=float(double(rect.width)/width);
  affine.bx=float((2.0*rect.x+rect.width)/width-1.0);
  affine.ay=float(double(rect.height)/height);
  affine.by=float(1.0-(2.0*rect.y+rect.height)/height);
  return affine;
}
inline NativeClipAffine NativeHudClipAffine(uint32_t width,uint32_t height,NativeHudSafeArea area) {
  return NativeCanvasClipAffine(NativeHudCanvasRect(width,height,area),width,height);
}
// Per draw, from the draw's legacy clip-space bounds (the whole canvas is
// [-1,1] on both axes):
//  - untextured (a fade, a flash, a tint, a letterbox band) and spanning the
//    whole canvas on an axis: keeps the legacy full-target mapping on that
//    axis, so a fade still covers the frame and a band still reaches the
//    edges (a flat colour has no shape to distort);
//  - anything else, textured full-screen pictures included, takes the layout:
//    pictures, menus and loading screens are pillarboxed or letterboxed
//    rather than stretched or cropped.
struct NativeClipBounds { float min_x=0,max_x=0,min_y=0,max_y=0; bool valid=false; };
inline NativeClipAffine NativeCanvasDrawAffine(const NativeClipAffine& layout,const NativeClipBounds& bounds,bool solid) {
  if(layout.identity() || !solid || !bounds.valid) return layout;
  constexpr float kCover=0.98f;
  NativeClipAffine draw=layout;
  if(bounds.min_x<=-kCover && bounds.max_x>=kCover) { draw.ax=1; draw.bx=0; }
  if(bounds.min_y<=-kCover && bounds.max_y>=kCover) { draw.ay=1; draw.by=0; }
  return draw;
}
inline float ApplyClipAffineX(const NativeClipAffine& a,float x) { return a.ax*x+a.bx; }
inline float ApplyClipAffineY(const NativeClipAffine& a,float y) { return a.ay*y+a.by; }
// A pixel rectangle (left, top, right, bottom) of a width x height target,
// mapped through the affine: left/top round down, right/bottom up, clamped to
// the target; an empty rectangle stays empty.
inline std::array<int32_t,4> MapNativeCanvasPixelRect(std::array<int32_t,4> rect,const NativeClipAffine& a,
                                                       uint32_t width,uint32_t height) {
  if(a.identity() || !width || !height) return rect;
  const auto map=[](int32_t value,float scale,float offset,uint32_t extent,bool flip,bool end) {
    const double clip=flip?1.0-2.0*value/extent:2.0*value/extent-1.0;
    const double mapped=clip*scale+offset;
    const double pixel=flip?(1.0-mapped)*0.5*extent:(mapped+1.0)*0.5*extent;
    const double clamped=std::clamp(pixel,0.0,double(extent));
    // Tolerate float noise at exact pixel edges before rounding outward.
    const double snapped=std::abs(clamped-std::round(clamped))<1e-4?std::round(clamped):clamped;
    return int32_t(end?std::ceil(snapped):std::floor(snapped));
  };
  std::array<int32_t,4> out{map(rect[0],a.ax,a.bx,width,false,false),map(rect[1],a.ay,a.by,height,true,false),
                            map(rect[2],a.ax,a.bx,width,false,true),map(rect[3],a.ay,a.by,height,true,true)};
  if(rect[0]>=rect[2]) out[2]=out[0];
  if(rect[1]>=rect[3]) out[3]=out[1];
  return out;
}
// A clip-space rectangle covering the whole target within `tolerance`.
inline bool NativeClipBoundsCoverTarget(const NativeClipBounds& bounds,float tolerance=0.02f) {
  return bounds.valid && bounds.min_x<=-1+tolerance && bounds.max_x>=1-tolerance &&
         bounds.min_y<=-1+tolerance && bounds.max_y>=1-tolerance;
}

// ---- Presentation --------------------------------------------------------------
// How the published frame is placed in the window. Integer-aware: a
// letterboxed image scaled by a whole factor >= 2 is replicated exactly
// (nearest); a downscale averages the source footprint (area); anything else
// is bilinear, which is also what 1:1 uses, so an unscaled frame is unchanged.
enum class NativePresentFilter:uint8_t { Bilinear=0,Nearest=1,Area=2 };
struct NativePresentFit {
  float x=0,y=0,width=0,height=0;  // viewport in the window
  NativePresentFilter filter=NativePresentFilter::Bilinear;
  uint32_t taps=1;                 // area: taps per axis
};
inline NativePresentFit ComputeNativePresentFit(uint32_t source_width,uint32_t source_height,
    uint32_t window_width,uint32_t window_height,bool preserve_aspect,bool smart_filter) {
  NativePresentFit fit;
  if(!source_width || !source_height || !window_width || !window_height) return fit;
  const double sx=double(window_width)/source_width,sy=double(window_height)/source_height;
  if(!preserve_aspect) {
    fit.width=float(window_width); fit.height=float(window_height);
  } else {
    const double scale=(std::min)(sx,sy);
    const double rounded=std::round(scale);
    if(smart_filter && scale>=2.0 && std::abs(scale-rounded)<1e-6) {
      fit.width=float(source_width*rounded); fit.height=float(source_height*rounded);
    } else if(smart_filter) {
      // Whole pixels, so the image edge does not blend into the bars.
      fit.width=float((std::min<double>)(window_width,std::round(source_width*scale)));
      fit.height=float((std::min<double>)(window_height,std::round(source_height*scale)));
    } else {
      fit.width=float(source_width*scale); fit.height=float(source_height*scale);
    }
    fit.x=(float(window_width)-fit.width)*0.5f;
    fit.y=(float(window_height)-fit.height)*0.5f;
    if(smart_filter) { fit.x=std::floor(fit.x); fit.y=std::floor(fit.y); }
  }
  if(!smart_filter) return fit;
  const double fx=fit.width/source_width,fy=fit.height/source_height;
  const double rx=std::round(fx),ry=std::round(fy);
  if(fx>=2.0 && fy>=2.0 && std::abs(fx-rx)<1e-6 && std::abs(fy-ry)<1e-6) fit.filter=NativePresentFilter::Nearest;
  else if(fx<1.0-1e-6 || fy<1.0-1e-6) {
    fit.filter=NativePresentFilter::Area;
    fit.taps=uint32_t(std::clamp(std::ceil(1.0/(std::min)(fx,fy)),2.0,4.0));
  }
  return fit;
}

}  // namespace edf::native

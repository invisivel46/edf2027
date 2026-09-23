#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <span>
#include <stdexcept>
#include <cstdint>
#include <string_view>
#include "native_display_layout.h"
namespace edf::native {
// Classify once when publishing a native parameter binding plan.
inline bool IsNativeCanvasXY(uint64_t source,std::string_view entry,
                             std::string_view parameter,size_t group) {
  return group<2 && source==0xc885203e230fe745ull &&
    (entry=="VS_2D" || entry=="VS_2DTex") &&
    (parameter=="_g_DX2DScale" || parameter=="_g_DX2DOffset");
}
// Which of the two Utility canvas constants a parameter is: the scale
// multiplies the vertex position, the offset is added after it
// (clip = position * _g_DX2DScale + _g_DX2DOffset).
enum class NativeCanvasParameter:uint8_t { None,Scale,Offset };
inline NativeCanvasParameter ClassifyNativeCanvasParameter(uint64_t source,std::string_view entry,
                                                           std::string_view parameter,size_t group) {
  if(!IsNativeCanvasXY(source,entry,parameter,group)) return NativeCanvasParameter::None;
  return parameter=="_g_DX2DOffset"?NativeCanvasParameter::Offset:NativeCanvasParameter::Scale;
}
inline float NativeGuestFloatAt(std::span<const uint8_t> bytes,size_t offset) {
  uint32_t word=0;
  for(size_t b=0;b<4;++b) word=(word<<8)|bytes[offset+b];
  return std::bit_cast<float>(word);
}
inline void StoreNativeGuestFloat(std::span<uint8_t> bytes,size_t offset,float value) {
  const auto word=std::bit_cast<uint32_t>(value);
  for(size_t b=0;b<4;++b) bytes[offset+b]=uint8_t(word>>(24-b*8));
}
// Host-owned copy: scale clip-space XY without changing Z/W or guest memory.
inline std::array<uint8_t,16> ScaleNativeCanvasXY(std::span<const uint8_t> input,float x,float y) {
  if(input.size()!=16 || !std::isfinite(x) || !std::isfinite(y) || x<=0 || y<=0)
    throw std::runtime_error("invalid native canvas constant");
  std::array<uint8_t,16> output{};
  for(size_t i=0;i<16;++i) output[i]=input[i];
  for(size_t lane=0;lane<2;++lane) {
    const float scale=lane?y:x;
    if(scale==1) continue;
    uint32_t word=0;
    for(size_t b=0;b<4;++b) word=(word<<8)|input[lane*4+b];
    word=std::bit_cast<uint32_t>(std::bit_cast<float>(word)*scale);
    for(size_t b=0;b<4;++b) output[lane*4+b]=uint8_t(word>>(24-b*8));
  }
  return output;
}
// The legacy canvas scale (ScaleNativeCanvasXY), then the layout's clip-space
// affine: a scale-like constant (multiplies positions) takes a*v, an
// offset-like one (added in clip space) a*v + b. An identity affine returns
// exactly the legacy bytes.
inline std::array<uint8_t,16> MapNativeCanvasXY(std::span<const uint8_t> input,float x,float y,
                                                const NativeClipAffine& affine,bool offset) {
  auto output=ScaleNativeCanvasXY(input,x,y);
  if(affine.identity()) return output;
  if(!std::isfinite(affine.ax) || !std::isfinite(affine.ay) || !std::isfinite(affine.bx) ||
     !std::isfinite(affine.by) || affine.ax<=0 || affine.ay<=0)
    throw std::runtime_error("invalid native canvas layout");
  for(size_t lane=0;lane<2;++lane) {
    const float a=lane?affine.ay:affine.ax,b=offset?(lane?affine.by:affine.bx):0.0f;
    StoreNativeGuestFloat(output,lane*4,NativeGuestFloatAt(output,lane*4)*a+b);
  }
  return output;
}
// XUI (82415330 builds a top-left pixel ortho from the viewport: 2/width,
// -2/height, offsets -1/+1).
// Map the authored canvas to the full output while retaining homogeneous
// translation/depth. X grows from the left edge and Y from the top edge.
inline std::array<float,16> NativeXuiCanvasProjection(std::span<const uint8_t> bytes,
                                                     float scale_x,float scale_y,
                                                     const NativeClipAffine& affine={}) {
  if(bytes.size()!=64 || !std::isfinite(scale_x) || !std::isfinite(scale_y) ||
     scale_x<=0 || scale_y<=0) throw std::runtime_error("invalid XUI canvas projection");
  std::array<float,16> matrix{};
  for(size_t i=0;i<16;++i) {
    uint32_t word=0;
    for(size_t b=0;b<4;++b) word=(word<<8)|bytes[i*4+b];
    matrix[i]=std::bit_cast<float>(word);
  }
  for(size_t i=0;i<4;++i) {
    matrix[i]=scale_x*matrix[i]+(scale_x-1)*matrix[12+i];
    matrix[4+i]=scale_y*matrix[4+i]+(1-scale_y)*matrix[12+i];
  }
  // The layout (native_display_layout.h) after the legacy canvas: clip x' =
  // ax*x + bx*w, y' = ay*y + by*w, with w the projected homogeneous row.
  if(!affine.identity()) for(size_t i=0;i<4;++i) {
    matrix[i]=affine.ax*matrix[i]+affine.bx*matrix[12+i];
    matrix[4+i]=affine.ay*matrix[4+i]+affine.by*matrix[12+i];
  }
  return matrix;
}
// Clip-space bounds of an XUI draw: float2 big-endian positions (8-byte
// vertices) through TransformRows (+ArithmeticBias.x) and the projection rows
// in `projection` (already canvas-mapped), as VS_XuiTexture computes them.
inline NativeClipBounds NativeXuiClipBounds(std::span<const uint8_t> registers,std::span<const uint8_t> bias,
                                            const std::array<float,16>& projection,std::span<const uint8_t> vertices) {
  NativeClipBounds bounds;
  if(registers.size()<160 || bias.size()<4) return bounds;
  const auto f=[&](size_t offset) { return NativeGuestFloatAt(registers,offset); };
  const float add=NativeGuestFloatAt(bias,0),params=f(128);
  for(size_t at=0;at+8<=vertices.size();at+=8) {
    const float px=NativeGuestFloatAt(vertices,at),py=NativeGuestFloatAt(vertices,at+4);
    const auto row=[&](size_t r) { return f(r*16)*px+f(r*16+4)*py+f(r*16+12); };
    const float tx=row(0)+add,ty=row(1)+add,tw=row(3);
    const auto clip=[&](size_t r) {
      return projection[r*4]*tx+projection[r*4+1]*ty+projection[r*4+3]*tw+params*projection[r*4+2];
    };
    const float w=clip(3);
    if(!std::isfinite(w) || std::abs(w)<1e-12f) continue;
    const float x=clip(0)/w,y=clip(1)/w;
    if(!std::isfinite(x) || !std::isfinite(y)) continue;
    if(!bounds.valid) { bounds={x,x,y,y,true}; continue; }
    bounds.min_x=(std::min)(bounds.min_x,x); bounds.max_x=(std::max)(bounds.max_x,x);
    bounds.min_y=(std::min)(bounds.min_y,y); bounds.max_y=(std::max)(bounds.max_y,y);
  }
  return bounds;
}
// Movie framing at any render size other than the engine's 1280x720. The movie
// quad (four float2 position + float2 UV vertices, VS_Movie: TransformRows then
// ProjectionRows, plus Params.x times the projection's z column) is placed by
// the game either over the whole target or, like XUI, in 1280x720 canvas pixels
// at the top left (the startup logo does this: at 2560x1440 it filled only the
// top-left 1280x720). Either way it is fitted into the centred 16:9 rectangle,
// the same one edf_hud_safe_area=16:9 gives the canvas: uniformly scaled,
// pillarboxed or letterboxed, never stretched or cropped. On a 16:9 target the
// rectangle is the whole target, so a canvas quad is scaled to fill it. A quad
// that already fills the target as drawn (1280x720, or a full-target quad on
// 16:9) and anything that is neither kind is left alone, byte for byte.
//
// pixel_center: the guest's PA_SU_VTX_CNTL half-pixel offset (0.5 under
// pix_center kD3DZero, GuestPixelCenterOffset). The game's quad sits on its
// target's pixel edges in that convention (-0.5 .. 1279.5); a remapped quad is
// moved onto the host's edges first, so the scaled picture covers the whole
// rectangle and does not bleed a column into the bars.
enum class NativeMovieFraming:uint8_t { Unchanged,FullTarget,Canvas };
struct NativeMovieLayout {
  std::array<uint8_t,160> registers{};
  NativeMovieFraming framing=NativeMovieFraming::Unchanged;
};
inline NativeClipBounds NativeMovieClipBounds(std::span<const uint8_t> registers,std::span<const uint8_t> vertices) {
  NativeClipBounds bounds;
  if(registers.size()!=160) return bounds;
  const auto f=[&](size_t offset) { return NativeGuestFloatAt(registers,offset); };
  const float params=f(128);
  for(size_t at=0;at+16<=vertices.size();at+=16) {
    const float px=NativeGuestFloatAt(vertices,at),py=NativeGuestFloatAt(vertices,at+4);
    const auto row=[&](size_t r) { return f(r*16)*px+f(r*16+4)*py+f(r*16+12); };
    const float tx=row(0),ty=row(1),tw=row(3);
    const auto clip=[&](size_t r) {
      const size_t p=64+r*16;
      return f(p)*tx+f(p+4)*ty+f(p+12)*tw+params*f(p+8);
    };
    const float w=clip(3);
    if(!std::isfinite(w) || std::abs(w)<1e-12f) continue;
    const float x=clip(0)/w,y=clip(1)/w;
    if(!std::isfinite(x) || !std::isfinite(y)) continue;
    if(!bounds.valid) { bounds={x,x,y,y,true}; continue; }
    bounds.min_x=(std::min)(bounds.min_x,x); bounds.max_x=(std::max)(bounds.max_x,x);
    bounds.min_y=(std::min)(bounds.min_y,y); bounds.max_y=(std::max)(bounds.max_y,y);
  }
  return bounds;
}
inline NativeMovieLayout MapNativeMovieRegisters(std::span<const uint8_t> registers,std::span<const uint8_t> vertices,
                                                 uint32_t width,uint32_t height,float pixel_center=0.0f) {
  NativeMovieLayout layout;
  if(registers.size()!=160) throw std::runtime_error("invalid native movie register block");
  for(size_t i=0;i<160;++i) layout.registers[i]=registers[i];
  if(!width || !height || (width==uint32_t(kNativeCanvasWidth) && height==uint32_t(kNativeCanvasHeight))) return layout;
  if(!std::isfinite(pixel_center)) pixel_center=0.0f;
  const auto bounds=NativeMovieClipBounds(registers,vertices);
  if(!bounds.valid) return layout;
  // Tolerance: two target pixels.
  const float tx=4.0f/float(width),ty=4.0f/float(height);
  const auto close_to=[](float a,float b,float tolerance) { return std::abs(a-b)<=tolerance; };
  const float canvas_right=-1.0f+2.0f*kNativeCanvasWidth/float(width);
  const float canvas_bottom=1.0f-2.0f*kNativeCanvasHeight/float(height);
  float sx=1,sy=1;
  if(close_to(bounds.min_x,-1,tx) && close_to(bounds.max_x,1,tx) && close_to(bounds.min_y,-1,ty) && close_to(bounds.max_y,1,ty))
    layout.framing=NativeMovieFraming::FullTarget;
  else if(close_to(bounds.min_x,-1,tx) && close_to(bounds.max_x,canvas_right,tx) &&
          close_to(bounds.max_y,1,ty) && close_to(bounds.min_y,canvas_bottom,ty)) {
    layout.framing=NativeMovieFraming::Canvas;
    sx=float(width)/kNativeCanvasWidth; sy=float(height)/kNativeCanvasHeight;
  } else return layout;
  const auto affine=NativeHudClipAffine(width,height,NativeHudSafeArea::Console);
  // Already where the fitted picture goes (a full-target quad on 16:9).
  if(sx==1 && sy==1 && affine.identity()) { layout.framing=NativeMovieFraming::Unchanged; return layout; }
  // Half a guest target pixel in clip space (x right, y up), added per
  // homogeneous w.
  const float cx=2.0f*pixel_center/float(width),cy=-2.0f*pixel_center/float(height);
  std::array<float,16> p{};
  for(size_t i=0;i<16;++i) p[i]=NativeGuestFloatAt(registers,64+i*4);
  for(size_t i=0;i<4;++i) {
    const float px=p[i]+cx*p[12+i],py=p[4+i]+cy*p[12+i];
    // The canvas case first takes XUI's top-left canvas mapping to the whole target.
    const float x=sx*px+(sx-1)*p[12+i],y=sy*py+(1-sy)*p[12+i];
    p[i]=affine.ax*x+affine.bx*p[12+i];
    p[4+i]=affine.ay*y+affine.by*p[12+i];
  }
  for(size_t i=0;i<16;++i) StoreNativeGuestFloat(layout.registers,64+i*4,p[i]);
  return layout;
}
// Clip-space bounds of Utility 2D vertices (float2 big-endian position at the
// start of each `stride`-byte vertex) under scale/offset constants that are
// already in their legacy canvas form.
inline NativeClipBounds NativeCanvasVertexBounds(std::span<const uint8_t> vertices,size_t stride,
                                                 std::span<const uint8_t> scale,std::span<const uint8_t> offset) {
  NativeClipBounds bounds;
  if(stride<8 || scale.size()<8 || offset.size()<8) return bounds;
  const float sx=NativeGuestFloatAt(scale,0),sy=NativeGuestFloatAt(scale,4);
  const float ox=NativeGuestFloatAt(offset,0),oy=NativeGuestFloatAt(offset,4);
  for(size_t at=0;at+8<=vertices.size();at+=stride) {
    const float x=NativeGuestFloatAt(vertices,at)*sx+ox,y=NativeGuestFloatAt(vertices,at+4)*sy+oy;
    if(!std::isfinite(x) || !std::isfinite(y)) continue;
    if(!bounds.valid) { bounds={x,x,y,y,true}; continue; }
    bounds.min_x=(std::min)(bounds.min_x,x); bounds.max_x=(std::max)(bounds.max_x,x);
    bounds.min_y=(std::min)(bounds.min_y,y); bounds.max_y=(std::max)(bounds.max_y,y);
  }
  return bounds;
}
}

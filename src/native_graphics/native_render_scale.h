#pragma once
// FSR upscaling's narrowed scene recording (native_fsr.h, "Upscaling"): the
// guest and the native passes describe viewports and scissors in output
// pixels; while a view is drawn for upscaling, the recording maps them into
// the top-left render-size rectangle of the same (output-size) targets.
// Exact for whole-surface rectangles (0..display maps to 0..render with no
// rounding); other rectangles scale by render/display per axis, scissors
// rounded outward. Pure arithmetic over the backend's value types.
#include "native_render_backend.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace edf::native {
struct NativeRenderScale {
  uint32_t render_width=0,render_height=0,display_width=0,display_height=0;
  // Identity (nothing to map) unless both sizes are set and differ.
  bool active() const {
    return render_width && render_height && display_width && display_height &&
           (render_width!=display_width || render_height!=display_height);
  }
  bool operator==(const NativeRenderScale&) const=default;
  // A coordinate along x or y, in output pixels, to render pixels; computed
  // as value * render / display in double, so display maps exactly to render.
  double X(double value) const { return value*double(render_width)/double(display_width); }
  double Y(double value) const { return value*double(render_height)/double(display_height); }
};
inline NativeBackendViewport ScaleNativeViewport(const NativeBackendViewport& viewport,const NativeRenderScale& scale) {
  if(!scale.active()) return viewport;
  NativeBackendViewport result=viewport;
  result.x=float(scale.X(viewport.x)); result.y=float(scale.Y(viewport.y));
  result.width=float(scale.X(double(viewport.x)+viewport.width))-result.x;
  result.height=float(scale.Y(double(viewport.y)+viewport.height))-result.y;
  return result;
}
// Rounded outward (a partly covered render pixel stays drawable), clamped to
// the render rectangle; an empty rectangle stays empty.
inline NativeBackendScissor ScaleNativeScissor(const NativeBackendScissor& scissor,const NativeRenderScale& scale) {
  if(!scale.active()) return scissor;
  const auto snap=[](double value,bool end) {
    const double rounded=std::round(value);
    if(std::abs(value-rounded)<1e-6) return int32_t(rounded);
    return int32_t(end?std::ceil(value):std::floor(value));
  };
  const int32_t w=int32_t(scale.render_width),h=int32_t(scale.render_height);
  NativeBackendScissor result;
  result.left=std::clamp(snap(scale.X(scissor.left),false),0,w);
  result.top=std::clamp(snap(scale.Y(scissor.top),false),0,h);
  result.right=std::clamp(snap(scale.X(scissor.right),true),0,w);
  result.bottom=std::clamp(snap(scale.Y(scissor.bottom),true),0,h);
  if(scissor.right<=scissor.left) result.right=result.left;
  if(scissor.bottom<=scissor.top) result.bottom=result.top;
  return result;
}
}  // namespace edf::native

#include "native_render_state_decode.h"
#include <stdexcept>

namespace edf::native {
namespace {
uint32_t Factor(uint32_t value,bool alpha) {
  switch (value) {
    case 0: return kNativeBlendZero;
    case 1: return kNativeBlendOne;
    case 4: return alpha ? kNativeBlendSrcAlpha : kNativeBlendSrcColor;
    case 5: return alpha ? kNativeBlendInvSrcAlpha : kNativeBlendInvSrcColor;
    case 6: return kNativeBlendSrcAlpha;
    case 7: return kNativeBlendInvSrcAlpha;
    case 8: return alpha ? kNativeBlendDestAlpha : kNativeBlendDestColor;
    case 9: return alpha ? kNativeBlendInvDestAlpha : kNativeBlendInvDestColor;
    case 10: return kNativeBlendDestAlpha;
    case 11: return kNativeBlendInvDestAlpha;
    case 12: case 14: return kNativeBlendFactor;
    case 13: case 15: return kNativeBlendInvFactor;
    case 16: return alpha ? kNativeBlendOne : kNativeBlendSrcAlphaSat;
    default: throw std::runtime_error("unsupported native blend factor (constant blend needs a separate value)");
  }
}
uint32_t Operation(uint32_t value) {
  switch (value) {
    case 0: return kNativeBlendOpAdd;
    case 1: return kNativeBlendOpSubtract;
    case 2: return kNativeBlendOpMin;
    case 3: return kNativeBlendOpMax;
    case 4: return kNativeBlendOpRevSubtract;
    default: throw std::runtime_error("unsupported native blend operation");
  }
}
uint32_t FillMode(uint32_t raster) {
  // CPU PA_SU_SC_MODE_CNTL encoding, decoded without a GPU register runtime.
  const auto mode=(raster>>3)&3;
  if(!mode) return kNativeFillSolid; // Per-face type fields are inactive.
  if(mode!=1) throw std::runtime_error("unsupported native polygon mode");
  const auto front=(raster>>5)&7,back=(raster>>8)&7;
  const auto visible=(raster&1)?back:front;
  if(!(raster&3) && front!=back)
    throw std::runtime_error("native mixed-face polygon mode requires split draws");
  if(visible==1) return kNativeFillWireframe;
  if(visible==2) return kNativeFillSolid;
  throw std::runtime_error("unsupported native point/invalid polygon type");
}
}  // namespace

NativeDecodedRenderState DecodeNativeRenderState(const RenderStateWords& words) {
  const auto blend = words[0], depth = words[1], raster = words[2], alpha = words[3];
  if (depth & 1) throw std::runtime_error("native stencil state not implemented");
  if (alpha & 24) throw std::runtime_error("native alpha test/coverage requires shader or MSAA handling");
  if ((raster & 3) == 3 || (raster & 0x3800))
    throw std::runtime_error("unsupported native cull/polygon/offset state");
  if (words[4] > 15 || words[5] > 1) throw std::runtime_error("invalid native write-mask/scissor state");

  NativeDecodedRenderState result;
  result.blend_enable = blend != 0x10001;
  result.src_color = Factor(blend & 31,false);
  result.dst_color = Factor((blend>>8)&31,false);
  result.color_op = Operation((blend>>5)&7);
  result.src_alpha = Factor((blend>>16)&31,true);
  result.dst_alpha = Factor((blend>>24)&31,true);
  result.alpha_op = Operation((blend>>21)&7);
  result.write_mask = static_cast<uint8_t>(words[4]);

  result.depth_enable = (depth & 2) != 0;
  result.depth_write = (depth & 4) != 0;
  result.depth_func = ((depth>>4)&7)+1;

  result.fill = FillMode(raster);
  result.cull = raster & 1 ? kNativeCullFront : raster & 2 ? kNativeCullBack : kNativeCullNone;
  result.front_counter_clockwise = (raster & 4) == 0;
  result.depth_clip = true;
  result.scissor = words[5] != 0;

  const auto color_factor=[](uint32_t v){return v==12 || v==13;};
  const auto alpha_factor=[](uint32_t v){return v==14 || v==15;};
  const auto src=blend&31,dst=(blend>>8)&31;
  if((color_factor(src) || color_factor(dst)) && (alpha_factor(src) || alpha_factor(dst)))
    throw std::runtime_error("native mixed constant color/alpha RGB blending requires shader handling");
  result.replicate_blend_alpha=alpha_factor(src) || alpha_factor(dst);
  for(unsigned shift:{0u,8u,16u,24u}) {
    const auto value=(blend>>shift)&31;
    result.requires_blend_factor|=color_factor(value) || alpha_factor(value);
  }
  return result;
}
}  // namespace edf::native

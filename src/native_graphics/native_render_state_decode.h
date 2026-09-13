#pragma once
#include <array>
#include <cstdint>

namespace edf::native {
// Blend, depth, raster, alpha control, RT0 channel mask, scissor enable, as the
// guest wrote them.
using RenderStateWords = std::array<uint32_t,6>;

// Decoding these six words is guest logic, not API logic: it reads CPU-side
// PA_SU_SC_MODE_CNTL and blend-control encodings and has nothing to do with
// which graphics API draws the result. It lives here, once, because a second
// backend with its own copy would drift from this one silently - and a blend
// factor that is wrong only on D3D12 is a bug nobody would think to look for
// in a decoder that "already works".
//
// The numeric values below are deliberately the ones both D3D11 and D3D12 use
// for these enumerations. Each backend static_asserts that correspondence, so
// the sharing is proved at compile time rather than asserted in a comment.
enum : uint32_t {
  kNativeBlendZero=1,kNativeBlendOne=2,kNativeBlendSrcColor=3,kNativeBlendInvSrcColor=4,
  kNativeBlendSrcAlpha=5,kNativeBlendInvSrcAlpha=6,kNativeBlendDestAlpha=7,
  kNativeBlendInvDestAlpha=8,kNativeBlendDestColor=9,kNativeBlendInvDestColor=10,
  kNativeBlendSrcAlphaSat=11,kNativeBlendFactor=14,kNativeBlendInvFactor=15,
};
enum : uint32_t {
  kNativeBlendOpAdd=1,kNativeBlendOpSubtract=2,kNativeBlendOpRevSubtract=3,
  kNativeBlendOpMin=4,kNativeBlendOpMax=5,
};
enum : uint32_t { kNativeFillWireframe=2,kNativeFillSolid=3 };
enum : uint32_t { kNativeCullNone=1,kNativeCullFront=2,kNativeCullBack=3 };
enum : uint32_t { kNativeDepthWriteZero=0,kNativeDepthWriteAll=1 };

struct NativeDecodedRenderState {
  bool blend_enable=false;
  uint32_t src_color=kNativeBlendOne,dst_color=kNativeBlendZero,color_op=kNativeBlendOpAdd;
  uint32_t src_alpha=kNativeBlendOne,dst_alpha=kNativeBlendZero,alpha_op=kNativeBlendOpAdd;
  uint8_t write_mask=15;

  bool depth_enable=false,depth_write=false;
  uint32_t depth_func=1;  // Comparison function, 1..8.

  uint32_t fill=kNativeFillSolid,cull=kNativeCullNone;
  bool front_counter_clockwise=true,depth_clip=true,scissor=false;

  // The draw must supply a constant blend factor; binding without one is a
  // silent colour error, so the D3D11 path refuses it and so must any other.
  bool requires_blend_factor=false;
  // The constant factor's alpha is replicated to all four channels.
  bool replicate_blend_alpha=false;
};

// Throws std::runtime_error on state this renderer does not implement, which is
// how an unsupported combination stays visible instead of being approximated.
NativeDecodedRenderState DecodeNativeRenderState(const RenderStateWords& words);
}  // namespace edf::native

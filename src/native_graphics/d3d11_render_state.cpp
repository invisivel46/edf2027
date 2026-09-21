#include "d3d11_render_state.h"
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace edf::native {
NativeViewportState ScaleNativeCanvasScissor(NativeViewportState state,float sx,float sy,bool centered) {
  if(!std::isfinite(sx) || !std::isfinite(sy) || sx<=0 || sy<=0)
    throw std::runtime_error("invalid native canvas scissor scale");
  if(sx==1 && sy==1) return state;
  const auto& v=state.viewport;
  const double ax=v.TopLeftX+(centered?v.Width*.5:0),ay=v.TopLeftY+(centered?v.Height*.5:0);
  auto edge=[](LONG value,double anchor,double scale,double low,double high,bool end) {
    const double mapped=std::clamp(anchor+(double(value)-anchor)*scale,low,high);
    return LONG(end?std::ceil(mapped):std::floor(mapped));
  };
  const auto old=state.scissor;
  state.scissor={edge(old.left,ax,sx,v.TopLeftX,v.TopLeftX+v.Width,false),
    edge(old.top,ay,sy,v.TopLeftY,v.TopLeftY+v.Height,false),
    edge(old.right,ax,sx,v.TopLeftX,v.TopLeftX+v.Width,true),
    edge(old.bottom,ay,sy,v.TopLeftY,v.TopLeftY+v.Height,true)};
  // Rounding must never turn an empty clip into a visible strip.
  if(old.left>=old.right) state.scissor.right=state.scissor.left;
  if(old.top>=old.bottom) state.scissor.bottom=state.scissor.top;
  return state;
}
// The six guest words are decoded once, in native_render_state_decode.cpp,
// and shared with every other backend. These assertions are what make that
// sharing safe: the neutral values are only usable as D3D11 enumerators
// because they are numerically the same, and if that ever stops being true
// this file stops compiling instead of blending wrongly.
static_assert(kNativeBlendZero==D3D11_BLEND_ZERO && kNativeBlendOne==D3D11_BLEND_ONE);
static_assert(kNativeBlendSrcColor==D3D11_BLEND_SRC_COLOR && kNativeBlendInvSrcColor==D3D11_BLEND_INV_SRC_COLOR);
static_assert(kNativeBlendSrcAlpha==D3D11_BLEND_SRC_ALPHA && kNativeBlendInvSrcAlpha==D3D11_BLEND_INV_SRC_ALPHA);
static_assert(kNativeBlendDestAlpha==D3D11_BLEND_DEST_ALPHA && kNativeBlendInvDestAlpha==D3D11_BLEND_INV_DEST_ALPHA);
static_assert(kNativeBlendDestColor==D3D11_BLEND_DEST_COLOR && kNativeBlendInvDestColor==D3D11_BLEND_INV_DEST_COLOR);
static_assert(kNativeBlendSrcAlphaSat==D3D11_BLEND_SRC_ALPHA_SAT);
static_assert(kNativeBlendFactor==D3D11_BLEND_BLEND_FACTOR && kNativeBlendInvFactor==D3D11_BLEND_INV_BLEND_FACTOR);
static_assert(kNativeBlendOpAdd==D3D11_BLEND_OP_ADD && kNativeBlendOpSubtract==D3D11_BLEND_OP_SUBTRACT);
static_assert(kNativeBlendOpRevSubtract==D3D11_BLEND_OP_REV_SUBTRACT);
static_assert(kNativeBlendOpMin==D3D11_BLEND_OP_MIN && kNativeBlendOpMax==D3D11_BLEND_OP_MAX);
static_assert(kNativeFillWireframe==D3D11_FILL_WIREFRAME && kNativeFillSolid==D3D11_FILL_SOLID);
static_assert(kNativeCullNone==D3D11_CULL_NONE && kNativeCullFront==D3D11_CULL_FRONT && kNativeCullBack==D3D11_CULL_BACK);
static_assert(kNativeDepthWriteZero==D3D11_DEPTH_WRITE_MASK_ZERO && kNativeDepthWriteAll==D3D11_DEPTH_WRITE_MASK_ALL);

NativeRenderState CreateNativeRenderState(ID3D11Device& device,const RenderStateWords& words) {
  return CreateNativeRenderState(&device,words);
}
NativeRenderState CreateNativeRenderState(ID3D11Device* device,const RenderStateWords& words) {
  const auto decoded=DecodeNativeRenderState(words);
  D3D11_BLEND_DESC blend_desc{};
  auto& target = blend_desc.RenderTarget[0];
  target.BlendEnable = decoded.blend_enable;
  target.SrcBlend = static_cast<D3D11_BLEND>(decoded.src_color);
  target.DestBlend = static_cast<D3D11_BLEND>(decoded.dst_color);
  target.BlendOp = static_cast<D3D11_BLEND_OP>(decoded.color_op);
  target.SrcBlendAlpha = static_cast<D3D11_BLEND>(decoded.src_alpha);
  target.DestBlendAlpha = static_cast<D3D11_BLEND>(decoded.dst_alpha);
  target.BlendOpAlpha = static_cast<D3D11_BLEND_OP>(decoded.alpha_op);
  target.RenderTargetWriteMask = decoded.write_mask;
  D3D11_DEPTH_STENCIL_DESC depth_desc{};
  depth_desc.DepthEnable = decoded.depth_enable;
  depth_desc.DepthWriteMask = decoded.depth_write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
  depth_desc.DepthFunc = static_cast<D3D11_COMPARISON_FUNC>(decoded.depth_func);
  D3D11_RASTERIZER_DESC raster_desc{};
  raster_desc.FillMode = static_cast<D3D11_FILL_MODE>(decoded.fill);
  raster_desc.CullMode = static_cast<D3D11_CULL_MODE>(decoded.cull);
  raster_desc.FrontCounterClockwise = decoded.front_counter_clockwise;
  raster_desc.DepthClipEnable = decoded.depth_clip;
  raster_desc.ScissorEnable = decoded.scissor;
  NativeRenderState result;
  result.requires_blend_factor=decoded.requires_blend_factor;
  result.replicate_blend_alpha=decoded.replicate_blend_alpha;
  if(!device) return result;
  if (FAILED(device->CreateBlendState(&blend_desc,&result.blend)) ||
      FAILED(device->CreateDepthStencilState(&depth_desc,&result.depth)) ||
      FAILED(device->CreateRasterizerState(&raster_desc,&result.raster)))
    throw std::runtime_error("native render state creation failed");
  return result;
}
NativeViewportState MakeNativeViewport(uint32_t x,uint32_t y,uint32_t width,uint32_t height,
  float min_depth,float max_depth,bool scissor_enabled,const std::array<int32_t,4>& scissor) {
  if (uint64_t(x)+width > 32767 || uint64_t(y)+height > 32767 ||
      !std::isfinite(min_depth) || !std::isfinite(max_depth) || min_depth < 0 ||
      max_depth > 1 || min_depth > max_depth)
    throw std::runtime_error("unsupported native viewport bounds/depth range");
  NativeViewportState result{{float(x),float(y),float(width),float(height),min_depth,max_depth},
                            {LONG(x),LONG(y),LONG(x+width),LONG(y+height)}};
  if (scissor_enabled) {
    result.scissor.left = (std::max)(result.scissor.left,LONG(scissor[0]));
    result.scissor.top = (std::max)(result.scissor.top,LONG(scissor[1]));
    result.scissor.right = (std::min)(result.scissor.right,LONG(scissor[2]));
    result.scissor.bottom = (std::min)(result.scissor.bottom,LONG(scissor[3]));
    // An empty intersection clips every pixel, rather than passing an inverted
    // rectangle to D3D11 or allowing a previous draw's rectangle to survive.
    if (result.scissor.right <= result.scissor.left || result.scissor.bottom <= result.scissor.top)
      result.scissor = {0,0,0,0};
  }
  return result;
}
void NativeViewportState::Bind(ID3D11DeviceContext& context) const {
  context.RSSetViewports(1,&viewport);
  context.RSSetScissorRects(1,&scissor);
}
NativeViewportState MakeNativeDrawViewport(uint32_t x,uint32_t y,uint32_t width,uint32_t height,
  float min_depth,float max_depth,bool scissor_enabled,const std::array<int32_t,4>& scissor) {
  const bool reversed=min_depth>max_depth;
  auto result=MakeNativeViewport(x,y,width,height,reversed?max_depth:min_depth,
    reversed?min_depth:max_depth,scissor_enabled,scissor);
  result.reverse_depth=reversed;
  return result;
}
void NativeRenderState::Bind(ID3D11DeviceContext& context) const {
  if(requires_blend_factor) throw std::runtime_error("native constant blend factor was not supplied");
  Bind(context,{1,1,1,1});
}
void NativeRenderState::Bind(ID3D11DeviceContext& context,const std::array<float,4>& factor) const {
  auto values=factor;
  if(requires_blend_factor) {
    for(float value:values) if(!std::isfinite(value) || value<0 || value>1)
      throw std::runtime_error("invalid native constant blend factor");
    if(replicate_blend_alpha) values.fill(factor[3]);
  }
  context.OMSetBlendState(blend.Get(),values.data(),0xffffffff);
  context.OMSetDepthStencilState(depth.Get(),0);
  context.RSSetState(raster.Get());
}
}

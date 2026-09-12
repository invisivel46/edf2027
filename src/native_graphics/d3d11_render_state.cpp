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
namespace {
D3D11_BLEND Factor(uint32_t value,bool alpha) {
  switch (value) {
    case 0: return D3D11_BLEND_ZERO;
    case 1: return D3D11_BLEND_ONE;
    case 4: return alpha ? D3D11_BLEND_SRC_ALPHA : D3D11_BLEND_SRC_COLOR;
    case 5: return alpha ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_INV_SRC_COLOR;
    case 6: return D3D11_BLEND_SRC_ALPHA;
    case 7: return D3D11_BLEND_INV_SRC_ALPHA;
    case 8: return alpha ? D3D11_BLEND_DEST_ALPHA : D3D11_BLEND_DEST_COLOR;
    case 9: return alpha ? D3D11_BLEND_INV_DEST_ALPHA : D3D11_BLEND_INV_DEST_COLOR;
    case 10: return D3D11_BLEND_DEST_ALPHA;
    case 11: return D3D11_BLEND_INV_DEST_ALPHA;
    case 12: case 14: return D3D11_BLEND_BLEND_FACTOR;
    case 13: case 15: return D3D11_BLEND_INV_BLEND_FACTOR;
    case 16: return alpha ? D3D11_BLEND_ONE : D3D11_BLEND_SRC_ALPHA_SAT;
    default: throw std::runtime_error("unsupported native blend factor (constant blend needs a separate value)");
  }
}
D3D11_BLEND_OP Operation(uint32_t value) {
  switch (value) {
    case 0: return D3D11_BLEND_OP_ADD;
    case 1: return D3D11_BLEND_OP_SUBTRACT;
    case 2: return D3D11_BLEND_OP_MIN;
    case 3: return D3D11_BLEND_OP_MAX;
    case 4: return D3D11_BLEND_OP_REV_SUBTRACT;
    default: throw std::runtime_error("unsupported native blend operation");
  }
}
D3D11_FILL_MODE FillMode(uint32_t raster) {
  // CPU PA_SU_SC_MODE_CNTL encoding, decoded without a GPU register runtime.
  const auto mode=(raster>>3)&3;
  if(!mode) return D3D11_FILL_SOLID; // Per-face type fields are inactive.
  if(mode!=1) throw std::runtime_error("unsupported native polygon mode");
  const auto front=(raster>>5)&7,back=(raster>>8)&7;
  const auto visible=(raster&1)?back:front;
  if(!(raster&3) && front!=back)
    throw std::runtime_error("native mixed-face polygon mode requires split draws");
  if(visible==1) return D3D11_FILL_WIREFRAME;
  if(visible==2) return D3D11_FILL_SOLID;
  throw std::runtime_error("unsupported native point/invalid polygon type");
}
}
NativeRenderState CreateNativeRenderState(ID3D11Device& device,const RenderStateWords& words) {
  const auto blend = words[0], depth = words[1], raster = words[2], alpha = words[3];
  if (depth & 1) throw std::runtime_error("native stencil state not implemented");
  if (alpha & 24) throw std::runtime_error("native alpha test/coverage requires shader or MSAA handling");
  if ((raster & 3) == 3 || (raster & 0x3800))
    throw std::runtime_error("unsupported native cull/polygon/offset state");
  D3D11_BLEND_DESC blend_desc{};
  auto& target = blend_desc.RenderTarget[0];
  target.BlendEnable = blend != 0x10001;
  target.SrcBlend = Factor(blend & 31,false); target.DestBlend = Factor((blend>>8)&31,false);
  target.BlendOp = Operation((blend>>5)&7);
  target.SrcBlendAlpha = Factor((blend>>16)&31,true); target.DestBlendAlpha = Factor((blend>>24)&31,true);
  target.BlendOpAlpha = Operation((blend>>21)&7);
  if (words[4] > 15 || words[5] > 1) throw std::runtime_error("invalid native write-mask/scissor state");
  target.RenderTargetWriteMask = static_cast<UINT8>(words[4]);
  D3D11_DEPTH_STENCIL_DESC depth_desc{};
  depth_desc.DepthEnable = (depth & 2) != 0;
  depth_desc.DepthWriteMask = depth & 4 ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
  depth_desc.DepthFunc = static_cast<D3D11_COMPARISON_FUNC>(((depth>>4)&7)+1);
  D3D11_RASTERIZER_DESC raster_desc{};
  raster_desc.FillMode = FillMode(raster);
  raster_desc.CullMode = raster & 1 ? D3D11_CULL_FRONT : raster & 2 ? D3D11_CULL_BACK : D3D11_CULL_NONE;
  raster_desc.FrontCounterClockwise = (raster & 4) == 0;
  raster_desc.DepthClipEnable = true;
  raster_desc.ScissorEnable = words[5] != 0;
  NativeRenderState result;
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
  if (FAILED(device.CreateBlendState(&blend_desc,&result.blend)) ||
      FAILED(device.CreateDepthStencilState(&depth_desc,&result.depth)) ||
      FAILED(device.CreateRasterizerState(&raster_desc,&result.raster)))
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

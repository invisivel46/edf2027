#include "d3d11_ui.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace edf::native {
namespace {
void Check(HRESULT result,const char* message) { if(FAILED(result)) throw std::runtime_error(message); }
void UploadBuffer(ID3D11Device& device,ID3D11DeviceContext& context,UINT bind,const void* data,UINT size,
    Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer,UINT& capacity) {
  if(!size) return;
  if(size>capacity) {
    D3D11_BUFFER_DESC desc{}; desc.ByteWidth=size; desc.Usage=D3D11_USAGE_DYNAMIC;
    desc.BindFlags=bind; desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    Microsoft::WRL::ComPtr<ID3D11Buffer> fresh;
    Check(device.CreateBuffer(&desc,nullptr,&fresh),"native UI buffer allocation failed");
    buffer=std::move(fresh); capacity=size;
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  Check(context.Map(buffer.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"native UI upload failed");
  std::memcpy(mapped.pData,data,size); context.Unmap(buffer.Get(),0);
}
}
NativeUiTexture CreateNativeUiTexture(ID3D11Device& device,UINT width,UINT height,
    std::span<const uint8_t> rgba,bool linear,bool repeat) {
  if(!width || !height || width>16384 || height>16384 || uint64_t(width)*height*4!=rgba.size())
    throw std::runtime_error("invalid native UI texture dimensions/data");
  NativeUiTexture result;
  D3D11_TEXTURE2D_DESC desc{}; desc.Width=width; desc.Height=height;
  desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
  desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.Usage=D3D11_USAGE_IMMUTABLE;
  desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  const D3D11_SUBRESOURCE_DATA initial{rgba.data(),width*4,0};
  Check(device.CreateTexture2D(&desc,&initial,&result.resource),"native UI texture allocation failed");
  Check(device.CreateShaderResourceView(result.resource.Get(),nullptr,&result.view),"native UI texture view failed");
  D3D11_SAMPLER_DESC sampler{};
  sampler.Filter=linear?D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT:D3D11_FILTER_MIN_MAG_MIP_POINT;
  sampler.AddressU=sampler.AddressV=sampler.AddressW=repeat?D3D11_TEXTURE_ADDRESS_WRAP:D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler.ComparisonFunc=D3D11_COMPARISON_NEVER; sampler.MaxLOD=D3D11_FLOAT32_MAX;
  Check(device.CreateSamplerState(&sampler,&result.sampler),"native UI sampler failed");
  return result;
}
NativeUiRenderer::NativeUiRenderer(ID3D11Device& device) : device_(&device) {
  Effect effect;
  effect.source=R"(
cbuffer Projection : register(b0) { float4 transform; };
struct Varying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };
Varying VS(float2 position : POSITION0,float2 uv : TEXCOORD0,float4 color : COLOR0) {
  Varying v; v.position=float4(position*transform.xy+transform.zw,0,1); v.uv=uv; v.color=color; return v;
}
Texture2D<float4> image : register(t0); SamplerState image_sampler : register(s0);
float4 PS(Varying v) : SV_TARGET { return v.color*image.Sample(image_sampler,v.uv); }
)";
  vertex_=CompileNativeShader(device,effect,{false,"VS","vs_5_0"},"native_ui.fx");
  pixel_=CompileNativeShader(device,effect,{true,"PS","ps_5_0"},"native_ui.fx");
  ValidateNativeShaderLink(vertex_,pixel_);
  const D3D11_INPUT_ELEMENT_DESC layout[]{
    {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
    {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0},
    {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
  Check(device.CreateInputLayout(layout,3,vertex_.bytecode->GetBufferPointer(),vertex_.bytecode->GetBufferSize(),&layout_),
        "native UI vertex layout failed");
  D3D11_BUFFER_DESC constant{}; constant.ByteWidth=16; constant.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
  Check(device.CreateBuffer(&constant,nullptr,&projection_),"native UI projection buffer failed");
  D3D11_BLEND_DESC blend{}; auto& color=blend.RenderTarget[0];
  color.BlendEnable=TRUE; color.SrcBlend=D3D11_BLEND_SRC_ALPHA; color.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
  color.BlendOp=color.BlendOpAlpha=D3D11_BLEND_OP_ADD;
  color.SrcBlendAlpha=color.DestBlendAlpha=D3D11_BLEND_ONE; color.RenderTargetWriteMask=15;
  Check(device.CreateBlendState(&blend,&blend_),"native UI blend state failed");
  D3D11_RASTERIZER_DESC raster{}; raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE;
  raster.ScissorEnable=TRUE; raster.DepthClipEnable=TRUE;
  Check(device.CreateRasterizerState(&raster,&raster_),"native UI rasterizer failed");
  const uint8_t white[]{255,255,255,255}; white_=CreateNativeUiTexture(device,1,1,white,false,false);
}
void NativeUiRenderer::CheckContext(ID3D11DeviceContext& context) const {
  Microsoft::WRL::ComPtr<ID3D11Device> owner; context.GetDevice(&owner);
  if(owner.Get()!=device_.Get() || context.GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)
    throw std::runtime_error("native UI context mismatch");
}
void NativeUiRenderer::Upload(ID3D11DeviceContext& context,std::span<const NativeUiVertex> vertices,
    std::span<const uint16_t> indices) {
  CheckContext(context); vertex_count_=0; index_values_.clear();
  if(vertices.size()>1048576 || indices.size()>4194304 || (vertices.empty() && !indices.empty()))
    throw std::runtime_error("native UI batch exceeds bounds");
  for(const auto& v:vertices)
    if(!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.u) || !std::isfinite(v.v))
      throw std::runtime_error("native UI vertex is nonfinite");
  UploadBuffer(*device_.Get(),context,D3D11_BIND_VERTEX_BUFFER,vertices.data(),UINT(vertices.size_bytes()),vertices_,vertex_capacity_);
  UploadBuffer(*device_.Get(),context,D3D11_BIND_INDEX_BUFFER,indices.data(),UINT(indices.size_bytes()),indices_,index_capacity_);
  index_values_.assign(indices.begin(),indices.end()); vertex_count_=UINT(vertices.size());
}
void NativeUiRenderer::Draw(ID3D11DeviceContext& context,ID3D11RenderTargetView& target,
    UINT width,UINT height,float coordinate_width,float coordinate_height,const NativeUiDraw& draw) {
  CheckContext(context);
  if(!draw.count) return;
  if(!width || !height || width>16384 || height>16384 || !std::isfinite(coordinate_width) ||
     !std::isfinite(coordinate_height) || coordinate_width<=0 || coordinate_height<=0 ||
     draw.count%(draw.lines?2:3)) throw std::runtime_error("invalid native UI draw dimensions/count");
  const float transform[]{2/coordinate_width,-2/coordinate_height,-1,1};
  if(!std::isfinite(transform[0]) || !std::isfinite(transform[1]))
    throw std::runtime_error("native UI projection overflows");
  if(index_values_.empty()) {
    if(draw.base_vertex<0 || uint64_t(draw.base_vertex)+draw.count>vertex_count_)
      throw std::runtime_error("native UI vertex range out of bounds");
  } else {
    if(uint64_t(draw.index_offset)+draw.count>index_values_.size())
      throw std::runtime_error("native UI index range out of bounds");
    for(UINT i=0;i<draw.count;++i) {
      const int64_t vertex=int64_t(index_values_[draw.index_offset+i])+draw.base_vertex;
      if(vertex<0 || vertex>=vertex_count_) throw std::runtime_error("native UI indexed vertex out of bounds");
    }
  }
  const auto& texture=draw.texture?*draw.texture:white_;
  if(!texture.view || !texture.sampler) throw std::runtime_error("native UI texture is incomplete");
  Microsoft::WRL::ComPtr<ID3D11Device> owner; target.GetDevice(&owner);
  if(owner.Get()!=device_.Get()) throw std::runtime_error("native UI target device mismatch");
  texture.view->GetDevice(owner.ReleaseAndGetAddressOf());
  if(owner.Get()!=device_.Get()) throw std::runtime_error("native UI texture device mismatch");
  texture.sampler->GetDevice(owner.ReleaseAndGetAddressOf());
  if(owner.Get()!=device_.Get()) throw std::runtime_error("native UI sampler device mismatch");
  Microsoft::WRL::ComPtr<ID3D11Resource> input,output;
  texture.view->GetResource(&input); target.GetResource(&output);
  if(input.Get()==output.Get()) throw std::runtime_error("native UI texture aliases target");
  D3D11_SHADER_RESOURCE_VIEW_DESC input_view{}; texture.view->GetDesc(&input_view);
  D3D11_RENDER_TARGET_VIEW_DESC output_view{}; target.GetDesc(&output_view);
  if(input_view.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D || input_view.Format!=DXGI_FORMAT_R8G8B8A8_UNORM ||
     output_view.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D || output_view.Format!=DXGI_FORMAT_R8G8B8A8_UNORM)
    throw std::runtime_error("native UI requires RGBA8 2D views");
  Microsoft::WRL::ComPtr<ID3D11Texture2D> surface;
  Check(output.As(&surface),"native UI target is not a texture");
  D3D11_TEXTURE2D_DESC desc{}; surface->GetDesc(&desc);
  if((std::max)(1u,desc.Width>>output_view.Texture2D.MipSlice)!=width ||
     (std::max)(1u,desc.Height>>output_view.Texture2D.MipSlice)!=height)
    throw std::runtime_error("native UI target dimensions mismatch");
  const RECT scissor{(std::clamp)(draw.scissor.left,0L,LONG(width)),(std::clamp)(draw.scissor.top,0L,LONG(height)),
    (std::clamp)(draw.scissor.right,0L,LONG(width)),(std::clamp)(draw.scissor.bottom,0L,LONG(height))};
  if(scissor.right<=scissor.left || scissor.bottom<=scissor.top) return;
  context.ClearState(); auto* rtv=&target; context.OMSetRenderTargets(1,&rtv,nullptr);
  context.OMSetBlendState(blend_.Get(),nullptr,~0u);
  context.RSSetState(raster_.Get()); context.RSSetScissorRects(1,&scissor);
  const D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1}; context.RSSetViewports(1,&viewport);
  context.UpdateSubresource(projection_.Get(),0,nullptr,transform,0,0);
  auto* constant=projection_.Get(); context.VSSetConstantBuffers(0,1,&constant);
  context.VSSetShader(vertex_.vertex.Get(),nullptr,0); context.PSSetShader(pixel_.pixel.Get(),nullptr,0);
  context.IASetInputLayout(layout_.Get());
  context.IASetPrimitiveTopology(draw.lines?D3D11_PRIMITIVE_TOPOLOGY_LINELIST:D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  auto* buffer=vertices_.Get(); const UINT stride=sizeof(NativeUiVertex),offset=0;
  context.IASetVertexBuffers(0,1,&buffer,&stride,&offset);
  auto* view=texture.view.Get(); context.PSSetShaderResources(0,1,&view);
  auto* sampler=texture.sampler.Get(); context.PSSetSamplers(0,1,&sampler);
  if(index_values_.empty()) context.Draw(draw.count,UINT(draw.base_vertex));
  else { context.IASetIndexBuffer(indices_.Get(),DXGI_FORMAT_R16_UINT,0); context.DrawIndexed(draw.count,draw.index_offset,draw.base_vertex); }
  view=nullptr; context.PSSetShaderResources(0,1,&view);
}
}

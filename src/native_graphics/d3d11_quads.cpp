#include "d3d11_quads.h"
#include "native_guest_vertex_stream.h"
#include <cstring>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace edf::native {
PositionTriangleStream::PositionTriangleStream(ID3D11Device& device,const NativeShader& shader)
    : device_(&device) {
  if(shader.entry.pixel || !shader.vertex || !shader.bytecode)
    throw std::runtime_error("position triangles require a native vertex shader");
  const D3D11_INPUT_ELEMENT_DESC element{
    "POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
  if(FAILED(device.CreateInputLayout(&element,1,shader.bytecode->GetBufferPointer(),
                                    shader.bytecode->GetBufferSize(),&layout_)))
    throw std::runtime_error("position triangle input layout mismatch");
}
void PositionTriangleStream::Draw(ID3D11DeviceContext& context,std::span<const uint8_t> guest) {
  Microsoft::WRL::ComPtr<ID3D11Device> owner; context.GetDevice(&owner);
  if(owner.Get()!=device_.Get() || context.GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)
    throw std::runtime_error("position triangles require matching immediate context");
  // Validation and the endian swap are guest logic, shared with whatever
  // draws these next; only the upload and the draw are D3D11's business.
  const auto host=ConvertGuestPositionTriangles(guest);
  const auto bytes=static_cast<UINT>(host.size());
  if(bytes>capacity_) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth=bytes; desc.Usage=D3D11_USAGE_DYNAMIC;
    desc.BindFlags=D3D11_BIND_VERTEX_BUFFER; desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    Microsoft::WRL::ComPtr<ID3D11Buffer> fresh;
    if(FAILED(device_->CreateBuffer(&desc,nullptr,&fresh)))
      throw std::runtime_error("position triangle buffer creation failed");
    vertices_=std::move(fresh); capacity_=bytes;
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if(FAILED(context.Map(vertices_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))
    throw std::runtime_error("position triangle upload failed");
  std::memcpy(mapped.pData,host.data(),host.size());
  context.Unmap(vertices_.Get(),0);
  auto* buffer=vertices_.Get(); const UINT stride=8,offset=0;
  context.IASetInputLayout(layout_.Get());
  context.IASetVertexBuffers(0,1,&buffer,&stride,&offset);
  context.IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);
  context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context.Draw(bytes/8,0);
}
bool CanInitializeReductionTarget(const NativeShader& vertex,const NativeShader& pixel,
  std::span<const uint8_t> guest,const NativeViewportState& viewport,
  const RenderStateWords& state,uint32_t width,uint32_t height,bool inputs_bound) {
  // Exact decoded PostEffect.dxsl source inspected in the retail dump. It has
  // no includes. VS copies clip XY, sets Z=0/W=1; the verified post PS
  // entries write RGBA unconditionally, without discard or depth output.
  // The tone variant additionally requires its old-history texture bound.
  constexpr uint64_t fingerprint=0x6b7926f9747c6933ull;
  if (!inputs_bound || vertex.entry.pixel || !pixel.entry.pixel ||
      vertex.source_bytes!=6292 || pixel.source_bytes!=6292 ||
      vertex.source_fingerprint!=fingerprint || pixel.source_fingerprint!=fingerprint ||
      vertex.entry.name!="VS_Main" || (pixel.entry.name!="PS_Downsample" && pixel.entry.name!="PS_Downsample_Mono" &&
      pixel.entry.name!="PS_Downsample_Tone" && pixel.entry.name!="PS_Tone" && pixel.entry.name!="PS_GaussBlur" &&
      pixel.entry.name!="PS_GaussBlur_Illuminance" && pixel.entry.name!="PS_Base" && pixel.entry.name!="PS_Bloom") ||
      guest.size()!=64 || !width || !height || viewport.reverse_depth ||
      state[0]!=0x10001 || (state[1]&3) || (state[2]&0x3ffb) ||
      (state[3]&24) || state[4]!=15 || state[5]!=0)
    return false;
  const auto& v=viewport.viewport;
  if (v.TopLeftX!=0 || v.TopLeftY!=0 || v.Width!=float(width) || v.Height!=float(height) ||
      v.MinDepth!=0 || v.MaxDepth!=1) return false;
  constexpr float corners[]{-1,1, 1,1, 1,-1, -1,-1};
  for (size_t i=0;i<4;++i) for (size_t lane=0;lane<4;++lane) {
    const auto* p=guest.data()+i*16+lane*4;
    const float value=std::bit_cast<float>((uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3]);
    if (!std::isfinite(value) || (lane<2 && value!=corners[i*2+lane])) return false;
  }
  return true;
}
QuadStream::QuadStream(ID3D11Device& device, const NativeShader& shader) : device_(&device) {
  if (shader.entry.pixel || !shader.vertex || !shader.bytecode)
    throw std::runtime_error("quad stream requires native vertex shader");
  const D3D11_INPUT_ELEMENT_DESC elements[]{
    {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
    {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}
  };
  if (FAILED(device.CreateInputLayout(elements,2,shader.bytecode->GetBufferPointer(),
                                     shader.bytecode->GetBufferSize(),&layout_)))
    throw std::runtime_error("native post quad input layout mismatch");
}
void QuadStream::Draw(ID3D11DeviceContext& context, std::span<const uint8_t> guest) {
  DrawStream(context,guest,false);
}
void QuadStream::DrawTriangleStrip(ID3D11DeviceContext& context,std::span<const uint8_t> guest) {
  DrawStream(context,guest,true);
}
void QuadStream::DrawStream(ID3D11DeviceContext& context,std::span<const uint8_t> guest,bool strip) {
  const auto host = ConvertGuestQuads(guest,strip);
  const UINT bytes = static_cast<UINT>(host.size());
  if (bytes > capacity_) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = bytes; desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER; desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Microsoft::WRL::ComPtr<ID3D11Buffer> fresh;
    if (FAILED(device_->CreateBuffer(&desc,nullptr,&fresh)))
      throw std::runtime_error("native quad vertex buffer creation failed");
    vertices_ = std::move(fresh); capacity_ = bytes;
  }
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context.Map(vertices_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))
    throw std::runtime_error("native quad vertex upload failed");
  std::memcpy(mapped.pData,host.data(),host.size());
  context.Unmap(vertices_.Get(),0);
  auto* buffer = vertices_.Get();
  const UINT stride = 16, offset = 0;
  context.IASetInputLayout(layout_.Get());
  context.IASetVertexBuffers(0,1,&buffer,&stride,&offset);
  context.IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);
  context.IASetPrimitiveTopology(strip ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context.Draw(bytes/16,0);
}
}

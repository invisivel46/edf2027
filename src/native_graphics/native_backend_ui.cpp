#include "native_backend_ui.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace edf::native {
namespace {
const char* kSource=R"SHADER(
cbuffer Projection : register(b0) { float4 transform; };
struct Varying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };
Varying VS(float2 position : POSITION0,float2 uv : TEXCOORD0,float4 color : COLOR0) {
  Varying v; v.position=float4(position*transform.xy+transform.zw,0,1); v.uv=uv; v.color=color; return v;
}
Texture2D<float4> image : register(t0); SamplerState image_sampler : register(s0);
float4 PS(Varying v) : SV_TARGET { return v.color*image.Sample(image_sampler,v.uv); }
)SHADER";
Microsoft::WRL::ComPtr<ID3DBlob> Compile(const char* entry,const char* profile) {
  Microsoft::WRL::ComPtr<ID3DBlob> code,errors;
  if(FAILED(D3DCompile(kSource,std::strlen(kSource),"native_backend_ui",nullptr,nullptr,
                      entry,profile,0,0,&code,&errors)))
    throw std::runtime_error(errors?static_cast<const char*>(errors->GetBufferPointer()):
                             "backend UI shader compilation failed");
  return code;
}
void UploadBuffer(NativeRenderBackend& backend,NativeBackendRecorder& recorder,
    std::unique_ptr<NativeBackendBuffer>& buffer,std::span<const uint8_t> bytes,bool index) {
  if(bytes.empty()) return;
  // Dynamic backend buffers keep earlier batches alive until GPU completion.
  if(!buffer || buffer->bytes()!=bytes.size()) {
    NativeBackendBufferDesc desc{}; desc.bytes=bytes.size(); desc.dynamic=true;
    desc.vertex=!index; desc.index=index;
    buffer=backend.CreateBuffer(desc,{});
  }
  recorder.UpdateBuffer(*buffer,0,bytes);
}
}
NativeBackendUiTexture NativeBackendUiRenderer::CreateTexture(uint32_t width,uint32_t height,
    std::span<const uint8_t> rgba,bool linear,bool repeat) {
  if(!width || !height || width>16384 || height>16384 || uint64_t(width)*height*4!=rgba.size())
    throw std::runtime_error("invalid backend UI texture dimensions/data");
  NativeBackendTextureDesc desc{}; desc.width=width; desc.height=height; desc.format=28;
  NativeBackendUiTexture result; result.texture=backend_.CreateTexture(desc,rgba);
  NativeBackendSamplerDesc sampler{};
  sampler.min=sampler.mag=linear?NativeBackendFilter::Linear:NativeBackendFilter::Point;
  sampler.mip=NativeBackendFilter::Point;
  sampler.u=sampler.v=sampler.w=repeat?NativeBackendAddress::Wrap:NativeBackendAddress::Clamp;
  result.sampler=&backend_.CreateSampler(sampler);
  return result;
}
NativeBackendUiRenderer::NativeBackendUiRenderer(NativeRenderBackend& backend) : backend_(backend) {
  const auto vertex=Compile("VS","vs_5_0"),pixel=Compile("PS","ps_5_0");
  const NativeBackendInputElement layout[]{
    {"POSITION",0,16,0,0,false,0},{"TEXCOORD",0,16,0,8,false,0},{"COLOR",0,28,0,16,false,0}};
  NativeBackendPipelineDesc desc{};
  desc.vertex={static_cast<const uint8_t*>(vertex->GetBufferPointer()),vertex->GetBufferSize()};
  desc.pixel={static_cast<const uint8_t*>(pixel->GetBufferPointer()),pixel->GetBufferSize()};
  desc.vertex_id=0x55495653; desc.pixel_id=0x55495053; desc.input_layout_id=0x55494c;
  desc.input_layout=layout;
  desc.state={6|(7<<8)|(1<<16)|(1<<24),0,0,0,15,1};
  desc.render_targets=1; desc.rtv_format[0]=28;
  desc.topology=NativeBackendTopology::TriangleList; triangles_=&backend.CreatePipeline(desc);
  desc.topology=NativeBackendTopology::LineList; lines_=&backend.CreatePipeline(desc);
  const uint8_t white[]{255,255,255,255}; white_=CreateTexture(1,1,white,false,false);
}
void NativeBackendUiRenderer::Upload(NativeBackendRecorder& recorder,
    std::span<const NativeBackendUiVertex> vertices,std::span<const uint16_t> indices) {
  vertex_count_=0; index_values_.clear();
  if(vertices.size()>1048576 || indices.size()>4194304 || (vertices.empty() && !indices.empty()))
    throw std::runtime_error("backend UI batch exceeds bounds");
  for(const auto& v:vertices)
    if(!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.u) || !std::isfinite(v.v))
      throw std::runtime_error("backend UI vertex is nonfinite");
  UploadBuffer(backend_,recorder,vertices_,{reinterpret_cast<const uint8_t*>(vertices.data()),vertices.size_bytes()},false);
  UploadBuffer(backend_,recorder,indices_,{reinterpret_cast<const uint8_t*>(indices.data()),indices.size_bytes()},true);
  index_values_.assign(indices.begin(),indices.end()); vertex_count_=vertices.size();
}
void NativeBackendUiRenderer::Draw(NativeBackendRecorder& recorder,NativeBackendRenderTarget& target,
    float coordinate_width,float coordinate_height,const NativeBackendUiDraw& draw) {
  if(!draw.count) return;
  const auto width=target.width(),height=target.height();
  if(!width || !height || width>16384 || height>16384 || !std::isfinite(coordinate_width) ||
     !std::isfinite(coordinate_height) || coordinate_width<=0 || coordinate_height<=0 ||
     draw.count%(draw.lines?2:3)) throw std::runtime_error("invalid backend UI draw dimensions/count");
  const float transform[]{2/coordinate_width,-2/coordinate_height,-1,1};
  if(!std::isfinite(transform[0]) || !std::isfinite(transform[1]))
    throw std::runtime_error("backend UI projection overflows");
  if(index_values_.empty()) {
    if(draw.base_vertex<0 || uint64_t(draw.base_vertex)+draw.count>vertex_count_)
      throw std::runtime_error("backend UI vertex range out of bounds");
  } else {
    if(uint64_t(draw.index_offset)+draw.count>index_values_.size())
      throw std::runtime_error("backend UI index range out of bounds");
    for(uint32_t i=0;i<draw.count;++i) {
      const int64_t vertex=int64_t(index_values_[draw.index_offset+i])+draw.base_vertex;
      if(vertex<0 || vertex>=vertex_count_) throw std::runtime_error("backend UI indexed vertex out of bounds");
    }
  }
  const auto& texture=draw.texture?*draw.texture:white_;
  if(!texture.texture || !texture.sampler || texture.texture.get()==target.texture())
    throw std::runtime_error("invalid backend UI texture");
  const NativeBackendScissor scissor{
    (std::clamp)(draw.scissor.left,0,int32_t(width)),(std::clamp)(draw.scissor.top,0,int32_t(height)),
    (std::clamp)(draw.scissor.right,0,int32_t(width)),(std::clamp)(draw.scissor.bottom,0,int32_t(height))};
  if(scissor.right<=scissor.left || scissor.bottom<=scissor.top) return;
  NativeBackendRenderTarget* targets[]{&target};
  recorder.SetRenderTargets(targets,nullptr);
  recorder.SetPipeline(draw.lines?*lines_:*triangles_);
  recorder.SetViewport({0,0,float(width),float(height),0,1});
  recorder.SetScissor(scissor,true);
  recorder.SetConstants(NativeBackendStage::Vertex,0,{reinterpret_cast<const uint8_t*>(transform),sizeof(transform)});
  recorder.SetVertexBuffer(0,*vertices_,sizeof(NativeBackendUiVertex),0);
  recorder.SetTexture(NativeBackendStage::Pixel,0,texture.texture.get());
  recorder.SetSampler(NativeBackendStage::Pixel,0,texture.sampler);
  if(index_values_.empty()) recorder.Draw(draw.count,uint32_t(draw.base_vertex));
  else {
    recorder.SetIndexBuffer(*indices_,NativeBackendIndexFormat::Uint16,0);
    recorder.DrawIndexed(draw.count,draw.index_offset,draw.base_vertex);
  }
  recorder.SetTexture(NativeBackendStage::Pixel,0,nullptr);
}
}

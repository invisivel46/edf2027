#include "native_backend_present.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace edf::native {
namespace {
// The renderer's own composite, not the game's, so it is compiled here rather
// than loaded from the disc - the same arrangement the D3D11 compositor uses.
const char* kSource=R"(
cbuffer Fit : register(b0) { float2 scale; float2 pad; };
Texture2D image : register(t0);
SamplerState filtering : register(s0);
struct Varying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Varying VS(float3 position : POSITION) {
  Varying output;
  output.position = float4(position.xy * scale, 0, 1);
  output.uv = float2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
  return output;
}
float4 PS(Varying input) : SV_TARGET { return float4(image.Sample(filtering, input.uv).rgb, 1); }
)";

ComPtr<ID3DBlob> Compile(const char* entry,const char* profile) {
  ComPtr<ID3DBlob> code,errors;
  if(FAILED(D3DCompile(kSource,std::strlen(kSource),nullptr,nullptr,nullptr,entry,profile,0,0,
                       &code,&errors)))
    throw std::runtime_error(std::string("backend present shader compile failed: ")+
                             (errors?static_cast<const char*>(errors->GetBufferPointer()):"?"));
  return code;
}
}  // namespace

NativeBackendWindowPresenter::NativeBackendWindowPresenter(NativeRenderBackend& backend)
    : backend_(&backend) {}

void NativeBackendWindowPresenter::CreateResources() {
  if(ready_) return;
  // A triangle covering clip space; the vertex shader scales it to preserve
  // the source's aspect inside whatever the window happens to be.
  const float cover[]={-1.0f,-1.0f,0.0f, 3.0f,-1.0f,0.0f, -1.0f,3.0f,0.0f};
  NativeBackendBufferDesc vertex_desc{};
  vertex_desc.bytes=sizeof(cover);
  vertex_desc.vertex=true;
  vertices_=backend_->CreateBuffer(vertex_desc,
    {reinterpret_cast<const uint8_t*>(cover),sizeof(cover)});

  const auto vertex=Compile("VS","vs_5_0");
  const auto pixel=Compile("PS","ps_5_0");
  static const NativeBackendInputElement layout[]={
    {"POSITION",0,6 /*R32G32B32_FLOAT*/,0,0,false,0}};
  NativeBackendPipelineDesc desc{};
  desc.vertex={static_cast<const uint8_t*>(vertex->GetBufferPointer()),vertex->GetBufferSize()};
  desc.pixel={static_cast<const uint8_t*>(pixel->GetBufferPointer()),pixel->GetBufferSize()};
  desc.vertex_id=0xB0E5E71;
  desc.pixel_id=0xB0E5E72;
  desc.input_layout=layout;
  desc.input_layout_id=0xB0E5E73;
  // No blending, no depth, solid, all channels - the same state the D3D11
  // compositor uses for the same job.
  desc.state={0x10001,0,0,0,15,0};
  desc.topology=NativeBackendTopology::TriangleList;
  desc.render_targets=1;
  desc.rtv_format[0]=28;  // R8G8B8A8_UNORM, the swap chain's.
  pipeline_=&backend_->CreatePipeline(desc);

  NativeBackendSamplerDesc sampler{};
  sampler.u=sampler.v=sampler.w=NativeBackendAddress::Clamp;
  sampler_=&backend_->CreateSampler(sampler);
  ready_=true;
}

bool NativeBackendWindowPresenter::Present(void* window,uint32_t width,uint32_t height,
                                           const SharedSource& source,bool vsync) {
  if(refused_ || !window || !width || !height || !source.texture || !source.fence) return false;
  CreateResources();

  if(!source_ || source_handle_!=source.texture) {
    NativeBackendTextureDesc desc{};
    desc.width=source.width;
    desc.height=source.height;
    desc.levels=1;
    desc.format=source.format;
    source_=backend_->OpenSharedTexture(source.texture,desc);
    if(!source_) { refused_=true; return false; }
    source_handle_=source.texture;
  }
  if(window_!=window || window_width_!=width || window_height_!=height) {
    backend_->AttachWindow(window,width,height);
    window_=window;
    window_width_=width;
    window_height_=height;
  }
  // The surface is still being written by the producing GPU until this value
  // is reached. Sampling without the wait reads it mid-write.
  if(!backend_->WaitSharedFence(source.fence,source.value)) { refused_=true; return false; }

  auto* back=backend_->BackBuffer();
  if(!back) return false;
  NativeBackendRenderTarget* colors[]={back};

  const float window_aspect=float(width)/float(height);
  const float source_aspect=float(source.width)/float(source.height);
  const float scale_x=source_aspect>window_aspect?1.0f:source_aspect/window_aspect;
  const float scale_y=source_aspect>window_aspect?window_aspect/source_aspect:1.0f;
  const float fit[4]{scale_x,scale_y,0,0};

  backend_->BeginFrame();
  auto& recorder=backend_->Recorder();
  recorder.SetRenderTargets(colors,nullptr);
  recorder.SetViewport({0,0,float(width),float(height),0,1});
  recorder.ClearColor(*back,{0,0,0,1});
  recorder.SetPipeline(*pipeline_);
  recorder.SetVertexBuffer(0,*vertices_,sizeof(float)*3,0);
  recorder.SetConstants(NativeBackendStage::Vertex,0,
                        {reinterpret_cast<const uint8_t*>(fit),sizeof(fit)});
  recorder.SetTexture(NativeBackendStage::Pixel,0,source_.get());
  recorder.SetSampler(NativeBackendStage::Pixel,0,sampler_);
  recorder.Draw(3,0);
  backend_->Submit();
  backend_->Present(vsync);
  ++presented_;
  return true;
}
}  // namespace edf::native

// FSR foundations on the D3D12 backend, on WARP with the debug layer: a
// sampled reversed-Z depth target read back through its SRV, compute-writable
// textures written by a raw compute dispatch, raw D3D12 access and the
// ordering of a raw pass against direct and draw-packet recording, outward
// scissor rounding for fractional viewports, and the FidelityFX DLL loader
// with no DLL, a broken DLL and (when staged next to this test) the real one.
#include "native_graphics/d3d12_backend.h"
#include "native_graphics/native_d3d12_raw.h"
#include "native_graphics/native_ffx.h"
#include "native_graphics/native_render_backend.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace edf::native;

namespace {
int failures=0;
void Check(bool ok,const std::string& message) {
  if(ok) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}
ComPtr<ID3DBlob> Compile(const char* source,const char* entry,const char* profile) {
  ComPtr<ID3DBlob> code,errors;
  if(FAILED(D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,entry,profile,0,0,&code,&errors)))
    throw std::runtime_error(std::string("shader compile failed: ")+
                             (errors?static_cast<const char*>(errors->GetBufferPointer()):"no detail"));
  return code;
}
std::span<const uint8_t> Bytes(ID3DBlob& blob) {
  return {static_cast<const uint8_t*>(blob.GetBufferPointer()),blob.GetBufferSize()};
}
template<class T> std::span<const uint8_t> Raw(const T& value) {
  return {reinterpret_cast<const uint8_t*>(&value),sizeof(value)};
}
float Half(uint16_t h) {
  const uint32_t sign=(h>>15)&1,exponent=(h>>10)&31,mantissa=h&1023;
  float value=exponent==0?std::ldexp(float(mantissa),-24)
             :exponent==31?INFINITY:std::ldexp(float(mantissa|1024),int(exponent)-25);
  return sign?-value:value;
}
float FloatAt(const std::vector<uint8_t>& pixels,uint32_t width,uint32_t x,uint32_t y) {
  float value; std::memcpy(&value,pixels.data()+(size_t(y)*width+x)*4,4); return value;
}
std::array<uint8_t,4> Rgba8At(const std::vector<uint8_t>& pixels,uint32_t width,uint32_t x,uint32_t y) {
  const size_t at=(size_t(y)*width+x)*4;
  return {pixels[at],pixels[at+1],pixels[at+2],pixels[at+3]};
}
std::unique_ptr<NativeRenderBackend> Warp(uint32_t workers=0,uint32_t minimum_draws=32) {
  NativeD3D12Options options;
  options.prefer_warp=true;
  options.debug_layer=true;
  options.geometry_workers=workers;
  options.geometry_minimum_draws=minimum_draws;
  return CreateNativeD3D12Backend(options);
}
void CheckClean(NativeRenderBackend& backend,const std::string& what) {
  const auto messages=backend.DrainValidationMessages();
  for(const auto& message:messages) std::cerr << "  validation (" << what << "): " << message << '\n';
  Check(messages.empty(),what+": the debug layer reported "+std::to_string(messages.size())+" message(s)");
}

const NativeBackendInputElement kLayout[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,false,0}};
// Lower-left half of the target, and a triangle covering all of it.
const float kLowerLeft[]={-1,-1,0, 1,-1,0, -1,1,0};
const float kCover[]={-1,-1,0, 3,-1,0, -1,3,0};

struct Geometry {
  std::unique_ptr<NativeBackendBuffer> lower_left,cover;
  explicit Geometry(NativeRenderBackend& backend) {
    NativeBackendBufferDesc desc{}; desc.bytes=sizeof(kCover); desc.vertex=true;
    lower_left=backend.CreateBuffer(desc,Raw(kLowerLeft));
    cover=backend.CreateBuffer(desc,Raw(kCover));
  }
};

// A flat-colour pipeline: VS places the triangle at z = constant, PS writes
// the pixel constant. `depth_state` 0 draws without depth.
NativeBackendPipeline& FlatPipeline(NativeRenderBackend& backend,uint32_t rtv,uint32_t dsv,uint32_t depth_state,uint64_t id) {
  static const auto vs=Compile(R"(
cbuffer V : register(b0) { float4 z; };
float4 VS(float3 p : POSITION) : SV_POSITION { return float4(p.xy, z.x, 1); }
)","VS","vs_5_0");
  static const auto ps=Compile(R"(
cbuffer P : register(b0) { float4 tint; };
float4 PS(float4 p : SV_POSITION) : SV_TARGET { return tint; }
)","PS","ps_5_0");
  NativeBackendPipelineDesc desc{};
  desc.vertex=Bytes(*vs.Get()); desc.pixel=Bytes(*ps.Get());
  desc.vertex_id=0x1000+id; desc.pixel_id=0x2000+id;
  desc.input_layout=kLayout; desc.input_layout_id=0x3000;
  desc.state={0x10001,depth_state,0,0,15,0};
  desc.topology=NativeBackendTopology::TriangleList;
  desc.render_targets=1; desc.rtv_format[0]=rtv; desc.dsv_format=dsv;
  return backend.CreatePipeline(desc);
}
// A full-screen Load of t0 into a target of `rtv`.
NativeBackendPipeline& LoadPipeline(NativeRenderBackend& backend,uint32_t rtv,const char* texture_type,uint64_t id) {
  const std::string source=std::string("Texture2D<")+texture_type+R"(> image : register(t0);
float4 VS(float3 p : POSITION) : SV_POSITION { return float4(p, 1); }
)"+texture_type+R"( PS(float4 p : SV_POSITION) : SV_TARGET { return image.Load(int3(p.xy, 0)); }
)";
  const auto vs=Compile(source.c_str(),"VS","vs_5_0"),ps=Compile(source.c_str(),"PS","ps_5_0");
  NativeBackendPipelineDesc desc{};
  desc.vertex=Bytes(*vs.Get()); desc.pixel=Bytes(*ps.Get());
  desc.vertex_id=0x4000+id; desc.pixel_id=0x5000+id;
  desc.input_layout=kLayout; desc.input_layout_id=0x3000;
  desc.state={0x10001,0,0,0,15,0};
  desc.topology=NativeBackendTopology::TriangleList;
  desc.render_targets=1; desc.rtv_format[0]=rtv;
  return backend.CreatePipeline(desc);
}
NativeBackendRenderTarget* One(const std::unique_ptr<NativeBackendRenderTarget>& target) { return target.get(); }

// ---------------------------------------------------------------------------
// Sampled reversed-Z depth: cleared to 0 (its declared clear), a triangle at
// 0.5 over the lower-left half, then a full cover at 0.25 with GREATER - kept
// only where the clear still is. Read back through the depth SRV, the halves
// hold 0.5 and 0.25; a clear to 1, a lost write or a wrong SRV plane each
// show as a different value.
void TestDepthSrv() {
  auto backend=Warp();
  Geometry geometry(*backend);
  constexpr uint32_t kSize=64;
  NativeBackendTextureDesc color_desc{};
  color_desc.width=color_desc.height=kSize; color_desc.format=DXGI_FORMAT_R8G8B8A8_UNORM; color_desc.render_target=true;
  const auto color=backend->CreateRenderTarget(color_desc);
  NativeBackendTextureDesc depth_desc{};
  depth_desc.width=depth_desc.height=kSize; depth_desc.format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  depth_desc.depth=true; depth_desc.render_target=true; depth_desc.sampled=true; depth_desc.clear_depth=0.0f;
  const auto depth=backend->CreateRenderTarget(depth_desc);
  Check(depth->texture()!=nullptr,"a sampled depth target has no texture view");
  auto* raw=backend->D3D12Raw();
  Check(raw!=nullptr,"the D3D12 backend offers no raw access");
  Check(raw->Resource(*depth)->GetDesc().Format==DXGI_FORMAT_R32G8X24_TYPELESS,
        "a sampled D32S8 depth target is not typeless underneath");
  Check(raw->Resource(*depth->texture())==raw->Resource(*depth),"the depth SRV is not the depth resource");
  // Not sampled: exactly as before, typed and without a texture.
  NativeBackendTextureDesc typed_desc=depth_desc; typed_desc.sampled=false;
  const auto typed=backend->CreateRenderTarget(typed_desc);
  Check(!typed->texture() && raw->Resource(*typed)->GetDesc().Format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
        "an unsampled depth target is no longer typed D32S8");
  // MSAA depth still works; only the SRV is 1x-only.
  if(backend->SupportsSamples(DXGI_FORMAT_D32_FLOAT_S8X24_UINT,4)) {
    NativeBackendTextureDesc msaa=typed_desc; msaa.samples=4;
    Check(backend->CreateRenderTarget(msaa)!=nullptr,"a 4x depth target could not be created");
    msaa.sampled=true;
    bool refused=false;
    try { backend->CreateRenderTarget(msaa); } catch(const std::exception&) { refused=true; }
    Check(refused,"a multisampled depth target was given an SRV");
  }

  NativeBackendTextureDesc float_desc{};
  float_desc.width=float_desc.height=kSize; float_desc.format=DXGI_FORMAT_R32_FLOAT; float_desc.render_target=true;
  const auto depth_copy=backend->CreateRenderTarget(float_desc);
  // depth enable | write | GREATER (func 5), in the guest encoding.
  auto& greater=FlatPipeline(*backend,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_D32_FLOAT_S8X24_UINT,0x46,1);
  auto& load=LoadPipeline(*backend,DXGI_FORMAT_R32_FLOAT,"float",1);
  const std::array<float,4> half{0.5f,0,0,0},quarter{0.25f,0,0,0},white{1,1,1,1};
  NativeBackendRenderTarget* colors[]={color.get()};
  NativeBackendRenderTarget* copies[]={depth_copy.get()};
  backend->BeginFrame();
  auto& recorder=backend->Recorder();
  recorder.SetRenderTargets(colors,depth.get());
  recorder.SetViewport({0,0,kSize,kSize,0,1});
  recorder.ClearColor(*color,{0,0,0,1});
  recorder.ClearDepthStencil(*depth,true,true,0.0f,0);
  recorder.SetPipeline(greater);
  recorder.SetConstants(NativeBackendStage::Pixel,0,Raw(white));
  recorder.SetVertexBuffer(0,*geometry.lower_left,12,0);
  recorder.SetConstants(NativeBackendStage::Vertex,0,Raw(half));
  recorder.Draw(3,0);
  recorder.SetVertexBuffer(0,*geometry.cover,12,0);
  recorder.SetConstants(NativeBackendStage::Vertex,0,Raw(quarter));
  recorder.Draw(3,0);
  // Unbound as a DSV before it is sampled.
  recorder.SetRenderTargets(copies,nullptr);
  recorder.ClearColor(*depth_copy,{-1,0,0,0});
  recorder.SetPipeline(load);
  recorder.SetTexture(NativeBackendStage::Pixel,0,depth->texture());
  recorder.Draw(3,0);
  backend->Submit();
  const auto values=backend->ReadRenderTarget(*depth_copy);
  const float lower_left=FloatAt(values,kSize,2,kSize-3),upper_right=FloatAt(values,kSize,kSize-3,2);
  Check(lower_left==0.5f,"depth SRV lower-left reads "+std::to_string(lower_left)+", expected 0.5");
  Check(upper_right==0.25f,"depth SRV upper-right reads "+std::to_string(upper_right)+", expected 0.25");
  // The depth plane itself, through the copy path, agrees.
  const auto plane=backend->ReadRenderTarget(*depth);
  Check(plane.size()==size_t(kSize)*kSize*4 && FloatAt(plane,kSize,2,kSize-3)==0.5f &&
        FloatAt(plane,kSize,kSize-3,2)==0.25f,"the typeless depth plane does not read back as written");
  CheckClean(*backend,"depth SRV");
  std::cout << "depth SRV: lower-left " << lower_left << ", upper-right " << upper_right << '\n';
}

// ---------------------------------------------------------------------------
// A compute shader writes (x/16, y/16, 0.5, 1) into an RGBA16F UAV texture
// through a raw pass that sets its own heap, root signature and pipeline;
// the draw after it samples that texture through the backend, which only
// works if the pass put the backend's heaps and root signature back.
void TestUnorderedAccess() {
  auto backend=Warp();
  auto* raw=backend->D3D12Raw();
  Geometry geometry(*backend);
  constexpr uint32_t kSize=16;
  for(const uint32_t format:{uint32_t(DXGI_FORMAT_R16G16B16A16_FLOAT),uint32_t(DXGI_FORMAT_R8_UNORM),
                             uint32_t(DXGI_FORMAT_R16G16_FLOAT)}) {
    NativeBackendTextureDesc desc{};
    desc.width=desc.height=kSize; desc.format=format; desc.unordered_access=true;
    const auto texture=backend->CreateTexture(desc,{});
    Check((raw->Resource(*texture)->GetDesc().Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)!=0,
          "format "+std::to_string(format)+" texture is not UAV-capable");
  }
  NativeBackendTextureDesc plain{};
  plain.width=plain.height=kSize; plain.format=DXGI_FORMAT_R16G16B16A16_FLOAT;
  Check((raw->Resource(*backend->CreateTexture(plain,{}))->GetDesc().Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)==0,
        "a texture that did not ask for UAV access got it");
  NativeBackendTextureDesc uav_target{};
  uav_target.width=uav_target.height=kSize; uav_target.format=DXGI_FORMAT_R16G16B16A16_FLOAT;
  uav_target.render_target=true; uav_target.unordered_access=true;
  Check((raw->Resource(*backend->CreateRenderTarget(uav_target))->GetDesc().Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)!=0,
        "a UAV colour target is not UAV-capable");

  NativeBackendTextureDesc desc{};
  desc.width=desc.height=kSize; desc.format=DXGI_FORMAT_R16G16B16A16_FLOAT; desc.unordered_access=true;
  const auto written=backend->CreateTexture(desc,{});

  // The test's own compute objects, as FFX would own its own.
  auto* device=raw->Device();
  D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0};
  D3D12_ROOT_PARAMETER parameter{};
  parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable={1,&range};
  parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
  const D3D12_ROOT_SIGNATURE_DESC root_desc{1,&parameter,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
  ComPtr<ID3DBlob> serialized,errors;
  if(FAILED(D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors)))
    throw std::runtime_error("compute root signature serialization failed");
  ComPtr<ID3D12RootSignature> root;
  if(FAILED(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root))))
    throw std::runtime_error("compute root signature creation failed");
  const auto cs=Compile(R"(
RWTexture2D<float4> output : register(u0);
[numthreads(8, 8, 1)] void CS(uint3 id : SV_DispatchThreadID) { output[id.xy] = float4(id.x / 16.0, id.y / 16.0, 0.5, 1); }
)","CS","cs_5_0");
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
  pso_desc.pRootSignature=root.Get();
  pso_desc.CS={cs->GetBufferPointer(),cs->GetBufferSize()};
  ComPtr<ID3D12PipelineState> pso;
  if(FAILED(device->CreateComputePipelineState(&pso_desc,IID_PPV_ARGS(&pso))))
    throw std::runtime_error("compute pipeline creation failed");
  const D3D12_DESCRIPTOR_HEAP_DESC heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,
                                             D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
  ComPtr<ID3D12DescriptorHeap> heap;
  if(FAILED(device->CreateDescriptorHeap(&heap_desc,IID_PPV_ARGS(&heap))))
    throw std::runtime_error("compute descriptor heap creation failed");
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
  uav.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
  uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
  device->CreateUnorderedAccessView(raw->Resource(*written),nullptr,&uav,heap->GetCPUDescriptorHandleForHeapStart());

  NativeBackendTextureDesc target_desc{};
  target_desc.width=target_desc.height=kSize; target_desc.format=DXGI_FORMAT_R8G8B8A8_UNORM; target_desc.render_target=true;
  const auto target=backend->CreateRenderTarget(target_desc);
  auto& load=LoadPipeline(*backend,DXGI_FORMAT_R8G8B8A8_UNORM,"float4",2);
  NativeBackendRenderTarget* colors[]={target.get()};

  backend->BeginFrame();
  auto& recorder=backend->Recorder();
  const NativeD3D12RawUse uses[]={{written.get(),nullptr,D3D12_RESOURCE_STATE_UNORDERED_ACCESS}};
  bool ran=false;
  raw->RecordRaw(recorder,uses,[&](NativeD3D12RawPass& pass) {
    ran=true;
    auto* list=pass.commands();
    ID3D12DescriptorHeap* heaps[]={heap.Get()};
    list->SetDescriptorHeaps(1,heaps);
    list->SetComputeRootSignature(root.Get());
    list->SetPipelineState(pso.Get());
    list->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());
    list->Dispatch(kSize/8,kSize/8,1);
    // Its writes finished before anything reads it.
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource=raw->Resource(*written);
    list->ResourceBarrier(1,&barrier);
  });
  Check(ran,"the raw pass body never ran");
  // Everything rebound, as EndExternal requires.
  recorder.SetRenderTargets(colors,nullptr);
  recorder.SetViewport({0,0,kSize,kSize,0,1});
  recorder.SetPipeline(load);
  recorder.SetVertexBuffer(0,*geometry.cover,12,0);
  recorder.SetTexture(NativeBackendStage::Pixel,0,written.get());
  recorder.Draw(3,0);
  backend->Submit();
  const auto halves=backend->ReadTexture(*written);
  const auto texel=[&](uint32_t x,uint32_t y,uint32_t channel) {
    uint16_t h; std::memcpy(&h,halves.data()+(size_t(y)*kSize+x)*8+channel*2,2); return Half(h);
  };
  Check(texel(3,5,0)==3/16.0f && texel(3,5,1)==5/16.0f && texel(3,5,2)==0.5f && texel(3,5,3)==1.0f,
        "the compute write did not land in the UAV texture: ("+std::to_string(texel(3,5,0))+", "+
        std::to_string(texel(3,5,1))+")");
  Check(texel(15,15,0)==15/16.0f && texel(15,15,1)==15/16.0f,"the dispatch did not cover the whole texture");
  const auto sampled=backend->ReadRenderTarget(*target);
  const auto pixel=Rgba8At(sampled,kSize,12,4);
  Check(std::abs(int(pixel[0])-int(std::lround(12/16.0*255)))<=1 && std::abs(int(pixel[1])-int(std::lround(4/16.0*255)))<=1 &&
        std::abs(int(pixel[2])-128)<=1,"the draw after the raw pass did not sample the compute result");
  CheckClean(*backend,"UAV compute");
  std::cout << "UAV compute: texel(3,5)=" << texel(3,5,0) << "," << texel(3,5,1) << " sampled after raw pass\n";
}

// ---------------------------------------------------------------------------
// The objects a raw pass is handed, and the calls it refuses.
void TestRawAccess() {
  auto backend=Warp();
  auto* raw=backend->D3D12Raw();
  Check(raw && raw->Device() && raw->Queue(),"raw access has no device or queue");
  NativeBackendTextureDesc desc{};
  desc.width=desc.height=8; desc.format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.render_target=true; desc.sampled=true;
  const auto target=backend->CreateRenderTarget(desc);
  const auto texture=backend->CreateTexture({8,8,1,1,false,DXGI_FORMAT_R8G8B8A8_UNORM},{});
  Check(raw->Resource(*target) && raw->Resource(*target)->GetDesc().Width==8,"a render target has no resource");
  Check(raw->Resource(*texture) && raw->Resource(*texture)->GetDesc().Height==8,"a texture has no resource");
  Check(raw->Resource(*target->texture())==raw->Resource(*target),"a sampled target's texture is another resource");
  backend->BeginFrame();
  auto& recorder=backend->Recorder();
  backend->Submit();
  bool refused=false;
  try { raw->RecordRaw(recorder,{},[](NativeD3D12RawPass&) {}); } catch(const std::exception&) { refused=true; }
  Check(refused,"a raw pass outside a frame was not refused");
  backend->BeginFrame();
  ID3D12Device* seen_device=nullptr; ID3D12CommandQueue* seen_queue=nullptr; ID3D12GraphicsCommandList* seen_list=nullptr;
  const NativeD3D12RawUse uses[]={{nullptr,target.get(),D3D12_RESOURCE_STATE_COPY_SOURCE},
                                  {texture.get(),nullptr,D3D12_RESOURCE_STATE_COPY_DEST}};
  raw->RecordRaw(recorder,uses,[&](NativeD3D12RawPass& pass) {
    seen_device=pass.device(); seen_queue=pass.queue(); seen_list=pass.commands();
    pass.commands()->CopyResource(raw->Resource(*texture),raw->Resource(*target));
    // Handed back through the backend, which keeps its record right.
    pass.Transition(*texture,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  });
  Check(seen_device==raw->Device() && seen_queue==raw->Queue() && seen_list!=nullptr,
        "the raw pass was handed objects other than the backend's");
  refused=false;
  const NativeD3D12RawUse both[]={{texture.get(),target.get(),D3D12_RESOURCE_STATE_COMMON}};
  try { raw->RecordRaw(recorder,both,[](NativeD3D12RawPass&) {}); } catch(const std::exception&) { refused=true; }
  Check(refused,"a use naming both a texture and a target was not refused");
  refused=false;
  try { raw->RecordRaw(recorder,{},[](NativeD3D12RawPass&) { throw std::runtime_error("body"); }); }
  catch(const std::exception&) { refused=true; }
  Check(refused,"a throwing raw pass body was swallowed");
  // The recorder is usable after both.
  recorder.ClearColor(*target,{0,0,1,1});
  backend->Submit();
  Check(Rgba8At(backend->ReadRenderTarget(*target),8,1,1)[2]==255,"the recorder did not work after raw passes");
  CheckClean(*backend,"raw access");
  std::cout << "raw access: device, queue, list and resources handed out\n";
}

// ---------------------------------------------------------------------------
// Ordering: green draws, a raw copy of the target, red draws. The copy must
// hold green (after everything before it, before everything after it) and
// the target red. With draw packets both ways a flush can go: a run below the
// minimum replayed onto recorder 0, and a run the workers record and submit.
// The draws after the pass re-bind nothing: the packet recorder re-sends its
// captured state itself.
void TestRawOrdering(uint32_t workers,uint32_t minimum,uint32_t draws,const char* name) {
  auto backend=Warp(workers,minimum);
  auto* raw=backend->D3D12Raw();
  Geometry geometry(*backend);
  constexpr uint32_t kSize=32;
  NativeBackendTextureDesc target_desc{};
  target_desc.width=target_desc.height=kSize; target_desc.format=DXGI_FORMAT_R8G8B8A8_UNORM; target_desc.render_target=true;
  const auto target=backend->CreateRenderTarget(target_desc);
  NativeBackendTextureDesc copy_desc{};
  copy_desc.width=copy_desc.height=kSize; copy_desc.format=DXGI_FORMAT_R8G8B8A8_UNORM;
  const auto copy=backend->CreateTexture(copy_desc,{});
  auto& flat=FlatPipeline(*backend,DXGI_FORMAT_R8G8B8A8_UNORM,0,0,3);
  const std::array<float,4> green{0,1,0,1},red{1,0,0,1},zero{0,0,0,0};
  NativeBackendRenderTarget* colors[]={target.get()};
  for(uint32_t frame=0;frame<2;++frame) {
    backend->BeginFrame();
    auto& recorder=backend->Recorder();
    recorder.SetRenderTargets(colors,nullptr);
    recorder.SetViewport({0,0,kSize,kSize,0,1});
    recorder.ClearColor(*target,{0,0,0,1});
    recorder.SetPipeline(flat);
    recorder.SetVertexBuffer(0,*geometry.cover,12,0);
    recorder.SetConstants(NativeBackendStage::Vertex,0,Raw(zero));
    recorder.SetConstants(NativeBackendStage::Pixel,0,Raw(green));
    for(uint32_t draw=0;draw<draws;++draw) recorder.Draw(3,0);
    const NativeD3D12RawUse uses[]={{nullptr,target.get(),D3D12_RESOURCE_STATE_COPY_SOURCE},
                                    {copy.get(),nullptr,D3D12_RESOURCE_STATE_COPY_DEST}};
    raw->RecordRaw(recorder,uses,[&](NativeD3D12RawPass& pass) {
      pass.commands()->CopyResource(raw->Resource(*copy),raw->Resource(*target));
    });
    if(!workers) {
      // A direct recorder forgets its bindings across the pass.
      recorder.SetRenderTargets(colors,nullptr);
      recorder.SetViewport({0,0,kSize,kSize,0,1});
      recorder.SetPipeline(flat);
      recorder.SetVertexBuffer(0,*geometry.cover,12,0);
      recorder.SetConstants(NativeBackendStage::Vertex,0,Raw(zero));
    }
    recorder.SetConstants(NativeBackendStage::Pixel,0,Raw(red));
    for(uint32_t draw=0;draw<draws;++draw) recorder.Draw(3,0);
    backend->Submit();
  }
  const auto copied=Rgba8At(backend->ReadTexture(*copy),kSize,kSize/2,kSize/2);
  const auto final=Rgba8At(backend->ReadRenderTarget(*target),kSize,kSize/2,kSize/2);
  Check(copied[1]==255 && copied[0]==0,std::string(name)+": the raw copy did not see the draws recorded before it");
  Check(final[0]==255 && final[1]==0,std::string(name)+": the draws after the raw pass did not land after it");
  const auto stats=backend->Statistics();
  if(workers && draws>=minimum)
    Check(stats.geometry_batches>0,std::string(name)+": no batch went to the workers, so the parallel path was not tested");
  CheckClean(*backend,name);
  std::cout << "raw ordering (" << name << "): batches=" << stats.geometry_batches
            << " serial_flushes=" << stats.geometry_serial_flushes << '\n';
}

// ---------------------------------------------------------------------------
// The default scissor follows the viewport outward (NativeViewportScissor),
// and on the GPU it never clips inside the viewport: for jitter-sized
// fractional viewports the default scissor draws exactly what an explicit
// full-target scissor draws, i.e. only the viewport's own clip applies. A
// whole-pixel viewport covers exactly its pixels, as before.
void TestScissorRounding() {
  const auto same=[](const NativeBackendScissor& a,const NativeBackendScissor& b) {
    return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
  };
  Check(same(NativeViewportScissor({0.25f,0.25f,31.5f,31.5f,0,1}),{0,0,32,32}),"fractional viewport: scissor not rounded outward");
  Check(same(NativeViewportScissor({-0.25f,-0.75f,32,32,0,1}),{-1,-1,32,32}),"negative fractional origin: scissor not floored");
  Check(same(NativeViewportScissor({0.5f,0.75f,32,32,0,1}),{0,0,33,33}),"fractional far edge: scissor not ceiled");
  Check(same(NativeViewportScissor({0,0,32,32,0,1}),{0,0,32,32}) && same(NativeViewportScissor({3,5,10,20,0,1}),{3,5,13,25}),
        "whole-pixel viewport: scissor differs from truncation");
  auto backend=Warp();
  Geometry geometry(*backend);
  constexpr uint32_t kSize=64;
  NativeBackendTextureDesc target_desc{};
  target_desc.width=target_desc.height=kSize; target_desc.format=DXGI_FORMAT_R8G8B8A8_UNORM; target_desc.render_target=true;
  const auto target=backend->CreateRenderTarget(target_desc);
  auto& flat=FlatPipeline(*backend,DXGI_FORMAT_R8G8B8A8_UNORM,0,0,4);
  const std::array<float,4> green{0,1,0,1},zero{0,0,0,0};
  NativeBackendRenderTarget* colors[]={target.get()};
  const auto draw=[&](const NativeBackendViewport& viewport,bool wide_scissor=false) {
    backend->BeginFrame();
    auto& recorder=backend->Recorder();
    recorder.SetRenderTargets(colors,nullptr);
    recorder.SetViewport({0,0,kSize,kSize,0,1});
    recorder.ClearColor(*target,{0,0,0,1});
    recorder.SetViewport(viewport);
    if(wide_scissor) recorder.SetScissor({0,0,kSize,kSize},true);
    recorder.SetPipeline(flat);
    recorder.SetVertexBuffer(0,*geometry.cover,12,0);
    recorder.SetConstants(NativeBackendStage::Vertex,0,Raw(zero));
    recorder.SetConstants(NativeBackendStage::Pixel,0,Raw(green));
    recorder.Draw(3,0);
    backend->Submit();
    return backend->ReadRenderTarget(*target);
  };
  const auto green_at=[](const std::vector<uint8_t>& pixels,uint32_t x,uint32_t y) {
    return Rgba8At(pixels,kSize,x,y)[1]==255;
  };
  uint32_t compared=0;
  for(const float offset:{-0.75f,-0.5f,-0.25f,0.25f,0.5f,0.75f}) {
    for(const float extent:{31.5f,32.0f}) {
      const NativeBackendViewport viewport{offset+8,offset+4,extent,extent,0,1};
      const auto implied=draw(viewport),wide=draw(viewport,true);
      Check(implied==wide,"viewport at "+std::to_string(offset)+"+"+std::to_string(extent)+
            ": the default scissor clipped inside the viewport");
      ++compared;
    }
  }
  const auto whole=draw({8,4,32,32,0,1});
  uint32_t covered=0;
  for(uint32_t y=0;y<kSize;++y) for(uint32_t x=0;x<kSize;++x) covered+=green_at(whole,x,y);
  Check(covered==32*32 && green_at(whole,8,4) && green_at(whole,39,35) && !green_at(whole,40,35) && !green_at(whole,39,36),
        "whole-pixel viewport: coverage changed ("+std::to_string(covered)+" pixels)");
  CheckClean(*backend,"scissor rounding");
  std::cout << "scissor rounding: " << compared << " fractional viewports clip only to themselves, whole-pixel unchanged\n";
}

// ---------------------------------------------------------------------------
// The loader never fails the caller: no DLL, a file that is not a DLL, and
// (when CMake staged one next to this test) the real DLL's providers.
void TestFfxLoader() {
  {
    NativeFfxLibrary missing(std::filesystem::path(L"Z:\\no\\such\\directory")/L"amd_fidelityfx_dx12.dll");
    Check(!missing.available() && !missing.error().empty(),"a missing FFX DLL reported available");
    Check(missing.Versions(0x00010000u).empty() && !missing.upscaler_available() &&
          !missing.frame_generation_available(),"a missing FFX DLL reported providers");
    bool threw=false;
    try { missing.functions(); } catch(const std::exception&) { threw=true; }
    Check(threw,"functions() of a missing FFX DLL did not throw");
    Check(missing.Describe().find("unavailable")!=std::string::npos,"Describe() of a missing DLL: "+missing.Describe());
    std::cout << "ffx missing: " << missing.Describe() << '\n';
  }
  {
    const auto bogus=std::filesystem::temp_directory_path()/L"edf_ffx_not_a_dll.dll";
    { std::ofstream(bogus,std::ios::binary) << "not a portable executable"; }
    {
      NativeFfxLibrary broken(bogus);
      Check(!broken.available() && !broken.error().empty(),"a file that is not a DLL loaded as FFX");
      std::cout << "ffx broken: " << broken.Describe() << '\n';
    }
    std::error_code ignored; std::filesystem::remove(bogus,ignored);
  }
  const auto staged=NativeFfxLibrary::DefaultPath();
  std::error_code ignored;
  if(!std::filesystem::exists(staged,ignored)) {
    std::cout << "ffx staged: none next to the test (" << staged.string() << "), skipped\n";
    return;
  }
  NativeFfxLibrary library;
  Check(library.available(),"the staged FFX DLL did not load: "+library.error());
  if(!library.available()) return;
  auto backend=Warp();
  // Both with and without a device: the null device path is what a caller
  // uses before its device exists.
  Check(library.upscaler_available(),"the staged FFX DLL offers no upscaler");
  Check(library.upscaler_available(backend->D3D12Raw()->Device()),"the staged FFX DLL offers no upscaler for WARP");
  std::cout << "ffx staged: " << library.Describe(backend->D3D12Raw()->Device()) << '\n';
}
}  // namespace

int main() {
  std::cout << std::unitbuf;
  try {
    TestFfxLoader();
    TestScissorRounding();
    TestDepthSrv();
    TestUnorderedAccess();
    TestRawAccess();
    TestRawOrdering(0,32,3,"direct");
    TestRawOrdering(2,32,3,"packets serial");
    TestRawOrdering(2,4,64,"packets parallel");
  } catch(const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  if(failures) { std::cerr << failures << " failure(s)\n"; return 1; }
  std::cout << "all FSR foundation checks passed\n";
  return 0;
}

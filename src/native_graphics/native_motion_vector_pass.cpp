#include "native_motion_vector_pass.h"
#include "native_scene_pass_inputs.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <functional>
#include <bit>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace edf::native {
namespace {
// A triangle covering the viewport, in clip space.
constexpr float kCoveringTriangle[]{-1.f,-1.f, 3.f,-1.f, -1.f,3.f};
const NativeBackendInputElement kTriangleLayout[]{{"POSITION",0,/*DXGI_FORMAT_R32G32_FLOAT*/16,0,0,false,0}};
constexpr uint64_t kIdBase=0x6d6f74696f6e0000ull;  // "motion"

constexpr char kFullscreenVertex[]=R"(
float4 VS(float2 position : POSITION) : SV_POSITION { return float4(position,0,1); }
)";
// Camera reprojection: current pixel -> previous UV, from the scene depth.
// Stored depth d is 1 - ndc z (reversed by z = w - z); 0 is the clear/sky,
// which reprojects through the rotation-only matrix as a direction.
constexpr char kCameraPixel[]=R"(
cbuffer Camera : register(b0) {
  row_major float4x4 g_geometry;  // current clip -> previous clip
  row_major float4x4 g_sky;       // the same, rotation only
  float4 g_viewport;              // x, y, 1/width, 1/height
};
Texture2D<float> g_depth : register(t0);
float2 PS(float4 position : SV_POSITION) : SV_TARGET {
  const float depth=g_depth.Load(int3(position.xy,0));
  const float2 uv=(position.xy-g_viewport.xy)*g_viewport.zw;
  const float4 clip=float4(uv.x*2-1,1-uv.y*2,1-depth,1);
  const float4 previous=mul(clip,depth>0?g_geometry:g_sky);
  if(!(previous.w>1e-6)) return float2(0,0);
  return previous.xy/previous.w*float2(0.5,-0.5)+0.5-uv;
}
)";
// Velocity: the draw's previous clip position, interpolated, to UV; the
// current UV from the pixel itself (as the camera pass takes it).
constexpr char kVelocityPixel[]=R"(
cbuffer Velocity : register(b0) { float4 g_viewport; };  // x, y, 1/width, 1/height
float2 PS(float4 position : SV_POSITION,float3 previous : TEXCOORD0) : SV_TARGET {
  const float2 uv=(position.xy-g_viewport.xy)*g_viewport.zw;
  if(!(previous.z>1e-6)) return float2(0,0);
  return previous.xy/previous.z*float2(0.5,-0.5)+0.5-uv;
}
)";
// Kinds by define: SKINNED (INDEX_TYPE, WEIGHTED), INSTANCED, else rigid.
constexpr char kVelocityVertex[]=R"(
cbuffer Motion : register(b0) {
  row_major float4x4 g_vp;
  row_major float4x4 g_previous_vp;
  row_major float4x4 g_world;
  row_major float4x4 g_previous_world;
  float4 g_params;  // x: reversed depth scale (1 + bias)
};
#ifdef SKINNED
cbuffer Palette : register(b1) {
  float4x3 g_palette[68];
  float4x3 g_previous_palette[68];
};
#endif
struct Input {
  float3 position : POSITION;
#ifdef SKINNED
  INDEX_TYPE indices : BLENDINDICES;
#ifdef WEIGHTED
  float4 weights : BLENDWEIGHT;
#endif
#endif
#ifdef INSTANCED
  float4 w0 : EDFMOTION0; float4 w1 : EDFMOTION1; float4 w2 : EDFMOTION2; float4 w3 : EDFMOTION3;
  float4 p0 : EDFMOTION4; float4 p1 : EDFMOTION5; float4 p2 : EDFMOTION6; float4 p3 : EDFMOTION7;
#endif
};
struct Output {
  float4 position : SV_POSITION;
  float3 previous : TEXCOORD0;
};
Output VS(Input input) {
  const float4 p=float4(input.position,1);
  float3 current,previous;
#ifdef SKINNED
  const int4 index=(int4)input.indices;
#ifdef WEIGHTED
  current=0; previous=0;
  [unroll] for(int i=0;i<4;++i) {
    current+=mul(p,g_palette[index[i]])*input.weights[i];
    previous+=mul(p,g_previous_palette[index[i]])*input.weights[i];
  }
#else
  current=mul(p,g_palette[index[0]]);
  previous=mul(p,g_previous_palette[index[0]]);
#endif
#elif defined(INSTANCED)
  current=mul(p,float4x4(input.w0,input.w1,input.w2,input.w3)).xyz;
  previous=mul(p,float4x4(input.p0,input.p1,input.p2,input.p3)).xyz;
#else
  current=mul(p,g_world).xyz;
  previous=mul(p,g_previous_world).xyz;
#endif
  const float4 clip=mul(float4(current,1),g_vp);
  const float4 before=mul(float4(previous,1),g_previous_vp);
  Output output;
  output.position=float4(clip.x,clip.y,(clip.w-clip.z)*g_params.x,clip.w);
  output.previous=before.xyw;
  return output;
}
)";
// Debug: mode 1 motion as colour, mode 2 the history-valid mask.
constexpr char kDebugPixel[]=R"(
cbuffer Debug : register(b0) {
  float4 g_scale;  // xy: render size / output size; zw: render size
  float4 g_mode;   // x: mode, y: reset
};
Texture2D<float2> g_motion : register(t0);
float4 PS(float4 position : SV_POSITION) : SV_TARGET {
  const int2 texel=int2(position.xy*g_scale.xy);
  const float2 motion=g_motion.Load(int3(texel,0));
  if(g_mode.x<1.5) {
    const float2 pixels=motion*g_scale.zw/16;
    return float4(saturate(0.5+pixels.x),saturate(0.5+pixels.y),0.5,1);
  }
  const float2 previous=(texel+0.5)/g_scale.zw+motion;
  const bool valid=g_mode.y<0.5 && all(previous>=0) && all(previous<=1);
  return valid?float4(0,0.6,0,1):float4(0.7,0,0,1);
}
)";
Microsoft::WRL::ComPtr<ID3DBlob> Compile(std::string_view source,const char* entry,const char* profile,
    const D3D_SHADER_MACRO* defines=nullptr) {
  Microsoft::WRL::ComPtr<ID3DBlob> code,errors;
  if(FAILED(D3DCompile(source.data(),source.size(),"native-motion-vectors",defines,nullptr,entry,profile,
                       D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors)))
    throw std::runtime_error(std::string("native motion vector shader failed: ")+
      (errors?static_cast<const char*>(errors->GetBufferPointer()):"unknown"));
  return code;
}
std::span<const uint8_t> Bytes(ID3DBlob& blob) {
  return {static_cast<const uint8_t*>(blob.GetBufferPointer()),blob.GetBufferSize()};
}
template<class T> std::span<const uint8_t> Raw(const T& value) {
  return {reinterpret_cast<const uint8_t*>(&value),sizeof(value)};
}
std::array<float,16> Floats(const NativeMotionMatrix& m) {
  std::array<float,16> r{};
  for(size_t i=0;i<16;++i) r[i]=float(m[i]);
  return r;
}
const NativeBackendInputElement* Find(const NativeOwnedInputLayout& layout,const char* semantic) {
  for(const auto& element:layout.elements())
    if(!_stricmp(element.semantic,semantic) && element.semantic_index==0 && element.slot==0) return &element;
  return nullptr;
}
struct CameraConstants {
  std::array<float,16> geometry{},sky{};
  std::array<float,4> viewport{};
};
struct VelocityConstants {
  std::array<float,16> vp{},previous_vp{},world{},previous_world{};
  std::array<float,4> params{};
};
struct PaletteConstants {
  std::array<float,kNativeMotionPaletteBones*12> palette{},previous{};
};
struct DebugConstants {
  std::array<float,4> scale{},mode{};
};
}  // namespace

NativeMotionSkinning NativeMotionSkinningOf(const NativeShader& shader) {
  NativeMotionSkinning result;
  if(!shader.reflection) return result;
  D3D11_SHADER_DESC description{};
  if(FAILED(shader.reflection->GetDesc(&description))) return result;
  for(UINT i=0;i<description.InputParameters;++i) {
    D3D11_SIGNATURE_PARAMETER_DESC input{};
    if(FAILED(shader.reflection->GetInputParameterDesc(i,&input)) || !input.ReadWriteMask || input.SemanticIndex) continue;
    if(!_stricmp(input.SemanticName,"BLENDINDICES")) result.indices=true;
    else if(!_stricmp(input.SemanticName,"BLENDWEIGHT")) result.weights=true;
  }
  return result;
}
std::array<float,16> NativeMotionWorldOf(const std::array<uint8_t,64>& registers) {
  std::array<float,16> world{};
  for(size_t c=0;c<4;++c) for(size_t r=0;r<4;++r) {
    const auto* at=registers.data()+(c*4+r)*4;
    world[r*4+c]=std::bit_cast<float>((uint32_t(at[0])<<24)|(uint32_t(at[1])<<16)|(uint32_t(at[2])<<8)|at[3]);
  }
  return world;
}

struct NativeMotionVectors::Resources {
  std::shared_ptr<NativeBackendRenderTarget> target;
  uint32_t width=0,height=0;
  std::unique_ptr<NativeBackendBuffer> triangle;
  Microsoft::WRL::ComPtr<ID3DBlob> fullscreen,camera_pixel,velocity_pixel,debug_pixel;
  NativeBackendPipeline* camera=nullptr;
  std::map<uint32_t,NativeBackendPipeline*> debug;
  // Vertex shader per (kind, index type, weighted); pipeline per those, the
  // geometry's layout and the depth format.
  std::map<std::tuple<int,int,bool>,Microsoft::WRL::ComPtr<ID3DBlob>> velocity_vertex;
  std::map<std::tuple<int,int,bool,uint64_t,uint32_t>,NativeBackendPipeline*> velocity;
};

NativeMotionVectors::NativeMotionVectors()=default;
NativeMotionVectors::~NativeMotionVectors() { delete resources_; }
NativeBackendRenderTarget* NativeMotionVectors::target() const { return resources_?resources_->target.get():nullptr; }

void NativeMotionVectors::Bind(NativeRenderBackend& backend) {
  if(backend_==&backend && resources_) return;
  // The old backend may already be destroyed: its objects are not released.
  resources_=new Resources;
  backend_=&backend;
  history_.Reset();
  auto& r=*resources_;
  NativeBackendBufferDesc vertices{};
  vertices.bytes=sizeof(kCoveringTriangle); vertices.vertex=true;
  r.triangle=backend.CreateBuffer(vertices,Raw(kCoveringTriangle));
  if(!r.triangle) throw std::runtime_error("native motion vector triangle creation failed");
  r.fullscreen=Compile(kFullscreenVertex,"VS","vs_5_0");
  r.camera_pixel=Compile(kCameraPixel,"PS","ps_5_0");
  r.velocity_pixel=Compile(kVelocityPixel,"PS","ps_5_0");
  r.debug_pixel=Compile(kDebugPixel,"PS","ps_5_0");
}
NativeBackendPipeline& NativeMotionVectors::CameraPipeline() {
  auto& r=*resources_;
  if(r.camera) return *r.camera;
  NativeBackendPipelineDesc desc{};
  desc.vertex=Bytes(*r.fullscreen.Get()); desc.pixel=Bytes(*r.camera_pixel.Get());
  desc.vertex_id=kIdBase+1; desc.pixel_id=kIdBase+2;
  desc.input_layout=kTriangleLayout; desc.input_layout_id=kIdBase+3;
  desc.state=kNativeOpaqueCopyState;
  desc.topology=NativeBackendTopology::TriangleList;
  desc.render_targets=1; desc.rtv_format[0]=kNativeMotionFormat;
  r.camera=&backend_->CreatePipeline(desc);
  return *r.camera;
}
NativeBackendPipeline* NativeMotionVectors::VelocityPipeline(NativeMotionVelocityKind kind,const NativeMotionVelocityDraw& draw,
    uint32_t depth_format,bool& weighted) {
  auto& r=*resources_;
  const auto& layout=draw.geometry->input_layout();
  if(!Find(layout,"POSITION")) return nullptr;
  int index_type=0;  // 0 float4, 1 int4, 2 uint4
  weighted=false;
  if(kind==NativeMotionVelocityKind::Skinned) {
    const auto* indices=Find(layout,"BLENDINDICES");
    if(!indices) return nullptr;
    index_type=indices->format==/*R32G32B32A32_SINT*/4?1:indices->format==/*R32G32B32A32_UINT*/3?2:0;
    weighted=draw.weighted && Find(layout,"BLENDWEIGHT");
  }
  NativeOwnedInputLayout combined;
  for(const auto& element:layout.elements()) combined.Add(element.semantic,element.semantic_index,element.format,element.slot,
    element.offset,element.per_instance,element.step_rate);
  if(kind==NativeMotionVelocityKind::Instanced)
    for(uint32_t row=0;row<8;++row) combined.Add("EDFMOTION",row,/*R32G32B32A32_FLOAT*/2,15,row*16,true,1);
  const auto fingerprint=combined.fingerprint();
  const auto key=std::make_tuple(int(kind),index_type,weighted,fingerprint,depth_format);
  if(const auto found=r.velocity.find(key);found!=r.velocity.end()) return found->second;
  auto& vertex=r.velocity_vertex[{int(kind),index_type,weighted}];
  if(!vertex) {
    std::vector<D3D_SHADER_MACRO> defines;
    static constexpr const char* kIndexTypes[]{"float4","int4","uint4"};
    if(kind==NativeMotionVelocityKind::Skinned) {
      defines.push_back({"SKINNED","1"});
      defines.push_back({"INDEX_TYPE",kIndexTypes[index_type]});
      if(weighted) defines.push_back({"WEIGHTED","1"});
    } else if(kind==NativeMotionVelocityKind::Instanced) defines.push_back({"INSTANCED","1"});
    defines.push_back({nullptr,nullptr});
    vertex=Compile(kVelocityVertex,"VS","vs_5_0",defines.data());
  }
  NativeBackendPipelineDesc desc{};
  desc.vertex=Bytes(*vertex.Get()); desc.pixel=Bytes(*r.velocity_pixel.Get());
  desc.vertex_id=kIdBase+0x100+uint64_t(kind)*16+uint64_t(index_type)*2+(weighted?1:0);
  desc.pixel_id=kIdBase+4;
  desc.input_layout=combined.elements(); desc.input_layout_id=fingerprint;
  // Depth test GREATER_EQUAL (func 7, guest encoding 6<<4), no depth write,
  // no culling, no blend: the reversed-Z scene's nearest surface passes.
  desc.state={0x10001,0x62,0,0,15,0};
  desc.topology=NativeBackendTopology::TriangleList;
  desc.render_targets=1; desc.rtv_format[0]=kNativeMotionFormat; desc.dsv_format=depth_format;
  auto* pipeline=&backend_->CreatePipeline(desc);
  r.velocity.emplace(key,pipeline);
  return pipeline;
}
void NativeMotionVectors::RecordVelocity(NativeBackendRecorder& recorder,const std::vector<NativeMotionVelocityDraw>& draws,
    const NativeMotionCamera& current,const NativeMotionCamera& previous,uint32_t depth_format,
    const NativeBackendViewport& viewport) {
  VelocityConstants constants;
  constants.vp=Floats(current.ViewProjection());
  constants.previous_vp=Floats(previous.ViewProjection());
  constants.params={1.f+kNativeMotionDepthBias,0,0,0};
  const std::array<float,4> pixel{viewport.x,viewport.y,1.f/viewport.width,1.f/viewport.height};
  recorder.SetWorldInstancing(false);
  recorder.SetViewport(viewport);
  recorder.SetScissor({},false);
  recorder.SetConstants(NativeBackendStage::Pixel,0,Raw(pixel));
  NativeBackendPipeline* bound=nullptr;
  auto palette=std::make_unique<PaletteConstants>();
  std::vector<float> instances;
  // Rigid and skinned in the models pass's order, then the instanced ones by
  // geometry, so the instances of one geometry draw together.
  std::vector<const NativeMotionVelocityDraw*> order;
  order.reserve(draws.size());
  for(const auto& draw:draws) if(draw.kind!=NativeMotionVelocityKind::Instanced) order.push_back(&draw);
  const auto first_instanced=order.size();
  for(const auto& draw:draws) if(draw.kind==NativeMotionVelocityKind::Instanced) order.push_back(&draw);
  std::stable_sort(order.begin()+ptrdiff_t(first_instanced),order.end(),[](const auto* a,const auto* b) {
    return std::less<const void*>()(a->geometry.get(),b->geometry.get());
  });
  for(size_t i=0;i<order.size();) {
    const auto& draw=*order[i];
    bool weighted=false;
    auto* pipeline=draw.geometry?VelocityPipeline(draw.kind,draw,depth_format,weighted):nullptr;
    if(!pipeline) { ++stats_.velocity_skipped; ++i; continue; }
    if(pipeline!=bound) { recorder.SetPipeline(*pipeline); bound=pipeline; }
    if(draw.kind==NativeMotionVelocityKind::Instanced) {
      // A run of instances of the same geometry: one instanced draw.
      instances.clear();
      size_t end=i;
      while(end<order.size() && end-i<1024 && order[end]->geometry==draw.geometry) {
        instances.insert(instances.end(),order[end]->world.begin(),order[end]->world.end());
        instances.insert(instances.end(),order[end]->previous_world.begin(),order[end]->previous_world.end());
        ++end;
      }
      recorder.SetConstants(NativeBackendStage::Vertex,0,Raw(constants));
      recorder.SetTransientVertices(15,{reinterpret_cast<const uint8_t*>(instances.data()),instances.size()*sizeof(float)},128);
      draw.geometry->DrawInstanced(recorder,uint32_t(end-i));
      ++stats_.velocity_draws; stats_.velocity_instances+=end-i;
      i=end;
      continue;
    }
    if(draw.kind==NativeMotionVelocityKind::Skinned) {
      if(!draw.palette || !draw.previous_palette || draw.palette->size()!=draw.previous_palette->size()) {
        ++stats_.velocity_skipped; ++i; continue;
      }
      palette->palette.fill(0); palette->previous.fill(0);
      const auto count=(std::min)(draw.palette->size(),palette->palette.size());
      std::copy_n(draw.palette->begin(),count,palette->palette.begin());
      std::copy_n(draw.previous_palette->begin(),count,palette->previous.begin());
      recorder.SetConstants(NativeBackendStage::Vertex,1,Raw(*palette));
    } else {
      constants.world=draw.world; constants.previous_world=draw.previous_world;
    }
    recorder.SetConstants(NativeBackendStage::Vertex,0,Raw(constants));
    draw.geometry->Draw(recorder);
    ++stats_.velocity_draws;
    ++i;
  }
}
NativeMotionVectorOutput RecordNativeMotionVectors(NativeMotionVectors& state,const NativeMotionVectorContext& context) {
  if(!context.backend || !context.recorder || !context.camera)
    throw std::runtime_error("native motion vectors need a backend, a recorder and a camera");
  NativeMotionVectorOutput output;
  const NativeMotionCamera camera{NativeMotionFloats(context.camera->view),NativeMotionFloats(context.camera->projection)};
  output.view_projection=Floats(camera.ViewProjection());
  output.near_plane=camera.Near(); output.far_plane=camera.Far(); output.fov_y=camera.FovY();
  auto* depth=context.depth?context.depth->texture():nullptr;
  const auto& v=context.viewport;
  if(!depth || !context.width || !context.height || !(v.width>0) || !(v.height>0)) {
    ++state.stats_.declined;
    return output;
  }
  state.Bind(*context.backend);
  auto& r=*state.resources_;
  bool resized=false;
  if(!r.target || r.width!=context.width || r.height!=context.height) {
    NativeBackendTextureDesc desc{};
    desc.width=context.width; desc.height=context.height; desc.format=kNativeMotionFormat;
    desc.render_target=true; desc.sampled=true;
    r.target=context.backend->CreateRenderTarget(desc);
    if(!r.target || !r.target->texture()) throw std::runtime_error("native motion vector target creation failed");
    r.width=context.width; r.height=context.height;
    resized=true;
  }
  ++state.stats_.records;
  NativeMotionFrameCamera frame{camera,context.frame,
    {uint32_t(v.x),uint32_t(v.y),uint32_t(v.width),uint32_t(v.height)},context.width,context.height};
  const auto history=state.history_.Advance(context.scene,frame);
  auto reset=history.reset;
  if(resized && reset==NativeMotionReset::None) reset=NativeMotionReset::Size;
  std::optional<NativeMotionReprojection> reprojection;
  if(reset==NativeMotionReset::None) {
    reprojection=NativeMotionReprojectionOf(camera,history.previous.camera);
    if(!reprojection) reset=NativeMotionReset::Invalid;
  }
  state.last_reset_=reset;
  ++state.stats_.reasons[size_t(reset)];
  if(reset!=NativeMotionReset::None) ++state.stats_.resets;
  auto& recorder=*context.recorder;
  NativeBackendRenderTarget* colors[]{r.target.get()};
  recorder.SetRenderTargets(colors,nullptr);
  const bool whole=v.x==0 && v.y==0 && uint32_t(v.width)==context.width && uint32_t(v.height)==context.height;
  if(reset!=NativeMotionReset::None || !whole) recorder.ClearColor(*r.target,{0,0,0,0});
  if(reset==NativeMotionReset::None) {
    CameraConstants constants;
    constants.geometry=Floats(reprojection->geometry);
    constants.sky=Floats(reprojection->sky);
    constants.viewport={v.x,v.y,1.f/v.width,1.f/v.height};
    recorder.SetWorldInstancing(false);
    recorder.SetViewport({v.x,v.y,v.width,v.height,0,1});
    recorder.SetScissor({},false);
    recorder.SetPipeline(state.CameraPipeline());
    recorder.SetConstants(NativeBackendStage::Pixel,0,Raw(constants));
    recorder.SetTexture(NativeBackendStage::Pixel,0,depth);
    recorder.SetVertexBuffer(0,*r.triangle,8,0);
    recorder.SetTopology(NativeBackendTopology::TriangleList);
    recorder.Draw(3,0);
    recorder.SetTexture(NativeBackendStage::Pixel,0,nullptr);
    if(context.velocity && !context.velocity->empty()) {
      recorder.SetRenderTargets(colors,context.depth);
      state.RecordVelocity(recorder,*context.velocity,camera,history.previous.camera,context.depth_format,
        {v.x,v.y,v.width,v.height,0,1});
    }
  }
  output.motion=r.target->texture();
  output.reset=reset!=NativeMotionReset::None;
  return output;
}
void NativeMotionVectors::RecordDebug(NativeRenderBackend& backend,NativeBackendRecorder& recorder,NativeBackendRenderTarget& output,
    uint32_t output_format,int mode,const NativeMotionVectorOutput& motion) {
  if(mode<1 || !motion.motion || !resources_ || backend_!=&backend || motion.motion!=resources_->target->texture()) return;
  auto& r=*resources_;
  auto& pipeline=r.debug[output_format];
  if(!pipeline) {
    NativeBackendPipelineDesc desc{};
    desc.vertex=Bytes(*r.fullscreen.Get()); desc.pixel=Bytes(*r.debug_pixel.Get());
    desc.vertex_id=kIdBase+1; desc.pixel_id=kIdBase+5;
    desc.input_layout=kTriangleLayout; desc.input_layout_id=kIdBase+3;
    desc.state=kNativeOpaqueCopyState;
    desc.topology=NativeBackendTopology::TriangleList;
    desc.render_targets=1; desc.rtv_format[0]=output_format;
    pipeline=&backend.CreatePipeline(desc);
  }
  DebugConstants constants;
  constants.scale={float(r.width)/float(output.width()),float(r.height)/float(output.height()),float(r.width),float(r.height)};
  constants.mode={float(mode),motion.reset?1.f:0.f,0,0};
  NativeBackendRenderTarget* colors[]{&output};
  recorder.SetRenderTargets(colors,nullptr);
  recorder.SetWorldInstancing(false);
  recorder.SetViewport({0,0,float(output.width()),float(output.height()),0,1});
  recorder.SetScissor({},false);
  recorder.SetPipeline(*pipeline);
  recorder.SetConstants(NativeBackendStage::Pixel,0,Raw(constants));
  recorder.SetTexture(NativeBackendStage::Pixel,0,motion.motion);
  recorder.SetVertexBuffer(0,*r.triangle,8,0);
  recorder.SetTopology(NativeBackendTopology::TriangleList);
  recorder.Draw(3,0);
  recorder.SetTexture(NativeBackendStage::Pixel,0,nullptr);
}
}  // namespace edf::native

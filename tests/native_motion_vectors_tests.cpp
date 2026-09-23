// Motion vectors (edf_native_motion_vectors, native_motion_vectors.h) on the
// D3D12 backend, on WARP with the debug layer:
//  - the history rules (first frame, gap, viewport, size, cut, a second view
//    of the same frame) and the reprojection matrices, on the CPU;
//  - the camera pass: a static world plane and the sky under a camera pan
//    (translation and yaw), every pixel against an independent reference
//    (ray-plane intersection projected with the previous camera; the sky as a
//    direction through the previous rotation), and the reset/resize rules;
//  - the velocity pass: a rigid quad moved by a known offset, two instances
//    of one geometry, and a skinned quad (blend indices and weights, a decoy
//    bone) whose motion only the previous palette explains;
//  - the debug view's two modes.
// --reprojection-error: the camera pass over a 16-frame pan and orbit, the
// per-pixel error against the analytic reference in pixels (mean, max,
// share over 0.05 px), printed per frame; a tool mode, not a check.
#include "native_graphics/d3d12_backend.h"
#include "native_graphics/effect.h"
#include "native_graphics/native_model_pass.h"
#include "native_graphics/native_motion_history.h"
#include "native_graphics/native_motion_vector_pass.h"
#include "native_graphics/native_motion_vectors.h"
#include "native_graphics/native_render_backend.h"
#include "native_graphics/native_scene_pass_inputs.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
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
constexpr uint32_t kSize=64;
constexpr double kPi=3.14159265358979323846;
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
std::shared_ptr<NativeRenderBackend> Warp() {
  NativeD3D12Options options;
  options.prefer_warp=true;
  options.debug_layer=true;
  return std::shared_ptr<NativeRenderBackend>(CreateNativeD3D12Backend(options));
}
void CheckClean(NativeRenderBackend& backend,const std::string& what) {
  const auto messages=backend.DrainValidationMessages();
  for(const auto& message:messages) std::cerr << "  validation (" << what << "): " << message << '\n';
  Check(messages.empty(),what+": the debug layer reported "+std::to_string(messages.size())+" message(s)");
}

// A camera at `position` turned `yaw` radians about +y (0 looks down +z),
// D3D perspective (row vectors), square target.
NativeMotionCamera MakeCamera(std::array<double,3> position,double yaw,double fov=kPi/2,double near_plane=1,double far_plane=1000) {
  const std::array<double,3> right{std::cos(yaw),0,-std::sin(yaw)},up{0,1,0},forward{std::sin(yaw),0,std::cos(yaw)};
  const std::array<std::array<double,3>,3> axes{right,up,forward};
  NativeMotionCamera camera;
  for(size_t i=0;i<3;++i) for(size_t j=0;j<3;++j) camera.view[i*4+j]=float(axes[j][i]);
  for(size_t j=0;j<3;++j)
    camera.view[12+j]=float(-(position[0]*axes[j][0]+position[1]*axes[j][1]+position[2]*axes[j][2]));
  camera.view[15]=1;
  const double scale=1/std::tan(fov/2),q=far_plane/(far_plane-near_plane);
  camera.projection={float(scale),0,0,0, 0,float(scale),0,0, 0,0,float(q),1, 0,0,float(-near_plane*q),0};
  return camera;
}
NativeScenePassCamera PassCamera(const NativeMotionCamera& camera) {
  NativeScenePassCamera pass;
  const auto vp=camera.ViewProjection();
  for(size_t i=0;i<16;++i) {
    pass.view[i]=std::bit_cast<uint32_t>(camera.view[i]);
    pass.projection[i]=std::bit_cast<uint32_t>(camera.projection[i]);
    pass.view_projection[i]=std::bit_cast<uint32_t>(float(vp[i]));
  }
  return pass;
}
// Camera position and world-space ray direction of a viewport UV (pixel centre).
struct Ray { std::array<double,3> origin,direction; };
Ray RayOf(const NativeMotionCamera& camera,double u,double v) {
  const auto inverse=*NativeMotionInverse(NativeMotionMatrixOf(camera.view));
  const double x=(u*2-1)/camera.projection[0],y=(1-v*2)/camera.projection[5];
  Ray ray;
  for(size_t j=0;j<3;++j) {
    ray.origin[j]=inverse[12+j];
    ray.direction[j]=x*inverse[0*4+j]+y*inverse[1*4+j]+1*inverse[2*4+j];
  }
  return ray;
}
// Where a direction (w = 0) projects under a camera's rotation.
std::optional<std::array<double,2>> ProjectDirection(const NativeMotionCamera& camera,const std::array<double,3>& d) {
  std::array<double,3> view{};
  for(size_t j=0;j<3;++j) for(size_t k=0;k<3;++k) view[j]+=d[k]*camera.view[k*4+j];
  if(!(view[2]>1e-9)) return std::nullopt;
  return std::array<double,2>{view[0]*camera.projection[0]/view[2]*0.5+0.5,0.5-view[1]*camera.projection[5]/view[2]*0.5};
}

// A frame's targets: colour (unused but bound), the reversed-Z sampled depth.
struct Targets {
  std::unique_ptr<NativeBackendRenderTarget> color,depth;
  explicit Targets(NativeRenderBackend& backend) {
    NativeBackendTextureDesc c{};
    c.width=c.height=kSize; c.format=DXGI_FORMAT_R8G8B8A8_UNORM; c.render_target=true;
    color=backend.CreateRenderTarget(c);
    NativeBackendTextureDesc d{};
    d.width=d.height=kSize; d.format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT; d.depth=true; d.render_target=true;
    d.sampled=true; d.clear_depth=0.0f;
    depth=backend.CreateRenderTarget(d);
    if(!color || !depth || !depth->texture()) throw std::runtime_error("targets");
  }
};
// Depth writer: world triangles (or a retained geometry with a combined
// world * view-projection), reversed-Z (z = w - z), GREATER_EQUAL, writing depth.
constexpr char kDepthShader[]=R"(
cbuffer V : register(b0) { row_major float4x4 g_matrix; };
float4 VS(float3 p : POSITION) : SV_POSITION { const float4 c=mul(float4(p,1),g_matrix); return float4(c.x,c.y,c.w-c.z,c.w); }
float4 PS(float4 p : SV_POSITION) : SV_TARGET { return float4(1,1,1,1); }
)";
NativeBackendPipeline& DepthPipeline(NativeRenderBackend& backend,std::span<const NativeBackendInputElement> layout,uint64_t layout_id) {
  static const auto vs=Compile(kDepthShader,"VS","vs_5_0"),ps=Compile(kDepthShader,"PS","ps_5_0");
  NativeBackendPipelineDesc desc{};
  desc.vertex=Bytes(*vs.Get()); desc.pixel=Bytes(*ps.Get());
  desc.vertex_id=0x7001; desc.pixel_id=0x7002;
  desc.input_layout=layout; desc.input_layout_id=layout_id;
  desc.state={0x10001,0x66,0,0,15,0};  // depth test GREATER_EQUAL, write
  desc.topology=NativeBackendTopology::TriangleList;
  desc.render_targets=1; desc.rtv_format[0]=DXGI_FORMAT_R8G8B8A8_UNORM; desc.dsv_format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  return backend.CreatePipeline(desc);
}
const NativeBackendInputElement kWorldLayout[]{{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,false,0}};
std::array<float,16> Floats(const NativeMotionMatrix& m) {
  std::array<float,16> r{};
  for(size_t i=0;i<16;++i) r[i]=float(m[i]);
  return r;
}
void BeginScene(NativeRenderBackend& backend,Targets& targets) {
  auto& recorder=backend.Recorder();
  NativeBackendRenderTarget* colors[]{targets.color.get()};
  recorder.SetRenderTargets(colors,targets.depth.get());
  recorder.SetViewport({0,0,float(kSize),float(kSize),0,1});
  recorder.SetScissor({},false);
  recorder.ClearColor(*targets.color,{0,0,0,1});
  recorder.ClearDepthStencil(*targets.depth,true,true,0.0f,0);
}
// World triangles with the camera's view-projection.
void DrawWorld(NativeRenderBackend& backend,const NativeMotionCamera& camera,std::span<const float> triangles) {
  auto& recorder=backend.Recorder();
  recorder.SetPipeline(DepthPipeline(backend,kWorldLayout,0x7003));
  const auto matrix=Floats(camera.ViewProjection());
  recorder.SetConstants(NativeBackendStage::Vertex,0,Raw(matrix));
  recorder.SetTransientVertices(0,{reinterpret_cast<const uint8_t*>(triangles.data()),triangles.size()*4},12);
  recorder.SetTopology(NativeBackendTopology::TriangleList);
  recorder.Draw(uint32_t(triangles.size()/3),0);
}
NativeMotionVectorContext Context(NativeRenderBackend& backend,Targets& targets,const NativeScenePassCamera& camera,uint64_t frame,
    const std::vector<NativeMotionVelocityDraw>* velocity=nullptr,uint32_t size=kSize) {
  NativeMotionVectorContext context;
  context.backend=&backend; context.recorder=&backend.Recorder();
  context.depth=targets.depth.get(); context.depth_format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  context.width=context.height=size;
  context.viewport={0,0,float(size),float(size),0,1};
  context.scene=0x1234; context.frame=frame; context.camera=&camera; context.velocity=velocity;
  return context;
}
struct Motion {
  std::vector<std::array<float,2>> uv;
  std::vector<float> depth;
  std::array<float,2> at(uint32_t x,uint32_t y) const { return uv[size_t(y)*kSize+x]; }
};
Motion Read(NativeRenderBackend& backend,NativeBackendTexture& motion,Targets& targets) {
  Motion result;
  const auto pixels=backend.ReadTexture(motion);
  if(pixels.size()!=size_t(kSize)*kSize*4) throw std::runtime_error("motion readback size");
  for(size_t i=0;i<size_t(kSize)*kSize;++i) {
    uint16_t x,y; std::memcpy(&x,pixels.data()+i*4,2); std::memcpy(&y,pixels.data()+i*4+2,2);
    result.uv.push_back({Half(x),Half(y)});
  }
  const auto depth=backend.ReadRenderTarget(*targets.depth);
  for(size_t i=0;i<size_t(kSize)*kSize;++i) { float d; std::memcpy(&d,depth.data()+i*4,4); result.depth.push_back(d); }
  return result;
}
// Pixels whose four neighbours are of the same class (geometry or sky): the
// ones whose coverage does not hang on a rasterization tie.
bool Interior(const Motion& motion,uint32_t x,uint32_t y) {
  if(x==0 || y==0 || x+1==kSize || y+1==kSize) return false;
  const bool here=motion.depth[size_t(y)*kSize+x]>0;
  for(const auto [dx,dy]:{std::pair{1,0},{-1,0},{0,1},{0,-1}})
    if((motion.depth[size_t(y+dy)*kSize+x+dx]>0)!=here) return false;
  return true;
}

// ---------------------------------------------------------------------------
void TestHistoryRules() {
  NativeMotionHistory history;
  const auto at=[](uint64_t frame,const NativeMotionCamera& camera,uint32_t width=kSize) {
    NativeMotionFrameCamera result;
    result.camera=camera; result.frame=frame; result.viewport={0,0,width,width}; result.width=result.height=width;
    return result;
  };
  const auto a=MakeCamera({0,0,0},0),b=MakeCamera({0.5,0,0},0.05);
  Check(history.Advance(1,at(1,a)).reset==NativeMotionReset::First,"a first render has no history");
  auto result=history.Advance(1,at(2,b));
  Check(result.reset==NativeMotionReset::None && result.previous.frame==1 && result.previous.camera==a,
        "the next frame's previous camera is the first frame's");
  // A second view of the same scene in the same frame answers against the same previous frame.
  result=history.Advance(1,at(2,b));
  Check(result.reset==NativeMotionReset::None && result.previous.frame==1,"a second record of a frame keeps its previous frame");
  Check(history.Advance(2,at(2,a)).reset==NativeMotionReset::First,"scenes have their own history");
  Check(history.Advance(1,at(4,b)).reset==NativeMotionReset::Gap,"a skipped frame resets");
  Check(history.Advance(1,at(5,b)).reset==NativeMotionReset::None,"and the frame after it is history again");
  auto viewport=at(6,b); viewport.viewport={0,0,kSize,kSize-1};
  Check(history.Advance(1,viewport).reset==NativeMotionReset::Viewport,"another viewport resets");
  auto size=at(7,b); size.viewport={0,0,kSize,kSize-1}; size.width=kSize*2;
  Check(history.Advance(1,size).reset==NativeMotionReset::Size,"another render size resets");
  history.Reset();
  Check(history.Advance(1,at(8,b)).reset==NativeMotionReset::First,"Reset forgets every scene");
  Check(history.Advance(1,at(9,MakeCamera({60.5,0,0},0.05))).reset==NativeMotionReset::Cut,"a 60-unit jump is a cut");
  Check(history.Advance(1,at(10,MakeCamera({60.5,0,49},0.05))).reset==NativeMotionReset::None,"a 49-unit move is not");
  Check(history.Advance(1,at(11,MakeCamera({60.5,0,49},0.05,kPi/2-0.3))).reset==NativeMotionReset::Cut,"a 0.3 rad zoom is a cut");
  Check(history.Advance(1,at(12,MakeCamera({60.5,0,49},0.05+1.7,kPi/2-0.3))).reset==NativeMotionReset::Cut,
        "a 97-degree turn is a cut");
  Check(history.Advance(1,at(13,MakeCamera({60.5,0,49},0.05+1.7+1.4,kPi/2-0.3))).reset==NativeMotionReset::None,
        "an 80-degree turn is not");
  NativeMotionCamera sheared=b; sheared.view[0]=2;
  Check(history.Advance(1,at(14,sheared)).reset==NativeMotionReset::Invalid,"a view that is not rigid is not history");
  // Camera parameters.
  const auto camera=MakeCamera({0,0,0},0,kPi/3,0.5,2000);
  Check(std::abs(camera.Near()-0.5f)<1e-4f && std::abs(camera.Far()-2000.f)<1.f && std::abs(camera.FovY()-float(kPi/3))<1e-5f,
        "near, far and fov from the projection");
}
// The CPU reprojection against independent references: a world point
// projected by both cameras, and a direction by both rotations.
void TestReprojectionMath() {
  const auto previous=MakeCamera({0,1,0},0),current=MakeCamera({0.7,1.2,0.3},0.08);
  const auto reprojection=*NativeMotionReprojectionOf(current,previous);
  double worst=0,worst_sky=0;
  for(const std::array<double,3> point:{std::array<double,3>{1,2,10},{-3,0,25},{0.5,-1,4},{8,3,60}}) {
    const auto now=*NativeMotionProjectUv(current,point),before=*NativeMotionProjectUv(previous,point);
    const auto clip=current.ViewProjection();
    double z=0,w=0;
    for(size_t k=0;k<3;++k) { z+=point[k]*clip[k*4+2]; w+=point[k]*clip[k*4+3]; }
    z+=clip[14]; w+=clip[15];
    const auto motion=*NativeMotionReprojectUv(reprojection,now[0],now[1],1-z/w);
    worst=(std::max)(worst,std::hypot(motion[0]-(before[0]-now[0]),motion[1]-(before[1]-now[1])));
  }
  for(const std::array<double,2> uv:{std::array<double,2>{0.5,0.5},{0.1,0.2},{0.9,0.7}}) {
    const auto ray=RayOf(current,uv[0],uv[1]);
    const auto before=*ProjectDirection(previous,ray.direction);
    const auto motion=*NativeMotionReprojectUv(reprojection,uv[0],uv[1],0);
    worst_sky=(std::max)(worst_sky,std::hypot(motion[0]-(before[0]-uv[0]),motion[1]-(before[1]-uv[1])));
  }
  Check(worst<1e-9,"geometry reprojection is off by "+std::to_string(worst));
  Check(worst_sky<1e-9,"sky reprojection is off by "+std::to_string(worst_sky));
  // A pure translation leaves the sky still.
  const auto moved=*NativeMotionReprojectionOf(MakeCamera({5,0,5},0.1),MakeCamera({0,0,0},0.1));
  const auto sky=*NativeMotionReprojectUv(moved,0.3,0.6,0);
  Check(std::abs(sky[0])<1e-12 && std::abs(sky[1])<1e-12,"a translated camera moves the sky");
  // Register images round-trip to the pose.
  const NativePoseMatrix pose{0,1,0,0, -1,0,0,0, 0,0,1,0, 3,4,5,1};
  const auto world=NativeMotionWorldOf(NativeModelWorldRegisters(pose));
  Check(world==pose,"a g_mWorld register image does not decode to its pose");
}

// The world: a wall at z = 10 over x <= 0, the rest sky.
std::vector<float> Wall() {
  return {-100,-100,10, 0,100,10, 0,-100,10,  -100,-100,10, -100,100,10, 0,100,10};
}
// Every interior pixel: the wall's point projected with the previous camera,
// the sky's direction through the previous rotation.
struct Error { double mean=0,max=0,over=0; size_t pixels=0,sky=0,wall=0; };
Error CameraError(const Motion& motion,const NativeMotionCamera& current,const NativeMotionCamera& previous) {
  Error error;
  size_t over=0;
  for(uint32_t y=0;y<kSize;++y) for(uint32_t x=0;x<kSize;++x) {
    if(!Interior(motion,x,y)) continue;
    const double u=(x+0.5)/kSize,v=(y+0.5)/kSize;
    const auto ray=RayOf(current,u,v);
    std::optional<std::array<double,2>> before;
    const bool wall=motion.depth[size_t(y)*kSize+x]>0;
    if(wall) {
      const double t=(10-ray.origin[2])/ray.direction[2];
      before=NativeMotionProjectUv(previous,{ray.origin[0]+t*ray.direction[0],ray.origin[1]+t*ray.direction[1],10});
    } else before=ProjectDirection(previous,ray.direction);
    if(!before) continue;
    const auto got=motion.at(x,y);
    const double e=std::hypot(got[0]-(before->at(0)-u),got[1]-(before->at(1)-v))*kSize;
    error.mean+=e; error.max=(std::max)(error.max,e); over+=e>0.05;
    ++error.pixels; ++(wall?error.wall:error.sky);
  }
  if(error.pixels) { error.mean/=double(error.pixels); error.over=double(over)/double(error.pixels); }
  return error;
}
void TestCameraPass() {
  auto backend=Warp();
  Targets targets(*backend);
  NativeMotionVectors motion;
  const auto wall=Wall();
  const auto first=MakeCamera({0,0.5,0},-0.2),second=MakeCamera({0.6,0.55,0.2},-0.14);
  // Frame 1: no history, all zero.
  backend->BeginFrame();
  BeginScene(*backend,targets);
  DrawWorld(*backend,first,wall);
  const auto first_pass=PassCamera(first);
  const auto one=RecordNativeMotionVectors(motion,Context(*backend,targets,first_pass,1));
  backend->Submit();
  Check(one.motion && one.reset,"the first frame resets");
  Check(std::abs(one.near_plane-1)<1e-4f && std::abs(one.far_plane-1000)<0.5f && std::abs(one.fov_y-float(kPi/2))<1e-5f,
        "the output's camera parameters");
  Check(one.view_projection==Floats(first.ViewProjection()),"the output's view-projection is the current camera's");
  const auto zero=Read(*backend,*one.motion,targets);
  bool all_zero=true;
  for(const auto& value:zero.uv) all_zero&=value[0]==0 && value[1]==0;
  Check(all_zero,"a reset frame's motion is zero");
  // Frame 2: panned and turned.
  backend->BeginFrame();
  BeginScene(*backend,targets);
  DrawWorld(*backend,second,wall);
  const auto second_pass=PassCamera(second);
  const auto two=RecordNativeMotionVectors(motion,Context(*backend,targets,second_pass,2));
  backend->Submit();
  Check(two.motion && !two.reset,"the second frame has history");
  const auto moved=Read(*backend,*two.motion,targets);
  const auto error=CameraError(moved,second,first);
  std::cout << "camera pass: pixels=" << error.pixels << " wall=" << error.wall << " sky=" << error.sky
            << " mean=" << error.mean << "px max=" << error.max << "px\n";
  Check(error.wall>500 && error.sky>500,"the pan covers both the wall and the sky");
  Check(error.max<0.02,"camera motion is off by up to "+std::to_string(error.max)+" px");
  // The sky moved (the camera turned) and the wall moved differently.
  Check(std::abs(moved.at(kSize-4,kSize/2)[0])>0.01f,"the turned camera's sky is still");
  // A cut: all zero again, reset.
  backend->BeginFrame();
  BeginScene(*backend,targets);
  DrawWorld(*backend,second,wall);
  const auto jumped=PassCamera(MakeCamera({80,0.55,0.2},-0.14));
  const auto three=RecordNativeMotionVectors(motion,Context(*backend,targets,jumped,3));
  backend->Submit();
  const auto cut=Read(*backend,*three.motion,targets);
  all_zero=true;
  for(const auto& value:cut.uv) all_zero&=value[0]==0 && value[1]==0;
  Check(three.reset && all_zero && motion.last_reset()==NativeMotionReset::Cut,"a cut resets to zero");
  // Without a sampled depth: declined, no texture.
  NativeBackendTextureDesc d{};
  d.width=d.height=kSize; d.format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT; d.depth=true; d.render_target=true; d.clear_depth=0;
  const auto typed=backend->CreateRenderTarget(d);
  backend->BeginFrame();
  auto context=Context(*backend,targets,second_pass,4);
  context.depth=typed.get();
  const auto declined=RecordNativeMotionVectors(motion,context);
  backend->Submit();
  Check(!declined.motion && declined.reset && motion.statistics().declined==1,"a depth without an SRV is declined");
  CheckClean(*backend,"camera pass");
}
// --reprojection-error: the camera pass over a pan and an orbit.
void ReprojectionErrorTool() {
  auto backend=Warp();
  Targets targets(*backend);
  NativeMotionVectors motion;
  const auto wall=Wall();
  std::optional<NativeMotionCamera> previous;
  std::cout << "frame,mean_px,max_px,over_0.05px,pixels\n";
  for(uint64_t frame=1;frame<=16;++frame) {
    const double t=double(frame);
    const auto camera=MakeCamera({0.15*t,0.5+0.02*std::sin(t),0.1*t},-0.3+0.02*t);
    const auto pass=PassCamera(camera);
    backend->BeginFrame();
    BeginScene(*backend,targets);
    DrawWorld(*backend,camera,wall);
    const auto output=RecordNativeMotionVectors(motion,Context(*backend,targets,pass,frame));
    backend->Submit();
    if(previous && !output.reset) {
      const auto error=CameraError(Read(*backend,*output.motion,targets),camera,*previous);
      std::cout << frame << ',' << error.mean << ',' << error.max << ',' << error.over << ',' << error.pixels << '\n';
    }
    previous=camera;
  }
}

// Geometry through the renderer's own mesh path: a quad in object space at
// z = 0 (x, y in [-half, half]); with `skinned` it carries BLENDINDICES (UBYTE4,
// all four = 1, read as int4) and BLENDWEIGHT (0.5, 0.5, 0, 0).
std::shared_ptr<const NativeIndexedMesh::RetainedDraw> Quad(const std::shared_ptr<NativeRenderBackend>& backend,bool skinned,
    float half=3) {
  Effect effect;
  effect.source=skinned?R"(
    float4x3 g_mWorldArray[8];
    row_major float4x4 g_mViewProjection;
    float4 VS(float3 position:POSITION0,int4 indices:BLENDINDICES0,float4 weights:BLENDWEIGHT0):SV_Position {
      float3 p=mul(float4(position,1),g_mWorldArray[indices.x])*weights.x+mul(float4(position,1),g_mWorldArray[indices.y])*weights.y;
      return mul(float4(p,1),g_mViewProjection);
    }
  )":R"(
    row_major float4x4 g_mWorld;
    row_major float4x4 g_mViewProjection;
    float4 VS(float3 position:POSITION0):SV_Position { return mul(mul(float4(position,1),g_mWorld),g_mViewProjection); }
  )";
  const auto shader=CompileNativeShader(nullptr,effect,{false,"VS","vs_3_0"},skinned?"motion-skinned.fx":"motion-rigid.fx");
  const auto word=[](std::vector<uint8_t>& bytes,size_t at,uint32_t value) {
    for(size_t i=0;i<4;++i) bytes[at+i]=uint8_t(value>>(24-8*i));
  };
  std::vector<uint8_t> declaration(skinned?36:12);
  word(declaration,4,0x2a23b9);  // POSITION float3 at 0
  uint32_t stride=12;
  if(skinned) {
    word(declaration,12,12); word(declaration,16,0x1a2286); declaration[21]=2;  // BLENDINDICES UBYTE4 at 12
    word(declaration,24,16); word(declaration,28,0x1a23a6); declaration[33]=1;  // BLENDWEIGHT float4 at 16
    stride=32;
  }
  const float corners[4][3]{{-half,-half,0},{-half,half,0},{half,half,0},{half,-half,0}};
  std::vector<uint8_t> vertices(size_t(stride)*4);
  for(size_t v=0;v<4;++v) {
    for(size_t i=0;i<3;++i) word(vertices,v*stride+i*4,std::bit_cast<uint32_t>(corners[v][i]));
    if(skinned) {
      word(vertices,v*stride+12,0x01010101);
      const float weights[4]{0.5f,0.5f,0,0};
      for(size_t i=0;i<4;++i) word(vertices,v*stride+16+i*4,std::bit_cast<uint32_t>(weights[i]));
    }
  }
  const std::vector<uint8_t> indices{0,0,0,1,0,2,0,0,0,2,0,3};
  NativeIndexedMesh mesh(*backend,shader,declaration,stride,vertices,indices,2);
  return std::make_shared<const NativeIndexedMesh::RetainedDraw>(mesh.RetainDraw(backend,0,6));
}
NativePoseMatrix Translation(float x,float y,float z) { return {1,0,0,0, 0,1,0,0, 0,0,1,0, x,y,z,1}; }
// The geometry into depth at a world transform.
void DrawGeometry(NativeRenderBackend& backend,const NativeMotionCamera& camera,const NativeIndexedMesh::RetainedDraw& geometry,
    const NativePoseMatrix& world) {
  auto& recorder=backend.Recorder();
  const auto& layout=geometry.input_layout();
  recorder.SetPipeline(DepthPipeline(backend,layout.elements(),layout.fingerprint()));
  const auto matrix=Floats(NativeMotionMultiply(NativeMotionMatrixOf(world),camera.ViewProjection()));
  recorder.SetConstants(NativeBackendStage::Vertex,0,Raw(matrix));
  geometry.Draw(recorder);
}
// Expected motion of a pixel on a plane z = depth (world) that moved by
// `offset` since the previous frame, under a still camera.
std::array<double,2> MovedPlane(const NativeMotionCamera& camera,uint32_t x,uint32_t y,double plane,std::array<double,3> offset) {
  const double u=(x+0.5)/kSize,v=(y+0.5)/kSize;
  const auto ray=RayOf(camera,u,v);
  const double t=(plane-ray.origin[2])/ray.direction[2];
  const auto before=*NativeMotionProjectUv(camera,{ray.origin[0]+t*ray.direction[0]-offset[0],
    ray.origin[1]+t*ray.direction[1]-offset[1],plane-offset[2]});
  return {before[0]-u,before[1]-v};
}
struct Region { double worst=0; size_t pixels=0,zero=0,background=0,background_nonzero=0; };
// Every interior pixel: on the object (depth > 0) against `expected`, off it
// zero. Zero is under a thousandth of a pixel: an identical camera's
// reprojection is the identity only to rounding.
template<class Expected> Region Compare(const Motion& motion,Expected&& expected) {
  Region region;
  for(uint32_t y=0;y<kSize;++y) for(uint32_t x=0;x<kSize;++x) {
    if(!Interior(motion,x,y)) continue;
    const auto got=motion.at(x,y);
    if(motion.depth[size_t(y)*kSize+x]>0) {
      const auto want=expected(x,y);
      region.worst=(std::max)(region.worst,std::hypot(got[0]-want[0],got[1]-want[1])*kSize);
      ++region.pixels; region.zero+=std::hypot(got[0],got[1])*kSize<1e-3;
    } else {
      ++region.background; region.background_nonzero+=std::hypot(got[0],got[1])*kSize>=1e-3;
    }
  }
  return region;
}
void TestVelocityPass() {
  auto backend=Warp();
  Targets targets(*backend);
  NativeMotionVectors motion;
  const auto camera=MakeCamera({0,0,0},0);
  const auto pass=PassCamera(camera);
  const auto rigid=Quad(backend,false),skinned=Quad(backend,true);
  // Frame 1 sets up history (reset, no velocity drawn).
  const auto frame=[&](uint64_t index,const std::vector<std::pair<const NativeIndexedMesh::RetainedDraw*,NativePoseMatrix>>& depth,
      const std::vector<NativeMotionVelocityDraw>& velocity) {
    backend->BeginFrame();
    BeginScene(*backend,targets);
    for(const auto& [geometry,world]:depth) DrawGeometry(*backend,camera,*geometry,world);
    const auto output=RecordNativeMotionVectors(motion,Context(*backend,targets,pass,index,&velocity));
    backend->Submit();
    return output;
  };
  frame(1,{},{});
  // Rigid: the quad at z = 10, moved by +0.5 x and +0.25 y since last frame.
  {
    const auto now=Translation(-1.5f,0.25f,10),before=Translation(-2.f,0.f,10);
    NativeMotionVelocityDraw draw;
    draw.geometry=rigid; draw.world=now; draw.previous_world=before;
    const auto output=frame(2,{{rigid.get(),now}},{draw});
    Check(!output.reset,"the rigid frame has history");
    const auto region=Compare(Read(*backend,*output.motion,targets),[&](uint32_t x,uint32_t y) {
      return MovedPlane(camera,x,y,10,{0.5,0.25,0});
    });
    std::cout << "rigid velocity: pixels=" << region.pixels << " worst=" << region.worst << "px\n";
    Check(region.pixels>200 && region.zero==0,"the moved quad has velocity in every pixel");
    Check(region.worst<0.02,"rigid velocity is off by up to "+std::to_string(region.worst)+" px");
    Check(region.background>1000 && region.background_nonzero==0,"the still background keeps zero motion");
  }
  // Instanced: two instances of one geometry, each moved its own way.
  {
    const auto a=Translation(-5.f,-1.f,10),b=Translation(5.f,1.f,10);
    NativeMotionVelocityDraw first,second;
    first.kind=second.kind=NativeMotionVelocityKind::Instanced;
    first.geometry=second.geometry=rigid;
    first.world=a; first.previous_world=Translation(-5.f,-1.5f,10);
    second.world=b; second.previous_world=Translation(5.75f,1.f,10);
    const auto draws_before=motion.statistics().velocity_draws;
    const auto output=frame(3,{{rigid.get(),a},{rigid.get(),b}},{first,second});
    Check(motion.statistics().velocity_draws==draws_before+1 && motion.statistics().velocity_instances>=2,
          "two instances of one geometry are one draw");
    const auto read=Read(*backend,*output.motion,targets);
    const auto region=Compare(read,[&](uint32_t x,uint32_t) {
      return x<kSize/2?MovedPlane(camera,x,0,10,{0,0.5,0}):MovedPlane(camera,x,0,10,{-0.75,0,0});
    });
    // MovedPlane's y is only in the ray; a uniform shift on a fronto-parallel plane does not depend on it.
    std::cout << "instanced velocity: pixels=" << region.pixels << " worst=" << region.worst << "px\n";
    Check(region.pixels>200 && region.zero==0 && region.worst<0.02,"each instance moves by its own world");
  }
  // Skinned: bone 1 (indices all 1, weights 0.5 + 0.5) carries the quad;
  // bone 0 is a decoy far away. Only the previous palette's bone 1 explains
  // the motion: 1 unit left and 0.5 down.
  {
    const auto pack=[](const std::vector<NativePoseMatrix>& bones) {
      auto floats=std::make_shared<std::vector<float>>(bones.size()*12);
      PackNativeBonePalette(bones,*floats);
      return std::shared_ptr<const std::vector<float>>(std::move(floats));
    };
    const auto now=Translation(1.f,0.f,12),decoy=Translation(50.f,50.f,50);
    NativeMotionVelocityDraw draw;
    draw.kind=NativeMotionVelocityKind::Skinned;
    draw.geometry=skinned; draw.weighted=true;
    draw.palette=pack({decoy,now}); draw.previous_palette=pack({Translation(-9.f,9.f,9),Translation(2.f,0.5f,12)});
    const auto output=frame(4,{{skinned.get(),now}},{draw});
    const auto region=Compare(Read(*backend,*output.motion,targets),[&](uint32_t x,uint32_t y) {
      return MovedPlane(camera,x,y,12,{-1,-0.5,0});
    });
    std::cout << "skinned velocity: pixels=" << region.pixels << " worst=" << region.worst << "px\n";
    Check(region.pixels>150 && region.zero==0 && region.worst<0.02,"skinned velocity follows the previous palette's bone");
    // Single-bone (VS_SingleBlend): Indices[0] alone, the weights ignored.
    draw.weighted=false;
    draw.palette=pack({decoy,now}); draw.previous_palette=pack({decoy,Translation(1.f,-0.75f,12)});
    const auto single=frame(5,{{skinned.get(),now}},{draw});
    const auto one=Compare(Read(*backend,*single.motion,targets),[&](uint32_t x,uint32_t y) {
      return MovedPlane(camera,x,y,12,{0,0.75,0});
    });
    Check(one.pixels>150 && one.zero==0 && one.worst<0.02,"single-bone velocity reads Indices[0]");
  }
  // An occluded mover: a still wall in front hides it; its velocity fails the depth test.
  {
    const auto hidden=Translation(0,0,20),wall=Translation(0,0,5);
    NativeMotionVelocityDraw draw;
    draw.geometry=rigid; draw.world=hidden; draw.previous_world=Translation(-3,0,20);
    const auto output=frame(6,{{rigid.get(),hidden},{rigid.get(),wall}},{draw});
    const auto region=Compare(Read(*backend,*output.motion,targets),[&](uint32_t,uint32_t) { return std::array<double,2>{0,0}; });
    Check(region.pixels>500 && region.worst<1e-3,"an occluded mover writes no velocity through the occluder");
  }
  // Classification of the game's vertex shaders.
  {
    Effect effect;
    effect.source=R"(
      float4x3 g_mWorldArray[8];
      float4 Blend(float3 p:POSITION0,int4 i:BLENDINDICES0,float4 w:BLENDWEIGHT0):SV_Position {
        return float4(mul(float4(p,1),g_mWorldArray[i.x])*w.x+mul(float4(p,1),g_mWorldArray[i.y])*w.y,1);
      }
      struct In { float3 p:POSITION0; float4 w:BLENDWEIGHT0; int4 i:BLENDINDICES0; };
      float4 Single(In input):SV_Position { return float4(mul(float4(input.p,1),g_mWorldArray[input.i.x]),1); }
      float4 Rigid(float3 p:POSITION0):SV_Position { return float4(p,1); }
    )";
    const auto blend=NativeMotionSkinningOf(CompileNativeShader(nullptr,effect,{false,"Blend","vs_3_0"},"motion-classify.fx"));
    const auto single=NativeMotionSkinningOf(CompileNativeShader(nullptr,effect,{false,"Single","vs_3_0"},"motion-classify.fx"));
    const auto plain=NativeMotionSkinningOf(CompileNativeShader(nullptr,effect,{false,"Rigid","vs_3_0"},"motion-classify.fx"));
    Check(blend.indices && blend.weights,"VS_Blend reads indices and weights");
    Check(single.indices && !single.weights,"VS_SingleBlend reads indices only");
    Check(!plain.indices && !plain.weights,"a rigid shader skins nothing");
  }
  CheckClean(*backend,"velocity pass");
}
// The debug view: mode 1 colours still pixels grey, mode 2 marks a reset red.
void TestDebugView() {
  auto backend=Warp();
  Targets targets(*backend);
  NativeMotionVectors motion;
  NativeBackendTextureDesc o{};
  o.width=o.height=kSize*2; o.format=DXGI_FORMAT_R8G8B8A8_UNORM; o.render_target=true;
  const auto output=backend->CreateRenderTarget(o);
  const auto camera=PassCamera(MakeCamera({0,0,0},0));
  const auto shot=[&](uint64_t frame,int mode) {
    backend->BeginFrame();
    BeginScene(*backend,targets);
    const auto result=RecordNativeMotionVectors(motion,Context(*backend,targets,camera,frame));
    motion.RecordDebug(*backend,backend->Recorder(),*output,DXGI_FORMAT_R8G8B8A8_UNORM,mode,result);
    backend->Submit();
    return backend->ReadRenderTarget(*output);
  };
  const auto reset=shot(1,2);
  Check(reset.size()==size_t(kSize)*kSize*16 && reset[0]>150 && reset[1]==0,"mode 2 marks a reset frame red");
  const auto valid=shot(2,2);
  Check(valid[0]==0 && valid[1]>140,"mode 2 marks valid history green");
  const auto colour=shot(3,1);
  Check(colour[0]>=127 && colour[0]<=129 && colour[1]>=127 && colour[1]<=129,"mode 1 shows still pixels grey");
  CheckClean(*backend,"debug view");
}
}  // namespace

int main(int argc,char** argv) {
  try {
    if(argc>1 && std::string_view(argv[1])=="--reprojection-error") { ReprojectionErrorTool(); return 0; }
    TestHistoryRules();
    TestReprojectionMath();
    TestCameraPass();
    TestVelocityPass();
    TestDebugView();
  } catch(const std::exception& error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
  if(failures) { std::cerr << failures << " check(s) failed\n"; return 1; }
  std::cout << "native motion vectors tests passed\n";
  return 0;
}

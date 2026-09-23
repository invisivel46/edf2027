#include "native_fsr.h"
#include "native_d3d12_raw.h"
#include "native_ffx.h"
#include "ffx_api/ffx_upscale.h"
#include "ffx_api/dx12/ffx_api_dx12.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <span>
#include <mutex>
#include <stdexcept>

namespace edf::native {
namespace {
std::mutex& MessageMutex() { static std::mutex mutex; return mutex; }
std::vector<std::string>& Messages() { static std::vector<std::string> messages; return messages; }
// ffxApiMessage has no user pointer, so the sink is process-wide; bounded so
// a context that complains every frame cannot grow it without limit.
void OnFfxMessage(uint32_t type,const wchar_t* message) {
  std::string text=type==FFX_API_MESSAGE_TYPE_ERROR?"error: ":"warning: ";
  for(const wchar_t* c=message;c && *c;++c) text.push_back(*c<128?char(*c):'?');
  std::lock_guard lock(MessageMutex());
  if(Messages().size()<256) Messages().push_back(std::move(text));
}
}  // namespace

std::optional<NativeFsrMode> ParseNativeFsrMode(std::string_view text) {
  if(text=="off" || text.empty()) return NativeFsrMode::Off;
  if(text=="native_aa") return NativeFsrMode::NativeAA;
  if(text=="quality") return NativeFsrMode::Quality;
  if(text=="balanced") return NativeFsrMode::Balanced;
  if(text=="performance") return NativeFsrMode::Performance;
  if(text=="ultra_performance") return NativeFsrMode::UltraPerformance;
  return std::nullopt;
}
std::string_view NativeFsrModeName(NativeFsrMode mode) {
  switch(mode) {
    case NativeFsrMode::Off: return "off";
    case NativeFsrMode::NativeAA: return "native_aa";
    case NativeFsrMode::Quality: return "quality";
    case NativeFsrMode::Balanced: return "balanced";
    case NativeFsrMode::Performance: return "performance";
    case NativeFsrMode::UltraPerformance: return "ultra_performance";
  }
  return "unknown";
}
uint32_t NativeFsrQualityMode(NativeFsrMode mode) {
  switch(mode) {
    case NativeFsrMode::Quality: return FFX_UPSCALE_QUALITY_MODE_QUALITY;
    case NativeFsrMode::Balanced: return FFX_UPSCALE_QUALITY_MODE_BALANCED;
    case NativeFsrMode::Performance: return FFX_UPSCALE_QUALITY_MODE_PERFORMANCE;
    case NativeFsrMode::UltraPerformance: return FFX_UPSCALE_QUALITY_MODE_ULTRA_PERFORMANCE;
    default: return FFX_UPSCALE_QUALITY_MODE_NATIVEAA;
  }
}
const char* NativeFsrExcludedBy(const NativeFsrExclusions& exclusions) {
  if(exclusions.ab_alternate) return "edf_native_ab_alternate";
  if(exclusions.reuse_off_alternate) return "edf_native_reuse_off_alternate";
  if(exclusions.shadow_render) return "edf_native_shadow_render";
  return nullptr;
}

int32_t NativeFsrJitterPhaseCount(uint32_t render_width,uint32_t display_width) {
  if(!render_width || !display_width) return 8;
  const float ratio=float(display_width)/float(render_width);
  return (std::max)(1,int32_t(8.0f*std::pow(ratio,2.0f)));
}
float NativeFsrHalton(int32_t index,int32_t base) {
  float f=1.0f,result=0.0f;
  for(int32_t current=index;current>0;) {
    f/=float(base);
    result+=f*float(current%base);
    current=int32_t(std::floor(float(current)/float(base)));
  }
  return result;
}
std::array<float,2> NativeFsrJitterOffset(int32_t index,int32_t phase_count) {
  if(phase_count<=0) return {0,0};
  const int32_t phase=((index%phase_count)+phase_count)%phase_count;
  return {NativeFsrHalton(phase+1,2)-0.5f,NativeFsrHalton(phase+1,3)-0.5f};
}
NativeFsrJitter MakeNativeFsrJitter(std::array<float,2> queried,uint32_t render_width,uint32_t render_height) {
  NativeFsrJitter jitter;
  if(!render_width || !render_height) return jitter;
  jitter.pixel_x=-queried[0]; jitter.pixel_y=-queried[1];
  jitter.clip_x=2.0f*jitter.pixel_x/float(render_width);
  jitter.clip_y=-2.0f*jitter.pixel_y/float(render_height);
  return jitter;
}
std::array<uint32_t,16> NativeFsrJitterGuestMatrix(const std::array<uint32_t,16>& matrix,float clip_x,float clip_y) {
  std::array<uint32_t,16> result=matrix;
  for(size_t row=0;row<4;++row) {
    const float w=std::bit_cast<float>(matrix[row*4+3]);
    result[row*4+0]=std::bit_cast<uint32_t>(std::bit_cast<float>(matrix[row*4+0])+clip_x*w);
    result[row*4+1]=std::bit_cast<uint32_t>(std::bit_cast<float>(matrix[row*4+1])+clip_y*w);
  }
  return result;
}

NativeFsrCameraParams NativeFsrCameraFromProjection(const std::array<float,16>& p) {
  NativeFsrCameraParams params;
  // Perspective: w = s z with s = P[2][3] = +1 (left-handed) or -1
  // (right-handed), P[3][3] = 0, and a y scale. The game's projection is
  // right-handed: 821C82C0 stores P[1][1] = cot(fov/2), P[2][2] = -f/(f-n),
  // P[3][2] = -n f/(f-n) and P[2][3] = -1.0 ([820013DC]), a forward matrix
  // made reversed-Z by the viewport. With the view depth d = s z (positive in
  // front) both handednesses are z_ndc = s P[2][2] + P[3][2] / d, the
  // left-handed form in (a, b). A reversed matrix (a = -n/(f-n)) gives the
  // planes swapped, which the min/max below puts back in order.
  const float s=p[11];
  if(!(std::abs(std::abs(s)-1.0f)<1e-3f) || !(std::abs(p[15])<1e-3f) || !(p[5]>1e-6f)) return params;
  const float a=s<0?-p[10]:p[10],b=p[14];
  if(!std::isfinite(a) || !std::isfinite(b) || a==0) return params;
  const float n=-b/a;
  const float f=a==1.0f?std::numeric_limits<float>::infinity():-b/(a-1.0f);
  const float fov=2.0f*std::atan(1.0f/p[5]);
  if(!(n>0) || !(f>0) || n==f || !std::isfinite(fov) || !(fov>0)) return params;
  params.near_plane=(std::min)(n,f);
  params.far_plane=std::isfinite(f)?(std::max)(n,f):1.0e30f;
  params.fov_y=fov;
  params.derived=true;
  return params;
}
NativeFsrCameraParams NativeFsrCameraFromProjectionWords(const std::array<uint32_t,16>& projection) {
  std::array<float,16> p{};
  for(size_t i=0;i<16;++i) p[i]=std::bit_cast<float>(projection[i]);
  return NativeFsrCameraFromProjection(p);
}
NativeFsrMotionPlan PlanNativeFsrMotion(const NativeMotionVectorOutput& output,const NativeFsrCameraParams& pass_camera) {
  NativeFsrMotionPlan plan;
  plan.motion=output.motion;
  plan.reset=output.reset || !output.motion;
  plan.camera=pass_camera;
  if(output.near_plane>0 && output.far_plane>0 && output.fov_y>0 && output.near_plane!=output.far_plane) {
    plan.camera.near_plane=(std::min)(output.near_plane,output.far_plane);
    plan.camera.far_plane=(std::max)(output.near_plane,output.far_plane);
    plan.camera.fov_y=output.fov_y;
    plan.camera.derived=true;
  }
  return plan;
}

uint32_t NativeFsrResetTracker::Next(const NativeFsrFrameFacts& facts) {
  uint32_t reasons=0;
  if(!last_) reasons|=kNativeFsrResetFirst;
  else {
    const auto& last=*last_;
    if(last.width!=facts.width || last.height!=facts.height) reasons|=kNativeFsrResetResize;
    if(last.format!=facts.format) reasons|=kNativeFsrResetFormat;
    if(facts.helper_frame!=last.helper_frame+1) reasons|=kNativeFsrResetRoute;
    if(last.ab_native!=facts.ab_native) reasons|=kNativeFsrResetAbSide;
    if(last.mode!=facts.mode) reasons|=kNativeFsrResetMode;
    const auto& a=last.camera; const auto& b=facts.camera;
    // A cut, not a zoom: the motion vectors reproject through both frames'
    // projections, so a continuous fov change (aiming, a scope) or a moved
    // near/far plane keeps the history. The fov limit is workstream B's
    // (NativeCameraHistory::CutWith, 0.25 rad), the planes a factor of 2.
    const auto ratio=[](float x,float y) { return (std::max)(x,y)/(std::max)((std::min)(x,y),1e-30f); };
    if(a.derived!=b.derived || std::abs(a.fov_y-b.fov_y)>kNativeFsrCutFov ||
       ratio(a.near_plane,b.near_plane)>kNativeFsrCutPlaneRatio || ratio(a.far_plane,b.far_plane)>kNativeFsrCutPlaneRatio)
      reasons|=kNativeFsrResetCut;
  }
  if(facts.motion_reset) reasons|=kNativeFsrResetMotion;
  last_=facts;
  return reasons;
}
std::string NativeFsrResetTracker::Describe(uint32_t reasons) {
  static constexpr std::pair<uint32_t,const char*> kNames[]{
    {kNativeFsrResetFirst,"first"},{kNativeFsrResetResize,"resize"},{kNativeFsrResetFormat,"format"},
    {kNativeFsrResetRoute,"route"},{kNativeFsrResetAbSide,"ab_side"},{kNativeFsrResetCut,"cut"},
    {kNativeFsrResetMotion,"motion"},{kNativeFsrResetMode,"mode"}};
  std::string out;
  for(const auto& [bit,name]:kNames) if(reasons&bit) out+=(out.empty()?"":"+")+std::string(name);
  return out.empty()?"none":out;
}

std::vector<std::string> DrainNativeFsrMessages() {
  std::lock_guard lock(MessageMutex());
  std::vector<std::string> out;
  out.swap(Messages());
  return out;
}
NativeFfxLibrary& NativeFsrLibrary() {
  // Never unloaded: upscalers owned by other statics destroy their contexts
  // through it at exit, whatever order the statics go in.
  static auto* library=new NativeFfxLibrary();
  return *library;
}

NativeFsrUpscaler::NativeFsrUpscaler()=default;
NativeFsrUpscaler::~NativeFsrUpscaler() {
  // At shutdown; the caller has stopped submitting FSR work by then.
  Release();
  for(auto& retired:retired_) Destroy(retired.context);
  retired_.clear();
}
void NativeFsrUpscaler::Destroy(void* context) {
  if(!context) return;
  auto& library=NativeFsrLibrary();
  if(!library.available()) return;
  ffxContext handle=context;
  library.functions().DestroyContext(&handle,nullptr);
}
void NativeFsrUpscaler::Release() {
  Destroy(context_);
  context_=nullptr;
  output_.reset(); reactive_.reset(); opaque_.reset(); zero_motion_.reset();
  backend_=nullptr; width_=height_=format_=0;
}

bool NativeFsrUpscaler::Prepare(NativeRenderBackend& backend,uint32_t width,uint32_t height,uint32_t color_format,
                                std::string* error) {
  recreated_=false;
  // Contexts replaced earlier are destroyed once their frames are long done.
  for(auto& retired:retired_) if(retired.frames) --retired.frames;
  for(auto it=retired_.begin();it!=retired_.end();) {
    if(it->frames) { ++it; continue; }
    Destroy(it->context);
    ++stats_.retired;
    it=retired_.erase(it);
  }
  const auto fail=[&](std::string why) { if(error) *error=std::move(why); return false; };
  if(context_ && backend_==&backend && width_==width && height_==height && format_==color_format) return true;
  auto* raw=backend.D3D12Raw();
  if(!raw) return fail("the scene backend "+std::string(backend.name())+" has no D3D12 raw access");
  if(!width || !height) return fail("empty scene");
  auto& library=NativeFsrLibrary();
  if(!library.available()) return fail(library.error());
  if(!library.upscaler_available(raw->Device())) return fail("the FidelityFX DLL offers no upscaler");
  if(context_) {
    Retired retired;
    retired.context=context_; retired.frames=kRetireFrames;
    retired.output=std::move(output_); retired.reactive=std::move(reactive_);
    retired.opaque=std::move(opaque_); retired.zero_motion=std::move(zero_motion_);
    retired_.push_back(std::move(retired));
    context_=nullptr;
  }
  const auto& ffx=library.functions();
  ffxCreateBackendDX12Desc backend_desc{};
  backend_desc.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
  backend_desc.device=raw->Device();
  ffxCreateContextDescUpscale create{};
  create.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
  create.header.pNext=&backend_desc.header;
  create.flags=FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE|FFX_UPSCALE_ENABLE_DEPTH_INVERTED|FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
  create.maxRenderSize={width,height};
  create.maxUpscaleSize={width,height};
  create.fpMessage=&OnFfxMessage;
  ffxOverrideVersion version{};
  if(version_override_) {
    version.header.type=FFX_API_DESC_TYPE_OVERRIDE_VERSION;
    version.versionId=version_override_;
    backend_desc.header.pNext=&version.header;
  }
  ffxContext context=nullptr;
  if(const auto code=ffx.CreateContext(&context,&create.header,nullptr);code!=FFX_API_RETURN_OK || !context)
    return fail("ffxCreateContext(upscale) returned "+std::to_string(code));
  context_=context; backend_=&backend; width_=width; height_=height; format_=color_format;
  ++stats_.contexts; recreated_=true;
  provider_.clear();
  {
    ffxQueryGetProviderVersion query{};
    query.header.type=FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
    if(ffx.Query(&context,&query.header)==FFX_API_RETURN_OK && query.versionName) provider_=query.versionName;
  }
  {
    FfxApiEffectMemoryUsage usage{};
    ffxQueryDescUpscaleGetGPUMemoryUsage query{};
    query.header.type=FFX_API_QUERY_DESC_TYPE_UPSCALE_GPU_MEMORY_USAGE;
    query.gpuMemoryUsageUpscaler=&usage;
    stats_.context_bytes=ffx.Query(&context,&query.header)==FFX_API_RETURN_OK?usage.totalUsageInBytes:0;
  }
  // The jitter sequence, from the context (native AA: render = display).
  int32_t phases=0;
  ffxQueryDescUpscaleGetJitterPhaseCount count{};
  count.header.type=FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
  count.renderWidth=width; count.displayWidth=width; count.pOutPhaseCount=&phases;
  jitter_from_ffx_=ffx.Query(&context,&count.header)==FFX_API_RETURN_OK && phases>0;
  phase_count_=jitter_from_ffx_?phases:NativeFsrJitterPhaseCount(width,width);
  // Resources: output (display size, RGBA16F, UAV: the post's input), the
  // reactive mask (R8, UAV), the opaque-only copy (colour format) and a zero
  // motion field (RG16F) for when there are no motion vectors.
  const auto texture=[&](uint32_t format,bool uav,std::span<const uint8_t> initial) {
    NativeBackendTextureDesc desc{};
    desc.width=width; desc.height=height; desc.levels=1; desc.format=format; desc.unordered_access=uav;
    auto created=backend.CreateTexture(desc,initial);
    if(!created) throw std::runtime_error("FSR texture creation failed");
    return created;
  };
  try {
    output_=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,true,{});
    reactive_=texture(DXGI_FORMAT_R8_UNORM,true,{});
    opaque_=texture(color_format,false,{});
    const std::vector<uint8_t> zeros(size_t(width)*height*4,0);
    zero_motion_=texture(DXGI_FORMAT_R16G16_FLOAT,false,zeros);
  } catch(const std::exception& failure) {
    Release();
    return fail(failure.what());
  }
  return true;
}

std::array<float,2> NativeFsrUpscaler::JitterOffset(int32_t index) const {
  if(context_ && jitter_from_ffx_) {
    auto& library=NativeFsrLibrary();
    float x=0,y=0;
    ffxQueryDescUpscaleGetJitterOffset query{};
    query.header.type=FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
    query.index=index; query.phaseCount=phase_count_; query.pOutX=&x; query.pOutY=&y;
    ffxContext context=context_;
    if(library.functions().Query(&context,&query.header)==FFX_API_RETURN_OK) return {x,y};
  }
  return NativeFsrJitterOffset(index,phase_count_);
}

void NativeFsrUpscaler::Dispatch(NativeRenderBackend& backend,NativeBackendRecorder& recorder,
                                 const NativeFsrDispatchInputs& inputs) {
  if(!context_ || backend_!=&backend) throw std::runtime_error("FSR dispatch without a prepared context");
  if(!inputs.color || !inputs.depth) throw std::runtime_error("FSR dispatch needs colour and depth");
  if(!inputs.depth->texture()) throw std::runtime_error("FSR needs the scene depth sampled (edf_native_scene_depth_srv)");
  auto* raw=backend.D3D12Raw();
  if(!raw) throw std::runtime_error("FSR dispatch on a backend without D3D12 raw access");
  auto* motion=inputs.motion?inputs.motion:zero_motion_.get();
  const bool reactive=inputs.opaque && opaque_;
  constexpr auto read=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  constexpr auto write=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  std::vector<NativeD3D12RawUse> uses{
    {inputs.color,nullptr,read},{nullptr,inputs.depth,read},{motion,nullptr,read},
    {output_.get(),nullptr,write},{reactive_.get(),nullptr,write}};
  if(reactive) uses.push_back({opaque_.get(),nullptr,read});
  const auto& ffx=NativeFsrLibrary().functions();
  const auto resource=[&](ID3D12Resource* resource,uint32_t state) { return ffxApiGetResourceDX12(resource,state); };
  ffxReturnCode_t reactive_code=FFX_API_RETURN_OK,upscale_code=FFX_API_RETURN_OK;
  raw->RecordRaw(recorder,uses,[&](NativeD3D12RawPass& pass) {
    ffxContext context=context_;
    const FfxApiResource color=resource(raw->Resource(*inputs.color),FFX_API_RESOURCE_STATE_COMPUTE_READ);
    const FfxApiResource mask=resource(raw->Resource(*reactive_),FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    if(reactive) {
      ffxDispatchDescUpscaleGenerateReactiveMask generate{};
      generate.header.type=FFX_API_DISPATCH_DESC_TYPE_UPSCALE_GENERATEREACTIVEMASK;
      generate.commandList=pass.commands();
      generate.colorOpaqueOnly=resource(raw->Resource(*opaque_),FFX_API_RESOURCE_STATE_COMPUTE_READ);
      generate.colorPreUpscale=color;
      generate.outReactive=mask;
      generate.renderSize={width_,height_};
      // The FidelityFX sample's values (fsrapirendermodule.cpp).
      generate.scale=1.0f; generate.cutoffThreshold=0.2f; generate.binaryValue=0.9f;
      generate.flags=FFX_UPSCALE_AUTOREACTIVEFLAGS_APPLY_TONEMAP|FFX_UPSCALE_AUTOREACTIVEFLAGS_APPLY_THRESHOLD|
        FFX_UPSCALE_AUTOREACTIVEFLAGS_USE_COMPONENTS_MAX;
      reactive_code=ffx.Dispatch(&context,&generate.header);
    }
    ffxDispatchDescUpscale upscale{};
    upscale.header.type=FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    upscale.commandList=pass.commands();
    upscale.color=color;
    upscale.depth=resource(raw->Resource(*inputs.depth),FFX_API_RESOURCE_STATE_COMPUTE_READ);
    upscale.motionVectors=resource(raw->Resource(*motion),FFX_API_RESOURCE_STATE_COMPUTE_READ);
    upscale.exposure=resource(nullptr,FFX_API_RESOURCE_STATE_COMPUTE_READ);
    upscale.reactive=reactive && reactive_code==FFX_API_RETURN_OK?mask:resource(nullptr,FFX_API_RESOURCE_STATE_COMPUTE_READ);
    upscale.transparencyAndComposition=resource(nullptr,FFX_API_RESOURCE_STATE_COMPUTE_READ);
    upscale.output=resource(raw->Resource(*output_),FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    upscale.jitterOffset={inputs.jitter.pixel_x,inputs.jitter.pixel_y};
    upscale.motionVectorScale={inputs.motion_scale[0],inputs.motion_scale[1]};
    upscale.renderSize={width_,height_};
    upscale.upscaleSize={width_,height_};
    upscale.enableSharpening=inputs.sharpness>0;
    upscale.sharpness=std::clamp(inputs.sharpness,0.0f,1.0f);
    upscale.frameTimeDelta=std::clamp(inputs.frame_ms,0.1f,200.0f);
    upscale.preExposure=1.0f;
    upscale.reset=inputs.reset;
    upscale.cameraNear=inputs.camera.near_plane;
    upscale.cameraFar=inputs.camera.far_plane;
    upscale.cameraFovAngleVertical=inputs.camera.fov_y;
    upscale.viewSpaceToMetersFactor=1.0f;
    upscale.flags=0;
    upscale_code=ffx.Dispatch(&context,&upscale.header);
  });
  if(reactive_code!=FFX_API_RETURN_OK)
    throw std::runtime_error("ffxDispatch(GENERATEREACTIVEMASK) returned "+std::to_string(reactive_code));
  if(upscale_code!=FFX_API_RETURN_OK) throw std::runtime_error("ffxDispatch(upscale) returned "+std::to_string(upscale_code));
  ++stats_.dispatches;
  if(reactive) ++stats_.reactive;
  if(inputs.reset) ++stats_.resets;
}
}  // namespace edf::native

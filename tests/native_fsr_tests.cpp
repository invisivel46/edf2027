// FSR integration (native_fsr.h): modes and validation exclusions, the jitter
// sequence and its projection math, the camera read from a projection, the
// motion-vector fallback for workstream B's interface, the history reset
// rules, and - on WARP with the debug layer, when the real FidelityFX DLL is
// staged next to this test - FSR native AA dispatched through a raw pass on
// a small jittered scene, checked against the scene's exact coverage.
// (The renderer's clip jitter and the static world's reuse under jitter are
// in edf_native_scene_tests: they need its fixtures.)
#include "native_graphics/d3d12_backend.h"
#include "native_graphics/native_d3d12_raw.h"
#include "native_graphics/native_display_layout.h"
#include "native_graphics/native_ffx.h"
#include "native_graphics/native_fsr.h"
#include "native_graphics/native_render_backend.h"
#include "native_graphics/native_render_scale.h"
#include "native_graphics/native_sampler_decode.h"
#include "native_graphics/native_scene.h"
#include "native_graphics/native_scene_pass_inputs.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <set>
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
bool Near(float a,float b,float tolerance) { return std::abs(a-b)<=tolerance; }

// ---------------------------------------------------------------------------
void TestModes() {
  using M=NativeFsrMode;
  const std::pair<const char*,M> names[]{{"off",M::Off},{"native_aa",M::NativeAA},{"quality",M::Quality},
    {"balanced",M::Balanced},{"performance",M::Performance},{"ultra_performance",M::UltraPerformance}};
  for(const auto& [name,mode]:names) {
    Check(ParseNativeFsrMode(name)==mode,std::string("mode not parsed: ")+name);
    Check(NativeFsrModeName(mode)==name,std::string("mode name does not round-trip: ")+name);
  }
  Check(ParseNativeFsrMode("")==M::Off,"an empty edf_native_fsr is not off");
  Check(!ParseNativeFsrMode("fsr3") && !ParseNativeFsrMode("Native_AA"),"an unknown mode was accepted");
  // Every mode runs as itself (Phase 1 ran the upscaling ones as native AA).
  Check(NativeFsrEffectiveMode(M::Off)==M::Off && NativeFsrEffectiveMode(M::NativeAA)==M::NativeAA &&
        NativeFsrEffectiveMode(M::Performance)==M::Performance,"effective modes");
  Check(!NativeFsrUpscales(M::Off) && !NativeFsrUpscales(M::NativeAA) && NativeFsrUpscales(M::Quality) &&
        NativeFsrUpscales(M::Balanced) && NativeFsrUpscales(M::Performance) && NativeFsrUpscales(M::UltraPerformance),
        "which modes upscale");
  Check(NativeFsrQualityMode(M::NativeAA)==0 && NativeFsrQualityMode(M::Quality)==1 &&
        NativeFsrQualityMode(M::UltraPerformance)==4,"FFX quality mode numbers");
  Check(!NativeFsrExcludedBy({}),"no validation run excluded FSR");
  Check(std::string(NativeFsrExcludedBy({true,false,false}))=="edf_native_ab_alternate" &&
        std::string(NativeFsrExcludedBy({false,true,false}))=="edf_native_reuse_off_alternate" &&
        std::string(NativeFsrExcludedBy({false,false,true}))=="edf_native_shadow_render","exclusions name their cvar");
  std::cout << "modes: parsed, native_aa effective, exclusions named\n";
}

// ---------------------------------------------------------------------------
// Upscaling sizes (AMD's ratios, FFX's truncation), the jitter sequence length
// per ratio, the mip bias, and render-pixel jitter.
void TestUpscaleSizes() {
  using M=NativeFsrMode;
  Check(NativeFsrUpscaleRatio(M::Off)==1 && NativeFsrUpscaleRatio(M::NativeAA)==1 && NativeFsrUpscaleRatio(M::Quality)==1.5f &&
        NativeFsrUpscaleRatio(M::Balanced)==1.7f && NativeFsrUpscaleRatio(M::Performance)==2 &&
        NativeFsrUpscaleRatio(M::UltraPerformance)==3,"upscale ratios");
  struct Case { M mode; uint32_t dw,dh,rw,rh; int32_t phases,bias_steps; };
  // Phases: 8 * (display/render)^2 truncated; bias: round(32 * (log2(render/display) - 1)).
  const Case cases[]{
    {M::NativeAA,1920,1080,1920,1080,8,0},
    {M::Quality,1920,1080,1280,720,18,-51},
    {M::Balanced,1920,1080,1129,635,23,-57},
    {M::Performance,1920,1080,960,540,32,-64},
    {M::UltraPerformance,1920,1080,640,360,72,-83},
    {M::Quality,2560,1440,1706,960,18,-51},
    {M::Balanced,2560,1440,1505,847,23,-57},
    {M::Performance,2560,1440,1280,720,32,-64},
    {M::UltraPerformance,2560,1440,853,480,72,-83},
    {M::Quality,3840,2160,2560,1440,18,-51},
    {M::Performance,3840,2160,1920,1080,32,-64},
    {M::Quality,1280,800,853,533,18,-51},  // the Steam Deck's 16:10
  };
  for(const auto& c:cases) {
    const auto size=NativeFsrRenderSizeFor(c.mode,c.dw,c.dh);
    const auto name=std::string(NativeFsrModeName(c.mode))+" at "+std::to_string(c.dw)+"x"+std::to_string(c.dh);
    Check(size==NativeFsrRenderSize{c.rw,c.rh},name+": render size "+std::to_string(size.width)+"x"+std::to_string(size.height));
    Check(NativeFsrJitterPhaseCount(size.width,c.dw)==c.phases,
          name+": jitter phases "+std::to_string(NativeFsrJitterPhaseCount(size.width,c.dw)));
    Check(NativeFsrMipBiasSteps(size.width,c.dw)==c.bias_steps,name+": mip bias steps "+std::to_string(NativeFsrMipBiasSteps(size.width,c.dw)));
    const float bias=NativeFsrMipBias(size.width,c.dw);
    Check(c.mode==M::NativeAA?bias==0:Near(bias,std::log2(float(size.width)/float(c.dw))-1,1e-6f),name+": mip bias");
  }
  Check(Near(NativeFsrMipBias(1280,1920),-1.5849625f,1e-5f),"quality mip bias is log2(2/3) - 1");
  Check(NativeFsrRenderSizeFor(M::UltraPerformance,2,2)==NativeFsrRenderSize{1,1} &&
        NativeFsrRenderSizeFor(M::Quality,0,0)==NativeFsrRenderSize{0,0},"degenerate sizes");
  // Jitter in render pixels: the NDC offset is 2 px / render width, so one
  // render pixel of jitter is display/render output pixels.
  const auto jitter=MakeNativeFsrJitter({0.25f,-0.25f},1280,720);
  Check(Near(jitter.clip_x,-0.5f/1280,1e-9f) && Near(jitter.clip_y,-0.5f/720,1e-9f),"render-pixel jitter");
  // The sequence covers every phase before repeating at each ratio.
  for(const auto& c:cases) {
    std::set<std::pair<float,float>> distinct;
    for(int32_t i=0;i<c.phases;++i) { const auto o=NativeFsrJitterOffset(i,c.phases); distinct.insert({o[0],o[1]}); }
    Check(int32_t(distinct.size())==c.phases,"jitter phases repeat within a cycle of "+std::to_string(c.phases));
  }
  std::cout << "upscale sizes: 1920x1080 -> 1280x720 / 1129x635 / 960x540 / 640x360, phases 18/23/32/72, bias -1.58/-1.77/-2/-2.58\n";
}

// The recording's rectangle mapping (native_render_scale.h): exact for the
// whole surface, proportional for split views, scissors outward and clamped.
void TestRenderScale() {
  const NativeRenderScale scale{1280,720,1920,1080},identity{1920,1080,1920,1080};
  Check(scale.active() && !identity.active() && !NativeRenderScale{}.active(),"scale activity");
  const NativeBackendViewport whole{0,0,1920,1080,0,1};
  const auto mapped=ScaleNativeViewport(whole,scale);
  Check(mapped.x==0 && mapped.y==0 && mapped.width==1280 && mapped.height==720 && mapped.min_depth==0 && mapped.max_depth==1,
        "the whole surface does not map exactly onto the render rectangle");
  const auto unchanged=ScaleNativeViewport(whole,identity);
  Check(std::memcmp(&whole,&unchanged,sizeof(whole))==0,"identity changed a viewport");
  // Balanced: 1129x635 of 1920x1080 is not a whole ratio; the whole surface is still exact.
  const auto balanced=ScaleNativeViewport(whole,{1129,635,1920,1080});
  Check(balanced.width==1129 && balanced.height==635,"balanced whole-surface viewport");
  // A split view (right half) maps to the right half of the render rectangle.
  const auto right=ScaleNativeViewport({960,0,960,1080,0,1},scale);
  Check(right.x==640 && right.width==640 && right.height==720,"split view");
  const auto scissor=ScaleNativeScissor({0,0,1920,1080},scale);
  Check(scissor.left==0 && scissor.top==0 && scissor.right==1280 && scissor.bottom==720,"whole scissor");
  const auto odd=ScaleNativeScissor({1,1,1919,1079},scale);  // 0.667 .. 1279.33: outward
  Check(odd.left==0 && odd.top==0 && odd.right==1280 && odd.bottom==720,"scissor rounds outward");
  const auto big=ScaleNativeScissor({-100,-100,4000,4000},scale);
  Check(big.left==0 && big.top==0 && big.right==1280 && big.bottom==720,"scissor clamped to the render rectangle");
  const auto empty=ScaleNativeScissor({300,300,300,200},scale);
  Check(empty.right==empty.left && empty.bottom==empty.top,"an empty scissor stays empty");
  std::cout << "render scale: whole surface exact, split views proportional, scissors outward\n";
}

// The mip bias rides the pass's filtering value into the sampler key: added
// to the guest's own bias, clamped, and absent (bit for bit) at 0.
void TestMipBiasFiltering() {
  for(const int mode:{-1,0,1,5}) {
    Check(NativeFilteringWithMipBias(mode,0)==mode,"no bias changed the filtering value "+std::to_string(mode));
    for(const int steps:{-51,-64,-83,-512,511,7}) {
      const int value=NativeFilteringWithMipBias(mode,steps);
      Check(NativeFilteringMode(value)==mode && NativeFilteringMipBias(value)==steps,
            "filtering "+std::to_string(mode)+" bias "+std::to_string(steps)+" does not round-trip");
    }
    Check(NativeFilteringMode(mode)==mode && NativeFilteringMipBias(mode)==0,"a plain mode reads as biased");
  }
  // A trilinear sampler with a guest bias of +0.5 (16 steps).
  SamplerStateWords words{0,(1u<<19)|(1u<<21)|(1u<<23),(16u<<12)|(10u<<6),0};
  for(const int mode:{-1,0,4}) {
    Check(NativeFilteringKey(words,mode)==NativeFilteringKey(words,NativeFilteringWithMipBias(mode,0)),"zero bias changed a key");
    const auto biased=DecodeNativeGuestSampler(NativeFilteringKey(words,NativeFilteringWithMipBias(mode,-51)));
    const auto plain=DecodeNativeGuestSampler(NativeFilteringKey(words,mode));
    Check(Near(plain.mip_lod_bias,0.5f,0) && Near(biased.mip_lod_bias,0.5f-51/32.f,1e-6f),
          "biased sampler lod bias "+std::to_string(biased.mip_lod_bias));
    Check(biased.min==plain.min && biased.mag==plain.mag && biased.mip==plain.mip && biased.max_anisotropy==plain.max_anisotropy &&
          biased.max_lod==plain.max_lod,"the bias changed more than the LOD bias");
  }
  const auto floor=DecodeNativeGuestSampler(NativeFilteringKey({0,(1u<<19)|(1u<<21)|(1u<<23),(1000u<<12),0},
    NativeFilteringWithMipBias(-1,-83)));  // -24 steps - 83 -> -107
  Check(Near(floor.mip_lod_bias,-107/32.f,1e-6f),"negative guest bias plus FSR bias");
  const auto clamp=DecodeNativeGuestSampler(NativeFilteringKey({0,(1u<<19)|(1u<<21)|(1u<<23),(600u<<12),0},
    NativeFilteringWithMipBias(-1,-200)));  // -424 - 200 clamps at -512
  Check(Near(clamp.mip_lod_bias,-16.f,1e-6f),"the bias is clamped to the field's range");
  std::cout << "mip bias filtering: round-trips, adds to the guest bias, clamps, zero is identity\n";
}

// Upscaling makes the output the window's: the render size request is replaced.
void TestRenderRequest() {
  Check((NativeRenderRequest(2560,1440,true)==std::array<int32_t,2>{0,-1}) &&
        (NativeRenderRequest(0,0,true)==std::array<int32_t,2>{0,-1}),"upscaling keeps a render size request");
  Check((NativeRenderRequest(2560,1440,false)==std::array<int32_t,2>{2560,1440}) &&
        (NativeRenderRequest(0,-1,false)==std::array<int32_t,2>{0,-1}) &&
        (NativeRenderRequest(0,0,false)==std::array<int32_t,2>{0,0}),"off and native AA changed the request");
  const auto output=ResolveNativeRenderSize(0,-1,1920,1080,NativeAspectMode::Native);
  Check(output.width==1920 && output.height==1080,"match window on the Ally's screen");
  const auto scene=NativeFsrRenderSizeFor(NativeFsrMode::Quality,uint32_t(output.width),uint32_t(output.height));
  Check(scene==NativeFsrRenderSize{1280,720},"the Ally's quality scene is 1280x720");
  std::cout << "render request: upscaling renders the output at the window's size\n";
}

// ---------------------------------------------------------------------------
// FFX's sequence: 8 phases at 1.0x (8 * ratio^2), Halton(2, 3) - 0.5 of
// phase + 1, periodic, in [-0.5, 0.5), and centred over a cycle.
void TestJitterSequence() {
  Check(NativeFsrJitterPhaseCount(1280,1280)==8 && NativeFsrJitterPhaseCount(1920,1920)==8,"native AA phase count is not 8");
  Check(NativeFsrJitterPhaseCount(640,1280)==32 && NativeFsrJitterPhaseCount(854,1280)==17,"scaled phase counts");
  Check(Near(NativeFsrHalton(1,2),0.5f,0) && Near(NativeFsrHalton(2,2),0.25f,0) && Near(NativeFsrHalton(3,2),0.75f,0) &&
        Near(NativeFsrHalton(1,3),1/3.f,1e-7f) && Near(NativeFsrHalton(2,3),2/3.f,1e-7f) && Near(NativeFsrHalton(4,3),4/9.f,1e-7f),
        "Halton radical inverses");
  const std::array<std::array<float,2>,8> expected{{
    {0.0f,-1/6.f},{-0.25f,1/6.f},{0.25f,-7/18.f},{-0.375f,-1/18.f},{0.125f,5/18.f},{-0.125f,-5/18.f},{0.375f,1/18.f},{-0.4375f,7/18.f}}};
  std::array<float,2> mean{};
  std::set<std::pair<float,float>> distinct;
  for(int32_t i=0;i<8;++i) {
    const auto o=NativeFsrJitterOffset(i,8);
    Check(Near(o[0],expected[i][0],1e-6f) && Near(o[1],expected[i][1],1e-6f),"jitter offset "+std::to_string(i));
    Check(o[0]>=-0.5f && o[0]<0.5f && o[1]>=-0.5f && o[1]<0.5f,"jitter outside a pixel");
    const auto again=NativeFsrJitterOffset(i+8,8);
    Check(again==o,"the sequence is not periodic in its phase count");
    mean[0]+=o[0]/8; mean[1]+=o[1]/8;
    distinct.insert({o[0],o[1]});
  }
  Check(distinct.size()==8,"jitter phases repeat within a cycle");
  Check(std::abs(mean[0])<0.1f && std::abs(mean[1])<0.1f,"the jitter cycle is not centred");
  std::cout << "jitter sequence: 8 phases, mean (" << mean[0] << ", " << mean[1] << ")\n";
}

// The projection math: the queried offset q becomes dispatch pixel = -q and
// NDC clip = (2 pixel.x / w, -2 pixel.y / h); folded into a row-vector
// projection it moves every projected point by exactly that NDC offset, i.e.
// the image by `pixel` pixels (x right, y down). The guest-word form equals
// the float form; view stays, projection and view*projection move together;
// zero jitter is the identity, bit for bit.
void TestJitterProjection() {
  const auto jitter=MakeNativeFsrJitter({0.25f,-0.125f},1280,720);
  Check(jitter.pixel_x==-0.25f && jitter.pixel_y==0.125f,"dispatch jitter is not the negated query (FidelityFX sample convention)");
  Check(Near(jitter.clip_x,2*-0.25f/1280,1e-9f) && Near(jitter.clip_y,-2*0.125f/720,1e-9f),"NDC jitter");
  Check(jitter.active() && !NativeFsrJitter{}.active(),"jitter activity");
  // A perspective projection (row vectors): n=0.5, f=5000, fov_y=60 deg, 16:9.
  const float n=0.5f,f=5000.f,fov=float(std::numbers::pi/3),y=1/std::tan(fov/2),x=y/(16/9.f);
  const NativeSceneMatrix projection{x,0,0,0, 0,y,0,0, 0,0,f/(f-n),1, 0,0,-n*f/(f-n),0};
  const auto jittered=NativeSceneClipJitter(projection,jitter.clip_x,jitter.clip_y);
  const auto project=[](const NativeSceneMatrix& m,std::array<float,4> p) {
    std::array<float,4> clip{};
    for(size_t c=0;c<4;++c) for(size_t r=0;r<4;++r) clip[c]+=p[r]*m[r*4+c];
    return std::array<float,3>{clip[0]/clip[3],clip[1]/clip[3],clip[2]/clip[3]};
  };
  for(const auto& point:{std::array<float,4>{1,2,10,1},{-30,5,400,1},{0.1f,-0.2f,0.7f,1}}) {
    const auto a=project(projection,point),b=project(jittered,point);
    Check(Near(b[0]-a[0],jitter.clip_x,1e-6f) && Near(b[1]-a[1],jitter.clip_y,1e-6f) && b[2]==a[2],
          "a jittered projection did not move NDC by exactly the jitter");
    // In pixels: x right by pixel_x, y down by pixel_y.
    Check(Near((b[0]-a[0])*1280/2,jitter.pixel_x,1e-3f) && Near(-(b[1]-a[1])*720/2,jitter.pixel_y,1e-3f),"pixel shift");
  }
  // Guest words: the same numbers, from the same bits.
  std::array<uint32_t,16> words{};
  for(size_t i=0;i<16;++i) words[i]=std::bit_cast<uint32_t>(projection[i]);
  const auto moved=NativeFsrJitterGuestMatrix(words,jitter.clip_x,jitter.clip_y);
  for(size_t i=0;i<16;++i) Check(std::bit_cast<float>(moved[i])==jittered[i],"guest-word jitter differs from the float form");
  Check(NativeFsrJitterGuestMatrix(words,0,0)==words && NativeSceneClipJitter(projection,0,0)==projection,
        "a zero jitter changed a matrix");
  // A pass camera: the view is untouched; view*projection moves with the
  // projection (jitter(V P) = V jitter(P): the column operation is linear).
  NativeSceneMatrix view=kNativeSceneIdentity;
  view[0]=0.8f; view[2]=-0.6f; view[8]=0.6f; view[10]=0.8f; view[12]=3; view[13]=-2; view[14]=7;
  NativeSceneMatrix vp{};
  for(size_t r=0;r<4;++r) for(size_t c=0;c<4;++c) for(size_t k=0;k<4;++k) vp[r*4+c]+=view[r*4+k]*projection[k*4+c];
  NativeScenePassCamera camera;
  for(size_t i=0;i<16;++i) {
    camera.projection[i]=words[i]; camera.view[i]=std::bit_cast<uint32_t>(view[i]); camera.view_projection[i]=std::bit_cast<uint32_t>(vp[i]);
  }
  const auto drawn=NativeFsrJitterCamera(camera,jitter);
  Check(drawn.view==camera.view,"the draw camera's view moved");
  NativeSceneMatrix jittered_vp{};
  for(size_t r=0;r<4;++r) for(size_t c=0;c<4;++c) for(size_t k=0;k<4;++k) jittered_vp[r*4+c]+=view[r*4+k]*jittered[k*4+c];
  for(size_t i=0;i<16;++i)
    Check(Near(std::bit_cast<float>(drawn.view_projection[i]),jittered_vp[i],1e-4f*(1+std::abs(jittered_vp[i]))),
          "the draw camera's view*projection is not view times the jittered projection");
  Check(NativeFsrJitterCamera(camera,NativeFsrJitter{})==camera,"a zero jitter changed the camera");
  std::cout << "jitter projection: NDC and pixel shifts exact, view kept, V*jitter(P) = jitter(V*P)\n";
}

// Near, far and vertical fov from the projection, as FSR's dispatch needs them.
void TestCameraParams() {
  const float n=0.5f,f=5000.f,fov=float(std::numbers::pi/3),y=1/std::tan(fov/2);
  std::array<float,16> p{y*9/16,0,0,0, 0,y,0,0, 0,0,f/(f-n),1, 0,0,-n*f/(f-n),0};
  const auto params=NativeFsrCameraFromProjection(p);
  Check(params.derived && Near(params.near_plane,n,1e-3f) && Near(params.far_plane,f,f*1e-3f) && Near(params.fov_y,fov,1e-5f),
        "camera parameters from a perspective projection: near="+std::to_string(params.near_plane)+" far="+
        std::to_string(params.far_plane)+" fov="+std::to_string(params.fov_y));
  std::array<uint32_t,16> words{};
  for(size_t i=0;i<16;++i) words[i]=std::bit_cast<uint32_t>(p[i]);
  const auto from_words=NativeFsrCameraFromProjectionWords(words);
  Check(from_words.derived && from_words.near_plane==params.near_plane && from_words.far_plane==params.far_plane,"camera from guest words");
  // The jitter does not move them.
  const auto jittered=NativeFsrCameraFromProjectionWords(NativeFsrJitterGuestMatrix(words,0.001f,-0.002f));
  Check(jittered.derived && Near(jittered.near_plane,params.near_plane,1e-6f) && Near(jittered.fov_y,params.fov_y,1e-6f),
        "the jitter moved the camera parameters");
  // The game's own shape, as 821C82C0 builds it: right-handed, P[2][2] =
  // -f/(f-n), P[3][2] = -n f/(f-n), P[2][3] = -1 (the first hardware run
  // logged the defaults for every dispatch because only P[2][3] = 1 derived).
  std::array<float,16> rh{y*9/16,0,0,0, 0,y,0,0, 0,0,-(f/(f-n)),-1, 0,0,-(n*f/(f-n)),0};
  const auto game=NativeFsrCameraFromProjection(rh);
  Check(game.derived && Near(game.near_plane,n,1e-3f) && Near(game.far_plane,f,f*1e-3f) && Near(game.fov_y,fov,1e-5f),
        "camera parameters from the game's right-handed projection: near="+std::to_string(game.near_plane)+" far="+
        std::to_string(game.far_plane)+" fov="+std::to_string(game.fov_y));
  // A reversed-Z matrix (P[2][2] = -n/(f-n), P[3][2] = n f/(f-n)): the same
  // planes, in order.
  std::array<float,16> reversed{y*9/16,0,0,0, 0,y,0,0, 0,0,-n/(f-n),1, 0,0,n*f/(f-n),0};
  const auto flipped=NativeFsrCameraFromProjection(reversed);
  Check(flipped.derived && Near(flipped.near_plane,n,1e-3f) && Near(flipped.far_plane,f,f*1e-3f),"reversed-Z projection");
  // Infinite far (P[2][2] = 1): a huge far.
  auto infinite=p; infinite[10]=1; infinite[14]=-n;
  const auto open=NativeFsrCameraFromProjection(infinite);
  Check(open.derived && Near(open.near_plane,n,1e-5f) && open.far_plane>1e20f,"infinite far projection");
  // Orthographic and degenerate: defaults, not derived.
  const std::array<float,16> ortho{0.01f,0,0,0, 0,0.02f,0,0, 0,0,0.001f,0, 0,0,0,1};
  const auto flat=NativeFsrCameraFromProjection(ortho);
  Check(!flat.derived && flat.near_plane>0 && flat.far_plane>flat.near_plane && flat.fov_y>0,"orthographic projection derived a camera");
  Check(!NativeFsrCameraFromProjection({}).derived,"a zero matrix derived a camera");
  std::cout << "camera parameters: near " << params.near_plane << " far " << params.far_plane << " fov_y " << params.fov_y << '\n';
}

// Workstream B's interface and the fallback without it: no texture means
// zero motion and a reset; B's reset and camera pass through.
void TestMotionPlan() {
  NativeFsrCameraParams pass;
  pass.near_plane=0.5f; pass.far_plane=5000; pass.fov_y=1; pass.derived=true;
  const auto none=PlanNativeFsrMotion(NativeMotionVectorOutput{},pass);
  Check(!none.motion && none.reset && none.camera.near_plane==0.5f && none.camera.far_plane==5000,"the fallback plan");
  NativeMotionVectorOutput b;
  auto* texture=reinterpret_cast<NativeBackendTexture*>(uintptr_t(0x1000));
  b.motion=texture; b.reset=false;
  const auto kept=PlanNativeFsrMotion(b,pass);
  Check(kept.motion==texture && !kept.reset && kept.camera.fov_y==1,"B's vectors without a camera");
  b.reset=true; b.near_plane=2; b.far_plane=100; b.fov_y=0.7f;
  const auto cut=PlanNativeFsrMotion(b,pass);
  Check(cut.reset && cut.camera.near_plane==2 && cut.camera.far_plane==100 && cut.camera.fov_y==0.7f,"B's reset and camera");
  std::cout << "motion plan: " << (EDF_NATIVE_FSR_MOTION_VECTORS?"workstream B's header":"fallback (no native_motion_vectors.h)") << '\n';
}

// History resets: first frame, size, format, a helper call in between that
// did not dispatch (route change), A/B side, mode, a projection change (cut),
// and B's reset; consecutive unchanged frames accumulate.
void TestResetRules() {
  NativeFsrResetTracker tracker;
  NativeFsrFrameFacts facts;
  facts.helper_frame=100; facts.width=1280; facts.height=720; facts.format=10;
  facts.camera.near_plane=0.5f; facts.camera.far_plane=5000; facts.camera.fov_y=1; facts.camera.derived=true;
  Check(tracker.Next(facts)==kNativeFsrResetFirst,"the first frame does not reset");
  const auto step=[&](auto change) { ++facts.helper_frame; change(facts); return tracker.Next(facts); };
  const auto none=[](NativeFsrFrameFacts&) {};
  Check(step(none)==0 && step(none)==0,"consecutive unchanged frames reset");
  Check(step([](auto& f) { f.width=1920; })==kNativeFsrResetResize,"resize");
  Check(step(none)==0,"accumulation after a resize");
  Check(step([](auto& f) { f.format=2; })==kNativeFsrResetFormat,"format change");
  facts.helper_frame+=2;  // one helper call took another route
  Check(tracker.Next(facts)==kNativeFsrResetRoute,"a route gap");
  Check(step(none)==0,"accumulation after a route gap");
  Check(step([](auto& f) { f.ab_native=false; })==kNativeFsrResetAbSide,"A/B side change");
  Check(step([](auto& f) { f.mode=NativeFsrMode::Quality; })==kNativeFsrResetMode,"mode change");
  Check(step([](auto& f) { f.camera.fov_y=1.3f; })==kNativeFsrResetCut,"fov jump (cut)");
  Check(step([](auto& f) { f.camera.fov_y=1.2f; })==0 && step([](auto& f) { f.camera.fov_y=1.0f; })==0,
        "a zoom (0.1 and 0.2 rad per frame) reset");
  Check(step([](auto& f) { f.camera.near_plane=0.2f; })==kNativeFsrResetCut,"near jump (cut)");
  Check(step([](auto& f) { f.camera.near_plane=0.3f; f.camera.far_plane=6000; })==0,"a small near/far change reset");
  Check(step([](auto& f) { f.camera.far_plane=20000; })==kNativeFsrResetCut,"far jump (cut)");
  Check(step([](auto& f) { f.camera.derived=false; })==kNativeFsrResetCut,"losing the derived camera");
  facts.camera.derived=true; ++facts.helper_frame; tracker.Next(facts);
  Check(step([](auto& f) { f.motion_reset=true; })==kNativeFsrResetMotion,"B's reset");
  Check(step([](auto& f) { f.motion_reset=false; f.width=640; })==kNativeFsrResetResize,"several frames then one reason");
  tracker.Forget();
  Check(tracker.Next(facts)==kNativeFsrResetFirst,"Forget does not start over");
  Check(NativeFsrResetTracker::Describe(kNativeFsrResetFirst|kNativeFsrResetCut)=="first+cut" &&
        NativeFsrResetTracker::Describe(0)=="none","reset descriptions");
  std::cout << "reset rules: first, resize, format, route, ab_side, mode, cut, motion\n";
}

// ---------------------------------------------------------------------------
// End to end on WARP: a 64x64 RGBA16F scene with a sampled reversed-Z depth,
// a white slanted triangle drawn with the frame's jitter (as the full frame's
// projection carries it), the opaque colour copied before a small
// "transparent" triangle, the colour resolved into a texture (ResolveScene),
// then GENERATEREACTIVEMASK + the upscale through RecordRaw, 24 frames. The
// output must be finite and closer to the triangle's exact per-pixel
// coverage than one aliased frame is; the DLL's jitter answers must be the
// local sequence; the debug layer must stay quiet.
ComPtr<ID3DBlob> Compile(const char* source,const char* entry,const char* profile) {
  ComPtr<ID3DBlob> code,errors;
  if(FAILED(D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,entry,profile,0,0,&code,&errors)))
    throw std::runtime_error(std::string("shader compile failed: ")+(errors?static_cast<const char*>(errors->GetBufferPointer()):""));
  return code;
}
std::span<const uint8_t> Bytes(ID3DBlob& blob) { return {static_cast<const uint8_t*>(blob.GetBufferPointer()),blob.GetBufferSize()}; }
template<class T> std::span<const uint8_t> Raw(const T& value) { return {reinterpret_cast<const uint8_t*>(&value),sizeof(value)}; }
float Half(uint16_t h) {
  const uint32_t sign=(h>>15)&1,exponent=(h>>10)&31,mantissa=h&1023;
  const float value=exponent==0?std::ldexp(float(mantissa),-24):exponent==31?(mantissa?NAN:INFINITY):
    std::ldexp(float(mantissa|1024),int(exponent)-25);
  return sign?-value:value;
}
std::vector<float> Luma(const std::vector<uint8_t>& halves,uint32_t size) {
  std::vector<float> out(size_t(size)*size);
  for(size_t i=0;i<out.size();++i) {
    uint16_t r; std::memcpy(&r,halves.data()+i*8,2);
    out[i]=Half(r);
  }
  return out;
}
constexpr uint32_t kSize=64;
// The white triangle, NDC.
constexpr float kTriangle[]{-0.9f,-0.9f,0.5f, 0.35f,-0.9f,0.5f, -0.3f,0.85f,0.5f};
// The "transparent" one, over the lower right corner (off the white one).
constexpr float kOverlay[]{0.55f,-0.95f,0.25f, 0.95f,-0.95f,0.25f, 0.95f,-0.5f,0.25f};
bool Inside(float px,float py) {  // pixel coordinates, y down
  const float x=px/kSize*2-1,y=1-py/kSize*2;
  const auto edge=[&](int a,int b) {
    const float ax=kTriangle[a*3],ay=kTriangle[a*3+1],bx=kTriangle[b*3],by=kTriangle[b*3+1];
    return (bx-ax)*(y-ay)-(by-ay)*(x-ax);
  };
  const float e0=edge(0,1),e1=edge(1,2),e2=edge(2,0);
  return (e0>=0 && e1>=0 && e2>=0) || (e0<=0 && e1<=0 && e2<=0);
}
bool OverlayArea(uint32_t x,uint32_t y) { return x>=kSize*0.7f && y>=kSize*0.7f; }

void TestFfxDispatch() {
  const auto staged=NativeFfxLibrary::DefaultPath();
  std::error_code ignored;
  if(!std::filesystem::exists(staged,ignored)) {
    std::cout << "FSR dispatch: SKIPPED, no amd_fidelityfx_dx12.dll next to the test (" << staged.string() << ")\n";
    return;
  }
  auto& library=NativeFsrLibrary();
  if(!library.available()) { std::cout << "FSR dispatch: SKIPPED, " << library.error() << '\n'; return; }
  NativeD3D12Options options;
  options.prefer_warp=true; options.debug_layer=true;
  auto backend=CreateNativeD3D12Backend(options);
  auto* raw=backend->D3D12Raw();
  if(!library.upscaler_available(raw->Device())) { std::cout << "FSR dispatch: SKIPPED, no upscaler provider on WARP\n"; return; }
  // The provider. WARP executes FSR 3.1.4's shaders into an access violation
  // inside d3d10warp.dll (+0x3aa8fd, in ExecuteCommandLists at the first
  // submitted dispatch, with or without the reactive mask), so on WARP the
  // run takes the same DLL's FSR 2.3.3 provider: the same ffx-api upscale
  // context, dispatch descriptions, resources, states and raw pass, which is
  // what this test checks. 3.1.4 itself is skipped with that message;
  // EDF_FSR_TEST_VERSION=<name> picks another provider (3.1.4 to see the
  // crash, or on a WARP that runs it).
  const char* requested=std::getenv("EDF_FSR_TEST_VERSION");
  const std::string wanted=requested?requested:"2.3.3";
  NativeFsrUpscaler upscaler;
  bool found=false;
  for(const auto& v:library.Versions(0x00010000u,raw->Device())) if(v.name==wanted) { upscaler.SetVersionOverride(v.id); found=true; }
  if(!found) { std::cout << "FSR dispatch: SKIPPED, provider " << wanted << " is not in the DLL\n"; return; }
  if(!requested)
    std::cout << "FSR dispatch: 3.1.4 SKIPPED on WARP (its shaders crash d3d10warp.dll); the ffx-api path runs with " << wanted << '\n';
  std::string error;
  const auto started=std::chrono::steady_clock::now();
  if(!upscaler.Prepare(*backend,kSize,kSize,DXGI_FORMAT_R16G16B16A16_FLOAT,&error)) {
    for(const auto& message:DrainNativeFsrMessages()) std::cout << "  FidelityFX: " << message << '\n';
    std::cout << "FSR dispatch: SKIPPED, WARP cannot create the FFX context: " << error << '\n';
    return;
  }
  Check(upscaler.recreated() && upscaler.ready() && upscaler.output() && upscaler.opaque_texture(),"Prepare made no context");
  Check(upscaler.Prepare(*backend,kSize,kSize,DXGI_FORMAT_R16G16B16A16_FLOAT,&error) && !upscaler.recreated(),
        "an unchanged Prepare recreated the context");
  // The DLL's jitter is the local sequence.
  Check(upscaler.jitter_from_ffx(),"the context did not answer GETJITTERPHASECOUNT");
  Check(upscaler.JitterPhaseCount()==NativeFsrJitterPhaseCount(kSize,kSize),
        "FFX phase count "+std::to_string(upscaler.JitterPhaseCount())+" differs from the local one");
  for(int32_t i=0;i<16;++i) {
    const auto ffx=upscaler.JitterOffset(i),local=NativeFsrJitterOffset(i,upscaler.JitterPhaseCount());
    Check(Near(ffx[0],local[0],1e-6f) && Near(ffx[1],local[1],1e-6f),"FFX jitter offset "+std::to_string(i)+" differs from the local one");
  }
  // The scene: colour target, its resolve texture, sampled reversed-Z depth.
  NativeBackendTextureDesc color_desc{};
  color_desc.width=color_desc.height=kSize; color_desc.format=DXGI_FORMAT_R16G16B16A16_FLOAT; color_desc.render_target=true;
  const auto color=backend->CreateRenderTarget(color_desc);
  NativeBackendTextureDesc resolved_desc{};
  resolved_desc.width=resolved_desc.height=kSize; resolved_desc.format=DXGI_FORMAT_R16G16B16A16_FLOAT;
  const auto resolved=backend->CreateTexture(resolved_desc,{});
  NativeBackendTextureDesc depth_desc{};
  depth_desc.width=depth_desc.height=kSize; depth_desc.format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  depth_desc.depth=true; depth_desc.render_target=true; depth_desc.sampled=true; depth_desc.clear_depth=0;
  const auto depth=backend->CreateRenderTarget(depth_desc);
  const NativeBackendInputElement layout[]{{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,false,0}};
  static const char* kSource=R"(
cbuffer V : register(b0) { float4 jitter; };
float4 VS(float3 p : POSITION) : SV_POSITION { return float4(p.xy + jitter.xy, p.z, 1); }
cbuffer P : register(b0) { float4 tint; };
float4 PS(float4 p : SV_POSITION) : SV_TARGET { return tint; }
)";
  const auto vs=Compile(kSource,"VS","vs_5_0"),ps=Compile(kSource,"PS","ps_5_0");
  NativeBackendPipelineDesc pipeline_desc{};
  pipeline_desc.vertex=Bytes(*vs.Get()); pipeline_desc.pixel=Bytes(*ps.Get());
  pipeline_desc.vertex_id=0xF5A0; pipeline_desc.pixel_id=0xF5A1;
  pipeline_desc.input_layout=layout; pipeline_desc.input_layout_id=0xF5A2;
  pipeline_desc.state={0x10001,0x46,0,0,15,0};  // depth test GREATER with write (reversed Z)
  pipeline_desc.render_targets=1; pipeline_desc.rtv_format[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;
  pipeline_desc.dsv_format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  auto& pipeline=backend->CreatePipeline(pipeline_desc);
  NativeBackendBufferDesc buffer_desc{}; buffer_desc.bytes=sizeof(kTriangle); buffer_desc.vertex=true;
  const auto triangle=backend->CreateBuffer(buffer_desc,Raw(kTriangle));
  const auto overlay=backend->CreateBuffer(buffer_desc,Raw(kOverlay));
  NativeBackendRenderTarget* colors[]{color.get()};
  const std::array<float,4> white{1,1,1,1},glow{2,0.5f,0.1f,1};
  constexpr uint32_t kFrames=24;
  std::vector<float> aliased;
  for(uint32_t frame=0;frame<kFrames;++frame) {
    const auto jitter=MakeNativeFsrJitter(upscaler.JitterOffset(int32_t(frame)),kSize,kSize);
    backend->BeginFrame();
    auto& recorder=backend->Recorder();
    recorder.SetRenderTargets(colors,depth.get());
    recorder.SetViewport({0,0,float(kSize),float(kSize),0,1});
    recorder.ClearColor(*color,{0,0,0,1});
    recorder.ClearDepthStencil(*depth,true,true,0.0f,0);
    recorder.SetPipeline(pipeline);
    const std::array<float,4> offset{jitter.clip_x,jitter.clip_y,0,0};
    recorder.SetConstants(NativeBackendStage::Vertex,0,Raw(offset));
    recorder.SetConstants(NativeBackendStage::Pixel,0,Raw(white));
    recorder.SetVertexBuffer(0,*triangle,12,0);
    recorder.Draw(3,0);
    // The opaque-only colour, then the "transparent" draw.
    recorder.ResolveTarget(*upscaler.opaque_texture(),*color);
    recorder.SetRenderTargets(colors,depth.get());
    recorder.SetViewport({0,0,float(kSize),float(kSize),0,1});
    recorder.SetPipeline(pipeline);
    recorder.SetConstants(NativeBackendStage::Vertex,0,Raw(offset));
    recorder.SetConstants(NativeBackendStage::Pixel,0,Raw(glow));
    recorder.SetVertexBuffer(0,*overlay,12,0);
    recorder.Draw(3,0);
    recorder.ResolveTarget(*resolved,*color);
    NativeFsrDispatchInputs inputs;
    inputs.color=resolved.get(); inputs.depth=depth.get(); inputs.opaque=true;
    inputs.jitter=jitter; inputs.motion_scale={float(kSize),float(kSize)};
    inputs.sharpness=0; inputs.frame_ms=16.6667f; inputs.reset=frame==0;
    inputs.camera.near_plane=0.5f; inputs.camera.far_plane=5000; inputs.camera.fov_y=1; inputs.camera.derived=true;
    try { upscaler.Dispatch(*backend,recorder,inputs); }
    catch(const std::exception& failure) {
      backend->Submit();
      for(const auto& message:DrainNativeFsrMessages()) std::cout << "  FidelityFX: " << message << '\n';
      Check(false,std::string("FSR dispatch threw: ")+failure.what());
      return;
    }
    backend->Submit();
    if(frame==0) aliased=Luma(backend->ReadTexture(*resolved),kSize);
  }
  const auto output=Luma(backend->ReadTexture(*upscaler.output()),kSize);
  const auto context_bytes=upscaler.stats().context_bytes;
  for(const auto& message:DrainNativeFsrMessages()) std::cout << "  FidelityFX: " << message << '\n';
  const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
  // Exact coverage of the white triangle (16x16 samples a pixel), and the
  // error of the FSR output and of the first (aliased, jittered) frame
  // against it, away from the transparent overlay.
  double fsr_error=0,aliased_error=0,edge_fsr=0,edge_aliased=0;
  uint32_t finite=0,compared=0,edges=0;
  for(uint32_t y=0;y<kSize;++y) for(uint32_t x=0;x<kSize;++x) {
    const float value=output[y*kSize+x];
    finite+=std::isfinite(value);
    if(OverlayArea(x,y)) continue;
    uint32_t covered=0;
    for(uint32_t sy=0;sy<16;++sy) for(uint32_t sx=0;sx<16;++sx) covered+=Inside(x+(sx+0.5f)/16,y+(sy+0.5f)/16);
    const float coverage=covered/256.f;
    const double e=std::abs(value-coverage),a=std::abs(aliased[y*kSize+x]-coverage);
    fsr_error+=e; aliased_error+=a; ++compared;
    if(coverage>0.02f && coverage<0.98f) { edge_fsr+=e; edge_aliased+=a; ++edges; }
  }
  fsr_error/=compared; aliased_error/=compared; edge_fsr/=(std::max)(edges,1u); edge_aliased/=(std::max)(edges,1u);
  Check(finite==kSize*kSize,"the FSR output has non-finite pixels");
  Check(fsr_error<0.08,"the FSR output is not the scene (mean error "+std::to_string(fsr_error)+")");
  Check(edge_fsr<edge_aliased,"FSR's edges are no closer to the coverage than one aliased frame ("+
        std::to_string(edge_fsr)+" vs "+std::to_string(edge_aliased)+")");
  const auto& stats=upscaler.stats();
  Check(stats.dispatches==kFrames && stats.reactive==kFrames && stats.resets==1 && stats.contexts==1,"upscaler statistics");
  const auto messages=backend->DrainValidationMessages();
  for(const auto& message:messages) std::cerr << "  validation: " << message << '\n';
  Check(messages.empty(),"the debug layer reported "+std::to_string(messages.size())+" message(s) around the FSR dispatch");
  // A size change makes a new context; the old one is destroyed after
  // kRetireFrames further Prepares.
  Check(upscaler.Prepare(*backend,kSize/2,kSize/2,DXGI_FORMAT_R16G16B16A16_FLOAT,&error) && upscaler.recreated() &&
        upscaler.width()==kSize/2 && upscaler.stats().contexts==2,"a resize did not recreate the context");
  for(uint32_t i=0;i<NativeFsrUpscaler::kRetireFrames;++i) upscaler.Prepare(*backend,kSize/2,kSize/2,DXGI_FORMAT_R16G16B16A16_FLOAT,&error);
  Check(upscaler.stats().retired==1,"the replaced context was not retired");
  std::cout << "FSR dispatch (" << upscaler.provider() << ", WARP): " << kFrames << " frames in " << seconds
            << " s; mean |error| vs coverage " << fsr_error << " (aliased frame " << aliased_error << "), edges "
            << edge_fsr << " vs " << edge_aliased << " over " << edges << " edge pixels; context GPU memory "
            << context_bytes/1024 << " KiB at " << kSize << "x" << kSize << '\n';
}

// Upscaling on WARP, the way the full frame does it: display-size targets
// (the scene's), the frame drawn into their render-size top-left corner (the
// viewport mapped by ScaleNativeViewport) with render-pixel jitter, the whole
// colour resolved, and FSR told renderSize = the corner, upscaleSize = the
// display. The same frames are also drawn into targets exactly the render
// size (FSR's plain use); the two outputs must agree, which shows the
// resources' extra width and height are ignored rather than read (the
// sub-rectangle scheme FFX's dynamic resolution is made for). The output must
// also be the scene at display resolution: finite, covering the triangle at
// display size, and closer to its exact coverage than the aliased render
// frame stretched up (nearest).
void TestFfxUpscaleDispatch() {
  const auto staged=NativeFfxLibrary::DefaultPath();
  std::error_code ignored;
  if(!std::filesystem::exists(staged,ignored)) { std::cout << "FSR upscale dispatch: SKIPPED, no DLL\n"; return; }
  auto& library=NativeFsrLibrary();
  if(!library.available()) { std::cout << "FSR upscale dispatch: SKIPPED, " << library.error() << '\n'; return; }
  NativeD3D12Options options;
  options.prefer_warp=true; options.debug_layer=true;
  auto backend=CreateNativeD3D12Backend(options);
  auto* raw=backend->D3D12Raw();
  if(!library.upscaler_available(raw->Device())) { std::cout << "FSR upscale dispatch: SKIPPED, no upscaler provider\n"; return; }
  const char* requested=std::getenv("EDF_FSR_TEST_VERSION");
  const std::string wanted=requested?requested:"2.3.3";  // see TestFfxDispatch: 3.1.4 crashes WARP
  uint64_t version=0;
  for(const auto& v:library.Versions(0x00010000u,raw->Device())) if(v.name==wanted) version=v.id;
  if(!version) { std::cout << "FSR upscale dispatch: SKIPPED, provider " << wanted << " is not in the DLL\n"; return; }
  constexpr uint32_t kDisplay=kSize;
  const auto render=NativeFsrRenderSizeFor(NativeFsrMode::Performance,kDisplay,kDisplay);  // 32x32
  const NativeRenderScale scale{render.width,render.height,kDisplay,kDisplay};
  const NativeBackendInputElement layout[]{{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,false,0}};
  static const char* kSource=R"(
cbuffer V : register(b0) { float4 jitter; };
float4 VS(float3 p : POSITION) : SV_POSITION { return float4(p.xy + jitter.xy, p.z, 1); }
cbuffer P : register(b0) { float4 tint; };
float4 PS(float4 p : SV_POSITION) : SV_TARGET { return tint; }
)";
  const auto vs=Compile(kSource,"VS","vs_5_0"),ps=Compile(kSource,"PS","ps_5_0");
  NativeBackendPipelineDesc pipeline_desc{};
  pipeline_desc.vertex=Bytes(*vs.Get()); pipeline_desc.pixel=Bytes(*ps.Get());
  pipeline_desc.vertex_id=0xF5B0; pipeline_desc.pixel_id=0xF5B1;
  pipeline_desc.input_layout=layout; pipeline_desc.input_layout_id=0xF5B2;
  pipeline_desc.state={0x10001,0x46,0,0,15,0};
  pipeline_desc.render_targets=1; pipeline_desc.rtv_format[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;
  pipeline_desc.dsv_format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  auto& pipeline=backend->CreatePipeline(pipeline_desc);
  NativeBackendBufferDesc buffer_desc{}; buffer_desc.bytes=sizeof(kTriangle); buffer_desc.vertex=true;
  const auto triangle=backend->CreateBuffer(buffer_desc,Raw(kTriangle));
  const std::array<float,4> white{1,1,1,1};
  constexpr uint32_t kFrames=48;
  // One run: targets of `extent` (the display's: the corner scheme; the
  // render's: FSR's plain use). Returns the display-size output's luma, and
  // the first frame's aliased render-size image.
  std::vector<float> aliased;
  bool ok=true;
  const auto run=[&](uint32_t extent,std::vector<float>* first) -> std::vector<float> {
    NativeFsrUpscaler upscaler;
    upscaler.SetVersionOverride(version);
    std::string error;
    if(!upscaler.Prepare(*backend,render.width,render.height,kDisplay,kDisplay,DXGI_FORMAT_R16G16B16A16_FLOAT,&error)) {
      for(const auto& message:DrainNativeFsrMessages()) std::cout << "  FidelityFX: " << message << '\n';
      std::cout << "FSR upscale dispatch: SKIPPED, WARP cannot create the FFX context: " << error << '\n';
      ok=false; return {};
    }
    Check(upscaler.width()==render.width && upscaler.display_width()==kDisplay,"upscaler sizes");
    Check(upscaler.JitterPhaseCount()==NativeFsrJitterPhaseCount(render.width,kDisplay) && upscaler.JitterPhaseCount()==32,
          "FFX phase count at 2x is "+std::to_string(upscaler.JitterPhaseCount()));
    for(int32_t i=0;i<40;++i) {
      const auto ffx=upscaler.JitterOffset(i),local=NativeFsrJitterOffset(i,upscaler.JitterPhaseCount());
      Check(Near(ffx[0],local[0],1e-6f) && Near(ffx[1],local[1],1e-6f),"FFX 2x jitter offset "+std::to_string(i));
    }
    NativeBackendTextureDesc color_desc{};
    color_desc.width=color_desc.height=extent; color_desc.format=DXGI_FORMAT_R16G16B16A16_FLOAT; color_desc.render_target=true;
    const auto color=backend->CreateRenderTarget(color_desc);
    NativeBackendTextureDesc resolved_desc{};
    resolved_desc.width=resolved_desc.height=extent; resolved_desc.format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    const auto resolved=backend->CreateTexture(resolved_desc,{});
    // The opaque copy is a whole copy of the scene target: its size here.
    const auto opaque=backend->CreateTexture(resolved_desc,{});
    NativeBackendTextureDesc depth_desc{};
    depth_desc.width=depth_desc.height=extent; depth_desc.format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    depth_desc.depth=true; depth_desc.render_target=true; depth_desc.sampled=true; depth_desc.clear_depth=0;
    const auto depth=backend->CreateRenderTarget(depth_desc);
    NativeBackendRenderTarget* colors[]{color.get()};
    // The display viewport, as the guest describes it, mapped into the corner
    // (or the render size itself when the targets are that size).
    const auto viewport=extent==kDisplay?ScaleNativeViewport({0,0,float(kDisplay),float(kDisplay),0,1},scale):
      NativeBackendViewport{0,0,float(render.width),float(render.height),0,1};
    Check(viewport.width==float(render.width) && viewport.height==float(render.height),"the mapped viewport is the render size");
    for(uint32_t frame=0;frame<kFrames;++frame) {
      const auto jitter=MakeNativeFsrJitter(upscaler.JitterOffset(int32_t(frame)),render.width,render.height);
      backend->BeginFrame();
      auto& recorder=backend->Recorder();
      recorder.SetRenderTargets(colors,depth.get());
      recorder.SetViewport(viewport);
      recorder.ClearColor(*color,{0,0,0,1});
      recorder.ClearDepthStencil(*depth,true,true,0.0f,0);
      recorder.SetPipeline(pipeline);
      const std::array<float,4> offset{jitter.clip_x,jitter.clip_y,0,0};
      recorder.SetConstants(NativeBackendStage::Vertex,0,Raw(offset));
      recorder.SetConstants(NativeBackendStage::Pixel,0,Raw(white));
      recorder.SetVertexBuffer(0,*triangle,12,0);
      recorder.Draw(3,0);
      recorder.ResolveTarget(*resolved,*color);
      NativeFsrDispatchInputs inputs;
      inputs.color=resolved.get(); inputs.depth=depth.get(); inputs.opaque=false;
      inputs.jitter=jitter; inputs.motion_scale={float(render.width),float(render.height)};
      inputs.sharpness=0; inputs.frame_ms=16.6667f; inputs.reset=frame==0;
      inputs.camera.near_plane=0.5f; inputs.camera.far_plane=5000; inputs.camera.fov_y=1; inputs.camera.derived=true;
      try { upscaler.Dispatch(*backend,recorder,inputs); }
      catch(const std::exception& failure) {
        backend->Submit();
        for(const auto& message:DrainNativeFsrMessages()) std::cout << "  FidelityFX: " << message << '\n';
        Check(false,std::string("FSR upscale dispatch threw: ")+failure.what());
        ok=false; return {};
      }
      backend->Submit();
      if(frame==0 && first) {
        const auto whole=Luma(backend->ReadTexture(*resolved),extent);
        first->assign(size_t(render.width)*render.height,0);
        for(uint32_t y=0;y<render.height;++y) for(uint32_t x=0;x<render.width;++x) (*first)[y*render.width+x]=whole[y*extent+x];
      }
    }
    auto output=Luma(backend->ReadTexture(*upscaler.output()),kDisplay);
    for(const auto& message:DrainNativeFsrMessages()) std::cout << "  FidelityFX: " << message << '\n';
    return output;
  };
  const auto corner=run(kDisplay,&aliased);
  if(!ok) return;
  std::vector<float> plain_aliased;
  const auto plain=run(render.width,&plain_aliased);
  if(!ok) return;
  Check(aliased==plain_aliased,"the corner and the render-size target rasterized differently");
  double max_difference=0;
  for(size_t i=0;i<corner.size();++i) max_difference=(std::max)(max_difference,double(std::abs(corner[i]-plain[i])));
  Check(max_difference<1e-3,"the corner scheme's output differs from FSR over render-size resources (max "+
        std::to_string(max_difference)+")");
  // Error against the exact coverage at display pixels; the aliased
  // reference is the first frame stretched to the display (nearest).
  double fsr_error=0,stretched_error=0;
  uint32_t finite=0,lit=0,covered_area=0;
  for(uint32_t y=0;y<kDisplay;++y) for(uint32_t x=0;x<kDisplay;++x) {
    const float value=corner[y*kDisplay+x];
    finite+=std::isfinite(value);
    lit+=value>0.5f;
    covered_area+=Inside(x+0.5f,y+0.5f);
    uint32_t covered=0;
    for(uint32_t sy=0;sy<16;++sy) for(uint32_t sx=0;sx<16;++sx) covered+=Inside(x+(sx+0.5f)/16,y+(sy+0.5f)/16);
    const float coverage=covered/256.f;
    const float stretched=aliased[(y*render.height/kDisplay)*render.width+x*render.width/kDisplay];
    fsr_error+=std::abs(value-coverage); stretched_error+=std::abs(stretched-coverage);
  }
  fsr_error/=double(kDisplay)*kDisplay; stretched_error/=double(kDisplay)*kDisplay;
  Check(finite==kDisplay*kDisplay,"the upscaled output has non-finite pixels");
  // A corner-only output (the render rectangle not upscaled) would light
  // about a quarter of the triangle's display-size area.
  Check(std::abs(double(lit)-covered_area)<0.15*covered_area,"the upscaled output does not cover the triangle at display size (lit "+
        std::to_string(lit)+" of "+std::to_string(covered_area)+")");
  Check(fsr_error<stretched_error,"the upscaled output is no closer to the coverage than the stretched aliased frame ("+
        std::to_string(fsr_error)+" vs "+std::to_string(stretched_error)+")");
  const auto messages=backend->DrainValidationMessages();
  for(const auto& message:messages) std::cerr << "  validation: " << message << '\n';
  Check(messages.empty(),"the debug layer reported "+std::to_string(messages.size())+" message(s) around the upscale dispatch");
  std::cout << "FSR upscale dispatch (" << wanted << ", WARP, " << render.width << "x" << render.height << " -> "
            << kDisplay << "x" << kDisplay << "): corner of display-size targets vs render-size targets max |diff| "
            << max_difference << "; mean |error| vs coverage " << fsr_error << " (stretched aliased " << stretched_error
            << "), lit " << lit << " of " << covered_area << '\n';
}
}  // namespace

int main() {
  std::cout << std::unitbuf;
  // A crash inside the FidelityFX DLL or the driver names its module.
  SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* info) -> LONG {
    const auto* record=info->ExceptionRecord;
    HMODULE module=nullptr; char name[MAX_PATH]="?";
    if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          static_cast<LPCSTR>(record->ExceptionAddress),&module) && module)
      GetModuleFileNameA(module,name,MAX_PATH);
    std::cerr << "FAIL: exception 0x" << std::hex << record->ExceptionCode << " at " << record->ExceptionAddress
              << " (" << name << "+0x" << (reinterpret_cast<uintptr_t>(record->ExceptionAddress)-reinterpret_cast<uintptr_t>(module))
              << ")" << std::dec << '\n';
    return EXCEPTION_EXECUTE_HANDLER;
  });
  try {
    TestModes();
    TestUpscaleSizes();
    TestRenderScale();
    TestMipBiasFiltering();
    TestRenderRequest();
    TestJitterSequence();
    TestJitterProjection();
    TestCameraParams();
    TestMotionPlan();
    TestResetRules();
    TestFfxDispatch();
    TestFfxUpscaleDispatch();
  } catch(const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  if(failures) { std::cerr << failures << " failure(s)\n"; return 1; }
  std::cout << "all FSR integration checks passed\n";
  return 0;
}

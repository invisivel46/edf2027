#pragma once
#include "native_graphics/native_post_finish_plan.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The finish/post stage 820B0B80 recorded with zero guest calls, for the
// full-frame native renderer. Everything is read from guest memory (read only)
// and derived from native_post_finish_plan.h; nothing here calls a guest
// function or writes a guest device mirror.
//
// Tone source. MiddleGray, LuminanceWhite, ToneMap and LuminanceVector are
// SHARED effect parameters: they live in the effect pool at [8257C02C], a
// std::map<std::string,parameter> the post effects read at activation.
// - 820B1028 (the post owner's constructor) registers them (821A3660) and sets
//   their defaults through 821A2228: MiddleGray (0.5,0,0,1) [820008D4],
//   ToneMap (1,0,0,1) [820008CC], LuminanceWhite (1.5,0,0,1) [82001F98],
//   LuminanceVector (0.6154,0.7154,0.0721,1) [82001F94/90/8C].
// - 820B5718(manager [82578678], index) applies environment preset `index`:
//   preset = table+[table+28]+index*172 with table=[manager+124] and the count
//   at [table+24]; it sets MiddleGray (p+108,0,0,1), LuminanceWhite
//   (p+112,0,0,1), ToneMap (p+116,0,0,1), LuminanceVector (p+120..128,1). The
//   index is chosen by VM command 101 (820D1518, operand from the script
//   stack), by 820D1260 through the table at 82003CB0, or is 0 on map load
//   (820BF230/820BF390/820BF548 after 820B4D58), and 820BD3C0 re-applies the
//   word at its r3. 820B5718 does not keep the index.
// - clLightingWindow::slot9 (820C4D70, the lighting editor) saves the pool
//   (820C4700 via 821A2278) and writes it back (820C4A38 via 821A2228).
// So the pool holds the live values every frame; there is no CPU-side
// per-frame adaptation. The adaptation is the tone-history pass: the last
// second-pyramid pass (PS_Downsample_Tone) samples its own previous texture as
// m_OldTone and blends on the GPU. The native read is therefore the pool entry,
// each frame.
namespace edf::native {
struct PostToneLayout {
  static constexpr uint32_t kPoolGlobal=0x8257C02Cu;      // lis -32168; lwz -16340
  static constexpr uint32_t kPoolHead=4;                  // 821A1D80: [pool+4] is the map head
  static constexpr uint32_t kNodeLeft=0,kNodeParent=4,kNodeRight=8,kNodeKey=12,kNodeValue=40,kNodeIsNil=61;
  static constexpr uint32_t kStringBuffer=4,kStringSize=20,kStringCapacity=24,kStringInline=16;
  static constexpr uint32_t kValueData=0,kValueCount=8;   // 821A16D8: float4s at [value+0], count [value+8]
  static constexpr uint32_t kManagerGlobal=0x82578678u;   // lis -32168; lwz -31112
  static constexpr uint32_t kManagerTable=124,kTableCount=24,kTableBase=28,kPresetStride=172;
  static constexpr uint32_t kPresetMiddleGray=108,kPresetLuminanceWhite=112,kPresetToneMap=116,kPresetLuminanceVector=120;
  static constexpr size_t kMaxDepth=64,kMaxKey=64;
};
inline constexpr const char* kPostMiddleGray="g_PostEffect_MiddleGray";
inline constexpr const char* kPostLuminanceWhite="g_PostEffect_LuminanceWhite";
inline constexpr const char* kPostToneMap="g_PostEffect_ToneMap";
inline constexpr const char* kPostLuminanceVector="g_PostEffect_LuminanceVector";

// Read-only guest memory. Bytes throws on an unreadable range.
class PostGuestMemory {
 public:
  virtual ~PostGuestMemory()=default;
  virtual const uint8_t* Bytes(uint32_t address,size_t size) const=0;
  uint32_t Word(uint32_t address) const;
  uint8_t Byte(uint32_t address) const { return *Bytes(address,1); }
  float Float(uint32_t address) const;
};

enum class NativePostToneOrigin:uint8_t { Pool, Preset };
struct NativePostTone {
  std::array<float,4> middle_gray{},luminance_white{},tone_map{},luminance_vector{};
  NativePostToneOrigin origin=NativePostToneOrigin::Pool;
  std::array<float,3> Scalars() const { return {middle_gray[0],luminance_white[0],tone_map[0]}; }
};
// 821A20C0 natively: the pool map's lower_bound by 821A1868's compare, then
// the value's first float4. nullopt when the name is not registered.
std::optional<std::array<float,4>> ReadPostPoolVector(const PostGuestMemory& memory,const char* name);
// The live tone constants, from the pool. Throws when one is not registered
// (before 820B1028 has run) or its value is unreadable or not finite.
NativePostTone ReadNativePostTone(const PostGuestMemory& memory);
// Preset `index` as 820B5718 would set it. For diagnostics and tests: the
// pool is what the frame uses, since the applied index is not kept.
NativePostTone ReadNativePostTonePreset(const PostGuestMemory& memory,uint32_t index);
// ReadPostFinishInput over PostGuestMemory, with the tone from the pool.
PostFinishInput ReadNativePostInput(const PostGuestMemory& memory,uint32_t self,const NativePostTone& tone);

// One recorded draw: everything the backend sink binds, by reflected name.
struct NativePostTexture { std::string name; uint32_t handle=0; };
// 821BCF28's record words (+16/+20/+24) and value (+12); only the passes that
// call it have one, the others keep the effect's own sampler state.
struct NativePostSampler { std::string name; std::array<uint32_t,3> words{}; float value=0; };
struct NativePostConstant { std::string name; std::vector<float> values; }; // float4s
struct NativePostDraw {
  PostPassKind kind=PostPassKind::Downsample;
  size_t pass=0;
  uint32_t target=0,target_texture=0; // the record; 0 for the bloom
  int32_t width=0,height=0;
  bool output=false; // the bloom composite into the presentation target
  uint32_t technique=0;
  std::vector<NativePostTexture> textures;
  std::vector<NativePostSampler> samplers;
  std::vector<NativePostConstant> constants;
  PostQuad quad{};
};
struct NativePostFrame {
  PostFinishPlan plan;
  NativePostTone tone;
  std::vector<NativePostDraw> draws;
};
// The plan's setters as bindings, plus the four pool constants on every draw
// (the sink sets a name only where the native shader reflects it). Throws on a
// plan that is not the finish sequence.
NativePostFrame BuildNativePostFrame(const PostFinishPlan& plan,const NativePostTone& tone);
const NativePostConstant* FindPostConstant(const NativePostDraw& draw,const char* name);
const NativePostTexture* FindPostTexture(const NativePostDraw& draw,const char* name);
// Whether the draw samples its own record's texture (+4), i.e. the target's
// resolved copy, never its surface. Two passes do so by design:
// - DownsampleTone reads its own previous resolve as m_OldTone (the history);
// - BlurH (820B04B8) draws into this+500, the record the Tone pass has just
//   resolved, while reading that resolve (this+504). On the Xbox the draw goes
//   to EDRAM and the texture is overwritten only by the resolve that ends the
//   pass; natively the surface and the resolved texture are separate resources
//   (CreateNativeRenderTarget), so the same order holds.
// Any other self-read is a plan error.
bool NativePostReadsOwnResolve(const NativePostDraw& draw);
bool NativePostOwnResolveAllowed(const NativePostDraw& draw);

// What records the draws. One call per draw, in order; the chain passes each
// draw into and then resolve their record's target, the bloom draws into the
// presentation target. Throwing stops the frame.
class NativePostSink {
 public:
  virtual ~NativePostSink()=default;
  virtual void Draw(const NativePostDraw& draw)=0;
};
// The post chain and bloom composite of 820B0B80 for post owner `self`
// (clSgsCoreRender), from guest memory alone. Returns what was recorded.
NativePostFrame RecordNativePost(NativePostSink& sink,const PostGuestMemory& memory,uint32_t self);
NativePostFrame RecordNativePost(NativePostSink& sink,const PostFinishInput& input,const NativePostTone& tone);

// The renderer's sink, in guest_shader_bridge.cpp: resolves the HDR scene of
// the screen owner [8257BFB4] into owner+104 as the 8219C930 hook does (mode
// 1), records every pass through the native target registry, the QuadStream
// seam and the registered PostEffect shaders, and composites into the owner's
// ordinary output, which it leaves active for presentation. False, with the
// reason, when the frame could not be recorded; draws already recorded stay
// recorded and the output is left invalid.
bool RecordNativeFullFramePost(uint8_t* base,uint32_t self,bool resolve_scene,std::string* error=nullptr);
}

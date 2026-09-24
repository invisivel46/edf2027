#pragma once
// The full frame's shared pass helpers: the scene targets and view fields every scene pass binds, the
// coverage census glue, the declines, and NativeFullFrameModelsShared, the per-view handoff from the Models,
// Sky and Effects passes to the Transparent pass and the host. Moved from guest_shader_bridge.cpp unchanged,
// out of its anonymous namespace; the functions are defined in full_frame_shared.cpp.
#include "../../bridge/bridge_helpers.h"
#include "../../native_coverage_census.h"
#include "../../native_full_frame.h"
#include "../../native_full_frame_effects.h"
#include "../../native_full_frame_models.h"
#include "../../native_map_effects.h"
#include "../../native_motion_vector_pass.h"
#include "../../native_scene_pass_inputs.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace edf::native {
// The open scene's targets and the view's viewport, as every full-frame scene
// pass binds them: pass formats for pipelines, the backend viewport/scissor.
template<class BridgeState>
auto NativeFullFrameSceneTargetsLocked(BridgeState& state,const edf::native::NativeFrameViewport& v,
    edf::native::NativeFullFramePassTargets& pass,edf::native::NativeBackendViewport& backend_viewport,
    edf::native::NativeBackendScissor& backend_scissor,edf::native::NativeViewportState& viewport) {
  const auto targets=edf::native::ActiveTargetsLocked(state);
  viewport=edf::native::MakeNativeDrawViewport(v.x,v.y,v.width,v.height,v.min_depth,v.max_depth,false,{});
  pass.count=targets.count; pass.rtv_format=targets.rtv_format;
  pass.dsv_format=targets.dsv_format; pass.samples=targets.samples;
  pass.reverse_depth=viewport.reverse_depth;
  const auto& d=viewport.viewport; const auto& s=viewport.scissor;
  backend_viewport={d.TopLeftX,d.TopLeftY,d.Width,d.Height,d.MinDepth,d.MaxDepth};
  backend_scissor={s.left,s.top,s.right,s.bottom};
  return targets;
}
// The camera fields the publication does not carry yet, read from the view:
// the visibility view (scene+96 matrix, +288 frustum, +400 depth scale, as
// 821C61D8/820B4038 read them through context+16/+8).
template<class Reader>
edf::native::NativeSceneVisibilityView NativeFullFrameVisibilityView(const Reader& reader,uint32_t scene) {
  edf::native::NativeSceneVisibilityView view;
  view.matrix=edf::native::ReadNativeVisibilityFloats<16>(reader,reader.Add(scene,96));
  view.frustum=edf::native::ReadNativeVisibilityFloats<26>(reader,reader.Add(scene,288));
  view.depth_scale=std::bit_cast<float>(reader.Word(reader.Add(scene,400)));
  return view;
}
// One world animation per frame (one world in practice); with several, the
// materials that need one decline rather than take the wrong world's.
inline std::optional<edf::native::NativeScenePassAnimation> NativeFullFrameAnimation(const edf::native::NativeFrameInputs& inputs) {
  if(inputs.animations && inputs.animations->size()==1) return inputs.animations->begin()->second;
  return std::nullopt;
}
// 821A1738's runtime palette clamp: Word(descriptor+16) of *(*(8257C02C)+36).
template<class Reader>
uint32_t NativeFullFramePaletteLimit(const Reader& reader) {
  const auto descriptor=reader.Word(reader.Add(reader.Word(0x8257c02c),36));
  return descriptor?reader.Word(reader.Add(descriptor,16)):edf::native::kNativeBonePaletteShaderBones;
}
// edf_native_coverage_census (native_coverage_census.h): the full frame's
// counting sites. Off, each site costs this one cvar read.
inline bool NativeCoverageCensusOn() { return REXCVAR_GET(edf_native_coverage_census); }
inline double NativeCoverageNow() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
// An object's vtable and slot 4 (vtable+16) as the census names its class;
// zeros when the object cannot be read.
template<class Reader>
std::pair<uint32_t,uint32_t> NativeCoverageClassOf(const Reader& reader,uint32_t object) {
  try {
    const auto vtable=object?reader.Word(object):0u;
    return {vtable,vtable?reader.Word(reader.Add(vtable,16)):0u};
  } catch(const std::exception&) { return {0u,0u}; }
}
// The world list (owner+44) of one view: every manager whose slot 2 the guest
// helper calls (821A51D8), covered when a native pass replaces that manager's
// walk (NativeCoverageWorldListPass), else uncovered with its slot 2.
template<class Reader>
void CensusNativeWorldList(const Reader& reader,uint32_t owner) {
  using namespace edf::native;
  std::vector<NativeCoverageMark> marks;
  try {
    const auto end=reader.Word(reader.Add(owner,NativeWorldList::end));
    uint32_t guard=0;
    for(auto node=reader.Word(reader.Add(owner,NativeWorldList::head));node!=end;node=reader.Word(reader.Add(node,NativeWorldList::next))) {
      if(++guard>NativeWorldList::limit) throw std::runtime_error("native world list does not terminate");
      const auto object=reader.Word(reader.Add(node,NativeWorldList::object));
      const auto vtable=object?reader.Word(object):0u;
      if(NativeCoverageWorldListPass(vtable)) marks.push_back({NativeCoverageStatus::Covered,vtable,nullptr,"world_list"});
      else marks.push_back({NativeCoverageStatus::Uncovered,vtable,nullptr,"world_list_unhandled",vtable?reader.Word(reader.Add(vtable,8)):0u});
    }
  } catch(const std::exception& error) {
    CoverageCensus().Add(NativeCoverageStatus::Uncovered,0,"world_list","world_list_unreadable",1,error.what());
  }
  CoverageCensus().Add(marks);
}
// A provider failure that dropped no draw: a change probe (the models' source
// memo validation, the source audit's refetch) or a step a retry recovered
// from. Logged once per reason; it is not a decline and the census does not
// count it (an item that does lose its draw is counted where it is dropped).
void NativeFullFrameNoted(const char* pass,const std::string& reason);
void NativeFullFrameDeclined(const char* pass,const std::string& reason);
// Whether the effect recording carries activations across items and encodes
// vertices before taking the locks (edf_native_effect_activation_share).
inline bool NativeEffectActivationShare() { return REXCVAR_GET(edf_native_effect_activation_share); }
// Encodes every draw's vertices (EncodeNativeEffectDrawCalls) off the bridge
// locks, when NativeEffectActivationShare; the recording uses them as it would
// have encoded them.
template<class Items>
void EncodeNativeEffectItems(Items& items) {
  if(!NativeEffectActivationShare()) return;
  HookTiming timing(HookPhase::FrameNativeEffectEncode);
  for(auto& item:items) for(auto& draw:item.draws) EncodeNativeEffectDrawCalls(draw);
}
// What the Models, Sky (map effects) and Effects passes of one view hand to its
// Transparent pass: the models' transparent batches (one item each, keyed),
// the map effects' and the effects' filed items, and the filing counts that
// place them in one sequence. The models' filing order is the guest's gather
// order among models (ReadNativeFullFrameModelGather), but every model is
// taken as filed before every effect; the two managers file in world-list
// order (NativeMapEffectFilingBases); only equal keys can tell.
struct NativeFullFrameModelsShared {
  std::shared_ptr<edf::native::NativeFullFrameModelFrame> transparent;
  std::vector<edf::native::NativeEffectItem> effects;
  std::vector<edf::native::NativeEffectItem> map_effects;  // orders from 0 within the map-effect walk
  uint32_t model_order=0;
  uint32_t map_effect_filings=0,effect_filings=0;
  // The models pass's velocity draws this view (NativeFullFrameModelFrame::
  // velocity; edf_native_motion_vectors), for the host's MotionVectors.
  std::shared_ptr<const std::vector<edf::native::NativeMotionVelocityDraw>> velocity;
  bool map_effects_first=true;  // clMapEffectManager precedes clEffectObjectManager on the world list
};
}  // namespace edf::native

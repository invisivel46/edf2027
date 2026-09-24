// The full frame's Effects pass, NativeFullFrameEffectsPass: moved from guest_shader_bridge.cpp unchanged, with the factory
// the 821A5080 hook makes it through (host.h). The class stays local to this file.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "host.h"
#include "../../native_full_frame_effects.h"
#include "../../native_console_counters.h"
#include "../../native_scene_cpu_window.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
// The full frame's Effects pass: CollectNativeEffectManager over the
// clEffectObjectManager (vtable 820072D4) the helper's world list (owner+44)
// holds, with the guest frame context the host wrote for this view (camera
// +16, depth scale +8, key words +0/+4). The walk commits clEffectEtc02's +612
// for every object whose slot 4 the guest would have called. Mode-0 effects
// draw here, inside the walk as in the guest; filed ones go to Transparent.
class NativeFullFrameEffectsPass final : public edf::native::NativeFramePass {
 public:
  NativeFullFrameEffectsPass(uint8_t* base,std::shared_ptr<edf::native::NativeFullFrameModelsShared> shared)
    :reader_(base),shared_(std::move(shared)) {}
  const char* name() const override { return "effects"; }
  void Record(edf::native::NativeFrameContext& context) override {
    using namespace edf::native;
    shared_->effects.clear();
    shared_->effect_filings=0;
    if(!context.renderer || !context.owner || !context.guest_context || !native_scene_pass_camera) return;
    const auto manager=FindNativeWorldListObject(reader_,context.owner,NativeEffectList::manager_vtable);
    if(!manager) { ++absent_; return; }
    // After the map effects' filings when their manager is earlier on the list.
    const auto first=NativeMapEffectFilingBases(shared_->map_effects_first,shared_->model_order,shared_->map_effect_filings,0).effects;
    uint32_t order=first;
    if(!context.inputs.tick_frame) ++held_frames_;
    NativeEffectCollection collection;
    // The ribbons' eye is the pass camera's (NativeEffectEyeFromView), not
    // [8257C02C]+192: 821BE8D0, its only writer, does not run in this mode.
    // Unlocked render-only frames withhold clEffectEtc02's +612 commit, so its
    // draw-counted lifetime still counts once per simulation tick.
    const auto& eye_view=native_scene_pass_camera->view;
    try { collection=CollectNativeEffectManager(reader_,manager,context.guest_context,eye_view,order,nullptr,context.inputs.tick_frame); }
    catch(const std::exception& error) { NativeFullFrameDeclined("effects",error.what()); return; }
    // Diagnostics: how often the guest's +192 differs from the derived eye
    // (stale in this mode unless something rewrote the pool for this view).
    if(reader_.Word(kNativeEffectCameraGlobal)) {
      const auto derived=NativeEffectEyeFromView(eye_view,ReadNativeFxFloat(reader_,kNativeEffectEyeScale));
      const auto guest=ReadNativeEffectGuestEye(reader_);
      bool same=true;
      for(size_t i=0;i<3;++i) same=same && std::bit_cast<uint32_t>(derived[i])==std::bit_cast<uint32_t>(guest[i]);
      if(!same) ++stale_eyes_;
    }
    shared_->effect_filings=order-first;
    if(NativeCoverageCensusOn()) CensusEffects(collection);
    // Objects skipped alone (the walk went on): each is one undrawn object,
    // counted by class in the census (effect_object_failed); logged once per reason.
    failed_objects_+=collection.failures.size();
    for(const auto& failure:collection.failures)
      if(failure_reasons_.size()<32 && failure_reasons_.insert(failure.reason).second)
        REXLOG_INFO("Native full frame effects: object {:#x} (vtable {:#x}, slot 4 {:#x}) not drawn: {}",
          failure.object,failure.vtable,failure.slot4,failure.reason);
    for(const auto slot:collection.unsupported_slots)
      if(unsupported_.insert(slot).second) {
        const auto* known=FindNativeEffectUnbuiltSlot(slot);
        REXLOG_INFO("Native full frame effects: unsupported class {} (slot 4 {:#x}, {})",known?known->name:"unknown",slot,
          known && known->model?"no effect builder; drawn by the models pass":"not drawn");
      }
    uint64_t drawn=0;
    if(!collection.immediate.empty()) {
      auto& state=State();
      std::lock_guard submission(state.submissions);
      std::lock_guard lock(state.mutex);
      NativeFullFramePassTargets formats; NativeBackendViewport backend_viewport; NativeBackendScissor scissor; NativeViewportState viewport;
      const auto targets=NativeFullFrameSceneTargetsLocked(state,context.viewport,formats,backend_viewport,scissor,viewport);
      if(state.active_scene==context.renderer && targets.count && targets.depth && state.scene_backend) {
        const NativeSceneCpuWindow window(reader_);
        const auto report=[](const std::string& reason) { NativeFullFrameDeclined("effects",reason); };
        for(const auto& item:collection.immediate)
          drawn+=RecordNativeFullFrameEffectsLocked(state,reader_,window,item.draws,NativeSceneDrawCamera(),viewport,formats,
            [&](const std::exception& error) { report(error.what()); });
      } else if(NativeCoverageCensusOn())
        CoverageCensus().Add(NativeCoverageStatus::Uncovered,0,"pass:effects","no_targets",collection.immediate.size());
    }
    {  // The console's "stats" (native_console_counters.h).
      auto& counters=ConsoleCounters();
      counters.effects_visited.store(collection.visited,std::memory_order_relaxed);
      counters.effects_filed.store(uint32_t(collection.items.size()),std::memory_order_relaxed);
      counters.effects_drawn.store(uint32_t(drawn),std::memory_order_relaxed);
      counters.effects_frames.fetch_add(1,std::memory_order_relaxed);
    }
    if(++frames_<=4 || frames_%1000==0)
      REXLOG_INFO("Native full frame effects: frames={} manager={:#x} visited={} culled={} hidden={} immediate={} drawn={} filed={} undrawn_keys={} unsupported={} failed={} (total {}) absent={} stale_guest_eye={} held_frames={}",
        frames_,manager,collection.visited,collection.culled,collection.hidden,collection.immediate.size(),drawn,
        collection.items.size(),collection.undrawn_keys,collection.unsupported,collection.failures.size(),failed_objects_,
        absent_,stale_eyes_,held_frames_);
    shared_->effects=std::move(collection.items);
  }
 private:
  const edf::native::GuestReader reader_;
  // The coverage census of one collection: each drawn object's class
  // (covered; an empty slot 4 is parity), each class no builder covers, and
  // the objects left as the guest leaves them (a key below 256) or of an
  // unknown sort mode.
  void CensusEffects(const edf::native::NativeEffectCollection& collection) {
    using namespace edf::native;
    auto& census=CoverageCensus();
    std::vector<NativeCoverageMark> marks;
    for(const auto* list:{&collection.immediate,&collection.items})
      for(const auto& item:*list) {
        const auto vtable=NativeCoverageClassOf(reader_,item.object).first;
        marks.push_back(item.type==NativeEffectClass::Empty?NativeCoverageMark{NativeCoverageStatus::Parity,vtable,nullptr,"empty_slot4",item.slot4}:
          NativeCoverageMark{NativeCoverageStatus::Covered,vtable,nullptr,"effects"});
      }
    census.Add(marks);
    // Effect-manager members without an effect builder: the model classes
    // (kNativeEffectUnbuiltSlots::model) are drawn and counted by the models
    // pass; the debug test classes (clIKTest, clDrawTestObject) only draw
    // debug lines and are counted as parity.
    for(const auto& [vtable,slot4,count]:collection.unsupported_classes) {
      const auto* known=FindNativeEffectUnbuiltSlot(slot4);
      if(known && known->model) continue;
      if(known) census.Add(NativeCoverageStatus::Parity,vtable,{},"effect_debug_only",count,std::format("slot=0x{:08X}",slot4));
      else census.Add(NativeCoverageStatus::Uncovered,vtable,{},"effect_no_builder",count,std::format("slot=0x{:08X}",slot4));
    }
    census.Add(NativeCoverageStatus::Uncovered,0,"effect_object","effect_unknown_mode",collection.unknown_modes);
    census.Add(NativeCoverageStatus::Parity,0,"effect_object","undrawn_key",collection.undrawn_keys);
    // Objects skipped alone: uncovered by class, the failure as the detail.
    for(const auto& failure:collection.failures)
      census.Add(NativeCoverageStatus::Uncovered,failure.vtable,failure.vtable?std::string_view{}:std::string_view("effect_object"),
        "effect_object_failed",1,failure.reason);
  }
  std::shared_ptr<edf::native::NativeFullFrameModelsShared> shared_;
  std::set<uint32_t> unsupported_;
  std::set<std::string> failure_reasons_;  // Logged once each.
  uint64_t frames_=0,absent_=0,stale_eyes_=0,held_frames_=0,failed_objects_=0;
};
}

namespace edf::native {
std::unique_ptr<NativeFramePass> MakeNativeFullFrameEffectsPass(uint8_t* base,std::shared_ptr<NativeFullFrameModelsShared> shared) {
  return std::make_unique<NativeFullFrameEffectsPass>(base,std::move(shared));
}
}  // namespace edf::native

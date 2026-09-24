// The full frame's Transparent pass, NativeFullFrameTransparentPass: moved from guest_shader_bridge.cpp unchanged, with the factory
// the 821A5080 hook makes it through (host.h). The class stays local to this file.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "host.h"
#include "../../native_full_frame_effects.h"
#include "../../native_scene_cpu_window.h"
#include "../../native_transparent_items.h"
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
// The frame's one transparent sequence (sub_821A3BA0): the models' mode-1/2
// batches and the effects' filed items merged by key descending, filing order
// on ties (MergeNativeTransparentItems), after the sky and mode-0 effects.
class NativeFullFrameTransparentPass final : public edf::native::NativeFramePass {
 public:
  NativeFullFrameTransparentPass(uint8_t* base,std::shared_ptr<edf::native::NativeFullFrameModelsShared> shared)
    :reader_(base),shared_(std::move(shared)) {}
  const char* name() const override { return "transparent"; }
  void Record(edf::native::NativeFrameContext& context) override {
    using namespace edf::native;
    auto frame=std::move(shared_->transparent);
    auto effects=std::move(shared_->effects);
    auto map_effects=std::move(shared_->map_effects);
    shared_->effects.clear(); shared_->map_effects.clear();
    // clGrassMap's items (the map-effect walk's filings) into the pass's one
    // filing sequence, after the models, before or after the effects' as the
    // two managers sit on the world list.
    const auto map_base=NativeMapEffectFilingBases(shared_->map_effects_first,shared_->model_order,
      shared_->map_effect_filings,shared_->effect_filings).map_effects;
    for(auto& item:map_effects) item.order+=map_base;
    if(((!frame || frame->batches.empty()) && effects.empty() && map_effects.empty()) || !context.renderer || !native_scene_pass_camera) return;
    // The effects' vertices, encoded before the locks are taken.
    EncodeNativeEffectItems(effects);
    EncodeNativeEffectItems(map_effects);
    auto& state=State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    NativeFullFramePassTargets formats; NativeBackendViewport backend_viewport; NativeBackendScissor scissor; NativeViewportState viewport;
    const auto targets=NativeFullFrameSceneTargetsLocked(state,context.viewport,formats,backend_viewport,scissor,viewport);
    if(state.active_scene!=context.renderer || !targets.count || !targets.depth || !state.scene_backend) {
      // Every filed item of the view is dropped.
      if(NativeCoverageCensusOn())
        CoverageCensus().Add(NativeCoverageStatus::Uncovered,0,"pass:transparent","no_targets",
          (frame?frame->batches.size():0)+effects.size()+map_effects.size());
      return;
    }
    const NativeSceneCpuWindow window(reader_);
    const auto report=[](const std::string& reason) { NativeFullFrameDeclined("transparent",reason); };
    // The effects' activations draw with the jittered camera under FSR.
    const auto& camera=NativeSceneDrawCamera();
    // One activation carried across alike effect items (NativeFullFrameEffectCarry).
    NativeFullFrameEffectCarry carry;
    const bool share=NativeEffectActivationShare();
    std::vector<NativeTransparentItem> models;
    if(frame) for(size_t index=0;index<frame->batches.size();++index)
      models.push_back({frame->batches[index].key,frame->batches[index].order,[&state,&targets,&carry,frame,index](NativeBackendRecorder& recorder) {
        const auto& batch=frame->batches[index];
        // The batch binds its own targets and pipelines: an effect after it activates again.
        carry.Reset();
        recorder.SetRenderTargets({targets.colors.data(),targets.count},targets.depth);
        state.scene_renderer.Render(*state.scene_backend,batch.snapshot,batch.view,1);
        // The batch bound its own targets and pipelines.
        ++state.bind_generation;
        state.recorded={};
      }});
    const auto model_count=models.size(),effect_count=effects.size(),map_effect_count=map_effects.size();
    // Per effect draw: recorded, or declined (reported once per reason).
    uint64_t effect_draws=0,effect_declined=0;
    const auto record_item=[&](NativeBackendRecorder&,const NativeEffectItem& item) {
      // One item's draws in a row (a run of alike draws activated once, and
      // with the carry on from the item before it); a model batch may run
      // between items.
      effect_draws+=RecordNativeFullFrameEffectsLocked(state,reader_,window,item.draws,camera,viewport,formats,
        [&](const std::exception& error) { report(error.what()); ++effect_declined; },share?&carry:nullptr);
    };
    auto effect_items=NativeEffectTransparentItems(std::move(effects),record_item);
    auto map_effect_items=NativeEffectTransparentItems(std::move(map_effects),record_item);
    std::vector<std::vector<NativeTransparentItem>> sources;
    sources.push_back(std::move(models)); sources.push_back(std::move(effect_items)); sources.push_back(std::move(map_effect_items));
    const auto sequence=MergeNativeTransparentItems(std::move(sources));
    RecordNativeTransparentItems(sequence,SceneRecorderLocked(state));
    if(frame) state.scene_recorded_frames.push_back(std::move(frame));
    activations_+=carry.activations; shared_activations_+=carry.shared;
    if(++frames_<=4 || frames_%1000==0)
      REXLOG_INFO("Native full frame transparent: frames={} model_batches={} effect_items={} map_effect_items={} merged_items={} effect_draws={} effect_declined={} "
        "activations={} shared={} (total {}/{})",
        frames_,model_count,effect_count,map_effect_count,sequence.size(),effect_draws,effect_declined,
        carry.activations,carry.shared,activations_,shared_activations_);
  }
 private:
  const edf::native::GuestReader reader_;
  std::shared_ptr<edf::native::NativeFullFrameModelsShared> shared_;
  uint64_t frames_=0,activations_=0,shared_activations_=0;
};
}

namespace edf::native {
std::unique_ptr<NativeFramePass> MakeNativeFullFrameTransparentPass(uint8_t* base,std::shared_ptr<NativeFullFrameModelsShared> shared) {
  return std::make_unique<NativeFullFrameTransparentPass>(base,std::move(shared));
}
}  // namespace edf::native

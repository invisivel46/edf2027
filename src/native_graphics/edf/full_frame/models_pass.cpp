// The full frame's Models pass, NativeFullFrameModelsPass: moved from guest_shader_bridge.cpp unchanged, with the factory
// the 821A5080 hook makes it through (host.h). The class stays local to this file.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "host.h"
#include "../../native_full_frame_model_cache.h"
#include "../../native_full_frame_models.h"
#include "../../native_lock_slices.h"
#include "../../native_model_publication.h"
#include "../../native_motion_vector_pass.h"
#include "../../native_render_registry.h"
#include "../../native_reuse.h"
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
// The full frame's Models pass: NativeFullFrameModels::Build over the
// renderable registry snapshot and the view's camera; the opaque batches are
// recorded here, the transparent ones (bucket-key order) by the Transparent
// pass. Program and geometry come from the model pass caches
// (NativeModelPassProgramLocked / NativeModelGeometryLocked) through the
// models' draw states and side table: gathered only for items new to the
// persistent draw states or at a new source generation, and then asked once
// per pass record and per pass record and batch value. The generation
// (NativeFullFrameModelSourceMemo) advances only when a provider would now
// return something else: each Build asks the providers again, in a few lock
// slices, for every program and geometry a current draw state holds, so a
// constant value moved by a step, by a 821A4DE8 listener on a render-only
// iteration or by anything else is seen at the next frame, and frames where
// nothing moved re-source nothing. A program republished with only its
// pass-owned globals moved (the camera the guest pool holds, which moves
// every render; NativeFullFrameModelSameProgram) is not a change: the pass
// replaces those values whole. Guest memory is read, never called.
// Between changes each draw carries its scene object and only the camera
// moves. The snapshot is the render registry's (fed on with the full
// frame); before its first tick there is none and this records nothing.
class NativeFullFrameModelsPass final : public edf::native::NativeFramePass {
 public:
  NativeFullFrameModelsPass(uint8_t* base,std::shared_ptr<edf::native::NativeFullFrameModelsShared> shared)
    :reader_(base),shared_(std::move(shared)) {}
  const char* name() const override { return "models"; }
  void Record(edf::native::NativeFrameContext& context) override {
    using namespace edf::native;
    shared_->transparent.reset();
    shared_->velocity.reset();
    shared_->model_order=0;
    const auto registry=context.inputs.registry;
    if(!registry || registry->entries.empty() || !context.view.scene) { ++empty_; return; }
    NativeFullFrameModelCamera camera;
    camera.visibility=NativeFullFrameVisibilityView(reader_,context.view.scene);
    // context+0/+4 as 821A5080 stores them per view: the mode-1 bucket key.
    camera.key_scale=std::bit_cast<float>(reader_.Word(0x8201711c));
    camera.key_offset=std::bit_cast<float>(reader_.Word(0x82017120));
    // Helper side effect (clBrokenObject slot 4 8211FAA8): obj+712 = obj+708
    // for every object whose slot 4 the guest would call this view, before
    // and whatever the native draw does (no layout or pose yet, declined, no
    // targets); its tick 8211FAF8 releases it once +708 - +712 > 10. An
    // object released since the snapshot is skipped: its registry lifetime
    // must still be the entry's (a new object born at the address since has
    // another, whatever its vtable), and the vtable is re-read.
    for(const auto* entry:NativeFullFrameBrokenObjects(*registry,camera)) {
      if(!RenderRegistry().Alive(entry->object,entry->generation) ||
         reader_.Word(entry->object)!=NativeBrokenObject::vtable) continue;
      reader_.StoreWord(reader_.Add(entry->object,NativeBrokenObject::drawn),reader_.Word(reader_.Add(entry->object,NativeBrokenObject::counter)));
      ++broken_;
    }
    if(!native_scene_pass_camera || !context.renderer) { ++empty_; return; }
    camera.pass=*native_scene_pass_camera;
    camera.animation=NativeFullFrameAnimation(context.inputs);
    NativeFullFrameModelPass pass;
    pass.palette_limit=NativeFullFramePaletteLimit(reader_);
    pass.filtering=REXCVAR_GET(edf_native_anisotropic_filtering);
    pass.motion=context.inputs.motion;
    // The guest's slot-4 order (its gather walk over the owner+44 managers)
    // and the pool carry's inputs. Without the order every entry is taken as
    // unlisted (snapshot order), which only an equal key or a pool store can tell.
    if(context.owner) {
      try { pass.gather=ReadNativeFullFrameModelGather(NativeSceneCpuWindow(reader_),context.owner); }
      catch(const std::exception& error) { pass.gather={}; NativeFullFrameDeclined("models gather order",error.what()); }
    }
    pass.tick_frame=context.inputs.tick_frame;
    pass.view=context.view.index;
    pass.guest_frames=native_guest_slot4_frames.load(std::memory_order_relaxed);
    pass.pool=[this](const std::string& name) -> std::optional<std::array<uint8_t,16>> {
      try {
        const GuestPostMemory memory(reader_);
        const auto value=ReadPostPoolVector(memory,name.c_str());
        if(!value) return std::nullopt;
        std::array<uint8_t,16> registers;
        for(size_t i=0;i<16;++i) registers[i]=uint8_t(std::bit_cast<uint32_t>((*value)[i/4])>>(24-(i%4)*8));
        return registers;
      } catch(const std::exception&) { return std::nullopt; }
    };
    pass.census=NativeCoverageCensusOn();
    // FSR implies motion vectors (MotionVectors records them for a jittered
    // view), so its views list the moved models' velocity draws too: without
    // them a moving soldier carries only the camera's motion and FSR's
    // history trails behind it (ghosting on the first hardware run, where
    // velocity_draws stayed 0 with edf_native_fsr=native_aa).
    pass.velocity=REXCVAR_GET(edf_native_motion_vectors) || native_scene_draw_camera.has_value();
    auto& state=State();
    // The bridge locks in short holds (NativeLockSlices): the targets, each use
    // of the model pass caches (program, geometry) and each resolve with its
    // intern, then the recording. Planning, culling and pose constants run off
    // them over the registry snapshot, an immutable generation.
    NativeLockSlices slices(state.submissions,state.mutex);
    NativeViewportState viewport;
    decltype(ActiveTargetsLocked(state)) targets{};
    std::shared_ptr<NativeRenderBackend> backend;
    if(!slices([&] {
      targets=NativeFullFrameSceneTargetsLocked(state,context.viewport,pass.targets,pass.viewport,pass.scissor,viewport);
      backend=state.scene_backend;
      return state.active_scene==context.renderer && targets.count && targets.depth && backend;
    })) {
      ++empty_;
      if(pass.census) CoverageCensus().Add(NativeCoverageStatus::Uncovered,0,"pass:models","no_targets");
      return;
    }
    const NativeSceneCpuWindow window(reader_);
    // A program fetch for a planned item that fails drops that item's draws
    // (Build marks each such item models_missing_program; the rest of the
    // pass draws): a decline. The change probes below ask the providers again
    // for every remembered result, including pass records of items no longer
    // drawn (an object died and its model resource was released, its
    // materials retired from material_parameters: "unpublished native
    // material parameters"); a probe failure only advances the source
    // generation and drops no draw, so it is noted, not declined.
    const auto report=[](const std::string& reason) { NativeFullFrameDeclined("models",reason); };
    const auto probe=[this](const std::string& reason) { ++source_probe_failures_; NativeFullFrameNoted("models source probe",reason); };
    // The providers, each inside its own slice: the model pass program of a
    // pass record and the retained geometry of a batch under it.
    const auto program_from=[&](uint32_t record,const std::function<void(const std::string&)>& sink) {
      return slices([&] { return NativeModelPassProgramLocked(state,window,record,true,sink); });
    };
    const auto fetch_program=[&](uint32_t record) { return program_from(record,report); };
    const auto fetch_geometry=[&](const NativeModelBatchLayout& batch,uint32_t record) {
      return slices([&] { return NativeModelGeometryLocked(state,reader_,NativeModelGeometrySource(reader_,batch,record),batch); });
    };
    // Reuse off (native_reuse.h): the providers are asked directly, past the
    // source memo, and the generation is unversioned, so Build re-sources
    // every draw and keeps no answer; the memo is left as it was and
    // validates itself at the next reuse-on frame.
    const bool reuse=NativeReuseAllowed();
    NativeFullFrameModelSources sources{
      [&](uint32_t record) {
        if(!reuse) return fetch_program(record);
        return source_memo_.ProgramFor(record,[&] { return fetch_program(record); });
      },
      [&](const NativeModelBatchLayout& batch,uint32_t record) {
        if(!reuse) return fetch_geometry(batch,record);
        const GeometryInput input{batch,record};
        return source_memo_.GeometryFor(SourceGeometryKey(input),input,[&] { return fetch_geometry(batch,record); });
      },
      // Called only inside exclusive, which holds the locks.
      [&state](std::shared_ptr<const NativeSceneMaterial> material) { return state.scene_adapter.InternMaterial(std::move(material)); },
      [&](const std::function<void()>& work) { slices(work); },
      // The providers' change signal (NativeFullFrameModelSourceMemo): it
      // advances when a host identity moved (a shader registration or
      // release, a backend replacement) or when a provider, asked again for
      // every program and geometry a current row holds, returns another
      // object (a rebuilt program, refreshed constant values, reloaded
      // geometry), whatever wrote the inputs and whether or not a simulation
      // step ran; a program equivalent to the one held
      // (NativeFullFrameModelSameProgram: only pass-owned values moved) is
      // not a change, and the held one keeps serving. Rows are fetched only
      // then, and served from the results just asked for. The providers are
      // called directly here, the locks held once per chunk.
      [&] {
        if(!reuse) return kNativeFullFrameModelUnversioned;
        return source_memo_.Validate(
          [&] { return SourceHost{state.shader_registry_generation,state.scene_backend.get()}; },
          [&](uint32_t record) { return NativeModelPassProgramLocked(state,window,record,true,probe); },
          [&](const GeometryInput& input) {
            return NativeModelGeometryLocked(state,reader_,NativeModelGeometrySource(reader_,input.first,input.second),input.first);
          },
          [&](const auto& work) { slices(work); },
          [](const std::shared_ptr<const NativeSceneGroupMaterial>& held,const std::shared_ptr<const NativeSceneGroupMaterial>& now) {
            return NativeFullFrameModelSameProgram(held,now);
          });
      }};
    // edf_native_model_source_audit: every draw state Build keeps at the
    // current generation (not re-sourced this frame) is fetched afresh from
    // the providers and compared with what it holds. A mismatch is a provider
    // input the change signal missed (or a guest write racing this frame);
    // it is logged, and the frame still draws the kept sources.
    if(REXCVAR_GET(edf_native_model_source_audit))
      sources.audit=[&](const NativeModelBatchLayout& batch,uint32_t record,const NativeFullFrameModelSourcePair& kept) {
        const char* mismatch=nullptr;
        try {
          const auto program=program_from(record,probe);
          const auto geometry=program && program->program?fetch_geometry(batch,record):nullptr;
          if(!program || !program->program) mismatch="program now missing";
          else if(!NativeFullFrameModelSameProgram(kept.first,program))
            mismatch=kept.first && program->program==kept.first->program?"constant values moved":"program rebuilt";
          else if(geometry!=kept.second) mismatch=geometry?"geometry reloaded":"geometry now missing";
        } catch(const std::exception&) { mismatch="refetch threw"; }
        ++source_audits_;
        if(!mismatch) return;
        ++source_mismatches_;
        if(source_mismatches_<=32 || !(source_mismatches_&(source_mismatches_-1)))
          REXLOG_WARN("Native full frame models source audit mismatch: pass={:#x} batch={:#x} reason={} generation={} "
            "audits={} mismatches={}",record,batch.address,mismatch,source_memo_.generation(),source_audits_,source_mismatches_);
      };
    // frame.native.models.{visibility,programs,resolve}: Build's stages, each
    // timed from its start to the next's.
    std::optional<HookTiming> stage;
    const auto phase=[&](NativeFullFrameModelPhase next) {
      stage.reset();
      switch(next) {
        case NativeFullFrameModelPhase::Visibility: stage.emplace(HookPhase::FrameNativeModelsVisibility); break;
        case NativeFullFrameModelPhase::Programs: stage.emplace(HookPhase::FrameNativeModelsPrograms); break;
        case NativeFullFrameModelPhase::Resolve: stage.emplace(HookPhase::FrameNativeModelsResolve); break;
        case NativeFullFrameModelPhase::Done: break;
      }
    };
    auto frame=std::make_shared<NativeFullFrameModelFrame>(models_.Build(*registry,camera,pass,sources,phase));
    stage.reset();
    if(pass.census) { CoverageCensus().Add(frame->plan.census); CoverageCensus().Add(frame->census); }
    auto transparent=std::make_shared<NativeFullFrameModelFrame>();
    for(auto& batch:frame->batches) if(batch.transparent) transparent->batches.push_back(std::move(batch));
    std::erase_if(frame->batches,[](const NativeFullFrameModelBatch& batch) { return batch.transparent; });
    NativeSceneRenderStatistics statistics;
    // Recorded only onto the targets the batches were resolved for: re-validated,
    // since the locks were released while building.
    const bool current=slices([&] {
      if(state.active_scene!=context.renderer || state.scene_backend!=backend || !(ActiveTargetsLocked(state)==targets)) return false;
      if(!frame->batches.empty()) {
        HookTiming record_timing(HookPhase::FrameNativeModelsRecord);
        SceneRecorderLocked(state).SetRenderTargets({targets.colors.data(),targets.count},targets.depth);
        statistics=NativeFullFrameModels::Record(*state.scene_backend,state.scene_renderer,*frame);
        ++state.bind_generation;
        state.recorded={};
      }
      // Kept until submission: the batches' snapshots, and the registry entries the plan points into.
      state.scene_recorded_frames.push_back(frame);
      state.scene_recorded_frames.push_back(registry);
      return true;
    });
    if(!current) {
      ++stale_;
      // Nothing of this view's models was recorded (the targets moved).
      if(pass.census) CoverageCensus().Add(NativeCoverageStatus::Uncovered,0,"pass:models","stale");
    }
    else if(!transparent->batches.empty()) shared_->transparent=std::move(transparent);
    if(current && !frame->velocity.empty())
      shared_->velocity=std::make_shared<const std::vector<NativeMotionVelocityDraw>>(std::move(frame->velocity));
    shared_->model_order=uint32_t(frame->plan.transparent.size());
    const auto& built=frame->stats;
    const auto& planned=frame->plan.stats;
    if(++frames_<=4 || frames_%1000==0)
      REXLOG_INFO("Native full frame models: frames={} empty={} stale={} entries={} opaque={} transparent={} culled={}/{}/{} attachments={} no_attachment={} items={} drawn={} blended={} interpolate={} draws={} renderer_draws={} resolves={} captures={} palettes={} cache_hits={} memo_hits={} reused={} derived={} object_constants={} object_materials={}/{} carried={} calls={} unlisted={} gathered={} pool_reseeds={} sourced={} providers={}/{} rows={}/{} sources={}/{} cached={}/{} states={}/{} missing={}/{} failed={} broken_objects={} lock_slices={} lock_ms={:.3f} lock_longest_ms={:.3f} "
        "source_generation={} source_memo={} source_validated={} source_advances={}/{}/{}/{} source_equivalents={} source_reuses={} source_audits={}/{} source_probe_failures={}",
        frames_,empty_,stale_,planned.entries,planned.opaque,planned.transparent,planned.distance,planned.frustum,planned.box,
        planned.attachments,planned.no_attachment,
        built.items,built.drawn,built.blended,pass.motion.interpolate,built.draws,statistics.draws,built.resolves,built.captures,built.palettes,built.cache_hits,built.memo_hits,
        built.reused,built.derived,built.object_constants,built.object_patches,built.object_captures,built.carried,planned.calls,planned.unlisted,pass.gather.objects.size(),built.reseeds,
        built.sourced,built.programs,built.geometries,built.camera_rows,built.rows,
        built.source_hits,built.source_fetches,models_.material_cache().size(),models_.source_table().size(),
        models_.item_states(),models_.row_states(),built.missing_program,built.missing_geometry,built.failed,broken_,
        slices.slices(),NativeLockSliceMs(slices.held()),NativeLockSliceMs(slices.longest()),
        source_memo_.generation(),source_memo_.size(),source_memo_.stats().validated,source_memo_.stats().advances,
        source_memo_.stats().host_changes,source_memo_.stats().changes,source_memo_.stats().prunes,source_memo_.stats().equivalents,
        source_memo_.stats().reuses,
        source_audits_,source_mismatches_,source_probe_failures_);
  }
 private:
  const edf::native::GuestReader reader_;
  std::shared_ptr<edf::native::NativeFullFrameModelsShared> shared_;
  edf::native::NativeFullFrameModels models_;
  // The source memo's host identities (shader registrations, backend) and
  // geometry key: the pass record and the whole batch value, every field of
  // it (a superset of what NativeModelGeometryLocked reads), as Build keys
  // its own per-generation answers.
  using SourceHost=std::tuple<uint64_t,const void*>;
  using GeometryInput=std::pair<edf::native::NativeModelBatchLayout,uint32_t>;
  using GeometryKey=std::tuple<uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint64_t,uint32_t,uint64_t,
    std::vector<uint32_t>>;
  static GeometryKey SourceGeometryKey(const GeometryInput& input) {
    const auto& [batch,record]=input;
    return {record,batch.address,batch.material,batch.declaration,batch.stride,batch.index_count,batch.draw_count,
      batch.vertex.owner,batch.vertex.generation,batch.index.owner,batch.index.generation,batch.passes};
  }
  edf::native::NativeFullFrameModelSourceMemo<SourceHost,std::shared_ptr<const edf::native::NativeSceneGroupMaterial>,
    GeometryKey,GeometryInput,std::shared_ptr<const edf::native::NativeIndexedMesh::RetainedDraw>> source_memo_;
  uint64_t source_audits_=0,source_mismatches_=0,source_probe_failures_=0;
  uint64_t frames_=0,empty_=0,stale_=0,broken_=0;
};
}

namespace edf::native {
std::unique_ptr<NativeFramePass> MakeNativeFullFrameModelsPass(uint8_t* base,std::shared_ptr<NativeFullFrameModelsShared> shared) {
  return std::make_unique<NativeFullFrameModelsPass>(base,std::move(shared));
}
}  // namespace edf::native

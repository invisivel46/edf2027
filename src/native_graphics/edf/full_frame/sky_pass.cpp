// The full frame's Sky pass (the sky and the map-effect walk), NativeFullFrameSkyPass: moved from guest_shader_bridge.cpp unchanged, with the factory
// the 821A5080 hook makes it through (host.h). The class stays local to this file.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "host.h"
#include "../../native_full_frame_base_state.h"
#include "../../native_full_frame_effects.h"
#include "../../native_full_frame_sky.h"
#include "../../native_lock_slices.h"
#include "../../native_map_effects.h"
#include "../../native_reuse.h"
#include "../../native_scene_cpu_window.h"
#include "../../native_static_world_cache.h"
#include "../../native_static_world_resolve.h"
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
// The full frame's Sky pass: RecordNativeSky poses the constructed clSky from
// the rendered camera (scene+224, the copy 821CDDF8 makes) and its static
// node tree; each draw is resolved here from the model pass caches with the
// shared base state, the sky's own pass states chained within the object as
// the guest chains them, and the pass camera, then recorded on the open scene.
// A palette-skinned sky declines (none is known).
//
// The pass is the guest's map-effect walk (820B35A0 over clMapEffectManager's
// +48 list, PlanNativeMapEffects in native_map_effects.h), in list order: the
// sky where the list holds it, clElectricWire's mode-0 strips
// (BuildNativeElectricWireDraws) drawn immediately as 821C0C00 runs its slot 4
// inside the walk, and clGrassMap (mode 2, key 65535: BuildNativeGrassMapDraws)
// filed for the Transparent pass, where 821A3BA0 would reach its slot 4. A
// filed wire, a mode-1 grass map and any other class are declared
// unsupported, once each, and not drawn. Without a manager on the world list
// the sky is drawn alone, as before.
class NativeFullFrameSkyPass final : public edf::native::NativeFramePass {
 public:
  NativeFullFrameSkyPass(uint8_t* base,std::shared_ptr<edf::native::NativeFullFrameModelsShared> shared)
    :reader_(base),shared_(std::move(shared)) {}
  const char* name() const override { return "sky"; }
  void Record(edf::native::NativeFrameContext& context) override {
    using namespace edf::native;
    // This view's filings, whatever happens below (the Transparent pass
    // reads them after the Effects pass).
    shared_->map_effects.clear();
    shared_->map_effect_filings=0;
    shared_->map_effects_first=true;
    const auto sky=NativeSkyObjects().Current();
    if(!context.renderer || !native_scene_pass_camera) return;
    // The walk's reads (world list, members, wire and grass records and
    // constants) go through one page window: no guest code runs during the pass.
    const NativeSceneCpuWindow walk(reader_);
    const bool census=NativeCoverageCensusOn();
    // The helper's world callbacks this view would run (the sky pass is the
    // first pass of every accepted view).
    if(census && context.owner) CensusNativeWorldList(walk,context.owner);
    std::vector<NativeMapEffectMember> members;
    uint32_t manager=0;
    try {
      if(context.owner) manager=FindNativeWorldListObject(walk,context.owner,kNativeMapEffectManagerVtable);
      if(manager) {
        members=CollectNativeMapEffectMembers(walk,manager);
        shared_->map_effects_first=NativeMapEffectsFiledBeforeEffects(walk,context.owner);
      }
    } catch(const std::exception& error) { NativeFullFrameDeclined("map effects",error.what()); manager=0; members.clear(); }
    if(!manager) {
      if(census && sky) CoverageCensus().Add(NativeCoverageStatus::Covered,0x8200284Cu,{},"sky");
      if(sky) RecordSky(context,sky);
      return;
    }
    // The walk's routes in list order: the sky, runs of immediate draws
    // around it (recorded here), and the filed grass maps (Transparent).
    // The wires' eye is the pass camera's, as for the effects pass.
    auto plan=PlanNativeMapEffects(walk,members,sky,context.view.scene,context.guest_context,native_scene_pass_camera->view,
      [](const std::string& reason) { NativeFullFrameDeclined("map effects",reason); });
    for(const auto& segment:plan.segments) {
      if(segment.sky) RecordSky(context,sky);
      else RecordMapEffectDraws(context,segment.draws);
    }
    if(census) CensusMapEffects(members,sky,plan);
    for(const auto& member:plan.unsupported)
      if(unsupported_.insert({member.vtable,member.mode}).second) {
        const auto* name=NativeMapEffectClassName(member.vtable);
        REXLOG_INFO("Native full frame map effects: unsupported {} {:#x} mode {} (slot 4 {:#x}); not drawn",
          name?name:"class",member.vtable,member.mode,member.render);
      }
    const auto& wires=plan.wires;
    const auto& grass=plan.grass;
    uint64_t grass_draws=0;
    for(const auto& item:plan.filed) grass_draws+=item.draws.size();
    grass_filed_+=plan.filed.size(); grass_draws_+=grass_draws;
    shared_->map_effect_filings=plan.filings;
    shared_->map_effects=std::move(plan.filed);
    if(++walks_<=4 || walks_%1000==0)
      REXLOG_INFO("Native full frame map effects: walks={} manager={:#x} members={} wire_records={} disabled={} distant={} culled={} strips={} recorded={} "
        "grass_maps={} grass_skipped={} grass_cells={} grass_outside={} grass_empty={} grass_culled={} blades={} blades_culled={} blades_faded={} "
        "blades_drawn={} grass_draws={} filed={} undrawn_keys={} map_first={} (total filed={} draws={})",
        walks_,manager,members.size(),wires.records,wires.disabled,wires.distant,wires.culled,wires.drawn,map_effect_draws_,
        grass.objects,grass.skipped,grass.cells,grass.outside,grass.empty,grass.culled,grass.blades,grass.blade_culled,grass.faded,
        grass.drawn,grass.draws,plan.filings,plan.undrawn_keys,shared_->map_effects_first,grass_filed_,grass_draws_);
  }
 private:
  // Immediate map-effect draws (the wires' strips, a mode-0 grass map's
  // quads), on the open scene.
  void RecordMapEffectDraws(edf::native::NativeFrameContext& context,const std::vector<edf::native::NativeEffectDraw>& draws) {
    using namespace edf::native;
    if(draws.empty()) return;
    auto& state=State();
    // The bridge locks in short holds (NativeLockSlices): the targets, then the
    // draws in slices, one draw per item.
    NativeLockSlices slices(state.submissions,state.mutex);
    NativeFullFramePassTargets formats; NativeBackendViewport backend_viewport; NativeBackendScissor scissor; NativeViewportState viewport;
    decltype(ActiveTargetsLocked(state)) targets{};
    std::shared_ptr<NativeRenderBackend> backend;
    if(!slices([&] {
      targets=NativeFullFrameSceneTargetsLocked(state,context.viewport,formats,backend_viewport,scissor,viewport);
      backend=state.scene_backend;
      return state.active_scene==context.renderer && targets.count && targets.depth && backend;
    })) return;
    const auto report=[](const std::string& reason) { NativeFullFrameDeclined("map effects",reason); };
    const auto& camera=NativeSceneDrawCamera();
    // A run of alike draws (the wires' strips) activates once: across draws
    // of one hold through the carry, or within the one call when it is off.
    NativeFullFrameEffectCarry carry;
    if(!NativeEffectActivationShare()) {
      slices([&] {
        if(state.active_scene!=context.renderer || state.scene_backend!=backend || !(ActiveTargetsLocked(state)==targets)) return;
        const NativeSceneCpuWindow window(reader_);
        map_effect_draws_+=RecordNativeFullFrameEffectsLocked(state,reader_,window,draws,camera,viewport,formats,
          [&](const std::exception& error) { report(error.what()); });
      });
      return;
    }
    RecordNativeFullFrameItemsSliced(slices,draws.size(),reader_,carry,
      [&] { return state.active_scene==context.renderer && state.scene_backend==backend && ActiveTargetsLocked(state)==targets; },
      [&](size_t index,const NativeSceneCpuWindow<GuestReader>& window,NativeFullFrameEffectCarry& held) {
        map_effect_draws_+=RecordNativeFullFrameEffectsLocked(state,reader_,window,std::span(draws).subspan(index,1),camera,viewport,formats,
          [&](const std::exception& error) { report(error.what()); },&held);
      });
  }
  // The coverage census of one map-effect walk, member by member as
  // PlanNativeMapEffects routes it: the current sky, mode-0 wires and grass
  // maps, and filed grass maps are covered; a hidden member does nothing in
  // the guest either; a mode-2 grass map left unfiled has a key the drain
  // never reaches or no draws (parity; a build failure is also a decline);
  // anything else is unsupported, as is a sky that is not the current one.
  void CensusMapEffects(const std::vector<edf::native::NativeMapEffectMember>& members,uint32_t sky,
      const edf::native::NativeMapEffectPlan& plan) {
    using namespace edf::native;
    std::vector<NativeCoverageMark> marks;
    for(const auto& member:members) {
      NativeCoverageMark mark{NativeCoverageStatus::Covered,member.vtable,nullptr,"map_effects"};
      if(member.kind==NativeMapEffectKind::Sky) {
        if(member.object==sky) mark.reason="sky";
        else { mark.status=NativeCoverageStatus::Uncovered; mark.reason="sky_not_current"; mark.slot=member.render; }
      } else if(member.hidden) continue;
      else if(member.kind==NativeMapEffectKind::ElectricWire && member.mode==0) {}
      else if(member.kind==NativeMapEffectKind::GrassMap && member.mode==0) {}
      else if(member.kind==NativeMapEffectKind::GrassMap && member.mode==2) {
        const bool filed=std::any_of(plan.filed.begin(),plan.filed.end(),[&](const auto& item) { return item.object==member.object; });
        if(!filed) { mark.status=NativeCoverageStatus::Parity; mark.reason="grass_not_filed"; }
      } else {
        mark.status=NativeCoverageStatus::Uncovered; mark.slot=member.render;
        mark.reason=member.mode==0?"map_effect_unsupported_mode0":member.mode==1?"map_effect_unsupported_mode1":
          member.mode==2?"map_effect_unsupported_mode2":"map_effect_unsupported_mode_other";
      }
      marks.push_back(mark);
    }
    CoverageCensus().Add(marks);
  }
  void RecordSky(edf::native::NativeFrameContext& context,uint32_t sky) {
    using namespace edf::native;
    if(!sky || !context.renderer || !native_scene_pass_camera) return;
    // The camera and pose walk run between lock slices, so they read through
    // the validating reader: a page window must not outlive a release of the
    // bridge locks (another thread may free or decommit an admitted page).
    // The final slice reads the pass records through its own window.
    NativeSkyFrameInputs inputs;
    inputs.camera_world=ReadNativeVisibilityFloats<16>(reader_,reader_.Add(context.view.scene,NativeSkyScene::world));
    inputs.palette_limit=NativeFullFramePaletteLimit(reader_);
    auto& state=State();
    // The bridge locks in short holds (NativeLockSlices): the targets, each
    // buffer generation the pose walk looks up, then the draws' resolve and
    // recording (a handful). The hierarchy walk and pose run off them.
    NativeLockSlices slices(state.submissions,state.mutex);
    NativeFullFramePassTargets formats; NativeBackendViewport backend_viewport; NativeBackendScissor scissor; NativeViewportState viewport;
    decltype(ActiveTargetsLocked(state)) targets{};
    std::shared_ptr<NativeRenderBackend> backend;
    if(!slices([&] {
      targets=NativeFullFrameSceneTargetsLocked(state,context.viewport,formats,backend_viewport,scissor,viewport);
      backend=state.scene_backend;
      return state.active_scene==context.renderer && targets.count && targets.depth && backend;
    })) return;
    const auto base=NativeFullFrameBaseState(formats);
    inputs.start=base.render;
    std::vector<NativeSkyDraw> draws;
    const auto record=RecordNativeSky(reader_,sky_,inputs,sky,
      [&](uint32_t owner,NativeModelBuffers::Kind kind)->uint64_t {
        return slices([&]()->uint64_t {
          const auto* found=state.model_buffers.Find(owner,kind);
          return found?found->generation:0;
        });
      },[&](const NativeSkyDraw& draw) { draws.push_back(draw); });
    const auto decline=[&](const std::string& reason) { ++declined_; NativeFullFrameDeclined("sky",reason); };
    if(record.status!=NativeSkyStatus::Recorded) { if(record.status==NativeSkyStatus::Declined) decline(record.reason); return; }
    if(!record.palette.empty()) return decline("palette-skinned sky");
    std::vector<std::pair<std::shared_ptr<const NativeSceneInstance>,NativeSceneView>> resolved;
    uint64_t hits=0;
    const bool recorded=slices([&] {
      const auto refuse=[&](const std::string& reason) { decline(reason); return false; };
      // Resolved and recorded in one hold, onto the targets checked at the start
      // (re-validated: the locks were released during the pose walk).
      if(state.active_scene!=context.renderer || state.scene_backend!=backend || !(ActiveTargetsLocked(state)==targets))
        return refuse("scene targets changed during the pose walk");
      auto before=base.render;
      const int filtering=REXCVAR_GET(edf_native_anisotropic_filtering);
      const NativeSceneCpuWindow window(reader_);  // Valid for this hold only.
      // Each draw declines alone (one decline each, with its reason): the
      // guest activates and draws every sky pass on its own, so one pass
      // without a program, geometry or resolve leaves the others drawn. The
      // next pass still chains from this one's state, which RecordNativeSky
      // computed from the pass records, not from the resolve.
      const auto resolve_draw=[&](size_t index) -> std::string {
        const auto& draw=draws[index];
        std::string provider;  // The program provider's reason, if it has none.
        const auto material=NativeModelPassProgramLocked(state,window,draw.pass,false,
          [&](const std::string& reason) { provider=reason; });
        if(!material || !material->program) return provider.empty()?"sky pass has no program":provider;
        const auto& program=*material->program;
        if(!program.CanDeferCpuActivation()) return "sky pass needs a scissor rectangle";
        const auto source=NativeModelGeometrySource(reader_,*draw.geometry,draw.pass);
        const auto geometry=NativeModelGeometryLocked(state,reader_,source,*draw.geometry);
        if(!geometry || geometry->backend()!=state.scene_backend.get()) return "sky geometry not retained";
        auto constants=material->constants;
        for(auto& constant:constants) native_scene_pass_camera->Apply(constant);
        std::array<uint8_t,64> world{};
        for(size_t i=0;i<16;++i) for(size_t byte=0;byte<4;++byte) world[i*4+byte]=uint8_t(draw.world[i]>>(24-byte*8));
        try {
          // The resolve's every input but the constants (SkyMaterials): the
          // program (its shaders, state and sampler operations), the geometry's
          // input layout, the chained state and base samplers, the targets,
          // filtering and the backend's caches. A hit is the resolve of these
          // constants: all equal but the camera, derived from them bit for bit,
          // and g_mWorld's matrix, which the capture zeroes (the world is each
          // draw's own, below). The stored material is interned, and kept alive
          // here, so it is what InternMaterial returns for an equal resolve.
          SkyMaterials::Key key;
          key.group=uint32_t(index); key.vertex=program.inputs.vertex; key.pixel=program.inputs.pixel;
          key.program=material->program; key.setup=source; key.geometry=geometry; key.backend=state.scene_backend;
          key.pass=NativeSceneMaterialPassState{before,base.samplers}; key.view=formats; key.filtering=filtering;
          key.shaders=state.shader_registry_generation;
          SkyResolve resolve;
          // Reuse off (native_reuse.h): resolved again and stored.
          auto* entry=NativeReuseAllowed()?sky_materials_.Candidate(key):nullptr;
          NativeSceneView derived;
          if(entry) derived=entry->material.capture.camera;
          if(entry && SkyMaterials::Current(*entry,constants,entry->material.capture.material.get(),derived)) {
            resolve=entry->material;
            resolve.capture.camera.view=derived.view; resolve.capture.camera.projection=derived.projection;
            resolve.capture.camera.view_projection=derived.view_projection;
            ++sky_materials_.hits; ++hits;
          } else {
            NativeBackendPipelineDesc desc;
            desc.vertex_id=(uint64_t(program.inputs.vertex)<<1)|uint64_t(formats.reverse_depth);
            desc.pixel_id=program.inputs.pixel;
            desc.input_layout=geometry->input_layout().elements(); desc.input_layout_id=geometry->input_layout().fingerprint();
            desc.render_targets=formats.count; desc.rtv_format=formats.rtv_format;
            desc.dsv_format=formats.dsv_format; desc.sample_count=formats.samples;
            // The dome is drawn first, so it must never write depth: a written
            // dome depth failed every world pixel beyond its radius (buildings cut
            // along the dome, the road ending past the footbridge). Z write off is
            // appended after the material's own state operations (kNativeSkyNoDepthWrite).
            auto result=program.Resolve(desc,formats.reverse_depth,constants,before,base.samplers,
              filtering,false,kNativeSkyNoDepthWrite);
            result.capture.material=state.scene_adapter.InternMaterial(std::move(result.capture.material));
            resolve={result.capture,result.render.words[5]!=0};
            ++sky_materials_.misses;
            sky_materials_.Store(std::move(key),std::move(constants),{},{},{},resolve,resolve.capture.material.get(),resolve.capture.camera);
          }
          ApplyNativeScenePublishedWorld(resolve.capture,world);
          auto object=std::make_shared<NativeSceneInstance>();
          object->id=(uint64_t(7)<<60)+ ++ids_; object->changed_tick=UINT64_MAX;
          object->object.geometry=geometry; object->object.material=resolve.capture.material;
          object->object.world=resolve.capture.world; object->previous=resolve.capture.world;
          resolved.emplace_back(std::move(object),NativeStaticInstanceView(resolve.capture.camera,viewport,resolve.scissor));
        } catch(const std::exception& error) { return error.what(); }
        return {};
      };
      for(size_t index=0;index<draws.size();++index) {
        if(const auto error=resolve_draw(index);!error.empty()) decline(error);
        before=draws[index].render;  // The next pass chains from this one's state, as RecordNativeSky computed it.
      }
      sky_materials_.EndPass();
      SceneRecorderLocked(state).SetRenderTargets({targets.colors.data(),targets.count},targets.depth);
      for(auto& [object,view]:resolved) {
        state.scene_recorded_snapshots.push_back(NativeSceneSnapshot{0,NativeSceneInstances(std::vector{std::move(object)})});
        state.scene_renderer.Render(*state.scene_backend,state.scene_recorded_snapshots.back(),view,1);
      }
      ++state.bind_generation;
      state.recorded={};
      return true;
    });
    if(!recorded) return;
    if(++frames_<=4 || frames_%1000==0)
      REXLOG_INFO("Native full frame sky: frames={} sky={:#x} draws={} declined={} hierarchy_builds={} layout_decodes={} material_hits={} (total {}) misses={}",
        frames_,sky,resolved.size(),declined_,sky_.hierarchy.builds(),sky_.decodes,hits,sky_materials_.hits,sky_materials_.misses);
  }
  // The resolved half of one sky draw: the interned capture and whether its
  // state enables scissor.
  struct SkyResolve {
    edf::native::NativeSceneMaterialCapture capture;
    bool scissor=false;
  };
  // Per draw of the sky's plan (group = the draw's index), as the static world
  // caches a group's resolve: reused while the key and Current hold.
  using SkyMaterials=edf::native::NativeStaticWorldGroupCache<edf::native::NativeFullFramePassTargets,SkyResolve>;
  const edf::native::GuestReader reader_;
  edf::native::NativeSkyPassState sky_;  // Cached hierarchy and layout.
  SkyMaterials sky_materials_;
  std::set<std::pair<uint32_t,int32_t>> unsupported_;  // (vtable, mode) reported.
  std::shared_ptr<edf::native::NativeFullFrameModelsShared> shared_;  // The filed grass maps, for Transparent.
  uint64_t frames_=0,declined_=0,ids_=0,walks_=0,map_effect_draws_=0,grass_filed_=0,grass_draws_=0;
};
}

namespace edf::native {
std::unique_ptr<NativeFramePass> MakeNativeFullFrameSkyPass(uint8_t* base,std::shared_ptr<NativeFullFrameModelsShared> shared) {
  return std::make_unique<NativeFullFrameSkyPass>(base,std::move(shared));
}
}  // namespace edf::native

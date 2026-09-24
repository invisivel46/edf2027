// The full frame's StaticWorld pass, NativeFullFrameStaticWorldPass: moved from guest_shader_bridge.cpp unchanged, with the factory
// the 821A5080 hook makes it through (host.h). The class stays local to this file.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "host.h"
#include "../../native_full_frame_static_world.h"
#include "../../native_lock_slices.h"
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
// The full frame's StaticWorld pass: SelectNativeFullFrameStaticWorld +
// NativeFullFrameStaticWorld::Build over the frame's publication and the view's
// camera, recorded through the scene renderer on the open scene target. No
// guest calls; the only guest reads are the view's visibility camera (scene+96
// matrix, +288 frustum, +400 depth scale), which the publication does not
// carry yet, and the route words (+0 vtable and its +16 slot, +52 mode, +64
// hidden) of the objects culling keeps, read live through one page window as
// 820B4038 reads them at render time (the same race with the simulation's
// unhooked writers the guest walk has).
class NativeFullFrameStaticWorldPass final : public edf::native::NativeFramePass {
 public:
  explicit NativeFullFrameStaticWorldPass(uint8_t* base):reader_(base) {}
  const char* name() const override { return "static_world"; }
  void Record(edf::native::NativeFrameContext& context) override {
    using namespace edf::native;
    const auto& publication=context.inputs.publication;
    if(!publication || !native_scene_pass_camera || !context.renderer) {
      ++skipped_;
      // No static world this view.
      if(NativeCoverageCensusOn() && !publication) CoverageCensus().Add(NativeCoverageStatus::Uncovered,0,"pass:static_world","no_publication");
      return;
    }
    const auto scene=context.view.scene;
    NativeFullFrameStaticCamera camera;
    camera.visibility.matrix=ReadNativeVisibilityFloats<16>(reader_,reader_.Add(scene,96));
    camera.visibility.frustum=ReadNativeVisibilityFloats<26>(reader_,reader_.Add(scene,288));
    camera.visibility.depth_scale=std::bit_cast<float>(reader_.Word(reader_.Add(scene,400)));
    camera.pass=*native_scene_pass_camera;
    // One world animation per frame (one world in practice); with several the
    // materials that need one decline rather than take the wrong world's.
    if(const auto& animations=context.inputs.animations;animations && animations->size()==1)
      camera.animation=animations->begin()->second;
    auto& state=State();
    // The bridge locks in short holds (NativeLockSlices): the targets, a cache
    // miss's resolve, then the recording. Selection and instancing run off
    // them: every input is an immutable generation (the frame's publication,
    // the pass camera), this pass's own cache, or guest memory read through
    // the live route reader (no bridge state).
    NativeLockSlices slices(state.submissions,state.mutex);
    decltype(ActiveTargetsLocked(state)) targets{};
    std::shared_ptr<NativeRenderBackend> backend;
    if(!slices([&] {
      targets=ActiveTargetsLocked(state);
      backend=state.scene_backend;
      return state.active_scene==context.renderer && targets.count && targets.depth && backend;
    })) {
      ++skipped_;
      if(NativeCoverageCensusOn()) CoverageCensus().Add(NativeCoverageStatus::Uncovered,0,"pass:static_world","no_targets");
      return;
    }
    const NativeSceneCpuWindow window(reader_);
    const NativeFullFrameLiveRoutes routes(window);
    const bool census=NativeCoverageCensusOn();
    world_.selection_cache.census=census;
    const auto& v=context.viewport;
    const auto viewport=MakeNativeDrawViewport(v.x,v.y,v.width,v.height,v.min_depth,v.max_depth,false,{});
    NativeFullFrameStaticPass pass;
    pass.targets.count=targets.count; pass.targets.rtv_format=targets.rtv_format;
    pass.targets.dsv_format=targets.dsv_format; pass.targets.samples=targets.samples;
    pass.targets.reverse_depth=viewport.reverse_depth;
    const auto& d=viewport.viewport; const auto& s=viewport.scissor;
    pass.viewport={d.TopLeftX,d.TopLeftY,d.Width,d.Height,d.MinDepth,d.MaxDepth};
    pass.scissor={s.left,s.top,s.right,s.bottom};
    pass.filtering=NativeSceneMaterialFiltering();  // with FSR upscaling's mip bias
    // A cache miss's resolve creates backend pipelines and samplers and interns
    // the material: one slice each (the resolver's intern runs inside it).
    const auto resolver=NativeFullFrameStaticResolver(pass,
      [&state](std::shared_ptr<const NativeSceneMaterial> material) { return state.scene_adapter.InternMaterial(std::move(material)); });
    // Sub-phases: frame.native.static_world.select (tree walk, visibility,
    // LOD, live route words), .build (group materials and instance worlds,
    // with any resolve slices), .record (the recording slice, its wait included).
    std::optional<edf::native::HookTiming> timing(std::in_place,edf::native::HookPhase::FrameNativeStaticWorldSelect);
    world_.Select(*publication,camera,std::cref(routes));
    timing.emplace(edf::native::HookPhase::FrameNativeStaticWorldBuild);
    const auto& frame=world_.BuildSelected(*publication,camera,pass,[&](const auto&... arguments) {
      return slices([&] { return resolver(arguments...); });
    });
    timing.emplace(edf::native::HookPhase::FrameNativeStaticWorldRecord);
    uint64_t drawn=0;
    // Recorded only onto the targets the draws were resolved for: re-validated,
    // since the locks were released while building.
    const bool current=slices([&] {
      if(state.active_scene!=context.renderer || state.scene_backend!=backend || !(ActiveTargetsLocked(state)==targets)) return false;
      if(frame.draws.empty()) return true;
      auto& recorder=SceneRecorderLocked(state);
      recorder.SetRenderTargets({targets.colors.data(),targets.count},targets.depth);
      for(const auto& draw:frame.draws) {
        // Kept until the recording is submitted, as the renderer requires.
        state.scene_recorded_snapshots.push_back(draw.Snapshot());
        // A draw whose instances have the uniform shape records from its kept
        // worlds (the same calls, no read of the instance objects); an
        // unchanged group's worlds are last frame's array. (Without geometry or
        // material Render refuses the draw, as before.)
        // Reuse off (native_reuse.h): Render over the snapshot instead.
        if(draw.worlds && draw.geometry && draw.material && NativeReuseAllowed()) {
          drawn+=state.scene_renderer.RenderUniform(*state.scene_backend,*draw.geometry,*draw.material,*draw.worlds,draw.view).draws;
          ++uniform_;
        } else drawn+=state.scene_renderer.Render(*state.scene_backend,state.scene_recorded_snapshots.back(),draw.view,1).draws;
      }
      // The batch bound its own targets and pipelines.
      ++state.bind_generation;
      state.recorded={};
      return true;
    });
    timing.reset();
    if(!current) ++stale_;
    if(census) CensusStaticWorld(window,frame,current);
    const auto& selected=frame.selection.stats;
    const auto& built=frame.stats;
    const auto& cached=world_.selection_cache.stats;
    if(++frames_<=4 || frames_%1000==0)
      REXLOG_INFO("Native full frame static world: frames={} skipped={} stale={} route_reads={} slot_reads={} route_failures={} worlds={} selected={} culled={}/{}/{} clusters={} flat_walks={} unrouted={} not_direct={} unpublished={} undrawable={} groups={} draws={} instances={} renderer_draws={} resolves={} cache_hits={} declined={} missing={}/{}/{} world_declines={} camera_only={} reused_draws={} reused_moved={} reused_instances={} moved={}/{} reused_frame={} list_hits={} list_builds={} list_invalidations={} list_patches={}/{} keyed={} uniform_draws={} lock_slices={} lock_ms={:.3f} lock_longest_ms={:.3f}",
        frames_,skipped_,stale_,routes.reads,routes.slot_reads,routes.failures,selected.worlds,selected.selected,selected.culled_distance,selected.culled_frustum,selected.culled_bulk,selected.clusters,selected.flat_walks,
        selected.unrouted,selected.not_direct,selected.unpublished,selected.undrawable,built.groups,built.draws,built.instances,drawn,
        built.resolves,built.cache_hits,built.declined,built.missing_group,built.missing_material,built.missing_geometry,built.world_declines,
        built.camera_only,built.reused_draws,built.reused_moved,built.reused_instances,built.moved_owners,built.moved_instances,built.reused_frame,cached.list_hits,cached.list_builds,cached.invalidations,cached.patches,cached.patched,built.keyed,uniform_,
        slices.slices(),NativeLockSliceMs(slices.held()),NativeLockSliceMs(slices.longest()));
  }
 private:
  // The coverage census of one static world frame. Selected objects are
  // covered by class. A skipped member (NativeFullFrameStaticSelection::
  // skipped) whose class the registry's table holds is the models pass's to
  // draw and count, one whose slot 4 is a bare blr draws nothing in the guest
  // either (parity), and any other is uncovered, named by its class and slot
  // 4. The build's group-level drops (no group, material or geometry; a
  // scissor or declined material) and instance declines, leaf lists without
  // a published membership and worlds without a published group order are
  // uncovered; groups outside a published order are parity (821C3BB8 never
  // reaches them). world+372, the one list the membership never tracks, is
  // parity, its live members named by class (NativeCoverageMapListMarks). A
  // stale frame recorded nothing.
  template<class Reader>
  static void CensusStaticWorld(const Reader& reader,const edf::native::NativeFullFrameStaticFrame& frame,bool current) {
    using namespace edf::native;
    using Skip=NativeFullFrameStaticSelection::Skip;
    auto& census=CoverageCensus();
    std::unordered_map<uint32_t,std::pair<uint32_t,uint32_t>> classes;  // owner -> (vtable, slot 4), this frame
    const auto class_of=[&](uint32_t owner) {
      auto [found,inserted]=classes.try_emplace(owner);
      if(inserted) found->second=NativeCoverageClassOf(reader,owner);
      return found->second;
    };
    std::vector<NativeCoverageMark> marks;
    marks.reserve(frame.selection.objects.size()+frame.selection.skipped.size());
    for(const auto& object:frame.selection.objects)
      marks.push_back({NativeCoverageStatus::Covered,class_of(object.owner).first,nullptr,"static_world"});
    for(const auto& skipped:frame.selection.skipped) {
      const auto [vtable,slot4]=class_of(skipped.owner);
      const auto owner=NativeCoverageSlotOwner(vtable,slot4);
      if(owner==NativeCoverageOwner::Models) continue;
      if(owner==NativeCoverageOwner::Empty) { marks.push_back({NativeCoverageStatus::Parity,vtable,nullptr,"empty_slot4",slot4}); continue; }
      const char* reason="static_unpublished";
      switch(skipped.reason) {
        case Skip::Unpublished: reason="static_unpublished"; break;
        case Skip::Unrouted: reason="static_unrouted"; break;
        case Skip::Virtual: reason="static_other_slot4"; break;
        case Skip::Bucket: reason="static_bucket_route"; break;
        case Skip::UnknownMode: reason="static_unknown_mode"; break;
        case Skip::RouteMismatch: reason="static_route_mismatch"; break;
        case Skip::MissingLod: reason="static_missing_lod"; break;
        case Skip::Undrawable: reason="static_undrawable"; break;
      }
      marks.push_back({NativeCoverageStatus::Uncovered,vtable,nullptr,reason,slot4});
    }
    // Lists without a published membership: world+372 is never tracked and
    // holds no static world object (parity; NativeCoverageMapListMarks names
    // its members from the live list), any other (a leaf list) is uncovered,
    // named by world and list.
    for(const auto& missing:frame.selection.missing_lists) {
      char detail[64];
      std::snprintf(detail,sizeof(detail),"world=0x%08X list=0x%08X",missing.world,missing.list);
      if(missing.list!=missing.world+kNativeCoverageMapList) {
        census.Add(NativeCoverageStatus::Uncovered,0,"static_list","static_missing_list",1,detail);
        continue;
      }
      census.Add(NativeCoverageStatus::Parity,0,"static_map_list","untracked_non_octree_list",1,detail);
      try { NativeCoverageMapListMarks(reader,missing.list,class_of,marks); }
      catch(const std::exception& error) {
        census.Add(NativeCoverageStatus::Uncovered,0,"static_map_list","static_map_list_unreadable",1,error.what());
      }
    }
    census.Add(marks);
    const auto& selected=frame.selection.stats;
    const auto& built=frame.stats;
    census.Add(NativeCoverageStatus::Uncovered,0,"static_world_owner","static_missing_order",selected.missing_orders);
    census.Add(NativeCoverageStatus::Parity,0,"static_group","unordered_group",selected.unordered_parts);
    census.Add(NativeCoverageStatus::Uncovered,0,"static_group","static_missing_group",built.missing_group);
    census.Add(NativeCoverageStatus::Uncovered,0,"static_group","static_missing_material",built.missing_material);
    census.Add(NativeCoverageStatus::Uncovered,0,"static_group","static_missing_geometry",built.missing_geometry);
    census.Add(NativeCoverageStatus::Uncovered,0,"static_group","static_scissor",built.scissor);
    census.Add(NativeCoverageStatus::Uncovered,0,"static_group","static_declined",built.declined);
    census.Add(NativeCoverageStatus::Uncovered,0,"static_instance","static_world_declined",built.world_declines);
    if(!current) census.Add(NativeCoverageStatus::Uncovered,0,"pass:static_world","stale");
  }
  const edf::native::GuestReader reader_;
  edf::native::NativeFullFrameStaticWorld world_;  // Cross-frame material cache and instance reuse.
  uint64_t frames_=0,skipped_=0,stale_=0,uniform_=0;
};
}

namespace edf::native {
std::unique_ptr<NativeFramePass> MakeNativeFullFrameStaticWorldPass(uint8_t* base) {
  return std::make_unique<NativeFullFrameStaticWorldPass>(base);
}
}  // namespace edf::native

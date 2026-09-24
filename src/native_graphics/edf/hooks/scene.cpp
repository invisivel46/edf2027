// The static world and the scene tree: publication of the scene sources, walk plans, group order, the render registry, scene begin and the camera.
// Moved from guest_shader_bridge.cpp unchanged (stage 3 of its split); what this file shares with the bridge
// and the other hook files is in bridge/bridge_shared.h.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../bridge/bridge_shared.h"
#include "../../guest_shader_bridge.h"
#include "../../native_renderer_preset.h"
#include "../../../pause_menu.h"
#include "../../d3d12_backend.h"
#include "../../native_scene_sources.h"
#include "../../native_scene_adapter.h"
#include "../../native_world_publication_mirror.h"
#include "../../native_scene_cpu_window.h"
#include "../../native_static_world_pass.h"
#include "../../guest_sdk_readable_range.h"
#include "../../native_full_frame_static_world.h"
#include "../../native_full_frame_models.h"
#include "../../native_scene_walk_lock.h"
#include "../../native_font_bindings.h"
#include "../../native_render_registry.h"
#include "../../bridge/native_cvars.h"
#include "../../bridge/bridge_state.h"
#include "../../bridge/bridge_helpers.h"
#include <rex/hook.h>
#include <rex/cvar.h>
#include <rex/ppc/func.h>
#include <rex/logging.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace edf::native {
// Lock-free prefilters for hot simulation hooks (NativeAddressFilter): list
// headers and member nodes the static walk plans or the scene membership
// track, and owners the scene sources have seen born. A hook whose addresses
// are absent skips the bridge lock, which the full frame's passes hold for
// milliseconds on the render thread. Leaked: hooks may run during shutdown.
NativeAddressFilter& SceneAnchors() { static auto* value=new NativeAddressFilter; return *value; }
NativeAddressFilter& SceneSourceOwners() { static auto* value=new NativeAddressFilter; return *value; }
// The adapter's per-world group orders and pass animations as last written
// (native_world_publication_mirror.h): the 820B4250 post-hook's lock-free
// proof that its publication is a no-op. Leaked like the filters above.
NativeWorldPublicationMirror& WorldPublications() { static auto* value=new NativeWorldPublicationMirror; return *value; }
namespace {
// State().mutex for one visibility walk; see native_scene_walk_lock.h.
using BridgeWalkLock=NativeWalkLockScope<BridgeMutex>;
using BridgeGuestCall=NativeWalkGuestCall<BridgeMutex>;
// The current tree walk's camera view, shared with the list gathers it runs
// under its own scope: one read per walk, again only after a guest call.
struct BridgeWalkView {
  const BridgeWalkLock* scope; uint32_t context;
  NativeGuestCallCached<NativeSceneVisibilityView> view;
  // The walk's page admissions, shared by its node reads and every list's
  // gather. Keyed to this thread's guest call count: a guest call drops them.
  const NativeSceneCpuWindow<GuestReader>* window=nullptr;
};
inline thread_local BridgeWalkView* bridge_walk_view=nullptr;
// The immutable source generation this pass reads, or null when the pass reads
// the producer's current sources (under state.mutex) instead. Taken once per
// walk: a published generation needs no lock, and cannot change within a pass.
std::shared_ptr<const NativeSceneSources> PublishedNativeSceneSourcesForPass(const Bridge& state) {
  if(!EDF_NATIVE_FLAG(scene_sources_owned) || !native_scene_publication || !native_scene_publication->sources) return {};
  NativeSceneSourcesForPass(state);
  return native_scene_publication->sources;
}
}
void RetireGroupOrder(uint32_t owner) {
  // Destructor path of every world: stay off the bridge lock unless orders exist.
  if(!EDF_NATIVE_FLAG(scene_group_order) && !REXCVAR_GET(edf_native_scene_group_order_audit)) return;
  auto& state=State();
  std::lock_guard lock(state.mutex);
  state.scene_adapter.RetireGroupOrder(owner);
  WorldPublications().RetiredOrder(owner);
}
// Only the 820B4038 static walk and its audit consume plans. The full frame
// reads its route words live at selection (NativeFullFrameLiveRoutes), for the
// objects culling keeps, so it publishes nothing per step for them.
bool NativeStaticWalkPlansEnabled() {
  return REXCVAR_GET(edf_native_scene_static_walk) || REXCVAR_GET(edf_native_scene_static_walk_audit);
}
void RetireStaticWalkPlans(uint32_t owner) {
  if(!NativeStaticWalkPlansEnabled()) return;
  auto& state=State();
  std::lock_guard lock(state.mutex);
  state.static_walk_plans.Retire(owner);
}
// After a guest link/unlink: each anchor is a list header or a member node,
// so the list it belongs to (by the plan's node index) loses its plan. The
// caller holds the bridge lock.
void TouchStaticWalkPlansLocked(Bridge& state,std::initializer_list<uint32_t> anchors) {
  if(!NativeStaticWalkPlansEnabled()) return;
  for(const auto anchor:anchors) state.static_walk_plans.Touch(anchor);
}
void TouchStaticWalkPlans(std::initializer_list<uint32_t> anchors) {
  if(!NativeStaticWalkPlansEnabled()) return;
  auto& state=State();
  std::lock_guard lock(state.mutex);
  TouchStaticWalkPlansLocked(state,anchors);
}
// 820B4250 post-hook: the world's plan, published after its tree, under the
// bridge lock (plans read the scene sources); with plans reused that is two
// header words per list through a page window.
void PublishStaticWalkPlans(uint8_t* base,uint32_t owner) {
  if(!NativeStaticWalkPlansEnabled()) return;
  HookTiming timing(HookPhase::SimStaticWalk);
  auto& state=State();
  const auto epoch=TreePublications().Epoch();
  const GuestReader backing(base);
  const NativeSceneCpuWindow reader(backing);
  HookTiming wait(HookPhase::SimLockWait);
  std::lock_guard lock(state.mutex);
  wait.Finish();
  try {
    state.static_walk_plans.SetAnchorFilter(&SceneAnchors());
    const auto& sources=state.scene_sources;
    state.static_walk_plans.Publish(reader,owner,epoch,sources.CandidateRevision(),
      [&](uint32_t object) { return sources.FindCandidate(object); });
    const auto& stats=state.static_walk_plans.stats();
    if(stats.publications<=4 || stats.publications%1000==0)
      REXLOG_INFO("Native static walk plan: owner={:#x} publications={} collections={} builds={} reuses={} refreshes={} touches={} lists={} nodes={}",
        owner,stats.publications,stats.collections,stats.builds,stats.reuses,stats.refreshes,stats.touches,
        state.static_walk_plans.lists(),state.static_walk_plans.nodes());
  } catch(const std::exception& error) {
    state.static_walk_plans.Retire(owner);
    static std::set<std::string> reported;
    static std::mutex reported_mutex;
    std::lock_guard reported_lock(reported_mutex);
    if(reported.insert(error.what()).second) REXLOG_INFO("Native static walk plan deferred: {}",error.what());
  }
}
}
#define EDF_TREE_MUTATION(address) \
  REX_EXTERN(__imp__sub_##address); \
  REX_HOOK_RAW(sub_##address) { \
    edf::native::TreePublications().Invalidate(); \
    __imp__sub_##address(ctx,base); \
  }
EDF_TREE_MUTATION(821C7740)
EDF_TREE_MUTATION(821C5730)
EDF_TREE_MUTATION(821C5488)
EDF_TREE_MUTATION(821C49A0)
#undef EDF_TREE_MUTATION
REX_EXTERN(__imp__sub_820B5F38);
REX_HOOK_RAW(sub_820B5F38) {
  edf::native::TreePublications().Retire(ctx.r3.u32);
  edf::native::RetireGroupOrder(ctx.r3.u32);
  edf::native::RetireStaticWalkPlans(ctx.r3.u32);
  __imp__sub_820B5F38(ctx,base);
}
REX_EXTERN(__imp__sub_821C61D8);
REX_EXTERN(__imp__sub_821C3070);
REX_EXTERN(__imp__sub_821C3178);
REX_EXTERN(sub_820B4038);
REX_HOOK_RAW(sub_821C61D8) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderChildren);
  const edf::native::GuestReader reader(base);
  const auto manager=ctx.r3.u32,context=ctx.r4.u32;
  if(!EDF_NATIVE_FLAG(scene_tree) || !EDF_NATIVE_FLAG(shader_bridge) ||
     *reader.Bytes(reader.Add(manager,108),1)) {
    __imp__sub_821C61D8(ctx,base); return;
  }
  auto work=ctx;
  if(work.r1.u32<320) throw std::runtime_error("invalid native tree stack");
  const auto stack=work.r1.u32-320;
  reader.StoreWord(stack,work.r1.u32); work.r1.u64=stack;
  uint64_t checks=0;
  auto& publications=edf::native::TreePublications();
  std::shared_ptr<const edf::native::NativeSceneTreeImage> hierarchy;
  if(EDF_NATIVE_FLAG(scene_tree_published)) {
    if(EDF_NATIVE_FLAG(scene_membership_owned) && edf::native::native_scene_publication) {
      const auto& trees=edf::native::native_scene_publication->trees;
      const auto found=trees.find(manager);
      if(found!=trees.end()) hierarchy=found->second;
    } else hierarchy=publications.Acquire(manager);
  }
  const bool audit=REXCVAR_GET(edf_native_scene_visibility_audit);
  // Live node reads, the manager+100 counter and every gather's owner headers
  // go through one page window for the walk. Each admission is an SDK region
  // query (heap lock plus a scan of the region's page entries), so a word read
  // through the bare reader costs one; admissions stay valid until this thread
  // makes a guest call, which drops them all.
  const edf::native::NativeSceneCpuWindow window(reader,&edf::native::BridgeWalkLock::guest_calls);
  edf::native::NativeSceneTreeReader tree_reader(window,publications,std::move(hierarchy),audit);
  // One bridge lock scope for the walk: each leaf list's gather reuses it,
  // every guest call below releases it and so does the end of every list, so
  // the hold never spans the traversal. The view is read once and again only
  // after a guest call, which alone can change camera data; the gathers share
  // it, and releasing the lock does not invalidate it.
  edf::native::BridgeWalkLock walk_lock(edf::native::State().mutex);
  edf::native::BridgeWalkView walk_view{&walk_lock,context,{},&window};
  const auto outer_view=std::exchange(edf::native::bridge_walk_view,&walk_view);
  struct RestoreWalkView {
    edf::native::BridgeWalkView* outer;
    ~RestoreWalkView() { edf::native::bridge_walk_view=outer; }
  } restore_view{outer_view};
  edf::native::TraverseNativeSceneTree(tree_reader,manager,[&](uint32_t node) {
    const auto& view=walk_view.view.Get(edf::native::BridgeWalkLock::guest_calls,
      [&] { return edf::native::ReadNativeSceneVisibilityView(window,context); });
    const auto node_class=edf::native::ClassifyNativeSceneTreeNode(tree_reader,node,view);
    const auto& transformed=node_class.transformed;
    const auto radius=node_class.radius;
    const auto sphere=node_class.sphere,result=node_class.result;
    if(audit) {
      const auto camera=reader.Word(reader.Add(context,16));
      for(uint32_t i=0;i<4;++i) reader.StoreWord(reader.Add(stack,80+i*4),std::bit_cast<uint32_t>(transformed[i]));
      work.r3.u64=reader.Add(camera,288); work.r4.u64=reader.Add(stack,80); work.f1.f64=radius;
      work.lr=0x821C6024;
      { edf::native::BridgeGuestCall guest; __imp__sub_821C3070(work,base); }
      const auto original_sphere=work.r3.u32;
      auto original=original_sphere;
      if(original_sphere==2) {
        work.r3.u64=reader.Add(camera,288); work.r4.u64=reader.Add(camera,96);
        work.r5.u64=reader.Add(node,32); work.r6.u64=reader.Add(node,48);
        work.lr=0x821C605C;
        { edf::native::BridgeGuestCall guest; __imp__sub_821C3178(work,base); }
        original=work.r3.u32;
      }
      if(sphere!=original_sphere || result!=original) throw std::runtime_error("native tree classification differs from original");
      ++checks;
    }
    return result;
  },[&](uint32_t list) {
    work.r3.u64=manager; work.r4.u64=list; work.r5.u64=context; work.lr=0x821C56F8;
    sub_820B4038(work,base);
    walk_lock.EndList();
  });
  walk_lock.Release();
  static std::atomic<uint64_t> traversals=0,audited=0,owned_reads=0,live_reads=0;
  audited+=checks;
  owned_reads+=tree_reader.owned_reads; live_reads+=tree_reader.live_reads;
  const auto count=++traversals;
  if(count<=4 || count%1000==0) REXLOG_INFO("Native scene tree: traversals={} classification_checks={} mismatches=0 owned_reads={} live_reads={}",
    count,audited.load(),owned_reads.load(),live_reads.load());
}
REX_EXTERN(__imp__sub_820B4250);
REX_HOOK_RAW(sub_820B4250) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderWorld);
  const auto owner=ctx.r3.u32;
  std::optional<edf::native::NativeScenePassAnimation> published;
  if(REXCVAR_GET(edf_native_scene_material_audit) || EDF_NATIVE_FLAG(scene_material_owned)) {
    const edf::native::GuestReader reader(base);
    published=edf::native::NativeScenePassAnimation{
      reader.Word(reader.Add(owner,356)),reader.Word(reader.Add(owner,364))};
  }
  __imp__sub_820B4250(ctx,base);
  if(EDF_NATIVE_FLAG(scene_tree_published)) {
    edf::native::HookTiming trees(edf::native::HookPhase::SimTrees);
    try {
      // Node and region reads go through one page window: each bare read is a
      // committed-page query, and an unchanged tree is every region compared.
      const edf::native::GuestReader backing(base);
      const edf::native::NativeSceneCpuWindow reader(backing);
      if(edf::native::TreePublications().Publish(reader,owner)) {
        native_step_trees.push_back(owner);
        static std::atomic<uint64_t> publications=0;
        const auto count=++publications;
        if(count<=4 || count%1000==0) REXLOG_INFO("Native tree producer publication: owner={:#x} completed={}",owner,count);
      }
    } catch(const std::exception& error) {
      edf::native::TreePublications().Retire(owner);
      static std::set<std::string> reported;
      static std::mutex reported_mutex;
      std::lock_guard lock(reported_mutex);
      if(reported.insert(error.what()).second) REXLOG_INFO("Native tree publication deferred: {}",error.what());
    }
  }
  edf::native::PublishStaticWalkPlans(base,owner);
  // Both publications below are no-ops for an unchanged value, which is the
  // common case at every step. WorldPublications mirrors what the adapter
  // holds, so an unchanged value is proven without the bridge mutex, which the
  // full frame's passes hold in long slices on the render thread (a locked
  // no-op here cost 0.26 ms a call in the intro, 0.02 ms on the guest path).
  if(EDF_NATIVE_FLAG(scene_group_order) || REXCVAR_GET(edf_native_scene_group_order_audit)) {
    static thread_local std::vector<uint32_t> order;
    try {
      const edf::native::GuestReader reader(base);
      edf::native::CaptureNativeSceneGroupOrder(reader,reader.Add(owner,240),order);
      auto& state=edf::native::State();
      edf::native::WorldPublications().PublishOrder(owner,order,[&] { return std::unique_lock(state.mutex); },
        [&](std::span<const uint32_t> value) {
          if(!state.scene_adapter.PublishGroupOrder(owner,value)) return;
          static std::atomic<uint64_t> changes=0;
          const auto count=++changes;
          if(count<=4 || count%1000==0) REXLOG_INFO("Native group order publication: owner={:#x} groups={} changes={}",owner,value.size(),count);
        });
    } catch(const std::exception& error) {
      edf::native::RetireGroupOrder(owner);
      static std::set<std::string> reported;
      static std::mutex reported_mutex;
      std::lock_guard lock(reported_mutex);
      if(reported.insert(error.what()).second) REXLOG_INFO("Native group order publication deferred: {}",error.what());
    }
  }
  if(published) {
    auto& state=edf::native::State();
    edf::native::WorldPublications().PublishAnimation(owner,*published,[&] { return std::unique_lock(state.mutex); },
      [&](const edf::native::NativeScenePassAnimation& value) { state.scene_adapter.PublishWorldAnimation(owner,value); });
  }
}
REX_EXTERN(__imp__sub_820B4310);
REX_EXTERN(sub_821C61D8);
REX_EXTERN(sub_821C3BB8);
REX_HOOK_RAW(sub_820B4310) {
  if(REXCVAR_GET(edf_native_scene_group_order_audit)) {
    static thread_local std::vector<uint32_t> live;
    const edf::native::GuestReader reader(base);
    bool captured=true;
    try { edf::native::CaptureNativeSceneGroupOrder(reader,reader.Add(ctx.r3.u32,240),live); }
    catch(const std::exception& error) {
      // A torn list is an audit finding, never an exception through guest code.
      static uint64_t failures=0;
      if(++failures<=8) REXLOG_INFO("Native group order audit: owner={:#x} live walk failed: {}",ctx.r3.u32,error.what());
      captured=false;
    }
    if(captured) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    const bool matched=state.scene_adapter.AuditGroupOrder(ctx.r3.u32,live);
    const auto& audit=state.scene_adapter.group_order_audit();
    if(audit.checks<=4 || audit.checks%1000==0 || (!matched && audit.mismatches+audit.missing<=64))
      REXLOG_INFO("Native group order audit: owner={:#x} groups={} checks={} mismatches={} missing={}",
        ctx.r3.u32,live.size(),audit.checks,audit.mismatches,audit.missing);
    }
  }
  if(REXCVAR_GET(edf_native_scene_material_audit) || EDF_NATIVE_FLAG(scene_material_owned)) {
    edf::native::native_scene_animation_owner=ctx.r3.u32;
    edf::native::native_scene_pass_animation.reset();
    // Select completed producer inputs at world-pass entry. The outer helper
    // can begin before this world's update has published its next generation.
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    edf::native::native_scene_pass_animations=state.scene_adapter.AcquireWorldAnimations();
    if(const auto publication=edf::native::native_scene_pass_animations) {
      const auto found=publication->find(ctx.r3.u32);
      if(found!=publication->end()) edf::native::native_scene_pass_animation=found->second;
    }
  }
  if(EDF_NATIVE_FLAG(scene_tree) && EDF_NATIVE_FLAG(shader_bridge)) {
    const edf::native::GuestReader reader(base);
    const auto owner=ctx.r3.u32,context=ctx.r4.u32;
    auto work=ctx;
    if(work.r1.u32<112) throw std::runtime_error("invalid native world dispatch stack");
    const auto stack=work.r1.u32-112;
    reader.StoreWord(stack,work.r1.u32); work.r1.u64=stack;
    work.r3.u64=owner; work.r4.u64=reader.Add(owner,372); work.r5.u64=context;
    work.lr=0x820B4338; sub_820B4038(work,base);
    work.r3.u64=owner; work.r4.u64=context; work.lr=0x820B4344; sub_821C61D8(work,base);
    // The static opaque world pass: native groups in published order, guest
    // 821D96D8 per unsupported group, original 821C3BB8 when no order applies.
    // edf_native_ab_alternate latches a guest side on alternate frames for image A/B.
    // A throw must never unwind through guest code: the pass is switched off for
    // the rest of the run and this frame's groups go to the original walk.
    static std::atomic<bool> world_pass_failed=false;
    bool drawn=false;
    if(!world_pass_failed.load(std::memory_order_relaxed) &&
       edf::native::NativeStaticWorldPassEnabled() && edf::native::NativeAbNativeSide()) {
      try { edf::native::RenderNativeStaticWorldPass(work,base,owner); drawn=true; }
      catch(const std::exception& error) {
        if(!world_pass_failed.exchange(true))
          REXLOG_ERROR("Native static world pass disabled after failure: {}",error.what());
      }
    }
    if(!drawn) { work.r3.u64=reader.Add(owner,240); work.lr=0x820B434C; sub_821C3BB8(work,base); }
  } else __imp__sub_820B4310(ctx,base);
}
REX_EXTERN(__imp__sub_820B5FA8);
REX_HOOK_RAW(sub_820B5FA8) {
  edf::native::TreePublications().Retire(ctx.r3.u32);
  {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    state.scene_adapter.RetireWorldAnimation(ctx.r3.u32);
    state.scene_adapter.RetireGroupOrder(ctx.r3.u32);
    edf::native::WorldPublications().RetiredAnimation(ctx.r3.u32);
    edf::native::WorldPublications().RetiredOrder(ctx.r3.u32);
    state.static_walk_plans.Retire(ctx.r3.u32);
  }
  __imp__sub_820B5FA8(ctx,base);
}
REX_EXTERN(__imp__sub_821C3BB8);
REX_EXTERN(sub_821D96D8);
REX_HOOK_RAW(sub_821C3BB8) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderQueued);
  if(!EDF_NATIVE_FLAG(scene_tree) || !EDF_NATIVE_FLAG(shader_bridge)) {
    __imp__sub_821C3BB8(ctx,base); return;
  }
  const edf::native::GuestReader reader(base);
  auto work=ctx;
  if(work.r1.u32<112) throw std::runtime_error("invalid native group traversal stack");
  const auto stack=work.r1.u32-112;
  reader.StoreWord(stack,work.r1.u32); work.r1.u64=stack;
  edf::native::NativeMaterialPassCursor material_pass;
  struct RestoreMaterialPass {
    edf::native::NativeMaterialPassCursor* previous=edf::native::native_material_pass_cursor;
    ~RestoreMaterialPass() { edf::native::native_material_pass_cursor=previous; }
  } restore_material_pass;
  edf::native::native_material_pass_cursor=REXCVAR_GET(edf_native_scene_pass_owned)?&material_pass:nullptr;
  edf::native::DispatchNativeSceneGroups(reader,ctx.r3.u32,[&](uint32_t group) {
    work.r3.u64=group; work.lr=0x821C3C04; sub_821D96D8(work,base);
  });
}
REX_EXTERN(sub_821C0C00);
REX_EXTERN(__imp__sub_821BEE68);
REX_HOOK_RAW(sub_821BEE68) {
  auto* queues=edf::native::native_scene_queues;
  if(queues && queues->enabled) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    const edf::native::GuestReader reader(base);
    const auto parts=edf::native::NativeSceneSourcesForPass(state).LodParts(ctx.r3.u32);
    bool supported=parts.has_value();
    if(parts) for(const auto& part:*parts) {
      if(!part.group || (!queues->Contains(part.group) &&
         reader.Word(reader.Add(part.group,4))!=reader.Word(reader.Add(part.group,8)))) { supported=false; break; }
    }
    if(supported) {
      for(const auto& part:*parts) queues->Push(part.group,part.instance);
      return;
    }
    // Restore all earlier selections before this unknown producer executes, so
    // mixed groups retain the exact original head-insertion order.
    queues->Materialize(reader);
    if(++state.scene_queue_fallbacks<=8)
      REXLOG_INFO("Native scene queue fallback: descriptor={:#x} known_lod={}",ctx.r3.u32,parts.has_value());
  }
  __imp__sub_821BEE68(ctx,base);
}
namespace edf::native {
namespace {
void PublishStaticScenePartsLocked(Bridge& state,const GuestReader& reader,uint32_t owner) {
  if(!state.scene_sources.HasOwner(owner)) return; // Nested initial model load precedes completed construction.
  // LOD owners (clMapArtifact_Base) or a fixed-record owner (clRock).
  const bool fixed=state.scene_sources.Fixed(owner);
  const auto parts=ReadNativeSceneOwnerParts(reader,owner,fixed);
  state.scene_sources.Observe(owner,parts);
  state.scene_sources.PublishWorld(owner,ReadNativeStaticWorld(reader,owner));
  state.scene_sources.PublishVisibility(owner,ReadNativeSceneOwnerVisibility(reader,owner,fixed));
  // A model replacement can remove parts/LODs as well as replace their assets.
  // Previously selected snapshots retain old native objects through submission.
  state.scene_adapter.Retire(owner);
  if(++state.scene_source_publications<=4 || state.scene_source_publications%1024==0)
    REXLOG_INFO("Native scene source events: publications={} owners={} parts={}",
      state.scene_source_publications,state.scene_sources.owners(),state.scene_sources.parts());
}
}
}
REX_EXTERN(__imp__sub_820B33B0);
REX_HOOK_RAW(sub_820B33B0) {
  const auto object=ctx.r3.u32;
  const bool scene=REXCVAR_GET(edf_native_scene_adapter_audit) || EDF_NATIVE_FLAG(scene_queued);
  if(scene) {
    auto& state=edf::native::State();
    // The destructor hook normally retired this address already. Only this
    // constructor can Born it, so an absent owner stays absent and the loader
    // need not wait for submission order to retire nothing.
    bool stale;
    { std::lock_guard lock(state.mutex);
      stale=state.scene_sources.HasOwner(object) || state.scene_adapter.HasOwner(object); }
    if(stale) {
      std::lock_guard submission(state.submissions);
      std::lock_guard lock(state.mutex);
      state.scene_adapter.Retire(object);
      state.scene_sources.Retire(object);
    }
  }
  __imp__sub_820B33B0(ctx,base);
  if(scene) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    edf::native::SceneSourceOwners().Add(object);  // Before the owner exists: see NativeAddressFilter.
    state.scene_sources.Born(object);
    edf::native::PublishStaticScenePartsLocked(state,edf::native::GuestReader(base),object);
  }
}
REX_EXTERN(__imp__sub_820B2870);
REX_HOOK_RAW(sub_820B2870) {
  if(REXCVAR_GET(edf_native_scene_adapter_audit) || EDF_NATIVE_FLAG(scene_queued)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    state.scene_adapter.Retire(ctx.r3.u32);
    state.scene_sources.Retire(ctx.r3.u32);
  }
  __imp__sub_820B2870(ctx,base);
}
// clRock (NativeSceneFixedRecord): the scene source of its one +396 record,
// born after its constructor 820BAF98 returns (the record is loaded, its bound
// at +288 and world registers written; nothing changes them later: its slot 2
// is empty) and retired by its slot-1 destructor 820BB208, as 820B33B0/820B2870
// do for the map artifacts. The full frame's static world draws it
// (NativeFullFrameStaticRoute::fixed).
REX_EXTERN(__imp__sub_820BAF98);
REX_HOOK_RAW(sub_820BAF98) {
  const auto object=ctx.r3.u32;
  const bool scene=REXCVAR_GET(edf_native_scene_adapter_audit) || EDF_NATIVE_FLAG(scene_queued);
  if(scene) {
    auto& state=edf::native::State();
    bool stale;
    { std::lock_guard lock(state.mutex);
      stale=state.scene_sources.HasOwner(object) || state.scene_adapter.HasOwner(object); }
    if(stale) {
      std::lock_guard submission(state.submissions);
      std::lock_guard lock(state.mutex);
      state.scene_adapter.Retire(object);
      state.scene_sources.Retire(object);
    }
  }
  __imp__sub_820BAF98(ctx,base);
  if(scene) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    edf::native::SceneSourceOwners().Add(object);
    state.scene_sources.Born(object,true);
    try { edf::native::PublishStaticScenePartsLocked(state,edf::native::GuestReader(base),object); }
    catch(const std::exception& error) {
      // An unreadable record leaves the rock unpublished (the static world counts it).
      state.scene_sources.Retire(object);
      static std::atomic<uint32_t> failures{0};
      if(failures.fetch_add(1,std::memory_order_relaxed)<8) REXLOG_INFO("Native scene source: clRock {:#x} not published: {}",object,error.what());
    }
  }
}
REX_EXTERN(__imp__sub_820BB208);
REX_HOOK_RAW(sub_820BB208) {
  if(REXCVAR_GET(edf_native_scene_adapter_audit) || EDF_NATIVE_FLAG(scene_queued)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    state.scene_adapter.Retire(ctx.r3.u32);
    state.scene_sources.Retire(ctx.r3.u32);
  }
  __imp__sub_820BB208(ctx,base);
}
REX_EXTERN(__imp__sub_820B2AC0);
REX_HOOK_RAW(sub_820B2AC0) {
  const auto owner=ctx.r3.u32;
  __imp__sub_820B2AC0(ctx,base);
  if(REXCVAR_GET(edf_native_scene_adapter_audit) || EDF_NATIVE_FLAG(scene_queued)) {
    auto& state=edf::native::State();
    // Nested in construction the owner is not yet born and cannot become so
    // on another thread; skip the submission wait for a publication of nothing.
    if(!edf::native::SceneSourceOwners().MayContain(owner)) return;
    { std::lock_guard lock(state.mutex); if(!state.scene_sources.HasOwner(owner)) return; }
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    edf::native::PublishStaticScenePartsLocked(state,edf::native::GuestReader(base),owner);
  }
}
REX_EXTERN(__imp__sub_820B2DF8);
REX_HOOK_RAW(sub_820B2DF8) {
  const auto owner=ctx.r3.u32;
  __imp__sub_820B2DF8(ctx,base);
  // Objects the scene sources never saw born have no generation: skip both locks.
  if(EDF_NATIVE_FLAG(scene_queued) && edf::native::SceneSourceOwners().MayContain(owner)) {
    // Timed with its lock wait: the other per-step bridge-mutex user inside the
    // simulation dispatch, next to the 820B4250 post-hook.
    edf::native::HookTiming timing(edf::native::HookPhase::SimWorldUpdate);
    auto& state=edf::native::State();
    // The bridge mutex alone, as the step's publication: a world update submits
    // nothing, and the submission gate would make it wait out the swap's pacing.
    std::lock_guard lock(state.mutex);
    if(const auto generation=state.scene_sources.Generation(owner)) {
      const auto world=edf::native::ReadNativeStaticWorld(edf::native::GuestReader(base),owner);
      state.scene_sources.PublishWorld(owner,world);
      state.scene_sources.PublishVisibility(owner,edf::native::ReadNativeSceneOwnerVisibility(edf::native::GuestReader(base),owner,
        state.scene_sources.Fixed(owner)));
      state.scene_adapter.UpdateWorld(owner,generation,world);
      ++state.scene_world_publications;
    }
  }
}
REX_EXTERN(__imp__sub_821C0C00);
// Per-object tail of sub_821C0C00. Only sort modes 1/2 are native; hidden,
// mode 0 (virtual render) and unknown modes (uninitialized stack key) remain
// the original routine. Shared by the hook and the native visibility walk.
// True when a sort-mode 1/2 object was inserted natively and no guest code
// ran; false leaves the object to the original routine.
static bool TryNativeBucketInsert(PPCContext& ctx,uint8_t* base) {
  if(!REXCVAR_GET(edf_native_bucket_dispatch) || REXCVAR_GET(edf_native_bucket_dispatch_audit) ||
     !edf::native::NativeAbNativeSide()) return false;
  using namespace edf::native;
  const GuestReader reader(base);
  const auto object=ctx.r3.u32,context=ctx.r4.u32;
  static std::atomic<uint64_t> inserts=0,failures=0;
  NativeBucketDispatch kind=NativeBucketDispatch::Unknown;
  try { kind=ClassifyNativeBucket(reader,object); } catch(const std::exception&) {}
  if(kind!=NativeBucketDispatch::Bucket) return false;
  // The original clears flush-to-zero before its first lfs.
  ctx.fpscr.disableFlushMode();
  try {
    InsertNativeBucket(reader,context,object);
    const auto count=++inserts;
    if(count==1 || count%1000000==0) REXLOG_INFO("Native bucket dispatch: inserts={} failures={}",count,failures.load());
    return true;
  } catch(const std::exception& e) {
    // Every read precedes the first store and the stores are the original's
    // own values in its order, so the original can redo a partial insert.
    if(++failures<=8) REXLOG_ERROR("Native bucket dispatch fell back: object={:#x} error={}",object,e.what());
  }
  return false;
}
// `native_tried`: the caller already ran TryNativeBucketInsert and it declined.
static void DispatchNativeBucketObject(PPCContext& ctx,uint8_t* base,bool native_tried=false) {
  // The audit always runs the original; only the native insert follows the A/B side.
  const bool audit=REXCVAR_GET(edf_native_bucket_dispatch_audit),
    native=REXCVAR_GET(edf_native_bucket_dispatch) && edf::native::NativeAbNativeSide();
  if(!audit && !native) { __imp__sub_821C0C00(ctx,base); return; }
  if(!audit) { if(native_tried || !TryNativeBucketInsert(ctx,base)) __imp__sub_821C0C00(ctx,base); return; }
  using namespace edf::native;
  const GuestReader reader(base);
  const auto object=ctx.r3.u32,context=ctx.r4.u32;
  static std::atomic<uint64_t> checks=0,mismatches=0,failures=0;
  NativeBucketDispatch kind=NativeBucketDispatch::Unknown;
  try { kind=ClassifyNativeBucket(reader,object); } catch(const std::exception&) {}
  if(kind!=NativeBucketDispatch::Bucket) { __imp__sub_821C0C00(ctx,base); return; }
  // The original clears flush-to-zero before its first lfs.
  ctx.fpscr.disableFlushMode();
  // Audit: shadow the native plan, run the original, compare what it wrote.
  std::optional<NativeBucketInsert> shadow;
  try { shadow=PlanNativeBucket(reader,context,object); }
  catch(const std::exception& e) { if(++failures<=8) REXLOG_ERROR("Native bucket audit plan failed: object={:#x} error={}",object,e.what()); }
  __imp__sub_821C0C00(ctx,base);
  if(!shadow) return;
  const auto* bytes=reader.Bytes(reader.Add(object,40),2);
  const uint8_t low=bytes[0],high=bytes[1];
  const auto depth=reader.Word(reader.Add(object,44)),link=reader.Word(reader.Add(object,60));
  const auto head=reader.Word(shadow->slot);
  const auto count=++checks;
  if(low!=shadow->key.low || high!=shadow->key.high || depth!=shadow->key.depth_bits ||
     link!=shadow->previous || head!=object) {
    if(++mismatches<=16) REXLOG_ERROR("Native bucket mismatch: object={:#x} mode={} key={:#x} bytes={:#x},{:#x}/{:#x},{:#x} depth={:#x}/{:#x} link={:#x}/{:#x} slot={:#x} head={:#x}/{:#x}",
      object,reader.Word(reader.Add(object,52)),shadow->key.key,shadow->key.low,shadow->key.high,low,high,
      shadow->key.depth_bits,depth,shadow->previous,link,shadow->slot,object,head);
  }
  if(count==1 || count%1000000==0) REXLOG_INFO("Native bucket audit: checks={} mismatches={}",count,mismatches.load());
}
REX_EXTERN(__imp__sub_821C0B88);
REX_HOOK_RAW(sub_821C0B88) {
  const auto owner=ctx.r3.u32;
  __imp__sub_821C0B88(ctx,base);
  if(EDF_NATIVE_FLAG(scene_queued) && edf::native::SceneSourceOwners().MayContain(owner)) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    if(state.scene_sources.HasOwner(owner)) state.scene_sources.PublishVisibility(owner,
      edf::native::ReadNativeSceneOwnerVisibility(edf::native::GuestReader(base),owner,state.scene_sources.Fixed(owner)));
  }
}
REX_EXTERN(__imp__sub_821BEF10);
REX_HOOK_RAW(sub_821BEF10) {
  const auto destination=ctx.r4.u32;
  __imp__sub_821BEF10(ctx,base);
  if(EDF_NATIVE_FLAG(scene_queued) && destination>=288 && edf::native::SceneSourceOwners().MayContain(destination-288)) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    const auto owner=destination-288;
    if(state.scene_sources.HasOwner(owner)) state.scene_sources.PublishVisibility(owner,
      edf::native::ReadNativeSceneOwnerVisibility(edf::native::GuestReader(base),owner,state.scene_sources.Fixed(owner)));
  }
}
REX_EXTERN(__imp__sub_820B4038);
REX_EXTERN(__imp__sub_821C4EB8);
REX_HOOK_RAW(sub_821C4EB8) {
  edf::native::TreePublications().Invalidate();
  const auto node=ctx.r3.u32;
  __imp__sub_821C4EB8(ctx,base);
  edf::native::TouchStaticWalkPlans({node+120});
  if(EDF_NATIVE_FLAG(scene_queued) && EDF_NATIVE_FLAG(scene_visibility)) {
    auto& state=edf::native::State(); std::lock_guard lock(state.mutex);
    edf::native::SceneAnchors().Add(node+120);
    state.scene_membership.Born(node+120); ++state.scene_membership_events;
  }
}
REX_EXTERN(__imp__sub_821C5D28);
REX_HOOK_RAW(sub_821C5D28) {
  edf::native::TreePublications().Invalidate();
  const auto node=ctx.r3.u32;
  __imp__sub_821C5D28(ctx,base);
  edf::native::TouchStaticWalkPlans({node+120});
  if(EDF_NATIVE_FLAG(scene_queued) && EDF_NATIVE_FLAG(scene_visibility)) {
    auto& state=edf::native::State(); std::lock_guard lock(state.mutex);
    edf::native::SceneAnchors().Add(node+120);
    state.scene_membership.Born(node+120,edf::native::GuestReader(base).Word(node+132));
    ++state.scene_membership_events;
  }
}
// 821A1628 / 821A1678 link and unlink every guest list node (object lists,
// update subscriptions, leaf lists). Only anchors the static walk plans or
// the scene membership track matter here; SceneAnchors() holds every one of
// them (added before tracking starts), so any other list's link skips the
// bridge lock. Tracked links take it once, not once per consumer.
REX_EXTERN(__imp__sub_821A1628);
REX_HOOK_RAW(sub_821A1628) {
  const auto anchor=ctx.r3.u32,node=ctx.r4.u32;
  const auto& anchors=edf::native::SceneAnchors();
  const bool tracked=anchors.MayContain(anchor) || anchors.MayContain(node);
  if(tracked && EDF_NATIVE_FLAG(scene_tree_published)) {
    auto& state=edf::native::State(); std::lock_guard lock(state.mutex);
    if(state.scene_membership.HasAnchor(anchor) || state.scene_membership.HasAnchor(node))
      edf::native::TreePublications().Invalidate();
  }
  __imp__sub_821A1628(ctx,base);
  if(!tracked) return;
  auto& state=edf::native::State(); std::lock_guard lock(state.mutex);
  // After the link: the destination by its anchor, the source list by the node.
  edf::native::TouchStaticWalkPlansLocked(state,{anchor,node});
  if(EDF_NATIVE_FLAG(scene_queued) && EDF_NATIVE_FLAG(scene_visibility)) {
    if(state.scene_membership.HasAnchor(anchor)) {
      edf::native::SceneAnchors().Add(node);
      state.scene_membership.InsertAfter(anchor,node,edf::native::GuestReader(base).Word(node+8));
      ++state.scene_membership_events;
    } else if(state.scene_membership.Remove(node)) ++state.scene_membership_events;
  }
}
REX_EXTERN(__imp__sub_821A1678);
REX_HOOK_RAW(sub_821A1678) {
  const auto node=ctx.r3.u32;
  const bool tracked=edf::native::SceneAnchors().MayContain(node);
  if(tracked && EDF_NATIVE_FLAG(scene_tree_published)) {
    auto& state=edf::native::State(); std::lock_guard lock(state.mutex);
    if(state.scene_membership.HasAnchor(node)) edf::native::TreePublications().Invalidate();
  }
  __imp__sub_821A1678(ctx,base);
  if(!tracked) return;
  auto& state=edf::native::State(); std::lock_guard lock(state.mutex);
  edf::native::TouchStaticWalkPlansLocked(state,{node});
  if(EDF_NATIVE_FLAG(scene_queued) && EDF_NATIVE_FLAG(scene_visibility))
    if(state.scene_membership.Remove(node)) ++state.scene_membership_events;
}
REX_EXTERN(__imp__sub_821B0198);
REX_EXTERN(__imp__sub_821C3070);
REX_EXTERN(__imp__sub_821C33E8);
REX_HOOK_RAW(sub_820B4038) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderGather);
  auto* queues=edf::native::native_scene_queues;
  if(!queues || !queues->enabled || !EDF_NATIVE_FLAG(scene_visibility)) {
    edf::native::BridgeGuestCall guest; __imp__sub_820B4038(ctx,base); return;
  }
  using namespace edf::native;
  // Inside a tree walk (821C61D8) the walk's lock scope is current and the
  // walk releases it when this list ends; standalone gathers own a scope for
  // this list. Either way a hold covers at most this list and kObjectBudget
  // candidates, and every guest call below releases it.
  std::optional<BridgeWalkLock> own_lock;
  auto* bridge=BridgeWalkLock::current;
  if(!bridge) bridge=&own_lock.emplace(State().mutex);
  auto& state=State();
  const GuestReader reader(base);
  const auto context=ctx.r5.u32;
  // The walk's view and page window when this list runs under the walk's own
  // scope; a standalone gather has its own. Every guest word below (context,
  // stack, list header, owner headers, vtable slots, group links) goes through
  // the window: one SDK region query per page per guest-call epoch instead of
  // one per word, and none per list for the pages the walk already admitted.
  auto* walk_view=bridge_walk_view && bridge_walk_view->scope==bridge && bridge_walk_view->context==context?bridge_walk_view:nullptr;
  std::optional<NativeSceneCpuWindow<GuestReader>> own_window;
  const auto& cpu=walk_view && walk_view->window?*walk_view->window:own_window.emplace(reader,&BridgeWalkLock::guest_calls);
  // Per-candidate sub-phases (render.gather.*): read once per list, and free
  // when timings are off.
  const bool phase_timing=REXCVAR_GET(edf_native_hook_timings);
  uint32_t end=0;
  const auto generation=cpu.Word(cpu.Add(context,12));
  uint32_t cursor=0;
  // The camera view is current until this thread makes a guest call; after a
  // callback it is re-read at the next candidate that needs it, not eagerly.
  NativeGuestCallCached<NativeSceneVisibilityView> own_view;
  auto& view_cache=walk_view?walk_view->view:own_view;
  const auto current_view=[&]() -> const NativeSceneVisibilityView& {
    return view_cache.Get(BridgeWalkLock::guest_calls,[&] { return ReadNativeSceneVisibilityView(cpu,context); });
  };
  current_view();
  auto* center_destination=const_cast<uint8_t*>(cpu.WritableBytes(cpu.Add(context,32),16,4));
  auto work=ctx;
  if(work.r1.u32<160) throw std::runtime_error("invalid native visibility stack");
  work.r1.u64=work.r1.u32-160;
  cpu.StoreWord(work.r1.u32,ctx.r1.u32);
  const bool audit=REXCVAR_GET(edf_native_scene_visibility_audit);
  std::shared_ptr<const NativeSceneMembership::Snapshot> membership;
  // Resolve the pass's source generation once per list, not per candidate:
  // each candidate then costs one owner lookup and no further lock.
  std::shared_ptr<const NativeSceneSources> pass_sources;
  {
    bridge->Hold();
    bool published=false;
    if(EDF_NATIVE_FLAG(scene_membership_owned) && native_scene_publication && native_scene_publication->membership) {
      const auto& lists=*native_scene_publication->membership;
      if(state.scene_membership.Current(lists)) {
        if(const auto* found=lists.lists.Find(ctx.r4.u32)) { membership=*found; published=true; }
      }
      if(!published) {
        static uint64_t fallbacks=0;
        if(++fallbacks<=4 || fallbacks%10000==0)
          REXLOG_INFO("Native published membership fallback: count={} list={:#x}",fallbacks,ctx.r4.u32);
      }
    }
    if(published) {
      end=membership->end;
      cursor=membership->members.empty()?end:membership->members.front().node;
      static uint64_t reads=0;
      if(++reads<=4 || reads%10000==0)
        REXLOG_INFO("Native published membership: lists={} revision={}",reads,native_scene_publication->membership->revision);
    } else membership=state.scene_membership.Acquire(ctx.r4.u32);
    if(!published || audit) {
      end=cpu.Word(cpu.Add(ctx.r4.u32,12)); cursor=cpu.Word(ctx.r4.u32);
    }
    if(membership && !published && !audit && (membership->end!=end ||
       (membership->members.empty()?end:membership->members.front().node)!=cursor)) {
      ++state.scene_membership_mismatches;
      if(state.scene_membership_mismatches<=8)
        REXLOG_ERROR("Native scene membership header changed outside tracked events: list={:#x}",ctx.r4.u32);
      membership.reset();
    }
    if(membership && audit) {
      std::vector<NativeSceneMembership::Member> live;
      for(auto at=cursor;at!=end;) {
        if(live.size()>=1000000) throw std::runtime_error("native membership audit list cycle");
        const auto node=ReadGuestWords<3>(reader,at);
        live.push_back({at,node[2]}); at=node[0];
      }
      ++state.scene_membership_checks;
      if(membership->end!=end || membership->members!=live) {
        ++state.scene_membership_mismatches;
        if(state.scene_membership_mismatches<=8)
          REXLOG_ERROR("Native scene membership mismatch: list={:#x} native={} guest={}",ctx.r4.u32,membership->members.size(),live.size());
        membership.reset();
      }
    }
    if(membership) ++state.scene_membership_lists;
  }
  size_t member_index=0;
  if(membership) cursor=membership->members.empty()?end:membership->members.front().node;
  pass_sources=PublishedNativeSceneSourcesForPass(state);
  // The step's static walk plan for this list (native_scene_static_walk.h).
  // Driving: membership, the vtable+16 test and source candidates come from
  // the plan; the header words stay live. Audit: the walk stays live and the
  // plan's classification is compared with it.
  const bool static_walk=REXCVAR_GET(edf_native_scene_static_walk),static_audit=REXCVAR_GET(edf_native_scene_static_walk_audit);
  std::shared_ptr<const NativeStaticWalkList> plan;
  bool plan_drives=false,plan_sources=false;
  size_t plan_index=0;
  uint64_t plan_touches=0,plan_members=0,direct_reuses=0,source_reuses=0,bucket_native=0,plan_abandoned=0;
  NativeStaticWalkAudit plan_audit;
  if(static_walk || static_audit) {
    bridge->Hold();
    plan_touches=state.static_walk_plans.Touches();
    plan=state.static_walk_plans.Acquire(ctx.r4.u32);
    // A list header rewritten outside the membership hooks drops the plan.
    if(plan && (plan->world!=ctx.r3.u32 || plan->Head()!=cpu.Word(ctx.r4.u32) || plan->end!=cpu.Word(reader.Add(ctx.r4.u32,12)))) {
      ++state.static_walk_stale; plan.reset();
    }
    if(plan) {
      ++state.static_walk_lists;
      plan_sources=plan->sources_revision==(pass_sources?pass_sources->CandidateRevision():state.scene_sources.CandidateRevision());
      plan_drives=static_walk && !static_audit;
      if(plan_drives) { membership.reset(); cursor=plan->Head(); end=plan->end; }
      else ++plan_audit.lists;
    } else ++state.static_walk_misses;
  }
  uint64_t candidates=0,retained=0,selected=0,checks=0,mismatches=0;
  uint64_t native_members=0;
  size_t visited=0;
  while(cursor!=end) {
    if(++visited>1000000) throw std::runtime_error("native visibility list cycle");
    // classify: the next member, the header (+48 marker, route words) and the
    // source candidate. Ends before any visibility math.
    HookTiming classify_timing(HookPhase::RenderGatherClassify,phase_timing);
    std::array<uint32_t,3> node;
    const NativeStaticWalkMember* planned=nullptr;
    if(plan_drives) {
      planned=&plan->members.at(plan_index);
      node={plan->Next(plan_index),0,planned->owner}; ++plan_index; ++plan_members;
    } else if(membership) {
      const auto& member=membership->members.at(member_index++);
      node={member_index<membership->members.size()?membership->members[member_index].node:end,0,member.owner};
      ++native_members;
    } else node=ReadGuestWords<3>(cpu,cursor);
    // Audit: the plan's member at the same position against the live node.
    if(plan && !plan_drives) {
      if(AuditNativeStaticWalkMember(*plan,plan_index,cursor,node[0],node[2],plan_audit)) planned=&plan->members[plan_index];
      else plan.reset();  // Positions no longer align; later members are not comparable.
      ++plan_index;
    }
    const auto owner=node[2];
    bool callback=false;
    uint32_t hidden=0,mode=0,table=0;
    bool unseen=false;
    {
      // The guest performs ordinary CPU stores here, not atomic publication.
      // Validate one complete header and do not retain its window over a call.
      auto* header=const_cast<uint8_t*>(cpu.WritableBytes(owner,80,4));
      unseen=GuestBlockWord(header+48)!=generation;
      if(unseen) {
        StoreGuestCpuWords(std::span<uint8_t>{header+48,4},std::array<uint32_t,1>{generation});
        hidden=GuestBlockWord(header+64)>>16; mode=GuestBlockWord(header+52); table=GuestBlockWord(header);
      }
    }
    if(unseen) {
      // A view of the candidate, not a retaining copy: a published generation
      // is immutable and held by pass_sources for the whole list, the plan by
      // `plan` until after the last use below, and the producer's answer by
      // `looked_up`.
      NativeSceneSources::Candidate looked_up;
      NativeSceneSources::CandidateView source;
      if(planned && plan_drives && plan_sources) { source=NativeSceneSources::View(planned->source); ++source_reuses; }
      else if(pass_sources) source=pass_sources->FindCandidateView(owner);
      else { bridge->Hold(); looked_up=state.scene_sources.FindCandidate(owner); source=NativeSceneSources::View(looked_up); }
      if(planned && !plan_drives) {
        const bool live_direct=!hidden && !mode && cpu.Word(cpu.Add(table,16))==kNativeStaticDirectRender;
        AuditNativeStaticWalkClassification(*planned,table,mode,hidden,live_direct,plan_sources?&source:nullptr,plan_audit);
      }
      // vtable+16 from the plan while the live vtable is the one it was read
      // through (vtables are image data); otherwise the live slot.
      const auto direct=[&] {
        if(planned && plan_drives && planned->vtable==table) { ++direct_reuses; return planned->direct; }
        return cpu.Word(cpu.Add(table,16))==kNativeStaticDirectRender;
      };
      const auto* published=source.visibility;
      const auto read_live=[&](bool lods) {
        const GuestReadWindow window(cpu,owner,lods?540:356);
        return ReadNativeSceneVisibility(window,owner,lods);
      };
      auto object=published?*published:read_live(false);
      ++candidates; retained+=bool(published);
      classify_timing.Finish();
      // visibility: transform, the context+32 center store, depth, sphere/box.
      HookTiming visibility_timing(HookPhase::RenderGatherVisibility,phase_timing);
      const auto& view=current_view();
      if(audit && published) {
        const auto live=read_live(true);
        if(live!=object) {
          static std::atomic<uint32_t> reports=0;
          if(reports++<16) REXLOG_ERROR("Native visibility source mismatch: owner={:#x} box={} radius={}/{} distance={}/{} lod_count={}/{} lod_thresholds={}",
            owner,live.box!=object.box,object.radius,live.radius,object.distance,live.distance,
            object.lod_count,live.lod_count,live.lod_thresholds!=object.lod_thresholds);
          ++mismatches; object=live;
        }
      }
      auto center=NativeVisibilityCenter(view,object);
      if(audit) {
        const auto camera=reader.Word(reader.Add(context,16));
        work.r3.u64=work.r1.u32+80; work.r4.u64=reader.Add(owner,288); work.r5.u64=reader.Add(camera,96);
        work.lr=0x820B40AC;
        { BridgeGuestCall guest; __imp__sub_821B0198(work,base); }
        const auto original=ReadNativeVisibilityFloats<4>(reader,work.r1.u32+80);
        if(!NativeVisibilityBitsEqual(original,center)) {
          static std::atomic<uint32_t> reports=0;
          if(reports++<16) REXLOG_ERROR("Native visibility transform mismatch: owner={:#x} native_bits={:#x},{:#x},{:#x},{:#x} original_bits={:#x},{:#x},{:#x},{:#x}",
            owner,std::bit_cast<uint32_t>(center[0]),std::bit_cast<uint32_t>(center[1]),std::bit_cast<uint32_t>(center[2]),std::bit_cast<uint32_t>(center[3]),
            std::bit_cast<uint32_t>(original[0]),std::bit_cast<uint32_t>(original[1]),std::bit_cast<uint32_t>(original[2]),std::bit_cast<uint32_t>(original[3]));
          ++mismatches; center=original;
        } else if(std::any_of(center.begin(),center.end(),[](float value){return std::isnan(value);})) {
          static std::atomic<uint64_t> nan_matches=0;
          const auto count=++nan_matches;
          if(count<=4 || count%10000==0)
            REXLOG_INFO("Native visibility NaN parity: checks={} owner={:#x} all_register_bits_match=true",count,owner);
        }
      }
      std::array<uint32_t,4> encoded_center;
      for(size_t i=0;i<4;++i) encoded_center[i]=std::bit_cast<uint32_t>(center[i]);
      // Re-admitted here, not after the callback that dropped it.
      if(!center_destination) center_destination=const_cast<uint8_t*>(cpu.WritableBytes(cpu.Add(context,32),16,4));
      StoreGuestCpuWords(std::span<uint8_t>{center_destination,16},encoded_center);
      // The shared decision (native_scene_visibility.h); the audit below may
      // still replace its classification with the original's.
      const auto selection=SelectNativeVisibility(view,object,center);
      bool visible=selection.in_range;
      if(visible) {
        auto sphere=selection.sphere;
        auto box=selection.box;
        if(audit) {
          const auto camera=reader.Word(reader.Add(context,16));
          work.r3.u64=reader.Add(camera,288); work.r4.u64=reader.Add(context,32); work.f1.f64=object.radius;
          work.lr=0x820B40D8;
          { BridgeGuestCall guest; __imp__sub_821C3070(work,base); }
          const auto original_sphere=work.r3.u32;
          uint32_t original_box=original_sphere;
          if(original_sphere==2) {
            work.r3.u64=reader.Add(camera,288); work.r4.u64=reader.Add(camera,96); work.r5.u64=reader.Add(owner,288);
            work.lr=0x820B40F8;
            { BridgeGuestCall guest; __imp__sub_821C33E8(work,base); }
            original_box=work.r3.u32;
          }
          ++checks;
          if(sphere!=original_sphere || box!=original_box) {
            static std::atomic<uint32_t> reports=0;
            if(reports++<16) REXLOG_ERROR("Native visibility classification mismatch: owner={:#x} sphere={}/{} box={}/{}",owner,sphere,original_sphere,box,original_box);
            ++mismatches; box=original_box;
          }
        }
        visible=box!=0;
      }
      visibility_timing.Finish();
      bool native_selected=false;
      // Preserve the guest hidden flag and nonzero sorting modes. Only the
      // audited static direct-dispatch method may bypass the virtual callback.
      if(visible && published && object.lod_count && queues->enabled &&
         hidden==0 && mode==0) {
        // lod: the direct-render test, the LOD's parts and their groups.
        HookTiming lod_timing(HookPhase::RenderGatherLod,phase_timing);
        if(direct()) {
          const auto parts=source.Lod(selection.lod);
          native_selected=parts.has_value();
          if(parts) for(const auto& part:*parts) {
            if(!part.group || (!queues->Contains(part.group) &&
               cpu.Word(cpu.Add(part.group,4))!=cpu.Word(cpu.Add(part.group,8)))) { native_selected=false; break; }
          }
          lod_timing.Finish();
          if(native_selected) {
            HookTiming push_timing(HookPhase::RenderGatherPush,phase_timing);
            for(const auto& part:*parts) queues->Push(part.group,part.instance);
            ++selected;
          }
        }
      }
      if(visible && !native_selected) {
        // guest_dispatch: the bucket route, including any original routine it
        // runs (inclusive of guest time).
        HookTiming guest_timing(HookPhase::RenderGatherGuest,phase_timing);
        // Classify first. Sort modes 1/2 go through the native bucket insert
        // when it is enabled: it touches guest memory only, so it is not a
        // guest call, keeps the hold, and membership, the view and the plan
        // stay valid. Only a route into the original routine is a guest call.
        const bool try_native=hidden==0 && (int32_t(mode)==1 || int32_t(mode)==2);
        work.r3.u64=owner; work.r4.u64=context; work.lr=0x820B410C;
        callback=NativeWalkBucketDispatch<BridgeMutex>(try_native,[&] { return TryNativeBucketInsert(work,base); },[&] {
          // Unported callbacks may change membership or node values. Continue
          // from the original post-callback link rather than an older snapshot.
          membership.reset();
          // No page admission, center pointer or view survives the call: the
          // window and view are keyed to the guest call count, and the center
          // is re-admitted at the next candidate that stores one.
          cpu.Invalidate(); center_destination=nullptr;
          // Hierarchy writer hooks invalidate tree images if this callback
          // changes membership, bounds or topology. Unrelated callback activity
          // must not discard every completed producer publication.
          DispatchNativeBucketObject(work,base,try_native);
        });
        bucket_native+=try_native && !callback;
      }
    }
    // Pure native math/queue selection cannot mutate membership. A remaining
    // guest dispatcher can, so only that route must reacquire the next link.
    if(callback && plan && state.static_walk_plans.Touches()!=plan_touches) {
      // A hook touched some plan during the callback: keep this one only while
      // its membership is still the list's.
      bridge->Hold();
      plan_touches=state.static_walk_plans.Touches();
      if(!state.static_walk_plans.Current(ctx.r4.u32,*plan)) { plan.reset(); plan_drives=false; ++plan_abandoned; }
    }
    if(callback) {
      const auto next=cpu.Word(cursor);
      // A driving plan continues only onto the node it expects next.
      if(plan_drives && next!=node[0]) { plan.reset(); plan_drives=false; ++plan_abandoned; }
      cursor=next;
    } else cursor=node[0];
    bridge->Advance();
  }
  if(plan && !plan_drives && plan_index!=plan->members.size()) ++plan_audit.membership;  // The plan holds more members.
  bridge->Hold();
  if(static_walk || static_audit) {
    state.static_walk_members+=plan_members; state.static_walk_direct_reuses+=direct_reuses;
    state.static_walk_source_reuses+=source_reuses; state.static_walk_abandoned+=plan_abandoned;
    state.static_walk_bucket_native+=bucket_native;
    auto& total=state.static_walk_audit;
    total.lists+=plan_audit.lists; total.members+=plan_audit.members; total.classified+=plan_audit.classified;
    total.membership+=plan_audit.membership; total.routes+=plan_audit.routes; total.sources+=plan_audit.sources;
    total.drift+=plan_audit.drift; total.abandoned+=static_audit?plan_abandoned:0;
    if(plan_audit.mismatches()) {
      static uint64_t reports=0;
      if(++reports<=16) REXLOG_ERROR("Native static walk audit mismatch: list={:#x} membership={} routes={} sources={}",
        ctx.r4.u32,plan_audit.membership,plan_audit.routes,plan_audit.sources);
    }
    static uint64_t walks=0;
    if(++walks<=4 || walks%100000==0)
      REXLOG_INFO("Native static walk: lists={} misses={} stale={} members={} direct_reuses={} source_reuses={} abandoned={} bucket_native={} audit_lists={} audit_members={} audit_classified={} mismatches={} (membership={} routes={} sources={}) drift={}",
        state.static_walk_lists,state.static_walk_misses,state.static_walk_stale,state.static_walk_members,
        state.static_walk_direct_reuses,state.static_walk_source_reuses,state.static_walk_abandoned,state.static_walk_bucket_native,
        total.lists,total.members,total.classified,total.mismatches(),total.membership,total.routes,total.sources,total.drift);
  }
  const auto previous=state.scene_visibility_candidates;
  state.scene_visibility_candidates+=candidates; state.scene_visibility_retained+=retained;
  state.scene_visibility_selected+=selected; state.scene_visibility_checks+=checks;
  state.scene_visibility_mismatches+=mismatches;
  state.scene_membership_nodes+=native_members;
  if(mismatches) REXLOG_ERROR("Native scene visibility mismatch: candidates={} mismatches={}",candidates,mismatches);
  if(previous/1000000!=state.scene_visibility_candidates/1000000) {
    REXLOG_INFO("Native scene visibility: candidates={} retained={} selected={} checks={} mismatches={}",
      state.scene_visibility_candidates,state.scene_visibility_retained,state.scene_visibility_selected,
      state.scene_visibility_checks,state.scene_visibility_mismatches);
    REXLOG_INFO("Native scene membership: events={} lists={} nodes={} checks={} mismatches={} registered={} live_nodes={}",
      state.scene_membership_events,state.scene_membership_lists,state.scene_membership_nodes,
      state.scene_membership_checks,state.scene_membership_mismatches,state.scene_membership.lists(),state.scene_membership.nodes());
  }
}
REX_HOOK_RAW(sub_821C0C00) {
  if(REXCVAR_GET(edf_native_scene_adapter_audit)) {
    const edf::native::GuestReader reader(base);
    const auto object=ctx.r3.u32;
    const auto table=reader.Word(object);
    const auto method=reader.Word(reader.Add(table,16));
    static std::mutex audit_mutex;
    static std::map<uint32_t,std::vector<uint32_t>> samples;
    std::lock_guard lock(audit_mutex);
    if(samples.contains(method) || samples.size()<32) {
      auto& objects=samples[method];
      if(objects.size()<4 && std::find(objects.begin(),objects.end(),object)==objects.end()) {
        objects.push_back(object);
        REXLOG_INFO("Native scene source: object={:#x} vtable={:#x} render={:#x} owner={:#x} mode={} parameter={:#x}",
          object,table,method,reader.Word(reader.Add(object,32)),reader.Word(reader.Add(object,52)),ctx.r4.u32);
      }
    }
  }
  DispatchNativeBucketObject(ctx,base);
}
REX_EXTERN(__imp__sub_821BE8D0);
REX_HOOK_RAW(sub_821BE8D0) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderSceneBegin);
  if(REXCVAR_GET(edf_native_scene_material_audit) || EDF_NATIVE_FLAG(scene_material_owned)) {
    const edf::native::GuestReader reader(base);
    const auto scene=ctx.r4.u32;
    const auto& cameras=edf::native::native_scene_pass_cameras;
    const auto found=cameras?cameras->find(scene):edf::native::NativeScenePassCameras::const_iterator{};
    if(cameras && found!=cameras->end()) {
      edf::native::native_scene_pass_camera=found->second;
      if(REXCVAR_GET(edf_native_scene_transform_audit) &&
         found->second!=edf::native::ReadNativeScenePassCamera(reader,scene)) {
        REXLOG_ERROR("Native published camera mismatch: scene={:#x}",scene);
        throw std::runtime_error("native camera changed after producer publication");
      }
      static uint64_t reads=0;
      if(++reads<=4 || reads%1000==0)
        REXLOG_INFO("Native published camera: reads={} audited={}",reads,REXCVAR_GET(edf_native_scene_transform_audit));
    } else {
      edf::native::native_scene_pass_camera=edf::native::ReadNativeScenePassCamera(reader,scene);
      if(cameras) {
        static uint64_t misses=0;
        if(++misses<=4 || misses%1000==0)
          REXLOG_INFO("Native camera publication fallback: scene={:#x} misses={}",scene,misses);
      }
    }
  }
  // This backend callback consumes scene+32/+96 before render-list traversal.
  // Observe its inputs, not guest state after another simulation tick. A new
  // submission with unchanged matrices is not evidence of interpolated motion.
  const auto limit=REXCVAR_GET(edf_native_motion_trace);
  if(limit>0) {
    static std::atomic<uint64_t> calls{0};
    const auto sequence=calls.fetch_add(1,std::memory_order_relaxed)+1;
    if(sequence<=uint64_t(limit)) {
      const edf::native::GuestReader reader(base);
      const auto scene=ctx.r4.u32;
      const auto fingerprint=[&](uint32_t offset) {
        const auto* bytes=reader.Bytes(reader.Add(scene,offset),64);
        uint64_t hash=14695981039346656037ull;
        for(size_t i=0;i<64;++i) { hash^=bytes[i]; hash*=1099511628211ull; }
        return hash;
      };
      const auto us=std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
      REXLOG_INFO("Native motion trace: seq={} us={} scene={:#x} viewport={} matrix32={:#x} matrix96={:#x}",
        sequence,us,scene,ctx.r5.u32,fingerprint(32),fingerprint(96));
    }
  }
  __imp__sub_821BE8D0(ctx,base);
}
REX_EXTERN(__imp__sub_821CDDF8);
REX_HOOK_RAW(sub_821CDDF8) {
  static thread_local std::unordered_map<uint32_t,edf::native::NativeCameraHistory> histories;
  const auto scene=ctx.r3.u32;
  const edf::native::GuestReader reader(base);
  // Vert+ (native_display_layout.h): 821CDDF8 derives the projection
  // (821C82C0) and the frustum (821C26C0) from the vertical angle at +480 and
  // the viewport +496/+500, so a render narrower than 16:9 would crop the
  // sides. For the main view (the viewport the renderer size gave it,
  // clSgsCoreRender::slot7) the angle it consumes is widened for the duration
  // of the call, so projection, frustum and every copy agree; +480 is restored
  // after, and the camera state the game reads keeps its own angle.
  const float fov_scale=[&] {
    static const auto mode=edf::native::ParseNativeAspectMode(REXCVAR_GET(edf_aspect));
    const auto& dimensions=edf::native::NativeRenderDimensions();
    if(!dimensions[0]) return 1.0f;
    const auto size=edf::native::ReadGuestWords<2>(reader,reader.Add(scene,496));
    const float width=std::bit_cast<float>(size[0]),height=std::bit_cast<float>(size[1]);
    if(width!=float(dimensions[0]) || height!=float(dimensions[1])) return 1.0f;
    return edf::native::NativeVerticalFovScale(width,height,mode);
  }();
  const auto derive=[&](float fov) {
    if(fov_scale==1.0f) { __imp__sub_821CDDF8(ctx,base); return; }
    auto* destination=const_cast<uint8_t*>(reader.WritableBytes(reader.Add(scene,480),4,4));
    struct Restore {
      uint8_t* destination;
      std::array<uint8_t,4> bytes;
      ~Restore() { std::memcpy(destination,bytes.data(),bytes.size()); }
    } restore{destination,{}};
    std::memcpy(restore.bytes.data(),destination,restore.bytes.size());
    const std::array<uint32_t,1> word{std::bit_cast<uint32_t>(edf::native::NativeAdjustedVerticalFov(fov,fov_scale))};
    edf::native::StoreGuestCpuWords(std::span<uint8_t>{destination,4},word);
    __imp__sub_821CDDF8(ctx,base);
  };
  const auto current_fov=[&] { return std::bit_cast<float>(reader.Word(reader.Add(scene,480))); };
  // Publication follows the render-helper join. Constructors and other callers
  // must initialize their real matrices and invalidate any reused address.
  if(ctx.lr!=0x821A4EB0 || !native_loop_budget.unlocked || native_loop_budget.divisor!=1 ||
     !REXCVAR_GET(edf_native_camera_interpolation)) {
    histories.erase(scene);
    derive(current_fov()); return;
  }
  const auto source=reader.Add(scene,416);
  const auto words=edf::native::ReadGuestWords<17>(reader,source);
  edf::native::NativeCameraPose pose;
  for(size_t i=0;i<16;++i) pose.world[i]=std::bit_cast<float>(words[i]);
  pose.fov=std::bit_cast<float>(words[16]);
  pose.viewport=edf::native::ReadGuestWords<4>(reader,reader.Add(scene,488));
  if(histories.size()>=16 && !histories.contains(scene)) histories.clear();
  auto& history=histories[scene];
  if(native_loop_budget.steps>1) history.Reset();
  const auto rendered=history.Sample(pose,native_loop_budget.tick,native_loop_budget.fraction);
  if(rendered==pose) { derive(pose.fov); return; }
  // Rebuild every derived camera matrix and its frustum from the same pose.
  // Restore authoritative simulation inputs even if the guest call throws.
  auto* destination=const_cast<uint8_t*>(reader.WritableBytes(source,68,4));
  struct Restore {
    uint8_t* destination;
    std::array<uint8_t,68> bytes;
    ~Restore() { std::memcpy(destination,bytes.data(),bytes.size()); }
  } restore{destination,{}};
  std::memcpy(restore.bytes.data(),destination,restore.bytes.size());
  std::array<uint32_t,17> interpolated;
  for(size_t i=0;i<16;++i) interpolated[i]=std::bit_cast<uint32_t>(rendered.world[i]);
  interpolated[16]=std::bit_cast<uint32_t>(edf::native::NativeAdjustedVerticalFov(rendered.fov,fov_scale));
  edf::native::StoreGuestCpuWords(std::span<uint8_t>{destination,68},interpolated);
  __imp__sub_821CDDF8(ctx,base);
}
// Render registry feeds (native_render_registry.h). Off: one cvar read each.
REX_EXTERN(__imp__sub_821C2090);
REX_HOOK_RAW(sub_821C2090) {
  const uint32_t object=ctx.r3.u32;
  __imp__sub_821C2090(ctx,base);
  if(REXCVAR_GET(edf_native_render_registry) || EDF_NATIVE_FLAG(full_frame)) edf::native::RenderRegistry().Born(object);
}
REX_EXTERN(__imp__sub_821C1FE8);
REX_HOOK_RAW(sub_821C1FE8) {
  if(REXCVAR_GET(edf_native_render_registry) || EDF_NATIVE_FLAG(full_frame)) edf::native::RenderRegistry().Died(ctx.r3.u32);
  __imp__sub_821C1FE8(ctx,base);
}
REX_EXTERN(__imp__sub_821C0D70);
REX_HOOK_RAW(sub_821C0D70) {
  // r4==1 links obj+120 into scene+100; any other value unlinks it.
  const uint32_t object=ctx.r3.u32,flag=ctx.r4.u32;
  __imp__sub_821C0D70(ctx,base);
  if(REXCVAR_GET(edf_native_render_registry) || EDF_NATIVE_FLAG(full_frame)) edf::native::RenderRegistry().Subscribed(object,flag==1);
}

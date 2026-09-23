#pragma once
// Renderable registry: the producer of NativeRenderRegistrySnapshot
// (native_render_entry.h) for the full-frame renderer, where the render helper
// 821A5080 does not run and no slot-4 draw names an object.
//
// Feeds (guest_shader_bridge.cpp hooks, behind edf_native_render_registry):
// - 821C2090 (base render-object constructor) returns: Born(obj). The derived
//   vtable is not set yet, so the class is resolved at the object's first tick.
// - 821C1FE8 (base destructor) entry: Died(obj), before +144/+120/+108 unlink.
// - 821C0D70(obj,flag): Subscribed(obj,flag==1), the scene+100 membership that
//   drives slot 2 (pose builds) in 821A4DE8.
// - 821A4DE8 exit: Tick. Events are applied in hook order, then the tick
//   re-reads only new objects, scene+100 members, objects with instanced
//   worlds (their inputs advance in slot 3, not through scene+100), objects
//   waiting for their first layout or derived vtable (backed off) and a small
//   round-robin refresh of the rest, and publishes a snapshot whose unchanged
//   entries are the previous pointers.
// Frame-cadence classes with a root (NativeRenderClass::frame_root, e.g.
// clBrokenObject) build their pose inside slot 4, which full-frame mode never
// runs: the registry re-reads them every tick and computes that pose natively
// (native_model_hierarchy.h), so their layout is captured with the tree's
// bone count and never waits for the guest pose vector to be sized.
//
// Per-object constants (NativeRenderClass::constants, parts): the float4s a
// slot 4 stores into shared effect-pool parameters before its draws
// (821A1730: g_Highlight and g_Time for the UFO, alien-tank and mothership
// classes, g_Scroll for the C_Tank treads) are read at the tick with the pool
// name of each handle and published with the draw they precede
// (NativeRenderEntry::constants, NativeRenderAttachment::constants); the
// models pass binds them over the material's globals of those names. Their
// objects are re-read every tick like the instanced ones. Across objects the
// pool is sticky in the guest (a draw that stores nothing sees the previous
// object's value, and a frame starts with what the last one left): the models
// pass carries it in the guest's slot-4 call order
// (NativeFullFrameModelPoolCarry). The one class that draws a material
// reading them without storing is C_PowerLoader (powerloader.Dxm is
// c_Mech01: g_Highlight, g_Time); g_Scroll is read only by the treads'
// c_tDPNC_Scroll (Caterpillar-l/-r), c_Shield01 only by the 4-leg tank's
// shields, all of which store it themselves.
//
// The values are simulation state, never computed in slot 4 (but g_Scroll's
// negation, which NativeRenderConstantSource::scroll replicates): slot 3
// (the tick) writes a source block and slot 2 (the scene+100 walk, 821A4DE8)
// copies its two float4s to the block slot 4 reads, e.g. clUfoSmall01
// 820E7C48 +2304 -> +2336, clAlienTank01 820EC7A0 +5040 -> +5072 and its
// turrets 820F0188 +1136 -> +1168, the 4-leg tank parts 820FC400 +1232 ->
// +1264; the C_Tank treads' +60 is advanced by slot 3 itself (821E61F0 ->
// 821E7B70: +60 += speed*0.9, wrapped to [0,2]). So a tick's value is final
// at the tick's 821A4DE8 exit; like the guest's one render per tick, the
// models pass holds it for every render of the tick (it is never blended:
// g_Time and g_Scroll wrap).
//
// Pose motion (NativeRenderPoseMotion, native_render_motion.h): every pose a
// re-read publishes (the model's, each attachment's and each instanced set's
// worlds) carries the previous tick's pose while the object was read on
// consecutive ticks, for the models pass to interpolate in unlocked mode; an
// unchanged pose keeps its motion, so unchanged entries stay shared. The
// bound (centre, axes, radius) carries its previous tick's the same way
// (NativeRenderBoundMotion), for conservative culling of a blended pose.
//
// Render-only iterations: 821A4DE8 runs its camera loop and the scene+100
// slot-2 walk on every iteration, stepped or not. Every slot 2 of the class
// table is idempotent (static reading of generated/default): it copies node
// world matrices (node+240) that only slot 3 moves into the pose vectors
// (821C9478 via 8210AE08 for the LOD pose +1088; 820DB220 the face +1636;
// 820E1A48 each people / vehicle weapon's w+144; 820F0188 and 821E7B60 the
// parts; the rigid ammo, shell and broken-piece bodies their own vector),
// plus "previous = current" 32-byte copies; none reads the camera, a clock
// or accumulates, and none calls slot 8. The one camera-dependent pose is
// clSky's, built in its slot 4 820BB270 (other_pass: the sky pass poses it).
// So no class is marked; instead a light tick probes the update members round
// robin, and a member a render-only iteration did change (a class missed
// here, or the unread scene+2228 list's writers) is found within
// members/probe iterations, published live and made render-dependent.
//
// Cost per tick is O(re-read objects + changes), never O(entries): a re-read
// builds into a reused scratch entry and compares it with the published one
// (the pose against the guest bytes, without decoding), so only an entry that
// changed allocates; `objects` and `entries` are shared-chunk containers, so a
// snapshot copies neither and a change clones one chunk of each.
//
// Threading: hooks may run on any thread (loaders construct objects) and only
// append to a locked event list. Tick, AuditScene, Clear and stats run on the
// engine thread. AcquireSnapshot is safe from any thread; snapshots and their
// entries are immutable.
#include "native_model_hierarchy.h"
#include "native_model_publication.h"
#include "native_render_entry.h"
#include "native_render_instances.h"
#include "native_render_motion.h"
#include "native_reuse.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace edf::native {
// Static class table (native_render_registry.cpp), sorted by vtable.
std::span<const NativeRenderClass> NativeRenderClasses();
const NativeRenderClass* FindNativeRenderClass(uint32_t vtable);

// Base render object (821C2090). The constructor stores 82019A1C, then
// 82019A2C; a derived constructor replaces it after the base returns.
inline constexpr uint32_t kNativeRenderBaseVtable=0x82019A2Cu,kNativeRenderBaseVtableEarly=0x82019A1Cu;
inline constexpr uint32_t kNativeRenderObjectScene=32,kNativeRenderObjectMode=52,kNativeRenderObjectBias=56,
  kNativeRenderObjectHidden=64,kNativeRenderObjectSubscribed=72,kNativeRenderObjectCull=76,
  kNativeRenderObjectCentre=288,kNativeRenderObjectRadius=352;
// Scene lists: scene+84 holds every render object (node obj+108), scene+100 the
// update subscribers (node obj+120).
inline constexpr uint32_t kNativeRenderSceneObjects=84,kNativeRenderSceneUpdates=100;
// 8210AE48: 48-byte LOD records at *(obj+1128), count obj+1136, threshold at
// rec+0 and instance at rec+4; obj+1168 is the default. It draws the last
// record whose threshold is below -(camera+40 * camera+8), with pose obj+1088.
inline constexpr uint32_t kNativeRenderLodRecords=1128,kNativeRenderLodCount=1136,kNativeRenderLodStride=48,
  kNativeRenderLodMax=32;
inline constexpr uint32_t kNativeRenderFaceFlag=1584,kNativeRenderFaceInstance=1588,kNativeRenderFacePose=1636,
  kNativeRenderWeaponGate=1768,kNativeRenderWeaponArray=1824,kNativeRenderWeaponCount=1832,
  kNativeRenderWeaponStride=1408,kNativeRenderWeaponMax=32;

// Guest list (insert 821A1628, remove 821A1678): nodes {+0 next,+4 prev,+8
// owner}, a head node at list+0 and the end marker in list+12. 821A4DE8 walks
// [Word(list),Word(list+12)). Returns the member count.
template<class Reader,class Visit>
size_t WalkNativeRenderList(const Reader& reader,uint32_t list,const Visit& visit,size_t limit=size_t(1)<<17) {
  const auto end=reader.Word(list+12);
  size_t count=0;
  for(uint32_t node=reader.Word(list);node!=end;node=reader.Word(node)) {
    if(!node || ++count>limit) throw std::runtime_error("native render list does not reach its end");
    visit(reader.Word(node+8));
  }
  return count;
}
template<class Reader> float NativeRenderFloat(const Reader& reader,uint32_t address) {
  return std::bit_cast<float>(reader.Word(address));
}
// Shared effect pool (the std::map at *(8257C02C)+4, NativePostToneLayout's):
// a handle 821A20C0 returns is node+40; the node's key std::string is at
// node+12 (text inline at +4 below capacity 16, else at *(+4); size +20,
// capacity +24); the value holds the float4 data pointer at +0 and the
// float4 count at +8.
inline constexpr uint32_t kNativeRenderPoolValue=40,kNativeRenderPoolKey=12,kNativeRenderPoolCount=8,
  kNativeRenderPoolKeyMax=64;
template<class Reader> std::string ReadNativeRenderPoolName(const Reader& reader,uint32_t handle) {
  if(handle<kNativeRenderPoolValue) throw std::runtime_error("invalid native render pool handle");
  const auto key=handle-kNativeRenderPoolValue+kNativeRenderPoolKey;
  const auto size=reader.Word(key+20),capacity=reader.Word(key+24);
  if(size>capacity || size>kNativeRenderPoolKeyMax) throw std::runtime_error("invalid native render pool key");
  if(!size) throw std::runtime_error("native render pool key is empty");
  const auto data=capacity>=16?reader.Word(key+4):key+4;
  const auto* text=reader.Bytes(data,size);
  return std::string(reinterpret_cast<const char*>(text),size);
}
// One 821A1730(pool, *(base+handle), value) store as the draws after it see
// it (NativeRenderConstantSource), merged into `constants`: a name already
// there takes the new value in place (the pool entry is one register block).
// Nothing for a null handle or a zero count (821A16D8 stores min(1, count)).
template<class Reader>
void ReadNativeRenderConstant(const Reader& reader,uint32_t base,const NativeRenderConstantSource& source,
    std::vector<NativeRenderObjectConstant>& constants) {
  if(!source.handle) return;
  const auto handle=reader.Word(base+source.handle);
  if(!handle || !reader.Word(handle+kNativeRenderPoolCount)) return;
  NativeRenderObjectConstant constant;
  constant.name=ReadNativeRenderPoolName(reader,handle);
  if(source.scroll) {
    // 821E7C50: fneg of the float at base+value, then 0.0 [820009A4], 0.0, 1.0 [820008CC].
    const auto x=reader.Word(base+source.value)^0x80000000u;
    const std::array<uint32_t,4> words{x,0u,0u,0x3F800000u};
    for(size_t i=0;i<16;++i) constant.registers[i]=uint8_t(words[i/4]>>(24-(i%4)*8));
  } else {
    const auto* bytes=reader.Bytes(base+source.value,16);
    std::copy(bytes,bytes+16,constant.registers.begin());
  }
  const auto same=std::find_if(constants.begin(),constants.end(),[&](const auto& c) { return c.name==constant.name; });
  if(same!=constants.end()) same->registers=constant.registers;
  else constants.push_back(std::move(constant));
}
// Bitwise equality; the pose, layout and entry pointers compare by identity
// because the registry shares them whenever their content is unchanged (pose
// motions too: previous by identity, tick and render_dependent by value).
inline bool SameNativeRenderEntry(const NativeRenderEntry& a,const NativeRenderEntry& b) {
  const auto bits=[](float value) { return std::bit_cast<uint32_t>(value); };
  const auto same_models=[](const NativeRenderModel& x,const NativeRenderModel& y) {
    return x.instance==y.instance && x.layout==y.layout;
  };
  if(a.object!=b.object || a.generation!=b.generation || a.type!=b.type || a.mode!=b.mode || a.hidden!=b.hidden ||
     bits(a.radius)!=bits(b.radius) || bits(a.cull_distance)!=bits(b.cull_distance) || bits(a.sort_bias)!=bits(b.sort_bias) ||
     a.pose!=b.pose || a.motion!=b.motion || a.pose_vector!=b.pose_vector || a.constants!=b.constants ||
     a.lod_thresholds.size()!=b.lod_thresholds.size() ||
     a.models.size()!=b.models.size() || a.attachments.size()!=b.attachments.size() || a.instanced.size()!=b.instanced.size()) return false;
  for(size_t i=0;i<4;++i) if(bits(a.centre[i])!=bits(b.centre[i])) return false;
  // The box half axes feed the partial-sphere box test (ClassifyNativeFullFrameModel).
  for(size_t i=0;i<a.axes.size();++i) if(bits(a.axes[i])!=bits(b.axes[i])) return false;
  const auto& x=a.bound_motion,&y=b.bound_motion;
  if(x.valid!=y.valid || (x.valid && (x.tick!=y.tick || bits(x.radius)!=bits(y.radius)))) return false;
  if(x.valid) {
    for(size_t i=0;i<4;++i) if(bits(x.centre[i])!=bits(y.centre[i])) return false;
    for(size_t i=0;i<x.axes.size();++i) if(bits(x.axes[i])!=bits(y.axes[i])) return false;
  }
  for(size_t i=0;i<a.lod_thresholds.size();++i) if(bits(a.lod_thresholds[i])!=bits(b.lod_thresholds[i])) return false;
  for(size_t i=0;i<a.models.size();++i) if(!same_models(a.models[i],b.models[i])) return false;
  for(size_t i=0;i<a.attachments.size();++i) {
    const auto& x=a.attachments[i],&y=b.attachments[i];
    if(!same_models(x.model,y.model) || x.pose_vector!=y.pose_vector || x.pose!=y.pose || x.motion!=y.motion ||
       x.constants!=y.constants) return false;
  }
  for(size_t i=0;i<a.instanced.size();++i)
    if(!same_models(a.instanced[i].model,b.instanced[i].model) || a.instanced[i].worlds!=b.instanced[i].worlds ||
       a.instanced[i].motion!=b.instanced[i].motion) return false;
  return true;
}

class NativeRenderRegistry {
 public:
  struct Stats {
    // ticks: publications; light_ticks: those made with refresh false;
    // idle_ticks: refresh-false ticks that read and published nothing.
    uint64_t ticks=0,light_ticks=0,idle_ticks=0,births=0,seeded=0,rebirths=0,deaths=0,unknown_deaths=0,subscriptions=0,unknown_subscriptions=0,
      unknown_classes=0,reclassified=0,deferred=0,foreign=0,builds=0,changed=0,unchanged=0,pose_reuses=0,read_failures=0,
      layout_captures=0,layout_failures=0,instanced_worlds=0,frame_poses=0,frame_pose_reuses=0,frame_pose_failures=0,
      constant_changes=0,  // Per-object constant sets published anew (NativeRenderEntry::constants and attachments').
      // Light-tick reads of update members (probes: the rotating sample;
      // live_reads: members known to change on render-only iterations) and
      // re-reads that changed an entry read earlier at the same tick.
      probes=0,live_reads=0,same_tick_changes=0,captures_pruned=0;
    size_t records=0,subscribed=0,published=0,retrying=0,live=0;
  };
  // In-game check (edf_native_render_registry_audit): scene+84 against the
  // registry's records of that scene, scene+100 against its subscriptions.
  struct Audit {
    size_t guest=0,guest_updates=0,registry=0,missing=0,extra=0,subscription=0;
    size_t mismatches() const { return missing+extra+subscription; }
  };
  // refresh: records outside scene+100 re-read per tick, round robin, so
  // visibility changes made without a subscription still reach the snapshot.
  // probe: update members (scene+100 subscribers and the animated set)
  // re-read per render-only (light) tick, round robin, so a member whose
  // slot 2 changes its entry on render-only iterations is found within
  // members/probe such iterations (then read on every one: `live`).
  explicit NativeRenderRegistry(size_t refresh=32,size_t probe=32):refresh_(refresh),probe_(probe) {}

  // Hooks, any thread; the caller checks the enable cvar first.
  void Born(uint32_t object) { Push(object,Event::Birth); }
  void Died(uint32_t object) { Push(object,Event::Death); }
  void Subscribed(uint32_t object,bool subscribed) { Push(object,subscribed?Event::Subscribe:Event::Unsubscribe); }
  // Set by any event or tick until Clear: a disabled tick drops state once.
  bool active() const { return active_.load(std::memory_order_relaxed); }

  // Engine thread, at the end of 821A4DE8 (after its scene+100 slot-2 walk).
  // decode(instance,pose_vector,bones) returns a NativeModelLayout sized for
  // `bones` pose entries (DecodeNativeModelLayoutWith's override) or throws.
  //
  // refresh false: a render-only iteration (unlocked, no simulation step, so
  // the tick has not advanced). The hooks' events are applied, pending (new,
  // resubscribed) objects and due retries are re-read, and of the update
  // members (whose slot 2 821A4DE8 runs on every iteration) only the live
  // ones and `probe` more, round robin: not every member, the animated set
  // or the round-robin refresh, whose inputs a simulation step writes. A
  // member whose entry a light read finds changed since its read at the same
  // tick changes on render-only iterations: it is live, read on every light
  // tick (its pose becomes render-dependent, drawn as read, never blended),
  // until kLiveQuiet light reads in a row leave it unchanged. When a light
  // tick publishes no change the published snapshot is returned as is: no
  // new generation.
  template<class Reader,class Decode>
  std::shared_ptr<const NativeRenderRegistrySnapshot> Tick(const Reader& reader,uint32_t scene,uint64_t tick,const Decode& decode,
      bool refresh=true) {
    active_.store(true,std::memory_order_relaxed);
    // Reuse off (native_reuse.h): every tick is a full one (no idle return, no
    // light tick's probe or live set: every update member is read).
    if(!NativeReuseAllowed()) refresh=true;
    dirty_=false;
    Drain();
    ++stats_.ticks;
    if(!refresh) ++stats_.light_ticks;
    // Enabled mid-game (or first tick): adopt what the scene already holds.
    if(!seeded_) {
      WalkNativeRenderList(reader,scene+kNativeRenderSceneObjects,[&](uint32_t object) {
        if(object && !records_.contains(object)) { Birth(object); ++stats_.seeded; }
      });
      seeded_=true;
    }
    std::vector<uint32_t> work(pending_.begin(),pending_.end());
    pending_.clear();
    if(refresh) {
      work.insert(work.end(),subscribed_.begin(),subscribed_.end());
      work.insert(work.end(),animated_.begin(),animated_.end());
    } else {
      for(const auto& [object,quiet]:live_) { work.push_back(object); ++stats_.live_reads; }
      for(size_t visited=0;visited<probe_ && !probes_.empty();++visited) {
        if(probe_cursor_>=probes_.size()) probe_cursor_=0;
        const auto [object,generation]=probes_[probe_cursor_];
        const auto found=records_.find(object);
        if(found==records_.end() || found->second.generation!=generation || !Updating(object,found->second)) {
          if(found!=records_.end() && found->second.generation==generation) found->second.probed=false;
          probes_[probe_cursor_]=probes_.back(); probes_.pop_back(); continue;
        }
        work.push_back(object); ++probe_cursor_; ++stats_.probes;
      }
    }
    for(auto at=retry_.begin();at!=retry_.end();) {
      if(at->second<=tick) { work.push_back(at->first); at=retry_.erase(at); } else ++at;
    }
    for(size_t visited=0;refresh && visited<refresh_ && !ring_.empty();++visited) {
      if(cursor_>=ring_.size()) cursor_=0;
      const auto [object,generation]=ring_[cursor_];
      const auto found=records_.find(object);
      if(found==records_.end() || found->second.generation!=generation) { ring_[cursor_]=ring_.back(); ring_.pop_back(); continue; }
      work.push_back(object); ++cursor_;
    }
    std::sort(work.begin(),work.end());
    work.erase(std::unique(work.begin(),work.end()),work.end());
    for(const auto object:work) {
      const auto found=records_.find(object);
      if(found==records_.end()) continue;
      auto& record=found->second;
      const bool same_tick=Update(reader,scene,tick,object,record,decode);
      if(same_tick) { ++stats_.same_tick_changes; if(Updating(object,record)) live_.insert_or_assign(object,0u); }
      else if(!refresh) {
        // A live member's quiet light reads.
        const auto live=live_.find(object);
        if(live!=live_.end() && ++live->second>=kLiveQuiet) live_.erase(live);
      }
    }
    if(!refresh && !dirty_) {
      std::lock_guard lock(publish_mutex_);
      if(published_ && published_->tick==tick) { ++stats_.idle_ticks; return published_; }
    }
    auto snapshot=std::make_shared<NativeRenderRegistrySnapshot>();
    snapshot->tick=tick; snapshot->generation=++publications_; snapshot->objects=objects_; snapshot->entries=entries_;
    std::shared_ptr<const NativeRenderRegistrySnapshot> result=std::move(snapshot);
    { std::lock_guard lock(publish_mutex_); published_=result; }
    return result;
  }
  template<class Reader>
  Audit AuditScene(const Reader& reader,uint32_t scene) const {
    Audit audit;
    std::unordered_set<uint32_t> guest,updates;
    audit.guest=WalkNativeRenderList(reader,scene+kNativeRenderSceneObjects,[&](uint32_t object) { guest.insert(object); });
    audit.guest_updates=WalkNativeRenderList(reader,scene+kNativeRenderSceneUpdates,[&](uint32_t object) { updates.insert(object); });
    for(const auto object:guest) if(!records_.contains(object)) ++audit.missing;
    for(const auto& [object,record]:records_) {
      if(record.vtable && record.scene!=scene) continue; // Resolved into another scene.
      ++audit.registry;
      if(!guest.contains(object)) ++audit.extra;
      if(record.vtable && record.subscribed!=updates.contains(object)) ++audit.subscription;
    }
    return audit;
  }
  // Whether `object` is still the render object of registry lifetime
  // `generation` (NativeRenderEntry::generation): false once its destructor
  // or another constructor at its address ran (the hooks' events count at
  // once, before a Tick applies them). Any thread.
  bool Alive(uint32_t object,uint64_t generation) const {
    std::lock_guard lock(events_mutex_);
    const auto found=lifetimes_.find(object);
    return found!=lifetimes_.end() && found->second==generation;
  }
  // edf_native_render_registry_idle_audit, after a light tick: the entries a
  // full tick at the same tick would change among the update members (the
  // scene+100 subscribers and the animated set), which the light tick left
  // unread. Compare only: nothing is published, and no record's read state,
  // retry, subscription, live set or pose motion moves (an audit that
  // published would mark exactly the poses it reports render-dependent).
  // A member whose vtable changed or that is not yet resolved is counted
  // changed without being read; a failed read counts when it would unpublish.
  template<class Reader,class Decode>
  size_t AuditLight(const Reader& reader,uint32_t scene,uint64_t tick,const Decode& decode) {
    const auto stats=stats_;
    std::vector<uint32_t> work(subscribed_.begin(),subscribed_.end());
    work.insert(work.end(),animated_.begin(),animated_.end());
    std::sort(work.begin(),work.end());
    work.erase(std::unique(work.begin(),work.end()),work.end());
    size_t changes=0;
    for(const auto object:work) {
      const auto found=records_.find(object);
      if(found==records_.end()) continue;
      auto& record=found->second;
      const auto* previous=objects_.Find(object);
      try {
        if(!record.vtable || reader.Word(object)!=record.vtable) { ++changes; continue; }
        if(record.scene!=scene || !Drawable(record)) continue;
        bool complete=true;
        Build(reader,tick,object,record,decode,complete);
        changes+=!previous || !SameNativeRenderEntry(**previous,scratch_);
      } catch(const std::exception&) { changes+=previous!=nullptr; }
    }
    stats_=stats;
    return changes;
  }
  // Resolved records whose vtable is not in the class table (nothing native
  // draws them from the snapshot), per vtable: (vtable, records), ascending.
  // Kept as records resolve and die, so this costs one pass over the few
  // distinct vtables (the coverage census, edf_native_coverage_census). Same
  // thread as Tick.
  std::vector<std::pair<uint32_t,uint32_t>> UnknownClasses() const {
    std::vector<std::pair<uint32_t,uint32_t>> classes(unknown_counts_.begin(),unknown_counts_.end());
    std::sort(classes.begin(),classes.end());
    return classes;
  }
  std::shared_ptr<const NativeRenderRegistrySnapshot> AcquireSnapshot() const {
    std::lock_guard lock(publish_mutex_);
    return published_;
  }
  Stats stats() const {
    auto stats=stats_;
    stats.records=records_.size(); stats.subscribed=subscribed_.size();
    stats.published=objects_.size(); stats.retrying=retry_.size(); stats.live=live_.size();
    return stats;
  }
  void Clear() {
    { std::lock_guard lock(events_mutex_); events_.clear(); }
    { std::lock_guard lock(events_mutex_); lifetimes_.clear(); }
    records_.clear(); pending_.clear(); subscribed_.clear(); animated_.clear(); retry_.clear(); ring_.clear();
    live_.clear(); probes_.clear();
    unknown_counts_.clear();
    objects_=decltype(objects_){}; entries_.clear();
    cursor_=0; probe_cursor_=0; seeded_=false;
    { std::lock_guard lock(publish_mutex_); published_.reset(); }
    active_.store(false,std::memory_order_relaxed);
  }

 private:
  enum class Event : uint8_t { Birth,Death,Subscribe,Unsubscribe };
  struct Pending { uint32_t object=0; Event event=Event::Birth; };
  // One captured model per instance address; the identity it was decoded from.
  struct Capture {
    uint32_t instance=0,container=0,node=0,pose_vector=0;
    std::shared_ptr<const NativeModelLayout> layout;
    bool rejected=false;
    uint64_t used=0;  // Record::builds at the last Build that drew this instance.
  };
  struct Record {
    uint64_t generation=0;
    uint32_t vtable=0,scene=0; // vtable 0: not resolved yet.
    const NativeRenderClass* type=nullptr;
    bool subscribed=false;
    uint32_t retries=0;
    std::vector<Capture> captures;
    // Frame-posed classes: the tree, and the pose last computed with the
    // root bits and hierarchy build it came from.
    NativeModelHierarchyCache hierarchy;
    NativeRenderPose frame_pose;
    std::array<uint32_t,16> frame_root{};
    uint64_t frame_builds=0;
    // The tick of the last read that published the entry (pose motion).
    uint64_t read_tick=0;
    bool read=false;
    // Reads that found the base vtable (the derived constructor has not run):
    // the next one is 2^deferrals ticks later (capped), so an object that
    // never leaves it does not keep every light tick busy.
    uint32_t deferrals=0;
    // In probes_ (the light ticks' round robin over update members).
    bool probed=false;
    // Builds of this record (captures unused for kCaptureAge of them go).
    uint64_t builds=0;
  };
  void Push(uint32_t object,Event event) {
    {
      std::lock_guard lock(events_mutex_);
      events_.push_back({object,event});
      // A constructor or destructor ends the lifetime Alive answers for.
      if(event==Event::Birth || event==Event::Death) lifetimes_.erase(object);
    }
    active_.store(true,std::memory_order_relaxed);
  }
  // Whether any event was applied.
  bool Drain() {
    std::vector<Pending> events;
    { std::lock_guard lock(events_mutex_); events.swap(events_); }
    if(events.empty()) return false;
    for(const auto& [object,event]:events) {
      if(event==Event::Birth) { if(records_.contains(object)) { ++stats_.rebirths; Forget(object); } Birth(object); continue; }
      const auto found=records_.find(object);
      if(event==Event::Death) {
        if(found==records_.end()) ++stats_.unknown_deaths; else { ++stats_.deaths; Forget(object); }
        continue;
      }
      if(found==records_.end()) { ++stats_.unknown_subscriptions; continue; }
      ++stats_.subscriptions;
      Subscribe(object,found->second,event==Event::Subscribe);
    }
    return true;
  }
  void Birth(uint32_t object) {
    auto& record=records_[object];
    record.generation=++generation_;
    {
      // Unless another constructor or destructor at this address is queued
      // behind this birth (Drain applies the list it swapped out).
      std::lock_guard lock(events_mutex_);
      const bool later=std::any_of(events_.begin(),events_.end(),[&](const Pending& e) { return e.object==object && e.event!=Event::Subscribe && e.event!=Event::Unsubscribe; });
      if(!later) lifetimes_.insert_or_assign(object,record.generation);
    }
    pending_.insert(object);
    ring_.emplace_back(object,record.generation);
    ++stats_.births;
  }
  void Forget(uint32_t object) {
    if(const auto found=records_.find(object);found!=records_.end()) {
      {
        std::lock_guard lock(events_mutex_);
        const auto lifetime=lifetimes_.find(object);
        if(lifetime!=lifetimes_.end() && lifetime->second==found->second.generation) lifetimes_.erase(lifetime);
      }
      CountUnknown(found->second,false);
      records_.erase(found);
    }
    pending_.erase(object); subscribed_.erase(object); animated_.erase(object); retry_.erase(object);
    live_.erase(object);
    Unpublish(object);
  }
  // unknown_counts_ for a resolved record of no known class.
  void CountUnknown(const Record& record,bool add) {
    if(!record.vtable || record.type) return;
    if(add) { ++unknown_counts_[record.vtable]; return; }
    const auto found=unknown_counts_.find(record.vtable);
    if(found!=unknown_counts_.end() && !--found->second) unknown_counts_.erase(found);
  }
  struct EntryObject { uint32_t operator()(const std::shared_ptr<const NativeRenderEntry>& entry) const { return entry->object; } };
  void Publish(uint32_t object,std::shared_ptr<const NativeRenderEntry> entry) {
    dirty_=true; entries_.Assign(entry,EntryObject{}); objects_.Set(object,std::move(entry));
  }
  bool Unpublish(uint32_t object) {
    entries_.Erase(object,EntryObject{});
    const bool erased=objects_.Erase(object);
    dirty_=dirty_ || erased;
    return erased;
  }
  // An update member: 821A4DE8 runs its slot 2 on every iteration (scene+100),
  // or its inputs advance outside it (the animated set).
  bool Updating(uint32_t object,const Record& record) const { return record.subscribed || animated_.contains(object); }
  void Probe(uint32_t object,Record& record) {
    if(record.probed) return;
    record.probed=true; probes_.emplace_back(object,record.generation);
  }
  void Classify(uint32_t object,Record& record) { if(Updating(object,record)) Probe(object,record); }
  static bool Drawable(const Record& record) {
    return record.type && !record.type->scene_source && !record.type->effect && !record.type->other_pass;
  }
  // An unsubscribe still gets one last read: the final pose stays published.
  void Subscribe(uint32_t object,Record& record,bool subscribed) {
    record.subscribed=subscribed;
    if(subscribed) subscribed_.insert(object); else subscribed_.erase(object);
    Classify(object,record);
    pending_.insert(object);
  }
  // Returns whether the read changed an entry published by a read at this
  // same tick (a render-only iteration moved it).
  template<class Reader,class Decode>
  bool Update(const Reader& reader,uint32_t scene,uint64_t tick,uint32_t object,Record& record,const Decode& decode) {
    ++stats_.builds;
    bool built=false,complete=true;
    const bool read_now=record.read && record.read_tick==tick;
    try {
      const auto vtable=reader.Word(object);
      // The derived constructor has not stored its vtable yet: resolve at a
      // later tick, backed off (1, 2, 4 ... 2^kDeferralShift ticks).
      if(vtable==kNativeRenderBaseVtable || vtable==kNativeRenderBaseVtableEarly) {
        ++stats_.deferred;
        retry_[object]=tick+(uint64_t(1)<<std::min(record.deferrals++,kDeferralShift));
        return false;
      }
      record.deferrals=0;
      if(vtable!=record.vtable) {
        if(record.vtable) ++stats_.reclassified;
        else {
          // First resolution; the base constructor linked obj+120 itself when obj+72 was 1.
          record.scene=reader.Word(object+kNativeRenderObjectScene);
          Subscribe(object,record,reader.Word(object+kNativeRenderObjectSubscribed)==1);
          pending_.erase(object);
        }
        CountUnknown(record,false);
        record.vtable=vtable; record.type=FindNativeRenderClass(vtable); record.captures.clear();
        CountUnknown(record,true);
        record.hierarchy.Reset(); record.frame_pose.reset();
        if(!record.type) ++stats_.unknown_classes;
        // Re-read every tick: inputs that advance outside scene+100 (instanced
        // worlds in slot 3, a frame-posed root, per-object constants).
        // Per-object constants too: the float4s slot 4 stores (g_Highlight,
        // g_Time) are copied by slot 2 in the scene+100 walk, but the C_Tank
        // treads' g_Scroll source (+60) advances in slot 3, so every tick.
        if(record.type && ((record.type->attachments&kNativeRenderMotherSpheres) || record.type->frame_root ||
           NativeRenderClassHasConstants(*record.type))) animated_.insert(object);
        else animated_.erase(object);
        live_.erase(object);
        Classify(object,record);
      }
      if(record.scene!=scene) ++stats_.foreign;
      else if(Drawable(record)) { Build(reader,tick,object,record,decode,complete); built=true; }
    } catch(const std::exception&) { ++stats_.read_failures; built=false; complete=false; }
    record.read=built; record.read_tick=tick;
    if(complete) { record.retries=0; retry_.erase(object); }
    else retry_[object]=tick+(uint64_t(1)<<std::min<uint32_t>(record.retries++,10));
    const auto* previous=objects_.Find(object);
    if(!built) { if(previous) Unpublish(object); return false; }
    // Reuse off (native_reuse.h): an unchanged entry is published anew (its
    // pose, world and layout pointers are still shared: pose motion needs them).
    const bool same=previous && SameNativeRenderEntry(**previous,scratch_);
    if(same && NativeReuseAllowed()) { ++stats_.unchanged; return false; }
    // Changed (or republished with reuse off): the one allocation (and chunk
    // clone) per changed entry. Only a real change at this tick is a
    // render-only iteration's.
    const bool same_tick=read_now && previous && !same;
    Publish(object,std::make_shared<const NativeRenderEntry>(scratch_));
    ++stats_.changed;
    return same_tick;
  }
  // Fills scratch_ (vectors keep their capacity across builds); poses equal
  // to the published entry's are that entry's shared pointers.
  template<class Reader,class Decode>
  void Build(const Reader& reader,uint64_t tick,uint32_t object,Record& record,const Decode& decode,bool& complete) {
    const auto& type=*record.type;
    const auto* previous=objects_.Find(object);
    const NativeRenderEntry* old=previous?previous->get():nullptr;
    auto* entry=&scratch_;
    // Pose motion against the published entry (native_render_motion.h).
    const bool same_generation=old && old->generation==record.generation;
    const std::optional<uint64_t> read=record.read?std::optional<uint64_t>(record.read_tick):std::nullopt;
    const auto motion=[&](const NativeRenderPose& pose,const NativeRenderPose* published,const NativeRenderPoseMotion* published_motion,
        bool same_layout) {
      return AdvanceNativeRenderPoseMotion(pose,published,published_motion,same_generation && same_layout,read,tick);
    };
    entry->lod_thresholds.clear(); entry->models.clear(); entry->attachments.clear(); entry->instanced.clear();
    entry->pose.reset(); entry->pose_vector=0; entry->axes={}; entry->motion={}; entry->constants.reset();
    entry->object=object; entry->generation=record.generation; entry->type=&type;
    for(uint32_t i=0;i<4;++i) entry->centre[i]=NativeRenderFloat(reader,object+kNativeRenderObjectCentre+i*4);
    // The oriented half axes of the 821B2B00 bound (obj+304/+320/+336): the
    // box 821C33E8 tests when the sphere is only partly inside.
    for(uint32_t i=0;i<12;++i) entry->axes[i]=NativeRenderFloat(reader,object+304+i*4);
    entry->radius=NativeRenderFloat(reader,object+kNativeRenderObjectRadius);
    entry->cull_distance=NativeRenderFloat(reader,object+kNativeRenderObjectCull);
    entry->sort_bias=NativeRenderFloat(reader,object+kNativeRenderObjectBias);
    entry->mode=int32_t(reader.Word(object+kNativeRenderObjectMode));
    const auto* hidden=reader.Bytes(object+kNativeRenderObjectHidden,2);
    entry->hidden=(hidden[0]|hidden[1])!=0;
    entry->bound_motion=AdvanceNativeRenderBoundMotion(*entry,old,same_generation,read,tick);
    ++record.builds;
    if(!type.instance || !type.pose) return; // Tracked, no model: visibility only.
    // The pool constants slot 4 stores before its first draw (821A1730), in
    // effect for every later draw of this slot 4 until one is stored again.
    constants_.clear();
    for(const auto& source:type.constants) ReadNativeRenderConstant(reader,object,source,constants_);
    entry->constants=ShareConstants(old?old->constants:nullptr);
    entry->pose_vector=object+type.pose;
    if(type.frame_root) {
      // A tree the walk rejects publishes the entry unposed (visibility and
      // routing still apply) and retries.
      std::optional<uint32_t> bones;
      try {
        entry->pose=FramePose(reader,object,record,old?old->pose:nullptr);
        bones=uint32_t(entry->pose->size());
      } catch(const std::exception&) { entry->pose.reset(); record.frame_pose.reset(); ++stats_.frame_pose_failures; complete=false; }
      entry->models.push_back(Model(reader,record,object+type.instance,entry->pose_vector,decode,complete,bones.value_or(0)));
    } else {
      entry->pose=ReadPose(reader,entry->pose_vector,old?old->pose:nullptr);
      entry->models.push_back(Model(reader,record,object+type.instance,entry->pose_vector,decode,complete));
    }
    if(type.lod==NativeRenderLodKind::Character) {
      const auto records=reader.Word(object+kNativeRenderLodRecords),count=reader.Word(object+kNativeRenderLodCount);
      if(count>kNativeRenderLodMax || (count && !records)) throw std::runtime_error("invalid native render LOD table");
      for(uint32_t i=0;i<count;++i) {
        const auto at=records+i*kNativeRenderLodStride;
        entry->lod_thresholds.push_back(NativeRenderFloat(reader,at));
        entry->models.push_back(Model(reader,record,at+4,entry->pose_vector,decode,complete));
      }
    }
    // Every LOD model draws the one pose: any model or layout change resets it.
    bool same_models=old && old->models.size()==entry->models.size();
    for(size_t i=0;same_models && i<entry->models.size();++i)
      same_models=old->models[i].instance==entry->models[i].instance && old->models[i].layout==entry->models[i].layout;
    entry->motion=motion(entry->pose,old?&old->pose:nullptr,old?&old->motion:nullptr,same_models);
    const auto attach=[&](uint32_t instance,uint32_t vector) {
      NativeRenderAttachment attachment;
      attachment.pose_vector=vector;
      attachment.model=Model(reader,record,instance,vector,decode,complete);
      const NativeRenderAttachment* earlier=nullptr;
      if(old) for(const auto& candidate:old->attachments) if(candidate.pose_vector==vector) { earlier=&candidate; break; }
      attachment.pose=ReadPose(reader,vector,earlier?earlier->pose:nullptr);
      attachment.motion=motion(attachment.pose,earlier?&earlier->pose:nullptr,earlier?&earlier->motion:nullptr,
        earlier && earlier->model.instance==attachment.model.instance && earlier->model.layout==attachment.model.layout);
      attachment.constants=ShareConstants(earlier?earlier->constants:nullptr);
      entry->attachments.push_back(std::move(attachment));
    };
    if((type.attachments&kNativeRenderFace) && reader.Bytes(object+kNativeRenderFaceFlag,1)[0])
      attach(object+kNativeRenderFaceInstance,object+kNativeRenderFacePose);
    if((type.attachments&kNativeRenderWeapons) && !reader.Word(object+kNativeRenderWeaponGate)) {
      const auto first=reader.Word(object+kNativeRenderWeaponArray),count=reader.Word(object+kNativeRenderWeaponCount);
      if(count>kNativeRenderWeaponMax || (count && !first)) throw std::runtime_error("invalid native render weapon array");
      for(uint32_t i=0;i<count;++i) {
        const auto weapon=first+i*kNativeRenderWeaponStride;
        const auto* flags=reader.Bytes(weapon+1404,2);
        if(!flags[0] || !flags[1] || !reader.Word(weapon+108)) continue;
        if(int32_t(reader.Word(weapon+412))==2 && !reader.Word(weapon+804)) continue;
        attach(weapon+100,weapon+144);
      }
    }
    // Vehicle weapon groups (82199DD8 / 821E4E90): 820E1A80 on element+64.
    for(const auto& group:type.weapon_groups) {
      if(!group.group) continue;
      const auto first=reader.Word(object+group.group+group.array),count=reader.Word(object+group.group+group.count);
      if(count>kNativeRenderWeaponMax || (count && !first)) throw std::runtime_error("invalid native render vehicle weapon array");
      for(uint32_t i=0;i<count;++i) {
        const auto weapon=first+i*kNativeRenderVehicleWeaponStride+kNativeRenderVehicleWeaponOffset;
        if(!reader.Bytes(weapon+1404,1)[0] || !reader.Word(weapon+108)) continue;
        if(int32_t(reader.Word(weapon+412))==2 && !reader.Word(weapon+804)) continue;
        attach(weapon+100,weapon+144);
      }
    }
    // Parts (820F01E8 / 821E7C50): each stores its constants, then draws.
    for(uint32_t i=0;i<type.parts.count;++i) {
      const auto part=object+type.parts.base+i*type.parts.stride;
      for(const auto& source:type.parts.constants) ReadNativeRenderConstant(reader,part,source,constants_);
      attach(part+type.parts.instance,part+type.parts.pose);
    }
    // 820EC180: after the +1100 draw, obj+1172 once per record world. The
    // worlds are shared with the published entry while bitwise unchanged.
    if(type.attachments&kNativeRenderMotherSpheres) {
      NativeRenderInstanced set;
      set.model=Model(reader,record,object+NativeMotherSpheres::instance,0,decode,complete);
      ReadNativeMotherSphereWorlds(reader,object,worlds_);
      stats_.instanced_worlds+=worlds_.size();
      const NativeRenderInstanced* earlier=nullptr;
      if(old) for(const auto& candidate:old->instanced) if(candidate.model.instance==set.model.instance) { earlier=&candidate; break; }
      if(earlier && earlier->worlds && SameNativeModelPose(*earlier->worlds,worlds_)) set.worlds=earlier->worlds;
      if(!set.worlds) set.worlds=std::make_shared<const std::vector<NativePoseMatrix>>(worlds_);
      set.motion=motion(set.worlds,earlier?&earlier->worlds:nullptr,earlier?&earlier->motion:nullptr,
        earlier && earlier->model.layout==set.model.layout);
      entry->instanced.push_back(std::move(set));
    }
  }
  // The constants in effect (constants_) as a shared value: the published
  // one while equal, null when there are none.
  NativeRenderConstants ShareConstants(const NativeRenderConstants& previous) {
    if(constants_.empty()) return nullptr;
    if(previous && *previous==constants_ && NativeReuseAllowed()) return previous;
    ++stats_.constant_changes;
    return std::make_shared<const std::vector<NativeRenderObjectConstant>>(constants_);
  }
  // The guest bytes are compared with the previous copy in place; only a
  // changed pose is decoded into a new shared copy.
  template<class Reader>
  NativeRenderPose ReadPose(const Reader& reader,uint32_t vector,const NativeRenderPose& previous) {
    const auto range=ReadNativeModelPoseRange(reader,vector);
    const auto* bytes=range.count?reader.Bytes(range.begin,size_t(range.count)*64):nullptr;
    if(previous && SameNativeModelPoseBytes(bytes,range.count,*previous)) { ++stats_.pose_reuses; return previous; }
    auto matrices=std::make_shared<std::vector<NativePoseMatrix>>(range.count);
    for(size_t bone=0;bone<range.count;++bone) for(size_t i=0;i<16;++i)
      (*matrices)[bone][i]=std::bit_cast<float>(GuestBlockWord(bytes+bone*64+i*4));
    return matrices;
  }
  // Frame cadence (NativeRenderClass::frame_root): slot 4's
  // 821C8C58(obj+instance+16, obj+frame_root) and 821C9478 computed from the
  // tick's object fields, the values a render between this tick and the next
  // would build. Recomputed only when the root's bits or the tree changed;
  // shared with the published pose while bitwise equal. Throws on a tree the
  // walk rejects.
  template<class Reader>
  NativeRenderPose FramePose(const Reader& reader,uint32_t object,Record& record,const NativeRenderPose& previous) {
    const auto& type=*record.type;
    const auto& hierarchy=record.hierarchy.Acquire(reader,object+type.instance+NativeModelTree::instance_offset);
    const auto root=ReadNativeGuestMatrix(reader,object+type.frame_root);
    const auto bits=std::bit_cast<std::array<uint32_t,16>>(root);
    // Reuse off (native_reuse.h): computed again (and still shared with the
    // published pose below when bitwise equal).
    if(record.frame_pose && record.frame_builds==record.hierarchy.builds() && record.frame_root==bits && NativeReuseAllowed()) {
      ++stats_.frame_pose_reuses; return record.frame_pose;
    }
    auto palette=ComputeNativeModelHierarchyPose(hierarchy,root).palette;
    ++stats_.frame_poses;
    record.frame_root=bits; record.frame_builds=record.hierarchy.builds();
    if(previous && SameNativeModelPose(*previous,palette)) record.frame_pose=previous;
    else record.frame_pose=std::make_shared<const std::vector<NativePoseMatrix>>(std::move(palette));
    return record.frame_pose;
  }
  // Captured once per (instance, container, node, pose vector, bone count),
  // and only when the model is assigned and its pose vector is sized: the
  // first tick after the constructor, after this tick's slot-2 pose build.
  // vector 0 is a 821C9DA8 model (single world, no pose): assigned suffices.
  // sized: the natively computed pose's count, for a frame-posed class whose
  // guest vector full-frame mode never sizes (0: no pose, not captured).
  template<class Reader,class Decode>
  NativeRenderModel Model(const Reader& reader,Record& record,uint32_t instance,uint32_t vector,const Decode& decode,bool& complete,
      std::optional<uint32_t> sized=std::nullopt) {
    NativeRenderModel model; model.instance=instance;
    const auto container=reader.Word(instance),node=reader.Word(instance+4);
    const auto bones=sized?*sized:vector?ReadNativeModelPoseRange(reader,vector).count:0;
    auto capture=std::find_if(record.captures.begin(),record.captures.end(),[&](const Capture& c) { return c.instance==instance; });
    if(capture!=record.captures.end() && capture->container==container && capture->node==node && capture->pose_vector==vector &&
       (capture->rejected || (capture->layout && capture->layout->bones==bones))) {
      capture->used=record.builds;
      model.layout=capture->layout;
      return model;
    }
    if(capture==record.captures.end()) {
      // Captures of instances no Build drew for kCaptureAge builds (a weapon
      // array reallocated, a LOD table replaced) release their layouts.
      const auto before=record.captures.size();
      std::erase_if(record.captures,[&](const Capture& c) { return c.used+kCaptureAge<record.builds; });
      stats_.captures_pruned+=before-record.captures.size();
      capture=record.captures.insert(record.captures.end(),Capture{instance});
    }
    capture->used=record.builds;
    capture->container=container; capture->node=node; capture->pose_vector=vector;
    capture->layout.reset(); capture->rejected=false;
    if(!container || !node || (vector && !bones)) { complete=false; return model; }
    try {
      capture->layout=std::make_shared<const NativeModelLayout>(decode(instance,vector,bones));
      ++stats_.layout_captures;
    } catch(const std::exception&) { capture->rejected=true; ++stats_.layout_failures; }
    model.layout=capture->layout;
    return model;
  }

  static constexpr uint32_t kDeferralShift=6;   // At most 64 ticks between reads of a base-vtable object.
  static constexpr uint32_t kLiveQuiet=240;     // Unchanged light reads before a live member is probed again.
  static constexpr uint64_t kCaptureAge=64;     // Builds a capture survives unused.
  size_t refresh_,probe_;
  std::atomic<bool> active_{false};
  mutable std::mutex events_mutex_;
  std::vector<Pending> events_;
  // object -> the generation of its current lifetime (Alive); guarded by events_mutex_.
  std::unordered_map<uint32_t,uint64_t> lifetimes_;
  // Engine thread only.
  std::unordered_map<uint32_t,Record> records_;
  std::unordered_set<uint32_t> pending_,subscribed_,animated_;  // animated_: re-read every tick (instanced worlds)
  std::unordered_map<uint32_t,uint64_t> retry_;              // object -> next tick to retry
  std::unordered_map<uint32_t,uint32_t> unknown_counts_;     // vtable -> resolved records of no known class
  std::vector<std::pair<uint32_t,uint64_t>> ring_;           // (object, generation), lazily pruned
  std::vector<std::pair<uint32_t,uint64_t>> probes_;         // Update members (object, generation), lazily pruned
  std::unordered_map<uint32_t,uint32_t> live_;               // object -> unchanged light reads in a row
  size_t cursor_=0,probe_cursor_=0;
  bool seeded_=false,dirty_=false;
  uint64_t generation_=0,publications_=0;
  NativeSharedMap<uint32_t,std::shared_ptr<const NativeRenderEntry>> objects_;
  NativeSharedVector<std::shared_ptr<const NativeRenderEntry>> entries_;  // objects_' values, same order
  NativeRenderEntry scratch_;
  std::vector<NativePoseMatrix> worlds_;  // Instanced world scratch.
  std::vector<NativeRenderObjectConstant> constants_;  // Constants in effect during one Build.
  Stats stats_;
  mutable std::mutex publish_mutex_;
  std::shared_ptr<const NativeRenderRegistrySnapshot> published_;
};
// Entries that differ between two snapshots: an object in only one of them,
// or whose entry is another object (the registry carries an unchanged entry's
// pointer over). Both lists are ordered by object.
inline size_t CountNativeRenderSnapshotChanges(const NativeRenderRegistrySnapshot& a,const NativeRenderRegistrySnapshot& b) {
  size_t i=0,j=0,changes=0;
  while(i<a.entries.size() || j<b.entries.size()) {
    if(j==b.entries.size() || (i<a.entries.size() && a.entries[i]->object<b.entries[j]->object)) { ++changes; ++i; }
    else if(i==a.entries.size() || b.entries[j]->object<a.entries[i]->object) { ++changes; ++j; }
    else { changes+=a.entries[i]!=b.entries[j]; ++i; ++j; }
  }
  return changes;
}
// Process-wide registry fed by the guest hooks; leaked like ModelPublications.
NativeRenderRegistry& RenderRegistry();
}

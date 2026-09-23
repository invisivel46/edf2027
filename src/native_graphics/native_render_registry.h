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
//   waiting for their first layout (backed off) and a small round-robin
//   refresh of the rest, and publishes a snapshot whose unchanged entries are
//   the previous pointers.
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
#include "native_model_publication.h"
#include "native_render_entry.h"
#include "native_render_instances.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <memory>
#include <mutex>
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
// Bitwise equality; the pose, layout and entry pointers compare by identity
// because the registry shares them whenever their content is unchanged.
inline bool SameNativeRenderEntry(const NativeRenderEntry& a,const NativeRenderEntry& b) {
  const auto bits=[](float value) { return std::bit_cast<uint32_t>(value); };
  const auto same_models=[](const NativeRenderModel& x,const NativeRenderModel& y) {
    return x.instance==y.instance && x.layout==y.layout;
  };
  if(a.object!=b.object || a.generation!=b.generation || a.type!=b.type || a.mode!=b.mode || a.hidden!=b.hidden ||
     bits(a.radius)!=bits(b.radius) || bits(a.cull_distance)!=bits(b.cull_distance) || bits(a.sort_bias)!=bits(b.sort_bias) ||
     a.pose!=b.pose || a.pose_vector!=b.pose_vector || a.lod_thresholds.size()!=b.lod_thresholds.size() ||
     a.models.size()!=b.models.size() || a.attachments.size()!=b.attachments.size() || a.instanced.size()!=b.instanced.size()) return false;
  for(size_t i=0;i<4;++i) if(bits(a.centre[i])!=bits(b.centre[i])) return false;
  for(size_t i=0;i<a.lod_thresholds.size();++i) if(bits(a.lod_thresholds[i])!=bits(b.lod_thresholds[i])) return false;
  for(size_t i=0;i<a.models.size();++i) if(!same_models(a.models[i],b.models[i])) return false;
  for(size_t i=0;i<a.attachments.size();++i) {
    const auto& x=a.attachments[i],&y=b.attachments[i];
    if(!same_models(x.model,y.model) || x.pose_vector!=y.pose_vector || x.pose!=y.pose) return false;
  }
  for(size_t i=0;i<a.instanced.size();++i)
    if(!same_models(a.instanced[i].model,b.instanced[i].model) || a.instanced[i].worlds!=b.instanced[i].worlds) return false;
  return true;
}

class NativeRenderRegistry {
 public:
  struct Stats {
    uint64_t ticks=0,births=0,seeded=0,rebirths=0,deaths=0,unknown_deaths=0,subscriptions=0,unknown_subscriptions=0,
      unknown_classes=0,reclassified=0,deferred=0,foreign=0,builds=0,changed=0,unchanged=0,pose_reuses=0,read_failures=0,
      layout_captures=0,layout_failures=0,instanced_worlds=0;
    size_t records=0,subscribed=0,published=0,retrying=0;
  };
  // In-game check (edf_native_render_registry_audit): scene+84 against the
  // registry's records of that scene, scene+100 against its subscriptions.
  struct Audit {
    size_t guest=0,guest_updates=0,registry=0,missing=0,extra=0,subscription=0;
    size_t mismatches() const { return missing+extra+subscription; }
  };
  // refresh: records outside scene+100 re-read per tick, round robin, so
  // visibility changes made without a subscription still reach the snapshot.
  explicit NativeRenderRegistry(size_t refresh=32):refresh_(refresh) {}

  // Hooks, any thread; the caller checks the enable cvar first.
  void Born(uint32_t object) { Push(object,Event::Birth); }
  void Died(uint32_t object) { Push(object,Event::Death); }
  void Subscribed(uint32_t object,bool subscribed) { Push(object,subscribed?Event::Subscribe:Event::Unsubscribe); }
  // Set by any event or tick until Clear: a disabled tick drops state once.
  bool active() const { return active_.load(std::memory_order_relaxed); }

  // Engine thread, at the end of 821A4DE8 (after its scene+100 slot-2 walk).
  // decode(instance,pose_vector) returns a NativeModelLayout or throws.
  template<class Reader,class Decode>
  std::shared_ptr<const NativeRenderRegistrySnapshot> Tick(const Reader& reader,uint32_t scene,uint64_t tick,const Decode& decode) {
    active_.store(true,std::memory_order_relaxed);
    ++stats_.ticks;
    Drain();
    // Enabled mid-game (or first tick): adopt what the scene already holds.
    if(!seeded_) {
      WalkNativeRenderList(reader,scene+kNativeRenderSceneObjects,[&](uint32_t object) {
        if(object && !records_.contains(object)) { Birth(object); ++stats_.seeded; }
      });
      seeded_=true;
    }
    std::vector<uint32_t> work(pending_.begin(),pending_.end());
    pending_.clear();
    work.insert(work.end(),subscribed_.begin(),subscribed_.end());
    work.insert(work.end(),animated_.begin(),animated_.end());
    for(auto at=retry_.begin();at!=retry_.end();) {
      if(at->second<=tick) { work.push_back(at->first); at=retry_.erase(at); } else ++at;
    }
    for(size_t visited=0;visited<refresh_ && !ring_.empty();++visited) {
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
      if(found!=records_.end()) Update(reader,scene,tick,object,found->second,decode);
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
  std::shared_ptr<const NativeRenderRegistrySnapshot> AcquireSnapshot() const {
    std::lock_guard lock(publish_mutex_);
    return published_;
  }
  Stats stats() const {
    auto stats=stats_;
    stats.records=records_.size(); stats.subscribed=subscribed_.size();
    stats.published=objects_.size(); stats.retrying=retry_.size();
    return stats;
  }
  void Clear() {
    { std::lock_guard lock(events_mutex_); events_.clear(); }
    records_.clear(); pending_.clear(); subscribed_.clear(); animated_.clear(); retry_.clear(); ring_.clear();
    objects_=decltype(objects_){}; entries_.clear();
    cursor_=0; seeded_=false;
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
  };
  struct Record {
    uint64_t generation=0;
    uint32_t vtable=0,scene=0; // vtable 0: not resolved yet.
    const NativeRenderClass* type=nullptr;
    bool subscribed=false;
    uint32_t retries=0;
    std::vector<Capture> captures;
  };
  void Push(uint32_t object,Event event) {
    { std::lock_guard lock(events_mutex_); events_.push_back({object,event}); }
    active_.store(true,std::memory_order_relaxed);
  }
  void Drain() {
    std::vector<Pending> events;
    { std::lock_guard lock(events_mutex_); events.swap(events_); }
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
  }
  void Birth(uint32_t object) {
    auto& record=records_[object];
    record.generation=++generation_;
    pending_.insert(object);
    ring_.emplace_back(object,record.generation);
    ++stats_.births;
  }
  void Forget(uint32_t object) {
    records_.erase(object); pending_.erase(object); subscribed_.erase(object); animated_.erase(object); retry_.erase(object);
    Unpublish(object);
  }
  struct EntryObject { uint32_t operator()(const std::shared_ptr<const NativeRenderEntry>& entry) const { return entry->object; } };
  void Publish(uint32_t object,std::shared_ptr<const NativeRenderEntry> entry) {
    entries_.Assign(entry,EntryObject{}); objects_.Set(object,std::move(entry));
  }
  bool Unpublish(uint32_t object) { entries_.Erase(object,EntryObject{}); return objects_.Erase(object); }
  // An unsubscribe still gets one last read: the final pose stays published.
  void Subscribe(uint32_t object,Record& record,bool subscribed) {
    record.subscribed=subscribed;
    if(subscribed) subscribed_.insert(object); else subscribed_.erase(object);
    pending_.insert(object);
  }
  template<class Reader,class Decode>
  void Update(const Reader& reader,uint32_t scene,uint64_t tick,uint32_t object,Record& record,const Decode& decode) {
    ++stats_.builds;
    bool built=false,complete=true;
    try {
      const auto vtable=reader.Word(object);
      // The derived constructor has not stored its vtable yet: resolve next tick.
      if(vtable==kNativeRenderBaseVtable || vtable==kNativeRenderBaseVtableEarly) { ++stats_.deferred; pending_.insert(object); return; }
      if(vtable!=record.vtable) {
        if(record.vtable) ++stats_.reclassified;
        else {
          // First resolution; the base constructor linked obj+120 itself when obj+72 was 1.
          record.scene=reader.Word(object+kNativeRenderObjectScene);
          Subscribe(object,record,reader.Word(object+kNativeRenderObjectSubscribed)==1);
          pending_.erase(object);
        }
        record.vtable=vtable; record.type=FindNativeRenderClass(vtable); record.captures.clear();
        if(!record.type) ++stats_.unknown_classes;
        if(record.type && (record.type->attachments&kNativeRenderMotherSpheres)) animated_.insert(object); else animated_.erase(object);
      }
      if(record.scene!=scene) ++stats_.foreign;
      else if(record.type && !record.type->scene_source && !record.type->effect && !record.type->other_pass) { Build(reader,object,record,decode,complete); built=true; }
    } catch(const std::exception&) { ++stats_.read_failures; built=false; complete=false; }
    if(complete) { record.retries=0; retry_.erase(object); }
    else retry_[object]=tick+(uint64_t(1)<<std::min<uint32_t>(record.retries++,10));
    const auto* previous=objects_.Find(object);
    if(!built) { if(previous) Unpublish(object); return; }
    if(previous && SameNativeRenderEntry(**previous,scratch_)) { ++stats_.unchanged; return; }
    // Changed: the one allocation (and chunk clone) per changed entry.
    Publish(object,std::make_shared<const NativeRenderEntry>(scratch_));
    ++stats_.changed;
  }
  // Fills scratch_ (vectors keep their capacity across builds); poses equal
  // to the published entry's are that entry's shared pointers.
  template<class Reader,class Decode>
  void Build(const Reader& reader,uint32_t object,Record& record,const Decode& decode,bool& complete) {
    const auto& type=*record.type;
    const auto* previous=objects_.Find(object);
    const NativeRenderEntry* old=previous?previous->get():nullptr;
    auto* entry=&scratch_;
    entry->lod_thresholds.clear(); entry->models.clear(); entry->attachments.clear(); entry->instanced.clear();
    entry->pose.reset(); entry->pose_vector=0; entry->axes={};
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
    if(!type.instance || !type.pose) return; // Tracked, no model: visibility only.
    entry->pose_vector=object+type.pose;
    entry->pose=ReadPose(reader,entry->pose_vector,old?old->pose:nullptr);
    entry->models.push_back(Model(reader,record,object+type.instance,entry->pose_vector,decode,complete));
    if(type.lod==NativeRenderLodKind::Character) {
      const auto records=reader.Word(object+kNativeRenderLodRecords),count=reader.Word(object+kNativeRenderLodCount);
      if(count>kNativeRenderLodMax || (count && !records)) throw std::runtime_error("invalid native render LOD table");
      for(uint32_t i=0;i<count;++i) {
        const auto at=records+i*kNativeRenderLodStride;
        entry->lod_thresholds.push_back(NativeRenderFloat(reader,at));
        entry->models.push_back(Model(reader,record,at+4,entry->pose_vector,decode,complete));
      }
    }
    const auto attach=[&](uint32_t instance,uint32_t vector) {
      NativeRenderAttachment attachment;
      attachment.pose_vector=vector;
      attachment.model=Model(reader,record,instance,vector,decode,complete);
      NativeRenderPose shared;
      if(old) for(const auto& earlier:old->attachments) if(earlier.pose_vector==vector) { shared=earlier.pose; break; }
      attachment.pose=ReadPose(reader,vector,shared);
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
    // 820EC180: after the +1100 draw, obj+1172 once per record world. The
    // worlds are shared with the published entry while bitwise unchanged.
    if(type.attachments&kNativeRenderMotherSpheres) {
      NativeRenderInstanced set;
      set.model=Model(reader,record,object+NativeMotherSpheres::instance,0,decode,complete);
      ReadNativeMotherSphereWorlds(reader,object,worlds_);
      stats_.instanced_worlds+=worlds_.size();
      if(old) for(const auto& earlier:old->instanced)
        if(earlier.model.instance==set.model.instance && earlier.worlds && SameNativeModelPose(*earlier.worlds,worlds_)) { set.worlds=earlier.worlds; break; }
      if(!set.worlds) set.worlds=std::make_shared<const std::vector<NativePoseMatrix>>(worlds_);
      entry->instanced.push_back(std::move(set));
    }
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
  // Captured once per (instance, container, node, pose vector, bone count),
  // and only when the model is assigned and its pose vector is sized: the
  // first tick after the constructor, after this tick's slot-2 pose build.
  // vector 0 is a 821C9DA8 model (single world, no pose): assigned suffices.
  template<class Reader,class Decode>
  NativeRenderModel Model(const Reader& reader,Record& record,uint32_t instance,uint32_t vector,const Decode& decode,bool& complete) {
    NativeRenderModel model; model.instance=instance;
    const auto container=reader.Word(instance),node=reader.Word(instance+4);
    const auto bones=vector?ReadNativeModelPoseRange(reader,vector).count:0;
    auto capture=std::find_if(record.captures.begin(),record.captures.end(),[&](const Capture& c) { return c.instance==instance; });
    if(capture!=record.captures.end() && capture->container==container && capture->node==node && capture->pose_vector==vector &&
       (capture->rejected || (capture->layout && capture->layout->bones==bones))) {
      model.layout=capture->layout;
      return model;
    }
    if(capture==record.captures.end()) capture=record.captures.insert(record.captures.end(),Capture{instance});
    capture->container=container; capture->node=node; capture->pose_vector=vector;
    capture->layout.reset(); capture->rejected=false;
    if(!container || !node || (vector && !bones)) { complete=false; return model; }
    try {
      capture->layout=std::make_shared<const NativeModelLayout>(decode(instance,vector));
      ++stats_.layout_captures;
    } catch(const std::exception&) { capture->rejected=true; ++stats_.layout_failures; }
    model.layout=capture->layout;
    return model;
  }

  size_t refresh_;
  std::atomic<bool> active_{false};
  std::mutex events_mutex_;
  std::vector<Pending> events_;
  // Engine thread only.
  std::unordered_map<uint32_t,Record> records_;
  std::unordered_set<uint32_t> pending_,subscribed_,animated_;  // animated_: re-read every tick (instanced worlds)
  std::unordered_map<uint32_t,uint64_t> retry_;              // object -> next tick to retry
  std::vector<std::pair<uint32_t,uint64_t>> ring_;           // (object, generation), lazily pruned
  size_t cursor_=0;
  bool seeded_=false;
  uint64_t generation_=0,publications_=0;
  NativeSharedMap<uint32_t,std::shared_ptr<const NativeRenderEntry>> objects_;
  NativeSharedVector<std::shared_ptr<const NativeRenderEntry>> entries_;  // objects_' values, same order
  NativeRenderEntry scratch_;
  std::vector<NativePoseMatrix> worlds_;  // Instanced world scratch.
  Stats stats_;
  mutable std::mutex publish_mutex_;
  std::shared_ptr<const NativeRenderRegistrySnapshot> published_;
};
// Process-wide registry fed by the guest hooks; leaked like ModelPublications.
NativeRenderRegistry& RenderRegistry();
}

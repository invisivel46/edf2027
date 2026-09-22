#pragma once
#include "guest_block.h"
#include "native_scene_sources.h"
#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

namespace edf::native {
// The per-world static walk plan (step 3 of native world selection). The
// visibility walk (sub_820B4038 hook) visits each leaf list per frame; the
// plan, published at the simulation step (820B4250 post-hook), holds what it
// would otherwise look up per candidate.
//
// What is cached and why:
//  - membership (node, owner, order, list header): only 821A1628/821A1678
//    link or unlink list nodes, and those hooks Touch the affected list; the
//    walk also checks the header words (list+0, list+12) once per list.
//  - vtable+16 for the vtable the member had at the step: vtables are image
//    data, never stored to, so the slot is valid whenever the live vtable is
//    the same word. The vtable itself is not trusted (ctor/dtor rewrite +0).
//  - the source candidate (visibility record, LOD part spans): valid while
//    NativeSceneSources::CandidateRevision() equals the revision it was read at.
// What stays live: +0 vtable, +52 mode (sub_821C0AF8 and inline stw sites),
// +64 hidden (sth sites such as sub_821C0B90). All three are in the 80-byte
// header the walk already fetches to write the +48 marker, so reading them
// live costs no extra line; the plan copies are advisory (audit drift only).
inline constexpr uint32_t kNativeStaticDirectRender=0x820B2670;
// sub_821C0C00's order: lhz 64 nonzero returns; lwz 52 signed: 0 is the render
// slot (direct when vtable+16 is 820B2670), 1/2 the depth buckets, anything
// else the original routine.
enum class NativeStaticWalkRoute : uint8_t { Hidden, Direct, Virtual, Bucket, Unknown };
inline NativeStaticWalkRoute ClassifyNativeStaticWalk(uint32_t hidden,uint32_t mode,bool direct) {
  if(hidden) return NativeStaticWalkRoute::Hidden;
  const auto sort=int32_t(mode);
  if(!sort) return direct?NativeStaticWalkRoute::Direct:NativeStaticWalkRoute::Virtual;
  return sort==1 || sort==2?NativeStaticWalkRoute::Bucket:NativeStaticWalkRoute::Unknown;
}
struct NativeStaticWalkMember {
  uint32_t node=0,owner=0;
  uint32_t vtable=0,mode=0;  // Advisory: as of the step.
  uint16_t hidden=0;         // Advisory: as of the step.
  bool direct=false;         // vtable+16==820B2670 for `vtable`.
  NativeSceneSources::Candidate source;
};
struct NativeStaticWalkList {
  uint32_t list=0,world=0,end=0;
  uint64_t membership=0;        // Stamp of the membership this plan was read with.
  uint64_t sources_revision=0;  // CandidateRevision() the sources were read at.
  std::vector<NativeStaticWalkMember> members;
  uint32_t Head() const { return members.empty()?end:members.front().node; }
  uint32_t Next(size_t index) const { return index+1<members.size()?members[index+1].node:end; }
};
// The member's vtable+16 test: the plan's answer while the live vtable is the
// one it was read through, else the live slot.
template<class ReadMethod>
bool PlannedNativeStaticDirect(const NativeStaticWalkMember& member,uint32_t vtable,ReadMethod&& read_method) {
  return member.vtable==vtable?member.direct:read_method(vtable)==kNativeStaticDirectRender;
}
inline bool SameNativeSceneCandidate(const NativeSceneSources::Candidate& a,const NativeSceneSources::Candidate& b) {
  if(a.registered!=b.registered || bool(a.visibility)!=bool(b.visibility)) return false;
  if(a.visibility && a.visibility!=b.visibility && *a.visibility!=*b.visibility) return false;
  for(size_t lod=0;lod<a.lods.size();++lod)
    if(!std::equal(a.lods[lod].begin(),a.lods[lod].end(),b.lods[lod].begin(),b.lods[lod].end())) return false;
  return true;
}
// Every occupied leaf's list (node+120) of the tree 821C61D8 walks, in walk
// order, with every node accepted. Reads only; no counters are written.
template<class Reader>
void CollectNativeSceneLeafLists(const Reader& r,uint32_t manager,std::vector<uint32_t>& lists) {
  lists.clear();
  const auto levels=r.Word(r.Add(manager,52)),level_end=r.Word(r.Add(manager,56));
  if(!levels || level_end<levels || level_end-levels<32) throw std::runtime_error("invalid native scene tree levels");
  const auto roots=r.Add(levels,16);
  const auto begin=r.Word(r.Add(roots,4)),end=r.Word(r.Add(roots,8));
  if(begin>end || (end-begin)%144) throw std::runtime_error("invalid native scene tree roots");
  size_t visited=0;
  const auto walk=[&](auto&& self,uint32_t node,uint32_t depth)->void {
    if(!node || depth>128 || ++visited>(1u<<20)) throw std::runtime_error("invalid native scene tree for a static walk plan");
    if(!r.Word(r.Add(node,116))) return;
    if(!r.Word(r.Add(node,84))) { lists.push_back(r.Add(node,120)); return; }
    for(uint32_t child=0;child<8;++child) self(self,r.Word(r.Add(node,84+child*4)),depth+1);
  };
  for(auto root=begin;root!=end;root=r.Add(root,144)) walk(walk,root,0);
}
// Reads one list as the walk would: head at list+0, sentinel at list+12,
// node+0 next, node+8 owner; each owner's header and vtable slot.
template<class Reader,class Find>
std::shared_ptr<NativeStaticWalkList> ReadNativeStaticWalkList(const Reader& r,uint32_t world,uint32_t list,
    uint64_t membership,uint64_t sources_revision,Find&& find) {
  auto plan=std::make_shared<NativeStaticWalkList>();
  plan->list=list; plan->world=world; plan->membership=membership; plan->sources_revision=sources_revision;
  plan->end=r.Word(r.Add(list,12));
  for(auto at=r.Word(list);at!=plan->end;at=r.Word(at)) {
    if(!at || plan->members.size()>=(1u<<20)) throw std::runtime_error("invalid native static walk list");
    NativeStaticWalkMember member;
    member.node=at; member.owner=r.Word(r.Add(at,8));
    member.vtable=r.Word(member.owner); member.mode=r.Word(r.Add(member.owner,52));
    member.hidden=uint16_t(r.Word(r.Add(member.owner,64))>>16);
    member.direct=member.vtable && r.Word(r.Add(member.vtable,16))==kNativeStaticDirectRender;
    member.source=find(member.owner);
    plan->members.push_back(std::move(member));
  }
  return plan;
}
// Registry of per-list plans. Not internally locked: the bridge keeps it under
// its own mutex. Touches() may be read without the lock to skip Current().
class NativeStaticWalkPlans {
 public:
  struct Stats {
    uint64_t publications=0,builds=0,reuses=0,refreshes=0,touches=0,collections=0,retired=0;
  };
  std::shared_ptr<const NativeStaticWalkList> Acquire(uint32_t list) const {
    const auto found=lists_.find(list);
    return found!=lists_.end()?found->second.plan:nullptr;
  }
  // The membership `plan` was read with is still the list's: no Touch since.
  bool Current(uint32_t list,const NativeStaticWalkList& plan) const {
    const auto found=lists_.find(list);
    return found!=lists_.end() && found->second.plan && found->second.plan->membership==plan.membership;
  }
  // Moves on every Touch that dirtied a plan and every rebuilt membership.
  uint64_t Touches() const { return touches_.load(std::memory_order_acquire); }
  // Membership hooks: anchor is a list header or a planned member node.
  bool Touch(uint32_t anchor) {
    auto found=lists_.find(anchor);
    if(found==lists_.end()) {
      const auto node=nodes_.find(anchor);
      if(node==nodes_.end()) return false;
      found=lists_.find(node->second);
      if(found==lists_.end()) return false;
    }
    if(!found->second.plan) return false;
    found->second.plan.reset(); ++stats_.touches;
    touches_.fetch_add(1,std::memory_order_acq_rel);
    return true;
  }
  void Retire(uint32_t world) {
    const auto found=worlds_.find(world);
    if(found==worlds_.end()) return;
    for(const auto list:found->second.lists) Drop(world,list);
    worlds_.erase(found); ++stats_.retired;
    touches_.fetch_add(1,std::memory_order_acq_rel);
  }
  // Incremental: the leaf set is recollected only when the tree epoch moved; a
  // list is re-read only when a Touch dropped its plan or its header differs;
  // a clean list whose sources moved only has its candidates refreshed.
  template<class Reader,class Find>
  void Publish(const Reader& r,uint32_t world,uint64_t tree_epoch,uint64_t sources_revision,Find&& find) {
    auto& entry=worlds_[world];
    ++stats_.publications;
    if(!entry.collected || entry.tree_epoch!=tree_epoch) {
      std::vector<uint32_t> lists;
      CollectNativeSceneLeafLists(r,world,lists);
      lists.push_back(r.Add(world,372));
      const std::set<uint32_t> keep(lists.begin(),lists.end());
      for(const auto list:entry.lists) if(!keep.contains(list)) Drop(world,list);
      entry.lists=std::move(lists); entry.tree_epoch=tree_epoch; entry.collected=true; ++stats_.collections;
    }
    for(const auto list:entry.lists) {
      auto& slot=lists_[list];
      if(slot.plan && slot.plan->world==world && slot.plan->Head()==r.Word(list) && slot.plan->end==r.Word(r.Add(list,12))) {
        if(slot.plan->sources_revision==sources_revision) { ++stats_.reuses; continue; }
        auto next=std::make_shared<NativeStaticWalkList>(*slot.plan);
        next->sources_revision=sources_revision;
        for(auto& member:next->members) member.source=find(member.owner);
        slot.plan=std::move(next); ++stats_.refreshes; continue;
      }
      auto plan=ReadNativeStaticWalkList(r,world,list,++next_membership_,sources_revision,find);
      for(const auto node:slot.indexed) { const auto at=nodes_.find(node); if(at!=nodes_.end() && at->second==list) nodes_.erase(at); }
      slot.indexed.clear(); slot.indexed.reserve(plan->members.size());
      for(const auto& member:plan->members) { nodes_[member.node]=list; slot.indexed.push_back(member.node); }
      slot.plan=std::move(plan); ++stats_.builds;
      touches_.fetch_add(1,std::memory_order_acq_rel);
    }
  }
  const Stats& stats() const { return stats_; }
  size_t lists() const { return lists_.size(); }
  size_t nodes() const { return nodes_.size(); }
 private:
  struct Slot {
    std::shared_ptr<const NativeStaticWalkList> plan;
    std::vector<uint32_t> indexed;  // Nodes routed here by Touch, kept after a Touch.
  };
  struct World { uint64_t tree_epoch=0; bool collected=false; std::vector<uint32_t> lists; };
  void Drop(uint32_t world,uint32_t list) {
    const auto found=lists_.find(list);
    if(found==lists_.end() || (found->second.plan && found->second.plan->world!=world)) return;
    for(const auto node:found->second.indexed) { const auto at=nodes_.find(node); if(at!=nodes_.end() && at->second==list) nodes_.erase(at); }
    lists_.erase(found);
  }
  std::map<uint32_t,Slot> lists_;
  std::map<uint32_t,uint32_t> nodes_;
  std::map<uint32_t,World> worlds_;
  std::atomic<uint64_t> touches_{0};
  uint64_t next_membership_=0;
  Stats stats_;
};
// edf_native_scene_static_walk_audit: plan-driven classification next to the
// live reads. Membership, route and source differences are mismatches; drift
// counts advisory header copies that gameplay has since rewritten.
struct NativeStaticWalkAudit {
  uint64_t lists=0,members=0,classified=0,membership=0,routes=0,sources=0,drift=0,abandoned=0;
  uint64_t mismatches() const { return membership+routes+sources; }
};
// One live candidate: node/owner/next from the list, the header words, the
// live vtable+16 test, and the live source when the plan's is current.
inline bool AuditNativeStaticWalkMember(const NativeStaticWalkList& plan,size_t index,uint32_t node,uint32_t next,
    uint32_t owner,NativeStaticWalkAudit& audit) {
  ++audit.members;
  if(index>=plan.members.size() || plan.members[index].node!=node || plan.members[index].owner!=owner || plan.Next(index)!=next) {
    ++audit.membership; return false;
  }
  return true;
}
inline void AuditNativeStaticWalkClassification(const NativeStaticWalkMember& planned,uint32_t vtable,uint32_t mode,
    uint32_t hidden,bool live_direct,const NativeSceneSources::Candidate* live_source,NativeStaticWalkAudit& audit) {
  ++audit.classified;
  if(planned.vtable!=vtable || planned.mode!=mode || planned.hidden!=hidden) ++audit.drift;
  const bool direct=planned.vtable==vtable?planned.direct:live_direct;
  if(ClassifyNativeStaticWalk(hidden,mode,direct)!=ClassifyNativeStaticWalk(hidden,mode,live_direct)) ++audit.routes;
  if(live_source && !SameNativeSceneCandidate(planned.source,*live_source)) ++audit.sources;
}
}

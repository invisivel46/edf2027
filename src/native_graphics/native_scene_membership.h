#pragma once
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace edf::native {
// Event-owned ordered membership for audited spatial-node lifetimes. Guest
// addresses identify bridge nodes only; snapshots never borrow guest memory.
class NativeSceneMembership {
 public:
  NativeSceneMembership()=default;
  NativeSceneMembership(const NativeSceneMembership&)=delete;
  NativeSceneMembership& operator=(const NativeSceneMembership&)=delete;
  struct Member {
    uint32_t node=0,owner=0;
    bool operator==(const Member&) const=default;
  };
  struct Snapshot {
    uint64_t generation=0;
    uint32_t end=0;
    std::vector<Member> members;
  };
  struct Publication {
    uint64_t revision=0;
    std::map<uint32_t,std::shared_ptr<const Snapshot>> lists;
  };
  bool Current(const Publication& publication) const { return publication.revision==revision_; }
  std::shared_ptr<const Publication> AcquirePublication() {
    if(!publication_ || !Current(*publication_)) {
      auto next=std::make_shared<Publication>(); next->revision=revision_;
      for(const auto& [address,list]:lists_) next->lists.emplace(address,Acquire(address));
      publication_=std::move(next);
    }
    return publication_;
  }
  void Born(uint32_t list,uint32_t end=0) {
    if(!list || next_generation_==UINT64_MAX) throw std::runtime_error("invalid native membership lifetime");
    Retire(list);
    auto& entry=lists_[list]; entry.generation=next_generation_++; entry.end=end;
    Changed(list,entry);
  }
  bool HasAnchor(uint32_t anchor) const { return lists_.contains(anchor) || nodes_.contains(anchor); }
  bool Retire(uint32_t list) {
    const auto found=lists_.find(list);
    if(found==lists_.end()) return false;
    for(const auto& member:found->second.members) nodes_.erase(member.node);
    lists_.erase(found); ++revision_; return true;
  }
  bool Remove(uint32_t node) {
    if(Retire(node)) return true; // Destruction of the intrusive list header.
    const auto found=nodes_.find(node);
    if(found==nodes_.end()) return false;
    auto& list=lists_.at(found->second.list);
    list.members.erase(found->second.at); Changed(found->second.list,list);
    nodes_.erase(found); return true;
  }
  bool InsertAfter(uint32_t anchor,uint32_t node,uint32_t owner) {
    if(!node || anchor==node) throw std::runtime_error("invalid native membership insertion");
    // Retail insertion first detaches a node from its old list, including a
    // move to an untracked destination. Resolve a member anchor after detach.
    Remove(node);
    uint32_t destination=anchor;
    auto target=lists_.find(anchor);
    std::list<Member>::iterator position;
    if(target!=lists_.end()) position=target->second.members.begin();
    else {
      const auto after=nodes_.find(anchor);
      if(after==nodes_.end()) return false;
      destination=after->second.list; target=lists_.find(destination);
      position=std::next(after->second.at);
    }
    auto& list=target->second;
    const auto inserted=list.members.insert(position,Member{node,owner});
    try { nodes_.emplace(node,Location{destination,inserted}); }
    catch(...) { list.members.erase(inserted); throw; }
    Changed(destination,list); return true;
  }
  std::shared_ptr<const Snapshot> Acquire(uint32_t list) {
    const auto found=lists_.find(list);
    if(found==lists_.end()) return {};
    auto& entry=found->second;
    if(!entry.snapshot) {
      auto snapshot=std::make_shared<Snapshot>();
      snapshot->generation=entry.generation; snapshot->end=entry.end;
      snapshot->members.assign(entry.members.begin(),entry.members.end());
      entry.snapshot=std::move(snapshot);
    }
    return entry.snapshot;
  }
  void Publish() {
    for(const auto address:changed_) {
      const auto found=lists_.find(address);
      if(found!=lists_.end() && found->second.pending) {
        Acquire(address); found->second.pending=false;
      }
    }
    changed_.clear();
  }
  size_t lists() const { return lists_.size(); }
  size_t nodes() const { return nodes_.size(); }
 private:
  struct List {
    uint64_t generation=0; uint32_t end=0;
    std::list<Member> members;
    std::shared_ptr<const Snapshot> snapshot;
    bool pending=false;
  };
  void Changed(uint32_t address,List& entry) {
    ++revision_;
    entry.snapshot.reset();
    if(!entry.pending) { changed_.push_back(address); entry.pending=true; }
  }
  struct Location { uint32_t list; std::list<Member>::iterator at; };
  std::map<uint32_t,List> lists_;
  std::map<uint32_t,Location> nodes_;
  std::vector<uint32_t> changed_;
  uint64_t next_generation_=1;
  uint64_t revision_=0;
  std::shared_ptr<const Publication> publication_;
};
}

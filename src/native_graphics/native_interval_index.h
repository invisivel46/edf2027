#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>

namespace edf::native {
// Overlap index over owner extents whose source of truth is a map elsewhere
// (NativeModelBuffers' buffers, NativeBufferWrites' subscriptions).
//
// Both used to rebuild a fully sorted interval vector after any publication or
// retirement, and to rebuild it on the next query. Scene teardown and loading
// alternate the two once per model buffer (retire, then query the pool
// block's extent; publish, then a file read or copy hits the page), so each
// step paid an O(N log N) rebuild over thousands of owners: the ~0.7 s frame
// at the start of the Mission 1 load, where 4096+ model buffers are released.
//
// Here a mutation does not rebuild. An addition goes to a small unsorted
// pending list; a removal only counts a stale entry, and every entry carries a
// tag (a publication generation or subscription lifetime, unique per owner
// lifetime) that the caller's Valid(owner,tag) checks against the live map, so
// a stale or replaced entry is never visited. A prefix maximum over the sorted
// entries stays a valid upper bound when entries go stale; it only lets a query
// walk further back than necessary. The sorted part is rebuilt once the
// pending list or the stale count outgrows a fraction of it, so the amortized
// cost per mutation is O(log N) and per query O(log N + overlaps + pending).
// The set of owners a query visits is the same as a full rebuild would give;
// only the order differs, and no caller depends on the order.
class NativeIntervalIndex {
 public:
  struct Interval { uint64_t begin,end,prefix_end; uint32_t owner; uint64_t tag; };
  bool built() const { return built_; }
  size_t sorted_size() const { return sorted_.size(); }
  size_t pending_size() const { return pending_.size(); }
  size_t stale() const { return stale_; }
  uint64_t rebuilds() const { return rebuilds_; }
  // Forget everything; the next query rebuilds from the source. Never throws.
  void Reset() noexcept { sorted_.clear(); pending_.clear(); stale_=0; built_=false; }
  // A newly published extent. Before the first build there is nothing to keep
  // current: the build enumerates the source.
  void Add(uint64_t begin,uint64_t end,uint32_t owner,uint64_t tag) noexcept {
    if(!built_ || begin>=end) return;
    try { pending_.push_back({begin,end,end,owner,tag}); }
    catch(const std::bad_alloc&) { Reset(); }
  }
  // An extent retired or replaced: its entry is now stale (filtered by Valid).
  void Remove() noexcept { if(built_) ++stale_; }
  bool NeedsRebuild() const {
    return !built_ || pending_.size()>kPendingSlack+sorted_.size()/8 || stale_>kStaleSlack+sorted_.size()/2;
  }
  // Enumerate(add) calls add(begin,end,owner,tag) for every live extent.
  // Returns false (index left unbuilt) on allocation failure.
  template<class Enumerate> bool Rebuild(Enumerate enumerate) noexcept {
    try {
      sorted_.clear(); pending_.clear(); stale_=0;
      enumerate([&](uint64_t begin,uint64_t end,uint32_t owner,uint64_t tag) {
        if(begin<end) sorted_.push_back({begin,end,0,owner,tag});
      });
      std::sort(sorted_.begin(),sorted_.end(),[](const Interval& a,const Interval& b) { return a.begin<b.begin; });
      uint64_t end=0;
      for(auto& interval:sorted_) { end=(std::max)(end,interval.end); interval.prefix_end=end; }
      built_=true; ++rebuilds_;
      return true;
    } catch(const std::bad_alloc&) { Reset(); return false; }
  }
  template<class Enumerate> bool Refresh(Enumerate enumerate) noexcept {
    return !NeedsRebuild() || Rebuild(std::move(enumerate));
  }
  // Visit(owner,tag) for every entry overlapping [address,address+bytes) that
  // Valid(owner,tag) accepts. Requires built().
  template<class Valid,class Visit> void Query(uint64_t address,uint64_t bytes,Valid valid,Visit visit) const {
    if(!bytes) return;
    const uint64_t end=address+bytes;
    auto at=std::lower_bound(sorted_.begin(),sorted_.end(),end,
      [](const Interval& interval,uint64_t limit) { return interval.begin<limit; });
    while(at!=sorted_.begin()) {
      --at;
      if(at->prefix_end<=address) break;
      if(at->end>address && valid(at->owner,at->tag)) visit(at->owner,at->tag);
    }
    for(const auto& interval:pending_)
      if(interval.begin<end && interval.end>address && valid(interval.owner,interval.tag)) visit(interval.owner,interval.tag);
  }
 private:
  static constexpr size_t kPendingSlack=64,kStaleSlack=64;
  std::vector<Interval> sorted_,pending_;
  size_t stale_=0;
  uint64_t rebuilds_=0;
  bool built_=false;
};
}

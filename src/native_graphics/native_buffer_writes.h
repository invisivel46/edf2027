#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <algorithm>
#include <bitset>
#include <memory>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>
#include <string_view>
#include <span>
#include <exception>
#include <thread>

namespace edf::native {
// Producer callbacks never acquire renderer locks or retain guest pointers.
// Exact intervals normally; valid-range overflow falls back to touched pages.
// Invalid extents or allocation failure still invalidate all tracked buffers.
class NativeBufferWrites {
 public:
  struct Range { uint32_t address,bytes; };
  enum class WriterKind { Unspecified, Bulk, FileRead, WordFill, InlineIndices, AllocationRelease, Count };
  // File names must have static lifetime (compiler source_location strings).
  struct WriterSite {
    const char* file=nullptr; uint32_t line=0;
    uint32_t provider=0,caller=0; // Guest entry and pre-call LR, not a source line.
  };
  struct WriterHit { WriterSite site; uint32_t owner; uint64_t lifetime,calls; Range first_range; };
  struct ObservedVersion {
    uint64_t lifetime,revision;
    uint64_t writer_epoch=0;
    bool operator==(const ObservedVersion&) const = default;
  };
  // Conservative first transaction boundary: any tracked physical bulk copy
  // prevents snapshot attachment. No allocation or renderer lock is needed.
  // This rejects raced snapshots; it does not make reading guest memory safe.
  class WriterScope {
   public:
    explicit WriterScope(NativeBufferWrites* queue,std::optional<Range> range={},WriterKind kind=WriterKind::Unspecified)
      :queue_(queue),exceptions_(std::uncaught_exceptions()),range_(range),kind_(kind) {
      if(!queue_) return;
      thread_=std::this_thread::get_id();
      if(range_ && (!range_->bytes || range_->address>=0x20000000u || range_->bytes>0x20000000u-range_->address))
        range_.reset(); // Malformed extents must never weaken exclusion.
      std::lock_guard lock(queue_->mutex_);
      if(queue_->writer_epoch_==UINT64_MAX || queue_->active_writers_==UINT64_MAX)
        throw std::runtime_error("native writer transaction counter exhausted");
      ++queue_->writer_epoch_; ++queue_->active_writers_;
      next_=queue_->writers_; if(next_) next_->previous_=this;
      queue_->writers_=this;
    }
    WriterScope(const WriterScope&)=delete;
    WriterScope& operator=(const WriterScope&)=delete;
    ~WriterScope() {
      if(queue_) {
        std::lock_guard lock(queue_->mutex_);
        // A partial write may have skipped its completion notification. The
        // failed scope conservatively retires all retained geometry, without
        // allocating or attributing a successful provider write.
        if(std::uncaught_exceptions()>exceptions_) {
          queue_->batch_.all=true;
          ++queue_->batch_.aborted_writers;
          for(auto& [owner,subscription]:queue_->subscriptions_) {
            if(subscription.version.revision==UINT64_MAX) queue_->subscriptions_unknown_=true;
            else ++subscription.version.revision;
          }
          queue_->pending_.store(true,std::memory_order_release);
        }
        --queue_->active_writers_;
        if(previous_) previous_->next_=next_; else queue_->writers_=next_;
        if(next_) next_->previous_=previous_;
      }
    }
   private:
    friend class NativeBufferWrites;
    NativeBufferWrites* queue_;
    int exceptions_;
    std::optional<Range> range_;
    WriterKind kind_;
    std::thread::id thread_;
    WriterScope* previous_=nullptr;
    WriterScope* next_=nullptr;
  };
  static constexpr uint32_t kPageBytes=4096,kPageCount=0x20000000u/kPageBytes;
  using ReleaseScopes=std::vector<std::unique_ptr<WriterScope>>;
  // Scope objects have stable addresses even when the returned owner moves.
  // Prepare all exclusions before retirement; retain through the actual free.
  [[nodiscard]] ReleaseScopes BeginReleaseSet(std::span<const Range> ranges) {
    ReleaseScopes scopes;
    scopes.reserve(ranges.size());
    for(const auto range:ranges)
      scopes.push_back(std::make_unique<WriterScope>(this,range,WriterKind::AllocationRelease));
    return scopes;
  }
  using PageMask=std::bitset<kPageCount>;
  struct Batch {
    std::array<Range,256> ranges{}; size_t count=0; bool all=false;
    std::shared_ptr<const PageMask> pages;
    uint64_t generated_calls=0,provider_calls=0;
    uint64_t aborted_writers=0; // Aborted scopes, including nested unwinds.
    // Original extents, before union/page coalescing. Bounded evidence only.
    std::array<Range,32> generated_samples{};
    size_t generated_sample_count=0;
    uint64_t generated_subscribed_calls=0;
    std::array<Range,32> subscribed_samples{};
    size_t subscribed_sample_count=0;
    bool subscriptions_unknown=false;
    uint64_t generated_exact_owner_hits=0,provider_exact_owner_hits=0;
    std::array<WriterHit,64> writer_hits{};
    size_t writer_hit_count=0;
    uint64_t writer_hits_omitted=0;
  };
  // Registry -> queue lock order only. These subscriptions classify diagnostics;
  // no write is discarded. Allocation failure permanently makes classification
  // conservative for this queue lifetime.
  void Subscribe(uint32_t owner,std::optional<uint32_t> physical,uint32_t bytes) {
    std::lock_guard lock(mutex_);
    if(!physical || !bytes) { RemoveSubscription(owner); return; }
    if(*physical>=0x20000000u || bytes>0x20000000u-*physical)
      throw std::runtime_error("invalid native write subscription");
    try {
      if(!subscription_pages_) subscription_pages_=std::make_unique<std::array<uint32_t,kPageCount>>();
      if(next_lifetime_==UINT64_MAX) { subscriptions_unknown_=true; return; }
      auto [at,inserted]=subscriptions_.try_emplace(owner,Subscription{});
      if(!inserted) ChangePages(at->second.range,false);
      at->second={{*physical,bytes},{++next_lifetime_,0}}; ChangePages(at->second.range,true);
      intervals_dirty_=true;
    } catch(const std::bad_alloc&) { subscriptions_unknown_=true; }
  }
  void Unsubscribe(uint32_t owner) {
    std::lock_guard lock(mutex_); RemoveSubscription(owner);
  }
  // Record revisions plus the tracked bulk-writer epoch. Tokens are withheld
  // during those providers, but are not a source-validation/read-lock substitute.
  std::optional<ObservedVersion> Version(uint32_t owner) {
    std::lock_guard lock(mutex_);
    const auto found=subscriptions_.find(owner);
    if(subscriptions_unknown_ || active_writers_ || found==subscriptions_.end()) return {};
    auto version=found->second.version; version.writer_epoch=writer_epoch_;
    return version;
  }
  struct ObservedSnapshot {
    ObservedVersion version;
    std::shared_ptr<const std::vector<uint8_t>> contents;
    bool unreported_change=false; // A compared observation disproved its own revision baseline.
    bool revision_audited=false; // Exact candidate and unchanged revision baseline existed.
    bool verified=false; // Guest bytes were actually read and compared for this observation.
  };
  // Residual verification schedule for candidates the revision already proves
  // unchanged. The byte comparison is the only oracle for writer coverage, so
  // it is sampled rather than abandoned: the first `verify_initial` observations
  // of each subscription lifetime and every `verify_interval`-th observation
  // afterwards still read guest bytes. `verify_interval` 0 compares every
  // observation - the original behaviour - and so does an enabled revision
  // audit. A single detected unreported change permanently revokes trust for
  // the whole queue, returning every later observation to full comparison.
  // Deliberately initializer-free: a nested aggregate with default member
  // initializers cannot be value-initialized inside the enclosing definition,
  // which this class's own default argument and call sites need. `{}` still
  // zeroes every field, which is the always-compare policy.
  struct SnapshotPolicy {
    uint64_t verify_initial,verify_interval;
    bool audit_revisions;
  };
  struct TrustCounters {
    uint64_t trusted=0,verified=0,unreported_changes=0;
    bool revoked=false;
  };
  TrustCounters Trust() {
    std::lock_guard lock(mutex_);
    return {trusted_,verified_,unreported_changes_,trust_revoked_};
  }
  struct SnapshotSource {
    uint32_t owner,physical;
    std::span<const uint8_t> bytes;
    std::shared_ptr<const std::vector<uint8_t>> candidate;
  };
  enum class SnapshotRejection { None, UnknownTracking, ActiveWriter, MissingOwner, ExtentMismatch };
  struct SnapshotFailure {
    SnapshotRejection reason=SnapshotRejection::None;
    uint32_t owner=0;
    uint64_t active_writers=0;
    uint64_t overlapping_writers=0,unknown_writers=0;
    uint64_t same_thread_writers=0; // Relevant scopes that cannot finish while this caller waits.
    uint64_t releasing_writers=0; // Relevant allocation release scopes, not ordinary payload updates.
    std::array<uint64_t,size_t(WriterKind::Count)> unknown_by_kind{};
  };
  // A draw's VB and IB must be observed at the same tracked-writer boundary.
  // Validate the entire set before reading any source; no partial success or
  // waiting for providers while a caller may hold the renderer registry lock.
  // The caller still owns source lifetime/mapping and raw-writer coverage.
  template<size_t Count>
  std::optional<std::array<ObservedSnapshot,Count>> CopyObservedSet(
      const std::array<SnapshotSource,Count>& sources,SnapshotFailure* failure=nullptr,
      SnapshotPolicy policy={}) {
    std::lock_guard lock(mutex_);
    if(failure) *failure={};
    const auto reject=[&](SnapshotRejection reason,uint32_t owner=0) {
      if(failure) *failure={reason,owner,active_writers_};
    };
    // Classify physical exclusion before subscriptions: retirement may already
    // have removed an owner while its allocation release is still in flight.
    // No source payload is read while identifying those conflicts.
    uint64_t overlapping=0,unknown=0,same_thread=0,releasing=0;
    const auto caller=writers_?std::this_thread::get_id():std::thread::id{};
    for(auto* writer=writers_;writer;writer=writer->next_) {
      if(!writer->range_) {
        ++unknown;
        if(writer->kind_==WriterKind::AllocationRelease) ++releasing;
        if(writer->thread_==caller) ++same_thread;
        if(failure) {
          const auto kind=size_t(writer->kind_);
          ++failure->unknown_by_kind[kind<size_t(WriterKind::Count)?kind:0];
        }
        continue;
      }
      const auto range=*writer->range_;
      for(const auto& source:sources)
        if(uint64_t(range.address)<uint64_t(source.physical)+source.bytes.size() &&
           uint64_t(source.physical)<uint64_t(range.address)+range.bytes) {
          ++overlapping;
          if(writer->kind_==WriterKind::AllocationRelease) ++releasing;
          if(writer->thread_==caller) ++same_thread;
          break;
        }
    }
    if(overlapping || unknown) {
      if(failure) {
        failure->reason=SnapshotRejection::ActiveWriter; failure->active_writers=active_writers_;
        failure->overlapping_writers=overlapping; failure->unknown_writers=unknown;
        failure->same_thread_writers=same_thread;
        failure->releasing_writers=releasing;
      }
      return {};
    }
    if(subscriptions_unknown_) { reject(SnapshotRejection::UnknownTracking); return {}; }
    std::array<ObservedSnapshot,Count> result;
    for(size_t i=0;i<Count;++i) {
      const auto& source=sources[i];
      const auto found=subscriptions_.find(source.owner);
      if(found==subscriptions_.end()) { reject(SnapshotRejection::MissingOwner,source.owner); return {}; }
      if(found->second.range.address!=source.physical || found->second.range.bytes!=source.bytes.size()) {
        reject(SnapshotRejection::ExtentMismatch,source.owner); return {};
      }
      result[i].version=found->second.version;
      result[i].version.writer_epoch=writer_epoch_;
    }
    for(size_t i=0;i<Count;++i) {
      const auto& source=sources[i];
      auto& subscription=subscriptions_.at(source.owner);
      // Only the exact immutable candidate previously observed in this
      // subscription lifetime carries a baseline. Subscribe() resets the
      // subscription, so a reused owner never inherits one, and a dropped
      // snapshot leaves a weak reference that no longer locks.
      const auto previous=subscription.audited_contents.lock();
      const bool proven=source.candidate && previous && previous==source.candidate &&
        subscription.audited_revision==subscription.version.revision;
      const auto observation=subscription.observations++;
      const bool sampled=policy.audit_revisions || !policy.verify_interval ||
        observation<policy.verify_initial || observation%policy.verify_interval==0;
      // Fail closed: anything without a proven baseline, any sampled
      // observation, and everything after a revoked trust reads guest bytes.
      const bool verify=trust_revoked_ || !proven || sampled;
      bool equal=false;
      if(verify) {
        ++verified_;
        equal=source.candidate && source.candidate->size()==source.bytes.size() &&
           std::equal(source.bytes.begin(),source.bytes.end(),source.candidate->begin());
        if(equal)
          result[i].contents=source.candidate;
        else result[i].contents=std::make_shared<const std::vector<uint8_t>>(
          source.bytes.begin(),source.bytes.end());
      } else {
        // Proven unchanged since this candidate was taken: reuse the immutable
        // snapshot without reading the source at all.
        ++trusted_;
        result[i].contents=source.candidate;
      }
      result[i].verified=verify;
      result[i].revision_audited=proven;
      result[i].unreported_change=verify && proven && !equal;
      if(result[i].unreported_change) {
        // A missed producer disproves the coverage the schedule depends on.
        // Revoke for the whole queue, not only this owner: an uncovered writer
        // is evidence about the notification contract, not about one buffer.
        ++unreported_changes_; trust_revoked_=true;
      }
      subscription.audited_contents=result[i].contents;
      subscription.audited_revision=subscription.version.revision;
    }
    return result;
  }
  // Copy only while no overlapping/unknown tracked provider is active, holding its entry mutex
  // throughout the CPU read. Never wait for an active provider while holding
  // the renderer registry lock: it could need that lock before returning.
  // No guest callbacks or GPU operations run here. Untracked stores remain
  // outside this exclusion contract; callers must retain live validation.
  std::optional<ObservedSnapshot> CopyObserved(uint32_t owner,uint32_t physical,
      std::span<const uint8_t> source) {
    auto result=CopyObservedSet(std::array<SnapshotSource,1>{{{owner,physical,source}}});
    if(!result) return {};
    return std::move((*result)[0]);
  }
  // Caller holds registry lock. Commit must not reenter this queue or issue GPU
  // work. This orders attachment against notifications and tracked provider
  // scopes, not arbitrary raw stores. Known disjoint scopes may remain active
  // if they already existed at acquisition; a newer entry still changes epoch.
  struct ObservedOwner { uint32_t owner; ObservedVersion version; };
  template<size_t Count,class Commit> bool CommitObservedSet(
      const std::array<ObservedOwner,Count>& observations,Commit commit) {
    std::lock_guard lock(mutex_);
    if(subscriptions_unknown_) return false;
    for(const auto& observation:observations) {
      const auto found=subscriptions_.find(observation.owner);
      const auto expected=observation.version;
      if(found==subscriptions_.end() || expected.writer_epoch!=writer_epoch_ ||
         found->second.version.lifetime!=expected.lifetime ||
         found->second.version.revision!=expected.revision) return false;
      const auto range=found->second.range;
      for(auto* writer=writers_;writer;writer=writer->next_) {
        if(!writer->range_) return false;
        const auto active=*writer->range_;
        if(uint64_t(active.address)<uint64_t(range.address)+range.bytes &&
           uint64_t(range.address)<uint64_t(active.address)+active.bytes) return false;
      }
    }
    commit(); return true;
  }
  template<class Commit> bool CommitObserved(uint32_t owner,ObservedVersion expected,Commit commit) {
    return CommitObservedSet(std::array<ObservedOwner,1>{{{owner,expected}}},std::move(commit));
  }
  // For a registry-owned synchronous invalidation, retire observation tokens
  // without queuing the same cache retirement a second time. Caller must hold
  // its registry lock and invalidate every overlapping cached owner before
  // releasing it. This orders completed invalidation, not the preceding write.
  void InvalidateObservedRange(uint32_t physical,uint32_t bytes) {
    if(!bytes) return;
    std::lock_guard lock(mutex_);
    if(TouchesSubscription(physical,bytes)) VisitOverlaps(physical,bytes,[&](uint32_t,auto& subscription) {
      if(subscription.version.revision==UINT64_MAX) subscriptions_unknown_=true;
      else ++subscription.version.revision;
    });
  }
  void Record(uint32_t physical,uint32_t bytes,bool generated=false,WriterSite site={nullptr,0,0,0}) {
    if(!bytes) return;
    std::lock_guard lock(mutex_);
    const bool page_hit=TouchesSubscription(physical,bytes);
    const bool valid=physical<0x20000000u && bytes<=0x20000000u-physical;
    auto advance=[&](uint32_t owner,auto& subscription) {
      if(subscription.version.revision==UINT64_MAX) subscriptions_unknown_=true;
      else ++subscription.version.revision;
      if(generated) ++batch_.generated_exact_owner_hits;
      else ++batch_.provider_exact_owner_hits;
      if(valid && ((site.file && site.line) || site.provider)) {
        bool found=false;
        for(size_t i=0;i<batch_.writer_hit_count;++i) {
          auto& hit=batch_.writer_hits[i];
          if(hit.owner==owner && hit.lifetime==subscription.version.lifetime &&
             hit.site.line==site.line && hit.site.provider==site.provider && hit.site.caller==site.caller &&
             std::string_view(hit.site.file?hit.site.file:"")==std::string_view(site.file?site.file:"")) {
            ++hit.calls; found=true; break;
          }
        }
        if(!found) {
          if(batch_.writer_hit_count<batch_.writer_hits.size())
            batch_.writer_hits[batch_.writer_hit_count++]={site,owner,subscription.version.lifetime,1,{physical,bytes}};
          else ++batch_.writer_hits_omitted;
        }
      }
    };
    if(page_hit) VisitOverlaps(physical,bytes,advance);
    if(generated) {
      ++batch_.generated_calls;
      if(batch_.generated_sample_count<batch_.generated_samples.size())
        batch_.generated_samples[batch_.generated_sample_count++]={physical,bytes};
      batch_.subscriptions_unknown|=subscriptions_unknown_;
      if(page_hit) {
        ++batch_.generated_subscribed_calls;
        if(batch_.subscribed_sample_count<batch_.subscribed_samples.size())
          batch_.subscribed_samples[batch_.subscribed_sample_count++]={physical,bytes};
      }
    } else ++batch_.provider_calls;
    if(physical>=0x20000000u || bytes>0x20000000u-physical)
      batch_.all=true;
    else if(!batch_.all && pages_) MarkPages(physical,bytes);
    else if(!batch_.all) {
      // Sorted disjoint intervals represent the exact union of completed writes.
      // Merge before checking capacity: a duplicate in a full queue is not an
      // overflow, and a bridging write may free multiple slots.
      size_t first=0;
      while(first<batch_.count && batch_.ranges[first].address+batch_.ranges[first].bytes<physical) ++first;
      auto end=physical+bytes;
      size_t last=first;
      while(last<batch_.count && batch_.ranges[last].address<=end) {
        physical=(std::min)(physical,batch_.ranges[last].address);
        end=(std::max)(end,batch_.ranges[last].address+batch_.ranges[last].bytes);
        ++last;
      }
      if(first==last && batch_.count==batch_.ranges.size()) {
        try {
          pages_=std::make_shared<PageMask>();
          for(size_t i=0;i<batch_.count;++i) MarkPages(batch_.ranges[i].address,batch_.ranges[i].bytes);
          MarkPages(physical,bytes);
          batch_.pages=pages_;
        } catch(const std::bad_alloc&) { batch_.all=true; }
      }
      else {
        if(first==last)
          std::move_backward(batch_.ranges.begin()+first,batch_.ranges.begin()+batch_.count,batch_.ranges.begin()+batch_.count+1);
        else if(last>first+1)
          std::move(batch_.ranges.begin()+last,batch_.ranges.begin()+batch_.count,batch_.ranges.begin()+first+1);
        batch_.count=batch_.count-(last-first)+1;
        batch_.ranges[first]={physical,end-physical};
      }
    }
    pending_.store(true,std::memory_order_release);
  }
  Batch Drain() {
    if(!pending_.load(std::memory_order_acquire)) return {};
    std::lock_guard lock(mutex_);
    const auto result=batch_; batch_={}; pages_.reset();
    pending_.store(false,std::memory_order_release);
    return result;
  }
  bool Pending() const { return pending_.load(std::memory_order_acquire); }
 private:
  // mutex_ held and subscription-page hit established. Keep token-only and queued invalidations on the same exact
  // interval lookup, including its allocation-failure and invalid-range paths.
  template<class Visitor> void VisitOverlaps(uint32_t physical,uint32_t bytes,Visitor visit) {
    if(!bytes) return;
    const bool valid=physical<0x20000000u && bytes<=0x20000000u-physical;
    if(valid && RebuildIntervals()) {
      auto at=std::lower_bound(subscription_intervals_.begin(),subscription_intervals_.end(),uint64_t(physical)+bytes,
        [](const auto& interval,uint64_t end) { return interval.begin<end; });
      while(at!=subscription_intervals_.begin()) {
        --at;
        if(at->prefix_end<=physical) break;
        if(at->end>physical) visit(at->owner,*at->subscription);
      }
    } else for(auto& [owner,subscription]:subscriptions_) {
      const auto& range=subscription.range;
      if(!valid || (uint64_t(physical)+bytes>range.address && uint64_t(range.address)+range.bytes>physical))
        visit(owner,subscription);
    }
  }
  void ChangePages(Range range,bool add) {
    for(uint32_t page=range.address/kPageBytes;page<=(range.address+range.bytes-1)/kPageBytes;++page)
      if(add) ++(*subscription_pages_)[page]; else --(*subscription_pages_)[page];
  }
  void RemoveSubscription(uint32_t owner) {
    const auto at=subscriptions_.find(owner);
    if(at==subscriptions_.end()) return;
    ChangePages(at->second.range,false); subscriptions_.erase(at);
    intervals_dirty_=true;
  }
  bool TouchesSubscription(uint32_t address,uint32_t bytes) const {
    if(subscriptions_unknown_ || address>=0x20000000u || bytes>0x20000000u-address) return true;
    if(!subscription_pages_) return false;
    for(uint32_t page=address/kPageBytes;page<=(address+bytes-1)/kPageBytes;++page)
      if((*subscription_pages_)[page]) return true;
    return false;
  }
  std::unique_ptr<std::array<uint32_t,kPageCount>> subscription_pages_;
  struct Subscription { Range range; ObservedVersion version;
    std::weak_ptr<const std::vector<uint8_t>> audited_contents;
    uint64_t audited_revision=0;
    uint64_t observations=0; // Snapshot observations in this subscription lifetime.
  };
  struct SubscriptionInterval { uint64_t begin,end,prefix_end; Subscription* subscription; uint32_t owner; };
  bool RebuildIntervals() {
    if(!intervals_dirty_) return true;
    try {
      subscription_intervals_.clear();
      subscription_intervals_.reserve(subscriptions_.size());
      for(auto& [owner,subscription]:subscriptions_) {
        const auto& range=subscription.range;
        subscription_intervals_.push_back({range.address,uint64_t(range.address)+range.bytes,0,&subscription,owner});
      }
      std::sort(subscription_intervals_.begin(),subscription_intervals_.end(),
        [](const auto& a,const auto& b) { return a.begin<b.begin; });
      uint64_t end=0;
      for(auto& interval:subscription_intervals_) {
        end=(std::max)(end,interval.end); interval.prefix_end=end;
      }
      intervals_dirty_=false; return true;
    } catch(const std::bad_alloc&) {
      // Leave dirty: no stale pointer may be queried; Record uses exact scan.
      subscription_intervals_.clear(); return false;
    }
  }
  std::vector<SubscriptionInterval> subscription_intervals_;
  bool intervals_dirty_=true;
  std::map<uint32_t,Subscription> subscriptions_;
  uint64_t next_lifetime_=0;
  uint64_t writer_epoch_=0,active_writers_=0;
  WriterScope* writers_=nullptr; // Intrusive live scopes; all access under mutex_.
  bool subscriptions_unknown_=false;
  uint64_t trusted_=0,verified_=0,unreported_changes_=0;
  bool trust_revoked_=false;
  void MarkPages(uint32_t address,uint32_t bytes) {
    for(uint32_t page=address/kPageBytes;page<=(address+bytes-1)/kPageBytes;++page) pages_->set(page);
  }
  std::shared_ptr<PageMask> pages_;
  std::mutex mutex_;
  std::atomic<bool> pending_{false};
  Batch batch_;
};
}

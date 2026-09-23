#pragma once
#include "native_scene_pass_inputs.h"
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace edf::native {
// What the scene adapter holds for each world owner's group walk order
// (PublishGroupOrder) and pass animation (PublishWorldAnimation), mirrored
// outside the bridge lock. The 820B4250 post-hook publishes both on every
// world update of every simulation step; each publication is a no-op when the
// value is unchanged, which it almost always is, yet it took the bridge mutex
// to find that out. In full-frame mode the render thread holds that mutex in
// long slices, so the step waited out those slices: 0.26 ms per 820B4250 call
// in the intro against 0.02 ms on the guest-helper path, which was the whole
// per-step difference there. With the mirror the hook takes the bridge mutex
// only for a value the adapter does not already hold.
//
// Invariant: an entry here equals the adapter's entry for that owner. Every
// write to the adapter's orders or animations (publish or retire) is followed,
// inside the same bridge-mutex section, by the matching call here; the mirror
// is never written anywhere else. A publication is skipped only when the mirror
// holds the same value; the check linearizes at the mirror lock, so a retire
// racing with it on another thread serializes after the (no-op) publication,
// which is an order the locked path also allowed. A missing entry only costs
// the locked path, never a skipped change. Lock order: bridge mutex, then this
// one; the fast path takes this one alone.
class NativeWorldPublicationMirror {
 public:
  bool OrderCurrent(uint32_t owner,std::span<const uint32_t> order) const {
    std::lock_guard lock(mutex_);
    const auto found=orders_.find(owner);
    return found!=orders_.end() && std::ranges::equal(found->second,order);
  }
  bool AnimationCurrent(uint32_t owner,const NativeScenePassAnimation& value) const {
    std::lock_guard lock(mutex_);
    const auto found=animations_.find(owner);
    return found!=animations_.end() && found->second==value;
  }
  // Under the bridge mutex, after the adapter call they mirror returned.
  void PublishedOrder(uint32_t owner,std::span<const uint32_t> order) {
    std::lock_guard lock(mutex_);
    orders_[owner].assign(order.begin(),order.end());
  }
  void PublishedAnimation(uint32_t owner,const NativeScenePassAnimation& value) {
    std::lock_guard lock(mutex_);
    animations_[owner]=value;
  }
  void RetiredOrder(uint32_t owner) {
    std::lock_guard lock(mutex_);
    orders_.erase(owner);
  }
  void RetiredAnimation(uint32_t owner) {
    std::lock_guard lock(mutex_);
    animations_.erase(owner);
  }
  // The 820B4250 publications. Nothing when the mirror proves the adapter
  // already holds the value; otherwise lock() (the bridge lock, returned as a
  // guard) is taken, publish(value) runs the adapter call and the mirror
  // follows inside the same hold. Returns whether publish ran. A throwing
  // publish leaves the mirror as it was (the caller retires on failure).
  template<class Lock,class Publish>
  bool PublishOrder(uint32_t owner,std::span<const uint32_t> order,const Lock& lock,const Publish& publish) {
    if(OrderCurrent(owner,order)) return false;
    const auto guard=lock();
    publish(order);
    PublishedOrder(owner,order);
    return true;
  }
  template<class Lock,class Publish>
  bool PublishAnimation(uint32_t owner,const NativeScenePassAnimation& value,const Lock& lock,const Publish& publish) {
    if(AnimationCurrent(owner,value)) return false;
    const auto guard=lock();
    publish(value);
    PublishedAnimation(owner,value);
    return true;
  }
  size_t orders() const { std::lock_guard lock(mutex_); return orders_.size(); }
  size_t animations() const { std::lock_guard lock(mutex_); return animations_.size(); }
 private:
  mutable std::mutex mutex_;
  std::unordered_map<uint32_t,std::vector<uint32_t>> orders_;
  std::unordered_map<uint32_t,NativeScenePassAnimation> animations_;
};
}

#pragma once
#include <cstdint>
#include <mutex>
#include <optional>

namespace edf::native {
// The bridge lock for one native visibility walk. Taken lazily at the first
// need and then kept across native work (tree classification, list gathers,
// source lookups, counters), so a walk pays one acquisition plus one per guest
// call. The mutex is not recursive and guest code re-enters hooks that take
// it: every guest call goes through NativeWalkGuestCall, which releases it
// first. A nested walk, or a hook entered from that guest code, installs its
// own scope and restores the outer one when it ends.
template<class Mutex>
class NativeWalkLockScope {
 public:
  explicit NativeWalkLockScope(Mutex& mutex):lock_(mutex,std::defer_lock),previous_(current) { current=this; }
  ~NativeWalkLockScope() { current=previous_; }
  NativeWalkLockScope(const NativeWalkLockScope&)=delete;
  NativeWalkLockScope& operator=(const NativeWalkLockScope&)=delete;
  void Hold() { if(!lock_.owns_lock()) { lock_.lock(); ++acquisitions; } }
  void Release() { if(lock_.owns_lock()) lock_.unlock(); }
  bool held() const { return lock_.owns_lock(); }
  uint64_t acquisitions=0;
  // The innermost walk on this thread; null inside a guest call.
  static inline thread_local NativeWalkLockScope* current=nullptr;
  // Guest calls made on this thread. Anything read from guest state that a
  // callback may change (the camera view) is current until this moves.
  static inline thread_local uint64_t guest_calls=0;
 private:
  std::unique_lock<Mutex> lock_;
  NativeWalkLockScope* previous_;
};
// Scope around one guest call: the walk lock is released and no walk is
// current until the call returns. Native code reacquires lazily afterwards.
template<class Mutex>
class NativeWalkGuestCall {
 public:
  using Scope=NativeWalkLockScope<Mutex>;
  NativeWalkGuestCall():scope_(Scope::current) {
    if(scope_) scope_->Release();
    Scope::current=nullptr; ++Scope::guest_calls;
  }
  ~NativeWalkGuestCall() { Scope::current=scope_; }
  NativeWalkGuestCall(const NativeWalkGuestCall&)=delete;
  NativeWalkGuestCall& operator=(const NativeWalkGuestCall&)=delete;
 private:
  Scope* scope_;
};
// A value read from guest state once and reused until a guest call can have
// changed it.
template<class Value>
class NativeGuestCallCached {
 public:
  template<class Read>
  const Value& Get(uint64_t guest_calls,Read&& read) {
    if(!value_ || epoch_!=guest_calls) { value_=read(); epoch_=guest_calls; ++reads; }
    return *value_;
  }
  uint64_t reads=0;
 private:
  std::optional<Value> value_;
  uint64_t epoch_=0;
};
}

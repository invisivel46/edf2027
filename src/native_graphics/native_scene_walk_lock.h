#pragma once
#include <cstdint>
#include <mutex>
#include <optional>

namespace edf::native {
// The bridge lock for one native visibility walk. Taken lazily at the first
// need and kept across native work within one leaf list (membership, source
// lookups, counters), but never across a whole traversal: the walk releases
// it at every list boundary (EndList) and a gather after kObjectBudget objects
// held (Advance), so submission, worker and host threads wait at most one
// list's native work. The mutex is not recursive and guest code re-enters
// hooks that take it: every guest call goes through NativeWalkGuestCall,
// which releases it first. A nested walk, or a hook entered from that guest
// code, installs its own scope and restores the outer one when it ends.
template<class Mutex>
class NativeWalkLockScope {
 public:
  explicit NativeWalkLockScope(Mutex& mutex):lock_(mutex,std::defer_lock),previous_(current) { current=this; }
  ~NativeWalkLockScope() { current=previous_; }
  NativeWalkLockScope(const NativeWalkLockScope&)=delete;
  NativeWalkLockScope& operator=(const NativeWalkLockScope&)=delete;
  static constexpr uint32_t kObjectBudget=64;
  void Hold() { if(!lock_.owns_lock()) { lock_.lock(); ++acquisitions; held_objects_=0; } }
  void Release() { if(lock_.owns_lock()) { lock_.unlock(); ++releases; } }
  // One candidate object handled; a hold that has covered `budget` of them
  // is released, and the next need reacquires it.
  void Advance(uint32_t budget=kObjectBudget) { if(lock_.owns_lock() && ++held_objects_>=budget) Release(); }
  // A leaf list is done: the hold never spans into the next one.
  void EndList() { Release(); ++lists; }
  bool held() const { return lock_.owns_lock(); }
  uint64_t acquisitions=0,releases=0,lists=0;
  // The innermost walk on this thread; null inside a guest call.
  static inline thread_local NativeWalkLockScope* current=nullptr;
  // Guest calls made on this thread. Anything read from guest state that a
  // callback may change (the camera view) is current until this moves.
  static inline thread_local uint64_t guest_calls=0;
 private:
  std::unique_lock<Mutex> lock_;
  NativeWalkLockScope* previous_;
  uint32_t held_objects_=0;
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
// One object's bucket dispatch. The object is classified first: a native
// insert (sort modes 1/2) touches guest memory only, so it runs under the
// walk's hold and is not a guest call. Only a path that reaches the original
// routine runs inside NativeWalkGuestCall. `native` returns true when it
// inserted; `guest` runs the original dispatch. True when guest code ran.
template<class Mutex,class Native,class Guest>
bool NativeWalkBucketDispatch(bool try_native,Native&& native,Guest&& guest) {
  if(try_native && native()) return false;
  NativeWalkGuestCall<Mutex> call; guest();
  return true;
}
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

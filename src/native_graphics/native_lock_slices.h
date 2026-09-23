#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <utility>

namespace edf::native {
// Short holds ("slices") of one fixed lock set, for work that runs mostly off
// it. The full frame's passes plan off the bridge locks over immutable
// generations and take the bridge locks only around what they truly share
// with other threads (the model pass caches, the backend's pipeline and
// sampler caches, the adapter's material intern table, the scene recorder),
// one slice per use, so a simulation step waits at most one slice, never a
// whole pass.
//
// Each call locks every mutex in declaration order (the bridge's order:
// submissions, then mutex), runs the function, and unlocks in reverse order,
// also when the function throws. A lock that throws unlocks the ones already
// taken. The statistics (slices taken, total and longest hold) are for the
// passes' reports; the object is used by one thread at a time.
//
// What a slice must not do: keep a pointer, reference or iterator into state
// the locks protect past its end. Whatever outlives a slice is an immutable
// generation (shared_ptr to const) or owned by the caller; anything read in
// one slice that the next slice depends on is re-validated there.
template<class... Mutexes>
class NativeLockSlices {
 public:
  static_assert(sizeof...(Mutexes)>0,"a slice holds at least one lock");
  using Clock=std::chrono::steady_clock;
  explicit NativeLockSlices(Mutexes&... mutexes):mutexes_(mutexes...) {}
  NativeLockSlices(const NativeLockSlices&)=delete;
  NativeLockSlices& operator=(const NativeLockSlices&)=delete;
  // Runs function() under every lock and returns its result.
  template<class Function>
  decltype(auto) operator()(Function&& function) {
    const Hold hold(*this);
    return std::forward<Function>(function)();
  }
  uint64_t slices() const { return slices_; }
  Clock::duration held() const { return held_; }
  Clock::duration longest() const { return longest_; }
 private:
  static constexpr size_t kCount=sizeof...(Mutexes);
  template<size_t Index>
  void Lock() {
    if constexpr(Index<kCount) {
      std::get<Index>(mutexes_).lock();
      try { Lock<Index+1>(); }
      catch(...) { std::get<Index>(mutexes_).unlock(); throw; }
    }
  }
  template<size_t Count>
  void Unlock() {
    if constexpr(Count>0) { std::get<Count-1>(mutexes_).unlock(); Unlock<Count-1>(); }
  }
  class Hold {
   public:
    explicit Hold(NativeLockSlices& owner):owner_(owner) { owner_.template Lock<0>(); start_=Clock::now(); }
    ~Hold() {
      const auto held=Clock::now()-start_;
      ++owner_.slices_; owner_.held_+=held;
      if(held>owner_.longest_) owner_.longest_=held;
      owner_.template Unlock<kCount>();
    }
    Hold(const Hold&)=delete;
    Hold& operator=(const Hold&)=delete;
   private:
    NativeLockSlices& owner_;
    Clock::time_point start_;
  };
  std::tuple<Mutexes&...> mutexes_;
  uint64_t slices_=0;
  Clock::duration held_{},longest_{};
};
template<class... Mutexes>
NativeLockSlices(Mutexes&...)->NativeLockSlices<Mutexes...>;
// A hold duration in milliseconds, for reports.
inline double NativeLockSliceMs(std::chrono::steady_clock::duration duration) {
  return std::chrono::duration<double,std::milli>(duration).count();
}
}

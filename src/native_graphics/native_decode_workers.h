#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace edf::native {
// Workers that decode guest parameter data ahead of the draw that needs it.
//
// Why this shape and not a fan-out. The draw path has no batch to spread: a
// material activation carries about two and a half parameter uploads, and each
// draw is its own call from the game. Dispatching 0.63 microseconds of work to
// a thread pool costs more than doing it, which is measurable and was measured
// before this was written.
//
// What it can do instead is run ahead. The hook copies the guest bytes it must
// read now - the game may overwrite them the moment it returns - and queues the
// conversion. Draws that follow keep submitting while the conversion happens
// beside them, and a draw that reaches one still in flight waits for just that
// one. The win is the overlap, not the division.
//
// A job is identified by a ticket. Tickets are handed out in order and complete
// in any order.
class NativeDecodeWorkers {
 public:
  // Zero workers means everything runs inline on the calling thread, which is
  // the honest way to express "threading off" - the same code path, no special
  // case to get wrong.
  explicit NativeDecodeWorkers(uint32_t workers);
  ~NativeDecodeWorkers();
  NativeDecodeWorkers(const NativeDecodeWorkers&)=delete;
  NativeDecodeWorkers& operator=(const NativeDecodeWorkers&)=delete;

  using Job=std::function<void()>;
  // Runs the job on a worker, or inline when there are none. The returned
  // ticket is what a later Wait refers to.
  uint64_t Submit(Job job);
  // Blocks until that job has finished. Waiting on an already-finished or
  // never-issued ticket returns at once.
  void Wait(uint64_t ticket);
  // Blocks until everything submitted so far has finished.
  void Drain();

  uint32_t workers() const { return static_cast<uint32_t>(threads_.size()); }
  uint64_t submitted() const { return submitted_.load(std::memory_order_relaxed); }
  // How often a draw reached a job that had not finished. High means the
  // workers are not keeping ahead and the overlap is not being won.
  uint64_t waits() const { return waits_.load(std::memory_order_relaxed); }
  uint64_t inline_jobs() const { return inline_jobs_.load(std::memory_order_relaxed); }

 private:
  void Run();

  struct Entry { uint64_t ticket=0; Job job; };
  std::vector<std::thread> threads_;
  std::mutex mutex_;
  std::condition_variable wake_,finished_;
  std::vector<Entry> queue_;
  // Everything below this ticket is finished. Tickets complete out of order,
  // so in-flight ones are tracked separately rather than assumed contiguous.
  std::vector<uint64_t> running_;
  uint64_t next_ticket_=1,retired_=0;
  std::atomic<uint64_t> submitted_{0},waits_{0},inline_jobs_{0};
  bool stop_=false;
};
// The fan-out the header comment says draws do not have, for a caller that
// does have a batch: body(begin,end) over [0,count) in contiguous slices of
// at least min_slice, the calling thread taking the first and one worker each
// of the others (at most workers()+1 slices, at most 17), returning when every
// slice has finished. Every index is visited exactly once. body must not
// throw on a worker (a worker cannot carry an exception back); if the caller's
// own slice throws, the submitted slices, which borrow body, are still joined
// before the exception leaves.
template<class Body>
void RunNativeSlices(NativeDecodeWorkers& workers,size_t count,size_t min_slice,const Body& body) {
  static constexpr size_t kMaxLanes=17;
  const size_t lanes=(std::min)({size_t(workers.workers())+1,kMaxLanes,
    (std::max)(size_t(1),count/(std::max)(size_t(1),min_slice))});
  if(lanes<=1) { body(size_t(0),count); return; }
  const size_t step=(count+lanes-1)/lanes;
  struct Join {
    NativeDecodeWorkers& workers;
    uint64_t tickets[kMaxLanes]{};
    ~Join() { for(const auto ticket:tickets) workers.Wait(ticket); }
  } join{workers};
  for(size_t lane=1;lane<lanes;++lane) {
    const size_t begin=(std::min)(count,lane*step),end=(std::min)(count,begin+step);
    if(begin<end) join.tickets[lane]=workers.Submit([&body,begin,end] { body(begin,end); });
  }
  body(size_t(0),(std::min)(count,step));
}
}  // namespace edf::native

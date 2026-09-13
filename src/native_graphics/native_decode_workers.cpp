#include "native_decode_workers.h"
#include <algorithm>

namespace edf::native {
NativeDecodeWorkers::NativeDecodeWorkers(uint32_t workers) {
  threads_.reserve(workers);
  for(uint32_t index=0;index<workers;++index) threads_.emplace_back([this] { Run(); });
}

NativeDecodeWorkers::~NativeDecodeWorkers() {
  {
    std::lock_guard lock(mutex_);
    stop_=true;
  }
  wake_.notify_all();
  for(auto& thread:threads_) thread.join();
}

uint64_t NativeDecodeWorkers::Submit(Job job) {
  if(threads_.empty()) {
    // No workers: run it here. Same path, no branch for the caller to get
    // wrong, and the ticket it gets back is already complete.
    inline_jobs_.fetch_add(1,std::memory_order_relaxed);
    submitted_.fetch_add(1,std::memory_order_relaxed);
    job();
    std::lock_guard lock(mutex_);
    return retired_=next_ticket_++;
  }
  uint64_t ticket=0;
  {
    std::lock_guard lock(mutex_);
    ticket=next_ticket_++;
    queue_.push_back({ticket,std::move(job)});
  }
  submitted_.fetch_add(1,std::memory_order_relaxed);
  wake_.notify_one();
  return ticket;
}

void NativeDecodeWorkers::Wait(uint64_t ticket) {
  if(!ticket || threads_.empty()) return;
  std::unique_lock lock(mutex_);
  const auto pending=[&] {
    if(ticket<=retired_) return false;
    // Still queued, or running on a worker.
    if(std::find(running_.begin(),running_.end(),ticket)!=running_.end()) return true;
    return std::any_of(queue_.begin(),queue_.end(),
                       [ticket](const Entry& entry) { return entry.ticket==ticket; });
  };
  if(!pending()) return;
  waits_.fetch_add(1,std::memory_order_relaxed);
  finished_.wait(lock,[&] { return !pending(); });
}

void NativeDecodeWorkers::Drain() {
  if(threads_.empty()) return;
  std::unique_lock lock(mutex_);
  finished_.wait(lock,[this] { return queue_.empty() && running_.empty(); });
}

void NativeDecodeWorkers::Run() {
  for(;;) {
    Entry entry;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock,[this] { return stop_ || !queue_.empty(); });
      if(stop_ && queue_.empty()) return;
      // Oldest first: a draw waiting on a ticket is almost always waiting on
      // the oldest one, so taking from the back would make it wait longer.
      entry=std::move(queue_.front());
      queue_.erase(queue_.begin());
      running_.push_back(entry.ticket);
    }
    // Run outside the lock, which is the entire point.
    entry.job();
    {
      std::lock_guard lock(mutex_);
      running_.erase(std::remove(running_.begin(),running_.end(),entry.ticket),running_.end());
      // Retired only advances past tickets nothing is still working on, so a
      // waiter cannot be told its job is done because a later one finished.
      uint64_t lowest=next_ticket_;
      for(const auto ticket:running_) lowest=(std::min)(lowest,ticket);
      for(const auto& queued:queue_) lowest=(std::min)(lowest,queued.ticket);
      retired_=lowest-1;
    }
    finished_.notify_all();
  }
}
}  // namespace edf::native

#include "native_graphics/native_lock_slices.h"
#include <atomic>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
// Records lock/unlock order; optionally throws from lock().
struct TraceMutex {
  std::vector<std::string>* trace=nullptr;
  std::string name;
  bool fail=false;
  int held=0;
  void lock() {
    if(fail) throw std::runtime_error("lock failed");
    ++held; trace->push_back("lock "+name);
  }
  void unlock() { --held; trace->push_back("unlock "+name); }
};
}

int main() {
  using edf::native::NativeLockSlices;
  int failures=0;
  auto check=[&](bool ok,const char* what) { if(!ok) { ++failures; std::cerr<<"FAILED: "<<what<<"\n"; } };

  // Locks in declaration order, unlocks in reverse; returns the value.
  {
    std::vector<std::string> trace;
    TraceMutex first{&trace,"submissions"},second{&trace,"mutex"};
    NativeLockSlices slices(first,second);
    const auto value=slices([&] {
      check(first.held==1 && second.held==1,"both held inside the slice");
      trace.push_back("work");
      return 42;
    });
    check(value==42,"slice returns the function's value");
    check(first.held==0 && second.held==0,"both released after the slice");
    check(trace==std::vector<std::string>{"lock submissions","lock mutex","work","unlock mutex","unlock submissions"},
      "lock order is declaration order, unlock order is reverse");
    check(slices.slices()==1,"one slice counted");
    check(slices.longest()<=slices.held(),"longest slice within the total");
  }
  // Void functions and references pass through.
  {
    std::vector<std::string> trace;
    TraceMutex only{&trace,"mutex"};
    NativeLockSlices slices(only);
    int counter=0;
    slices([&] { ++counter; });
    int& ref=slices([&]()->int& { return counter; });
    check(&ref==&counter && counter==1,"void and reference results");
    check(slices.slices()==2,"two slices counted");
  }
  // A throwing function releases every lock and propagates.
  {
    std::vector<std::string> trace;
    TraceMutex first{&trace,"a"},second{&trace,"b"};
    NativeLockSlices slices(first,second);
    bool thrown=false;
    try { slices([]()->int { throw std::runtime_error("work failed"); }); }
    catch(const std::runtime_error&) { thrown=true; }
    check(thrown,"the function's exception propagates");
    check(first.held==0 && second.held==0,"locks released after a throwing function");
    check(slices.slices()==1,"a throwing slice is still counted");
  }
  // A lock that throws unlocks the ones already taken; the function never runs.
  {
    std::vector<std::string> trace;
    TraceMutex first{&trace,"a"},second{&trace,"b",true};
    NativeLockSlices slices(first,second);
    bool ran=false,thrown=false;
    try { slices([&] { ran=true; }); }
    catch(const std::runtime_error&) { thrown=true; }
    check(thrown && !ran,"a failed lock throws before the function");
    check(first.held==0,"the first lock is released when the second fails");
    check(trace==std::vector<std::string>{"lock a","unlock a"},"only the taken lock is released");
    check(slices.slices()==0,"a failed acquisition is not a slice");
  }
  // Real mutexes: another thread is excluded during a slice and gets the lock
  // between slices (the property the simulation relies on).
  {
    std::recursive_mutex submissions;
    std::mutex mutex;
    NativeLockSlices slices(submissions,mutex);
    std::atomic<int> excluded_inside=0,acquired_between=0;
    slices([&] {
      std::thread other([&] {
        if(!mutex.try_lock()) ++excluded_inside; else mutex.unlock();
        if(!submissions.try_lock()) ++excluded_inside; else submissions.unlock();
      });
      other.join();
    });
    std::thread between([&] {
      if(mutex.try_lock()) { ++acquired_between; mutex.unlock(); }
      if(submissions.try_lock()) { ++acquired_between; submissions.unlock(); }
    });
    between.join();
    check(excluded_inside==2,"both locks exclude another thread inside a slice");
    check(acquired_between==2,"both locks are free between slices");
    // The recursive gate may already be held by the caller (as the render
    // thread's frame may): a slice nests inside it.
    std::lock_guard outer(submissions);
    const bool nested=slices([&] { return true; });
    check(nested,"a slice nests inside a held recursive gate");
  }
  // Many short slices from two threads keep a shared counter consistent.
  {
    std::mutex mutex;
    uint64_t counter=0;
    const auto work=[&] {
      NativeLockSlices slices(mutex);
      for(int i=0;i<10000;++i) slices([&] { ++counter; });
      return slices.slices();
    };
    uint64_t first=0;
    std::thread other([&] { first=work(); });
    const auto second=work();
    other.join();
    check(first==10000 && second==10000,"each thread counts its own slices");
    check(counter==20000,"slices exclude each other");
  }
  if(failures) { std::cerr<<failures<<" failure(s)\n"; return 1; }
  std::cout<<"native lock slices tests passed\n";
  return 0;
}

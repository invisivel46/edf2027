#include "native_graphics/d3d12_backend.h"
#include "native_graphics/native_frame_flight.h"
#include "native_graphics/native_backend_frame_queue.h"
#include <future>
#include "native_graphics/d3d11_completion.h"
#include "native_graphics/d3d11_signals.h"
#include "native_graphics/d3d11_gpu_timer.h"
#include <iostream>
#include <thread>
using namespace edf::native;
void Require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
template<class F> void Reject(F operation) {
  bool rejected=false; try { operation(); } catch(const std::exception&) { rejected=true; }
  Require(rejected,"invalid completion operation accepted");
}
template<class F> void Wait(F done) {
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(!done()) {
    Require(std::chrono::steady_clock::now()<deadline,"backend completion timed out");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
int main() {
  try {
    {
      struct Completion final:NativeBackendCompletion {
        bool done=false;
        bool Complete() const override { return done; }
      };
      Reject([]{NativeFrameFlight invalid(0);});
      NativeFrameFlight serial(1),overlapped(2);
      auto first=std::make_shared<Completion>();
      auto second=std::make_shared<Completion>();
      serial.Submit(first);
      Require(!serial.Ready(),"single-frame mode admitted unfinished GPU work");
      overlapped.Submit(first);
      Require(overlapped.Ready(),"two-frame mode did not overlap CPU preparation");
      overlapped.Submit(second);
      Require(!overlapped.Ready(),"frame lead exceeded its bound");
      Reject([&]{overlapped.Submit(std::make_shared<Completion>());});
      second->done=true;
      Require(!overlapped.Ready(),"later completion bypassed the oldest frame");
      first->done=true;
      Require(serial.Ready() && overlapped.Ready() && overlapped.pending()==0,
        "completed frame credits were not retired");
      Reject([&]{overlapped.Submit(nullptr);});
    }
    {
      struct Completion final:NativeBackendCompletion {
        bool done=false;
        bool Complete() const override { return done; }
      };
      const auto frame=[](uint64_t sequence) {
        return NativeBackendPublishedFrame{reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),
          sequence,sequence,sequence,16,16,28,{}};
      };
      NativeBackendFrameQueue queue;
      auto owner=std::make_shared<int>(1);
      std::weak_ptr<int> retained=owner;
      const auto first=queue.Reserve(std::chrono::steady_clock::now());
      queue.Publish(first,frame(1),owner); owner.reset();
      auto copying=std::make_shared<Completion>();
      uint64_t seen=0;
      const auto copy=[&](const NativeBackendPublishedFrame& image) {
        Require(image.sequence==seen+1,"presentation skipped an ordered frame");
        seen=image.sequence;
        return NativeBackendFrameCopied{nullptr,0,copying};
      };
      Require(!queue.Visit(0,copy),"presentation did not prefill its jitter cushion");
      auto second=queue.Reserve(std::chrono::steady_clock::now());
      queue.Publish(second,frame(2),std::make_shared<int>(2));
      Require(queue.Visit(0,copy) && seen==1,"presentation did not choose oldest frame");
      auto third=queue.Reserve(std::chrono::steady_clock::now());
      queue.Publish(third,frame(3),std::make_shared<int>(3));
      Reject([&]{queue.Reserve(std::chrono::steady_clock::now());});
      Require(!retained.expired(),"surface retired while host copy was incomplete");
      copying->done=true;
      Require(queue.Reserve(std::chrono::steady_clock::now())==first && retained.expired(),
        "completed copy did not release exactly its slot/owner");
      queue.Publish(first,frame(4),std::make_shared<int>(4));
      Require(queue.Visit(seen,copy) && queue.Visit(seen,copy) && queue.Visit(seen,copy) && seen==4,
        "presentation reordered buffered frames");
      Require(!queue.Visit(seen,copy),"presentation reused a consumed frame");
      queue.SetActive(false);
      for(uint64_t sequence=5;sequence<=20;++sequence) {
        auto slot=queue.Reserve(std::chrono::steady_clock::now());
        queue.Publish(slot,frame(sequence),std::make_shared<int>(0));
      }
      Require(!queue.Visit(seen,copy),"restored presentation did not refill cushion");
      auto slot=queue.Reserve(std::chrono::steady_clock::now());
      queue.Publish(slot,frame(21),std::make_shared<int>(0));
      seen=19;
      Require(queue.Visit(seen,copy) && seen==20,"background queue did not retain its latest frame");

      NativeBackendFrameQueue bounded;
      for(uint64_t sequence=1;sequence<=3;++sequence) {
        auto free=bounded.Reserve(std::chrono::steady_clock::now());
        bounded.Publish(free,frame(sequence),std::make_shared<int>(0));
      }
      auto waiter=std::async(std::launch::async,[&] {
        return bounded.Reserve(std::chrono::steady_clock::now()+std::chrono::seconds(2));
      });
      Require(waiter.wait_for(std::chrono::milliseconds(10))==std::future_status::timeout,
        "full visible queue overwrote a pending image");
      Require(bounded.Visit(0,[&](const auto&) {return NativeBackendFrameCopied{nullptr,0,copying};}),
        "independent presenter could not consume while producer waited");
      (void)waiter.get();
      bounded.SetActive(false);

      NativeBackendFrameQueue mirrored;
      for(uint64_t sequence=1;sequence<=3;++sequence) {
        auto free=mirrored.Reserve(std::chrono::steady_clock::now());
        mirrored.Publish(free,frame(sequence),std::make_shared<int>(0));
      }
      auto mirror_copy=std::make_shared<Completion>();
      Require(mirrored.VisitMirror(0,[&](const auto& image) {
        Require(image.sequence==3,"mirror did not select newest image");
        return NativeBackendFrameCopied{nullptr,0,mirror_copy};
      }),"mirror did not acquire image");
      seen=0;
      Require(mirrored.Visit(seen,copy) && mirrored.Visit(seen,copy) && mirrored.Visit(seen,copy) && seen==3,
        "mirror consumed or reordered primary presentation");
      (void)mirrored.Reserve(std::chrono::steady_clock::now());
      (void)mirrored.Reserve(std::chrono::steady_clock::now());
      Reject([&]{mirrored.Reserve(std::chrono::steady_clock::now());});
      mirror_copy->done=true;
      (void)mirrored.Reserve(std::chrono::steady_clock::now());

      NativeBackendFrameQueue inactive_mirror;
      inactive_mirror.SetActive(false);
      auto pinned=std::make_shared<int>(0);
      std::weak_ptr<int> mirror_owner=pinned;
      auto free=inactive_mirror.Reserve(std::chrono::steady_clock::now());
      inactive_mirror.Publish(free,frame(1),pinned); pinned.reset();
      mirror_copy=std::make_shared<Completion>();
      Require(inactive_mirror.VisitMirror(0,[&](const auto&) {
        return NativeBackendFrameCopied{nullptr,0,mirror_copy};
      }),"inactive mirror did not acquire image");
      for(uint64_t sequence=2;sequence<=8;++sequence) {
        auto next=inactive_mirror.Reserve(std::chrono::steady_clock::now());
        inactive_mirror.Publish(next,frame(sequence),std::make_shared<int>(0));
      }
      Require(!mirror_owner.expired(),"background publication retired an active mirror read");
      mirror_copy->done=true;
      for(uint64_t sequence=9;sequence<=11;++sequence) {
        auto next=inactive_mirror.Reserve(std::chrono::steady_clock::now());
        inactive_mirror.Publish(next,frame(sequence),std::make_shared<int>(0));
      }
      Require(mirror_owner.expired(),"completed mirror retained its surface indefinitely");

      NativeBackendFrameQueue failed;
      for(uint64_t sequence=1;sequence<=2;++sequence) {
        auto free=failed.Reserve(std::chrono::steady_clock::now());
        failed.Publish(free,frame(sequence),std::make_shared<int>(0));
      }
      Reject([&]{failed.Visit(0,[](const auto&) -> NativeBackendFrameCopied {
        throw std::runtime_error("injected failure after possible copy submission");
      });});
      failed.SetActive(false);
      Reject([&]{failed.Reserve(std::chrono::steady_clock::now());});
    }
    NativeD3D12Options options{}; options.prefer_warp=true; options.debug_layer=true;
    auto backend=CreateNativeD3D12Backend(options);
    NativeBackendTextureDesc desc{}; desc.width=desc.height=32; desc.format=28; desc.render_target=true;
    auto target=backend->CreateRenderTarget(desc);
    NativeCompletionQueue fences(*backend,3);
    fences.Capture(0x100,8,2,0x200); fences.Capture(0x108,8,4,0x208);
    Require(!fences.Poll() && fences.unsubmitted()==2,"capture completed before submission");
    Reject([&]{ fences.SubmitRange(0x100,4); });
    backend->BeginFrame(); backend->Recorder().ClearColor(*target,{1,0,0,1});
    Reject([&]{ fences.SubmitRange(0x100,16); });
    Require(fences.unsubmitted()==2,"failed submission armed a guest fence");
    backend->Submit();
    Require(fences.SubmitRange(0x108,8)==1,"later fence submission failed");
    Require(!fences.Poll(),"later completion bypassed an unsubmitted predecessor");
    Require(fences.SubmitRange(0x100,8)==1,"first fence submission failed");
    Wait([&]{ return fences.Poll()==4; });
    Require(fences.completed_cursor()==0x208 && !fences.pending(),"fence cursor ordering lost");
    const auto pixels=backend->ReadRenderTarget(*target);
    Require(pixels[0]==255 && pixels[1]==0,"completion did not cover submitted drawing");
    NativeSignalQueue signals(*backend,3);
    const NativeSignal first{0x1234,1,4},second{0x1234,2,4};
    signals.Capture(0x300,8,second); signals.Capture(0x200,8,first);
    Require(signals.Poll().empty(),"captured signal was delivered early");
    Reject([&]{ signals.SubmitRange(0x200,4); });
    Require(signals.SubmitRange(0x200,0x108)==2,"signal range submission failed");
    Wait([&]{ return !signals.PeekCompleted(1).empty(); });
    Require(signals.PeekCompleted(1).front()==first,"signal address order lost");
    Require(signals.PeekCompleted(1).front()==first,"peek acknowledged a signal");
    signals.AcknowledgeCompleted(1);
    Wait([&]{ return !signals.PeekCompleted(1).empty(); });
    Require(signals.Poll(1).front()==second && !signals.pending(),"signal acknowledgement lost work");
    {
      NativeGpuTimer timer(*backend,[&]() -> NativeBackendRecorder& {return backend->Recorder();});
      backend->BeginFrame(); timer.Begin(17);
      backend->Recorder().ClearColor(*target,{0,1,0,1});
      backend->Submit();
      Require(!timer.Poll(),"unfinished timestamp span produced a result");
      backend->BeginFrame(); timer.MarkMiddle();
      backend->Recorder().ClearColor(*target,{0,0,1,1}); timer.End();
      Require(!timer.Poll(),"unsubmitted timestamps produced a result");
      backend->Submit();
      std::optional<NativeGpuTiming> timing;
      Wait([&]{timing=timer.Poll(); return timing.has_value();});
      Require(timing->tag==17 && timing->reliable && timing->frequency && timing->middle &&
              timing->Milliseconds().has_value(),"D3D12 GPU timestamp span is invalid");
      backend->BeginFrame(); timer.Begin(18); timer.Cancel(); backend->Submit();
      Require(!timer.active() && !timer.Poll(),"cancelled timer span was published");
    }
    Require(backend->DrainValidationMessages().empty(),"completion/timer GPU validation failed");
    auto marker=backend->MarkCompletion(); Wait([&]{return marker->Complete();});
    target.reset(); backend.reset();
    Require(marker->Complete(),"completion marker lost fence ownership");
    std::cout<<"Backend completion and signal tests passed\n";
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}

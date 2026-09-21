#include "d3d11_signals.h"
#include <algorithm>
#include <stdexcept>

namespace edf::native {
namespace {
uint64_t RangeEnd(uint32_t begin,uint32_t bytes) {
  const uint64_t end=uint64_t(begin)+bytes;
  if(!bytes || (begin&3) || (bytes&3) || end>(uint64_t(1)<<32))
    throw std::invalid_argument("invalid native signal command range");
  return end;
}
}
NativeSignalQueue::NativeSignalQueue(ID3D11Device& device,ID3D11DeviceContext& context,size_t capacity)
    :device_(&device),context_(&context),capacity_(capacity) {
  Microsoft::WRL::ComPtr<ID3D11Device> owner;
  context.GetDevice(&owner);
  if(!capacity || capacity>4096 || owner.Get()!=&device || context.GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)
    throw std::invalid_argument("invalid native signal context/capacity");
  completed_.reserve(capacity_); // Completion transfer cannot lose work to allocation failure.
}
NativeSignalQueue::NativeSignalQueue(NativeRenderBackend& backend,size_t capacity)
    :backend_(&backend),capacity_(capacity) {
  if(!capacity || capacity>4096) throw std::invalid_argument("invalid backend signal capacity");
  completed_.reserve(capacity_);
}
void NativeSignalQueue::Capture(uint32_t begin,uint32_t bytes,NativeSignal signal) {
  const auto end=RangeEnd(begin,bytes);
  if(!signal.callback || !signal.cpu_mask || (signal.cpu_mask&~63u))
    throw std::invalid_argument("invalid native signal callback/CPU mask");
  if(count_>=capacity_) throw std::runtime_error("native signal queue capacity exceeded");
  for(const auto& entry:captured_)
    if(begin<uint64_t(entry.begin)+entry.bytes && entry.begin<end)
      throw std::runtime_error("unsubmitted native signal command storage overwritten");
  captured_.push_back({begin,bytes,signal});
  ++count_;
}
size_t NativeSignalQueue::SubmitRange(uint32_t begin,uint32_t bytes) {
  const auto end=RangeEnd(begin,bytes);
  std::vector<Captured> selected;
  for(const auto& entry:captured_) {
    const auto entry_end=uint64_t(entry.begin)+entry.bytes;
    if(begin>=entry_end || entry.begin>=end) continue;
    if(entry.begin<begin || entry_end>end)
      throw std::runtime_error("native submission splits a captured signal");
    selected.push_back(entry);
  }
  if(selected.empty()) return 0;
  std::stable_sort(selected.begin(),selected.end(),[](const auto& a,const auto& b){return a.begin<b.begin;});
  Submitted batch;
  batch.signals.reserve(selected.size());
  for(const auto& entry:selected) batch.signals.push_back(entry.signal);
  if(backend_) {
    submitted_.push_back(std::move(batch));
    try { submitted_.back().completion=backend_->MarkCompletion(); }
    catch(...) { submitted_.pop_back(); throw; }
  } else {
  const D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT,0};
  if(FAILED(device_->CreateQuery(&desc,&batch.query)))
    throw std::runtime_error("native signal query creation failed");
  // Complete all potentially throwing allocations before emitting the event.
  submitted_.push_back(std::move(batch));
  context_->End(submitted_.back().query.Get());
  context_->Flush();
  }
  std::erase_if(captured_,[&](const auto& entry){return entry.begin>=begin && uint64_t(entry.begin)+entry.bytes<=end;});
  return selected.size();
}
std::vector<NativeSignal> NativeSignalQueue::Poll(size_t limit,bool allow_flush) {
  auto ready=PeekCompleted(limit,allow_flush);
  AcknowledgeCompleted(ready.size());
  return ready;
}
std::vector<NativeSignal> NativeSignalQueue::PeekCompleted(size_t limit,bool allow_flush) {
  while(!submitted_.empty() && completed_.size()<limit) {
    auto& batch=submitted_.front();
    if(backend_) {
      try { if(!batch.completion->Complete()) break; }
      catch(...) { if(!completed_.empty()) break; throw; }
    } else {
    BOOL done=FALSE;
    const auto result=context_->GetData(batch.query.Get(),&done,sizeof(done),allow_flush?0:D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if(FAILED(result)) {
      if(!completed_.empty()) break; // Retain earlier completions; report failure after delivery.
      throw std::runtime_error("native signal completion query failed");
    }
    if(result!=S_OK || !done) break;
    }
    const auto take=(std::min)(batch.signals.size(),limit-completed_.size());
    completed_.insert(completed_.end(),batch.signals.begin(),batch.signals.begin()+take);
    if(take==batch.signals.size()) submitted_.pop_front();
    else batch.signals.erase(batch.signals.begin(),batch.signals.begin()+take);
  }
  return {completed_.begin(),completed_.begin()+(std::min)(limit,completed_.size())};
}
void NativeSignalQueue::AcknowledgeCompleted(size_t count) {
  if(count>completed_.size()) throw std::runtime_error("native signal acknowledgement exceeds completed work");
  completed_.erase(completed_.begin(),completed_.begin()+count);
  count_-=count;
}
}

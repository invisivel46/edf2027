#include "d3d11_completion.h"
#include <stdexcept>
#include <thread>
#include <algorithm>
#include <vector>

namespace edf::native {
NativeCompletionQueue::NativeCompletionQueue(ID3D11Device& device,ID3D11DeviceContext& context,size_t capacity)
    : device_(&device),context_(&context),capacity_(capacity) {
  Microsoft::WRL::ComPtr<ID3D11Device> owner;
  context.GetDevice(&owner);
  if(!capacity || capacity>4096 || owner.Get()!=&device || context.GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)
    throw std::runtime_error("invalid native completion queue context/capacity");
}
NativeCompletionQueue::NativeCompletionQueue(NativeRenderBackend& backend,size_t capacity)
    : backend_(&backend),capacity_(capacity) {
  if(!capacity || capacity>4096) throw std::invalid_argument("invalid backend completion capacity");
}
void NativeCompletionQueue::Submit(uint32_t value,uint32_t cursor) {
  if(issued_ && uint32_t(value-*issued_)!=2)
    throw std::runtime_error("native completion fence sequence changed without reset");
  if(entries_.size()>=capacity_) throw std::runtime_error("native completion queue capacity exceeded");
  Entry entry{value,cursor,{}};
  if(backend_) {
    entries_.push_back(std::move(entry));
    try { entries_.back().completion=backend_->MarkCompletion(); }
    catch(...) { entries_.pop_back(); throw; }
    submitted_=issued_=value;
    return;
  }
  const D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT,0};
  if(FAILED(device_->CreateQuery(&desc,&entry.query)))
    throw std::runtime_error("native completion query creation failed");
  // Allocate queue storage before emitting work, so allocation failure cannot
  // leave an untracked event in the native submission stream.
  entries_.push_back(std::move(entry));
  context_->End(entries_.back().query.Get());
  context_->Flush();
  submitted_=value;
  issued_=value;
}
namespace {
uint64_t CompletionRangeEnd(uint32_t begin,uint32_t bytes) {
  const uint64_t end=uint64_t(begin)+bytes;
  if(!bytes || (begin&3) || (bytes&3) || end>(uint64_t(1)<<32))
    throw std::invalid_argument("invalid native fence command range");
  return end;
}
}
void NativeCompletionQueue::Capture(uint32_t begin,uint32_t bytes,uint32_t value,uint32_t cursor) {
  const auto end=CompletionRangeEnd(begin,bytes);
  if(issued_ && uint32_t(value-*issued_)!=2)
    throw std::runtime_error("native captured fence sequence changed without reset");
  if(entries_.size()>=capacity_) throw std::runtime_error("native completion queue capacity exceeded");
  for(const auto& entry:entries_)
    if(!entry.armed() && begin<uint64_t(entry.begin)+entry.bytes && entry.begin<end)
      throw std::runtime_error("unsubmitted native fence storage overwritten");
  entries_.push_back({value,cursor,{},begin,bytes});
  issued_=value;
}
size_t NativeCompletionQueue::unsubmitted() const {
  return std::count_if(entries_.begin(),entries_.end(),[](const auto& entry){return !entry.armed();});
}
size_t NativeCompletionQueue::SubmitRange(uint32_t begin,uint32_t bytes) {
  const auto end=CompletionRangeEnd(begin,bytes);
  struct Armed { Entry* entry; Microsoft::WRL::ComPtr<ID3D11Query> query; };
  std::vector<Armed> selected;
  for(auto& entry:entries_) {
    if(entry.armed() || begin>=uint64_t(entry.begin)+entry.bytes || entry.begin>=end) continue;
    if(entry.begin<begin || uint64_t(entry.begin)+entry.bytes>end)
      throw std::runtime_error("native submission splits a captured fence");
    selected.push_back({&entry,{}});
  }
  if(backend_) {
    if(!selected.empty()) {
      const auto completion=backend_->MarkCompletion();
      for(auto& item:selected) item.entry->completion=completion;
    }
    for(const auto& entry:entries_) {
      if(!entry.armed()) break;
      submitted_=entry.value;
    }
    return selected.size();
  }
  const D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT,0};
  for(auto& item:selected)
    if(FAILED(device_->CreateQuery(&desc,&item.query)))
      throw std::runtime_error("native captured fence query creation failed");
  // No completion or storage release until actual descriptor consumption.
  for(auto& item:selected) {
    item.entry->query=std::move(item.query);
    context_->End(item.entry->query.Get());
  }
  if(!selected.empty()) context_->Flush();
  // Submission knowledge may advance only through a contiguous issued prefix.
  for(const auto& entry:entries_) {
    if(!entry.armed()) break;
    submitted_=entry.value;
  }
  return selected.size();
}
std::optional<uint32_t> NativeCompletionQueue::Poll(bool allow_flush) {
  while(!entries_.empty()) {
    if(!entries_.front().armed()) break;
    if(backend_) {
      if(!entries_.front().completion->Complete()) break;
      completed_=entries_.front().value;
      completed_cursor_=entries_.front().cursor;
      entries_.pop_front();
      continue;
    }
    BOOL finished=FALSE;
    const auto result=context_->GetData(entries_.front().query.Get(),&finished,sizeof(finished),
                                        allow_flush?0:D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if(FAILED(result)) throw std::runtime_error("native completion query failed");
    if(result!=S_OK || !finished) break;
    completed_=entries_.front().value;
    completed_cursor_=entries_.front().cursor;
    entries_.pop_front();
  }
  return completed_;
}
NativeWaitResult NativeCompletionQueue::WaitUntil(uint32_t issued,uint32_t target,
    std::chrono::steady_clock::time_point deadline,std::stop_token stop) {
  for(;;) {
    if(target && !stop.stop_requested()) Poll(true);
    const auto now=std::chrono::steady_clock::now();
    const auto result=EvaluateNativeWait(issued,target,submitted_,completed_,stop.stop_requested(),now>=deadline);
    if(result!=NativeWaitResult::Pending) return result;
    // Only already-submitted events can reach this point. Release the CPU
    // between polls; no packet processing or guest counter writes are needed.
    std::this_thread::sleep_until((std::min)(deadline,now+std::chrono::milliseconds(1)));
  }
}
}  // namespace edf::native

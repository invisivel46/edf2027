#include "d3d11_gpu_timer.h"
#include <stdexcept>

namespace edf::native {
void NativePresentProfiler::Start(uint32_t sequence) {
  if(started_ || (expected_ && *expected_!=sequence))
    throw std::logic_error("native present profiling start out of sequence");
  if(timer_.active()) timer_.MarkMiddle();
  else timer_.Begin(sequence);
  expected_=sequence; started_=true;
}
void NativePresentProfiler::Finish(uint32_t sequence) {
  if(!started_ || !expected_ || *expected_!=sequence)
    throw std::logic_error("native present profiling end out of sequence");
  timer_.End();
  started_=false; expected_=sequence+2;
  timer_.Begin(*expected_);
}
NativeGpuTimer::NativeGpuTimer(ID3D11Device& device,ID3D11DeviceContext& context,size_t capacity)
    :device_(&device),context_(&context),capacity_(capacity) {
  Microsoft::WRL::ComPtr<ID3D11Device> owner;
  context.GetDevice(&owner);
  if(!capacity || capacity>4096 || owner.Get()!=&device ||
     context.GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)
    throw std::invalid_argument("invalid native GPU timer context/capacity");
}
NativeGpuTimer::NativeGpuTimer(NativeRenderBackend& backend,std::function<NativeBackendRecorder&()> recorder,size_t capacity)
    :backend_(&backend),recorder_(std::move(recorder)),capacity_(capacity) {
  if(!capacity || capacity>4096 || !recorder_ || !backend.TimestampFrequency())
    throw std::invalid_argument("invalid backend GPU timer");
}
NativeGpuTimer::~NativeGpuTimer() { Cancel(); }
void NativeGpuTimer::Begin(uint64_t tag) {
  if(active_) throw std::logic_error("nested native GPU timing span");
  if(submitted_.size()>=capacity_) throw std::runtime_error("native GPU timing capacity exceeded");
  Span span; span.tag=tag;
  if(backend_) {
    span.backend_begin=backend_->CreateQuery(NativeBackendQueryKind::Timestamp);
    span.backend_end=backend_->CreateQuery(NativeBackendQueryKind::Timestamp);
    active_.emplace(std::move(span));
    recorder_().EndQuery(*active_->backend_begin);
    return;
  }
  const D3D11_QUERY_DESC disjoint{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
  const D3D11_QUERY_DESC timestamp{D3D11_QUERY_TIMESTAMP,0};
  if(FAILED(device_->CreateQuery(&disjoint,&span.disjoint)) ||
     FAILED(device_->CreateQuery(&timestamp,&span.begin)) ||
     FAILED(device_->CreateQuery(&timestamp,&span.end)))
    throw std::runtime_error("native GPU timer query creation failed");
  active_.emplace(std::move(span));
  context_->Begin(active_->disjoint.Get());
  context_->End(active_->begin.Get());
}
void NativeGpuTimer::MarkMiddle() {
  if(!active_ || active_->middle || active_->backend_middle) throw std::logic_error("native GPU middle marker requires an unmarked active span");
  if(backend_) {
    active_->backend_middle=backend_->CreateQuery(NativeBackendQueryKind::Timestamp);
    recorder_().EndQuery(*active_->backend_middle);
    return;
  }
  const D3D11_QUERY_DESC timestamp{D3D11_QUERY_TIMESTAMP,0};
  Microsoft::WRL::ComPtr<ID3D11Query> query;
  if(FAILED(device_->CreateQuery(&timestamp,&query)))
    throw std::runtime_error("native GPU middle query creation failed");
  active_->middle=std::move(query);
  context_->End(active_->middle.Get());
}
void NativeGpuTimer::End() {
  if(!active_) throw std::logic_error("native GPU timing end without begin");
  // Allocate queue storage before emitting the closing commands. Allocation
  // failure leaves the active span owned and cancellable.
  submitted_.push_back(std::move(*active_));
  active_.reset();
  if(backend_) { recorder_().EndQuery(*submitted_.back().backend_end); return; }
  context_->End(submitted_.back().end.Get());
  context_->End(submitted_.back().disjoint.Get());
  context_->Flush();
}
void NativeGpuTimer::Cancel() {
  if(!active_) return;
  if(backend_) { active_.reset(); return; }
  context_->End(active_->end.Get());
  context_->End(active_->disjoint.Get());
  active_.reset();
}
std::optional<NativeGpuTiming> NativeGpuTimer::Poll(bool allow_flush) {
  if(submitted_.empty()) return std::nullopt;
  const auto& span=submitted_.front();
  if(backend_) {
    NativeGpuTiming result; result.tag=span.tag; result.frequency=backend_->TimestampFrequency();
    const auto read=[&](NativeBackendQuery& query,uint64_t& value) {
      return backend_->ReadQuery(query,{reinterpret_cast<uint8_t*>(&value),sizeof(value)});
    };
    if(!read(*span.backend_begin,result.begin) || !read(*span.backend_end,result.end)) return std::nullopt;
    result.reliable=result.frequency && result.end>=result.begin;
    if(span.backend_middle) {
      uint64_t middle=0; if(!read(*span.backend_middle,middle)) return std::nullopt;
      result.middle=middle; result.reliable=result.reliable && middle>=result.begin && middle<=result.end;
    }
    submitted_.pop_front(); return result;
  }
  const UINT flags=allow_flush?0:D3D11_ASYNC_GETDATA_DONOTFLUSH;
  D3D11_QUERY_DATA_TIMESTAMP_DISJOINT domain{};
  const auto ready=context_->GetData(span.disjoint.Get(),&domain,sizeof(domain),flags);
  if(FAILED(ready)) throw std::runtime_error("native GPU timer domain query failed");
  if(ready!=S_OK) return std::nullopt;
  NativeGpuTiming result; result.tag=span.tag; result.frequency=domain.Frequency;
  if(!domain.Disjoint && domain.Frequency) {
    const auto begin=context_->GetData(span.begin.Get(),&result.begin,sizeof(result.begin),flags);
    const auto end=context_->GetData(span.end.Get(),&result.end,sizeof(result.end),flags);
    if(FAILED(begin) || FAILED(end)) throw std::runtime_error("native GPU timestamp query failed");
    if(begin!=S_OK || end!=S_OK) return std::nullopt;
    result.reliable=result.end>=result.begin;
    if(span.middle) {
      uint64_t value=0;
      const auto middle=context_->GetData(span.middle.Get(),&value,sizeof(value),flags);
      if(FAILED(middle)) throw std::runtime_error("native GPU middle timestamp query failed");
      if(middle!=S_OK) return std::nullopt;
      result.middle=value;
      result.reliable=result.reliable && value>=result.begin && value<=result.end;
    }
  }
  // Invalid/disjoint samples retire explicitly, never masquerade as zero GPU
  // work or stay at the head of the queue preventing later valid measurements.
  submitted_.pop_front();
  return result;
}
}

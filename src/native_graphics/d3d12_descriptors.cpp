#include "d3d12_descriptors.h"
#include <cstdio>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace edf::native {
namespace {
void Require(HRESULT result, const char* what) {
  if(SUCCEEDED(result)) return;
  char code[16];
  std::snprintf(code,sizeof(code),"0x%08lx",static_cast<unsigned long>(result));
  throw std::runtime_error(std::string("D3D12 ")+what+" failed: "+code);
}
}  // namespace

NativeD3D12DescriptorRing::NativeD3D12DescriptorRing(ID3D12Device& device,
                                                     D3D12_DESCRIPTOR_HEAP_TYPE type,
                                                     uint32_t descriptors)
    : ring_(descriptors),capacity_(descriptors) {
  const D3D12_DESCRIPTOR_HEAP_DESC desc{type,descriptors,
                                        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
  Require(device.CreateDescriptorHeap(&desc,IID_PPV_ARGS(&heap_)),"descriptor heap creation");
  increment_=device.GetDescriptorHandleIncrementSize(type);
  cpu_start_=heap_->GetCPUDescriptorHandleForHeapStart();
  gpu_start_=heap_->GetGPUDescriptorHandleForHeapStart();
}

NativeD3D12DescriptorRing::NativeD3D12DescriptorRing(ID3D12Device& device,
                                                     ID3D12DescriptorHeap& heap,
                                                     D3D12_DESCRIPTOR_HEAP_TYPE type,
                                                     uint32_t first, uint32_t count)
    : heap_(&heap),ring_(count),capacity_(count),first_(first) {
  increment_=device.GetDescriptorHandleIncrementSize(type);
  cpu_start_=heap.GetCPUDescriptorHandleForHeapStart();
  gpu_start_=heap.GetGPUDescriptorHandleForHeapStart();
  cpu_start_.ptr+=static_cast<SIZE_T>(first)*increment_;
  gpu_start_.ptr+=static_cast<UINT64>(first)*increment_;
}

NativeD3D12DescriptorRing::Result NativeD3D12DescriptorRing::TryAllocate(uint32_t count) {
  // Alignment of 1: descriptors have no alignment rule beyond being adjacent,
  // and the ring's straddle handling already guarantees the run is contiguous.
  const auto allocation=ring_.Allocate(count,1);
  if(allocation.status!=NativeUploadRing::Status::Ok) return {allocation.status,{}};
  const uint64_t index=allocation.offset%capacity_;
  Table table{{cpu_start_.ptr+index*increment_},{gpu_start_.ptr+index*increment_}};
  return {NativeUploadRing::Status::Ok,table};
}

NativeD3D12CpuDescriptorHeap::NativeD3D12CpuDescriptorHeap(ID3D12Device& device,
                                                           D3D12_DESCRIPTOR_HEAP_TYPE type,
                                                           uint32_t descriptors)
    : capacity_(descriptors) {
  const D3D12_DESCRIPTOR_HEAP_DESC desc{type,descriptors,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};
  Require(device.CreateDescriptorHeap(&desc,IID_PPV_ARGS(&heap_)),"CPU descriptor heap creation");
  increment_=device.GetDescriptorHandleIncrementSize(type);
  start_=heap_->GetCPUDescriptorHandleForHeapStart();
}

D3D12_CPU_DESCRIPTOR_HANDLE NativeD3D12CpuDescriptorHeap::Allocate() {
  uint32_t index=0;
  if(!free_.empty()) { index=free_.back(); free_.pop_back(); }
  else if(next_<capacity_) index=next_++;
  else throw std::runtime_error("CPU descriptor heap exhausted at "+std::to_string(capacity_)+
                                " descriptors");
  ++live_;
  return {start_.ptr+static_cast<SIZE_T>(index)*increment_};
}

void NativeD3D12CpuDescriptorHeap::Free(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  if(!handle.ptr||handle.ptr<start_.ptr) return;
  const auto index=static_cast<uint32_t>((handle.ptr-start_.ptr)/increment_);
  if(index>=capacity_) return;
  free_.push_back(index);
  --live_;
}

NativeD3D12SamplerCache::NativeD3D12SamplerCache(ID3D12Device& device, uint32_t slots_per_table,
                                                 uint32_t max_tables)
    : device_(&device),slots_per_table_(slots_per_table),max_tables_(max_tables) {
  if(!slots_per_table||!max_tables) throw std::runtime_error("sampler cache needs a non-zero shape");
  const uint32_t descriptors=slots_per_table*max_tables;
  if(descriptors>D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE)
    throw std::runtime_error("sampler cache of "+std::to_string(descriptors)+
                             " descriptors exceeds the shader-visible limit of "+
                             std::to_string(D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE));
  const D3D12_DESCRIPTOR_HEAP_DESC desc{D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,descriptors,
                                        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
  Require(device.CreateDescriptorHeap(&desc,IID_PPV_ARGS(&heap_)),"sampler heap creation");
  increment_=device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  cpu_start_=heap_->GetCPUDescriptorHandleForHeapStart();
  gpu_start_=heap_->GetGPUDescriptorHandleForHeapStart();
}

D3D12_GPU_DESCRIPTOR_HANDLE NativeD3D12SamplerCache::Table(std::span<const D3D12_SAMPLER_DESC> samplers) {
  if(samplers.size()>slots_per_table_)
    throw std::runtime_error("a shader asked for "+std::to_string(samplers.size())+
                             " samplers but the root signature has "+std::to_string(slots_per_table_));
  std::string key(reinterpret_cast<const char*>(samplers.data()),
                  samplers.size()*sizeof(D3D12_SAMPLER_DESC));
  if(const auto found=tables_.find(key);found!=tables_.end()) { ++hits_; return found->second; }
  if(tables_.size()>=max_tables_)
    throw std::runtime_error("sampler cache is full at "+std::to_string(max_tables_)+
                             " distinct combinations; the shader-visible sampler heap holds at most "+
                             std::to_string(D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE)+" descriptors");
  const uint32_t index=static_cast<uint32_t>(tables_.size())*slots_per_table_;
  for(size_t slot=0;slot<samplers.size();++slot) {
    const D3D12_CPU_DESCRIPTOR_HANDLE at{cpu_start_.ptr+(index+slot)*increment_};
    device_->CreateSampler(&samplers[slot],at);
  }
  // Slots the shader does not use still have to hold something: a descriptor
  // table is bound as a whole, and the debug layer objects to a table whose
  // unused entries were never written even when the shader never reads them.
  for(size_t slot=samplers.size();slot<slots_per_table_;++slot) {
    const D3D12_CPU_DESCRIPTOR_HANDLE at{cpu_start_.ptr+(index+slot)*increment_};
    const D3D12_SAMPLER_DESC fallback{D3D12_FILTER_MIN_MAG_MIP_POINT,
                                      D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                      D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                      D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                      0.0f,1,D3D12_COMPARISON_FUNC_ALWAYS,{0,0,0,0},0.0f,0.0f};
    device_->CreateSampler(&fallback,at);
  }
  const D3D12_GPU_DESCRIPTOR_HANDLE table{gpu_start_.ptr+index*increment_};
  tables_.emplace(std::move(key),table);
  ++misses_;
  return table;
}
}  // namespace edf::native

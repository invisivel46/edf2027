#include "d3d12_buffer_pool.h"
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace edf::native {
namespace {
// Private-data slot the pool's allocation record lives in on each placed buffer.
// {6e1a3c52-9d1b-4f0e-8a3b-5127c04e9f12}
constexpr GUID kAllocationSlot={0x6e1a3c52,0x9d1b,0x4f0e,{0x8a,0x3b,0x51,0x27,0xc0,0x4e,0x9f,0x12}};
uint64_t AlignUp(uint64_t value,uint64_t alignment) { return (value+alignment-1)/alignment*alignment; }
}  // namespace

struct NativeD3D12BufferPool::State {
  struct Heap {
    ComPtr<ID3D12Heap> heap;
    uint64_t size=0,used=0;
    bool reserved=false;
    std::map<uint64_t,uint64_t> free;  // offset -> length, coalesced.
  };
  std::mutex mutex;
  ComPtr<ID3D12Device> device;
  uint64_t heap_bytes=0,max_buffer_bytes=0;
  std::map<uint64_t,Heap> heaps;  // By creation serial, so first fit prefers the oldest.
  uint64_t next_heap=0;
  Statistics stats;

  // Called with the mutex held.
  Heap* AddHeap(uint64_t size,bool reserved) {
    D3D12_HEAP_DESC desc{};
    desc.SizeInBytes=size;
    desc.Properties={D3D12_HEAP_TYPE_DEFAULT,D3D12_CPU_PAGE_PROPERTY_UNKNOWN,D3D12_MEMORY_POOL_UNKNOWN,0,0};
    desc.Alignment=kAlignment;
    // Not zeroed: every buffer placed here is written whole before it is read.
    // Runtimes that predate the flag refuse it, and then zeroed memory is the
    // same thing at a small one-off cost.
    desc.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS|D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
    ComPtr<ID3D12Heap> heap;
    if(FAILED(device->CreateHeap(&desc,IID_PPV_ARGS(&heap)))) {
      desc.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
      if(FAILED(device->CreateHeap(&desc,IID_PPV_ARGS(&heap)))) return nullptr;
    }
    auto& added=heaps[next_heap++];
    added.heap=std::move(heap);
    added.size=size;
    added.reserved=reserved;
    added.free.emplace(0,size);
    ++stats.heaps_created;
    ++stats.heaps; stats.heap_bytes+=size;
    return &added;
  }

  void Free(uint64_t serial,uint64_t offset,uint64_t length) {
    std::lock_guard lock(mutex);
    const auto found=heaps.find(serial);
    if(found==heaps.end()) return;
    auto& heap=found->second;
    auto inserted=heap.free.emplace(offset,length).first;
    if(const auto next=std::next(inserted);next!=heap.free.end() && inserted->first+inserted->second==next->first) {
      inserted->second+=next->second;
      heap.free.erase(next);
    }
    if(inserted!=heap.free.begin()) {
      const auto previous=std::prev(inserted);
      if(previous->first+previous->second==inserted->first) {
        previous->second+=inserted->second;
        heap.free.erase(inserted);
      }
    }
    heap.used-=length;
    --stats.live; stats.live_bytes-=length;
    if(!heap.used && !heap.reserved) {
      // Nothing placed in it is alive any more, so nothing can reference it.
      stats.heap_bytes-=heap.size; --stats.heaps; ++stats.heaps_released;
      heaps.erase(found);
    }
  }
};

namespace {
// The pool's record of one placed buffer. Held only as the buffer's private
// data, so its last reference goes when D3D12 destroys the buffer, and that is
// when the range becomes free.
class Allocation final : public IUnknown {
 public:
  Allocation(std::shared_ptr<NativeD3D12BufferPool::State> state,uint64_t heap,uint64_t offset,uint64_t length)
      : state_(std::move(state)),heap_(heap),offset_(offset),length_(length) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void** out) override {
    if(!out) return E_POINTER;
    if(riid==__uuidof(IUnknown)) { *out=static_cast<IUnknown*>(this); AddRef(); return S_OK; }
    *out=nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG remaining=--references_;
    if(!remaining) delete this;
    return remaining;
  }
 private:
  ~Allocation() { state_->Free(heap_,offset_,length_); }
  std::shared_ptr<NativeD3D12BufferPool::State> state_;
  uint64_t heap_,offset_,length_;
  std::atomic<ULONG> references_{1};
};
}  // namespace

NativeD3D12BufferPool::NativeD3D12BufferPool(ID3D12Device& device,uint64_t heap_bytes,uint64_t max_buffer_bytes,
                                             uint32_t reserve_heaps)
    : state_(std::make_shared<State>()) {
  if(!heap_bytes || !max_buffer_bytes) throw std::runtime_error("a buffer pool needs a heap and a buffer size");
  state_->device=&device;
  state_->heap_bytes=AlignUp(heap_bytes,kAlignment);
  state_->max_buffer_bytes=(std::min)(max_buffer_bytes,state_->heap_bytes);
  std::lock_guard lock(state_->mutex);
  for(uint32_t index=0;index<reserve_heaps;++index) state_->AddHeap(state_->heap_bytes,true);
}

NativeD3D12BufferPool::~NativeD3D12BufferPool()=default;

ComPtr<ID3D12Resource> NativeD3D12BufferPool::Create(uint64_t bytes) {
  auto& state=*state_;
  if(!bytes || bytes>state.max_buffer_bytes) {
    std::lock_guard lock(state.mutex);
    ++state.stats.refused;
    return nullptr;
  }
  const uint64_t length=AlignUp(bytes,kAlignment);
  uint64_t serial=0,offset=0;
  ComPtr<ID3D12Heap> heap;
  {
    std::lock_guard lock(state.mutex);
    for(auto& [candidate,held]:state.heaps) {
      for(auto range=held.free.begin();range!=held.free.end();++range) {
        if(range->second<length) continue;
        serial=candidate; offset=range->first; heap=held.heap;
        const auto rest=range->second-length;
        held.free.erase(range);
        if(rest) held.free.emplace(offset+length,rest);
        held.used+=length;
        break;
      }
      if(heap) break;
    }
    if(!heap) {
      auto* added=state.AddHeap(state.heap_bytes,false);
      if(!added) { ++state.stats.refused; return nullptr; }
      serial=state.next_heap-1; offset=0; heap=added->heap;
      added->free.clear();
      if(added->size>length) added->free.emplace(length,added->size-length);
      added->used=length;
    }
    ++state.stats.live; state.stats.live_bytes+=length;
  }
  // From here the range is the allocation's: releasing it returns the range.
  ComPtr<IUnknown> allocation;
  allocation.Attach(new Allocation(state_,serial,offset,length));
  const D3D12_RESOURCE_DESC desc{D3D12_RESOURCE_DIMENSION_BUFFER,0,bytes,1,1,1,DXGI_FORMAT_UNKNOWN,{1,0},
                                 D3D12_TEXTURE_LAYOUT_ROW_MAJOR,D3D12_RESOURCE_FLAG_NONE};
  ComPtr<ID3D12Resource> resource;
  if(FAILED(state.device->CreatePlacedResource(heap.Get(),offset,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,
                                               IID_PPV_ARGS(&resource))) ||
     FAILED(resource->SetPrivateDataInterface(kAllocationSlot,allocation.Get()))) {
    std::lock_guard lock(state.mutex);
    ++state.stats.refused;
    return nullptr;  // The allocation's release returns the range.
  }
  std::lock_guard lock(state.mutex);
  ++state.stats.placed; state.stats.placed_bytes+=bytes;
  return resource;
}

NativeD3D12BufferPool::Statistics NativeD3D12BufferPool::statistics() const {
  std::lock_guard lock(state_->mutex);
  return state_->stats;
}
}  // namespace edf::native

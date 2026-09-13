#include "d3d12_device.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace edf::native {
namespace {
void Require(HRESULT result, const char* what) {
  if(SUCCEEDED(result)) return;
  char code[16];
  std::snprintf(code,sizeof(code),"0x%08lx",static_cast<unsigned long>(result));
  throw std::runtime_error(std::string("D3D12 ")+what+" failed: "+code);
}
// The debug layer is a process-wide switch that can only be thrown before the
// first D3D12 device exists: turning it on afterwards removes every device
// already created. That is not hypothetical here - the window's presenter
// builds its device first, and the scene backend used to turn the layer on when
// it built its own, which removed the presenter's mid-frame and read as a
// present failure with no cause.
//
// So the first device to be built decides for the process, and a later
// disagreement is reported rather than acted on.
std::mutex& DebugLayerMutex() {
  static std::mutex mutex;
  return mutex;
}
bool debug_layer_decided=false;
bool debug_layer_on=false;
// Empty when the request was honoured; otherwise why it could not be.
std::string DecideDebugLayer(bool wanted) {
  std::lock_guard lock(DebugLayerMutex());
  if(!debug_layer_decided) {
    debug_layer_decided=true;
    if(!wanted) return {};
    ComPtr<ID3D12Debug> debug;
    if(FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
      return "the D3D12 debug layer was asked for but is not installed on this machine";
    debug->EnableDebugLayer();
    debug_layer_on=true;
    // GPU-based validation as well, because the ordinary debug layer checks
    // the API calls and this checks what the shaders actually did with them -
    // a descriptor read that points nowhere, an index out of range. Those are
    // the ones that present as a GPU hang with nothing in the log, which the
    // plain layer is silent about. It is very slow, and it is the only thing
    // that finds this class of fault.
    ComPtr<ID3D12Debug1> gpu_validation;
    if(SUCCEEDED(debug.As(&gpu_validation)))
      gpu_validation->SetEnableGPUBasedValidation(TRUE);
    return {};
  }
  if(wanted==debug_layer_on) return {};
  return wanted
    ? "the D3D12 debug layer was asked for after a device already existed; turning "
      "it on now would remove that device, so this one runs without it"
    : "the D3D12 debug layer is already on for this process, so this device has it too";
}
std::string Narrow(const wchar_t* wide) {
  if(!wide) return {};
  const int bytes=WideCharToMultiByte(CP_UTF8,0,wide,-1,nullptr,0,nullptr,nullptr);
  if(bytes<=1) return {};
  std::string narrow(static_cast<size_t>(bytes-1),'\0');
  WideCharToMultiByte(CP_UTF8,0,wide,-1,narrow.data(),bytes,nullptr,nullptr);
  return narrow;
}
}  // namespace

NativeD3D12Device::NativeD3D12Device(const NativeD3D12Options& options) {
  if(!options.frames_in_flight)
    throw std::runtime_error("D3D12 needs at least one frame in flight");
  if(!options.recorders) throw std::runtime_error("D3D12 needs at least one recorder");

  // Whether this device gets the layer is the process's answer, not this
  // call's; see DecideDebugLayer. A refusal is a note to hand back, not a
  // failure to create a device.
  if(auto note=DecideDebugLayer(options.debug_layer); !note.empty())
    notes_.push_back(std::move(note));
  debug_layer_active_=debug_layer_on;

  UINT factory_flags=0;
  if(debug_layer_active_) factory_flags|=DXGI_CREATE_FACTORY_DEBUG;
  Require(CreateDXGIFactory2(factory_flags,IID_PPV_ARGS(&factory_)),"factory creation");

  ComPtr<IDXGIAdapter1> adapter;
  if(options.prefer_warp) {
    Require(factory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"WARP adapter enumeration");
    Require(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device_)),
            "WARP device creation");
    is_warp_=true;
  } else {
    // Take the first adapter that actually creates a device rather than the
    // first that enumerates: a machine can list an adapter whose driver cannot
    // serve D3D12, and silently landing on WARP would look like a catastrophic
    // performance regression with no explanation.
    for(UINT index=0;factory_->EnumAdapters1(index,adapter.ReleaseAndGetAddressOf())!=DXGI_ERROR_NOT_FOUND;++index) {
      DXGI_ADAPTER_DESC1 description{};
      if(FAILED(adapter->GetDesc1(&description))) continue;
      if(description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) continue;
      if(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device_)))) break;
      device_.Reset();
    }
    if(!device_) throw std::runtime_error("no D3D12 adapter could create a feature level 11_0 device");
  }
  if(adapter) {
    DXGI_ADAPTER_DESC1 description{};
    if(SUCCEEDED(adapter->GetDesc1(&description))) adapter_name_=Narrow(description.Description);
  }

  // Only errors and corruption. Warnings on WARP are noisy about things that
  // are not defects, and a check nobody trusts is a check nobody reads.
  if(debug_layer_active_ && SUCCEEDED(device_.As(&messages_))) {
    D3D12_MESSAGE_SEVERITY severities[]={D3D12_MESSAGE_SEVERITY_CORRUPTION,D3D12_MESSAGE_SEVERITY_ERROR};
    D3D12_INFO_QUEUE_FILTER filter{};
    filter.AllowList.NumSeverities=2;
    filter.AllowList.pSeverityList=severities;
    messages_->PushRetrievalFilter(&filter);
  }

  const D3D12_COMMAND_QUEUE_DESC queue_desc{D3D12_COMMAND_LIST_TYPE_DIRECT,0,
                                            D3D12_COMMAND_QUEUE_FLAG_NONE,0};
  Require(device_->CreateCommandQueue(&queue_desc,IID_PPV_ARGS(&queue_)),"command queue creation");
  Require(device_->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence_)),"fence creation");
  fence_event_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  if(!fence_event_) throw std::runtime_error("D3D12 fence event creation failed");

  frames_.resize(options.frames_in_flight);
  for(auto& frame:frames_) {
    frame.allocators.resize(options.recorders);
    for(auto& allocator:frame.allocators)
      Require(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),
              "command allocator creation");
  }
  lists_.resize(options.recorders);
  for(uint32_t index=0;index<options.recorders;++index) {
    Require(device_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       frames_.front().allocators[index].Get(),nullptr,
                                       IID_PPV_ARGS(&lists_[index])),"command list creation");
    // Created open; every frame opens them itself.
    Require(lists_[index]->Close(),"command list close");
  }

  const D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_UPLOAD,D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                   D3D12_MEMORY_POOL_UNKNOWN,0,0};
  // The upload buffer is one allocation split evenly between recorders, so no
  // thread's allocations can touch another one's bytes.
  const uint64_t per_recorder=(options.upload_bytes/options.recorders)&~uint64_t(255);
  if(!per_recorder) throw std::runtime_error("upload_bytes is too small to split between recorders");
  const uint64_t upload_bytes=per_recorder*options.recorders;
  for(uint32_t index=0;index<options.recorders;++index) {
    rings_.emplace_back(per_recorder);
    ring_bases_.push_back(per_recorder*index);
  }
  const D3D12_RESOURCE_DESC desc{D3D12_RESOURCE_DIMENSION_BUFFER,0,upload_bytes,1,1,1,
                                 DXGI_FORMAT_UNKNOWN,{1,0},D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                 D3D12_RESOURCE_FLAG_NONE};
  Require(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
                                           D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
                                           IID_PPV_ARGS(&upload_)),"upload buffer creation");
  // Mapped once for the life of the device. Mapping per use would cost more
  // than the copy at this traffic, and an upload heap is CPU-visible anyway.
  const D3D12_RANGE no_read{0,0};
  void* mapped=nullptr;
  Require(upload_->Map(0,&no_read,&mapped),"upload buffer map");
  upload_cpu_=static_cast<uint8_t*>(mapped);
  upload_gpu_=upload_->GetGPUVirtualAddress();

  // One heap, sliced. Each command list binds the same heap, so switching
  // recorders costs nothing, and each allocates only inside its own slice.
  const uint32_t per_recorder_views=options.view_descriptors/options.recorders;
  if(!per_recorder_views) throw std::runtime_error("view_descriptors is too small to split between recorders");
  const D3D12_DESCRIPTOR_HEAP_DESC heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                             per_recorder_views*options.recorders,
                                             D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
  Require(device_->CreateDescriptorHeap(&heap_desc,IID_PPV_ARGS(&view_heap_)),"view heap creation");
  for(uint32_t index=0;index<options.recorders;++index)
    views_.push_back(std::make_unique<NativeD3D12DescriptorRing>(
        *device_.Get(),*view_heap_.Get(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        per_recorder_views*index,per_recorder_views));
  samplers_=std::make_unique<NativeD3D12SamplerCache>(*device_.Get(),options.sampler_slots,
                                                      options.sampler_tables);
}

bool NativeD3D12Device::WaitForOldestFrame() {
  const uint64_t completed=fence_->GetCompletedValue();
  uint64_t oldest=0;
  for(const auto& frame:frames_)
    if(frame.fence>completed && (!oldest||frame.fence<oldest)) oldest=frame.fence;
  if(!oldest) return false;
  WaitForFence(oldest);
  const uint64_t now=fence_->GetCompletedValue();
  for(uint32_t index=0;index<rings_.size();++index) {
    rings_[index].Retire(now);
    views_[index]->Retire(now);
  }
  return true;
}

NativeD3D12Device::~NativeD3D12Device() {
  // The GPU must not be reading memory we are about to unmap and free. A
  // destructor is the one place this is easy to forget and impossible to debug.
  try { WaitIdle(); } catch(...) {}
  if(upload_) { const D3D12_RANGE none{0,0}; upload_->Unmap(0,&none); }
  if(fence_event_) CloseHandle(fence_event_);
}

void NativeD3D12Device::WaitForFence(uint64_t value) {
  if(!value || fence_->GetCompletedValue()>=value) return;
  Require(fence_->SetEventOnCompletion(value,fence_event_),"fence event registration");
  // Deadlined, not INFINITE. A GPU that has hung never signals, and an
  // infinite wait turns that into a frozen process with nothing in the log -
  // which is how every hang in this port presented, and why each one cost a
  // bisect to find. Ten seconds is far longer than any frame and far shorter
  // than a person's patience.
  if(WaitForSingleObject(fence_event_,10000)==WAIT_OBJECT_0) return;
  std::string reason="the device reports no removal reason";
  switch(device_->GetDeviceRemovedReason()) {
    case DXGI_ERROR_DEVICE_HUNG: reason="the GPU hung on this device's own work"; break;
    case DXGI_ERROR_DEVICE_RESET: reason="the device was reset"; break;
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR: reason="the driver reported an internal error"; break;
    case DXGI_ERROR_INVALID_CALL: reason="an invalid call was made on this device"; break;
    case S_OK: break;
    default: reason="an unrecognised removal reason"; break;
  }
  throw std::runtime_error("D3D12 waited 10 seconds for fence value "+std::to_string(value)+
                           " (completed "+std::to_string(fence_->GetCompletedValue())+"); "+reason);
}

void NativeD3D12Device::BeginFrame() {
  if(open_) throw std::runtime_error("D3D12 frame already open");
  open_frame_=static_cast<uint32_t>(frame_counter_%frames_.size());
  auto& frame=frames_[open_frame_];
  // This slot's previous frame must be off the GPU before its allocator is
  // reset: resetting an allocator whose commands are still executing is
  // undefined, and it is the classic way a D3D12 port crashes only under load.
  if(frame.fence && fence_->GetCompletedValue()<frame.fence) {
    const auto started=std::chrono::steady_clock::now();
    WaitForFence(frame.fence);
    ++frame_waits_;
    frame_wait_ns_+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now()-started).count());
  }
  const uint64_t completed=fence_->GetCompletedValue();
  // Anything whose last possible reader has finished. Erased here rather than
  // at the drop, because here is the one place that knows what the GPU has
  // actually got through.
  std::erase_if(retiring_,[completed](const Retiring& held) { return held.fence<=completed; });
  // Every ring takes the frame's fence value: they are all freed by the same
  // signal because they are all read by the same submission.
  ++next_fence_;
  for(uint32_t index=0;index<lists_.size();++index) {
    rings_[index].Retire(completed);
    views_[index]->Retire(completed);
    Require(frame.allocators[index]->Reset(),"command allocator reset");
    Require(lists_[index]->Reset(frame.allocators[index].Get(),nullptr),"command list reset");
    rings_[index].BeginFrame(next_fence_);
    views_[index]->BeginFrame(next_fence_);
  }
  open_=true;
}

void NativeD3D12Device::Retire(ComPtr<ID3D12Resource> resource) {
  if(!resource) return;
  // The open frame's value, or the last one signalled when none is open.
  // Either way it is the highest value any submitted or recording work can
  // carry, which is exactly the wait this resource needs.
  retiring_.push_back({next_fence_,std::move(resource)});
}

void NativeD3D12Device::EndFrame() {
  if(!open_) throw std::runtime_error("D3D12 has no open frame");
  std::vector<ID3D12CommandList*> lists;
  lists.reserve(lists_.size());
  for(auto& list:lists_) {
    Require(list->Close(),"command list close");
    lists.push_back(list.Get());
  }
  // Submitted together and in recorder order, so work recorded in parallel
  // still executes in a defined order on the queue.
  queue_->ExecuteCommandLists(static_cast<UINT>(lists.size()),lists.data());
  // The fence value the ring already recorded for this frame, so the memory it
  // handed out is freed by exactly the signal that proves the GPU is done.
  Require(queue_->Signal(fence_.Get(),next_fence_),"queue signal");
  frames_[open_frame_].fence=next_fence_;
  for(uint32_t index=0;index<lists_.size();++index) {
    rings_[index].EndFrame();
    views_[index]->EndFrame();
  }
  ++frame_counter_;
  open_=false;
}

void NativeD3D12Device::WaitIdle() {
  if(!queue_||!fence_) return;
  Require(queue_->Signal(fence_.Get(),++next_fence_),"queue signal");
  WaitForFence(next_fence_);
  const uint64_t completed=fence_->GetCompletedValue();
  for(auto& ring:rings_) ring.Retire(completed);
  for(auto& views:views_) views->Retire(completed);
}

std::vector<std::string> NativeD3D12Device::DrainValidationErrors() {
  std::vector<std::string> found;
  found.swap(notes_);
  if(!messages_) return found;
  const UINT64 count=messages_->GetNumStoredMessagesAllowedByRetrievalFilter();
  for(UINT64 index=0;index<count;++index) {
    SIZE_T bytes=0;
    if(FAILED(messages_->GetMessage(index,nullptr,&bytes))||!bytes) continue;
    std::vector<uint8_t> storage(bytes);
    auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
    if(FAILED(messages_->GetMessage(index,message,&bytes))) continue;
    found.emplace_back(message->pDescription,message->DescriptionByteLength?message->DescriptionByteLength-1:0);
  }
  messages_->ClearStoredMessages();
  return found;
}

NativeD3D12Device::Upload NativeD3D12Device::Allocate(uint64_t bytes, uint64_t alignment,
                                                     uint32_t recorder) {
  auto& ring=rings_.at(recorder);
  auto allocation=ring.Allocate(bytes,alignment);
  if(allocation.status==NativeUploadRing::Status::Full) {
    // Give the GPU a chance to release a frame, then try again. Waiting on the
    // oldest in-flight frame is the smallest wait that can possibly help. It
    // is only safe from one thread, so with several recorders a stall here is
    // a sizing failure and the messages below say which ring ran out.
    ++upload_stalls_;
    while(allocation.status==NativeUploadRing::Status::Full && WaitForOldestFrame())
      allocation=ring.Allocate(bytes,alignment);
  }
  if(allocation.status==NativeUploadRing::Status::TooLarge)
    throw std::runtime_error("D3D12 upload of "+std::to_string(bytes)+
                             " bytes exceeds this recorder's upload ring of "+
                             std::to_string(ring.capacity())+" bytes");
  if(allocation.status!=NativeUploadRing::Status::Ok)
    // Every frame in flight has been waited on and it still does not fit, so a
    // single frame needs more upload memory than the ring holds. Say that,
    // rather than stalling forever on a fence that cannot help.
    throw std::runtime_error("D3D12 upload ring of "+std::to_string(ring.capacity())+
                             " bytes cannot satisfy one frame; high water "+
                             std::to_string(ring.high_water())+" bytes");
  const uint64_t offset=ring_bases_.at(recorder)+allocation.offset%ring.capacity();
  return {upload_cpu_+offset,upload_gpu_+offset,upload_.Get(),offset};
}
NativeD3D12DescriptorRing::Table NativeD3D12Device::AllocateViews(uint32_t count, uint32_t recorder) {
  auto& views=*views_.at(recorder);
  auto result=views.TryAllocate(count);
  if(result.status==NativeUploadRing::Status::Full) {
    ++descriptor_stalls_;
    while(result.status==NativeUploadRing::Status::Full && WaitForOldestFrame())
      result=views.TryAllocate(count);
  }
  if(result.status!=NativeUploadRing::Status::Ok)
    // Either the table is wider than the whole heap or one frame needs more
    // descriptors than the heap holds. Both are a sizing mistake, and saying
    // so beats binding a table that overlaps a draw still in flight.
    throw std::runtime_error("D3D12 view descriptor heap slice of "+
                             std::to_string(views.ring().capacity())+
                             " cannot satisfy a run of "+std::to_string(count)+
                             "; high water "+std::to_string(views.ring().high_water()));
  return result.table;
}
}  // namespace edf::native

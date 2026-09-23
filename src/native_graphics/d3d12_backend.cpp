#include "d3d12_backend.h"
#include "d3d12_pipeline.h"
#include "native_parallel_recorder.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>
#include <stdexcept>
#include <string>
#include <string_view>
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

// Every resource carries the state it is in. D3D11 tracked this for us and
// inserted barriers silently; here a wrong answer is a silent corruption on
// some hardware and a validation error on the rest, so the state lives with
// the resource and only the recorder is allowed to change it.
struct TrackedResource {
  // Constructed against the device it will be retired to. A resource whose
  // last owner drops it is not necessarily one the GPU has finished with, and
  // releasing it here rather than on the frame fence is a page fault waiting
  // for the next draw that still names it; see NativeD3D12Device::Retire. The
  // default constructor is for the resources the swap chain owns, which it
  // reclaims on its own schedule and must not be held past.
  TrackedResource()=default;
  explicit TrackedResource(NativeD3D12Device& retire_to):retire_to_(&retire_to) {}
  TrackedResource(TrackedResource&& other) noexcept
      : resource(std::move(other.resource)),state(std::move(other.state)),retire_to_(other.retire_to_) {
    other.retire_to_=nullptr;
  }
  TrackedResource& operator=(TrackedResource&& other) noexcept {
    if(this==&other) return *this;
    Release();
    resource=std::move(other.resource);
    state=std::move(other.state);
    retire_to_=other.retire_to_;
    other.retire_to_=nullptr;
    return *this;
  }
  TrackedResource(const TrackedResource&)=delete;
  TrackedResource& operator=(const TrackedResource&)=delete;
  ~TrackedResource() { Release(); }

  void NotifyDestroyed(const void* object) const {
    if(retire_to_ && retire_to_->before_resource_destroy) retire_to_->before_resource_destroy(object);
  }
  ComPtr<ID3D12Resource> resource;
  std::shared_ptr<D3D12_RESOURCE_STATES> state=
    std::make_shared<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_COMMON);

 private:
  void Release() {
    if(retire_to_ && resource) retire_to_->Retire(std::move(resource));
    resource.Reset();
  }
  NativeD3D12Device* retire_to_=nullptr;
};

class D3D12Buffer final : public NativeBackendBuffer {
 public:
  D3D12Buffer(TrackedResource tracked, size_t bytes, bool dynamic)
      : tracked_(std::move(tracked)),bytes_(bytes),dynamic_(dynamic) {}
  ~D3D12Buffer() override { tracked_.NotifyDestroyed(this); }
  size_t bytes() const override { return bytes_; }
  bool dynamic() const { return dynamic_; }
  // A token that expires with this object, so a staged upload can tell whether
  // the resource it was made for is still there.
  std::shared_ptr<void> alive() const { return alive_; }
  TrackedResource& tracked() { return tracked_; }
  D3D12_GPU_VIRTUAL_ADDRESS address() const { return tracked_.resource->GetGPUVirtualAddress(); }
 private:
  TrackedResource tracked_;
  size_t bytes_;
  bool dynamic_=false;
  std::shared_ptr<void> alive_=std::make_shared<char>();
};

class D3D12Texture final : public NativeBackendTexture {
 public:
  // The tracked resource is shared, not owned, because a sampled render target
  // is one resource behind two handles and both must transition the same
  // state. Two copies would each believe the resource was in the state they
  // last put it in, and the barrier that mattered would be skipped.
  D3D12Texture(std::shared_ptr<TrackedResource> tracked, uint32_t width, uint32_t height,
               D3D12_CPU_DESCRIPTOR_HANDLE view, NativeD3D12CpuDescriptorHeap& heap)
      : tracked_(std::move(tracked)),width_(width),height_(height),view_(view),heap_(&heap) {}
  D3D12Texture(TrackedResource tracked, uint32_t width, uint32_t height,
               D3D12_CPU_DESCRIPTOR_HANDLE view, NativeD3D12CpuDescriptorHeap& heap)
      : D3D12Texture(std::make_shared<TrackedResource>(std::move(tracked)),width,height,view,heap) {}
  ~D3D12Texture() override { tracked_->NotifyDestroyed(this); if(heap_) heap_->Free(view_); }
  uint32_t width() const override { return width_; }
  uint32_t height() const override { return height_; }
  TrackedResource& tracked() { return *tracked_; }
  const std::shared_ptr<TrackedResource>& shared() const { return tracked_; }
  std::shared_ptr<void> alive() const { return alive_; }
  D3D12_CPU_DESCRIPTOR_HANDLE view() const { return view_; }
 private:
  std::shared_ptr<TrackedResource> tracked_;
  uint32_t width_,height_;
  D3D12_CPU_DESCRIPTOR_HANDLE view_;
  NativeD3D12CpuDescriptorHeap* heap_;
  std::shared_ptr<void> alive_=std::make_shared<char>();
};

class D3D12RenderTarget final : public NativeBackendRenderTarget {
 public:
  D3D12RenderTarget(std::shared_ptr<TrackedResource> tracked, uint32_t width, uint32_t height,
                    bool depth, D3D12_CPU_DESCRIPTOR_HANDLE view, NativeD3D12CpuDescriptorHeap& heap,
                    std::unique_ptr<D3D12Texture> sampled={})
      : tracked_(std::move(tracked)),sampled_(std::move(sampled)),width_(width),height_(height),
        depth_(depth),view_(view),heap_(&heap) {}
  D3D12RenderTarget(TrackedResource tracked, uint32_t width, uint32_t height, bool depth,
                    D3D12_CPU_DESCRIPTOR_HANDLE view, NativeD3D12CpuDescriptorHeap& heap)
      : D3D12RenderTarget(std::make_shared<TrackedResource>(std::move(tracked)),width,height,depth,
                          view,heap) {}
  ~D3D12RenderTarget() override { tracked_->NotifyDestroyed(this); if(heap_) heap_->Free(view_); }
  uint32_t width() const override { return width_; }
  uint32_t height() const override { return height_; }
  NativeBackendTexture* texture() override { return sampled_.get(); }
  bool depth() const { return depth_; }
  TrackedResource& tracked() { return *tracked_; }
  const std::shared_ptr<TrackedResource>& shared() const { return tracked_; }
  D3D12_CPU_DESCRIPTOR_HANDLE view() const { return view_; }
 private:
  std::shared_ptr<TrackedResource> tracked_;
  // The same resource behind a texture handle, sharing the state above.
  std::unique_ptr<D3D12Texture> sampled_;
  uint32_t width_,height_;
  bool depth_;
  D3D12_CPU_DESCRIPTOR_HANDLE view_;
  NativeD3D12CpuDescriptorHeap* heap_;
};

class D3D12SharedSurface final : public NativeBackendSharedSurface {
 public:
  D3D12SharedSurface(std::shared_ptr<TrackedResource> tracked, ComPtr<ID3D12Fence> fence,
                     void* texture_handle, void* fence_handle,
                     uint32_t width, uint32_t height, uint32_t format)
      : tracked_(std::move(tracked)),fence_(std::move(fence)),
        texture_handle_(texture_handle),fence_handle_(fence_handle),
        width_(width),height_(height),format_(format) {}
  ~D3D12SharedSurface() override {
    if(texture_handle_) CloseHandle(texture_handle_);
    if(fence_handle_) CloseHandle(fence_handle_);
  }
  void* texture_handle() const override { return texture_handle_; }
  void* fence_handle() const override { return fence_handle_; }
  uint32_t width() const override { return width_; }
  uint32_t height() const override { return height_; }
  uint32_t format() const override { return format_; }
  uint64_t value() const override { return value_; }
  TrackedResource& tracked() { return *tracked_; }
  ID3D12Fence* fence() const { return fence_.Get(); }
  uint64_t Advance() { return ++value_; }
 private:
  std::shared_ptr<TrackedResource> tracked_;
  ComPtr<ID3D12Fence> fence_;
  void* texture_handle_=nullptr;
  void* fence_handle_=nullptr;
  uint32_t width_,height_,format_;
  uint64_t value_=0;
};

class D3D12Pipeline final : public NativeBackendPipeline {
 public:
  D3D12Pipeline(ID3D12PipelineState& state, D3D12_PRIMITIVE_TOPOLOGY topology,
                bool requires_blend_factor, bool replicate_blend_alpha)
      : state_(&state),topology_(topology),requires_blend_factor_(requires_blend_factor),
        replicate_blend_alpha_(replicate_blend_alpha) {}
  ID3D12PipelineState& state() const { return *state_; }
  D3D12_PRIMITIVE_TOPOLOGY topology() const { return topology_; }
  bool requires_blend_factor() const override { return requires_blend_factor_; }
  bool replicate_blend_alpha() const { return replicate_blend_alpha_; }
 private:
  ID3D12PipelineState* state_;
  D3D12_PRIMITIVE_TOPOLOGY topology_;
  bool requires_blend_factor_;
  bool replicate_blend_alpha_=false;
};
// The factor this pipeline actually wants, from the factor the guest wrote.
// Duplicated deliberately in both backends rather than shared: it is three
// lines, and a helper that only one of them called would be the thing that
// drifts.
inline std::array<float,4> ResolvedBlendFactor(bool requires_factor,bool replicate,
                                               const std::array<float,4>& factor) {
  if(!requires_factor) return factor;
  for(float value:factor)
    if(!std::isfinite(value) || value<0 || value>1)
      throw std::runtime_error("a constant blend factor outside [0,1] cannot be bound");
  if(replicate) return {factor[3],factor[3],factor[3],factor[3]};
  return factor;
}

class D3D12Sampler final : public NativeBackendSampler {
 public:
  explicit D3D12Sampler(const D3D12_SAMPLER_DESC& desc) : desc_(desc) {}
  const D3D12_SAMPLER_DESC& desc() const { return desc_; }
 private:
  D3D12_SAMPLER_DESC desc_;
};

// A D3D12 query is not readable where it is written. The result has to be
// resolved into a buffer on the command list, and that buffer is only valid
// once the GPU has passed the fence for the frame that resolved it - which is
// why the query remembers a fence value rather than a flag.
class D3D12Query final : public NativeBackendQuery {
 public:
  D3D12Query(NativeD3D12Device& gpu,ComPtr<ID3D12QueryHeap> heap, ComPtr<ID3D12Resource> readback, NativeBackendQueryKind kind)
      : gpu_(&gpu),heap_(std::move(heap)),readback_(std::move(readback)),kind_(kind) {}
  ~D3D12Query() override { gpu_->Retire(std::move(heap_)); gpu_->Retire(std::move(readback_)); }
  D3D12_QUERY_TYPE type() const { return kind_==NativeBackendQueryKind::Timestamp?
    D3D12_QUERY_TYPE_TIMESTAMP:D3D12_QUERY_TYPE_OCCLUSION; }
  ID3D12QueryHeap* heap() const { return heap_.Get(); }
  ID3D12Resource* readback() const { return readback_.Get(); }
  NativeBackendQueryKind kind() const { return kind_; }
  void Resolved(uint64_t fence) { fence_=fence; }
  uint64_t fence() const { return fence_; }
 private:
  NativeD3D12Device* gpu_;
  ComPtr<ID3D12QueryHeap> heap_;
  ComPtr<ID3D12Resource> readback_;
  NativeBackendQueryKind kind_;
  uint64_t fence_=0;  // 0 = never resolved.
};

// Profiling timestamps (edf_native_gpu_timings): one heap and one readback
// buffer for the whole set. Each slot remembers the fence of the frame that
// last resolved it, so a range is readable exactly when the GPU has passed
// every resolve that covered it. A resolve may be recorded by a packet worker,
// hence atomics; the producer only reads them after joining the workers.
class D3D12Timestamps final : public NativeBackendTimestamps {
 public:
  D3D12Timestamps(NativeD3D12Device& gpu,ComPtr<ID3D12QueryHeap> heap,ComPtr<ID3D12Resource> readback,uint32_t capacity)
      : gpu_(&gpu),heap_(std::move(heap)),readback_(std::move(readback)),capacity_(capacity),
        fences_(std::make_unique<std::atomic<uint64_t>[]>(capacity)) {}
  ~D3D12Timestamps() override { gpu_->Retire(std::move(heap_)); gpu_->Retire(std::move(readback_)); }
  uint32_t capacity() const override { return capacity_; }
  ID3D12QueryHeap* heap() const { return heap_.Get(); }
  ID3D12Resource* readback() const { return readback_.Get(); }
  void Check(uint32_t first,uint32_t count) const {
    if(!count || first>=capacity_ || count>capacity_-first)
      throw std::runtime_error("timestamp range "+std::to_string(first)+"+"+std::to_string(count)+
                               " is outside a set of "+std::to_string(capacity_));
  }
  void Resolved(uint32_t first,uint32_t count,uint64_t fence) {
    for(uint32_t slot=first;slot<first+count;++slot) fences_[slot].store(fence,std::memory_order_release);
  }
  // The fence the whole range waits for; 0 when any slot was never resolved.
  uint64_t fence(uint32_t first,uint32_t count) const {
    uint64_t latest=0;
    for(uint32_t slot=first;slot<first+count;++slot) {
      const auto value=fences_[slot].load(std::memory_order_acquire);
      if(!value) return 0;
      latest=(std::max)(latest,value);
    }
    return latest;
  }
 private:
  NativeD3D12Device* gpu_;
  ComPtr<ID3D12QueryHeap> heap_;
  ComPtr<ID3D12Resource> readback_;
  uint32_t capacity_;
  std::unique_ptr<std::atomic<uint64_t>[]> fences_;
};

D3D12_FILTER_TYPE FilterType(NativeBackendFilter filter) {
  return filter==NativeBackendFilter::Point?D3D12_FILTER_TYPE_POINT:D3D12_FILTER_TYPE_LINEAR;
}
D3D12_TEXTURE_ADDRESS_MODE AddressMode(NativeBackendAddress address) {
  switch(address) {
    case NativeBackendAddress::Wrap: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case NativeBackendAddress::Mirror: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case NativeBackendAddress::Clamp: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case NativeBackendAddress::Border: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    case NativeBackendAddress::MirrorOnce: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
  }
  throw std::runtime_error("unknown backend address mode");
}
D3D12_SAMPLER_DESC SamplerDesc(const NativeBackendSamplerDesc& desc) {
  D3D12_SAMPLER_DESC native{};
  // Anisotropic is a whole-filter mode in D3D12, not a per-axis one, so asking
  // for it on any axis means asking for it.
  const bool anisotropic=desc.min==NativeBackendFilter::Anisotropic||
                         desc.mag==NativeBackendFilter::Anisotropic||
                         desc.mip==NativeBackendFilter::Anisotropic;
  native.Filter=anisotropic?D3D12_FILTER_ANISOTROPIC
                           :D3D12_ENCODE_BASIC_FILTER(FilterType(desc.min),FilterType(desc.mag),
                                                      FilterType(desc.mip),
                                                      D3D12_FILTER_REDUCTION_TYPE_STANDARD);
  native.AddressU=AddressMode(desc.u);
  native.AddressV=AddressMode(desc.v);
  native.AddressW=AddressMode(desc.w);
  native.MipLODBias=desc.mip_lod_bias;
  native.MaxAnisotropy=anisotropic?(desc.max_anisotropy?desc.max_anisotropy:16):1;
  // ALWAYS, matching the renderer's own sampler decode and the D3D11 backend.
  native.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
  for(size_t index=0;index<4;++index) native.BorderColor[index]=desc.border[index];
  native.MinLOD=desc.min_lod;
  native.MaxLOD=desc.max_lod;
  return native;
}

D3D12_PRIMITIVE_TOPOLOGY_TYPE TopologyType(NativeBackendTopology topology) {
  switch(topology) {
    case NativeBackendTopology::TriangleList:
    case NativeBackendTopology::TriangleStrip: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    case NativeBackendTopology::LineList: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case NativeBackendTopology::PointList: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
  }
  throw std::runtime_error("unknown backend topology");
}
D3D12_PRIMITIVE_TOPOLOGY Topology(NativeBackendTopology topology) {
  switch(topology) {
    case NativeBackendTopology::TriangleList: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case NativeBackendTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case NativeBackendTopology::LineList: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case NativeBackendTopology::PointList: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
  }
  throw std::runtime_error("unknown backend topology");
}

class D3D12Recorder final : public NativeBackendRecorder {
 public:
  D3D12Recorder(NativeD3D12Device& gpu, ID3D12RootSignature& signature, uint32_t index)
      : gpu_(&gpu),signature_(&signature),index_(index) {}

  void Begin(ID3D12GraphicsCommandList& commands) {
    commands_=&commands;
    ID3D12DescriptorHeap* heaps[]={gpu_->view_heap(),gpu_->samplers().heap()};
    commands_->SetDescriptorHeaps(2,heaps);
    commands_->SetGraphicsRootSignature(signature_);
    bound_={};
    stack_.clear();
    transitions_.clear();
  }
  void End() { commands_=nullptr; transitions_.clear(); }

  // First-use transitions are resolved on the submitting thread in command-list
  // order. Workers never read or mutate the resource's global state. Later uses
  // within this list have a known local predecessor and can record barriers now.
  void Transition(TrackedResource& tracked, D3D12_RESOURCE_STATES wanted) {
    auto found=transitions_.find(tracked.state.get());
    if(found==transitions_.end()) {
      transitions_.emplace(tracked.state.get(),TransitionState{tracked.resource,tracked.state,wanted,wanted});
      return;
    }
    if(found->second.last==wanted) return;
    Barrier(Commands(),tracked.resource.Get(),found->second.last,wanted);
    found->second.last=wanted;
  }
  void ResolveTransitions() {
    auto& before=*gpu_->preamble(index_);
    for(auto& [key,transition]:transitions_) {
      if(*transition.global!=transition.first)
        Barrier(before,transition.resource.Get(),*transition.global,transition.first);
      *transition.global=transition.last;
    }
  }
  static void Barrier(ID3D12GraphicsCommandList& commands,ID3D12Resource* resource,
                      D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=resource;
    barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore=before;
    barrier.Transition.StateAfter=after;
    commands.ResourceBarrier(1,&barrier);
  }

  void SetPipeline(NativeBackendPipeline& pipeline) override {
    auto& concrete=static_cast<D3D12Pipeline&>(pipeline);
    Commands().SetPipelineState(&concrete.state());
    Commands().IASetPrimitiveTopology(concrete.topology());
    bound_.pipeline=&concrete;
  }
  void SetVertexBuffer(uint32_t slot, NativeBackendBuffer& buffer, uint32_t stride, uint32_t offset) override {
    auto& concrete=static_cast<D3D12Buffer&>(buffer);
    Transition(concrete.tracked(),D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    const D3D12_VERTEX_BUFFER_VIEW view{concrete.address()+offset,
                                        static_cast<UINT>(concrete.bytes()-offset),stride};
    Commands().IASetVertexBuffers(slot,1,&view);
  }
  void SetIndexBuffer(NativeBackendBuffer& buffer, NativeBackendIndexFormat format, uint32_t offset) override {
    auto& concrete=static_cast<D3D12Buffer&>(buffer);
    Transition(concrete.tracked(),D3D12_RESOURCE_STATE_INDEX_BUFFER);
    const D3D12_INDEX_BUFFER_VIEW view{concrete.address()+offset,
                                       static_cast<UINT>(concrete.bytes()-offset),
                                       format==NativeBackendIndexFormat::Uint16?DXGI_FORMAT_R16_UINT
                                                                               :DXGI_FORMAT_R32_UINT};
    Commands().IASetIndexBuffer(&view);
  }
  void SetTransientVertices(uint32_t slot, std::span<const uint8_t> bytes, uint32_t stride) override {
    if(bytes.empty()) throw std::runtime_error("transient vertices need at least one byte");
    // The upload ring is an upload heap, which is always readable as a vertex
    // buffer: a copy and a view, no resource and no transition. The same
    // fenced slice SetConstants uses, retired when the frame completes.
    const auto upload=gpu_->Allocate(bytes.size(),16,index_);
    std::memcpy(upload.cpu,bytes.data(),bytes.size());
    const D3D12_VERTEX_BUFFER_VIEW view{upload.gpu,static_cast<UINT>(bytes.size()),stride};
    Commands().IASetVertexBuffers(slot,1,&view);
  }
  void SetTopology(NativeBackendTopology topology) override {
    Commands().IASetPrimitiveTopology(Topology(topology));
  }
  void SetBlendFactor(const std::array<float,4>& factor) override {
    // Needs the pipeline, because whether the alpha is replicated is part of
    // the blend state it was built from.
    if(!bound_.pipeline)
      throw std::runtime_error("a blend factor was set before the pipeline it belongs to");
    Commands().OMSetBlendFactor(ResolvedBlendFactor(bound_.pipeline->requires_blend_factor(),
                                                    bound_.pipeline->replicate_blend_alpha(),
                                                    factor).data());
    bound_.blend_factor_set=true;
  }

  void SetConstants(NativeBackendStage stage, uint32_t slot, std::span<const uint8_t> bytes) override {
    // Straight into the upload ring and then into a root descriptor: no
    // constant buffer object, no descriptor, no map. This is the path the slot
    // measurement bought - it is why the constant buffers are root CBVs.
    const auto upload=gpu_->Allocate(bytes.size(),
                                     D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,index_);
    std::memcpy(upload.cpu,bytes.data(),bytes.size());
    Commands().SetGraphicsRootConstantBufferView(RootConstantSlot(stage,slot),upload.gpu);
  }
  // A packet recorder's frame-owned image: staged once per submission, by
  // whichever list binds it first, and bound by address from every list after
  // that. All lists of a submission retire together with its fence, so a
  // slice in another list's ring lives as long as this list does. Two lists
  // racing to stage it each copy the same bytes; either address is right.
  void SetConstantImage(NativeBackendStage stage, uint32_t slot,
                        const NativeBackendConstantImage& image) override {
    const auto root=RootConstantSlot(stage,slot);
    const auto frame=gpu_->pending_fence();
    D3D12_GPU_VIRTUAL_ADDRESS address=0;
    if(image.staged_frame.load(std::memory_order_acquire)==frame) {
      address=image.staged_address.load(std::memory_order_relaxed);
      constant_upload_reuses_.fetch_add(1,std::memory_order_relaxed);
    } else {
      const auto upload=gpu_->Allocate(image.bytes.size(),
                                       D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,index_);
      std::memcpy(upload.cpu,image.bytes.data(),image.bytes.size());
      address=upload.gpu;
      image.staged_address.store(address,std::memory_order_relaxed);
      image.staged_frame.store(frame,std::memory_order_release);
      constant_uploads_.fetch_add(1,std::memory_order_relaxed);
    }
    Commands().SetGraphicsRootConstantBufferView(root,address);
  }
  uint64_t constant_uploads() const { return constant_uploads_.load(std::memory_order_relaxed); }
  uint64_t constant_upload_reuses() const { return constant_upload_reuses_.load(std::memory_order_relaxed); }
  void SetTexture(NativeBackendStage stage, uint32_t slot, NativeBackendTexture* texture) override {
    if(stage!=NativeBackendStage::Pixel)
      throw std::runtime_error("this root signature declares no vertex-stage textures");
    if(slot>=NativeD3D12RootLayout::kPixelTextures)
      throw std::runtime_error("texture slot "+std::to_string(slot)+" is outside the root signature");
    bound_.textures[slot].set(static_cast<D3D12Texture*>(texture));
    bound_.textures_dirty=true;
  }
  void SetSampler(NativeBackendStage stage, uint32_t slot, NativeBackendSampler* sampler) override {
    if(stage!=NativeBackendStage::Pixel)
      throw std::runtime_error("this root signature declares no vertex-stage samplers");
    if(slot>=NativeD3D12RootLayout::kPixelSamplers)
      throw std::runtime_error("sampler slot "+std::to_string(slot)+" is outside the root signature");
    bound_.samplers[slot]=static_cast<D3D12Sampler*>(sampler);
    bound_.samplers_dirty=true;
  }

  void SetRenderTargets(std::span<NativeBackendRenderTarget* const> colors,
                        NativeBackendRenderTarget* depth) override {
    D3D12_CPU_DESCRIPTOR_HANDLE views[8]{};
    uint32_t count=0;
    for(auto* target:colors) {
      if(!target) continue;
      auto& concrete=*static_cast<D3D12RenderTarget*>(target);
      Transition(concrete.tracked(),D3D12_RESOURCE_STATE_RENDER_TARGET);
      views[count++]=concrete.view();
      if(count==8) break;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE depth_view{};
    if(depth) {
      auto& concrete=*static_cast<D3D12RenderTarget*>(depth);
      Transition(concrete.tracked(),D3D12_RESOURCE_STATE_DEPTH_WRITE);
      depth_view=concrete.view();
    }
    Commands().OMSetRenderTargets(count,count?views:nullptr,FALSE,depth?&depth_view:nullptr);
    bound_.render_targets=count;
  }
  void SetViewport(const NativeBackendViewport& viewport) override {
    const D3D12_VIEWPORT native{viewport.x,viewport.y,viewport.width,viewport.height,
                                viewport.min_depth,viewport.max_depth};
    Commands().RSSetViewports(1,&native);
    // D3D12 clips nothing without a scissor rectangle, so a viewport with no
    // scissor set would draw outside itself. Default it to the viewport; an
    // explicit SetScissor overrides this.
    const D3D12_RECT rect{static_cast<LONG>(viewport.x),static_cast<LONG>(viewport.y),
                          static_cast<LONG>(viewport.x+viewport.width),
                          static_cast<LONG>(viewport.y+viewport.height)};
    bound_.viewport_scissor=rect;
    Commands().RSSetScissorRects(1,bound_.scissor_enabled?&bound_.scissor:&bound_.viewport_scissor);
  }
  void SetScissor(const NativeBackendScissor& scissor, bool enabled) override {
    bound_.scissor_enabled=enabled;
    bound_.scissor={scissor.left,scissor.top,scissor.right,scissor.bottom};
    Commands().RSSetScissorRects(1,enabled?&bound_.scissor:&bound_.viewport_scissor);
  }

  void ClearColor(NativeBackendRenderTarget& target, const std::array<float,4>& color) override {
    auto& concrete=static_cast<D3D12RenderTarget&>(target);
    Transition(concrete.tracked(),D3D12_RESOURCE_STATE_RENDER_TARGET);
    Commands().ClearRenderTargetView(concrete.view(),color.data(),0,nullptr);
  }
  void ClearDepthStencil(NativeBackendRenderTarget& target, bool depth, bool stencil,
                         float depth_value, uint8_t stencil_value) override {
    auto& concrete=static_cast<D3D12RenderTarget&>(target);
    Transition(concrete.tracked(),D3D12_RESOURCE_STATE_DEPTH_WRITE);
    D3D12_CLEAR_FLAGS flags{};
    if(depth) flags|=D3D12_CLEAR_FLAG_DEPTH;
    if(stencil) flags|=D3D12_CLEAR_FLAG_STENCIL;
    if(!static_cast<uint32_t>(flags)) return;
    Commands().ClearDepthStencilView(concrete.view(),flags,depth_value,stencil_value,0,nullptr);
  }

  void Draw(uint32_t vertices, uint32_t first_vertex) override {
    RequireBlendFactor();
    FlushTextures();
    FlushSamplers();
    Commands().DrawInstanced(vertices,1,first_vertex,0);
  }
  void DrawIndexed(uint32_t indices, uint32_t first_index, int32_t base_vertex) override {
    RequireBlendFactor();
    FlushTextures();
    FlushSamplers();
    Commands().DrawIndexedInstanced(indices,1,first_index,base_vertex,0);
  }
  void DrawIndexedInstanced(uint32_t indices, uint32_t instances, uint32_t first_index,
                            int32_t base_vertex, uint32_t first_instance) override {
    RequireBlendFactor();
    FlushTextures();
    FlushSamplers();
    Commands().DrawIndexedInstanced(indices,instances,first_index,base_vertex,first_instance);
  }

  void CopyTexture(NativeBackendTexture& destination, NativeBackendTexture& source) override {
    auto& to=static_cast<D3D12Texture&>(destination);
    auto& from=static_cast<D3D12Texture&>(source);
    Transition(to.tracked(),D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(from.tracked(),D3D12_RESOURCE_STATE_COPY_SOURCE);
    Commands().CopyResource(to.tracked().resource.Get(),from.tracked().resource.Get());
  }
  void ReleaseSharedTexture(NativeBackendTexture& texture) override {
    Transition(static_cast<D3D12Texture&>(texture).tracked(),D3D12_RESOURCE_STATE_COMMON);
  }
  void ResolveTarget(NativeBackendTexture& destination, NativeBackendRenderTarget& source) override {
    auto& to=static_cast<D3D12Texture&>(destination);
    auto& from=static_cast<D3D12RenderTarget&>(source);
    const auto description=from.tracked().resource->GetDesc();
    if(description.SampleDesc.Count>1) {
      // A real resolve. The states are their own pair, not the copy ones, and
      // using COPY_SOURCE here is the mistake validation would catch.
      Transition(to.tracked(),D3D12_RESOURCE_STATE_RESOLVE_DEST);
      Transition(from.tracked(),D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
      Commands().ResolveSubresource(to.tracked().resource.Get(),0,
                                    from.tracked().resource.Get(),0,description.Format);
      return;
    }
    // Resolving a single-sampled target is a copy, and callers do it rather
    // than branching on the sample count themselves.
    Transition(to.tracked(),D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(from.tracked(),D3D12_RESOURCE_STATE_COPY_SOURCE);
    Commands().CopyResource(to.tracked().resource.Get(),from.tracked().resource.Get());
  }
  void CopyToShared(NativeBackendSharedSurface& destination,
                    NativeBackendRenderTarget& source) override {
    auto& to=static_cast<D3D12SharedSurface&>(destination);
    auto& from=static_cast<D3D12RenderTarget&>(source);
    const auto description=from.tracked().resource->GetDesc();
    if(description.SampleDesc.Count>1) {
      Transition(to.tracked(),D3D12_RESOURCE_STATE_RESOLVE_DEST);
      Transition(from.tracked(),D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
      Commands().ResolveSubresource(to.tracked().resource.Get(),0,
                                    from.tracked().resource.Get(),0,description.Format);
    } else {
      Transition(to.tracked(),D3D12_RESOURCE_STATE_COPY_DEST);
      Transition(from.tracked(),D3D12_RESOURCE_STATE_COPY_SOURCE);
      Commands().CopyResource(to.tracked().resource.Get(),from.tracked().resource.Get());
    }
    // Left readable by the other API. A shared surface has no state tracking
    // on the consumer's side, so it has to be in a state that API can sample
    // from before this command list ends.
    Transition(to.tracked(),D3D12_RESOURCE_STATE_COMMON);
  }
  void UpdateBuffer(NativeBackendBuffer& buffer, uint32_t offset, std::span<const uint8_t> bytes) override {
    auto& concrete=static_cast<D3D12Buffer&>(buffer);
    if(offset+bytes.size()>concrete.bytes())
      throw std::runtime_error("a buffer update of "+std::to_string(bytes.size())+
                               " bytes at "+std::to_string(offset)+" runs past its "+
                               std::to_string(concrete.bytes())+" bytes");
    // The same whole-or-nothing rule the D3D11 backend enforces, so a caller
    // cannot write a partial dynamic update that happens to work here and
    // returns undefined bytes there. The copy below is recorded in order, so
    // draws already in this list keep the contents they were recorded with.
    if(concrete.dynamic() && (offset || bytes.size()!=concrete.bytes()))
      throw std::runtime_error("a dynamic buffer is updated whole or not at all; this update covers "+
                               std::to_string(bytes.size())+" of "+std::to_string(concrete.bytes())+
                               " bytes at offset "+std::to_string(offset));
    const auto upload=gpu_->Allocate(bytes.size(),16,index_);
    std::memcpy(upload.cpu,bytes.data(),bytes.size());
    Transition(concrete.tracked(),D3D12_RESOURCE_STATE_COPY_DEST);
    Commands().CopyBufferRegion(concrete.tracked().resource.Get(),offset,upload.resource,
                                upload.offset,bytes.size());
  }

  void UpdateTexture(NativeBackendTexture& texture, std::span<const uint8_t> bytes) override {
    auto& concrete=static_cast<D3D12Texture&>(texture);
    const auto description=concrete.tracked().resource->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 total=0,row_bytes=0;
    UINT rows=0;
    gpu_->device()->GetCopyableFootprints(&description,0,1,0,&footprint,&rows,&row_bytes,&total);
    if(bytes.size()<static_cast<size_t>(row_bytes)*rows)
      throw std::runtime_error("texture update is "+std::to_string(bytes.size())+
                               " bytes but the texture needs "+std::to_string(row_bytes*rows));
    const auto upload=gpu_->Allocate(total,D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT,index_);
    for(UINT row=0;row<rows;++row)
      std::memcpy(upload.cpu+static_cast<size_t>(row)*footprint.Footprint.RowPitch,
                  bytes.data()+static_cast<size_t>(row)*row_bytes,static_cast<size_t>(row_bytes));
    footprint.Offset=upload.offset;
    Transition(concrete.tracked(),D3D12_RESOURCE_STATE_COPY_DEST);
    const D3D12_TEXTURE_COPY_LOCATION from{upload.resource,
                                           D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,{footprint}};
    D3D12_TEXTURE_COPY_LOCATION to{};
    to.pResource=concrete.tracked().resource.Get();
    to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.SubresourceIndex=0;
    Commands().CopyTextureRegion(&to,0,0,0,&from,nullptr);
  }

  void BeginQuery(NativeBackendQuery& query) override {
    auto& concrete=static_cast<D3D12Query&>(query);
    if(concrete.kind()!=NativeBackendQueryKind::Occlusion)
      throw std::runtime_error("timestamp queries are recorded with EndQuery only");
    Commands().BeginQuery(concrete.heap(),concrete.type(),0);
  }
  void EndQuery(NativeBackendQuery& query) override {
    auto& concrete=static_cast<D3D12Query&>(query);
    Commands().EndQuery(concrete.heap(),concrete.type(),0);
    // Resolved straight away rather than on a later frame: the result is only
    // readable out of a buffer, and deferring the resolve would mean tracking
    // which frame each unresolved query belonged to.
    Commands().ResolveQueryData(concrete.heap(),concrete.type(),0,1,
                                concrete.readback(),0);
    concrete.Resolved(gpu_->pending_fence());
  }
  void WriteTimestamp(NativeBackendTimestamps& set,uint32_t slot) override {
    auto& concrete=static_cast<D3D12Timestamps&>(set);
    concrete.Check(slot,1);
    Commands().EndQuery(concrete.heap(),D3D12_QUERY_TYPE_TIMESTAMP,slot);
  }
  void ResolveTimestamps(NativeBackendTimestamps& set,uint32_t first,uint32_t count) override {
    auto& concrete=static_cast<D3D12Timestamps&>(set);
    concrete.Check(first,count);
    Commands().ResolveQueryData(concrete.heap(),D3D12_QUERY_TYPE_TIMESTAMP,first,count,
                                concrete.readback(),uint64_t(first)*sizeof(uint64_t));
    concrete.Resolved(first,count,gpu_->pending_fence());
  }
  void PushState() override { stack_.push_back(bound_); }
  void PopState() override {
    if(stack_.empty()) throw std::runtime_error("PopState with nothing pushed");
    bound_=stack_.back();
    stack_.pop_back();
    if(bound_.pipeline) SetPipeline(*bound_.pipeline);
    bound_.textures_dirty=true;
    bound_.samplers_dirty=true;
  }

 private:
  ID3D12GraphicsCommandList& Commands() {
    if(!commands_) throw std::runtime_error("the D3D12 recorder is not inside a frame");
    return *commands_;
  }
  static uint32_t RootConstantSlot(NativeBackendStage stage, uint32_t slot) {
    if(stage==NativeBackendStage::Vertex) {
      if(slot>=NativeD3D12RootLayout::kVertexConstantBuffers)
        throw std::runtime_error("vertex constant slot "+std::to_string(slot)+
                                 " is outside the root signature");
      return NativeD3D12RootLayout::kVertexConstants0+slot;
    }
    if(stage!=NativeBackendStage::Pixel)
      throw std::runtime_error("this root signature declares no compute stage");
    if(slot>=NativeD3D12RootLayout::kPixelConstantBuffers)
      throw std::runtime_error("pixel constant slot "+std::to_string(slot)+
                               " is outside the root signature");
    return NativeD3D12RootLayout::kPixelConstants0+slot;
  }
  // Textures are gathered as they are set and written into one table just
  // before the draw that reads them. Copying eight descriptors per SetTexture
  // would write the table up to eight times for one draw.
  // The whole table is resolved at once from the cache, because that is the
  // only shape the 2,048-descriptor sampler heap allows. Slots left unbound get
  // a defined sampler rather than whatever the last combination had there.
  void RequireBlendFactor() {
    if(bound_.pipeline && bound_.pipeline->requires_blend_factor() && !bound_.blend_factor_set)
      throw std::runtime_error("this draw blends against a constant blend factor that was never set");
  }
  void FlushSamplers() {
    if(!bound_.samplers_dirty) return;
    // The same combination as the last draw, which is the usual case: a run of
    // draws shares its material. Comparing eight pointers is cheaper than
    // rebuilding eight descriptions and looking them up by their bytes. Keyed
    // on the frame's fence value, because a table from an earlier frame may
    // have been recycled since.
    if(sampler_memo_frame_==gpu_->pending_fence() &&
       std::equal(std::begin(bound_.samplers),std::end(bound_.samplers),std::begin(sampler_memo_))) {
      Commands().SetGraphicsRootDescriptorTable(NativeD3D12RootLayout::kPixelSamplerTable,
                                                sampler_memo_table_);
      bound_.samplers_dirty=false;
      return;
    }
    std::array<D3D12_SAMPLER_DESC,NativeD3D12RootLayout::kPixelSamplers> descs{};
    for(uint32_t slot=0;slot<descs.size();++slot)
      descs[slot]=bound_.samplers[slot]?bound_.samplers[slot]->desc():DefaultSampler();
    // Stamped with the frame being recorded and told what the GPU has passed,
    // so a combination this frame needs can take the slots of one no frame is
    // still reading.
    const auto table=gpu_->samplers().Table(descs,gpu_->pending_fence(),gpu_->completed_fence());
    Commands().SetGraphicsRootDescriptorTable(NativeD3D12RootLayout::kPixelSamplerTable,table);
    std::copy(std::begin(bound_.samplers),std::end(bound_.samplers),std::begin(sampler_memo_));
    sampler_memo_table_=table;
    sampler_memo_frame_=gpu_->pending_fence();
    bound_.samplers_dirty=false;
  }
  static D3D12_SAMPLER_DESC DefaultSampler() {
    D3D12_SAMPLER_DESC sampler{};
    sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxAnisotropy=1;
    sampler.MaxLOD=D3D12_FLOAT32_MAX;
    return sampler;
  }
  void FlushTextures() {
    if(!bound_.textures_dirty) return;
    // The barriers are not part of the memo, and finding that out cost a
    // conformance failure: a sampled render target can be written again
    // between two draws that bind it, so every draw has to say what state it
    // needs even when the descriptors have not moved. Transition first,
    // unconditionally; only the table is reused.
    for(uint32_t slot=0;slot<NativeD3D12RootLayout::kPixelTextures;++slot)
      if(auto* texture=bound_.textures[slot].get())
        Transition(texture->tracked(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    // The same eight textures need the same table, and a ring-allocated table
    // stays valid for the frame that allocated it. Without this every draw
    // allocated a fresh table and copied eight descriptors into it to say what
    // the last one already said.
    if(texture_memo_frame_==gpu_->pending_fence() && SameBoundTextures()) {
      Commands().SetGraphicsRootDescriptorTable(NativeD3D12RootLayout::kPixelTextureTable,
                                                texture_memo_table_);
      bound_.textures_dirty=false;
      return;
    }
    const auto table=gpu_->AllocateViews(NativeD3D12RootLayout::kPixelTextures,index_);
    const auto increment=gpu_->views(index_).increment();
    for(uint32_t slot=0;slot<NativeD3D12RootLayout::kPixelTextures;++slot) {
      const D3D12_CPU_DESCRIPTOR_HANDLE at{table.cpu.ptr+static_cast<SIZE_T>(slot)*increment};
      auto* texture=bound_.textures[slot].get();
      if(texture) {
        gpu_->device()->CopyDescriptorsSimple(1,at,texture->view(),
                                              D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      } else {
        // An unbound slot still has to hold a descriptor: the table is bound
        // whole, and validation objects to a slot that was never written even
        // when the shader never samples it.
        gpu_->device()->CopyDescriptorsSimple(1,at,null_texture_view_,
                                              D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      }
    }
    Commands().SetGraphicsRootDescriptorTable(NativeD3D12RootLayout::kPixelTextureTable,table.gpu);
    for(uint32_t slot=0;slot<NativeD3D12RootLayout::kPixelTextures;++slot)
      texture_memo_[slot]=bound_.textures[slot].get();
    texture_memo_table_=table.gpu;
    texture_memo_frame_=gpu_->pending_fence();
    bound_.textures_dirty=false;
  }
  // Compared through get(), so a texture destroyed since the memo was taken
  // reads as a different slot rather than as a match on a dead pointer.
  bool SameBoundTextures() const {
    for(uint32_t slot=0;slot<NativeD3D12RootLayout::kPixelTextures;++slot)
      if(texture_memo_[slot]!=bound_.textures[slot].get()) return false;
    return true;
  }

  // A bound texture, held weakly. Expired means the caller destroyed it while
  // it was still bound, which is a slot to leave empty rather than a
  // descriptor to copy out of freed memory.
  struct BoundTexture {
    D3D12Texture* texture=nullptr;
    std::weak_ptr<void> alive;
    D3D12Texture* get() const { return alive.expired()?nullptr:texture; }
    void reset() { texture=nullptr; alive.reset(); }
    void set(D3D12Texture* value) {
      texture=value;
      if(value) alive=value->alive(); else alive.reset();
    }
  };
  struct Bound {
    D3D12Pipeline* pipeline=nullptr;
    BoundTexture textures[NativeD3D12RootLayout::kPixelTextures]{};
    D3D12Sampler* samplers[NativeD3D12RootLayout::kPixelSamplers]{};
    uint32_t render_targets=0;
    D3D12_RECT viewport_scissor{},scissor{};
    bool scissor_enabled=false;
    bool textures_dirty=false,samplers_dirty=false,blend_factor_set=false;
  };

  // One-frame memos of the last table bound; see FlushSamplers.
  D3D12Sampler* sampler_memo_[NativeD3D12RootLayout::kPixelSamplers]{};
  D3D12_GPU_DESCRIPTOR_HANDLE sampler_memo_table_{};
  uint64_t sampler_memo_frame_=0;
  D3D12Texture* texture_memo_[NativeD3D12RootLayout::kPixelTextures]{};
  D3D12_GPU_DESCRIPTOR_HANDLE texture_memo_table_{};
  uint64_t texture_memo_frame_=0;

  NativeD3D12Device* gpu_;
  ID3D12RootSignature* signature_;
  uint32_t index_=0;
  // Constant images staged and re-bound from a staged slice (SetConstantImage).
  std::atomic<uint64_t> constant_uploads_{0},constant_upload_reuses_{0};
  ID3D12GraphicsCommandList* commands_=nullptr;
  struct TransitionState {
    ComPtr<ID3D12Resource> resource;
    std::shared_ptr<D3D12_RESOURCE_STATES> global;
    D3D12_RESOURCE_STATES first,last;
  };
  std::unordered_map<D3D12_RESOURCE_STATES*,TransitionState> transitions_;
  Bound bound_;
  std::vector<Bound> stack_;

 public:
  D3D12_CPU_DESCRIPTOR_HANDLE null_texture_view_{};
};

class D3D12Completion final : public NativeBackendCompletion {
 public:
  D3D12Completion(ComPtr<ID3D12Fence> fence,uint64_t value) : fence_(std::move(fence)),value_(value) {}
  bool Complete() const override {
    const auto completed=fence_->GetCompletedValue();
    if(completed==UINT64_MAX) throw std::runtime_error("D3D12 completion fence device removed");
    return completed>=value_;
  }
 private:
  ComPtr<ID3D12Fence> fence_;
  uint64_t value_;
};

class D3D12Backend final : public NativeRenderBackend {
 public:
  explicit D3D12Backend(const NativeD3D12Options& options)
      : gpu_(options),
        signature_(CreateNativeD3D12RootSignature(*gpu_.device())),
        pipelines_(*gpu_.device(),*signature_.Get()),
        texture_views_(*gpu_.device(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,4096),
        render_target_views_(*gpu_.device(),D3D12_DESCRIPTOR_HEAP_TYPE_RTV,256),
        depth_views_(*gpu_.device(),D3D12_DESCRIPTOR_HEAP_TYPE_DSV,64) {
    for(uint32_t index=0;index<gpu_.recorders();++index)
      recorders_.push_back(std::make_unique<D3D12Recorder>(gpu_,*signature_.Get(),index));
    // One null descriptor, written once, for every texture slot a draw leaves
    // unbound. Creating it per draw would be pure waste on the hottest path.
    null_texture_=texture_views_.Allocate();
    D3D12_SHADER_RESOURCE_VIEW_DESC null{};
    null.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    null.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    null.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    null.Texture2D.MipLevels=1;
    gpu_.device()->CreateShaderResourceView(nullptr,&null,null_texture_);
    for(auto& recorder:recorders_) recorder->null_texture_view_=null_texture_;
    if(options.geometry_workers) {
      std::vector<NativeBackendRecorder*> workers;
      for(auto& recorder:recorders_) workers.push_back(recorder.get());
      parallel_=std::make_unique<NativeParallelRecorder>(std::move(workers),[this](bool reopen) {
        SubmitRecorded(); if(reopen) BeginRecorded();
      },options.geometry_minimum_draws);
      gpu_.before_resource_destroy=[this](const void* resource) {
        try { if(!parallel_failure_) parallel_->Forget(resource); }
        catch(...) { parallel_failure_=std::current_exception(); }
      };
    }
  }

  void ReleaseFrameLatency() {
    if(!frame_latency_) return;
    CloseHandle(frame_latency_);
    frame_latency_=nullptr;
  }
  ~D3D12Backend() override {
    gpu_.before_resource_destroy={};
    if(open_) { try { Submit(); } catch(...) {} }
    parallel_.reset();
    ReleaseFrameLatency();
    if(shared_fence_handle_) CloseHandle(shared_fence_handle_);
    // The swap chain and its buffers are released before this object's own
    // device member is destroyed, so nothing here waits for the GPU on our
    // behalf. Presenting work may still be in flight, and releasing a back
    // buffer it is reading takes the process down.
    try { gpu_.WaitIdle(); } catch(...) {}
  }

  std::string_view name() const override { return "d3d12"; }

  std::unique_ptr<NativeBackendBuffer> CreateBuffer(const NativeBackendBufferDesc& desc,
                                                    std::span<const uint8_t> initial) override {
    if(!desc.bytes) throw std::runtime_error("a zero-byte buffer cannot be created");
    TrackedResource tracked(gpu_);
    *tracked.state=D3D12_RESOURCE_STATE_COMMON;
    const D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT,D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                     D3D12_MEMORY_POOL_UNKNOWN,0,0};
    const D3D12_RESOURCE_DESC description{D3D12_RESOURCE_DIMENSION_BUFFER,0,desc.bytes,1,1,1,
                                          DXGI_FORMAT_UNKNOWN,{1,0},D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                          D3D12_RESOURCE_FLAG_NONE};
    Require(gpu_.device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,
                                                   *tracked.state,nullptr,IID_PPV_ARGS(&tracked.resource)),
            "buffer creation");
    auto buffer=std::make_unique<D3D12Buffer>(std::move(tracked),desc.bytes,desc.dynamic);
    buffers_created_.fetch_add(1,std::memory_order_relaxed);
    buffer_bytes_created_.fetch_add(desc.bytes,std::memory_order_relaxed);
    if(!initial.empty()) {
      if(initial.size()>desc.bytes)
        throw std::runtime_error("initial buffer contents are larger than the buffer");
      if(open_) recorders_.front()->UpdateBuffer(*buffer,0,initial);
      else pending_.push_back({buffer->alive(),buffer.get(),
                                std::vector<uint8_t>(initial.begin(),initial.end())});
    }
    return buffer;
  }

  NativeBackendSampler& CreateSampler(const NativeBackendSamplerDesc& desc) override {
    const auto native=SamplerDesc(desc);
    // Deduplicated on the description, so a caller that describes the same
    // sampler for every material does not fill the cache with copies of it.
    std::string key(reinterpret_cast<const char*>(&native),sizeof(native));
    auto found=samplers_.find(key);
    if(found==samplers_.end())
      found=samplers_.emplace(std::move(key),std::make_unique<D3D12Sampler>(native)).first;
    return *found->second;
  }

  std::unique_ptr<NativeBackendTexture> CreateTexture(const NativeBackendTextureDesc& desc,
                                                      std::span<const uint8_t> initial) override {
    TrackedResource tracked(gpu_);
    *tracked.state=D3D12_RESOURCE_STATE_COMMON;
    const D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT,D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                     D3D12_MEMORY_POOL_UNKNOWN,0,0};
    D3D12_RESOURCE_DESC description{};
    description.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width=desc.width;
    description.Height=desc.height;
    description.DepthOrArraySize=static_cast<UINT16>(desc.cube?6:(desc.array_size?desc.array_size:1));
    description.MipLevels=static_cast<UINT16>(desc.levels?desc.levels:1);
    description.Format=static_cast<DXGI_FORMAT>(desc.format);
    description.SampleDesc={1,0};
    RequireDevice(gpu_.device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,
                                                        *tracked.state,nullptr,IID_PPV_ARGS(&tracked.resource)),
                  "texture creation");
    const auto view=texture_views_.Allocate();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format=description.Format;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if(desc.cube) {
      srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURECUBE;
      srv.TextureCube.MipLevels=description.MipLevels;
    } else if(description.DepthOrArraySize>1) {
      srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
      srv.Texture2DArray.MipLevels=description.MipLevels;
      srv.Texture2DArray.ArraySize=description.DepthOrArraySize;
    } else {
      srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
      srv.Texture2D.MipLevels=description.MipLevels;
    }
    gpu_.device()->CreateShaderResourceView(tracked.resource.Get(),&srv,view);
    auto texture=std::make_unique<D3D12Texture>(std::move(tracked),desc.width,desc.height,view,
                                                texture_views_);
    textures_created_.fetch_add(1,std::memory_order_relaxed);
    texture_bytes_created_.fetch_add(initial.size(),std::memory_order_relaxed);
    // Staged on the next frame that opens, like buffer contents: there is no
    // command list to copy with until then.
    if(!initial.empty()) {
      std::vector<uint8_t> bytes(initial.begin(),initial.end());
      if(open_) UploadTexture(*texture,bytes);
      else pending_textures_.push_back({texture->alive(),texture.get(),std::move(bytes)});
    }
    return texture;
  }

  bool SupportsSamples(uint32_t format,uint32_t samples) override {
    if(samples==1) return true;
    if(samples!=2 && samples!=4) return false;
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{};
    levels.Format=static_cast<DXGI_FORMAT>(format);
    levels.SampleCount=samples;
    return SUCCEEDED(gpu_.device()->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                                                        &levels,sizeof(levels))) && levels.NumQualityLevels;
  }

  std::unique_ptr<NativeBackendRenderTarget> CreateRenderTarget(const NativeBackendTextureDesc& desc) override {
    TrackedResource tracked(gpu_);
    const D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT,D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                     D3D12_MEMORY_POOL_UNKNOWN,0,0};
    if(desc.sampled && desc.samples>1)
      throw std::runtime_error("a multisampled target cannot be sampled directly; resolve it into a texture");
    D3D12_RESOURCE_DESC description{};
    description.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width=desc.width;
    description.Height=desc.height;
    description.DepthOrArraySize=1;
    description.MipLevels=1;
    description.Format=static_cast<DXGI_FORMAT>(desc.format);
    description.SampleDesc={desc.samples?desc.samples:1,0};
    description.Flags=desc.depth?D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                                :D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    *tracked.state=desc.depth?D3D12_RESOURCE_STATE_DEPTH_WRITE:D3D12_RESOURCE_STATE_RENDER_TARGET;
    // A clear value must be declared up front or every clear is a slow path,
    // and it must match what is actually cleared or validation complains.
    D3D12_CLEAR_VALUE clear{};
    clear.Format=description.Format;
    if(desc.depth) clear.DepthStencil={1.0f,0};
    Require(gpu_.device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,
                                                   *tracked.state,&clear,IID_PPV_ARGS(&tracked.resource)),
            "render target creation");
    auto shared=std::make_shared<TrackedResource>(std::move(tracked));
    auto& pool=desc.depth?depth_views_:render_target_views_;
    const auto view=pool.Allocate();
    const bool multisampled=description.SampleDesc.Count>1;
    if(desc.depth) {
      D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
      dsv.Format=description.Format;
      dsv.ViewDimension=multisampled?D3D12_DSV_DIMENSION_TEXTURE2DMS:D3D12_DSV_DIMENSION_TEXTURE2D;
      gpu_.device()->CreateDepthStencilView(shared->resource.Get(),&dsv,view);
    } else {
      D3D12_RENDER_TARGET_VIEW_DESC rtv{};
      rtv.Format=description.Format;
      rtv.ViewDimension=multisampled?D3D12_RTV_DIMENSION_TEXTURE2DMS:D3D12_RTV_DIMENSION_TEXTURE2D;
      gpu_.device()->CreateRenderTargetView(shared->resource.Get(),&rtv,view);
    }
    std::unique_ptr<D3D12Texture> sampled;
    if(desc.sampled) {
      const auto srv_handle=texture_views_.Allocate();
      D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
      srv.Format=description.Format;
      srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
      srv.Texture2D.MipLevels=1;
      gpu_.device()->CreateShaderResourceView(shared->resource.Get(),&srv,srv_handle);
      // The same shared state, so a draw that samples this and a draw that
      // writes it transition one resource, in order.
      sampled=std::make_unique<D3D12Texture>(shared,desc.width,desc.height,srv_handle,texture_views_);
    }
    return std::make_unique<D3D12RenderTarget>(std::move(shared),desc.width,desc.height,desc.depth,
                                               view,pool,std::move(sampled));
  }

  std::unique_ptr<NativeBackendQuery> CreateQuery(NativeBackendQueryKind kind) override {
    if(kind!=NativeBackendQueryKind::Occlusion && kind!=NativeBackendQueryKind::Timestamp)
      throw std::runtime_error("D3D12 timestamp domains use the command queue frequency");
    const D3D12_QUERY_HEAP_DESC desc{kind==NativeBackendQueryKind::Timestamp?
      D3D12_QUERY_HEAP_TYPE_TIMESTAMP:D3D12_QUERY_HEAP_TYPE_OCCLUSION,1,0};
    ComPtr<ID3D12QueryHeap> heap;
    Require(gpu_.device()->CreateQueryHeap(&desc,IID_PPV_ARGS(&heap)),"query heap creation");
    // Its own readback buffer, created in COPY_DEST because that is the state
    // ResolveQueryData requires and a readback heap can never leave it.
    const D3D12_HEAP_PROPERTIES readback_heap{D3D12_HEAP_TYPE_READBACK,D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                              D3D12_MEMORY_POOL_UNKNOWN,0,0};
    const D3D12_RESOURCE_DESC readback_desc{D3D12_RESOURCE_DIMENSION_BUFFER,0,sizeof(uint64_t),1,1,1,
                                            DXGI_FORMAT_UNKNOWN,{1,0},D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                            D3D12_RESOURCE_FLAG_NONE};
    ComPtr<ID3D12Resource> readback;
    Require(gpu_.device()->CreateCommittedResource(&readback_heap,D3D12_HEAP_FLAG_NONE,&readback_desc,
                                                   D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
                                                   IID_PPV_ARGS(&readback)),"query readback creation");
    return std::make_unique<D3D12Query>(gpu_,std::move(heap),std::move(readback),kind);
  }

  uint64_t TimestampFrequency() const override {
    UINT64 frequency=0;
    Require(gpu_.queue()->GetTimestampFrequency(&frequency),"timestamp frequency query");
    return frequency;
  }
  std::unique_ptr<NativeBackendTimestamps> CreateTimestamps(uint32_t capacity) override {
    if(!capacity) throw std::runtime_error("an empty timestamp set cannot be created");
    const D3D12_QUERY_HEAP_DESC desc{D3D12_QUERY_HEAP_TYPE_TIMESTAMP,capacity,0};
    ComPtr<ID3D12QueryHeap> heap;
    Require(gpu_.device()->CreateQueryHeap(&desc,IID_PPV_ARGS(&heap)),"timestamp heap creation");
    const D3D12_HEAP_PROPERTIES readback_heap{D3D12_HEAP_TYPE_READBACK,D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                              D3D12_MEMORY_POOL_UNKNOWN,0,0};
    const D3D12_RESOURCE_DESC readback_desc{D3D12_RESOURCE_DIMENSION_BUFFER,0,uint64_t(capacity)*sizeof(uint64_t),
                                            1,1,1,DXGI_FORMAT_UNKNOWN,{1,0},D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                            D3D12_RESOURCE_FLAG_NONE};
    ComPtr<ID3D12Resource> readback;
    Require(gpu_.device()->CreateCommittedResource(&readback_heap,D3D12_HEAP_FLAG_NONE,&readback_desc,
                                                   D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
                                                   IID_PPV_ARGS(&readback)),"timestamp readback creation");
    return std::make_unique<D3D12Timestamps>(gpu_,std::move(heap),std::move(readback),capacity);
  }
  bool ReadTimestamps(NativeBackendTimestamps& set,uint32_t first,std::span<uint64_t> out) override {
    auto& concrete=static_cast<D3D12Timestamps&>(set);
    const auto count=static_cast<uint32_t>(out.size());
    concrete.Check(first,count);
    const auto fence=concrete.fence(first,count);
    if(!fence || gpu_.completed_fence()<fence) return false;
    void* mapped=nullptr;
    const D3D12_RANGE range{size_t(first)*sizeof(uint64_t),size_t(first+count)*sizeof(uint64_t)};
    if(FAILED(concrete.readback()->Map(0,&range,&mapped))) return false;
    std::memcpy(out.data(),static_cast<const uint8_t*>(mapped)+range.Begin,size_t(count)*sizeof(uint64_t));
    const D3D12_RANGE none{0,0};
    concrete.readback()->Unmap(0,&none);
    return true;
  }
  bool ReadQuery(NativeBackendQuery& query, std::span<uint8_t> result) override {
    auto& concrete=static_cast<D3D12Query&>(query);
    if(result.size()<sizeof(uint64_t))
      throw std::runtime_error("a backend query result needs 8 bytes");
    // Never blocks, as the interface promises. Not yet resolved, or resolved
    // by a frame the GPU has not reached, both mean "ask again later".
    if(!concrete.fence()||gpu_.completed_fence()<concrete.fence()) return false;
    void* mapped=nullptr;
    const D3D12_RANGE whole{0,sizeof(uint64_t)};
    if(FAILED(concrete.readback()->Map(0,&whole,&mapped))) return false;
    std::memcpy(result.data(),mapped,sizeof(uint64_t));
    const D3D12_RANGE none{0,0};
    concrete.readback()->Unmap(0,&none);
    return true;
  }

  NativeBackendPipeline& CreatePipeline(const NativeBackendPipelineDesc& desc) override {
    NativeD3D12PipelineCache::Request request{};
    request.key.vertex_shader=desc.vertex_id;
    request.key.pixel_shader=desc.pixel_id;
    request.key.input_layout=desc.input_layout_id;
    request.key.blend=desc.state[0];
    request.key.depth=desc.state[1];
    request.key.raster=desc.state[2];
    request.key.alpha=desc.state[3];
    request.key.write_mask=desc.state[4];
    request.key.topology=TopologyType(desc.topology);
    request.key.render_targets=desc.render_targets;
    for(size_t index=0;index<desc.rtv_format.size();++index)
      request.key.rtv_format[index]=desc.rtv_format[index];
    request.key.dsv_format=desc.dsv_format;
    request.key.sample_count=desc.sample_count;
    // A warm pipeline lookup must not reflect shader bytecode, decode render
    // state, or allocate an input-layout vector again for every geometry draw.
    // Shader/layout identities are stable by the backend interface contract.
    const uint32_t primitive=uint32_t(Topology(desc.topology));
    // Cache hits only borrow this stack key. Own the bytes on a miss, avoiding
    // string allocation and growth for every routine material transition.
    std::array<char,sizeof(request.key)+sizeof(primitive)> key_bytes;
    std::memcpy(key_bytes.data(),&request.key,sizeof(request.key));
    std::memcpy(key_bytes.data()+sizeof(request.key),&primitive,sizeof(primitive));
    const std::string_view key(key_bytes.data(),key_bytes.size());
    if(const auto found=wrappers_.find(key);found!=wrappers_.end()) {
      ++wrapper_hits_;
      return *found->second;
    }
    // Decoded once here rather than twice: the pipeline cache builds its own
    // state from these words, and the wrapper below needs the two blend-factor
    // answers out of the same decode.
    const auto decoded=DecodeNativeRenderState(desc.state);
    ValidateAgainstRootLayout(desc.vertex,false,"vertex shader "+std::to_string(desc.vertex_id));
    ValidateAgainstRootLayout(desc.pixel,true,"pixel shader "+std::to_string(desc.pixel_id));

    std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
    elements.reserve(desc.input_layout.size());
    for(const auto& element:desc.input_layout)
      elements.push_back({element.semantic,element.semantic_index,
                          static_cast<DXGI_FORMAT>(element.format),element.slot,element.offset,
                          element.per_instance?D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                                              :D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                          element.step_rate});

    request.vertex={desc.vertex.data(),desc.vertex.size()};
    request.pixel={desc.pixel.data(),desc.pixel.size()};
    request.input_layout=elements;
    request.state=desc.state;

    auto& state=pipelines_.Get(request);
    // The wrapper is keyed the same way as the pipeline, so repeated calls with
    // the same description hand back the same object rather than leaking one
    // wrapper per call for the same underlying pipeline.
    auto found=wrappers_.find(key);
    if(found==wrappers_.end())
      found=wrappers_.emplace(std::string(key),
                              std::make_unique<D3D12Pipeline>(state,Topology(desc.topology),
                                                              decoded.requires_blend_factor,
                                                              decoded.replicate_blend_alpha)).first;
    return *found->second;
  }

  NativeBackendRecorder& Recorder(uint32_t index) override {
    if(parallel_failure_) std::rethrow_exception(parallel_failure_);
    if(!open_) throw std::runtime_error("the D3D12 backend has no frame open; call BeginFrame first");
    if(parallel_) {
      if(index) throw std::runtime_error("the draw-packet recorder is owned by one producer");
      return *parallel_;
    }
    if(index>=recorders_.size())
      throw std::runtime_error("recorder "+std::to_string(index)+" does not exist; this backend has "+
                               std::to_string(recorders_.size()));
    return *recorders_[index];
  }
  uint32_t RecorderCount() const override { return parallel_?1:static_cast<uint32_t>(recorders_.size()); }
  // This capability describes caller-owned recorders. Packet mode exposes one
  // producer; its internal worker concurrency is reported in Statistics().
  bool SupportsParallelRecording() const override { return !parallel_ && recorders_.size()>1; }

  void Submit() override {
    if(parallel_failure_) std::rethrow_exception(parallel_failure_);
    if(parallel_) parallel_->Flush(false); else SubmitRecorded();
  }
  void SubmitRecorded() {
    if(!open_) throw std::runtime_error("Submit with no frame open");
    // A flip-model back buffer must be in PRESENT state when it is presented,
    // and only the open command list can put it there. Doing it here rather
    // than in Present keeps it off a second submission, and doing it for every
    // buffer rather than the current one costs nothing: Transition is a no-op
    // for a buffer already in that state.
    for(auto& buffer:back_buffers_)
      recorders_.front()->Transition(buffer->tracked(),D3D12_RESOURCE_STATE_PRESENT);
    for(auto& recorder:recorders_) { recorder->ResolveTransitions(); recorder->End(); }
    gpu_.EndFrame();
    open_=false;
  }

  std::shared_ptr<NativeBackendCompletion> MarkCompletion() override {
    if(open_) throw std::runtime_error("D3D12 completion requires submitted command lists");
    if(!completion_fence_)
      Require(gpu_.device()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&completion_fence_)),
              "completion fence creation");
    if(completion_value_==UINT64_MAX-1) throw std::runtime_error("D3D12 completion fence exhausted");
    const auto value=completion_value_+1;
    auto completion=std::make_shared<D3D12Completion>(completion_fence_,value);
    Require(gpu_.queue()->Signal(completion_fence_.Get(),value),"completion fence signal");
    completion_value_=value;
    return completion;
  }

  void BeginFrame() override {
    if(parallel_failure_) std::rethrow_exception(parallel_failure_);
    BeginRecorded(); if(parallel_) parallel_->Reset();
  }
  void BeginRecorded() {
    if(open_) throw std::runtime_error("a D3D12 frame is already open");
    gpu_.BeginFrame();
    for(uint32_t index=0;index<recorders_.size();++index)
      recorders_[index]->Begin(*gpu_.commands(index));
    open_=true;
    // Buffer contents supplied at creation are staged on the first frame that
    // opens: there is no command list to copy them with until then, and a
    // backend that quietly dropped them would produce an empty mesh.
    // Staged on recorder 0: these are one-off creation uploads, and putting
    // them anywhere else would make the frame's first list depend on which
    // thread happened to create a resource.
    // Skipped, not dropped silently in the dark: a resource destroyed before
    // its contents were staged never had them, and uploading into freed memory
    // is worse than an empty mesh.
    for(auto& upload:pending_)
      if(!upload.alive.expired()) recorders_.front()->UpdateBuffer(*upload.buffer,0,upload.bytes);
    pending_.clear();
    for(auto& upload:pending_textures_)
      if(!upload.alive.expired()) UploadTexture(*upload.texture,upload.bytes);
    pending_textures_.clear();
  }

  std::vector<uint8_t> ReadTexture(NativeBackendTexture& texture) override {
    if(open_) throw std::runtime_error("ReadTexture cannot run inside an open frame");
    auto& concrete=static_cast<D3D12Texture&>(texture);
    return ReadTracked(concrete.tracked());
  }
  std::vector<uint8_t> ReadRenderTarget(NativeBackendRenderTarget& target) override {
    if(open_) throw std::runtime_error("ReadRenderTarget cannot run inside an open frame");
    auto& concrete=static_cast<D3D12RenderTarget&>(target);
    return ReadTracked(concrete.tracked());
  }
  // The top level of subresource 0, copied into a readback buffer and waited
  // for. One body for both handles: which wrapper names the resource makes no
  // difference to how it is read.
  std::vector<uint8_t> ReadTracked(TrackedResource& tracked) {
    const auto description=tracked.resource->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 total=0,row_bytes=0;
    UINT rows=0;
    gpu_.device()->GetCopyableFootprints(&description,0,1,0,&footprint,&rows,&row_bytes,&total);

    const D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_READBACK,D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                     D3D12_MEMORY_POOL_UNKNOWN,0,0};
    const D3D12_RESOURCE_DESC buffer{D3D12_RESOURCE_DIMENSION_BUFFER,0,total,1,1,1,
                                     DXGI_FORMAT_UNKNOWN,{1,0},D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                     D3D12_RESOURCE_FLAG_NONE};
    ComPtr<ID3D12Resource> readback;
    Require(gpu_.device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,
                                                   D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
                                                   IID_PPV_ARGS(&readback)),"readback buffer creation");

    BeginFrame();
    recorders_.front()->Transition(tracked,D3D12_RESOURCE_STATE_COPY_SOURCE);
    const D3D12_TEXTURE_COPY_LOCATION to{readback.Get(),
                                         D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,{footprint}};
    D3D12_TEXTURE_COPY_LOCATION from{};
    from.pResource=tracked.resource.Get();
    from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.SubresourceIndex=0;
    gpu_.commands()->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    Submit();
    gpu_.WaitIdle();

    void* mapped=nullptr;
    const D3D12_RANGE whole{0,static_cast<SIZE_T>(total)};
    Require(readback->Map(0,&whole,&mapped),"readback map");
    // Rows arrive padded to the copy alignment; hand back tightly packed bytes
    // so a caller comparing two backends is not comparing padding.
    std::vector<uint8_t> pixels(static_cast<size_t>(row_bytes)*rows);
    for(UINT row=0;row<rows;++row)
      std::memcpy(pixels.data()+static_cast<size_t>(row)*row_bytes,
                  static_cast<const uint8_t*>(mapped)+static_cast<size_t>(row)*footprint.Footprint.RowPitch,
                  static_cast<size_t>(row_bytes));
    const D3D12_RANGE none{0,0};
    readback->Unmap(0,&none);
    return pixels;
  }

  std::unique_ptr<NativeBackendTexture> OpenSharedTexture(void* handle,
                                                          const NativeBackendTextureDesc& desc) override {
    if(!handle) return {};
    TrackedResource tracked(gpu_);
    // A surface another API owns and may still be writing. It arrives in
    // COMMON, which is the only state a shared resource can be handed over in.
    *tracked.state=D3D12_RESOURCE_STATE_COMMON;
    if(FAILED(gpu_.device()->OpenSharedHandle(handle,IID_PPV_ARGS(&tracked.resource)))) return {};
    const auto view=texture_views_.Allocate();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format=static_cast<DXGI_FORMAT>(desc.format);
    srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels=1;
    gpu_.device()->CreateShaderResourceView(tracked.resource.Get(),&srv,view);
    return std::make_unique<D3D12Texture>(std::move(tracked),desc.width,desc.height,view,
                                          texture_views_);
  }

  std::unique_ptr<NativeBackendSharedSurface> CreateSharedSurface(
      const NativeBackendTextureDesc& desc) override {
    TrackedResource tracked(gpu_);
    // COMMON, because that is the state a shared resource has to be in for
    // another API to pick it up, and where CopyToShared leaves it.
    *tracked.state=D3D12_RESOURCE_STATE_COMMON;
    const D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT,D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                     D3D12_MEMORY_POOL_UNKNOWN,0,0};
    D3D12_RESOURCE_DESC description{};
    description.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width=desc.width;
    description.Height=desc.height;
    description.DepthOrArraySize=1;
    description.MipLevels=1;
    description.Format=static_cast<DXGI_FORMAT>(desc.format);
    description.SampleDesc={1,0};
    description.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if(FAILED(gpu_.device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&description,
                                                     *tracked.state,nullptr,
                                                     IID_PPV_ARGS(&tracked.resource))))
      return nullptr;
    void* texture_handle=nullptr;
    if(FAILED(gpu_.device()->CreateSharedHandle(tracked.resource.Get(),nullptr,GENERIC_ALL,
                                                nullptr,&texture_handle)))
      return nullptr;
    ComPtr<ID3D12Fence> fence;
    void* fence_handle=nullptr;
    if(FAILED(gpu_.device()->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence))) ||
       FAILED(gpu_.device()->CreateSharedHandle(fence.Get(),nullptr,GENERIC_ALL,nullptr,&fence_handle))) {
      CloseHandle(texture_handle);
      return nullptr;
    }
    return std::make_unique<D3D12SharedSurface>(
      std::make_shared<TrackedResource>(std::move(tracked)),std::move(fence),
      texture_handle,fence_handle,desc.width,desc.height,desc.format);
  }

  uint64_t SignalShared(NativeBackendSharedSurface& surface) override {
    auto& concrete=static_cast<D3D12SharedSurface&>(surface);
    // A queue signal, so it lands after everything already submitted - which
    // is why this has to be called after Submit and not inside the frame.
    //
    // The value is only committed once the signal is accepted. Committing it
    // first and failing would leave the consumer waiting on the GPU for a
    // value that is never going to arrive, which is not an error anywhere - it
    // is the window quietly never drawing again.
    const auto value=concrete.value()+1;
    if(FAILED(gpu_.queue()->Signal(concrete.fence(),value)))
      throw std::runtime_error("D3D12 shared surface signal failed");
    return concrete.Advance();
  }

  bool WaitSharedFence(void* handle, uint64_t value) override {
    if(!handle) return false;
    if(!shared_fence_ || !CompareObjectHandles(shared_fence_handle_,handle)) {
      ComPtr<ID3D12Fence> imported;
      if(FAILED(gpu_.device()->OpenSharedHandle(handle,IID_PPV_ARGS(&imported)))) return false;
      HANDLE retained=nullptr;
      if(!DuplicateHandle(GetCurrentProcess(),handle,GetCurrentProcess(),&retained,0,FALSE,DUPLICATE_SAME_ACCESS))
        return false;
      if(shared_fence_handle_) CloseHandle(shared_fence_handle_);
      shared_fence_handle_=retained;
      shared_fence_=std::move(imported);
    }
    // A queue-side wait, not a CPU one: the GPU stalls until the producer has
    // signalled, and this thread carries on recording. Remembered because a
    // wait that is never satisfied is indistinguishable from any other hang
    // once the device is gone, and this is the one number that separates them.
    shared_wait_value_=value;
    return SUCCEEDED(gpu_.queue()->Wait(shared_fence_.Get(),value));
  }

  std::vector<std::string> DrainValidationMessages() override { return gpu_.DrainValidationErrors(); }

  NativeBackendStatistics Statistics() const override {
    NativeBackendStatistics out;
    out.frames=gpu_.frames_submitted();
    if(parallel_) {
      const auto geometry=parallel_->statistics();
      out.geometry_draws=geometry.draws; out.geometry_batches=geometry.batches;
      out.geometry_worker_mask=geometry.worker_mask; out.geometry_max_concurrent=geometry.max_concurrent;
      out.geometry_record_ns=geometry.record_ns; out.geometry_wait_ns=geometry.wait_ns;
      out.geometry_serial_draws=geometry.serial_draws; out.geometry_serial_flushes=geometry.serial_flushes;
      out.geometry_instanced_draws=geometry.instanced_draws; out.geometry_folded_draws=geometry.folded_draws;
      out.geometry_transient_appends=geometry.transient_appends;
      out.geometry_world_constant_reuses=geometry.world_constant_reuses;
      out.geometry_constant_snapshot_bytes=geometry.constant_snapshot_bytes;
      out.geometry_constant_interned=geometry.constant_interned;
      out.geometry_constant_interned_bytes=geometry.constant_interned_bytes;
      out.geometry_streamed_jobs=geometry.streamed_jobs;
      for(const auto& recorder:recorders_) {
        out.geometry_constant_uploads+=recorder->constant_uploads();
        out.geometry_constant_upload_reuses+=recorder->constant_upload_reuses();
      }
    }
    out.upload_stalls=gpu_.upload_stalls();
    out.descriptor_stalls=gpu_.descriptor_stalls();
    out.pipelines=pipelines_.size();
    out.pipeline_hits=pipelines_.hits()+wrapper_hits_;
    out.pipeline_misses=pipelines_.misses();
    out.sampler_tables=gpu_.samplers().tables();
    out.sampler_hits=gpu_.samplers().hits();
    out.sampler_misses=gpu_.samplers().misses();
    out.sampler_evictions=gpu_.samplers().evictions();
    out.retiring=gpu_.retiring();
    out.buffers_created=buffers_created_.load(std::memory_order_relaxed);
    out.buffer_bytes_created=buffer_bytes_created_.load(std::memory_order_relaxed);
    out.textures_created=textures_created_.load(std::memory_order_relaxed);
    out.texture_bytes_created=texture_bytes_created_.load(std::memory_order_relaxed);
    out.frame_waits=gpu_.frame_waits();
    out.frame_wait_ns=gpu_.frame_wait_ns();
    return out;
  }

  void AttachWindow(void* window, uint32_t width, uint32_t height) override {
    if(open_) throw std::runtime_error("a window cannot be attached inside an open frame");
    if(!window) throw std::runtime_error("AttachWindow needs a window");
    // Anything already submitted may still be reading a buffer we are about to
    // release, so the queue has to drain before the old chain goes.
    gpu_.WaitIdle();
    back_buffers_.clear();
    ReleaseFrameLatency();
    swap_chain_.Reset();

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width=width;
    desc.Height=height;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc={1,0};
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    // Flip-discard is the only model worth having: the older ones copy the
    // back buffer through the desktop compositor every frame.
    desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.BufferCount=kBackBuffers;
    desc.Scaling=DXGI_SCALING_STRETCH;
    desc.AlphaMode=DXGI_ALPHA_MODE_UNSPECIFIED;
    // Waitable, so vsync throttles by making this thread sleep on an event
    // rather than by blocking inside Present. A blocking Present holds the
    // queue, and this process has a second D3D12 device drawing the scene on
    // the same adapter: throttling the window that way throttled the game with
    // it, to a sixth of its frame rate. The wait belongs before the frame, on
    // a handle, which is what this flag buys.
    desc.Flags=DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    ComPtr<IDXGIFactory5> modern_factory;
    BOOL supported=FALSE;
    allow_tearing_=SUCCEEDED(gpu_.factory()->QueryInterface(IID_PPV_ARGS(&modern_factory))) &&
      SUCCEEDED(modern_factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
        &supported,sizeof(supported))) && supported;
    if(allow_tearing_) desc.Flags|=DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    ComPtr<IDXGISwapChain1> chain;
    Require(gpu_.factory()->CreateSwapChainForHwnd(gpu_.queue(),static_cast<HWND>(window),&desc,
                                                   nullptr,nullptr,&chain),"swap chain creation");
    // Alt+Enter belongs to the game's own display handling, not to DXGI.
    gpu_.factory()->MakeWindowAssociation(static_cast<HWND>(window),DXGI_MWA_NO_ALT_ENTER);
    Require(chain.As(&swap_chain_),"swap chain interface");
    ReleaseFrameLatency();
    // One frame of latency: the shallowest queue that still keeps the GPU fed,
    // and the one that keeps the window's present from running ahead of the
    // scene it is showing.
    swap_chain_->SetMaximumFrameLatency(1);
    frame_latency_=swap_chain_->GetFrameLatencyWaitableObject();

    for(uint32_t index=0;index<kBackBuffers;++index) {
      TrackedResource tracked;
      *tracked.state=D3D12_RESOURCE_STATE_PRESENT;
      Require(swap_chain_->GetBuffer(index,IID_PPV_ARGS(&tracked.resource)),"swap chain buffer");
      const auto view=render_target_views_.Allocate();
      D3D12_RENDER_TARGET_VIEW_DESC rtv{};
      rtv.Format=desc.Format;
      rtv.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
      gpu_.device()->CreateRenderTargetView(tracked.resource.Get(),&rtv,view);
      back_buffers_.push_back(std::make_unique<D3D12RenderTarget>(std::move(tracked),width,height,
                                                                  false,view,render_target_views_));
    }
  }

  NativeBackendRenderTarget* BackBuffer() override {
    if(!swap_chain_) return nullptr;
    return back_buffers_[swap_chain_->GetCurrentBackBufferIndex()].get();
  }

  // Why the device went away, in words. DXGI_ERROR_DEVICE_REMOVED on its own
  // says only that something the GPU was asked to do was fatal; the reason
  // separates "this process did something invalid" from "the driver reset".
  // Like Require, but says why the device went away when that is what
  // happened. A bare 0x887a0005 on a resource creation tells you the device is
  // gone and nothing about what killed it, which is the wrong end of a hang to
  // start from.
  void RequireDevice(HRESULT result, const char* what) {
    if(SUCCEEDED(result)) return;
    if(result==DXGI_ERROR_DEVICE_REMOVED || result==DXGI_ERROR_DEVICE_RESET)
      throw std::runtime_error(std::string("D3D12 ")+what+" failed: the device was removed - "+RemovedReason());
    Require(result,what);
  }
  // What the device was waiting for when it died, when it was waiting for a
  // producer on another device. A completed value below the awaited one says
  // the producer never signalled, which is a different bug from anything this
  // device's own command lists did.
  std::optional<NativeBackendPresentationStatistics> PresentationStatistics() const override {
    if(!swap_chain_) return std::nullopt;
    NativeBackendPresentationStatistics result;
    DXGI_FRAME_STATISTICS stats{};
    result.result=swap_chain_->GetFrameStatistics(&stats);
    result.last_present_result=swap_chain_->GetLastPresentCount(&result.last_present_count);
    if(SUCCEEDED(result.result)) {
      result.present_count=stats.PresentCount;
      result.present_refresh_count=stats.PresentRefreshCount;
      result.sync_refresh_count=stats.SyncRefreshCount;
      result.sync_qpc=stats.SyncQPCTime.QuadPart;
    }
    return result;
  }
  std::string SharedWaitState() {
    if(!shared_fence_ || !shared_wait_value_) return {};
    return ", awaiting shared fence "+std::to_string(shared_wait_value_)+
           " which has reached "+std::to_string(shared_fence_->GetCompletedValue());
  }
  const char* RemovedReason() {
    switch(gpu_.device()->GetDeviceRemovedReason()) {
      case DXGI_ERROR_DEVICE_HUNG: return "the GPU hung on this device's own work";
      case DXGI_ERROR_DEVICE_RESET: return "the device was reset, usually by another process hanging the GPU";
      case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return "the driver reported an internal error";
      case DXGI_ERROR_INVALID_CALL: return "an invalid call was made on this device";
      case DXGI_ERROR_DEVICE_REMOVED: return "the adapter was removed or its driver was updated";
      case S_OK: return "the device reports no removal reason";
      default: return "an unrecognised removal reason";
    }
  }
  void Present(bool vsync) override {
    if(!swap_chain_) throw std::runtime_error("Present with no window attached");
    if(open_) throw std::runtime_error("Present inside an open frame; submit it first");
    // Throttle once per presentation, never once per command submission:
    // snapshot copies and readbacks also open frames without presenting.
    if(frame_latency_ && WaitForSingleObject(frame_latency_,1000)==WAIT_TIMEOUT)
      ++present_waits_timed_out_;
    // The host uses windowed/borderless presentation, never DXGI exclusive
    // fullscreen. Explicit VSync-off must permit tearing where supported;
    // these flags are also required for the driver's variable-refresh path.
    // VSync-on always retains interval 1 with no tearing flag.
    const auto result=swap_chain_->Present(vsync?1:0,!vsync && allow_tearing_?DXGI_PRESENT_ALLOW_TEARING:0);
    if(result==DXGI_ERROR_DEVICE_REMOVED || result==DXGI_ERROR_DEVICE_RESET)
      throw std::runtime_error(std::string("D3D12 present failed: the device was removed - ")+
                               RemovedReason()+SharedWaitState());
    Require(result,"present");
  }

  NativeD3D12Device& gpu() { return gpu_; }
  const NativeD3D12PipelineCache& pipelines() const { return pipelines_; }

 private:
  // A texture upload is not a buffer copy: rows land on a 256-byte pitch that
  // has nothing to do with the source's packed rows, so each level's copy is
  // described by a footprint the device computes and its rows are written one
  // at a time. The source is expected tightly packed, smallest stride, in
  // subresource order, which is how every mipped and cube asset in this game
  // arrives from the shared DDS decode.
  void UploadTexture(D3D12Texture& texture, const std::vector<uint8_t>& bytes) {
    const auto description=texture.tracked().resource->GetDesc();
    // Every subresource, not only the first face's mip chain: a cube map is
    // six of them and uploading one would leave five undefined.
    const UINT levels=description.MipLevels?description.MipLevels:1;
    const UINT subresources=levels*(description.DepthOrArraySize?description.DepthOrArraySize:1);
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(subresources);
    std::vector<UINT64> row_bytes(subresources);
    std::vector<UINT> rows(subresources);
    UINT64 total=0;
    gpu_.device()->GetCopyableFootprints(&description,0,subresources,0,footprints.data(),rows.data(),
                                         row_bytes.data(),&total);
    size_t needed=0;
    for(UINT index=0;index<subresources;++index)
      needed+=static_cast<size_t>(row_bytes[index])*rows[index];
    if(bytes.size()<needed)
      throw std::runtime_error("initial texture contents are "+std::to_string(bytes.size())+
                               " bytes but its "+std::to_string(subresources)+
                               " subresources need "+std::to_string(needed));

    const auto upload=gpu_.Allocate(total,D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    recorders_.front()->Transition(texture.tracked(),D3D12_RESOURCE_STATE_COPY_DEST);
    size_t source=0;
    for(UINT index=0;index<subresources;++index) {
      auto& footprint=footprints[index];
      for(UINT row=0;row<rows[index];++row)
        std::memcpy(upload.cpu+footprint.Offset+static_cast<size_t>(row)*footprint.Footprint.RowPitch,
                    bytes.data()+source+static_cast<size_t>(row)*row_bytes[index],
                    static_cast<size_t>(row_bytes[index]));
      source+=static_cast<size_t>(row_bytes[index])*rows[index];
      // GetCopyableFootprints laid the levels out from offset zero; the ring
      // put the block somewhere else, so every level's offset moves with it.
      footprint.Offset+=upload.offset;
      const D3D12_TEXTURE_COPY_LOCATION from{upload.resource,
                                             D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,{footprint}};
      D3D12_TEXTURE_COPY_LOCATION to{};
      to.pResource=texture.tracked().resource.Get();
      to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      to.SubresourceIndex=index;
      gpu_.commands()->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    }
  }

  // Weak, because the caller owns these and may destroy one before the next
  // frame opens. A raw pointer here is read as a live object exactly once and
  // hangs the GPU.
  struct PendingUpload { std::weak_ptr<void> alive; D3D12Buffer* buffer; std::vector<uint8_t> bytes; };
  struct PendingTexture { std::weak_ptr<void> alive; D3D12Texture* texture; std::vector<uint8_t> bytes; };

  NativeD3D12Device gpu_;
  ComPtr<ID3D12Fence> completion_fence_;
  uint64_t completion_value_=0;
  ComPtr<ID3D12RootSignature> signature_;
  NativeD3D12PipelineCache pipelines_;
  uint64_t wrapper_hits_=0;
  // Resource creations, for attributing a slow frame (edf_native_frame_times).
  std::atomic<uint64_t> buffers_created_{0},buffer_bytes_created_{0},textures_created_{0},texture_bytes_created_{0};
  NativeD3D12CpuDescriptorHeap texture_views_,render_target_views_,depth_views_;
  std::vector<std::unique_ptr<D3D12Recorder>> recorders_;
  std::map<std::string,std::unique_ptr<D3D12Pipeline>,std::less<>> wrappers_;
  // Three, to match the frames in flight: the device will not reuse a frame
  // slot until its fence has passed, which is also what keeps us off a buffer
  // the display is still showing.
  static constexpr uint32_t kBackBuffers=3;
  ComPtr<ID3D12Fence> shared_fence_;
  uint64_t shared_wait_value_=0;
  // Signalled when the swap chain is ready for another frame. Waited on before
  // recording rather than inside Present; see AttachWindow.
  HANDLE frame_latency_=nullptr;
  bool allow_tearing_=false;
  uint64_t present_waits_timed_out_=0;
  void* shared_fence_handle_=nullptr;
  ComPtr<IDXGISwapChain3> swap_chain_;
  std::vector<std::unique_ptr<D3D12RenderTarget>> back_buffers_;
  std::vector<PendingUpload> pending_;
  std::vector<PendingTexture> pending_textures_;
  std::map<std::string,std::unique_ptr<D3D12Sampler>> samplers_;
  D3D12_CPU_DESCRIPTOR_HANDLE null_texture_{};
  bool open_=false;
  std::exception_ptr parallel_failure_;
  std::unique_ptr<NativeParallelRecorder> parallel_;
};
}  // namespace

std::unique_ptr<NativeRenderBackend> CreateNativeD3D12Backend(const NativeD3D12Options& options) {
  auto configured=options;
  if(configured.geometry_workers) {
    if(configured.geometry_workers>32) throw std::runtime_error("geometry worker limit is 32");
    configured.recorders=configured.geometry_workers+1;
  }
  return std::make_unique<D3D12Backend>(configured);
}

namespace {
std::atomic<uint32_t>& UploadMegabytes() {
  static std::atomic<uint32_t> megabytes{0};
  return megabytes;
}
std::atomic<bool>& DebugLayer() {
  static std::atomic<bool> enabled{false};
  return enabled;
}
NativeD3D12Options RegistryOptions() {
  NativeD3D12Options options;
  if(const auto megabytes=UploadMegabytes().load(std::memory_order_relaxed))
    options.upload_bytes=uint64_t(megabytes)<<20;
  options.debug_layer=DebugLayer().load(std::memory_order_relaxed);
  return options;
}
}  // namespace
void SetNativeD3D12UploadMegabytes(uint32_t megabytes) {
  UploadMegabytes().store(megabytes,std::memory_order_relaxed);
}
void SetNativeD3D12DebugLayer(bool enabled) {
  DebugLayer().store(enabled,std::memory_order_relaxed);
}
std::unique_ptr<NativeRenderBackend> CreateNativeD3D12SceneBackend(bool warp,uint32_t workers) {
  auto options=RegistryOptions();
  options.prefer_warp=warp; options.debug_layer|=warp;
  if(workers>32) throw std::runtime_error("geometry worker limit is 32");
  // Recorder zero handles the entire UI/serial prefix. Adding geometry workers
  // must not divide that recorder's descriptor budget by the worker count.
  // Keep the default 64K budget per recorder, subject to the 1M heap limit.
  // Descriptors remain fence-retired; this provides room for overlapping frames.
  options.view_descriptors=(std::min)(1u<<20,options.view_descriptors*(workers?workers+1:1));
  options.geometry_workers=workers;
  return CreateNativeD3D12Backend(options);
}
void RegisterNativeD3D12Backend() {
  static bool registered=false;
  if(registered) return;
  RegisterNativeRenderBackend("d3d12",[]() -> std::unique_ptr<NativeRenderBackend> {
    return CreateNativeD3D12Backend(RegistryOptions());
  });
  // WARP as its own selectable backend, not a hidden fallback. It is how a
  // rendering difference gets attributed: if it reproduces on the software
  // rasteriser the bug is ours, and if it does not it is the driver's. It also
  // lets the test suite drive the whole registry path on a machine with no
  // D3D12 hardware.
  RegisterNativeRenderBackend("d3d12-warp",[]() -> std::unique_ptr<NativeRenderBackend> {
    auto options=RegistryOptions();
    options.prefer_warp=true;
    // Always on for WARP: it exists to attribute a difference, and a run
    // without validation cannot do that.
    options.debug_layer=true;
    return CreateNativeD3D12Backend(options);
  });
  registered=true;
}
}  // namespace edf::native

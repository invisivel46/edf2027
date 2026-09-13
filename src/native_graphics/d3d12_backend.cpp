#include "d3d12_backend.h"
#include "d3d12_pipeline.h"
#include <algorithm>
#include <cstring>
#include <map>
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

// Every resource carries the state it is in. D3D11 tracked this for us and
// inserted barriers silently; here a wrong answer is a silent corruption on
// some hardware and a validation error on the rest, so the state lives with
// the resource and only the recorder is allowed to change it.
struct TrackedResource {
  ComPtr<ID3D12Resource> resource;
  D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;
};

class D3D12Buffer final : public NativeBackendBuffer {
 public:
  D3D12Buffer(TrackedResource tracked, size_t bytes, bool dynamic)
      : tracked_(std::move(tracked)),bytes_(bytes),dynamic_(dynamic) {}
  size_t bytes() const override { return bytes_; }
  bool dynamic() const { return dynamic_; }
  TrackedResource& tracked() { return tracked_; }
  D3D12_GPU_VIRTUAL_ADDRESS address() const { return tracked_.resource->GetGPUVirtualAddress(); }
 private:
  TrackedResource tracked_;
  size_t bytes_;
  bool dynamic_=false;
};

class D3D12Texture final : public NativeBackendTexture {
 public:
  D3D12Texture(TrackedResource tracked, uint32_t width, uint32_t height,
               D3D12_CPU_DESCRIPTOR_HANDLE view, NativeD3D12CpuDescriptorHeap& heap)
      : tracked_(std::move(tracked)),width_(width),height_(height),view_(view),heap_(&heap) {}
  ~D3D12Texture() override { if(heap_) heap_->Free(view_); }
  uint32_t width() const override { return width_; }
  uint32_t height() const override { return height_; }
  TrackedResource& tracked() { return tracked_; }
  D3D12_CPU_DESCRIPTOR_HANDLE view() const { return view_; }
 private:
  TrackedResource tracked_;
  uint32_t width_,height_;
  D3D12_CPU_DESCRIPTOR_HANDLE view_;
  NativeD3D12CpuDescriptorHeap* heap_;
};

class D3D12RenderTarget final : public NativeBackendRenderTarget {
 public:
  D3D12RenderTarget(TrackedResource tracked, uint32_t width, uint32_t height, bool depth,
                    D3D12_CPU_DESCRIPTOR_HANDLE view, NativeD3D12CpuDescriptorHeap& heap)
      : tracked_(std::move(tracked)),width_(width),height_(height),depth_(depth),
        view_(view),heap_(&heap) {}
  ~D3D12RenderTarget() override { if(heap_) heap_->Free(view_); }
  uint32_t width() const override { return width_; }
  uint32_t height() const override { return height_; }
  bool depth() const { return depth_; }
  TrackedResource& tracked() { return tracked_; }
  D3D12_CPU_DESCRIPTOR_HANDLE view() const { return view_; }
 private:
  TrackedResource tracked_;
  uint32_t width_,height_;
  bool depth_;
  D3D12_CPU_DESCRIPTOR_HANDLE view_;
  NativeD3D12CpuDescriptorHeap* heap_;
};

class D3D12Pipeline final : public NativeBackendPipeline {
 public:
  D3D12Pipeline(ID3D12PipelineState& state, D3D12_PRIMITIVE_TOPOLOGY topology, bool requires_blend_factor)
      : state_(&state),topology_(topology),requires_blend_factor_(requires_blend_factor) {}
  ID3D12PipelineState& state() const { return *state_; }
  D3D12_PRIMITIVE_TOPOLOGY topology() const { return topology_; }
  bool requires_blend_factor() const { return requires_blend_factor_; }
 private:
  ID3D12PipelineState* state_;
  D3D12_PRIMITIVE_TOPOLOGY topology_;
  bool requires_blend_factor_;
};

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
  D3D12Query(ComPtr<ID3D12QueryHeap> heap, ComPtr<ID3D12Resource> readback, NativeBackendQueryKind kind)
      : heap_(std::move(heap)),readback_(std::move(readback)),kind_(kind) {}
  ID3D12QueryHeap* heap() const { return heap_.Get(); }
  ID3D12Resource* readback() const { return readback_.Get(); }
  NativeBackendQueryKind kind() const { return kind_; }
  void Resolved(uint64_t fence) { fence_=fence; }
  uint64_t fence() const { return fence_; }
 private:
  ComPtr<ID3D12QueryHeap> heap_;
  ComPtr<ID3D12Resource> readback_;
  NativeBackendQueryKind kind_;
  uint64_t fence_=0;  // 0 = never resolved.
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
  }
  void End() { commands_=nullptr; }

  // Transitions a resource only when it is not already in the state wanted.
  // The redundant-barrier case is not merely wasteful: the debug layer reports
  // a transition whose before state is wrong, and emitting one unconditionally
  // would make every such report a false alarm.
  void Transition(TrackedResource& tracked, D3D12_RESOURCE_STATES wanted) {
    if(tracked.state==wanted) return;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=tracked.resource.Get();
    barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore=tracked.state;
    barrier.Transition.StateAfter=wanted;
    Commands().ResourceBarrier(1,&barrier);
    tracked.state=wanted;
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
  void SetTopology(NativeBackendTopology topology) override {
    Commands().IASetPrimitiveTopology(Topology(topology));
  }
  void SetBlendFactor(const std::array<float,4>& factor) override {
    Commands().OMSetBlendFactor(factor.data());
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
  void SetTexture(NativeBackendStage stage, uint32_t slot, NativeBackendTexture* texture) override {
    if(stage!=NativeBackendStage::Pixel)
      throw std::runtime_error("this root signature declares no vertex-stage textures");
    if(slot>=NativeD3D12RootLayout::kPixelTextures)
      throw std::runtime_error("texture slot "+std::to_string(slot)+" is outside the root signature");
    bound_.textures[slot]=static_cast<D3D12Texture*>(texture);
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
    Commands().RSSetScissorRects(1,&rect);
  }
  void SetScissor(const NativeBackendScissor& scissor, bool enabled) override {
    if(!enabled) return;  // The viewport-sized rectangle set above stands.
    const D3D12_RECT rect{scissor.left,scissor.top,scissor.right,scissor.bottom};
    Commands().RSSetScissorRects(1,&rect);
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
    Commands().BeginQuery(concrete.heap(),D3D12_QUERY_TYPE_OCCLUSION,0);
  }
  void EndQuery(NativeBackendQuery& query) override {
    auto& concrete=static_cast<D3D12Query&>(query);
    Commands().EndQuery(concrete.heap(),D3D12_QUERY_TYPE_OCCLUSION,0);
    // Resolved straight away rather than on a later frame: the result is only
    // readable out of a buffer, and deferring the resolve would mean tracking
    // which frame each unresolved query belonged to.
    Commands().ResolveQueryData(concrete.heap(),D3D12_QUERY_TYPE_OCCLUSION,0,1,
                                concrete.readback(),0);
    concrete.Resolved(gpu_->pending_fence());
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
    std::array<D3D12_SAMPLER_DESC,NativeD3D12RootLayout::kPixelSamplers> descs{};
    for(uint32_t slot=0;slot<descs.size();++slot)
      descs[slot]=bound_.samplers[slot]?bound_.samplers[slot]->desc():DefaultSampler();
    const auto table=gpu_->samplers().Table(descs);
    Commands().SetGraphicsRootDescriptorTable(NativeD3D12RootLayout::kPixelSamplerTable,table);
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
    const auto table=gpu_->AllocateViews(NativeD3D12RootLayout::kPixelTextures,index_);
    const auto increment=gpu_->views(index_).increment();
    for(uint32_t slot=0;slot<NativeD3D12RootLayout::kPixelTextures;++slot) {
      const D3D12_CPU_DESCRIPTOR_HANDLE at{table.cpu.ptr+static_cast<SIZE_T>(slot)*increment};
      auto* texture=bound_.textures[slot];
      if(texture) {
        Transition(texture->tracked(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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
    bound_.textures_dirty=false;
  }

  struct Bound {
    D3D12Pipeline* pipeline=nullptr;
    D3D12Texture* textures[NativeD3D12RootLayout::kPixelTextures]{};
    D3D12Sampler* samplers[NativeD3D12RootLayout::kPixelSamplers]{};
    uint32_t render_targets=0;
    bool textures_dirty=false,samplers_dirty=false,blend_factor_set=false;
  };

  NativeD3D12Device* gpu_;
  ID3D12RootSignature* signature_;
  uint32_t index_=0;
  ID3D12GraphicsCommandList* commands_=nullptr;
  Bound bound_;
  std::vector<Bound> stack_;

 public:
  D3D12_CPU_DESCRIPTOR_HANDLE null_texture_view_{};
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
  }

  ~D3D12Backend() override {
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
    TrackedResource tracked;
    tracked.state=D3D12_RESOURCE_STATE_COMMON;
    const D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT,D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                     D3D12_MEMORY_POOL_UNKNOWN,0,0};
    const D3D12_RESOURCE_DESC description{D3D12_RESOURCE_DIMENSION_BUFFER,0,desc.bytes,1,1,1,
                                          DXGI_FORMAT_UNKNOWN,{1,0},D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                          D3D12_RESOURCE_FLAG_NONE};
    Require(gpu_.device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,
                                                   tracked.state,nullptr,IID_PPV_ARGS(&tracked.resource)),
            "buffer creation");
    auto buffer=std::make_unique<D3D12Buffer>(std::move(tracked),desc.bytes,desc.dynamic);
    if(!initial.empty()) {
      if(initial.size()>desc.bytes)
        throw std::runtime_error("initial buffer contents are larger than the buffer");
      pending_.push_back({buffer.get(),std::vector<uint8_t>(initial.begin(),initial.end())});
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
    TrackedResource tracked;
    tracked.state=D3D12_RESOURCE_STATE_COMMON;
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
    Require(gpu_.device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,
                                                   tracked.state,nullptr,IID_PPV_ARGS(&tracked.resource)),
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
    // Staged on the next frame that opens, like buffer contents: there is no
    // command list to copy with until then.
    if(!initial.empty())
      pending_textures_.push_back({texture.get(),std::vector<uint8_t>(initial.begin(),initial.end())});
    return texture;
  }

  std::unique_ptr<NativeBackendRenderTarget> CreateRenderTarget(const NativeBackendTextureDesc& desc) override {
    TrackedResource tracked;
    const D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT,D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                     D3D12_MEMORY_POOL_UNKNOWN,0,0};
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
    tracked.state=desc.depth?D3D12_RESOURCE_STATE_DEPTH_WRITE:D3D12_RESOURCE_STATE_RENDER_TARGET;
    // A clear value must be declared up front or every clear is a slow path,
    // and it must match what is actually cleared or validation complains.
    D3D12_CLEAR_VALUE clear{};
    clear.Format=description.Format;
    if(desc.depth) clear.DepthStencil={1.0f,0};
    Require(gpu_.device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,
                                                   tracked.state,&clear,IID_PPV_ARGS(&tracked.resource)),
            "render target creation");
    auto& pool=desc.depth?depth_views_:render_target_views_;
    const auto view=pool.Allocate();
    const bool multisampled=description.SampleDesc.Count>1;
    if(desc.depth) {
      D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
      dsv.Format=description.Format;
      dsv.ViewDimension=multisampled?D3D12_DSV_DIMENSION_TEXTURE2DMS:D3D12_DSV_DIMENSION_TEXTURE2D;
      gpu_.device()->CreateDepthStencilView(tracked.resource.Get(),&dsv,view);
    } else {
      D3D12_RENDER_TARGET_VIEW_DESC rtv{};
      rtv.Format=description.Format;
      rtv.ViewDimension=multisampled?D3D12_RTV_DIMENSION_TEXTURE2DMS:D3D12_RTV_DIMENSION_TEXTURE2D;
      gpu_.device()->CreateRenderTargetView(tracked.resource.Get(),&rtv,view);
    }
    return std::make_unique<D3D12RenderTarget>(std::move(tracked),desc.width,desc.height,desc.depth,
                                               view,pool);
  }

  std::unique_ptr<NativeBackendQuery> CreateQuery(NativeBackendQueryKind kind) override {
    if(kind!=NativeBackendQueryKind::Occlusion)
      throw std::runtime_error("only occlusion queries are implemented in the D3D12 backend");
    const D3D12_QUERY_HEAP_DESC desc{D3D12_QUERY_HEAP_TYPE_OCCLUSION,1,0};
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
    return std::make_unique<D3D12Query>(std::move(heap),std::move(readback),kind);
  }

  bool ReadQuery(NativeBackendQuery& query, std::span<uint8_t> result) override {
    auto& concrete=static_cast<D3D12Query&>(query);
    if(result.size()<sizeof(uint64_t))
      throw std::runtime_error("an occlusion query result needs 8 bytes");
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
    request.vertex={desc.vertex.data(),desc.vertex.size()};
    request.pixel={desc.pixel.data(),desc.pixel.size()};
    request.input_layout=elements;
    request.state=desc.state;

    auto& state=pipelines_.Get(request);
    // The wrapper is keyed the same way as the pipeline, so repeated calls with
    // the same description hand back the same object rather than leaking one
    // wrapper per call for the same underlying pipeline.
    std::string key(reinterpret_cast<const char*>(&request.key),sizeof(request.key));
    auto found=wrappers_.find(key);
    if(found==wrappers_.end())
      found=wrappers_.emplace(std::move(key),
                              std::make_unique<D3D12Pipeline>(state,Topology(desc.topology),
                                                              DecodeNativeRenderState(desc.state)
                                                                .requires_blend_factor)).first;
    return *found->second;
  }

  NativeBackendRecorder& Recorder(uint32_t index) override {
    if(!open_) throw std::runtime_error("the D3D12 backend has no frame open; call BeginFrame first");
    if(index>=recorders_.size())
      throw std::runtime_error("recorder "+std::to_string(index)+" does not exist; this backend has "+
                               std::to_string(recorders_.size()));
    return *recorders_[index];
  }
  uint32_t RecorderCount() const override { return static_cast<uint32_t>(recorders_.size()); }
  // True only when there is genuinely more than one command list. A backend
  // that claimed this with one recorder would have callers spawn threads that
  // then serialise on it, which is slower than not threading at all.
  bool SupportsParallelRecording() const override { return recorders_.size()>1; }

  void Submit() override {
    if(!open_) throw std::runtime_error("Submit with no frame open");
    // A flip-model back buffer must be in PRESENT state when it is presented,
    // and only the open command list can put it there. Doing it here rather
    // than in Present keeps it off a second submission, and doing it for every
    // buffer rather than the current one costs nothing: Transition is a no-op
    // for a buffer already in that state.
    for(auto& buffer:back_buffers_)
      recorders_.front()->Transition(buffer->tracked(),D3D12_RESOURCE_STATE_PRESENT);
    for(auto& recorder:recorders_) recorder->End();
    gpu_.EndFrame();
    open_=false;
  }

  void BeginFrame() override {
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
    for(auto& upload:pending_) recorders_.front()->UpdateBuffer(*upload.buffer,0,upload.bytes);
    pending_.clear();
    for(auto& upload:pending_textures_) UploadTexture(*upload.texture,upload.bytes);
    pending_textures_.clear();
  }

  std::vector<uint8_t> ReadRenderTarget(NativeBackendRenderTarget& target) override {
    if(open_) throw std::runtime_error("ReadRenderTarget cannot run inside an open frame");
    auto& concrete=static_cast<D3D12RenderTarget&>(target);
    const auto description=concrete.tracked().resource->GetDesc();
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
    recorders_.front()->Transition(concrete.tracked(),D3D12_RESOURCE_STATE_COPY_SOURCE);
    const D3D12_TEXTURE_COPY_LOCATION to{readback.Get(),
                                         D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,{footprint}};
    D3D12_TEXTURE_COPY_LOCATION from{};
    from.pResource=concrete.tracked().resource.Get();
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
    TrackedResource tracked;
    // A surface another API owns and may still be writing. It arrives in
    // COMMON, which is the only state a shared resource can be handed over in.
    tracked.state=D3D12_RESOURCE_STATE_COMMON;
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

  bool WaitSharedFence(void* handle, uint64_t value) override {
    if(!handle) return false;
    if(!shared_fence_ || shared_fence_handle_!=handle) {
      shared_fence_.Reset();
      if(FAILED(gpu_.device()->OpenSharedHandle(handle,IID_PPV_ARGS(&shared_fence_)))) return false;
      shared_fence_handle_=handle;
    }
    // A queue-side wait, not a CPU one: the GPU stalls until the producer has
    // signalled, and this thread carries on recording.
    return SUCCEEDED(gpu_.queue()->Wait(shared_fence_.Get(),value));
  }

  std::vector<std::string> DrainValidationMessages() override { return gpu_.DrainValidationErrors(); }

  void AttachWindow(void* window, uint32_t width, uint32_t height) override {
    if(open_) throw std::runtime_error("a window cannot be attached inside an open frame");
    if(!window) throw std::runtime_error("AttachWindow needs a window");
    // Anything already submitted may still be reading a buffer we are about to
    // release, so the queue has to drain before the old chain goes.
    gpu_.WaitIdle();
    back_buffers_.clear();
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

    ComPtr<IDXGISwapChain1> chain;
    Require(gpu_.factory()->CreateSwapChainForHwnd(gpu_.queue(),static_cast<HWND>(window),&desc,
                                                   nullptr,nullptr,&chain),"swap chain creation");
    // Alt+Enter belongs to the game's own display handling, not to DXGI.
    gpu_.factory()->MakeWindowAssociation(static_cast<HWND>(window),DXGI_MWA_NO_ALT_ENTER);
    Require(chain.As(&swap_chain_),"swap chain interface");

    for(uint32_t index=0;index<kBackBuffers;++index) {
      TrackedResource tracked;
      tracked.state=D3D12_RESOURCE_STATE_PRESENT;
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

  void Present(bool vsync) override {
    if(!swap_chain_) throw std::runtime_error("Present with no window attached");
    if(open_) throw std::runtime_error("Present inside an open frame; submit it first");
    Require(swap_chain_->Present(vsync?1:0,0),"present");
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

  struct PendingUpload { D3D12Buffer* buffer; std::vector<uint8_t> bytes; };
  struct PendingTexture { D3D12Texture* texture; std::vector<uint8_t> bytes; };

  NativeD3D12Device gpu_;
  ComPtr<ID3D12RootSignature> signature_;
  NativeD3D12PipelineCache pipelines_;
  NativeD3D12CpuDescriptorHeap texture_views_,render_target_views_,depth_views_;
  std::vector<std::unique_ptr<D3D12Recorder>> recorders_;
  std::map<std::string,std::unique_ptr<D3D12Pipeline>> wrappers_;
  // Three, to match the frames in flight: the device will not reuse a frame
  // slot until its fence has passed, which is also what keeps us off a buffer
  // the display is still showing.
  static constexpr uint32_t kBackBuffers=3;
  ComPtr<ID3D12Fence> shared_fence_;
  void* shared_fence_handle_=nullptr;
  ComPtr<IDXGISwapChain3> swap_chain_;
  std::vector<std::unique_ptr<D3D12RenderTarget>> back_buffers_;
  std::vector<PendingUpload> pending_;
  std::vector<PendingTexture> pending_textures_;
  std::map<std::string,std::unique_ptr<D3D12Sampler>> samplers_;
  D3D12_CPU_DESCRIPTOR_HANDLE null_texture_{};
  bool open_=false;
};
}  // namespace

std::unique_ptr<NativeRenderBackend> CreateNativeD3D12Backend(const NativeD3D12Options& options) {
  return std::make_unique<D3D12Backend>(options);
}

void RegisterNativeD3D12Backend() {
  static bool registered=false;
  if(registered) return;
  RegisterNativeRenderBackend("d3d12",[]() -> std::unique_ptr<NativeRenderBackend> {
    return CreateNativeD3D12Backend();
  });
  // WARP as its own selectable backend, not a hidden fallback. It is how a
  // rendering difference gets attributed: if it reproduces on the software
  // rasteriser the bug is ours, and if it does not it is the driver's. It also
  // lets the test suite drive the whole registry path on a machine with no
  // D3D12 hardware.
  RegisterNativeRenderBackend("d3d12-warp",[]() -> std::unique_ptr<NativeRenderBackend> {
    NativeD3D12Options options;
    options.prefer_warp=true;
    options.debug_layer=true;
    return CreateNativeD3D12Backend(options);
  });
  registered=true;
}
}  // namespace edf::native

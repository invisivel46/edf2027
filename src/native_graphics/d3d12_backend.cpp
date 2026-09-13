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
  D3D12Buffer(TrackedResource tracked, size_t bytes) : tracked_(std::move(tracked)),bytes_(bytes) {}
  size_t bytes() const override { return bytes_; }
  TrackedResource& tracked() { return tracked_; }
  D3D12_GPU_VIRTUAL_ADDRESS address() const { return tracked_.resource->GetGPUVirtualAddress(); }
 private:
  TrackedResource tracked_;
  size_t bytes_;
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
  D3D12Pipeline(ID3D12PipelineState& state, D3D12_PRIMITIVE_TOPOLOGY topology)
      : state_(&state),topology_(topology) {}
  ID3D12PipelineState& state() const { return *state_; }
  D3D12_PRIMITIVE_TOPOLOGY topology() const { return topology_; }
 private:
  ID3D12PipelineState* state_;
  D3D12_PRIMITIVE_TOPOLOGY topology_;
};

class D3D12Query final : public NativeBackendQuery {
 public:
  D3D12Query(ComPtr<ID3D12QueryHeap> heap, NativeBackendQueryKind kind) : heap_(std::move(heap)),kind_(kind) {}
  ID3D12QueryHeap* heap() const { return heap_.Get(); }
  NativeBackendQueryKind kind() const { return kind_; }
 private:
  ComPtr<ID3D12QueryHeap> heap_;
  NativeBackendQueryKind kind_;
};

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
  D3D12Recorder(NativeD3D12Device& gpu, ID3D12RootSignature& signature)
      : gpu_(&gpu),signature_(&signature) {}

  void Begin(ID3D12GraphicsCommandList& commands) {
    commands_=&commands;
    ID3D12DescriptorHeap* heaps[]={gpu_->views().heap(),gpu_->samplers().heap()};
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

  void SetConstants(NativeBackendStage stage, uint32_t slot, std::span<const uint8_t> bytes) override {
    // Straight into the upload ring and then into a root descriptor: no
    // constant buffer object, no descriptor, no map. This is the path the slot
    // measurement bought - it is why the constant buffers are root CBVs.
    const auto upload=gpu_->Allocate(bytes.size());
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
    // Samplers reach this backend through the pipeline's sampler table, which
    // is cached by combination; a per-slot sampler object has no equivalent
    // here and pretending otherwise would bind nothing.
    (void)stage; (void)slot; (void)sampler;
    throw std::runtime_error("the D3D12 backend binds samplers as a cached table, not per slot");
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
    FlushTextures();
    Commands().DrawInstanced(vertices,1,first_vertex,0);
  }
  void DrawIndexed(uint32_t indices, uint32_t first_index, int32_t base_vertex) override {
    FlushTextures();
    Commands().DrawIndexedInstanced(indices,1,first_index,base_vertex,0);
  }
  void DrawIndexedInstanced(uint32_t indices, uint32_t instances, uint32_t first_index,
                            int32_t base_vertex, uint32_t first_instance) override {
    FlushTextures();
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
    Transition(to.tracked(),D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(from.tracked(),D3D12_RESOURCE_STATE_COPY_SOURCE);
    // Single-sampled targets are the common case here and a resolve of one is
    // a copy; a genuine MSAA resolve needs the sample count, which this seam
    // does not yet carry, so it is refused rather than silently downgraded.
    const auto description=from.tracked().resource->GetDesc();
    if(description.SampleDesc.Count>1)
      throw std::runtime_error("multisample resolve is not implemented in the D3D12 backend");
    Commands().CopyResource(to.tracked().resource.Get(),from.tracked().resource.Get());
  }
  void UpdateBuffer(NativeBackendBuffer& buffer, uint32_t offset, std::span<const uint8_t> bytes) override {
    auto& concrete=static_cast<D3D12Buffer&>(buffer);
    const auto upload=gpu_->Allocate(bytes.size(),16);
    std::memcpy(upload.cpu,bytes.data(),bytes.size());
    Transition(concrete.tracked(),D3D12_RESOURCE_STATE_COPY_DEST);
    Commands().CopyBufferRegion(concrete.tracked().resource.Get(),offset,upload.resource,
                                upload.offset,bytes.size());
  }

  void BeginQuery(NativeBackendQuery& query) override {
    auto& concrete=static_cast<D3D12Query&>(query);
    Commands().BeginQuery(concrete.heap(),D3D12_QUERY_TYPE_OCCLUSION,0);
  }
  void EndQuery(NativeBackendQuery& query) override {
    auto& concrete=static_cast<D3D12Query&>(query);
    Commands().EndQuery(concrete.heap(),D3D12_QUERY_TYPE_OCCLUSION,0);
  }
  bool ReadQuery(NativeBackendQuery&, std::span<uint8_t>) override {
    // Resolving a query needs a readback buffer and a fence the caller can
    // poll. Returning false says "not ready" forever, which reads as a stall
    // rather than a missing feature, so this is refused instead.
    throw std::runtime_error("query readback is not implemented in the D3D12 backend");
  }

  void PushState() override { stack_.push_back(bound_); }
  void PopState() override {
    if(stack_.empty()) throw std::runtime_error("PopState with nothing pushed");
    bound_=stack_.back();
    stack_.pop_back();
    if(bound_.pipeline) SetPipeline(*bound_.pipeline);
    bound_.textures_dirty=true;
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
  void FlushTextures() {
    if(!bound_.textures_dirty) return;
    const auto table=gpu_->AllocateViews(NativeD3D12RootLayout::kPixelTextures);
    const auto increment=gpu_->views().increment();
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
    uint32_t render_targets=0;
    bool textures_dirty=false;
  };

  NativeD3D12Device* gpu_;
  ID3D12RootSignature* signature_;
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
        depth_views_(*gpu_.device(),D3D12_DESCRIPTOR_HEAP_TYPE_DSV,64),
        recorder_(gpu_,*signature_.Get()) {
    // One null descriptor, written once, for every texture slot a draw leaves
    // unbound. Creating it per draw would be pure waste on the hottest path.
    null_texture_=texture_views_.Allocate();
    D3D12_SHADER_RESOURCE_VIEW_DESC null{};
    null.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    null.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    null.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    null.Texture2D.MipLevels=1;
    gpu_.device()->CreateShaderResourceView(nullptr,&null,null_texture_);
    recorder_.null_texture_view_=null_texture_;
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
    auto buffer=std::make_unique<D3D12Buffer>(std::move(tracked),desc.bytes);
    if(!initial.empty()) {
      if(initial.size()>desc.bytes)
        throw std::runtime_error("initial buffer contents are larger than the buffer");
      pending_.push_back({buffer.get(),std::vector<uint8_t>(initial.begin(),initial.end())});
    }
    return buffer;
  }

  std::unique_ptr<NativeBackendTexture> CreateTexture(const NativeBackendTextureDesc& desc,
                                                      std::span<const uint8_t> initial) override {
    if(!initial.empty())
      // Uploading pixels needs a footprint-aware staged copy; leaving it
      // unimplemented and loud beats accepting the bytes and dropping them.
      throw std::runtime_error("initial texture contents are not implemented in the D3D12 backend");
    TrackedResource tracked;
    tracked.state=D3D12_RESOURCE_STATE_COMMON;
    const D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT,D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                     D3D12_MEMORY_POOL_UNKNOWN,0,0};
    D3D12_RESOURCE_DESC description{};
    description.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width=desc.width;
    description.Height=desc.height;
    description.DepthOrArraySize=1;
    description.MipLevels=static_cast<UINT16>(desc.levels?desc.levels:1);
    description.Format=static_cast<DXGI_FORMAT>(desc.format);
    description.SampleDesc={1,0};
    Require(gpu_.device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,
                                                   tracked.state,nullptr,IID_PPV_ARGS(&tracked.resource)),
            "texture creation");
    const auto view=texture_views_.Allocate();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format=description.Format;
    srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels=description.MipLevels;
    gpu_.device()->CreateShaderResourceView(tracked.resource.Get(),&srv,view);
    return std::make_unique<D3D12Texture>(std::move(tracked),desc.width,desc.height,view,texture_views_);
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
    description.SampleDesc={1,0};
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
    if(desc.depth) {
      D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
      dsv.Format=description.Format;
      dsv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
      gpu_.device()->CreateDepthStencilView(tracked.resource.Get(),&dsv,view);
    } else {
      D3D12_RENDER_TARGET_VIEW_DESC rtv{};
      rtv.Format=description.Format;
      rtv.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
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
    return std::make_unique<D3D12Query>(std::move(heap),kind);
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
                              std::make_unique<D3D12Pipeline>(state,Topology(desc.topology))).first;
    return *found->second;
  }

  NativeBackendRecorder& Recorder() override {
    if(!open_) throw std::runtime_error("the D3D12 backend has no frame open; call Submit's counterpart first");
    return recorder_;
  }
  // One immediate recorder for now. Parallel recording is stage 3 and needs a
  // command list per thread; claiming it here would be a lie the caller would
  // act on by spawning threads that then serialise.
  bool SupportsParallelRecording() const override { return false; }

  void Submit() override {
    if(!open_) throw std::runtime_error("Submit with no frame open");
    recorder_.End();
    gpu_.EndFrame();
    open_=false;
  }

  void BeginFrame() override {
    if(open_) throw std::runtime_error("a D3D12 frame is already open");
    auto* commands=gpu_.BeginFrame();
    recorder_.Begin(*commands);
    open_=true;
    // Buffer contents supplied at creation are staged on the first frame that
    // opens: there is no command list to copy them with until then, and a
    // backend that quietly dropped them would produce an empty mesh.
    for(auto& upload:pending_) recorder_.UpdateBuffer(*upload.buffer,0,upload.bytes);
    pending_.clear();
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
    recorder_.Transition(concrete.tracked(),D3D12_RESOURCE_STATE_COPY_SOURCE);
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

  std::vector<std::string> DrainValidationMessages() override { return gpu_.DrainValidationErrors(); }

  NativeD3D12Device& gpu() { return gpu_; }
  const NativeD3D12PipelineCache& pipelines() const { return pipelines_; }

 private:
  struct PendingUpload { D3D12Buffer* buffer; std::vector<uint8_t> bytes; };

  NativeD3D12Device gpu_;
  ComPtr<ID3D12RootSignature> signature_;
  NativeD3D12PipelineCache pipelines_;
  NativeD3D12CpuDescriptorHeap texture_views_,render_target_views_,depth_views_;
  D3D12Recorder recorder_;
  std::map<std::string,std::unique_ptr<D3D12Pipeline>> wrappers_;
  std::vector<PendingUpload> pending_;
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

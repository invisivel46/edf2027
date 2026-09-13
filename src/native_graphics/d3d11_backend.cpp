#include "d3d11_backend.h"
#include "native_dxgi_format.h"
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
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
  throw std::runtime_error(std::string("D3D11 ")+what+" failed: "+code);
}

class D3D11Buffer final : public NativeBackendBuffer {
 public:
  D3D11Buffer(ComPtr<ID3D11Buffer> buffer, size_t bytes) : buffer_(std::move(buffer)),bytes_(bytes) {}
  size_t bytes() const override { return bytes_; }
  ID3D11Buffer* buffer() const { return buffer_.Get(); }
 private:
  ComPtr<ID3D11Buffer> buffer_;
  size_t bytes_;
};

class D3D11Texture final : public NativeBackendTexture {
 public:
  // An adopted view has no texture of its own; nothing here needs one except
  // CopyTexture, which refuses rather than dereferencing null.
  D3D11Texture(ComPtr<ID3D11Texture2D> texture, ComPtr<ID3D11ShaderResourceView> view,
               uint32_t width, uint32_t height)
      : texture_(std::move(texture)),view_(std::move(view)),width_(width),height_(height) {}
  uint32_t width() const override { return width_; }
  uint32_t height() const override { return height_; }
  ID3D11Texture2D* texture() const { return texture_.Get(); }
  ID3D11ShaderResourceView* view() const { return view_.Get(); }
 private:
  ComPtr<ID3D11Texture2D> texture_;
  ComPtr<ID3D11ShaderResourceView> view_;
  uint32_t width_,height_;
};

class D3D11RenderTarget final : public NativeBackendRenderTarget {
 public:
  D3D11RenderTarget(ComPtr<ID3D11Texture2D> texture, ComPtr<ID3D11RenderTargetView> colour,
                    ComPtr<ID3D11DepthStencilView> depth, uint32_t width, uint32_t height)
      : texture_(std::move(texture)),colour_(std::move(colour)),depth_(std::move(depth)),
        width_(width),height_(height) {}
  uint32_t width() const override { return width_; }
  uint32_t height() const override { return height_; }
  ID3D11Texture2D* texture() const { return texture_.Get(); }
  ID3D11RenderTargetView* colour() const { return colour_.Get(); }
  ID3D11DepthStencilView* depth() const { return depth_.Get(); }
 private:
  ComPtr<ID3D11Texture2D> texture_;
  ComPtr<ID3D11RenderTargetView> colour_;
  ComPtr<ID3D11DepthStencilView> depth_;
  uint32_t width_,height_;
};

class D3D11Sampler final : public NativeBackendSampler {
 public:
  explicit D3D11Sampler(ComPtr<ID3D11SamplerState> state) : state_(std::move(state)) {}
  ID3D11SamplerState* state() const { return state_.Get(); }
 private:
  ComPtr<ID3D11SamplerState> state_;
};

// D3D12 and Vulkan fuse these into one object; D3D11 keeps them apart. The
// seam takes the fused form, so this holds the pieces and binds them together.
// Callers therefore cannot depend on D3D11's ability to change one without the
// others - which is the point, since the other backends cannot offer it.
class D3D11Pipeline final : public NativeBackendPipeline {
 public:
  ComPtr<ID3D11VertexShader> vertex;
  ComPtr<ID3D11PixelShader> pixel;
  ComPtr<ID3D11InputLayout> layout;
  ComPtr<ID3D11BlendState> blend;
  ComPtr<ID3D11DepthStencilState> depth;
  ComPtr<ID3D11RasterizerState> raster;
  D3D11_PRIMITIVE_TOPOLOGY topology=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  bool requires_blend_factor=false;
};

class D3D11Query final : public NativeBackendQuery {
 public:
  explicit D3D11Query(ComPtr<ID3D11Query> query) : query_(std::move(query)) {}
  ID3D11Query* query() const { return query_.Get(); }
 private:
  ComPtr<ID3D11Query> query_;
};

D3D11_PRIMITIVE_TOPOLOGY Topology(NativeBackendTopology topology) {
  switch(topology) {
    case NativeBackendTopology::TriangleList: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case NativeBackendTopology::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case NativeBackendTopology::LineList: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    case NativeBackendTopology::PointList: return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
  }
  throw std::runtime_error("unknown backend topology");
}
D3D11_FILTER_TYPE FilterType(NativeBackendFilter filter) {
  return filter==NativeBackendFilter::Point?D3D11_FILTER_TYPE_POINT:D3D11_FILTER_TYPE_LINEAR;
}
D3D11_TEXTURE_ADDRESS_MODE AddressMode(NativeBackendAddress address) {
  switch(address) {
    case NativeBackendAddress::Wrap: return D3D11_TEXTURE_ADDRESS_WRAP;
    case NativeBackendAddress::Mirror: return D3D11_TEXTURE_ADDRESS_MIRROR;
    case NativeBackendAddress::Clamp: return D3D11_TEXTURE_ADDRESS_CLAMP;
    case NativeBackendAddress::Border: return D3D11_TEXTURE_ADDRESS_BORDER;
    case NativeBackendAddress::MirrorOnce: return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
  }
  throw std::runtime_error("unknown backend address mode");
}

// The shared format table is written in raw DXGI numbers so it does not drag a
// graphics header into the API-independent side. That is only safe if the
// numbers are right, and getting one wrong reads the wrong number of bytes per
// texture rather than failing - so they are proved here instead of trusted.
// Two of them were in fact wrong when first written: the BC groupings were
// shifted by one, which put BC4's 8-byte blocks in with BC3's 16-byte ones.
static_assert(DXGI_FORMAT_BC1_TYPELESS==70 && DXGI_FORMAT_BC1_UNORM==71 && DXGI_FORMAT_BC1_UNORM_SRGB==72);
static_assert(DXGI_FORMAT_BC2_TYPELESS==73 && DXGI_FORMAT_BC2_UNORM==74 && DXGI_FORMAT_BC2_UNORM_SRGB==75);
static_assert(DXGI_FORMAT_BC3_TYPELESS==76 && DXGI_FORMAT_BC3_UNORM==77 && DXGI_FORMAT_BC3_UNORM_SRGB==78);
static_assert(DXGI_FORMAT_R32G32B32A32_FLOAT==2 && DXGI_FORMAT_R16G16B16A16_FLOAT==10);
static_assert(DXGI_FORMAT_D32_FLOAT_S8X24_UINT==20 && DXGI_FORMAT_R10G10B10A2_UNORM==24);
static_assert(DXGI_FORMAT_R8G8B8A8_UNORM==28 && DXGI_FORMAT_R8G8B8A8_UNORM_SRGB==29);
static_assert(DXGI_FORMAT_R16G16_FLOAT==34 && DXGI_FORMAT_D32_FLOAT==40 && DXGI_FORMAT_R32_FLOAT==41);
static_assert(DXGI_FORMAT_D24_UNORM_S8_UINT==45 && DXGI_FORMAT_R8G8_UNORM==49);
static_assert(DXGI_FORMAT_R16_FLOAT==54 && DXGI_FORMAT_R8_UNORM==61);
static_assert(DXGI_FORMAT_B8G8R8A8_UNORM==87 && DXGI_FORMAT_B8G8R8A8_UNORM_SRGB==91);

constexpr uint32_t kTextureSlots=8,kSamplerSlots=8,kConstantSlots=4;

class D3D11Recorder final : public NativeBackendRecorder {
 public:
  D3D11Recorder(ID3D11Device& device, ID3D11DeviceContext& context)
      : device_(&device),context_(&context) {}

  void Begin() { bound_={}; stack_.clear(); }

  void SetPipeline(NativeBackendPipeline& pipeline) override {
    auto& concrete=static_cast<D3D11Pipeline&>(pipeline);
    context_->VSSetShader(concrete.vertex.Get(),nullptr,0);
    context_->PSSetShader(concrete.pixel.Get(),nullptr,0);
    context_->IASetInputLayout(concrete.layout.Get());
    context_->OMSetBlendState(concrete.blend.Get(),bound_.blend_factor.data(),0xffffffff);
    context_->OMSetDepthStencilState(concrete.depth.Get(),0);
    context_->RSSetState(concrete.raster.Get());
    context_->IASetPrimitiveTopology(concrete.topology);
    bound_.pipeline=&concrete;
  }
  void SetVertexBuffer(uint32_t slot, NativeBackendBuffer& buffer, uint32_t stride, uint32_t offset) override {
    auto* value=static_cast<D3D11Buffer&>(buffer).buffer();
    context_->IASetVertexBuffers(slot,1,&value,&stride,&offset);
  }
  void SetIndexBuffer(NativeBackendBuffer& buffer, NativeBackendIndexFormat format, uint32_t offset) override {
    context_->IASetIndexBuffer(static_cast<D3D11Buffer&>(buffer).buffer(),
                               format==NativeBackendIndexFormat::Uint16?DXGI_FORMAT_R16_UINT
                                                                       :DXGI_FORMAT_R32_UINT,offset);
  }
  void SetTopology(NativeBackendTopology topology) override {
    context_->IASetPrimitiveTopology(Topology(topology));
  }
  void SetBlendFactor(const std::array<float,4>& factor) override {
    bound_.blend_factor=factor;
    bound_.blend_factor_set=true;
    // D3D11 carries the factor on the blend state, so it is re-sent with the
    // state rather than on its own.
    if(bound_.pipeline)
      context_->OMSetBlendState(bound_.pipeline->blend.Get(),factor.data(),0xffffffff);
  }

  void SetConstants(NativeBackendStage stage, uint32_t slot, std::span<const uint8_t> bytes) override {
    if(slot>=kConstantSlots)
      throw std::runtime_error("constant slot "+std::to_string(slot)+" is outside this backend");
    // WRITE_DISCARD renames the buffer, so a draw already recorded keeps the
    // values it was given. That is the one place D3D11 does for free what the
    // D3D12 backend needs an explicit fenced ring for, and it is why the same
    // per-draw constants interface can sit on both.
    auto& buffer=Constants(stage,slot,bytes.size());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Require(context_->Map(buffer.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"constant map");
    std::memcpy(mapped.pData,bytes.data(),bytes.size());
    context_->Unmap(buffer.Get(),0);
    auto* value=buffer.Get();
    if(stage==NativeBackendStage::Vertex) context_->VSSetConstantBuffers(slot,1,&value);
    else context_->PSSetConstantBuffers(slot,1,&value);
  }
  void SetTexture(NativeBackendStage stage, uint32_t slot, NativeBackendTexture* texture) override {
    if(stage!=NativeBackendStage::Pixel)
      throw std::runtime_error("this backend declares no vertex-stage textures");
    if(slot>=kTextureSlots)
      throw std::runtime_error("texture slot "+std::to_string(slot)+" is outside this backend");
    bound_.textures[slot]=texture?static_cast<D3D11Texture*>(texture)->view():nullptr;
    bound_.textures_dirty=true;
  }
  void SetSampler(NativeBackendStage stage, uint32_t slot, NativeBackendSampler* sampler) override {
    if(stage!=NativeBackendStage::Pixel)
      throw std::runtime_error("this backend declares no vertex-stage samplers");
    if(slot>=kSamplerSlots)
      throw std::runtime_error("sampler slot "+std::to_string(slot)+" is outside this backend");
    bound_.samplers[slot]=sampler?static_cast<D3D11Sampler*>(sampler)->state():nullptr;
    bound_.samplers_dirty=true;
  }

  void SetRenderTargets(std::span<NativeBackendRenderTarget* const> colors,
                        NativeBackendRenderTarget* depth) override {
    ID3D11RenderTargetView* views[8]{};
    uint32_t count=0;
    for(auto* target:colors) {
      if(!target) continue;
      views[count++]=static_cast<D3D11RenderTarget*>(target)->colour();
      if(count==8) break;
    }
    context_->OMSetRenderTargets(count,count?views:nullptr,
                                 depth?static_cast<D3D11RenderTarget*>(depth)->depth():nullptr);
  }
  void SetViewport(const NativeBackendViewport& viewport) override {
    const D3D11_VIEWPORT native{viewport.x,viewport.y,viewport.width,viewport.height,
                                viewport.min_depth,viewport.max_depth};
    context_->RSSetViewports(1,&native);
    // Matching the D3D12 backend deliberately: it must set a scissor because
    // D3D12 clips nothing without one, so this sets the same default rather
    // than leaving the two backends clipping differently.
    const D3D11_RECT rect{static_cast<LONG>(viewport.x),static_cast<LONG>(viewport.y),
                          static_cast<LONG>(viewport.x+viewport.width),
                          static_cast<LONG>(viewport.y+viewport.height)};
    context_->RSSetScissorRects(1,&rect);
  }
  void SetScissor(const NativeBackendScissor& scissor, bool enabled) override {
    if(!enabled) return;
    const D3D11_RECT rect{scissor.left,scissor.top,scissor.right,scissor.bottom};
    context_->RSSetScissorRects(1,&rect);
  }

  void ClearColor(NativeBackendRenderTarget& target, const std::array<float,4>& color) override {
    context_->ClearRenderTargetView(static_cast<D3D11RenderTarget&>(target).colour(),color.data());
  }
  void ClearDepthStencil(NativeBackendRenderTarget& target, bool depth, bool stencil,
                         float depth_value, uint8_t stencil_value) override {
    UINT flags=0;
    if(depth) flags|=D3D11_CLEAR_DEPTH;
    if(stencil) flags|=D3D11_CLEAR_STENCIL;
    if(!flags) return;
    context_->ClearDepthStencilView(static_cast<D3D11RenderTarget&>(target).depth(),flags,
                                    depth_value,stencil_value);
  }

  void Draw(uint32_t vertices, uint32_t first_vertex) override {
    Flush();
    context_->Draw(vertices,first_vertex);
  }
  void DrawIndexed(uint32_t indices, uint32_t first_index, int32_t base_vertex) override {
    Flush();
    context_->DrawIndexed(indices,first_index,base_vertex);
  }
  void DrawIndexedInstanced(uint32_t indices, uint32_t instances, uint32_t first_index,
                            int32_t base_vertex, uint32_t first_instance) override {
    Flush();
    context_->DrawIndexedInstanced(indices,instances,first_index,base_vertex,first_instance);
  }

  void CopyTexture(NativeBackendTexture& destination, NativeBackendTexture& source) override {
    auto& to=static_cast<D3D11Texture&>(destination);
    auto& from=static_cast<D3D11Texture&>(source);
    if(!to.texture() || !from.texture())
      throw std::runtime_error("an adopted texture was copied; adoption wraps a view, not a resource");
    context_->CopyResource(to.texture(),from.texture());
  }
  void ResolveTarget(NativeBackendTexture& destination, NativeBackendRenderTarget& source) override {
    auto& to=static_cast<D3D11Texture&>(destination);
    auto& from=static_cast<D3D11RenderTarget&>(source);
    D3D11_TEXTURE2D_DESC description{};
    from.texture()->GetDesc(&description);
    if(description.SampleDesc.Count>1)
      context_->ResolveSubresource(to.texture(),0,from.texture(),0,description.Format);
    else context_->CopyResource(to.texture(),from.texture());
  }
  void UpdateBuffer(NativeBackendBuffer& buffer, uint32_t offset, std::span<const uint8_t> bytes) override {
    auto& concrete=static_cast<D3D11Buffer&>(buffer);
    const D3D11_BOX box{offset,0,0,static_cast<UINT>(offset+bytes.size()),1,1};
    context_->UpdateSubresource(concrete.buffer(),0,&box,bytes.data(),0,0);
  }

  void UpdateTexture(NativeBackendTexture& texture, std::span<const uint8_t> bytes) override {
    auto& concrete=static_cast<D3D11Texture&>(texture);
    D3D11_TEXTURE2D_DESC description{};
    concrete.texture()->GetDesc(&description);
    const auto info=DescribeNativeDxgiFormat(description.Format);
    const UINT pitch=static_cast<UINT>(NativeDxgiRowPitch(info,description.Width));
    const uint64_t needed=NativeDxgiLevelBytes(info,description.Width,description.Height);
    if(bytes.size()<needed)
      throw std::runtime_error("texture update is "+std::to_string(bytes.size())+
                               " bytes but the texture needs "+std::to_string(needed));
    context_->UpdateSubresource(concrete.texture(),0,nullptr,bytes.data(),pitch,0);
  }

  void BeginQuery(NativeBackendQuery& query) override {
    context_->Begin(static_cast<D3D11Query&>(query).query());
  }
  void EndQuery(NativeBackendQuery& query) override {
    context_->End(static_cast<D3D11Query&>(query).query());
  }

  void PushState() override { stack_.push_back(bound_); }
  void PopState() override {
    if(stack_.empty()) throw std::runtime_error("PopState with nothing pushed");
    bound_=stack_.back();
    stack_.pop_back();
    if(bound_.pipeline) SetPipeline(*bound_.pipeline);
    bound_.textures_dirty=bound_.samplers_dirty=true;
  }

 private:
  // Bound as runs just before the draw, matching the D3D12 backend, so both
  // send the same number of API calls per draw and a timing comparison between
  // them is a comparison of the APIs rather than of two binding strategies.
  void Flush() {
    if(bound_.pipeline && bound_.pipeline->requires_blend_factor && !bound_.blend_factor_set)
      throw std::runtime_error("this draw blends against a constant blend factor that was never set");
    if(bound_.textures_dirty) {
      context_->PSSetShaderResources(0,kTextureSlots,bound_.textures);
      bound_.textures_dirty=false;
    }
    if(bound_.samplers_dirty) {
      context_->PSSetSamplers(0,kSamplerSlots,bound_.samplers);
      bound_.samplers_dirty=false;
    }
  }
  ComPtr<ID3D11Buffer>& Constants(NativeBackendStage stage, uint32_t slot, size_t bytes) {
    auto& slots=stage==NativeBackendStage::Vertex?vertex_constants_:pixel_constants_;
    auto& buffer=slots[slot];
    // A constant buffer is a multiple of 16 bytes and is never shrunk: the
    // same slot is written thousands of times a frame and reallocating on
    // every smaller write would cost more than the wasted space.
    const UINT wanted=static_cast<UINT>((bytes+15)&~size_t(15));
    if(buffer) {
      D3D11_BUFFER_DESC existing{};
      buffer->GetDesc(&existing);
      if(existing.ByteWidth>=wanted) return buffer;
    }
    const D3D11_BUFFER_DESC desc{wanted,D3D11_USAGE_DYNAMIC,D3D11_BIND_CONSTANT_BUFFER,
                                 D3D11_CPU_ACCESS_WRITE,0,0};
    buffer.Reset();
    Require(device_->CreateBuffer(&desc,nullptr,&buffer),"constant buffer creation");
    return buffer;
  }

  struct Bound {
    D3D11Pipeline* pipeline=nullptr;
    ID3D11ShaderResourceView* textures[kTextureSlots]{};
    ID3D11SamplerState* samplers[kSamplerSlots]{};
    std::array<float,4> blend_factor{1,1,1,1};
    bool textures_dirty=false,samplers_dirty=false,blend_factor_set=false;
  };

  ID3D11Device* device_;
  ID3D11DeviceContext* context_;
  ComPtr<ID3D11Buffer> vertex_constants_[kConstantSlots],pixel_constants_[kConstantSlots];
  Bound bound_;
  std::vector<Bound> stack_;
};

class D3D11Backend final : public NativeRenderBackend {
 public:
  D3D11Backend(ID3D11Device& device, ID3D11DeviceContext& context)
      : device_(&device),context_(&context),owns_device_(false) {
    recorder_=std::make_unique<D3D11Recorder>(*device_.Get(),*context_.Get());
  }
  explicit D3D11Backend(const NativeD3D11BackendOptions& options) {
    UINT flags=0;
    if(options.debug_layer) flags|=D3D11_CREATE_DEVICE_DEBUG;
    const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
    HRESULT created=D3D11CreateDevice(nullptr,options.prefer_warp?D3D_DRIVER_TYPE_WARP
                                                                 :D3D_DRIVER_TYPE_HARDWARE,
                                      nullptr,flags,&level,1,D3D11_SDK_VERSION,&device_,nullptr,
                                      &context_);
    if(FAILED(created) && options.debug_layer) {
      // The debug layer is an optional Windows feature; without it device
      // creation fails outright rather than quietly running unvalidated.
      debug_layer_refused_=true;
      created=D3D11CreateDevice(nullptr,options.prefer_warp?D3D_DRIVER_TYPE_WARP
                                                           :D3D_DRIVER_TYPE_HARDWARE,
                                nullptr,flags&~UINT(D3D11_CREATE_DEVICE_DEBUG),&level,1,
                                D3D11_SDK_VERSION,&device_,nullptr,&context_);
    }
    Require(created,"device creation");
    recorder_=std::make_unique<D3D11Recorder>(*device_.Get(),*context_.Get());
  }
  ~D3D11Backend() override {
    // Only when the device is ours. Clearing a context the caller still uses
    // would unbind everything the unported paths had set, and the symptom
    // would be a frame going blank at shutdown for no visible reason.
    if(owns_device_ && context_) context_->ClearState();
  }

  std::string_view name() const override { return "d3d11"; }

  std::unique_ptr<NativeBackendBuffer> CreateBuffer(const NativeBackendBufferDesc& desc,
                                                    std::span<const uint8_t> initial) override {
    if(!desc.bytes) throw std::runtime_error("a zero-byte buffer cannot be created");
    UINT bind=0;
    if(desc.vertex) bind|=D3D11_BIND_VERTEX_BUFFER;
    if(desc.index) bind|=D3D11_BIND_INDEX_BUFFER;
    if(desc.constant) bind|=D3D11_BIND_CONSTANT_BUFFER;
    const D3D11_BUFFER_DESC description{static_cast<UINT>(desc.bytes),D3D11_USAGE_DEFAULT,bind,0,0,0};
    const D3D11_SUBRESOURCE_DATA data{initial.data(),0,0};
    if(!initial.empty() && initial.size()>desc.bytes)
      throw std::runtime_error("initial buffer contents are larger than the buffer");
    ComPtr<ID3D11Buffer> buffer;
    Require(device_->CreateBuffer(&description,initial.empty()?nullptr:&data,&buffer),"buffer creation");
    return std::make_unique<D3D11Buffer>(std::move(buffer),desc.bytes);
  }

  std::unique_ptr<NativeBackendTexture> CreateTexture(const NativeBackendTextureDesc& desc,
                                                      std::span<const uint8_t> initial) override {
    D3D11_TEXTURE2D_DESC description{};
    description.Width=desc.width;
    description.Height=desc.height;
    description.MipLevels=desc.levels?desc.levels:1;
    description.ArraySize=1;
    description.Format=static_cast<DXGI_FORMAT>(desc.format);
    description.SampleDesc={1,0};
    description.Usage=D3D11_USAGE_DEFAULT;
    description.BindFlags=D3D11_BIND_SHADER_RESOURCE;

    // The seam's initial contents are tightly packed, smallest stride, level
    // after level - the same layout the D3D12 backend expects, so a caller
    // does not have to know which backend it is talking to.
    std::vector<D3D11_SUBRESOURCE_DATA> levels;
    size_t offset=0;
    const auto info=DescribeNativeDxgiFormat(description.Format);
    for(UINT level=0;level<description.MipLevels && !initial.empty();++level) {
      const uint32_t width=(std::max)(1u,desc.width>>level),height=(std::max)(1u,desc.height>>level);
      // Blocks, not texels: a 2x2 level of a BC format still costs a whole
      // 4x4 block, and width x bytes would under-count it.
      const size_t pitch=static_cast<size_t>(NativeDxgiRowPitch(info,width));
      const size_t level_bytes=static_cast<size_t>(NativeDxgiLevelBytes(info,width,height));
      if(offset+level_bytes>initial.size())
        throw std::runtime_error("initial texture contents are "+std::to_string(initial.size())+
                                 " bytes, too few for "+std::to_string(description.MipLevels)+" levels");
      levels.push_back({initial.data()+offset,static_cast<UINT>(pitch),0});
      offset+=level_bytes;
    }
    ComPtr<ID3D11Texture2D> texture;
    Require(device_->CreateTexture2D(&description,levels.empty()?nullptr:levels.data(),&texture),
            "texture creation");
    ComPtr<ID3D11ShaderResourceView> view;
    Require(device_->CreateShaderResourceView(texture.Get(),nullptr,&view),"texture view creation");
    return std::make_unique<D3D11Texture>(std::move(texture),std::move(view),desc.width,desc.height);
  }

  std::unique_ptr<NativeBackendRenderTarget> CreateRenderTarget(const NativeBackendTextureDesc& desc) override {
    D3D11_TEXTURE2D_DESC description{};
    description.Width=desc.width;
    description.Height=desc.height;
    description.MipLevels=1;
    description.ArraySize=1;
    description.Format=static_cast<DXGI_FORMAT>(desc.format);
    description.SampleDesc={desc.samples?desc.samples:1,0};
    description.Usage=D3D11_USAGE_DEFAULT;
    description.BindFlags=desc.depth?D3D11_BIND_DEPTH_STENCIL:D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> texture;
    Require(device_->CreateTexture2D(&description,nullptr,&texture),"render target creation");
    ComPtr<ID3D11RenderTargetView> colour;
    ComPtr<ID3D11DepthStencilView> depth;
    if(desc.depth) Require(device_->CreateDepthStencilView(texture.Get(),nullptr,&depth),"depth view creation");
    else Require(device_->CreateRenderTargetView(texture.Get(),nullptr,&colour),"render target view creation");
    return std::make_unique<D3D11RenderTarget>(std::move(texture),std::move(colour),std::move(depth),
                                               desc.width,desc.height);
  }

  std::unique_ptr<NativeBackendQuery> CreateQuery(NativeBackendQueryKind kind) override {
    if(kind!=NativeBackendQueryKind::Occlusion)
      throw std::runtime_error("only occlusion queries are implemented in the D3D11 backend");
    const D3D11_QUERY_DESC desc{D3D11_QUERY_OCCLUSION,0};
    ComPtr<ID3D11Query> query;
    Require(device_->CreateQuery(&desc,&query),"query creation");
    return std::make_unique<D3D11Query>(std::move(query));
  }

  bool ReadQuery(NativeBackendQuery& query, std::span<uint8_t> result) override {
    if(result.size()<sizeof(uint64_t))
      throw std::runtime_error("an occlusion query result needs 8 bytes");
    UINT64 samples=0;
    // DONT_FLUSH so this never blocks, which is what the interface promises;
    // without it GetData can push the queue and stall the caller.
    if(context_->GetData(static_cast<D3D11Query&>(query).query(),&samples,sizeof(samples),
                         D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK) return false;
    std::memcpy(result.data(),&samples,sizeof(samples));
    return true;
  }

  NativeBackendSampler& CreateSampler(const NativeBackendSamplerDesc& desc) override {
    D3D11_SAMPLER_DESC native{};
    const bool anisotropic=desc.min==NativeBackendFilter::Anisotropic||
                           desc.mag==NativeBackendFilter::Anisotropic||
                           desc.mip==NativeBackendFilter::Anisotropic;
    native.Filter=anisotropic?D3D11_FILTER_ANISOTROPIC
                             :D3D11_ENCODE_BASIC_FILTER(FilterType(desc.min),FilterType(desc.mag),
                                                        FilterType(desc.mip),
                                                        D3D11_FILTER_REDUCTION_TYPE_STANDARD);
    native.AddressU=AddressMode(desc.u);
    native.AddressV=AddressMode(desc.v);
    native.AddressW=AddressMode(desc.w);
    native.MipLODBias=desc.mip_lod_bias;
    native.MaxAnisotropy=anisotropic?(desc.max_anisotropy?desc.max_anisotropy:16):1;
    // ALWAYS, matching what the renderer's own sampler decode emits. It is
    // ignored for the standard reduction these filters use, but a backend that
    // quietly differs from the thing it replaces is a bad habit to start.
    native.ComparisonFunc=D3D11_COMPARISON_ALWAYS;
    for(size_t index=0;index<4;++index) native.BorderColor[index]=desc.border[index];
    native.MinLOD=desc.min_lod;
    native.MaxLOD=desc.max_lod;
    std::string key(reinterpret_cast<const char*>(&native),sizeof(native));
    auto found=samplers_.find(key);
    if(found==samplers_.end()) {
      ComPtr<ID3D11SamplerState> state;
      Require(device_->CreateSamplerState(&native,&state),"sampler creation");
      found=samplers_.emplace(std::move(key),std::make_unique<D3D11Sampler>(std::move(state))).first;
    }
    return *found->second;
  }

  NativeBackendPipeline& CreatePipeline(const NativeBackendPipelineDesc& desc) override {
    // Keyed exactly as the D3D12 backend keys its pipeline cache, so the same
    // description produces one object on both and a caller cannot come to
    // depend on one of them handing back a fresh object each time.
    std::string key;
    const auto add=[&key](const void* bytes,size_t size) {
      key.append(static_cast<const char*>(bytes),size);
    };
    add(&desc.vertex_id,sizeof(desc.vertex_id));
    add(&desc.pixel_id,sizeof(desc.pixel_id));
    add(&desc.input_layout_id,sizeof(desc.input_layout_id));
    add(desc.state.data(),desc.state.size()*sizeof(uint32_t));
    add(&desc.topology,sizeof(desc.topology));
    add(&desc.render_targets,sizeof(desc.render_targets));
    add(desc.rtv_format.data(),desc.rtv_format.size()*sizeof(uint32_t));
    add(&desc.dsv_format,sizeof(desc.dsv_format));
    add(&desc.sample_count,sizeof(desc.sample_count));
    if(const auto found=pipelines_.find(key);found!=pipelines_.end()) return *found->second;

    auto pipeline=std::make_unique<D3D11Pipeline>();
    Require(device_->CreateVertexShader(desc.vertex.data(),desc.vertex.size(),nullptr,&pipeline->vertex),
            "vertex shader creation");
    Require(device_->CreatePixelShader(desc.pixel.data(),desc.pixel.size(),nullptr,&pipeline->pixel),
            "pixel shader creation");
    std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
    elements.reserve(desc.input_layout.size());
    for(const auto& element:desc.input_layout)
      elements.push_back({element.semantic,element.semantic_index,
                          static_cast<DXGI_FORMAT>(element.format),element.slot,element.offset,
                          element.per_instance?D3D11_INPUT_PER_INSTANCE_DATA:D3D11_INPUT_PER_VERTEX_DATA,
                          element.step_rate});
    if(!elements.empty())
      Require(device_->CreateInputLayout(elements.data(),static_cast<UINT>(elements.size()),
                                         desc.vertex.data(),desc.vertex.size(),&pipeline->layout),
              "input layout creation");

    // The same shared decode the D3D12 backend uses, so the two cannot
    // disagree about what a guest render state word means.
    const auto decoded=DecodeNativeRenderState(desc.state);
    D3D11_BLEND_DESC blend{};
    auto& rt=blend.RenderTarget[0];
    rt.BlendEnable=decoded.blend_enable;
    rt.SrcBlend=static_cast<D3D11_BLEND>(decoded.src_color);
    rt.DestBlend=static_cast<D3D11_BLEND>(decoded.dst_color);
    rt.BlendOp=static_cast<D3D11_BLEND_OP>(decoded.color_op);
    rt.SrcBlendAlpha=static_cast<D3D11_BLEND>(decoded.src_alpha);
    rt.DestBlendAlpha=static_cast<D3D11_BLEND>(decoded.dst_alpha);
    rt.BlendOpAlpha=static_cast<D3D11_BLEND_OP>(decoded.alpha_op);
    rt.RenderTargetWriteMask=decoded.write_mask;
    Require(device_->CreateBlendState(&blend,&pipeline->blend),"blend state creation");
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable=decoded.depth_enable;
    depth.DepthWriteMask=decoded.depth_write?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc=static_cast<D3D11_COMPARISON_FUNC>(decoded.depth_func);
    Require(device_->CreateDepthStencilState(&depth,&pipeline->depth),"depth state creation");
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode=static_cast<D3D11_FILL_MODE>(decoded.fill);
    raster.CullMode=static_cast<D3D11_CULL_MODE>(decoded.cull);
    raster.FrontCounterClockwise=decoded.front_counter_clockwise;
    raster.DepthClipEnable=decoded.depth_clip;
    // Always on, and the viewport-sized rectangle set by SetViewport stands
    // when the caller asks for no scissor. D3D12 has no enable flag at all, so
    // leaving it off here would make the two backends clip differently.
    raster.ScissorEnable=TRUE;
    raster.MultisampleEnable=desc.sample_count>1;
    Require(device_->CreateRasterizerState(&raster,&pipeline->raster),"raster state creation");
    pipeline->topology=Topology(desc.topology);
    pipeline->requires_blend_factor=decoded.requires_blend_factor;

    auto& stored=pipelines_.emplace(std::move(key),std::move(pipeline)).first->second;
    return *stored;
  }

  NativeBackendRecorder& Recorder(uint32_t index) override {
    if(!open_) throw std::runtime_error("the D3D11 backend has no frame open; call BeginFrame first");
    if(index) throw std::runtime_error("D3D11 has one immediate context and so one recorder");
    return *recorder_;
  }
  uint32_t RecorderCount() const override { return 1; }
  bool SupportsParallelRecording() const override { return false; }

  void BeginFrame() override {
    if(open_) throw std::runtime_error("a D3D11 frame is already open");
    recorder_->Begin();
    open_=true;
  }
  void Submit() override {
    if(!open_) throw std::runtime_error("Submit with no frame open");
    open_=false;
  }

  std::vector<uint8_t> ReadRenderTarget(NativeBackendRenderTarget& target) override {
    if(open_) throw std::runtime_error("ReadRenderTarget cannot run inside an open frame");
    auto& concrete=static_cast<D3D11RenderTarget&>(target);
    D3D11_TEXTURE2D_DESC description{};
    concrete.texture()->GetDesc(&description);

    ComPtr<ID3D11Texture2D> source=concrete.texture();
    if(description.SampleDesc.Count>1) {
      // A multisampled surface cannot be mapped, so it is resolved first.
      auto resolved_desc=description;
      resolved_desc.SampleDesc={1,0};
      resolved_desc.BindFlags=0;
      ComPtr<ID3D11Texture2D> resolved;
      Require(device_->CreateTexture2D(&resolved_desc,nullptr,&resolved),"resolve texture creation");
      context_->ResolveSubresource(resolved.Get(),0,concrete.texture(),0,description.Format);
      source=resolved;
    }
    auto staging_desc=description;
    staging_desc.SampleDesc={1,0};
    staging_desc.Usage=D3D11_USAGE_STAGING;
    staging_desc.BindFlags=0;
    staging_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Require(device_->CreateTexture2D(&staging_desc,nullptr,&staging),"staging texture creation");
    context_->CopyResource(staging.Get(),source.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    Require(context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"staging map");
    // Render targets are never block compressed, but the shared helper is used
    // anyway so an unexpected format is refused rather than mis-sized.
    const auto info=DescribeNativeDxgiFormat(description.Format);
    const size_t pitch=static_cast<size_t>(NativeDxgiRowPitch(info,description.Width));
    std::vector<uint8_t> pixels(pitch*description.Height);
    for(UINT row=0;row<description.Height;++row)
      std::memcpy(pixels.data()+static_cast<size_t>(row)*pitch,
                  static_cast<const uint8_t*>(mapped.pData)+static_cast<size_t>(row)*mapped.RowPitch,
                  pitch);
    context_->Unmap(staging.Get(),0);
    return pixels;
  }

  std::unique_ptr<NativeBackendTexture> OpenSharedTexture(void* handle,
                                                          const NativeBackendTextureDesc& desc) override {
    if(!handle) return {};
    ComPtr<ID3D11Device1> extended;
    if(FAILED(device_.As(&extended))) return {};
    ComPtr<ID3D11Texture2D> texture;
    if(FAILED(extended->OpenSharedResource1(handle,IID_PPV_ARGS(&texture)))) return {};
    ComPtr<ID3D11ShaderResourceView> view;
    if(FAILED(device_->CreateShaderResourceView(texture.Get(),nullptr,&view))) return {};
    return std::make_unique<D3D11Texture>(std::move(texture),std::move(view),desc.width,desc.height);
  }

  bool WaitSharedFence(void* handle, uint64_t value) override {
    if(!handle) return false;
    if(!shared_fence_ || shared_fence_handle_!=handle) {
      shared_fence_.Reset();
      ComPtr<ID3D11Device5> fencing;
      if(FAILED(device_.As(&fencing))) return false;
      if(FAILED(fencing->OpenSharedFence(handle,IID_PPV_ARGS(&shared_fence_)))) return false;
      shared_fence_handle_=handle;
    }
    ComPtr<ID3D11DeviceContext4> fenced;
    if(FAILED(context_.As(&fenced))) return false;
    fenced->Wait(shared_fence_.Get(),value);
    return true;
  }

  std::vector<std::string> DrainValidationMessages() override {
    std::vector<std::string> found;
    if(debug_layer_refused_)
      found.push_back("the D3D11 debug layer was asked for and is not installed, so nothing is validated");
    if(!messages_) {
      if(FAILED(device_.As(&messages_))) return found;
      D3D11_MESSAGE_SEVERITY severities[]={D3D11_MESSAGE_SEVERITY_CORRUPTION,D3D11_MESSAGE_SEVERITY_ERROR};
      D3D11_INFO_QUEUE_FILTER filter{};
      filter.AllowList.NumSeverities=2;
      filter.AllowList.pSeverityList=severities;
      messages_->PushRetrievalFilter(&filter);
    }
    const UINT64 count=messages_->GetNumStoredMessagesAllowedByRetrievalFilter();
    for(UINT64 index=0;index<count;++index) {
      SIZE_T bytes=0;
      if(FAILED(messages_->GetMessage(index,nullptr,&bytes))||!bytes) continue;
      std::vector<uint8_t> storage(bytes);
      auto* message=reinterpret_cast<D3D11_MESSAGE*>(storage.data());
      if(FAILED(messages_->GetMessage(index,message,&bytes))) continue;
      found.emplace_back(message->pDescription,
                         message->DescriptionByteLength?message->DescriptionByteLength-1:0);
    }
    messages_->ClearStoredMessages();
    return found;
  }

  void AttachWindow(void* window, uint32_t width, uint32_t height) override {
    if(open_) throw std::runtime_error("a window cannot be attached inside an open frame");
    if(!window) throw std::runtime_error("AttachWindow needs a window");
    context_->ClearState();
    back_buffer_.reset();
    swap_chain_.Reset();

    ComPtr<IDXGIDevice> dxgi;
    Require(device_.As(&dxgi),"DXGI device");
    ComPtr<IDXGIAdapter> adapter;
    Require(dxgi->GetAdapter(&adapter),"DXGI adapter");
    ComPtr<IDXGIFactory2> factory;
    Require(adapter->GetParent(IID_PPV_ARGS(&factory)),"DXGI factory");

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width=width;
    desc.Height=height;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc={1,0};
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount=3;
    desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling=DXGI_SCALING_STRETCH;
    desc.AlphaMode=DXGI_ALPHA_MODE_UNSPECIFIED;
    Require(factory->CreateSwapChainForHwnd(device_.Get(),static_cast<HWND>(window),&desc,nullptr,
                                            nullptr,&swap_chain_),"swap chain creation");
    factory->MakeWindowAssociation(static_cast<HWND>(window),DXGI_MWA_NO_ALT_ENTER);

    ComPtr<ID3D11Texture2D> texture;
    Require(swap_chain_->GetBuffer(0,IID_PPV_ARGS(&texture)),"swap chain buffer");
    ComPtr<ID3D11RenderTargetView> view;
    Require(device_->CreateRenderTargetView(texture.Get(),nullptr,&view),"back buffer view creation");
    back_buffer_=std::make_unique<D3D11RenderTarget>(std::move(texture),std::move(view),nullptr,
                                                     width,height);
  }

  NativeBackendRenderTarget* BackBuffer() override { return back_buffer_.get(); }

  void Present(bool vsync) override {
    if(!swap_chain_) throw std::runtime_error("Present with no window attached");
    if(open_) throw std::runtime_error("Present inside an open frame; submit it first");
    Require(swap_chain_->Present(vsync?1:0,0),"present");
  }

 private:
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<ID3D11InfoQueue> messages_;
  ComPtr<ID3D11Fence> shared_fence_;
  void* shared_fence_handle_=nullptr;
  ComPtr<IDXGISwapChain1> swap_chain_;
  std::unique_ptr<D3D11Recorder> recorder_;
  std::unique_ptr<D3D11RenderTarget> back_buffer_;
  std::map<std::string,std::unique_ptr<D3D11Sampler>> samplers_;
  std::map<std::string,std::unique_ptr<D3D11Pipeline>> pipelines_;
  bool open_=false,debug_layer_refused_=false,owns_device_=true;
};
}  // namespace

std::unique_ptr<NativeBackendTexture> AdoptNativeD3D11Texture(
    NativeRenderBackend& backend, ID3D11ShaderResourceView& view, uint32_t width, uint32_t height) {
  if(backend.name()!="d3d11")
    throw std::runtime_error("a D3D11 texture can only be adopted by a D3D11 backend; "
                             "this one is "+std::string(backend.name()));
  return std::make_unique<D3D11Texture>(nullptr,&view,width,height);
}

std::unique_ptr<NativeBackendRenderTarget> AdoptNativeD3D11RenderTarget(
    NativeRenderBackend& backend, ID3D11RenderTargetView* colour, ID3D11DepthStencilView* depth,
    uint32_t width, uint32_t height) {
  if(backend.name()!="d3d11")
    throw std::runtime_error("a D3D11 render target can only be adopted by a D3D11 backend; "
                             "this one is "+std::string(backend.name()));
  if(!colour && !depth) throw std::runtime_error("adopting a render target needs a view");
  return std::make_unique<D3D11RenderTarget>(nullptr,colour,depth,width,height);
}

std::unique_ptr<NativeRenderBackend> AdoptNativeD3D11Backend(ID3D11Device& device,
                                                             ID3D11DeviceContext& context) {
  return std::make_unique<D3D11Backend>(device,context);
}

std::unique_ptr<NativeRenderBackend> CreateNativeD3D11Backend(const NativeD3D11BackendOptions& options) {
  return std::make_unique<D3D11Backend>(options);
}

void RegisterNativeD3D11Backend() {
  static bool registered=false;
  if(registered) return;
  RegisterNativeRenderBackend("d3d11",[]() -> std::unique_ptr<NativeRenderBackend> {
    return CreateNativeD3D11Backend();
  });
  RegisterNativeRenderBackend("d3d11-warp",[]() -> std::unique_ptr<NativeRenderBackend> {
    NativeD3D11BackendOptions options;
    options.prefer_warp=true;
    options.debug_layer=true;
    return CreateNativeD3D11Backend(options);
  });
  registered=true;
}
}  // namespace edf::native

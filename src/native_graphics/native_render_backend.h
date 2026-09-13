#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace edf::native {
// The seam a second graphics backend plugs into.
//
// Deliberately NOT a 1:1 mirror of the 47 D3D11 context methods this renderer
// currently calls. Mirroring D3D11 would force a D3D12 or Vulkan backend to
// emulate D3D11's model badly - independent state objects, implicit hazard
// tracking, a queryable device state - which is the opposite of why those APIs
// are worth moving to. The operations below are the ones the bridge actually
// needs, at a level both models can serve honestly.
//
// Three D3D11 habits are deliberately absent, because they have no equivalent
// and porting them would poison the interface:
//
//   * Reading bound state back from the device. Roughly ten of the current call
//     sites do this (VSGetShader, OMGetRenderTargets, SOGetTargets, ...) to save
//     and restore around foreign work. Neither target API keeps such state, so
//     the recorder is the authority instead and callers ask it, not the device.
//   * SwapDeviceContextState isolation. Replaced by recording into separate
//     command lists, which is what d3d11_frame_handoff.cpp is approximating.
//   * Independent blend/depth/raster/input-layout objects. Both targets fuse
//     these with the shader pair into one pipeline, so the interface takes the
//     whole combination and the backend caches it.
//
// Resource lifetime is the caller's: every Create* returns an owning handle and
// destroying it releases the backend object. Handles are opaque on purpose so a
// backend can hold whatever it needs behind them.
class NativeBackendBuffer {
 public:
  virtual ~NativeBackendBuffer()=default;
  virtual size_t bytes() const=0;
};
class NativeBackendTexture {
 public:
  virtual ~NativeBackendTexture()=default;
  virtual uint32_t width() const=0;
  virtual uint32_t height() const=0;
};
class NativeBackendSampler {
 public:
  virtual ~NativeBackendSampler()=default;
};
// The fused shader pair + render state + vertex layout. Expensive to create on
// both target APIs, so the backend caches these and a cold miss during gameplay
// is a visible hitch rather than a small cost: create them at load where the
// combination is already known.
class NativeBackendPipeline {
 public:
  virtual ~NativeBackendPipeline()=default;
};
class NativeBackendRenderTarget {
 public:
  virtual ~NativeBackendRenderTarget()=default;
  virtual uint32_t width() const=0;
  virtual uint32_t height() const=0;
};
enum class NativeBackendQueryKind : uint32_t { Occlusion, Timestamp, TimestampDisjoint };
class NativeBackendQuery {
 public:
  virtual ~NativeBackendQuery()=default;
};

enum class NativeBackendStage : uint32_t { Vertex, Pixel, Compute, Count };
enum class NativeBackendTopology : uint32_t { TriangleList, TriangleStrip, LineList, PointList };
enum class NativeBackendIndexFormat : uint32_t { Uint16, Uint32 };

struct NativeBackendViewport {
  float x=0,y=0,width=0,height=0,min_depth=0,max_depth=1;
};
struct NativeBackendScissor {
  int32_t left=0,top=0,right=0,bottom=0;
};

// Records work for one thread. On D3D11 this wraps the immediate context and
// there is exactly one; on a second backend there may be several, which is the
// entire point of the migration. Nothing here reads device state back.
class NativeBackendRecorder {
 public:
  virtual ~NativeBackendRecorder()=default;

  virtual void SetPipeline(NativeBackendPipeline& pipeline)=0;
  virtual void SetVertexBuffer(uint32_t slot,NativeBackendBuffer& buffer,uint32_t stride,uint32_t offset)=0;
  virtual void SetIndexBuffer(NativeBackendBuffer& buffer,NativeBackendIndexFormat format,uint32_t offset)=0;
  virtual void SetTopology(NativeBackendTopology topology)=0;

  // Constants are handed over as bytes rather than as a mapped buffer: at the
  // measured ~45,000 material activations a second, how that memory is staged
  // is a backend decision (UpdateSubresource here, an upload ring with fencing
  // on a second backend) and callers must not depend on either.
  virtual void SetConstants(NativeBackendStage stage,uint32_t slot,std::span<const uint8_t> bytes)=0;
  virtual void SetTexture(NativeBackendStage stage,uint32_t slot,NativeBackendTexture* texture)=0;
  virtual void SetSampler(NativeBackendStage stage,uint32_t slot,NativeBackendSampler* sampler)=0;

  virtual void SetRenderTargets(std::span<NativeBackendRenderTarget* const> colors,
                                NativeBackendRenderTarget* depth)=0;
  virtual void SetViewport(const NativeBackendViewport& viewport)=0;
  virtual void SetScissor(const NativeBackendScissor& scissor,bool enabled)=0;

  virtual void ClearColor(NativeBackendRenderTarget& target,const std::array<float,4>& color)=0;
  virtual void ClearDepthStencil(NativeBackendRenderTarget& target,bool depth,bool stencil,
                                 float depth_value,uint8_t stencil_value)=0;

  virtual void Draw(uint32_t vertices,uint32_t first_vertex)=0;
  virtual void DrawIndexed(uint32_t indices,uint32_t first_index,int32_t base_vertex)=0;
  // The reason stage 0 exists: 77.4% of this game's draws are collapsible into
  // a preceding instanced draw, measured with zero register-shape breaks.
  virtual void DrawIndexedInstanced(uint32_t indices,uint32_t instances,
                                    uint32_t first_index,int32_t base_vertex,uint32_t first_instance)=0;

  virtual void CopyTexture(NativeBackendTexture& destination,NativeBackendTexture& source)=0;
  virtual void ResolveTarget(NativeBackendTexture& destination,NativeBackendRenderTarget& source)=0;
  virtual void UpdateBuffer(NativeBackendBuffer& buffer,uint32_t offset,std::span<const uint8_t> bytes)=0;

  virtual void BeginQuery(NativeBackendQuery& query)=0;
  virtual void EndQuery(NativeBackendQuery& query)=0;
  // False when the result is not ready; never blocks.
  virtual bool ReadQuery(NativeBackendQuery& query,std::span<uint8_t> result)=0;

  // State ownership, replacing the D3D11 getters. A caller that needs to run
  // foreign work and restore afterwards asks the recorder, because on a second
  // backend there is nothing to ask the device.
  virtual void PushState()=0;
  virtual void PopState()=0;
};

struct NativeBackendBufferDesc {
  size_t bytes=0;
  bool vertex=false,index=false,constant=false;
  bool dynamic=false; // Rewritten per frame; the backend decides how to stage it.
};
struct NativeBackendTextureDesc {
  uint32_t width=0,height=0,levels=1;
  uint32_t format=0;  // Backend-specific format code; see the backend's mapping.
  bool render_target=false,depth=false;
};

class NativeRenderBackend {
 public:
  virtual ~NativeRenderBackend()=default;
  // Stable identifier for logs, the settings screen and A/B runs.
  virtual std::string_view name() const=0;

  virtual std::unique_ptr<NativeBackendBuffer> CreateBuffer(const NativeBackendBufferDesc& desc,
                                                            std::span<const uint8_t> initial)=0;
  virtual std::unique_ptr<NativeBackendTexture> CreateTexture(const NativeBackendTextureDesc& desc,
                                                              std::span<const uint8_t> initial)=0;
  virtual std::unique_ptr<NativeBackendRenderTarget> CreateRenderTarget(const NativeBackendTextureDesc& desc)=0;
  virtual std::unique_ptr<NativeBackendQuery> CreateQuery(NativeBackendQueryKind kind)=0;

  // One recorder per thread that submits work. D3D11 returns the same immediate
  // recorder every time and rejects a second thread; a second backend hands out
  // independent ones, which is what unlocks parallel submission.
  virtual NativeBackendRecorder& Recorder()=0;
  virtual bool SupportsParallelRecording() const=0;

  // Hand everything recorded so far to the GPU. Ordering between recorders is
  // the backend's responsibility.
  virtual void Submit()=0;
};

// Backends register here; selection is by name so a run can A/B them without a
// rebuild. Only D3D11 exists today - the registry is the seam, not a promise
// that a second backend is present.
const std::vector<std::string>& NativeRenderBackendNames();
// Throws if the name is unknown, so a typo fails loudly at startup rather than
// silently falling back to a backend the caller did not ask for.
std::unique_ptr<NativeRenderBackend> CreateNativeRenderBackend(std::string_view name);
void RegisterNativeRenderBackend(std::string name,
                                 std::unique_ptr<NativeRenderBackend>(*factory)());
}  // namespace edf::native

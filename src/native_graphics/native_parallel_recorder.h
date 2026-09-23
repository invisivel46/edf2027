#pragma once
#include "native_render_backend.h"
#include <functional>

namespace edf::native {
// The producer captures resolved draw state; persistent workers record contiguous
// ranges into independent command lists. Resource mutation is a flush boundary.
// A batch's leading ranges are handed to workers while the producer is still
// capturing it (sized from the same batch in the previous frame), so a flush
// joins only the tail; lists are still submitted in draw order.
// Immutable constant images and shared binding snapshots belong to the frame
// and survive intermediate flushes and saved states; Reset retires them after
// all recording has finished. Constant images are interned by content for the
// frame and bound through SetConstantImage, so a backend can stage each one
// once per submission. Geometry and constant references stay per draw.
// The owner must call Forget before destroying any resource referenced here.
class NativeParallelRecorder final : public NativeBackendRecorder {
 public:
  struct Statistics {
    uint64_t draws=0,batches=0,worker_mask=0,record_ns=0,wait_ns=0;
    uint64_t serial_draws=0,serial_flushes=0;
    uint64_t instanced_draws=0,folded_draws=0;
    uint64_t world_constant_reuses=0,constant_snapshot_bytes=0;
    // SetConstants calls bound to an image the frame already held (the
    // slot's own binding or an interned equal image), and the bytes they did
    // not copy. constant_snapshot_bytes counts only the copies still made.
    uint64_t constant_interned=0,constant_interned_bytes=0;
    // Worker ranges handed over while the producer was still capturing the
    // batch, rather than at the flush that joins them.
    uint64_t streamed_jobs=0;
    // Transient list draws appended to the draw before them (TryAppendTransient).
    uint64_t transient_appends=0;
    uint32_t max_concurrent=0;
  };
  // Recorder 0 holds ordered non-draw work. Workers own recorders 1..N.
  // finish submits these lists in that order, then optionally opens another frame.
  NativeParallelRecorder(std::vector<NativeBackendRecorder*> recorders,
                         std::function<void(bool)> finish,uint32_t minimum_draws=32);
  ~NativeParallelRecorder();
  void Reset();
  void Flush(bool reopen=true);
  void Forget(const void* resource);
  Statistics statistics() const;
  void SetPipeline(NativeBackendPipeline&) override;
  void SetWorldInstancing(bool,bool reuse_constants=true) override;
  void SetTransientBatching(bool) override;
  void SetVertexBuffer(uint32_t,NativeBackendBuffer&,uint32_t,uint32_t) override;
  void SetIndexBuffer(NativeBackendBuffer&,NativeBackendIndexFormat,uint32_t) override;
  void SetTransientVertices(uint32_t,std::span<const uint8_t>,uint32_t) override;
  void SetTransientVerticesOwned(uint32_t,std::vector<uint8_t>&,uint32_t) override;
  void SetTopology(NativeBackendTopology) override;
  void SetBlendFactor(const std::array<float,4>&) override;
  void SetConstants(NativeBackendStage,uint32_t,std::span<const uint8_t>) override;
  void SetTexture(NativeBackendStage,uint32_t,NativeBackendTexture*) override;
  void SetSampler(NativeBackendStage,uint32_t,NativeBackendSampler*) override;
  void SetRenderTargets(std::span<NativeBackendRenderTarget* const>,NativeBackendRenderTarget*) override;
  void SetViewport(const NativeBackendViewport&) override;
  void SetScissor(const NativeBackendScissor&,bool) override;
  void ClearColor(NativeBackendRenderTarget&,const std::array<float,4>&) override;
  void ClearDepthStencil(NativeBackendRenderTarget&,bool,bool,float,uint8_t) override;
  void Draw(uint32_t,uint32_t) override;
  void DrawIndexed(uint32_t,uint32_t,int32_t) override;
  void DrawIndexedInstanced(uint32_t,uint32_t,uint32_t,int32_t,uint32_t) override;
  void CopyTexture(NativeBackendTexture&,NativeBackendTexture&) override;
  void ReleaseSharedTexture(NativeBackendTexture&) override;
  void CopyToShared(NativeBackendSharedSurface&,NativeBackendRenderTarget&) override;
  void ResolveTarget(NativeBackendTexture&,NativeBackendRenderTarget&) override;
  void UpdateBuffer(NativeBackendBuffer&,uint32_t,std::span<const uint8_t>) override;
  void UpdateTexture(NativeBackendTexture&,std::span<const uint8_t>) override;
  void BeginQuery(NativeBackendQuery&) override;
  void EndQuery(NativeBackendQuery&) override;
  void WriteTimestamp(NativeBackendTimestamps&,uint32_t) override;
  void ResolveTimestamps(NativeBackendTimestamps&,uint32_t,uint32_t) override;
  void PushState() override;
  void PopState() override;
  // Flushes (so recorder 0 is the tail of the stream) and hands out recorder
  // 0; EndExternal forwards to it and makes the next serial draw re-send its
  // whole state. Producer state and saved states survive, like a clear.
  NativeBackendRecorder& BeginExternal() override;
  void EndExternal() override;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}

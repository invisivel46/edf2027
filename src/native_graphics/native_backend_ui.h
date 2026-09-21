#pragma once
#include "native_render_backend.h"

namespace edf::native {
struct NativeBackendUiVertex { float x,y,u,v; uint32_t color; };
static_assert(sizeof(NativeBackendUiVertex)==20);
struct NativeBackendUiTexture {
  std::unique_ptr<NativeBackendTexture> texture;
  NativeBackendSampler* sampler=nullptr;
};
struct NativeBackendUiDraw {
  uint32_t count=0,index_offset=0;
  int32_t base_vertex=0;
  bool lines=false;
  NativeBackendScissor scissor{};
  const NativeBackendUiTexture* texture=nullptr;
};
// UI geometry is recorded into the caller's frame, after scene composition.
class NativeBackendUiRenderer {
 public:
  explicit NativeBackendUiRenderer(NativeRenderBackend& backend);
  NativeBackendUiTexture CreateTexture(uint32_t width,uint32_t height,
    std::span<const uint8_t> rgba,bool linear,bool repeat);
  void Upload(NativeBackendRecorder& recorder,std::span<const NativeBackendUiVertex> vertices,
              std::span<const uint16_t> indices);
  void Draw(NativeBackendRecorder& recorder,NativeBackendRenderTarget& target,
            float coordinate_width,float coordinate_height,const NativeBackendUiDraw& draw);
 private:
  NativeRenderBackend& backend_;
  NativeBackendPipeline* triangles_=nullptr;
  NativeBackendPipeline* lines_=nullptr;
  std::unique_ptr<NativeBackendBuffer> vertices_,indices_;
  NativeBackendUiTexture white_;
  size_t vertex_count_=0;
  std::vector<uint16_t> index_values_;
};
}

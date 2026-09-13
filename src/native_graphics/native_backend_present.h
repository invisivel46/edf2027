#pragma once
#include "native_render_backend.h"
#include <cstdint>
#include <memory>

namespace edf::native {
// Presents a shared surface to a window through a backend.
//
// The renderer draws a frame into a surface it shares; this waits for that
// work, draws it across the window, and presents. Which API owns the swap
// chain and the final draw is therefore a backend choice rather than something
// welded into the presenter, which is what lets the window move to D3D12 while
// the passes that fill the surface are still being ported.
//
// Everything is created on first use and kept: the pipeline, the full-screen
// triangle, the sampler, the opened shared texture and the swap chain. A size
// change reattaches the window; a different shared handle reopens the texture.
class NativeBackendWindowPresenter {
 public:
  explicit NativeBackendWindowPresenter(NativeRenderBackend& backend);

  struct SharedSource {
    void* texture=nullptr;
    void* fence=nullptr;
    uint64_t value=0;
    uint32_t width=0,height=0,format=0;
  };

  // False when the source cannot be imported or the window has no area, in
  // which case the caller must fall back rather than assume a frame appeared.
  // Letterboxes rather than stretching, matching the D3D11 compositor.
  bool Present(void* window, uint32_t width, uint32_t height,
               const SharedSource& source, bool vsync);

  uint64_t presented() const { return presented_; }
  // Set once the source could not be imported, so a caller can stop asking.
  bool refused() const { return refused_; }

 private:
  void CreateResources();

  NativeRenderBackend* backend_;
  std::unique_ptr<NativeBackendBuffer> vertices_;
  std::unique_ptr<NativeBackendTexture> source_;
  NativeBackendPipeline* pipeline_=nullptr;
  NativeBackendSampler* sampler_=nullptr;
  void* window_=nullptr;
  void* source_handle_=nullptr;
  uint32_t window_width_=0,window_height_=0;
  uint64_t presented_=0;
  bool ready_=false,refused_=false;
};
}  // namespace edf::native

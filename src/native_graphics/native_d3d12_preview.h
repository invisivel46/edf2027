#pragma once
#include "native_render_backend.h"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace edf::native {
// A window whose every pixel is drawn by a backend, not by the renderer's
// direct D3D11 path.
//
// This is the first place the game actually renders through the seam. It is
// deliberately a separate window: the main presentation path cannot move until
// everything it composites has moved, and a second window can be wrong without
// the game being wrong. What it proves is not small - a backend, selected by
// name at startup, compositing and presenting real game frames inside the
// running game.
//
// Frames arrive by CPU readback of the published snapshot and are uploaded
// again on the backend side. That is a staging step and is stated as one: it
// costs a round trip per frame and exists because it needs no cross-API
// sharing, so the thing being proved here is the backend drawing and
// presenting, not a shared-surface path that would have to be debugged at the
// same time. A real port shares the surface instead.
class NativeD3D12Preview {
 public:
  // Runs on its own thread, which owns the window and is the only thread that
  // touches the backend afterwards. A backend's command recording is not
  // thread-safe, and the alternative - ticking from the game's frame loop -
  // would need a per-frame hook the app does not have.
  //
  // The caller keeps ownership of the backend and must outlive this.
  explicit NativeD3D12Preview(NativeRenderBackend& backend);
  ~NativeD3D12Preview();
  NativeD3D12Preview(const NativeD3D12Preview&)=delete;
  NativeD3D12Preview& operator=(const NativeD3D12Preview&)=delete;

  uint64_t presented() const { return presented_.load(std::memory_order_relaxed); }

 private:
  static LRESULT CALLBACK WindowProcedure(HWND,UINT,WPARAM,LPARAM);
  void Run();
  void CreateResources();
  bool Tick();
  void Draw(const uint8_t* pixels, uint32_t width, uint32_t height);

  NativeRenderBackend* backend_;
  HWND window_=nullptr;
  std::unique_ptr<NativeBackendBuffer> vertices_;
  std::unique_ptr<NativeBackendTexture> frame_;
  NativeBackendPipeline* pipeline_=nullptr;
  NativeBackendSampler* sampler_=nullptr;
  uint32_t frame_width_=0,frame_height_=0,window_width_=0,window_height_=0;
  uint64_t last_sequence_=0;
  std::atomic<uint64_t> presented_{0};
  std::atomic<bool> stop_{false};
  std::thread thread_;
  bool closed_=false,attached_=false;
};
}  // namespace edf::native

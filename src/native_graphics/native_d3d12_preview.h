#pragma once
#include "native_render_backend.h"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
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
// Frames arrive by shared surface where the backend can open one: the
// renderer publishes into a shareable texture and signals a shared fence, and
// this samples it where it lies. Nothing crosses system memory.
//
// The copy path is kept for backends that cannot import a handle, because
// "shared or nothing" would mean a preview that silently shows nothing rather
// than one that is merely slower. Which path a run took is in the log.
class NativeD3D12Preview {
 public:
  // Runs on its own thread, which owns the window and is the only thread that
  // touches the backend afterwards. A backend's command recording is not
  // thread-safe, and the alternative - ticking from the game's frame loop -
  // would need a per-frame hook the app does not have.
  //
  // Owns its backend rather than sharing the renderer's. A backend has one
  // command list and is not thread-safe, and this runs on its own thread: two
  // windows driving one backend corrupted its command allocator after three
  // frames, which is the kind of failure that looks random.
  //
  // Throws if the named backend cannot be created, or if it is the adopted
  // D3D11 one - that shares the renderer's immediate context, so a second
  // thread driving it has the same problem a step further along.
  explicit NativeD3D12Preview(const std::string& backend_name);
  ~NativeD3D12Preview();
  NativeD3D12Preview(const NativeD3D12Preview&)=delete;
  NativeD3D12Preview& operator=(const NativeD3D12Preview&)=delete;

  uint64_t presented() const { return presented_.load(std::memory_order_relaxed); }

 private:
  static LRESULT CALLBACK WindowProcedure(HWND,UINT,WPARAM,LPARAM);
  void Run();
  void CreateResources();
  bool Tick();
  // Returns false when no shared surface is available and the copy path has
  // to be used instead.
  bool DrawShared();
  void Draw(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t format);
  void Composite(NativeBackendTexture& frame, uint32_t width, uint32_t height);

  std::unique_ptr<NativeRenderBackend> owned_backend_;
  NativeRenderBackend* backend_=nullptr;
  HWND window_=nullptr;
  std::unique_ptr<NativeBackendBuffer> vertices_;
  std::unique_ptr<NativeBackendTexture> frame_;
  std::unique_ptr<NativeBackendTexture> shared_frame_;
  void* shared_handle_=nullptr;
  bool shared_refused_=false;
  NativeBackendPipeline* pipeline_=nullptr;
  NativeBackendSampler* sampler_=nullptr;
  uint32_t frame_width_=0,frame_height_=0,frame_format_=0,window_width_=0,window_height_=0;
  uint64_t last_sequence_=0;
  std::atomic<uint64_t> presented_{0};
  std::atomic<bool> stop_{false};
  std::thread thread_;
  bool closed_=false,attached_=false;
};
}  // namespace edf::native

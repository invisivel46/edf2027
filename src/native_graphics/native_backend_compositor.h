#pragma once
#include "native_render_backend.h"
#include "native_display_gamma.h"

namespace edf::native {
// Records the final RGBA8 image directly on the destination backend. The caller
// owns BeginFrame/Submit and may record overlays after Draw, on the same target.
class NativeBackendCompositor {
 public:
  explicit NativeBackendCompositor(NativeRenderBackend& backend);
  void Draw(NativeBackendRecorder& recorder, NativeBackendTexture& source,
            NativeBackendRenderTarget& destination, bool preserve_aspect=true,
            const NativeDisplayGamma* gamma=nullptr);
 private:
  NativeBackendPipeline* pipeline_=nullptr;
  NativeBackendSampler* sampler_=nullptr;
};
}

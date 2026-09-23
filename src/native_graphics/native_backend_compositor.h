#pragma once
#include "native_render_backend.h"
#include "native_display_gamma.h"

namespace edf::native {
// Records the final RGBA8 image directly on the destination backend. The caller
// owns BeginFrame/Submit and may record overlays after Draw, on the same target.
//
// smart_filter (native_display_layout.h ComputeNativePresentFit): the image is
// placed on whole pixels, replicated exactly at whole scale factors >= 2,
// area-filtered when it is larger than its place in the window (a render size
// above the window's), and bilinear otherwise; 1:1 stays bilinear, which
// samples texel centres exactly. Off, the placement and filter are the
// original ones (bilinear, fractional letterbox).
class NativeBackendCompositor {
 public:
  explicit NativeBackendCompositor(NativeRenderBackend& backend);
  void Draw(NativeBackendRecorder& recorder, NativeBackendTexture& source,
            NativeBackendRenderTarget& destination, bool preserve_aspect=true,
            const NativeDisplayGamma* gamma=nullptr, bool smart_filter=false);
 private:
  NativeBackendPipeline* pipeline_=nullptr;
  NativeBackendSampler* sampler_=nullptr;
};
}

#pragma once
#include "native_display_gamma.h"
#include <functional>
#include <optional>
#include <memory>

namespace edf::native {
class NativeBackendCompletion;
struct NativeBackendPublishedFrame {
  void* texture=nullptr;
  void* fence=nullptr;
  uint64_t value=0,sequence=0,generation=0;
  uint32_t width=0,height=0,format=0;
  std::optional<NativeDisplayGamma> gamma;
};
struct NativeBackendFrameCopied {
  void* fence=nullptr; uint64_t value=0;
  std::shared_ptr<NativeBackendCompletion> completion;
};
struct NativeBackendFrameVisitTiming { double lock_ms=0,copy_ms=0,release_ms=0; };
// The callback copies the oldest queued image on its own GPU queue and returns
// an owning completion marker. The queue retains its surface until that copy
// completes. Presentation uses its own short lock, independent of guest loading.
bool VisitNativeBackendFrame(uint64_t after_sequence,
  const std::function<NativeBackendFrameCopied(const NativeBackendPublishedFrame&)>& copy,
  NativeBackendFrameVisitTiming* timing=nullptr);
void SetNativeBackendFrameConsumerActive(bool active);
void SetNativeBackendFrameReadyCallback(std::function<void()> callback);
bool NativeFramerateUnlockActive();
bool VisitNativeBackendFrameMirror(uint64_t after_sequence,
  const std::function<NativeBackendFrameCopied(const NativeBackendPublishedFrame&)>& copy);
}

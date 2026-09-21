#pragma once
#include "native_scene.h"
#include "d3d11_bindings.h"

namespace edf::native {
struct NativeSceneMaterialCapture {
  std::shared_ptr<const NativeSceneMaterial> material;
  NativeSceneMatrix world=kNativeSceneIdentity;
  NativeSceneView camera;
};
// Capture a retail rigid-world material into independently owned native state.
// Rejects skinning/unsupported camera matrices instead of freezing their values.
// Source bindings can be changed or destroyed immediately after this returns.
NativeSceneMaterialCapture CaptureNativeSceneMaterial(
  std::shared_ptr<NativeRenderBackend> backend,NativeBackendPipeline& pipeline,
  const ShaderBindings& vertex,const ShaderBindings& pixel,
  std::optional<std::array<float,4>> blend_factor={},
  std::shared_ptr<const NativeSceneMaterial> previous={});
}

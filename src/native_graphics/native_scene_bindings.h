#pragma once
#include "native_scene.h"
#include "d3d11_bindings.h"

namespace edf::native {
struct NativeSceneMaterialCapture {
  std::shared_ptr<const NativeSceneMaterial> material;
  NativeSceneMatrix world=kNativeSceneIdentity;
  NativeSceneView camera;
};
// Apply source-owned float4 registers using the selected shader's world layout.
// This is independent of the currently bound guest/native shader constants.
void ApplyNativeScenePublishedWorld(NativeSceneMaterialCapture& capture,std::span<const uint8_t,64> registers);
// Capture a retail rigid-world material into independently owned native state.
// Rejects skinning/unsupported camera matrices instead of freezing their values.
// Source bindings can be changed or destroyed immediately after this returns.
// palette admits the model pass's skinned draws: the vertex g_mWorldArray
// array keeps the registers its caller bound (the object's packed palette) in
// the immutable image, and g_mWorld becomes optional (identity and no world
// binding when the shader does not declare it).
NativeSceneMaterialCapture CaptureNativeSceneMaterial(
  std::shared_ptr<NativeRenderBackend> backend,NativeBackendPipeline& pipeline,
  const ShaderBindings& vertex,const ShaderBindings& pixel,
  std::optional<std::array<float,4>> blend_factor={},
  std::shared_ptr<const NativeSceneMaterial> previous={},bool palette=false);
// Whether the capture's material binds a world matrix (ApplyNativeScenePublishedWorld applies).
bool NativeSceneCaptureBindsWorld(const NativeSceneMaterialCapture& capture);
}

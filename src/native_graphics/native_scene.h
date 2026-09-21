#pragma once
#include "d3d11_mesh.h"
#include <atomic>
#include <map>

namespace edf::native {
using NativeSceneMatrix=std::array<float,16>;
inline constexpr NativeSceneMatrix kNativeSceneIdentity{
  1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

// Canonical CPU matrices use row vectors. Packing into a shader is explicit.
enum class NativeSceneMatrixSource { World, View, Projection, ViewProjection, WorldViewProjection, ViewTranspose };
inline NativeSceneMatrix NativeSceneTranspose(const NativeSceneMatrix& matrix) {
  NativeSceneMatrix result;
  for(size_t row=0;row<4;++row) for(size_t col=0;col<4;++col) result[row*4+col]=matrix[col*4+row];
  return result;
}
struct NativeSceneMatrixBinding {
  NativeSceneMatrixSource source;
  uint32_t offset;
  bool column_major=false;
  bool operator==(const NativeSceneMatrixBinding&) const=default;
};
struct NativeSceneConstant {
  NativeBackendStage stage=NativeBackendStage::Vertex;
  uint32_t slot=0;
  std::vector<uint8_t> bytes;
  std::vector<NativeSceneMatrixBinding> matrices;
  bool operator==(const NativeSceneConstant&) const=default;
};
struct NativeSceneTexture {
  uint32_t slot=0;
  std::shared_ptr<NativeBackendTexture> texture;
  bool operator==(const NativeSceneTexture&) const=default;
};
struct NativeSceneSampler {
  uint32_t slot=0;
  // Owned by the material's backend, including an explicit unbound slot.
  NativeBackendSampler* sampler=nullptr;
};

// Immutable material generation. Backend owns pipelines/samplers; this object
// retains that backend and all texture generations used by the material.
class NativeSceneMaterial {
 public:
  NativeSceneMaterial(std::shared_ptr<NativeRenderBackend> backend,
    NativeBackendPipeline& pipeline,std::vector<NativeSceneConstant> constants,
    std::vector<NativeSceneTexture> textures={},std::vector<NativeSceneSampler> samplers={},
    std::optional<std::array<float,4>> blend_factor={});
  NativeSceneMaterial(const NativeSceneMaterial&)=delete;
  NativeSceneMaterial& operator=(const NativeSceneMaterial&)=delete;
  const NativeRenderBackend* backend() const { return backend_.get(); }
  bool Equivalent(const NativeSceneMaterial& other) const;
  uint64_t fingerprint() const { return fingerprint_; }
  NativeBackendPipeline* pipeline() const { return pipeline_; }
  const auto& constants() const { return constants_; }
  const auto& textures() const { return textures_; }
  const auto& samplers() const { return samplers_; }
  const auto& blend_factor() const { return blend_factor_; }
 private:
  friend class NativeSceneRenderer;
  std::shared_ptr<NativeRenderBackend> backend_;
  NativeBackendPipeline* pipeline_;
  std::vector<NativeSceneConstant> constants_;
  std::vector<NativeSceneTexture> textures_;
  std::vector<std::pair<uint32_t,NativeBackendSampler*>> samplers_;
  std::optional<std::array<float,4>> blend_factor_;
  std::optional<NativeSceneMatrixBinding> instance_world_;
  uint64_t fingerprint_=0;
};

struct NativeSceneBounds {
  std::array<float,3> minimum{},maximum{};
  bool operator==(const NativeSceneBounds&) const=default;
};
struct NativeSceneObject {
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  std::shared_ptr<const NativeSceneMaterial> material;
  NativeSceneMatrix world=kNativeSceneIdentity;
  std::optional<NativeSceneBounds> bounds;
  // Explicit pass order, including transparent order. The renderer never sorts
  // by material across this order; only adjacent compatible objects can batch.
  uint64_t order=0;
  bool visible=true;
  bool operator==(const NativeSceneObject&) const=default;
};
struct NativeSceneInstance {
  uint64_t id=0,changed_tick=0;
  NativeSceneObject object;
  NativeSceneMatrix previous=kNativeSceneIdentity;
};
struct NativeSceneSnapshot {
  uint64_t tick=0;
  std::vector<std::shared_ptr<const NativeSceneInstance>> instances;
};

// Single simulation producer; readers acquire immutable snapshots atomically.
// IDs are native lifetime tokens, never guest pointers, and are never reused.
// Release snapshots/resources on the backend's serialized ownership thread.
class NativeSceneDatabase {
 public:
  uint64_t Create(NativeSceneObject object);
  void Update(uint64_t id,NativeSceneObject object);
  bool Remove(uint64_t id);
  void Clear();
  std::shared_ptr<const NativeSceneSnapshot> Publish(uint64_t tick);
  // During migration the guest can supply visibility/order while the objects
  // and assets remain native. This selection uses current state, without adding
  // a second interpolation to transforms already sampled by the guest adapter.
  NativeSceneSnapshot Select(std::span<const uint64_t> ids);
  std::shared_ptr<const NativeSceneInstance> SelectOne(uint64_t id);
  std::shared_ptr<const NativeSceneSnapshot> Acquire() const { return published_.load(); }
 private:
  struct Entry {
    NativeSceneObject object;
    std::shared_ptr<const NativeSceneInstance> published;
    std::shared_ptr<const NativeSceneInstance> selected;
    bool dirty=true;
  };
  std::map<uint64_t,Entry> objects_;
  std::atomic<std::shared_ptr<const NativeSceneSnapshot>> published_;
  uint64_t next_id_=1;
};

// Camera matrices are supplied by the native camera controller for THIS render,
// independently of the simulation snapshot. No guest memory is read here.
struct NativeSceneView {
  NativeSceneMatrix view=kNativeSceneIdentity,projection=kNativeSceneIdentity;
  // Some retail passes publish only a combined camera matrix. A native camera
  // controller may supply that directly; otherwise view*projection is used.
  std::optional<NativeSceneMatrix> view_projection;
  NativeBackendViewport viewport;
  NativeBackendScissor scissor;
  bool scissor_enabled=false;
};
struct NativeSceneRenderStatistics {
  uint64_t visible=0,culled=0,draws=0,instanced_draws=0;
};
class NativeSceneRenderer {
 public:
  // Caller owns BeginFrame/targets/clears/Submit and retains the snapshot until
  // recording is submitted. This function owns all scene shader bindings.
  NativeSceneRenderStatistics Render(NativeRenderBackend& backend,
    const NativeSceneSnapshot& snapshot,const NativeSceneView& view,float fraction);
 private:
  struct Visible {
    const NativeSceneInstance* instance;
    NativeSceneMatrix world;
  };
  std::vector<Visible> visible_;
  std::vector<uint8_t> constants_scratch_,instances_scratch_;
};
}

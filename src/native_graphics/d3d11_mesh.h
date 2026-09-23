#pragma once
#include "native_input_layout.h"
#include "mesh_key.h"
#include "d3d11_effect.h"
#include "native_render_backend.h"
#include <array>
#include <map>
#include <memory>
#include <optional>

namespace edf::native {
struct NativeVertexAttribute {
  UINT guest_offset,host_offset,components;
  uint32_t type;
  bool integer,defaulted=false;
  bool operator==(const NativeVertexAttribute&) const = default;
};
// Owns a layout-specific native allocation referencing an immutable CPU snapshot.
// Different conversion layouts can retain the same validated source generation.
// Immutable allocations may be shared; dynamic allocations remain mesh-local.
class NativeVertexBuffer {
 public:
  bool MatchesSource(std::span<const uint8_t> bytes) const;
  bool OwnsSource(std::span<const uint8_t> bytes) const {
    return bytes.size()==source_bytes_ && bytes.data()==source_->data()+source_offset_;
  }
  size_t SourceBytes() const { return source_bytes_; }
  size_t SourceOffset() const { return source_offset_; }
  // Backing resource contents, not necessarily just this conversion's range.
  const std::shared_ptr<const std::vector<uint8_t>>& SourceSnapshot() const { return source_; }
  // Conservatively charge the entire retained backing allocation per mesh.
  size_t StorageBytes() const { return source_->size()+size_t(stride_)*vertex_count_+attributes_.size()*sizeof(NativeVertexAttribute); }
 private:
  friend class NativeIndexedMesh;
  NativeVertexBuffer(NativeRenderBackend& backend,std::vector<NativeVertexAttribute> attributes,
    uint32_t guest_stride,uint32_t native_stride,std::span<const uint8_t> source,bool dynamic,
    std::shared_ptr<const std::vector<uint8_t>> validated_snapshot={},size_t snapshot_offset=0);
  std::vector<uint8_t> ConvertVertices(std::span<const uint8_t> source) const;
  // The same conversion into a caller-owned vector, so a per-draw caller can
  // reuse one instead of allocating.
  void ConvertVerticesInto(std::span<const uint8_t> source,std::vector<uint8_t>& native) const;
  void Update(ID3D11DeviceContext& context,std::span<const uint8_t> source);
  void Update(NativeBackendRecorder& recorder,std::span<const uint8_t> source);
  // The backend that made storage_, kept for the identity checks that used to
  // compare devices: sharing a converted buffer across backends would bind a
  // resource one of them has never seen.
  NativeRenderBackend* backend_=nullptr;
  std::unique_ptr<NativeBackendBuffer> storage_;
  // The same buffer, for the draw and the update - both still D3D11. They go
  // when the draw path itself moves onto a recorder.
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer_;
  std::vector<NativeVertexAttribute> attributes_;
  std::shared_ptr<const std::vector<uint8_t>> source_;
  size_t source_offset_,source_bytes_;
  uint32_t guest_stride_,stride_,vertex_count_;
  bool dynamic_vertices_;
};
// Immutable native generation of one guest index resource. Mesh/shader variants
// share it; replacement never mutates storage referenced by earlier draws.
class NativeIndexBuffer {
 public:
  NativeIndexBuffer(NativeRenderBackend& backend,std::span<const uint8_t> bytes,uint32_t width,
    std::shared_ptr<const std::vector<uint8_t>> contents={});
  bool Matches(NativeRenderBackend& backend,std::span<const uint8_t> bytes,uint32_t width) const;
  bool OwnsSource(NativeRenderBackend& backend,std::span<const uint8_t> bytes,uint32_t width) const {
    return backend_==&backend && width_==width && bytes.size()==source_->size() && bytes.data()==source_->data();
  }
  const std::shared_ptr<const std::vector<uint8_t>>& SourceSnapshot() const { return source_; }
  size_t StorageBytes() const { return source_->size()*2+values_.size()*sizeof(uint32_t); }
 private:
  friend class NativeIndexedMesh;
  NativeRenderBackend* backend_=nullptr;
  std::unique_ptr<NativeBackendBuffer> storage_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> buffer_;
  std::shared_ptr<const std::vector<uint8_t>> source_;
  std::vector<uint32_t> values_;
  uint32_t width_,minimum_=UINT32_MAX,maximum_=0;
  DXGI_FORMAT format_;
};
// Single-stream geometry with float attributes, UBYTE4 indices and D3DCOLOR.
// Packed indices expand to float4/int4/uint4 according to the native shader's
// reflected input signature; packed colors expand to normalized float4.
// Missing known semantics use the guest declaration binder's (0,0,0,1) default.
class NativeIndexedMesh {
 public:
  NativeIndexedMesh(const NativeIndexedMesh&)=delete;
  NativeIndexedMesh& operator=(const NativeIndexedMesh&)=delete;
  class PreparedDraw {
   public:
    void Draw(NativeBackendRecorder& recorder) const;
   private:
    friend class NativeIndexedMesh;
    PreparedDraw(const NativeIndexedMesh* mesh,uint32_t first,uint32_t count,int32_t base)
      :mesh_(mesh),first_(first),count_(count),base_(base) {}
    const NativeIndexedMesh* mesh_;
    uint32_t first_,count_;
    int32_t base_;
  };
  // Bound to this mesh's immutable indices and fixed vertex extent. The
  // returned reference lives until the next PrepareDraw or mesh destruction.
  const PreparedDraw& PrepareDraw(uint32_t first,uint32_t count,int32_t base=0);
  const PreparedDraw* FindPreparedDraw(uint32_t first,uint32_t count,int32_t base=0) const;
  // A scene may outlive the guest resource and the mesh cache entry. Retain
  // immutable GPU generations, never the PreparedDraw's borrowed mesh pointer.
  class RetainedDraw {
   public:
    RetainedDraw(const RetainedDraw&)=default;
    RetainedDraw(RetainedDraw&&)=default;
    RetainedDraw& operator=(const RetainedDraw&)=delete;
    RetainedDraw& operator=(RetainedDraw&&)=delete;
    void Draw(NativeBackendRecorder& recorder) const;
    // Caller binds an instanced pipeline and the matching instance stream.
    void DrawInstanced(NativeBackendRecorder& recorder,uint32_t instances) const;
    const NativeOwnedInputLayout& input_layout() const { return *layout_; }
    const NativeRenderBackend* backend() const { return backend_.get(); }
   private:
    friend class NativeIndexedMesh;
    RetainedDraw(std::shared_ptr<NativeRenderBackend> backend,
      const NativeIndexedMesh& mesh,uint32_t first,uint32_t count,int32_t base);
    void Bind(NativeBackendRecorder& recorder) const;
    // Declaration order keeps the backend alive through resource destruction.
    std::shared_ptr<NativeRenderBackend> backend_;
    std::shared_ptr<const NativeVertexBuffer> vertices_;
    std::shared_ptr<const NativeIndexBuffer> indices_;
    std::shared_ptr<const NativeOwnedInputLayout> layout_;
    uint32_t stride_,first_,count_;
    int32_t base_;
  };
  RetainedDraw RetainDraw(std::shared_ptr<NativeRenderBackend> backend,
    uint32_t first,uint32_t count,int32_t base=0) const;
  enum class IndexReuse { RequireMatch, ReplaceStale };
  NativeIndexedMesh(NativeRenderBackend& backend,const NativeShader& vertex_shader,
    std::span<const uint8_t> guest_declaration,uint32_t stride,
    std::span<const uint8_t> guest_vertices,std::span<const uint8_t> guest_indices,
    uint32_t index_bytes,bool dynamic_vertices=false,std::shared_ptr<const NativeIndexBuffer> index_storage={},
    std::shared_ptr<NativeVertexBuffer> vertex_storage={},IndexReuse index_reuse=IndexReuse::RequireMatch,
    std::shared_ptr<const std::vector<uint8_t>> vertex_contents={},size_t contents_offset=0,
    std::shared_ptr<const std::vector<uint8_t>> index_contents={});
  // Same declaration/count only. WRITE_DISCARD preserves queued draws while
  // replacing changing immediate geometry without creating another GPU buffer.
  void UpdateVertices(ID3D11DeviceContext& context,std::span<const uint8_t> vertices);
  void UpdateVertices(NativeBackendRecorder& recorder,std::span<const uint8_t> vertices);
  void Draw(ID3D11DeviceContext& context,uint32_t first_index,uint32_t index_count,int32_t base_vertex=0) const;
  // The same draw through a recorder. The pipeline carries the vertex layout
  // and the shaders, so the caller sets that; this sets the buffers and issues
  // the draw, which is all that is per-mesh about it.
  void Draw(NativeBackendRecorder& recorder,uint32_t first_index,uint32_t index_count,
            int32_t base_vertex=0) const;
  void ValidateDraw(uint32_t first_index,uint32_t index_count,int32_t base_vertex=0) const;
  // Diagnostic float3 input from the exact CPU generations used by this mesh.
  // Attribute offset is relative to the guest stride; no guest memory is read.
  // Up to 65536 indexed samples, independent of primitive boundaries.
  std::vector<std::array<float,3>> CaptureSourceFloat3(uint32_t first_index,
    uint32_t index_count,int32_t base_vertex,uint32_t attribute_offset) const;
  void DrawLines(ID3D11DeviceContext& context,uint32_t first_index,uint32_t index_count,int32_t base_vertex=0) const;
  void DrawLines(NativeBackendRecorder& recorder,uint32_t first_index,uint32_t index_count,
                 int32_t base_vertex=0) const;
  // The same draws with this frame's vertices staged through the recorder
  // instead of written into the mesh's buffer. For a dynamic mesh only: the
  // bytes are converted per draw against this mesh's declaration, so their
  // stride and count must be the mesh's. Not a flush boundary, which is the
  // reason these exist.
  void DrawTransient(NativeBackendRecorder& recorder,std::span<const uint8_t> guest_vertices,
                     uint32_t first_index,uint32_t index_count,int32_t base_vertex=0) const;
  void DrawLinesTransient(NativeBackendRecorder& recorder,std::span<const uint8_t> guest_vertices,
                          uint32_t first_index,uint32_t index_count,int32_t base_vertex=0) const;
  // DrawTransient / DrawLinesTransient recorded without the index buffer: the
  // converted vertices written out in index order and drawn as a non-indexed
  // list (ExpandIndexedVertices), the same primitives from the same vertex
  // values in the same order. Being non-indexed from vertex 0 is what lets a
  // packet recorder append the next identical-state draw to this one; a
  // generated index buffer only covers its own draw's vertices. List
  // topologies only. `pipeline` is the one bound for this draw: the expansion
  // is only equal for a pipeline whose transient_batchable is set (slot-0
  // per-vertex layout, no SV_VertexID/SV_InstanceID/SV_PrimitiveID); any other,
  // including one not yet decided, takes DrawTransient / DrawLinesTransient,
  // the indexed draw. Returns whether it expanded.
  bool DrawTransientExpanded(NativeBackendRecorder& recorder,const NativeBackendPipeline& pipeline,
                             std::span<const uint8_t> guest_vertices,
                             uint32_t first_index,uint32_t index_count,NativeBackendTopology topology,
                             int32_t base_vertex=0) const;
  // Vertex stride and index width, for a caller building the draw itself.
  uint32_t stride() const { return stride_; }
  void ValidateLineDraw(uint32_t first_index,uint32_t index_count,int32_t base_vertex=0) const;
  // Diagnostic replay only: capture SV_POSITION before rasterization. Requires
  // this VS already bound, no GS or SO targets, and at most 96 triangle vertices.
  // Leaves the framebuffer untouched and restores GS/SO to their empty state.
  std::vector<std::array<float,4>> CaptureClipPositions(ID3D11DeviceContext& context,
    const NativeShader& shader,uint32_t first_index,uint32_t index_count,int32_t base_vertex=0) const;
  // Conservatively charge shared storage to each mesh for cache-budget bounds.
  size_t StorageBytes() const { return vertex_storage_->StorageBytes()+index_storage_->StorageBytes(); }
  const std::shared_ptr<const NativeIndexBuffer>& IndexStorage() const { return index_storage_; }
  const std::shared_ptr<NativeVertexBuffer>& VertexStorage() const { return vertex_storage_; }
 private:
  // The vertex layout this mesh was built with, in neutral form, so a pipeline
  // can be created from it without rebuilding it from the guest declaration.
 public:
  const NativeOwnedInputLayout& input_layout() const { return input_layout_; }
 private:
  NativeOwnedInputLayout input_layout_;
  std::optional<PreparedDraw> prepared_draw_;
  void ValidateRange(uint32_t first,uint32_t count,int32_t base,uint32_t primitive_width) const;
  void BindAndDraw(ID3D11DeviceContext& context,uint32_t first,uint32_t count,int32_t base,D3D11_PRIMITIVE_TOPOLOGY topology) const;
  void BindAndDraw(NativeBackendRecorder& recorder,uint32_t first,uint32_t count,int32_t base,
                   NativeBackendTopology topology) const;
  void BindTransientAndDraw(NativeBackendRecorder& recorder,std::span<const uint8_t> guest_vertices,
                            uint32_t first,uint32_t count,int32_t base,NativeBackendTopology topology) const;
  std::shared_ptr<NativeVertexBuffer> vertex_storage_;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
  std::shared_ptr<const NativeIndexBuffer> index_storage_;
  uint32_t stride_ = 0, vertex_count_ = 0;
};
// Exact byte comparison prevents stale geometry even before all guest update
// producers are hooked. Keys identify VB/IB/declaration/shader/variant, not GPU
// addresses. Returned references remain valid only until the next mutation.
// Optional dynamic mode updates equal-sized vertices on the device's immediate
// context; callers must serialize Acquire with rendering on that context.
class NativeDeclaration;
class NativeGeneratedIndices;
// Synchronous, non-owning callback: no allocation on the per-draw path.
struct NativeSnapshotObserver {
  void* context=nullptr;
  void (*notify)(void*)=nullptr;
  explicit operator bool() const { return notify!=nullptr; }
  void operator()() const { notify(context); }
};
class NativeMeshCache {
 public:
  using Key=std::array<uint32_t,5>;
  explicit NativeMeshCache(size_t budget=64*1024*1024,size_t entry_limit=1024,bool dynamic_vertices=false)
      : budget_(budget),entry_limit_(entry_limit),dynamic_vertices_(dynamic_vertices) {}
  // A consecutive static draw may reuse the exact immutable sources already
  // owned by its cached mesh. No ownership handles are copied on this path.
  // A null result changes nothing; Acquire performs the full fallback.
  NativeIndexedMesh* TryAcquireOwned(NativeRenderBackend& backend,const NativeShader& shader,
    const Key& key,const NativeDeclaration& declaration,uint32_t stride,
    std::span<const uint8_t> vertices,std::span<const uint8_t> indices,uint32_t index_bytes);
  uint64_t owned_hits() const { return owned_hits_; }
  NativeIndexedMesh& Acquire(NativeRenderBackend& backend,const NativeShader& shader,const Key& key,
    std::span<const uint8_t> declaration,uint32_t stride,std::span<const uint8_t> vertices,
    std::span<const uint8_t> indices,uint32_t index_bytes,
    std::shared_ptr<const NativeDeclaration> owned_declaration={},
    std::shared_ptr<const NativeGeneratedIndices> owned_indices={},
    std::shared_ptr<const NativeIndexBuffer> index_storage={},
    std::shared_ptr<NativeVertexBuffer> vertex_storage={},
    // Called before construction/update reads, never on an unchanged cache hit.
    // Must not mutate this cache or the supplied resource registry.
    NativeSnapshotObserver before_snapshot={},
    std::shared_ptr<const std::vector<uint8_t>> vertex_contents={},size_t contents_offset=0,
    std::shared_ptr<const std::vector<uint8_t>> index_contents={},
    // Where a dynamic mesh's vertices are rewritten. Null means the caller is
    // still on the direct path and the immediate context does it.
    NativeBackendRecorder* recorder=nullptr);
  void Invalidate(uint32_t resource);
  void Clear();
  uint64_t hits() const { return hits_; }
  uint64_t builds() const { return builds_; }
  uint64_t updates() const { return updates_; }
  // Failed existing-cache source checks only, not proof of a completed writer.
  uint64_t vertex_mismatches() const { return vertex_mismatches_; }
  uint64_t index_mismatches() const { return index_mismatches_; }
  uint64_t published_index_reuses() const { return published_index_reuses_; }
  uint64_t published_index_rejections() const { return published_index_rejections_; }
  uint64_t retained_vertex_reuses() const { return retained_vertex_reuses_; }
  uint64_t retained_vertex_replacements() const { return retained_vertex_replacements_; }
  struct SourceChecks {
    uint64_t vertex_checks=0,index_checks=0,vertex_candidate_bytes=0,index_candidate_bytes=0;
    uint64_t vertex_identity_hits=0,index_identity_hits=0;
  };
  // Where Acquire's time goes, in nanoseconds, split the three ways it can be
  // spent. A cache hit ought to be a map lookup and two pointer comparisons;
  // when it measures twenty microseconds instead, the only way to say which
  // part is lying is to time them separately.
  struct Spend {
    uint64_t prologue_ns=0,lookup_ns=0,tail_ns=0,calls=0;
  };
  const Spend& spend() const { return spend_; }
  void SetTimingsEnabled(bool enabled) { timings_enabled_=enabled; }
  // Cache-hit validation only; excludes construction/update copies. Candidate
  // lengths are not actual memory traffic: mismatches may short-circuit.
  const SourceChecks& source_checks() const { return source_checks_; }
  // Valid after the corresponding mismatch count becomes nonzero. Detection
  // identifies a consumer/resource, not the instruction that wrote its bytes.
  const Key& last_vertex_mismatch() const { return last_vertex_mismatch_; }
  const Key& last_index_mismatch() const { return last_index_mismatch_; }
  size_t bytes() const { return bytes_; }
  size_t entries() const { return entries_.size(); }
  uint64_t entry_evictions() const { return entry_evictions_; }
  uint64_t budget_evictions() const { return budget_evictions_; }
 private:
  struct Entry {
    Microsoft::WRL::ComPtr<ID3DBlob> shader;
    std::vector<uint8_t> declaration;
    uint32_t stride,index_bytes;
    std::unique_ptr<NativeIndexedMesh> mesh;
    size_t bytes;
    uint64_t used;
    std::shared_ptr<const NativeDeclaration> owned_declaration;
    std::shared_ptr<const NativeGeneratedIndices> owned_indices;
  };
  std::map<Key,Entry,NativeMeshKeyLess> entries_;
  // Only accelerates key lookup; every hit still validates layout and sources.
  Entry* last_entry_=nullptr;
  Key last_key_{};
  // A bounded shortcut for alternating meshes. Collisions fall back to the
  // owning map; full keys and all existing source/layout checks still apply.
  struct RecentEntry { Key key{}; Entry* entry=nullptr; };
  std::array<RecentEntry,64> recent_{};
  static size_t RecentSlot(const Key& key) {
    uint32_t hash=2166136261u;
    for(const auto word:key) hash=(hash^word)*16777619u;
    hash^=hash>>16; hash*=0x7feb352du; hash^=hash>>15;
    return hash&63u;
  }
  void Remember(const Key& key,Entry& entry) {
    last_entry_=&entry; last_key_=key;
    recent_[RecentSlot(key)]={key,&entry};
  }
  void ForgetLookup(const Key& key) {
    if(last_entry_ && last_key_==key) last_entry_=nullptr;
    auto& recent=recent_[RecentSlot(key)];
    if(recent.entry && recent.key==key) recent.entry=nullptr;
  }
  bool timings_enabled_=false;
  uint64_t owned_hits_=0;
  std::map<uint32_t,std::weak_ptr<const NativeIndexBuffer>> index_resources_;
  std::map<uint32_t,std::weak_ptr<NativeVertexBuffer>> vertex_resources_;
  std::unique_ptr<NativeIndexedMesh> transient_;
  NativeRenderBackend* backend_=nullptr;
  size_t budget_,entry_limit_,bytes_=0;
  bool dynamic_vertices_=false;
  uint64_t updates_=0;
  uint64_t vertex_mismatches_=0,index_mismatches_=0;
  uint64_t published_index_reuses_=0,published_index_rejections_=0;
  uint64_t retained_vertex_reuses_=0,retained_vertex_replacements_=0;
  SourceChecks source_checks_;
  Spend spend_;
  Key last_vertex_mismatch_{},last_index_mismatch_{};
  uint64_t entry_evictions_=0,budget_evictions_=0;
  uint64_t tick_=0,hits_=0,builds_=0;
};
}

// A triangle drawn through the backend seam by the D3D12 backend, on WARP, and
// read back pixel by pixel. This is the check that the seam is an interface a
// real backend can be built behind rather than a shape that merely compiles.
#include "native_graphics/d3d12_backend.h"
#include "native_graphics/native_render_backend.h"
#include "native_graphics/native_parallel_recorder.h"
#include "native_graphics/native_transient_batching.h"
#include <windows.h>
#include <d3d11shader.h>
#include <map>
#include <optional>
#include <d3dcompiler.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <chrono>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace edf::native;

namespace {
int failures = 0;
void Check(bool ok, const std::string& message) {
  if (ok) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}
ComPtr<ID3DBlob> Compile(const char* source, const char* entry, const char* profile) {
  ComPtr<ID3DBlob> code, errors;
  if (FAILED(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, entry, profile, 0, 0,
                        &code, &errors)))
    throw std::runtime_error(std::string("shader compile failed: ") +
                             (errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no detail"));
  return code;
}
std::span<const uint8_t> Bytes(ID3DBlob& blob) {
  return {static_cast<const uint8_t*>(blob.GetBufferPointer()), blob.GetBufferSize()};
}

// ---------------------------------------------------------------------------
// Transient batching (NativeParallelRecorder::TryAppendTransient).
//
// A command list that does what a GPU front end does with what it is given:
// every draw becomes the primitives it assembles, each carrying the complete
// state it is drawn under and the bytes of its own vertices. Two recordings
// whose primitive lists are equal draw the same thing - the same vertices, in
// the same order, under the same state - whatever their draw calls were.
struct FakeTexture final : NativeBackendTexture {
  uint32_t width() const override { return 1; }
  uint32_t height() const override { return 1; }
};
struct FakeTarget final : NativeBackendRenderTarget {
  uint32_t width() const override { return 1; }
  uint32_t height() const override { return 1; }
};
struct FakeBuffer final : NativeBackendBuffer {
  std::vector<uint8_t> data;
  size_t bytes() const override { return data.size(); }
};
struct Primitive {
  std::vector<uint8_t> state;     // Everything bound, serialized.
  std::vector<uint8_t> vertices;  // This primitive's vertices, in assembly order.
  bool operator==(const Primitive&) const = default;
};
class FrontEndRecorder final : public NativeBackendRecorder {
 public:
  std::vector<Primitive> primitives;
  uint32_t draws = 0;
  // A new command list starts with nothing bound.
  void Reset() {
    pipeline_ = nullptr; topology_ = NativeBackendTopology::TriangleList; blend_.reset();
    streams_ = {}; indices_ = nullptr; index_offset_ = 0;
    constants_.clear(); textures_.clear(); samplers_.clear();
    colors_.clear(); depth_ = nullptr; viewport_ = {}; scissor_ = {}; scissor_enabled_ = false;
  }
  void SetPipeline(NativeBackendPipeline& pipeline) override { pipeline_ = &pipeline; }
  void SetVertexBuffer(uint32_t slot, NativeBackendBuffer& buffer, uint32_t stride, uint32_t offset) override {
    auto& fake = static_cast<FakeBuffer&>(buffer);
    streams_.at(slot) = {std::vector<uint8_t>(fake.data.begin() + offset, fake.data.end()), stride};
  }
  void SetIndexBuffer(NativeBackendBuffer& buffer, NativeBackendIndexFormat format, uint32_t offset) override {
    if (format != NativeBackendIndexFormat::Uint16) throw std::runtime_error("front end: 16-bit indices only");
    indices_ = &static_cast<FakeBuffer&>(buffer); index_offset_ = offset;
  }
  void SetTransientVertices(uint32_t slot, std::span<const uint8_t> bytes, uint32_t stride) override {
    streams_.at(slot) = {std::vector<uint8_t>(bytes.begin(), bytes.end()), stride};
  }
  void SetTopology(NativeBackendTopology topology) override { topology_ = topology; }
  void SetBlendFactor(const std::array<float, 4>& factor) override { blend_ = factor; }
  void SetConstants(NativeBackendStage stage, uint32_t slot, std::span<const uint8_t> bytes) override {
    constants_[{uint32_t(stage), slot}] = std::vector<uint8_t>(bytes.begin(), bytes.end());
  }
  void SetTexture(NativeBackendStage stage, uint32_t slot, NativeBackendTexture* texture) override {
    textures_[{uint32_t(stage), slot}] = texture;
  }
  void SetSampler(NativeBackendStage stage, uint32_t slot, NativeBackendSampler* sampler) override {
    samplers_[{uint32_t(stage), slot}] = sampler;
  }
  void SetRenderTargets(std::span<NativeBackendRenderTarget* const> colors, NativeBackendRenderTarget* depth) override {
    colors_.assign(colors.begin(), colors.end()); depth_ = depth;
  }
  void SetViewport(const NativeBackendViewport& viewport) override { viewport_ = viewport; }
  void SetScissor(const NativeBackendScissor& scissor, bool enabled) override {
    scissor_ = scissor; scissor_enabled_ = enabled;
  }
  void ClearColor(NativeBackendRenderTarget&, const std::array<float, 4>&) override {}
  void ClearDepthStencil(NativeBackendRenderTarget&, bool, bool, float, uint8_t) override {}
  void Draw(uint32_t count, uint32_t first) override {
    ++draws;
    std::vector<uint32_t> order(count);
    for (uint32_t i = 0; i < count; ++i) order[i] = first + i;
    Assemble(order);
  }
  void DrawIndexed(uint32_t count, uint32_t first, int32_t base) override {
    ++draws;
    if (!indices_) throw std::runtime_error("front end: no index buffer bound");
    std::vector<uint32_t> order(count);
    for (uint32_t i = 0; i < count; ++i) {
      const size_t at = index_offset_ + (size_t(first) + i) * 2;
      if (at + 2 > indices_->data.size()) throw std::runtime_error("front end: index outside its buffer");
      uint16_t value; std::memcpy(&value, indices_->data.data() + at, 2);
      order[i] = uint32_t(int64_t(value) + base);
    }
    Assemble(order);
  }
  // The packet replay issues every indexed draw as a one-instance draw.
  void DrawIndexedInstanced(uint32_t count, uint32_t instances, uint32_t first, int32_t base,
                            uint32_t first_instance) override {
    if (instances != 1 || first_instance) throw std::runtime_error("front end: instancing is not what this test records");
    DrawIndexed(count, first, base);
  }
  void CopyTexture(NativeBackendTexture&, NativeBackendTexture&) override {}
  void CopyToShared(NativeBackendSharedSurface&, NativeBackendRenderTarget&) override {}
  void ResolveTarget(NativeBackendTexture&, NativeBackendRenderTarget&) override {}
  void UpdateBuffer(NativeBackendBuffer& buffer, uint32_t offset, std::span<const uint8_t> bytes) override {
    auto& fake = static_cast<FakeBuffer&>(buffer);
    std::copy(bytes.begin(), bytes.end(), fake.data.begin() + offset);
  }
  void UpdateTexture(NativeBackendTexture&, std::span<const uint8_t>) override {}
  void BeginQuery(NativeBackendQuery&) override {}
  void EndQuery(NativeBackendQuery&) override {}
  void PushState() override {}
  void PopState() override {}
  // Where each timestamp lands: this list's primitive count when it is written.
  std::vector<size_t> timestamps;
  void WriteTimestamp(NativeBackendTimestamps&, uint32_t) override { timestamps.push_back(primitives.size()); }

 private:
  struct Stream { std::vector<uint8_t> bytes; uint32_t stride = 0; };
  template <class T> static void Put(std::vector<uint8_t>& out, const T& value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
  }
  std::vector<uint8_t> State() const {
    std::vector<uint8_t> out;
    Put(out, pipeline_); Put(out, topology_); Put(out, blend_.value_or(std::array<float, 4>{-1, -1, -1, -1}));
    Put(out, colors_.size()); for (auto* color : colors_) Put(out, color);
    Put(out, depth_); Put(out, viewport_); Put(out, scissor_); Put(out, scissor_enabled_);
    for (const auto& [key, bytes] : constants_) {
      Put(out, key); Put(out, bytes.size()); out.insert(out.end(), bytes.begin(), bytes.end());
    }
    for (const auto& [key, texture] : textures_) { Put(out, key); Put(out, texture); }
    for (const auto& [key, sampler] : samplers_) { Put(out, key); Put(out, sampler); }
    return out;
  }
  void Assemble(const std::vector<uint32_t>& order) {
    if (!pipeline_) throw std::runtime_error("front end: draw with no pipeline");
    const auto& stream = streams_[0];
    if (!stream.stride) throw std::runtime_error("front end: draw with no vertices");
    const auto state = State();
    auto emit = [&](std::initializer_list<uint32_t> indices) {
      Primitive primitive{state, {}};
      for (const auto index : indices) {
        const size_t at = size_t(index) * stream.stride;
        // A draw reading past what was staged for it is exactly what a wrong
        // append would produce; the GPU would read garbage, this refuses.
        if (at + stream.stride > stream.bytes.size()) throw std::runtime_error("front end: vertex outside its stream");
        primitive.vertices.insert(primitive.vertices.end(), stream.bytes.begin() + at,
                                  stream.bytes.begin() + at + stream.stride);
      }
      primitives.push_back(std::move(primitive));
    };
    const size_t n = order.size();
    switch (topology_) {
      case NativeBackendTopology::TriangleList:
        for (size_t i = 0; i + 3 <= n; i += 3) emit({order[i], order[i + 1], order[i + 2]});
        break;
      case NativeBackendTopology::LineList:
        for (size_t i = 0; i + 2 <= n; i += 2) emit({order[i], order[i + 1]});
        break;
      case NativeBackendTopology::PointList:
        for (size_t i = 0; i < n; ++i) emit({order[i]});
        break;
      case NativeBackendTopology::TriangleStrip:
        for (size_t i = 0; i + 3 <= n; ++i) {
          if (i & 1) emit({order[i + 1], order[i], order[i + 2]});
          else emit({order[i], order[i + 1], order[i + 2]});
        }
        break;
    }
  }
  NativeBackendPipeline* pipeline_ = nullptr;
  NativeBackendTopology topology_ = NativeBackendTopology::TriangleList;
  std::optional<std::array<float, 4>> blend_;
  std::array<Stream, 16> streams_{};
  FakeBuffer* indices_ = nullptr;
  uint32_t index_offset_ = 0;
  std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> constants_;
  std::map<std::pair<uint32_t, uint32_t>, NativeBackendTexture*> textures_;
  std::map<std::pair<uint32_t, uint32_t>, NativeBackendSampler*> samplers_;
  std::vector<NativeBackendRenderTarget*> colors_;
  NativeBackendRenderTarget* depth_ = nullptr;
  NativeBackendViewport viewport_{};
  NativeBackendScissor scissor_{};
  bool scissor_enabled_ = false;
};
struct FrontEndRun {
  std::vector<Primitive> primitives;
  uint32_t draws = 0;
  NativeParallelRecorder::Statistics statistics;
};
// The resources both recordings bind: the same objects, so the serialized
// state of equal bindings is equal bytes.
struct UiObjects {
  NativeBackendPipeline brush, lines, opaque;
  FakeTexture atlas, other;
  FakeTarget target;
  FakeBuffer update_target, quad_indices;
  UiObjects() {
    brush.transient_batchable = lines.transient_batchable = true;
    opaque.transient_batchable = false;  // Say, a shader reading SV_VertexID.
    update_target.data.resize(16);
    for (const uint16_t index : {0, 1, 2, 0, 2, 3}) {
      quad_indices.data.push_back(uint8_t(index)); quad_indices.data.push_back(uint8_t(index >> 8));
    }
  }
};
// Vertices whose bytes say which draw and which vertex they are, so a
// primitive assembled from the wrong vertices cannot compare equal.
std::vector<uint8_t> TaggedVertices(uint32_t draw, uint32_t count, uint32_t stride) {
  std::vector<uint8_t> bytes(size_t(count) * stride);
  for (uint32_t vertex = 0; vertex < count; ++vertex)
    for (uint32_t byte = 0; byte < stride; ++byte)
      bytes[size_t(vertex) * stride + byte] = uint8_t(draw * 31 + vertex * 7 + byte);
  return bytes;
}
// The bridge's recording of a UI phase, reduced to the calls that matter: runs
// that should become one draw, and every kind of difference that must not.
FrontEndRun RecordUiPhase(UiObjects& objects, bool batching, uint32_t minimum_draws) {
  FrontEndRecorder serial, first, second;
  FrontEndRun run;
  NativeParallelRecorder recorder({&serial, &first, &second}, [&](bool) {
    // Submission order: the serial list, then each worker's.
    for (auto* list : {&serial, &first, &second}) {
      run.primitives.insert(run.primitives.end(), list->primitives.begin(), list->primitives.end());
      run.draws += list->draws;
      list->primitives.clear(); list->draws = 0; list->Reset();
    }
  }, minimum_draws);
  NativeBackendRenderTarget* colors[] = {&objects.target};
  const std::array<uint8_t, 16> projection{1, 2, 3, 4}, projection_copy = projection, moved{9, 9, 9, 9};
  std::vector<uint8_t> scratch;
  uint32_t tag = 0;
  auto transient = [&](uint32_t count, uint32_t stride) {
    scratch = TaggedVertices(++tag, count, stride);
    recorder.SetTransientVerticesOwned(0, scratch, stride);
  };
  auto draw = [&](NativeBackendTopology topology, uint32_t count, uint32_t stride = 8) {
    transient(count, stride);
    recorder.SetTopology(topology);
    recorder.Draw(count, 0);
  };
  constexpr auto list = NativeBackendTopology::TriangleList;
  recorder.Reset();
  recorder.SetTransientBatching(batching);
  recorder.SetRenderTargets(colors, nullptr);
  recorder.SetViewport({0, 0, 1280, 720, 0, 1});
  recorder.SetScissor({0, 0, 1280, 720}, false);
  recorder.SetPipeline(objects.brush);
  recorder.SetConstants(NativeBackendStage::Vertex, 0, projection);
  recorder.SetTexture(NativeBackendStage::Pixel, 0, &objects.atlas);
  // A run of brushes: one draw when batched (4 appends).
  for (int i = 0; i < 5; ++i) draw(list, 6);
  // New constants split the run; the same bytes sent again (a new image, as
  // after a bind-generation bump) do not (1 append).
  recorder.SetConstants(NativeBackendStage::Vertex, 0, moved);
  draw(list, 6);
  recorder.SetConstants(NativeBackendStage::Vertex, 0, moved);
  draw(list, 12);
  recorder.SetConstants(NativeBackendStage::Vertex, 0, projection_copy);
  draw(list, 6);
  // A different texture splits it, and so does a scissor change (1 append).
  recorder.SetTexture(NativeBackendStage::Pixel, 0, &objects.other);
  draw(list, 6);
  recorder.SetScissor({0, 0, 640, 360}, true);
  draw(list, 6);
  draw(list, 6);
  // A partial triangle's leftover vertex must not pair up with the next draw's.
  draw(list, 4);
  draw(list, 3);
  // Strips never append: their primitives share vertices across the seam.
  draw(NativeBackendTopology::TriangleStrip, 4);
  draw(NativeBackendTopology::TriangleStrip, 4);
  // Lines append among themselves, under their own pipeline (2 appends).
  recorder.SetPipeline(objects.lines);
  for (int i = 0; i < 3; ++i) draw(NativeBackendTopology::LineList, 4, 20);
  // A different stride under the same pipeline is a different stream.
  draw(NativeBackendTopology::LineList, 2, 12);
  // A pipeline not marked batchable never appends.
  recorder.SetPipeline(objects.opaque);
  draw(list, 3);
  draw(list, 3);
  // The Utility path's old indexed draw does not append, and nothing appends
  // to it.
  recorder.SetPipeline(objects.brush);
  transient(4, 8);
  recorder.SetIndexBuffer(objects.quad_indices, NativeBackendIndexFormat::Uint16, 0);
  recorder.SetTopology(list);
  recorder.DrawIndexed(6, 0, 0);
  draw(list, 6);
  // The same image drawn twice is two draws of it; then new images append to
  // a copy, and the first draw of it still reads only its own vertices
  // (2 appends).
  recorder.Draw(6, 0);
  draw(list, 6);
  draw(list, 6);
  // A flush boundary (a buffer update) after a draw that starts its own
  // packet. The draw after it redraws that image without restaging it, which
  // the serial replay skips re-staging because it compares image identity;
  // then a new image appends. Had the append grown that image in place, the
  // replay would draw twelve vertices from the six it staged (41 appends).
  recorder.SetScissor({0, 0, 1280, 720}, false);
  draw(list, 6);
  recorder.UpdateBuffer(objects.update_target, 0, projection);
  recorder.Draw(6, 0);
  draw(list, 6);
  for (int i = 0; i < 40; ++i) draw(list, 6);
  recorder.Flush(false);
  run.statistics = recorder.statistics();
  return run;
}
struct FakeTimestamps final : NativeBackendTimestamps {
  uint32_t capacity() const override { return 8; }
};
// A timestamp between two appendable draws: the second must not append to the
// first, or its vertices are drawn before the timestamp, in the span before it.
// The world-instancing fold already refuses to fold across one.
void TestTransientBatchingMarkers() {
  UiObjects objects;
  FakeTimestamps set;
  for (const uint32_t minimum : {1u, 1000u}) {
    const auto label = " (worker minimum " + std::to_string(minimum) + ")";
    FrontEndRecorder serial, first, second;
    std::vector<Primitive> primitives;
    std::vector<size_t> timestamps;
    NativeParallelRecorder recorder({&serial, &first, &second}, [&](bool) {
      for (auto* list : {&serial, &first, &second}) {
        for (const auto at : list->timestamps) timestamps.push_back(primitives.size() + at);
        primitives.insert(primitives.end(), list->primitives.begin(), list->primitives.end());
        list->primitives.clear(); list->timestamps.clear(); list->draws = 0; list->Reset();
      }
    }, minimum);
    NativeBackendRenderTarget* colors[] = {&objects.target};
    const std::array<uint8_t, 16> projection{1, 2, 3, 4};
    std::vector<uint8_t> scratch;
    uint32_t tag = 0;
    auto draw = [&] {
      scratch = TaggedVertices(++tag, 6, 8);
      recorder.SetTransientVerticesOwned(0, scratch, 8);
      recorder.SetTopology(NativeBackendTopology::TriangleList);
      recorder.Draw(6, 0);
    };
    recorder.Reset();
    recorder.SetTransientBatching(true);
    recorder.SetRenderTargets(colors, nullptr);
    recorder.SetViewport({0, 0, 1280, 720, 0, 1});
    recorder.SetPipeline(objects.brush);
    recorder.SetConstants(NativeBackendStage::Vertex, 0, projection);
    // A run of two, a timestamp, a run of three, a timestamp, one more: 2 + 1
    // appends, and each timestamp after exactly the primitives drawn before it.
    draw(); draw();
    recorder.WriteTimestamp(set, 0);
    draw(); draw(); draw();
    recorder.WriteTimestamp(set, 1);
    draw();
    recorder.Flush(false);
    const auto appends = recorder.statistics().transient_appends;
    Check(appends == 3, "appends across timestamps: " + std::to_string(appends) + " (want 3)" + label);
    Check(primitives.size() == 12, "timestamp run drew " + std::to_string(primitives.size()) + " primitives (want 12)" + label);
    Check((timestamps == std::vector<size_t>{4, 10}),
          "timestamps landed after " + (timestamps.size() == 2 ? std::to_string(timestamps[0]) + " and " +
          std::to_string(timestamps[1]) : std::to_string(timestamps.size()) + " markers") +
          " primitives (want 4 and 10)" + label);
  }
}
void TestTransientBatching() {
  TestTransientBatchingMarkers();
  UiObjects objects;
  // Every draw on the serial list, every flush on the workers, and a mix.
  for (const uint32_t minimum : {1u, 4u, 1000u}) {
    const auto label = " (worker minimum " + std::to_string(minimum) + ")";
    try {
      const auto plain = RecordUiPhase(objects, false, minimum);
      const auto batched = RecordUiPhase(objects, true, minimum);
      Check(plain.statistics.transient_appends == 0, "appends were made with batching off" + label);
      Check(!plain.primitives.empty() && plain.primitives == batched.primitives,
            "batched recording assembles different primitives than the unbatched one" + label + ": " +
            std::to_string(plain.primitives.size()) + " vs " + std::to_string(batched.primitives.size()));
      Check(plain.draws - batched.draws == batched.statistics.transient_appends,
            "draws saved (" + std::to_string(plain.draws) + " - " + std::to_string(batched.draws) +
            ") differ from appends counted (" + std::to_string(batched.statistics.transient_appends) + ")" + label);
      Check(batched.statistics.transient_appends == 51,
            "unexpected number of appends: " + std::to_string(batched.statistics.transient_appends) + label);
    } catch (const std::exception& error) {
      Check(false, std::string("transient batching recording failed") + label + ": " + error.what());
    }
  }
  // What makes a pipeline eligible.
  const NativeBackendInputElement slot_zero[] = {{"POSITION", 0, 0, 0, 0, false, 0}, {"COLOR", 0, 0, 0, 8, false, 0}};
  const NativeBackendInputElement two_streams[] = {{"POSITION", 0, 0, 0, 0, false, 0}, {"COLOR", 0, 0, 1, 0, false, 0}};
  const NativeBackendInputElement instanced[] = {{"POSITION", 0, 0, 0, 0, true, 1}};
  Check(NativeInputLayoutSlotZeroOnly(slot_zero), "a slot-0 per-vertex layout was refused");
  Check(!NativeInputLayoutSlotZeroOnly(two_streams), "a two-stream layout was accepted");
  Check(!NativeInputLayoutSlotZeroOnly(instanced), "a per-instance layout was accepted");
  const char* kShaders = R"(
struct V { float4 position : SV_POSITION; float4 color : COLOR0; };
V VsPlain(float2 p : POSITION, float4 c : COLOR0) { V v; v.position = float4(p, 0, 1); v.color = c; return v; }
V VsNumbered(float2 p : POSITION, uint id : SV_VertexID) { V v; v.position = float4(p, 0, 1); v.color = id; return v; }
float4 PsPlain(V v) : SV_TARGET { return v.color; }
float4 PsNumbered(V v, uint id : SV_PrimitiveID) : SV_TARGET { return v.color * id; }
)";
  auto reflect = [&](const char* entry, const char* profile) {
    const auto code = Compile(kShaders, entry, profile);
    ComPtr<ID3D11ShaderReflection> reflection;
    if (FAILED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), __uuidof(ID3D11ShaderReflection),
                          reinterpret_cast<void**>(reflection.GetAddressOf()))))
      throw std::runtime_error("reflection failed");
    return reflection;
  };
  const auto vs = reflect("VsPlain", "vs_5_0"), vs_id = reflect("VsNumbered", "vs_5_0");
  const auto ps = reflect("PsPlain", "ps_5_0"), ps_id = reflect("PsNumbered", "ps_5_0");
  Check(NativePipelineTransientBatchable(slot_zero, vs.Get(), ps.Get()), "a plain pipeline was not batchable");
  Check(!NativePipelineTransientBatchable(slot_zero, vs_id.Get(), ps.Get()), "an SV_VertexID pipeline was batchable");
  Check(!NativePipelineTransientBatchable(slot_zero, vs.Get(), ps_id.Get()), "an SV_PrimitiveID pipeline was batchable");
  Check(!NativePipelineTransientBatchable(slot_zero, nullptr, ps.Get()), "an unreflected shader was batchable");
  Check(!NativePipelineTransientBatchable(two_streams, vs.Get(), ps.Get()), "a two-stream pipeline was batchable");
  // An indexed quad list written out in index order is the same primitives.
  {
    const auto vertices = TaggedVertices(7, 8, 12);
    const std::vector<uint32_t> quads{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    std::vector<uint8_t> expanded;
    ExpandIndexedVertices(vertices, 12, quads, 0, 12, 0, expanded);
    bool same = expanded.size() == 12 * 12;
    for (size_t i = 0; same && i < quads.size(); ++i)
      same = std::equal(expanded.begin() + i * 12, expanded.begin() + (i + 1) * 12,
                        vertices.begin() + quads[i] * 12);
    Check(same, "expanded quad vertices are not the indexed ones in index order");
    ExpandIndexedVertices(vertices, 12, quads, 6, 6, -4, expanded);
    Check(expanded.size() == 72 && std::equal(expanded.begin(), expanded.begin() + 12, vertices.begin()),
          "a first index and base vertex were not applied");
    bool refused = false;
    try { ExpandIndexedVertices(vertices, 12, quads, 6, 6, 1, expanded); }
    catch (const std::runtime_error&) { refused = true; }
    Check(refused, "an index past the vertices was expanded");
  }
  std::cout << "transient batching: front-end comparisons done\n";
}
// The same on the real backend: a scene backend with geometry workers (the
// game's configuration), overlapping triangles whose colour travels in their
// vertices so a whole run shares its state, drawn with and without batching
// and read back. Opaque and overlapping, so any change of order, any vertex
// from the wrong draw and any dropped triangle changes pixels.
void TestTransientBatchingPixels() {
  const char* kSource = R"(
struct V { float4 position : SV_POSITION; float4 color : COLOR0; };
V VS(float2 p : POSITION, float4 c : COLOR0) { V v; v.position = float4(p, 0, 1); v.color = c; return v; }
float4 PS(V v) : SV_TARGET { return v.color; }
)";
  const auto vertex = Compile(kSource, "VS", "vs_5_0");
  const auto pixel = Compile(kSource, "PS", "ps_5_0");
  constexpr uint32_t kSize = 64;
  auto render = [&](bool batching, uint64_t& appends) {
    auto backend = CreateNativeD3D12SceneBackend(true, 2);
    NativeBackendTextureDesc target_desc{};
    target_desc.width = target_desc.height = kSize;
    target_desc.levels = 1;
    target_desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    target_desc.render_target = true;
    const auto target = backend->CreateRenderTarget(target_desc);
    const NativeBackendInputElement layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, false, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, false, 0}};
    NativeBackendPipelineDesc desc{};
    desc.vertex = Bytes(*vertex.Get());
    desc.pixel = Bytes(*pixel.Get());
    desc.vertex_id = 0x71; desc.pixel_id = 0x72;
    desc.input_layout = layout; desc.input_layout_id = 0x73;
    desc.state = {0x10001, 0, 0, 0, 15, 0};
    desc.topology = NativeBackendTopology::TriangleList;
    desc.render_targets = 1;
    desc.rtv_format[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    auto& pipeline = backend->CreatePipeline(desc);
    pipeline.transient_batchable = true;
    NativeBackendRenderTarget* colors[] = {target.get()};
    backend->BeginFrame();
    auto& recorder = backend->Recorder();
    recorder.SetTransientBatching(batching);
    recorder.SetRenderTargets(colors, nullptr);
    recorder.SetViewport({0, 0, float(kSize), float(kSize), 0, 1});
    recorder.ClearColor(*target, {0, 0, 0, 1});
    recorder.SetPipeline(pipeline);
    recorder.SetTopology(NativeBackendTopology::TriangleList);
    // 48 draws of two overlapping triangles each, shifted and recoloured per
    // draw: later draws cover parts of earlier ones.
    std::vector<uint8_t> scratch;
    for (uint32_t draw = 0; draw < 48; ++draw) {
      std::vector<float> v;
      const float x = -1.0f + float(draw % 8) * 0.22f, y = -1.0f + float(draw / 8) * 0.3f;
      const float r = float(draw % 5) / 4.0f, g = float(draw % 7) / 6.0f, b = float(draw % 3) / 2.0f;
      for (int t = 0; t < 2; ++t) {
        const float s = t ? 0.6f : 0.9f;
        for (const auto& corner : {std::array<float, 2>{0, 0}, std::array<float, 2>{s, 0}, std::array<float, 2>{0, s}})
          v.insert(v.end(), {x + corner[0], y + corner[1], t ? b : r, g, t ? r : b, 1});
      }
      scratch.assign(reinterpret_cast<const uint8_t*>(v.data()), reinterpret_cast<const uint8_t*>(v.data() + v.size()));
      recorder.SetTransientVerticesOwned(0, scratch, 24);
      recorder.Draw(6, 0);
    }
    backend->Submit();
    appends = backend->Statistics().geometry_transient_appends;
    for (const auto& message : backend->DrainValidationMessages())
      Check(false, "D3D12 validation error in transient batching: " + message);
    return backend->ReadRenderTarget(*target);
  };
  uint64_t plain_appends = 0, batched_appends = 0;
  const auto plain = render(false, plain_appends);
  const auto batched = render(true, batched_appends);
  Check(plain_appends == 0 && batched_appends == 47,
        "WARP transient appends: " + std::to_string(plain_appends) + " unbatched, " +
        std::to_string(batched_appends) + " batched (want 0 and 47)");
  Check(plain.size() == size_t(kSize) * kSize * 4 && plain == batched,
        "batched and unbatched transient draws produced different pixels");
  size_t lit = 0;
  for (size_t i = 0; i + 3 < plain.size(); i += 4) lit += plain[i] || plain[i + 1] || plain[i + 2];
  Check(lit > kSize * kSize / 4, "the transient batching scene drew almost nothing: " + std::to_string(lit));
  std::cout << "transient batching: WARP pixels equal, " << batched_appends << " appends, " << lit << " lit\n";
}
}  // namespace

int main() {
  // Unbuffered: if a later stage kills the process, the output that says how
  // far it got must not die in the buffer with it.
  std::cout << std::unitbuf;
  HWND present_window = nullptr;
  WNDCLASSEXW present_class{};
  try {
    TestTransientBatching();
    TestTransientBatchingPixels();
    RegisterNativeD3D12Backend();
    const auto& names = NativeRenderBackendNames();
    Check(std::find(names.begin(), names.end(), "d3d12") != names.end(),
          "the D3D12 backend did not register itself");
    Check(std::find(names.begin(), names.end(), "d3d12-warp") != names.end(),
          "the WARP backend name is missing, so a driver bug cannot be attributed");
    // Registering twice must not throw: the entry point is callable from more
    // than one place and a duplicate registration would otherwise be fatal.
    RegisterNativeD3D12Backend();

    // Selected by name through the registry, so the whole path the game will
    // use is what gets exercised - not a direct constructor call the shipping
    // code never makes.
    auto backend = CreateNativeRenderBackend("d3d12-warp");
    Check(backend != nullptr, "the registry did not produce a backend");
    Check(backend->name() == "d3d12", "the backend reported the wrong name");
    Check(!backend->SupportsParallelRecording(),
          "the backend claims parallel recording it does not yet have");

    constexpr uint32_t kSize = 64;
    NativeBackendTextureDesc target_desc{};
    target_desc.width = target_desc.height = kSize;
    target_desc.levels = 1;
    target_desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    target_desc.render_target = true;
    const auto target = backend->CreateRenderTarget(target_desc);
    Check(target && target->width() == kSize, "the render target was not created at the size asked for");

    // A triangle covering exactly the lower-left half of the target, so the
    // readback can tell "drawn" from "cleared" by position and by area. Not the
    // usual (-1,-1)(3,-1)(-1,3) full-screen triangle: that covers everything,
    // which would make a completely wrong viewport look like a pass.
    const float vertices[] = {-1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, -1.0f, 1.0f, 0.0f};
    NativeBackendBufferDesc buffer_desc{};
    buffer_desc.bytes = sizeof(vertices);
    buffer_desc.vertex = true;
    const auto vertex_buffer = backend->CreateBuffer(
        buffer_desc, {reinterpret_cast<const uint8_t*>(vertices), sizeof(vertices)});

    const char* kSource = R"(
cbuffer PixelData : register(b0) { float4 tint; };
float4 VS(float3 position : POSITION) : SV_POSITION { return float4(position, 1); }
float4 PS(float4 position : SV_POSITION) : SV_TARGET { return tint; }
)";
    const auto vertex = Compile(kSource, "VS", "vs_5_0");
    const auto pixel = Compile(kSource, "PS", "ps_5_0");

    const NativeBackendInputElement layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, false, 0}};
    NativeBackendPipelineDesc pipeline_desc{};
    pipeline_desc.vertex = Bytes(*vertex.Get());
    pipeline_desc.pixel = Bytes(*pixel.Get());
    pipeline_desc.vertex_id = 0x11;
    pipeline_desc.pixel_id = 0x22;
    pipeline_desc.input_layout = layout;
    pipeline_desc.input_layout_id = 0x33;
    pipeline_desc.state = {0x10001, 0, 0, 0, 15, 0};  // No blending, no depth, solid, all channels.
    pipeline_desc.topology = NativeBackendTopology::TriangleList;
    pipeline_desc.render_targets = 1;
    pipeline_desc.rtv_format[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    auto& pipeline = backend->CreatePipeline(pipeline_desc);
    Check(&pipeline == &backend->CreatePipeline(pipeline_desc),
          "an identical pipeline description produced a second pipeline object");

    const std::array<float, 4> tint{0.0f, 1.0f, 0.0f, 1.0f};
    NativeBackendRenderTarget* colors[] = {target.get()};

    backend->BeginFrame();
    auto& recorder = backend->Recorder();
    recorder.SetRenderTargets(colors, nullptr);
    recorder.SetViewport({0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1});
    recorder.ClearColor(*target, {1.0f, 0.0f, 0.0f, 1.0f});
    recorder.SetPipeline(pipeline);
    recorder.SetVertexBuffer(0, *vertex_buffer, sizeof(float) * 3, 0);
    recorder.SetConstants(NativeBackendStage::Pixel, 0,
                          {reinterpret_cast<const uint8_t*>(tint.data()), sizeof(float) * 4});
    recorder.Draw(3, 0);
    backend->Submit();

    const auto pixels = backend->ReadRenderTarget(*target);
    Check(pixels.size() == static_cast<size_t>(kSize) * kSize * 4,
          "the readback was not tightly packed: " + std::to_string(pixels.size()) + " bytes");
    const auto at = [&](uint32_t x, uint32_t y) {
      const size_t index = (static_cast<size_t>(y) * kSize + x) * 4;
      return std::array<uint8_t, 4>{pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]};
    };
    // The triangle covers the lower-left; the opposite corner stays cleared.
    const auto inside = at(2, kSize - 3), outside = at(kSize - 2, 1);
    Check(inside[1] > 200 && inside[0] < 60,
          "the triangle did not draw: covered pixel is not the tint colour");
    Check(outside[0] > 200 && outside[1] < 60,
          "the clear did not happen: uncovered pixel is not the clear colour");

    uint32_t drawn = 0;
    for (uint32_t y = 0; y < kSize; ++y)
      for (uint32_t x = 0; x < kSize; ++x)
        if (at(x, y)[1] > 200) ++drawn;
    // Half the surface, give or take the diagonal. A wildly different number
    // means the viewport or the vertex buffer went somewhere unintended, which
    // a two-pixel spot check would not notice.
    const uint32_t total = kSize * kSize;
    Check(drawn > total * 4 / 10 && drawn < total * 6 / 10,
          "the triangle covered " + std::to_string(drawn) + " of " + std::to_string(total) +
              " pixels, which is not the half it should");
    std::cout << "triangle covered " << drawn << " of " << total << " pixels\n";

    {
      // A textured draw: upload a 2x2 texture, sample it with point filtering
      // and clamping, and read the quadrants back. This is the path every real
      // material takes, and none of it exists in a triangle of flat colour.
      const uint8_t texels[] = {
          255, 0,   0,   255,   0,   255, 0,   255,     // top row:    red,  green
          0,   0,   255, 255,   255, 255, 0,   255};    // bottom row: blue, yellow
      NativeBackendTextureDesc texture_desc{};
      texture_desc.width = texture_desc.height = 2;
      texture_desc.levels = 1;
      texture_desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
      const auto texture = backend->CreateTexture(texture_desc, texels);

      NativeBackendSamplerDesc sampler_desc{};
      sampler_desc.min = sampler_desc.mag = sampler_desc.mip = NativeBackendFilter::Point;
      sampler_desc.u = sampler_desc.v = sampler_desc.w = NativeBackendAddress::Clamp;
      auto& sampler = backend->CreateSampler(sampler_desc);
      Check(&sampler == &backend->CreateSampler(sampler_desc),
            "an identical sampler description produced a second sampler");

      const char* kTextured = R"(
Texture2D image : register(t0);
SamplerState filtering : register(s0);
struct Varying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Varying VS(float3 position : POSITION) {
  Varying output;
  output.position = float4(position, 1);
  output.uv = position.xy * 0.5 + 0.5;
  return output;
}
float4 PS(Varying input) : SV_TARGET { return image.Sample(filtering, input.uv); }
)";
      const auto textured_vs = Compile(kTextured, "VS", "vs_5_0");
      const auto textured_ps = Compile(kTextured, "PS", "ps_5_0");

      // A full-screen covering triangle is right here: every pixel should come
      // from the texture, so there is nothing for it to hide.
      const float cover[] = {-1.0f, -1.0f, 0.0f, 3.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f};
      NativeBackendBufferDesc cover_desc{};
      cover_desc.bytes = sizeof(cover);
      cover_desc.vertex = true;
      const auto cover_buffer = backend->CreateBuffer(
          cover_desc, {reinterpret_cast<const uint8_t*>(cover), sizeof(cover)});

      NativeBackendPipelineDesc textured{};
      textured.vertex = Bytes(*textured_vs.Get());
      textured.pixel = Bytes(*textured_ps.Get());
      textured.vertex_id = 0x44;
      textured.pixel_id = 0x55;
      textured.input_layout = layout;
      textured.input_layout_id = 0x33;
      textured.state = {0x10001, 0, 0, 0, 15, 0};
      textured.topology = NativeBackendTopology::TriangleList;
      textured.render_targets = 1;
      textured.rtv_format[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
      auto& textured_pipeline = backend->CreatePipeline(textured);

      backend->BeginFrame();
      auto& textured_recorder = backend->Recorder();
      textured_recorder.SetRenderTargets(colors, nullptr);
      textured_recorder.SetViewport({0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1});
      textured_recorder.ClearColor(*target, {0.0f, 0.0f, 0.0f, 1.0f});
      textured_recorder.SetPipeline(textured_pipeline);
      textured_recorder.SetVertexBuffer(0, *cover_buffer, sizeof(float) * 3, 0);
      textured_recorder.SetTexture(NativeBackendStage::Pixel, 0, texture.get());
      textured_recorder.SetSampler(NativeBackendStage::Pixel, 0, &sampler);
      textured_recorder.Draw(3, 0);
      backend->Submit();

      const auto sampled = backend->ReadRenderTarget(*target);
      const auto texel = [&](uint32_t x, uint32_t y) {
        const size_t index = (static_cast<size_t>(y) * kSize + x) * 4;
        return std::array<uint8_t, 4>{sampled[index], sampled[index + 1], sampled[index + 2]};
      };
      // uv maps NDC to [0,1] with v increasing upward, so the target's top rows
      // hold the texture's bottom row. Checking all four quadrants catches a
      // flip or a swapped row, which a single sample would not.
      const auto top_left = texel(kSize / 4, kSize / 4), top_right = texel(kSize * 3 / 4, kSize / 4);
      const auto bottom_left = texel(kSize / 4, kSize * 3 / 4),
                 bottom_right = texel(kSize * 3 / 4, kSize * 3 / 4);
      Check(top_left[2] > 200 && top_left[0] < 60, "top-left quadrant is not the texture's blue texel");
      Check(top_right[0] > 200 && top_right[1] > 200,
            "top-right quadrant is not the texture's yellow texel");
      Check(bottom_left[0] > 200 && bottom_left[1] < 60,
            "bottom-left quadrant is not the texture's red texel");
      Check(bottom_right[1] > 200 && bottom_right[0] < 60,
            "bottom-right quadrant is not the texture's green texel");
      std::cout << "textured draw: four quadrants sampled\n";
    }

    {
      // Indexed, instanced drawing: the stage-0 primitive. 77.4% of this game's
      // draws are a repeat of the one before differing only in per-instance
      // constants, so this is the call that is meant to replace them, and the
      // per-instance input element is how the differing data gets there.
      const float quad[] = {-0.2f, -0.2f, 0.0f, 0.2f, -0.2f, 0.0f,
                            -0.2f, 0.2f,  0.0f, 0.2f, 0.2f,  0.0f};
      const uint16_t indices[] = {0, 2, 1, 1, 2, 3};
      const float offsets[] = {-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};

      NativeBackendBufferDesc desc{};
      desc.bytes = sizeof(quad);
      desc.vertex = true;
      const auto quad_buffer =
          backend->CreateBuffer(desc, {reinterpret_cast<const uint8_t*>(quad), sizeof(quad)});
      desc.bytes = sizeof(offsets);
      const auto instance_buffer =
          backend->CreateBuffer(desc, {reinterpret_cast<const uint8_t*>(offsets), sizeof(offsets)});
      NativeBackendBufferDesc index_desc{};
      index_desc.bytes = sizeof(indices);
      index_desc.index = true;
      const auto index_buffer = backend->CreateBuffer(
          index_desc, {reinterpret_cast<const uint8_t*>(indices), sizeof(indices)});

      const char* kInstanced = R"(
cbuffer PixelData : register(b0) { float4 tint; };
float4 VS(float3 position : POSITION, float2 offset : TEXCOORD0) : SV_POSITION {
  return float4(position.xy + offset, 0, 1);
}
float4 PS(float4 position : SV_POSITION) : SV_TARGET { return tint; }
)";
      const auto instanced_vs = Compile(kInstanced, "VS", "vs_5_0");
      const auto instanced_ps = Compile(kInstanced, "PS", "ps_5_0");

      const NativeBackendInputElement instanced_layout[] = {
          {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, false, 0},
          {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0, true, 1}};
      NativeBackendPipelineDesc instanced{};
      instanced.vertex = Bytes(*instanced_vs.Get());
      instanced.pixel = Bytes(*instanced_ps.Get());
      instanced.vertex_id = 0x66;
      instanced.pixel_id = 0x77;
      instanced.input_layout = instanced_layout;
      instanced.input_layout_id = 0x88;
      instanced.state = {0x10001, 0, 0, 0, 15, 0};
      instanced.topology = NativeBackendTopology::TriangleList;
      instanced.render_targets = 1;
      instanced.rtv_format[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
      auto& instanced_pipeline = backend->CreatePipeline(instanced);

      const std::array<float, 4> white{1.0f, 1.0f, 1.0f, 1.0f};
      backend->BeginFrame();
      auto& r = backend->Recorder();
      r.SetRenderTargets(colors, nullptr);
      r.SetViewport({0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1});
      r.ClearColor(*target, {0.0f, 0.0f, 0.0f, 1.0f});
      r.SetPipeline(instanced_pipeline);
      r.SetVertexBuffer(0, *quad_buffer, sizeof(float) * 3, 0);
      r.SetVertexBuffer(1, *instance_buffer, sizeof(float) * 2, 0);
      r.SetIndexBuffer(*index_buffer, NativeBackendIndexFormat::Uint16, 0);
      r.SetConstants(NativeBackendStage::Pixel, 0,
                     {reinterpret_cast<const uint8_t*>(white.data()), sizeof(float) * 4});
      r.DrawIndexedInstanced(6, 4, 0, 0, 0);
      backend->Submit();

      const auto drawn = backend->ReadRenderTarget(*target);
      const auto lit = [&](uint32_t x, uint32_t y) {
        return drawn[(static_cast<size_t>(y) * kSize + x) * 4] > 200;
      };
      // One quad per quadrant, and nothing in the middle or the corners: four
      // instances that all landed on top of each other would light one spot,
      // and one instance drawn four times would light none of the others.
      // Not `near`/`far`: windef.h still defines those as macros.
      const uint32_t low = kSize / 4, high = kSize * 3 / 4;
      Check(lit(low, low) && lit(high, low) && lit(low, high) && lit(high, high),
            "not all four instances drew: a quadrant centre is unlit");
      Check(!lit(kSize / 2, kSize / 2), "the instances collapsed onto the centre");
      Check(!lit(1, 1) && !lit(kSize - 2, kSize - 2), "something drew outside the four quads");
      uint32_t area = 0;
      for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x)
          if (lit(x, y)) ++area;
      // Four quads of 0.4 x 0.4 in a 2.0 clip range: 4 x (0.2 x 64)^2 = ~655.
      Check(area > 500 && area < 850,
            "the four instances covered " + std::to_string(area) + " pixels, not the ~655 expected");
      std::cout << "instanced draw: 4 instances, " << area << " pixels\n";
    }

    {
      // Presentation, against a real but never-shown window. A swap chain is
      // the one part that cannot be checked by rendering to a texture, and its
      // characteristic failure - a back buffer presented in the wrong resource
      // state - is exactly what validation exists to catch.
      Check(backend->BackBuffer() == nullptr, "a back buffer existed before a window was attached");
      bool refused = false;
      try { backend->Present(false); } catch (const std::runtime_error&) { refused = true; }
      Check(refused, "Present with no window attached was accepted");

      // Declared outside the block that uses it: the window must outlive the
      // backend, because the swap chain holds it, and a window destroyed from
      // under a live swap chain is a crash rather than an error.
      WNDCLASSEXW window_class{};
      window_class.cbSize = sizeof(window_class);
      window_class.lpfnWndProc = DefWindowProcW;
      window_class.hInstance = GetModuleHandleW(nullptr);
      window_class.lpszClassName = L"EdfD3D12PresentTest";
      RegisterClassExW(&window_class);
      HWND window = CreateWindowExW(0, window_class.lpszClassName, L"", WS_POPUP, 0, 0, kSize, kSize,
                                    nullptr, nullptr, window_class.hInstance, nullptr);
      Check(window != nullptr, "the test could not create a window to present to");
      if (window) {
        backend->AttachWindow(window, kSize, kSize);
        auto* back = backend->BackBuffer();
        Check(back != nullptr, "no back buffer after attaching a window");
        Check(back->width() == kSize && back->height() == kSize,
              "the back buffer is not the size the window was attached at");

        // More frames than there are buffers, so the chain rotates and each
        // buffer is rendered into, presented, and come back to.
        for (uint32_t frame = 0; frame < 8; ++frame) {
          auto* buffer = backend->BackBuffer();
          NativeBackendRenderTarget* chain[] = {buffer};
          backend->BeginFrame();
          auto& present_recorder = backend->Recorder();
          present_recorder.SetRenderTargets(chain, nullptr);
          present_recorder.SetViewport({0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1});
          present_recorder.ClearColor(*buffer, {0.0f, 0.0f, 1.0f, 1.0f});
          backend->Submit();
          backend->Present(false);
        }
        for (const auto& message : backend->DrainValidationMessages())
          Check(false, "D3D12 validation error while presenting: " + message);

        // The buffer handed out must rotate; always returning the same one
        // would mean writing to a buffer the display is still showing.
        auto* first = backend->BackBuffer();
        backend->BeginFrame();
        backend->Submit();
        backend->Present(false);
        Check(first != backend->BackBuffer(), "the swap chain handed out the same buffer twice running");

        std::cout << "presented 9 frames across " << 3 << " buffers\n";
        DestroyWindow(window);
      }
      UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
    }

    {
      // Parallel recording, from real threads. Two recorders, two command
      // lists, each writing its own half of the target. The lists execute in
      // recorder order, so recorder 0 clears and recorder 1 can rely on it.
      NativeD3D12Options parallel_options;
      parallel_options.prefer_warp = true;
      parallel_options.debug_layer = true;
      parallel_options.recorders = 2;
      const auto parallel = CreateNativeD3D12Backend(parallel_options);
      Check(parallel->RecorderCount() == 2, "the backend did not create two recorders");
      Check(parallel->SupportsParallelRecording(),
            "a two-recorder backend still reports it cannot record in parallel");

      const auto split_target = parallel->CreateRenderTarget(target_desc);
      NativeBackendRenderTarget* split_colors[] = {split_target.get()};

      // Two triangles, each covering one half, so a recorder writing the wrong
      // half or not writing at all is visible rather than averaged away.
      const float left[] = {-1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, -1.0f, 1.0f, 0.0f};
      const float right[] = {0.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f};
      NativeBackendBufferDesc half_desc{};
      half_desc.bytes = sizeof(left);
      half_desc.vertex = true;
      const auto left_buffer =
          parallel->CreateBuffer(half_desc, {reinterpret_cast<const uint8_t*>(left), sizeof(left)});
      const auto right_buffer =
          parallel->CreateBuffer(half_desc, {reinterpret_cast<const uint8_t*>(right), sizeof(right)});

      NativeBackendPipelineDesc split_pipeline_desc = pipeline_desc;
      auto& split_pipeline = parallel->CreatePipeline(split_pipeline_desc);

      const std::array<float, 4> green{0.0f, 1.0f, 0.0f, 1.0f};
      const std::array<float, 4> blue{0.0f, 0.0f, 1.0f, 1.0f};

      parallel->BeginFrame();
      const auto record = [&](uint32_t index) {
        auto& recorder = parallel->Recorder(index);
        recorder.SetRenderTargets(split_colors, nullptr);
        recorder.SetViewport({0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1});
        if (index == 0) recorder.ClearColor(*split_target, {1.0f, 0.0f, 0.0f, 1.0f});
        recorder.SetPipeline(split_pipeline);
        recorder.SetVertexBuffer(0, index == 0 ? *left_buffer : *right_buffer, sizeof(float) * 3, 0);
        const auto& colour = index == 0 ? green : blue;
        recorder.SetConstants(NativeBackendStage::Pixel, 0,
                              {reinterpret_cast<const uint8_t*>(colour.data()), sizeof(float) * 4});
        recorder.Draw(3, 0);
      };
      std::thread worker([&] { record(1); });
      record(0);
      worker.join();
      parallel->Submit();

      const auto halves = parallel->ReadRenderTarget(*split_target);
      const auto channel = [&](uint32_t x, uint32_t y, uint32_t component) {
        return halves[(static_cast<size_t>(y) * kSize + x) * 4 + component];
      };
      // Counted rather than spot-checked. The first version of this sampled
      // points that sat exactly on a triangle edge, where coverage is a coin
      // flip, and blamed the backend for it.
      uint32_t green_left = 0, green_right = 0, blue_left = 0, blue_right = 0, cleared = 0;
      for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x) {
          const bool left_half = x < kSize / 2;
          if (channel(x, y, 1) > 200) ++(left_half ? green_left : green_right);
          else if (channel(x, y, 2) > 200) ++(left_half ? blue_left : blue_right);
          else if (channel(x, y, 0) > 200) ++cleared;
        }
      Check(green_left > 0, "recorder 0 drew nothing");
      Check(blue_right > 0, "recorder 1 drew nothing");
      // Each recorder owns a half, so its colour must not appear in the other.
      Check(green_right == 0, "recorder 0 drew into recorder 1's half");
      Check(blue_left == 0, "recorder 1 drew into recorder 0's half");
      // Recorder 0 issued the clear and recorder 1 relies on the lists
      // executing in order; red surviving on the right would mean they did not.
      Check(cleared > 0, "nothing was left cleared, so the triangles are not the shape this expects");
      for (const auto& message : parallel->DrainValidationMessages())
        Check(false, "D3D12 validation error while recording in parallel: " + message);
      std::cout << "parallel recording: " << green_left << " px from recorder 0, " << blue_right
                << " px from recorder 1\n";
    }

    {
      // An occlusion query around a draw whose coverage is already known from
      // the first test: 2,016 pixels. A query that returns a plausible-looking
      // number is not enough - it has to return that one.
      const auto query = backend->CreateQuery(NativeBackendQueryKind::Occlusion);
      Check(query != nullptr, "the occlusion query was not created");

      uint64_t samples = 0;
      std::span<uint8_t> into{reinterpret_cast<uint8_t*>(&samples), sizeof(samples)};
      backend->BeginFrame();
      auto& counted = backend->Recorder();
      counted.SetRenderTargets(colors, nullptr);
      counted.SetViewport({0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1});
      counted.ClearColor(*target, {0.0f, 0.0f, 0.0f, 1.0f});
      counted.SetPipeline(pipeline);
      counted.SetVertexBuffer(0, *vertex_buffer, sizeof(float) * 3, 0);
      counted.SetConstants(NativeBackendStage::Pixel, 0,
                           {reinterpret_cast<const uint8_t*>(tint.data()), sizeof(float) * 4});
      counted.BeginQuery(*query);
      counted.Draw(3, 0);
      counted.EndQuery(*query);
      backend->Submit();

      // Never blocks, so the caller polls. Reading it before the GPU has
      // reached the resolve must say "not ready", not hand back stale bytes.
      uint32_t attempts = 0;
      while (!backend->ReadQuery(*query, into) && attempts < 10000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ++attempts;
      }
      Check(attempts < 10000, "the occlusion query never became readable");
      Check(samples == 2016, "the occlusion query counted " + std::to_string(samples) +
                                 " samples, not the 2016 pixels that draw covers");
      std::cout << "occlusion query: " << samples << " samples after " << attempts << " polls\n";
    }
    {
      // A mipped texture, uploaded level by level. Level 1 is a different
      // colour from level 0, and the draw samples level 1 explicitly - so if
      // only the top level were uploaded, this reads back whatever level 1
      // happened to contain, which is not the colour asked for.
      NativeBackendTextureDesc mipped_desc{};
      mipped_desc.width = mipped_desc.height = 2;
      mipped_desc.levels = 2;
      mipped_desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
      // Level 0 is 2x2 red, level 1 is 1x1 blue, packed tightly one after the
      // other, which is how the disc assets arrive.
      const uint8_t mip_bytes[] = {255, 0, 0, 255,  255, 0, 0, 255,
                                   255, 0, 0, 255,  255, 0, 0, 255,
                                   0,   0, 255, 255};
      const auto mipped = backend->CreateTexture(mipped_desc, mip_bytes);

      NativeBackendSamplerDesc mip_sampler_desc{};
      mip_sampler_desc.min = mip_sampler_desc.mag = mip_sampler_desc.mip = NativeBackendFilter::Point;
      mip_sampler_desc.u = mip_sampler_desc.v = mip_sampler_desc.w = NativeBackendAddress::Clamp;
      auto& mip_sampler = backend->CreateSampler(mip_sampler_desc);

      const char* kMipped = R"(
Texture2D image : register(t0);
SamplerState filtering : register(s0);
struct Varying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Varying VS(float3 position : POSITION) {
  Varying output;
  output.position = float4(position, 1);
  output.uv = position.xy * 0.5 + 0.5;
  return output;
}
float4 PS(Varying input) : SV_TARGET { return image.SampleLevel(filtering, input.uv, 1); }
)";
      const auto mip_vs = Compile(kMipped, "VS", "vs_5_0");
      const auto mip_ps = Compile(kMipped, "PS", "ps_5_0");
      const float cover[] = {-1.0f, -1.0f, 0.0f, 3.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f};
      NativeBackendBufferDesc cover_desc{};
      cover_desc.bytes = sizeof(cover);
      cover_desc.vertex = true;
      const auto cover_buffer = backend->CreateBuffer(
          cover_desc, {reinterpret_cast<const uint8_t*>(cover), sizeof(cover)});

      NativeBackendPipelineDesc mip_pipeline_desc = pipeline_desc;
      mip_pipeline_desc.vertex = Bytes(*mip_vs.Get());
      mip_pipeline_desc.pixel = Bytes(*mip_ps.Get());
      mip_pipeline_desc.vertex_id = 0x99;
      mip_pipeline_desc.pixel_id = 0xAA;
      auto& mip_pipeline = backend->CreatePipeline(mip_pipeline_desc);

      backend->BeginFrame();
      auto& mip_recorder = backend->Recorder();
      mip_recorder.SetRenderTargets(colors, nullptr);
      mip_recorder.SetViewport({0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1});
      mip_recorder.ClearColor(*target, {0.0f, 0.0f, 0.0f, 1.0f});
      mip_recorder.SetPipeline(mip_pipeline);
      mip_recorder.SetVertexBuffer(0, *cover_buffer, sizeof(float) * 3, 0);
      mip_recorder.SetTexture(NativeBackendStage::Pixel, 0, mipped.get());
      mip_recorder.SetSampler(NativeBackendStage::Pixel, 0, &mip_sampler);
      mip_recorder.Draw(3, 0);
      backend->Submit();

      const auto sampled_mip = backend->ReadRenderTarget(*target);
      const size_t middle = (static_cast<size_t>(kSize / 2) * kSize + kSize / 2) * 4;
      Check(sampled_mip[middle + 2] > 200 && sampled_mip[middle] < 60,
            "level 1 of the mipped texture is not the colour it was uploaded with");
      std::cout << "mipped upload: level 1 sampled correctly\n";
    }

    {
      // A 4x multisampled target, resolved and then sampled. The renderer does
      // use 2x and 4x targets, so this is not a hypothetical path.
      //
      // Checked by looking for partially covered pixels along the diagonal.
      // Single-sample rasterisation can only produce the triangle colour or
      // the clear colour; intermediate values exist only if four samples were
      // taken and averaged. A resolve that silently degraded to a copy would
      // produce none, and would pass any check that only looked at corners.
      NativeBackendTextureDesc msaa_desc{};
      msaa_desc.width = msaa_desc.height = kSize;
      msaa_desc.levels = 1;
      msaa_desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
      msaa_desc.samples = 4;
      msaa_desc.render_target = true;
      const auto msaa_target = backend->CreateRenderTarget(msaa_desc);

      NativeBackendTextureDesc resolved_desc{};
      resolved_desc.width = resolved_desc.height = kSize;
      resolved_desc.levels = 1;
      resolved_desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
      const auto resolved = backend->CreateTexture(resolved_desc, {});

      NativeBackendPipelineDesc msaa_pipeline_desc = pipeline_desc;
      msaa_pipeline_desc.sample_count = 4;
      msaa_pipeline_desc.vertex_id = 0xB1;
      msaa_pipeline_desc.pixel_id = 0xB2;
      auto& msaa_pipeline = backend->CreatePipeline(msaa_pipeline_desc);

      NativeBackendRenderTarget* msaa_colors[] = {msaa_target.get()};
      backend->BeginFrame();
      auto& msaa_recorder = backend->Recorder();
      msaa_recorder.SetRenderTargets(msaa_colors, nullptr);
      msaa_recorder.SetViewport({0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1});
      msaa_recorder.ClearColor(*msaa_target, {0.0f, 0.0f, 0.0f, 1.0f});
      msaa_recorder.SetPipeline(msaa_pipeline);
      msaa_recorder.SetVertexBuffer(0, *vertex_buffer, sizeof(float) * 3, 0);
      msaa_recorder.SetConstants(NativeBackendStage::Pixel, 0,
                                 {reinterpret_cast<const uint8_t*>(tint.data()), sizeof(float) * 4});
      msaa_recorder.Draw(3, 0);
      msaa_recorder.ResolveTarget(*resolved, *msaa_target);
      backend->Submit();

      // Sampled through the normal single-sample target, which is the only
      // surface this seam can read back.
      const char* kBlit = R"(
Texture2D image : register(t0);
SamplerState filtering : register(s0);
struct Varying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Varying VS(float3 position : POSITION) {
  Varying output;
  output.position = float4(position, 1);
  output.uv = float2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
  return output;
}
float4 PS(Varying input) : SV_TARGET { return image.SampleLevel(filtering, input.uv, 0); }
)";
      const auto blit_vs = Compile(kBlit, "VS", "vs_5_0");
      const auto blit_ps = Compile(kBlit, "PS", "ps_5_0");
      const float cover[] = {-1.0f, -1.0f, 0.0f, 3.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f};
      NativeBackendBufferDesc cover_desc{};
      cover_desc.bytes = sizeof(cover);
      cover_desc.vertex = true;
      const auto cover_buffer = backend->CreateBuffer(
          cover_desc, {reinterpret_cast<const uint8_t*>(cover), sizeof(cover)});
      NativeBackendSamplerDesc point_desc{};
      point_desc.min = point_desc.mag = point_desc.mip = NativeBackendFilter::Point;
      point_desc.u = point_desc.v = point_desc.w = NativeBackendAddress::Clamp;
      auto& point_sampler = backend->CreateSampler(point_desc);

      NativeBackendPipelineDesc blit_desc = pipeline_desc;
      blit_desc.vertex = Bytes(*blit_vs.Get());
      blit_desc.pixel = Bytes(*blit_ps.Get());
      blit_desc.vertex_id = 0xB3;
      blit_desc.pixel_id = 0xB4;
      auto& blit_pipeline = backend->CreatePipeline(blit_desc);

      backend->BeginFrame();
      auto& blit_recorder = backend->Recorder();
      blit_recorder.SetRenderTargets(colors, nullptr);
      blit_recorder.SetViewport({0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1});
      blit_recorder.ClearColor(*target, {0.0f, 0.0f, 0.0f, 1.0f});
      blit_recorder.SetPipeline(blit_pipeline);
      blit_recorder.SetVertexBuffer(0, *cover_buffer, sizeof(float) * 3, 0);
      blit_recorder.SetTexture(NativeBackendStage::Pixel, 0, resolved.get());
      blit_recorder.SetSampler(NativeBackendStage::Pixel, 0, &point_sampler);
      blit_recorder.Draw(3, 0);
      backend->Submit();

      const auto edges = backend->ReadRenderTarget(*target);
      uint32_t partial = 0, full = 0;
      for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x) {
          const uint8_t green = edges[(static_cast<size_t>(y) * kSize + x) * 4 + 1];
          if (green > 200) ++full;
          else if (green > 20) ++partial;
        }
      Check(full > 1500, "the multisampled draw did not cover the target");
      Check(partial > 20, "no partially covered pixels survived the resolve: " +
                              std::to_string(partial) + ", so nothing was multisampled");
      for (const auto& message : backend->DrainValidationMessages())
        Check(false, "D3D12 validation error around the resolve: " + message);
      std::cout << "4x resolve: " << full << " covered, " << partial << " edge pixels\n";
    }

    // A missing barrier would not change a single pixel above; only the API's
    // own validation sees it, so it is read rather than left in the debugger.
    for (const auto& message : backend->DrainValidationMessages())
      Check(false, "D3D12 validation error: " + message);

    // Misuse of the frame lifecycle must be loud.
    bool caught = false;
    try { backend->Recorder(); } catch (const std::runtime_error&) { caught = true; }
    Check(caught, "the recorder was handed out with no frame open");
    caught = false;
    try { backend->Submit(); } catch (const std::runtime_error&) { caught = true; }
    Check(caught, "Submit outside a frame was accepted");

    // The backend goes first, then the window it was presenting to.
    backend.reset();
  } catch (const std::exception& error) {
    std::cerr << "unexpected: " << error.what() << '\n';
    return 1;
  }
  std::cout << "native d3d12 backend tests: " << failures << " failures\n";
  return failures ? 1 : 0;
}

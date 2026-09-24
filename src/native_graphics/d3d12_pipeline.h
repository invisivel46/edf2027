#pragma once
#include "native_disk_cache.h"
#include "native_render_state_decode.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace edf::native {
// The root signature and the pipeline cache - consequence 3 of the migration
// note, where blend, depth, raster, input layout and the shader pair stop being
// five independent objects and fuse into one.
//
// The shape below is not a guess. `edf_native_shader_check --slots` reports the
// widest slot any of the 44 disc effects actually binds: 2 vertex constant
// buffers, 1 pixel constant buffer, 7 pixel textures, 7 pixel samplers, and no
// vertex textures at all. Declaring the D3D11 maxima instead would cost root
// space and descriptor traffic on every one of ~2,370 draws a frame.
//
// Constant buffers are root descriptors rather than a table: at 44,917 material
// activations a second, binding one is then a GPU virtual address written into
// the root arguments, with no descriptor to allocate or copy. Textures cannot
// be, because a root SRV can only address a buffer.
struct NativeD3D12RootLayout {
  static constexpr uint32_t kVertexConstantBuffers=2;
  static constexpr uint32_t kPixelConstantBuffers=1;
  // Eight, not the measured seven: a table is bound whole and the round number
  // costs nothing, while a shader that needs an eighth would otherwise have to
  // rebuild the signature.
  static constexpr uint32_t kPixelTextures=8;
  static constexpr uint32_t kPixelSamplers=8;

  // Root parameter indices, in the order they are declared.
  static constexpr uint32_t kVertexConstants0=0;
  static constexpr uint32_t kVertexConstants1=1;
  static constexpr uint32_t kPixelConstants0=2;
  static constexpr uint32_t kPixelTextureTable=3;
  static constexpr uint32_t kPixelSamplerTable=4;
  static constexpr uint32_t kParameterCount=5;
};

// Throws, naming the shader's own slot, when a shader binds outside the layout
// above. The disc shaders were measured; the renderer's own UI, font, movie and
// post HLSL was not, so this is the check that keeps an unmeasured shader from
// silently losing a binding.
// Returns the slots the shader reads (bit n = slot n), which the reflection
// lists already.
struct NativeShaderSlots { uint32_t constant_buffers=0,textures=0,samplers=0; };
NativeShaderSlots ValidateAgainstRootLayout(std::span<const uint8_t> bytecode, bool pixel, const std::string& name);

// The serialized (version 1.0) root signature every pipeline is built against.
// Its bytes are part of every persistent pipeline key: a manifest written for a
// different signature describes pipelines this build cannot use.
std::vector<uint8_t> SerializeNativeD3D12RootSignature();
Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateNativeD3D12RootSignature(ID3D12Device& device);

// Everything that must match for two draws to share a pipeline. Compared as
// raw bytes, so it must have no padding to leave uninitialised - asserted
// below, because a padding byte would make identical draws miss the cache and,
// worse, make the cache's answer depend on stack garbage.
struct NativeD3D12PipelineKey {
  uint64_t vertex_shader=0,pixel_shader=0;  // Stable shader identity, caller's choice.
  uint64_t input_layout=0;                  // Hash of the element descriptions.
  // The render state words that are pipeline state. Scissor enable is
  // deliberately absent: D3D11 kept it in the rasteriser object, but in D3D12
  // the scissor rectangle is a command, so two draws differing only in
  // scissoring share a pipeline instead of building a second one.
  uint32_t blend=0,depth=0,raster=0,alpha=0,write_mask=0;
  uint32_t topology=0;                      // D3D12_PRIMITIVE_TOPOLOGY_TYPE.
  uint32_t render_targets=0;
  uint32_t rtv_format[8]{};
  uint32_t dsv_format=0;
  uint32_t sample_count=1;
  uint32_t sample_quality=0;
};
static_assert(std::has_unique_object_representations_v<NativeD3D12PipelineKey>,
              "the pipeline key is compared as bytes, so it must not contain padding");

// Which adapter and driver a manifest was written on. Recorded, and reported
// when it changes, but it does not invalidate the manifest: the manifest holds
// pipeline descriptions (bytecode, layout, state, formats), which mean the
// same thing to every driver. A new driver only makes the background rebuild
// slower, because its own shader cache is cold; a description the new adapter
// cannot build fails in the background and is dropped at the next save.
struct NativeD3D12AdapterIdentity {
  uint32_t vendor=0,device=0,subsystem=0,revision=0;
  uint64_t driver=0;  // UMD version, as IDXGIAdapter::CheckInterfaceSupport reports it.
  bool operator==(const NativeD3D12AdapterIdentity&) const=default;
};

struct NativeD3D12PipelineCacheOptions {
  // The persistent manifest. Empty: nothing is read or written and nothing is
  // built ahead; the cache behaves exactly as an in-memory map.
  std::filesystem::path manifest;
  NativeD3D12AdapterIdentity adapter;
  // Threads that build the manifest's pipelines in the background. Zero
  // leaves them queued: each is then built by the first lookup that needs it.
  uint32_t prewarm_threads=2;
  // Save keeps an entry only if it was used within this many runs, and at most
  // this many entries, most recently used first.
  uint32_t max_age_runs=32;
  size_t max_entries=4096;
};

// The pipeline cache.
//
// A miss builds a pipeline, which is far more expensive than CreateBlendState
// ever was - a cold miss during gameplay is a visible hitch (45 of them in the
// second frame of a mission, beside that frame's meshes, made a 76 ms frame),
// not a small cost. So the cache persists what it built: every pipeline's full
// description goes into a manifest, and the next run rebuilds all of them on
// background threads from the moment the backend exists - long before a
// mission draws with them.
//
// A draw never skips anything and waits only when it must: the pipeline it
// asks for is built (a hit), being built by a worker (it waits for that one
// build, never for the queue), queued (it takes it and builds it itself), or
// unknown (it builds it, as before). Every path creates the pipeline from the
// same description through the same code, so a prebuilt pipeline is the
// pipeline the draw would have built.
//
// Two keys: the caller's identity key (shader ids, layout id, state words),
// which is what a draw looks up, and a content key - SHA-256 over the bytecode,
// the element descriptions, the pipeline-state words, formats and the root
// signature - which is what persists, since shader ids are guest handles that
// mean nothing in the next run.
class NativeD3D12PipelineCache {
 public:
  NativeD3D12PipelineCache(ID3D12Device& device, ID3D12RootSignature& signature,
                           NativeD3D12PipelineCacheOptions options={});
  ~NativeD3D12PipelineCache();
  NativeD3D12PipelineCache(const NativeD3D12PipelineCache&)=delete;
  NativeD3D12PipelineCache& operator=(const NativeD3D12PipelineCache&)=delete;

  struct Request {
    NativeD3D12PipelineKey key;
    D3D12_SHADER_BYTECODE vertex{},pixel{};
    std::span<const D3D12_INPUT_ELEMENT_DESC> input_layout;
    RenderStateWords state{};
  };
  // Thread-safe. Throws what pipeline creation throws.
  ID3D12PipelineState& Get(const Request& request);
  // The persistent identity of what Get(request) builds. Ignores the caller's
  // ids and the scissor word, exactly as the pipeline does.
  NativeContentHash ContentKey(const Request& request) const;

  // Identity-key hits, and pipelines built on the calling thread (a cold miss,
  // or a queued manifest entry the caller got to first). Misses are the number
  // that says whether the cache is being warmed properly.
  uint64_t hits() const;
  uint64_t misses() const;
  size_t size() const;  // Distinct identity keys seen.
  struct Statistics {
    uint64_t hits=0,misses=0;
    uint64_t content_hits=0;     // New identity, pipeline already built (usually by a warmer).
    uint64_t waits=0,wait_ns=0;  // Lookups that waited for a warmer's build in progress.
    uint64_t prebuilt=0;         // Built by the warmers.
    uint64_t prebuild_ns=0;      // Warmer time inside pipeline creation, summed over warmers.
    uint64_t queued=0;           // Manifest entries still waiting for a warmer.
    uint64_t prebuild_failures=0;
    uint64_t manifest_entries=0; // Loaded from the manifest.
    uint64_t saves=0;
    bool adapter_changed=false;
    size_t pipelines=0;          // Distinct pipelines held, built or not.
  };
  // Counters only, so a per-frame caller pays no allocation.
  Statistics statistics() const;
  // "none", "missing", "loaded", or "rejected: <why>".
  std::string manifest_status() const;
  // Blocks until no manifest entry is waiting or being built. Tests; never on
  // a frame path.
  void WaitForPrebuild();

  // Persistence. The sampler combinations ride along in the same file, for the
  // descriptor cache's own warm start (NativeD3D12SamplerCache::Prewarm).
  using SamplerCombination=std::vector<D3D12_SAMPLER_DESC>;
  const std::vector<SamplerCombination>& loaded_samplers() const { return loaded_samplers_; }
  // Whether anything a save would write changed since the last one.
  bool dirty() const;
  // Writes the manifest now. false when there is no manifest path or the write
  // failed.
  bool Save(std::vector<SamplerCombination> samplers);
  // Takes the snapshot now and writes it on a background thread; skipped while
  // an earlier background save is still writing.
  void SaveInBackground(std::vector<SamplerCombination> samplers);

 private:
  struct Element {
    std::string semantic;
    uint32_t index=0,format=0,slot=0,offset=0,classification=0,step=0;
  };
  struct Desc {
    std::shared_ptr<const std::vector<uint8_t>> vertex,pixel;
    std::vector<Element> layout;
    RenderStateWords state{};  // Scissor word always zero.
    uint32_t topology=0,render_targets=0;
    uint32_t rtv_format[8]{};
    uint32_t dsv_format=0,sample_count=1,sample_quality=0;
  };
  enum class EntryState { Queued, Building, Ready, Failed };
  struct Entry {
    Desc desc;
    NativeContentHash key{};
    EntryState state=EntryState::Queued;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    uint32_t last_used_run=0;
  };
  struct Snapshot {
    std::vector<std::pair<std::shared_ptr<const Entry>,uint32_t>> entries;
    std::vector<SamplerCombination> samplers;
    uint32_t run=0;
  };

  Desc DescOf(const Request& request);
  NativeContentHash KeyOf(const Desc& desc) const;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> Build(const Desc& desc) const;
  std::shared_ptr<const std::vector<uint8_t>> Intern(std::span<const uint8_t> bytecode);
  void Load();
  void Prebuild();
  Snapshot TakeSnapshot(std::vector<SamplerCombination> samplers);
  std::vector<uint8_t> Serialize(const Snapshot& snapshot) const;
  void MarkUsed(Entry& entry);
  void JoinSaver();

  ID3D12Device* device_=nullptr;
  ID3D12RootSignature* signature_=nullptr;
  NativeD3D12PipelineCacheOptions options_;
  NativeContentHash signature_hash_{};

  mutable std::mutex mutex_;
  std::condition_variable built_;
  std::map<std::string,ID3D12PipelineState*,std::less<>> by_identity_;
  std::map<NativeContentHash,std::shared_ptr<Entry>> by_content_;
  std::map<NativeContentHash,std::weak_ptr<const std::vector<uint8_t>>> bytecode_;
  std::deque<std::shared_ptr<Entry>> queue_;
  size_t building_=0;
  bool stopping_=false,dirty_=false;
  uint32_t run_=1;
  uint64_t hits_=0,misses_=0,content_hits_=0,waits_=0,wait_ns_=0,prebuilt_=0,prebuild_failures_=0,prebuild_ns_=0;
  uint64_t manifest_entries_=0,saves_=0;
  bool adapter_changed_=false;
  std::string manifest_status_="none";
  std::vector<SamplerCombination> loaded_samplers_;
  std::vector<std::thread> warmers_;
  std::mutex save_mutex_;
  std::thread saver_;
  std::atomic<bool> saving_{false};
};
}  // namespace edf::native

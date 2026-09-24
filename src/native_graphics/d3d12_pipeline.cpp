#include "d3d12_pipeline.h"
#include "native_first_use.h"
#include <d3d12shader.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <format>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace edf::native {
namespace {
void Require(HRESULT result, const char* what) {
  if(SUCCEEDED(result)) return;
  char code[16];
  std::snprintf(code,sizeof(code),"0x%08lx",static_cast<unsigned long>(result));
  throw std::runtime_error(std::string("D3D12 ")+what+" failed: "+code);
}
// A first-use record's description of a pipeline: the caller's shader and
// layout ids, the pipeline-state words and the first target format.
std::string DescribeRequest(const NativeD3D12PipelineCache::Request& request) {
  const auto& key=request.key;
  return std::format("vs={:#x} ps={:#x} layout={:#x} blend={:#x} depth={:#x} raster={:#x} alpha={:#x} mask={:#x} "
                     "topology={} rts={} rt0={} ds={} samples={}",
                     key.vertex_shader,key.pixel_shader,key.input_layout,key.blend,key.depth,key.raster,key.alpha,
                     key.write_mask,key.topology,key.render_targets,key.rtv_format[0],key.dsv_format,key.sample_count);
}
}  // namespace

// The shared decode hands back neutral values; they are usable as D3D12
// enumerators only because they are numerically the same. Proved here, exactly
// as the D3D11 side proves it, so a divergence breaks the build instead of the
// picture.
static_assert(kNativeBlendZero==D3D12_BLEND_ZERO && kNativeBlendOne==D3D12_BLEND_ONE);
static_assert(kNativeBlendSrcColor==D3D12_BLEND_SRC_COLOR && kNativeBlendInvSrcColor==D3D12_BLEND_INV_SRC_COLOR);
static_assert(kNativeBlendSrcAlpha==D3D12_BLEND_SRC_ALPHA && kNativeBlendInvSrcAlpha==D3D12_BLEND_INV_SRC_ALPHA);
static_assert(kNativeBlendDestAlpha==D3D12_BLEND_DEST_ALPHA && kNativeBlendInvDestAlpha==D3D12_BLEND_INV_DEST_ALPHA);
static_assert(kNativeBlendDestColor==D3D12_BLEND_DEST_COLOR && kNativeBlendInvDestColor==D3D12_BLEND_INV_DEST_COLOR);
static_assert(kNativeBlendSrcAlphaSat==D3D12_BLEND_SRC_ALPHA_SAT);
static_assert(kNativeBlendFactor==D3D12_BLEND_BLEND_FACTOR && kNativeBlendInvFactor==D3D12_BLEND_INV_BLEND_FACTOR);
static_assert(kNativeBlendOpAdd==D3D12_BLEND_OP_ADD && kNativeBlendOpSubtract==D3D12_BLEND_OP_SUBTRACT);
static_assert(kNativeBlendOpRevSubtract==D3D12_BLEND_OP_REV_SUBTRACT);
static_assert(kNativeBlendOpMin==D3D12_BLEND_OP_MIN && kNativeBlendOpMax==D3D12_BLEND_OP_MAX);
static_assert(kNativeFillWireframe==D3D12_FILL_MODE_WIREFRAME && kNativeFillSolid==D3D12_FILL_MODE_SOLID);
static_assert(kNativeCullNone==D3D12_CULL_MODE_NONE && kNativeCullFront==D3D12_CULL_MODE_FRONT &&
              kNativeCullBack==D3D12_CULL_MODE_BACK);
static_assert(kNativeDepthWriteZero==D3D12_DEPTH_WRITE_MASK_ZERO && kNativeDepthWriteAll==D3D12_DEPTH_WRITE_MASK_ALL);

NativeShaderSlots ValidateAgainstRootLayout(std::span<const uint8_t> bytecode, bool pixel, const std::string& name) {
  ComPtr<ID3D12ShaderReflection> reflection;
  Require(D3DReflect(bytecode.data(),bytecode.size(),IID_PPV_ARGS(&reflection)),"shader reflection");
  D3D12_SHADER_DESC description{};
  Require(reflection->GetDesc(&description),"shader description");
  const uint32_t constant_buffers=pixel?NativeD3D12RootLayout::kPixelConstantBuffers
                                       :NativeD3D12RootLayout::kVertexConstantBuffers;
  const uint32_t textures=pixel?NativeD3D12RootLayout::kPixelTextures:0;
  const uint32_t samplers=pixel?NativeD3D12RootLayout::kPixelSamplers:0;
  const auto refuse=[&](const char* kind,UINT slot,uint32_t allowed) {
    throw std::runtime_error(name+" binds "+kind+" slot "+std::to_string(slot)+
                             " but the root signature declares "+std::to_string(allowed)+
                             ((pixel?" pixel ":" vertex ")+std::string(kind)+" slots"));
  };
  NativeShaderSlots used;
  for(UINT index=0;index<description.BoundResources;++index) {
    D3D12_SHADER_INPUT_BIND_DESC binding{};
    Require(reflection->GetResourceBindingDesc(index,&binding),"resource binding");
    switch(binding.Type) {
      case D3D_SIT_CBUFFER:
        if(binding.BindPoint>=constant_buffers) refuse("constant buffer",binding.BindPoint,constant_buffers);
        used.constant_buffers|=1u<<binding.BindPoint;
        break;
      case D3D_SIT_TEXTURE:
        if(binding.BindPoint>=textures) refuse("texture",binding.BindPoint,textures);
        used.textures|=1u<<binding.BindPoint;
        break;
      case D3D_SIT_SAMPLER:
        if(binding.BindPoint>=samplers) refuse("sampler",binding.BindPoint,samplers);
        used.samplers|=1u<<binding.BindPoint;
        break;
      default:
        // Anything else needs a root parameter that does not exist. Better to
        // say so than to build a pipeline whose binding silently goes nowhere.
        throw std::runtime_error(name+" binds resource type "+std::to_string(binding.Type)+
                                 " at slot "+std::to_string(binding.BindPoint)+
                                 ", which this root signature does not declare");
    }
  }
  return used;
}


namespace {
D3D12_ROOT_SIGNATURE_DESC RootSignatureDesc(D3D12_DESCRIPTOR_RANGE (&ranges)[2],
                                            D3D12_ROOT_PARAMETER (&parameters)[NativeD3D12RootLayout::kParameterCount]) {
  ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,NativeD3D12RootLayout::kPixelTextures,0,0,
             D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
  ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,NativeD3D12RootLayout::kPixelSamplers,0,0,
             D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
  const auto constant_buffer=[](UINT slot,D3D12_SHADER_VISIBILITY visibility) {
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor={slot,0};
    parameter.ShaderVisibility=visibility;
    return parameter;
  };
  parameters[NativeD3D12RootLayout::kVertexConstants0]=constant_buffer(0,D3D12_SHADER_VISIBILITY_VERTEX);
  parameters[NativeD3D12RootLayout::kVertexConstants1]=constant_buffer(1,D3D12_SHADER_VISIBILITY_VERTEX);
  parameters[NativeD3D12RootLayout::kPixelConstants0]=constant_buffer(0,D3D12_SHADER_VISIBILITY_PIXEL);
  for(uint32_t index=0;index<2;++index) {
    auto& parameter=parameters[NativeD3D12RootLayout::kPixelTextureTable+index];
    parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable={1,&ranges[index]};
    parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  }

  D3D12_ROOT_SIGNATURE_DESC desc{};
  desc.NumParameters=NativeD3D12RootLayout::kParameterCount;
  desc.pParameters=parameters;
  // Denying the stages this renderer does not use is not tidiness: it lets the
  // driver skip publishing root arguments to them on every draw.
  desc.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|
             D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS|
             D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS|
             D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
  return desc;
}
}  // namespace

std::vector<uint8_t> SerializeNativeD3D12RootSignature() {
  D3D12_DESCRIPTOR_RANGE ranges[2]{};
  D3D12_ROOT_PARAMETER parameters[NativeD3D12RootLayout::kParameterCount]{};
  const auto desc=RootSignatureDesc(ranges,parameters);
  ComPtr<ID3DBlob> serialized,errors;
  if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors))) {
    const std::string detail=errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),
                                                errors->GetBufferSize()):std::string("no detail");
    throw std::runtime_error("D3D12 root signature serialization failed: "+detail);
  }
  const auto* bytes=static_cast<const uint8_t*>(serialized->GetBufferPointer());
  return std::vector<uint8_t>(bytes,bytes+serialized->GetBufferSize());
}

ComPtr<ID3D12RootSignature> CreateNativeD3D12RootSignature(ID3D12Device& device) {
  const auto serialized=SerializeNativeD3D12RootSignature();
  ComPtr<ID3D12RootSignature> signature;
  Require(device.CreateRootSignature(0,serialized.data(),serialized.size(),IID_PPV_ARGS(&signature)),
          "root signature creation");
  return signature;
}

namespace {
constexpr char kManifestMagic[8]={'E','D','F','P','S','O','0','1'};
constexpr uint32_t kManifestFormat=1;
constexpr size_t kManifestLimit=256u<<20;
constexpr size_t kBytecodeLimit=4u<<20;
constexpr uint64_t kCountLimit=1u<<16;
constexpr size_t kSemanticLimit=256;
// D3D12_SAMPLER_DESC is persisted as its bytes; the sampler cache already keys
// on exactly those bytes, and all thirteen members are four bytes wide.
static_assert(sizeof(D3D12_SAMPLER_DESC)==52);

std::span<const uint8_t> BytesOf(const D3D12_SHADER_BYTECODE& code) {
  return {static_cast<const uint8_t*>(code.pShaderBytecode),code.BytecodeLength};
}
}  // namespace

NativeD3D12PipelineCache::NativeD3D12PipelineCache(ID3D12Device& device, ID3D12RootSignature& signature,
                                                   NativeD3D12PipelineCacheOptions options)
    : device_(&device),signature_(&signature),options_(std::move(options)) {
  signature_hash_=NativeSha256Of(SerializeNativeD3D12RootSignature());
  if(options_.manifest.empty()) return;
  Load();
  if(queue_.empty()) return;
  const uint32_t threads=(std::min)(options_.prewarm_threads,8u);
  for(uint32_t index=0;index<threads;++index) warmers_.emplace_back([this] { Prebuild(); });
}

NativeD3D12PipelineCache::~NativeD3D12PipelineCache() {
  {
    std::lock_guard lock(mutex_);
    stopping_=true;
  }
  built_.notify_all();
  for(auto& warmer:warmers_) if(warmer.joinable()) warmer.join();
  JoinSaver();
}

void NativeD3D12PipelineCache::JoinSaver() {
  std::lock_guard lock(save_mutex_);
  if(saver_.joinable()) saver_.join();
}

std::shared_ptr<const std::vector<uint8_t>> NativeD3D12PipelineCache::Intern(std::span<const uint8_t> bytecode) {
  // One copy per distinct shader, however many pipelines use it. Called with
  // mutex_ held.
  const auto hash=NativeSha256Of(bytecode);
  auto& slot=bytecode_[hash];
  if(auto held=slot.lock()) return held;
  auto owned=std::make_shared<const std::vector<uint8_t>>(bytecode.begin(),bytecode.end());
  slot=owned;
  return owned;
}

NativeD3D12PipelineCache::Desc NativeD3D12PipelineCache::DescOf(const Request& request) {
  Desc desc;
  {
    std::lock_guard lock(mutex_);
    desc.vertex=Intern(BytesOf(request.vertex));
    desc.pixel=Intern(BytesOf(request.pixel));
  }
  desc.layout.reserve(request.input_layout.size());
  for(const auto& element:request.input_layout)
    desc.layout.push_back({element.SemanticName?element.SemanticName:"",element.SemanticIndex,
                           uint32_t(element.Format),element.InputSlot,element.AlignedByteOffset,
                           uint32_t(element.InputSlotClass),element.InstanceDataStepRate});
  desc.state=request.state;
  desc.state[5]=0;  // Scissor: a command in D3D12, never pipeline state.
  desc.topology=request.key.topology;
  desc.render_targets=request.key.render_targets;
  for(uint32_t index=0;index<8;++index) desc.rtv_format[index]=request.key.rtv_format[index];
  desc.dsv_format=request.key.dsv_format;
  desc.sample_count=request.key.sample_count;
  desc.sample_quality=request.key.sample_quality;
  return desc;
}

NativeContentHash NativeD3D12PipelineCache::KeyOf(const Desc& desc) const {
  NativeSha256 hash;
  hash.AddField(std::string_view("edf-d3d12-pipeline-v1"));
  hash.Add(signature_hash_);
  hash.AddField(std::span<const uint8_t>(*desc.vertex));
  hash.AddField(std::span<const uint8_t>(*desc.pixel));
  hash.AddU64(desc.layout.size());
  for(const auto& element:desc.layout) {
    hash.AddField(std::string_view(element.semantic));
    hash.AddU32(element.index).AddU32(element.format).AddU32(element.slot).AddU32(element.offset)
        .AddU32(element.classification).AddU32(element.step);
  }
  for(uint32_t index=0;index<5;++index) hash.AddU32(desc.state[index]);
  hash.AddU32(desc.topology).AddU32(desc.render_targets);
  // Only the formats a pipeline declares are pipeline state; the rest of the
  // array is whatever the caller left there.
  for(uint32_t index=0;index<desc.render_targets && index<8;++index) hash.AddU32(desc.rtv_format[index]);
  hash.AddU32(desc.dsv_format).AddU32(desc.sample_count).AddU32(desc.sample_quality);
  return hash.Finish();
}

NativeContentHash NativeD3D12PipelineCache::ContentKey(const Request& request) const {
  // DescOf interns; a const probe builds a private description instead.
  Desc desc;
  desc.vertex=std::make_shared<const std::vector<uint8_t>>(BytesOf(request.vertex).begin(),BytesOf(request.vertex).end());
  desc.pixel=std::make_shared<const std::vector<uint8_t>>(BytesOf(request.pixel).begin(),BytesOf(request.pixel).end());
  for(const auto& element:request.input_layout)
    desc.layout.push_back({element.SemanticName?element.SemanticName:"",element.SemanticIndex,
                           uint32_t(element.Format),element.InputSlot,element.AlignedByteOffset,
                           uint32_t(element.InputSlotClass),element.InstanceDataStepRate});
  desc.state=request.state;
  desc.state[5]=0;
  desc.topology=request.key.topology;
  desc.render_targets=request.key.render_targets;
  for(uint32_t index=0;index<8;++index) desc.rtv_format[index]=request.key.rtv_format[index];
  desc.dsv_format=request.key.dsv_format;
  desc.sample_count=request.key.sample_count;
  desc.sample_quality=request.key.sample_quality;
  return KeyOf(desc);
}

ComPtr<ID3D12PipelineState> NativeD3D12PipelineCache::Build(const Desc& source) const {
  const auto decoded=DecodeNativeRenderState(source.state);
  std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
  elements.reserve(source.layout.size());
  for(const auto& element:source.layout)
    elements.push_back({element.semantic.c_str(),element.index,static_cast<DXGI_FORMAT>(element.format),
                        element.slot,element.offset,
                        static_cast<D3D12_INPUT_CLASSIFICATION>(element.classification),element.step});

  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
  desc.pRootSignature=signature_;
  desc.VS={source.vertex->data(),source.vertex->size()};
  desc.PS={source.pixel->data(),source.pixel->size()};
  desc.SampleMask=UINT_MAX;
  desc.PrimitiveTopologyType=static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(source.topology);
  desc.NumRenderTargets=source.render_targets;
  for(uint32_t index=0;index<source.render_targets && index<8;++index)
    desc.RTVFormats[index]=static_cast<DXGI_FORMAT>(source.rtv_format[index]);
  desc.DSVFormat=static_cast<DXGI_FORMAT>(source.dsv_format);
  desc.SampleDesc={source.sample_count,source.sample_quality};
  desc.InputLayout={elements.data(),static_cast<UINT>(elements.size())};

  auto& target=desc.BlendState.RenderTarget[0];
  target.BlendEnable=decoded.blend_enable;
  target.SrcBlend=static_cast<D3D12_BLEND>(decoded.src_color);
  target.DestBlend=static_cast<D3D12_BLEND>(decoded.dst_color);
  target.BlendOp=static_cast<D3D12_BLEND_OP>(decoded.color_op);
  target.SrcBlendAlpha=static_cast<D3D12_BLEND>(decoded.src_alpha);
  target.DestBlendAlpha=static_cast<D3D12_BLEND>(decoded.dst_alpha);
  target.BlendOpAlpha=static_cast<D3D12_BLEND_OP>(decoded.alpha_op);
  target.LogicOp=D3D12_LOGIC_OP_NOOP;
  target.RenderTargetWriteMask=decoded.write_mask;

  desc.DepthStencilState.DepthEnable=decoded.depth_enable;
  desc.DepthStencilState.DepthWriteMask=decoded.depth_write?D3D12_DEPTH_WRITE_MASK_ALL
                                                           :D3D12_DEPTH_WRITE_MASK_ZERO;
  desc.DepthStencilState.DepthFunc=static_cast<D3D12_COMPARISON_FUNC>(decoded.depth_func);

  desc.RasterizerState.FillMode=static_cast<D3D12_FILL_MODE>(decoded.fill);
  desc.RasterizerState.CullMode=static_cast<D3D12_CULL_MODE>(decoded.cull);
  desc.RasterizerState.FrontCounterClockwise=decoded.front_counter_clockwise;
  desc.RasterizerState.DepthClipEnable=decoded.depth_clip;
  // decoded.scissor is intentionally unused here: in D3D12 the scissor
  // rectangle is a command, not pipeline state, so the recorder applies it.

  ComPtr<ID3D12PipelineState> pipeline;
  Require(device_->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&pipeline)),"pipeline state creation");
  return pipeline;
}

void NativeD3D12PipelineCache::MarkUsed(Entry& entry) {
  if(entry.last_used_run==run_) return;
  entry.last_used_run=run_;
  dirty_=true;
}

ID3D12PipelineState& NativeD3D12PipelineCache::Get(const Request& request) {
  const std::string_view identity(reinterpret_cast<const char*>(&request.key),sizeof(request.key));
  {
    std::lock_guard lock(mutex_);
    if(const auto found=by_identity_.find(identity);found!=by_identity_.end()) { ++hits_; return *found->second; }
  }
  // A new identity. Hashing the description costs microseconds; the build it
  // may avoid costs milliseconds.
  auto desc=DescOf(request);
  const auto key=KeyOf(desc);
  std::unique_lock lock(mutex_);
  auto& slot=by_content_[key];
  if(!slot) {
    slot=std::make_shared<Entry>();
    slot->desc=std::move(desc);
    slot->key=key;
    slot->state=EntryState::Failed;  // Built below, as a cold miss.
  }
  const auto entry=slot;
  if(entry->state==EntryState::Building) {
    // A warmer has it. Waiting for that one build costs less than building it
    // a second time, and it is the same pipeline.
    const auto start=std::chrono::steady_clock::now();
    ++waits_;
    built_.wait(lock,[&] { return entry->state!=EntryState::Building; });
    const auto waited=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now()-start).count());
    wait_ns_+=waited;
    if(NativeFirstUseLog::Get().enabled())
      NativeFirstUseLog::Get().Record(NativeFirstUseKind::PipelineWait,double(waited)/1e6,
                                      NativeHashHex(key).substr(0,16),DescribeRequest(request));
  }
  if(entry->state==EntryState::Ready) {
    ++content_hits_;
  } else {
    // Queued (the draw got there before a warmer, so it takes the entry rather
    // than waiting behind the queue), failed in the background, or new: built
    // here, exactly as a cache without a manifest would have.
    const bool queued=entry->state==EntryState::Queued;
    entry->state=EntryState::Building;
    ++building_;
    lock.unlock();
    ComPtr<ID3D12PipelineState> pipeline;
    const NativeFirstUseTimer timer;
    try {
      pipeline=Build(entry->desc);
      if(timer.on())
        NativeFirstUseLog::Get().Record(NativeFirstUseKind::PipelineBuild,timer.ms(),NativeHashHex(key).substr(0,16),
          std::string(queued?"source=manifest_queue ":"source=new ")+DescribeRequest(request));
    } catch(...) {
      lock.lock();
      entry->state=EntryState::Failed;
      --building_;
      built_.notify_all();
      throw;
    }
    lock.lock();
    entry->pipeline=std::move(pipeline);
    entry->state=EntryState::Ready;
    --building_;
    ++misses_;
    dirty_=true;
    built_.notify_all();
  }
  MarkUsed(*entry);
  auto* pipeline=entry->pipeline.Get();
  by_identity_.emplace(std::string(identity),pipeline);
  return *pipeline;
}

void NativeD3D12PipelineCache::Prebuild() {
  // Background work: below the threads that make frames.
  SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
  std::unique_lock lock(mutex_);
  for(;;) {
    built_.wait(lock,[&] { return stopping_ || !queue_.empty(); });
    if(stopping_) return;
    const auto entry=queue_.front();
    queue_.pop_front();
    if(entry->state!=EntryState::Queued) { built_.notify_all(); continue; }  // A draw took it.
    entry->state=EntryState::Building;
    ++building_;
    lock.unlock();
    ComPtr<ID3D12PipelineState> pipeline;
    const auto start=std::chrono::steady_clock::now();
    try { pipeline=Build(entry->desc); } catch(...) {}
    const auto spent=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now()-start).count());
    lock.lock();
    prebuild_ns_+=spent;
    --building_;
    if(pipeline) {
      entry->pipeline=std::move(pipeline);
      entry->state=EntryState::Ready;
      ++prebuilt_;
    } else {
      // Not fatal and not dropped yet: a draw that needs it builds it itself
      // and reports the real error, as it would have without a manifest.
      entry->state=EntryState::Failed;
      ++prebuild_failures_;
    }
    built_.notify_all();
  }
}

void NativeD3D12PipelineCache::WaitForPrebuild() {
  std::unique_lock lock(mutex_);
  built_.wait(lock,[&] {
    if(building_) return false;
    for(const auto& entry:queue_) if(entry->state==EntryState::Queued) return warmers_.empty() || stopping_;
    return true;
  });
}

uint64_t NativeD3D12PipelineCache::hits() const { std::lock_guard lock(mutex_); return hits_; }
uint64_t NativeD3D12PipelineCache::misses() const { std::lock_guard lock(mutex_); return misses_; }
size_t NativeD3D12PipelineCache::size() const { std::lock_guard lock(mutex_); return by_identity_.size(); }
bool NativeD3D12PipelineCache::dirty() const { std::lock_guard lock(mutex_); return dirty_; }

NativeD3D12PipelineCache::Statistics NativeD3D12PipelineCache::statistics() const {
  std::lock_guard lock(mutex_);
  Statistics out;
  out.hits=hits_; out.misses=misses_; out.content_hits=content_hits_;
  out.waits=waits_; out.wait_ns=wait_ns_; out.prebuilt=prebuilt_; out.prebuild_failures=prebuild_failures_;
  out.prebuild_ns=prebuild_ns_; out.queued=0;
  for(const auto& entry:queue_) if(entry->state==EntryState::Queued) ++out.queued;
  out.manifest_entries=manifest_entries_; out.saves=saves_; out.adapter_changed=adapter_changed_;
  out.pipelines=by_content_.size();
  return out;
}
std::string NativeD3D12PipelineCache::manifest_status() const {
  std::lock_guard lock(mutex_);
  return manifest_status_;
}

void NativeD3D12PipelineCache::Load() {
  const auto file=NativeReadCacheFile(options_.manifest,kManifestLimit);
  if(!file) { manifest_status_="missing"; return; }
  // Parsed completely before anything is kept: a file that fails anywhere is
  // discarded whole, never half-used.
  std::vector<std::shared_ptr<Entry>> entries;
  std::vector<SamplerCombination> samplers;
  uint32_t run=0;
  bool adapter_changed=false;
  try {
    if(file->size()<sizeof(kManifestMagic)+32) throw NativeCacheFormatError("the manifest is truncated");
    const std::span<const uint8_t> body(file->data(),file->size()-32);
    NativeContentHash digest;
    std::memcpy(digest.data(),file->data()+body.size(),digest.size());
    if(NativeSha256Of(body)!=digest) throw NativeCacheFormatError("the manifest digest does not match");
    NativeCacheReader reader(body);
    char magic[sizeof(kManifestMagic)];
    reader.Bytes(magic,sizeof(magic));
    if(std::memcmp(magic,kManifestMagic,sizeof(magic))) throw NativeCacheFormatError("not a pipeline manifest");
    if(reader.U32()!=kManifestFormat) throw NativeCacheFormatError("the manifest format changed");
    if(reader.Hash()!=signature_hash_) throw NativeCacheFormatError("the root signature changed");
    NativeD3D12AdapterIdentity adapter;
    adapter.vendor=reader.U32(); adapter.device=reader.U32(); adapter.subsystem=reader.U32();
    adapter.revision=reader.U32(); adapter.driver=reader.U64();
    adapter_changed=!(adapter==options_.adapter);
    run=reader.U32();
    const auto shaders=reader.U64();
    if(shaders>kCountLimit) throw NativeCacheFormatError("too many shaders");
    std::vector<std::shared_ptr<const std::vector<uint8_t>>> bytecode;
    bytecode.reserve(size_t(shaders));
    for(uint64_t index=0;index<shaders;++index) {
      auto blob=reader.Blob(kBytecodeLimit);
      std::lock_guard lock(mutex_);
      bytecode.push_back(Intern(blob));
    }
    const auto count=reader.U64();
    if(count>kCountLimit) throw NativeCacheFormatError("too many pipelines");
    for(uint64_t index=0;index<count;++index) {
      auto entry=std::make_shared<Entry>();
      const auto stored=reader.Hash();
      const auto vertex=reader.U32(),pixel=reader.U32();
      if(vertex>=bytecode.size() || pixel>=bytecode.size()) throw NativeCacheFormatError("a shader index is out of range");
      entry->desc.vertex=bytecode[vertex];
      entry->desc.pixel=bytecode[pixel];
      const auto elements=reader.U64();
      if(elements>D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT) throw NativeCacheFormatError("too many input elements");
      for(uint64_t element=0;element<elements;++element) {
        Element value;
        value.semantic=reader.Text(kSemanticLimit);
        value.index=reader.U32(); value.format=reader.U32(); value.slot=reader.U32();
        value.offset=reader.U32(); value.classification=reader.U32(); value.step=reader.U32();
        entry->desc.layout.push_back(std::move(value));
      }
      for(auto& word:entry->desc.state) word=reader.U32();
      entry->desc.state[5]=0;
      entry->desc.topology=reader.U32();
      entry->desc.render_targets=reader.U32();
      if(entry->desc.render_targets>8) throw NativeCacheFormatError("too many render targets");
      for(auto& format:entry->desc.rtv_format) format=reader.U32();
      entry->desc.dsv_format=reader.U32();
      entry->desc.sample_count=reader.U32();
      entry->desc.sample_quality=reader.U32();
      entry->last_used_run=reader.U32();
      // The key is recomputed, never trusted: what gets built is what this
      // build's key says the description is.
      entry->key=KeyOf(entry->desc);
      if(entry->key!=stored) throw NativeCacheFormatError("a pipeline key does not match its description");
      entries.push_back(std::move(entry));
    }
    const auto tables=reader.U64();
    if(tables>kCountLimit) throw NativeCacheFormatError("too many sampler tables");
    for(uint64_t index=0;index<tables;++index) {
      const auto slots=reader.U64();
      if(slots>NativeD3D12RootLayout::kPixelSamplers) throw NativeCacheFormatError("a sampler table is too wide");
      SamplerCombination combination(static_cast<size_t>(slots));
      for(auto& sampler:combination) reader.Bytes(&sampler,sizeof(sampler));
      samplers.push_back(std::move(combination));
    }
    if(reader.remaining()) throw NativeCacheFormatError("the manifest has trailing bytes");
  } catch(const std::exception& error) {
    manifest_status_=std::string("rejected: ")+error.what();
    return;
  }
  std::lock_guard lock(mutex_);
  run_=run+1;
  adapter_changed_=adapter_changed;
  for(auto& entry:entries) {
    auto& slot=by_content_[entry->key];
    if(slot) continue;
    slot=entry;
    queue_.push_back(entry);
    ++manifest_entries_;
  }
  loaded_samplers_=std::move(samplers);
  manifest_status_="loaded";
  // Every run advances the run counter the ages are measured in.
  dirty_=true;
}

NativeD3D12PipelineCache::Snapshot NativeD3D12PipelineCache::TakeSnapshot(std::vector<SamplerCombination> samplers) {
  Snapshot snapshot;
  snapshot.samplers=std::move(samplers);
  std::lock_guard lock(mutex_);
  snapshot.run=run_;
  for(const auto& [key,entry]:by_content_) {
    // A description this adapter failed to build and nobody used is dropped;
    // one used within the age limit is kept, built or still queued.
    if(entry->state==EntryState::Failed && entry->last_used_run!=run_) continue;
    if(run_-entry->last_used_run>=options_.max_age_runs && entry->last_used_run!=run_) continue;
    snapshot.entries.emplace_back(entry,entry->last_used_run);
  }
  // Most recently used first, so the cap drops the stalest.
  std::stable_sort(snapshot.entries.begin(),snapshot.entries.end(),
                   [](const auto& a,const auto& b) { return a.second>b.second; });
  if(snapshot.entries.size()>options_.max_entries) snapshot.entries.resize(options_.max_entries);
  dirty_=false;
  return snapshot;
}

std::vector<uint8_t> NativeD3D12PipelineCache::Serialize(const Snapshot& snapshot) const {
  // Entries' descriptions are immutable once created, so this runs without
  // the mutex, on whichever thread is saving.
  NativeCacheWriter writer;
  writer.Bytes(kManifestMagic,sizeof(kManifestMagic));
  writer.U32(kManifestFormat);
  writer.Hash(signature_hash_);
  writer.U32(options_.adapter.vendor); writer.U32(options_.adapter.device);
  writer.U32(options_.adapter.subsystem); writer.U32(options_.adapter.revision);
  writer.U64(options_.adapter.driver);
  writer.U32(snapshot.run);
  std::map<const std::vector<uint8_t>*,uint32_t> shader_index;
  std::vector<const std::vector<uint8_t>*> shaders;
  const auto index_of=[&](const std::shared_ptr<const std::vector<uint8_t>>& code) {
    const auto [found,inserted]=shader_index.emplace(code.get(),uint32_t(shaders.size()));
    if(inserted) shaders.push_back(code.get());
    return found->second;
  };
  for(const auto& [entry,used]:snapshot.entries) { index_of(entry->desc.vertex); index_of(entry->desc.pixel); }
  writer.U64(shaders.size());
  for(const auto* code:shaders) writer.Blob(*code);
  writer.U64(snapshot.entries.size());
  for(const auto& [entry,used]:snapshot.entries) {
    const auto& desc=entry->desc;
    writer.Hash(entry->key);
    writer.U32(shader_index.at(desc.vertex.get()));
    writer.U32(shader_index.at(desc.pixel.get()));
    writer.U64(desc.layout.size());
    for(const auto& element:desc.layout) {
      writer.Text(element.semantic);
      writer.U32(element.index); writer.U32(element.format); writer.U32(element.slot);
      writer.U32(element.offset); writer.U32(element.classification); writer.U32(element.step);
    }
    for(const auto word:desc.state) writer.U32(word);
    writer.U32(desc.topology);
    writer.U32(desc.render_targets);
    for(const auto format:desc.rtv_format) writer.U32(format);
    writer.U32(desc.dsv_format);
    writer.U32(desc.sample_count);
    writer.U32(desc.sample_quality);
    writer.U32(used);
  }
  writer.U64(snapshot.samplers.size());
  for(const auto& combination:snapshot.samplers) {
    writer.U64(combination.size());
    for(const auto& sampler:combination) writer.Bytes(&sampler,sizeof(sampler));
  }
  const auto digest=NativeSha256Of(writer.bytes());
  writer.Hash(digest);
  return std::move(writer.bytes());
}

bool NativeD3D12PipelineCache::Save(std::vector<SamplerCombination> samplers) {
  if(options_.manifest.empty()) return false;
  JoinSaver();
  const auto bytes=Serialize(TakeSnapshot(std::move(samplers)));
  if(!NativeWriteCacheFile(options_.manifest,bytes)) return false;
  std::lock_guard lock(mutex_);
  ++saves_;
  return true;
}

void NativeD3D12PipelineCache::SaveInBackground(std::vector<SamplerCombination> samplers) {
  if(options_.manifest.empty() || saving_.exchange(true)) return;
  std::lock_guard lock(save_mutex_);
  if(saver_.joinable()) saver_.join();
  auto snapshot=std::make_shared<Snapshot>(TakeSnapshot(std::move(samplers)));
  saver_=std::thread([this,snapshot] {
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
    const bool saved=NativeWriteCacheFile(options_.manifest,Serialize(*snapshot));
    if(saved) { std::lock_guard lock(mutex_); ++saves_; }
    saving_=false;
  });
}
}  // namespace edf::native

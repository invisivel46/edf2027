// First-use hitch removal: the shader bytecode cache (CompileNativeShader), the
// persistent D3D12 pipeline manifest and its background prebuild, the placed
// buffer pool and the sampler-table warm start. Each is checked for what it
// must never do - hand back something that differs from the cold path - as
// well as for what it is for: a hit where the cold path would have paid.
// Runs on WARP, like the other D3D12 suites.
#include "native_graphics/d3d11_effect.h"
#include "native_graphics/d3d12_backend.h"
#include "native_graphics/d3d12_buffer_pool.h"
#include "native_graphics/d3d12_device.h"
#include "native_graphics/d3d12_pipeline.h"
#include "native_graphics/native_disk_cache.h"
#include "native_graphics/native_frame_times.h"
#include "native_graphics/native_first_use.h"
#include "native_graphics/native_shader_precompile.h"
#include "native_graphics/native_render_backend.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace edf::native;

namespace {
int failures=0;
void Check(bool ok,const std::string& message) {
  if(ok) return;
  ++failures;
  std::cerr<<"FAIL: "<<message<<'\n';
}
std::filesystem::path Scratch(const char* name) {
  const auto path=std::filesystem::temp_directory_path()/
    ("edf_first_use_"+std::to_string(GetCurrentProcessId())+"_"+name);
  std::error_code error;
  std::filesystem::remove_all(path,error);
  std::filesystem::create_directories(path);
  return path;
}
void WriteText(const std::filesystem::path& path,const std::string& text) {
  std::ofstream file(path,std::ios::binary|std::ios::trunc);
  file<<text;
}
std::vector<uint8_t> BytesOf(ID3DBlob& blob) {
  const auto* data=static_cast<const uint8_t*>(blob.GetBufferPointer());
  return {data,data+blob.GetBufferSize()};
}
size_t FileCount(const std::filesystem::path& directory) {
  if(!std::filesystem::exists(directory)) return 0;
  size_t count=0;
  for(const auto& entry:std::filesystem::directory_iterator(directory)) if(entry.is_regular_file()) ++count;
  return count;
}
ComPtr<ID3DBlob> Compile(const char* source,const char* entry,const char* profile) {
  ComPtr<ID3DBlob> code,errors;
  if(FAILED(D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,entry,profile,0,0,&code,&errors)))
    throw std::runtime_error(std::string("shader compile failed: ")+
                             (errors?static_cast<const char*>(errors->GetBufferPointer()):"no detail"));
  return code;
}

// ---------------------------------------------------------------------------
void TestShaderCache() {
  const auto root=Scratch("shaders");
  const auto cache=root/"cache";
  const auto source_path=root/"effect.fx";
  WriteText(root/"Included.fx","float4 Tint() { return float4(0.25,0.5,0.75,1); }\n");
  const char* kSource=R"(
#include "Included.fx"
struct VOut { float4 position : POSITION; };
VOut VS(float4 p : POSITION) { VOut o; o.position = normalize(p) + Tint(); return o; }
float4 PS(float4 p : SV_POSITION) : SV_TARGET { return Tint(); }
)";
  Effect effect; effect.source=kSource;
  const ShaderEntry vs{false,"VS","vs_3_0"},ps{true,"PS","ps_3_0"};
  SetNativeCacheDirectory(cache);
  ClearNativeShaderMemoryCache();
  const auto start=GetNativeShaderCacheStatistics();
  const auto compiles_before=FrameEventCounters().shader_compiles.load();
  const auto hits_before=FrameEventCounters().shader_cache_hits.load();

  auto cold=CompileNativeShader(nullptr,effect,vs,source_path);
  auto after=GetNativeShaderCacheStatistics();
  Check(after.compiles==start.compiles+1 && after.disk_stores==start.disk_stores+1,
        "a first compile did not compile once and store once");
  Check(FileCount(cache/"shaders")==1,"a first compile did not leave exactly one cache file");
  Check(FrameEventCounters().shader_compiles.load()==compiles_before+1,"shader_compiles did not count the real compile");

  auto memory=CompileNativeShader(nullptr,effect,vs,source_path);
  after=GetNativeShaderCacheStatistics();
  Check(after.compiles==start.compiles+1 && after.memory_hits==start.memory_hits+1,
        "an identical second compile was not answered from memory");
  Check(BytesOf(*memory.bytecode.Get())==BytesOf(*cold.bytecode.Get()),
        "a memory hit returned different bytecode than the compile it cached");
  Check(memory.reflection!=nullptr,"a cache hit came back without reflection");
  Check(FrameEventCounters().shader_cache_hits.load()==hits_before+1,"shader_cache_hits did not count the hit");

  // A new process: nothing in memory, the file from the first run.
  ClearNativeShaderMemoryCache();
  auto disk=CompileNativeShader(nullptr,effect,vs,source_path);
  after=GetNativeShaderCacheStatistics();
  Check(after.compiles==start.compiles+1 && after.disk_hits==start.disk_hits+1,
        "a second run's compile was not answered from disk");
  Check(BytesOf(*disk.bytecode.Get())==BytesOf(*cold.bytecode.Get()),
        "a disk hit returned different bytecode than the compile that wrote it");

  // Cache off: the same bytes straight from the compiler, so a hit is exactly
  // what an uncached run would have used.
  SetNativeCacheDirectory({});
  ClearNativeShaderMemoryCache();
  auto uncached=CompileNativeShader(nullptr,effect,vs,source_path);
  Check(BytesOf(*uncached.bytecode.Get())==BytesOf(*cold.bytecode.Get()),
        "the cached bytecode differs from an uncached compile");
  SetNativeCacheDirectory(cache);
  ClearNativeShaderMemoryCache();

  // Everything the bytecode depends on is in the key.
  auto count=GetNativeShaderCacheStatistics().compiles;
  CompileNativeShader(nullptr,effect,ps,source_path);
  Check(GetNativeShaderCacheStatistics().compiles==count+1,"a different entry point hit another entry's bytecode");
  count=GetNativeShaderCacheStatistics().compiles;
  CompileNativeShader(nullptr,effect,vs,source_path,true);
  Check(GetNativeShaderCacheStatistics().compiles==count+1,"the reversed-depth variant hit the plain variant");
  count=GetNativeShaderCacheStatistics().compiles;
  Effect edited=effect; edited.source+="\n// edited\nstatic const float kUnused=1;\n";
  CompileNativeShader(nullptr,edited,vs,source_path);
  Check(GetNativeShaderCacheStatistics().compiles==count+1,"an edited source hit the old bytecode");

  // An include is not in the source text; only the preprocessed key sees it.
  WriteText(root/"Included.fx","float4 Tint() { return float4(1,0,0,1); }\n");
  count=GetNativeShaderCacheStatistics().compiles;
  auto changed=CompileNativeShader(nullptr,effect,vs,source_path);
  Check(GetNativeShaderCacheStatistics().compiles==count+1,"a changed include hit bytecode built from the old include");
  Check(BytesOf(*changed.bytecode.Get())!=BytesOf(*cold.bytecode.Get()),"the changed include did not change the bytecode");
  WriteText(root/"Included.fx","float4 Tint() { return float4(0.25,0.5,0.75,1); }\n");

  // A compiler update misses everything the old one built.
  ClearNativeShaderMemoryCache();
  SetNativeShaderCompilerIdentityForTesting("99.0.0.0|1");
  count=GetNativeShaderCacheStatistics().compiles;
  CompileNativeShader(nullptr,effect,vs,source_path);
  Check(GetNativeShaderCacheStatistics().compiles==count+1,"a different compiler version hit the old compiler's bytecode");
  SetNativeShaderCompilerIdentityForTesting(NativeModuleIdentity(L"d3dcompiler_47.dll"));

  // A damaged file is rejected, recompiled and replaced - never used.
  ClearNativeShaderMemoryCache();
  for(const auto& entry:std::filesystem::directory_iterator(cache/"shaders")) {
    std::fstream file(entry.path(),std::ios::binary|std::ios::in|std::ios::out);
    file.seekg(-3,std::ios::end);
    char byte=0;
    file.get(byte);
    file.seekp(-3,std::ios::end);
    file.put(char(byte^0x5a));
  }
  const auto rejects=GetNativeShaderCacheStatistics().disk_rejects;
  count=GetNativeShaderCacheStatistics().compiles;
  auto repaired=CompileNativeShader(nullptr,effect,vs,source_path);
  Check(GetNativeShaderCacheStatistics().disk_rejects>rejects,"a damaged cache file was not rejected");
  Check(GetNativeShaderCacheStatistics().compiles==count+1,"a damaged cache file was not recompiled");
  Check(BytesOf(*repaired.bytecode.Get())==BytesOf(*cold.bytecode.Get()),"the recompile after damage differs");
  ClearNativeShaderMemoryCache();
  count=GetNativeShaderCacheStatistics().compiles;
  CompileNativeShader(nullptr,effect,vs,source_path);
  Check(GetNativeShaderCacheStatistics().compiles==count,"the damaged file was not replaced by the recompile");

  // No directory: nothing on disk.
  const auto files=FileCount(cache/"shaders");
  SetNativeCacheDirectory({});
  ClearNativeShaderMemoryCache();
  Effect fresh=effect; fresh.source+="\nstatic const float kOther=2;\n";
  CompileNativeShader(nullptr,fresh,vs,source_path);
  Check(FileCount(cache/"shaders")==files,"a cache-less compile wrote a file");
  const auto stats=GetNativeShaderCacheStatistics();
  std::cout<<"shader cache: compiles="<<stats.compiles<<" memory_hits="<<stats.memory_hits<<" disk_hits="
           <<stats.disk_hits<<" stores="<<stats.disk_stores<<" rejects="<<stats.disk_rejects<<'\n';
}

// ---------------------------------------------------------------------------
// Two threads asking for the same uncached shader at once: one compiles, the
// other waits for that compile, and both get its bytes.
void TestShaderInFlight() {
  const auto root=Scratch("in_flight");
  SetNativeCacheDirectory(root/"cache");
  ClearNativeShaderMemoryCache();
  NativeFirstUseLog::Get().SetEnabled(true);
  (void)NativeFirstUseLog::Get().Take();
  Effect effect;
  // Long enough to compile that the second thread arrives while it runs.
  effect.source="float4 VS(float4 p : POSITION) : POSITION { float4 a=p;\n";
  for(int index=0;index<400;++index) effect.source+="a=sin(a)*cos(a+"+std::to_string(index)+");\n";
  effect.source+="return a; }\n";
  const ShaderEntry vs{false,"VS","vs_3_0"};
  const auto before=GetNativeShaderCacheStatistics();
  NativeShader first,second;
  std::thread other([&] { first=CompileNativeShader(nullptr,effect,vs,root/"effect.fx"); });
  second=CompileNativeShader(nullptr,effect,vs,root/"effect.fx");
  other.join();
  const auto after=GetNativeShaderCacheStatistics();
  Check(after.compiles==before.compiles+1,"two concurrent requests for one shader compiled it "+
        std::to_string(after.compiles-before.compiles)+" times");
  Check(BytesOf(*first.bytecode.Get())==BytesOf(*second.bytecode.Get()),"concurrent requests got different bytecode");
  const auto events=NativeFirstUseLog::Get().Take();
  size_t compiles=0;
  for(const auto& event:events) if(event.kind==NativeFirstUseKind::ShaderCompile) {
    ++compiles;
    Check(event.detail.find("entry=VS target=vs_5_0 variant=plain")!=std::string::npos,
          "a compile record does not name its entry: "+event.detail);
    Check(event.ms>0 && event.key.size()==16,"a compile record lacks its time or key");
  }
  Check(compiles==1,"first-use records: "+std::to_string(compiles)+" compiles for one shader");
  NativeFirstUseLog::Get().SetEnabled(false);
  SetNativeCacheDirectory({});
}

// A minimal retail-layout DXSL (effect.cpp's ParseEffect): header, entry
// records, their strings and the HLSL source; no techniques.
std::vector<uint8_t> MakeDxsl(const std::string& source) {
  std::vector<uint8_t> out(48,0);
  const auto word=[&](size_t at,uint32_t value) { std::memcpy(out.data()+at,&value,4); };
  const auto text=[&](const std::string& value) {
    const auto at=uint32_t(out.size());
    out.insert(out.end(),value.begin(),value.end());
    out.push_back(0);
    return at;
  };
  word(0,0x4c535844);  // "DXSL"
  word(8,0); word(12,24);
  word(16,2); word(20,24);
  const uint32_t names[4]={text("VS"),text("vs_3_0"),text("PS"),text("ps_3_0")};
  word(24,0); word(28,names[0]-24); word(32,names[1]-24);
  word(36,1); word(40,names[2]-36); word(44,names[3]-36);
  word(4,text(source));
  return out;
}

void TestShaderPrecompile() {
  const auto root=Scratch("precompile");
  const auto cache=root/"cache";
  std::filesystem::create_directories(root/"game"/"Shader");
  const char* kSource=R"(
float4x4 g_mWorld;
float4x4 g_mViewProj;
struct VOut { float4 position : POSITION; };
VOut VS(float4 p : POSITION) { VOut o; o.position = mul(mul(p, g_mWorld), g_mViewProj); return o; }
float4 PS(float4 p : SV_POSITION) : SV_TARGET { return float4(1,0.5,0.25,1); }
)";
  const auto dxsl=MakeDxsl(kSource);
  {
    std::ofstream file(root/"game"/"Shader"/"Test.dxsl",std::ios::binary);
    file.write(reinterpret_cast<const char*>(dxsl.data()),std::streamsize(dxsl.size()));
  }
  WriteText(root/"game"/"Shader"/"Common.fx","// not included\n");
  SetNativeCacheDirectory(cache);
  ClearNativeShaderMemoryCache();
  const auto options=[&](std::string build) {
    NativeShaderPrecompileOptions out;
    out.game_root=root/"game"; out.cache_directory=cache; out.build_identity=std::move(build); out.threads=2;
    return out;
  };
  NativeShaderPrecompileStatus reported;
  {
    NativeShaderPrecompiler precompiler;
    auto cold=options("build-a");
    cold.finished=[&](const NativeShaderPrecompileStatus& status) { reported=status; };
    precompiler.Start(cold);
    precompiler.Wait();
    const auto status=precompiler.status();
    Check(status.state==NativeShaderPrecompileStatus::State::Finished,
          std::string("a cold precompile did not finish: ")+status.state_name()+" "+status.reason);
    Check(status.effects==1 && status.jobs==2 && status.done==2 && status.failed==0,
          "a cold precompile did not take both entries: done="+std::to_string(status.done)+" failed="+
          std::to_string(status.failed)+" "+status.reason);
    // Plain VS, its instanced form, reversed VS, its instanced form, PS.
    Check(status.compiles==5,"a cold precompile made "+std::to_string(status.compiles)+" compiles, not 5");
    Check(reported.state==NativeShaderPrecompileStatus::State::Finished,"the finished callback was not called");
    Check(std::filesystem::exists(cache/"shaders"/"precompile.stamp"),"a complete pass wrote no stamp");
  }
  // The game's registration in a later process: nothing left to compile.
  ClearNativeShaderMemoryCache();
  const auto effect=ParseEffect(DecodeSourceAsset(dxsl));
  const auto source_path=GuestEffectSourcePath(root/"game");
  auto count=GetNativeShaderCacheStatistics().compiles;
  for(const auto& entry:effect.entries) {
    auto shader=CompileNativeShader(nullptr,effect,entry,source_path);
    if(entry.pixel) continue;
    Check(AddNativeWorldInstancing(shader,effect,source_path),"the test VS does not instance");
    auto reversed=CompileNativeShader(nullptr,effect,entry,source_path,true);
    AddNativeWorldInstancing(reversed,effect,source_path,true);
  }
  Check(GetNativeShaderCacheStatistics().compiles==count,
        "a registration after the precompile still compiled "+
        std::to_string(GetNativeShaderCacheStatistics().compiles-count)+" shaders");
  // A warm run: the stamp matches, nothing is read or compiled.
  ClearNativeShaderMemoryCache();
  count=GetNativeShaderCacheStatistics().compiles;
  const auto disk_hits=GetNativeShaderCacheStatistics().disk_hits;
  {
    NativeShaderPrecompiler precompiler;
    precompiler.Start(options("build-a"));
    precompiler.Wait();
    const auto status=precompiler.status();
    Check(status.state==NativeShaderPrecompileStatus::State::Skipped && status.jobs==0,
          std::string("a warm precompile was not skipped: ")+status.state_name()+" "+status.reason);
    Check(GetNativeShaderCacheStatistics().compiles==count && GetNativeShaderCacheStatistics().disk_hits==disk_hits,
          "a skipped precompile touched the shader cache");
  }
  // A new build runs the pass again; the cache still answers every entry.
  {
    NativeShaderPrecompiler precompiler;
    precompiler.Start(options("build-b"));
    precompiler.Wait();
    const auto status=precompiler.status();
    Check(status.state==NativeShaderPrecompileStatus::State::Finished && status.done==2,
          std::string("a new build did not run the pass: ")+status.state_name());
    Check(GetNativeShaderCacheStatistics().compiles==count,"a new build's pass recompiled cached shaders");
  }
  // No cache directory: nothing to keep, so nothing runs.
  {
    NativeShaderPrecompiler precompiler;
    auto off=options("build-c");
    off.cache_directory.clear();
    precompiler.Start(off);
    precompiler.Wait();
    Check(precompiler.status().state==NativeShaderPrecompileStatus::State::Skipped,
          "a precompile ran without a cache directory");
  }
  // Stopped at once: it ends, writes no stamp unless the pass completed, and
  // the destructor after Stop is harmless.
  {
    std::filesystem::remove(cache/"shaders"/"precompile.stamp");
    NativeShaderPrecompiler precompiler;
    precompiler.Start(options("build-d"));
    precompiler.Stop();
    const auto state=precompiler.status().state;
    Check(state!=NativeShaderPrecompileStatus::State::Running,"a stopped precompile still reports running");
    if(state==NativeShaderPrecompileStatus::State::Stopped)
      Check(!std::filesystem::exists(cache/"shaders"/"precompile.stamp"),"a stopped pass wrote a stamp");
  }
  SetNativeCacheDirectory({});
}

// ---------------------------------------------------------------------------
struct PipelineFixture {
  ComPtr<ID3DBlob> vertex,pixel,other_pixel;
  D3D12_INPUT_ELEMENT_DESC layout[1]{
    {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
  PipelineFixture() {
    const char* kSource=R"(
cbuffer PixelData : register(b0) { float4 tint; };
float4 VS(float3 position : POSITION) : SV_POSITION { return float4(position, 1); }
float4 PS(float4 position : SV_POSITION) : SV_TARGET { return tint; }
float4 OtherPS(float4 position : SV_POSITION) : SV_TARGET { return tint.bgra; }
)";
    vertex=Compile(kSource,"VS","vs_5_0");
    pixel=Compile(kSource,"PS","ps_5_0");
    other_pixel=Compile(kSource,"OtherPS","ps_5_0");
  }
  NativeD3D12PipelineCache::Request Request(uint64_t id,uint32_t blend=0x10001) const {
    NativeD3D12PipelineCache::Request request{};
    request.key.vertex_shader=id;
    request.key.pixel_shader=id+1;
    request.key.input_layout=id+2;
    request.key.blend=blend;
    request.key.write_mask=15;
    request.key.topology=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    request.key.render_targets=1;
    request.key.rtv_format[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
    request.vertex={vertex->GetBufferPointer(),vertex->GetBufferSize()};
    request.pixel={pixel->GetBufferPointer(),pixel->GetBufferSize()};
    request.input_layout=layout;
    request.state={blend,0,0,0,15,0};
    return request;
  }
};

// Rewrites a manifest field and re-signs the file, so a test can present a
// well-formed manifest that differs in exactly one respect.
void PatchManifest(const std::filesystem::path& path,size_t offset,const void* bytes,size_t size) {
  auto file=NativeReadCacheFile(path,1u<<30);
  if(!file || file->size()<offset+size+32) throw std::runtime_error("manifest too small to patch");
  std::memcpy(file->data()+offset,bytes,size);
  const auto digest=NativeSha256Of(std::span<const uint8_t>(file->data(),file->size()-32));
  std::memcpy(file->data()+file->size()-32,digest.data(),32);
  if(!NativeWriteCacheFile(path,*file)) throw std::runtime_error("manifest patch write failed");
}
// Header: magic[8], format u32, root-signature hash[32], vendor, device,
// subsystem, revision (u32 each), driver u64, run u32.
constexpr size_t kSignatureOffset=12,kDriverOffset=12+32+16;

void TestPipelineManifest() {
  NativeD3D12Options options;
  options.prefer_warp=true;
  options.debug_layer=true;  // The first device in the process decides; see DecideDebugLayer.
  NativeD3D12Device gpu(options);
  const auto signature=CreateNativeD3D12RootSignature(*gpu.device());
  const PipelineFixture fixture;
  const auto root=Scratch("pipelines");
  NativeD3D12PipelineCacheOptions cache_options;
  cache_options.manifest=root/"pipelines.bin";
  cache_options.adapter={1,2,3,4,5};

  NativeContentHash first_key{};
  {
    NativeD3D12PipelineCache cache(*gpu.device(),*signature.Get(),cache_options);
    Check(cache.manifest_status()=="missing","a first run reported a manifest it does not have");
    const auto request=fixture.Request(100);
    auto& built=cache.Get(request);
    Check(&cache.Get(request)==&built && cache.misses()==1 && cache.hits()==1,"the first run did not build once and hit once");
    // Identical descriptions under other ids: one pipeline, found by content.
    auto renamed=fixture.Request(900);
    Check(cache.ContentKey(renamed)==cache.ContentKey(request),"identical descriptions under other ids have different keys");
    Check(&cache.Get(renamed)==&built,"an identical description under other ids built a second pipeline");
    Check(cache.misses()==1 && cache.statistics().content_hits==1,"the renamed request was not a content hit");
    // Scissor is a command, not pipeline state.
    auto scissored=request; scissored.state[5]=1;
    Check(cache.ContentKey(scissored)==cache.ContentKey(request),"the scissor word changed the pipeline key");
    // Real differences are different keys.
    auto blended=fixture.Request(100,0x10005);
    Check(cache.ContentKey(blended)!=cache.ContentKey(request),"a different blend state has the same key");
    auto shaded=request; shaded.pixel={fixture.other_pixel->GetBufferPointer(),fixture.other_pixel->GetBufferSize()};
    Check(cache.ContentKey(shaded)!=cache.ContentKey(request),"a different pixel shader has the same key");
    cache.Get(blended);
    first_key=cache.ContentKey(request);
    Check(cache.dirty(),"new pipelines did not mark the manifest dirty");
    Check(cache.Save({{{D3D12_FILTER_MIN_MAG_MIP_LINEAR,D3D12_TEXTURE_ADDRESS_MODE_WRAP,D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                        D3D12_TEXTURE_ADDRESS_MODE_WRAP,0,1,D3D12_COMPARISON_FUNC_NEVER,{0,0,0,0},0,D3D12_FLOAT32_MAX}}}),
          "the manifest was not written");
    Check(!cache.dirty(),"a save left the manifest dirty");
  }
  {
    // The next run: both pipelines prebuilt before any draw asks, then found
    // by content under ids this run made up.
    NativeD3D12PipelineCache cache(*gpu.device(),*signature.Get(),cache_options);
    const auto loaded=cache.statistics();
    Check(cache.manifest_status()=="loaded" && loaded.manifest_entries==2,
          "the manifest did not load its two pipelines: "+cache.manifest_status());
    Check(cache.loaded_samplers().size()==1 && cache.loaded_samplers()[0].size()==1 &&
          cache.loaded_samplers()[0][0].AddressU==D3D12_TEXTURE_ADDRESS_MODE_WRAP,
          "the manifest did not carry its sampler combination");
    cache.WaitForPrebuild();
    Check(cache.statistics().prebuilt==2,"the manifest's pipelines were not prebuilt");
    auto& found=cache.Get(fixture.Request(7777));
    auto& blended=cache.Get(fixture.Request(4242,0x10005));
    Check(&found!=&blended,"two different pipelines came back as one");
    Check(cache.misses()==0 && cache.statistics().content_hits==2,
          "a prebuilt pipeline was built again on the draw's thread");
    // A shader the manifest never saw is an ordinary miss.
    auto shaded=fixture.Request(5000);
    shaded.pixel={fixture.other_pixel->GetBufferPointer(),fixture.other_pixel->GetBufferSize()};
    cache.Get(shaded);
    Check(cache.misses()==1,"a changed shader did not miss");
    Check(cache.Save({}),"the second run's manifest was not written");
  }
  {
    // Without warmers, a queued entry is built by the lookup that needs it -
    // on its thread, once, and still from the stored description.
    auto inline_options=cache_options;
    inline_options.prewarm_threads=0;
    NativeD3D12PipelineCache cache(*gpu.device(),*signature.Get(),inline_options);
    Check(cache.statistics().manifest_entries==3,"the second run's addition was not persisted");
    cache.WaitForPrebuild();
    cache.Get(fixture.Request(1));
    Check(cache.misses()==1 && cache.statistics().prebuilt==0,"a queued entry was not built by its first lookup");
  }
  {
    // A driver update keeps the manifest: descriptions mean the same to it.
    const uint64_t driver=6;
    PatchManifest(cache_options.manifest,kDriverOffset,&driver,sizeof(driver));
    NativeD3D12PipelineCache cache(*gpu.device(),*signature.Get(),cache_options);
    const auto stats=cache.statistics();
    Check(cache.manifest_status()=="loaded" && stats.adapter_changed,"a driver change was not reported, or rejected the manifest");
    cache.WaitForPrebuild();
    Check(cache.statistics().prebuilt==3,"a driver change stopped the prebuild");
  }
  {
    // Another root signature: the descriptions are not this build's pipelines.
    uint8_t byte=0;
    {
      auto file=NativeReadCacheFile(cache_options.manifest,1u<<30);
      byte=uint8_t((*file)[kSignatureOffset]^0xff);
    }
    PatchManifest(cache_options.manifest,kSignatureOffset,&byte,1);
    NativeD3D12PipelineCache cache(*gpu.device(),*signature.Get(),cache_options);
    const auto stats=cache.statistics();
    Check(cache.manifest_status().find("root signature")!=std::string::npos && stats.manifest_entries==0,
          "a manifest for another root signature was used: "+cache.manifest_status());
    cache.Get(fixture.Request(1));
    Check(cache.misses()==1,"the cache did not fall back to building after rejecting its manifest");
  }
  {
    // Damage anywhere discards the file whole.
    WriteText(cache_options.manifest,"not a manifest at all, but long enough to be read as one...........");
    NativeD3D12PipelineCache cache(*gpu.device(),*signature.Get(),cache_options);
    Check(cache.manifest_status().rfind("rejected",0)==0,"a damaged manifest was not rejected");
  }
  {
    // Entries age out: unused for max_age_runs runs, dropped at save.
    auto aging=cache_options;
    aging.manifest=root/"aging.bin";
    aging.max_age_runs=2;
    {
      NativeD3D12PipelineCache cache(*gpu.device(),*signature.Get(),aging);
      cache.Get(fixture.Request(1));
      cache.Save({});
    }
    for(int run=0;run<3;++run) {
      NativeD3D12PipelineCache cache(*gpu.device(),*signature.Get(),aging);
      cache.WaitForPrebuild();
      cache.Save({});
    }
    NativeD3D12PipelineCache cache(*gpu.device(),*signature.Get(),aging);
    Check(cache.statistics().manifest_entries==0,"an entry unused for longer than the age limit survived");
  }
  for(const auto& message:gpu.DrainValidationErrors()) Check(false,"D3D12 validation error: "+message);
}

// ---------------------------------------------------------------------------
void TestSamplerPrewarm() {
  NativeD3D12Options options;
  options.prefer_warp=true;
  options.debug_layer=true;  // The first device in the process decides; see DecideDebugLayer.
  options.sampler_tables=4;
  NativeD3D12Device gpu(options);
  const D3D12_SAMPLER_DESC linear{D3D12_FILTER_MIN_MAG_MIP_LINEAR,D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                                  D3D12_TEXTURE_ADDRESS_MODE_WRAP,D3D12_TEXTURE_ADDRESS_MODE_WRAP,0,1,
                                  D3D12_COMPARISON_FUNC_NEVER,{0,0,0,0},0,D3D12_FLOAT32_MAX};
  const D3D12_SAMPLER_DESC point{D3D12_FILTER_MIN_MAG_MIP_POINT,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                 D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,0,1,
                                 D3D12_COMPARISON_FUNC_NEVER,{0,0,0,0},0,D3D12_FLOAT32_MAX};
  const D3D12_SAMPLER_DESC pair[]={linear,point};
  Check(gpu.samplers().Prewarm(pair),"a combination was not prewarmed");
  Check(!gpu.samplers().Prewarm(pair),"a combination was prewarmed twice");
  const auto table=gpu.samplers().Table(pair,1,0);
  Check(gpu.samplers().misses()==0 && gpu.samplers().hits()==1,"a prewarmed combination missed");
  Check(table.ptr!=0,"a prewarmed table has no handle");
  const auto combinations=gpu.samplers().Combinations();
  Check(combinations.size()==1 && combinations[0].size()==2 &&
        !std::memcmp(combinations[0].data(),pair,sizeof(pair)),"the held combinations do not round-trip");
  // A warm start never takes a slot a used table would need: it stops at capacity.
  const D3D12_SAMPLER_DESC singles[4][1]={{linear},{point},{pair[0]},{pair[1]}};
  gpu.samplers().Prewarm(singles[0]);
  gpu.samplers().Prewarm(singles[1]);
  gpu.samplers().Prewarm(std::span<const D3D12_SAMPLER_DESC>(pair,1));  // Same as singles[0].
  D3D12_SAMPLER_DESC anisotropic=linear; anisotropic.Filter=D3D12_FILTER_ANISOTROPIC; anisotropic.MaxAnisotropy=4;
  const D3D12_SAMPLER_DESC third[]={anisotropic};
  gpu.samplers().Prewarm(third);
  D3D12_SAMPLER_DESC border=point; border.AddressU=D3D12_TEXTURE_ADDRESS_MODE_BORDER;
  const D3D12_SAMPLER_DESC fourth[]={border};
  Check(!gpu.samplers().Prewarm(fourth),"a warm start went past the cache's capacity");
  // Prewarmed tables are the first to give way to a table a draw needs.
  gpu.samplers().Table(fourth,2,1);
  Check(gpu.samplers().evictions()==1,"a draw's table did not displace a prewarmed one");
  std::cout<<"sampler prewarm: "<<gpu.samplers().prewarmed()<<" tables written ahead\n";
}

// ---------------------------------------------------------------------------
void TestBufferPool() {
  NativeD3D12Options options;
  options.prefer_warp=true;
  options.debug_layer=true;  // The first device in the process decides; see DecideDebugLayer.
  NativeD3D12Device gpu(options);
  constexpr uint64_t kHeap=4u<<20;
  NativeD3D12BufferPool pool(*gpu.device(),kHeap,1u<<20,1);
  Check(pool.statistics().heaps==1,"the reserved heap was not created up front");
  auto a=pool.Create(1000),b=pool.Create(70000),c=pool.Create(1u<<20);
  Check(a && b && c,"the pool refused buffers it has room for");
  Check(a->GetDesc().Width==1000 && b->GetDesc().Width==70000,"a placed buffer has the wrong size");
  auto stats=pool.statistics();
  Check(stats.live==3 && stats.live_bytes==(1+2+16)*NativeD3D12BufferPool::kAlignment,
        "placed ranges are not 64 KB granular: "+std::to_string(stats.live_bytes));
  Check(!pool.Create((1u<<20)+1),"a buffer over the pool's limit was placed");
  const auto b_address=b->GetGPUVirtualAddress();
  b.Reset();
  Check(pool.statistics().live==2,"a destroyed placed buffer kept its range");
  auto reused=pool.Create(70000);
  Check(reused && reused->GetGPUVirtualAddress()==b_address,"a freed range was not reused first-fit");
  // Fill past the reserved heap: a second heap, released again once empty.
  std::vector<ComPtr<ID3D12Resource>> many;
  // 45 of the reserved heap's 64 units are free: two fit, three go to a second heap.
  for(int index=0;index<5;++index) many.push_back(pool.Create(1u<<20));
  Check(pool.statistics().heaps==2,"a full pool did not add a heap");
  many.clear();
  Check(pool.statistics().heaps==1 && pool.statistics().heaps_released==1,
        "an emptied extra heap was not released, or the reserved one was");
  a.Reset(); c.Reset(); reused.Reset();
  stats=pool.statistics();
  Check(stats.live==0 && stats.live_bytes==0,"ranges leaked after every buffer was destroyed");
  std::cout<<"buffer pool: placed="<<stats.placed<<" heaps_created="<<stats.heaps_created<<'\n';
}

// ---------------------------------------------------------------------------
// The whole path through the backend: a triangle whose vertex buffer is placed
// or committed, whose pipeline is prebuilt or built cold, reads back the same
// pixels either way.
struct TriangleResult {
  std::vector<uint8_t> pixels;
  NativeBackendStatistics stats;
};
TriangleResult DrawTriangle(NativeRenderBackend& backend,uint64_t ids,bool full_initial) {
  constexpr uint32_t kSize=32;
  NativeBackendTextureDesc target_desc{};
  target_desc.width=target_desc.height=kSize;
  target_desc.levels=1;
  target_desc.format=DXGI_FORMAT_R8G8B8A8_UNORM;
  target_desc.render_target=true;
  const auto target=backend.CreateRenderTarget(target_desc);
  const float vertices[]={-1.0f,-1.0f,0.0f,1.0f,-1.0f,0.0f,-1.0f,1.0f,0.0f};
  NativeBackendBufferDesc buffer_desc{};
  buffer_desc.bytes=sizeof(vertices)+(full_initial?0:16);
  buffer_desc.vertex=true;
  const auto vertex_buffer=backend.CreateBuffer(buffer_desc,{reinterpret_cast<const uint8_t*>(vertices),sizeof(vertices)});
  const PipelineFixture fixture;
  const NativeBackendInputElement layout[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,false,0}};
  NativeBackendPipelineDesc pipeline_desc{};
  pipeline_desc.vertex={static_cast<const uint8_t*>(fixture.vertex->GetBufferPointer()),fixture.vertex->GetBufferSize()};
  pipeline_desc.pixel={static_cast<const uint8_t*>(fixture.pixel->GetBufferPointer()),fixture.pixel->GetBufferSize()};
  pipeline_desc.vertex_id=ids; pipeline_desc.pixel_id=ids+1;
  pipeline_desc.input_layout=layout; pipeline_desc.input_layout_id=ids+2;
  pipeline_desc.state={0x10001,0,0,0,15,0};
  pipeline_desc.topology=NativeBackendTopology::TriangleList;
  pipeline_desc.render_targets=1;
  pipeline_desc.rtv_format[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
  auto& pipeline=backend.CreatePipeline(pipeline_desc);
  const std::array<float,4> tint{0.0f,1.0f,0.0f,1.0f};
  NativeBackendRenderTarget* colors[]={target.get()};
  backend.BeginFrame();
  auto& recorder=backend.Recorder();
  recorder.SetRenderTargets(colors,nullptr);
  recorder.SetViewport({0,0,float(kSize),float(kSize),0,1});
  recorder.ClearColor(*target,{1.0f,0.0f,0.0f,1.0f});
  recorder.SetPipeline(pipeline);
  recorder.SetVertexBuffer(0,*vertex_buffer,sizeof(float)*3,0);
  recorder.SetConstants(NativeBackendStage::Pixel,0,{reinterpret_cast<const uint8_t*>(tint.data()),sizeof(float)*4});
  recorder.Draw(3,0);
  backend.Submit();
  TriangleResult result;
  result.pixels=backend.ReadRenderTarget(*target);
  result.stats=backend.Statistics();
  return result;
}

void TestBackendIdentity() {
  const auto root=Scratch("backend");
  NativeD3D12Options baseline_options;
  baseline_options.prefer_warp=true;
  baseline_options.debug_layer=true;
  baseline_options.buffer_pool_heap_bytes=0;  // Every buffer committed, no manifest: the old path.
  TriangleResult baseline;
  {
    auto backend=CreateNativeD3D12Backend(baseline_options);
    baseline=DrawTriangle(*backend,0x10,true);
    Check(baseline.stats.buffers_committed==1 && baseline.stats.buffers_placed==0,"the baseline did not commit its buffer");
    for(const auto& message:backend->DrainValidationMessages()) Check(false,"baseline validation: "+message);
  }
  size_t green=0;
  for(size_t index=0;index+3<baseline.pixels.size();index+=4) if(baseline.pixels[index+1]>200) ++green;
  Check(green>32*32*4/10 && green<32*32*6/10,"the baseline triangle did not draw");

  auto options=baseline_options;
  options.buffer_pool_heap_bytes=4u<<20;
  options.buffer_pool_reserve_heaps=1;
  options.pipeline_manifest=root/"pipelines.bin";
  {
    auto backend=CreateNativeD3D12Backend(options);
    const auto cold=DrawTriangle(*backend,0x20,true);
    Check(cold.pixels==baseline.pixels,"a placed vertex buffer drew different pixels");
    Check(cold.stats.buffers_placed==1 && cold.stats.buffers_committed==0,"a fully initialised buffer was not placed");
    Check(cold.stats.pipeline_misses==1,"the cold run did not build its pipeline");
    const auto partial=DrawTriangle(*backend,0x20,false);
    Check(partial.stats.buffers_committed==1,"a partially initialised buffer was placed");
    Check(partial.pixels==baseline.pixels,"a partially initialised buffer drew different pixels");
    for(const auto& message:backend->DrainValidationMessages()) Check(false,"pooled validation: "+message);
  }  // Destruction writes the manifest.
  Check(std::filesystem::exists(options.pipeline_manifest),"the backend did not write its manifest on destruction");
  {
    auto backend=CreateNativeD3D12Backend(options);
    Check(backend->DescribeCaches().find("pipeline manifest loaded")!=std::string::npos,
          "the second backend did not load the manifest: "+backend->DescribeCaches());
    // Let the background build finish first, so the count below is about the
    // lookup and not about which thread won a race.
    for(int poll=0;poll<2000 && backend->Statistics().pipeline_prebuilt<1;++poll) Sleep(5);
    // The pipeline is prebuilt in the background; the draw either finds it
    // built or waits for that one build, and never builds it itself.
    const auto warm=DrawTriangle(*backend,0x30,true);
    Check(warm.pixels==baseline.pixels,"a prebuilt pipeline drew different pixels");
    Check(warm.stats.pipeline_misses==0 && warm.stats.pipeline_content_hits==1,
          "the second run built its pipeline on the draw's thread");
    std::cout<<"backend caches: "<<backend->DescribeCaches()<<'\n';
    for(const auto& message:backend->DrainValidationMessages()) Check(false,"warm validation: "+message);
  }
  // Placed buffers created and dropped across frames come back to the pool
  // once the GPU has finished with them.
  {
    // Two 100 KB buffers to a 256 KB heap: a pool that never got its ranges
    // back would need a heap for every two frames.
    auto compact=options;
    compact.pipeline_manifest.clear();
    compact.buffer_pool_heap_bytes=256u<<10;
    compact.buffer_pool_max_buffer_bytes=256u<<10;
    auto backend=CreateNativeD3D12Backend(compact);
    const std::vector<uint8_t> bytes(100000,0x5a);
    NativeBackendBufferDesc desc{}; desc.bytes=bytes.size(); desc.vertex=true;
    for(int frame=0;frame<8;++frame) {
      backend->BeginFrame();
      auto buffer=backend->CreateBuffer(desc,bytes);
      backend->Submit();
    }
    const auto stats=backend->Statistics();
    Check(stats.buffers_placed==8 && stats.buffer_heaps<=2,
          "retired placed buffers did not return their ranges: heaps="+std::to_string(stats.buffer_heaps));
    // Reused ranges are new resources over memory an earlier, destroyed one
    // used; the debug layer is what would object if that were not allowed.
    for(const auto& message:backend->DrainValidationMessages()) Check(false,"reuse validation: "+message);
  }
}
}  // namespace

int main() {
  std::cout<<std::unitbuf;
  try {
    TestShaderCache();
    TestShaderInFlight();
    TestShaderPrecompile();
    TestPipelineManifest();
    TestSamplerPrewarm();
    TestBufferPool();
    TestBackendIdentity();
  } catch(const std::exception& error) {
    std::cerr<<"FAIL: unexpected exception: "<<error.what()<<'\n';
    ++failures;
  }
  std::cout<<"native first-use cache tests: "<<failures<<" failures\n";
  return failures?1:0;
}

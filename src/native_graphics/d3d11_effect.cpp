#include "d3d11_effect.h"
#include "native_disk_cache.h"
#include "native_frame_times.h"
#include <d3dcompiler.h>
#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <stdexcept>
#include <unordered_map>

namespace edf::native {
namespace {
// The bytecode cache behind CompileNativeShader (see d3d11_effect.h).
struct ShaderCache {
  std::mutex mutex;
  std::unordered_map<std::string,std::shared_ptr<const std::vector<uint8_t>>> memory;
  std::optional<std::string> compiler_override;
  std::atomic<uint64_t> compiles{0},memory_hits{0},disk_hits{0},disk_stores{0},disk_rejects{0},unkeyed{0};
};
ShaderCache& Shaders() { static ShaderCache cache; return cache; }
constexpr char kShaderFileMagic[8]={'E','D','F','D','X','B','C','1'};
constexpr size_t kShaderFileLimit=16u<<20;

std::string CompilerIdentity() {
  auto& cache=Shaders();
  {
    std::lock_guard lock(cache.mutex);
    if(cache.compiler_override) return *cache.compiler_override;
  }
  static const std::string identity=NativeModuleIdentity(L"d3dcompiler_47.dll");
  return identity;
}
// The key is taken over the preprocessed text rather than the source plus a
// list of includes: preprocessing is what the compiler itself consumes, so a
// changed include, a changed define or the Common.fx adaptation all reach it
// without this code having to know which files were opened.
std::optional<NativeContentHash> ShaderKey(const std::string& source,const std::string& source_name,
    const D3D_SHADER_MACRO* defines,ID3DInclude* includes,const char* entry,const char* target,UINT flags) {
  Microsoft::WRL::ComPtr<ID3DBlob> preprocessed,errors;
  if(FAILED(D3DPreprocess(source.data(),source.size(),source_name.c_str(),defines,includes,&preprocessed,&errors)) ||
     !preprocessed)
    return std::nullopt;
  NativeSha256 hash;
  hash.AddField(std::string_view("edf-native-shader-v1"));
  hash.AddField(std::string_view(CompilerIdentity()));
  hash.AddField(std::string_view(target));
  hash.AddField(std::string_view(entry));
  hash.AddU32(flags);
  for(const auto* define=defines;define && define->Name;++define) {
    hash.AddField(std::string_view(define->Name));
    hash.AddField(std::string_view(define->Definition?define->Definition:""));
  }
  hash.AddField(std::span<const uint8_t>(static_cast<const uint8_t*>(preprocessed->GetBufferPointer()),
                                         preprocessed->GetBufferSize()));
  return hash.Finish();
}
std::filesystem::path ShaderFile(const NativeContentHash& key) {
  const auto directory=NativeCacheDirectory();
  if(directory.empty()) return {};
  return directory/"shaders"/(NativeHashHex(key)+".dxbc");
}
std::shared_ptr<const std::vector<uint8_t>> LoadShaderFile(const NativeContentHash& key) {
  const auto path=ShaderFile(key);
  if(path.empty()) return nullptr;
  const auto file=NativeReadCacheFile(path,kShaderFileLimit);
  if(!file) return nullptr;
  try {
    NativeCacheReader reader(*file);
    char magic[sizeof(kShaderFileMagic)];
    reader.Bytes(magic,sizeof(magic));
    if(std::memcmp(magic,kShaderFileMagic,sizeof(magic)) || reader.Hash()!=key)
      throw NativeCacheFormatError("foreign shader cache file");
    const auto digest=reader.Hash();
    auto payload=reader.Blob(kShaderFileLimit);
    if(reader.remaining() || payload.empty() || NativeSha256Of(payload)!=digest)
      throw NativeCacheFormatError("shader cache payload does not match its digest");
    return std::make_shared<const std::vector<uint8_t>>(std::move(payload));
  } catch(const NativeCacheFormatError&) {
    ++Shaders().disk_rejects;
    return nullptr;
  }
}
void StoreShaderFile(const NativeContentHash& key,std::span<const uint8_t> bytecode) {
  const auto path=ShaderFile(key);
  if(path.empty()) return;
  NativeCacheWriter writer;
  writer.Bytes(kShaderFileMagic,sizeof(kShaderFileMagic));
  writer.Hash(key);
  writer.Hash(NativeSha256Of(bytecode));
  writer.Blob(bytecode);
  if(NativeWriteCacheFile(path,writer.bytes())) ++Shaders().disk_stores;
}
Microsoft::WRL::ComPtr<ID3DBlob> BlobOf(std::span<const uint8_t> bytes) {
  Microsoft::WRL::ComPtr<ID3DBlob> blob;
  if(FAILED(D3DCreateBlob(bytes.size(),&blob))) throw std::runtime_error("shader blob allocation failed");
  std::memcpy(blob->GetBufferPointer(),bytes.data(),bytes.size());
  return blob;
}
// D3DCompile with the cache in front. The compile itself is called exactly as
// it was before the cache existed, so a miss produces the bytes it always did.
HRESULT CompileCached(const std::string& source,const std::string& source_name,const D3D_SHADER_MACRO* defines,
    ID3DInclude* includes,const char* entry,const char* target,UINT flags,
    Microsoft::WRL::ComPtr<ID3DBlob>& bytecode,Microsoft::WRL::ComPtr<ID3DBlob>& errors) {
  auto& cache=Shaders();
  const auto key=ShaderKey(source,source_name,defines,includes,entry,target,flags);
  if(key) {
    const auto hex=NativeHashHex(*key);
    std::shared_ptr<const std::vector<uint8_t>> found;
    {
      std::lock_guard lock(cache.mutex);
      if(const auto entry_found=cache.memory.find(hex);entry_found!=cache.memory.end()) found=entry_found->second;
    }
    if(found) {
      ++cache.memory_hits;
      FrameEventCounters().shader_cache_hits.fetch_add(1,std::memory_order_relaxed);
      bytecode=BlobOf(*found);
      return S_OK;
    }
    if((found=LoadShaderFile(*key))) {
      ++cache.disk_hits;
      FrameEventCounters().shader_cache_hits.fetch_add(1,std::memory_order_relaxed);
      {
        std::lock_guard lock(cache.mutex);
        cache.memory.emplace(hex,found);
      }
      bytecode=BlobOf(*found);
      return S_OK;
    }
  } else {
    ++cache.unkeyed;
  }
  // Attribution for edf_native_frame_times: a compile mid-gameplay is a hitch.
  FrameEventCounters().shader_compiles.fetch_add(1,std::memory_order_relaxed);
  ++cache.compiles;
  const HRESULT result=D3DCompile(source.data(),source.size(),source_name.c_str(),defines,includes,entry,target,
                                  flags,0,&bytecode,&errors);
  if(FAILED(result) || !bytecode || !key) return result;
  const std::span<const uint8_t> bytes(static_cast<const uint8_t*>(bytecode->GetBufferPointer()),
                                       bytecode->GetBufferSize());
  {
    std::lock_guard lock(cache.mutex);
    cache.memory.emplace(NativeHashHex(*key),std::make_shared<const std::vector<uint8_t>>(bytes.begin(),bytes.end()));
  }
  StoreShaderFile(*key,bytes);
  return result;
}
std::string AdaptNormalReconstruction(std::string source) {
  // BC3/DXT5 normal XY is quantized independently, so its squared length can
  // exceed one. Reconstruct the positive hemisphere at its boundary instead
  // of sending a negative radicand to SM5 sqrt. Preserve all valid inputs and
  // genuine NaNs; this is scoped to the authored normal decoder, not sqrt.
  static const std::regex helper(R"(float3\s+tex2D_DXT5N_xGxR\s*\([^)]*\)\s*\{[^{}]*\})");
  static const std::regex reconstruction(R"(N\s*\.\s*z\s*=\s*sqrt\s*\(\s*1\s*-\s*dot\s*\(\s*N\s*\.\s*xy\s*,\s*N\s*\.\s*xy\s*\)\s*\)\s*;)");
  std::smatch match;
  if(std::regex_search(source,match,helper)) {
    const auto body=match.str();
    // A preprocessed instance variant already contains the include adapter's
    // correction. Recognize that exact expression instead of adapting twice.
    static const std::regex corrected_reconstruction(R"(float\s+EdfNormalZSquared\s*=\s*1\s*-\s*dot\s*\(\s*N\s*\.\s*xy\s*,\s*N\s*\.\s*xy\s*\)\s*;\s*N\s*\.\s*z\s*=\s*sqrt\s*\(\s*EdfNormalZSquared\s*<\s*0\s*\?\s*0\s*:\s*EdfNormalZSquared\s*\)\s*;)");
    if(std::regex_search(body,corrected_reconstruction)) return source;
    if(!std::regex_search(body,reconstruction))
      throw std::runtime_error("unrecognized authored DXT5 normal reconstruction");
    const auto corrected=std::regex_replace(body,reconstruction,
      "float EdfNormalZSquared=1-dot(N.xy,N.xy); N.z=sqrt(EdfNormalZSquared<0?0:EdfNormalZSquared);");
    source.replace(size_t(match.position()),size_t(match.length()),corrected);
  }
  return source;
}
class Includes : public ID3DInclude {
 public:
  explicit Includes(std::filesystem::path root) : root_(std::move(root)) {}
  HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* data, UINT* size) noexcept override {
    try {
      // Retail source retains its build-tree spelling; the disc flattens this
      // particular include into Shader/. Other directory paths are rejected.
      const std::filesystem::path relative(std::strcmp(name, "../Common.fx") == 0 ? "Common.fx" : name);
      if (relative.empty() || relative.has_parent_path() || relative.is_absolute()) return E_FAIL;
      auto bytes = ReadSourceAsset(root_ / relative);
      if (bytes.empty()) return E_FAIL;
      if(relative=="Common.fx") {
        const auto source=AdaptNormalReconstruction(std::string(bytes.begin(),bytes.end()));
        bytes.assign(source.begin(),source.end());
      }
      const auto* key = bytes.data();
      const auto length = static_cast<UINT>(bytes.size());
      buffers_.emplace(key, std::move(bytes));
      *size = length;
      *data = key;
      return S_OK;
    } catch (...) { return E_FAIL; }
  }
  HRESULT __stdcall Close(LPCVOID data) noexcept override { buffers_.erase(data); return S_OK; }
 private:
  std::filesystem::path root_;
  std::map<const void*, std::vector<uint8_t>> buffers_;
};
}
void ValidateNativeShaderLink(const NativeShader& vertex, const NativeShader& pixel) {
  if (vertex.entry.pixel || !pixel.entry.pixel || !vertex.reflection || !pixel.reflection)
    throw std::runtime_error("invalid native shader stage pair");
  D3D11_SHADER_DESC vs{}, ps{};
  if (FAILED(vertex.reflection->GetDesc(&vs)) || FAILED(pixel.reflection->GetDesc(&ps)))
    throw std::runtime_error("cannot reflect native shader linkage");
  for (UINT i = 0; i < ps.InputParameters; ++i) {
    D3D11_SIGNATURE_PARAMETER_DESC input{};
    if (FAILED(pixel.reflection->GetInputParameterDesc(i,&input)))
      throw std::runtime_error("cannot reflect pixel input");
    if (!input.ReadWriteMask) continue;
    // These values are generated by rasterization, not vertex shader outputs.
    if (input.SystemValueType == D3D_NAME_IS_FRONT_FACE || input.SystemValueType == D3D_NAME_PRIMITIVE_ID ||
        input.SystemValueType == D3D_NAME_SAMPLE_INDEX || input.SystemValueType == D3D_NAME_COVERAGE) continue;
    bool matched = false;
    for (UINT j = 0; j < vs.OutputParameters; ++j) {
      D3D11_SIGNATURE_PARAMETER_DESC output{};
      if (FAILED(vertex.reflection->GetOutputParameterDesc(j,&output)))
        throw std::runtime_error("cannot reflect vertex output");
      if (_stricmp(input.SemanticName,output.SemanticName) || input.SemanticIndex != output.SemanticIndex) continue;
      matched = input.Register == output.Register && input.ComponentType == output.ComponentType &&
                (input.ReadWriteMask & output.Mask) == input.ReadWriteMask;
      break;
    }
    if (!matched) throw std::runtime_error("native shader varying mismatch: " + vertex.entry.name + " -> " +
                                           pixel.entry.name + " at " + input.SemanticName + std::to_string(input.SemanticIndex));
  }
}
NativeShader CompileNativeShader(ID3D11Device& device,const Effect& effect,const ShaderEntry& entry,
    const std::filesystem::path& source_path,bool reverse_depth) {
  return CompileNativeShader(&device,effect,entry,source_path,reverse_depth);
}
NativeShader CompileNativeShader(ID3D11Device* device, const Effect& effect,
                                 const ShaderEntry& entry,
                                 const std::filesystem::path& source_path,bool reverse_depth) {
  NativeShader result;
  result.entry = entry;
  result.source_fingerprint=EffectSourceFingerprint(effect.source);
  result.source_bytes=effect.source.size();
  Includes includes(source_path.parent_path());
  // Common.fx guards its actual shadow sampling with this engine define.
  // Omitting it compiles successfully but silently removes shadow lighting.
  const D3D_SHADER_MACRO defines[]={{"__DX__","1"},{nullptr,nullptr}};
  Microsoft::WRL::ComPtr<ID3DBlob> errors;
  // Guest declarations may omit a tangent, supplying (0,0,0,1). Legacy
  // normalization's final multiply yields zero for 0 * infinity; SM5 yields
  // NaN. Preserve that defined zero-vector case without hiding NaN inputs or
  // changing the intrinsic for nonzero vectors. Define the macro only after
  // the wrappers so their intrinsic calls are not recursively substituted.
  std::string source=R"(
float EdfNativeNormalize(float v) { if(v*v==0) return 0; return normalize(v); }
float2 EdfNativeNormalize(float2 v) { if(dot(v,v)==0) return 0; return normalize(v); }
float3 EdfNativeNormalize(float3 v) { if(dot(v,v)==0) return 0; return normalize(v); }
float4 EdfNativeNormalize(float4 v) { if(dot(v,v)==0) return 0; return normalize(v); }
#define normalize EdfNativeNormalize
)"+AdaptNormalReconstruction(effect.source),compile_entry=entry.name;
  if (reverse_depth) {
    if (entry.pixel) throw std::runtime_error("depth reversal requires a vertex shader");
    // Preprocess first so comments, includes and macros cannot masquerade as
    // entry declarations. Support the game's typed, struct-returning entries;
    // reject other forms rather than rewriting arbitrary return statements.
    Microsoft::WRL::ComPtr<ID3DBlob> preprocessed;
    if (FAILED(D3DPreprocess(source.data(),source.size(),source_path.string().c_str(),defines,&includes,&preprocessed,&errors)))
      throw std::runtime_error("cannot preprocess reversed-depth vertex shader");
    source.assign(static_cast<const char*>(preprocessed->GetBufferPointer()),preprocessed->GetBufferSize());
    while (!source.empty() && source.back()=='\0') source.pop_back();
    if (!std::regex_match(entry.name,std::regex("[A-Za-z_][A-Za-z_0-9]*")))
      throw std::runtime_error("invalid reversed-depth entry identifier");
    std::smatch function;
    const std::regex signature("\\b([A-Za-z_][A-Za-z_0-9]*)\\s+"+entry.name+"\\s*\\(([^()]*)\\)\\s*\\{");
    if (!std::regex_search(source,function,signature)) throw std::runtime_error("unsupported reversed-depth entry signature: "+entry.name);
    const auto result_type=function[1].str(),parameters=function[2].str();
    std::smatch structure,position;
    if (!std::regex_search(source,structure,std::regex("\\bstruct\\s+"+result_type+"\\s*\\{([^{}]*)\\}")))
      throw std::runtime_error("reversed-depth entry must return a structure");
    const auto fields=structure[1].str();
    if (!std::regex_search(fields,position,std::regex("\\bfloat4\\s+([A-Za-z_][A-Za-z_0-9]*)\\s*:\\s*(?:SV_POSITION|POSITION)0?\\s*;",std::regex::icase)))
      throw std::runtime_error("reversed-depth entry needs a float4 position");
    std::string arguments;
    size_t begin=0;
    while (begin<parameters.size()) {
      auto end=parameters.find(',',begin); if (end==std::string::npos) end=parameters.size();
      const auto parameter=parameters.substr(begin,end-begin);
      std::smatch argument;
      if (!std::regex_match(parameter,argument,std::regex("\\s*(?:(?:in|uniform)\\s+)?[A-Za-z_][A-Za-z_0-9]*\\s+([A-Za-z_][A-Za-z_0-9]*)(?:\\s*:\\s*[A-Za-z_][A-Za-z_0-9]*)?(?:\\s*=\\s*[0-9]+)?\\s*")))
        throw std::runtime_error("unsupported reversed-depth entry parameter: "+parameter);
      if (!arguments.empty()) arguments+=",";
      arguments+=argument[1].str(); begin=end+1;
    }
    compile_entry="edf_native_reversed_depth_entry";
    const auto member="edf_native_result."+position[1].str();
    source+="\n"+result_type+" "+compile_entry+"("+parameters+") { "+result_type+
      " edf_native_result="+entry.name+"("+arguments+"); "+member+".z="+member+".w-"+member+
      ".z; return edf_native_result; }\n";
  }
  const char* target = entry.pixel ? "ps_5_0" : "vs_5_0";
  // Counted in shader_compiles only when the compiler really runs; a cache
  // hit is counted in shader_cache_hits instead.
  HRESULT hr = CompileCached(source, source_path.string(), defines, &includes, compile_entry.c_str(), target,
                             D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY | D3DCOMPILE_OPTIMIZATION_LEVEL3,
                             result.bytecode, errors);
  if (FAILED(hr)) {
    std::string message = "native compilation failed: " + source_path.filename().string() + ':' + entry.name;
    if (errors && errors->GetBufferSize()) {
      message += '\n';
      message.append(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize() - 1);
    }
    throw std::runtime_error(message);
  }
  hr = D3DReflect(result.bytecode->GetBufferPointer(), result.bytecode->GetBufferSize(),
                  __uuidof(ID3D11ShaderReflection), reinterpret_cast<void**>(result.reflection.GetAddressOf()));
  if (FAILED(hr)) throw std::runtime_error("native shader reflection failed: " + entry.name);
  if (!device) return result;
  if (entry.pixel)
    hr = device->CreatePixelShader(result.bytecode->GetBufferPointer(), result.bytecode->GetBufferSize(), nullptr, &result.pixel);
  else
    hr = device->CreateVertexShader(result.bytecode->GetBufferPointer(), result.bytecode->GetBufferSize(), nullptr, &result.vertex);
  if (FAILED(hr)) throw std::runtime_error("native shader creation failed: " + entry.name);
  return result;
}
NativeShaderCacheStatistics GetNativeShaderCacheStatistics() {
  auto& cache=Shaders();
  NativeShaderCacheStatistics out;
  out.compiles=cache.compiles; out.memory_hits=cache.memory_hits; out.disk_hits=cache.disk_hits;
  out.disk_stores=cache.disk_stores; out.disk_rejects=cache.disk_rejects; out.unkeyed=cache.unkeyed;
  return out;
}
void ClearNativeShaderMemoryCache() {
  auto& cache=Shaders();
  std::lock_guard lock(cache.mutex);
  cache.memory.clear();
}
void SetNativeShaderCompilerIdentityForTesting(std::string identity) {
  auto& cache=Shaders();
  std::lock_guard lock(cache.mutex);
  cache.compiler_override=std::move(identity);
}
bool AddNativeWorldInstancing(NativeShader& shader,const Effect& effect,
    const std::filesystem::path& source_path,bool reverse_depth,std::string* reason) {
  const auto reject=[&](const char* message) { if(reason) *reason=message; return false; };
  if(shader.entry.pixel || shader.instanced_bytecode) return reject("pixel shader or already compiled");
  try {
    auto* world=shader.reflection->GetVariableByName("g_mWorld");
    D3D11_SHADER_VARIABLE_DESC wd{}; D3D11_SHADER_TYPE_DESC wt{};
    D3D11_SHADER_BUFFER_DESC wb{}; D3D11_SHADER_INPUT_BIND_DESC binding{};
    if(FAILED(world->GetDesc(&wd)) || !(wd.uFlags&D3D_SVF_USED) || wd.Size!=64 ||
       FAILED(world->GetType()->GetDesc(&wt)) || wt.Type!=D3D_SVT_FLOAT ||
       wt.Rows!=4 || wt.Columns!=4 || wt.Elements ||
       (wt.Class!=D3D_SVC_MATRIX_ROWS && wt.Class!=D3D_SVC_MATRIX_COLUMNS) ||
       FAILED(world->GetBuffer()->GetDesc(&wb)) ||
       FAILED(shader.reflection->GetResourceBindingDescByName(wb.Name,&binding)) || binding.BindPoint>=14) return reject("world is absent, unused, or not a float4x4");
    Includes includes(source_path.parent_path());
    const D3D_SHADER_MACRO defines[]={{"__DX__","1"},{nullptr,nullptr}};
    Microsoft::WRL::ComPtr<ID3DBlob> preprocessed,errors;
    if(FAILED(D3DPreprocess(effect.source.data(),effect.source.size(),source_path.string().c_str(),
      defines,&includes,&preprocessed,&errors))) return reject("preprocessing failed");
    std::string source(static_cast<const char*>(preprocessed->GetBufferPointer()),preprocessed->GetBufferSize());
    while(!source.empty() && source.back()=='\0') source.pop_back();
    if(source.find("edf_instance_")!=std::string::npos ||
       std::regex_search(source,std::regex("SV_InstanceID|SV_VertexID|EDFINSTANCE",std::regex::icase))) return reject("reserved/system-value input");
    // Restrict rewriting to a plain, standalone world matrix declaration.
    // Keep that declaration so the original uniform buffer's offsets survive.
    const std::regex declaration(R"(\b(?:(?:shared|uniform|row_major|column_major)\s+)*float4x4\s+g_mWorld\s*(?::\s*(?:register\s*\(\s*c[0-9]+\s*\)|[A-Za-z_][A-Za-z_0-9]*))?\s*(?:<[^<>]*>\s*)?;)");
    std::smatch declared;
    if(!std::regex_search(source,declared,declaration)) return reject("unsupported world declaration");
    const auto declaration_text=declared.str();
    source.replace(size_t(declared.position()),size_t(declared.length()),"edf_instance_declaration_marker;");
    source=std::regex_replace(source,std::regex(R"(\bg_mWorld\b)"),"edf_instance_world");
    source.replace(source.find("edf_instance_declaration_marker;"),
      std::strlen("edf_instance_declaration_marker;"),declaration_text);
    std::smatch signature;
    if(!std::regex_search(source,signature,std::regex("\\b([A-Za-z_][A-Za-z_0-9]*)\\s+"+
        shader.entry.name+"\\s*\\(([^()]*)\\)\\s*\\{"))) return reject("unsupported entry signature");
    const auto result_type=signature[1].str(),parameters=signature[2].str();
    std::string arguments;
    for(size_t begin=0;begin<parameters.size();) {
      auto end=parameters.find(',',begin); if(end==std::string::npos) end=parameters.size();
      std::smatch argument; const auto parameter=parameters.substr(begin,end-begin);
      if(!std::regex_match(parameter,argument,std::regex(
        "\\s*(?:in\\s+)?[A-Za-z_][A-Za-z_0-9]*\\s+([A-Za-z_][A-Za-z_0-9]*)(?:\\s*:\\s*[A-Za-z_][A-Za-z_0-9]*)?\\s*"))) return reject("unsupported entry parameter");
      if(!arguments.empty()) arguments+=",";
      arguments+=argument[1].str(); begin=end+1;
    }
    std::string rows;
    for(unsigned i=0;i<4;++i) {
      if(i) rows+=",";
      rows+="edf_instance_row"+std::to_string(i);
    }
    std::string matrix="float4x4("+rows+")";
    if(wt.Class==D3D_SVC_MATRIX_COLUMNS) matrix="transpose("+matrix+")";
    source="static float4x4 edf_instance_world;\n"+source+"\n"+result_type+
      " edf_instance_entry("+parameters;
    for(unsigned i=0;i<4;++i) source+=(i || !parameters.empty()?",":"")+
      std::string("float4 edf_instance_row")+std::to_string(i)+": EDFINSTANCE"+std::to_string(i);
    source+=") { edf_instance_world="+matrix+"; return "+shader.entry.name+"("+arguments+"); }\n";
    Effect variant; variant.source=std::move(source);
    auto compiled=CompileNativeShader(nullptr,variant,{false,"edf_instance_entry","vs_3_0"},source_path,reverse_depth);
    D3D11_SHADER_DESC original_desc{},variant_desc{};
    shader.reflection->GetDesc(&original_desc); compiled.reflection->GetDesc(&variant_desc);
    if(original_desc.OutputParameters!=variant_desc.OutputParameters) return reject("output count changed");
    for(UINT i=0;i<original_desc.OutputParameters;++i) {
      D3D11_SIGNATURE_PARAMETER_DESC a{},b{};
      shader.reflection->GetOutputParameterDesc(i,&a); compiled.reflection->GetOutputParameterDesc(i,&b);
      if(std::strcmp(a.SemanticName,b.SemanticName) || a.SemanticIndex!=b.SemanticIndex ||
         a.Register!=b.Register || a.Mask!=b.Mask || a.ComponentType!=b.ComponentType ||
         a.SystemValueType!=b.SystemValueType) return reject("output signature changed");
    }
    // Every consumed uniform in the alternate shader must still refer to the
    // exact bytes uploaded for the original. Never infer layout from source.
    for(UINT i=0;i<variant_desc.BoundResources;++i) {
      D3D11_SHADER_INPUT_BIND_DESC resource{},old_resource{};
      compiled.reflection->GetResourceBindingDesc(i,&resource);
      if(resource.Type!=D3D_SIT_CBUFFER ||
         FAILED(shader.reflection->GetResourceBindingDescByName(resource.Name,&old_resource)) ||
         resource.BindPoint!=old_resource.BindPoint) return reject("constant resource layout changed");
      auto* cb=compiled.reflection->GetConstantBufferByName(resource.Name);
      D3D11_SHADER_BUFFER_DESC cbd{}; cb->GetDesc(&cbd);
      for(UINT v=0;v<cbd.Variables;++v) {
        D3D11_SHADER_VARIABLE_DESC variable{},old_variable{};
        cb->GetVariableByIndex(v)->GetDesc(&variable);
        if(!(variable.uFlags&D3D_SVF_USED)) continue;
        if(FAILED(shader.reflection->GetVariableByName(variable.Name)->GetDesc(&old_variable)) ||
           variable.StartOffset!=old_variable.StartOffset || variable.Size!=old_variable.Size) return reject("constant variable layout changed");
      }
    }
    shader.instanced_bytecode=compiled.bytecode;
    shader.instance_world_slot=binding.BindPoint;
    shader.instance_world_offset=wd.StartOffset;
    return true;
  } catch(const std::exception& error) { return reject(error.what()); }
}
}  // namespace edf::native

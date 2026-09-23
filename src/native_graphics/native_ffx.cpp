#include "native_ffx.h"
#include "ffx_api/ffx_framegeneration.h"
#include "ffx_api/ffx_upscale.h"
#include <stdexcept>

namespace edf::native {
std::filesystem::path NativeFfxLibrary::DefaultPath() {
  std::wstring buffer(MAX_PATH,L'\0');
  for(;;) {
    const DWORD length=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));
    if(!length) return L"amd_fidelityfx_dx12.dll";
    if(length<buffer.size()) { buffer.resize(length); break; }
    buffer.resize(buffer.size()*2);
  }
  return std::filesystem::path(buffer).parent_path()/L"amd_fidelityfx_dx12.dll";
}

NativeFfxLibrary::NativeFfxLibrary(const std::filesystem::path& dll) : path_(dll) {
  std::error_code missing;
  if(!std::filesystem::is_regular_file(path_,missing)) {
    error_="not found: "+path_.string();
    return;
  }
  // No error box for a DLL whose own dependencies are missing: the caller
  // reports it and carries on without FSR.
  const UINT previous=SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX);
  HMODULE module=LoadLibraryExW(path_.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
  SetErrorMode(previous);
  if(!module) {
    error_="LoadLibrary failed ("+std::to_string(GetLastError())+"): "+path_.string();
    return;
  }
  ffxLoadFunctions(&functions_,module);
  if(!functions_.CreateContext || !functions_.DestroyContext || !functions_.Configure ||
     !functions_.Query || !functions_.Dispatch) {
    FreeLibrary(module);
    functions_={};
    error_="missing ffx entry points: "+path_.string();
    return;
  }
  module_=module;
}

NativeFfxLibrary::~NativeFfxLibrary() {
  if(module_) FreeLibrary(static_cast<HMODULE>(module_));
}

const ffxFunctions& NativeFfxLibrary::functions() const {
  if(!module_) throw std::runtime_error("FidelityFX is unavailable: "+error_);
  return functions_;
}

std::vector<NativeFfxProviderVersion> NativeFfxLibrary::Versions(uint64_t create_desc_type,
                                                                 ID3D12Device* device) const {
  std::vector<NativeFfxProviderVersion> out;
  if(!module_) return out;
  // Count first, then fetch: the query fills at most the capacity it is given.
  uint64_t count=0;
  ffxQueryDescGetVersions query{};
  query.header.type=FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
  query.createDescType=create_desc_type;
  query.device=device;
  query.outputCount=&count;
  if(functions_.Query(nullptr,&query.header)!=FFX_API_RETURN_OK || !count) return out;
  std::vector<uint64_t> ids(count);
  std::vector<const char*> names(count,nullptr);
  query.versionIds=ids.data();
  query.versionNames=names.data();
  if(functions_.Query(nullptr,&query.header)!=FFX_API_RETURN_OK) return out;
  for(uint64_t index=0;index<count && index<ids.size();++index)
    out.push_back({ids[index],names[index]?names[index]:""});
  return out;
}

bool NativeFfxLibrary::upscaler_available(ID3D12Device* device) const {
  return !Versions(FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE,device).empty();
}
bool NativeFfxLibrary::frame_generation_available(ID3D12Device* device) const {
  return !Versions(FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION,device).empty();
}

std::string NativeFfxLibrary::Describe(ID3D12Device* device) const {
  if(!module_) return "FidelityFX unavailable: "+error_;
  const auto list=[](const std::vector<NativeFfxProviderVersion>& versions) {
    if(versions.empty()) return std::string("none");
    std::string out;
    for(const auto& version:versions) out+=(out.empty()?"":", ")+version.name;
    return out;
  };
  return "FidelityFX "+path_.string()+": upscale="+list(Versions(FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE,device))+
         " framegeneration="+list(Versions(FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION,device));
}
}  // namespace edf::native

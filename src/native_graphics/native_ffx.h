#pragma once
// The FidelityFX API DLL, loaded at run time. The headers are vendored in
// third_party/ffx_api (FidelityFX SDK 1.1.4 ffx-api, rexglue/FidelityFX-SDK
// @eee08db, MIT); nothing links against amd_fidelityfx_dx12.lib, so a missing
// or broken DLL is a state this reports, never a failure to start.
//
// Which DLL: CMake stages AMD's signed prebuilt next to edf2027.exe
// (EDF2027_FFX_SIGNED_DLL), the one with the frame-generation provider; the
// SDK build's own is upscaler-only. Both export the same five entry points.
#include "ffx_api/ffx_api.h"
#include "ffx_api/ffx_api_loader.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct ID3D12Device;

namespace edf::native {
struct NativeFfxProviderVersion {
  uint64_t id=0;
  std::string name;
};

// Owns one reference on the module (FreeLibrary on destruction). The game
// process usually has the DLL loaded already, because rexruntime.dll imports
// it; loading the same path again only adds a reference.
class NativeFfxLibrary {
 public:
  // amd_fidelityfx_dx12.dll in the directory of the running executable - by
  // full path, so the loader search order cannot substitute another copy.
  static std::filesystem::path DefaultPath();
  explicit NativeFfxLibrary(const std::filesystem::path& dll=DefaultPath());
  ~NativeFfxLibrary();
  NativeFfxLibrary(const NativeFfxLibrary&)=delete;
  NativeFfxLibrary& operator=(const NativeFfxLibrary&)=delete;

  // Loaded, and all five entry points resolved. False with error() saying why.
  bool available() const { return module_!=nullptr; }
  const std::string& error() const { return error_; }
  const std::filesystem::path& path() const { return path_; }
  // The entry points. Throws when !available().
  const ffxFunctions& functions() const;

  // The providers this DLL offers for a context type, newest first as the DLL
  // orders them (FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE = 0x00010000,
  // FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION = 0x00020001). `device`
  // may be null, which skips only driver-supplied providers. Empty when the
  // DLL is unavailable or offers none; never throws.
  std::vector<NativeFfxProviderVersion> Versions(uint64_t create_desc_type,ID3D12Device* device=nullptr) const;
  bool upscaler_available(ID3D12Device* device=nullptr) const;
  bool frame_generation_available(ID3D12Device* device=nullptr) const;
  // One line for the log: path, and the upscaler and frame-generation
  // versions, or why the DLL is unavailable.
  std::string Describe(ID3D12Device* device=nullptr) const;

 private:
  std::filesystem::path path_;
  void* module_=nullptr;  // HMODULE
  ffxFunctions functions_{};
  std::string error_;
};
}  // namespace edf::native

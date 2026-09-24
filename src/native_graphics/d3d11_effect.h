#pragma once
#include "effect.h"
#include <d3d11.h>
#include <d3d11shader.h>
#include <wrl/client.h>

namespace edf::native {
struct NativeShader {
  ShaderEntry entry;
  uint64_t source_fingerprint=0;
  size_t source_bytes=0;
  Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
  Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel;
  // Optional world-only instance stream variant; all other bindings retain
  // their original reflected slots and offsets.
  Microsoft::WRL::ComPtr<ID3DBlob> instanced_bytecode;
  UINT instance_world_slot=0,instance_world_offset=0;
};
// Retain reflection for named parameters and native constant-buffer layouts,
// and vertex bytecode for input layouts. Xbox register numbers are not used.
// reverse_depth wraps supported struct-returning HLSL vertex entries with
// z=w-z, since D3D11 rejects reversed MinDepth/MaxDepth viewport ranges.
// Callers must use a sorted native range and matching variant bindings.
NativeShader CompileNativeShader(ID3D11Device& device, const Effect& effect,
                                 const ShaderEntry& entry,
                                 const std::filesystem::path& source_path,
                                 bool reverse_depth = false);
// Null device compiles and reflects bytecode without allocating D3D11 objects.
// The backend pipeline owns the executable shader on the recorded path.
NativeShader CompileNativeShader(ID3D11Device* device, const Effect& effect,
                                 const ShaderEntry& entry,
                                 const std::filesystem::path& source_path,
                                 bool reverse_depth = false);
// Separately compiled legacy stages must agree on native varying registers,
// not only semantic names. Throws before a mismatched pair is submitted.
void ValidateNativeShaderLink(const NativeShader& vertex, const NativeShader& pixel);
// Compiled bytecode is cached, so a shader the game registers again (a menu
// reloading its effect) or registered in an earlier run costs a preprocess and
// a hash instead of a D3DCompile. The key is a SHA-256 over the fully
// preprocessed source (every include expanded, the native adaptations already
// applied), the entry point, target, flags, defines and the loaded
// d3dcompiler's version: anything that can change the bytecode changes the key.
// A hit returns the bytes that compile produced, so it is the same shader.
// In-process always; on disk under NativeCacheDirectory()/shaders when a cache
// directory is set (native_disk_cache.h), one <key>.dxbc file per shader
// carrying its key and a SHA-256 of its payload, so a torn or foreign file is
// rejected and recompiled rather than used.
struct NativeShaderCacheStatistics {
  uint64_t compiles=0;       // D3DCompile calls actually made.
  uint64_t memory_hits=0;    // Served from this process's cache.
  uint64_t disk_hits=0;      // Served from a file an earlier compile wrote.
  uint64_t disk_stores=0;    // Files written.
  uint64_t disk_rejects=0;   // Files present but unusable (key or payload mismatch).
  uint64_t unkeyed=0;        // Compiles whose source could not be preprocessed for a key.
  uint64_t waits=0;          // Served by waiting for another thread's read or compile of the same key.
  double compile_ms=0;       // Wall time inside D3DCompile, summed over threads.
};
NativeShaderCacheStatistics GetNativeShaderCacheStatistics();
// The loaded d3dcompiler's identity as it enters every cache key.
std::string NativeShaderCompilerIdentity();
// Tests: forget the in-process entries so the next lookup goes to disk, and
// stand in for a different compiler version.
void ClearNativeShaderMemoryCache();
void SetNativeShaderCompilerIdentityForTesting(std::string identity);
// Unsupported source/signature/layout leaves the ordinary shader untouched.
bool AddNativeWorldInstancing(NativeShader& shader,const Effect& effect,
                             const std::filesystem::path& source_path,bool reverse_depth=false,std::string* reason=nullptr);
}  // namespace edf::native

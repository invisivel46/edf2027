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
// Unsupported source/signature/layout leaves the ordinary shader untouched.
bool AddNativeWorldInstancing(NativeShader& shader,const Effect& effect,
                             const std::filesystem::path& source_path,bool reverse_depth=false,std::string* reason=nullptr);
}  // namespace edf::native

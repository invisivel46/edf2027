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
// Separately compiled legacy stages must agree on native varying registers,
// not only semantic names. Throws before a mismatched pair is submitted.
void ValidateNativeShaderLink(const NativeShader& vertex, const NativeShader& pixel);
}  // namespace edf::native

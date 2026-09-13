#pragma once
#include "d3d11_effect.h"
#include <map>
#include <array>
#include <optional>
#include <span>
#include <memory>

namespace edf::native {
// Owns a shader and its named bindings. Mutation and Bind run on the render
// thread. The guest adapter converts endianness and serializes matrices/arrays
// to the reflected layout before passing bytes here.
class ShaderBindings {
  // `used` is the reflection's D3D_SVF_USED bit. A constant buffer reflects
  // every constant the source declares, including ones the compiler proved the
  // shader never reads, so "has a binding" is not "consumes". Coverage work that
  // conflates the two reports gaps that cannot affect a pixel.
  struct Variable { size_t buffer; UINT offset, size; D3D11_SHADER_TYPE_DESC type; bool used; };
 public:
  class FloatRegisterBinding {
   public:
    size_t bytes() const { return slots_*16; }
   private:
    friend class ShaderBindings;
    std::shared_ptr<const uint8_t> owner_;
    Variable variable_{};
    std::string name_;
    size_t slots_=0,components_=0;
  };
  FloatRegisterBinding ResolveFloatRegisters(const std::string& name) const;
  class ResourceBinding {
   private:
    friend class ShaderBindings;
    std::shared_ptr<const uint8_t> owner_;
    std::optional<UINT> texture_,sampler_;
  };
  ResourceBinding ResolveResource(const std::string& name) const;
  bool TrySetTexture(const ResourceBinding& binding,ID3D11ShaderResourceView* texture);
  bool TrySetSampler(const ResourceBinding& binding,ID3D11SamplerState* sampler);
  bool Owns(const FloatRegisterBinding& binding) const { return binding.owner_==generation_; }
  bool SetGuestFloatRegisters(const FloatRegisterBinding& binding,std::span<const uint8_t> registers);
  ShaderBindings(ID3D11Device& device, NativeShader shader);
  ShaderBindings(const ShaderBindings&)=delete;
  ShaderBindings& operator=(const ShaderBindings&)=delete;
  void SetConstant(const std::string& name, std::span<const uint8_t> packed_bytes);
  // Convert the engine's big-endian float4 register stream to the reflected
  // native layout. Returns false if the native compiler optimized the name out.
  bool SetGuestFloatRegisters(const std::string& name, std::span<const uint8_t> registers);
  // Update a subset of float4 slots, preserving all other slots and padding.
  bool PatchGuestFloatRegisters(const std::string& name, size_t first_slot,
                               std::span<const uint8_t> registers);
  bool PatchGuestFloatRegisters(const FloatRegisterBinding& binding,size_t first_slot,
                               std::span<const uint8_t> registers);
  size_t GuestFloatRegisterBytes(const std::string& name) const;
  void SetTexture(const std::string& name, ID3D11ShaderResourceView* texture);
  bool TrySetTexture(const std::string& name, ID3D11ShaderResourceView* texture);
  void ClearTextures();
  // Diagnostic snapshot of the current named view; does not change binding state.
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ReadTexture(const std::string& name) const;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> ReadSampler(const std::string& name) const;
  void ClearSamplers();
  void SetSampler(const std::string& name, ID3D11SamplerState* sampler);
  bool TrySetSampler(const std::string& name, ID3D11SamplerState* sampler);
  void Bind(ID3D11DeviceContext& context);
  // Upload and set only the constants, for a draw that follows one which left
  // this shader, its textures and its samplers already bound. In a run of
  // same-material draws that is everything that actually changed - the rest is
  // the same calls with the same arguments, once per draw.
  //
  // The caller owns the claim that nothing has bound since; this cannot check
  // it. Getting that wrong draws with the previous material's textures.
  void BindConstants(ID3D11DeviceContext& context);
  // The packed CPU image of each constant buffer - the exact bytes Bind would
  // upload - without touching the GPU buffer or the dirty flag.
  //
  // This is what lets a draw carry its own constants. Today every draw on a
  // shader points at the one GPU buffer this object owns, which is overwritten
  // per activation, so draws cannot be built independently or recorded from
  // more than one thread. A caller that copies these bytes per draw instead
  // has no such dependency, and it is the shape both target APIs want anyway.
  struct ConstantImage { UINT slot; std::span<const uint8_t> bytes; };
  std::vector<ConstantImage> ConstantImages() const;
  bool HasAllTextureInputs() const;
  bool UsesTextureResource(ID3D11Resource& resource) const;
  std::vector<float> ReadFloatVector(const std::string& name) const;
  // Flattened float array: `Elements * Columns` values, skipping each element's
  // constant-buffer padding. Missing/optimized-out names return an empty span;
  // a non-array or non-float name is rejected rather than reinterpreted.
  std::vector<float> ReadFloatArray(const std::string& name) const;
  // Whether the compiled shader actually reads this constant, as opposed to
  // merely declaring it. False for an absent name.
  bool ConsumesConstant(const std::string& name) const;
  // Logical row-major values, independent of the reflected storage layout.
  // Missing/optimized-out names return nullopt; other types are rejected.
  std::optional<std::array<float,16>> ReadFloat4x4(const std::string& name) const;
  const NativeShader& shader() const { return shader_; }
 private:
  struct Buffer {
    UINT slot;
    std::vector<uint8_t> bytes;
    Microsoft::WRL::ComPtr<ID3D11Buffer> gpu;
    bool dirty = true;
  };
  const std::shared_ptr<const uint8_t> generation_=std::make_shared<const uint8_t>(0);
  NativeShader shader_;
  std::vector<Buffer> buffers_;
  std::map<std::string, Variable> variables_;
  std::map<std::string, UINT> textures_, samplers_;
  std::map<UINT, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> texture_values_;
  std::map<UINT, Microsoft::WRL::ComPtr<ID3D11SamplerState>> sampler_values_;
  // Immutable reflected runs, with live pointers backed by the owning maps.
  // These cache binding data, never D3D context state: Bind always re-emits it.
  std::vector<std::pair<UINT,UINT>> texture_runs_,sampler_runs_;
  std::array<ID3D11ShaderResourceView*,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> texture_slots_{};
  std::array<ID3D11SamplerState*,D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> sampler_slots_{};
};
}  // namespace edf::native

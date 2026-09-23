#pragma once
#include "d3d11_effect.h"
#include "native_render_backend.h"
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
  // Where SetGuestFloatRegisters(binding) writes: its buffer's slot and the
  // variable's byte range in that buffer's image. Nothing outside the range
  // moves (a short last element's padding stops at the variable's size).
  // Empty (size 0) for a name the compiler optimized out.
  struct FloatRegisterRange { UINT slot=0,offset=0,size=0; };
  FloatRegisterRange Range(const FloatRegisterBinding& binding) const;
  class ResourceBinding {
   public:
    bool used() const { return texture_.has_value() || sampler_.has_value(); }
   private:
    friend class ShaderBindings;
    std::shared_ptr<const uint8_t> owner_;
    std::optional<UINT> texture_,sampler_;
  };
  ResourceBinding ResolveResource(const std::string& name) const;
  // Textures and samplers are backend handles, not D3D11 views. A texture is
  // held by shared_ptr because the registry it came from can drop it while a
  // material still has it bound - which the ComPtr this replaces also handled,
  // and a raw pointer would not. Samplers are owned by the backend for its
  // whole life, so those stay raw.
  bool TrySetTexture(const ResourceBinding& binding,const std::shared_ptr<NativeBackendTexture>& texture);
  bool TrySetSampler(const ResourceBinding& binding,NativeBackendSampler* sampler);
  bool Owns(const FloatRegisterBinding& binding) const { return binding.owner_==generation_; }
  bool SetGuestFloatRegisters(const FloatRegisterBinding& binding,std::span<const uint8_t> registers);
  ShaderBindings(ID3D11Device& device, NativeShader shader);
  // Null device retains CPU binding images for a backend recorder only.
  ShaderBindings(ID3D11Device* device, NativeShader shader);
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
  void SetTexture(const std::string& name, std::shared_ptr<NativeBackendTexture> texture);
  bool TrySetTexture(const std::string& name, const std::shared_ptr<NativeBackendTexture>& texture);
  void ClearTextures();
  // Replace a material's resource set without clearing unchanged bindings.
  // End removes slots not supplied since Begin. On failure clear both sets.
  void BeginResourceUpdate();
  void EndResourceUpdate();
  // Diagnostic snapshot of the current named binding; changes no state.
  NativeBackendTexture* ReadTexture(const std::string& name) const;
  NativeBackendSampler* ReadSampler(const std::string& name) const;
  void ClearSamplers();
  // Every reflected sampler slot's current value, and putting such a set back,
  // for a caller that runs another route's activations on these bindings and
  // must leave them as it found them (the shadow render's guest route: the
  // full-frame post reads its samplers back from here). Restore returns how
  // many slots it changed; slots the bindings do not reflect are ignored.
  std::map<UINT,NativeBackendSampler*> SamplerValues() const { return sampler_values_; }
  size_t RestoreSamplerValues(const std::map<UINT,NativeBackendSampler*>& values);
  void SetSampler(const std::string& name, NativeBackendSampler* sampler);
  bool TrySetSampler(const std::string& name, NativeBackendSampler* sampler);
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
  //
  // `version` counts this buffer's changes, from a counter shared by every
  // binding object, so (owner, version) names one exact image: a recorder
  // that sent it can tell whether the draw after needs it again without
  // comparing the bytes.
  struct ConstantImage { UINT slot; std::span<const uint8_t> bytes; const uint64_t* version; };
  std::span<const ConstantImage> ConstantImages() const { return constant_images_; }
  // Changes whenever any constant buffer changes, including mirrored updates.
  uint64_t constant_generation() const { return constant_generation_; }
  // What a recorder has to be told to bind, slot by slot, including the empty
  // slots: leaving one unset inherits the previous material's resource, which
  // is a wrong texture rather than a missing one.
  struct TextureImage { UINT slot; NativeBackendTexture* texture; };
  struct SamplerImage { UINT slot; NativeBackendSampler* sampler; };
  // Rebuilt only when the resources change; asked for per draw.
  const std::vector<TextureImage>& TextureImages() const;
  // Retain the exact bound generation for a persistent native material.
  std::shared_ptr<NativeBackendTexture> RetainTexture(UINT slot) const;
  const std::vector<SamplerImage>& SamplerImages() const;
  // Whether `other` reflects the same constant buffers, slot for slot and
  // variable for variable, so its bytes can stand in for this object's.
  bool SharesConstantLayout(const ShaderBindings& other) const;
  // Copy `source`'s constant bytes into this object's buffers, those that have
  // changed since the last copy. The reversed-depth variant of a shader is a
  // second compilation of the same source and reflects the same buffers, and
  // the scene draws only one variant; uploading every material into both, and
  // patching every instance into both, was half of the activation cost.
  void MirrorConstantsFrom(const ShaderBindings& source);
  // Bumped by every texture and sampler change. A caller that re-sent this
  // material's resources last draw can skip them this draw only if this has
  // not moved; comparing the object's address would not catch an activation
  // re-pointing a texture inside it, which is the common case.
  uint64_t resource_generation() const { return resource_generation_; }
  // Whether this shader binds any textures or samplers at all. Cheap on
  // purpose: the recorded draw path asks this per draw, and the answer used to
  // cost two vectors built and thrown away.
  bool BindsResources() const { return !textures_.empty() || !samplers_.empty(); }
  bool HasAllTextureInputs() const;
  bool UsesTextureResource(ID3D11Resource& resource) const;
  bool UsesTexture(const NativeBackendTexture& texture) const;
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
    uint64_t version = 0;
  };
  // Every change to a buffer's bytes goes through here: dirty for the D3D11
  // upload, a fresh version for everyone else.
  void Touch(Buffer& buffer);
  uint64_t constant_generation_=0;
  const std::shared_ptr<const uint8_t> generation_=std::make_shared<const uint8_t>(0);
  NativeShader shader_;
  std::vector<Buffer> buffers_;
  // Buffer extents are immutable after reflection; bytes are updated in place.
  std::vector<ConstantImage> constant_images_;
  std::map<std::string, Variable> variables_;
  std::map<std::string, UINT> textures_, samplers_;
  std::map<UINT, std::shared_ptr<NativeBackendTexture>> texture_values_;
  std::map<UINT, NativeBackendSampler*> sampler_values_;
  // Reflected map nodes never move or get erased. Direct slot access avoids
  // tree lookups during activation while the maps retain ownership/iteration.
  std::array<std::shared_ptr<NativeBackendTexture>*,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> texture_values_by_slot_{};
  std::array<NativeBackendSampler**,D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> sampler_values_by_slot_{};
  uint64_t resource_generation_=1;
  mutable std::vector<TextureImage> texture_images_;
  mutable std::vector<SamplerImage> sampler_images_;
  mutable uint64_t images_generation_=0;
  // Per buffer, the source version MirrorConstantsFrom last copied.
  std::vector<uint64_t> mirrored_versions_;
  std::array<bool,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> texture_seen_{};
  std::array<bool,D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> sampler_seen_{};
  // Immutable reflected runs, with live pointers backed by the owning maps.
  // These cache binding data, never D3D context state: Bind always re-emits it.
  std::vector<std::pair<UINT,UINT>> texture_runs_,sampler_runs_;
  std::array<ID3D11ShaderResourceView*,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> texture_slots_{};
  std::array<ID3D11SamplerState*,D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> sampler_slots_{};
};
}  // namespace edf::native

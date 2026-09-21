#pragma once
#include "native_material_parameters.h"
#include "native_scene_bindings.h"

namespace edf::native {
// A material is a program over pass state, not the device state left by its
// last visible draw. All values below are owned; addresses are identities only.
struct NativeSceneMaterialInputs {
  struct Constant {
    bool pixel=false;
    std::string name;
    std::vector<uint8_t> registers;
    bool global=false;
    bool operator==(const Constant&) const=default;
  };
  struct Texture {
    std::string name;
    uint32_t handle=0,slot=0;
    // Ordered activation fields: LOD bias bits, mip filter, min filter, mag filter.
    std::array<uint32_t,4> settings{};
    // Texture binding replaces the LOD range, but preserves pass addressing,
    // filtering and other sampler fields. Null binding preserves the old range.
    std::optional<uint32_t> lod_range;
    bool global=false;
    bool operator==(const Texture&) const=default;
  };
  uint32_t vertex=0,pixel=0;
  std::vector<Constant> constants;
  std::vector<Texture> textures;
  std::vector<std::array<uint32_t,2>> state_overrides;
  bool operator==(const NativeSceneMaterialInputs&) const=default;
};

// Requirements come from native reflection. Unused parameters must not cause
// reads of unused guest payloads, just as the ordinary native activation path.
template<class Reader,class ConstantBytes,class UsesTexture>
NativeSceneMaterialInputs ReadNativeSceneMaterialInputs(const Reader& reader,uint32_t material,
    const NativeMaterialParameters::Groups& schema,ConstantBytes&& required,UsesTexture&& uses_texture) {
  NativeSceneMaterialInputs result;
  const auto pass=reader.Word(reader.Add(material,108));
  result.vertex=reader.Word(reader.Word(pass));
  result.pixel=reader.Word(reader.Add(reader.Word(reader.Add(pass,4)),4));
  for(size_t group=0;group<4;++group) for(const auto& parameter:schema[group]) {
    const auto bytes=required(group>=2,parameter.name);
    if(!bytes) continue;
    if(bytes%16 || bytes>size_t(parameter.registers)*16)
      throw std::runtime_error("native scene material parameter extent mismatch: "+parameter.name);
    const auto value=parameter.ReadValue(reader,(group&1)!=0);
    if(value.available>4096 || bytes>size_t(value.available)*16)
      throw std::runtime_error("native scene material global capacity mismatch: "+parameter.name);
    const auto* data=reader.Bytes(value.data,bytes);
    result.constants.push_back({group>=2,parameter.name,{data,data+bytes},(group&1)!=0});
  }
  for(size_t global=0;global<2;++global) for(const auto& parameter:schema.textures[global]) {
    if(!uses_texture(parameter.name)) continue;
    const auto value=parameter.ReadValue(reader,global!=0);
    if(value.slot>=16) throw std::runtime_error("native scene material texture slot is invalid");
    const auto settings=global?reader.Add(reader.Word(parameter.record),32):reader.Add(parameter.record,12);
    NativeSceneMaterialInputs::Texture texture{parameter.name,value.handle,value.slot,ReadGuestWords<4>(reader,settings)};
    texture.global=global!=0;
    if(value.handle) texture.lod_range=reader.Word(reader.Add(value.handle,44))&0x3fc;
    result.textures.push_back(std::move(texture));
  }
  const auto states=reader.Word(reader.Add(material,96)),count=reader.Word(reader.Add(material,104));
  if(count>4096) throw std::runtime_error("native scene material state count is invalid");
  for(uint32_t i=0;i<count;++i) result.state_overrides.push_back(ReadGuestWords<2>(reader,reader.Add(states,i*8)));
  return result;
}

struct NativeSceneMaterialProgram {
  std::shared_ptr<NativeRenderBackend> backend;
  NativeSceneMaterialInputs inputs;
  NativeShader vertex,reversed_vertex,pixel;
  std::vector<std::shared_ptr<NativeBackendTexture>> textures;
  // The caller supplies a pipeline/blend factor with state_overrides already
  // applied, and samplers resolved against explicit pass state. This constructs
  // bindings only; it never executes the recorded state operations or borrows
  // the currently active shader bindings/guest device state.
  NativeSceneMaterialCapture Capture(NativeBackendPipeline& pipeline,bool reversed,
      std::span<NativeBackendSampler* const> samplers,
      std::optional<std::array<float,4>> blend_factor={}) const {
    if(textures.size()!=inputs.textures.size() || samplers.size()!=textures.size())
      throw std::runtime_error("native scene material resources are incomplete");
    ShaderBindings vs(nullptr,reversed?reversed_vertex:vertex),ps(nullptr,pixel);
    ValidateNativeShaderLink(vs.shader(),ps.shader());
    for(const auto& constant:inputs.constants) {
      auto& bindings=constant.pixel?ps:vs;
      const auto bytes=bindings.GuestFloatRegisterBytes(constant.name);
      if(bytes>constant.registers.size()) throw std::runtime_error("native material shader changed register requirements");
      if(bytes) bindings.SetGuestFloatRegisters(constant.name,std::span(constant.registers).first(bytes));
    }
    for(size_t i=0;i<textures.size();++i) {
      const auto& name=inputs.textures[i].name;
      const bool texture=ps.TrySetTexture(name,textures[i]);
      const bool sampler=ps.TrySetSampler(name,samplers[i]);
      if(!texture && !sampler)
        throw std::runtime_error("native material resource does not match its shader");
    }
    return CaptureNativeSceneMaterial(backend,pipeline,vs,ps,blend_factor);
  }
};
}

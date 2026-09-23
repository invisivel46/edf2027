#pragma once
#include "native_material_parameters.h"
#include "native_scene_bindings.h"
#include "native_material_sampler.h"
#include "native_material_render_state.h"
#include <algorithm>
#include <bit>
#include <optional>
#include <span>
#include <string_view>

namespace edf::native {
// A material is a program over pass state, not the device state left by its
// last visible draw. All values below are owned; addresses are identities only.
struct NativeSceneMaterialDefinition {
  struct VertexRegisters {
    std::string name;
    uint32_t first=0,count=0;
    bool operator==(const VertexRegisters&) const=default;
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
  std::vector<VertexRegisters> vertex_registers;
  std::vector<Texture> textures;
  std::vector<std::array<uint32_t,2>> state_overrides;
  std::optional<uint32_t> WorldRegisterFirst() const {
    const VertexRegisters* world=nullptr;
    for(const auto& range:vertex_registers) if(range.name=="g_mWorld") {
      if(world || range.count!=4 || range.first>252) return {};
      world=&range;
    }
    if(!world) return {};
    for(const auto& range:vertex_registers)
      if(&range!=world && range.first<uint64_t(world->first)+4 && uint64_t(range.first)+range.count>world->first)
        return {};
    return world->first;
  }
  bool operator==(const NativeSceneMaterialDefinition&) const=default;
};
struct NativeSceneMaterialInputs : NativeSceneMaterialDefinition {
  struct Constant {
    bool pixel=false;
    std::string name;
    std::vector<uint8_t> registers;
    bool global=false;
    bool operator==(const Constant&) const=default;
  };
  std::vector<Constant> constants;
  bool operator==(const NativeSceneMaterialInputs&) const=default;
};

// Globals the scene pass always replaces (NativeScenePassCamera/Animation::Apply)
// before any use; their published bytes are layout only, never values.
inline bool NativeScenePassOwnedConstant(bool global,std::string_view name) {
  return global && (name=="g_mProjection" || name=="g_mView" || name=="g_mViewTranspose" ||
    name=="g_mViewProjection" || name=="m_WaterTime" || name=="g_SignalBrightness");
}
// The global g_mWorld: whatever object the guest drew last (821A17D8 and the
// instance loops store it), so its value moves every tick. Every consumer of a
// published static group material replaces it before it is observable:
// - the world pass and TryAppendPublishedNativeSceneInstance resolve only
//   groups whose capture binds exactly one vertex world matrix
//   (ConfigureNativeQueuedWorldLocked, else WorldParameter), which
//   CaptureNativeSceneMaterial zeroes from the image, and apply each
//   instance's own published world;
// - the guest path's published activation (ObservePublishedActivation) binds
//   constants to the live shader bindings, so it re-reads this value live
//   (ReadNativeSceneLiveWorldLocked) as 821B94E8 would upload it;
// - the model pass keeps its own loads and supplies each record's world.
// Its only value dependence is the capture's self-comparison, which throws on
// a NaN: a published NaN is refreshed like any other constant.
inline bool NativeSceneObjectWorldConstant(bool global,std::string_view name) { return global && name=="g_mWorld"; }
// Whether the first 64 bytes (16 big-endian floats, as the capture reads a
// 4x4 back) of a world constant hold a NaN.
inline bool NativeSceneWorldHasNaN(std::span<const uint8_t> registers) {
  for(size_t i=0;i+4<=64 && i+4<=registers.size();i+=4) {
    const auto value=std::bit_cast<float>(uint32_t(registers[i])<<24|uint32_t(registers[i+1])<<16|uint32_t(registers[i+2])<<8|registers[i+3]);
    if(value!=value) return true;
  }
  return false;
}
// Which schema parameters a program uploads, and their extents. A function of
// the schema and native reflection only: it stays valid while both do.
struct NativeSceneMaterialConstantSlot {
  uint32_t group=0,parameter=0,bytes=0;
  bool pass_owned=false,object_world=false;
  bool operator==(const NativeSceneMaterialConstantSlot&) const=default;
};
using NativeSceneMaterialConstantLayout=std::vector<NativeSceneMaterialConstantSlot>;
// Requirements come from native reflection. Unused parameters must not cause
// reads of unused guest payloads, just as the ordinary native activation path.
template<class ConstantBytes>
NativeSceneMaterialConstantLayout ResolveNativeSceneMaterialConstants(
    const NativeMaterialParameters::Groups& schema,ConstantBytes&& required) {
  NativeSceneMaterialConstantLayout layout;
  for(uint32_t group=0;group<4;++group) for(uint32_t index=0;index<schema[group].size();++index) {
    const auto& parameter=schema[group][index];
    const auto bytes=required(group>=2,parameter.name);
    if(!bytes) continue;
    const bool global=(group&1)!=0;
    // Globals use native reflection and the live backing vector's capacity.
    // Locals follow the ordinary bridge's exact descriptor-sized upload.
    if(bytes%16 || bytes>UINT32_MAX || (!global && bytes!=size_t(parameter.registers)*16))
      throw std::runtime_error("native scene material parameter extent mismatch: "+parameter.name);
    layout.push_back({group,index,uint32_t(bytes),NativeScenePassOwnedConstant(global,parameter.name),
      NativeSceneObjectWorldConstant(global,parameter.name)});
  }
  return layout;
}
template<class Reader>
const uint8_t* ReadNativeSceneMaterialConstant(const Reader& reader,const NativeMaterialParameters::Groups& schema,
    const NativeSceneMaterialConstantSlot& slot) {
  if(slot.group>=4 || slot.parameter>=schema[slot.group].size()) throw std::runtime_error("native material constant slot is stale");
  const auto& parameter=schema[slot.group][slot.parameter];
  const bool global=(slot.group&1)!=0;
  const auto value=parameter.ReadValue(reader,global);
  if(global && (value.available>4096 || slot.bytes>size_t(value.available)*16))
    throw std::runtime_error("native scene material global capacity mismatch: "+parameter.name);
  return reader.Bytes(value.data,slot.bytes);
}
template<class Reader>
std::vector<NativeSceneMaterialInputs::Constant> ReadNativeSceneMaterialConstants(const Reader& reader,
    const NativeMaterialParameters::Groups& schema,const NativeSceneMaterialConstantLayout& layout) {
  std::vector<NativeSceneMaterialInputs::Constant> result;
  result.reserve(layout.size());
  for(const auto& slot:layout) {
    const auto* data=ReadNativeSceneMaterialConstant(reader,schema,slot);
    result.push_back({slot.group>=2,schema[slot.group][slot.parameter].name,{data,data+slot.bytes},(slot.group&1)!=0});
  }
  return result;
}
// Per-tick refresh of an unchanged program: reads only the constant values and
// allocates only when one differs. Pass-owned slots are not read, nor is the
// object world unless its published value is a NaN (NativeSceneObjectWorldConstant).
// Returns the replacement constants, or nothing when `published` is still current.
template<class Reader>
std::optional<std::vector<NativeSceneMaterialInputs::Constant>> RefreshNativeSceneMaterialConstants(const Reader& reader,
    const NativeMaterialParameters::Groups& schema,const NativeSceneMaterialConstantLayout& layout,
    const std::vector<NativeSceneMaterialInputs::Constant>& published) {
  if(published.size()!=layout.size()) throw std::runtime_error("native material constants do not match their layout");
  std::optional<std::vector<NativeSceneMaterialInputs::Constant>> result;
  for(size_t i=0;i<layout.size();++i) {
    if(layout[i].pass_owned) continue;
    if(layout[i].object_world && published[i].registers.size()==layout[i].bytes &&
       !NativeSceneWorldHasNaN(published[i].registers)) continue;
    const auto* data=ReadNativeSceneMaterialConstant(reader,schema,layout[i]);
    const auto& old=published[i].registers;
    if(old.size()==layout[i].bytes && std::equal(old.begin(),old.end(),data)) continue;
    if(!result) result=published;
    (*result)[i].registers.assign(data,data+layout[i].bytes);
  }
  return result;
}
// Everything except constant values: the stable part of an owned program.
template<class Reader,class UsesTexture>
NativeSceneMaterialDefinition ReadNativeSceneMaterialDefinition(const Reader& reader,uint32_t material,
    const NativeMaterialParameters::Groups& schema,UsesTexture&& uses_texture) {
  NativeSceneMaterialDefinition result;
  const auto pass=reader.Word(reader.Add(material,108));
  result.vertex=reader.Word(reader.Word(pass));
  result.pixel=reader.Word(reader.Add(reader.Word(reader.Add(pass,4)),4));
  for(size_t group=0;group<2;++group) for(const auto& parameter:schema[group]) {
    if(!parameter.registers) continue;
    if(parameter.first>256 || parameter.registers>256-parameter.first)
      throw std::runtime_error("invalid published vertex register range");
    result.vertex_registers.push_back({parameter.name,parameter.first,parameter.registers});
  }
  for(size_t global=0;global<2;++global) for(const auto& parameter:schema.textures[global]) {
    if(!uses_texture(parameter.name)) continue;
    const auto value=parameter.ReadValue(reader,global!=0);
    if(value.slot>=16) throw std::runtime_error("native scene material texture slot is invalid");
    const auto settings=global?reader.Add(reader.Word(parameter.record),32):reader.Add(parameter.record,12);
    NativeSceneMaterialDefinition::Texture texture{parameter.name,value.handle,value.slot,ReadGuestWords<4>(reader,settings)};
    texture.global=global!=0;
    if(value.handle) texture.lod_range=reader.Word(reader.Add(value.handle,44))&0x3fc;
    result.textures.push_back(std::move(texture));
  }
  const auto states=reader.Word(reader.Add(material,96)),count=reader.Word(reader.Add(material,104));
  if(count>4096) throw std::runtime_error("native scene material state count is invalid");
  for(uint32_t i=0;i<count;++i) result.state_overrides.push_back(ReadGuestWords<2>(reader,reader.Add(states,i*8)));
  return result;
}
template<class Reader,class ConstantBytes,class UsesTexture>
NativeSceneMaterialInputs ReadNativeSceneMaterialInputs(const Reader& reader,uint32_t material,
    const NativeMaterialParameters::Groups& schema,ConstantBytes&& required,UsesTexture&& uses_texture) {
  const auto layout=ResolveNativeSceneMaterialConstants(schema,required);
  NativeSceneMaterialInputs result;
  static_cast<NativeSceneMaterialDefinition&>(result)=ReadNativeSceneMaterialDefinition(reader,material,schema,uses_texture);
  result.constants=ReadNativeSceneMaterialConstants(reader,schema,layout);
  return result;
}

struct NativeSceneResolvedMaterial {
  NativeSceneMaterialCapture capture;
  NativeMaterialRenderPass render;
  std::array<NativeMaterialSamplerPass,16> samplers;
};
struct NativeSceneMaterialProgram {
  std::shared_ptr<NativeRenderBackend> backend;
  NativeSceneMaterialDefinition inputs;
  std::vector<NativeMaterialSamplerOperation> sampler_operations;
  NativeShader vertex,reversed_vertex,pixel;
  std::vector<std::shared_ptr<NativeBackendTexture>> textures;
  bool CanDeferCpuActivation() const {
    // Scissor enable recomputes viewport-dependent rectangle storage through
    // a retained callback. Its geometry is not yet an owned pass input.
    return std::none_of(inputs.state_overrides.begin(),inputs.state_overrides.end(),
      [](const auto& operation) { return operation[0]==0xc8; });
  }
  NativeMaterialRenderPass ResolveRenderState(NativeMaterialRenderPass pass) const {
    for(const auto& operation:inputs.state_overrides) ApplyNativeMaterialState(pass,operation[0],operation[1]);
    return pass;
  }
  std::array<NativeMaterialSamplerPass,16> ResolveSamplers(std::array<NativeMaterialSamplerPass,16> pass) const {
    for(const auto& operation:sampler_operations) {
      if(operation.slot>=pass.size()) throw std::runtime_error("native material sampler slot is invalid");
      pass[operation.slot]=ApplyNativeMaterialSampler(pass[operation.slot],operation);
    }
    return pass;
  }
  // Target formats, topology, layout and stable shader/layout identities come
  // from the native pass. Shader bytes, material state and samplers come from
  // this owned program. Constants are explicit immutable publication/pass
  // inputs, never a previous visible draw or live guest bindings.
  NativeSceneResolvedMaterial Resolve(NativeBackendPipelineDesc desc,bool reversed,
      std::span<const NativeSceneMaterialInputs::Constant> constants,
      NativeMaterialRenderPass render,std::array<NativeMaterialSamplerPass,16> samplers,
      int filtering_override=-1,bool palette=false) const {
    if(!backend) throw std::runtime_error("native material has no backend");
    render=ResolveRenderState(std::move(render));
    samplers=ResolveSamplers(std::move(samplers));
    const auto& selected=reversed?reversed_vertex:vertex;
    const auto bytes=[](const auto& code) -> std::span<const uint8_t> {
      if(!code) throw std::runtime_error("native material has no shader bytecode");
      return {static_cast<const uint8_t*>(code->GetBufferPointer()),code->GetBufferSize()};
    };
    desc.vertex=bytes(selected.bytecode); desc.pixel=bytes(pixel.bytecode); desc.state=render.words;
    auto& pipeline=backend->CreatePipeline(desc);
    if(selected.instanced_bytecode && !pipeline.world_instanced && desc.input_layout.size()<=28 &&
       std::none_of(desc.input_layout.begin(),desc.input_layout.end(),
         [](const auto& element) { return element.slot==15 || element.per_instance; })) {
      NativeOwnedInputLayout layout;
      for(const auto& element:desc.input_layout) layout.Add(element.semantic,element.semantic_index,
        element.format,element.slot,element.offset,element.per_instance,element.step_rate);
      for(uint32_t row=0;row<4;++row) layout.Add("EDFINSTANCE",row,DXGI_FORMAT_R32G32B32A32_FLOAT,15,row*16,true,1);
      desc.vertex=bytes(selected.instanced_bytecode); desc.vertex_id|=uint64_t(1)<<63;
      desc.input_layout=layout.elements(); desc.input_layout_id=layout.fingerprint();
      pipeline.world_instanced=&backend->CreatePipeline(desc);
      pipeline.instance_world_slot=selected.instance_world_slot;
      pipeline.instance_world_offset=selected.instance_world_offset;
    }
    std::vector<NativeBackendSampler*> resources;
    resources.reserve(inputs.textures.size());
    for(const auto& texture:inputs.textures) {
      if(texture.slot>=samplers.size()) throw std::runtime_error("native material sampler slot is invalid");
      const auto key=NativeFilteringKey(samplers[texture.slot].words,filtering_override);
      resources.push_back(&backend->CreateSampler(DecodeNativeGuestSampler(key)));
    }
    std::optional<std::array<float,4>> factor;
    if(pipeline.requires_blend_factor()) {
      factor.emplace();
      for(size_t i=0;i<4;++i) (*factor)[i]=std::bit_cast<float>(render.blend_factor[i]);
    }
    return {Capture(pipeline,reversed,constants,resources,factor,palette),std::move(render),std::move(samplers)};
  }
  // The caller supplies a pipeline/blend factor with state_overrides already
  // applied, and samplers resolved against explicit pass state. This constructs
  // bindings only; it never executes the recorded state operations or borrows
  // the currently active shader bindings/guest device state.
  NativeSceneMaterialCapture Capture(NativeBackendPipeline& pipeline,bool reversed,
      std::span<const NativeSceneMaterialInputs::Constant> constants,
      std::span<NativeBackendSampler* const> samplers,
      std::optional<std::array<float,4>> blend_factor={},bool palette=false) const {
    if(textures.size()!=inputs.textures.size() || samplers.size()!=textures.size())
      throw std::runtime_error("native scene material resources are incomplete");
    ShaderBindings vs(nullptr,reversed?reversed_vertex:vertex),ps(nullptr,pixel);
    ValidateNativeShaderLink(vs.shader(),ps.shader());
    ApplyBindings(vs,ps,constants,samplers);
    return CaptureNativeSceneMaterial(backend,pipeline,vs,ps,blend_factor,{},palette);
  }
  // Also refresh the shared bindings used by mixed passes. Resource updates
  // must remove slots omitted by this material, while unsupplied constants
  // retain the shader's existing/default value, as ordinary activation does.
  void ApplyBindings(ShaderBindings& vs,ShaderBindings& ps,
      std::span<const NativeSceneMaterialInputs::Constant> constants,
      std::span<NativeBackendSampler* const> samplers) const {
    if(textures.size()!=inputs.textures.size() || samplers.size()!=textures.size())
      throw std::runtime_error("native scene material resources are incomplete");
    for(const auto& constant:constants) {
      auto& bindings=constant.pixel?ps:vs;
      const auto bytes=bindings.GuestFloatRegisterBytes(constant.name);
      if(bytes>constant.registers.size() || (bytes && !constant.global && bytes!=constant.registers.size()))
        throw std::runtime_error("native material shader changed register requirements");
      if(bytes) bindings.SetGuestFloatRegisters(constant.name,std::span(constant.registers).first(bytes));
    }
    ps.BeginResourceUpdate();
    for(size_t i=0;i<textures.size();++i) {
      const auto& name=inputs.textures[i].name;
      const bool texture=ps.TrySetTexture(name,textures[i]);
      const bool sampler=ps.TrySetSampler(name,samplers[i]);
      if(!texture && !sampler)
        throw std::runtime_error("native material resource does not match its shader");
    }
    ps.EndResourceUpdate();
  }
};
// Capture(...,palette=true) split for a run of draws whose constants differ
// only in some named constants' registers (the model pass's g_mWorldArray,
// each object's packed bone palette): captured once, it keeps its own
// bindings, and With derives a draw's capture by rebinding only those
// constants and copying their variables' byte ranges (ShaderBindings::Range)
// into a copy of the captured image. That is Capture's result for the
// constants with those replaced, byte for byte: a replaced variable may not
// be one of the capture's recorded matrices (refused; those are zeroed from
// the image and read back as world or camera), so every other image byte,
// the world, camera, textures, samplers and blend factor are the captured
// ones. The palette itself is never read back (Capture keeps it as bound).
// Construction takes Capture's hold (the backend's texture objects); With
// touches only this object, the retained textures' counts and the pipeline's
// fields, which its resolve set. Not synchronized. The pipeline is backend-owned.
class NativeScenePaletteCapture {
 public:
  NativeScenePaletteCapture(const NativeSceneMaterialProgram& program,NativeBackendPipeline& pipeline,bool reversed,
      std::span<const NativeSceneMaterialInputs::Constant> constants,std::span<NativeBackendSampler* const> samplers,
      std::optional<std::array<float,4>> blend_factor={})
      :backend_(program.backend),pipeline_(&pipeline),blend_factor_(blend_factor),
       vertex_(std::make_unique<ShaderBindings>(nullptr,reversed?program.reversed_vertex:program.vertex)),
       pixel_(std::make_unique<ShaderBindings>(nullptr,program.pixel)) {
    if(program.textures.size()!=program.inputs.textures.size() || samplers.size()!=program.textures.size())
      throw std::runtime_error("native scene material resources are incomplete");
    ValidateNativeShaderLink(vertex_->shader(),pixel_->shader());
    program.ApplyBindings(*vertex_,*pixel_,constants,samplers);
    capture_=CaptureNativeSceneMaterial(backend_,pipeline,*vertex_,*pixel_,blend_factor,{},true);
  }
  NativeScenePaletteCapture(const NativeScenePaletteCapture&)=delete;
  NativeScenePaletteCapture& operator=(const NativeScenePaletteCapture&)=delete;
  // The capture of the constructor's constants.
  const NativeSceneMaterialCapture& capture() const { return capture_; }
  // A fresh material (never the captured object, as a per-draw Capture makes
  // one): the captured one with each of replaced's variables rebound, in
  // order, as ApplyBindings binds a constant (the last of a name wins).
  NativeSceneMaterialCapture With(std::span<const NativeSceneMaterialInputs::Constant> replaced) {
    patches_.clear();
    for(const auto& constant:replaced) {
      auto& bindings=constant.pixel?*pixel_:*vertex_;
      const auto binding=bindings.ResolveFloatRegisters(constant.name);
      const auto bytes=binding.bytes();
      if(bytes>constant.registers.size() || (bytes && !constant.global && bytes!=constant.registers.size()))
        throw std::runtime_error("native material shader changed register requirements");
      if(!bytes) continue;
      bindings.SetGuestFloatRegisters(binding,std::span(constant.registers).first(bytes));
      patches_.push_back({constant.pixel?NativeBackendStage::Pixel:NativeBackendStage::Vertex,bindings.Range(binding)});
    }
    const auto& material=*capture_.material;
    auto constants=material.constants();
    for(const auto& [stage,range]:patches_) {
      const auto image=std::find_if(constants.begin(),constants.end(),
        [&](const auto& c) { return c.stage==stage && c.slot==range.slot; });
      const auto sources=(stage==NativeBackendStage::Vertex?*vertex_:*pixel_).ConstantImages();
      const auto source=std::find_if(sources.begin(),sources.end(),[&](const auto& i) { return i.slot==range.slot; });
      if(image==constants.end() || source==sources.end() || source->bytes.size()!=image->bytes.size() ||
         range.offset>image->bytes.size() || image->bytes.size()-range.offset<range.size)
        throw std::runtime_error("native palette capture image missing");
      for(const auto& matrix:image->matrices)
        if(matrix.offset<uint64_t(range.offset)+range.size && uint64_t(matrix.offset)+64>range.offset)
          throw std::runtime_error("native palette capture cannot replace a captured matrix");
      std::copy_n(source->bytes.begin()+range.offset,range.size,image->bytes.begin()+range.offset);
    }
    std::vector<NativeSceneSampler> samplers;
    samplers.reserve(material.samplers().size());
    for(const auto& [slot,sampler]:material.samplers()) samplers.push_back({slot,sampler});
    NativeSceneMaterialCapture result;
    result.material=std::make_shared<NativeSceneMaterial>(backend_,*pipeline_,std::move(constants),
      material.textures(),std::move(samplers),blend_factor_);
    result.world=capture_.world; result.camera=capture_.camera;
    return result;
  }
 private:
  struct Patch { NativeBackendStage stage; ShaderBindings::FloatRegisterRange range; };
  std::shared_ptr<NativeRenderBackend> backend_;
  NativeBackendPipeline* pipeline_;
  std::optional<std::array<float,4>> blend_factor_;
  std::unique_ptr<ShaderBindings> vertex_,pixel_;
  NativeSceneMaterialCapture capture_;
  std::vector<Patch> patches_;
};
// An immutable value at a material boundary. Resolve both stages before
// publishing the next value so an unsupported operation cannot partly advance
// inherited state. Unmentioned sampler slots and requested state survive.
struct NativeSceneMaterialPassState {
  NativeMaterialRenderPass render;
  std::array<NativeMaterialSamplerPass,16> samplers{};
  NativeSceneMaterialPassState Inputs() const {
    auto result=*this;
    for(auto& sampler:result.samplers) sampler=NativeMaterialSamplerInputs(sampler);
    return result;
  }
  NativeSceneMaterialPassState After(const NativeSceneMaterialProgram& program) const {
    return NativeSceneMaterialPassState{program.ResolveRenderState(render),program.ResolveSamplers(samplers)}.Inputs();
  }
  bool operator==(const NativeSceneMaterialPassState&) const=default;
};
}

#pragma once
#include "native_material_cpu_program.h"
#include "native_shader_binding.h"
#include "native_scene_geometry.h"
#include <algorithm>
#include <array>

namespace edf::native {
enum class NativeStaticGroupEligibility {
  Supported, Geometry, TextureOrState, Alignment, RetirementMode, AliasedInput, PassState
};
struct NativeStaticReadRange { uint32_t address; size_t bytes; };
template<class Reader> struct NativeStaticReadTrace {
  const Reader& backing;
  std::vector<NativeStaticReadRange>& reads;
  NativeStaticReadRange untraced{0,0};
  uint32_t Add(uint32_t address,uint32_t offset) const { return backing.Add(address,offset); }
  uint32_t Word(uint32_t address) const {
    if(uint64_t(address)-untraced.address>=untraced.bytes) reads.push_back({address,4});
    return backing.Word(address);
  }
  const uint8_t* Bytes(uint32_t address,size_t bytes) const {
    reads.push_back({address,bytes}); return backing.Bytes(address,bytes);
  }
};
// Deferred-group contract: retained indexed geometry, the native constant,
// texture and decoded-state executors, native shader defaults and empty/fence
// retirement. Scissor, undecoded or invalid operation lists, queue growth and
// input aliases remain explicit compatibility work.
// The native draw consumes an explicit pass captured before activation and
// resolved by this material's own state operations; the cursor carries the
// result forward. Only that resolved pass must be representable, not the
// incoming one. This runs before deferring any CPU work, while inputs are intact.
template<class Reader>
NativeStaticGroupEligibility AssessNativeStaticGroup(const Reader& reader,uint32_t device,
    uint32_t stack,const NativeSceneGeometrySource& geometry) {
  using Result=NativeStaticGroupEligibility;
  if(!geometry.vertex || !geometry.index || !geometry.declaration || !geometry.material ||
     !geometry.count || geometry.count%3 || !geometry.stride || geometry.stride%4 || geometry.stride>2048)
    return Result::Geometry;
  if((device&15) || stack<4096) return Result::Alignment;
  std::vector<NativeStaticReadRange> reads;
  // Setter identities are compared, not captured; no activation write lands there.
  const NativeStaticReadTrace<Reader> trace{reader,reads,{reader.Add(device,56),0x158}};
  NativeMaterialCpuProgram program;
  try { program=ReadNativeMaterialCpuProgram(trace,geometry.material,device); }
  catch(const std::exception&) { return Result::TextureOrState; }
  if(!program.vertex || !program.pixel || program.vertex!=geometry.shader) return Result::Geometry;
  const auto word=[&](uint32_t offset) { return reader.Word(reader.Add(device,offset)); };
  std::vector<NativeStaticReadRange> writes;
  auto pass=ReadNativeMaterialRenderPassMirrors(reader,device);
  for(const auto& state:program.states) {
    const auto cpu=NativeMaterialStateCpuWrites(pass,state.offset,state.value,0,0);
    if(!cpu) return Result::TextureOrState; // Scissor rectangle owner remains a callback.
    for(const auto& [offset,value]:*cpu) writes.push_back({reader.Add(device,offset),4});
    ApplyNativeMaterialState(pass,state.offset,state.value);
  }
  try { DecodeNativeRenderState(pass.words); }
  catch(const std::exception&) { return Result::PassState; }
  // Defaults can write beyond the usual device mirror. Include their full
  // destination extent and their source bytes in the alias preflight.
  uint32_t device_extent=13520;
  for(bool pixel:{false,true}) {
    const auto defaults=ReadNativeShaderDefaults(trace,pixel?program.pixel:program.vertex,pixel);
    for(const auto& value:defaults.words) {
      const auto offset=1024+value.offset;
      for(uint32_t pass_offset:{10424u,10420u,10440u,10428u,10332u,11584u})
        if(offset==pass_offset) return Result::PassState;
      device_extent=(std::max)(device_extent,offset+4);
    }
  }
  writes.push_back({device,device_extent}); writes.push_back({stack-4096,4096});
  const auto fence=word(10780);
  for(uint32_t offset:{12188u,12164u,12420u,12416u}) {
    const auto previous=word(offset);
    if(!previous) continue;
    if(!fence) return Result::RetirementMode;
    writes.push_back({reader.Add(previous,8),4});
  }
  // Texture binding: descriptor words, binding handle, dirty bank and the
  // previous handle's fence. Repeated slots retire the handle bound earlier.
  std::array<uint32_t,16> bound;
  for(uint32_t slot=0;slot<16;++slot) bound[slot]=word(12272+slot*4);
  for(const auto& texture:program.textures) {
    writes.push_back({reader.Add(device,1024+texture.slot*24),24});
    writes.push_back({reader.Add(device,12272+texture.slot*4),4});
    writes.push_back({reader.Add(device,16),8});
    if(const auto previous=bound[texture.slot]) {
      if(!fence) return Result::RetirementMode;
      writes.push_back({reader.Add(previous,8),4});
    }
    if(texture.handle) reads.push_back({reader.Add(texture.handle,28),24});
    bound[texture.slot]=texture.handle;
  }
  // Captured operation tables must remain stable through setup/activation.
  // Payload reads are included too: the selected contract excludes vector-copy
  // aliases and shader-default payload aliases rather than changing their order.
  for(const auto& read:reads) for(const auto& write:writes)
    if(uint64_t(read.address)<uint64_t(write.address)+write.bytes &&
       uint64_t(write.address)<uint64_t(read.address)+read.bytes) return Result::AliasedInput;
  return Result::Supported;
}
}

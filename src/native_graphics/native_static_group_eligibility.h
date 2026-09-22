#pragma once
#include "native_material_cpu_program.h"
#include "native_shader_binding.h"
#include "native_scene_geometry.h"
#include <algorithm>
#include <array>
#include <optional>

namespace edf::native {
enum class NativeStaticGroupEligibility {
  Supported, Geometry, TextureOrState, Alignment, RetirementMode, AliasedInput, PassState
};
struct NativeStaticReadRange { uint32_t address; size_t bytes; };
// A word the assessment reads outside a recording reader's record
// (NativeRecordingReader::Unrecorded); plain readers read it as usual.
template<class Reader> uint32_t NativeUnrecordedWord(const Reader& reader,uint32_t address) {
  if constexpr(requires { reader.Unrecorded(); }) return reader.Unrecorded().Word(address);
  else return reader.Word(address);
}
// The per-frame inputs of a Supported assessment, kept as the predicate its
// outcome depends on instead of their bytes. The device fence and its
// retirement slots (the previous shaders', and each texture slot's previously
// bound handle) move as frames retire, and the stack follows call depth. The
// assessment uses them only as below, so with the same recorded bytes it
// returns Supported again iff Holds:
// - stack: stack>=4096 and [stack-4096,stack) aliases no alias-checked read;
// - each retirement slot: zero, or the fence is non-zero and the 4 bytes at
//   its value+8 alias no alias-checked read;
// - fence: only zero or not, when a slot or the program's own repeated texture
//   slot (fenced) retires a handle.
// Every other alias-checked read and write follows from device and recorded
// bytes (program tables, pass mirrors and texture descriptors are read through
// the recorder and their addresses from recorded pointers), so `reads` is the
// same list while those bytes are unchanged.
struct NativeStaticEligibilityWitness {
  std::vector<NativeStaticReadRange> reads;
  std::vector<uint32_t> retirements;
  uint32_t fence=0;
  bool fenced=false;
  template<class Reader> bool Holds(const Reader& reader,uint32_t stack) const {
    const auto aliases=[&](uint64_t address,uint64_t bytes) {
      for(const auto& read:reads)
        if(uint64_t(read.address)<address+bytes && address<uint64_t(read.address)+read.bytes) return true;
      return false;
    };
    if(stack<4096 || aliases(uint64_t(stack)-4096,4096)) return false;
    try {
      std::optional<bool> live;
      const auto fence_set=[&] { if(!live) live=reader.Word(fence)!=0; return *live; };
      if(fenced && !fence_set()) return false;
      for(const auto slot:retirements) {
        const auto previous=reader.Word(slot);
        if(previous && (!fence_set() || aliases(reader.Add(previous,8),4))) return false;
      }
    } catch(const std::exception&) { return false; }
    return true;
  }
};
template<class Reader> struct NativeStaticReadTrace {
  const Reader& backing;
  std::vector<NativeStaticReadRange>& reads;
  NativeStaticReadRange untraced{0,0};
  uint32_t Add(uint32_t address,uint32_t offset) const { return backing.Add(address,offset); }
  void Record(uint32_t address,size_t bytes) const {
    const auto at=uint64_t(address)-untraced.address;
    if(at>=untraced.bytes || bytes>untraced.bytes-at) reads.push_back({address,bytes});
  }
  uint32_t Word(uint32_t address) const { Record(address,4); return backing.Word(address); }
  const uint8_t* Bytes(uint32_t address,size_t bytes) const {
    Record(address,bytes); return backing.Bytes(address,bytes);
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
// witness, when given, receives a Supported outcome's per-frame predicate. The
// fence and retirement slots are read unrecorded, so a caller recording this
// assessment's bytes must keep the witness with them and check both.
template<class Reader>
NativeStaticGroupEligibility AssessNativeStaticGroup(const Reader& reader,uint32_t device,
    uint32_t stack,const NativeSceneGeometrySource& geometry,NativeStaticEligibilityWitness* witness=nullptr) {
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
  std::vector<NativeStaticReadRange> writes;
  // Pass inputs through the same readers prepare and the state/texture
  // executors run: the live render pass (8200964C color scale check) and every
  // slot's sampler pass (bias scale 82003198, the anisotropy table entry the
  // slot's device byte selects). Device mirrors among them are the modeled
  // pass; state/sampler operations replay their own writes, so only shader
  // defaults can change them unmodeled. Reads outside the device join the
  // alias preflight like the program's own tables.
  std::vector<NativeStaticReadRange> pass_reads,mirrors;
  const NativeStaticReadTrace<Reader> pass_trace{reader,pass_reads};
  NativeMaterialRenderPass pass;
  try { pass=ReadNativeMaterialRenderPass(pass_trace,device); }
  catch(const std::exception&) { return Result::PassState; }
  try { for(uint32_t slot=0;slot<16;++slot) ReadNativeMaterialSamplerPass(pass_trace,device,slot); }
  catch(const std::exception&) { return Result::TextureOrState; }
  for(const auto& read:pass_reads) (uint64_t(read.address)-device<13520?mirrors:reads).push_back(read);
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
      const auto at=uint64_t(device)+offset;
      for(const auto& mirror:mirrors)
        if(uint64_t(mirror.address)<at+4 && at<uint64_t(mirror.address)+mirror.bytes) return Result::PassState;
      device_extent=(std::max)(device_extent,offset+4);
    }
  }
  writes.push_back({device,device_extent}); writes.push_back({stack-4096,4096});
  // Fence and retirement slots are per-frame words: read unrecorded and used
  // only as NativeStaticEligibilityWitness describes.
  const auto fence_word=reader.Add(device,10780);
  std::optional<uint32_t> fence;
  bool fenced=false;
  std::vector<uint32_t> retirements;
  const auto retires=[&](uint32_t previous) {
    if(!previous) return true;
    if(!fence) fence=NativeUnrecordedWord(reader,fence_word);
    if(!*fence) return false;
    writes.push_back({reader.Add(previous,8),4});
    return true;
  };
  for(uint32_t offset:{12188u,12164u,12420u,12416u}) {
    const auto slot=reader.Add(device,offset);
    retirements.push_back(slot);
    if(!retires(NativeUnrecordedWord(reader,slot))) return Result::RetirementMode;
  }
  // Texture binding: descriptor words, binding handle, dirty bank and the
  // previous handle's fence. Repeated slots retire the handle bound earlier:
  // the program's own (recorded) handle, not a device word.
  std::array<std::optional<uint32_t>,16> bound;
  for(const auto& texture:program.textures) {
    if(texture.slot>=bound.size()) return Result::TextureOrState;
    writes.push_back({reader.Add(device,1024+texture.slot*24),24});
    writes.push_back({reader.Add(device,12272+texture.slot*4),4});
    writes.push_back({reader.Add(device,16),8});
    if(const auto& previous=bound[texture.slot]) {
      if(*previous) fenced=true;
      if(!retires(*previous)) return Result::RetirementMode;
    } else {
      const auto slot=reader.Add(device,12272+texture.slot*4);
      retirements.push_back(slot);
      if(!retires(NativeUnrecordedWord(reader,slot))) return Result::RetirementMode;
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
  if(witness) *witness=NativeStaticEligibilityWitness{std::move(reads),std::move(retirements),fence_word,fenced};
  return Result::Supported;
}
}

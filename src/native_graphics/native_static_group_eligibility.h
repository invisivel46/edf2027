#pragma once
#include "native_material_cpu_program.h"
#include "native_shader_binding.h"
#include "native_scene_geometry.h"
#include <algorithm>

namespace edf::native {
enum class NativeStaticGroupEligibility {
  Supported, Geometry, TextureOrState, Alignment, RetirementMode, AliasedInput, PassState
};
struct NativeStaticReadRange { uint32_t address; size_t bytes; };
template<class Reader> struct NativeStaticReadTrace {
  const Reader& backing;
  std::vector<NativeStaticReadRange>& reads;
  uint32_t Add(uint32_t address,uint32_t offset) const { return backing.Add(address,offset); }
  uint32_t Word(uint32_t address) const {
    reads.push_back({address,4}); return backing.Word(address);
  }
  const uint8_t* Bytes(uint32_t address,size_t bytes) const {
    reads.push_back({address,bytes}); return backing.Bytes(address,bytes);
  }
};
// First-group contract: retained indexed geometry, constants-only activation,
// native shader defaults and empty/fence retirement. More general texture/state,
// queue-growth and alias contracts remain explicit compatibility work.
// This runs before deferring any CPU work, while guest inputs are still intact.
template<class Reader>
NativeStaticGroupEligibility AssessNativeStaticGroup(const Reader& reader,uint32_t device,
    uint32_t stack,const NativeSceneGeometrySource& geometry) {
  using Result=NativeStaticGroupEligibility;
  if(!geometry.vertex || !geometry.index || !geometry.declaration || !geometry.material ||
     !geometry.count || geometry.count%3 || !geometry.stride || geometry.stride%4 || geometry.stride>2048)
    return Result::Geometry;
  if((device&15) || stack<4096) return Result::Alignment;
  if(reader.Word(reader.Add(device,10424))!=0x10001) return Result::PassState;
  if(reader.Word(reader.Add(geometry.material,80)) || reader.Word(reader.Add(geometry.material,92)) ||
     reader.Word(reader.Add(geometry.material,104))) return Result::TextureOrState;
  std::vector<NativeStaticReadRange> reads;
  const NativeStaticReadTrace<Reader> trace{reader,reads};
  const auto program=ReadNativeMaterialCpuProgram(trace,geometry.material,device);
  if(!program.vertex || !program.pixel || program.vertex!=geometry.shader) return Result::Geometry;
  // Defaults can write beyond the usual device mirror. Include their full
  // destination extent and their source bytes in the alias preflight.
  uint32_t device_extent=13520;
  for(bool pixel:{false,true}) {
    const auto defaults=ReadNativeShaderDefaults(trace,pixel?program.pixel:program.vertex,pixel);
    for(const auto& word:defaults.words) {
      const auto offset=1024+word.offset;
      for(uint32_t pass_offset:{10424u,10420u,10440u,10428u,10332u,11584u})
        if(offset==pass_offset) return Result::PassState;
      device_extent=(std::max)(device_extent,offset+4);
    }
  }
  std::vector<NativeStaticReadRange> writes{{device,device_extent},{stack-4096,4096}};
  const auto fence=reader.Word(reader.Add(device,10780));
  for(uint32_t offset:{12188u,12164u,12420u,12416u}) {
    const auto previous=reader.Word(reader.Add(device,offset));
    if(!previous) continue;
    if(!fence) return Result::RetirementMode;
    writes.push_back({reader.Add(previous,8),4});
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

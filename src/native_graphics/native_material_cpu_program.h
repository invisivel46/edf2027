#pragma once
#include "guest_block.h"
#include "native_material_sampler.h"
#include "native_material_render_state.h"
#include <vector>
#include <cstring>

namespace edf::native {
struct NativeMaterialCpuProgram {
  struct Constant { bool pixel; uint32_t first,data,count; };
  struct Texture { uint32_t slot,handle; NativeMaterialSamplerOperation sampler; };
  struct State { uint32_t offset,setter,value; };
  uint32_t vertex=0,pixel=0;
  std::vector<Constant> constants;
  std::vector<Texture> textures;
  std::vector<State> states;
};
// Returns false before mutation when vector load/store ordering still requires
// the aliased compatibility path. The caller retains that explicit fallback.
template<class Reader>
bool UploadNativeMaterialConstant(const Reader& reader,uint32_t device,
    const NativeMaterialCpuProgram::Constant& operation,uint64_t mask) {
  const auto destination=reader.Add(device,(operation.first+(operation.pixel?368:112))*16);
  const size_t bytes=size_t(operation.count)*16;
  if((destination&15) || !(uint64_t(operation.data)+bytes<=destination || uint64_t(destination)+bytes<=operation.data))
    return false;
  const auto* source=reader.Bytes(operation.data,bytes);
  auto* target=const_cast<uint8_t*>(reader.WritableBytes(destination,bytes,4));
  std::memcpy(target,source,bytes);
  const auto at=reader.Add(device,operation.pixel?8:0);
  const auto dirty=(uint64_t(reader.Word(at))<<32)|reader.Word(reader.Add(at,4));
  reader.StoreWord(at,uint32_t((dirty|mask)>>32));
  reader.StoreWord(reader.Add(at,4),uint32_t(dirty|mask));
  return true;
}
// Same bounded input capture used by live activation. Execution remains ordered
// shader bindings -> constants -> textures -> states; payloads are read at use.
template<class Reader>
NativeMaterialCpuProgram ReadNativeMaterialCpuProgram(const Reader& reader,uint32_t instance,uint32_t device) {
  NativeMaterialCpuProgram result;
  auto& constants=result.constants; auto& textures=result.textures; auto& states=result.states;
  const auto pass=reader.Word(reader.Add(instance,108));
  const auto vertex=reader.Word(reader.Word(pass));
  const auto pixel=reader.Word(reader.Add(reader.Word(reader.Add(pass,4)),4));
  for(const auto offset:std::array<uint32_t,4>{0,24,36,60}) {
    const auto header=edf::native::ReadGuestWords<3>(reader,reader.Add(instance,offset));
    if(header[2]>4096) throw std::runtime_error("native activation constant count");
    for(uint32_t i=0;i<header[2];++i) {
      const auto value=edf::native::ReadGuestWords<4>(reader,reader.Add(header[0],i*16));
      const bool global=offset==24 || offset==60;
      NativeMaterialCpuProgram::Constant operation{offset>=36,global?value[3]:value[0],global?reader.Word(value[0]):value[1],global?value[2]:value[3]};
      if(!operation.count || operation.first>=256 || operation.count>256-operation.first)
        throw std::runtime_error("native activation constant range");
      reader.Bytes(operation.data,size_t(operation.count)*16);
      constants.push_back(operation);
    }
  }
  for(const bool global:{false,true}) {
    const auto header=edf::native::ReadGuestWords<3>(reader,reader.Add(instance,global?84:72));
    if(header[2]>4096) throw std::runtime_error("native activation texture count");
    for(uint32_t i=0;i<header[2];++i) {
      const auto record=reader.Add(header[0],i*(global?8:28));
      const auto source=global?reader.Word(record):record;
      NativeMaterialCpuProgram::Texture operation{};
      operation.slot=reader.Word(reader.Add(record,global?4:8));
      operation.handle=reader.Word(reader.Add(source,global?28:4));
      if(operation.slot>=16) throw std::runtime_error("native activation sampler slot");
      operation.sampler.slot=operation.slot;
      operation.sampler.settings=edf::native::ReadGuestWords<4>(reader,reader.Add(source,global?32:12));
      textures.push_back(operation);
    }
  }
  const auto state_header=edf::native::ReadGuestWords<3>(reader,reader.Add(instance,96));
  if(state_header[2]>4096) throw std::runtime_error("native activation state count");
  for(uint32_t i=0;i<state_header[2];++i) {
    const auto value=edf::native::ReadGuestWords<2>(reader,reader.Add(state_header[0],i*8));
    const auto setter=edf::native::NativeMaterialStateSetter(value[0]);
    if(reader.Word(reader.Add(device,56+value[0]))!=setter) throw std::runtime_error("native activation state setter changed");
    states.push_back({value[0],setter,value[1]});
  }
  result.vertex=vertex; result.pixel=pixel;
  return result;
}
template<class Shader,class Constant,class Texture,class State>
void ExecuteNativeMaterialCpuProgram(const NativeMaterialCpuProgram& program,
    Shader&& shader,Constant&& constant,Texture&& texture,State&& state) {
  shader(program.vertex,false); shader(program.pixel,true);
  for(const auto& operation:program.constants) constant(operation);
  for(const auto& operation:program.textures) texture(operation);
  for(const auto& operation:program.states) state(operation);
}
}

#pragma once
#include "native_scene_material.h"
#include <map>

namespace edf::native {
// Owned row-vector scene matrices. No global parameter nodes or shader binding
// images are retained. The producer selects one camera for the entire pass.
struct NativeScenePassCamera {
  std::array<uint32_t,16> projection{},view{},view_projection{};
  bool operator==(const NativeScenePassCamera&) const=default;

  bool Apply(NativeSceneMaterialInputs::Constant& constant) const {
    if(!constant.global) return false;
    const std::array<uint32_t,16>* matrix=nullptr;
    bool transpose=true;
    if(constant.name=="g_mProjection") matrix=&projection;
    else if(constant.name=="g_mView") matrix=&view;
    else if(constant.name=="g_mViewTranspose") { matrix=&view; transpose=false; }
    else if(constant.name=="g_mViewProjection") matrix=&view_projection;
    else return false;
    if(constant.registers.size()!=64) throw std::runtime_error("native pass camera requires four registers");
    for(size_t row=0;row<4;++row) for(size_t col=0;col<4;++col) {
      const auto word=(*matrix)[transpose?col*4+row:row*4+col];
      for(size_t byte=0;byte<4;++byte)
        constant.registers[(row*4+col)*4+byte]=uint8_t(word>>(24-byte*8));
    }
    return true;
  }
};
// Values selected before the world's update callback advances its counters.
// Advancing simulation state belongs to the producer, not material resolution.
struct NativeScenePassAnimation {
  uint32_t water_time=0,signal_counter=0;
  bool operator==(const NativeScenePassAnimation&) const=default;
  bool Apply(NativeSceneMaterialInputs::Constant& constant) const {
    if(!constant.global || (constant.name!="m_WaterTime" && constant.name!="g_SignalBrightness")) return false;
    if(constant.registers.size()!=16) throw std::runtime_error("native pass animation extent: "+constant.name+
      " bytes="+std::to_string(constant.registers.size()));
    const uint32_t value=constant.name=="m_WaterTime"?water_time:
      ((signal_counter&64)?0x41200000u:0u);
    const std::array<uint32_t,4> words{value,0,0,0x3f800000};
    for(size_t i=0;i<4;++i) for(size_t byte=0;byte<4;++byte)
      constant.registers[i*4+byte]=uint8_t(words[i]>>(24-byte*8));
    return true;
  }
};
template<class Reader>
NativeScenePassCamera ReadNativeScenePassCamera(const Reader& reader,uint32_t scene) {
  return {ReadGuestWords<16>(reader,reader.Add(scene,32)),
          ReadGuestWords<16>(reader,reader.Add(scene,96)),
          ReadGuestWords<16>(reader,reader.Add(scene,160))};
}
using NativeScenePassCameras=std::map<uint32_t,NativeScenePassCamera>;
// Replace the complete active-view set after producer camera updates. Removed
// views disappear from the next generation; acquired generations remain valid.
template<class Reader>
NativeScenePassCameras ReadNativeScenePassCameras(const Reader& reader,uint32_t manager) {
  NativeScenePassCameras result;
  auto node=reader.Word(manager);
  const auto end=reader.Word(reader.Add(manager,12));
  size_t visited=0;
  while(node!=end) {
    if(++visited>256) throw std::runtime_error("native camera publication view list is cyclic or excessive");
    const auto scene=reader.Word(reader.Add(node,8));
    result.insert_or_assign(scene,ReadNativeScenePassCamera(reader,scene));
    node=reader.Word(node);
  }
  return result;
}
}

#pragma once
#include "native_scene_geometry.h"
#include "native_stream_binding.h"
#include <optional>

namespace edf::native {
enum class NativeGeometryBinding { Stream, Declaration, Index };
// Owned by one group installation. CPU mutation and host publication are
// separate steps: a failed publication must not retire the new binding again.
struct NativeSceneGeometryInstallState {
  std::optional<NativeSceneGeometrySource> input;
  uint32_t device=0;
  unsigned step=0;
  void Bind(uint32_t target,const NativeSceneGeometrySource& source) {
    if(input && (device!=target || *input!=source))
      throw std::runtime_error("geometry installation retry changed its inputs");
    if(!input) { input=source; device=target; }
  }
};
// The group's actual CPU setup, with retirement and publication boundaries
// injected for the host. Publication follows each completed binding, never
// before its old resource has been retired. Allocation remains explicit.
// Reader accesses must be valid and Observe must not throw (the live observer
// is noexcept). Publish may throw and must tolerate retrying an incomplete host
// publication. Keep progress for as long as the enclosing handoff is pending.
template<class Reader,class Reserve,class Tag,class Observe,class Publish>
void InstallNativeSceneGeometry(const Reader& reader,uint32_t device,
    const NativeSceneGeometrySource& input,Reserve&& reserve,Tag&& tag,
    Observe&& observe,Publish&& publish,NativeSceneGeometryInstallState* progress=nullptr) {
  NativeSceneGeometryInstallState local;
  auto& state=progress?*progress:local;
  state.Bind(device,input);
  if(state.step==0) {
    SetNativeStreamResource(reader,device,0,input.vertex,0,input.stride,4096,
      [&] { return reserve(false); },[&] { return tag(false); },
      [&](auto path,auto previous,auto value,auto cursor) { observe(false,path,previous,value,cursor); });
    ++state.step;
  }
  if(state.step==1) { publish(NativeGeometryBinding::Stream); ++state.step; }
  if(state.step==2) {
    reader.StoreWord(reader.Add(device,11536),input.declaration);
    const auto dirty=reader.DoubleWord(reader.Add(device,16))|(uint64_t(1)<<51);
    reader.StoreWord(reader.Add(device,16),uint32_t(dirty>>32));
    reader.StoreWord(reader.Add(device,20),uint32_t(dirty));
    ++state.step;
  }
  if(state.step==3) { publish(NativeGeometryBinding::Declaration); ++state.step; }
  if(state.step==4) {
    SetNativeIndexResource(reader,device,input.index,
      [&] { return reserve(true); },[&] { return tag(true); },
      [&](auto path,auto previous,auto value,auto cursor) { observe(true,path,previous,value,cursor); });
    ++state.step;
  }
  if(state.step==5) { publish(NativeGeometryBinding::Index); ++state.step; }
}
}

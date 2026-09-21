#pragma once
#include "guest_block.h"

namespace edf::native {
struct NativeSceneGeometrySource {
  uint32_t vertex=0,index=0,declaration=0,stride=0,count=0,material=0,shader=0;
  bool operator==(const NativeSceneGeometrySource&) const=default;
};
// Read the inputs of 821D96D8 without executing its device setters, material
// activation, instance callback or draw. 82149A90 binds the declaration from
// the selected node; 821375C0 binds the embedded index resource at +84.
template<class Reader>
NativeSceneGeometrySource ReadNativeSceneGeometrySource(const Reader& reader,uint32_t group) {
  const auto descriptor=reader.Word(reader.Add(group,12));
  const auto container=reader.Word(reader.Add(descriptor,72));
  const auto node=reader.Word(reader.Add(descriptor,76));
  if(!container || !node || node==reader.Word(reader.Add(container,4)))
    throw std::runtime_error("native scene geometry has no declaration node");
  NativeSceneGeometrySource result;
  result.vertex=reader.Add(descriptor,4);
  result.index=reader.Add(descriptor,84);
  result.declaration=reader.Word(reader.Add(node,28));
  result.stride=reader.Word(reader.Add(descriptor,60));
  const auto count=int32_t(reader.Word(reader.Add(descriptor,140)));
  if(count<=0 || !result.stride || result.stride>2048 || result.stride%4)
    throw std::runtime_error("unsupported native scene geometry extent");
  result.count=uint32_t((count/3)*3);
  if(!result.count) throw std::runtime_error("native scene geometry has no triangles");
  result.material=reader.Word(reader.Add(reader.Word(descriptor),16));
  const auto pass=reader.Word(reader.Add(result.material,108));
  result.shader=reader.Word(reader.Word(pass));
  if(!result.declaration || !result.shader) throw std::runtime_error("native scene geometry has incomplete inputs");
  return result;
}
}

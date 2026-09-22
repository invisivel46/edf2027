#pragma once
#include "native_scene_material.h"
#include <array>
#include <cstdint>
#include <stdexcept>

namespace edf::native {
// Target formats and depth direction of a full-frame scene pass: the pipeline
// inputs a resolved material depends on besides its program and base state.
// Shared by the static world, model and sky passes.
struct NativeFullFramePassTargets {
  uint32_t count=1;
  std::array<uint32_t,8> rtv_format{};
  uint32_t dsv_format=0,samples=1;
  bool reverse_depth=false;
  bool operator==(const NativeFullFramePassTargets&) const=default;
};
// The one explicit base state every full-frame scene draw starts from (static
// world, models opaque and transparent, sky): the device state right after
// scene begin. A draw's state is this plus its own material's state
// operations - never chained from the previous draw or group, and never read
// from the guest device mirrors.
//
// Why: in the guest, a draw inherits whatever the previous draw (or pass) left
// on the device and 821B8E48 applies only the operations its material lists.
// Chaining in walk order would make one draw's state depend on what was
// visible before it this frame, and chaining from the mirrors needs guest
// state. 821BE8D0 (scene begin) forwards the scene's viewport and clears,
// uploads the camera and calls 82135530(device,1) - operation 0x28, depth
// enable - and sets nothing else. The rest is the Xbox 360 D3D device default
// state, written as the operations that produce it:
//   0x28=1 depth test on (scene begin)     0x30=1 depth write on
//   0x2c=3 depth func LESS_EQUAL           0x38=6 cull D3DCULL_CCW (back faces, CW front)
//   0x3c=0 blend off (words[0]=0x10001)    0x48=1/0x4c=0 ONE/ZERO
//   0x60=0 alpha test off                  0xd4..0xe0=15 color writes on
//   stencil and scissor off
// Samplers start from the default record: every slot a material samples is
// written by its own sampler operations (filters, LOD range, bias); addressing
// stays as the texture's. A transparent material's blend, depth-write and
// alpha operations are among its own, so it keeps them. A material that
// relied on an inherited state it does not set shows up as a draw-local
// difference in the image A/B against the guest path, which validates this.
inline constexpr std::array<std::array<uint32_t,2>,12> kNativeFullFrameBaseOperations{{
  {0x28,1},{0x30,1},{0x2c,3},{0x38,6},{0x3c,0},{0x48,1},{0x4c,0},{0x60,0},{0xd4,15},{0xd8,15},{0xdc,15},{0xe0,15}}};
inline NativeSceneMaterialPassState NativeFullFrameBaseState(const NativeFullFramePassTargets& targets) {
  if(targets.count>4) throw std::runtime_error("native full-frame pass has more than four color targets");
  NativeSceneMaterialPassState state;
  auto& render=state.render;
  // Target presence gates depth enable and the color write mask, as it does
  // for the device's own setters.
  render.depth_target=targets.dsv_format?1:0;
  for(uint32_t i=0;i<targets.count;++i) render.color_targets[i]=1;
  render.words[0]=0x10001;
  for(const auto& [offset,value]:kNativeFullFrameBaseOperations) ApplyNativeMaterialState(render,offset,value);
  for(auto& sampler:state.samplers) sampler=NativeMaterialSamplerPass{};
  return state.Inputs();
}
}

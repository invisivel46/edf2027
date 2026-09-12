#pragma once
#include "guest_block.h"
#include <bit>
#include <algorithm>
#include <stdexcept>

namespace edf::native {
struct NativeBufferUpdateExtent { uint32_t address,bytes; };
// VB lock/unlock masks low address bits and its packed size word; IB uses
// plain address and size words. An unlock conservatively covers the whole
// allocation, not just a requested subrange or a completed outer transaction.
inline NativeBufferUpdateExtent DecodeNativeBufferUpdateExtent(
    uint32_t address_word,uint32_t size_word,bool index_buffer) {
  return index_buffer ? NativeBufferUpdateExtent{address_word,size_word} :
    NativeBufferUpdateExtent{address_word&0xfffffffcu,size_word&0x03fffffcu};
}
struct NativeSceneViewportCpuState {
  std::array<uint32_t,6> viewport;
  std::array<float,6> transform;
  std::array<uint32_t,2> packed_scissor;
};
inline NativeSceneViewportCpuState MakeNativeSceneViewportCpuState(
    std::array<uint32_t,6> viewport,uint32_t width,uint32_t height,
    const std::array<uint32_t,4>& scissor,bool scissor_enabled,
    const std::array<uint32_t,2>& packed,bool require_full_frame=true) {
  // Audited full-frame caller8219C828. Native extent replaces the Xbox tile
  // descriptor; retain CPU outputs of371D0/370E0, not34BD8's GPU packets.
  if(!width || !height || width>16384 || height>16384 || viewport[0]>width || viewport[1]>height ||
     (require_full_frame && (viewport[0] || viewport[1] || viewport[2]!=width || viewport[3]!=height)))
    throw std::runtime_error("unsupported native scene viewport contract");
  viewport[2]=std::min(viewport[2],width-viewport[0]);
  viewport[3]=std::min(viewport[3],height-viewport[1]);
  const float half_width=float(viewport[2])*.5f,half_height=float(viewport[3])*.5f;
  const float min_depth=std::bit_cast<float>(viewport[4]);
  const float max_depth=std::bit_cast<float>(viewport[5]);
  NativeSceneViewportCpuState result{viewport,
    {half_width,float(viewport[0])+half_width,-half_height,float(viewport[1])+half_height,
      max_depth-min_depth,min_depth},packed};
  std::array<int32_t,4> clip{int32_t(viewport[0]),int32_t(viewport[1]),
    int32_t(viewport[0]+viewport[2]),int32_t(viewport[1]+viewport[3])};
  if(scissor_enabled) {
    clip[0]=std::max(clip[0],std::bit_cast<int32_t>(scissor[0]));
    clip[1]=std::max(clip[1],std::bit_cast<int32_t>(scissor[1]));
    clip[2]=std::min(clip[2],std::bit_cast<int32_t>(scissor[2]));
    clip[3]=std::min(clip[3],std::bit_cast<int32_t>(scissor[3]));
  }
  for(size_t i=0;i<2;++i)
    result.packed_scissor[i]=(packed[i]&0x80008000u) |
      ((uint32_t(clip[i*2+1])&0x7fffu)<<16) | (uint32_t(clip[i*2])&0x7fffu);
  return result;
}
inline bool IsFontVertexDeclaration(uint32_t count,const std::array<uint32_t,6>& elements) {
  //821AD3B8 initializes method/usage/index but leaves each element's final
  //padding byte untouched.82149AB0 copies all12 bytes including that padding.
  return count==2 && elements[0]==0 && elements[1]==0x2c23a5 &&
    (elements[2]&0xffffff00)==0 && elements[3]==8 && elements[4]==0x2c23a5 &&
    (elements[5]&0xffffff00)==0x50000;
}
// Xenos PA_SU_VTX_CNTL (register 0x2302), shadowed at device+10560.
// 8214EFF8 - the same initializer the native device defaults are extracted
// from - stores 4 there, and 82132F48 emits 0x2302=4 in the device's default
// register packet. 82136440 is the only later writer: rlwimi r4,r11,0,0,30
// keeps the stored word's upper 31 bits and takes only bit 0 from its
// argument, then raises dirty bit 35, which the 821330C8 flush maps to
// register index 2 above base 0x2300. The recompilation contains no direct
// caller of 82136440, so the mode is read live per draw rather than assumed
// constant. 4 decodes as pix_center kD3DZero, round_mode kRoundToEven and
// 1/16th vertex quantization - the ordinary Direct3D 9 configuration.
struct GuestVertexCenters {
  // pix_center kD3DZero: a vertex at .0 addresses a pixel centre, so screen
  // coordinates need the half-pixel offset. kOGLHalf already centres at .5.
  bool integer_centers;
  uint32_t rounding, quantization;
};
inline GuestVertexCenters DecodeGuestVertexCenters(uint32_t word) {
  return {(word&1)==0,(word>>1)&3,(word>>3)&7};
}
// Same rule the SDK's own GPU path applies for pix_center==kD3DZero.
inline float GuestPixelCenterOffset(uint32_t word) {
  return DecodeGuestVertexCenters(word).integer_centers ? .5f : 0.f;
}
template<class Reader>
uint32_t ReadVertexCenterWord(const Reader& reader,uint32_t device) {
  return ReadGuestWords<1>(reader,reader.Add(device,10560))[0];
}
struct GuestShaderPair {
  uint32_t pixel, vertex;
};
template<class Reader>
std::array<float,4> ReadBlendFactor(const Reader& reader,uint32_t device) {
  // 82135418 stores R, G, B, A as big-endian floats in this order.
  const auto words=ReadGuestWords<4>(reader,reader.Add(device,10336));
  return {std::bit_cast<float>(words[0]),std::bit_cast<float>(words[1]),
          std::bit_cast<float>(words[2]),std::bit_cast<float>(words[3])};
}
template<class Reader>
GuestShaderPair ReadShaderPair(const Reader& reader,uint32_t device) {
  const auto words=ReadGuestWords<2>(reader,reader.Add(device,12416));
  return {words[0],words[1]};
}
struct GuestViewportWords {
  // x, y, width, height, min/max depth bits, signed scissor l/t/r/b bits.
  std::array<uint32_t,10> words;
  bool scissor_enabled;
};
struct GuestXuiDeviceWords {
  std::array<uint32_t,6> render;
  GuestViewportWords viewport;
  uint32_t surface,texture;
};
template<class Reader>
GuestXuiDeviceWords ReadXuiDeviceWords(const Reader& reader,uint32_t device) {
  // These fields belong to the same allocated retail device. Validate the
  // containing range once per draw; retain no pointers or mapping cache.
  // Shader/declaration identity belongs to native setter state. The range ends
  // before the old shader slots; do not make those slots a read prerequisite.
  const auto* bytes=reader.Bytes(reader.Add(device,10332),12416-10332);
  auto word=[&](uint32_t offset){return GuestBlockWord(bytes+offset-10332);};
  GuestXuiDeviceWords result{};
  result.render={word(10424),word(10420),word(10440),word(10428),word(10332)&15,
                 uint32_t(word(11584)!=0)};
  for(size_t i=0;i<10;++i) result.viewport.words[i]=word(12376+uint32_t(i)*4);
  result.viewport.scissor_enabled=word(11584)!=0;
  result.surface=word(12168); result.texture=word(12272);
  return result;
}
template<class Reader>
GuestViewportWords ReadViewportWords(const Reader& reader,uint32_t device) {
  return {ReadGuestWords<10>(reader,reader.Add(device,12376)),
          ReadGuestWords<1>(reader,reader.Add(device,11584))[0]!=0};
}
template<class Reader>
GuestViewportWords ReadViewportWords(const Reader& reader,uint32_t device,bool owned_scissor) {
  return {ReadGuestWords<10>(reader,reader.Add(device,12376)),owned_scissor};
}
template<class Reader>
GuestXuiDeviceWords ReadXuiDeviceWords(const Reader& reader,uint32_t device,
    const std::array<uint32_t,6>& owned_render) {
  // Surface/texture/viewport remain guest-owned. Do not read or validate the
  // earlier render words, including scissor-enable, for this native-state path.
  const auto* bytes=reader.Bytes(reader.Add(device,12168),12416-12168);
  auto word=[&](uint32_t offset){return GuestBlockWord(bytes+offset-12168);};
  GuestXuiDeviceWords result{};
  result.render=owned_render;
  for(size_t i=0;i<10;++i) result.viewport.words[i]=word(12376+uint32_t(i)*4);
  result.viewport.scissor_enabled=owned_render[5]!=0;
  result.surface=word(12168); result.texture=word(12272);
  return result;
}
template<class Reader>
std::array<uint32_t,6> ReadRenderStateWords(const Reader& reader,uint32_t device) {
  const auto state=ReadGuestWords<6>(reader,reader.Add(device,10420));
  const auto mask=ReadGuestWords<1>(reader,reader.Add(device,10332))[0];
  const auto scissor=ReadGuestWords<1>(reader,reader.Add(device,11584))[0];
  // Same word order consumed by CreateNativeRenderState.
  return {state[1],state[0],state[5],state[2],mask&15,uint32_t(scissor!=0)};
}
}  // namespace edf::native

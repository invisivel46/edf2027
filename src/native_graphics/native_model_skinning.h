#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace edf::native {
// Same alias as native_model_pose_history.h (a legal redeclaration), kept here so
// this header stays free of the history/camera dependencies. One source matrix is
// the 64-byte guest pose entry read as sixteen floats: element i sits at +4*i.
using NativePoseMatrix=std::array<float,16>;

// Bone-palette and rigid-world uploads of 821C9C20, transcribed from the
// recompiled bodies. Pure functions: no guest memory, no shader bridge.
//
// 821A1738 (generated/default/edf2017_recomp.62.cpp:7812-7922), called from
// 821C9C20 at 821C9D24 (edf2017_recomp.65.cpp:8909-8917) with
// r4=*(*(8257C02C)+36) (the 13-character name lookup at 821A37E8), r5=pose begin
// (vector+4) and r6=(end-begin)>>6 (srawi; 0 for a null begin, which then traps):
//   - returns at once for a null descriptor (r4==0);
//   - count=min(r6, Word(descriptor+16)) as unsigned words (cmplw/ble at 821A1748/821A174C);
//   - returns when the clamped count is 0; destination=Word(descriptor+0);
//   - per bone b: out[b*12+c*4+r]=src[b*16+r*4+c] for c in 0..2, r in 0..3, i.e.
//     three float4 rows, each a source column (source offsets 0/16/32/48,
//     4/20/36/52, 8/24/40/56). Source column 3 (offsets 12/28/44/60) is dropped.
//     Source advances 64 bytes, destination 48: a column-major float4x3.
//   - bones at or past the limit are neither read nor written. Destination bones
//     past the clamped count keep whatever the shared scratch held before, as
//     does everything past Word(descriptor+16) bones.
//   - no register base is applied here: the body writes CPU scratch at
//     descriptor+0. The constant register comes from the shader's constant table
//     when the material uploads g_mWorldArray (68 bones, 204 registers per the
//     decoded Blend/SingleBlend shaders, docs/framerate-unlock.md:255-259). The
//     runtime value of descriptor+16 was not read in this work; callers pass it.
// 821A17D8 (edf2017_recomp.3.cpp:7388-7403) returns for a null descriptor,
// otherwise tails 821C8000 (edf2017_recomp.60.cpp:8547-8649) with
// r3=Word(descriptor+0), r4=source. It is called at 821C9CB8/821C9D88 with
// r4=*(*(8257C02C)+32) and source=pose begin+64*Word(record+44), after 821C9C20
// traps unless that index is below the pose count. 821C8000 writes a full 4x4
// transpose, out[c*4+r]=src[r*4+c], sixteen floats, no limit and no clamp.
//
// Precision: every element is lfs/stfs through a double (f0). Single to double to
// single is exact for all finite values, infinities and quiet NaNs; flush mode is
// disabled on entry so denormals survive. Hardware lfs/stfs do not quiet a
// signalling NaN, so these helpers copy bits. The recompiled host code may quiet
// one (bit 22) depending on whether the compiler keeps the conversion pair.
// The guest bodies interleave loads and stores; these helpers assume the source
// and destination spans do not overlap.
//
// Layout choice: guest scratch holds big-endian words (REX_STORE_U32). A native
// constant buffer binds host floats in the same element order: PackNativeBonePalette
// and NativeRigidWorld are what a native draw binds (three float4 registers per
// bone, four for the rigid world). The Guest* variants reproduce the scratch
// bytes exactly, for comparison against or writes into guest memory.
inline constexpr uint32_t kNativeBonePaletteFloats=12,kNativeBonePaletteBytes=48,
  kNativeBonePaletteShaderBones=68,kNativeRigidWorldFloats=16,kNativeRigidWorldBytes=64;
inline constexpr uint32_t kNativeBonePaletteDescriptor=36,kNativeRigidWorldDescriptor=32,
  kNativeBonePaletteLimitOffset=16;

// Unsigned clamp at 821A1748-821A1750 (cmplw, ble, mr r10,r11). requested is the srawi pose count.
inline constexpr uint32_t NativeBonePaletteCount(uint32_t requested,uint32_t limit) {
  return requested>limit?limit:requested;
}
inline void StoreNativeGuestFloat(uint8_t* out,float value) {
  const auto word=std::bit_cast<uint32_t>(value);
  out[0]=uint8_t(word>>24); out[1]=uint8_t(word>>16); out[2]=uint8_t(word>>8); out[3]=uint8_t(word);
}
// Returns the number of bones written. out must hold count*12 floats; nothing
// past that is touched, matching the guest's untouched scratch tail.
inline uint32_t PackNativeBonePalette(std::span<const NativePoseMatrix> pose,std::span<float> out,
    uint32_t limit=kNativeBonePaletteShaderBones) {
  if(pose.size()>0xffffffffull) throw std::length_error("native bone palette source too large");
  const auto count=NativeBonePaletteCount(uint32_t(pose.size()),limit);
  if(out.size()/kNativeBonePaletteFloats<count) throw std::length_error("native bone palette destination too small");
  for(uint32_t bone=0;bone<count;++bone) {
    const auto& m=pose[bone];
    float* row=out.data()+size_t(bone)*kNativeBonePaletteFloats;
    for(size_t c=0;c<3;++c) for(size_t r=0;r<4;++r) row[c*4+r]=m[r*4+c];
  }
  return count;
}
// Same bones as big-endian guest scratch bytes (48 per bone).
inline uint32_t PackGuestBonePalette(std::span<const NativePoseMatrix> pose,std::span<uint8_t> out,
    uint32_t limit=kNativeBonePaletteShaderBones) {
  if(pose.size()>0xffffffffull) throw std::length_error("guest bone palette source too large");
  const auto count=NativeBonePaletteCount(uint32_t(pose.size()),limit);
  if(out.size()/kNativeBonePaletteBytes<count) throw std::length_error("guest bone palette destination too small");
  for(uint32_t bone=0;bone<count;++bone) {
    const auto& m=pose[bone];
    uint8_t* row=out.data()+size_t(bone)*kNativeBonePaletteBytes;
    for(size_t c=0;c<3;++c) for(size_t r=0;r<4;++r) StoreNativeGuestFloat(row+(c*4+r)*4,m[r*4+c]);
  }
  return count;
}
// 821A17D8/821C8000 for one bone: the full transpose, column c in floats c*4..c*4+3.
inline NativePoseMatrix NativeRigidWorld(const NativePoseMatrix& m) {
  NativePoseMatrix out{};
  for(size_t c=0;c<4;++c) for(size_t r=0;r<4;++r) out[c*4+r]=m[r*4+c];
  return out;
}
inline std::array<uint8_t,kNativeRigidWorldBytes> GuestRigidWorld(const NativePoseMatrix& m) {
  std::array<uint8_t,kNativeRigidWorldBytes> out{};
  for(size_t c=0;c<4;++c) for(size_t r=0;r<4;++r) StoreNativeGuestFloat(out.data()+(c*4+r)*4,m[r*4+c]);
  return out;
}
// Checked bone selection, as 821C9C20 traps when index>=pose count.
inline NativePoseMatrix NativeRigidWorld(std::span<const NativePoseMatrix> pose,uint32_t bone) {
  if(bone>=pose.size()) throw std::out_of_range("native rigid world bone out of range");
  return NativeRigidWorld(pose[bone]);
}
}

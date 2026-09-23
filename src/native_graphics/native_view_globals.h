#pragma once
// The shared view globals the guest scene begin writes, for the full-frame
// renderer's guest listeners.
//
// clSgsCoreRender +4 (821BE8D0, edf2017_recomp.9.cpp:8424) ends with (821BE9A4..BC):
//   821A17F8([8257C02C], scene+32)   the projection
//   821A19F0([8257C02C], scene+96)   the view
// The full frame never calls 821BE8D0 (its native half is BeginView), so
// without this the effect pool keeps whatever the last guest frame left there.
// What still reads it in full-frame mode, per view after BeginView:
// - the guest listeners (NativeFullFrameHost::ViewOverlays): clSatoCallback
//   8216DA80 -> 8217A728 -> 82122640 walks the [8257BF88] instance list and
//   calls each instance's slot 3 (vtable+12), which is clItem01 8218F3D8 (the
//   pickups: one Ps_ZParticle billboard through 821A7640); clPlayerCamera
//   820D3FD0 draws its follow/talk icons through 821A7640 and its trajectory
//   through 821A8360, then 820D3050 its lines through 821A7B58. All of these
//   are immediate draws whose shaders take g_mView/g_mProjection/
//   g_mViewProjection from the pool's parameter values below (committed into
//   the device constants by the activation), and 821A8360 reads the eye
//   pool+192 directly.
// The native effects pass derives the same eye from the pass camera itself
// (NativeEffectEyeFromView) and compares pool+192 with it as a diagnostic
// (stale_guest_eye), which this write brings to zero.
//
// The pool [8257C02C] (built by 821A37E8) caches its shared parameters' value
// records: +32 g_mWorld, +36 g_mWorldArray, +40 g_mView, +44 g_mViewTranspose,
// +48 g_mViewInverseTranspose, +52 g_mViewProjection, +56 g_mProjection. A
// value record holds its float4 data pointer at +0 and its float4 count at +8
// (821A16D8). The pool's own copies: +64 the projection, +128 the view, +192
// the eye (x, y, z, w).
//
// 821A17F8(pool, P) (edf2017_recomp.47.cpp:7593):
//   if [pool+56]: 821C8000([[pool+56]], P)    g_mProjection = transpose(P)
//   pool+64 = P                                 (8 ld/std)
// 821A19F0(pool, V) (edf2017_recomp.37.cpp:7582):
//   821A16D8(pool, [pool+44], V, 4)            g_mViewTranspose = the first
//                                               min(4, count) rows of V as is
//   if [pool+48]: 821C8000(.., V)              g_mViewInverseTranspose = transpose(V)
//   if [pool+40]: 821C8000(.., V)              g_mView = transpose(V)
//   pool+128 = V
//   m = V; 821C8750(m)                          m's upper 3x3 transposed in place
//   821B0130(m.row3, m.row3, m)                 row 3's xyz through that 3x3
//   xyz *= [820013DC] (-1.0); pool+192 = (x, y, z, V[15])
//   821C8198(tmp, V, pool+64)                   tmp = V x P (the identity it
//                                               first builds at sp+80 is fully
//                                               overwritten)
//   if [pool+52]: 821C8000(.., tmp)            g_mViewProjection = transpose(V x P)
// (The names are 821A37E8's; the guest's "InverseTranspose" is transpose(V) and
// its "Transpose" is V itself. They are recorded, not reinterpreted.)
#include "guest_block.h"
#include "native_full_frame_effects.h"
#include "native_render_instances.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace edf::native {
struct NativeViewGlobalsLayout {
  static constexpr uint32_t kPoolGlobal=0x8257C02Cu;      // lis r11,-32168; lwz -16340
  static constexpr uint32_t kView=40,kViewTranspose=44,kViewInverseTranspose=48,kViewProjection=52,kProjection=56;
  static constexpr uint32_t kProjectionCopy=64,kViewCopy=128,kEye=192;
  static constexpr uint32_t kValueData=0,kValueCount=8;
  static constexpr uint32_t kViewTransposeRows=4;        // 821A19F0's li r6,4
  static constexpr uint32_t kMinusOne=kNativeEffectEyeScale; // 820013DC: lfs f0,5084(r10), -1.0
};
using NativeViewWords=std::array<uint32_t,16>;
struct NativeViewGlobals {
  NativeViewWords projection{},view{},view_projection{};
  std::array<uint32_t,4> eye{};
  bool operator==(const NativeViewGlobals&) const=default;
};

// sub_821C8000(dst, m): dst[r*4+c] = m[c*4+r], one lfs/stfs per float.
inline NativeViewWords NativeViewTranspose(const NativeViewWords& m) {
  NativeViewWords out{};
  for(size_t r=0;r<4;++r) for(size_t c=0;c<4;++c) out[r*4+c]=m[c*4+r];
  return out;
}
inline float NativeViewFloat(uint32_t word) { return std::bit_cast<float>(word); }
inline uint32_t NativeViewWord(float value) { return std::bit_cast<uint32_t>(value); }
// The eye of 821A19F0 (821C8750, then 821B0130, then the three fmuls by
// [820013DC]; w copied): NativeEffectEyeFromView, which the effect builders
// derive their ribbon eye with, so the stored eye and theirs are one function.
inline std::array<uint32_t,4> NativeViewEye(const NativeViewWords& view,float minus_one) {
  const auto eye=NativeEffectEyeFromView(view,minus_one);
  return {NativeViewWord(eye[0]),NativeViewWord(eye[1]),NativeViewWord(eye[2]),NativeViewWord(eye[3])};
}
// Everything 821A17F8 then 821A19F0 derive from (P, V). minus_one is the
// image constant [820013DC].
inline NativeViewGlobals ComputeNativeViewGlobals(const NativeViewWords& projection,const NativeViewWords& view,float minus_one) {
  NativeViewGlobals out;
  out.projection=projection;
  out.view=view;
  out.eye=NativeViewEye(view,minus_one);
  NativeGuestMatrix a{},b{};
  for(size_t i=0;i<16;++i) { a[i]=NativeViewFloat(view[i]); b[i]=NativeViewFloat(projection[i]); }
  // 821C8198(sp+80, V, pool+64): the multiply native_render_instances.h
  // transcribes (DPPS order; its flush mode is not modeled there either).
  const auto product=NativeGuestMatrixMultiply(a,b);
  for(size_t i=0;i<16;++i) out.view_projection[i]=NativeViewWord(product[i]);
  return out;
}

template<class Reader>
void StoreNativeViewWords(const Reader& r,uint32_t at,const NativeViewWords& words) {
  for(uint32_t i=0;i<16;++i) r.StoreWord(r.Add(at,i*4),words[i]);
}
// The stores 821A17F8 then 821A19F0 make, in their order, through a reader
// with Word/Add/StoreWord. Returns false (and writes nothing) when the pool
// pointer is null; 821BE8D0 would fault there.
template<class Reader>
bool WriteNativeViewGlobals(const Reader& r,const NativeViewWords& projection,const NativeViewWords& view) {
  using L=NativeViewGlobalsLayout;
  const auto pool=r.Word(L::kPoolGlobal);
  if(!pool) return false;
  const auto globals=ComputeNativeViewGlobals(projection,view,NativeViewFloat(r.Word(L::kMinusOne)));
  const auto value=[&](uint32_t offset) { return r.Word(r.Add(pool,offset)); };
  const auto transposed=[&](uint32_t offset,const NativeViewWords& m) {
    if(const auto record=value(offset)) StoreNativeViewWords(r,r.Word(r.Add(record,L::kValueData)),NativeViewTranspose(m));
  };
  // 821A17F8.
  transposed(L::kProjection,globals.projection);
  StoreNativeViewWords(r,r.Add(pool,L::kProjectionCopy),globals.projection);
  // 821A19F0: 821A16D8 first (count read unsigned, cmplw).
  if(const auto record=value(L::kViewTranspose)) {
    const auto rows=std::min(L::kViewTransposeRows,r.Word(r.Add(record,L::kValueCount)));
    const auto data=r.Word(r.Add(record,L::kValueData));
    for(uint32_t i=0;i<rows*4;++i) r.StoreWord(r.Add(data,i*4),globals.view[i]);
  }
  transposed(L::kViewInverseTranspose,globals.view);
  transposed(L::kView,globals.view);
  StoreNativeViewWords(r,r.Add(pool,L::kViewCopy),globals.view);
  for(uint32_t i=0;i<4;++i) r.StoreWord(r.Add(pool,L::kEye+i*4),globals.eye[i]);
  transposed(L::kViewProjection,globals.view_projection);
  return true;
}
}  // namespace edf::native

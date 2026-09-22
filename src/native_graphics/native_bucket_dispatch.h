#pragma once
#include <bit>
#include <climits>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace edf::native {
// Native port of the per-object tail of sub_821C0C00 (edf2017_recomp.57.cpp:8443)
// and of the bucket head insert sub_821A3B80 (edf2017_recomp.2.cpp:7457).
// r3=object, r4=context. The recompiled C++ is the reference: single results
// are double(float(...)) of double arithmetic, fctidz follows the recomp's
// host conversion, and constants are read from the guest image, not folded.
enum class NativeBucketDispatch : uint8_t { Return, Mode0, Bucket, Unknown };
inline constexpr uint32_t kNativeBucketMinimum=0x820009A4;  // 0.0f
inline constexpr uint32_t kNativeBucketMaximum=0x82018EB4;  // 65535.0f
inline constexpr uint32_t kNativeBucketMode2Scale=0x82019A20;  // 65536.0f

struct NativeBucketKey {
  uint16_t key=0;
  uint8_t low=0,high=0;       // object+40, object+41
  uint32_t depth_bits=0;      // object+44 (context+40)
  bool operator==(const NativeBucketKey&) const=default;
};
struct NativeBucketInsert {
  NativeBucketKey key;
  uint32_t heads=0,slot=0,previous=0;  // object+32, heads+(low+42)*4, old head (object+60)
  bool operator==(const NativeBucketInsert&) const=default;
};

// lhz 64 != 0 returns; lwz 52 is compared signed against 0, 1 and 2. Any
// other nonzero mode reaches the clamp with an uninitialized stack float.
template<class Reader>
NativeBucketDispatch ClassifyNativeBucket(const Reader& r,uint32_t object) {
  const auto* hidden=r.Bytes(r.Add(object,64),2);
  if((uint32_t(hidden[0])<<8|hidden[1])!=0) return NativeBucketDispatch::Return;
  const auto mode=int32_t(r.Word(r.Add(object,52)));
  if(mode==0) return NativeBucketDispatch::Mode0;
  return mode==1 || mode==2?NativeBucketDispatch::Bucket:NativeBucketDispatch::Unknown;
}
// Recomp fctidz: NaN -> INT64_MIN, > 2^63 -> INT64_MAX, else cvttsd2si, whose
// out-of-range result (including exactly 2^63 and < -2^63) is INT64_MIN.
inline int64_t NativeFctidz(double value) {
  if(std::isnan(value)) return INT64_MIN;
  if(value>double(LLONG_MAX)) return LLONG_MAX;
  if(value>=9223372036854775808.0 || value<-9223372036854775808.0) return INT64_MIN;
  return int64_t(value);
}
inline float NativeBucketSingle(uint32_t bits) { return std::bit_cast<float>(bits); }
// Mode 1: fmadds(ctx40,ctx0,ctx4) then fmuls by object+56. Mode 2: fmuls
// object+56 by 65536. Ordered clamp [0,65535]: f<min -> min, f>max -> max;
// NaN passes both compares and converts to INT64_MIN (key 0).
// depth_bits stands for context+40, the view-space z sub_821B0198 stored for
// this object; a native walk that never ran the guest transform passes its own.
template<class Reader>
NativeBucketKey ComputeNativeBucketKeyForDepth(const Reader& r,uint32_t context,uint32_t object,uint32_t depth_bits) {
  const auto single=[&](uint32_t at){ return double(NativeBucketSingle(r.Word(at))); };
  const auto mode=int32_t(r.Word(r.Add(object,52)));
  NativeBucketKey out;
  // lfs/stfs of the same register: the recomp's float(double(x)) round trip
  // is folded by the compiler, so the word is copied unchanged (as on PPC).
  out.depth_bits=depth_bits;
  double depth;
  if(mode==1) {
    depth=double(float(std::fma(double(NativeBucketSingle(depth_bits)),single(context),single(r.Add(context,4)))));
    depth=double(float(depth*single(r.Add(object,56))));
  } else if(mode==2) {
    depth=double(float(single(r.Add(object,56))*single(kNativeBucketMode2Scale)));
  } else throw std::runtime_error("native bucket key for an unsupported sort mode");
  const double minimum=single(kNativeBucketMinimum);
  if(depth<minimum) depth=minimum;
  else { const double maximum=single(kNativeBucketMaximum); if(depth>maximum) depth=maximum; }
  // stfd then lhz +6: the low halfword; stb low byte to +40, next byte to +41.
  out.key=uint16_t(uint64_t(NativeFctidz(depth)));
  out.low=uint8_t(out.key); out.high=uint8_t(out.key>>8);
  return out;
}
template<class Reader>
NativeBucketKey ComputeNativeBucketKey(const Reader& r,uint32_t context,uint32_t object) {
  return ComputeNativeBucketKeyForDepth(r,context,object,r.Word(r.Add(context,40)));
}
// Reads only: the addresses and values InsertNativeBucket will write.
template<class Reader>
NativeBucketInsert PlanNativeBucket(const Reader& r,uint32_t context,uint32_t object) {
  NativeBucketInsert plan;
  plan.key=ComputeNativeBucketKey(r,context,object);
  plan.heads=r.Word(r.Add(object,32));
  plan.slot=r.Add(plan.heads,(uint32_t(plan.key.low)+42)*4);
  plan.previous=r.Word(plan.slot);
  return plan;
}
// Stores in guest order: +44, +40, +41, then sub_821A3B80's +60 and head.
// Every read precedes the first store; a failed store leaves a state the
// original routine can redo from the start.
template<class Reader>
NativeBucketInsert InsertNativeBucket(const Reader& r,uint32_t context,uint32_t object) {
  const auto plan=PlanNativeBucket(r,context,object);
  r.StoreWord(r.Add(object,44),plan.key.depth_bits);
  r.StoreByte(r.Add(object,40),plan.key.low);
  r.StoreByte(r.Add(object,41),plan.key.high);
  r.StoreWord(r.Add(object,60),plan.previous);
  r.StoreWord(plan.slot,object);
  return plan;
}
}

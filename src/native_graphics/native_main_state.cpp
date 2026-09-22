#include "edf2017_pch.h"
REX_EXTERN(__imp__edf_native_shader_upload_cpu_tail);
REX_EXTERN(__imp__edf_native_shader_cache_cpu_tail);
REX_EXTERN(__imp__edf_native_derived_cpu_tail);

// Native CPU orchestration for ECB0. Packet emission was separated by the
// original audited extractor; these effects remain visible to guest fallback.
DEFINE_REX_FUNC(edf_native_main_state_cpu_tail) {
  const auto device=ctx.r3.u32;
  auto dirty=ctx.r4.u64;
  auto work=ctx;
  if(work.r1.u32<256) throw std::runtime_error("invalid native main-state stack");
  work.r1.u64=work.r1.u32-256;
  REX_STORE_U32(work.r1.u32,ctx.r1.u32);
  const auto output=work.r1.u32+80;
  REX_STORE_U32(output+4,UINT32_MAX); REX_STORE_U32(output+8,UINT32_MAX);
  const auto vertex=REX_LOAD_U32(device+12420);
  const auto pixel=REX_LOAD_U32(device+12416);
  const auto declaration=REX_LOAD_U32(device+11536);
  uint32_t variant=0,vertex_meta=vertex+REX_LOAD_U32(vertex+896)+872;
  uint32_t pixel_meta=0,pixel_first=0,pixel_second=0;
  uint32_t tag_difference;
  constexpr uint64_t vertex_dirty=uint64_t(1)<<51,pixel_dirty=uint64_t(1)<<52;
  if(!pixel) {
    if(REX_LOAD_U32(vertex+872)&0x20) {
      variant=1; vertex_meta=vertex+REX_LOAD_U32(vertex+904)+872;
    }
    const auto state=REX_LOAD_U32(device+10452);
    if((state&7)!=5) {
      dirty|=vertex_dirty|8;
      REX_STORE_U32(device+10452,(state&~7u)|5);
    }
    tag_difference=REX_LOAD_U32(vertex_meta+20)&31;
  } else {
    pixel_meta=pixel+REX_LOAD_U32(pixel+64)+40;
    const auto flags=REX_LOAD_U8(device+10810);
    pixel_first=REX_LOAD_U32(pixel_meta+8);
    pixel_second=REX_LOAD_U32(pixel_meta+12);
    REX_STORE_U8(device+10810,flags|((pixel_first&0x70000000u)>0x10000000u?8:0));
    if(dirty&pixel_dirty) {
      dirty|=uint64_t(1)<<46;
      const auto state=REX_LOAD_U32(device+10452);
      REX_STORE_U32(device+10408,REX_LOAD_U32(pixel_meta+24));
      if((state&7)!=4) {
        dirty|=vertex_dirty|8;
        REX_STORE_U32(device+10452,(state&~7u)|4);
      }
    }
    tag_difference=(REX_LOAD_U32(pixel_meta+20)^REX_LOAD_U32(vertex_meta+20))&0x7ffff;
  }
  const auto state=REX_LOAD_U32(device+10452);
  const auto vertex_second=REX_LOAD_U32(vertex_meta+12);
  auto render_state=state;
  REX_STORE_U32(output,REX_LOAD_U32(vertex_meta+8));
  if(REX_LOAD_U8(device+10808)&0x40) {
    if(!pixel_meta || (!(REX_LOAD_U32(device+10428)&8) && !(REX_LOAD_U32(pixel_meta+28)&0x30)))
      render_state=(render_state&~7u)|5;
    if(render_state!=REX_LOAD_U32(device+12424) || (dirty&8)) {
      dirty|=8; REX_STORE_U32(device+12424,render_state);
    }
  }
  const auto upload=[&](uint32_t mode) {
    auto call=work;
    call.r3.u64=device; call.r4.u64=mode; call.r5.u64=vertex; call.r6.u64=output;
    call.r7.u64=declaration; call.r8.u64=vertex_meta; call.r9.u64=pixel_meta;
    call.r10.u64=variant; call.lr=0x8213F070;
    __imp__edf_native_shader_upload_cpu_tail(call,base);
  };
  const auto cache=[&](uint32_t selected) {
    auto call=work;
    call.r3.u64=device; call.r4.u64=vertex; call.r5.u64=declaration; call.r6.u64=selected;
    call.lr=selected==variant?0x8213EF84:0x8213EFE4;
    __imp__edf_native_shader_cache_cpu_tail(call,base);
    return call.r3.u32!=0;
  };
  const auto flags=REX_LOAD_U8(device+10810);
  if(!(flags&0x80) && !tag_difference) {
    if(dirty&vertex_dirty) {
      REX_STORE_U8(device+10810,REX_LOAD_U8(device+10810)&0xbf);
      if(declaration && !(REX_LOAD_U32(vertex+872)&0x40)) {
        if(REX_LOAD_U8(device+10808)&0x80) upload(0);
        else {
          if(!cache(variant)) upload(0);
          if(((render_state^state)&7) && (REX_LOAD_U32(vertex+872)&0x20) && cache(1))
            REX_STORE_U8(device+10810,REX_LOAD_U8(device+10810)|0x40);
        }
      } else if(((render_state^state)&7) && (REX_LOAD_U32(vertex+872)&0x20)) {
        REX_STORE_U8(device+10810,REX_LOAD_U8(device+10810)|0x40);
      }
    }
  } else if(dirty&(vertex_dirty|pixel_dirty)) {
    REX_STORE_U8(device+10810,flags&0xbf);
    upload(tag_difference!=0);
  }
  if(dirty&(vertex_dirty|pixel_dirty)) {
    REX_STORE_U32(device+10404,vertex_second|pixel_second);
    dirty|=uint64_t(3)<<47;
    REX_STORE_U32(device+10400,REX_LOAD_U32(output)|pixel_first);
  }
  if(REX_LOAD_U8(device+10808)&0x40) {
    dirty&=~uint64_t(8);
    if(dirty&(uint64_t(1)<<48)) dirty&=~(uint64_t(3)<<47);
  }
  if(dirty&(uint64_t(1)<<49)) {
    auto call=work; call.r3.u64=device; call.r4.u64=dirty; call.lr=0x8213F368;
    __imp__edf_native_derived_cpu_tail(call,base);
    dirty=call.r3.u64;
  }
  ctx.r3.u64=dirty;
}

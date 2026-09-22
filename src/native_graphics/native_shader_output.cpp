#include "edf2017_pch.h"

// Native layouts replace Xbox output-link microcode edits. The surviving CPU
// output is the interpolator count nibble; do not walk patch records merely to
// discard their packet-only work.
DEFINE_REX_FUNC(edf_native_shader_output_cpu_tail) {
  const auto vertex=ctx.r4.u32,pixel=ctx.r7.u32,output=ctx.r6.u32;
  if((REX_LOAD_U8(vertex+8)&7)==7 || (REX_LOAD_U32(vertex+20)&0x40000)) return;
  if(pixel && (REX_LOAD_U32(pixel+20)&0x20000)) return;
  uint32_t count=pixel?(REX_LOAD_U32(pixel+20)&31):0;
  if(count) --count;
  const auto previous=REX_LOAD_U32(output);
  REX_STORE_U32(output,(previous&~0xf00000u)|((count<<20)&0xf00000u));
}

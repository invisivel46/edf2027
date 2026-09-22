#include "edf2017_pch.h"
REX_EXTERN(__imp__edf_native_shader_output_cpu_tail);

// No guest code allocation, copy, patch or upload is owed by a native shader.
// Preserve the CPU output summary, mode bit and cached input signatures.
DEFINE_REX_FUNC(edf_native_shader_upload_cpu_tail) {
  const auto device=ctx.r3.u32,mode=ctx.r4.u32;
  if(mode) {
    auto work=ctx;
    work.r4.u64=ctx.r8.u32; work.r7.u64=ctx.r9.u32;
    // Output helper uses r6 as its CPU result address and no guest stack.
    __imp__edf_native_shader_output_cpu_tail(work,base);
  }
  const auto flags=REX_LOAD_U8(device+10810);
  REX_STORE_U8(device+10810,(flags&0x7f)|(mode?0x80:0));
  const auto first=REX_LOAD_U64(device+12256);
  const auto second=REX_LOAD_U64(device+12264);
  REX_STORE_U64(device+11552,first);
  REX_STORE_U64(device+11560,second);
}

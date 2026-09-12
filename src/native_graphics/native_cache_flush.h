#pragma once
#include <cstdint>

namespace edf::native {
// 82141AB8: the recompiler emits no host operation for dcbf/sync. Preserve
// its CPU-visible endpoint, including scratch and volatile registers, without
// iterating over guest cache lines. This is not geometry write notification.
template<class Context,class Reader>
void NativeCacheFlushCpu(Context& ctx,const Reader& reader) {
  ctx.r11.s64=133103616;
  ctx.r10.u64=ctx.r3.u64-2131755008ull;
  ctx.r11.u64|=65535;
  ctx.cr6.template compare<uint32_t>(ctx.r10.u32,ctx.r11.u32,ctx.xer);
  if(!ctx.cr6.gt) return;

  const uint32_t begin=ctx.r3.u32&0xffffff80u;
  const uint32_t end=(ctx.r4.u32+127u)&0xffffff80u;
  // Original signed arithmetic shift followed by addze. The difference is
  // aligned to 128, so its discarded bits and addze carry are always zero.
  const uint32_t difference=end-begin;
  const uint32_t lines=(difference>>7)|((difference&0x80000000u)?0xfe000000u:0u);
  const uint32_t groups=lines>>3,remainder=lines&7;
  ctx.r8.u64=remainder;
  reader.StoreWord(ctx.r1.u32-8,remainder);
  ctx.r9.u64=0;
  ctx.xer.ca=groups!=0; // Last addic in a nonempty eight-line loop.
  ctx.cr0.template compare<int32_t>(0,0,ctx.xer);
  ctx.r11.u64=uint64_t(begin)+uint64_t(groups)*1024;
  reader.StoreWord(ctx.r1.u32-16,ctx.r11.u32);
  ctx.r10.u64=0;
  ctx.cr6.template compare<uint32_t>(remainder,0,ctx.xer);
  reader.StoreWord(ctx.r1.u32-12,0);
  if(remainder) {
    ctx.r10.u64=remainder;
    ctx.r11.u64+=uint64_t(remainder)*128;
    ctx.cr6.template compare<uint32_t>(remainder,remainder,ctx.xer);
    reader.StoreWord(ctx.r1.u32-12,remainder);
    reader.StoreWord(ctx.r1.u32-16,ctx.r11.u32);
  }
}
}

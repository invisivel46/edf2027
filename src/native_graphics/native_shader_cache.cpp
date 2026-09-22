// CPU completion formerly performed by sub_8213EB68. This entry is used only
// by the packet-free native draw chain; the retail shader patcher stays retail.
#include "edf2017_pch.h"

namespace {
uint32_t AllocateDeclarationId(uint8_t* base) {
  constexpr uint32_t counter=0x82578d0c;
  // Share the guest counter and synchronization domain with retail callers.
  // UINT32_MAX and zero are reserved; increment again across either value.
  for(;;) {
    REX_ENTER_GLOBAL_LOCK();
    auto* address=reinterpret_cast<uint32_t*>(REX_RAW_ADDR(counter));
    const auto previous=*address;
    const uint32_t next=__builtin_bswap32(previous)+1u;
    const bool exchanged=__sync_bool_compare_and_swap(address,previous,__builtin_bswap32(next));
    REX_LEAVE_GLOBAL_LOCK();
    if(exchanged && uint32_t(next+1u)>=2u) return next;
  }
}

bool CompleteShaderCache(uint8_t* base,uint32_t device,uint32_t program,
                         uint32_t declaration,uint32_t variant) {
  // Keep metadata in guest-owned storage: fallback sees the same generation,
  // signature and fence tag. No host map survives retirement/address reuse.
  const uint32_t record=program+variant*416u;
  bool matched=REX_LOAD_U32(declaration+48)==REX_LOAD_U32(record+40);
  if(matched) {
    const auto first=REX_LOAD_U64(record+48);
    const auto second=REX_LOAD_U64(record+56);
    const auto current_second=REX_LOAD_U64(device+12264);
    const auto first_mask=REX_LOAD_U64(declaration+32);
    const auto second_mask=REX_LOAD_U64(declaration+40);
    const auto current_first=REX_LOAD_U64(device+12256);
    matched=(((first^current_first)&first_mask)|((second^current_second)&second_mask))==0;
  }
  if(!matched) {
    const auto used=REX_LOAD_U32(record+64);
    if(used) {
      const auto completed_address=REX_LOAD_U32(device+10768);
      const auto current=REX_LOAD_U32(device+10780);
      const auto completed=REX_LOAD_U32(completed_address);
      // Unsigned modular distances reproduce the guest wraparound comparison.
      if(uint32_t(current-used)<uint32_t(current-completed)) return false;
    }
    // Native geometry/shaders need no Xbox microcode patch. Its CPU metadata
    // completion is still owed. Preserve reload/store order for aliased inputs.
    if(!REX_LOAD_U32(declaration+48))
      REX_STORE_U32(declaration+48,AllocateDeclarationId(base));
    const auto id=REX_LOAD_U32(declaration+48);
    REX_STORE_U32(record+40,id);
    const auto first=REX_LOAD_U64(device+12256);
    REX_STORE_U64(record+48,first);
    const auto second=REX_LOAD_U64(device+12264);
    REX_STORE_U64(record+56,second);
  }
  const auto current=REX_LOAD_U32(device+10780);
  REX_STORE_U32(record+64,current);
  const auto first=REX_LOAD_U64(device+12256);
  const auto second=REX_LOAD_U64(device+12264);
  REX_STORE_U64(device+11552,first);
  REX_STORE_U64(device+11560,second);
  return true;
}
}

// Preserve the existing native-chain ABI name; this is now authored C++, not
// extracted PPC execution. Only the return register changes; nonvolatiles,
// stack, LR and MSR remain caller-owned. Stack scratch bytes are not an output.
DEFINE_REX_FUNC(edf_native_shader_cache_cpu_tail) {
  ctx.r3.u64=CompleteShaderCache(base,ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32)?1:0;
}

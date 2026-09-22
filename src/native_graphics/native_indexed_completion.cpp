#include "edf2017_pch.h"
#include "native_indexed_completion.h"

REX_EXTERN(__imp__edf_native_main_state_cpu_tail);
namespace {
struct IndexedMemory {
  uint8_t* base;
  uint64_t Wide(uint32_t address) const { return REX_LOAD_U64(address); }
  void StoreWide(uint32_t address,uint64_t value) const { REX_STORE_U64(address,value); }
};
}

// Keep the established bridge ABI. No guest geometry or packet storage is read.
// Work context isolates helper register mutations and exceptions from the caller.
DEFINE_REX_FUNC(edf_native_indexed_cpu_tail) {
  const auto device=ctx.r3.u32;
  edf::native::CompleteNativeIndexedState(IndexedMemory{base},device,[&](uint64_t dirty) {
    auto work=ctx;
    if(work.r1.u32<240) throw std::runtime_error("invalid native indexed completion stack");
    // Preserve the helper's original stack position and caller attribution.
    work.r1.u64=work.r1.u32-240;
    REX_STORE_U32(work.r1.u32,ctx.r1.u32);
    work.r3.u64=device; work.r4.u64=dirty; work.lr=0x821FE3F4;
    __imp__edf_native_main_state_cpu_tail(work,base);
  });
}

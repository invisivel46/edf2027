#pragma once
// Timing-only codegen overhead experiments (docs/codegen-overhead.md).
//
// Appended after the generated edf2017_pch.h to the recompiled-code target only
// when EDF_CODEGEN_EXPERIMENT is set (CMakeLists.txt); the default build never
// sees this file. Each macro below is an UPPER-BOUND experiment: it removes a
// piece of generic translation overhead to measure what that overhead costs.
// Several are NOT exact (they can change guest results); none may ship as is.
//
//   EDF_EXP_NO_VOLATILE  guest loads/stores through plain (aligned(1)) pointers
//                        instead of volatile ones: lets clang merge, forward and
//                        drop guest memory accesses. Unsafe for guest memory that
//                        another guest/host thread writes concurrently.
//   EDF_EXP_NO_PHYS      REX_PHYS_HOST_OFFSET compiled to 0: every guest access
//                        above 0xE0000000 (physical/GPU memory) lands 4 KB off.
//                        Wrong results for such accesses; timing only.
//   EDF_EXP_NO_FLUSH     ctx.fpscr.{enable,disable}FlushMode() compiled out: the
//                        host MXCSR flush/denormal mode is left as it is.
//                        Denormal results may change; timing only.

#if defined(EDF_EXP_NO_VOLATILE)
namespace edf_exp {
typedef u16 __attribute__((aligned(1), may_alias)) u16u;
typedef u32 __attribute__((aligned(1), may_alias)) u32u;
typedef u64 __attribute__((aligned(1), may_alias)) u64u;
}
#undef REX_LOAD_U8
#undef REX_LOAD_U16
#undef REX_LOAD_U32
#undef REX_LOAD_U64
#undef REX_STORE_U8
#undef REX_STORE_U16
#undef REX_STORE_U32
#undef REX_STORE_U64
#define REX_LOAD_U8(x) (*(u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOAD_U16(x) __builtin_bswap16(*(edf_exp::u16u*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOAD_U32(x) __builtin_bswap32(*(edf_exp::u32u*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOAD_U64(x) __builtin_bswap64(*(edf_exp::u64u*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_STORE_U8(x, y) (*(u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = (y))
#define REX_STORE_U16(x, y) (*(edf_exp::u16u*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap16(y))
#define REX_STORE_U32(x, y) (*(edf_exp::u32u*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap32(y))
#define REX_STORE_U64(x, y) (*(edf_exp::u64u*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap64(y))
#endif

#if defined(EDF_EXP_NO_PHYS)
#undef REX_PHYS_HOST_OFFSET
#define REX_PHYS_HOST_OFFSET(addr) 0u
#endif

#if defined(EDF_EXP_NO_FLUSH)
// `ctx.fpscr.disableFlushMode();` becomes `ctx.fpscr.csr;` (a discarded read).
#pragma clang diagnostic ignored "-Wunused-value"
#define disableFlushMode() csr
#define enableFlushMode() csr
#endif

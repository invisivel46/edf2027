// Start-up CPU requirement check for builds compiled with -march=x86-64-v3.
//
// cpu_check_logic.cpp decides, from CPUID and XGETBV results, which x86-64-v3
// features are missing; cpu_check_entry.cpp is the executable's entry point
// and runs that decision on the real CPU before the C runtime starts (so before
// any static initializer of v3-compiled code). Both files belong to the
// edf_cpu_check object library, which never receives the v3 flags.
//
// Keep both files free of inline and template functions from shared headers:
// an inline function also instantiated by a v3 translation unit becomes a
// COMDAT, and the linker may keep the v3 copy.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace edf::cpu_check {

struct CpuidRegs {
  uint32_t eax, ebx, ecx, edx;
};

// Where CPUID and XGETBV results come from: the real instructions in the
// executable, a table in the tests.
struct CpuProbe {
  CpuidRegs (*cpuid)(void* context, uint32_t leaf, uint32_t subleaf);
  // XCR0. Only called once CPUID reports OSXSAVE; XGETBV faults otherwise.
  uint64_t (*xgetbv0)(void* context);
  void* context;
};

// One bit per requirement; the v2 subset is what x86-64-v3 builds on.
enum Feature : uint32_t {
  kSse3 = 1u << 0,
  kSsse3 = 1u << 1,
  kSse41 = 1u << 2,
  kSse42 = 1u << 3,
  kPopcnt = 1u << 4,
  kCx16 = 1u << 5,
  kLahfSahf = 1u << 6,
  kAvx = 1u << 7,
  kAvx2 = 1u << 8,
  kFma = 1u << 9,
  kBmi1 = 1u << 10,
  kBmi2 = 1u << 11,
  kLzcnt = 1u << 12,
  kMovbe = 1u << 13,
  kF16c = 1u << 14,
  // XSAVE + OSXSAVE and XCR0 enabling SSE and AVX (YMM) state: without it the
  // OS does not preserve YMM registers, so AVX instructions fault.
  kOsAvxState = 1u << 15,
};
inline constexpr uint32_t kFeatureCount = 16;
inline constexpr uint32_t kX86_64V3Features = (1u << kFeatureCount) - 1;

// The Feature bits of kX86_64V3Features this CPU lacks; 0 when it runs v3 code.
uint32_t MissingX86_64V3Features(const CpuProbe& probe);

// Short name of one Feature bit ("AVX2"), or "?" for anything else.
const char* FeatureName(uint32_t feature);

// Writes the names of the set bits as "AVX2, FMA, BMI2" into out, always
// NUL-terminated when capacity > 0, truncating if needed. Returns the length.
size_t FormatFeatureNames(uint32_t mask, char* out, size_t capacity);

}  // namespace edf::cpu_check

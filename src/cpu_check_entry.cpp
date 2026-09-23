// The executable's entry point (/ENTRY:edf_cpu_check_entry). It runs before
// the C runtime's own entry point, so before any static initializer, and in a
// build compiled with -march=x86-64-v3 refuses to start on a CPU that cannot
// execute that code, instead of dying with an illegal instruction.
//
// This file must stay baseline x86-64: it is compiled in the edf_cpu_check
// object library, which cmake/edf_optimization.cmake never gives the v3 flags,
// and the #error below fails the build if it ever is. It calls only Win32 APIs
// and cpu_check_logic.cpp; the CRT is not initialized yet.
#if defined(__AVX__) || defined(__AVX2__) || defined(__FMA__) || defined(__BMI2__) || defined(__F16C__)
#error "cpu_check_entry.cpp must be compiled for baseline x86-64, without -march=x86-64-v3"
#endif
#ifndef EDF_CPU_CHECK_REQUIRE_X86_64_V3
#error "EDF_CPU_CHECK_REQUIRE_X86_64_V3 must be defined to 0 or 1 (CMakeLists.txt)"
#endif

#include "cpu_check.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#if EDF_CPU_CHECK_REQUIRE_X86_64_V3 && (defined(_M_X64) || defined(__x86_64__))
#define EDF_CPU_CHECK_ACTIVE 1
#include <intrin.h>
#else
#define EDF_CPU_CHECK_ACTIVE 0
#endif

namespace {

#if EDF_CPU_CHECK_ACTIVE
using edf::cpu_check::CpuidRegs;

CpuidRegs RealCpuid(void*, uint32_t leaf, uint32_t subleaf) {
  int regs[4];
  __cpuidex(regs, static_cast<int>(leaf), static_cast<int>(subleaf));
  return {static_cast<uint32_t>(regs[0]), static_cast<uint32_t>(regs[1]),
          static_cast<uint32_t>(regs[2]), static_cast<uint32_t>(regs[3])};
}

// Inline asm rather than _xgetbv, which clang only allows in functions
// compiled with the xsave target feature.
uint64_t RealXgetbv0(void*) {
  uint32_t low, high;
  __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
  return (static_cast<uint64_t>(high) << 32) | low;
}

void Append(const char* text, char* out, size_t capacity, size_t& length) {
  for (; *text != 0 && length + 1 < capacity; ++text) out[length++] = *text;
  out[length] = 0;
}

void RequireX86_64V3() {
  const edf::cpu_check::CpuProbe probe{&RealCpuid, &RealXgetbv0, nullptr};
  const uint32_t missing = edf::cpu_check::MissingX86_64V3Features(probe);
  if (missing == 0) return;

  char names[256];
  edf::cpu_check::FormatFeatureNames(missing, names, sizeof(names));
  char text[512];
  size_t length = 0;
  Append("This build requires a CPU with AVX2/FMA (Intel Haswell / AMD Zen or newer).\n\n"
         "This CPU or operating system lacks: ",
         text, sizeof(text), length);
  Append(names, text, sizeof(text), length);
  Append(".", text, sizeof(text), length);
  MessageBoxA(nullptr, text, "EDF2027 - unsupported CPU", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
  ExitProcess(1);
}
#endif

}  // namespace

// The CRT entry point for a wWinMain executable (vcruntime exe_wwinmain.cpp);
// the ReXGlue SDK supplies wWinMain.
extern "C" DWORD wWinMainCRTStartup(LPVOID);

extern "C" DWORD edf_cpu_check_entry(LPVOID peb) {
#if EDF_CPU_CHECK_ACTIVE
  RequireX86_64V3();
#endif
  return wWinMainCRTStartup(peb);
}

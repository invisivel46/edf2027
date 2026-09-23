// x86-64-v3 feature decision (see cpu_check.h). Portable, no C runtime calls:
// it runs from the entry point before the CRT is initialized.
#include "cpu_check.h"

namespace edf::cpu_check {
namespace {

constexpr uint32_t Bit(uint32_t n) { return 1u << n; }

struct Requirement {
  uint32_t feature;
  const char* name;
};

constexpr Requirement kRequirements[kFeatureCount] = {
    {kSse3, "SSE3"},   {kSsse3, "SSSE3"}, {kSse41, "SSE4.1"},     {kSse42, "SSE4.2"},
    {kPopcnt, "POPCNT"}, {kCx16, "CMPXCHG16B"}, {kLahfSahf, "LAHF/SAHF"}, {kAvx, "AVX"},
    {kAvx2, "AVX2"},   {kFma, "FMA"},     {kBmi1, "BMI1"},        {kBmi2, "BMI2"},
    {kLzcnt, "LZCNT"}, {kMovbe, "MOVBE"}, {kF16c, "F16C"},        {kOsAvxState, "OS AVX support"},
};

void Append(const char* text, char* out, size_t capacity, size_t& length) {
  for (; *text != 0 && length + 1 < capacity; ++text) out[length++] = *text;
}

}  // namespace

uint32_t MissingX86_64V3Features(const CpuProbe& probe) {
  uint32_t present = 0;
  const CpuidRegs leaf0 = probe.cpuid(probe.context, 0, 0);
  const uint32_t max_leaf = leaf0.eax;

  if (max_leaf >= 1) {
    const uint32_t ecx = probe.cpuid(probe.context, 1, 0).ecx;
    if (ecx & Bit(0)) present |= kSse3;
    if (ecx & Bit(9)) present |= kSsse3;
    if (ecx & Bit(12)) present |= kFma;
    if (ecx & Bit(13)) present |= kCx16;
    if (ecx & Bit(19)) present |= kSse41;
    if (ecx & Bit(20)) present |= kSse42;
    if (ecx & Bit(22)) present |= kMovbe;
    if (ecx & Bit(23)) present |= kPopcnt;
    if (ecx & Bit(28)) present |= kAvx;
    if (ecx & Bit(29)) present |= kF16c;
    const bool xsave = (ecx & Bit(26)) != 0;
    const bool osxsave = (ecx & Bit(27)) != 0;
    if (xsave && osxsave) {
      // XCR0 bit 1: SSE (XMM) state, bit 2: AVX (upper YMM) state.
      const uint64_t xcr0 = probe.xgetbv0(probe.context);
      if ((xcr0 & 0x6) == 0x6) present |= kOsAvxState;
    }
  }
  if (max_leaf >= 7) {
    const uint32_t ebx = probe.cpuid(probe.context, 7, 0).ebx;
    if (ebx & Bit(3)) present |= kBmi1;
    if (ebx & Bit(5)) present |= kAvx2;
    if (ebx & Bit(8)) present |= kBmi2;
  }
  const uint32_t max_extended_leaf = probe.cpuid(probe.context, 0x80000000u, 0).eax;
  if (max_extended_leaf >= 0x80000001u) {
    const uint32_t ecx = probe.cpuid(probe.context, 0x80000001u, 0).ecx;
    if (ecx & Bit(0)) present |= kLahfSahf;
    if (ecx & Bit(5)) present |= kLzcnt;  // ABM / LZCNT
  }
  return kX86_64V3Features & ~present;
}

const char* FeatureName(uint32_t feature) {
  for (const Requirement& requirement : kRequirements) {
    if (requirement.feature == feature) return requirement.name;
  }
  return "?";
}

size_t FormatFeatureNames(uint32_t mask, char* out, size_t capacity) {
  if (capacity == 0) return 0;
  size_t length = 0;
  bool first = true;
  for (const Requirement& requirement : kRequirements) {
    if ((mask & requirement.feature) == 0) continue;
    if (!first) Append(", ", out, capacity, length);
    Append(requirement.name, out, capacity, length);
    first = false;
  }
  out[length] = '\0';
  return length;
}

}  // namespace edf::cpu_check

// Start-up CPU check (cpu_check.h): the x86-64-v3 feature decision against a
// fake CPUID / XGETBV, including the leaf-range and OSXSAVE guards.
#include "cpu_check.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <utility>

namespace {
int failures = 0;
void Check(bool condition, const char* expression, int line) {
  if (!condition) {
    std::cerr << "line " << line << ": CHECK(" << expression << ") failed\n";
    ++failures;
  }
}
#define CHECK(value) Check(static_cast<bool>(value), #value, __LINE__)

using namespace edf::cpu_check;

constexpr uint32_t Bit(uint32_t n) { return 1u << n; }

// Leaf 1 ECX bits of a Haswell: SSE3, SSSE3, FMA, CX16, SSE4.1, SSE4.2,
// MOVBE, POPCNT, XSAVE, OSXSAVE, AVX, F16C.
constexpr uint32_t kHaswellLeaf1Ecx = Bit(0) | Bit(9) | Bit(12) | Bit(13) | Bit(19) | Bit(20) |
                                      Bit(22) | Bit(23) | Bit(26) | Bit(27) | Bit(28) | Bit(29);
constexpr uint32_t kHaswellLeaf7Ebx = Bit(3) | Bit(5) | Bit(8);  // BMI1, AVX2, BMI2
constexpr uint32_t kHaswellExtEcx = Bit(0) | Bit(5);             // LAHF/SAHF, LZCNT

struct FakeCpu {
  std::map<std::pair<uint32_t, uint32_t>, CpuidRegs> leaves;
  uint64_t xcr0 = 0x7;  // x87, SSE, AVX state
  int xgetbv_calls = 0;
  bool queried_out_of_range = false;

  static CpuidRegs Cpuid(void* context, uint32_t leaf, uint32_t subleaf) {
    auto& cpu = *static_cast<FakeCpu*>(context);
    const uint32_t max = leaf >= 0x80000000u ? cpu.leaves[{0x80000000u, 0}].eax : cpu.leaves[{0, 0}].eax;
    if (leaf != 0 && leaf != 0x80000000u && leaf > max) {
      // A real CPU answers with some other leaf's data; make it look fully capable.
      cpu.queried_out_of_range = true;
      return {~0u, ~0u, ~0u, ~0u};
    }
    auto it = cpu.leaves.find({leaf, subleaf});
    return it == cpu.leaves.end() ? CpuidRegs{0, 0, 0, 0} : it->second;
  }
  static uint64_t Xgetbv0(void* context) {
    auto& cpu = *static_cast<FakeCpu*>(context);
    ++cpu.xgetbv_calls;
    return cpu.xcr0;
  }
  uint32_t Missing() { return MissingX86_64V3Features(CpuProbe{&Cpuid, &Xgetbv0, this}); }
};

FakeCpu Haswell() {
  FakeCpu cpu;
  cpu.leaves[{0, 0}] = {0xD, 0, 0, 0};
  cpu.leaves[{1, 0}] = {0, 0, kHaswellLeaf1Ecx, 0};
  cpu.leaves[{7, 0}] = {0, kHaswellLeaf7Ebx, 0, 0};
  cpu.leaves[{0x80000000u, 0}] = {0x80000008u, 0, 0, 0};
  cpu.leaves[{0x80000001u, 0}] = {0, 0, kHaswellExtEcx, 0};
  return cpu;
}

void TestHaswellPasses() {
  FakeCpu cpu = Haswell();
  CHECK(cpu.Missing() == 0);
  CHECK(cpu.xgetbv_calls == 1);
  CHECK(!cpu.queried_out_of_range);
}

void TestEachCpuidBitIsRequired() {
  struct Case {
    uint32_t leaf, bit;
    int reg;  // 1 = ebx, 2 = ecx
    uint32_t expected;
  };
  const Case cases[] = {
      {1, 0, 2, kSse3},       {1, 9, 2, kSsse3},      {1, 12, 2, kFma},   {1, 13, 2, kCx16},
      {1, 19, 2, kSse41},     {1, 20, 2, kSse42},     {1, 22, 2, kMovbe}, {1, 23, 2, kPopcnt},
      {1, 28, 2, kAvx},       {1, 29, 2, kF16c},      {7, 3, 1, kBmi1},   {7, 5, 1, kAvx2},
      {7, 8, 1, kBmi2},       {0x80000001u, 0, 2, kLahfSahf},             {0x80000001u, 5, 2, kLzcnt},
      {1, 26, 2, kOsAvxState},  // XSAVE
      {1, 27, 2, kOsAvxState},  // OSXSAVE
  };
  for (const Case& c : cases) {
    FakeCpu cpu = Haswell();
    CpuidRegs& regs = cpu.leaves[{c.leaf, 0}];
    (c.reg == 1 ? regs.ebx : regs.ecx) &= ~Bit(c.bit);
    const uint32_t missing = cpu.Missing();
    if (missing != c.expected) {
      std::cerr << "leaf " << std::hex << c.leaf << " bit " << std::dec << c.bit << ": missing 0x" << std::hex
                << missing << ", expected 0x" << c.expected << std::dec << "\n";
      ++failures;
    }
  }
}

void TestSandyBridgeFails() {
  // AVX with OS support, but no FMA, F16C, MOVBE, AVX2, BMI1/2 or LZCNT.
  FakeCpu cpu = Haswell();
  cpu.leaves[{1, 0}].ecx &= ~(Bit(12) | Bit(22) | Bit(29));
  cpu.leaves[{7, 0}].ebx = 0;
  cpu.leaves[{0x80000001u, 0}].ecx &= ~Bit(5);
  CHECK(cpu.Missing() == (kFma | kF16c | kMovbe | kAvx2 | kBmi1 | kBmi2 | kLzcnt));
}

void TestOsWithoutAvxState() {
  // The CPU has everything, but the OS does not save YMM state (XCR0 bit 2).
  FakeCpu cpu = Haswell();
  cpu.xcr0 = 0x3;
  CHECK(cpu.Missing() == kOsAvxState);
  cpu.xcr0 = 0x5;  // no SSE state
  CHECK(cpu.Missing() == kOsAvxState);
}

void TestNoXgetbvWithoutOsxsave() {
  // XGETBV faults when CR4.OSXSAVE is clear: it must not be executed.
  FakeCpu cpu = Haswell();
  cpu.leaves[{1, 0}].ecx &= ~Bit(27);
  CHECK(cpu.Missing() == kOsAvxState);
  CHECK(cpu.xgetbv_calls == 0);
}

void TestLeafRangesAreRespected() {
  // Max basic leaf 1: leaf 7 is not queried and its features count as missing.
  FakeCpu cpu = Haswell();
  cpu.leaves[{0, 0}].eax = 1;
  CHECK(cpu.Missing() == (kAvx2 | kBmi1 | kBmi2));
  CHECK(!cpu.queried_out_of_range);

  // No extended leaf 0x80000001.
  cpu = Haswell();
  cpu.leaves[{0x80000000u, 0}].eax = 0x80000000u;
  CHECK(cpu.Missing() == (kLahfSahf | kLzcnt));
  CHECK(!cpu.queried_out_of_range);

  // No leaves at all beyond 0: everything is missing, nothing else is read.
  FakeCpu empty;
  CHECK(empty.Missing() == kX86_64V3Features);
  CHECK(empty.xgetbv_calls == 0);
  CHECK(!empty.queried_out_of_range);
}

void TestFeatureNames() {
  char buffer[128];
  CHECK(FormatFeatureNames(0, buffer, sizeof(buffer)) == 0 && buffer[0] == 0);
  const size_t length = FormatFeatureNames(kAvx2 | kFma | kOsAvxState, buffer, sizeof(buffer));
  CHECK(std::string(buffer) == "AVX2, FMA, OS AVX support");
  CHECK(length == std::strlen(buffer));

  // Truncation keeps the terminator inside the buffer.
  char small[8];
  std::memset(small, 'x', sizeof(small));
  CHECK(FormatFeatureNames(kAvx2 | kFma, small, sizeof(small)) == 7);
  CHECK(std::string(small) == "AVX2, F");
  CHECK(FormatFeatureNames(kAvx2, small, 0) == 0);

  // Every feature bit has a real name.
  for (uint32_t i = 0; i < kFeatureCount; ++i) CHECK(std::string(FeatureName(1u << i)) != "?");
  CHECK(std::string(FeatureName(1u << kFeatureCount)) == "?");
}

}  // namespace

int main() {
  TestHaswellPasses();
  TestEachCpuidBitIsRequired();
  TestSandyBridgeFails();
  TestOsWithoutAvxState();
  TestNoXgetbvWithoutOsxsave();
  TestLeafRangesAreRespected();
  TestFeatureNames();
  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "cpu_check_tests: all passed\n";
  return 0;
}

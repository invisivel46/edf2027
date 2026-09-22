// Runs exact generated guest bodies, with a separate bit-by-bit contract oracle.
#include "edf2017_pch.h"
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

struct GetterCase {
  const char* name;
  void (*original)(PPCContext&, uint8_t*);
  int displacement;
  unsigned load_register, shift, mask_begin, mask_end;
  bool holdout;
};
static_assert(std::is_trivially_copyable_v<PPCContext>);

#include "renderer_scalar_original_fixture.inc"

namespace {
constexpr size_t MemorySize = 0x6000;
using Memory = std::array<uint8_t, MemorySize>;

void Require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

// Does not use the generator's duplicated-word rotate expression or a mask table.
uint32_t Evaluate(uint32_t word, unsigned shift, unsigned begin, unsigned end) {
  uint32_t result = 0;
  for (unsigned bit = 0; bit < 32; ++bit) {
    const unsigned ppc_bit = 31 - bit;
    const bool included = begin <= end ? begin <= ppc_bit && ppc_bit <= end
                                      : ppc_bit >= begin || ppc_bit <= end;
    const unsigned input_bit = (bit + 32 - shift) % 32;
    if (included && ((word >> input_bit) & 1u)) result |= uint32_t{1} << bit;
  }
  return result;
}

uint32_t EffectiveAddress(const GetterCase& getter, const PPCContext& context) {
  const uint32_t address = context.r3.u32 + static_cast<uint32_t>(getter.displacement);
  Require(address % 4 == 0 && uint64_t(address) + 4 <= MemorySize,
          "Invocation outside aligned ordinary-memory precondition");
  return address;
}

void StoreBE(Memory& memory, uint32_t address, uint32_t word) {
  for (unsigned i = 0; i < 4; ++i) memory[address + i] = uint8_t(word >> (24 - 8 * i));
}

PPCContext Expected(const GetterCase& getter, const PPCContext& initial, const Memory& memory) {
  PPCContext expected;
  std::memcpy(&expected, &initial, sizeof(expected));
  const auto address = EffectiveAddress(getter, initial);
  uint32_t word = 0;
  for (unsigned i = 0; i < 4; ++i) word = word * 256u + memory[address + i];
  if (getter.load_register == 11) expected.r11.u64 = word;
  expected.r3.u64 = Evaluate(word, getter.shift, getter.mask_begin, getter.mask_end);
  return expected;
}

bool Matches(const PPCContext& actual, const PPCContext& expected,
             const Memory& memory, const Memory& before) {
  return std::memcmp(&actual, &expected, sizeof(actual)) == 0 && memory == before;
}

uint32_t Next(uint32_t& state) {
  state ^= state << 13; state ^= state >> 17; state ^= state << 5;
  return state;
}
}

int main(int argc, char** argv) {
  const auto start = std::chrono::steady_clock::now();
  try {
    Require(argc <= 2, "usage: scalar-contract-tests [report.json]");
    Require(std::size(kGetterCases) == 59, "Frozen pilot must exercise all 59 accepted bodies");
    // Independent known answers cover high bits and wrapping masks, which this corpus lacks.
    Require(Evaluate(0x80000000u, 1, 31, 31) == 1, "32-bit rotation oracle");
    Require(Evaluate(0xffffffffu, 0, 29, 3) == 0xf0000007u, "Wrapping PPC mask oracle");
    Require(Evaluate(0x12345678u, 0, 0, 31) == 0x12345678u, "Identity oracle");
    std::vector<uint32_t> words{0, 0xffffffffu, 0x80000001u, 0xa5a5a5a5u, 0x01234567u, 0x89abcdefu};
    for (unsigned bit = 0; bit < 32; ++bit) {
      words.push_back(uint32_t{1} << bit);
      words.push_back(~(uint32_t{1} << bit));
    }
    uint32_t random_state = 0x20260922u;
    for (unsigned i = 0; i < 128; ++i) words.push_back(Next(random_state));
    size_t invocations = 0, holdout_invocations = 0, holdout_functions = 0;
    size_t controls = 0;
    alignas(32) Memory memory{}, before{};
    for (const GetterCase& getter : kGetterCases) {
      if (getter.holdout) ++holdout_functions;
      for (uint32_t object : {0u, 0x400u, 0x1400u}) {
        for (uint32_t word : words) {
          PPCContext context;
          // Poison every register/control byte; the selected bodies read only GPRs.
          std::memset(&context, uint8_t(Next(random_state)), sizeof(context));
          context.r3.u64 = 0xdeadbeef00000000ull | object;
          memory.fill(uint8_t(Next(random_state)));
          const auto address = EffectiveAddress(getter, context);
          StoreBE(memory, address, word);
          before = memory;
          const PPCContext expected = Expected(getter, context, before);
          getter.original(context, memory.data());
          Require(Matches(context, expected, memory, before),
                  std::string(getter.name) + " guest/contract disagreement for " + std::to_string(word));
          ++invocations;
          if (getter.holdout) ++holdout_invocations;
        }
      }
      // Deliberately corrupt each observable independently: no mutation can hide
      // behind the same final return value or state leaked from another invocation.
      PPCContext context;
      std::memset(&context, 0x5a, sizeof(context));
      context.r3.u64 = 0xcafebabe00000400ull;
      memory.fill(0x9d);
      StoreBE(memory, EffectiveAddress(getter, context), 0x01234567u);
      before = memory;
      const auto expected = Expected(getter, context, before);
      getter.original(context, memory.data());
      context.r3.u64 ^= 1;
      Require(!Matches(context, expected, memory, before), "Return mutation escaped"); ++controls;
      std::memcpy(&context, &expected, sizeof(context)); context.r12.u64 ^= 1;
      Require(!Matches(context, expected, memory, before), "Undeclared register mutation escaped"); ++controls;
      std::memcpy(&context, &expected, sizeof(context)); memory[0x20] ^= 1;
      Require(!Matches(context, expected, memory, before), "Unexpected memory/global write escaped"); ++controls;
      memory = before;
      context.r3.u64 = 0x400;
      const auto correct_endian = Expected(getter, context, memory);
      auto reversed_memory = memory;
      StoreBE(reversed_memory, EffectiveAddress(getter, context), 0x67452301u);
      const auto wrong_endian = Expected(getter, context, reversed_memory);
      getter.original(context, memory.data());
      Require(Matches(context, correct_endian, memory, before), "Endian control baseline failed");
      Require(std::memcmp(&context, &wrong_endian, sizeof(context)) != 0, "Endian mutation escaped"); ++controls;
      context.r3.u64 = 1;
      bool rejected = false;
      try { (void)EffectiveAddress(getter, context); } catch (const std::runtime_error&) { rejected = true; }
      Require(rejected, "Unaligned invocation accepted"); ++controls;
      context.r3.u64 = 0xa0000000u;
      rejected = false;
      try { (void)EffectiveAddress(getter, context); } catch (const std::runtime_error&) { rejected = true; }
      Require(rejected, "Unmapped invocation accepted"); ++controls;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::string report = "{\n  \"passed\": true,\n  \"functions\": 59,\n  \"holdout_functions\": " +
      std::to_string(holdout_functions) + ",\n  \"invocations\": " + std::to_string(invocations) +
      ",\n  \"holdout_invocations\": " + std::to_string(holdout_invocations) +
      ",\n  \"negative_controls\": " + std::to_string(controls) +
      ",\n  \"seconds\": " + std::to_string(seconds) +
      ",\n  \"scope\": \"Actual generated guest getters versus bitwise contract evaluator; ordinary mapped memory only; no native adapter or gameplay\"\n}\n";
    if (argc == 2) { std::ofstream output(argv[1]); output << report; Require(bool(output), "Cannot write report"); }
    std::cout << report;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Scalar contract failure: " << error.what() << '\n';
    return 1;
  }
}

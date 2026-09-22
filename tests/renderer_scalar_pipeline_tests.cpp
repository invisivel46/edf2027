// Three-way scalar pipeline test: unchanged generated code, derived contract,
// and an independent decoder/interpreter over the original raw PowerPC words.
#include "edf2017_pch.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

struct Write {
  uint32_t address;
  unsigned width;
  uint64_t value;
  bool operator==(const Write&) const = default;
};
static std::vector<Write> g_writes;

static void Real8(uint8_t* base, uint32_t address, uint8_t value) { REX_STORE_U8(address, value); }
static void Real32(uint8_t* base, uint32_t address, uint32_t value) { REX_STORE_U32(address, value); }
static void Real64(uint8_t* base, uint32_t address, uint64_t value) { REX_STORE_U64(address, value); }
static void Trace8(uint8_t* base, uint32_t a, uint8_t v) { g_writes.push_back({a, 1, v}); Real8(base, a, v); }
static void Trace32(uint8_t* base, uint32_t a, uint32_t v) { g_writes.push_back({a, 4, v}); Real32(base, a, v); }
static void Trace64(uint8_t* base, uint32_t a, uint64_t v) { g_writes.push_back({a, 8, v}); Real64(base, a, v); }

// The effect-engine fixture calls this for derived stores. Width is in bits.
static void EngineTraceStore(uint8_t* base, uint32_t a, unsigned width, uint64_t v) {
  if (width == 8) Trace8(base, a, uint8_t(v));
  else if (width == 32) Trace32(base, a, uint32_t(v));
  else if (width == 64) Trace64(base, a, v);
  else throw std::runtime_error("Effect engine requested unsupported store width");
}

using EngineFn = void (*)(PPCContext&, uint8_t*);
struct EngineCase {
  const char* name;
  EngineFn original;
  EngineFn derived;
  uint32_t entry;
  const uint32_t* words;
  size_t word_count;
  bool holdout;
  const char* body_sha;
  const char* run_id;
  const char* contract_sha256;
};
static_assert(std::is_trivially_copyable_v<PPCContext>);

#undef REX_STORE_U8
#undef REX_STORE_U32
#undef REX_STORE_U64
#define REX_STORE_U8(x,y) Trace8(base, uint32_t(x), uint8_t(y))
#define REX_STORE_U32(x,y) Trace32(base, uint32_t(x), uint32_t(y))
#define REX_STORE_U64(x,y) Trace64(base, uint32_t(x), uint64_t(y))
#include "renderer_scalar_effect_fixture.inc"

namespace {
constexpr size_t kMemorySize = 0x6000;
using Memory = std::array<uint8_t, kMemorySize>;

void Require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

uint64_t& Reg(PPCContext& c, unsigned n) {
#define R(N) case N: return c.r##N.u64
  switch (n) {
    R(0); R(1); R(2); R(3); R(4); R(5); R(6); R(7);
    R(8); R(9); R(10); R(11); R(12); R(13); R(14); R(15);
    R(16); R(17); R(18); R(19); R(20); R(21); R(22); R(23);
    R(24); R(25); R(26); R(27); R(28); R(29); R(30); R(31);
    default: throw std::runtime_error("Invalid GPR number");
  }
#undef R
}

int32_t Signed16(uint32_t word) { return int16_t(word); }

uint32_t Address(PPCContext& c, unsigned ra, int32_t displacement, unsigned width) {
  Require(ra == 3, "Memory instruction does not use r3 as its base");
  const uint32_t address = uint32_t(Reg(c, ra)) + uint32_t(displacement);
  Require((width == 1 || address % width == 0) && uint64_t(address) + width <= kMemorySize,
          "Raw program accessed unaligned or unmapped ordinary memory");
  return address;
}

uint64_t ReadBE(const Memory& memory, uint32_t address, unsigned width) {
  uint64_t value = 0;
  for (unsigned i = 0; i < width; ++i) value = value * 256 + memory[address + i];
  return value;
}

void WriteBE(Memory& memory, uint32_t address, unsigned width, uint64_t value,
             std::vector<Write>& writes) {
  if (width < 8) value &= (uint64_t{1} << (width * 8)) - 1;
  for (unsigned i = 0; i < width; ++i)
    memory[address + i] = uint8_t(value >> (8 * (width - 1 - i)));
  writes.push_back({address, width, value});
}

bool Selected(unsigned ppc, unsigned begin, unsigned end) {
  return begin <= end ? begin <= ppc && ppc <= end : ppc >= begin || ppc <= end;
}

// Bit loops intentionally avoid generator expressions and mask tables.
uint64_t RotateWord(uint32_t source, unsigned shift, unsigned begin, unsigned end) {
  uint64_t result = 0;
  for (unsigned bit = 0; bit < 64; ++bit) {
    const unsigned ppc = 63 - bit;
    if (Selected(ppc, 32 + begin, 32 + end) &&
        ((source >> ((bit + 32 - shift) % 32)) & 1u))
      result |= uint64_t{1} << bit;
  }
  return result;
}

uint64_t InsertWord(uint64_t destination, uint32_t source, unsigned shift,
                    unsigned begin, unsigned end) {
  for (unsigned bit = 0; bit < 32; ++bit) {
    const unsigned ppc = 31 - bit;
    if (!Selected(ppc, begin, end)) continue;
    const uint64_t mask = uint64_t{1} << bit;
    destination &= ~mask;
    if ((source >> ((bit + 32 - shift) % 32)) & 1u) destination |= mask;
  }
  return destination;
}

uint64_t RotateDoubleClearRight(uint64_t source, unsigned shift, unsigned end) {
  uint64_t result = 0;
  for (unsigned bit = 0; bit < 64; ++bit) {
    const unsigned ppc = 63 - bit;
    if (ppc <= end && ((source >> ((bit + 64 - shift) % 64)) & 1u))
      result |= uint64_t{1} << bit;
  }
  return result;
}

struct Outcome { PPCContext context; Memory memory; std::vector<Write> writes; };

Outcome Interpret(const EngineCase& test, const PPCContext& initial, const Memory& before) {
  Outcome out;
  std::memcpy(&out.context, &initial, sizeof(initial));
  out.memory = before;
  bool returned = false;
  for (size_t pc = 0; pc < test.word_count; ++pc) {
    const uint32_t w = test.words[pc];
    if (w == 0x4e800020u) {
      Require(pc + 1 == test.word_count, std::string(test.name) + ": blr is not final");
      returned = true;
      continue;
    }
    Require(!returned, std::string(test.name) + ": instruction follows blr");
    const unsigned op = w >> 26, rt = (w >> 21) & 31, ra = (w >> 16) & 31;
    if (op == 20 || op == 21 || op == 30 || op == 31)
      Require((w & 1) == 0, std::string(test.name) + ": condition update unsupported");
    if (op == 14) { // addi, including li when RA=0
      const uint64_t lhs = ra ? Reg(out.context, ra) : 0;
      Reg(out.context, rt) = lhs + uint64_t(int64_t(Signed16(w)));
    } else if (op == 24 || op == 25) { // ori, oris
      const uint64_t immediate = uint64_t(uint16_t(w)) << (op == 25 ? 16 : 0);
      Reg(out.context, ra) = Reg(out.context, rt) | immediate;
    } else if (op == 31 && ((w >> 1) & 0x3ff) == 444) { // or
      const unsigned rb = (w >> 11) & 31;
      Reg(out.context, ra) = Reg(out.context, rt) | Reg(out.context, rb);
    } else if (op == 21) { // rlwinm, PPC64 word mask positions are 32+MB..32+ME
      Reg(out.context, ra) = RotateWord(uint32_t(Reg(out.context, rt)),
          (w >> 11) & 31, (w >> 6) & 31, (w >> 1) & 31);
    } else if (op == 20) { // rlwimi preserves the destination high 32 bits
      Require(((w >> 6) & 31) <= ((w >> 1) & 31),
              std::string(test.name) + ": wrapping rlwimi is outside the verified subset");
      Reg(out.context, ra) = InsertWord(Reg(out.context, ra), uint32_t(Reg(out.context, rt)),
          (w >> 11) & 31, (w >> 6) & 31, (w >> 1) & 31);
    } else if (op == 30 && ((w >> 2) & 7) == 1) { // rldicr
      const unsigned sh = ((w >> 11) & 31) | ((w & 2) << 4);
      const unsigned me = ((w >> 6) & 31) | ((w & 0x20) ? 32 : 0);
      Reg(out.context, ra) = RotateDoubleClearRight(Reg(out.context, rt), sh, me);
    } else if (op == 32 || op == 34 || op == 40) { // lwz, lbz, lhz: zero extend
      const unsigned width = op == 34 ? 1 : (op == 40 ? 2 : 4);
      const auto a = Address(out.context, ra, Signed16(w), width);
      Reg(out.context, rt) = ReadBE(out.memory, a, width);
    } else if (op == 36 || op == 38) { // stw, stb
      const unsigned width = op == 36 ? 4 : 1;
      const auto a = Address(out.context, ra, Signed16(w), width);
      WriteBE(out.memory, a, width, Reg(out.context, rt), out.writes);
    } else if ((op == 58 || op == 62) && (w & 3) == 0) { // ld, std DS form
      const int32_t displacement = int16_t(w & 0xfffcu);
      const auto a = Address(out.context, ra, displacement, 8);
      if (op == 58) Reg(out.context, rt) = ReadBE(out.memory, a, 8);
      else WriteBE(out.memory, a, 8, Reg(out.context, rt), out.writes);
    } else {
      std::ostringstream message;
      message << test.name << ": unsupported raw opcode 0x" << std::hex << w
              << " at 0x" << (test.entry + uint32_t(pc * 4));
      throw std::runtime_error(message.str());
    }
  }
  Require(returned, std::string(test.name) + ": missing exact canonical blr");
  return out;
}

void CheckNarrowLoads() {
  for (unsigned width : {1u, 2u}) {
    const uint32_t opcode = width == 1 ? 34 : 40;
    for (uint32_t address : {0u, 2u, uint32_t(kMemorySize - width)}) {
      for (uint64_t value : {0ull, 0x7full, 0x80ull, 0xffull, 0x8001ull, 0xffffull}) {
        const uint64_t expected_value = value & (width == 1 ? 0xff : 0xffff);
        Memory memory; memory.fill(0x5a);
        memory[address] = uint8_t(width == 1 ? value : value >> 8);
        if (width == 2) memory[address + 1] = uint8_t(value);
        // Negative displacement; dirty upper receiver bits must be discarded.
        PPCContext initial{};
        initial.r3.u64 = 0xdeadbeef00000000ull | (address + width);
        initial.r5.u64 = ~uint64_t{0};
        const uint32_t words[]{(opcode << 26) | (5 << 21) | (3 << 16) |
                              uint16_t(-int(width)), 0x4e800020u};
        EngineCase test = kEngineCases[0]; test.words = words; test.word_count = 2;
        const Outcome out = Interpret(test, initial, memory);
        PPCContext expected;
        std::memcpy(&expected, &initial, sizeof(initial)); expected.r5.u64 = expected_value;
        Require(std::memcmp(&expected, &out.context, sizeof(expected)) == 0 &&
                out.memory == memory && out.writes.empty(), "Narrow load state/zero-extension known answer");
        Require(EngineReadBE(memory.data(), address, width * 8) == expected_value,
                "Derived narrow load endian/width known answer");
        uint8_t* base = memory.data();
        const uint64_t actual = width == 1 ? REX_LOAD_U8(address) : REX_LOAD_U16(address);
        Require(actual == expected_value, "Original narrow load endian/width known answer");
      }
    }
    // A positive displacement wraps a 32-bit guest address to mapped address zero.
    PPCContext initial{}; initial.r3.u64 = 0xffffffffffffffffull;
    Memory memory{}; memory[0] = 0x80; memory[1] = 0x01;
    const uint32_t words[]{(opcode << 26) | (5 << 21) | (3 << 16) | 1, 0x4e800020u};
    EngineCase test = kEngineCases[0]; test.words = words; test.word_count = 2;
    Require(Interpret(test, initial, memory).context.r5.u64 == (width == 1 ? 0x80 : 0x8001),
            "Narrow load address wrap known answer");
    for (uint32_t address : {uint32_t(kMemorySize), 0xffffffffu}) {
      initial.r3.u64 = address - 1u;
      bool rejected = false;
      try { Interpret(test, initial, memory); }
      catch (const std::runtime_error& e) { rejected = std::string(e.what()).find("unaligned or unmapped") != std::string::npos; }
      Require(rejected, "Narrow load out-of-range access accepted");
    }
    if (width == 2) {
      initial.r3.u64 = 0;  // +1 is mapped but unaligned
      bool rejected = false;
      try { Interpret(test, initial, memory); }
      catch (const std::runtime_error& e) { rejected = std::string(e.what()).find("unaligned or unmapped") != std::string::npos; }
      Require(rejected, "Unaligned halfword load accepted");
    }
  }
}

bool Same(const PPCContext& a, const PPCContext& b, const Memory& am, const Memory& bm,
          const std::vector<Write>& aw, const std::vector<Write>& bw) {
  return std::memcmp(&a, &b, sizeof(a)) == 0 && am == bm && aw == bw;
}

uint32_t Next(uint32_t& state) {
  state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state;
}

std::string Escape(const char* value) {
  std::string result;
  for (const unsigned char ch : std::string(value ? value : "")) {
    if (ch == '\\' || ch == '"') { result += '\\'; result += char(ch); }
    else if (ch < 0x20) { result += "?"; }
    else result += char(ch);
  }
  return result;
}

std::set<std::string> ReadOnlyFile(const std::string& path) {
  std::ifstream input(path);
  Require(bool(input), "Cannot open --only names file: " + path);
  std::set<std::string> names;
  std::string line;
  while (std::getline(input, line)) {
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos || line[first] == '#') continue;
    const auto last = line.find_last_not_of(" \t\r");
    names.insert(line.substr(first, last - first + 1));
  }
  return names;
}
} // namespace

int main(int argc, char** argv) {
  try {
    std::string report_path;
    std::set<std::string> only;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      Require((arg == "--report" || arg == "--only") && i + 1 < argc,
              "usage: renderer-scalar-pipeline-tests [--only names.txt] [--report report.json]");
      if (arg == "--report") report_path = argv[++i];
      else only = ReadOnlyFile(argv[++i]);
    }
    Require(std::size(kEngineCases) > 0, "Effect-engine fixture is empty");
    CheckNarrowLoads();
    Require(RotateWord(0x12345678, 0, 28, 23) == 0x1234567812345608ull,
            "PPC64 wrapping word-mask known answer");
    Require(InsertWord(0xdeadbeef00000000ull, 0x80000000, 1, 31, 31) ==
            0xdeadbeef00000001ull, "rlwimi high-half preservation known answer");
    Require(RotateDoubleClearRight(1, 45, 63) == 0x200000000000ull,
            "rldicr known answer");
    for (uint32_t word : {0x5083003fu, 0x5483003fu, 0x78830005u, 0x7c832379u}) {
      const uint32_t words[]{word, 0x4e800020u};
      EngineCase rejection = kEngineCases[0];
      rejection.words = words; rejection.word_count = 2;
      bool rejected = false;
      try { Interpret(rejection, PPCContext{}, Memory{}); }
      catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("condition update unsupported") != std::string::npos;
      }
      Require(rejected, "Raw oracle accepted an unsupported Rc variant");
    }

    std::vector<uint32_t> values{0, 0xffffffffu, 0x80000001u, 0x01234567u,
                                 0x89abcdefu, 0xa5a5a5a5u};
    for (unsigned bit = 0; bit < 32; ++bit) {
      values.push_back(uint32_t{1} << bit);
      values.push_back(~(uint32_t{1} << bit));
    }
    uint32_t random = 0x20270922u;
    for (unsigned i = 0; i < 64; ++i) values.push_back(Next(random));

    size_t functions = 0, invocations = 0, controls = 0;
    std::ostringstream function_json;
    bool first_json = true;
    for (const auto& test : kEngineCases) {
      if (!only.empty() && !only.count(test.name)) continue;
      Require(test.words && test.word_count, std::string(test.name) + ": missing raw body");
      size_t case_invocations = 0, case_controls = 0;
      uint32_t case_random = test.entry ^ 0x9e3779b9u;
      for (uint32_t receiver : {0u, 0x400u, 0x1400u}) for (uint32_t value : values) {
        PPCContext initial;
        std::memset(&initial, uint8_t(Next(case_random)), sizeof(initial));
        initial.r3.u64 = 0xdeadbeef00000000ull | receiver;
        initial.r4.u64 = (uint64_t(Next(case_random)) << 32) | value;
        Memory before;
        for (auto& byte : before) byte = uint8_t(Next(case_random));
        const std::string example = std::string(test.name) + " receiver=" +
            std::to_string(receiver) + " value=" + std::to_string(value);

        // Interpretation happens first and therefore validates every effective address
        // before either generated implementation is allowed to touch host memory.
        const Outcome oracle = Interpret(test, initial, before);
        PPCContext actual;
        std::memcpy(&actual, &initial, sizeof(initial));
        Memory actual_memory = before;
        g_writes.clear();
        test.original(actual, actual_memory.data());
        const auto actual_writes = g_writes;
        Require(Same(actual, oracle.context, actual_memory, oracle.memory,
                     actual_writes, oracle.writes), example + ": actual/raw mismatch");

        PPCContext derived;
        std::memcpy(&derived, &initial, sizeof(initial));
        Memory derived_memory = before;
        g_writes.clear();
        test.derived(derived, derived_memory.data());
        const auto derived_writes = g_writes;
        Require(Same(derived, oracle.context, derived_memory, oracle.memory,
                     derived_writes, oracle.writes), example + ": contract/raw mismatch");
        ++invocations; ++case_invocations;
      }

      PPCContext seed{};
      seed.r3.u64 = 0x400;
      seed.r4.u64 = 0x89abcdef01234567ull;
      Memory seed_memory; seed_memory.fill(0x96);
      const Outcome expected = Interpret(test, seed, seed_memory);
      Outcome changed = expected;
      Reg(changed.context, 31) ^= 1;
      Require(!Same(changed.context, expected.context, changed.memory, expected.memory,
                    changed.writes, expected.writes), "Register negative control escaped");
      ++controls; ++case_controls;
      changed = expected; changed.memory[0x20] ^= 1;
      Require(!Same(changed.context, expected.context, changed.memory, expected.memory,
                    changed.writes, expected.writes), "Memory negative control escaped");
      ++controls; ++case_controls;
      changed = expected; changed.writes.push_back({0x20, 1, 0x5a});
      Require(!Same(changed.context, expected.context, changed.memory, expected.memory,
                    changed.writes, expected.writes), "Fabricated-write negative control escaped");
      ++controls; ++case_controls;
      if (!expected.writes.empty()) {
        changed = expected; changed.writes[0].width ^= 1;
        Require(!Same(changed.context, expected.context, changed.memory, expected.memory,
                      changed.writes, expected.writes), "Write-width negative control escaped");
        ++controls; ++case_controls;
        if (expected.writes.size() > 1) {
          changed = expected; std::swap(changed.writes[0], changed.writes[1]);
          Require(!Same(changed.context, expected.context, changed.memory, expected.memory,
                        changed.writes, expected.writes), "Write-order negative control escaped");
          ++controls; ++case_controls;
        }
      }

      if (!first_json) function_json << ",\n";
      first_json = false;
      function_json << "    {\"function\":\"" << Escape(test.name) << "\",\"passed\":true"
                    << ",\"invocations\":" << case_invocations
                    << ",\"negative_controls\":" << case_controls
                    << ",\"body_sha\":\"" << Escape(test.body_sha) << "\""
                    << ",\"run_id\":\"" << Escape(test.run_id) << "\""
                    << ",\"contract_sha256\":\"" << Escape(test.contract_sha256) << "\"}";
      ++functions;
    }
    if (!only.empty()) {
      Require(functions == only.size(), "--only contains a name absent from the generated fixture");
    }
    Require(functions > 0, "No scalar functions selected");
    std::ostringstream report;
    report << "{\n  \"passed\": true,\n  \"function_count\": " << functions
           << ",\n  \"invocations\": " << invocations
           << ",\n  \"negative_controls\": " << controls
           << ",\n  \"source_identity\": \"raw words plus generated-body SHA-256\","
           << "\n  \"functions\": [\n" << function_json.str() << "\n  ]\n}\n";
    if (!report_path.empty()) {
      std::ofstream output(report_path);
      output << report.str();
      Require(bool(output), "Cannot write JSON report: " + report_path);
    }
    std::cout << report.str();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Scalar pipeline failure: " << error.what() << '\n';
    return 1;
  }
}

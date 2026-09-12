#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

// Only fields used by the adapter. The real guest setter is deliberately mocked;
// these tests cover forwarding and observation, not guest callback semantics.
struct Register { uint32_t u32{}; bool operator==(const Register&) const = default; };
struct Context {
  Register r3, r4, r5, r6;
  uint64_t lr{};
  uint64_t untouched{};
  bool operator==(const Context&) const = default;
};
struct Observation {
  uint64_t sequence;
  uint32_t worker, mode, callback, context, return_address;
};
static bool enabled{};
static unsigned calls{}, warnings{};
static Context expected_input{}, expected_output{};
static std::vector<Observation> observations;
static void Require(bool condition) { if (!condition) std::abort(); }

static void __imp__sub_8243A000(Context& ctx, uint8_t* base) {
  Require(base == nullptr);
  Require(ctx == expected_input);
  ++calls;
  ctx = expected_output;
}
static void LogInfo(const char*, uint64_t sequence, uint32_t worker,
                    uint32_t mode, uint32_t callback, uint32_t context,
                    uint32_t return_address) {
  // The original must have returned before an observation is emitted.
  Require(calls > sequence);
  observations.push_back({sequence, worker, mode, callback, context, return_address});
}
static void LogWarning(const char*) { ++warnings; }
#define REXCVAR_GET(name) enabled
#define REX_HOOK_RAW(name) static void name(Context& ctx, uint8_t* base)
#define REXLOG_INFO(...) LogInfo(__VA_ARGS__)
#define REXLOG_WARN(...) LogWarning(__VA_ARGS__)
#include "native_worker_callback_audit_fixture.inc"

static void Invoke(uint32_t id) {
  expected_input = {{id}, {id % 6}, {id ? 0x82100000u + id : 0},
                    {0x100000u + id}, 0xffffffff82001000ull + id, 0x12345678};
  // Deliberately overwrite all argument registers and LR in the mocked callee.
  expected_output = {{0xabcdef01}, {9}, {8}, {7}, 0x1234, 0x87654321};
  auto ctx = expected_input;
  const auto before = calls;
  sub_8243A000(ctx, nullptr);
  Require(calls == before + 1);
  Require(ctx == expected_output);
}
int main() {
  Invoke(42); // Disabled mode forwards without spending the logging budget.
  Require(observations.empty() && warnings == 0);
  enabled = true;
  for (uint32_t i = 0; i < 300; ++i) {
    Invoke(i);
    if (i < 256) {
      const auto& row = observations.back();
      Require(row.sequence == i + 1 && row.worker == i && row.mode == i % 6);
      Require(row.callback == expected_input.r5.u32 && row.context == expected_input.r6.u32);
      Require(row.return_address == static_cast<uint32_t>(expected_input.lr));
    }
  }
  Require(observations.size() == 256 && warnings == 1 && calls == 301);
  enabled = false;
  Invoke(17);
  Require(observations.size() == 256 && warnings == 1 && calls == 302);
  std::cout << "Worker callback adapter forwarding, snapshots, disabled mode and log cap passed\n";
}

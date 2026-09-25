// EDF2027 - differential audit of the codegen fork's locals form (docs/codegen-fork.md).
//
// Built only with EDF_CODEGEN_FORK_AUDIT=ON, together with edf_ipa_audit = true in
// edf2017_codegen_fork.toml. For every replayable guest function the fork emits the
// locals-form body (candidate), the ordinary ctx-form body (reference) and a wrapper
// that calls edf_ipa_audit(). Sampled calls run the candidate with a store journal,
// roll guest memory and the context back, run the reference, and compare every
// PPCContext field and every guest byte either run wrote. The reference result is the
// one that stays. Nested calls inside a sampled call run the candidate directly.

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

#include <xmmintrin.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

REXCVAR_DEFINE_BOOL(edf_ipa_audit, true, "EDF2027",
  "Codegen fork differential audit (audit builds only): replay sampled guest calls with the "
  "ctx-form reference body and compare registers and written memory");
REXCVAR_DEFINE_INT32(edf_ipa_audit_first, 2, "EDF2027",
  "Codegen fork audit: audit the first N calls of every function").range(0, 1000000);
REXCVAR_DEFINE_INT32(edf_ipa_audit_every, 512, "EDF2027",
  "Codegen fork audit: then audit every Nth call of every function").range(1, 1 << 30);

struct EdfIpaJournal {
  struct Entry {
    uint8_t* host;
    uint32_t size;
    uint8_t old[8];
  };
  std::vector<Entry> entries;
};

thread_local EdfIpaJournal* edf_ipa_tl_journal = nullptr;

void edf_ipa_journal_record(EdfIpaJournal* journal, uint8_t* host, unsigned size) {
  EdfIpaJournal::Entry e;
  e.host = host;
  e.size = size;
  std::memcpy(e.old, host, size);
  journal->entries.push_back(e);
}

namespace {

constexpr uint32_t kSlots = 1u << 16;
std::atomic<uint32_t> g_calls[kSlots];
std::atomic<uint32_t> g_audited_per_fn[kSlots];
std::atomic<uint64_t> g_audited{0};
std::atomic<uint64_t> g_mismatches{0};
std::atomic<uint64_t> g_reg_mismatches{0};
std::atomic<uint64_t> g_mem_mismatches{0};
std::atomic<uint32_t> g_functions_audited{0};
std::atomic<uint32_t> g_functions_mismatched{0};
std::atomic<uint32_t> g_mismatch_logged[kSlots];
std::atomic<int64_t> g_last_report_ms{0};
thread_local int t_depth = 0;

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

struct Diff {
  std::string text;
  int count = 0;
  void add(const char* what, uint64_t a, uint64_t b) {
    if (count++ < 8)
      text += fmt::format(" {}:cand={:X},ref={:X}", what, a, b);
  }
};

// LIVEOUT bit layout (codegen fork, edf_ipa.cpp): r0-r31 = 0-31, f0-f31 = 32-63,
// cr0-cr7 = 64-71, ctr 72, xer 73, lr 74, reserved 75. Registers outside the function's
// LIVEOUT are dead for every caller the analysis sees and may legitimately differ.
struct LiveOut {
  uint64_t lo, hi;
  bool has(int bit) const { return bit < 64 ? ((lo >> bit) & 1) : ((hi >> (bit - 64)) & 1); }
};

void CompareContexts(const PPCContext& a, const PPCContext& b, const LiveOut& live, Diff& d) {
  const PPCRegister* ra[32] = {&a.r0,  &a.r1,  &a.r2,  &a.r3,  &a.r4,  &a.r5,  &a.r6,  &a.r7,
                               &a.r8,  &a.r9,  &a.r10, &a.r11, &a.r12, &a.r13, &a.r14, &a.r15,
                               &a.r16, &a.r17, &a.r18, &a.r19, &a.r20, &a.r21, &a.r22, &a.r23,
                               &a.r24, &a.r25, &a.r26, &a.r27, &a.r28, &a.r29, &a.r30, &a.r31};
  const PPCRegister* rb[32] = {&b.r0,  &b.r1,  &b.r2,  &b.r3,  &b.r4,  &b.r5,  &b.r6,  &b.r7,
                               &b.r8,  &b.r9,  &b.r10, &b.r11, &b.r12, &b.r13, &b.r14, &b.r15,
                               &b.r16, &b.r17, &b.r18, &b.r19, &b.r20, &b.r21, &b.r22, &b.r23,
                               &b.r24, &b.r25, &b.r26, &b.r27, &b.r28, &b.r29, &b.r30, &b.r31};
  for (int i = 0; i < 32; ++i) {
    if (live.has(i) && ra[i]->u64 != rb[i]->u64) {
      std::string n = fmt::format("r{}", i);
      d.add(n.c_str(), ra[i]->u64, rb[i]->u64);
    }
    const PPCRegister& fa = (&a.f0)[i];
    const PPCRegister& fb = (&b.f0)[i];
    if (live.has(32 + i) && fa.u64 != fb.u64) {
      std::string n = fmt::format("f{}", i);
      d.add(n.c_str(), fa.u64, fb.u64);
    }
  }
  for (int i = 0; i < 8; ++i) {
    const PPCCRRegister& ca = (&a.cr0)[i];
    const PPCCRRegister& cb = (&b.cr0)[i];
    if (live.has(64 + i) &&
        (ca.lt != cb.lt || ca.gt != cb.gt || ca.eq != cb.eq || ca.so != cb.so)) {
      std::string n = fmt::format("cr{}", i);
      d.add(n.c_str(), ca.raw(), cb.raw());
    }
  }
  if (live.has(72) && a.ctr.u64 != b.ctr.u64)
    d.add("ctr", a.ctr.u64, b.ctr.u64);
  if (live.has(73) && (a.xer.so != b.xer.so || a.xer.ov != b.xer.ov || a.xer.ca != b.xer.ca))
    d.add("xer", (a.xer.so << 16) | (a.xer.ov << 8) | a.xer.ca,
          (b.xer.so << 16) | (b.xer.ov << 8) | b.xer.ca);
  if (live.has(74) && a.lr != b.lr)
    d.add("lr", a.lr, b.lr);
  if (live.has(75) && a.reserved.u64 != b.reserved.u64)
    d.add("reserved", a.reserved.u64, b.reserved.u64);
  if (a.msr != b.msr)
    d.add("msr", a.msr, b.msr);
  if (a.fpscr.csr != b.fpscr.csr)
    d.add("fpscr", a.fpscr.csr, b.fpscr.csr);
  if (a.vscr_sat != b.vscr_sat)
    d.add("vscr_sat", a.vscr_sat, b.vscr_sat);
  const PPCVRegister* va = &a.v0;
  const PPCVRegister* vb = &b.v0;
  for (int i = 0; i < 128; ++i) {
    if (std::memcmp(&va[i], &vb[i], sizeof(PPCVRegister)) != 0) {
      std::string n = fmt::format("v{}", i);
      d.add(n.c_str(), va[i].u64[0] ^ va[i].u64[1], vb[i].u64[0] ^ vb[i].u64[1]);
    }
  }
}

}  // namespace
extern "C" uint32_t edf_vt_miss_count();
extern "C" uint32_t edf_vt_verify(const uint8_t* base);
namespace {

void Report(bool force, const uint8_t* base) {
  int64_t now = NowMs();
  int64_t last = g_last_report_ms.load(std::memory_order_relaxed);
  if (!force && now - last < 10000)
    return;
  if (!g_last_report_ms.compare_exchange_strong(last, now))
    return;
  REXLOG_INFO(
      "IPA audit: audited={} functions={} mismatches={} (registers {}, memory {}) "
      "functions_mismatched={} vtable_guard_misses={} vtable_words_changed={}",
      g_audited.load(), g_functions_audited.load(), g_mismatches.load(), g_reg_mismatches.load(),
      g_mem_mismatches.load(), g_functions_mismatched.load(), edf_vt_miss_count(),
      edf_vt_verify(base));
}

}  // namespace

extern "C" void edf_ipa_audit(uint32_t index, uint32_t addr, PPCContext& ctx, uint8_t* base,
                              PPCFunc* cand, PPCFunc* ref, uint64_t live_lo, uint64_t live_hi) {
  if (t_depth > 0 || !REXCVAR_GET(edf_ipa_audit)) {
    cand(ctx, base);
    return;
  }
  uint32_t slot = index & (kSlots - 1);
  uint32_t n = g_calls[slot].fetch_add(1, std::memory_order_relaxed);
  uint32_t first = static_cast<uint32_t>(REXCVAR_GET(edf_ipa_audit_first));
  uint32_t every = static_cast<uint32_t>(REXCVAR_GET(edf_ipa_audit_every));
  if (!(n < first || (n % every) == 0)) {
    cand(ctx, base);
    return;
  }
  ++t_depth;

  // Run 1: candidate (locals form), journaled.
  alignas(64) PPCContext c0 = ctx;
  unsigned csr0 = _mm_getcsr();
  EdfIpaJournal j1;
  edf_ipa_tl_journal = &j1;
  cand(ctx, base);
  edf_ipa_tl_journal = nullptr;
  alignas(64) PPCContext c1 = ctx;
  unsigned csr1 = _mm_getcsr();
  // What run 1 left in memory, byte by byte (last write wins), then roll it back.
  std::unordered_map<uint8_t*, uint8_t> after1;
  for (const auto& e : j1.entries)
    for (uint32_t i = 0; i < e.size; ++i)
      after1[e.host + i] = e.host[i];
  for (auto it = j1.entries.rbegin(); it != j1.entries.rend(); ++it)
    std::memcpy(it->host, it->old, it->size);

  // Run 2: reference (ctx form) from the same state.
  ctx = c0;
  _mm_setcsr(csr0);
  EdfIpaJournal j2;
  edf_ipa_tl_journal = &j2;
  ref(ctx, base);
  edf_ipa_tl_journal = nullptr;
  unsigned csr2 = _mm_getcsr();

  Diff regs;
  CompareContexts(c1, ctx, LiveOut{live_lo, live_hi}, regs);
  // Only the guest-owned MXCSR bits (rounding, flush); sticky exception flags differ
  // whenever clang drops a dead FP operation from the locals form, and no guest reads them.
  if ((csr1 ^ csr2) & PPCFPSCRRegister::GuestMask)
    regs.add("mxcsr", csr1, csr2);
  // Final guest bytes: the candidate's are what run 1 left (or the pre-call value where it
  // wrote nothing); the reference's are what memory holds now.
  Diff mem;
  auto guest = [&](uint8_t* p) -> uint64_t {
    return static_cast<uint32_t>(p - base);
  };
  for (const auto& [p, v] : after1) {
    if (v != *p)
      mem.add("mem", guest(p), (uint64_t(v) << 8) | *p);
  }
  std::unordered_map<uint8_t*, uint8_t> pre2;
  for (const auto& e : j2.entries)
    for (uint32_t i = 0; i < e.size; ++i)
      pre2.try_emplace(e.host + i, e.old[i]);
  for (const auto& [p, old] : pre2) {
    if (!after1.contains(p) && old != *p)
      mem.add("mem-ref-only", guest(p), (uint64_t(old) << 8) | *p);
  }

  g_audited.fetch_add(1, std::memory_order_relaxed);
  if (g_audited_per_fn[slot].fetch_add(1, std::memory_order_relaxed) == 0)
    g_functions_audited.fetch_add(1, std::memory_order_relaxed);
  if (regs.count || mem.count) {
    g_mismatches.fetch_add(1, std::memory_order_relaxed);
    if (regs.count)
      g_reg_mismatches.fetch_add(1, std::memory_order_relaxed);
    if (mem.count)
      g_mem_mismatches.fetch_add(1, std::memory_order_relaxed);
    uint32_t logged = g_mismatch_logged[slot].fetch_add(1, std::memory_order_relaxed);
    if (logged == 0)
      g_functions_mismatched.fetch_add(1, std::memory_order_relaxed);
    if (logged < 3) {
      REXLOG_WARN("IPA audit MISMATCH sub_{:08X} call {}: {} register diffs{} ; {} memory diffs{}",
                  addr, n, regs.count, regs.text, mem.count, mem.text);
    }
  }
  --t_depth;
  Report(false, base);
}

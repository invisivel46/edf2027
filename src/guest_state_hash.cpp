// EDF2027 - guest state hash trace (guest_state_hash.h).
#include "guest_state_hash.h"

#include <rex/cvar.h>
#include <rex/hash.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "scripted_input_logic.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

REXCVAR_DEFINE_STRING(edf_guest_hash_trace, "", "EDF2027",
  "Exactness gate: CSV path; hash all committed guest memory after every engine step dispatch "
  "(docs/codegen-overhead.md). Empty (default) disables.");
REXCVAR_DEFINE_INT32(edf_guest_hash_every, 1, "EDF2027",
  "Exactness gate: hash after every Nth step dispatch").range(1, 1000000);
REXCVAR_DEFINE_INT32(edf_guest_hash_skip, 0, "EDF2027",
  "Exactness gate: first dispatch sequence number to hash (earlier dispatches are only counted)").range(0, 100000000);
REXCVAR_DEFINE_STRING(edf_guest_hash_dump_at, "", "EDF2027",
  "Exactness gate: comma-separated dispatch sequence numbers at which to also write per-chunk hashes "
  "(<trace>.chunks-<seq>.csv), to pin a divergence to address ranges");
REXCVAR_DEFINE_STRING(edf_guest_hash_raw, "", "EDF2027",
  "Exactness gate: comma-separated guest ranges start-end (hex) whose raw bytes are also written "
  "at each edf_guest_hash_dump_at dispatch (<trace>.raw-<seq>-<start>.bin), to find the words that differ");
REXCVAR_DEFINE_INT32(edf_guest_hash_chunk_kb, 64, "EDF2027",
  "Exactness gate: chunk size in KiB for the per-chunk dumps (4 = one guest small page)").range(4, 65536);
REXCVAR_DEFINE_STRING(edf_guest_hash_exclude, "", "EDF2027",
  "Exactness gate: comma-separated guest ranges start-end (hex, end exclusive) left out of the hash, "
  "e.g. 8257C300-8257C310");
REXCVAR_DEFINE_BOOL(edf_step_timing, false, "EDF2027",
  "Log the engine step dispatch wall time per simulation step every 5 s (two clock reads per "
  "dispatch; lighter than edf_native_hook_timings, for codegen A/B timing)");
REXCVAR_DEFINE_BOOL(edf_deterministic_steps, false, "EDF2027",
  "Exactness gate: the engine heartbeat grants exactly one simulation tick per heartbeat with no "
  "wall-clock wait (game speed then follows host speed); for seeded step-by-step comparisons");

namespace edf::gate {
namespace {
struct Range { uint32_t begin, end; };
struct Chunk { uint32_t address, size; uint8_t heap; };

// Heaps in address order. The physical heaps A0/C0/E0 are views of one physical memory,
// but each physical page is committed through exactly one of them, so hashing each heap's
// committed regions covers every physical page once.
constexpr uint32_t kHeapProbes[] = {0x00010000u, 0x40000000u, 0x80000000u, 0x90000000u,
                                    0xA0000000u, 0xC0000000u, 0xE0000000u};
constexpr const char* kHeapNames[] = {"v00", "v40", "v80", "v90", "vA0", "vC0", "vE0"};
constexpr size_t kHeaps = std::size(kHeapProbes);

std::vector<Range> ParseRanges(const std::string& text) {
  std::vector<Range> out;
  std::stringstream in(text);
  std::string item;
  while (std::getline(in, item, ',')) {
    if (item.empty()) continue;
    const auto dash = item.find('-');
    if (dash == std::string::npos) throw std::invalid_argument("edf_guest_hash_exclude: expected start-end");
    out.push_back({uint32_t(std::stoul(item.substr(0, dash), nullptr, 16)),
                   uint32_t(std::stoul(item.substr(dash + 1), nullptr, 16))});
  }
  std::sort(out.begin(), out.end(), [](auto a, auto b) { return a.begin < b.begin; });
  return out;
}

std::vector<uint64_t> ParseList(const std::string& text) {
  std::vector<uint64_t> out;
  std::stringstream in(text);
  std::string item;
  while (std::getline(in, item, ',')) if (!item.empty()) out.push_back(std::stoull(item));
  std::sort(out.begin(), out.end());
  return out;
}

// Calls emit(begin, end, heap) for the host-readable parts of guest [begin, end).
template <typename Emit>
void EmitHostReadable(rex::memory::Memory& memory, uint64_t begin, uint64_t end, uint8_t heap, Emit& emit) {
  while (begin < end) {
    auto* host = memory.TranslateVirtual<const uint8_t*>(uint32_t(begin));
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(host, &mbi, sizeof(mbi))) return;
    const auto* region_end_host = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
    const uint64_t span = std::min<uint64_t>(end - begin, uint64_t(region_end_host - host));
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (mbi.State == MEM_COMMIT && (mbi.Protect & readable) && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
      emit(begin, begin + span, heap);
    begin += span;
  }
}

// Committed regions of every heap, cut into chunks of at most chunk_size bytes that never
// straddle an excluded range.
std::vector<Chunk> CollectChunks(rex::memory::Memory& memory, uint32_t chunk_size,
                                 const std::vector<Range>& excluded) {
  std::vector<Chunk> chunks;
  auto emit = [&](uint64_t begin, uint64_t end, uint8_t heap) {
    auto cut = [&](uint64_t from, uint64_t to) {
      for (uint64_t a = from; a < to; a += chunk_size)
        chunks.push_back({uint32_t(a), uint32_t(std::min<uint64_t>(chunk_size, to - a)), heap});
    };
    for (const auto& x : excluded) {
      if (x.end <= begin || x.begin >= end) continue;
      if (x.begin > begin) cut(begin, x.begin);
      begin = std::max<uint64_t>(begin, x.end);
      if (begin >= end) return;
    }
    cut(begin, end);
  };
  for (size_t h = 0; h < kHeaps; ++h) {
    auto* heap = memory.LookupHeap(kHeapProbes[h]);
    if (!heap) continue;
    const uint64_t heap_end = uint64_t(heap->heap_base()) + heap->heap_size();
    uint64_t address = heap->heap_base();
    while (address < heap_end) {
      rex::memory::HeapAllocationInfo info{};
      if (!heap->QueryRegionInfo(uint32_t(address), &info) || !info.region_size) {
        address += heap->page_size();
        continue;
      }
      const uint64_t region_end = std::min<uint64_t>(uint64_t(info.base_address) + info.region_size, heap_end);
      // Committed and guest-readable (guard pages, e.g. under thread stacks, are committed
      // no-access), then split by what the host can actually read.
      if ((info.state & rex::memory::kMemoryAllocationCommit) &&
          (info.protect & rex::memory::kMemoryProtectRead) && region_end > address)
        EmitHostReadable(memory, address, region_end, uint8_t(h), emit);
      address = std::max<uint64_t>(region_end, address + heap->page_size());
    }
  }
  return chunks;
}

class Trace {
 public:
  void Step(uint8_t* base, uint32_t steps) {
    ++sequence_;
    if (!initialized_) Initialize();
    if (sequence_ < uint64_t(REXCVAR_GET(edf_guest_hash_skip))) return;
    if (sequence_ % uint64_t(REXCVAR_GET(edf_guest_hash_every))) return;
    auto* memory = REX_KERNEL_MEMORY();
    if (!memory || memory->virtual_membase() != base) throw std::runtime_error("guest hash: memory mismatch");
    const auto began = std::chrono::steady_clock::now();
    const bool dump = std::binary_search(dump_at_.begin(), dump_at_.end(), sequence_);
    const auto chunks = CollectChunks(*memory, dump ? chunk_size_ : kHashChunk, excluded_);
    std::vector<uint64_t> hashes(chunks.size());
    // Hash chunks on a few threads; the combination below is in address order, so the result
    // does not depend on the split.
    const size_t workers = std::clamp<size_t>(std::thread::hardware_concurrency() / 2, 1, 8);
    std::atomic<size_t> next{0};
    auto work = [&] {
      for (size_t i; (i = next.fetch_add(1, std::memory_order_relaxed)) < chunks.size();)
        hashes[i] = XXH3_64bits(memory->TranslateVirtual<const uint8_t*>(chunks[i].address), chunks[i].size);
    };
    std::vector<std::thread> pool;
    for (size_t w = 1; w < workers; ++w) pool.emplace_back(work);
    work();
    for (auto& t : pool) t.join();
    // Per heap and overall: XXH3 over (address, size, hash) triples in address order.
    std::vector<uint64_t> heap_hash(kHeaps, 0);
    std::vector<std::vector<uint64_t>> heap_words(kHeaps);
    std::vector<uint64_t> all_words;
    uint64_t bytes = 0;
    for (size_t i = 0; i < chunks.size(); ++i) {
      const uint64_t key = (uint64_t(chunks[i].address) << 32) | chunks[i].size;
      heap_words[chunks[i].heap].insert(heap_words[chunks[i].heap].end(), {key, hashes[i]});
      all_words.insert(all_words.end(), {key, hashes[i]});
      bytes += chunks[i].size;
    }
    for (size_t h = 0; h < kHeaps; ++h)
      heap_hash[h] = heap_words[h].empty() ? 0 : XXH3_64bits(heap_words[h].data(), heap_words[h].size() * 8);
    const uint64_t all = XXH3_64bits(all_words.data(), all_words.size() * 8);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    char line[512];
    int n = std::snprintf(line, sizeof(line), "%llu,%llu,%u,%llu,%016llx", (unsigned long long)sequence_,
                          (unsigned long long)edf::SimulationTicks().load(std::memory_order_relaxed), steps,
                          (unsigned long long)bytes, (unsigned long long)all);
    for (size_t h = 0; h < kHeaps; ++h)
      n += std::snprintf(line + n, sizeof(line) - n, ",%016llx", (unsigned long long)heap_hash[h]);
    std::snprintf(line + n, sizeof(line) - n, ",%.2f\n", ms);
    file_ << line;
    if (++written_ % 60 == 0) file_.flush();
    if (dump) {
      std::ofstream out(path_.string() + ".chunks-" + std::to_string(sequence_) + ".csv");
      out << "address,size,heap,hash\n";
      for (size_t i = 0; i < chunks.size(); ++i) {
        char row[96];
        std::snprintf(row, sizeof(row), "%08X,%u,%s,%016llx\n", chunks[i].address, chunks[i].size,
                      kHeapNames[chunks[i].heap], (unsigned long long)hashes[i]);
        out << row;
      }
      for (const auto& r : raw_) {
        char suffix[64];
        std::snprintf(suffix, sizeof(suffix), ".raw-%llu-%08X.bin", (unsigned long long)sequence_, r.begin);
        std::ofstream bin(path_.string() + suffix, std::ios::binary);
        for (uint64_t a = r.begin; a < r.end; a += 4096) {
          // Unreadable pages are written as zeros so offsets stay aligned.
          static const char zeros[4096] = {};
          bool readable = false;
          for (const auto& c : chunks) if (a >= c.address && a < uint64_t(c.address) + c.size) { readable = true; break; }
          bin.write(readable ? memory->TranslateVirtual<const char*>(uint32_t(a)) : zeros, 4096);
        }
      }
      REXLOG_INFO("Guest hash: wrote chunk hashes for dispatch {} ({} chunks)", sequence_, chunks.size());
    }
  }

 private:
  // Fixed chunking for the per-step hash so the combined hash never depends on the dump size.
  static constexpr uint32_t kHashChunk = 1u << 20;
  void Initialize() {
    initialized_ = true;
    path_ = REXCVAR_GET(edf_guest_hash_trace);
    if (std::filesystem::exists(path_)) throw std::runtime_error("guest hash trace output already exists");
    file_.open(path_);
    if (!file_) throw std::runtime_error("cannot create guest hash trace output");
    file_ << "seq,sim_ticks,steps,bytes,hash";
    for (auto* name : kHeapNames) file_ << ',' << name;
    file_ << ",ms\n";
    file_.flush();
    excluded_ = ParseRanges(REXCVAR_GET(edf_guest_hash_exclude));
    dump_at_ = ParseList(REXCVAR_GET(edf_guest_hash_dump_at));
    raw_ = ParseRanges(REXCVAR_GET(edf_guest_hash_raw));
    chunk_size_ = uint32_t(REXCVAR_GET(edf_guest_hash_chunk_kb)) * 1024u;
    REXLOG_INFO("Guest hash trace: {} (every {} from {}, {} excluded ranges, deterministic steps {})",
                path_.string(), REXCVAR_GET(edf_guest_hash_every), REXCVAR_GET(edf_guest_hash_skip),
                excluded_.size(), REXCVAR_GET(edf_deterministic_steps));
  }
  bool initialized_ = false;
  uint64_t sequence_ = 0, written_ = 0;
  std::filesystem::path path_;
  std::ofstream file_;
  std::vector<Range> excluded_, raw_;
  std::vector<uint64_t> dump_at_;
  uint32_t chunk_size_ = 65536;
};
}  // namespace

uint64_t BeforeStepDispatch() {
  if (!REXCVAR_GET(edf_step_timing)) return 0;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return uint64_t(now.QuadPart);
}

void AfterStepDispatch(uint8_t* base, uint32_t steps, uint64_t started) {
  if (started) {
    // Engine thread only.
    static uint64_t window_start = 0, dispatches = 0, stepped = 0, ticks = 0, frequency = 0;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!frequency) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); frequency = uint64_t(f.QuadPart); }
    if (!window_start) window_start = started;
    ++dispatches; stepped += steps; ticks += uint64_t(now.QuadPart) - started;
    if (uint64_t(now.QuadPart) - window_start >= 5 * frequency) {
      const double ms = 1000.0 * double(ticks) / double(frequency);
      REXLOG_INFO("Step timing: dispatches={} steps={} dispatch_ms={:.1f} ms/step={:.4f} ms/dispatch={:.4f} sim_ticks={}",
                  dispatches, stepped, ms, stepped ? ms / double(stepped) : 0.0, ms / double(dispatches),
                  edf::SimulationTicks().load(std::memory_order_relaxed));
      window_start = uint64_t(now.QuadPart); dispatches = stepped = ticks = 0;
    }
  }
  // Empty cvar: one string test per dispatch, nothing else.
  if (REXCVAR_GET(edf_guest_hash_trace).empty()) return;
  static Trace trace;  // engine thread only
  trace.Step(base, steps);
}

bool DeterministicSteps() { return REXCVAR_GET(edf_deterministic_steps); }
}  // namespace edf::gate

#pragma once
#include <array>
#include <cstdint>
#include <compare>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace edf::native {
// A draw contract: what the renderer would have to support, not where the draw
// came from. Two draws from different callers with the same shaders,
// declaration, state and formats are one gap to close, so caller addresses are
// deliberately excluded from identity. Counting by caller made repeated
// rejections of one unsupported layout look like many separate problems.
enum class NativeContractPath : uint32_t {
  Indexed, Immediate, Font, Movie, Xui, Utility, Output, Count
};
constexpr std::string_view NativeContractPathName(NativeContractPath path) {
  switch(path) {
    case NativeContractPath::Indexed: return "indexed";
    case NativeContractPath::Immediate: return "immediate";
    case NativeContractPath::Font: return "font";
    case NativeContractPath::Movie: return "movie";
    case NativeContractPath::Xui: return "xui";
    case NativeContractPath::Utility: return "utility";
    case NativeContractPath::Output: return "output";
    default: return "unknown";
  }
}
struct NativeContract {
  NativeContractPath path=NativeContractPath::Indexed;
  // Effect source fingerprints, not shader handles: a handle is an address the
  // guest reuses, while the fingerprint identifies the disc source that would
  // have to be supported. Zero means the stage was not resolved at rejection.
  uint64_t vertex_source=0,pixel_source=0;
  uint64_t declaration=0; // Hash of the declaration bytes; 0 when unresolved.
  std::array<uint32_t,6> state{}; // RenderStateWords, or zero when unread.
  uint32_t topology=0,stride=0,index_width=0,elements=0;
  auto operator<=>(const NativeContract&) const = default;
  bool operator==(const NativeContract&) const = default;
};
// Stable across runs and builds, so a contract recorded today names the same
// declaration tomorrow. FNV-1a: the value is an identity, never a checksum.
inline uint64_t HashNativeDeclaration(std::span<const uint8_t> bytes) {
  uint64_t hash=1469598103934665603ull;
  for(const auto byte:bytes) { hash^=byte; hash*=1099511628211ull; }
  return hash ? hash : 1; // Keep 0 reserved for "unresolved".
}
// Distinct-contract accounting for rendering coverage. A rejected draw is
// dropped from the frame, so the only honest coverage statement is that no
// contract was ever rejected. The ledger exists to make that statement
// checkable instead of relying on someone noticing a missing mesh.
class NativeContractLedger {
 public:
  struct Counters {
    uint64_t rejected=0,submitted=0;
    uint64_t distinct_rejected=0,distinct_submitted=0;
    // Contracts that could not be retained because the limit was reached.
    // Reported rather than silently dropped: an unknown number of unrecorded
    // gaps is not the same as none.
    uint64_t omitted_rejected=0,omitted_submitted=0;
    std::array<uint64_t,size_t(NativeContractPath::Count)> rejected_by_path{};
  };
  explicit NativeContractLedger(size_t limit=4096):limit_(limit) {}
  void set_limit(size_t limit) { limit_=limit; }
  size_t limit() const { return limit_; }
  // Returns true the first time this exact contract is rejected, so the caller
  // can log the full detail once instead of once per draw.
  bool RecordRejected(const NativeContract& contract,std::string_view reason) {
    ++counters_.rejected;
    const auto path=size_t(contract.path);
    if(path<counters_.rejected_by_path.size()) ++counters_.rejected_by_path[path];
    const auto found=rejected_.find(contract);
    if(found!=rejected_.end()) { ++found->second.draws; return false; }
    if(rejected_.size()>=limit_) { ++counters_.omitted_rejected; return false; }
    rejected_.emplace(contract,Entry{std::string(reason),1});
    ++counters_.distinct_rejected;
    return true;
  }
  // Only worth recording during an explicit coverage run: this is a set lookup
  // on every submitted draw, and gameplay submits over a hundred thousand a
  // second. Rejections are rare enough to record unconditionally.
  bool RecordSubmitted(const NativeContract& contract) {
    ++counters_.submitted;
    if(submitted_.contains(contract)) return false;
    if(submitted_.size()>=limit_) { ++counters_.omitted_submitted; return false; }
    submitted_.insert(contract);
    ++counters_.distinct_submitted;
    return true;
  }
  const Counters& counters() const { return counters_; }
  size_t distinct_rejected() const { return rejected_.size(); }
  size_t distinct_submitted() const { return submitted_.size(); }
  // Every retained rejection, for an end-of-run report. Ordered by contract so
  // two runs of the same content produce comparable output.
  struct Rejection { NativeContract contract; std::string reason; uint64_t draws; };
  std::vector<Rejection> Rejections() const {
    std::vector<Rejection> result;
    result.reserve(rejected_.size());
    for(const auto& [contract,entry]:rejected_)
      result.push_back({contract,entry.reason,entry.draws});
    return result;
  }
  std::vector<NativeContract> Submitted() const {
    return {submitted_.begin(),submitted_.end()};
  }
  // Coverage is clean only when nothing was dropped and nothing was hidden.
  bool clean() const {
    return !counters_.rejected && !counters_.omitted_rejected;
  }
  // The disc stores no declaration in the form the renderer consumes: the guest
  // synthesises the 12-byte elements at load, and a scan of all 331 model assets
  // finds none of the runtime type words. So a declaration can only be learned
  // by running the game once - after which it is replayable offline forever.
  // Retaining the bytes, not just the hash, is what makes that possible.
  void RetainDeclaration(uint64_t hash,std::span<const uint8_t> bytes) {
    if(!hash || bytes.empty() || declarations_.size()>=limit_) return;
    declarations_.try_emplace(hash,bytes.begin(),bytes.end());
  }
  const std::map<uint64_t,std::vector<uint8_t>>& declarations() const { return declarations_; }
  // Line-based on purpose: a capture is meant to be committed, diffed between
  // runs and read by a person deciding whether a new layout appeared.
  std::string Export() const {
    std::string text="# edf2027 native draw contracts v1\n";
    for(const auto& [hash,bytes]:declarations_) {
      text+="declaration "+Hex(hash)+" ";
      for(const auto byte:bytes) {
        text+="0123456789abcdef"[byte>>4];
        text+="0123456789abcdef"[byte&15];
      }
      text+="\n";
    }
    for(const auto& [contract,entry]:rejected_) text+=Line("rejected",contract,entry.draws);
    for(const auto& contract:submitted_) text+=Line("submitted",contract,0);
    return text;
  }
 private:
  static std::string Hex(uint64_t value) {
    std::string text;
    for(int shift=60;shift>=0;shift-=4) text+="0123456789abcdef"[(value>>shift)&15];
    return text;
  }
  static std::string Line(std::string_view kind,const NativeContract& contract,uint64_t draws) {
    std::string text{kind};
    text+=" "; text+=NativeContractPathName(contract.path);
    text+=" "+Hex(contract.vertex_source)+" "+Hex(contract.pixel_source)+" "+Hex(contract.declaration);
    for(const auto word:{contract.topology,contract.stride,contract.index_width,contract.elements})
      text+=" "+std::to_string(word);
    if(draws) text+=" draws="+std::to_string(draws);
    return text+"\n";
  }
  struct Entry { std::string reason; uint64_t draws; };
  std::map<uint64_t,std::vector<uint8_t>> declarations_;
  std::map<NativeContract,Entry> rejected_;
  std::set<NativeContract> submitted_;
  Counters counters_;
  size_t limit_;
};
}  // namespace edf::native

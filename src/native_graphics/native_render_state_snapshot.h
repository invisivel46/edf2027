#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <stdexcept>

namespace edf::native {
// Setter-owned CPU snapshot. External synchronization is the bridge state lock.
// Check is diagnostic only: it never learns or repairs state from a consumer.
class NativeRenderStateSnapshots {
 public:
  using Words=std::array<uint32_t,6>;
  using BlendWords=std::array<uint32_t,4>;
  static constexpr uint32_t kAllFields=63;
  struct Snapshot {
    Words words{};
    uint64_t revision=0;
    uint32_t producer=0,valid_fields=0;
    std::array<uint32_t,6> producers{};
    BlendWords blend{};
    bool blend_valid=false;
    uint32_t blend_producer=0;
  };
  enum class Result { Missing, Match, Mismatch };
  struct Counters { uint64_t publications=0,checks=0,missing=0,mismatches=0; };
  void Publish(uint32_t device,Words words,uint32_t producer,uint32_t fields=kAllFields) {
    if(!device || !fields || (fields&~kAllFields))
      throw std::runtime_error("invalid native render-state publication");
    auto& snapshot=snapshots_[device];
    for(size_t field=0;field<words.size();++field) if(fields&(1u<<field)) {
      snapshot.words[field]=words[field];
      snapshot.producers[field]=producer;
    }
    snapshot.valid_fields|=fields;
    snapshot.revision=++counters_.publications;
    snapshot.producer=producer;
  }
  const Snapshot* Find(uint32_t device) const {
    const auto found=snapshots_.find(device);
    return found==snapshots_.end()?nullptr:&found->second;
  }
  void Retire(uint32_t device) { snapshots_.erase(device); }
  void PublishBlend(uint32_t device,BlendWords words,uint32_t producer) {
    if(!device) throw std::runtime_error("invalid native blend-factor owner");
    auto& snapshot=snapshots_[device];
    snapshot.blend=words; snapshot.blend_valid=true; snapshot.blend_producer=producer;
    ++blend_counters_.publications;
  }
  BlendWords RequireBlend(uint32_t device) const {
    const auto* snapshot=Find(device);
    if(!snapshot || !snapshot->blend_valid)
      throw std::runtime_error("native blend-factor ownership is incomplete");
    return snapshot->blend;
  }
  Result CheckBlend(uint32_t device,const BlendWords& live) {
    ++blend_counters_.checks;
    const auto* snapshot=Find(device);
    if(!snapshot || !snapshot->blend_valid) {++blend_counters_.missing; return Result::Missing;}
    if(snapshot->blend!=live) {++blend_counters_.mismatches; return Result::Mismatch;}
    return Result::Match;
  }
  const Counters& blend_counters() const {return blend_counters_;}
  Words RequireOwned(uint32_t device) const {
    const auto* snapshot=Find(device);
    if(!snapshot || snapshot->valid_fields!=kAllFields)
      throw std::runtime_error("native render-state ownership is incomplete");
    return snapshot->words;
  }
  Result Check(uint32_t device,const Words& live) {
    ++counters_.checks;
    const auto* snapshot=Find(device);
    if(!snapshot || snapshot->valid_fields!=kAllFields) {++counters_.missing; return Result::Missing;}
    if(snapshot->words!=live) {++counters_.mismatches; return Result::Mismatch;}
    return Result::Match;
  }
  const Counters& counters() const {return counters_;}
 private:
  std::map<uint32_t,Snapshot> snapshots_;
  Counters counters_;
  Counters blend_counters_;
};

// Lazy live reader: the owned, non-audit path must not touch guest render words.
template<class ReadLive,class Audit>
NativeRenderStateSnapshots::Words ResolveNativeRenderState(
    const NativeRenderStateSnapshots& snapshots,uint32_t device,bool owned,bool audit,
    ReadLive&& read_live,Audit&& audit_live) {
  if(owned && !audit) return snapshots.RequireOwned(device);
  const auto live=read_live();
  if(audit) audit_live(live);
  if(!owned) return live;
  const auto native=snapshots.RequireOwned(device);
  if(native!=live) throw std::runtime_error("native render-state audit mismatch");
  return native;
}

// Literal device-field writers audited from the generated retail bodies.
// No catch-all initializer publication: its nested setters own their fields.
inline uint32_t NativeRenderStateProducerFields(uint32_t producer) {
  switch(producer) {
    case 0x82134f58: case 0x82134fe8: case 0x82135078: case 0x82135108:
    case 0x82135198: case 0x82135208: case 0x82135278: case 0x821352e8: return 1;
    case 0x82135530: case 0x82135578: case 0x821355a8: case 0x821355e8:
    case 0x82135630: case 0x82135670: case 0x821356a0: case 0x821356e0:
    case 0x82135720: case 0x82135750: case 0x82135780: case 0x821357c0:
    case 0x82135800: case 0x82137cb8: return 2;
    case 0x82134eb8: case 0x82134ee8: case 0x82135948: case 0x82135a10:
    case 0x82135ab8: case 0x82136478: case 0x8214eff8: return 4;
    case 0x82134f18: case 0x821353e8: case 0x821364c8: case 0x821364f8: return 8;
    case 0x82135b08: case 0x82135b40: case 0x82135b78: case 0x82135bb0:
    case 0x82137988: return 16;
    case 0x82137978: return 32;
    default: throw std::runtime_error("unknown native render-state producer");
  }
}
}

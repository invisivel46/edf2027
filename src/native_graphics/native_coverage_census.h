#pragma once
// Runtime coverage census of the full-frame renderer (edf_native_coverage_census).
//
// In full-frame mode the guest render helper sub_821A5080 never runs, so a
// draw it would have made that no native pass makes is silently missing. The
// passes' own counters say how many objects they skipped, but not which
// classes, and most are logged only for the first frames. The census turns
// every skip path into a named, counted item: (status, class vtable, class
// name, reason). Counting sites in the passes add items; the census folds
// them per frame and keeps, per distinct item, the frames it appeared in, the
// objects counted over those frames, the most in one frame and when it was
// first and last seen. Covered classes are counted too, so a summary shows
// what the native frame draws beside what it drops.
//
// Statuses:
//  - covered:   a native pass drew it (or deliberately owns it);
//  - uncovered: the guest helper would have drawn it and no native pass did;
//  - parity:    skipped natively where the guest skips it too (a key < 256
//               that 821A3BA0 never drains, a slot 4 that is a bare blr, a
//               group outside the published order 821C3BB8 never reaches).
//
// Log lines (tools/coverage-report.py reads them; values are cumulative from
// the census start, so the last line of a key is its total):
//   Native coverage summary: kind=<window|final> seconds=S frames=F items=N covered_items=C uncovered_items=U
//     parity_items=P covered_objects=CO uncovered_objects=UO parity_objects=PO coverage=X%
//   Native coverage: <status> class=<name> vtable=0x<hex> reason=<reason> frames=F objects=O peak=K
//     first_seen=S.Ss last_seen=S.Ss detail=<free text to end of line>
// Class, reason and detail are tokens: whitespace becomes '_'. coverage is
// covered objects over covered plus uncovered objects (parity excluded).
//
// Threading: every member is safe from any thread (one mutex). The counting
// sites run only while the cvar is on; off, each is one bool test.
#include <cstdint>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace edf::native {
enum class NativeCoverageStatus : uint8_t { Covered, Uncovered, Parity };
const char* NativeCoverageStatusName(NativeCoverageStatus status);
// One counting-site observation, pre-aggregation: `name` and `reason` are
// static strings (literals or the class table's names).
struct NativeCoverageMark {
  NativeCoverageStatus status=NativeCoverageStatus::Uncovered;
  uint32_t vtable=0;
  const char* name=nullptr;  // null: NativeCoverageClassName(vtable)
  const char* reason="";
  // A vtable slot the item is known by (slot 4, or slot 2 for a world-list
  // manager); nonzero, it is the item's detail "slot=0x<hex>".
  uint32_t slot=0;
};
// One distinct item's totals.
struct NativeCoverageItem {
  NativeCoverageStatus status=NativeCoverageStatus::Uncovered;
  uint32_t vtable=0;
  std::string name,reason,detail;
  uint64_t frames=0,objects=0,peak=0;
  double first_seen=0,last_seen=0;  // Seconds from the census origin.
};
struct NativeCoverageTotals {
  uint64_t frames=0,items=0,covered_items=0,uncovered_items=0,parity_items=0;
  uint64_t covered_objects=0,uncovered_objects=0,parity_objects=0;
  double seconds=0;
  // Covered over covered plus uncovered objects, in percent; 100 with neither.
  double coverage() const {
    const auto drawn=double(covered_objects),all=double(covered_objects+uncovered_objects);
    return all>0?100.0*drawn/all:100.0;
  }
};
// "whitespace_to_underscores", empty -> "-".
std::string NativeCoverageToken(std::string_view text);
// The class name the census prints for a vtable: the render registry's class
// table, the map-effect and effect classes, the helper's world-list managers;
// "unknown" otherwise.
std::string NativeCoverageClassName(uint32_t vtable);
// Which native pass owns a render-object class, from its vtable and its slot
// 4 (vtable+16), for a class the render registry's table does not draw:
//  StaticWorld: slot 4 820B2670 (LOD records) or 820BAF90 (clRock's fixed
//               record), what the static world pass selects, and the
//               registry's scene-source classes (clFieldParts);
//  Effects:     a slot 4 an effect builder covers (ClassifyNativeEffect);
//  MapEffects:  clSky, clElectricWire, clGrassMap (the sky pass's walk);
//  Models:      any other class in the registry's table (drawn from its snapshot);
//  Empty:       slot 4 is the bare blr 8252B718 (draws nothing);
//  None:        no native pass draws it.
enum class NativeCoverageOwner : uint8_t { None, Models, StaticWorld, Effects, MapEffects, Empty };
NativeCoverageOwner NativeCoverageSlotOwner(uint32_t vtable,uint32_t slot4);
// The helper's world-list (owner+44) managers whose slot 2 a native pass
// replaces, and the pass: clEffectObjectManager (effects), clMapObjectManager
// (static world, models), clGameObject_Manager and clGameBossObject_Manager
// (models), clMapEffectManager (sky). Null for any other vtable.
const char* NativeCoverageWorldListPass(uint32_t vtable);

class NativeCoverageCensus {
 public:
  // An observation for the frame being gathered: `objects` more of the item.
  // detail is kept from the item's first observation that carries one.
  void Add(NativeCoverageStatus status,uint32_t vtable,std::string_view name,std::string_view reason,uint64_t objects=1,
    std::string_view detail={});
  // Marks, aggregated first (one lookup per distinct mark).
  void Add(std::span<const NativeCoverageMark> marks);
  // A standing population (e.g. the registry's unknown classes), counted into
  // every frame that ends while it is set; replaces what `source` set before.
  struct Population { NativeCoverageStatus status=NativeCoverageStatus::Uncovered; uint32_t vtable=0; std::string name,reason,detail; uint64_t objects=0; };
  void SetPopulation(std::string_view source,std::vector<Population> population);
  // Folds the frame's observations (and the populations) into the totals.
  // `now` is seconds on any monotonic clock; the first call sets the origin.
  void EndFrame(double now);
  // The summary lines when `interval` seconds have passed since the last
  // periodic summary (or the origin), else none.
  std::vector<std::string> Poll(double now,double interval);
  // The summary lines now: the totals line, then uncovered, parity and covered
  // items, each by objects descending.
  std::vector<std::string> Summary(std::string_view kind) const;
  std::vector<NativeCoverageItem> Items() const;
  NativeCoverageTotals Totals() const;
  void Reset();
 private:
  using Key=std::tuple<uint8_t,uint32_t,std::string,std::string>;  // status, vtable, name, reason
  struct Pending { uint64_t objects=0; std::string detail; };
  void AddLocked(const Key& key,uint64_t objects,std::string_view detail);
  std::vector<std::string> SummaryLocked(std::string_view kind) const;
  NativeCoverageTotals TotalsLocked() const;
  mutable std::mutex mutex_;
  std::map<Key,Pending> frame_;
  std::map<Key,NativeCoverageItem> items_;
  std::map<std::string,std::vector<Population>,std::less<>> populations_;
  bool started_=false;
  double origin_=0,now_=0,last_report_=0;
  uint64_t frames_=0;
};
// Process-wide census the bridge feeds; leaked (hooks may run during shutdown).
NativeCoverageCensus& CoverageCensus();
}

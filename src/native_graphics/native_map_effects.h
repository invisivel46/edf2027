#pragma once
#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace edf::native {
// clMapEffectManager (vtable 0x82002214) keeps an intrusive list header at
// +48. Its constructor sub_820B3620 clears +0/+4 through sub_821D1880 and then
// +8/+12; sub_821A1628 links nodes at the head (+0 next, +4 prev). Each node
// carries its scene object at +8. sub_820B35A0 (reached through the slot-2
// adapter sub_820B3610 with r4=manager+48, r5=context) reads the first node and
// the end marker once, loads the object before calling sub_821C0C00 and loads
// the next link only after that call returns: a callback that unlinks or
// relinks the current node redirects the walk, and a node inserted at the head
// is not visited. The native walk keeps exactly those read points.
struct NativeMapEffectList {
  static constexpr uint32_t manager_offset=48,first=0,end=12,next=0,object=8;
};
// Object fields sub_821C0C00 dispatches on: nonzero +64 (u16) skips the object,
// +52 selects slot 4 (mode 0) or the mode-1/2 bucket path (sub_821A3B80).
struct NativeMapEffectObject {
  static constexpr uint32_t mode=52,hidden=64,render_slot=16;
};
template<class Reader,class Call>
uint32_t WalkNativeMapEffects(const Reader& reader,uint32_t list,Call&& call) {
  auto node=reader.Word(reader.Add(list,NativeMapEffectList::first));   // lwz r31,0(r4)
  const auto end=reader.Word(reader.Add(list,NativeMapEffectList::end)); // lwz r30,12(r4)
  uint32_t visited=0;
  while(node!=end) {
    call(reader.Word(reader.Add(node,NativeMapEffectList::object)));     // lwz r3,8(r31)
    node=reader.Word(reader.Add(node,NativeMapEffectList::next));        // lwz r31,0(r31), after the call
    ++visited;
  }
  return visited;
}
// RTTI names (edf-analysis classes table) of the vtables whose slot 4 is a
// scene-object render method; anything else prints as its address.
inline const char* NativeMapEffectClassName(uint32_t vtable) {
  switch(vtable) {
    case 0x82002158: return "clMapArtifact_Base";
    case 0x8200225c: return "clMapObject_Base";
    case 0x820026bc: return "clBuilding";
    case 0x82002760: return "clFieldParts";
    case 0x8200277c: return "clNameBoard";
    case 0x820027a4: return "clNameBoard_Board";
    case 0x820027c0: return "clNameBoard_Box";
    case 0x82002868: return "clSmallBoard";
    case 0x82002884: return "clSmallEtc";
    case 0x820028a0: return "clSmallPole";
    case 0x82012c78: return "clEffectEtc02";
    case 0x82012cc8: return "clEffectGlass";
    case 0x82012cec: return "clEffectEtc01";
    case 0x82019a2c: return "clObject_Base@Sgs";
    default: return nullptr;
  }
}
struct NativeMapEffectClass {
  uint32_t vtable=0,mode=0,render=0;
  auto operator<=>(const NativeMapEffectClass&) const=default;
};
// Read-only tally of the objects the walk would visit. It follows only the
// links present before the original runs, so it never observes callback
// mutations; it exists to rank what the per-object port has to cover.
class NativeMapEffectCensus {
 public:
  struct Row { NativeMapEffectClass key; uint64_t count=0,hidden=0; double percent=0; };
  template<class Reader>
  uint32_t Record(const Reader& reader,uint32_t list,uint32_t limit=1u<<20) {
    auto node=reader.Word(reader.Add(list,NativeMapEffectList::first));
    const auto end=reader.Word(reader.Add(list,NativeMapEffectList::end));
    uint32_t visited=0;
    for(;node!=end;node=reader.Word(reader.Add(node,NativeMapEffectList::next))) {
      if(visited==limit) { ++truncated_; break; }
      const auto object=reader.Word(reader.Add(node,NativeMapEffectList::object));
      const auto table=reader.Word(object);
      const NativeMapEffectClass key{table,reader.Word(reader.Add(object,NativeMapEffectObject::mode)),
        reader.Word(reader.Add(table,NativeMapEffectObject::render_slot))};
      const auto* hidden=reader.Bytes(reader.Add(object,NativeMapEffectObject::hidden),2);
      auto& tally=counts_[key];
      ++tally.count; if(hidden[0]|hidden[1]) ++tally.hidden;
      ++visited;
    }
    ++walks_; objects_+=visited;
    return visited;
  }
  std::vector<Row> Top(size_t limit) const {
    std::vector<Row> rows; rows.reserve(counts_.size());
    for(const auto& [key,tally]:counts_)
      rows.push_back({key,tally.count,tally.hidden,objects_?100.0*double(tally.count)/double(objects_):0.0});
    // Count descending; equal counts keep key order so summaries are stable.
    std::stable_sort(rows.begin(),rows.end(),[](const Row& a,const Row& b) { return a.count>b.count; });
    if(rows.size()>limit) rows.resize(limit);
    return rows;
  }
  void Reset() { counts_.clear(); walks_=objects_=truncated_=0; }
  uint64_t walks() const { return walks_; }
  uint64_t objects() const { return objects_; }
  uint64_t truncated() const { return truncated_; }
  size_t classes() const { return counts_.size(); }
 private:
  struct Tally { uint64_t count=0,hidden=0; };
  std::map<NativeMapEffectClass,Tally> counts_;
  uint64_t walks_=0,objects_=0,truncated_=0;
};
}

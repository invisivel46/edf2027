#include "native_coverage_census.h"
#include "native_full_frame_effects.h"
#include "native_map_effects.h"
#include "native_render_registry.h"
#include "native_scene_sources.h"
#include "native_scene_static_walk.h"
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace edf::native {
const char* NativeCoverageStatusName(NativeCoverageStatus status) {
  switch(status) {
    case NativeCoverageStatus::Covered: return "covered";
    case NativeCoverageStatus::Uncovered: return "uncovered";
    case NativeCoverageStatus::Parity: return "parity";
  }
  return "uncovered";
}
std::string NativeCoverageToken(std::string_view text) {
  std::string token;
  token.reserve(text.size());
  for(const char c:text) token.push_back(std::isspace(static_cast<unsigned char>(c))?'_':c);
  return token.empty()?std::string("-"):token;
}
namespace {
// Effect classes by vtable (ClassifyNativeEffect keys them by slot 4; the
// vtables are its comments', from the edf-analysis classes table), and the
// helper's world-list managers (renderer-completion-inventory.md, "World and
// camera factories").
const char* EffectOrManagerName(uint32_t vtable) {
  switch(vtable) {
    case 0x82004F6C: return "clParticle01_Limit";
    case 0x82007784: return "clParticle02";
    case 0x82012CC8: return "clEffectGlass";
    case 0x820074F4: return "clRocketAmmo01";
    case 0x820072F0: return "clAcidAmmo01";
    case 0x82007368: return "clBeamAmmo01";
    case 0x82007524: return "clRocketAmmo02";
    case 0x82007614: return "clSolidAmmo01";
    case 0x82007438: return "clLaserAmmo01";
    case 0x82007670: return "clWebAmmo01";
    case 0x82012C78: return "clEffectEtc02";
    case 0x820077BC: return "clSpark02";
    case 0x82012CEC: return "clEffectEtc01";
    case 0x82007940: return "clSmokeLine";
    case 0x820072D4: return "clEffectObjectManager";
    case 0x82002624: return "clMapObjectManager";
    case 0x8200427C: return "clGameObject_Manager";
    case 0x82003FBC: return "clGameBossObject_Manager";
    case 0x82002214: return "clMapEffectManager";
    case 0x82002830: return "clRock";
    default: return nullptr;
  }
}
}
std::string NativeCoverageClassName(uint32_t vtable) {
  if(const auto* type=FindNativeRenderClass(vtable)) return type->name;
  if(const auto* name=NativeMapEffectClassName(vtable)) return name;
  if(const auto* name=EffectOrManagerName(vtable)) return name;
  return "unknown";
}
NativeCoverageOwner NativeCoverageSlotOwner(uint32_t vtable,uint32_t slot4) {
  if(const auto* type=FindNativeRenderClass(vtable)) {
    // Tracked, but drawn by another pass: clFieldParts from the scene sources
    // (the static world), clSky by the sky pass.
    if(type->scene_source) return NativeCoverageOwner::StaticWorld;
    if(type->other_pass) return NativeCoverageOwner::MapEffects;
    return NativeCoverageOwner::Models;
  }
  if(ClassifyNativeMapEffect(vtable)!=NativeMapEffectKind::Other) return NativeCoverageOwner::MapEffects;
  if(slot4==kNativeStaticDirectRender || slot4==NativeSceneFixedRecord::render) return NativeCoverageOwner::StaticWorld;
  switch(ClassifyNativeEffect(slot4)) {
    case NativeEffectClass::Empty: return NativeCoverageOwner::Empty;
    case NativeEffectClass::Unknown: return NativeCoverageOwner::None;
    default: return NativeCoverageOwner::Effects;
  }
}
const char* NativeCoverageWorldListPass(uint32_t vtable) {
  switch(vtable) {
    case NativeEffectList::manager_vtable: return "effects";
    case 0x82002624: return "static_world";
    case 0x8200427C: case 0x82003FBC: return "models";
    case kNativeMapEffectManagerVtable: return "sky";
    default: return nullptr;
  }
}

void NativeCoverageCensus::AddLocked(const Key& key,uint64_t objects,std::string_view detail) {
  auto& pending=frame_[key];
  pending.objects+=objects;
  if(pending.detail.empty() && !detail.empty()) pending.detail=detail;
}
void NativeCoverageCensus::Add(NativeCoverageStatus status,uint32_t vtable,std::string_view name,std::string_view reason,
    uint64_t objects,std::string_view detail) {
  if(!objects) return;
  Key key{uint8_t(status),vtable,name.empty()?NativeCoverageClassName(vtable):NativeCoverageToken(name),NativeCoverageToken(reason)};
  std::lock_guard lock(mutex_);
  AddLocked(key,objects,detail);
}
void NativeCoverageCensus::Add(std::span<const NativeCoverageMark> marks) {
  if(marks.empty()) return;
  // Aggregated by the mark's identity (its strings are static) before any
  // string is built: one key per distinct mark.
  using Identity=std::tuple<uint8_t,uint32_t,const char*,const char*,uint32_t>;
  std::map<Identity,uint64_t> counts;
  for(const auto& mark:marks) ++counts[{uint8_t(mark.status),mark.vtable,mark.name,mark.reason,mark.slot}];
  struct Keyed { Key key; uint64_t count=0; std::string detail; };
  std::vector<Keyed> keyed;
  keyed.reserve(counts.size());
  for(const auto& [identity,count]:counts) {
    const auto& [status,vtable,name,reason,slot]=identity;
    Keyed item{Key{status,vtable,name?NativeCoverageToken(name):NativeCoverageClassName(vtable),NativeCoverageToken(reason?reason:"")},count,{}};
    if(slot) {
      char text[32];
      std::snprintf(text,sizeof(text),"slot=0x%08X",slot);
      item.detail=text;
    }
    keyed.push_back(std::move(item));
  }
  std::lock_guard lock(mutex_);
  for(const auto& item:keyed) AddLocked(item.key,item.count,item.detail);
}
void NativeCoverageCensus::SetPopulation(std::string_view source,std::vector<Population> population) {
  for(auto& item:population) {
    item.name=item.name.empty()?NativeCoverageClassName(item.vtable):NativeCoverageToken(item.name);
    item.reason=NativeCoverageToken(item.reason);
  }
  std::lock_guard lock(mutex_);
  if(population.empty()) {
    if(const auto found=populations_.find(source);found!=populations_.end()) populations_.erase(found);
    return;
  }
  populations_[std::string(source)]=std::move(population);
}
void NativeCoverageCensus::EndFrame(double now) {
  std::lock_guard lock(mutex_);
  if(!started_) { started_=true; origin_=now; last_report_=now; }
  now_=now;
  ++frames_;
  for(const auto& [source,population]:populations_)
    for(const auto& item:population) AddLocked(Key{uint8_t(item.status),item.vtable,item.name,item.reason},item.objects,item.detail);
  const double at=now-origin_;
  for(auto& [key,pending]:frame_) {
    if(!pending.objects) continue;
    auto [found,inserted]=items_.try_emplace(key);
    auto& item=found->second;
    if(inserted) {
      item.status=NativeCoverageStatus(std::get<0>(key)); item.vtable=std::get<1>(key);
      item.name=std::get<2>(key); item.reason=std::get<3>(key);
      item.first_seen=at;
    }
    if(item.detail.empty() && !pending.detail.empty()) item.detail=NativeCoverageToken(pending.detail);
    ++item.frames; item.objects+=pending.objects;
    item.peak=(std::max)(item.peak,pending.objects);
    item.last_seen=at;
  }
  frame_.clear();
}
NativeCoverageTotals NativeCoverageCensus::TotalsLocked() const {
  NativeCoverageTotals totals;
  totals.frames=frames_;
  totals.seconds=started_?now_-origin_:0;
  for(const auto& [key,item]:items_) {
    ++totals.items;
    switch(item.status) {
      case NativeCoverageStatus::Covered: ++totals.covered_items; totals.covered_objects+=item.objects; break;
      case NativeCoverageStatus::Uncovered: ++totals.uncovered_items; totals.uncovered_objects+=item.objects; break;
      case NativeCoverageStatus::Parity: ++totals.parity_items; totals.parity_objects+=item.objects; break;
    }
  }
  return totals;
}
NativeCoverageTotals NativeCoverageCensus::Totals() const {
  std::lock_guard lock(mutex_);
  return TotalsLocked();
}
std::vector<NativeCoverageItem> NativeCoverageCensus::Items() const {
  std::vector<NativeCoverageItem> items;
  {
    std::lock_guard lock(mutex_);
    items.reserve(items_.size());
    for(const auto& [key,item]:items_) items.push_back(item);
  }
  // Uncovered, parity, covered; each by objects descending, then name and reason.
  const auto rank=[](NativeCoverageStatus status) {
    return status==NativeCoverageStatus::Uncovered?0:status==NativeCoverageStatus::Parity?1:2;
  };
  std::stable_sort(items.begin(),items.end(),[&](const NativeCoverageItem& a,const NativeCoverageItem& b) {
    if(rank(a.status)!=rank(b.status)) return rank(a.status)<rank(b.status);
    if(a.objects!=b.objects) return a.objects>b.objects;
    return std::tie(a.name,a.reason,a.vtable)<std::tie(b.name,b.reason,b.vtable);
  });
  return items;
}
std::vector<std::string> NativeCoverageCensus::SummaryLocked(std::string_view kind) const {
  std::vector<std::string> lines;
  const auto totals=TotalsLocked();
  char buffer[512];
  std::snprintf(buffer,sizeof(buffer),
    "Native coverage summary: kind=%s seconds=%.1f frames=%llu items=%llu covered_items=%llu uncovered_items=%llu parity_items=%llu "
    "covered_objects=%llu uncovered_objects=%llu parity_objects=%llu coverage=%.3f%%",
    NativeCoverageToken(kind).c_str(),totals.seconds,(unsigned long long)totals.frames,(unsigned long long)totals.items,
    (unsigned long long)totals.covered_items,(unsigned long long)totals.uncovered_items,(unsigned long long)totals.parity_items,
    (unsigned long long)totals.covered_objects,(unsigned long long)totals.uncovered_objects,(unsigned long long)totals.parity_objects,
    totals.coverage());
  lines.emplace_back(buffer);
  return lines;
}
std::vector<std::string> NativeCoverageCensus::Summary(std::string_view kind) const {
  std::vector<std::string> lines;
  {
    std::lock_guard lock(mutex_);
    lines=SummaryLocked(kind);
  }
  char buffer[512];
  for(const auto& item:Items()) {
    std::snprintf(buffer,sizeof(buffer),
      "Native coverage: %s class=%s vtable=0x%08X reason=%s frames=%llu objects=%llu peak=%llu first_seen=%.1fs last_seen=%.1fs detail=",
      NativeCoverageStatusName(item.status),item.name.c_str(),item.vtable,item.reason.c_str(),(unsigned long long)item.frames,
      (unsigned long long)item.objects,(unsigned long long)item.peak,item.first_seen,item.last_seen);
    lines.push_back(std::string(buffer)+(item.detail.empty()?std::string("-"):item.detail));
  }
  return lines;
}
std::vector<std::string> NativeCoverageCensus::Poll(double now,double interval) {
  {
    std::lock_guard lock(mutex_);
    if(!started_ || now-last_report_<interval) return {};
    last_report_=now;
  }
  return Summary("window");
}
void NativeCoverageCensus::Reset() {
  std::lock_guard lock(mutex_);
  frame_.clear(); items_.clear(); populations_.clear();
  started_=false; origin_=now_=last_report_=0; frames_=0;
}
NativeCoverageCensus& CoverageCensus() { static auto* census=new NativeCoverageCensus; return *census; }
}

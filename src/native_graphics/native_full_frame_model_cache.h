#pragma once
#include "native_full_frame_base_state.h"
#include "native_static_world_cache.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

namespace edf::native {
// A source generation no stored value is valid at: the table fetches afresh.
inline constexpr uint64_t kNativeFullFrameModelUnversioned=UINT64_MAX;
// Cross-frame side table of the full-frame Models pass: the program and
// geometry of each (pass record, batch, layout), fetched from the providers
// once per source generation instead of once per draw per frame. The layout
// is the registry's immutable NativeModelLayout object, one per (instance,
// layout generation); it is held, so its address is never reused while its
// row lives. generation is the providers' own change signal
// (NativeFullFrameModelSources::generation): a value is reused only when it
// was fetched at the current generation; kNativeFullFrameModelUnversioned
// never matches. A fetched value is stored only when keep accepts it (both
// sources present), so a missing source is asked again. Not synchronized.
template<class Value>
class NativeFullFrameModelSourceTable {
 public:
  template<class Fetch,class Keep>
  Value Get(uint32_t pass,uint32_t batch,const std::shared_ptr<const void>& layout,uint64_t generation,Fetch&& fetch,Keep&& keep) {
    const auto key=std::tuple(pass,batch,layout.get());
    if(const auto found=entries_.find(key);found!=entries_.end() && generation!=kNativeFullFrameModelUnversioned &&
       found->second.generation==generation) {
      found->second.used=frame_; ++hits;
      return found->second.value;
    }
    Value value=fetch();
    ++fetches;
    if(generation!=kNativeFullFrameModelUnversioned && keep(value))
      entries_.insert_or_assign(key,Entry{layout,value,generation,frame_});
    else entries_.erase(key);
    return value;
  }
  // Once per frame: rows unused for kAge frames (unloaded or no longer
  // visible layouts) release what they hold; past kLimit everything goes.
  void EndFrame() {
    if(entries_.size()>kLimit) { entries_.clear(); return; }
    if(++frame_%kAge) return;
    std::erase_if(entries_,[&](const auto& item) { return frame_-item.second.used>kAge; });
  }
  void Clear() { entries_.clear(); }
  size_t size() const { return entries_.size(); }
  uint64_t hits=0,fetches=0;
 private:
  struct Entry {
    std::shared_ptr<const void> layout;
    Value value{};
    uint64_t generation=0,used=0;
  };
  static constexpr size_t kLimit=16384;
  static constexpr uint64_t kAge=256;
  std::map<std::tuple<uint32_t,uint32_t,const void*>,Entry> entries_;
  uint64_t frame_=0;
};
// Every input of one model draw's resolve other than the pass constants,
// compared whole, as NativeStaticWorldGroupKey is for a static group:
// program.Resolve's pipeline description (vertex/pixel ids and shaders from
// program, input layout from geometry, target formats, samples and reverse
// depth from targets, render state from base after the program's operations),
// its samplers (base, program, filtering) and the backend caches behind them
// (program.backend, never cleared, so a description returns the same object).
// program and geometry are the providers' objects: a rebuilt program or a
// reloaded geometry is a new object, so their identities are the program and
// geometry generations. Both are held, so their addresses are never reused.
struct NativeFullFrameModelMaterialKey {
  uint32_t pass=0;
  bool skinned=false;
  std::shared_ptr<const void> program,geometry;
  NativeSceneMaterialPassState base;
  NativeFullFramePassTargets targets;
  int filtering=-1;
  bool operator==(const NativeFullFrameModelMaterialKey&) const=default;
};
// Cross-frame material cache of the full-frame Models pass, one row per
// (pass record, program, geometry, skinned). Material is the resolved
// pipeline half (pipeline, sampler objects, blend factor, scissor) and, for a
// rigid draw, the interned capture. Rigid rows also keep the pass constants
// the capture was made from: Current accepts a frame's constants when every
// one is equal except those that only feed the capture's camera, which it
// then derives from them (NativeStaticCaptureCamera, as the static world
// cache does for its camera constants). The rigid world is not a constant
// here: the program provider never refreshes g_mWorld, and each instance
// carries its own world (instanced like the static pass). Skinned rows keep
// the same: their pass constants before any palette is bound, and the capture
// of those each draw derives its palette-bound material from. Not synchronized.
template<class Material>
class NativeFullFrameModelMaterialCache {
 public:
  using Key=NativeFullFrameModelMaterialKey;
  using Constants=std::vector<NativeSceneMaterialInputs::Constant>;
  struct Entry {
    Key key;
    Constants constants;
    std::vector<uint8_t> camera;  // Per constant: only reaches the capture as its camera.
    bool derived=false;
    Material material{};
    uint64_t used=0;
  };
  // The row when every key component matches, else null.
  Entry* Candidate(const Key& key) {
    const auto found=entries_.find(Slot(key));
    if(found==entries_.end() || !(found->second.key==key)) return nullptr;
    found->second.used=frame_;
    return &found->second;
  }
  // Whether entry's capture is the one these pass constants make; camera
  // receives the derived camera when the row derives it (else it is left).
  static bool Current(const Entry& entry,std::span<const NativeSceneMaterialInputs::Constant> constants,
      const NativeSceneMaterial* material,NativeSceneView& camera) {
    if(constants.size()!=entry.constants.size()) return false;
    for(size_t i=0;i<constants.size();++i) {
      const auto& a=constants[i]; const auto& b=entry.constants[i];
      if(entry.derived && entry.camera[i]) {
        if(a.pixel!=b.pixel || a.global!=b.global || a.name!=b.name || a.registers.size()!=b.registers.size()) return false;
      } else if(!(a==b)) return false;
    }
    if(!entry.derived) return true;
    if(!material) return false;
    const auto derived=NativeStaticCaptureCamera(*material,constants);
    if(!derived) return false;
    camera.view=derived->view; camera.projection=derived->projection; camera.view_projection=derived->view_projection;
    return true;
  }
  // Records a resolve. The camera is derived on reuse only when deriving it
  // from these constants reproduces the capture's camera bit for bit;
  // otherwise, or with no capture, every camera constant must match.
  Entry& Store(Key key,Constants constants,Material material,const NativeSceneMaterial* captured,const NativeSceneView& camera) {
    Entry entry{std::move(key),std::move(constants)};
    const auto derived=captured?NativeStaticCaptureCamera(*captured,entry.constants,&entry.camera):std::nullopt;
    entry.derived=derived && NativeSceneCameraIdentical(*derived,camera);
    if(!entry.derived) entry.camera.clear();
    entry.material=std::move(material); entry.used=frame_;
    ++stores;
    const auto slot=Slot(entry.key);
    return entries_.insert_or_assign(slot,std::move(entry)).first->second;
  }
  // Once per frame, as NativeFullFrameModelSourceTable::EndFrame.
  void EndFrame() {
    if(entries_.size()>kLimit) { entries_.clear(); return; }
    if(++frame_%kAge) return;
    std::erase_if(entries_,[&](const auto& item) { return frame_-item.second.used>kAge; });
  }
  void Clear() { entries_.clear(); }
  size_t size() const { return entries_.size(); }
  uint64_t hits=0,misses=0,stores=0;
 private:
  using SlotKey=std::tuple<uint32_t,const void*,const void*,bool>;
  static SlotKey Slot(const Key& key) { return {key.pass,key.program.get(),key.geometry.get(),key.skinned}; }
  static constexpr size_t kLimit=16384;
  static constexpr uint64_t kAge=256;
  std::map<SlotKey,Entry> entries_;
  uint64_t frame_=0;
};
// The host's side of NativeFullFrameModelSources: the change signal
// (generation) and a memo in front of the program and geometry providers, so
// the signal advances only when a provider would now return something else,
// not whenever time passed.
//
// Every provider result handed out since the last advance is remembered: one
// program per pass record, one geometry per GeometryKey (whatever identifies
// the geometry provider's inputs; input is what it is called with). Validate,
// once per Build before any row is asked for, compares host (every host
// identity the caller wants compared whole; a change drops everything) and
// then asks the providers again for every remembered result, in chunks, each
// chunk inside one run(work) call (the host's lock slice). The providers'
// results are shared objects that are replaced, never mutated, whenever what
// they return changes (a rebuilt program, refreshed constant values, reloaded
// geometry), and they are held here, so an address is never reused: a result
// that is not the same object, or null, or a provider that throws, advances
// the generation. The signal is thus exactly as complete as the providers'
// own checks, whoever wrote their inputs (a simulation step, a 821A4DE8
// listener on a render-only iteration, a loader): a row the table reuses at
// the current generation was fetched at it, so its results are remembered
// here, and every one of them was just asked again.
//
// On an advance the results just asked for are carried: a row refetched at
// the new generation is served from them without a provider call (null
// results are not carried: those are asked again). What no row asks for
// before the next Validate is dropped, so what is remembered is the working
// set of rows current at this generation. A result no row asked for in the
// last `recent` Validates is stale (its rows are no longer drawn); while some
// are, past max(`limit`, twice the live results) results, or after `age`
// Validates without an advance, the generation advances anyway (carrying, so
// it costs no provider call) to shed them. The bound follows the live working
// set, so a working set over `limit` is not shed (and re-sourced) at every
// Validate, and a memo with nothing stale never advances for pruning. Not
// synchronized.
template<class Host,class Program,class GeometryKey,class GeometryInput,class Geometry>
class NativeFullFrameModelSourceMemo {
 public:
  struct Stats { uint64_t validates=0,validated=0,advances=0,host_changes=0,changes=0,prunes=0,reuses=0,fetches=0,slices=0; };
  explicit NativeFullFrameModelSourceMemo(size_t chunk=16,size_t limit=2048,uint64_t age=256,uint64_t recent=8)
    :chunk_(chunk?chunk:1),limit_(limit),age_(age),recent_(recent?recent:1) {}
  template<class HostFn,class ProgramFn,class GeometryFn,class Run>
  uint64_t Validate(HostFn&& host,ProgramFn&& program,GeometryFn&& geometry,Run&& run) {
    ++stats_.validates;
    carried_programs_.clear(); carried_geometry_.clear();
    bool host_changed=false,changed=false,first=true;
    size_t live=0;
    const auto current=[&](uint64_t asked) { return asked+recent_>=stats_.validates; };
    std::map<uint32_t,Program> fresh_programs;
    std::map<GeometryKey,std::pair<GeometryInput,Geometry>> fresh_geometry;
    auto p=programs_.begin(); auto g=geometry_.begin();
    while(first || p!=programs_.end() || g!=geometry_.end()) {
      ++stats_.slices;
      run([&] {
        if(first) {
          first=false;
          auto now=host();
          if(!host_ || !(*host_==now)) { host_=std::move(now); host_changed=true; return; }
        }
        for(size_t n=0;n<chunk_ && (p!=programs_.end() || g!=geometry_.end());++n) {
          ++stats_.validated;
          if(p!=programs_.end()) {
            Program now{};
            try { now=program(p->first); } catch(...) { now=Program{}; }
            if(!now || now!=p->second.value) changed=true;
            live+=current(p->second.asked);
            if(now) fresh_programs.emplace(p->first,std::move(now));
            ++p;
          } else {
            Geometry now{};
            try { now=geometry(g->second.input); } catch(...) { now=Geometry{}; }
            if(!now || now!=g->second.value) changed=true;
            live+=current(g->second.asked);
            if(now) fresh_geometry.emplace(g->first,std::pair<GeometryInput,Geometry>{g->second.input,std::move(now)});
            ++g;
          }
        }
      });
      if(host_changed) break;
    }
    bool prune=false;
    if(!host_changed && !changed) {
      const size_t size=programs_.size()+geometry_.size();
      const bool aged=age_ && ++idle_>=age_;
      prune=live<size && (size>(std::max)(limit_,2*live) || aged);
    }
    if(host_changed || changed || prune) {
      ++generation_; ++stats_.advances; idle_=0;
      if(host_changed) ++stats_.host_changes; else if(changed) ++stats_.changes; else ++stats_.prunes;
      programs_.clear(); geometry_.clear();
      if(!host_changed) { carried_programs_=std::move(fresh_programs); carried_geometry_=std::move(fresh_geometry); }
    }
    return generation_;
  }
  // The program of pass: remembered, carried, or fetch() (remembered unless null).
  template<class Fetch>
  Program ProgramFor(uint32_t pass,Fetch&& fetch) {
    if(const auto found=programs_.find(pass);found!=programs_.end()) {
      ++stats_.reuses; found->second.asked=stats_.validates;
      return found->second.value;
    }
    if(const auto carried=carried_programs_.find(pass);carried!=carried_programs_.end()) {
      auto value=carried->second; carried_programs_.erase(carried);
      programs_.emplace(pass,Remembered{value,stats_.validates}); ++stats_.reuses;
      return value;
    }
    Program value=fetch(); ++stats_.fetches;
    if(value) programs_.emplace(pass,Remembered{value,stats_.validates});
    return value;
  }
  // The geometry of key (called with input): remembered, carried, or fetch().
  template<class Fetch>
  Geometry GeometryFor(const GeometryKey& key,const GeometryInput& input,Fetch&& fetch) {
    if(const auto found=geometry_.find(key);found!=geometry_.end()) {
      ++stats_.reuses; found->second.asked=stats_.validates;
      return found->second.value;
    }
    if(const auto carried=carried_geometry_.find(key);carried!=carried_geometry_.end()) {
      auto value=carried->second.second; carried_geometry_.erase(carried);
      geometry_.emplace(key,RememberedGeometry{input,value,stats_.validates}); ++stats_.reuses;
      return value;
    }
    Geometry value=fetch(); ++stats_.fetches;
    if(value) geometry_.emplace(key,RememberedGeometry{input,value,stats_.validates});
    return value;
  }
  uint64_t generation() const { return generation_; }
  size_t size() const { return programs_.size()+geometry_.size(); }
  const Stats& stats() const { return stats_; }
 private:
  // asked: the Validate count when a row last asked for it.
  struct Remembered { Program value; uint64_t asked=0; };
  struct RememberedGeometry { GeometryInput input; Geometry value; uint64_t asked=0; };
  size_t chunk_,limit_;
  uint64_t age_,recent_,idle_=0,generation_=0;
  std::optional<Host> host_;
  std::map<uint32_t,Remembered> programs_;
  std::map<uint32_t,Program> carried_programs_;
  std::map<GeometryKey,RememberedGeometry> geometry_;
  std::map<GeometryKey,std::pair<GeometryInput,Geometry>> carried_geometry_;
  Stats stats_;
};
}

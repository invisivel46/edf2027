#pragma once
#include "native_full_frame_base_state.h"
#include "native_static_world_cache.h"
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
}

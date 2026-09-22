#pragma once
#include "native_scene_adapter.h"
#include "native_queued_scene.h"
#include "native_material_render_state.h"
#include "native_material_sampler.h"
#include "native_material_cpu_program.h"
#include "native_shader_binding.h"
#include "native_texture_binding.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace edf::native {
// Why a published static group ran through the guest group callback 821D96D8
// instead of the native world pass.
enum class NativeStaticWorldFallback : uint32_t {
  GuestQueue, Program, Scissor, Eligibility, PassState, Instance, Count
};
inline constexpr std::array<const char*,size_t(NativeStaticWorldFallback::Count)> kNativeStaticWorldFallbackNames{
  "guest_queue","program","scissor","eligibility","pass_state","instance"
};
struct NativeStaticWorldPassCounters {
  uint64_t passes=0,original=0,native_groups=0,empty_groups=0,instances=0,draws=0,batches=0,handoffs=0,replays=0,binds=0,composed=0;
  std::array<uint64_t,size_t(NativeStaticWorldFallback::Count)> fallbacks{};
};
// The published walk of owner+240, or null when the original 821C3BB8 must run:
// no publication, no order for this owner, queues materialized to the guest, or
// a native selection for a group outside the order (a stale order would leave
// it undrawn and the frame's queues non-empty).
inline std::shared_ptr<const NativeSceneGroupOrder> NativeStaticWorldOrder(
    const NativeScenePublication* publication,uint32_t owner,const NativeSceneQueues* queues) {
  if(!publication || !queues || !queues->enabled) return nullptr;
  const auto* found=publication->group_order.Find(owner);
  if(!found || !*found) return nullptr;
  std::vector<uint32_t> sorted((*found)->begin(),(*found)->end());
  std::ranges::sort(sorted);
  if(!queues->Within([&](uint32_t group) { return std::ranges::binary_search(sorted,group); })) return nullptr;
  return *found;
}
// Published order, one decision per group. native returns a fallback reason
// without having changed guest or queue state; the owed device state of every
// native group so far is handed off before any guest group and at pass end.
template<class Native,class Fallback,class Handoff>
void WalkNativeStaticWorld(std::span<const uint32_t> order,Native&& native,Fallback&& fallback,Handoff&& handoff) {
  for(const auto group:order) {
    const std::optional<NativeStaticWorldFallback> reason=native(group);
    if(!reason) continue;
    handoff(); fallback(group,*reason);
  }
  handoff();
}
// The CPU effects of one group's handoff bind (ActivateNativeMaterial with
// states=false), read from its material once and composed without running it.
// In activation order:
//  shaders   vertex then pixel, as SetNativeShaderResource: retire the bound
//            shader (+12420 / +12416), store the new one, clear +10810 bit 7
//            (vertex), OR +16 bit 51 (vertex) or bits 52 and 49 (pixel); then
//            the shader's defaults: clear +0 / +8 bits, OR +24 bit 1, masked
//            words at +1024+offset.
//  constants float4 copies to +(first+112 | first+368)*16 and the register
//            mask OR'd into +0 / +8 (vertex / pixel).
//  textures  as SetNativeTextureResource: a non-null handle rewrites the slot
//            record (+1024+slot*24) from its header over the preserved record
//            bits and ORs +16 bit 43-slot; the handle is stored at +12272+slot*4
//            and the handle it replaces retires. Then the sampler step: +12 and
//            +16 of the record from the record and the operation, bit 43-slot again.
// Globals are read through their records when composed, as the activation's
// program read does per call: a constant's storage (data holds the record) and
// a texture's source, handle and sampler settings (handle holds the record).
struct NativeStaticWorldBindEffects {
  struct Constant { bool pixel=false,global=false; uint32_t first=0,count=0,data=0; uint64_t mask=0; };
  struct Texture { bool global=false; uint32_t slot=0,handle=0; NativeMaterialSamplerOperation sampler; };
  std::array<uint32_t,2> shaders{};
  std::array<NativeShaderDefaults,2> defaults;
  std::vector<Constant> constants;
  std::vector<Texture> textures;
};
// Same reads and validation as the activation's ReadNativeMaterialCpuProgram,
// keeping the records globals are read through.
template<class Reader>
NativeStaticWorldBindEffects ReadNativeStaticWorldBindEffects(const Reader& reader,uint32_t instance,uint32_t device) {
  const auto program=ReadNativeMaterialCpuProgram(reader,instance,device);
  NativeStaticWorldBindEffects result;
  result.shaders={program.vertex,program.pixel};
  for(const bool pixel:{false,true}) result.defaults[pixel]=ReadNativeShaderDefaults(reader,result.shaders[pixel],pixel);
  size_t next=0;
  for(const auto offset:std::array<uint32_t,4>{0,24,36,60}) {
    const auto header=ReadGuestWords<3>(reader,reader.Add(instance,offset));
    const bool global=offset==24 || offset==60;
    for(uint32_t i=0;i<header[2];++i) {
      if(next>=program.constants.size()) throw std::runtime_error("native bind effects constant count changed");
      const auto& operation=program.constants[next++];
      const auto first=operation.first/4,last=(operation.first+operation.count-1)/4;
      result.constants.push_back({operation.pixel,global,operation.first,operation.count,
        global?reader.Word(reader.Add(header[0],i*16)):operation.data,(~uint64_t(0)>>first)&(~uint64_t(0)<<(63-last))});
    }
  }
  if(next!=program.constants.size()) throw std::runtime_error("native bind effects constant count changed");
  next=0;
  for(const bool global:{false,true}) {
    const auto header=ReadGuestWords<3>(reader,reader.Add(instance,global?84:72));
    for(uint32_t i=0;i<header[2];++i) {
      if(next>=program.textures.size()) throw std::runtime_error("native bind effects texture count changed");
      const auto& operation=program.textures[next++];
      result.textures.push_back({global,operation.slot,global?reader.Add(header[0],i*8):operation.handle,operation.sampler});
    }
  }
  if(next!=program.textures.size()) throw std::runtime_error("native bind effects texture count changed");
  return result;
}
// Host copy of the device block for a run of composed binds: a word is read
// from the guest once, stores stay here, and Flush writes each changed word
// once (the register and sampler mirrors as one validated copy per run and
// page), then forgets everything. Memory outside the block passes through
// immediately, in order: retirement stamps, queue entries, handle and source
// reads. Flush before any guest call or direct device access.
template<class Reader>
class NativeStaticWorldDeviceOverlay {
 public:
  static constexpr uint32_t kBytes=13520,kWords=kBytes/4,kMirrorBegin=1024,kMirrorEnd=(368+256)*16;
  NativeStaticWorldDeviceOverlay(const Reader& reader,uint32_t device):reader_(reader),device_(device) {}
  uint32_t Add(uint32_t address,uint32_t offset) const { return reader_.Add(address,offset); }
  const uint8_t* Bytes(uint32_t address,size_t size) const {
    const auto offset=Inside(address,size);
    if(!offset) return reader_.Bytes(address,size);
    Load(*offset,size);
    return bytes_.data()+*offset;
  }
  uint32_t Word(uint32_t address) const { return GuestBlockWord(Bytes(address,4)); }
  uint64_t DoubleWord(uint32_t address) const { return (uint64_t(Word(address))<<32)|Word(reader_.Add(address,4)); }
  void StoreWord(uint32_t address,uint32_t value) const {
    const auto offset=Inside(address,4);
    if(!offset) { reader_.StoreWord(address,value); return; }
    const std::array<uint8_t,4> raw{uint8_t(value>>24),uint8_t(value>>16),uint8_t(value>>8),uint8_t(value)};
    Put(*offset,raw.data(),4);
  }
  void StoreDoubleWord(uint32_t address,uint64_t value) const {
    if(!Inside(address,8)) { reader_.StoreDoubleWord(address,value); return; }
    StoreWord(address,uint32_t(value>>32)); StoreWord(reader_.Add(address,4),uint32_t(value));
  }
  void StoreByte(uint32_t address,uint8_t value) const {
    const auto offset=Inside(address,1);
    if(!offset) { reader_.StoreByte(address,value); return; }
    Load(*offset&~3u,4);
    bytes_[*offset]=value; state_[*offset/4]=kDirty; dirty_=true;
  }
  // Whole words in guest byte order, not loaded first: every byte is replaced.
  void StoreBytes(uint32_t address,const uint8_t* data,size_t size) const {
    const auto offset=Inside(address,size);
    if(!offset) throw std::runtime_error("native static world bytes store outside the device block");
    Put(*offset,data,size);
  }
  void Flush() const {
    if(!touched_) return;
    for(uint32_t word=0;word<kWords;) {
      if(state_[word]!=kDirty) { ++word; continue; }
      auto end=word+1;
      if(word*4>=kMirrorBegin && word*4<kMirrorEnd)
        while(end<kWords && end*4<kMirrorEnd && state_[end]==kDirty && (device_+end*4)%4096) ++end;
      const auto at=reader_.Add(device_,word*4);
      if(end-word>1) std::memcpy(const_cast<uint8_t*>(reader_.WritableBytes(at,(end-word)*4,4)),bytes_.data()+word*4,(end-word)*4);
      else reader_.StoreWord(at,GuestBlockWord(bytes_.data()+word*4));
      word=end;
    }
    state_.fill(kAbsent); touched_=dirty_=false;
  }
  bool dirty() const { return dirty_; }
 private:
  enum : uint8_t { kAbsent, kClean, kDirty };
  std::optional<uint32_t> Inside(uint32_t address,size_t size) const {
    const uint64_t begin=address,end=begin+size,block=device_;
    if(end<=block || begin>=block+kBytes) return std::nullopt;
    if(begin<block || end>block+kBytes) throw std::runtime_error("native static world access straddles the device block");
    return uint32_t(begin-block);
  }
  void Load(uint32_t offset,size_t size) const {
    for(uint32_t word=offset/4,last=uint32_t((offset+size+3)/4);word<last;) {
      if(state_[word]!=kAbsent) { ++word; continue; }
      auto end=word+1;
      while(end<last && state_[end]==kAbsent && (device_+end*4)%4096) ++end;
      std::memcpy(bytes_.data()+word*4,reader_.Bytes(reader_.Add(device_,word*4),(end-word)*4),(end-word)*4);
      std::fill(state_.begin()+word,state_.begin()+end,uint8_t(kClean));
      touched_=true; word=end;
    }
  }
  void Put(uint32_t offset,const uint8_t* data,size_t size) const {
    if((offset|size)&3) throw std::runtime_error("unaligned native static world device store");
    std::memcpy(bytes_.data()+offset,data,size);
    std::fill_n(state_.begin()+offset/4,size/4,uint8_t(kDirty));
    touched_=dirty_=true;
  }
  const Reader& reader_;
  uint32_t device_;
  mutable std::array<uint8_t,kBytes> bytes_{};
  mutable std::array<uint8_t,kWords> state_{};
  mutable bool touched_=false,dirty_=false;
};
// Which setter a composed retirement stands for: the retirement allocation
// call and the legacy tag word come from that setter's frame.
enum class NativeStaticWorldRetirement { Vertex, Pixel, Texture };
// One bind, composed on the overlay in activation order. Globals and constant
// sources resolve first, as the activation's program read does. Returns false
// before any store when an upload would take the guest's aliased vector path
// (unaligned or overlapping destination); that group binds through the guest.
template<class Overlay,class Reserve,class Tag>
bool ComposeNativeStaticWorldBind(const Overlay& memory,uint32_t device,const NativeStaticWorldBindEffects& effects,
    Reserve&& reserve,Tag&& tag) {
  using R=NativeStaticWorldRetirement;
  std::vector<uint32_t> sources;
  sources.reserve(effects.constants.size());
  for(const auto& constant:effects.constants) {
    const auto data=constant.global?memory.Word(constant.data):constant.data;
    const uint64_t destination=memory.Add(device,(constant.first+(constant.pixel?368:112))*16),bytes=uint64_t(constant.count)*16;
    if((destination&15) || !(data+bytes<=destination || destination+bytes<=data)) return false;
    sources.push_back(data);
  }
  auto textures=effects.textures;
  for(auto& texture:textures) if(texture.global) {
    const auto source=memory.Word(texture.handle);
    texture.handle=memory.Word(memory.Add(source,28));
    texture.sampler.settings=ReadGuestWords<4>(memory,memory.Add(source,32));
  }
  for(const bool pixel:{false,true}) {
    const auto kind=pixel?R::Pixel:R::Vertex;
    SetNativeShaderResource(memory,device,effects.shaders[pixel],pixel,[&] { return reserve(kind); },
      [&] { return tag(kind); },&effects.defaults[pixel]);
  }
  for(size_t i=0;i<effects.constants.size();++i) {
    const auto& constant=effects.constants[i];
    const size_t bytes=size_t(constant.count)*16;
    memory.StoreBytes(memory.Add(device,(constant.first+(constant.pixel?368:112))*16),memory.Bytes(sources[i],bytes),bytes);
    const auto dirty=memory.Add(device,constant.pixel?8:0);
    memory.StoreDoubleWord(dirty,memory.DoubleWord(dirty)|constant.mask);
  }
  for(const auto& texture:textures) {
    const uint64_t mask=uint64_t(1)<<(43-texture.slot);
    SetNativeTextureResource(memory,device,texture.slot,texture.handle,mask,[&] { return reserve(R::Texture); },
      [&] { return tag(R::Texture); });
    const auto resolved=ApplyNativeMaterialSampler(ReadNativeMaterialSamplerPass(memory,device,texture.slot),texture.sampler);
    memory.StoreWord(memory.Add(device,1036+texture.slot*24),resolved.words[1]);
    memory.StoreWord(memory.Add(device,1040+texture.slot*24),resolved.words[2]);
    const auto dirty=memory.Add(device,16);
    memory.StoreDoubleWord(dirty,memory.DoubleWord(dirty)|mask);
  }
  return true;
}
// One native group owed to the guest device: its ordered state operations, the
// sampler slots its textures touch (bit per slot) and, when known, its bind
// effects (a group without them binds through the guest activation).
struct NativeStaticWorldHandoffGroup {
  std::span<const std::array<uint32_t,2>> states;
  uint32_t slots=0;
  const NativeStaticWorldBindEffects* effects=nullptr;
};
// Activations replayed whole at handoff, in walk order: the last group
// (shaders, constants, its textures) and the last group to touch each sampler
// slot (bound texture and its setter publications). Other groups bind only.
inline std::vector<size_t> NativeStaticWorldReplayPlan(std::span<const NativeStaticWorldHandoffGroup> groups) {
  std::vector<size_t> result;
  uint32_t seen=0;
  for(size_t i=groups.size();i--;) {
    if(i+1==groups.size() || (groups[i].slots&~seen)) result.push_back(i);
    seen|=groups[i].slots;
  }
  std::ranges::reverse(result);
  return result;
}
// Combined CPU mirror writes of every state operation in the run, as the
// sequential setters would leave them: last value per device offset, dirty
// masks OR'd, and each operation offset once (last occurrence) for publication.
struct NativeStaticWorldStateWrites {
  std::vector<std::pair<uint32_t,uint32_t>> words;
  uint64_t dirty16=0,dirty24=0;
  std::vector<uint32_t> operations;
  NativeMaterialRenderPass render;
};
inline NativeStaticWorldStateWrites NativeStaticWorldStateHandoff(NativeMaterialRenderPass pass,
    std::span<const NativeStaticWorldHandoffGroup> groups) {
  NativeStaticWorldStateWrites result;
  for(const auto& group:groups) for(const auto& [offset,value]:group.states) {
    const auto writes=NativeMaterialStateCpuWrites(pass,offset,value,0,0);
    if(!writes) throw std::runtime_error("native static world handoff has a scissor state callback");
    ApplyNativeMaterialState(pass,offset,value);
    for(const auto& [address,word]:*writes) {
      if(address==16) result.dirty16|=uint64_t(word)<<32;
      else if(address==20) result.dirty16|=word;
      else if(address==24) result.dirty24|=uint64_t(word)<<32;
      else if(address==28) result.dirty24|=word;
      else {
        const auto found=std::ranges::find(result.words,address,&std::pair<uint32_t,uint32_t>::first);
        if(found!=result.words.end()) found->second=word; else result.words.emplace_back(address,word);
      }
    }
    std::erase(result.operations,offset); result.operations.push_back(offset);
  }
  result.render=pass;
  return result;
}
// Leaves the device mirrors as the skipped guest groups would. In walk order,
// replay the planned activations (full 821B94E8: host shader bindings, setter
// publications) and compose every other group's bind, the activation minus its
// state operations, from its effects on a host overlay of the device block
// (ComposeNativeStaticWorldBind); a group without effects, or one whose upload
// needs the guest's aliased ordering, runs bind instead. The overlay is flushed
// before every replay, bind and retirement allocation, so each guest call sees
// the sequential device. Each shader and texture bind retires the handle it
// replaces, in guest order, through the resource fence at +8 (Release 82134220
// and Lock 82134408 block on it) or the deferred queue, with the current fence:
// no guest fence is inserted inside a native run, so it is the value the
// sequential bind would have read or a later one, never an earlier one. The
// retirement's allocation and legacy tag come from reserve(kind) and tag(kind),
// as the setter's frame under ActivateNativeMaterial would supply them.
// Then the combined render words and dirty masks, and the combined sampler
// words of every touched slot, go on the same overlay: every device word the
// composed run changes is written once. Descriptor words come from the last
// non-null binder of each slot, as a null bind leaves them. The state writes
// are computed before any write, so a throw there changes nothing.
template<class Reader,class Replay,class Bind,class Reserve,class Tag>
NativeStaticWorldStateWrites HandOffNativeStaticWorld(const Reader& reader,uint32_t device,
    const NativeMaterialRenderPass& start,const std::array<NativeMaterialSamplerPass,16>& samplers,
    std::span<const NativeStaticWorldHandoffGroup> groups,Replay&& replay,Bind&& bind,Reserve&& reserve,Tag&& tag) {
  auto writes=NativeStaticWorldStateHandoff(start,groups);
  const auto plan=NativeStaticWorldReplayPlan(groups);
  const NativeStaticWorldDeviceOverlay<Reader> memory(reader,device);
  const auto allocate=[&](NativeStaticWorldRetirement kind) -> uint32_t { memory.Flush(); return reserve(kind); };
  for(size_t index=0,next=0;index<groups.size();++index) {
    if(next<plan.size() && plan[next]==index) { memory.Flush(); replay(index); ++next; }
    else if(!groups[index].effects || !ComposeNativeStaticWorldBind(memory,device,*groups[index].effects,allocate,tag)) {
      memory.Flush(); bind(index);
    }
  }
  for(const auto& [offset,value]:writes.words) memory.StoreWord(memory.Add(device,offset),value);
  const auto dirty=[&](uint32_t offset,uint64_t mask) {
    if(!mask) return;
    const auto at=memory.Add(device,offset);
    memory.StoreDoubleWord(at,memory.DoubleWord(at)|mask);
  };
  dirty(16,writes.dirty16); dirty(24,writes.dirty24);
  uint32_t slots=0;
  for(const auto& group:groups) slots|=group.slots;
  for(uint32_t slot=0;slot<16;++slot) if(slots&(1u<<slot)) {
    const auto record=memory.Add(device,1024+slot*24);
    const auto filter=memory.Add(record,12);
    memory.StoreWord(filter,(memory.Word(filter)&0x7ffffu)|(samplers[slot].words[1]&0xfff80000u));
    memory.StoreWord(memory.Add(record,16),samplers[slot].words[2]);
  }
  memory.Flush();
  return writes;
}
// Every group not replayed binds through the guest activation, whatever
// effects it carries.
template<class Reader,class Replay,class Bind>
NativeStaticWorldStateWrites HandOffNativeStaticWorld(const Reader& reader,uint32_t device,
    const NativeMaterialRenderPass& start,const std::array<NativeMaterialSamplerPass,16>& samplers,
    std::span<const NativeStaticWorldHandoffGroup> groups,Replay&& replay,Bind&& bind) {
  std::vector<NativeStaticWorldHandoffGroup> binds(groups.begin(),groups.end());
  for(auto& group:binds) group.effects=nullptr;
  const auto none=[](NativeStaticWorldRetirement) -> uint32_t {
    throw std::logic_error("native static world bind composed without retirement calls");
  };
  return HandOffNativeStaticWorld(reader,device,start,samplers,std::span<const NativeStaticWorldHandoffGroup>(binds),
    replay,bind,none,none);
}
}

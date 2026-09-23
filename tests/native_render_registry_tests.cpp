#include "native_graphics/native_render_registry.h"
#include <emmintrin.h>
#include <array>
#include <bit>
#include <climits>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

using namespace edf::native;
namespace {
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
// Synthetic big-endian guest memory, as in the model publication tests.
struct Memory {
  std::vector<uint8_t>& bytes;
  uint32_t Add(uint32_t address,uint32_t offset) const { return address+offset; }
  uint32_t Word(uint32_t at) const {
    uint32_t value=0; for(unsigned i=0;i<4;++i) value=(value<<8)|bytes.at(at+i); return value;
  }
  const uint8_t* Bytes(uint32_t at,size_t size) const {
    if(!at || at>bytes.size() || size>bytes.size()-at) throw std::runtime_error("registry test range");
    return bytes.data()+at;
  }
  void StoreWord(uint32_t at,uint32_t value) const { for(unsigned i=0;i<4;++i) bytes.at(at+i)=uint8_t(value>>(24-i*8)); }
  void StoreByte(uint32_t at,uint8_t value) const { bytes.at(at)=value; }
  void StoreFloat(uint32_t at,float value) const { StoreWord(at,std::bit_cast<uint32_t>(value)); }
  float Float(uint32_t at) const { return std::bit_cast<float>(Word(at)); }
};
constexpr uint32_t kScene=0x1000,kObjectsEnd=0x1F00,kUpdatesEnd=0x1F20,kContainer=0x1E00,kNode=0x1E40,kOtherNode=0x1E80,
  kTreeVtable=0x820028F8u,kSoldierVtable=0x8200452Cu;
constexpr uint32_t kObjectsList=kScene+kNativeRenderSceneObjects,kUpdatesList=kScene+kNativeRenderSceneUpdates;
// 821A1628: the node goes in front of the head's successor.
void Link(const Memory& memory,uint32_t list,uint32_t node,uint32_t owner) {
  memory.StoreWord(node+8,owner);
  const auto next=memory.Word(list);
  memory.StoreWord(node+4,list); memory.StoreWord(node,next); memory.StoreWord(list,node);
  if(next) memory.StoreWord(next+4,node);
}
// 821A1678.
void Unlink(const Memory& memory,uint32_t node) {
  const auto next=memory.Word(node),prev=memory.Word(node+4);
  if(next) memory.StoreWord(next+4,prev);
  if(prev) memory.StoreWord(prev,next);
  memory.StoreWord(node,0); memory.StoreWord(node+4,0);
}
void BuildScene(const Memory& memory) {
  memory.StoreWord(kObjectsList,kObjectsEnd); memory.StoreWord(kObjectsList+12,kObjectsEnd);
  memory.StoreWord(kUpdatesList,kUpdatesEnd); memory.StoreWord(kUpdatesList+12,kUpdatesEnd);
  memory.StoreWord(kContainer+4,0x1EC0); // End node; distinct from kNode and kOtherNode.
}
// A model with no meshes decodes to a layout once its pose vector is sized.
void BuildInstance(const Memory& memory,uint32_t instance,uint32_t node=kNode) {
  memory.StoreWord(instance,kContainer); memory.StoreWord(instance+4,node); memory.StoreByte(instance+12,0);
}
void BuildPose(const Memory& memory,uint32_t vector,uint32_t storage,uint32_t bones,float seed) {
  memory.StoreWord(vector+4,bones?storage:0); memory.StoreWord(vector+8,bones?storage+64*bones:0);
  for(uint32_t i=0;i<bones*16;++i) memory.StoreFloat(storage+i*4,seed+float(i));
}
// Base render object fields plus both scene links (the base constructor's work).
void BuildObject(const Memory& memory,uint32_t object,uint32_t vtable,bool subscribed) {
  memory.StoreWord(object,vtable); memory.StoreWord(object+kNativeRenderObjectScene,kScene);
  memory.StoreWord(object+kNativeRenderObjectSubscribed,subscribed);
  for(uint32_t i=0;i<4;++i) memory.StoreFloat(object+kNativeRenderObjectCentre+i*4,float(i+1));
  memory.StoreFloat(object+kNativeRenderObjectRadius,5.0f); memory.StoreFloat(object+kNativeRenderObjectCull,300.0f);
  memory.StoreWord(object+kNativeRenderObjectMode,1); memory.StoreFloat(object+kNativeRenderObjectBias,0.25f);
  Link(memory,kObjectsList,object+108,object);
  if(subscribed) Link(memory,kUpdatesList,object+120,object);
}
// clTree: instance +416, pose vector +400, pose storage inside the object block.
void BuildTree(const Memory& memory,uint32_t object,bool subscribed=false,uint32_t bones=2) {
  BuildObject(memory,object,kTreeVtable,subscribed);
  BuildInstance(memory,object+416);
  BuildPose(memory,object+400,object+0x600,bones,1.0f);
}
auto Decoder(const Memory& memory,size_t* decodes=nullptr) {
  return [&memory,decodes](uint32_t instance,uint32_t vector,uint32_t bones) {
    if(decodes) ++*decodes;
    return DecodeNativeModelLayoutWith(memory,instance,vector,[](uint32_t,NativeModelBuffers::Kind)->uint64_t { return 0; },bones);
  };
}
std::shared_ptr<const NativeRenderEntry> EntryOf(const NativeRenderRegistrySnapshot& snapshot,uint32_t object) {
  const auto* found=snapshot.objects.Find(object);
  return found?*found:nullptr;
}

void ClassTableLookup() {
  const auto classes=NativeRenderClasses();
  Require(classes.size()>=40,"class table covers the model-drawing classes");
  for(size_t i=0;i<classes.size();++i) {
    Require(!i || classes[i-1].vtable<classes[i].vtable,"class table sorted and unique");
    Require(FindNativeRenderClass(classes[i].vtable)==&classes[i],"lookup finds every row");
  }
  const auto* tree=FindNativeRenderClass(kTreeVtable);
  Require(tree && std::string_view(tree->name)=="clTree" && tree->instance==416 && tree->pose==400 &&
    tree->cadence==NativeRenderPoseCadence::Constructed && tree->lod==NativeRenderLodKind::None,"clTree row");
  const auto* soldier=FindNativeRenderClass(kSoldierVtable);
  Require(soldier && soldier->lod==NativeRenderLodKind::Character && soldier->instance==1168 && soldier->pose==1088 &&
    soldier->attachments==(kNativeRenderFace|kNativeRenderWeapons),"clFriendSoldier row");
  const auto* people=FindNativeRenderClass(0x820042BCu);
  Require(people && people->attachments==kNativeRenderFace,"clFriendPeople draws a face and no weapons");
  const auto* sky=FindNativeRenderClass(0x8200284Cu);
  Require(sky && sky->cadence==NativeRenderPoseCadence::Frame && sky->instance==412 && sky->pose==384 && sky->other_pass,"clSky row");
  for(const auto& type:classes) Require(!type.other_pass || type.vtable==0x8200284Cu,"only clSky is drawn by another pass");
  const auto* broken=FindNativeRenderClass(0x820077D8u);
  Require(broken && broken->cadence==NativeRenderPoseCadence::Frame && broken->instance==384 && broken->pose==428 &&
    broken->frame_root==640,"clBrokenObject row: posed by the registry from +640");
  Require(!sky->frame_root,"clSky is posed by the sky pass, not the registry");
  const auto* missile=FindNativeRenderClass(0x82007478u);
  Require(missile && missile->instance==892 && missile->pose==936,"clMissileAmmo01 row");
  const auto* parts=FindNativeRenderClass(0x82002760u);
  Require(parts && parts->scene_source && parts->lod==NativeRenderLodKind::FieldParts,"clFieldParts is left to the scene sources");
  Require(!FindNativeRenderClass(0) && !FindNativeRenderClass(kNativeRenderBaseVtable) && !FindNativeRenderClass(kTreeVtable+4),
    "unknown vtables have no class");
}

void BirthAndDeath() {
  std::vector<uint8_t> bytes(0x20000); const Memory memory{bytes}; BuildScene(memory);
  NativeRenderRegistry registry(0);
  const auto decode=Decoder(memory);
  Require(registry.Tick(memory,kScene,1,decode)->entries.empty(),"empty scene publishes nothing");
  constexpr uint32_t kTree=0x2000;
  BuildTree(memory,kTree);
  memory.StoreByte(kTree+kNativeRenderObjectHidden+1,1);
  registry.Born(kTree);
  const auto born=registry.Tick(memory,kScene,2,decode);
  Require(born->entries.size()==1 && born->generation==2 && born->tick==2,"birth publishes one entry");
  const auto entry=EntryOf(*born,kTree);
  Require(entry && entry==born->entries[0] && entry->type==FindNativeRenderClass(kTreeVtable),"entry names its class");
  Require(entry->centre==std::array<float,4>{1,2,3,4} && entry->radius==5.0f && entry->cull_distance==300.0f &&
    entry->mode==1 && entry->sort_bias==0.25f && entry->hidden,"visibility inputs");
  Require(entry->models.size()==1 && entry->models[0].instance==kTree+416 && entry->models[0].layout &&
    entry->models[0].layout->pose_vector==kTree+400,"model instance and layout");
  Require(entry->pose && entry->pose->size()==2 && (*entry->pose)[1][0]==17.0f && entry->pose_vector==kTree+400,"pose copy");
  Require(!registry.AuditScene(memory,kScene).mismatches(),"registry matches scene+84");
  // Death: the destructor hook fires before its unlink.
  registry.Died(kTree);
  Unlink(memory,kTree+108);
  const auto dead=registry.Tick(memory,kScene,3,decode);
  Require(dead->entries.empty() && !dead->objects.Find(kTree),"death removes the entry");
  Require(EntryOf(*born,kTree)==entry && entry->pose->size()==2,"older snapshots keep their entries");
  Require(!registry.AuditScene(memory,kScene).mismatches(),"registry matches scene+84 after death");
  // Address reuse: a new lifetime token.
  BuildTree(memory,kTree);
  registry.Born(kTree);
  const auto reborn=EntryOf(*registry.Tick(memory,kScene,4,decode),kTree);
  Require(reborn && reborn->generation>entry->generation,"address reuse gets a new generation");
  registry.Born(kTree);                     // Missed destructor.
  registry.Died(0x7000);                    // Never born.
  registry.Subscribed(0x7100,true);         // Never born.
  const auto rebirth=EntryOf(*registry.Tick(memory,kScene,5,decode),kTree);
  const auto stats=registry.stats();
  Require(stats.rebirths==1 && stats.unknown_deaths==1 && stats.unknown_subscriptions==1 && stats.deaths==1 &&
    rebirth->generation>reborn->generation && stats.records==1,"birth/death bookkeeping");
  registry.Clear();
  Require(!registry.active() && !registry.AcquireSnapshot() && !registry.stats().records,"clear drops everything");
}

// clSky is tracked (born, audited) but never published: the sky pass draws it
// from its node tree and the camera, and its pose vector - written only by its
// own slot 4, which never runs in full-frame mode - may hold a stale pose.
void SkyIsNotPublished() {
  std::vector<uint8_t> bytes(0x20000); const Memory memory{bytes}; BuildScene(memory);
  NativeRenderRegistry registry(0);
  const auto decode=Decoder(memory);
  constexpr uint32_t kSky=0x3000,kTree=0x5000;
  BuildObject(memory,kSky,0x8200284Cu,false);
  memory.StoreWord(kSky+kNativeRenderObjectMode,0);
  BuildInstance(memory,kSky+412);
  BuildPose(memory,kSky+384,kSky+0x800,1,3.0f);  // A sized pose, as a guest render leaves one.
  BuildTree(memory,kTree);
  registry.Born(kSky); registry.Born(kTree);
  const auto snapshot=registry.Tick(memory,kScene,1,decode);
  Require(snapshot->entries.size()==1 && EntryOf(*snapshot,kTree) && !EntryOf(*snapshot,kSky),
    "clSky published to the models pass");
  Require(!registry.AuditScene(memory,kScene).mismatches(),"the unpublished sky is still tracked");
}
void ResolvesClassAfterDerivedConstructor() {
  std::vector<uint8_t> bytes(0x20000); const Memory memory{bytes}; BuildScene(memory);
  NativeRenderRegistry registry(0);
  const auto decode=Decoder(memory);
  constexpr uint32_t kTree=0x2000;
  BuildTree(memory,kTree);
  memory.StoreWord(kTree,kNativeRenderBaseVtable); // Inside the derived constructor.
  registry.Born(kTree);
  Require(registry.Tick(memory,kScene,1,decode)->entries.empty() && registry.stats().deferred==1,"base vtable defers resolution");
  memory.StoreWord(kTree,kTreeVtable);
  const auto snapshot=registry.Tick(memory,kScene,2,decode);
  Require(snapshot->entries.size()==1 && snapshot->entries[0]->type->vtable==kTreeVtable,"resolved at the next tick");
  // Unknown classes are tracked (audit) but not published.
  constexpr uint32_t kEffect=0x3000;
  BuildObject(memory,kEffect,0x82999990u,false);
  registry.Born(kEffect);
  const auto after=registry.Tick(memory,kScene,3,decode);
  Require(after->entries.size()==1 && registry.stats().unknown_classes==1 && registry.stats().records==2 &&
    !registry.AuditScene(memory,kScene).mismatches(),"unknown class tracked, not published");
}

void SubscriptionDrivesRereads() {
  std::vector<uint8_t> bytes(0x20000); const Memory memory{bytes}; BuildScene(memory);
  NativeRenderRegistry registry(0);
  const auto decode=Decoder(memory);
  constexpr uint32_t kStill=0x2000,kMoving=0x3000,kBornSubscribed=0x4000;
  BuildTree(memory,kStill); BuildTree(memory,kMoving);
  registry.Born(kStill); registry.Born(kMoving);
  const auto first=registry.Tick(memory,kScene,1,decode);
  Require(first->entries.size()==2 && first->entries[0]->object==kStill && first->entries[1]->object==kMoving,
    "two entries, ordered by object");
  // Not subscribed: not re-read (refresh budget 0).
  BuildPose(memory,kStill+400,kStill+0x600,2,100.0f);
  const auto second=registry.Tick(memory,kScene,2,decode);
  Require(EntryOf(*second,kStill)==EntryOf(*first,kStill) && second->objects.Shares(first->objects) &&
    second->entries.Shares(first->entries),"unsubscribed entry is not re-read");
  // 821C0D70(obj,1): re-read every tick.
  registry.Subscribed(kMoving,true); Link(memory,kUpdatesList,kMoving+120,kMoving);
  memory.StoreWord(kMoving+kNativeRenderObjectSubscribed,1);
  BuildPose(memory,kMoving+400,kMoving+0x600,2,50.0f);
  const auto third=registry.Tick(memory,kScene,3,decode);
  const auto moved=EntryOf(*third,kMoving);
  Require(moved!=EntryOf(*second,kMoving) && (*moved->pose)[0][0]==50.0f && (*EntryOf(*second,kMoving)->pose)[0][0]==1.0f,
    "subscribed pose re-read; old snapshot unchanged");
  Require(moved->models[0].layout==EntryOf(*second,kMoving)->models[0].layout,"layout kept across pose changes");
  Require(EntryOf(*third,kStill)==EntryOf(*first,kStill) && third->entries[0]==first->entries[0] && third->entries[1]==moved &&
    second->entries[1]==EntryOf(*second,kMoving),"unchanged entry shared into the new snapshot, or an old snapshot changed");
  const auto unchanged=registry.stats().unchanged,pose_reuses=registry.stats().pose_reuses,changed=registry.stats().changed;
  const auto fourth=registry.Tick(memory,kScene,4,decode);
  Require(EntryOf(*fourth,kMoving)==moved && fourth->objects.Shares(third->objects) && fourth->entries.Shares(third->entries),
    "unchanged subscribed entry is the same pointer");
  Require(registry.stats().unchanged==unchanged+1 && registry.stats().pose_reuses==pose_reuses+1 &&
    registry.stats().changed==changed,"an unchanged re-read allocated an entry or decoded its pose"); 
  Require(!registry.AuditScene(memory,kScene).mismatches(),"subscription matches scene+100");
  // Unsubscribe: one last read, then frozen.
  registry.Subscribed(kMoving,false); Unlink(memory,kMoving+120);
  memory.StoreWord(kMoving+kNativeRenderObjectSubscribed,0);
  BuildPose(memory,kMoving+400,kMoving+0x600,2,60.0f);
  const auto fifth=registry.Tick(memory,kScene,5,decode);
  Require((*EntryOf(*fifth,kMoving)->pose)[0][0]==60.0f,"unsubscribe reads the final pose");
  BuildPose(memory,kMoving+400,kMoving+0x600,2,70.0f);
  Require(EntryOf(*registry.Tick(memory,kScene,6,decode),kMoving)==EntryOf(*fifth,kMoving),"unsubscribed entry frozen");
  // obj+72 set by the constructor links scene+100 without 821C0D70.
  BuildTree(memory,kBornSubscribed,true);
  registry.Born(kBornSubscribed);
  registry.Tick(memory,kScene,7,decode);
  Require(registry.stats().subscribed==1 && !registry.AuditScene(memory,kScene).mismatches(),"constructor subscription adopted");
  // A refresh budget re-reads unsubscribed entries round robin.
  NativeRenderRegistry refreshing(8);
  refreshing.Born(kStill);
  const auto before=refreshing.Tick(memory,kScene,1,decode);
  memory.StoreByte(kStill+kNativeRenderObjectHidden,1);
  const auto after=refreshing.Tick(memory,kScene,2,decode);
  Require(!EntryOf(*before,kStill)->hidden && EntryOf(*after,kStill)->hidden &&
    EntryOf(*after,kStill)->pose==EntryOf(*before,kStill)->pose,"refresh catches visibility changes and shares the pose");
}

void LayoutCaptureWaitsForSizedPose() {
  std::vector<uint8_t> bytes(0x20000); const Memory memory{bytes}; BuildScene(memory);
  NativeRenderRegistry registry(0);
  size_t decodes=0;
  const auto decode=Decoder(memory,&decodes);
  constexpr uint32_t kTree=0x2000;
  BuildTree(memory,kTree,false,0); // Constructor end, pose not built yet.
  registry.Born(kTree);
  auto snapshot=registry.Tick(memory,kScene,10,decode);
  Require(snapshot->entries.size()==1 && !snapshot->entries[0]->models[0].layout && !decodes &&
    registry.stats().retrying==1,"no capture before the pose is sized");
  registry.Tick(memory,kScene,11,decode);   // Retry due at 11; still empty, next at 13.
  BuildPose(memory,kTree+400,kTree+0x600,2,1.0f);
  snapshot=registry.Tick(memory,kScene,12,decode);
  Require(!snapshot->entries[0]->models[0].layout && !decodes,"retry backs off");
  snapshot=registry.Tick(memory,kScene,13,decode);
  const auto layout=snapshot->entries[0]->models[0].layout;
  Require(layout && layout->bones==2 && decodes==1 && registry.stats().retrying==0 && registry.stats().layout_captures==1,
    "captured at the first tick after the pose build");
  registry.Subscribed(kTree,true); Link(memory,kUpdatesList,kTree+120,kTree);
  snapshot=registry.Tick(memory,kScene,14,decode);
  Require(snapshot->entries[0]->models[0].layout==layout && decodes==1,"captured once per identity");
  BuildInstance(memory,kTree+416,kOtherNode); // The instance now names another model.
  snapshot=registry.Tick(memory,kScene,15,decode);
  Require(snapshot->entries[0]->models[0].layout!=layout && snapshot->entries[0]->models[0].layout->node==kOtherNode &&
    decodes==2,"a new model identity is recaptured");
  BuildPose(memory,kTree+400,kTree+0x600,3,1.0f);
  snapshot=registry.Tick(memory,kScene,16,decode);
  Require(snapshot->entries[0]->models[0].layout->bones==3 && decodes==3,"a resized skeleton is recaptured");
  // A model that does not decode is remembered, not re-decoded every tick.
  memory.StoreWord(kTree+416+4,memory.Word(kContainer+4));
  snapshot=registry.Tick(memory,kScene,17,decode);
  registry.Tick(memory,kScene,18,decode);
  Require(!snapshot->entries[0]->models[0].layout && decodes==4 && registry.stats().layout_failures==1,"rejections are cached");
}

void CharacterLodAndAttachments() {
  std::vector<uint8_t> bytes(0x20000); const Memory memory{bytes}; BuildScene(memory);
  NativeRenderRegistry registry(0);
  const auto decode=Decoder(memory);
  constexpr uint32_t kSoldier=0x4000,kLods=0xA000,kWeapons=0x10000;
  BuildObject(memory,kSoldier,kSoldierVtable,true);
  BuildInstance(memory,kSoldier+1168);
  BuildPose(memory,kSoldier+1088,0x9000,3,1.0f);
  memory.StoreWord(kSoldier+kNativeRenderLodRecords,kLods); memory.StoreWord(kSoldier+kNativeRenderLodCount,2);
  memory.StoreFloat(kLods,-10.0f); BuildInstance(memory,kLods+4);
  memory.StoreFloat(kLods+48,-50.0f); BuildInstance(memory,kLods+52);
  memory.StoreByte(kSoldier+kNativeRenderFaceFlag,1);
  BuildInstance(memory,kSoldier+kNativeRenderFaceInstance); BuildPose(memory,kSoldier+kNativeRenderFacePose,0x9400,1,7.0f);
  memory.StoreWord(kSoldier+kNativeRenderWeaponArray,kWeapons); memory.StoreWord(kSoldier+kNativeRenderWeaponCount,3);
  for(uint32_t i=0;i<3;++i) {
    const auto weapon=kWeapons+i*kNativeRenderWeaponStride;
    memory.StoreByte(weapon+1404,1); memory.StoreByte(weapon+1405,1); memory.StoreWord(weapon+108,0x1234);
    BuildInstance(memory,weapon+100); BuildPose(memory,weapon+144,0x9800+i*0x100,1,float(i));
  }
  memory.StoreWord(kWeapons+kNativeRenderWeaponStride+412,2);   // Second weapon: mode 2 with w+804 zero.
  memory.StoreByte(kWeapons+2*kNativeRenderWeaponStride+1405,0); // Third weapon: not listed for drawing.
  registry.Born(kSoldier);
  auto snapshot=registry.Tick(memory,kScene,1,decode);
  auto entry=EntryOf(*snapshot,kSoldier);
  Require(entry && entry->lod_thresholds==std::vector<float>{-10.0f,-50.0f} && entry->models.size()==3 &&
    entry->models[0].instance==kSoldier+1168 && entry->models[1].instance==kLods+4 && entry->models[2].instance==kLods+52,
    "LOD records follow 8210AE48");
  for(const auto& model:entry->models) Require(model.layout && model.layout->pose_vector==kSoldier+1088,"every LOD shares obj+1088");
  Require(entry->attachments.size()==2 && entry->attachments[0].model.instance==kSoldier+1588 &&
    entry->attachments[0].pose_vector==kSoldier+1636 && (*entry->attachments[0].pose)[0][0]==7.0f &&
    entry->attachments[1].model.instance==kWeapons+100 && entry->attachments[1].pose_vector==kWeapons+144 &&
    entry->attachments[1].model.layout,"face and the drawable weapon");
  memory.StoreWord(kWeapons+kNativeRenderWeaponStride+804,1);    // Second weapon becomes drawable.
  BuildPose(memory,kSoldier+kNativeRenderFacePose,0x9400,1,7.0f);
  const auto previous=entry;
  entry=EntryOf(*registry.Tick(memory,kScene,2,decode),kSoldier);
  Require(entry->attachments.size()==3 && entry->attachments[0].pose==previous->attachments[0].pose,"attachment poses shared when unchanged");
  memory.StoreWord(kSoldier+kNativeRenderWeaponGate,1);          // 820DBBD0 gate: no weapons.
  memory.StoreByte(kSoldier+kNativeRenderFaceFlag,0);
  entry=EntryOf(*registry.Tick(memory,kScene,3,decode),kSoldier);
  Require(entry->attachments.empty(),"face and weapon gates");
}

// The class facts the audit recovered from the slot-4 bodies (addresses in
// native_render_registry.cpp): per-object constants, vehicle weapon groups
// and parts.
void ClassConstantsAndParts() {
  const auto constants=[](uint32_t vtable,uint32_t handle,uint32_t highlight,uint32_t time) {
    const auto* type=FindNativeRenderClass(vtable);
    Require(type && type->lod==NativeRenderLodKind::Character && type->instance==1168 && type->pose==1088,"a character row");
    Require(type->constants[0].handle==handle && type->constants[0].value==highlight && !type->constants[0].scroll &&
      type->constants[1].handle==handle+4 && type->constants[1].value==time && !type->constants[1].scroll,
      "g_Highlight and g_Time follow the slot-4 821A1730 calls");
    Require(NativeRenderClassHasConstants(*type) && !type->parts.count && !type->weapon_groups[0].group,"constants only");
  };
  constants(0x820052C0u,2288,2336,2352);    // clUfoSmall01 820E7FB8
  constants(0x820053F0u,1648,1696,1712);    // clUfoCarrier01 820EA398
  constants(0x82005E14u,2384,2432,2448);    // clAlien4LegTank01 820F5630
  for(const auto vtable:{0x82005EF8u,0x82005F70u,0x82006098u,0x82006128u})
    constants(vtable,1296,1264,1280);       // 820FC528
  constants(0x820063C4u,1984,2032,2048);    // clMonster01Mech 820FFFC0
  constants(0x820065A4u,1248,1296,1312);    // clUfoMother01 82100D00
  for(const auto vtable:{0x820066CCu,0x820068B0u,0x820068ECu,0x82006A04u,0x82006A74u})
    constants(vtable,1360,1408,1424);       // 82108BE8
  const auto* tank=FindNativeRenderClass(0x82005678u);        // clAlienTank01 820ECCC0 / 820F01E8
  Require(tank && tank->constants[0].handle==5024 && tank->constants[0].value==5072 && tank->constants[1].handle==5028 &&
    tank->constants[1].value==5088 && tank->parts.base==1920 && tank->parts.count==2 && tank->parts.stride==1488 &&
    tank->parts.instance==108 && tank->parts.pose==152 && tank->parts.constants[0].handle==1120 &&
    tank->parts.constants[0].value==1168 && tank->parts.constants[1].handle==1124 && tank->parts.constants[1].value==1184,
    "clAlienTank01 draws two turrets after its model");
  for(const auto vtable:{0x82015E94u,0x82015F24u,0x82016434u}) {  // 8219A2D0 -> 82199DD8
    const auto* vehicle=FindNativeRenderClass(vtable);
    Require(vehicle && vehicle->weapon_groups[0].group==1824 && vehicle->weapon_groups[0].array==36 &&
      vehicle->weapon_groups[0].count==44 && !vehicle->weapon_groups[1].group && !NativeRenderClassHasConstants(*vehicle),
      "C_VehicleBase weapons");
  }
  const auto* heli=FindNativeRenderClass(0x8202032Cu);        // 821E2250 -> 821E4E90 twice
  Require(heli && heli->weapon_groups[0].group==1536 && heli->weapon_groups[1].group==1580 &&
    heli->weapon_groups[0].array==28 && heli->weapon_groups[0].count==36 && !heli->parts.count,"C_Helicopter weapons");
  const auto* ctank=FindNativeRenderClass(0x82020674u);       // 821E5810 -> 821E4E90, 821E7C50 twice
  Require(ctank && ctank->weapon_groups[0].group==2208 && ctank->weapon_groups[0].array==28 && ctank->parts.base==4216 &&
    ctank->parts.count==2 && ctank->parts.stride==92 && ctank->parts.instance==0 && ctank->parts.pose==72 &&
    ctank->parts.constants[0].handle==88 && ctank->parts.constants[0].value==60 && ctank->parts.constants[0].scroll &&
    !ctank->parts.constants[1].handle && NativeRenderClassHasConstants(*ctank),"C_Tank weapons and treads");
  // Every other row stores nothing and draws nothing extra.
  size_t with=0;
  for(const auto& type:NativeRenderClasses()) with+=NativeRenderClassHasConstants(type);
  Require(with==16,"sixteen classes store per-object constants");
  // clMapArtifact_Base (82002158) is abstract: its constructor 820B33B0 is
  // called only by the eight subclass constructors, each storing its own
  // vtable right after (clBuilding 820B8198, clFieldParts 820BAD20, ...),
  // and those share slot 4 820B2670 with the static world pass's scene
  // sources. No row, so an object with that vtable is never drawn twice.
  Require(!FindNativeRenderClass(0x82002158u),"clMapArtifact_Base has no row");
}
// A shared effect pool node (the 821A20C0 map): key std::string at +12, value
// at +40 (data +0, count +8). A key of capacity 16 or more lives out of line.
uint32_t BuildPoolNode(const Memory& memory,uint32_t node,std::string_view name,uint32_t data,uint32_t count=1) {
  const auto key=node+12;
  const bool inline_text=name.size()<16;
  const auto text=inline_text?key+4:node+0x80;
  if(!inline_text) memory.StoreWord(key+4,text);
  for(size_t i=0;i<name.size();++i) memory.StoreByte(text+uint32_t(i),uint8_t(name[i]));
  memory.StoreWord(key+20,uint32_t(name.size())); memory.StoreWord(key+24,inline_text?15:31);
  memory.StoreWord(node+40,data); memory.StoreWord(node+48,count);
  return node+40;
}
void StoreVector(const Memory& memory,uint32_t at,std::array<float,4> value) {
  for(uint32_t i=0;i<4;++i) memory.StoreFloat(at+i*4,value[i]);
}
std::array<float,4> VectorOf(const NativeRenderObjectConstant& constant) {
  std::array<float,4> value{};
  for(size_t i=0;i<4;++i) {
    uint32_t word=0; for(size_t b=0;b<4;++b) word=(word<<8)|constant.registers[i*4+b];
    value[i]=std::bit_cast<float>(word);
  }
  return value;
}
// 821A1730 before 8210AE48 (clUfoSmall01): both constants published with the
// entry, re-read every tick, shared while unchanged; a null handle or a zero
// count stores nothing; an out-of-line pool key reads through its pointer.
void ObjectConstants() {
  std::vector<uint8_t> bytes(0x40000); const Memory memory{bytes}; BuildScene(memory);
  NativeRenderRegistry registry(0);
  const auto decode=Decoder(memory);
  constexpr uint32_t kUfo=0x4000,kPool=0x20000;
  const auto highlight=BuildPoolNode(memory,kPool,"g_Highlight",0x21000);
  const auto time=BuildPoolNode(memory,kPool+0x100,"g_TimeWithALongerName",0x21100);
  Require(ReadNativeRenderPoolName(memory,time)=="g_TimeWithALongerName","an out-of-line pool key");
  BuildObject(memory,kUfo,0x820052C0u,false);
  BuildInstance(memory,kUfo+1168); BuildPose(memory,kUfo+1088,0x9000,2,1.0f);
  memory.StoreWord(kUfo+2288,highlight); memory.StoreWord(kUfo+2292,time);
  StoreVector(memory,kUfo+2336,{1,.5f,.25f,0}); StoreVector(memory,kUfo+2352,{3,0,0,1});
  registry.Born(kUfo);
  auto first=EntryOf(*registry.Tick(memory,kScene,1,decode),kUfo);
  Require(first && first->constants && first->constants->size()==2 && (*first->constants)[0].name=="g_Highlight" &&
    VectorOf((*first->constants)[0])==std::array<float,4>{1,.5f,.25f,0} && (*first->constants)[1].name=="g_TimeWithALongerName" &&
    VectorOf((*first->constants)[1])==std::array<float,4>{3,0,0,1},"both constants in slot-4 order");
  // Unsubscribed and no refresh budget, yet re-read every tick.
  auto same=EntryOf(*registry.Tick(memory,kScene,2,decode),kUfo);
  Require(same==first,"unchanged constants keep the entry");
  StoreVector(memory,kUfo+2352,{4,0,0,1});
  auto moved=EntryOf(*registry.Tick(memory,kScene,3,decode),kUfo);
  Require(moved!=first && moved->constants!=first->constants && VectorOf((*moved->constants)[1])[0]==4.0f &&
    moved->pose==first->pose && registry.stats().constant_changes==2,"a moved g_Time republishes the constants, not the pose");
  memory.StoreWord(kPool+0x100+48,0);                          // 821A16D8: min(1, count 0) stores nothing.
  memory.StoreWord(kUfo+2288,0);                               // A null handle stores nothing.
  auto none=EntryOf(*registry.Tick(memory,kScene,4,decode),kUfo);
  Require(none && !none->constants && none->models.size()==1,"no stored constant, no constants");
}
// clAlienTank01: its constants, the model, then per turret (820F01E8) its
// own constants and model; the pool keeps the last store, so each turret's
// set is the entry's with its own values over them.
void AlienTankTurrets() {
  std::vector<uint8_t> bytes(0x40000); const Memory memory{bytes}; BuildScene(memory);
  NativeRenderRegistry registry(0);
  const auto decode=Decoder(memory);
  constexpr uint32_t kTank=0x4000,kPool=0x20000;
  const auto highlight=BuildPoolNode(memory,kPool,"g_Highlight",0x21000);
  const auto time=BuildPoolNode(memory,kPool+0x100,"g_Time",0x21100);
  BuildObject(memory,kTank,0x82005678u,true);
  BuildInstance(memory,kTank+1168); BuildPose(memory,kTank+1088,0x9000,2,1.0f);
  memory.StoreWord(kTank+5024,highlight); memory.StoreWord(kTank+5028,time);
  StoreVector(memory,kTank+5072,{1,0,0,0}); StoreVector(memory,kTank+5088,{10,0,0,1});
  for(uint32_t i=0;i<2;++i) {
    const auto part=kTank+1920+i*1488;
    BuildInstance(memory,part+108); BuildPose(memory,part+152,0x9400+i*0x100,1,float(20+i));
    memory.StoreWord(part+1120,highlight); memory.StoreWord(part+1124,i?0:time);  // Turret 1 stores no g_Time.
    StoreVector(memory,part+1168,{float(2+i),0,0,0}); StoreVector(memory,part+1184,{float(30+i),0,0,1});
  }
  registry.Born(kTank);
  const auto entry=EntryOf(*registry.Tick(memory,kScene,1,decode),kTank);
  Require(entry && entry->attachments.size()==2 && entry->attachments[0].model.instance==kTank+1920+108 &&
    entry->attachments[0].pose_vector==kTank+1920+152 && entry->attachments[1].model.instance==kTank+1920+1488+108 &&
    (*entry->attachments[1].pose)[0][0]==21.0f,"two turrets after the model");
  Require(VectorOf((*entry->constants)[0])[0]==1.0f && VectorOf((*entry->constants)[1])[0]==10.0f,"the body's constants");
  const auto& first=*entry->attachments[0].constants,&second=*entry->attachments[1].constants;
  Require(first.size()==2 && VectorOf(first[0])[0]==2.0f && VectorOf(first[1])[0]==30.0f,"turret 0 stores both");
  Require(second.size()==2 && second[0].name=="g_Highlight" && VectorOf(second[0])[0]==3.0f && second[1].name=="g_Time" &&
    VectorOf(second[1])[0]==30.0f,"turret 1 keeps turret 0's g_Time (the pool is sticky)");
}
// Vehicle weapon groups (820E1A80 on element+64, 1504 apart, no w+1405
// test) and the C_Tank treads' g_Scroll (-x, 0, 0, 1).
void VehicleWeaponsAndTreads() {
  std::vector<uint8_t> bytes(0x40000); const Memory memory{bytes}; BuildScene(memory);
  NativeRenderRegistry registry(0);
  const auto decode=Decoder(memory);
  constexpr uint32_t kTank=0x4000,kHeli=0x8000,kBike=0xC000,kPool=0x20000,kWeapons=0x28000;
  const auto scroll=BuildPoolNode(memory,kPool,"g_Scroll",0x21000);
  uint32_t next=kWeapons;
  const auto weapons=[&](uint32_t owner,uint32_t group,uint32_t array,uint32_t count_at,uint32_t count) {
    const auto first=next; next+=count*kNativeRenderVehicleWeaponStride;
    memory.StoreWord(owner+group+array,first); memory.StoreWord(owner+group+count_at,count);
    for(uint32_t i=0;i<count;++i) {
      const auto w=first+i*kNativeRenderVehicleWeaponStride+kNativeRenderVehicleWeaponOffset;
      memory.StoreByte(w+1404,1); memory.StoreByte(w+1405,0); memory.StoreWord(w+108,0x1234);
      BuildInstance(memory,w+100); BuildPose(memory,w+144,0x30000+(w-kWeapons)/4,1,float(i));
    }
    return first;
  };
  const auto character=[&](uint32_t object,uint32_t vtable) {
    BuildObject(memory,object,vtable,true); BuildInstance(memory,object+1168); BuildPose(memory,object+1088,object+0x1800,2,1.0f);
  };
  character(kTank,0x82020674u);
  const auto tank_weapons=weapons(kTank,2208,28,36,1);
  for(uint32_t i=0;i<2;++i) {
    const auto tread=kTank+4216+i*92;
    BuildInstance(memory,tread); BuildPose(memory,tread+72,kTank+0x1A00+i*0x40,1,float(40+i));
    memory.StoreWord(tread+88,scroll); memory.StoreFloat(tread+60,float(i)+.5f);
  }
  character(kHeli,0x8202032Cu);
  const auto rotor=weapons(kHeli,1536,28,36,2),gun=weapons(kHeli,1580,28,36,1);
  memory.StoreWord(rotor+kNativeRenderVehicleWeaponStride+64+412,2);  // Mode 2 with w+804 zero: not drawn.
  character(kBike,0x82015F24u);
  const auto bike=weapons(kBike,1824,36,44,2);
  memory.StoreByte(bike+64+1404,0);                                      // Not drawn.
  registry.Born(kTank); registry.Born(kHeli); registry.Born(kBike);
  const auto snapshot=registry.Tick(memory,kScene,1,decode);
  const auto tank=EntryOf(*snapshot,kTank),heli=EntryOf(*snapshot,kHeli),bike_entry=EntryOf(*snapshot,kBike);
  Require(tank && tank->attachments.size()==3 && tank->attachments[0].model.instance==tank_weapons+64+100 &&
    tank->attachments[0].pose_vector==tank_weapons+64+144 && !tank->attachments[0].constants && !tank->constants,
    "C_Tank: the weapon draws before the treads, with no constant stored yet");
  for(uint32_t i=0;i<2;++i) {
    const auto& tread=tank->attachments[1+i];
    Require(tread.model.instance==kTank+4216+i*92 && tread.pose_vector==kTank+4216+i*92+72 && tread.model.layout &&
      tread.constants && tread.constants->size()==1 && (*tread.constants)[0].name=="g_Scroll" &&
      VectorOf((*tread.constants)[0])==std::array<float,4>{-(float(i)+.5f),0,0,1},"a tread draws with (-x,0,0,1)");
  }
  Require(heli && heli->attachments.size()==2 && heli->attachments[0].model.instance==rotor+64+100 &&
    heli->attachments[1].model.instance==gun+64+100,"C_Helicopter: both groups in order, without w+1405");
  Require(bike_entry && bike_entry->attachments.size()==1 &&
    bike_entry->attachments[0].model.instance==bike+kNativeRenderVehicleWeaponStride+64+100,"C_Bike: +1824 elements, w+1404 gate");
  // A later tread value is re-read without a subscription change.
  memory.StoreFloat(kTank+4216+60,7.0f);
  const auto moved=EntryOf(*registry.Tick(memory,kScene,2,decode),kTank);
  Require(VectorOf((*moved->attachments[1].constants)[0])[0]==-7.0f && moved->attachments[2].constants==tank->attachments[2].constants,
    "only the moved tread's constants are new");
}

void AuditCountsMismatches() {
  std::vector<uint8_t> bytes(0x20000); const Memory memory{bytes}; BuildScene(memory);
  constexpr uint32_t kEarly=0x2000,kUnseen=0x3000,kPhantom=0x4000;
  BuildTree(memory,kEarly);                 // Constructed before the registry was enabled.
  NativeRenderRegistry registry(0);
  const auto decode=Decoder(memory);
  const auto seeded=registry.Tick(memory,kScene,1,decode);
  Require(seeded->entries.size()==1 && registry.stats().seeded==1 && !registry.AuditScene(memory,kScene).mismatches(),
    "first tick adopts scene+84");
  BuildTree(memory,kUnseen);                // Constructor hook missed.
  BuildObject(memory,kPhantom,kTreeVtable,false);
  Unlink(memory,kPhantom+108);              // Unlinked without a destructor hook.
  registry.Born(kPhantom);
  Link(memory,kUpdatesList,kEarly+120,kEarly); // Subscribed without 821C0D70.
  registry.Tick(memory,kScene,2,decode);
  const auto audit=registry.AuditScene(memory,kScene);
  Require(audit.guest==2 && audit.guest_updates==1 && audit.registry==2 && audit.missing==1 && audit.extra==1 &&
    audit.subscription==1 && audit.mismatches()==3,"audit counts each mismatch kind");
}
// ---- Transcription of 820EC180's sphere loop, statement for statement. ----
// The image constants the bodies load, by guest address, as read from
// guest_image.bin (edfkb KB.f32/u32): the trig table at 82556678 and the
// loop's 1.0 / 0.0 / 0.1.
double ImageDouble(uint32_t at) {
  switch(at) {
    case 0x82556678u: return std::bit_cast<double>(0x3ff921fb54442d18ull);
    case 0x82556680u: return std::bit_cast<double>(0x3fd45f306dc9c883ull);
    case 0x82556688u: return std::bit_cast<double>(0x41aa39de00000000ull);
    case 0x825566A0u: return std::bit_cast<double>(0x400921fb54400000ull);
    case 0x825566A8u: return std::bit_cast<double>(0x3de0b4611a600000ull);
    case 0x825566B0u: return std::bit_cast<double>(0xbfc5555555555555ull);
    case 0x825566B8u: return std::bit_cast<double>(0x3f811111111110b0ull);
    case 0x825566C0u: return std::bit_cast<double>(0xbf2a01a01a013e1aull);
    case 0x825566C8u: return std::bit_cast<double>(0x3ec71de3a524f063ull);
    case 0x825566D0u: return std::bit_cast<double>(0xbe5ae6454b5dc0abull);
    case 0x825566D8u: return std::bit_cast<double>(0x3de6123c686ad430ull);
    case 0x825566E0u: return std::bit_cast<double>(0xbd6ae420dc08499cull);
    case 0x825566E8u: return std::bit_cast<double>(0x3ce880ff6993df95ull);
    case 0x82019BF0u: return std::bit_cast<double>(0x3ff0000000000000ull);
    case 0x82556D50u: return std::bit_cast<double>(0xfff8000000000000ull);
  }
  throw std::runtime_error("no image double at that address");
}
double ImageSingle(uint32_t at) { // lfs
  switch(at) {
    case 0x82556690u: return double(std::bit_cast<float>(0x00000000u));
    case 0x82556694u: return double(std::bit_cast<float>(0x3f800000u));
    case 0x82556698u: return double(std::bit_cast<float>(0xbf800000u));
    case 0x8255669Cu: return double(std::bit_cast<float>(0x3f000000u));
    case 0x820008CCu: return double(std::bit_cast<float>(0x3f800000u));
    case 0x820009A4u: return double(std::bit_cast<float>(0x00000000u));
    case 0x820021F0u: return double(std::bit_cast<float>(0x3dcccccdu));
  }
  throw std::runtime_error("no image single at that address");
}
uint64_t U(double value) { return std::bit_cast<uint64_t>(value); }
double D(uint64_t value) { return std::bit_cast<double>(value); }
// The recompiled fctid / fctidz: the guards, then SSE2's conversions.
int64_t Cvt(double value) {
  return std::isnan(value)?int64_t(0x8000000000000000ULL):(value>double(LLONG_MAX))?LLONG_MAX:_mm_cvtsd_si64(_mm_load_sd(&value));
}
int64_t Cvtt(double value) {
  return std::isnan(value)?int64_t(0x8000000000000000ULL):(value>double(LLONG_MAX))?LLONG_MAX:_mm_cvttsd_si64(_mm_load_sd(&value));
}
// sub_821E9558 (edf2017_recomp.72.cpp:9055).
double Guest821E9558(double f1) {
  const uint32_t r11=0x82550000u+26232;
  double f12=D(U(f1)&~0x8000000000000000ull);
  const uint64_t stack8=U(f12);
  double f9=ImageSingle(r11+28),f0=ImageSingle(r11+32);
  f0=f1>=0.0?f9:f0;
  f9=ImageDouble(r11+8);
  f9=f9*f12;
  double f13=ImageDouble(r11+40),f11=ImageDouble(r11+48),f10=ImageDouble(r11+112);
  int64_t f8=Cvt(f9);
  f9=f0;
  f0=double(f8);
  f13=-std::fma(f13,f0,-f12);
  f8=Cvtt(f0);
  const int64_t stack16=f8;
  f13=-std::fma(f11,f0,-f13);
  f11=ImageDouble(r11+104);
  f0=f13*f13;
  f10=std::fma(f10,f0,f11);
  f11=ImageDouble(r11+96);
  f10=std::fma(f10,f0,f11);
  f11=ImageDouble(r11+88);
  f10=std::fma(f10,f0,f11);
  f11=ImageDouble(r11+80);
  const bool even=(stack16&1)==0;
  f10=std::fma(f10,f0,f11);
  f11=ImageDouble(r11+72);
  f10=std::fma(f10,f0,f11);
  f11=ImageDouble(r11+64);
  f10=std::fma(f10,f0,f11);
  f11=ImageDouble(r11+56);
  f10=std::fma(f10,f0,f11);
  f11=ImageDouble(0x82020000u-25616);
  f0=std::fma(f10,f0,f11);
  f0=f0*f13;
  if(!even) f0=D(U(f0)^0x8000000000000000ull);
  f13=ImageDouble(r11+16);
  f11=f0*f9;
  f13=f12-f13;
  f0=ImageDouble(0x82550000u+27984);
  f0=f13>=0.0?f0:f11;
  if(stack8==0) return f1;
  return f0;
}
// sub_821E9630 (edf2017_recomp.0.cpp:9720).
double Guest821E9630(double f1) {
  double f12=D(U(f1)&~0x8000000000000000ull);
  const uint32_t r11=0x82550000u+26232;
  double f0=ImageDouble(r11+0);
  double f13=ImageSingle(r11+36);
  double f10=ImageDouble(r11+40);
  double f11=f0+f12;
  f0=ImageDouble(r11+8);
  double f9=ImageDouble(r11+48);
  double f8=ImageDouble(r11+112);
  f0=f0*f11;
  f0=double(Cvt(f0));
  const double f7=f0;
  f0=f0-f13;
  const int64_t stack16=Cvtt(f7);
  f13=-std::fma(f10,f0,-f12);
  f10=ImageDouble(r11+104);
  f13=-std::fma(f9,f0,-f13);
  f0=f13*f13;
  f9=std::fma(f8,f0,f10);
  f10=ImageDouble(r11+96);
  f9=std::fma(f9,f0,f10);
  f10=ImageDouble(r11+88);
  const bool even=(stack16&1)==0;
  f9=std::fma(f9,f0,f10);
  f10=ImageDouble(r11+80);
  f9=std::fma(f9,f0,f10);
  f10=ImageDouble(r11+72);
  f9=std::fma(f9,f0,f10);
  f10=ImageDouble(r11+64);
  f9=std::fma(f9,f0,f10);
  f10=ImageDouble(r11+56);
  f9=std::fma(f9,f0,f10);
  f10=ImageDouble(0x82020000u-25616);
  f0=std::fma(f9,f0,f10);
  f0=f0*f13;
  if(!even) f0=D(U(f0)^0x8000000000000000ull);
  f10=ImageDouble(r11+16);
  f10=f11-f10;
  f11=ImageSingle(r11+24);
  const bool equal=f12==f11;
  f13=ImageDouble(0x82550000u+27984);
  f1=f10>=0.0?f13:f0;
  if(!equal) return f1;
  return ImageSingle(r11+28);
}
double Lfs(const Memory& m,uint32_t at) { return double(std::bit_cast<float>(m.Word(at))); }
void Stfs(const Memory& m,uint32_t at,double value) { m.StoreFloat(at,float(value)); }
// sub_821C7B20 (edf2017_recomp.55.cpp:8684).
void Guest821C7B20(const Memory& m,uint32_t r3,double f1) {
  double f31=f1;
  double f0=Guest821E9558(f1);
  f1=f31;
  f31=double(float(f0));
  f1=Guest821E9630(f1);
  const double f13=double(float(f1));
  const double f12=D(U(f31)^0x8000000000000000ull);
  f0=ImageSingle(0x820008CCu);
  Stfs(m,r3+0,f0);
  f0=ImageSingle(0x820009A4u);
  Stfs(m,r3+4,f0); Stfs(m,r3+8,f0); Stfs(m,r3+12,f0); Stfs(m,r3+16,f0);
  Stfs(m,r3+20,f13); Stfs(m,r3+24,f31); Stfs(m,r3+28,f0); Stfs(m,r3+32,f0);
  Stfs(m,r3+36,f12); Stfs(m,r3+40,f13); Stfs(m,r3+44,f0);
}
// sub_821C7D80 (edf2017_recomp.50.cpp:8904).
void Guest821C7D80(const Memory& m,uint32_t r31,double f1) {
  double f31=f1;
  double f0=Guest821E9558(f1);
  f1=f31;
  f31=double(float(f0));
  f1=Guest821E9630(f1);
  double f13=Lfs(m,r31+0);
  f0=double(float(f1));
  double f12=Lfs(m,r31+8);
  double f11=double(float(f13*f31));
  double f10=double(float(f12*f31));
  f12=double(float(std::fma(f12,f0,-f11)));
  Stfs(m,r31+8,f12);
  f13=double(float(std::fma(f13,f0,f10)));
  Stfs(m,r31+0,f13);
  f13=Lfs(m,r31+16);
  f11=double(float(f13*f0));
  f12=Lfs(m,r31+24);
  f10=double(float(f13*f31));
  f13=double(float(std::fma(f12,f31,f11)));
  f12=double(float(std::fma(f12,f0,-f10)));
  Stfs(m,r31+24,f12);
  Stfs(m,r31+16,f13);
  f13=Lfs(m,r31+32);
  f10=double(float(f13*f31));
  f12=Lfs(m,r31+40);
  f11=double(float(f13*f0));
  f0=double(float(std::fma(f12,f0,-f10)));
  Stfs(m,r31+40,f0);
  f13=double(float(std::fma(f12,f31,f11)));
  Stfs(m,r31+32,f13);
}
// sub_821C8198 (edf2017_recomp.79.cpp:8666), as in the sky tests: a
// 128-bit register as simde sees it after the byte-reversing load.
using Lanes=std::array<float,4>;
Lanes Load(const Memory& m,uint32_t at) { return {m.Float(at+12),m.Float(at+8),m.Float(at+4),m.Float(at)}; }
void Store(const Memory& m,uint32_t at,const Lanes& v) { for(unsigned i=0;i<4;++i) m.StoreFloat(at+i*4,v[3-i]); }
Lanes Hi(const Lanes& a,const Lanes& b) { return {a[2],b[2],a[3],b[3]}; }
Lanes Lo(const Lanes& a,const Lanes& b) { return {a[0],b[0],a[1],b[1]}; }
Lanes Dp(const Lanes& a,const Lanes& b) {
  const float t0=a[0]*b[0]; const float t1=a[1]*b[1]; const float t2=a[2]*b[2]; const float t3=a[3]*b[3];
  const float l=t0+t1; const float h=t2+t3; const float s=l+h;
  return {s,s,s,s};
}
void Guest821C8198(const Memory& m,uint32_t r3,uint32_t r4,uint32_t r5) {
  Lanes v10=Load(m,r5),v0=Load(m,r4),v9=Load(m,r5+16),v7=Load(m,r5+48),v8=Load(m,r5+32);
  Lanes v5=Hi(v7,v9),v6=Hi(v8,v10);
  Lanes v13=Load(m,r4+16);
  Lanes v4=Lo(v8,v10);
  Lanes v12=Load(m,r4+32);
  v7=Lo(v7,v9);
  Lanes v11=Load(m,r4+48);
  v9=Lo(v5,v6); v10=Hi(v5,v6); v8=Hi(v7,v4); v7=Lo(v7,v4);
  Lanes v1=Dp(v13,v9); v5=Dp(v13,v10); Lanes v2=Dp(v13,v8); v13=Dp(v13,v7);
  v6=Dp(v0,v10); Lanes v3=Dp(v0,v8); Lanes v31=Dp(v12,v10),v30=Dp(v12,v8),v29=Dp(v12,v9); v12=Dp(v12,v7);
  v4=Dp(v0,v9); v0=Dp(v0,v7); Lanes v28=Dp(v11,v7); v10=Dp(v11,v10); v8=Dp(v11,v8); v9=Dp(v11,v9);
  v7=Hi(v2,v5); v13=Hi(v13,v1); v11=Hi(v3,v6); v13=Hi(v13,v7); v6=Hi(v30,v31); v12=Hi(v12,v29); v0=Hi(v0,v4);
  const Lanes s48=v13;
  v13=Hi(v12,v6); v0=Hi(v0,v11); v10=Hi(v8,v10); v11=Hi(v28,v9);
  const Lanes s32=v13;
  v13=Hi(v11,v10);
  const Lanes s16=v13;
  Store(m,r3,v0); Store(m,r3+16,s48); Store(m,r3+32,s32); Store(m,r3+48,s16);
}
// sub_820EC180 (edf2017_recomp.80.cpp:1951) after its 821C9C20: the matrix
// each 821C9DA8(this+1172, r1+96) receives, in call order. r1 is the frame.
std::vector<NativeGuestMatrix> Guest820EC180Worlds(const Memory& m,uint32_t r30,uint32_t r1) {
  std::vector<NativeGuestMatrix> worlds;
  uint32_t r11=m.Word(r30+1228),r31=m.Word(r30+1220);
  const uint32_t r9=r11<<2;
  r11=r11+r9;
  r11=r11<<2;
  const uint32_t r29=r11+r31;
  if(r31==r29) return worlds;
  const double f30=ImageSingle(0x82000000u+2252),f29=ImageSingle(0x82000000u+8688);
  Stfs(m,r1+92,f30);
  const double f31=ImageSingle(0x82000000u+2468);
  do {
    const uint32_t r3=r1+96;
    Stfs(m,r1+96,f30); Stfs(m,r1+100,f31); Stfs(m,r1+104,f31); Stfs(m,r1+108,f31);
    Stfs(m,r1+112,f31); Stfs(m,r1+116,f30); Stfs(m,r1+120,f31); Stfs(m,r1+124,f31);
    Stfs(m,r1+128,f31); Stfs(m,r1+132,f31); Stfs(m,r1+136,f30); Stfs(m,r1+140,f31);
    Stfs(m,r1+144,f31); Stfs(m,r1+148,f31); Stfs(m,r1+152,f31); Stfs(m,r1+156,f30);
    Guest821C7B20(m,r3,Lfs(m,r31+4));
    double f0=Lfs(m,r31+16);
    double f13=Lfs(m,r30+1216);
    const double f12=Lfs(m,r31+8);
    Guest821C7D80(m,r3,double(float(std::fma(f0,f13,f12))));
    f0=Lfs(m,r31+12);
    f13=Lfs(m,r1+128);
    f13=double(float(f13*f0));
    Stfs(m,r1+80,f13);
    f13=Lfs(m,r1+132);
    f13=double(float(f13*f0));
    Stfs(m,r1+84,f13);
    f13=Lfs(m,r1+136);
    f0=double(float(f13*f0));
    Stfs(m,r1+88,f0);
    for(uint32_t w=0;w<4;++w) m.StoreWord(r1+144+w*4,m.Word(r1+80+w*4));
    Guest821C8198(m,r1+96,r1+96,r30+224);
    f0=Lfs(m,r1+128); f0=double(float(f0*f29)); Stfs(m,r1+128,f0);
    f0=Lfs(m,r1+132); f0=double(float(f0*f29)); Stfs(m,r1+132,f0);
    f0=Lfs(m,r1+136); f0=double(float(f0*f29)); Stfs(m,r1+136,f0);
    auto& world=worlds.emplace_back();
    for(uint32_t i=0;i<16;++i) world[i]=m.Float(r1+96+i*4);
    r31+=20;
  } while(r31!=r29);
  return worlds;
}
bool SameBits(const NativeGuestMatrix& a,const NativeGuestMatrix& b) {
  return std::bit_cast<std::array<uint32_t,16>>(a)==std::bit_cast<std::array<uint32_t,16>>(b);
}
// A NaN result's sign bit is whatever x86 NaN propagation yields for the
// operation order the optimizer picked (it differs between -O0 and -O2 on
// either side); only NaN-ness is a guest fact here.
bool SameBits(double a,double b) { return U(a)==U(b) || (std::isnan(a) && std::isnan(b)); }

void GuestTrigMatchesTheRecompiledBodies() {
  // Reduction boundaries, both signs and zeros, huge and out-of-range values, NaN.
  std::vector<double> inputs{0.0,-0.0,1e-30,-1e-30,0.5,-0.5,1.0,1.5707963705062866,3.1415927410125732,-3.1415927410125732,
    6.2831854820251465,40.0,-40.0,12345.678,1e7,2.1999e8,2.2e8,-2.2e8,3e9,1e300,std::numeric_limits<double>::infinity(),
    -std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()};
  uint32_t seed=12345;
  for(int i=0;i<4000;++i) {
    seed=seed*0x41C64E6Du+12345u;
    inputs.push_back(double(std::bit_cast<float>((seed&0x807FFFFFu)|((0x70u+(seed>>24)%0x18u)<<23))));
  }
  for(const auto x:inputs) {
    Require(SameBits(NativeGuestSin(x),Guest821E9558(x)),"NativeGuestSin is 821E9558 bit for bit");
    Require(SameBits(NativeGuestCos(x),Guest821E9630(x)),"NativeGuestCos is 821E9630 bit for bit");
  }
  // The table is the CRT's: agreement with the host library to a few ulps.
  for(const double x:{0.3,1.0,2.5,-7.25,100.0,-12345.678})
    Require(std::fabs(NativeGuestSin(x)-std::sin(x))<1e-12 && std::fabs(NativeGuestCos(x)-std::cos(x))<1e-12,"guest trig is sin/cos");
  Require(SameBits(NativeGuestSin(-0.0),-0.0) && NativeGuestCos(-0.0)==1.0 && std::isnan(NativeGuestSin(3e8)),"zero and range paths");
}
constexpr uint32_t kMotherVtable=0x820054D0u;
// A synthetic clUfoMother01_Dummy: records as the constructor lays them out
// (ring index, x angle, y angle, radius, spin), a world with a translation.
void BuildMother(const Memory& memory,uint32_t object,uint32_t records,uint32_t count,float phase) {
  BuildObject(memory,object,kMotherVtable,false);
  BuildInstance(memory,object+1100); BuildPose(memory,object+1144,object+0x600,1,3.0f);
  BuildInstance(memory,object+NativeMotherSpheres::instance,kOtherNode);
  const std::array<float,16> world{0.9238795f,0,-0.3826834f,0, 0.0417f,0.99f,0.1007f,0, 0.3804f,-0.1236f,0.9184f,0,
    -1234.5f,850.25f,6021.75f,1};
  for(uint32_t i=0;i<16;++i) memory.StoreFloat(object+NativeMotherSpheres::world+i*4,world[i]);
  memory.StoreFloat(object+NativeMotherSpheres::phase,phase);
  memory.StoreWord(object+NativeMotherSpheres::records,records); memory.StoreWord(object+NativeMotherSpheres::records+4,records+count*20);
  memory.StoreWord(object+NativeMotherSpheres::count,count);
  for(uint32_t i=0;i<count;++i) {
    const auto at=records+i*20,ring=i%6;
    memory.StoreWord(at,ring);
    memory.StoreFloat(at+4,ring==0?0.0f:float(ring)*(1.0f/6.0f)*3.14159274f-1.57079637f);
    memory.StoreFloat(at+8,float(i)/float(count)*6.28318548f);
    memory.StoreFloat(at+12,std::array<float,3>{390,385,380}[ring%3]*1.2f);
    memory.StoreFloat(at+16,(ring&1?-1.0f:1.0f)*float(i*7919u%32768u)*6.98491931e-11f*float(1u<<20));
  }
}
void MotherSphereWorldsMatchTheGuest() {
  std::vector<uint8_t> bytes(0x20000); const Memory memory{bytes}; BuildScene(memory);
  constexpr uint32_t kMother=0x4000,kRecords=0xB000,kFrame=0xC000;
  for(const float phase:{0.0f,0.01f,123.45f,-3.5f,98765.4f}) {
    BuildMother(memory,kMother,kRecords,36,phase);
    std::vector<NativeGuestMatrix> native;
    ReadNativeMotherSphereWorlds(memory,kMother,native);
    const auto guest=Guest820EC180Worlds(memory,kMother,kFrame);
    Require(native.size()==36 && guest.size()==36,"one world per record");
    for(size_t i=0;i<native.size();++i) Require(SameBits(native[i],guest[i]),"sphere world is 820EC180's bit for bit");
  }
  memory.StoreWord(kMother+NativeMotherSpheres::count,0);
  std::vector<NativeGuestMatrix> none{NativeGuestMatrix{}};
  ReadNativeMotherSphereWorlds(memory,kMother,none);
  Require(none.empty() && Guest820EC180Worlds(memory,kMother,kFrame).empty(),"no records, no spheres");
  memory.StoreWord(kMother+NativeMotherSpheres::count,NativeMotherSpheres::max_records+1);
  bool rejected=false;
  try { ReadNativeMotherSphereWorlds(memory,kMother,none); } catch(const std::exception&) { rejected=true; }
  Require(rejected,"an implausible record count is refused");
}
void SingleWorldLayoutFollows821C9DA8() {
  std::vector<uint8_t> bytes(0x20000); const Memory memory{bytes}; BuildScene(memory);
  constexpr uint32_t kInstance=0x3000,kMeshes=0x3100;
  BuildInstance(memory,kInstance,kOtherNode);
  memory.StoreByte(kInstance+12,1);                      // Palette byte: 821C9DA8 never reads it.
  memory.StoreWord(kOtherNode+44,kMeshes); memory.StoreWord(kOtherNode+52,2);
  memory.StoreWord(kMeshes+44,0); memory.StoreByte(kMeshes+48,1);
  memory.StoreWord(kMeshes+4,0x10); memory.StoreWord(kMeshes+8,0x5); // Unreadable batches: never walked.
  memory.StoreWord(kMeshes+52+44,9); memory.StoreByte(kMeshes+52+48,0);
  const auto layout=Decoder(memory)(kInstance,0,0);
  Require(layout.single_world && !layout.skinned && !layout.bones && !layout.pose_vector && layout.meshes.size()==2,"single-world layout");
  Require(!layout.meshes[0].uploads_bone && layout.meshes[0].batches.empty(),"a rec+48 record is neither uploaded nor drawn");
  Require(layout.meshes[1].uploads_bone && layout.meshes[1].bone==9,"a rec+48==0 record uploads the world whatever its bone");
}
void MotherSpheresArePublishedEveryTick() {
  std::vector<uint8_t> bytes(0x20000); const Memory memory{bytes}; BuildScene(memory);
  NativeRenderRegistry registry(0);
  const auto decode=Decoder(memory);
  constexpr uint32_t kMother=0x4000,kRecords=0xB000,kFrame=0xC000;
  BuildMother(memory,kMother,kRecords,12,5.0f);
  const auto* type=FindNativeRenderClass(kMotherVtable);
  Require(type && type->instance==1100 && type->pose==1144 && type->attachments==kNativeRenderMotherSpheres,"clUfoMother01_Dummy row");
  registry.Born(kMother);
  auto entry=EntryOf(*registry.Tick(memory,kScene,1,decode),kMother);
  Require(entry && entry->models.size()==1 && entry->models[0].instance==kMother+1100 && entry->pose->size()==1,"the +1100 part keeps its pose");
  Require(entry->instanced.size()==1 && entry->instanced[0].model.instance==kMother+1172 && entry->instanced[0].model.layout &&
    entry->instanced[0].model.layout->single_world && entry->instanced[0].model.layout->node==kOtherNode,"sphere model captured without a pose");
  auto guest=Guest820EC180Worlds(memory,kMother,kFrame);
  Require(entry->instanced[0].worlds->size()==12,"a world per record");
  for(size_t i=0;i<guest.size();++i) Require(SameBits((*entry->instanced[0].worlds)[i],guest[i]),"published world is the guest's");
  // Not subscribed and no refresh budget: slot 3's phase still reaches the next tick.
  const auto first=entry;
  const auto unchanged=registry.Tick(memory,kScene,2,decode);
  Require(EntryOf(*unchanged,kMother)==first,"unchanged worlds keep the entry");
  memory.StoreFloat(kMother+NativeMotherSpheres::phase,5.01f);
  entry=EntryOf(*registry.Tick(memory,kScene,3,decode),kMother);
  guest=Guest820EC180Worlds(memory,kMother,kFrame);
  Require(entry!=first && entry->instanced[0].worlds!=first->instanced[0].worlds && entry->pose==first->pose &&
    entry->instanced[0].model.layout==first->instanced[0].model.layout,"a new phase republishes only the worlds");
  for(size_t i=0;i<guest.size();++i) Require(SameBits((*entry->instanced[0].worlds)[i],guest[i]),"re-read world is the guest's");
  registry.Died(kMother); Unlink(memory,kMother+108);
  Require(registry.Tick(memory,kScene,4,decode)->entries.empty(),"death removes the mothership");
}
// Pose motion (NativeModelPoseHistory's rules over published poses): a pose
// read on consecutive ticks carries the previous tick's pose; unchanged
// re-reads keep the entry pointer; a change within one tick is
// render-dependent until a reset; an unread tick, a bone count change and a
// new generation reset.
void PoseMotion() {
  std::vector<uint8_t> bytes(0x20000); const Memory memory{bytes}; BuildScene(memory);
  NativeRenderRegistry registry(0);
  const auto decode=Decoder(memory);
  constexpr uint32_t kTree=0x2000;
  BuildTree(memory,kTree,true);
  registry.Born(kTree);
  const auto tick=[&](uint64_t at) { return EntryOf(*registry.Tick(memory,kScene,at,decode),kTree); };
  const auto move=[&](float seed,uint32_t bones=2) { BuildPose(memory,kTree+400,kTree+0x600,bones,seed); };
  auto first=tick(1);
  Require(first && !first->motion.previous && first->motion.tick==1 && !first->motion.render_dependent,"a first read does not blend");
  move(2);
  auto second=tick(2);
  Require(second->motion.previous==first->pose && second->motion.tick==2 && !second->motion.render_dependent,
    "a pose read on consecutive ticks keeps the previous tick's");
  Require(tick(2)==second,"an unchanged re-read within the tick keeps the entry");
  Require(tick(3)==second,"an unchanged pose on the next tick keeps the entry (stationary since its tick)");
  move(3);
  auto fourth=tick(4);
  Require(fourth->motion.previous==second->pose && fourth->motion.tick==4,"a pose moving after a stationary tick blends from it");
  move(4);
  auto dependent=tick(4);
  Require(!dependent->motion.previous && dependent->motion.render_dependent && dependent->motion.tick==4,
    "a pose changing between two reads of one tick is render-dependent");
  move(5);
  auto sticky=tick(5);
  Require(!sticky->motion.previous && sticky->motion.render_dependent,"render dependence is sticky until a reset");
  move(6);
  auto skipped=tick(7);
  Require(!skipped->motion.previous && !skipped->motion.render_dependent && skipped->motion.tick==7,"an unread tick resets");
  move(7);
  auto resumed=tick(8);
  Require(resumed->motion.previous==skipped->pose && resumed->motion.tick==8,"blending resumes after a reset");
  move(8,3);
  auto resized=tick(9);
  Require(resized->pose->size()==3 && resized->models[0].layout!=resumed->models[0].layout && !resized->motion.previous,
    "a bone count change resets");
  move(9,3);
  Require(tick(10)->motion.previous==resized->pose,"blending resumes after a resize");
  registry.Died(kTree); registry.Born(kTree);
  move(10,3);
  auto reborn=tick(11);
  Require(reborn->generation!=resized->generation && !reborn->motion.previous,"a new generation resets");
  // Unchanged after a reset: nothing to republish.
  Require(tick(12)==reborn,"a reset, unchanged pose keeps the entry");
}
// Attachments and instanced worlds carry their own motion, keyed as their
// poses are shared (pose vector; set instance).
void AttachmentAndInstancedMotion() {
  std::vector<uint8_t> bytes(0x20000); const Memory memory{bytes}; BuildScene(memory);
  NativeRenderRegistry registry(0);
  const auto decode=Decoder(memory);
  constexpr uint32_t kSoldier=0x4000,kWeapons=0x10000;
  BuildObject(memory,kSoldier,kSoldierVtable,true);
  BuildInstance(memory,kSoldier+1168); BuildPose(memory,kSoldier+1088,0x9000,3,1.0f);
  memory.StoreByte(kSoldier+kNativeRenderFaceFlag,1);
  BuildInstance(memory,kSoldier+kNativeRenderFaceInstance); BuildPose(memory,kSoldier+kNativeRenderFacePose,0x9400,1,7.0f);
  memory.StoreWord(kSoldier+kNativeRenderWeaponArray,kWeapons); memory.StoreWord(kSoldier+kNativeRenderWeaponCount,1);
  memory.StoreByte(kWeapons+1404,1); memory.StoreByte(kWeapons+1405,1); memory.StoreWord(kWeapons+108,0x1234);
  BuildInstance(memory,kWeapons+100); BuildPose(memory,kWeapons+144,0x9800,1,2.0f);
  registry.Born(kSoldier);
  const auto first=EntryOf(*registry.Tick(memory,kScene,1,decode),kSoldier);
  Require(first->attachments.size()==2 && !first->attachments[0].motion.previous && !first->attachments[1].motion.previous,
    "attachments start without a previous pose");
  BuildPose(memory,kSoldier+kNativeRenderFacePose,0x9400,1,8.0f);
  const auto second=EntryOf(*registry.Tick(memory,kScene,2,decode),kSoldier);
  Require(second->attachments[0].motion.previous==first->attachments[0].pose && second->attachments[0].motion.tick==2 &&
    second->attachments[1].motion==first->attachments[1].motion && second->attachments[1].pose==first->attachments[1].pose &&
    second->motion==first->motion,"only the moved face carries its previous pose");
  constexpr uint32_t kMother=0x6000,kRecords=0xB000;
  BuildMother(memory,kMother,kRecords,6,5.0f);
  registry.Born(kMother);
  const auto mother=EntryOf(*registry.Tick(memory,kScene,3,decode),kMother);
  Require(mother->instanced.size()==1 && !mother->instanced[0].motion.previous,"a first read of the worlds does not blend");
  memory.StoreFloat(kMother+NativeMotherSpheres::phase,5.01f);
  const auto moved=EntryOf(*registry.Tick(memory,kScene,4,decode),kMother);
  Require(moved->instanced[0].motion.previous==mother->instanced[0].worlds && moved->instanced[0].motion.tick==4,
    "moved worlds carry the previous tick's");
}
}

// A render-only iteration (refresh false) without probes: the published
// snapshot is returned as is while no event arrived and nothing is pending,
// whatever a subscribed object's memory holds; events (a birth, a death, a
// subscription) are still applied and published at the same tick; the next
// full tick re-reads the subscribers. The idle audit (AuditLight) counts what
// a full tick would change without publishing or moving any motion.
void RenderOnlyTicks() {
  std::vector<uint8_t> bytes(0x20000); const Memory memory{bytes}; BuildScene(memory);
  NativeRenderRegistry registry(8,0);
  const auto decode=Decoder(memory);
  constexpr uint32_t kMoving=0x2000,kStill=0x3000,kLate=0x4000;
  BuildTree(memory,kMoving,true); BuildTree(memory,kStill);
  registry.Born(kMoving); registry.Born(kStill);
  const auto stepped=registry.Tick(memory,kScene,1,decode);
  Require(stepped->entries.size()==2 && registry.stats().subscribed==1,"the step publishes both entries");
  // Nothing happened: the same snapshot, no new generation, nothing read.
  const auto builds=registry.stats().builds;
  const auto idle=registry.Tick(memory,kScene,1,decode,false);
  Require(idle==stepped && registry.stats().builds==builds && registry.stats().idle_ticks==1 && registry.stats().light_ticks==1,
    "an idle render-only tick read or published something");
  // A subscriber's pose moved without a step (what the idle audit looks for):
  // the light tick leaves it (no probe); the audit counts the one entry and
  // leaves the published entry, its motion and the live set alone.
  BuildPose(memory,kMoving+400,kMoving+0x600,2,40.0f);
  Require(registry.Tick(memory,kScene,1,decode,false)==stepped,"a light tick re-read a subscriber");
  const auto stats=registry.stats();
  Require(registry.AuditLight(memory,kScene,1,decode)==1,"the audit counts the moved subscriber, and only it");
  Require(registry.AcquireSnapshot()==stepped && !EntryOf(*stepped,kMoving)->motion.render_dependent &&
    registry.stats().live==0 && registry.stats().builds==stats.builds && registry.stats().changed==stats.changed,
    "the audit published, counted or marked something");
  Require(registry.Tick(memory,kScene,1,decode,false)==stepped,"the audit made the next light tick publish");
  // A birth and a subscription on a render-only iteration: applied, read and
  // published at the same tick, without re-reading the other subscribers.
  BuildPose(memory,kMoving+400,kMoving+0x600,2,80.0f);
  BuildTree(memory,kLate);
  registry.Born(kLate);
  const auto born=registry.Tick(memory,kScene,1,decode,false);
  Require(born!=stepped && born->generation==stepped->generation+1 && born->tick==1 && EntryOf(*born,kLate) &&
    EntryOf(*born,kMoving)==EntryOf(*stepped,kMoving) && registry.stats().light_ticks==4,"a render-only birth");
  Require(CountNativeRenderSnapshotChanges(*stepped,*born)==1 && CountNativeRenderSnapshotChanges(*born,*stepped)==1 &&
    CountNativeRenderSnapshotChanges(*born,*born)==0,"an added entry counts once either way");
  registry.Subscribed(kStill,true); Link(memory,kUpdatesList,kStill+120,kStill);
  memory.StoreWord(kStill+kNativeRenderObjectSubscribed,1);
  memory.StoreByte(kStill+kNativeRenderObjectHidden,1);
  const auto subscribed=registry.Tick(memory,kScene,1,decode,false);
  Require(EntryOf(*subscribed,kStill)->hidden && EntryOf(*subscribed,kMoving)==EntryOf(*stepped,kMoving) &&
    registry.stats().subscribed==2,"a render-only subscription reads its object");
  // A death on a render-only iteration is unpublished at once.
  registry.Died(kLate); Unlink(memory,kLate+108);
  const auto dead=registry.Tick(memory,kScene,1,decode,false);
  Require(!EntryOf(*dead,kLate) && dead->entries.size()==2 && CountNativeRenderSnapshotChanges(*subscribed,*dead)==1,
    "a render-only death");
  // The next step re-reads every subscriber; the pose moved since tick 1 blends.
  const auto next=registry.Tick(memory,kScene,2,decode);
  const auto moving=EntryOf(*next,kMoving);
  Require((*moving->pose)[0][0]==80.0f && next->tick==2 && moving->motion.previous==EntryOf(*stepped,kMoving)->pose &&
    !moving->motion.render_dependent,"the next step reads what the light ticks left");
  Require(!registry.AuditScene(memory,kScene).mismatches(),"light ticks keep the registry matching the scene");
  // Before any publication a render-only tick publishes (seeds) anyway.
  NativeRenderRegistry fresh(0);
  const auto first=fresh.Tick(memory,kScene,5,decode,false);
  Require(first && first->entries.size()==2 && fresh.stats().seeded==2,"a first render-only tick seeds and publishes");
}
// Render-only iterations with the probe: a light tick re-reads `probe`
// update members round robin, so a member whose pose a render-only iteration
// moves is found within members/probe light ticks; its pose is then
// render-dependent (drawn as read), and it is live: read on every light tick
// until kLiveQuiet light reads leave it unchanged. Idempotent members publish
// nothing, so an unchanged light tick is still no new generation.
void RenderOnlyProbes() {
  std::vector<uint8_t> bytes(0x20000); const Memory memory{bytes}; BuildScene(memory);
  NativeRenderRegistry registry(0,1);
  const auto decode=Decoder(memory);
  constexpr uint32_t kA=0x2000,kB=0x3000,kC=0x4000;
  BuildTree(memory,kA,true); BuildTree(memory,kB,true); BuildTree(memory,kC);
  registry.Born(kA); registry.Born(kB); registry.Born(kC);
  const auto stepped=registry.Tick(memory,kScene,1,decode);
  // Idempotent members: probed, unchanged, no new generation.
  const auto builds=registry.stats().builds;
  Require(registry.Tick(memory,kScene,1,decode,false)==stepped && registry.stats().builds==builds+1 &&
    registry.stats().probes==1 && registry.stats().idle_ticks==1,"an unchanged probe published");
  Require(registry.Tick(memory,kScene,1,decode,false)==stepped && registry.stats().probes==2,"the probe moves on");
  // kB's slot 2 poses on render-only iterations: found within two light ticks
  // (two members, one probe each); kC (no subscription) is never probed.
  BuildPose(memory,kB+400,kB+0x600,2,40.0f);
  BuildPose(memory,kC+400,kC+0x600,2,40.0f);
  std::shared_ptr<const NativeRenderRegistrySnapshot> found;
  for(int i=0;i<2 && !found;++i) {
    const auto light=registry.Tick(memory,kScene,1,decode,false);
    if(light!=stepped) found=light;
  }
  Require(found && found->tick==1 && (*EntryOf(*found,kB)->pose)[0][0]==40.0f,"the probe missed a render-posed member");
  Require(EntryOf(*found,kB)->motion.render_dependent && !EntryOf(*found,kB)->motion.previous &&
    registry.stats().same_tick_changes==1 && registry.stats().live==1,"a render-only change is render-dependent and live");
  Require(EntryOf(*found,kC)==EntryOf(*stepped,kC) && EntryOf(*found,kA)==EntryOf(*stepped,kA),"only the moved member changed");
  // Live: read on every light tick, drawn as read.
  BuildPose(memory,kB+400,kB+0x600,2,41.0f);
  const auto live=registry.Tick(memory,kScene,1,decode,false);
  Require((*EntryOf(*live,kB)->pose)[0][0]==41.0f && registry.stats().live_reads>=1,"a live member is read every light tick");
  // A step: still render-dependent (sticky), not blended.
  BuildPose(memory,kB+400,kB+0x600,2,42.0f);
  const auto next=registry.Tick(memory,kScene,2,decode);
  Require(EntryOf(*next,kB)->motion.render_dependent && !EntryOf(*next,kB)->motion.previous,"render dependence is sticky");
  // Quiet: kLiveQuiet unchanged light reads return it to the probe.
  for(int i=0;i<300 && registry.stats().live;++i) registry.Tick(memory,kScene,2,decode,false);
  Require(!registry.stats().live,"a quiet live member stays live");
  // A death drops it from the probe.
  registry.Died(kA); Unlink(memory,kA+108); Unlink(memory,kA+120);
  registry.Tick(memory,kScene,3,decode);
  for(int i=0;i<4;++i) registry.Tick(memory,kScene,3,decode,false);
  Require(!registry.AuditScene(memory,kScene).mismatches(),"probing kept the registry matching the scene");
}
// An object whose vtable stays the base's is re-read at spaced ticks, not at
// every tick: light ticks in between stay idle.
void PermanentlyDeferredBacksOff() {
  std::vector<uint8_t> bytes(0x20000); const Memory memory{bytes}; BuildScene(memory);
  NativeRenderRegistry registry(0,0);
  const auto decode=Decoder(memory);
  constexpr uint32_t kTree=0x2000,kBase=0x3000;
  BuildTree(memory,kTree);
  BuildObject(memory,kBase,kNativeRenderBaseVtable,false);
  registry.Born(kTree); registry.Born(kBase);
  const auto first=registry.Tick(memory,kScene,1,decode);
  Require(registry.stats().deferred==1,"the base-vtable object is deferred");
  // Light ticks of the same tick: nothing due, idle.
  for(int i=0;i<4;++i) Require(registry.Tick(memory,kScene,1,decode,false)==first,"a deferred object kept a light tick busy");
  Require(registry.stats().deferred==1 && registry.stats().idle_ticks==4,"a deferred object was re-read at the same tick");
  // Across ticks the reads spread out: 1, 2, 4, 8 ... ticks apart.
  for(uint64_t tick=2;tick<=64;++tick) {
    registry.Tick(memory,kScene,tick,decode);
    for(int i=0;i<2;++i) registry.Tick(memory,kScene,tick,decode,false);
  }
  Require(registry.stats().deferred<=8,"deferred reads are not backed off");
  // Resolved when the derived vtable arrives (at the next due read).
  memory.StoreWord(kBase,kTreeVtable);
  BuildInstance(memory,kBase+416); BuildPose(memory,kBase+400,kBase+0x600,2,1.0f);
  std::shared_ptr<const NativeRenderRegistrySnapshot> resolved;
  for(uint64_t tick=65;tick<=200 && !(resolved && EntryOf(*resolved,kBase));++tick) resolved=registry.Tick(memory,kScene,tick,decode);
  Require(resolved && EntryOf(*resolved,kBase),"a backed-off object never resolved");
}
// Alive: an entry's lifetime ends at its object's destructor or at another
// constructor at its address, as soon as the hook runs (before a Tick).
void LifetimeTokens() {
  std::vector<uint8_t> bytes(0x20000); const Memory memory{bytes}; BuildScene(memory);
  NativeRenderRegistry registry(0);
  const auto decode=Decoder(memory);
  constexpr uint32_t kTree=0x2000;
  BuildTree(memory,kTree);
  registry.Born(kTree);
  const auto entry=EntryOf(*registry.Tick(memory,kScene,1,decode),kTree);
  Require(entry && registry.Alive(kTree,entry->generation) && !registry.Alive(kTree,entry->generation+1) && !registry.Alive(0x5000,1),
    "a published entry is alive");
  registry.Died(kTree);
  Require(!registry.Alive(kTree,entry->generation),"a death ends the lifetime before the tick");
  registry.Born(kTree);  // Same address, same vtable.
  Require(!registry.Alive(kTree,entry->generation),"a rebirth is not the old lifetime");
  const auto reborn=EntryOf(*registry.Tick(memory,kScene,2,decode),kTree);
  Require(reborn->generation!=entry->generation && registry.Alive(kTree,reborn->generation) && !registry.Alive(kTree,entry->generation),
    "the new lifetime is the new entry's");
  // A birth and a death drained together leave no lifetime.
  registry.Died(kTree); registry.Born(kTree); registry.Died(kTree);
  registry.Tick(memory,kScene,3,decode);
  Require(!registry.Alive(kTree,reborn->generation) && !registry.Alive(kTree,reborn->generation+1),"a dead object is alive");
  registry.Born(kTree);
  const auto last=EntryOf(*registry.Tick(memory,kScene,4,decode),kTree);
  Require(registry.Alive(kTree,last->generation),"a later birth is alive");
  registry.Clear();
  Require(!registry.Alive(kTree,last->generation),"Clear keeps lifetimes");
}
// Captures of instances a Build no longer draws (a weapon array moved) are
// released once kCaptureAge builds pass without them.
void CapturesArePruned() {
  std::vector<uint8_t> bytes(0x60000); const Memory memory{bytes}; BuildScene(memory);
  NativeRenderRegistry registry(0);
  const auto decode=Decoder(memory);
  constexpr uint32_t kSoldier=0x4000;
  BuildObject(memory,kSoldier,kSoldierVtable,true);
  BuildInstance(memory,kSoldier+1168); BuildPose(memory,kSoldier+1088,0x9000,3,1.0f);
  memory.StoreWord(kSoldier+kNativeRenderWeaponCount,1);
  registry.Born(kSoldier);
  // Each tick the weapon array is somewhere else: a new instance address.
  for(uint64_t tick=1;tick<=200;++tick) {
    const uint32_t weapons=0x10000+uint32_t(tick%100)*0x800;
    memory.StoreWord(kSoldier+kNativeRenderWeaponArray,weapons);
    memory.StoreByte(weapons+1404,1); memory.StoreByte(weapons+1405,1); memory.StoreWord(weapons+108,0x1234);
    BuildInstance(memory,weapons+100); BuildPose(memory,weapons+144,weapons+0x600,1,2.0f);
    const auto entry=EntryOf(*registry.Tick(memory,kScene,tick,decode),kSoldier);
    Require(entry && entry->attachments.size()==1 && entry->attachments[0].model.layout,"the moved weapon is drawn");
  }
  Require(registry.stats().captures_pruned>=100,"captures of instances no longer drawn were kept");
}
// The previous tick's bound: carried while the object is read on consecutive
// ticks and the bound moves; kept while unchanged; reset by an unread tick.
void BoundMotion() {
  std::vector<uint8_t> bytes(0x20000); const Memory memory{bytes}; BuildScene(memory);
  NativeRenderRegistry registry(0);
  const auto decode=Decoder(memory);
  constexpr uint32_t kTree=0x2000;
  BuildTree(memory,kTree,true);
  registry.Born(kTree);
  const auto tick=[&](uint64_t at,bool refresh=true) { return EntryOf(*registry.Tick(memory,kScene,at,decode,refresh),kTree); };
  const auto first=tick(1);
  Require(!first->bound_motion.valid,"a first read has no previous bound");
  Require(tick(2)==first,"an unchanged bound keeps the entry");
  memory.StoreFloat(kTree+kNativeRenderObjectCentre,50.0f); memory.StoreFloat(kTree+kNativeRenderObjectRadius,6.0f);
  const auto moved=tick(3);
  Require(moved->bound_motion.valid && moved->bound_motion.tick==3 && moved->bound_motion.centre[0]==1.0f &&
    moved->bound_motion.radius==5.0f && moved->bound_motion.axes==first->axes,"a moved bound carries the previous tick's");
  Require(tick(4)==moved,"an unchanged bound keeps its motion");
  memory.StoreFloat(kTree+kNativeRenderObjectCentre+8,9.0f);
  const auto again=tick(5);
  Require(again->bound_motion.tick==5 && again->bound_motion.centre[0]==50.0f && again->bound_motion.centre[2]==3.0f,
    "a bound moving after a stationary tick has that tick's");
  memory.StoreFloat(kTree+kNativeRenderObjectCentre+8,10.0f);
  const auto same_tick=tick(5,false);
  Require(same_tick->bound_motion.tick==5 && same_tick->bound_motion.centre[2]==3.0f,
    "a render-only move keeps the previous tick's bound");
  memory.StoreFloat(kTree+kNativeRenderObjectCentre+8,11.0f);
  Require(!tick(7)->bound_motion.valid,"an unread tick resets");
}

// The box half axes decide the partial-sphere box test: a change to them alone
// must publish a new entry, not keep the old one.
void EntryEqualityCoversAxes() {
  NativeRenderEntry a; a.object=0x2000; a.generation=1;
  auto b=a;
  Require(SameNativeRenderEntry(a,b),"identical entries are the same");
  b.axes[7]=1.0f;
  Require(!SameNativeRenderEntry(a,b),"an axes-only change is a new entry");
}

int main() {
  try {
    ClassTableLookup(); EntryEqualityCoversAxes();
    BirthAndDeath();
    SkyIsNotPublished();
    ResolvesClassAfterDerivedConstructor();
    SubscriptionDrivesRereads();
    RenderOnlyTicks();
    RenderOnlyProbes();
    PermanentlyDeferredBacksOff();
    LifetimeTokens();
    CapturesArePruned();
    BoundMotion();
    LayoutCaptureWaitsForSizedPose();
    CharacterLodAndAttachments();
    ClassConstantsAndParts();
    ObjectConstants();
    AlienTankTurrets();
    VehicleWeaponsAndTreads();
    AuditCountsMismatches();
    GuestTrigMatchesTheRecompiledBodies();
    MotherSphereWorldsMatchTheGuest();
    SingleWorldLayoutFollows821C9DA8();
    MotherSpheresArePublishedEveryTick();
    PoseMotion();
    AttachmentAndInstancedMotion();
  } catch(const std::exception& error) {
    std::cerr<<"native render registry test failed: "<<error.what()<<"\n";
    return 1;
  }
  std::cout<<"native render registry tests passed\n";
  return 0;
}

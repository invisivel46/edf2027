#include "native_graphics/native_render_registry.h"
#include <bit>
#include <iostream>
#include <string_view>
#include <vector>

using namespace edf::native;
namespace {
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
// Synthetic big-endian guest memory, as in the model publication tests.
struct Memory {
  std::vector<uint8_t>& bytes;
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
  return [&memory,decodes](uint32_t instance,uint32_t vector) {
    if(decodes) ++*decodes;
    return DecodeNativeModelLayoutWith(memory,instance,vector,[](uint32_t,NativeModelBuffers::Kind)->uint64_t { return 0; });
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
  Require(sky && sky->cadence==NativeRenderPoseCadence::Frame && sky->instance==412 && sky->pose==384,"clSky row");
  const auto* broken=FindNativeRenderClass(0x820077D8u);
  Require(broken && broken->cadence==NativeRenderPoseCadence::Frame && broken->instance==384 && broken->pose==428,"clBrokenObject row");
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
}

int main() {
  try {
    ClassTableLookup();
    BirthAndDeath();
    ResolvesClassAfterDerivedConstructor();
    SubscriptionDrivesRereads();
    LayoutCaptureWaitsForSizedPose();
    CharacterLodAndAttachments();
    AuditCountsMismatches();
  } catch(const std::exception& error) {
    std::cerr<<"native render registry test failed: "<<error.what()<<"\n";
    return 1;
  }
  std::cout<<"native render registry tests passed\n";
  return 0;
}

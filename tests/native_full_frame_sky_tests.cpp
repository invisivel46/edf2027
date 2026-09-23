#include "native_graphics/native_full_frame_sky.h"
#include "native_graphics/native_render_registry.h"
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>
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
    if(!at || at>bytes.size() || size>bytes.size()-at) throw std::runtime_error("sky test range");
    return bytes.data()+at;
  }
  void StoreWord(uint32_t at,uint32_t value) const { for(unsigned i=0;i<4;++i) bytes.at(at+i)=uint8_t(value>>(24-i*8)); }
  void StoreByte(uint32_t at,uint8_t value) const { bytes.at(at)=value; }
  void StoreFloat(uint32_t at,float value) const { StoreWord(at,std::bit_cast<uint32_t>(value)); }
  float Float(uint32_t at) const { return std::bit_cast<float>(Word(at)); }
};

// ---- Transcription of the guest bodies, operating on guest memory. ----
// A 128-bit register as simde sees it after the byte-reversing VectorMaskL
// load: lane i is guest element 3-i.
using Lanes=std::array<float,4>;
Lanes Load(const Memory& m,uint32_t at) { return {m.Float(at+12),m.Float(at+8),m.Float(at+4),m.Float(at)}; }
void Store(const Memory& m,uint32_t at,const Lanes& v) { for(unsigned i=0;i<4;++i) m.StoreFloat(at+i*4,v[3-i]); }
Lanes Hi(const Lanes& a,const Lanes& b) { return {a[2],b[2],a[3],b[3]}; } // simde_mm_unpackhi_epi32
Lanes Lo(const Lanes& a,const Lanes& b) { return {a[0],b[0],a[1],b[1]}; } // simde_mm_unpacklo_epi32
// _mm_dp_ps(a,b,0xFF): (t0+t1)+(t2+t3) broadcast, one rounding per operation.
Lanes Dp(const Lanes& a,const Lanes& b) {
  const float t0=a[0]*b[0]; const float t1=a[1]*b[1]; const float t2=a[2]*b[2]; const float t3=a[3]*b[3];
  const float l=t0+t1; const float h=t2+t3; const float s=l+h;
  return {s,s,s,s};
}
// sub_821C8198 (edf2017_recomp.79.cpp:8666), statement for statement.
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
// sub_821D1688 (edf2017_recomp.52.cpp:8930).
void Guest821D1688(const Memory& m,uint32_t node,uint32_t parent) {
  Guest821C8198(m,node+240,node+176,parent);
  const auto children=m.Word(node+80);
  for(uint32_t i=0,count=m.Word(node+88);i<count;++i) Guest821D1688(m,children+i*304,node+240);
}
// sub_821C8C58 (edf2017_recomp.34.cpp:8935).
void Guest821C8C58(const Memory& m,uint32_t tree,uint32_t parent) {
  const auto roots=m.Word(tree);
  for(uint32_t i=0,count=m.Word(tree+8);i<count;++i) Guest821D1688(m,roots+i*304,parent);
}
// sub_821C9478 (edf2017_recomp.35.cpp:8778); the vector is already sized, so
// 821C9400 does not run.
void Guest821C9478(const Memory& m,uint32_t tree,uint32_t vector) {
  const auto count=m.Word(tree+20),begin=m.Word(vector+4);
  Require((m.Word(vector+8)-begin)/64==count,"transcription expects a sized pose vector");
  for(uint32_t i=0;i<count;++i) {
    const auto node=m.Word(m.Word(tree+12)+i*4);
    if(!m.Bytes(tree+24,1)[0]) for(uint32_t w=0;w<16;++w) m.StoreWord(begin+i*64+w*4,m.Word(node+240+w*4));
    else Guest821C8198(m,begin+i*64,node+112,node+240);
  }
}
// sub_820BB270 up to the model draw.
void Guest820BB270(const Memory& m,uint32_t sky,uint32_t context) {
  const auto scene=m.Word(context+16);
  for(uint32_t w=0;w<4;++w) m.StoreWord(sky+272+w*4,m.Word(scene+272+w*4));
  Guest821C8C58(m,sky+428,sky+224);
  Guest821C9478(m,sky+428,sky+384);
}

// ---- Synthetic sky. ----
constexpr uint32_t kSky=0x1000,kRoots=0x2000,kChildren=0x3000,kGrandchild=0x4000,kBones=0x5000,kPose=0x6000,
  kScene=0x7000,kContext=0x7800,kContainer=0x8000,kModelNode=0x8100,kRecords=0x8200,kBatch=0x8400,kMaterial=0x8600,
  kPass=0x8700,kStates=0x8800,kDeclarationMap=0x8900,kDeclarationNode=0x8A00;
void StoreMatrix(const Memory& m,uint32_t at,const NativeSkyMatrix& value) { for(uint32_t i=0;i<16;++i) m.StoreFloat(at+i*4,value[i]); }
NativeSkyMatrix Rigid(float yaw,float pitch,float x,float y,float z,float scale=1) {
  const float cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
  return {scale*cy,0,-scale*sy,0, scale*sy*sp,scale*cp,scale*cy*sp,0, scale*sy*cp,-scale*sp,scale*cy*cp,0, x,y,z,1};
}
// Two roots; root 0 has two children, child 1 has one child. Four bones in a
// different order than the walk.
void BuildTree(const Memory& m,uint32_t tree,bool bind) {
  m.StoreWord(tree,kRoots); m.StoreWord(tree+8,2);
  m.StoreWord(tree+12,kBones); m.StoreWord(tree+20,4); m.StoreByte(tree+24,bind);
  const std::array<uint32_t,5> nodes{kRoots,kRoots+304,kChildren,kChildren+304,kGrandchild};
  for(size_t i=0;i<nodes.size();++i) {
    StoreMatrix(m,nodes[i]+176,Rigid(0.3f*float(i)+0.11f,0.05f*float(i),1.5f*float(i)-2,0.25f+float(i),-0.75f*float(i),1+0.125f*float(i)));
    StoreMatrix(m,nodes[i]+112,Rigid(-0.2f*float(i),0.1f,0.5f,-1.25f*float(i),2.0f));
  }
  m.StoreWord(kRoots+80,kChildren); m.StoreWord(kRoots+88,2);
  m.StoreWord(kChildren+304+80,kGrandchild); m.StoreWord(kChildren+304+88,1);
  for(const auto [i,node]:std::array<std::pair<uint32_t,uint32_t>,4>{{{0,kGrandchild},{1,kRoots+304},{2,kRoots},{3,kChildren}}})
    m.StoreWord(kBones+i*4,node);
}
void BuildSky(const Memory& m,bool bind) {
  m.StoreWord(kSky,NativeSkyObject::vtable);
  StoreMatrix(m,kSky+224,Rigid(0.7f,0,-3.25f,17.5f,9.125f));
  BuildTree(m,kSky+428,bind);
  m.StoreWord(kSky+384+4,kPose); m.StoreWord(kSky+384+8,kPose+4*64);
  m.StoreWord(kContext+16,kScene);
  for(const auto [w,value]:std::array<std::pair<uint32_t,float>,4>{{{0,1234.5f},{1,-87.25f},{2,40961.125f},{3,1.0f}}})
    m.StoreFloat(kScene+272+w*4,value);
}
std::array<float,16> CameraWorld(const Memory& m) {
  std::array<float,16> world{};
  for(uint32_t w=0;w<4;++w) world[12+w]=m.Float(kScene+272+w*4);
  return world;
}
bool SameBits(const NativeSkyMatrix& a,const Memory& m,uint32_t at) {
  for(uint32_t i=0;i<16;++i) if(std::bit_cast<uint32_t>(a[i])!=m.Word(at+i*4)) return false;
  return true;
}
void PoseMatchesGuest(bool bind) {
  std::vector<uint8_t> bytes(0x10000); const Memory memory{bytes};
  BuildSky(memory,bind);
  NativeSkyHierarchyCache cache;
  const auto& hierarchy=cache.Acquire(memory,kSky);
  // Native first: it only reads, so the guest run afterwards sees the same inputs.
  const auto pose=ComputeNativeSkyPose(hierarchy,NativeSkyWorld(memory,kSky,NativeSkyCameraTranslation(CameraWorld(memory))));
  Guest820BB270(memory,kSky,kContext);
  Require(hierarchy.nodes.size()==5 && hierarchy.bones==std::vector<uint32_t>{3,4,0,1},"hierarchy walk order and bone map");
  Require(SameBits(pose.world,memory,kSky+224),"sky world matches this+224 after the camera copy");
  for(size_t i=0;i<hierarchy.nodes.size();++i) Require(SameBits(pose.nodes[i],memory,hierarchy.nodes[i].address+240),"node world matches node+240");
  for(uint32_t i=0;i<4;++i) Require(SameBits(pose.palette[i],memory,kPose+i*64),"pose entry matches the guest pose vector");
}
void CacheReusesAndDetectsChange() {
  std::vector<uint8_t> bytes(0x10000); const Memory memory{bytes};
  BuildSky(memory,false);
  NativeSkyHierarchyCache cache; cache.verify_interval=2;
  const auto* first=&cache.Acquire(memory,kSky);
  Require(&cache.Acquire(memory,kSky)==first && cache.builds()==1 && cache.verifications()==0,"unchanged tree is reused");
  Require(&cache.Acquire(memory,kSky)==first && cache.verifications()==1 && cache.changes()==0,"verification keeps an unchanged tree");
  memory.StoreFloat(kGrandchild+176+48,99.0f);
  (void)cache.Acquire(memory,kSky);
  Require(cache.builds()==1,"a local change is only seen at verification");
  const auto& changed=cache.Acquire(memory,kSky);
  Require(cache.builds()==2 && cache.changes()==1 && changed.nodes[3].local[12]==99.0f,"verification rebuilds a changed local");
  memory.StoreWord(kSky+448,3);
  Require(cache.Acquire(memory,kSky).bones.size()==3 && cache.builds()==3,"a new bone table rebuilds at once");
  memory.StoreWord(kChildren+304+80,kRoots); memory.StoreWord(kChildren+304+88,1);
  bool rejected=false;
  try { (void)ReadNativeSkyHierarchy(memory,kSky); } catch(const std::exception&) { rejected=true; }
  Require(rejected,"a cyclic tree is rejected");
}
// One rigid mesh record, one batch, one pass whose ops turn depth test and write off.
void BuildModel(const Memory& m,uint32_t bone,uint32_t instance=kSky+412) {
  m.StoreWord(instance,kContainer); m.StoreWord(instance+4,kModelNode); m.StoreByte(instance+12,0);
  m.StoreWord(kContainer+4,kContainer+0x40);
  m.StoreWord(kModelNode+44,kRecords); m.StoreWord(kModelNode+52,1);
  m.StoreWord(kRecords+4,kBatch); m.StoreWord(kRecords+8,kBatch+148); m.StoreWord(kRecords+44,bone);
  m.StoreWord(kBatch,kMaterial); m.StoreWord(kBatch+60,32); m.StoreWord(kBatch+72,kDeclarationMap);
  m.StoreWord(kBatch+76,kDeclarationNode); m.StoreWord(kBatch+140,300);
  m.StoreWord(kDeclarationMap+4,kDeclarationMap+0x40); m.StoreWord(kDeclarationNode+28,0x9000);
  m.StoreWord(kMaterial+16,kPass); m.StoreWord(kMaterial+24,1);
  m.StoreWord(kPass+96,kStates); m.StoreWord(kPass+104,2);
  m.StoreWord(kStates,0x28); m.StoreWord(kStates+4,0); m.StoreWord(kStates+8,0x30); m.StoreWord(kStates+12,0);
}
void RecordsSkyDraws() {
  std::vector<uint8_t> bytes(0x10000); const Memory memory{bytes};
  BuildSky(memory,false); BuildModel(memory,1);
  NativeSkyPassState state;
  NativeSkyFrameInputs inputs; inputs.camera_world=CameraWorld(memory);
  inputs.start.depth_target=1; inputs.start.words[1]=2|4|(3u<<4);
  const auto lookup=[](uint32_t,NativeModelBuffers::Kind) -> uint64_t { return 1; };
  std::vector<NativeSkyDraw> draws;
  const auto record=RecordNativeSky(memory,state,inputs,kSky,lookup,[&](const NativeSkyDraw& draw) { draws.push_back(draw); });
  Require(record.status==NativeSkyStatus::Recorded && record.draws==1 && draws.size()==1,"one sky draw");
  Require(draws[0].pass==kPass && draws[0].geometry && draws[0].geometry->draw_count==300,"draw names its pass and batch");
  Require(draws[0].depth==NativeSkyDepth{false,false,3},"depth state from the pass's state ops");
  Require(draws[0].world==NativeModelWorldWords(record.pose.palette[1]),"g_mWorld is the record's bone, column-major");
  Guest820BB270(memory,kSky,kContext);
  Require(SameBits(record.pose.palette[1],memory,kPose+64),"recorded pose matches the guest");
  // Reuse, then the gates.
  (void)RecordNativeSky(memory,state,inputs,kSky,lookup,[](const NativeSkyDraw&) {});
  Require(state.decodes==1 && state.hierarchy.builds()==1,"layout and hierarchy are cached");
  memory.StoreByte(kSky+65,1);
  Require(RecordNativeSky(memory,state,inputs,kSky,lookup,[](const NativeSkyDraw&) {}).status==NativeSkyStatus::Hidden,"hidden sky");
  memory.StoreByte(kSky+65,0); memory.StoreByte(kSky+36,1);
  Require(RecordNativeSky(memory,state,inputs,kSky,lookup,[](const NativeSkyDraw&) {}).status==NativeSkyStatus::Released,"released sky");
  memory.StoreByte(kSky+36,0);
  const auto none=[](uint32_t,NativeModelBuffers::Kind) -> uint64_t { return 0; };
  Require(RecordNativeSky(memory,state,inputs,kSky,none,[](const NativeSkyDraw&) {}).status==NativeSkyStatus::Declined,"unpublished buffers decline");
  memory.StoreWord(kStates,0x04);
  size_t sunk=0;
  Require(RecordNativeSky(memory,state,inputs,kSky,lookup,[&](const NativeSkyDraw&) { ++sunk; }).status==NativeSkyStatus::Declined && !sunk,
    "an undecoded state op declines before any draw");
  Require(RecordNativeSky(memory,state,inputs,0,lookup,[](const NativeSkyDraw&) {}).status==NativeSkyStatus::NoSky,"no sky");
  NativeSkyRegistry registry;
  registry.Constructed(kSky); registry.Destroyed(kSky+4);
  Require(registry.Current()==kSky,"another object's destruction keeps the sky");
  registry.Destroyed(kSky);
  Require(registry.Current()==0,"the sky's destruction clears it");
}
// ---- clBrokenObject (vtable 820077D8): the same walk from obj+640. ----
// sub_8211FAA8 (edf2017_recomp.78.cpp:3596) up to its 821C9C20 draw.
void Guest8211FAA8(const Memory& m,uint32_t object) {
  m.StoreWord(object+712,m.Word(object+708));
  Guest821C8C58(m,object+400,object+640);
  Guest821C9478(m,object+400,object+428);
}
constexpr uint32_t kBroken=0x1000,kBrokenVtable=0x820077D8u,kSceneEnd=0x7F00;
// A full-frame clBrokenObject: model instance +384 (tree +400), root +640,
// and the pose vector +428 empty, since slot 4 never ran to size it.
void BuildBrokenObject(const Memory& m,bool bind) {
  m.StoreWord(kBroken,kBrokenVtable); m.StoreWord(kBroken+kNativeRenderObjectScene,kScene);
  m.StoreFloat(kBroken+kNativeRenderObjectRadius,5.0f); m.StoreFloat(kBroken+kNativeRenderObjectCull,300.0f);
  StoreMatrix(m,kBroken+640,Rigid(-1.1f,0.35f,812.625f,-4.5f,-2210.25f,1.0625f));
  BuildTree(m,kBroken+400,bind);
  BuildModel(m,2,kBroken+384);
  m.StoreWord(kBroken+708,57); m.StoreWord(kBroken+712,40);
  for(const auto list:{kScene+kNativeRenderSceneObjects,kScene+kNativeRenderSceneUpdates}) { m.StoreWord(list,kSceneEnd); m.StoreWord(list+12,kSceneEnd); }
}
auto BrokenDecoder(const Memory& memory,size_t& decodes) {
  return [&memory,&decodes](uint32_t instance,uint32_t vector,uint32_t bones) {
    ++decodes;
    return DecodeNativeModelLayoutWith(memory,instance,vector,[](uint32_t,NativeModelBuffers::Kind)->uint64_t { return 1; },bones);
  };
}
// The registry poses the object natively and captures its layout from the
// tree's bone count; the pose is the guest's 8211FAA8 pose bit for bit.
void BrokenObjectPoseMatchesGuest(bool bind) {
  std::vector<uint8_t> bytes(0x10000); const Memory memory{bytes};
  BuildBrokenObject(memory,bind);
  const auto* type=FindNativeRenderClass(kBrokenVtable);
  Require(type && type->cadence==NativeRenderPoseCadence::Frame && type->frame_root==640 && type->instance+16==400 && type->pose==428,
    "clBrokenObject row names 8211FAA8's root, tree and pose vector");
  NativeRenderRegistry registry(0);
  size_t decodes=0;
  const auto decode=BrokenDecoder(memory,decodes);
  registry.Born(kBroken);
  auto snapshot=registry.Tick(memory,kScene,1,decode);
  const auto* found=snapshot->objects.Find(kBroken);
  Require(found && (*found)->pose && (*found)->pose->size()==4,"the unsized guest vector still publishes a four-bone pose");
  const auto entry=*found;
  Require(entry->models.size()==1 && entry->models[0].layout && entry->models[0].layout->bones==4 && decodes==1 &&
    registry.stats().retrying==0 && registry.stats().frame_poses==1,"the layout is captured from the tree's bone count, no retry");
  Require(memory.Word(kBroken+432)==0 && memory.Word(kBroken+712)==40,"the registry writes no guest memory");
  // The guest, with the vector sized as 821C9400 would.
  memory.StoreWord(kBroken+428+4,kPose); memory.StoreWord(kBroken+428+8,kPose+4*64);
  const auto hierarchy=ReadNativeModelHierarchy(memory,kBroken+400);
  const auto pose=ComputeNativeModelHierarchyPose(hierarchy,ReadNativeGuestMatrix(memory,kBroken+640));
  Guest8211FAA8(memory,kBroken);
  Require(memory.Word(kBroken+712)==57,"8211FAA8 stores +708 into +712");
  for(size_t i=0;i<hierarchy.nodes.size();++i) Require(SameBits(pose.nodes[i],memory,hierarchy.nodes[i].address+240),"node world matches node+240");
  for(uint32_t i=0;i<4;++i) {
    Require(SameBits((*entry->pose)[i],memory,kPose+i*64),"registry pose entry matches the guest pose vector");
    Require(SameBits(pose.palette[i],memory,kPose+i*64),"computed pose entry matches the guest pose vector");
  }
  // Unchanged inputs keep the published pose and entry.
  snapshot=registry.Tick(memory,kScene,2,decode);
  Require(*snapshot->objects.Find(kBroken)==entry && registry.stats().frame_pose_reuses==1 && decodes==1,"unchanged root reuses the pose");
  // The root moves (debris settles): recomputed, still the guest's.
  StoreMatrix(memory,kBroken+640,Rigid(-1.05f,0.3f,812.5f,-6.75f,-2210.0f,1.0625f));
  snapshot=registry.Tick(memory,kScene,3,decode);
  const auto moved=*snapshot->objects.Find(kBroken);
  Require(moved!=entry && moved->pose!=entry->pose && moved->models[0].layout==entry->models[0].layout && decodes==1,
    "a moved root republishes the pose and keeps the layout");
  Guest8211FAA8(memory,kBroken);
  for(uint32_t i=0;i<4;++i) Require(SameBits((*moved->pose)[i],memory,kPose+i*64),"moved pose matches the guest");
  // A tree the walk rejects keeps the entry (its +712 store still happens) unposed and retrying.
  memory.StoreWord(kChildren+304+80,kRoots); memory.StoreWord(kChildren+304+88,1);
  memory.StoreWord(kBroken+420,3); // A header change rebuilds at once.
  snapshot=registry.Tick(memory,kScene,4,decode);
  const auto* rejected=snapshot->objects.Find(kBroken);
  Require(rejected && !(*rejected)->pose && !(*rejected)->models[0].layout && registry.stats().frame_pose_failures==1 &&
    registry.stats().retrying==1,"a rejected tree publishes the entry unposed and retries");
}
}
int main() {
  try {
    PoseMatchesGuest(false);
    PoseMatchesGuest(true);
    CacheReusesAndDetectsChange();
    RecordsSkyDraws();
    BrokenObjectPoseMatchesGuest(false);
    BrokenObjectPoseMatchesGuest(true);
  } catch(const std::exception& error) { std::cerr<<"native full-frame sky test failed: "<<error.what()<<"\n"; return 1; }
  std::cout<<"native full-frame sky tests passed\n";
  return 0;
}

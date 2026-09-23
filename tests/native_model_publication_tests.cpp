#include "native_graphics/native_model_publication.h"
#include "native_graphics/native_model_pass.h"
#include "native_graphics/native_queued_scene.h"
#include <bit>
#include <cstring>
#include <functional>
#include <iostream>
#include <utility>
#include <vector>

using namespace edf::native;
namespace {
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
template<class F> void Reject(F f,const char* message) {
  bool rejected=false; try { f(); } catch(const std::exception&) { rejected=true; } Require(rejected,message);
}
// Synthetic big-endian guest memory, as in the scene tests.
struct Memory {
  std::vector<uint8_t>& bytes;
  uint32_t Add(uint32_t address,uint32_t offset) const { return address+offset; }
  uint32_t Word(uint32_t at) const {
    uint32_t value=0; for(unsigned i=0;i<4;++i) value=(value<<8)|bytes.at(at+i); return value;
  }
  const uint8_t* Bytes(uint32_t at,size_t size) const {
    if(!at || at>bytes.size() || size>bytes.size()-at) throw std::runtime_error("model test range");
    return bytes.data()+at;
  }
  void StoreWord(uint32_t at,uint32_t value) const { for(unsigned i=0;i<4;++i) bytes.at(at+i)=uint8_t(value>>(24-i*8)); }
  void StoreByte(uint32_t at,uint8_t value) const { bytes.at(at)=value; }
  void StoreFloat(uint32_t at,float value) const { StoreWord(at,std::bit_cast<uint32_t>(value)); }
};
// Instance -> model node -> two mesh records -> three 148-byte batches.
constexpr uint32_t kInstance=0x500,kVector=0x600,kContainer=0x1000,kNode=0x2000,kRecords=0x3000,
  kBatches=0x4000,kSecondBatch=0x5000,kMaterial=0x6000,kPasses=0x7000,kDeclarationMap=0x8000,
  kDeclarationNode=0x8200,kDeclaration=0x9000,kPose=0xA000;
void BuildModel(const Memory& memory,uint32_t bones=2,bool skinned=true) {
  memory.StoreWord(kInstance,kContainer); memory.StoreWord(kInstance+4,kNode); memory.StoreByte(kInstance+12,skinned);
  memory.StoreWord(kContainer+4,0x1100); // End node; distinct from kNode.
  memory.StoreWord(kNode+44,kRecords); memory.StoreWord(kNode+52,2);
  memory.StoreWord(kRecords+4,kBatches); memory.StoreWord(kRecords+8,kBatches+2*148);
  memory.StoreWord(kRecords+44,1); memory.StoreByte(kRecords+48,0);
  memory.StoreWord(kRecords+52+4,kSecondBatch); memory.StoreWord(kRecords+52+8,kSecondBatch+148);
  memory.StoreWord(kRecords+52+44,7); memory.StoreByte(kRecords+52+48,1); // Palette-skinned: no bone upload.
  memory.StoreWord(kMaterial+16,kPasses); memory.StoreWord(kMaterial+24,2);
  memory.StoreWord(kDeclarationMap+4,0x8100); memory.StoreWord(kDeclarationNode+28,kDeclaration);
  for(const auto batch:{kBatches,kBatches+148,kSecondBatch}) {
    memory.StoreWord(batch,kMaterial); memory.StoreWord(batch+60,32);
    memory.StoreWord(batch+72,kDeclarationMap); memory.StoreWord(batch+76,kDeclarationNode);
    memory.StoreWord(batch+140,100);
  }
  memory.StoreWord(kVector+4,bones?kPose:0); memory.StoreWord(kVector+8,bones?kPose+64*bones:0);
  for(uint32_t i=0;i<bones*16;++i) memory.StoreFloat(kPose+i*4,float(i)+0.5f);
}
NativeModelLayout Decode(const Memory& memory,const NativeModelBuffers* buffers=nullptr) {
  return DecodeNativeModelLayout(memory,kInstance,kVector,buffers);
}
void DecodesGuestLayout() {
  std::vector<uint8_t> bytes(0x10000);
  const Memory memory{bytes};
  BuildModel(memory);
  NativeModelBuffers buffers;
  buffers.Publish(kBatches+4,NativeModelBuffers::Kind::Vertex,0xB000,32,4);
  buffers.Publish(kSecondBatch+84,NativeModelBuffers::Kind::Index,0xC000,2,100);
  const auto layout=Decode(memory,&buffers);
  Require(layout.container==kContainer && layout.node==kNode && layout.skinned && layout.bones==2 &&
    layout.meshes.size()==2 && layout.Batches()==3,"model layout shape");
  const auto& rigid=layout.meshes[0],&palette=layout.meshes[1];
  Require(rigid.address==kRecords && rigid.bone==1 && rigid.uploads_bone && !rigid.skinned,"bone-uploading record");
  Require(palette.address==kRecords+52 && palette.skinned && !palette.uploads_bone,"palette record must not name a bone");
  const auto& batch=rigid.batches[1];
  Require(batch.address==kBatches+148 && batch.material==kMaterial && batch.declaration==kDeclaration &&
    batch.stride==32 && batch.index_count==100 && batch.draw_count==99,"batch fields");
  Require(batch.passes==std::vector<uint32_t>{kPasses,kPasses+112},"material pass record addresses");
  Require(rigid.batches[0].vertex.owner==kBatches+4 && rigid.batches[0].vertex.generation==buffers.Find(kBatches+4,NativeModelBuffers::Kind::Vertex)->generation &&
    rigid.batches[0].index==NativeModelBufferIdentity{kBatches+84,0},"vertex identity through NativeModelBuffers");
  Require(palette.batches[0].index.generation==buffers.Find(kSecondBatch+84,NativeModelBuffers::Kind::Index)->generation &&
    palette.batches[0].vertex==NativeModelBufferIdentity{kSecondBatch+4,0},"index identity through NativeModelBuffers");
  Require(Decode(memory,&buffers)==layout && !(Decode(memory)==layout),"layout equality covers buffer identities");
  // A rigid instance uploads every record's bone, so bone 7 is out of range.
  memory.StoreByte(kInstance+12,0);
  Reject([&] { Decode(memory); },"rigid record bone outside the pose accepted");
  memory.StoreWord(kRecords+52+44,0);
  const auto rigid_layout=Decode(memory);
  Require(!rigid_layout.skinned && rigid_layout.meshes[1].uploads_bone,"rigid instance uploads every record");
}
void RejectsUnexpectedMemory() {
  const std::vector<std::pair<const char*,std::function<void(const Memory&)>>> cases{
    {"container end node",[](const Memory& m) { m.StoreWord(kContainer+4,kNode); }},
    {"null container",[](const Memory& m) { m.StoreWord(kInstance,0); }},
    {"mesh count",[](const Memory& m) { m.StoreWord(kNode+52,kNativeModelMaxMeshes+1); }},
    {"mesh table address",[](const Memory& m) { m.StoreWord(kNode+44,0); }},
    {"batch extent",[](const Memory& m) { m.StoreWord(kRecords+8,kBatches+150); }},
    {"batch end without storage",[](const Memory& m) { m.StoreWord(kRecords+4,0); }},
    {"bone outside pose",[](const Memory& m) { m.StoreWord(kRecords+44,2); }},
    {"empty skinned pose",[](const Memory& m) { m.StoreWord(kVector+4,0); m.StoreWord(kVector+8,0); }},
    {"pose extent",[](const Memory& m) { m.StoreWord(kVector+8,kPose+100); }},
    {"negative pass count",[](const Memory& m) { m.StoreWord(kMaterial+24,0xffffffffu); }},
    {"passes without records",[](const Memory& m) { m.StoreWord(kMaterial+16,0); }},
    {"missing material",[](const Memory& m) { m.StoreWord(kSecondBatch,0); }},
    {"stride",[](const Memory& m) { m.StoreWord(kBatches+60,6); }},
    {"declaration end node",[](const Memory& m) { m.StoreWord(kDeclarationMap+4,kDeclarationNode); }},
    {"missing declaration",[](const Memory& m) { m.StoreWord(kDeclarationNode+28,0); }},
    {"index count",[](const Memory& m) { m.StoreWord(kBatches+140,kNativeModelMaxIndices+1); }},
    {"unmapped pass table",[](const Memory& m) { m.StoreWord(kMaterial+16,0xfff0); }},
  };
  for(const auto& [name,change]:cases) {
    std::vector<uint8_t> bytes(0x10000);
    const Memory memory{bytes};
    BuildModel(memory); Decode(memory);
    change(memory);
    bool rejected=false;
    try { Decode(memory); } catch(const std::exception&) { rejected=true; }
    if(!rejected) { std::cerr<<"accepted: "<<name<<"\n"; throw std::runtime_error("unexpected model memory accepted"); }
  }
}
void RegistryGenerationsAndRetirement() {
  std::vector<uint8_t> bytes(0x10000);
  const Memory memory{bytes};
  BuildModel(memory);
  NativeModelPublications models;
  Require(!models.Find(kInstance) && !models.Current(kInstance,kContainer,kNode,kVector),"empty registry");
  const auto first=models.Register(Decode(memory),kPose);
  Require(first.generation==1 && models.size()==1 && models.Find(kInstance).layout==first.layout &&
    models.Current(kInstance,kContainer,kNode,kVector) && !models.Current(kInstance,kContainer,kNode,kVector+16),
    "first-sight registration");
  Require(!models.RetireAddress(0x4321) && models.size()==1,"unrelated free retired a layout");
  // Same instance, new model: a new generation replaces the old one.
  const auto second=models.Register(Decode(memory),kPose);
  Require(second.generation==2 && models.size()==1 && models.Find(kInstance).generation==2 && first.layout->node==kNode,
    "re-registration must bump the generation and keep the old layout immutable");
  // Pose storage free (821C8F10 growth or destruction through 820B2510).
  Require(models.RetireAddress(kPose)==1 && !models.Find(kInstance) && models.size()==0 && models.retirements()==1,
    "pose storage free did not retire the layout");
  models.Register(Decode(memory),kPose);
  Require(models.RetireAddress(kNode)==1 && !models.Find(kInstance),"model node free did not retire the layout");
  models.Register(Decode(memory),kPose);
  Require(models.Retire(kInstance) && !models.Retire(kInstance) && !models.RetireAddress(kPose),"retirement keys survived");
  models.Reject(kInstance,kNode,kVector);
  Require(models.Rejected(kInstance,kNode,kVector) && !models.Rejected(kInstance,kNode+4,kVector),"rejection identity");
  models.Register(Decode(memory),kPose);
  Require(!models.Rejected(kInstance,kNode,kVector),"registration kept a stale rejection");
}
void PosePublicationIsImmutableAndDirtyOnly() {
  std::vector<uint8_t> bytes(0x20000);
  const Memory memory{bytes};
  BuildModel(memory);
  // A second rigid instance on its own pose vector and model.
  constexpr uint32_t other=0x700,other_vector=0x780,other_pose=0xE000;
  memory.StoreWord(other,kContainer); memory.StoreWord(other+4,kNode); memory.StoreByte(other+12,1);
  memory.StoreWord(other_vector+4,other_pose); memory.StoreWord(other_vector+8,other_pose+128);
  memory.StoreFloat(other_pose,42.f);
  NativeModelPublications models;
  const auto layout=models.Register(Decode(memory),kPose);
  const auto other_layout=models.Register(DecodeNativeModelLayout(memory,other,other_vector,nullptr),other_pose);
  // First sight seeds both poses without a dirty walk.
  const auto first=models.PublishPoses(memory,10,{});
  Require(first->generation==1 && first->tick==10 && first->poses.size()==2,"first-sight poses were not seeded");
  const auto* pose=first->Find(kInstance);
  Require(pose && pose->layout_generation==layout.generation && pose->matrices.size()==2 &&
    pose->matrices[1][3]==19.5f,"seeded pose contents");
  const auto live=ReadNativeModelPose(memory,kVector);
  Require(SameNativeModelPose(pose->matrices,live),"published pose differs from live memory");
  // Only kVector was rebuilt this tick: the other pose must stay shared.
  memory.StoreFloat(kPose,-1.f); memory.StoreFloat(other_pose,-2.f);
  const std::vector<uint32_t> dirty{kVector,kVector,0x9990};
  const auto second=models.PublishPoses(memory,11,dirty);
  Require(second->generation==2 && second->Find(kInstance)->matrices[0][0]==-1.f &&
    second->Find(kInstance)->pose_generation==2,"dirty pose was not snapshotted");
  Require(second->Find(other)==first->Find(other) && second->Find(other)->matrices[0][0]==42.f,
    "clean pose was re-read instead of shared");
  Require(first->Find(kInstance)->matrices[0][0]==0.5f,"an older publication changed");
  // Audit: layout and pose agree, then a live change is reported.
  const auto lookup=[](uint32_t,NativeModelBuffers::Kind) { return uint64_t(0); };
  auto audit=AuditNativeModelPublication(memory,layout,second.get(),kVector,lookup);
  Require(audit.decoded && !audit.layout_mismatch && audit.published && !audit.pose_mismatch,"clean audit");
  memory.StoreFloat(kPose+4,99.f);
  audit=AuditNativeModelPublication(memory,layout,second.get(),kVector,lookup);
  Require(audit.published && audit.pose_mismatch && !audit.layout_mismatch,"pose audit missed a live change");
  memory.StoreWord(kBatches+140,90);
  audit=AuditNativeModelPublication(memory,layout,second.get(),kVector,lookup);
  Require(audit.layout_mismatch && audit.decoded,"layout audit missed an index count change");
  memory.StoreWord(kContainer+4,kNode);
  audit=AuditNativeModelPublication(memory,layout,second.get(),kVector,lookup);
  Require(audit.layout_mismatch && !audit.decoded,"layout audit hid a decode failure");
  memory.StoreWord(kContainer+4,0x1100); memory.StoreWord(kBatches+140,100);
  audit=AuditNativeModelPublication(memory,other_layout,first.get(),kVector,lookup);
  Require(audit.layout_mismatch,"audit ignored a different pose vector");
  // Skeleton size change retires; retirement leaves older publications intact.
  memory.StoreWord(other_vector+8,other_pose+192);
  const auto third=models.PublishPoses(memory,12,std::vector<uint32_t>{other_vector});
  Require(!third->Find(other) && !models.Find(other) && models.pose_failures()==1 && second->Find(other),
    "skeleton size change kept a stale layout");
  Require(models.RetireAddress(kPose)==1,"pose storage free");
  const auto fourth=models.PublishPoses(memory,13,std::vector<uint32_t>{kVector});
  Require(fourth->poses.empty() && third->Find(kInstance) && models.AcquirePoses()==fourth,"retired pose still published");
  // Storage moved by vector growth: the new storage becomes the retirement key.
  models.Register(Decode(memory),kPose);
  memory.StoreWord(kVector+4,0xF000); memory.StoreWord(kVector+8,0xF000+128);
  models.PublishPoses(memory,14,std::vector<uint32_t>{kVector});
  Require(!models.RetireAddress(kPose) && models.RetireAddress(0xF000)==1,"moved pose storage kept the old key");
  // Unreadable dirty storage drops only that pose.
  models.Register(Decode(memory),0xF000);
  models.PublishPoses(memory,15,{});
  memory.StoreWord(kVector+8,0xF000+100);
  const auto failed=models.PublishPoses(memory,16,std::vector<uint32_t>{kVector});
  Require(!failed->Find(kInstance) && models.Find(kInstance),"unreadable pose stayed published");
  models.Clear();
  Require(!models.size() && !models.AcquirePoses() && !models.RetireAddress(0xF000),"clear");
}
// A rigid model whose three batches all have published buffers.
void RigidBuffers(const Memory& memory,NativeModelBuffers& buffers) {
  BuildModel(memory,2,false);
  memory.StoreWord(kRecords+52+44,0); // Rigid instances upload every record's bone.
  uint32_t address=0x10000;
  for(const auto batch:{kBatches,kBatches+148,kSecondBatch}) {
    buffers.Publish(batch+4,NativeModelBuffers::Kind::Vertex,address,32,4); address+=0x1000;
    buffers.Publish(batch+84,NativeModelBuffers::Kind::Index,address,2,100); address+=0x1000;
  }
}
void ModelPassGateDeclines() {
  using D=NativeModelPassDecline;
  std::vector<uint8_t> bytes(0x20000);
  const Memory memory{bytes};
  NativeModelBuffers buffers;
  RigidBuffers(memory,buffers);
  NativeModelPublications models;
  const auto layout=models.Register(Decode(memory,&buffers),kPose);
  const auto poses=models.PublishPoses(memory,10,{});
  const auto accepted=GateNativeModelPass(layout,true,0,poses.get(),10,false);
  Require(accepted && accepted.pose==poses->Find(kInstance) && accepted.pose->matrices.size()==2,"rigid published model declined");
  const auto reason=[](const NativeModelPoseGate& gate) { return gate.decline.value_or(D::Count); };
  Require(reason(GateNativeModelPass({},true,0,poses.get(),10,false))==D::Unpublished,"unpublished layout accepted");
  Require(reason(GateNativeModelPass(layout,false,0,poses.get(),10,false))==D::Stale,"stale identity accepted");
  Require(reason(GateNativeModelPass(layout,true,1,poses.get(),10,false))==D::Skinned,"live instance+12 byte ignored");
  Require(reason(GateNativeModelPass(layout,true,0,poses.get(),11,false))==D::PoseTick,"pose from another tick accepted");
  Require(reason(GateNativeModelPass(layout,true,0,nullptr,10,false))==D::PoseTick,"missing pose publication accepted");
  Require(reason(GateNativeModelPass(layout,true,0,poses.get(),10,true))==D::RenderDependent,"render-dependent pose accepted");
  // A bone outside the published pose (a hand-made layout: decoding rejects it).
  auto moved=*layout.layout; moved.meshes[1].bone=5;
  const NativeModelPublications::Layout bone_layout{layout.generation,std::make_shared<const NativeModelLayout>(moved)};
  Require(reason(GateNativeModelPass(bone_layout,true,0,poses.get(),10,false))==D::Bone,"bone outside the pose accepted");
  // Re-registration: the published pose belongs to the older generation.
  const auto newer=models.Register(Decode(memory,&buffers),kPose);
  Require(reason(GateNativeModelPass(newer,true,0,poses.get(),10,false))==D::Pose,"pose of an older layout generation accepted");
  const auto seeded=models.PublishPoses(memory,11,{});
  Require(reason(GateNativeModelPass(newer,true,0,seeded.get(),11,false))==D::Count,"seeded pose of the new generation declined");
  // The render-dependence latch follows the layout generation.
  models.MarkRenderDependent(kInstance,layout.generation);
  Require(!models.RenderDependent(kInstance,newer.generation),"latch applied to a stale generation");
  models.MarkRenderDependent(kInstance,newer.generation);
  Require(models.RenderDependent(kInstance,newer.generation),"render-dependence latch lost");
  const auto relatched=models.Register(Decode(memory,&buffers),kPose);
  Require(!models.RenderDependent(kInstance,relatched.generation) && !models.RenderDependent(kInstance,newer.generation),
    "a new generation inherited the latch");
  // Unpublished vertex/index buffers.
  const auto bare=models.Register(Decode(memory),kPose);
  const auto bare_poses=models.PublishPoses(memory,12,{});
  Require(reason(GateNativeModelPass(bare,true,0,bare_poses.get(),12,false))==D::Buffers,"unpublished buffers accepted");
  // A skinned layout declines even when the live byte reads rigid.
  BuildModel(memory,2,true);
  const auto skinned=models.Register(Decode(memory,&buffers),kPose);
  const auto skinned_poses=models.PublishPoses(memory,13,{});
  Require(reason(GateNativeModelPass(skinned,true,0,skinned_poses.get(),13,false))==D::Skinned,"skinned layout accepted");
}
// The palette-skinned fixture (record 0 uploads bone 1, record 1 has rec+48 set)
// over an eight-bone pose, with every batch's buffers published.
void SkinnedBuffers(const Memory& memory,NativeModelBuffers& buffers) {
  BuildModel(memory,8,true);
  uint32_t address=0x10000;
  for(const auto batch:{kBatches,kBatches+148,kSecondBatch}) {
    buffers.Publish(batch+4,NativeModelBuffers::Kind::Vertex,address,32,4); address+=0x1000;
    buffers.Publish(batch+84,NativeModelBuffers::Kind::Index,address,2,100); address+=0x1000;
  }
}
void SkinnedModelPassGate() {
  using D=NativeModelPassDecline;
  const auto reason=[](const NativeModelPoseGate& gate) { return gate.decline.value_or(D::Count); };
  std::vector<uint8_t> bytes(0x20000);
  const Memory memory{bytes};
  NativeModelBuffers buffers;
  SkinnedBuffers(memory,buffers);
  NativeModelPublications models;
  const auto layout=models.Register(Decode(memory,&buffers),kPose);
  const auto poses=models.PublishPoses(memory,10,{});
  Require(layout.layout->skinned && layout.layout->bones==8,"skinned fixture");
  const auto accepted=GateNativeModelPass(layout,true,1,poses.get(),10,false,true);
  Require(accepted && accepted.pose==poses->Find(kInstance) && accepted.pose->matrices.size()==8,"enabled skinned model declined");
  Require(reason(GateNativeModelPass(layout,true,1,poses.get(),10,false))==D::Skinned,"skinned model accepted with the cvar off");
  Require(reason(GateNativeModelPass(layout,true,0,poses.get(),10,false,true))==D::Skinned,"skinned layout accepted under a rigid live byte");
  Require(reason(GateNativeModelPass(layout,true,1,poses.get(),10,true,true))==D::RenderDependent,"render-dependent skinned pose accepted");
  Require(reason(GateNativeModelPass(layout,true,1,poses.get(),11,false,true))==D::PoseTick,"skinned pose from another tick accepted");
  models.MarkRenderDependent(kInstance,layout.generation);
  Require(models.RenderDependent(kInstance,layout.generation),"skinned render-dependence latch");
  // A palette record names no bone; an uploading record's bone must be in range.
  auto palette_bone=*layout.layout; palette_bone.meshes[1].bone=99;
  Require(reason(GateNativeModelPass({layout.generation,std::make_shared<const NativeModelLayout>(palette_bone)},true,1,poses.get(),10,false,true))==D::Count,
    "palette record's bone field was range-checked");
  auto upload_bone=*layout.layout; upload_bone.meshes[0].bone=8;
  Require(reason(GateNativeModelPass({layout.generation,std::make_shared<const NativeModelLayout>(upload_bone)},true,1,poses.get(),10,false,true))==D::Bone,
    "uploading record's bone outside the pose accepted");
  // A rigid layout never takes a non-uploading record, nor the palette path.
  auto rigid=*layout.layout; rigid.skinned=false;
  const NativeModelPublications::Layout rigid_layout{layout.generation,std::make_shared<const NativeModelLayout>(rigid)};
  Require(reason(GateNativeModelPass(rigid_layout,true,0,poses.get(),10,false,true))==D::Bone,"rigid layout with a palette record accepted");
  Require(reason(GateNativeModelPass(rigid_layout,true,1,poses.get(),10,false,true))==D::Skinned,"rigid layout accepted under a palette live byte");
}
void PaletteFitsShaderArray() {
  std::vector<NativePoseMatrix> pose(70);
  for(size_t bone=0;bone<pose.size();++bone) for(size_t i=0;i<16;++i) pose[bone][i]=float(bone*16+i)+0.125f;
  pose[0][4]=-0.f;
  // The decoded Blend/SingleBlend layout: 68 float4x3, 204 registers.
  std::vector<uint8_t> scratch(204*16);
  for(size_t i=0;i<scratch.size();++i) scratch[i]=uint8_t(i*7+3);
  const auto full=NativeModelPaletteRegisters(pose,68,scratch);
  Require(full && full->size()==scratch.size(),"a 68-bone palette must fit a 204-register array");
  std::vector<uint8_t> guest(68*kNativeBonePaletteBytes);
  Require(PackGuestBonePalette(pose,guest,68)==68 && std::equal(guest.begin(),guest.end(),full->begin()),
    "palette registers differ from the 821A1738 scratch bytes");
  Require(GuestBlockWord(full->data()+4)==0x80000000u,"palette lost a signed zero");
  // An array smaller than the clamped palette declines; the limit clamps first.
  Require(!NativeModelPaletteRegisters(pose,68,std::span(scratch).first(203*16)),"palette larger than the shader array accepted");
  Require(!NativeModelPaletteRegisters(pose,69,scratch),"69 bones accepted by a 68-bone array");
  const auto clamped=NativeModelPaletteRegisters(pose,2,std::span(scratch).first(6*16));
  Require(clamped && clamped->size()==96 && std::equal(clamped->begin(),clamped->end(),guest.begin()),
    "limit-clamped palette in an exact array");
  // Bones past the pose or the limit keep the live scratch, as in the guest.
  const std::span<const NativePoseMatrix> three(pose.data(),3);
  const auto partial=NativeModelPaletteRegisters(three,68,scratch);
  Require(partial && std::equal(guest.begin(),guest.begin()+3*48,partial->begin()) &&
    std::equal(scratch.begin()+3*48,scratch.end(),partial->begin()+3*48),"palette tail did not keep the scratch");
  Require(!NativeModelPaletteRegisters(three,68,std::span(scratch).first(100)),"partial register scratch accepted");
  // Handoff words: stored big-endian they are the same scratch bytes.
  const auto words=NativeModelPaletteWords(pose,68);
  Require(words.size()==68,"handoff palette bone count");
  for(size_t bone=0;bone<words.size();++bone) for(size_t i=0;i<12;++i)
    Require(GuestBlockWord(guest.data()+bone*48+i*4)==words[bone][i],"handoff palette word differs from 821A1738");
  Require(NativeModelPaletteWords(pose,0).empty(),"zero limit packed bones");
}
void SkinnedWorldFollowsRecordFlag() {
  std::vector<uint8_t> bytes(0x20000);
  const Memory memory{bytes};
  NativeModelBuffers buffers;
  SkinnedBuffers(memory,buffers);
  const auto layout=Decode(memory,&buffers);
  const auto pose=ReadNativeModelPose(memory,kVector);
  std::array<uint8_t,64> entry{};
  for(size_t i=0;i<entry.size();++i) entry[i]=uint8_t(0xA0+i);
  // Record 0 (rec+48 clear) uploads bone 1; record 1 (rec+48 set) inherits it.
  auto worlds=NativeModelWorldPlan(layout,pose,entry);
  Require(worlds.size()==2 && worlds[0]==NativeModelWorldRegisters(pose[1]) && worlds[1]==worlds[0],
    "palette record did not inherit the previous upload");
  Require(NativeModelLastWorldUpload(layout)==0u,"last uploading record");
  // Palette record first: it draws with the storage's entry bytes.
  auto swapped=layout; std::swap(swapped.meshes[0],swapped.meshes[1]);
  worlds=NativeModelWorldPlan(swapped,pose,entry);
  Require(worlds[0]==entry && worlds[1]==NativeModelWorldRegisters(pose[1]),"palette record before any upload");
  Require(NativeModelLastWorldUpload(swapped)==1u,"last uploading record after the swap");
  // No uploading record: the storage is left alone.
  auto none=layout; none.meshes[0].uploads_bone=false; none.meshes[0].skinned=true;
  worlds=NativeModelWorldPlan(none,pose,entry);
  Require(worlds[0]==entry && worlds[1]==entry && !NativeModelLastWorldUpload(none),"no upload changed the world");
  // Rigid: every record uploads its own bone.
  memory.StoreByte(kInstance+12,0); memory.StoreWord(kRecords+52+44,5);
  const auto rigid=Decode(memory,&buffers);
  worlds=NativeModelWorldPlan(rigid,pose,entry);
  Require(worlds[0]==NativeModelWorldRegisters(pose[1]) && worlds[1]==NativeModelWorldRegisters(pose[5]) &&
    NativeModelLastWorldUpload(rigid)==1u,"rigid records upload their own bones");
  auto outside=layout; outside.meshes[0].bone=8;
  Reject([&] { NativeModelWorldPlan(outside,pose,entry); },"world plan accepted a bone outside the pose");
}
void ModelPassDrawPlanFollowsGuestOrder() {
  std::vector<uint8_t> bytes(0x20000);
  const Memory memory{bytes};
  NativeModelBuffers buffers;
  RigidBuffers(memory,buffers);
  const auto plan=NativeModelDrawPlan(Decode(memory,&buffers));
  // Record 0: two batches of two passes; record 1: one batch of two passes.
  const std::vector<std::array<uint32_t,3>> expected{{0,0,kPasses},{0,0,kPasses+112},{0,1,kPasses},{0,1,kPasses+112},
    {1,0,kPasses},{1,0,kPasses+112}};
  Require(plan.size()==expected.size(),"draw plan size");
  for(size_t i=0;i<plan.size();++i)
    Require(plan[i].mesh==expected[i][0] && plan[i].batch==expected[i][1] && plan[i].pass==expected[i][2],"draw plan order");
}
void ModelWorldMatchesGuestUpload() {
  NativePoseMatrix pose;
  for(size_t i=0;i<16;++i) pose[i]=float(i)+0.25f;
  pose[5]=-0.f;
  // 821C8000's stores, in destination order: the source byte offset of each word.
  constexpr std::array<uint32_t,16> source{0,16,32,48,4,20,36,52,8,24,40,56,12,28,44,60};
  const auto words=NativeModelWorldWords(pose);
  for(size_t k=0;k<16;++k) Require(words[k]==std::bit_cast<uint32_t>(pose[source[k]/4]),"g_mWorld word differs from 821C8000");
  Require(words[3]==std::bit_cast<uint32_t>(pose[12]) && words[7]==std::bit_cast<uint32_t>(pose[13]),
    "pose translation row must land in the fourth register column");
  Require(words[5]==0x80000000u,"signed zero must survive the upload");
  const auto registers=NativeModelWorldRegisters(pose);
  for(size_t k=0;k<16;++k) Require(GuestBlockWord(registers.data()+k*4)==words[k],"registers are not big-endian upload words");
  const auto decoded=DecodeNativeQueuedWorld(registers,true);
  Require(std::bit_cast<std::array<uint32_t,16>>(decoded)==std::bit_cast<std::array<uint32_t,16>>(pose),
    "column-major world decode does not recover the row-major pose");
  const auto transposed=DecodeNativeQueuedWorld(registers,false);
  Require(transposed[1]==pose[4] && transposed[12]==pose[3],"row-major decode of the upload must transpose");
}
// Frees the given address through the registry at the first byte-range read,
// as a guest free (820B2510) on another thread would while a decode or a pose
// read is in progress. Reads never hold the registry lock, so this re-enters.
struct FreeingReader {
  const Memory& memory;
  NativeModelPublications& models;
  mutable uint32_t free=0;
  mutable size_t retired=0;
  uint32_t Word(uint32_t at) const { return memory.Word(at); }
  const uint8_t* Bytes(uint32_t at,size_t size) const {
    if(const auto address=std::exchange(free,0)) retired+=models.RetireAddress(address);
    return memory.Bytes(at,size);
  }
};
// The registry's unchanged-pose test: guest bytes against a decoded pose,
// bitwise (signed zero and NaN payloads differ), without decoding.
void PoseBytesCompareBitwise() {
  std::vector<uint8_t> bytes(0x1000); const Memory memory{bytes};
  constexpr uint32_t kStorage=0x100;
  for(uint32_t i=0;i<32;++i) memory.StoreFloat(kStorage+i*4,float(i)*0.5f);
  const auto decoded=ReadNativeModelPose(memory,[&] {
    memory.StoreWord(0x80+4,kStorage); memory.StoreWord(0x80+8,kStorage+128); return 0x80u; }());
  const auto* raw=memory.Bytes(kStorage,128);
  Require(decoded.size()==2 && SameNativeModelPoseBytes(raw,2,decoded),"equal pose bytes compare different");
  Require(!SameNativeModelPoseBytes(raw,1,decoded) && SameNativeModelPoseBytes(nullptr,0,{}),"pose sizes not compared");
  memory.StoreFloat(kStorage+4,-0.0f);
  auto zero=decoded; zero[0][1]=0.0f;
  Require(!SameNativeModelPoseBytes(raw,2,zero),"signed zero compared equal");
  memory.StoreWord(kStorage+124,0x7FC00001u);
  auto nan=decoded; nan[0][1]=-0.0f; nan[1][15]=std::bit_cast<float>(0x7FC00001u);
  Require(SameNativeModelPoseBytes(raw,2,nan),"a NaN payload copied bitwise compared different");
  nan[1][15]=std::bit_cast<float>(0x7FC00002u);
  Require(!SameNativeModelPoseBytes(raw,2,nan),"a different NaN payload compared equal");
}
void CaptureRacesWithFree() {
  std::vector<uint8_t> bytes(0x20000);
  const Memory memory{bytes};
  BuildModel(memory);
  NativeModelPublications models;
  const auto capture=[&](uint32_t freed,uint32_t storage=kPose) {
    NativeModelPublications::CaptureScope scope(models);
    const FreeingReader reader{memory,models,freed};
    auto layout=DecodeNativeModelLayout(reader,kInstance,kVector,nullptr);
    return models.Register(std::move(layout),storage,&scope);
  };
  // The very first capture: nothing is registered, yet a node free during the
  // decode must still drop it.
  Require(!models.size() && !capture(kNode) && !models.Find(kInstance) && models.stale_captures()==1,
    "first capture registered a layout whose node was freed during decode");
  Require(!capture(kInstance) && !capture(kPose) && models.stale_captures()==3 && !models.size(),
    "capture registered a layout whose instance or pose storage was freed during decode");
  // An unrelated free does not drop it; a free before the scope opened does not either.
  Require(capture(0x4321) && models.size()==1,"unrelated free dropped a capture");
  Require(models.RetireAddress(kNode)==1 && !models.size(),"registered node free");
  Require(capture(0) && models.stale_captures()==3,"free before the capture began dropped it");
  // A stale capture replaces nothing: the registered generation and its
  // render-dependence latch (the model pass gate) survive.
  const auto registered=models.Find(kInstance);
  models.MarkRenderDependent(kInstance,registered.generation);
  {
    NativeModelPublications::CaptureScope scope(models);
    auto layout=Decode(memory);
    models.RetireAddress(0xE000); // Unkeyed, but recorded while a capture is open.
    Require(!models.Register(std::move(layout),0xE000,&scope) && models.Find(kInstance).generation==registered.generation,
      "stale capture replaced the registered layout");
  }
  Require(models.RenderDependent(kInstance,registered.generation),"stale capture cleared the render-dependence latch");
  // Overlapping scopes: a free between the two openings drops only the older one.
  {
    NativeModelPublications::CaptureScope older(models);
    Require(models.RetireAddress(kNode)==1,"node free with a capture open");
    NativeModelPublications::CaptureScope newer(models);
    Require(!models.Register(Decode(memory),kPose,&older) && models.Register(Decode(memory),kPose,&newer),
      "free epochs across overlapping captures");
  }
  // Scopes closed: frees are no longer recorded, and keyed addresses still retire.
  Require(capture(0) && !models.RetireAddress(0x4321) && models.RetireAddress(kPose)==1 && !models.size(),
    "retirement after the captures closed");
  // Pose reads happen outside the lock: freeing the registered storage while
  // it is read retires the layout, and the commit publishes nothing for it.
  models.Register(Decode(memory),kPose);
  FreeingReader reader{memory,models,kPose};
  const auto raced=models.PublishPoses(reader,20,{});
  Require(reader.retired==1 && !raced->Find(kInstance) && !models.Find(kInstance),"pose committed after its storage was freed");
  // Storage moved and the new storage freed mid-read: not keyed yet, so only
  // the free epoch catches it; the pose is dropped and reseeded.
  models.Register(Decode(memory),kPose);
  models.PublishPoses(memory,21,{});
  memory.StoreWord(kVector+4,0xF000); memory.StoreWord(kVector+8,0xF000+128);
  reader.free=0xF000; reader.retired=0;
  const auto moved=models.PublishPoses(reader,22,std::vector<uint32_t>{kVector});
  Require(!reader.retired && !moved->Find(kInstance) && models.Find(kInstance) && !models.RetireAddress(0xF000),
    "pose read from freed storage was committed or keyed");
  // The unlocked read keeps the latch and publishes under the live generation.
  const auto current=models.Find(kInstance);
  models.MarkRenderDependent(kInstance,current.generation);
  const auto reseeded=models.PublishPoses(memory,23,{});
  Require(reseeded->Find(kInstance) && reseeded->Find(kInstance)->layout_generation==current.generation &&
    models.RenderDependent(kInstance,current.generation),"pose commit lost the generation or the latch");
  Require(models.RetireAddress(0xF000)==1 && !models.RenderDependent(kInstance,current.generation),
    "freed pose read was not reseeded, or retirement kept the latch");
}
}
int main() {
  try {
    DecodesGuestLayout();
    RejectsUnexpectedMemory();
    RegistryGenerationsAndRetirement();
    PosePublicationIsImmutableAndDirtyOnly();
    ModelPassGateDeclines();
    SkinnedModelPassGate();
    PaletteFitsShaderArray();
    SkinnedWorldFollowsRecordFlag();
    ModelPassDrawPlanFollowsGuestOrder();
    ModelWorldMatchesGuestUpload();
    CaptureRacesWithFree();
    PoseBytesCompareBitwise();
  } catch(const std::exception& error) {
    std::cerr<<"native model publication test failed: "<<error.what()<<"\n";
    return 1;
  }
  std::cout<<"native model publication tests passed\n";
  return 0;
}

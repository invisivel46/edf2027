#include "native_graphics/native_full_frame_models.h"
#include "native_graphics/d3d11_backend.h"
#include "native_graphics/d3d12_backend.h"
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <cmath>
#include <functional>
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

using namespace edf::native;
namespace {
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
using C=NativeFullFrameModelCull;
constexpr float kSide=0.70710678f;
// Identity view, d=z (depth scale -1), near 1, far 100000, 90-degree side
// planes x-z / -x-z / y-z / -y-z scaled by 1/sqrt(2).
NativeFullFrameModelCamera MakeCamera() {
  NativeFullFrameModelCamera camera;
  auto& view=camera.visibility;
  view.matrix={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  view.depth_scale=-1;
  auto& f=view.frustum;
  f[8]=kSide; f[10]=-kSide; f[12]=-kSide; f[14]=-kSide;
  f[17]=kSide; f[18]=-kSide; f[21]=-kSide; f[22]=-kSide;
  f[24]=1; f[25]=100000;
  camera.key_scale=1; camera.key_offset=0;
  return camera;
}
NativeModelBatchLayout Batch(uint32_t address,std::vector<uint32_t> passes) {
  NativeModelBatchLayout batch; batch.address=address; batch.passes=std::move(passes); return batch;
}
std::shared_ptr<const NativeModelLayout> Layout(uint32_t node,bool skinned,uint32_t bones,
    std::vector<NativeModelMeshLayout> meshes) {
  auto layout=std::make_shared<NativeModelLayout>();
  layout->instance=node+0x10000; layout->node=node; layout->skinned=skinned; layout->bones=bones;
  layout->meshes=std::move(meshes);
  return layout;
}
NativeModelMeshLayout Mesh(uint32_t bone,bool skinned,bool uploads,std::vector<NativeModelBatchLayout> batches) {
  NativeModelMeshLayout mesh; mesh.bone=bone; mesh.skinned=skinned; mesh.uploads_bone=uploads; mesh.batches=std::move(batches);
  return mesh;
}
std::shared_ptr<const std::vector<NativePoseMatrix>> Pose(size_t bones) {
  auto pose=std::make_shared<std::vector<NativePoseMatrix>>(bones);
  for(size_t b=0;b<bones;++b) for(size_t i=0;i<16;++i) (*pose)[b][i]=float(b*100+i);
  return pose;
}
const NativeRenderClass kPlain{0x82000000,"plain",NativeRenderPoseCadence::Tick,NativeRenderLodKind::None};
const NativeRenderClass kCharacter{0x82000100,"character",NativeRenderPoseCadence::Tick,NativeRenderLodKind::Character};
const NativeRenderClass kFieldParts{0x82000200,"fieldparts",NativeRenderPoseCadence::Tick,NativeRenderLodKind::FieldParts};
const NativeRenderClass kBroken{NativeBrokenObject::vtable,"clBrokenObject",NativeRenderPoseCadence::Frame,NativeRenderLodKind::None,384,428};
// A one-bone rigid model with one batch and one pass: node, batch and pass
// addresses identify the model resource and material.
std::shared_ptr<NativeRenderEntry> Entry(uint32_t object,std::array<float,3> centre,float radius=1,
    std::shared_ptr<const NativeModelLayout> layout=nullptr) {
  auto entry=std::make_shared<NativeRenderEntry>();
  entry->object=object; entry->type=&kPlain;
  entry->centre={centre[0],centre[1],centre[2],1};
  entry->radius=radius; entry->cull_distance=10000;
  if(!layout) layout=Layout(0x1000,false,1,{Mesh(0,false,true,{Batch(0x2000,{0x3000})})});
  entry->models.push_back({layout->instance,layout});
  entry->pose=Pose(layout->bones);
  return entry;
}
void Visibility() {
  const auto camera=MakeCamera();
  const auto& view=camera.visibility;
  {
    auto entry=Entry(1,{0,0,100});
    const auto result=ClassifyNativeFullFrameModel(*entry,view);
    Require(result && result.sphere==1 && result.depth==100 && result.centre[2]==100,"inside entry is accepted");
    entry->hidden=true;
    Require(ClassifyNativeFullFrameModel(*entry,view).cull==C::Hidden,"hidden entry is rejected");
    entry->hidden=false; entry->mode=3;
    Require(ClassifyNativeFullFrameModel(*entry,view).cull==C::Mode,"unsupported sort mode is rejected");
  }
  {
    auto entry=Entry(2,{0,0,600}); entry->cull_distance=500;
    Require(ClassifyNativeFullFrameModel(*entry,view).cull==C::Distance,"entry past its cull distance is rejected");
    entry->cull_distance=600;
    Require(bool(ClassifyNativeFullFrameModel(*entry,view)),"cull distance compares strictly greater");
  }
  {
    auto entry=Entry(3,{500,0,100});
    Require(ClassifyNativeFullFrameModel(*entry,view).cull==C::Frustum,"entry outside a side plane is rejected by the sphere");
    auto behind=Entry(4,{0,0,-50});
    Require(ClassifyNativeFullFrameModel(*behind,view).cull==C::Frustum,"entry before the near plane is rejected");
  }
  {
    // Partial sphere (x-z=5 against radius 10) whose small box lies wholly
    // outside the right plane: the box test rejects it.
    auto entry=Entry(5,{105,0,100},10);
    entry->axes={0.1f,0,0,0, 0,0.1f,0,0, 0,0,0.1f,0};
    const auto result=ClassifyNativeFullFrameModel(*entry,view);
    Require(result.sphere==2 && result.box==0 && result.cull==C::Box,"partial sphere with an outside box is rejected");
    // The same sphere with a box reaching inside the frustum is drawn.
    entry->axes={10,0,0,0, 0,1,0,0, 0,0,1,0};
    const auto partial=ClassifyNativeFullFrameModel(*entry,view);
    Require(partial && partial.sphere==2 && partial.box==2,"partial sphere with a straddling box is accepted");
  }
  {
    // Plan counts each rejection.
    NativeRenderRegistrySnapshot snapshot;
    auto hidden=Entry(1,{0,0,100}); hidden->hidden=true;
    auto distant=Entry(2,{0,0,20000});
    auto out=Entry(3,{500,0,100});
    auto box=Entry(4,{105,0,100},10); box->axes={0.1f,0,0,0, 0,0.1f,0,0, 0,0,0.1f,0};
    auto posed=Entry(5,{0,0,100}); posed->pose=Pose(3);
    auto good=Entry(6,{0,0,100});
    for(auto& entry:{hidden,distant,out,box,posed,good}) snapshot.entries.push_back(entry);
    const auto plan=PlanNativeFullFrameModels(snapshot,camera);
    Require(plan.stats.entries==6 && plan.stats.hidden==1 && plan.stats.distance==1 && plan.stats.frustum==1 &&
      plan.stats.box==1 && plan.stats.no_pose==1 && plan.opaque.size()==1 && plan.opaque[0].entry==good.get(),
      "plan routes every rejection");
  }
}
void Lod() {
  const auto layout=Layout(0x1000,false,1,{Mesh(0,false,true,{})});
  NativeRenderEntry entry;
  for(int i=0;i<3;++i) entry.models.push_back({0x5000u+i,layout});
  entry.type=&kCharacter; entry.lod_thresholds={50,100};
  Require(SelectNativeFullFrameModelLod(entry,10)==0u,"character: no threshold passed selects the default");
  Require(SelectNativeFullFrameModelLod(entry,50)==0u,"character: thresholds compare strictly");
  Require(SelectNativeFullFrameModelLod(entry,60)==1u,"character: first threshold selects model 1");
  Require(SelectNativeFullFrameModelLod(entry,150)==2u,"character: second threshold selects model 2");
  entry.lod_thresholds={100,50};
  Require(SelectNativeFullFrameModelLod(entry,75)==2u,"character: the last passed threshold wins");
  entry.type=&kFieldParts; entry.lod_thresholds={1e9f,50,100};
  Require(SelectNativeFullFrameModelLod(entry,10)==0u,"field parts: below every threshold selects LOD 0");
  Require(SelectNativeFullFrameModelLod(entry,60)==1u,"field parts: LOD 1");
  Require(SelectNativeFullFrameModelLod(entry,150)==2u,"field parts: LOD 2");
  entry.lod_thresholds={0,50,100};
  Require(SelectNativeFullFrameModelLod(entry,10)==0u,"field parts: threshold 0 is never compared");
  entry.lod_thresholds={0,100,50};
  Require(SelectNativeFullFrameModelLod(entry,75)==2u,"field parts: the last passed threshold wins");
  entry.models[2].layout=nullptr;
  Require(!SelectNativeFullFrameModelLod(entry,75),"a LOD without a model draws nothing");
  entry.type=&kPlain;
  Require(SelectNativeFullFrameModelLod(entry,1e6f)==0u,"no LOD kind always selects the default");
}
void Constants() {
  const auto pose=Pose(3);
  {
    const auto layout=Layout(0x1000,false,3,{Mesh(2,false,true,{}),Mesh(0,false,true,{})});
    const auto values=NativeFullFrameModelConstantsFor(*layout,*pose,68);
    Require(!values.skinned && values.palette.empty() && values.bones==0,"rigid binds no palette");
    Require(values.worlds.size()==2 && values.worlds[0]==NativeModelWorldRegisters((*pose)[2]) &&
      values.worlds[1]==NativeModelWorldRegisters((*pose)[0]),"rigid binds each record's pose[rec+44]");
    const auto first=NativeRigidWorld((*pose)[2]);
    Require(std::bit_cast<float>(uint32_t(values.worlds[0][4])<<24|uint32_t(values.worlds[0][5])<<16|
      uint32_t(values.worlds[0][6])<<8|values.worlds[0][7])==first[1],"rigid world is the NativeRigidWorld transpose");
  }
  {
    // Skinned: record 0 is palette-skinned (rec+48 set), record 1 uploads bone 1.
    const auto layout=Layout(0x1000,true,3,{Mesh(0,true,false,{}),Mesh(1,false,true,{}),Mesh(0,true,false,{})});
    const auto values=NativeFullFrameModelConstantsFor(*layout,*pose,2);
    Require(values.skinned && values.bones==2 && values.palette.size()==24,"skinned palette is clamped to the runtime limit");
    std::vector<float> expected(24);
    PackNativeBonePalette(*pose,expected,2);
    Require(values.palette==expected,"skinned palette is PackNativeBonePalette");
    Require(values.worlds[0]==NativeModelWorldRegisters(kNativeSceneIdentity),"a skinned record before any upload sees identity");
    Require(values.worlds[1]==NativeModelWorldRegisters((*pose)[1]),"a rec+48==0 record uploads its bone");
    Require(values.worlds[2]==values.worlds[1],"a later skinned record sees the last uploaded bone");
    std::vector<NativeSceneMaterialInputs::Constant> constants{
      {false,"g_mWorldArray",std::vector<uint8_t>(68*48,0xcd),true},{false,"g_mWorld",std::vector<uint8_t>(64,0xab),true}};
    Require(BindNativeFullFrameModelPalette(constants,values.palette),"palette fits the shader array");
    Require(std::bit_cast<float>(uint32_t(constants[0].registers[0])<<24|uint32_t(constants[0].registers[1])<<16|
      uint32_t(constants[0].registers[2])<<8|constants[0].registers[3])==expected[0],"palette is stored big-endian");
    Require(constants[0].registers[96]==0 && constants[0].registers.back()==0,"the palette tail is zeroed");
    Require(constants[1].registers[0]==0xab,"g_mWorld is left to the capture");
    constants[0].registers.resize(32);
    Require(!BindNativeFullFrameModelPalette(constants,values.palette),"a palette larger than the shader array is refused");
  }
}
void SortKeys() {
  Require(NativeFullFrameModelKey(1,300,1,1,0)==300,"mode 1: fma(z,scale,offset)*bias");
  Require(NativeFullFrameModelKey(1,300,0.5f,2,10)==305,"mode 1 applies scale, offset and bias");
  Require(NativeFullFrameModelKey(1,-5,1,1,0)==0,"keys clamp at zero");
  Require(NativeFullFrameModelKey(1,70000,1,1,0)==65535,"keys clamp at 65535");
  Require(NativeFullFrameModelKey(2,12345,0.01f,1,0)==655,"mode 2: bias*65536, independent of depth");
  Require(NativeFullFrameModelKey(1,std::nanf(""),1,1,0)==0,"a NaN key converts to zero");
  const auto camera=MakeCamera();
  NativeRenderRegistrySnapshot snapshot;
  const auto transparent=[&](uint32_t object,float z,int32_t mode,float bias) {
    auto entry=Entry(object,{0,0,z}); entry->mode=mode; entry->sort_bias=bias; entry->cull_distance=1e6f;
    snapshot.entries.push_back(entry);
  };
  transparent(1,300,1,1);     // 300
  transparent(2,1000,1,1);    // 1000
  transparent(3,100,1,1);     // 100: high byte 0, never traversed
  transparent(4,70000,1,1);   // 65535
  transparent(5,20,2,0.01f);  // 655
  transparent(6,1000,1,1);    // 1000, after entry 2
  auto opaque=Entry(7,{0,0,50}); snapshot.entries.push_back(opaque);
  const auto plan=PlanNativeFullFrameModels(snapshot,camera);
  Require(plan.opaque.size()==1 && plan.opaque[0].entry->object==7,"mode 0 is opaque");
  Require(plan.stats.bucket_zero==1 && plan.transparent.size()==5,"bucket 0 keys are dropped");
  const std::array<uint32_t,5> order{4,2,6,5,1};
  for(size_t i=0;i<order.size();++i) Require(plan.transparent[i].entry->object==order[i],"transparents draw key descending, ties in gather order");
  Require(plan.transparent[0].key==65535 && plan.transparent[3].key==655,"transparent keys are recorded");
}
void Instancing() {
  // Two model resources: A has two passes on one batch, B one pass. Entries
  // interleave A, B, A, A; the opaque order groups identical draws.
  const auto a=Layout(0x1000,false,1,{Mesh(0,false,true,{Batch(0x2000,{0x3000,0x3100})})});
  const auto b=Layout(0x1800,false,1,{Mesh(0,false,true,{Batch(0x2800,{0x3800})})});
  NativeRenderRegistrySnapshot snapshot;
  for(uint32_t i=0;i<4;++i) snapshot.entries.push_back(Entry(i+1,{0,0,100},1,i==1?b:a));
  const auto plan=PlanNativeFullFrameModels(snapshot,MakeCamera());
  Require(plan.opaque.size()==4,"identical entries are all visible");
  const auto draws=OrderNativeFullFrameModelDraws(plan.opaque,true);
  Require(draws.size()==7,"every pass of every entry draws");
  // Pass 0 of A for items 0,2,3 adjacent, then B, then pass 1 of A for 0,2,3.
  const std::array<std::pair<uint32_t,uint32_t>,7> expected{{
    {0,0x3000},{2,0x3000},{3,0x3000},{1,0x3800},{0,0x3100},{2,0x3100},{3,0x3100}}};
  for(size_t i=0;i<expected.size();++i)
    Require(draws[i].item==expected[i].first && draws[i].draw.pass==expected[i].second,"identical draws are adjacent");
  for(uint32_t item=0;item<4;++item) {
    int last=-1;
    for(const auto& draw:draws) if(draw.item==item) { Require(int(draw.pass_index)>last,"an entry's passes keep their order"); last=int(draw.pass_index); }
  }
  // Transparent lists keep each entry's draws together in guest order.
  const auto sorted=OrderNativeFullFrameModelDraws(plan.opaque,false);
  Require(sorted[0].item==0 && sorted[1].item==0 && sorted[1].draw.pass==0x3100 && sorted[2].item==1,"transparent draws stay in item order");
}
// 821C9DA8 sets: one item per world after the entry's own model, sharing its
// route; the sphere draws of every world are adjacent in the opaque order.
void InstancedWorlds() {
  auto sphere=std::make_shared<NativeModelLayout>();
  sphere->instance=0x9000; sphere->node=0x9100; sphere->single_world=true;
  sphere->meshes={Mesh(0,false,true,{Batch(0x9200,{0x9300})}),Mesh(0,true,false,{})};
  auto worlds=std::make_shared<std::vector<NativePoseMatrix>>(3);
  for(size_t w=0;w<3;++w) for(size_t i=0;i<16;++i) (*worlds)[w][i]=float(w*1000+i);
  auto mother=Entry(1,{0,0,100});
  mother->instanced.push_back({{sphere->instance,sphere},worlds});
  auto tree=Entry(2,{0,0,100});
  NativeRenderRegistrySnapshot snapshot;
  snapshot.entries.push_back(mother); snapshot.entries.push_back(tree);
  const auto plan=PlanNativeFullFrameModels(snapshot,MakeCamera());
  Require(plan.opaque.size()==5 && plan.stats.instances==3 && plan.stats.opaque==5,"the model, three worlds, then the next entry");
  Require(plan.opaque[0].instanced==-1 && plan.opaque[1].instanced==0 && plan.opaque[1].world==0 && plan.opaque[3].world==2 &&
    plan.opaque[3].entry==mother.get() && plan.opaque[4].entry==tree.get(),"worlds follow their entry in record order");
  Require(&NativeFullFrameModelItemLayout(plan.opaque[2])==sphere.get() &&
    &NativeFullFrameModelItemLayout(plan.opaque[0])==mother->models[0].layout.get(),"an instance item names the sphere layout");
  const auto values=NativeFullFrameModelInstancedConstants(*sphere,(*worlds)[1]);
  Require(!values.skinned && values.palette.empty() && values.worlds.size()==2 &&
    values.worlds[0]==NativeModelWorldRegisters((*worlds)[1]) && values.worlds[1]==values.worlds[0],"every record uploads the one world");
  bool rejected=false;
  try { NativeFullFrameModelInstancedConstants(*mother->models[0].layout,(*worlds)[0]); } catch(const std::exception&) { rejected=true; }
  Require(rejected,"a pose layout is not drawn as instances");
  const auto draws=OrderNativeFullFrameModelDraws(plan.opaque,true);
  Require(draws.size()==5,"one draw per world and one per model");
  size_t first=draws.size();
  for(size_t i=0;i<draws.size();++i) if(draws[i].draw.pass==0x9300) { first=(std::min)(first,i); Require(i-first<3,"sphere draws are adjacent"); }
  Require(first<draws.size(),"the sphere draws");
  // Without a pose the model is dropped, not its instances; an undecoded set draws nothing.
  mother->pose=Pose(4);
  auto undecoded=Entry(3,{0,0,100}); undecoded->instanced.push_back({{0x9400,nullptr},worlds});
  NativeRenderRegistrySnapshot unposed;
  unposed.entries.push_back(mother); unposed.entries.push_back(undecoded);
  const auto partial=PlanNativeFullFrameModels(unposed,MakeCamera());
  Require(partial.stats.no_pose==1 && partial.stats.no_instanced==1 && partial.opaque.size()==4 &&
    partial.opaque[0].instanced==0 && partial.opaque[3].entry==undecoded.get() && partial.opaque[3].instanced==-1,
    "instances survive a missing pose");
}
// 820DEA08: 8210AE48's LOD model, then the face (820DB268), then each weapon
// (820DE790 -> 820E1A80), all inside the one slot-4 call: attachment items
// follow their entry's model in published order, share its route and key,
// and draw with their own layout and pose; an attachment whose model has not
// decoded for its pose is skipped alone.
void AttachmentsFollowTheModel() {
  const auto face=Layout(0x4000,false,1,{Mesh(0,false,true,{Batch(0x4100,{0x4200})})});
  const auto weapon=Layout(0x5000,true,2,{Mesh(0,true,false,{Batch(0x5100,{0x5200})})});
  const auto soldier=[&](uint32_t object,float z,int32_t mode) {
    auto entry=Entry(object,{0,0,z}); entry->mode=mode; entry->sort_bias=1; entry->cull_distance=1e6f;
    entry->attachments.push_back({{0x1588,face},0x1636,Pose(1)});
    entry->attachments.push_back({{0x1100,nullptr},0x1144,Pose(2)});   // Not decoded yet.
    entry->attachments.push_back({{0x2100,weapon},0x2144,Pose(2)});
    entry->attachments.push_back({{0x3100,weapon},0x3144,Pose(3)});    // Pose not the layout's size.
    return entry;
  };
  NativeRenderRegistrySnapshot snapshot;
  const auto opaque=soldier(1,100,0);
  const auto filed=soldier(2,1000,1);
  const auto unposed=soldier(3,100,0); unposed->pose.reset();
  const auto after=Entry(4,{0,0,100});
  for(const auto& entry:{opaque,filed,unposed,after}) snapshot.entries.push_back(entry);
  const auto plan=PlanNativeFullFrameModels(snapshot,MakeCamera());
  Require(plan.stats.attachments==6 && plan.stats.no_attachment==6 && plan.stats.no_pose==1,"attachments counted");
  Require(plan.opaque.size()==6,"model, face and weapon; face and weapon without the model; the next entry");
  Require(plan.opaque[0].entry==opaque.get() && plan.opaque[0].attachment==-1 &&
    plan.opaque[1].entry==opaque.get() && plan.opaque[1].attachment==0 &&
    plan.opaque[2].entry==opaque.get() && plan.opaque[2].attachment==2 &&
    plan.opaque[3].entry==unposed.get() && plan.opaque[3].attachment==0 && plan.opaque[4].attachment==2 &&
    plan.opaque[5].entry==after.get() && plan.opaque[5].attachment==-1,"attachments follow their model in guest order");
  Require(&NativeFullFrameModelItemLayout(plan.opaque[1])==face.get() && &NativeFullFrameModelItemLayout(plan.opaque[2])==weapon.get() &&
    NativeFullFrameModelItemLayoutObject(plan.opaque[2])==weapon,"an attachment item draws its own layout");
  Require(NativeFullFrameModelItemPose(plan.opaque[2]).first==&opaque->attachments[2].pose &&
    NativeFullFrameModelItemPose(plan.opaque[2]).second==&opaque->attachments[2].motion &&
    NativeFullFrameModelItemPose(plan.opaque[0]).first==&opaque->pose,"an attachment item draws its own pose");
  Require(plan.transparent.size()==3 && plan.transparent[0].attachment==-1 && plan.transparent[1].attachment==0 &&
    plan.transparent[2].attachment==2 && plan.transparent[1].key==plan.transparent[0].key && plan.transparent[2].key==1000,
    "a filed entry's attachments share its key and stay in order");
  const auto draws=OrderNativeFullFrameModelDraws(plan.transparent,false);
  Require(draws.size()==3 && draws[0].item==0 && draws[1].item==1 && draws[2].item==2 && draws[2].draw.pass==0x5200,
    "transparent attachment draws follow the model's");
}
// A valid bone: rows scaled (sx, sy, sz) of a rotation about z, and a translation.
NativePoseMatrix Bone(float angle,std::array<float,3> scale,std::array<float,3> at) {
  const float c=std::cos(angle),s=std::sin(angle);
  return {c*scale[0],s*scale[0],0,0, -s*scale[1],c*scale[1],0,0, 0,0,scale[2],0, at[0],at[1],at[2],1};
}
// The pose source: the published pose unless the frame interpolates a pose of
// its tick with a previous one; blended bones are BlendNativePosePrepared's.
void PoseSource() {
  auto previous=std::make_shared<std::vector<NativePoseMatrix>>();
  auto current=std::make_shared<std::vector<NativePoseMatrix>>();
  previous->push_back(Bone(.1f,{1,2,1},{0,0,0}));     current->push_back(Bone(.5f,{2,2,1},{10,4,-2}));  // Moving.
  previous->push_back(Bone(1,{1,1,1},{5,5,5}));       current->push_back((*previous)[1]);               // Stationary.
  previous->push_back(Bone(.2f,{1,1,1},{0,0,0}));     current->push_back(Bone(.3f,{1,1,1},{1,0,0}));
  (*current)[2][1]+=.5f;                                                                             // Sheared.
  previous->push_back(Bone(.2f,{1,1,1},{0,0,0}));     current->push_back(Bone(.3f,{1,1,1},{900,0,0}));  // Cut.
  const NativeRenderPose pose=current;
  const NativeRenderPoseMotion motion{previous,7,false};
  const NativeFrameMotion frame{7,.5f,1,true};
  NativeRenderPoseBlender blender;
  // Off: the published pose itself.
  auto off=frame; off.interpolate=false;
  Require(blender.Pose(pose,motion,off).data()==current->data(),"interpolation off draws the published pose");
  auto other=frame; other.tick=8;
  Require(blender.Pose(pose,motion,other).data()==current->data(),"a pose of an earlier tick is stationary since");
  auto end=frame; end.fraction=1;
  Require(blender.Pose(pose,motion,end).data()==current->data(),"alpha 1 draws the published pose");
  Require(blender.Pose(pose,{previous,7,true},frame).data()==current->data(),"a render-dependent pose is not blended again");
  Require(blender.Pose(pose,{nullptr,7,false},frame).data()==current->data(),"no previous pose, no blend");
  Require(blender.stats().blended==0,"nothing blended yet");
  const auto same=[](const NativePoseMatrix& a,const NativePoseMatrix& b) { return !std::memcmp(a.data(),b.data(),sizeof(a)); };
  auto start=frame; start.fraction=0;
  const auto first=blender.Pose(pose,motion,start);
  const std::vector<NativePoseMatrix> at_start(first.begin(),first.end());
  Require(same(at_start[0],(*previous)[0]),"alpha 0 draws the previous pose's bits");
  Require(same(at_start[1],(*current)[1]) && same(at_start[2],(*current)[2]) && same(at_start[3],(*current)[3]),
    "alpha 0: stationary, sheared and cut bones keep the current bits");
  const auto second=blender.Pose(pose,motion,frame);
  const std::vector<NativePoseMatrix> half(second.begin(),second.end());
  Require(same(half[0],BlendNativePoseMatrix((*previous)[0],(*current)[0],.5f)) && !same(half[0],(*current)[0]),
    "a moving bone is BlendNativePoseMatrix's");
  Require(same(half[1],(*current)[1]),"a stationary bone keeps the current bits");
  Require(same(half[2],(*current)[2]),"a sheared bone passes through");
  Require(same(half[3],(*current)[3]),"a cut bone is not swept");
  Require(blender.stats().blended==2 && blender.stats().prepared==1 && blender.stats().reused==1,"the pair is prepared once and reused");
  // One matrix of a pose (an instanced world), and a pose of another size.
  Require(same(blender.Matrix(pose,motion,0,frame),half[0]) && same(blender.Matrix(pose,motion,0,off),(*current)[0]),
    "an instanced world blends as a bone");
  auto shorter=std::make_shared<std::vector<NativePoseMatrix>>(previous->begin(),previous->begin()+2);
  Require(blender.Pose(pose,{shorter,7,false},frame).data()==current->data(),"a previous pose of another size is not blended");
  // Rows unused for two frames are released.
  blender.EndFrame();
  Require(blender.size()==1,"a row used this frame is kept");
  blender.EndFrame();
  Require(blender.size()==0,"a row unused since the last frame is dropped");
}
void BaseState() {
  NativeFullFrameModelTargets targets; targets.dsv_format=1;
  const auto state=NativeFullFrameModelBaseState(targets);
  Require(state.render.depth_requested==1 && (state.render.words[1]&2),"base state enables depth test");
  Require(state.render.blend_control==0 || !(state.render.blend_control&0x80000000u),"base state disables blending");
  targets.count=5;
  bool rejected=false;
  try { NativeFullFrameModelBaseState(targets); } catch(const std::exception&) { rejected=true; }
  Require(rejected,"more than four color targets are refused");
  // One base state for every full-frame scene pass.
  Require(&kNativeFullFrameModelBaseOperations==&kNativeFullFrameBaseOperations,"models share the full-frame base operations");
  targets.count=1;
  Require(NativeFullFrameModelBaseState(targets)==NativeFullFrameBaseState(targets),"models share the full-frame base state");
}
using SourcePair=std::pair<std::shared_ptr<const int>,std::shared_ptr<const int>>;
void SourceTable() {
  NativeFullFrameModelSourceTable<SourcePair> table;
  const auto layout=Layout(0x1000,false,1,{Mesh(0,false,true,{Batch(0x2000,{0x3000})})});
  auto program=std::make_shared<const int>(1),geometry=std::make_shared<const int>(2);
  int fetched=0;
  const auto fetch=[&] { ++fetched; return SourcePair{program,geometry}; };
  const auto keep=[](const SourcePair& value) { return value.first && value.second; };
  const auto first=table.Get(0x3000,0x2000,layout,1,fetch,keep);
  Require(fetched==1 && first.first==program && first.second==geometry,"the first frame fetches the sources");
  table.EndFrame();
  const auto second=table.Get(0x3000,0x2000,layout,1,fetch,keep);
  Require(fetched==1 && second==first && table.hits==1,"the second frame reuses them without the providers");
  // The providers' generation moves with a rebuilt program or reloaded geometry.
  program=std::make_shared<const int>(3);
  const auto rebuilt=table.Get(0x3000,0x2000,layout,2,fetch,keep);
  Require(fetched==2 && rebuilt.first==program,"a new generation fetches the rebuilt program");
  geometry=std::make_shared<const int>(4);
  const auto reloaded=table.Get(0x3000,0x2000,layout,3,fetch,keep);
  Require(fetched==3 && reloaded.second==geometry,"a new generation fetches the reloaded geometry");
  Require(table.Get(0x3000,0x2000,layout,3,fetch,keep)==reloaded && fetched==3,"the refetched row is reused at its generation");
  // A new layout generation is a new layout object: its own row.
  const auto relayout=Layout(0x1000,false,1,{Mesh(0,false,true,{Batch(0x2000,{0x3000})})});
  table.Get(0x3000,0x2000,relayout,3,fetch,keep);
  Require(fetched==4 && table.size()==2,"a new layout fetches its own sources");
  // A missing source is asked again; an unversioned source is never kept.
  geometry=nullptr;
  table.Get(0x3100,0x2000,layout,3,fetch,keep); table.Get(0x3100,0x2000,layout,3,fetch,keep);
  Require(fetched==6,"a missing geometry is not stored");
  geometry=std::make_shared<const int>(5);
  table.Get(0x3000,0x2000,layout,kNativeFullFrameModelUnversioned,fetch,keep);
  table.Get(0x3000,0x2000,layout,kNativeFullFrameModelUnversioned,fetch,keep);
  Require(fetched==8,"without a generation every use fetches");
}
// The host's change signal (NativeFullFrameModelSourceMemo): it advances only
// when a provider, asked again for every result a current row holds, returns
// another object (or none, or throws) or a host identity moved; rows
// refetched after an advance are served from the results just asked for.
void SourceMemo() {
  using Program=std::shared_ptr<const int>;
  using Memo=NativeFullFrameModelSourceMemo<int,Program,uint32_t,uint32_t,Program>;
  Memo memo(2,64,0);
  int host=1;
  std::map<uint32_t,Program> programs{{0x3000,std::make_shared<const int>(1)},{0x3100,std::make_shared<const int>(2)},
    {0x3200,std::make_shared<const int>(3)}};
  std::map<uint32_t,Program> geometry{{0x2000,std::make_shared<const int>(10)},{0x2100,std::make_shared<const int>(11)}};
  bool throws=false;
  size_t asked=0,runs=0,fetched=0;
  const auto validate=[&] {
    return memo.Validate([&] { return host; },
      [&](uint32_t pass) { ++asked; if(throws) throw std::runtime_error("provider"); return programs.at(pass); },
      [&](uint32_t batch) { ++asked; return geometry.at(batch); },
      [&](const auto& work) { ++runs; work(); });
  };
  const auto program=[&](uint32_t pass) { return memo.ProgramFor(pass,[&] { ++fetched; return programs.at(pass); }); };
  const auto batch=[&](uint32_t address) { return memo.GeometryFor(address,address,[&] { ++fetched; return geometry.at(address); }); };
  const auto first=validate();
  Require(first==1 && asked==0 && runs==1,"the first Build advances on its host");
  Require(program(0x3000)==programs[0x3000] && program(0x3100)==programs[0x3100] && program(0x3000)==programs[0x3000] &&
    batch(0x2000)==geometry[0x2000] && batch(0x2100)==geometry[0x2100] && fetched==4 && memo.size()==4,
    "each distinct program and geometry is fetched once");
  // Unchanged: every remembered result asked again, in chunks of two, one
  // slice each; the generation holds and nothing is fetched.
  asked=0; runs=0;
  Require(validate()==first && asked==4 && runs==2 && memo.stats().validated==4,"an unchanged Build moved the generation");
  // A refreshed program (a constant value moved on any iteration): the
  // generation advances, and refetched rows are served from what was just
  // asked, the new program included, without a provider call.
  programs[0x3100]=std::make_shared<const int>(20);
  const auto second=validate();
  Require(second==first+1 && memo.stats().changes==1,"a refreshed program did not advance the generation");
  fetched=0;
  Require(program(0x3100)==programs[0x3100] && program(0x3000)==programs[0x3000] && batch(0x2000)==geometry[0x2000] && fetched==0,
    "rows refetched after an advance called a provider");
  // What no row asked for since the advance (0x2100) is dropped.
  asked=0;
  Require(validate()==second && asked==3,"a result no current row holds was still validated");
  Require(batch(0x2100)==geometry[0x2100] && fetched==1,"a dropped result is fetched again");
  // Reloaded geometry advances too.
  geometry[0x2000]=std::make_shared<const int>(12);
  const auto third=validate();
  Require(third==second+1 && batch(0x2000)==geometry[0x2000] && fetched==1,"reloaded geometry");
  // A result that is now missing: advance, not carried, asked again.
  Require(program(0x3000)==programs[0x3000] && program(0x3100)==programs[0x3100] && fetched==1,"carried programs");
  auto missing=programs[0x3000]; programs[0x3000]=nullptr;
  const auto fourth=validate();
  Require(fourth==third+1 && !program(0x3000) && fetched==2,"a missing program was carried or not asked again");
  programs[0x3000]=missing;
  Require(program(0x3000)==missing && fetched==3,"a missing program is not remembered");
  program(0x3100); batch(0x2000);
  // A provider that throws advances.
  throws=true;
  const auto fifth=validate();
  throws=false;
  Require(fifth==fourth+1,"a throwing provider did not advance");
  // A host change advances without asking the providers and carries nothing.
  program(0x3000); fetched=0; asked=0;
  host=2;
  Require(validate()==fifth+1 && asked==0 && memo.stats().host_changes==2,"a host change");
  Require(program(0x3000)==programs[0x3000] && fetched==1,"a host change carried a result");
  // The side table over the memo's generation: rows hit while it holds.
  NativeFullFrameModelSourceTable<SourcePair> table;
  const auto layout=Layout(0x1000,false,1,{Mesh(0,false,true,{Batch(0x2000,{0x3000})})});
  size_t rows=0;
  const auto row=[&](uint64_t generation) {
    return table.Get(0x3000,0x2000,layout,generation,[&] { ++rows; return SourcePair{program(0x3000),batch(0x2000)}; },
      [](const SourcePair& value) { return value.first && value.second; });
  };
  auto generation=validate(); row(generation);
  for(int frame=0;frame<3;++frame) { generation=validate(); row(generation); }
  Require(rows==1 && table.hits==3,"rows are fetched once while nothing changes");
  programs[0x3000]=std::make_shared<const int>(30);
  generation=validate(); row(generation);
  Require(rows==2 && row(generation).first==programs[0x3000],"a changed program refetches its rows");
  // Pruning: after `age` idle Builds the generation advances without a
  // provider call; past `limit` results as well.
  Memo aging(16,64,3);
  const auto age=[&] { return aging.Validate([&] { return host; },[&](uint32_t pass) { return programs.at(pass); },
    [&](uint32_t address) { return geometry.at(address); },[](const auto& work) { work(); }); };
  const auto start=age();
  aging.ProgramFor(0x3000,[&] { return programs.at(0x3000); });
  Require(age()==start && age()==start && age()==start+1 && aging.stats().prunes==1,"an idle memo is pruned after its age");
  fetched=0;
  Require(aging.ProgramFor(0x3000,[&] { ++fetched; return programs.at(0x3000); })==programs[0x3000] && fetched==0,
    "a prune carries what it asked");
  Memo bounded(16,1,0);
  bounded.Validate([&] { return host; },[&](uint32_t pass) { return programs.at(pass); },[&](uint32_t address) { return geometry.at(address); },
    [](const auto& work) { work(); });
  bounded.ProgramFor(0x3000,[&] { return programs.at(0x3000); }); bounded.ProgramFor(0x3100,[&] { return programs.at(0x3100); });
  const auto before=bounded.generation();
  Require(bounded.Validate([&] { return host; },[&](uint32_t pass) { return programs.at(pass); },
    [&](uint32_t address) { return geometry.at(address); },[](const auto& work) { work(); })==before+1,"a memo past its limit is pruned");
}
NativeFullFrameModelMaterialKey MaterialKey(uint32_t pass,std::shared_ptr<const void> program,std::shared_ptr<const void> geometry) {
  NativeFullFrameModelTargets targets; targets.dsv_format=1;
  return {pass,false,std::move(program),std::move(geometry),NativeFullFrameModelBaseState(targets),targets,-1};
}
void MaterialCache() {
  using Cache=NativeFullFrameModelMaterialCache<int>;
  Cache cache;
  const auto key=MaterialKey(0x3000,std::make_shared<const int>(1),std::make_shared<const int>(2));
  const std::vector<NativeSceneMaterialInputs::Constant> constants{{false,"g_vColor",std::vector<uint8_t>(16,1),false}};
  Require(!cache.Candidate(key),"the first frame misses");
  cache.Store(key,constants,7,nullptr,{});
  cache.EndFrame();
  NativeSceneView camera;
  auto* entry=cache.Candidate(key);
  Require(entry && entry->material==7 && Cache::Current(*entry,constants,nullptr,camera),"the second frame hits the stored resolve");
  auto moved=constants; moved[0].registers[0]=2;
  Require(!Cache::Current(*entry,moved,nullptr,camera),"a moved constant is not current");
  auto program=key; program.program=std::make_shared<const int>(1);
  Require(!cache.Candidate(program),"a rebuilt program misses");
  auto geometry=key; geometry.geometry=std::make_shared<const int>(2);
  Require(!cache.Candidate(geometry),"reloaded geometry misses");
  auto filtering=key; filtering.filtering=4;
  Require(!cache.Candidate(filtering),"a filtering change misses");
  auto targets=key; targets.targets.reverse_depth=true;
  Require(!cache.Candidate(targets),"a target change misses");
  auto skinned=key; skinned.skinned=true;
  Require(!cache.Candidate(skinned),"skinned and rigid rows are distinct");
  // The rebuilt program's resolve is its own row; the old one ages out.
  cache.Store(program,constants,8,nullptr,{});
  Require(cache.Candidate(program)->material==8 && cache.Candidate(key)->material==7 && cache.size()==2,
    "each program generation keeps its own resolve");
  for(int frame=0;frame<600;++frame) { cache.Candidate(program); cache.EndFrame(); }
  Require(!cache.Candidate(key) && cache.Candidate(program) && cache.size()==1,"unused rows age out");
}
void RigidInstancing() {
  // Three identical rigid entries with their own poses: every draw finds the
  // one material row, the draws are adjacent and each keeps its own world.
  const auto layout=Layout(0x1000,false,1,{Mesh(0,false,true,{Batch(0x2000,{0x3000})})});
  NativeRenderRegistrySnapshot snapshot;
  for(uint32_t i=0;i<3;++i) {
    auto entry=Entry(i+1,{float(i),0,100},1,layout);
    auto pose=std::make_shared<std::vector<NativePoseMatrix>>(1,NativePoseMatrix{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1});
    (*pose)[0][12]=float(i*10);
    entry->pose=pose;
    snapshot.entries.push_back(entry);
  }
  const auto plan=PlanNativeFullFrameModels(snapshot,MakeCamera());
  Require(plan.opaque.size()==3,"identical rigid entries are visible");
  const auto draws=OrderNativeFullFrameModelDraws(plan.opaque,true);
  Require(draws.size()==3 && draws[0].item==0 && draws[1].item==1 && draws[2].item==2,"identical draws are adjacent");
  NativeFullFrameModelMaterialCache<int> cache;
  const auto program=std::make_shared<const int>(1),geometry=std::make_shared<const int>(2);
  int stored=0;
  std::vector<int> materials;
  std::vector<std::array<uint8_t,64>> worlds;
  for(int frame=0;frame<2;++frame) {
    for(const auto& ref:draws) {
      const auto& item=plan.opaque[ref.item];
      const auto& model=*item.entry->models[item.model].layout;
      const auto key=MaterialKey(ref.draw.pass,program,geometry);
      auto* entry=cache.Candidate(key);
      if(!entry) entry=&cache.Store(key,{},stored++,nullptr,{});
      materials.push_back(entry->material);
      worlds.push_back(NativeFullFrameModelConstantsFor(model,*item.entry->pose,68).worlds[ref.draw.mesh]);
    }
    cache.EndFrame();
  }
  Require(stored==1 && cache.size()==1 && std::count(materials.begin(),materials.end(),0)==6,
    "identical rigid entries share one material across frames");
  Require(worlds[0]!=worlds[1] && worlds[1]!=worlds[2] && worlds[0]==worlds[3],"each instance keeps its own world");
}
void InstancedCache() {
  // The mothership's single-world sphere items: every world's draw uses one
  // source row (the set's layout object) and one material row across frames,
  // with its own world, so the renderer instances them.
  auto sphere=std::make_shared<NativeModelLayout>();
  sphere->instance=0x9000; sphere->node=0x9100; sphere->single_world=true;
  sphere->meshes={Mesh(0,false,true,{Batch(0x9200,{0x9300})})};
  auto worlds=std::make_shared<std::vector<NativePoseMatrix>>(4,NativePoseMatrix{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1});
  for(size_t w=0;w<worlds->size();++w) (*worlds)[w][12]=float(w*10);
  auto mother=Entry(1,{0,0,100});
  mother->instanced.push_back({{sphere->instance,sphere},worlds});
  NativeRenderRegistrySnapshot snapshot;
  snapshot.entries.push_back(mother);
  const auto plan=PlanNativeFullFrameModels(snapshot,MakeCamera());
  Require(plan.opaque.size()==5 && plan.stats.instances==4,"the model and four sphere worlds");
  NativeFullFrameModelSourceTable<SourcePair> table;
  NativeFullFrameModelMaterialCache<int> cache;
  const auto program=std::make_shared<const int>(1),geometry=std::make_shared<const int>(2);
  int fetched=0,stored=0;
  const auto fetch=[&] { ++fetched; return SourcePair{program,geometry}; };
  const auto keep=[](const SourcePair& value) { return value.first && value.second; };
  std::vector<std::array<uint8_t,64>> sphere_worlds;
  for(int frame=0;frame<2;++frame) {
    for(const auto& ref:OrderNativeFullFrameModelDraws(plan.opaque,true)) {
      const auto& item=plan.opaque[ref.item];
      if(item.instanced<0) continue;
      const auto& set=item.entry->instanced[size_t(item.instanced)];
      const auto& layout=NativeFullFrameModelItemLayout(item);
      Require(&layout==sphere.get(),"an instance item draws the set's layout");
      const auto source=table.Get(ref.draw.pass,layout.meshes[ref.draw.mesh].batches[ref.draw.batch].address,set.model.layout,1,fetch,keep);
      const auto key=MaterialKey(ref.draw.pass,source.first,source.second);
      if(!cache.Candidate(key)) cache.Store(key,{},stored++,nullptr,{});
      if(!frame) sphere_worlds.push_back(NativeFullFrameModelInstancedConstants(layout,set.worlds->at(item.world)).worlds[ref.draw.mesh]);
    }
    table.EndFrame(); cache.EndFrame();
  }
  Require(fetched==1 && table.size()==1,"every sphere world shares one source row across frames");
  Require(stored==1 && cache.size()==1,"every sphere world shares one material row across frames");
  Require(sphere_worlds.size()==4 && sphere_worlds[0]!=sphere_worlds[1] && sphere_worlds[2]!=sphere_worlds[3],
    "each sphere instance keeps its own world");
}
// The +712 = +708 set: every clBrokenObject 821C0C00 would hand to slot 4
// (visible, mode 0 or a traversed bucket), drawn or not; nothing else.
void BrokenObjects() {
  const auto camera=MakeCamera();
  NativeRenderRegistrySnapshot snapshot;
  const auto add=[&](uint32_t object,std::array<float,3> centre,const NativeRenderClass* type=&kBroken) {
    auto entry=Entry(object,centre); entry->type=type; snapshot.entries.push_back(entry); return entry;
  };
  const auto drawn=add(1,{0,0,100});                                               // Posed and laid out: drawn.
  const auto unposed=add(2,{0,0,120}); unposed->pose.reset();                        // No native pose yet.
  const auto undecoded=add(3,{0,0,140}); undecoded->models[0].layout.reset();        // No layout yet.
  const auto modelless=add(4,{0,0,160}); modelless->models.clear(); modelless->pose.reset();
  const auto hidden=add(5,{0,0,100}); hidden->hidden=true;
  const auto outside=add(6,{500,0,100});
  const auto distant=add(7,{0,0,20000});
  const auto boxed=add(8,{105,0,100}); boxed->radius=10; boxed->axes={0.1f,0,0,0, 0,0.1f,0,0, 0,0,0.1f,0};
  const auto filed=add(9,{0,0,300}); filed->mode=1; filed->sort_bias=1;             // Key 300: traversed.
  const auto bucket_zero=add(10,{0,0,100}); bucket_zero->mode=1; bucket_zero->sort_bias=1; // Key 100: never traversed.
  const auto unsupported=add(11,{0,0,100}); unsupported->mode=3;
  const auto other=add(12,{0,0,100},&kPlain);                                        // Visible, not a clBrokenObject.
  const auto set=NativeFullFrameBrokenObjects(snapshot,camera);
  const std::vector<const NativeRenderEntry*> expected{drawn.get(),unposed.get(),undecoded.get(),modelless.get(),filed.get()};
  Require(set==expected,"every dispatched clBrokenObject is in the +712 set, drawn or not, in snapshot order");
  // The draw plan is the posed subset: the set is not.
  const auto plan=PlanNativeFullFrameModels(snapshot,camera);
  size_t drawn_broken=0;
  for(const auto* items:{&plan.opaque,&plan.transparent}) for(const auto& item:*items) drawn_broken+=item.entry->type==&kBroken;
  Require(drawn_broken==2 && plan.stats.no_model==2,"only the posed, laid-out objects are drawn");
  // Visibility decides: moving a hidden object back into view adds it.
  hidden->hidden=false; outside->centre={0,0,100,1};
  Require(NativeFullFrameBrokenObjects(snapshot,camera).size()==expected.size()+2,"visible again, stored again");
  Require(NativeFullFrameModelDispatched(ClassifyNativeFullFrameModel(*filed,camera.visibility),*filed,camera) &&
    !NativeFullFrameModelDispatched(ClassifyNativeFullFrameModel(*bucket_zero,camera.visibility),*bucket_zero,camera),
    "a filed object's slot 4 runs only from a traversed bucket");
}
// A skinned program: a column_major float4x3 palette (3 registers per bone, 4
// components) sharing $Globals with the world and camera matrices the capture
// zeroes, a float3 array (padded registers), a pixel local and a texture.
struct SkinnedFixture {
  using Constant=NativeSceneMaterialInputs::Constant;
  std::shared_ptr<NativeRenderBackend> backend;
  std::shared_ptr<NativeSceneMaterialProgram> program=std::make_shared<NativeSceneMaterialProgram>();
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  std::vector<Constant> constants;
  explicit SkinnedFixture(std::shared_ptr<NativeRenderBackend> with):backend(std::move(with)) {
    Effect effect;
    effect.source=R"(
      float4x3 g_mWorldArray[8];
      row_major float4x4 g_mWorld;
      row_major float4x4 g_mViewProjection;
      float3 g_vLights[4];
      struct V { float4 position:SV_Position; float4 color:COLOR0; };
      V VS(float3 position:POSITION0) {
        V o; int bone=int(abs(position.z*7))%8;
        float3 skinned=mul(float4(position,1),g_mWorldArray[bone]);
        o.position=mul(mul(float4(skinned,1),g_mWorld),g_mViewProjection);
        o.color=float4(g_vLights[bone%4],1); return o;
      }
      float4 tint;
      Texture2D image; SamplerState imageSampler;
      float4 PS(V v):SV_Target { return v.color*tint*image.Sample(imageSampler,float2(.5,.5)); }
    )";
    program->backend=backend;
    program->vertex=program->reversed_vertex=CompileNativeShader(nullptr,effect,{false,"VS","vs_3_0"},"palette-test.fx");
    program->pixel=CompileNativeShader(nullptr,effect,{true,"PS","ps_3_0"},"palette-test.fx");
    std::vector<uint8_t> declaration(12),vertices(48),indices{0,0,0,1,0,2,0,0,0,2,0,3};
    Word(declaration,4,0x2a23b9);
    const float points[]{-.125f,-.25f,.5f, -.125f,.25f,.5f, .125f,.25f,.5f, .125f,-.25f,.5f};
    for(size_t i=0;i<12;++i) Word(vertices,i*4,std::bit_cast<uint32_t>(points[i]));
    NativeIndexedMesh mesh(*backend,program->vertex,declaration,12,vertices,indices,2);
    geometry=std::make_shared<const NativeIndexedMesh::RetainedDraw>(mesh.RetainDraw(backend,0,6));
    NativeBackendTextureDesc texture; texture.width=texture.height=1; texture.format=DXGI_FORMAT_R8G8B8A8_UNORM;
    const uint8_t white[]{255,255,255,255};
    program->inputs.textures.push_back({"image"}); program->inputs.textures.push_back({"imageSampler"});
    program->textures={backend->CreateTexture(texture,white),{}};
    // The published palette is whatever the last object left (never read).
    constants.push_back({false,"g_mWorldArray",std::vector<uint8_t>(32*16,0xcd),true});
    constants.push_back({false,"g_mWorld",Floats({1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}),true});
    constants.push_back({false,"g_mViewProjection",Floats({2,0,0,0, 0,2,0,0, 0,0,1,0, 0,0,0,1}),true});
    std::vector<uint8_t> lights(4*16);
    for(size_t i=0;i<16;++i) Word(lights,i*4,std::bit_cast<uint32_t>(float(i)+.5f));
    constants.push_back({false,"g_vLights",lights,true});
    constants.push_back({true,"tint",Floats({1,.5f,.25f,1}),false});
  }
  static void Word(std::vector<uint8_t>& bytes,size_t offset,uint32_t word) {
    for(size_t i=0;i<4;++i) bytes[offset+i]=uint8_t(word>>(24-i*8));
  }
  static std::vector<uint8_t> Floats(std::initializer_list<float> values) {
    std::vector<uint8_t> bytes(values.size()*4);
    size_t i=0; for(const auto value:values) Word(bytes,4*i++,std::bit_cast<uint32_t>(value));
    return bytes;
  }
  NativeFullFrameModelPass Pass() const {
    NativeFullFrameModelPass pass;
    pass.targets.rtv_format[0]=DXGI_FORMAT_R8G8B8A8_UNORM; pass.targets.dsv_format=DXGI_FORMAT_D32_FLOAT;
    return pass;
  }
  // The per-draw capture the palette capture replaces: every constant bound by
  // name into fresh bindings with the palette bound, then CaptureNativeSceneMaterial.
  NativeSceneMaterialCapture Full(NativeBackendPipeline& pipeline,std::vector<Constant> bound,std::span<const float> palette,
      std::span<NativeBackendSampler* const> samplers,std::optional<std::array<float,4>> blend) const {
    Require(BindNativeFullFrameModelPalette(bound,palette),"the palette fits");
    return program->Capture(pipeline,false,bound,samplers,blend,true);
  }
};
bool SameCapture(const NativeSceneMaterialCapture& a,const NativeSceneMaterialCapture& b) {
  return a.material && b.material && a.material!=b.material && a.material->Equivalent(*b.material) &&
    a.material->fingerprint()==b.material->fingerprint() && !std::memcmp(a.world.data(),b.world.data(),sizeof(a.world)) &&
    NativeSceneCameraIdentical(a.camera,b.camera);
}
std::vector<float> Palette(uint32_t bones,float seed) {
  std::vector<float> palette(size_t(bones)*kNativeBonePaletteFloats);
  for(size_t i=0;i<palette.size();++i) palette[i]=seed+float(i)*.25f;
  return palette;
}
// NativeScenePaletteCapture::With is Capture of the same constants with the
// palette bound, byte for byte, in any order of palettes; it never shares.
void PaletteCapture(std::shared_ptr<NativeRenderBackend> backend) {
  SkinnedFixture fixture(backend);
  const auto& program=*fixture.program;
  NativeBackendPipelineDesc desc;
  desc.vertex_id=0x500; desc.pixel_id=0x501;
  desc.input_layout=fixture.geometry->input_layout().elements(); desc.input_layout_id=fixture.geometry->input_layout().fingerprint();
  desc.render_targets=1; desc.rtv_format=fixture.Pass().targets.rtv_format; desc.dsv_format=DXGI_FORMAT_D32_FLOAT;
  NativeMaterialRenderPass render; render.words={0x10001,0,0,0,15,0}; render.color_targets[0]=1;
  const auto resolved=program.Resolve(desc,false,fixture.constants,render,{},-1,true);
  auto& pipeline=*resolved.capture.material->pipeline();
  const auto blend=resolved.capture.material->blend_factor();
  NativeBackendSampler* sampler=resolved.capture.material->samplers().at(0).second;
  const std::array<NativeBackendSampler*,2> samplers{sampler,sampler};
  NativeScenePaletteCapture palette(program,pipeline,false,fixture.constants,samplers,blend);
  Require(palette.capture().material->Equivalent(*resolved.capture.material),"the row capture is Resolve's capture");
  std::vector<SkinnedFixture::Constant> replaced;
  for(const auto& constant:fixture.constants) if(constant.name=="g_mWorldArray") replaced.push_back(constant);
  NativeSceneMaterialCapture previous;
  for(const auto& [bones,seed]:{std::pair{3u,1.f},std::pair{8u,-7.f},std::pair{1u,40.f},std::pair{3u,1.f},std::pair{0u,0.f}}) {
    const auto values=Palette(bones,seed);
    Require(BindNativeFullFrameModelPalette(replaced[0],values),"the palette binds");
    const auto derived=palette.With(replaced);
    const auto full=fixture.Full(pipeline,fixture.constants,values,samplers,blend);
    Require(SameCapture(derived,full),"a derived palette material is the full capture's, byte for byte");
    Require(derived.material!=palette.capture().material,"every draw has its own material");
    if(previous.material && bones)
      Require(!previous.material->Equivalent(*derived.material),"the palette reaches the image");
    previous=derived;
  }
  // A padded (float3) array and the pixel local rebind the same way.
  auto lights=fixture.constants[3]; lights.registers[5]=0x42;
  auto tint=fixture.constants[4]; tint.registers[0]=0x3e;
  const std::vector<SkinnedFixture::Constant> all{replaced[0],lights,tint};
  auto bound=fixture.constants; bound[0]=replaced[0]; bound[3]=lights; bound[4]=tint;
  Require(SameCapture(palette.With(all),program.Capture(pipeline,false,bound,samplers,blend,true)),
    "any non-matrix constant is rebound as Capture binds it");
  // Recorded matrices are world or camera, not image bytes: refused.
  auto world=fixture.constants[1]; world.registers[0]^=0x40;
  bool refused=false;
  try { palette.With(std::span(&world,1)); } catch(const std::exception&) { refused=true; }
  Require(refused,"a captured matrix is not rebindable");
  auto shortened=replaced[0]; shortened.registers.resize(16);
  refused=false;
  try { palette.With(std::span(&shortened,1)); } catch(const std::exception&) { refused=true; }
  Require(refused,"a palette smaller than the shader array is refused as Capture refuses it");
}
// Build's skinned draws against full captures over frames: a miss resolves
// the row once, later frames reuse it with new palettes and a moved camera,
// and a moved published constant recaptures it; every draw's material, world
// and camera are the full capture's.
void SkinnedBuild(std::shared_ptr<NativeRenderBackend> backend) {
  SkinnedFixture fixture(backend);
  // Record 0 palette-skinned, record 1 uploads bone 1 (a g_mWorld of its own).
  const auto layout=Layout(0x1000,true,3,{Mesh(0,true,false,{Batch(0x2000,{0x3000})}),Mesh(1,false,true,{Batch(0x2100,{0x3000})})});
  NativeRenderRegistrySnapshot snapshot;
  std::vector<std::shared_ptr<NativeRenderEntry>> entries;
  for(uint32_t i=0;i<3;++i) { entries.push_back(Entry(i+1,{float(i),0,100},1,layout)); snapshot.entries.push_back(entries.back()); }
  auto camera=MakeCamera();
  const auto vp=[&](float scale) {
    for(size_t i=0;i<16;++i) camera.pass.view_projection[i]=std::bit_cast<uint32_t>(i%5?0.f:scale);
  };
  vp(1.5f);
  auto pass=fixture.Pass();
  // The pose each item should have drawn, and how many items blend.
  std::function<std::vector<NativePoseMatrix>(const NativeFullFrameModelItem&)> expected=[](const NativeFullFrameModelItem& item) {
    return *item.entry->pose;
  };
  uint64_t blended=0;
  uint64_t generation=1;
  auto published=std::make_shared<NativeSceneGroupMaterial>();
  published->program=fixture.program; published->constants=fixture.constants;
  NativeFullFrameModelSources sources;
  sources.program=[&](uint32_t) { return std::shared_ptr<const NativeSceneGroupMaterial>(published); };
  sources.geometry=[&](const NativeModelBatchLayout&,uint32_t) { return fixture.geometry; };
  sources.generation=[&] { return generation; };
  NativeFullFrameModels models;
  const auto check=[&](const char* frame_name,uint64_t resolves,uint64_t captures,uint64_t hits) {
    const auto frame=models.Build(snapshot,camera,pass,sources);
    const auto& stats=frame.stats;
    Require(stats.drawn==3 && stats.draws==6 && stats.palettes==6 && stats.palette==0 && stats.failed==0,frame_name);
    Require(stats.resolves==resolves && stats.captures==captures && stats.cache_hits==hits && stats.blended==blended,frame_name);
    auto constants=published->constants;
    for(auto& constant:constants) camera.pass.Apply(constant);
    std::vector<std::pair<const NativeSceneInstance*,const NativeSceneView*>> drawn;
    for(const auto& batch:frame.batches) for(const auto& object:batch.snapshot.instances) drawn.emplace_back(object.get(),&batch.view);
    const auto refs=OrderNativeFullFrameModelDraws(frame.plan.opaque,true);
    Require(drawn.size()==refs.size(),"every draw is drawn in order");
    std::vector<const NativeSceneMaterial*> materials;
    for(size_t d=0;d<refs.size();++d) {
      const auto& item=frame.plan.opaque[refs[d].item];
      const auto values=NativeFullFrameModelConstantsFor(*layout,expected(item),pass.palette_limit);
      const auto& material=*drawn[d].first->object.material;
      NativeBackendSampler* sampler=material.samplers().at(0).second;
      const std::array<NativeBackendSampler*,2> samplers{sampler,sampler};
      auto full=fixture.Full(*material.pipeline(),constants,values.palette,samplers,material.blend_factor());
      if(NativeSceneCaptureBindsWorld(full)) ApplyNativeScenePublishedWorld(full,values.worlds[refs[d].draw.mesh]);
      NativeSceneMaterialCapture built{drawn[d].first->object.material,drawn[d].first->object.world,*drawn[d].second};
      Require(SameCapture(built,full),"a skinned draw's material, world and camera are the full capture's");
      materials.push_back(&material);
    }
    std::sort(materials.begin(),materials.end());
    Require(std::unique(materials.begin(),materials.end())==materials.end(),"skinned draws never share a material");
  };
  const auto pose=[&](float seed) {
    for(size_t e=0;e<entries.size();++e) {
      auto moved=std::make_shared<std::vector<NativePoseMatrix>>(3);
      for(size_t b=0;b<3;++b) for(size_t i=0;i<16;++i) (*moved)[b][i]=seed+float(e*100+b*16+i)*.125f;
      entries[e]->pose=moved;
    }
  };
  pose(1);
  check("the first frame resolves the row",1,0,0);
  pose(2);
  check("new palettes reuse the row",0,0,1);
  pose(3); vp(3.f);
  check("a moved camera is derived",0,0,1);
  auto republished=std::make_shared<NativeSceneGroupMaterial>(*published);
  republished->constants[3].registers[7]^=0x10;
  published=republished; ++generation;
  check("a moved published constant recaptures the row",0,1,0);
  // Pose motion: every entry moved from its previous tick's pose (one bone
  // stationary). Interpolation off draws the published poses; on, each drawn
  // pose is the stateless blend at the frame's fraction, and at fraction 0 the
  // previous pose.
  for(size_t e=0;e<entries.size();++e) {
    auto from=std::make_shared<std::vector<NativePoseMatrix>>(),to=std::make_shared<std::vector<NativePoseMatrix>>();
    for(size_t b=0;b<3;++b) {
      from->push_back(Bone(.1f*float(b),{1,1,1},{float(e),0,0}));
      to->push_back(Bone(.1f*float(b)+.3f,{1.5f,1,1},{float(e)+2,1,0}));
    }
    (*to)[1]=(*from)[1];
    entries[e]->pose=to; entries[e]->motion={from,9,false};
  }
  pass.motion={9,.25f,1,false};
  check("interpolation off draws the published poses",0,0,1);
  pass.motion.interpolate=true; blended=3;
  expected=[](const NativeFullFrameModelItem& item) {
    std::vector<NativePoseMatrix> pose;
    for(size_t b=0;b<item.entry->pose->size();++b)
      pose.push_back(BlendNativePoseMatrix((*item.entry->motion.previous)[b],(*item.entry->pose)[b],.25f));
    return pose;
  };
  check("interpolated poses are the stateless blend",0,0,1);
  pass.motion.fraction=0;
  expected=[](const NativeFullFrameModelItem& item) { return *item.entry->motion.previous; };
  check("alpha 0 draws the previous poses",0,0,1);
  pass.motion.fraction=1; blended=0;
  expected=[](const NativeFullFrameModelItem& item) { return *item.entry->pose; };
  check("alpha 1 draws the published poses",0,0,1);
  // The next tick's render with the same poses: stationary since, so every
  // object made at alpha 1 is carried.
  pass.motion={10,.5f,0,true};
  const auto carried=models.Build(snapshot,camera,pass,sources);
  Require(carried.stats.reused==6 && carried.stats.derived==0 && carried.stats.blended==0,
    "a pose of an earlier tick keeps carrying its objects");
}
// Attachments through Build: the face draws after the model with its own
// layout, pose and constants, in its own draw state; moving only the face
// remakes only its object.
void AttachmentBuild(std::shared_ptr<NativeRenderBackend> backend) {
  SkinnedFixture fixture(backend);
  const auto layout=Layout(0x1000,true,3,{Mesh(0,true,false,{Batch(0x2000,{0x3000})})});
  const auto face=Layout(0x1800,true,2,{Mesh(0,true,false,{Batch(0x2800,{0x3000})})});
  auto entry=Entry(1,{0,0,100},1,layout);
  entry->attachments.push_back({{face->instance,face},0x1636,Pose(2)});
  NativeRenderRegistrySnapshot snapshot;
  snapshot.entries.push_back(entry);
  const auto camera=MakeCamera();
  const auto pass=fixture.Pass();
  auto published=std::make_shared<NativeSceneGroupMaterial>();
  published->program=fixture.program; published->constants=fixture.constants;
  NativeFullFrameModelSources sources;
  sources.program=[&](uint32_t) { return std::shared_ptr<const NativeSceneGroupMaterial>(published); };
  sources.geometry=[&](const NativeModelBatchLayout&,uint32_t) { return fixture.geometry; };
  sources.generation=[] { return uint64_t(1); };
  NativeFullFrameModels models;
  const auto check=[&](const char* name,uint64_t derived,uint64_t reused) {
    const auto frame=models.Build(snapshot,camera,pass,sources);
    Require(frame.plan.opaque.size()==2 && frame.plan.opaque[1].attachment==0 && frame.stats.drawn==2 && frame.stats.draws==2 &&
      frame.stats.derived==derived && frame.stats.reused==reused && models.item_states()==2,name);
    auto constants=published->constants;
    for(auto& constant:constants) camera.pass.Apply(constant);
    std::vector<const NativeSceneInstance*> drawn;
    for(const auto& batch:frame.batches) for(const auto& object:batch.snapshot.instances) drawn.push_back(object.get());
    const auto refs=OrderNativeFullFrameModelDraws(frame.plan.opaque,true);
    Require(drawn.size()==refs.size(),name);
    for(size_t d=0;d<refs.size();++d) {
      const auto& item=frame.plan.opaque[refs[d].item];
      const auto values=NativeFullFrameModelConstantsFor(NativeFullFrameModelItemLayout(item),**NativeFullFrameModelItemPose(item).first,
        pass.palette_limit);
      const auto& material=*drawn[d]->object.material;
      NativeBackendSampler* sampler=material.samplers().at(0).second;
      const std::array<NativeBackendSampler*,2> samplers{sampler,sampler};
      const auto full=fixture.Full(*material.pipeline(),constants,values.palette,samplers,material.blend_factor());
      Require(material.Equivalent(*full.material),"each draw binds its own item's palette (model or face)");
    }
  };
  check("the model and its face draw",2,0);
  check("an unchanged frame carries both",0,2);
  auto moved=std::make_shared<std::vector<NativePoseMatrix>>(*entry->attachments[0].pose);
  (*moved)[0][0]+=1;
  entry->attachments[0].pose=moved;
  check("a moved face remakes only its own object",1,1);
}
std::string SameModelFrame(const NativeFullFrameModelFrame& a,const NativeFullFrameModelFrame& b);
// The draw states' carry rules, draw by draw: an unchanged frame and a moved
// camera carry every object (only the batch views move); a new pose pointer
// with the same values carries them; moved values remake only that item's;
// a new generation asks the providers once per pass record and batch value
// and carries the objects its sources did not change; a republished program
// with a moved constant remakes every object of its row.
void PersistentDraws(std::shared_ptr<NativeRenderBackend> backend) {
  SkinnedFixture fixture(backend);
  const auto layout=Layout(0x1000,true,3,{Mesh(0,true,false,{Batch(0x2000,{0x3000})}),Mesh(1,false,true,{Batch(0x2100,{0x3000})})});
  std::vector<std::shared_ptr<NativeRenderEntry>> entries;
  for(uint32_t i=0;i<3;++i) entries.push_back(Entry(i+1,{float(i),0,100},1,layout));
  const auto snapshot=[&] {
    NativeRenderRegistrySnapshot result;
    for(const auto& entry:entries) result.entries.push_back(entry);
    return result;
  };
  auto camera=MakeCamera();
  const auto vp=[&](float scale) { for(size_t i=0;i<16;++i) camera.pass.view_projection[i]=std::bit_cast<uint32_t>(i%5?0.f:scale); };
  vp(1.5f);
  const auto pass=fixture.Pass();
  uint64_t generation=1;
  int programs=0,geometries=0;
  auto published=std::make_shared<NativeSceneGroupMaterial>();
  published->program=fixture.program; published->constants=fixture.constants;
  NativeFullFrameModelSources sources;
  sources.program=[&](uint32_t) { ++programs; return std::shared_ptr<const NativeSceneGroupMaterial>(published); };
  sources.geometry=[&](const NativeModelBatchLayout&,uint32_t) { ++geometries; return fixture.geometry; };
  sources.generation=[&] { return generation; };
  NativeFullFrameModels models;
  const auto objects=[](const NativeFullFrameModelFrame& frame) {
    std::vector<const NativeSceneInstance*> result;
    for(const auto& batch:frame.batches) for(const auto& object:batch.snapshot.instances) result.push_back(object.get());
    return result;
  };
  const auto first=models.Build(snapshot(),camera,pass,sources);
  const auto drawn=objects(first);
  Require(drawn.size()==6 && first.stats.derived==6 && first.stats.sourced==3 && programs==1 && geometries==2,
    "the first frame makes every object, asking once per pass record and batch value");
  const auto same=models.Build(snapshot(),camera,pass,sources);
  Require(objects(same)==drawn && same.stats.reused==6 && same.stats.derived==0 && same.stats.sourced==0 &&
    same.stats.rows==1 && same.stats.camera_rows==1 && programs==1 && geometries==2,"an unchanged frame carries every object");
  vp(3.f);
  const auto moved=models.Build(snapshot(),camera,pass,sources);
  Require(objects(moved)==drawn && moved.stats.derived==0 && moved.stats.camera_rows==1 &&
    !NativeSceneCameraIdentical(moved.batches[0].view,same.batches[0].view),"a moved camera carries the objects and moves the views");
  // Entry 0: a new pose object with the same values; entry 1: moved values.
  entries[0]->pose=std::make_shared<std::vector<NativePoseMatrix>>(*entries[0]->pose);
  auto pose=std::make_shared<std::vector<NativePoseMatrix>>(*entries[1]->pose); (*pose)[1][0]+=1; entries[1]->pose=pose;
  const auto posed=models.Build(snapshot(),camera,pass,sources);
  const auto reposed=objects(posed);
  size_t kept=0;
  for(const auto* object:reposed) kept+=std::count(drawn.begin(),drawn.end(),object);
  Require(posed.stats.derived==2 && posed.stats.reused==4 && kept==4,"only the moved pose's objects are made again");
  // A new generation with the same answers: asked once per pass record and
  // batch value, every object carried.
  ++generation;
  const auto regenerated=models.Build(snapshot(),camera,pass,sources);
  Require(objects(regenerated)==reposed && regenerated.stats.sourced==3 && programs==2 && geometries==4,
    "a new generation with the same sources carries the objects");
  // A republished program with a moved constant: the row is captured again
  // and all its objects are made again.
  auto republished=std::make_shared<NativeSceneGroupMaterial>(*published);
  republished->constants[3].registers[7]^=0x10;
  published=republished; ++generation;
  const auto recaptured=models.Build(snapshot(),camera,pass,sources);
  Require(recaptured.stats.captures==1 && recaptured.stats.derived==6 && recaptured.stats.reused==0,
    "a recaptured row makes its objects again");
  // Every frame above is a fresh build's.
  NativeFullFrameModels fresh;
  const auto reason=SameModelFrame(recaptured,fresh.Build(snapshot(),camera,pass,sources));
  Require(reason.empty(),"a persistent frame differs from a fresh build");
}
// Build over the host's memo (as the bridge wires it): an unchanged frame
// asks the providers only to validate, re-sources nothing and hands every
// kept draw to the audit; a republished program advances the generation, and
// re-sourcing is served from what the validation asked, with no fetch.
void MemoDrivenBuild(std::shared_ptr<NativeRenderBackend> backend) {
  SkinnedFixture fixture(backend);
  const auto layout=Layout(0x1000,true,3,{Mesh(0,true,false,{Batch(0x2000,{0x3000})}),Mesh(1,false,true,{Batch(0x2100,{0x3000})})});
  NativeRenderRegistrySnapshot snapshot;
  for(uint32_t i=0;i<3;++i) snapshot.entries.push_back(Entry(i+1,{float(i),0,100},1,layout));
  auto camera=MakeCamera();
  for(size_t i=0;i<16;++i) camera.pass.view_projection[i]=std::bit_cast<uint32_t>(i%5?0.f:1.5f);
  const auto pass=fixture.Pass();
  auto published=std::make_shared<NativeSceneGroupMaterial>();
  published->program=fixture.program; published->constants=fixture.constants;
  using Program=std::shared_ptr<const NativeSceneGroupMaterial>;
  using Geometry=std::shared_ptr<const NativeIndexedMesh::RetainedDraw>;
  using Input=std::pair<NativeModelBatchLayout,uint32_t>;
  NativeFullFrameModelSourceMemo<int,Program,std::pair<uint32_t,uint32_t>,Input,Geometry> memo;
  int asked=0,fetched=0,audited=0,stale=0;
  const auto provide_program=[&](uint32_t) { return Program(published); };
  const auto provide_geometry=[&](const NativeModelBatchLayout&,uint32_t) { return fixture.geometry; };
  NativeFullFrameModelSources sources;
  sources.program=[&](uint32_t record) { return memo.ProgramFor(record,[&] { ++fetched; return provide_program(record); }); };
  sources.geometry=[&](const NativeModelBatchLayout& batch,uint32_t record) {
    return memo.GeometryFor({record,batch.address},Input{batch,record},[&] { ++fetched; return provide_geometry(batch,record); });
  };
  sources.generation=[&] {
    return memo.Validate([] { return 1; },[&](uint32_t record) { ++asked; return provide_program(record); },
      [&](const Input& input) { ++asked; return provide_geometry(input.first,input.second); },[](const auto& work) { work(); });
  };
  sources.audit=[&](const NativeModelBatchLayout& batch,uint32_t record,const NativeFullFrameModelSourcePair& kept) {
    ++audited; stale+=kept.first!=provide_program(record) || kept.second!=provide_geometry(batch,record);
  };
  NativeFullFrameModels models;
  const auto first=models.Build(snapshot,camera,pass,sources);
  Require(first.stats.sourced==3 && first.stats.derived==6 && fetched==3 && asked==0 && audited==0,
    "the first frame fetches one program and two geometries");
  for(int frame=0;frame<3;++frame) {
    const auto same=models.Build(snapshot,camera,pass,sources);
    Require(same.stats.sourced==0 && same.stats.reused==6 && fetched==3,"an unchanged frame re-sourced or fetched");
  }
  Require(asked==9 && audited==18 && !stale,"each unchanged frame validates three results and audits six kept draws");
  // A constant value moved (a republished program, whoever wrote it).
  auto republished=std::make_shared<NativeSceneGroupMaterial>(*published);
  republished->constants[3].registers[7]^=0x10;
  published=republished;
  const auto moved=models.Build(snapshot,camera,pass,sources);
  Require(moved.stats.sourced==3 && moved.stats.captures==1 && moved.stats.derived==6 && fetched==3 && asked==12 && audited==18,
    "a moved constant re-sources every item from the validation's answers");
  const auto after=models.Build(snapshot,camera,pass,sources);
  Require(after.stats.sourced==0 && after.stats.reused==6 && audited==24 && !stale,"the new generation holds");
}
// A gameplay-like registry for the multi-frame checks and --models-bench:
// 1600 entries on a grid around the camera, of which about 150 pass
// visibility: skinned characters with three LOD models (a palette record and
// a bone-uploading record each), rigid props (one of whose programs reads the
// world animation), a mothership with instanced sphere worlds, and filed
// (mode 1) transparents. Every second frame is a registry publication: a new
// sources generation and animation, most skinned entries publish a new pose,
// some move (crossing LOD thresholds and the frustum), one disappears and one
// appears, and now and then a group material is republished with a moved
// constant. The camera moves every frame.
struct ModelsScene {
  using Constant=NativeSceneMaterialInputs::Constant;
  static constexpr uint32_t kEntries=1600,kBones=40,kSkinnedPasses=4,kRigidPasses=3;
  std::shared_ptr<NativeRenderBackend> backend;
  std::shared_ptr<NativeSceneMaterialProgram> skinned=std::make_shared<NativeSceneMaterialProgram>(),
    rigid=std::make_shared<NativeSceneMaterialProgram>(),water=std::make_shared<NativeSceneMaterialProgram>();
  std::vector<Constant> skinned_constants,rigid_constants,water_constants;
  std::map<uint32_t,std::shared_ptr<const NativeSceneGroupMaterial>> published;  // By pass record.
  std::vector<std::shared_ptr<const NativeIndexedMesh::RetainedDraw>> skinned_geometry,rigid_geometry;
  std::vector<std::array<std::shared_ptr<const NativeModelLayout>,3>> characters;  // Three kinds, three LODs.
  std::vector<std::shared_ptr<const NativeModelLayout>> props;
  std::shared_ptr<const NativeModelLayout> sphere;
  std::vector<std::shared_ptr<const NativeRenderEntry>> entries;
  NativeFullFrameModelCamera camera=MakeCamera();
  uint64_t generation=1;
  uint32_t next_object=0,frame=0;
  static std::vector<uint8_t> Floats(size_t count,float seed) {
    std::vector<uint8_t> bytes(count*4);
    for(size_t i=0;i<count;++i) SkinnedFixture::Word(bytes,i*4,std::bit_cast<uint32_t>(seed+float(i%37)*.125f));
    return bytes;
  }
  void Compile(NativeSceneMaterialProgram& program,const char* source,const char* name) {
    Effect effect; effect.source=source;
    program.backend=backend;
    program.vertex=program.reversed_vertex=CompileNativeShader(nullptr,effect,{false,"VS","vs_3_0"},name);
    program.pixel=CompileNativeShader(nullptr,effect,{true,"PS","ps_3_0"},name);
    NativeBackendTextureDesc texture; texture.width=texture.height=1; texture.format=DXGI_FORMAT_R8G8B8A8_UNORM;
    const uint8_t white[]{255,255,255,255};
    program.inputs.textures.push_back({"image"}); program.inputs.textures.push_back({"imageSampler"});
    program.textures={backend->CreateTexture(texture,white),{}};
  }
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> Geometry(const NativeSceneMaterialProgram& program,float scale) {
    std::vector<uint8_t> declaration(12),vertices(48),indices{0,0,0,1,0,2,0,0,0,2,0,3};
    SkinnedFixture::Word(declaration,4,0x2a23b9);
    const float points[]{-.125f*scale,-.25f,.5f, -.125f*scale,.25f,.5f, .125f*scale,.25f,.5f, .125f*scale,-.25f,.5f};
    for(size_t i=0;i<12;++i) SkinnedFixture::Word(vertices,i*4,std::bit_cast<uint32_t>(points[i]));
    NativeIndexedMesh mesh(*backend,program.vertex,declaration,12,vertices,indices,2);
    return std::make_shared<const NativeIndexedMesh::RetainedDraw>(mesh.RetainDraw(backend,0,6));
  }
  void Publish(uint32_t pass,const std::shared_ptr<NativeSceneMaterialProgram>& program,std::vector<Constant> constants) {
    auto group=std::make_shared<NativeSceneGroupMaterial>();
    group->group=pass; group->program=program; group->constants=std::move(constants);
    published[pass]=group;
  }
  explicit ModelsScene(std::shared_ptr<NativeRenderBackend> with):backend(std::move(with)) {
    // The palette shares $Globals with the world and camera matrices the
    // capture zeroes and a block of material parameters, as the game's do.
    Compile(*skinned,R"(
      float4x3 g_mWorldArray[68];
      row_major float4x4 g_mWorld;
      row_major float4x4 g_mViewProjection;
      float4 g_vParams[48];
      struct V { float4 position:SV_Position; float4 color:COLOR0; };
      V VS(float3 position:POSITION0) {
        V o; int bone=int(abs(position.z*67))%68;
        float3 skinned=mul(float4(position,1),g_mWorldArray[bone]);
        o.position=mul(mul(float4(skinned,1),g_mWorld),g_mViewProjection);
        o.color=g_vParams[bone%48]; return o;
      }
      float4 tint;
      Texture2D image; SamplerState imageSampler;
      float4 PS(V v):SV_Target { return v.color*tint*image.Sample(imageSampler,float2(.5,.5)); }
    )","models-bench-skinned.fx");
    Compile(*rigid,R"(
      row_major float4x4 g_mWorld;
      row_major float4x4 g_mViewProjection;
      float4 g_vParams[16];
      struct V { float4 position:SV_Position; float4 color:COLOR0; };
      V VS(float3 position:POSITION0) {
        V o; o.position=mul(mul(float4(position,1),g_mWorld),g_mViewProjection);
        o.color=g_vParams[int(abs(position.x*15))%16]; return o;
      }
      float4 tint;
      Texture2D image; SamplerState imageSampler;
      float4 PS(V v):SV_Target { return v.color*tint*image.Sample(imageSampler,float2(.5,.5)); }
    )","models-bench-rigid.fx");
    Compile(*water,R"(
      row_major float4x4 g_mWorld;
      row_major float4x4 g_mViewProjection;
      float4 m_WaterTime;
      struct V { float4 position:SV_Position; float4 color:COLOR0; };
      V VS(float3 position:POSITION0) {
        V o; o.position=mul(mul(float4(position,1),g_mWorld),g_mViewProjection);
        o.color=m_WaterTime; return o;
      }
      float4 tint;
      Texture2D image; SamplerState imageSampler;
      float4 PS(V v):SV_Target { return v.color*tint*image.Sample(imageSampler,float2(.5,.5)); }
    )","models-bench-water.fx");
    const auto identity=SkinnedFixture::Floats({1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1});
    skinned_constants={{false,"g_mWorldArray",std::vector<uint8_t>(68*48,0xcd),true},{false,"g_mWorld",identity,true},
      {false,"g_mViewProjection",identity,true},{false,"g_vParams",Floats(48*4,.5f),true},{true,"tint",Floats(4,1),false}};
    rigid_constants={{false,"g_mWorld",identity,true},{false,"g_mViewProjection",identity,true},
      {false,"g_vParams",Floats(16*4,.25f),true},{true,"tint",Floats(4,.75f),false}};
    water_constants={{false,"g_mWorld",identity,true},{false,"g_mViewProjection",identity,true},
      {false,"m_WaterTime",std::vector<uint8_t>(16),true},{true,"tint",Floats(4,.5f),false}};
    for(uint32_t p=0;p<kSkinnedPasses;++p) {
      auto constants=skinned_constants; constants[3].registers[p*4+1]^=uint8_t(p+1);
      Publish(0x3000+p*0x10,skinned,std::move(constants));
    }
    for(uint32_t p=0;p<kRigidPasses;++p) {
      auto constants=p==2?water_constants:rigid_constants; constants[p==2?3:2].registers[5]^=uint8_t(p+1);
      Publish(0x3800+p*0x10,p==2?water:rigid,std::move(constants));
    }
    for(uint32_t g=0;g<3;++g) { skinned_geometry.push_back(Geometry(*skinned,1+g*.5f)); rigid_geometry.push_back(Geometry(*rigid,1+g*.25f)); }
    // Characters: LOD 0 has a palette record and a bone record with two
    // passes, LODs 1 and 2 lose a pass each. Batches name their geometry by
    // address (Geometry(): address bits 4-5).
    for(uint32_t kind=0;kind<3;++kind) {
      std::array<std::shared_ptr<const NativeModelLayout>,3> lods;
      for(uint32_t lod=0;lod<3;++lod) {
        const uint32_t node=0x100000+kind*0x10000+lod*0x1000,a=0x3000+((kind+lod)%kSkinnedPasses)*0x10,b=0x3000+((kind+2)%kSkinnedPasses)*0x10;
        std::vector<NativeModelMeshLayout> meshes{Mesh(0,true,false,{Batch(node+0x10*((kind+lod)%3),{a})})};
        if(lod<2) meshes.push_back(Mesh(kind%kBones,false,true,{Batch(node+0x100+0x10*(kind%3),lod?std::vector<uint32_t>{b}:std::vector<uint32_t>{b,a})}));
        lods[lod]=Layout(node,true,kBones,std::move(meshes));
      }
      characters.push_back(lods);
    }
    for(uint32_t kind=0;kind<3;++kind)
      props.push_back(Layout(0x200000+kind*0x1000,false,2,{Mesh(0,false,true,{Batch(0x200000+kind*0x1000+0x10*kind,{0x3800u+kind*0x10})}),
        Mesh(1,false,true,{Batch(0x200800+kind*0x1000,{0x3800u+((kind+1)%kRigidPasses)*0x10})})}));
    auto layout=std::make_shared<NativeModelLayout>();
    layout->instance=0x9000; layout->node=0x300000; layout->single_world=true;
    layout->meshes={Mesh(0,false,true,{Batch(0x300010,{0x3800})}),Mesh(0,true,false,{})};
    sphere=layout;
    for(uint32_t i=0;i<kEntries;++i) entries.push_back(Spawn(i));
    camera.animation=NativeScenePassAnimation{0,0};
  }
  std::shared_ptr<const NativeRenderEntry> Spawn(uint32_t slot) {
    auto entry=std::make_shared<NativeRenderEntry>();
    entry->object=0x40000000u+slot*0x1000; entry->generation=++next_object;
    // A jittered grid, so rows do not cross LOD and cull depths together.
    const float x=float(int(slot%40)*20-390)+float(slot*37%17)*.9f,z=float(int(slot/40)*20-390)+float(slot*53%19)*.85f;
    entry->centre={x,0,z,1}; entry->radius=6; entry->cull_distance=280;
    const auto kind=slot%10;
    if(kind<7) {
      // A character: LOD thresholds 90 and 180.
      entry->type=&kCharacter; entry->lod_thresholds={90,180};
      for(const auto& lod:characters[slot%3]) entry->models.push_back({lod->instance,lod});
      entry->pose=AnimatedPose(slot,0);
      if(kind==6 && slot%3==0) { entry->mode=1; entry->sort_bias=float(1+slot%5); entry->cull_distance=1e6f; entry->radius=4; }
    } else {
      entry->type=&kPlain;
      const auto& prop=props[slot%3];
      entry->models.push_back({prop->instance,prop});
      entry->pose=AnimatedPose(slot,0,2);
      if(slot==47) {
        auto worlds=std::make_shared<std::vector<NativePoseMatrix>>(8,NativePoseMatrix{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1});
        for(size_t w=0;w<worlds->size();++w) (*worlds)[w][12]=float(w*3);
        entry->instanced.push_back({{sphere->instance,sphere},worlds});
      }
    }
    return entry;
  }
  static std::shared_ptr<const std::vector<NativePoseMatrix>> AnimatedPose(uint32_t slot,uint32_t step,uint32_t bones=kBones) {
    auto pose=std::make_shared<std::vector<NativePoseMatrix>>(bones);
    for(uint32_t b=0;b<bones;++b) {
      auto& m=(*pose)[b];
      m={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
      m[12]=float(slot%40)+float(b)*.5f; m[13]=float(step%64)*.03125f+float(b); m[14]=float(slot/40);
      m[1]=float((step+b)%9)*.01f;
    }
    return pose;
  }
  NativeFullFrameModelSources Sources() {
    NativeFullFrameModelSources sources;
    sources.program=[this](uint32_t pass) { const auto found=published.find(pass); return found==published.end()?nullptr:found->second; };
    sources.geometry=[this](const NativeModelBatchLayout& batch,uint32_t pass) {
      const auto& pool=pass>=0x3800?rigid_geometry:skinned_geometry;
      return pool[(batch.address>>4)%pool.size()];
    };
    sources.generation=[this] { return generation; };
    return sources;
  }
  NativeRenderRegistrySnapshot Snapshot() const {
    NativeRenderRegistrySnapshot snapshot;
    snapshot.generation=generation;
    for(const auto& entry:entries) snapshot.entries.push_back(entry);
    return snapshot;
  }
  // The next frame's inputs. Every second frame publishes.
  void Step() {
    ++frame;
    auto& view=camera.visibility.matrix;
    view[12]=std::sin(float(frame)*.05f)*30; view[14]=std::cos(float(frame)*.03f)*25;
    for(size_t i=0;i<16;++i) camera.pass.view_projection[i]=std::bit_cast<uint32_t>(i%5?float(i)*.001f*float(frame%7):1.f+float(frame)*.01f);
    camera.pass.view=camera.pass.view_projection;
    if(frame%2) return;
    ++generation;
    const auto tick=frame/2;
    camera.animation=NativeScenePassAnimation{tick,tick/3};
    for(uint32_t slot=0;slot<entries.size();++slot) {
      const bool animated=entries[slot]->type==&kCharacter && (slot*7+tick)%10<7;
      const bool moved=(slot*13+tick*29)%97==0;
      if(!animated && !moved) continue;
      auto entry=std::make_shared<NativeRenderEntry>(*entries[slot]);
      if(animated) entry->pose=AnimatedPose(slot,tick);
      if(moved) { entry->centre[2]+=float(int(tick%5)-2)*40; entry->centre[0]+=float(int(tick%3)-1)*15; }
      entries[slot]=entry;
    }
    // One entry dies and its slot is reborn as a new object (a new generation).
    const auto dead=(tick*131)%entries.size();
    entries[dead]=Spawn(uint32_t(dead));
    // A disappearing entry: hidden for one publication, then shown again.
    for(const auto [slot,hide]:{std::pair{((tick-1)*71)%entries.size(),false},std::pair{(tick*71)%entries.size(),true}}) {
      auto toggled=std::make_shared<NativeRenderEntry>(*entries[slot]); toggled->hidden=hide; entries[slot]=toggled;
    }
    // Now and then a group material republishes with a moved constant (and,
    // at other times, with equal constants: a new object, the same values).
    if(tick%8==3) {
      auto group=std::make_shared<NativeSceneGroupMaterial>(*published[0x3000+(tick%kSkinnedPasses)*0x10]);
      group->constants[3].registers[9]=uint8_t(tick); published[group->group]=group;
    }
    if(tick%8==6) published[0x3810]=std::make_shared<NativeSceneGroupMaterial>(*published[0x3810]);
  }
  NativeFullFrameModelPass Pass() const {
    NativeFullFrameModelPass pass;
    pass.targets.rtv_format[0]=DXGI_FORMAT_R8G8B8A8_UNORM; pass.targets.dsv_format=DXGI_FORMAT_D32_FLOAT;
    pass.viewport={0,0,1280,720,0,1}; pass.scissor={0,0,1280,720};
    return pass;
  }
};
// Whether two builds of the same inputs made the same frame: the same plan,
// the same batches (view bit for bit, transparency, key, filing order) with
// the same draws in the same order: geometry, pipeline, an equivalent material
// (every constant byte, texture, sampler and blend factor), world and previous
// bit for bit, and the same material sharing between neighbours (what the
// renderer instances). Instance ids are lifetime tokens and not compared.
std::string SameModelFrame(const NativeFullFrameModelFrame& a,const NativeFullFrameModelFrame& b) {
  const auto same_bits=[](const auto& x,const auto& y) { return !std::memcmp(&x,&y,sizeof(x)); };
  const auto same_items=[&](const std::vector<NativeFullFrameModelItem>& x,const std::vector<NativeFullFrameModelItem>& y) {
    if(x.size()!=y.size()) return false;
    for(size_t i=0;i<x.size();++i)
      if(x[i].entry!=y[i].entry || x[i].model!=y[i].model || !same_bits(x[i].depth,y[i].depth) || !same_bits(x[i].view_z,y[i].view_z) ||
         x[i].key!=y[i].key || x[i].transparent!=y[i].transparent || x[i].instanced!=y[i].instanced || x[i].world!=y[i].world) return false;
    return true;
  };
  if(!same_items(a.plan.opaque,b.plan.opaque) || !same_items(a.plan.transparent,b.plan.transparent)) return "plan";
  if(a.stats.items!=b.stats.items || a.stats.drawn!=b.stats.drawn || a.stats.draws!=b.stats.draws) return "draw counts";
  if(a.batches.size()!=b.batches.size()) return "batch count";
  for(size_t i=0;i<a.batches.size();++i) {
    const auto& x=a.batches[i]; const auto& y=b.batches[i];
    if(x.transparent!=y.transparent || x.key!=y.key || x.order!=y.order || x.snapshot.tick!=y.snapshot.tick) return "batch route";
    if(!NativeSceneCameraIdentical(x.view,y.view) || !same_bits(x.view.viewport,y.view.viewport) ||
       !same_bits(x.view.scissor,y.view.scissor) || x.view.scissor_enabled!=y.view.scissor_enabled) return "batch view";
    if(x.snapshot.instances.size()!=y.snapshot.instances.size()) return "batch draw count";
    for(size_t d=0;d<x.snapshot.instances.size();++d) {
      const auto& p=*x.snapshot.instances[d]; const auto& q=*y.snapshot.instances[d];
      if(p.object.geometry!=q.object.geometry || !p.object.material || !q.object.material ||
         p.object.material->pipeline()!=q.object.material->pipeline() || !p.object.material->Equivalent(*q.object.material)) return "draw material";
      if(!same_bits(p.object.world,q.object.world) || !same_bits(p.previous,q.previous) || p.changed_tick!=q.changed_tick ||
         p.object.visible!=q.object.visible || p.object.order!=q.object.order || p.object.bounds!=q.object.bounds) return "draw world";
      if(d && (x.snapshot.instances[d-1]->object.material==p.object.material)!=(y.snapshot.instances[d-1]->object.material==q.object.material))
        return "material sharing";
    }
  }
  return {};
}
bool models_bench=false;
// NativeFullFrameModels across gameplay-like frames (ModelsScene): every
// frame of one persistent instance equals a fresh instance's build of the
// same inputs, through moves, LOD changes, appearing and disappearing
// entries, camera moves, new poses, new generations and republished
// materials. With --models-bench it runs 600 frames, checks every 50th and
// prints the persistent build's mean and worst per-frame times.
void ModelFrames(std::shared_ptr<NativeRenderBackend> backend) {
  ModelsScene scene(backend);
  const auto sources=scene.Sources();
  auto pass=scene.Pass();
  NativeFullFrameModels models;
  const bool bench=models_bench;
  const uint32_t frames=bench?600:40;
  double total_ms=0,publication_ms=0,worst_ms=0;
  std::array<double,3> stages_ms{};  // Visibility, programs, resolve.
  uint64_t draws=0,items=0,publications=0;
  NativeFullFrameModelFrame::Stats totals{};
  for(uint32_t frame=0;frame<frames;++frame) {
    scene.Step();
    // Pass inputs move too, for a while: the filtering (every row resolves
    // again) and the palette clamp (every skinned constant set is repacked).
    // Not in the bench, which measures gameplay.
    const bool filtered=!bench && frame>=17 && frame<21,clamped=!bench && frame>=25 && frame<31;
    const bool pass_moved=filtered!=(pass.filtering==4) || clamped!=(pass.palette_limit==32);
    pass.filtering=filtered?4:-1;
    pass.palette_limit=clamped?32:kNativeBonePaletteShaderBones;
    const auto snapshot=scene.Snapshot();
    // Build's stages as the host times them (frame.native.models.*).
    std::array<double,3> stage{};
    auto mark=std::chrono::steady_clock::now();
    int current=-1;
    const auto timed=[&](NativeFullFrameModelPhase next) {
      const auto now=std::chrono::steady_clock::now();
      if(current>=0) stage[size_t(current)]+=std::chrono::duration<double,std::milli>(now-mark).count();
      mark=now; current=next==NativeFullFrameModelPhase::Done?-1:int(next);
    };
    const auto t0=std::chrono::steady_clock::now();
    const auto built=models.Build(snapshot,scene.camera,pass,sources,timed);
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
    if(frame>=4) {
      for(size_t i=0;i<stage.size();++i) stages_ms[i]+=stage[i];
      total_ms+=ms; worst_ms=(std::max)(worst_ms,ms);
      if(scene.frame%2==0) { publication_ms+=ms; ++publications; }
    }
    const auto& s=built.stats;
    draws+=s.draws; items+=s.items;
    totals.resolves+=s.resolves; totals.captures+=s.captures; totals.palettes+=s.palettes; totals.cache_hits+=s.cache_hits;
    totals.reused+=s.reused; totals.derived+=s.derived; totals.sourced+=s.sourced; totals.programs+=s.programs;
    totals.geometries+=s.geometries; totals.rows+=s.rows; totals.camera_rows+=s.camera_rows;
    Require(s.failed==0 && s.missing_program==0 && s.missing_geometry==0 && s.palette==0,"a models bench draw failed");
    Require(built.plan.opaque.size()>80 && s.draws>100 && !built.plan.transparent.empty(),"the models bench scene drew too little");
    Require(s.reused+s.derived==s.draws,"every drawn object is carried or made");
    if(frame>=2 && !pass_moved) {
      if(scene.frame%2) {
        // Between publications only the camera moved: only items the moved
        // camera brought into view or to another LOD are gathered (the
        // providers asked only for batch values new this generation), almost
        // every object is carried and every row takes the camera in place.
        Require(s.sourced*8<=s.items && (s.sourced || (s.programs==0 && s.geometries==0)) && s.reused*10>=s.draws*9 &&
          s.camera_rows==s.rows,
          "a camera-only models frame rebuilt what did not move");
      } else {
        // A publication: every item is gathered again at the new generation,
        // the providers answer once per pass record and batch value, and the
        // carried objects are those of entries whose poses did not move.
        Require(s.sourced==s.items && s.programs<=ModelsScene::kSkinnedPasses+ModelsScene::kRigidPasses &&
          s.geometries<=24 && s.reused*5>=s.draws,"a publication frame rebuilt what did not move");
      }
    }
    if(bench && frame%50) continue;
    NativeFullFrameModels fresh;
    const auto reason=SameModelFrame(built,fresh.Build(snapshot,scene.camera,pass,sources));
    if(!reason.empty()) throw std::runtime_error("a persistent models frame differs from a fresh build: "+reason+" (frame "+std::to_string(frame)+")");
  }
  if(bench) {
    const auto measured=double(frames-4);
    std::cout<<"models frames: build_ms="<<total_ms/measured<<" publication_ms="<<publication_ms/double(publications)
      <<" other_ms="<<(total_ms-publication_ms)/(measured-double(publications))<<" worst_ms="<<worst_ms
      <<" visibility_ms="<<stages_ms[0]/measured<<" programs_ms="<<stages_ms[1]/measured<<" resolve_ms="<<stages_ms[2]/measured
      <<" items="<<items/frames<<" draws="<<draws/frames<<" resolves="<<double(totals.resolves)/frames
      <<" captures="<<double(totals.captures)/frames<<" palettes="<<double(totals.palettes)/frames
      <<" cache_hits="<<double(totals.cache_hits)/frames<<" reused="<<double(totals.reused)/frames
      <<" derived="<<double(totals.derived)/frames<<" sourced="<<double(totals.sourced)/frames
      <<" programs="<<double(totals.programs)/frames<<" geometries="<<double(totals.geometries)/frames
      <<" rows="<<double(totals.rows)/frames<<" camera_rows="<<double(totals.camera_rows)/frames<<"\n";
  }
}
}
// clSky's registry row says another pass draws it (the sky pass): an entry of
// such a class is never planned, even visible, posed and in either route.
void OtherPassClasses() {
  const NativeRenderClass sky{.vtable=0x8200284Cu,.name="clSky",.cadence=NativeRenderPoseCadence::Frame,
    .lod=NativeRenderLodKind::None,.instance=412,.pose=384,.other_pass=true};
  NativeRenderRegistrySnapshot snapshot;
  auto opaque=Entry(1,{0,0,100}); opaque->type=&sky; snapshot.entries.push_back(opaque);
  auto filed=Entry(2,{0,0,1000}); filed->type=&sky; filed->mode=1; filed->sort_bias=1; snapshot.entries.push_back(filed);
  snapshot.entries.push_back(Entry(3,{0,0,100}));
  const auto plan=PlanNativeFullFrameModels(snapshot,MakeCamera());
  Require(plan.stats.entries==3 && plan.stats.other_pass==2 && plan.opaque.size()==1 && plan.opaque[0].entry->object==3 &&
    plan.transparent.empty(),"a class drawn by another pass was planned by the models pass");
}
int main(int argc,char** argv) {
  // --models-bench: ModelFrames runs 600 frames on D3D11 and prints its per-frame timings.
  models_bench=argc>1 && std::string_view(argv[1])=="--models-bench";
  try {
    if(models_bench) {
      ModelFrames(std::shared_ptr<NativeRenderBackend>(CreateNativeD3D11Backend({true,false})));
      return 0;
    }
    Visibility(); Lod(); Constants(); SortKeys(); Instancing(); InstancedWorlds(); BaseState();
    SourceTable(); SourceMemo(); MaterialCache(); RigidInstancing(); InstancedCache(); BrokenObjects(); OtherPassClasses();
    AttachmentsFollowTheModel(); PoseSource();
    // The skinned material path against full captures, on both backends (WARP).
    for(int backend=0;backend<2;++backend) {
      NativeD3D12Options options; options.prefer_warp=true;
      const std::shared_ptr<NativeRenderBackend> device=backend?std::shared_ptr<NativeRenderBackend>(CreateNativeD3D12Backend(options)):
        std::shared_ptr<NativeRenderBackend>(CreateNativeD3D11Backend({true,false}));
      PaletteCapture(device); SkinnedBuild(device); AttachmentBuild(device); PersistentDraws(device); MemoDrivenBuild(device); ModelFrames(device);
    }
  } catch(const std::exception& error) {
    std::cerr<<"FAILED: "<<error.what()<<"\n";
    return 1;
  }
  std::cout<<"native full-frame models tests passed\n";
  return 0;
}

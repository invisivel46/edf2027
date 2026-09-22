#include "native_graphics/native_full_frame_models.h"
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
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
void BaseState() {
  NativeFullFrameModelTargets targets; targets.dsv_format=1;
  const auto state=NativeFullFrameModelBaseState(targets);
  Require(state.render.depth_requested==1 && (state.render.words[1]&2),"base state enables depth test");
  Require(state.render.blend_control==0 || !(state.render.blend_control&0x80000000u),"base state disables blending");
  targets.count=5;
  bool rejected=false;
  try { NativeFullFrameModelBaseState(targets); } catch(const std::exception&) { rejected=true; }
  Require(rejected,"more than four color targets are refused");
}
}
int main() {
  try {
    Visibility(); Lod(); Constants(); SortKeys(); Instancing(); BaseState();
  } catch(const std::exception& error) {
    std::cerr<<"FAILED: "<<error.what()<<"\n";
    return 1;
  }
  std::cout<<"native full-frame models tests passed\n";
  return 0;
}

#include "native_graphics/native_full_frame_models.h"
#include <array>
#include <bit>
#include <cmath>
#include <algorithm>
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
int main() {
  try {
    Visibility(); Lod(); Constants(); SortKeys(); Instancing(); InstancedWorlds(); BaseState();
    SourceTable(); MaterialCache(); RigidInstancing(); InstancedCache(); OtherPassClasses();
  } catch(const std::exception& error) {
    std::cerr<<"FAILED: "<<error.what()<<"\n";
    return 1;
  }
  std::cout<<"native full-frame models tests passed\n";
  return 0;
}

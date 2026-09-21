#include "native_graphics/native_scene.h"
#include "native_graphics/native_scene_bindings.h"
#include "native_graphics/native_scene_sources.h"
#include "native_graphics/native_scene_adapter.h"
#include "native_graphics/native_queued_scene.h"
#include "native_graphics/native_scene_cpu_window.h"
#include "native_graphics/native_scene_membership.h"
#include "native_graphics/native_scene_geometry.h"
#include "native_graphics/d3d11_backend.h"
#include "native_graphics/d3d12_backend.h"
#include <bit>
#include <cstring>
#include <iostream>

using namespace edf::native;
namespace {
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
template<class F> void Reject(F f) { bool rejected=false; try { f(); } catch(const std::exception&) { rejected=true; } Require(rejected,"invalid scene operation accepted"); }
void Word(std::vector<uint8_t>& bytes,size_t offset,uint32_t word) {
  for(size_t i=0;i<4;++i) bytes[offset+i]=uint8_t(word>>(24-i*8));
}
void Visibility() {
  NativeSceneMembership membership;
  membership.Born(100); membership.Born(200);
  Require(membership.InsertAfter(100,1000,10) && membership.InsertAfter(100,2000,20) &&
    membership.InsertAfter(1000,3000,30),"membership insertion failed");
  membership.Publish();
  const auto original=membership.Acquire(100);
  Require(original->members==std::vector<NativeSceneMembership::Member>{{2000,20},{1000,10},{3000,30}},
    "native membership changed insertion order");
  Require(membership.Acquire(100)==original,"unchanged membership publication was copied");
  membership.InsertAfter(200,1000,10);
  Require(membership.Acquire(100)->members==std::vector<NativeSceneMembership::Member>{{2000,20},{3000,30}} &&
    membership.Acquire(200)->members==std::vector<NativeSceneMembership::Member>{{1000,10}},"cross-list move retained old membership");
  Require(original->members.size()==3,"membership update mutated a published list");
  membership.Remove(2000);
  Require(!membership.InsertAfter(9999,3000,30) && membership.Acquire(100)->members.empty(),
    "move to untracked list retained an old native node");
  membership.Remove(200);
  Require(!membership.Acquire(200) && membership.nodes()==0,"list-header retirement leaked members");
  membership.Born(100);
  Require(membership.Acquire(100)->generation!=original->generation && membership.Acquire(100)->members.empty(),
    "recycled list address inherited old membership");
  struct Memory {
    mutable size_t reads=0,writes=0;
    mutable std::array<uint8_t,12288> bytes{};
    bool writable=true;
    uint32_t Add(uint32_t a,uint32_t b) const { return a+b; }
    const uint8_t* Bytes(uint32_t at,size_t size) const {
      ++reads; if(at>bytes.size() || size>bytes.size()-at) throw std::runtime_error("range");
      return bytes.data()+at;
    }
    const uint8_t* WritableBytes(uint32_t at,size_t size,size_t alignment) const {
      ++writes;
      if(!writable || at%alignment) throw std::runtime_error("protection");
      return Bytes(at,size);
    }
  } memory;
  NativeSceneCpuWindow window(memory);
  Require(window.Word(4096)==0 && window.Word(4100)==0 && memory.reads==1,"CPU page admission was not reused");
  memory.bytes[4103]=17;
  Require(window.Word(4100)==17,"CPU window cached contents instead of access");
  window.WritableBytes(4100,4,4);
  Require(memory.writes==1,"read-only page was not upgraded through validation");
  window.WritableBytes(4104,4,4);
  Require(memory.writes==1,"writable page was revalidated for each store");
  memory.writable=false; window.Invalidate();
  Reject([&] { window.WritableBytes(4100,4,4); });
  memory.writable=true;
  const auto before_reads=memory.reads;
  window.Bytes(8190,4);
  Require(memory.reads==before_reads+1,"cross-page access bypassed the backing validator");
  Reject([&] { window.Bytes(12287,2); });
  NativeSceneVisibilityView view;
  view.matrix=kNativeSceneIdentity; view.depth_scale=-1;
  const float n=std::sqrt(.5f);
  view.frustum[8]=n; view.frustum[10]=-n;
  view.frustum[12]=-n; view.frustum[14]=-n;
  view.frustum[17]=n; view.frustum[18]=-n;
  view.frustum[21]=-n; view.frustum[22]=-n;
  view.frustum[24]=1; view.frustum[25]=100;
  Require(NativeVisibilitySphere(view,{0,0,10,1},1)==1,"interior sphere culled");
  Require(NativeVisibilitySphere(view,{0,0,-1,1},1)==0,"near sphere accepted");
  Require(NativeVisibilitySphere(view,{0,0,101,1},2)==2,"far sphere intersection lost");
  Require(NativeVisibilitySphere(view,{20,0,10,1},1)==0,"side sphere accepted");
  Require(NativeVisibilitySphere(view,{0,0,1,1},0)==1,"near boundary was exclusive");
  NativeSceneVisibility object;
  object.box={0,0,10,1, 1,0,0,0, 0,1,0,0, 0,0,1,0};
  Require(NativeVisibilityBox(view,object.box)==1,"interior box culled");
  object.box[0]=30;
  Require(NativeVisibilityBox(view,object.box)==0,"outside box accepted");
  object.box[0]=10;
  Require(NativeVisibilityBox(view,object.box)==2,"intersecting box culled");
  object.box={0,0,50,1, 200,0,0,0, 0,200,0,0, 0,0,100,0};
  Require(NativeVisibilityBox(view,object.box)==2,"frustum-enclosing box culled because no corner was inside");
  object.lod_count=3; object.lod_thresholds={20,40};
  Require(NativeVisibilityLod(object,20)==0 && NativeVisibilityLod(object,21)==1 &&
    NativeVisibilityLod(object,40)==1 && NativeVisibilityLod(object,41)==2,"LOD threshold boundaries changed");
  const std::array<float,16> transform{1,2,3,0, 4,5,6,0, 7,8,9,0, 10,11,12,1};
  Require(NativeVisibilityTransform({2,3,4,1},transform)==std::array<float,4>{52,62,72,1},
    "visibility center matrix convention changed");
  NativeSceneSources sources; sources.Born(100);
  Require(sources.PublishVisibility(100,object),"visibility source not registered");
  const auto old=sources.Visibility(100);
  sources.PublishVisibility(100,object);
  Require(sources.Visibility(100)==old,"unchanged bound was recopied");
  object.distance=250; sources.PublishVisibility(100,object);
  Require(old->distance==0 && sources.Visibility(100)->distance==250,"bound update mutated retained state");
  sources.Retire(100); sources.Born(100);
  Require(!sources.Visibility(100) && old->lod_count==3,"recycled owner inherited old visibility");
}
void QueuedGuestState() {
  for(bool column:{false,true}) {
    std::array<uint8_t,64> raw{};
    std::array<float,16> expected{};
    for(size_t row=0;row<4;++row) for(size_t col=0;col<4;++col) {
      const float value=float(row*4+col)-8.5f; expected[row*4+col]=value;
      const auto bits=std::bit_cast<uint32_t>(value);
      const auto offset=(column?col*4+row:row*4+col)*4;
      for(size_t b=0;b<4;++b) raw[offset+b]=uint8_t(bits>>(24-b*8));
    }
    Require(DecodeNativeQueuedWorld(raw,column)==expected,"direct world register matrix layout");
  }
  struct Reader {
    mutable std::vector<uint8_t> bytes=std::vector<uint8_t>(32768);
    uint32_t Add(uint32_t a,uint32_t b) const { if(b>UINT32_MAX-a) throw std::runtime_error("overflow"); return a+b; }
    const uint8_t* Bytes(uint32_t at,size_t size) const {
      if(!at || at>bytes.size() || size>bytes.size()-at) throw std::runtime_error("range");
      return bytes.data()+at;
    }
    const uint8_t* WritableBytes(uint32_t at,size_t size,size_t alignment) const {
      if(at%alignment) throw std::runtime_error("alignment"); return Bytes(at,size);
    }
    uint32_t Word(uint32_t at) const { return GuestBlockWord(Bytes(at,4)); }
    void StoreWord(uint32_t at,uint32_t value) const { ::Word(bytes,at,value); }
  } reader;
  constexpr uint32_t device=1024,bank=device+1792;
  for(size_t i=0;i<256;++i) reader.bytes[16384+i]=uint8_t(i+1);
  const InstanceParameter patches[]{{16384,3,4},{16512,5,2},{16384,255,1}};
  auto expected=reader.bytes;
  // Retail upload copies bytes in list order. The native indexed CPU prefix
  // consumes the accumulated vertex dirty mask, leaving all five masks zero.
  for(const auto& p:patches) std::memcpy(expected.data()+bank+p.first*16,expected.data()+p.data,p.count*16);
  Require(NativeQueuedConstantsConsumed(reader,device,patches) && reader.bytes==expected,
    "native queued constants differ from ordered retail upload/consume effects");
  for(size_t offset=0;offset<40;++offset) {
    reader.bytes[device+offset]=1; const auto before=reader.bytes;
    Require(!NativeQueuedConstantsConsumed(reader,device,patches) && before==reader.bytes,"dirty state was bypassed");
    reader.bytes[device+offset]=0;
  }
  for(const auto bad:{InstanceParameter{bank,0,1},InstanceParameter{16384,256,1},InstanceParameter{16384,0,0}}) {
    const auto before=reader.bytes;
    Require(!NativeQueuedConstantsConsumed(reader,device,std::span(&bad,1)) && before==reader.bytes,"unsafe override changed state");
  }
  const InstanceParameter invalid[]{{16384,0,1},{32760,1,1}};
  const auto before=reader.bytes;
  Reject([&] { NativeQueuedConstantsConsumed(reader,device,invalid); });
  Require(before==reader.bytes,"invalid override partially wrote guest bank");
  constexpr uint32_t group=64,first=128,second=160,last=192,sentinel=224;
  reader.StoreWord(group+4,first); reader.StoreWord(group+8,sentinel);
  reader.StoreWord(first,1000); reader.StoreWord(first+4,second);
  reader.StoreWord(second,2000); reader.StoreWord(second+4,sentinel);
  reader.StoreWord(last,3000); reader.StoreWord(last+4,sentinel);
  std::vector<uint32_t> order;
  VisitNativeQueuedScene(reader,group,[&] { order.push_back(0); reader.StoreWord(group+4,sentinel); },[&](uint32_t instance) {
    order.push_back(instance); if(instance==1000) reader.StoreWord(first+4,last);
  });
  Require(order==std::vector<uint32_t>{0,1000,3000} && reader.Word(group+4)==0,
    "native queue changed callback order, initial selection, link mutation or final clear");
  reader.StoreWord(group+4,sentinel);
  VisitNativeQueuedScene(reader,group,[] { throw std::runtime_error("empty queue setup"); },[](uint32_t) { throw std::runtime_error("empty draw"); });
  Require(reader.Word(group+4)==sentinel,"empty queue was cleared");
  constexpr uint32_t other_group=96;
  reader.StoreWord(other_group+4,sentinel); reader.StoreWord(other_group+8,sentinel);
  NativeSceneQueues queues;
  const auto no_links=reader.bytes;
  queues.Push(group,first); queues.Push(other_group,second); queues.Push(group,last);
  Require(queues.Take(group)==std::vector<uint32_t>{last,first} &&
    queues.Take(other_group)==std::vector<uint32_t>{second} && queues.empty() && reader.bytes==no_links,
    "native queues changed order, crossed groups or wrote guest links");
  queues.Push(group,first); queues.Push(other_group,second); queues.Push(group,last);
  queues.Materialize(reader);
  Require(!queues.enabled && queues.empty() && reader.Word(group+4)==last &&
    reader.Word(last+4)==first && reader.Word(first+4)==sentinel &&
    reader.Word(other_group+4)==second && reader.Word(second+4)==sentinel,
    "native queue fallback did not restore original head-insertion order");
  Reject([&] { queues.Push(group,first); });
  constexpr uint32_t owner=8192;
  NativeSceneSources sources;
  Require(!sources.HasOwner(owner),"unconstructed static owner exists");
  sources.Born(owner);
  reader.StoreWord(owner+404,2);
  reader.StoreWord(owner+412,17000); reader.StoreWord(owner+416,17056);
  reader.StoreWord(owner+456,18000); reader.StoreWord(owner+460,18028);
  auto parts=ReadNativeStaticSceneParts(reader,owner);
  Require(parts==std::vector<NativeSceneSources::Part>{{17000,0,0},{17028,0,1},{18000,1,0}},
    "static source event did not enumerate all LOD parts");
  sources.Observe(owner,parts);
  Require(sources.LodParts(owner+408)->size()==2 && sources.LodParts(owner+452)->size()==1 &&
    !sources.LodParts(30000),"native LOD selection did not resolve its registered parts");
  reader.StoreWord(owner+436,20000); reader.StoreWord(20004,21000);
  sources.Observe(owner,ReadNativeStaticSceneParts(reader,owner));
  const auto source=*sources.Find(17000);
  for(size_t i=0;i<16;++i) reader.StoreWord(owner+224+uint32_t(i)*4,std::bit_cast<uint32_t>(float(i)+.25f));
  auto world=ReadNativeStaticWorld(reader,owner);
  for(size_t row=0;row<4;++row) for(size_t col=0;col<4;++col)
    Require(GuestBlockWord(world.data()+(col*4+row)*4)==reader.Word(owner+224+uint32_t(row*4+col)*4),
      "static event did not reproduce guest world-register transpose");
  Require(!sources.WorldRegisters(source,21000),"world existed before publication");
  Require(sources.PublishWorld(owner,world),"live world publication rejected");
  const auto original_world=sources.WorldRegisters(source,21000);
  Require(original_world && !sources.WorldRegisters(source,21016),"world storage identity was ignored");
  sources.PublishWorld(owner,world);
  Require(sources.WorldRegisters(source,21000)==original_world,"unchanged world was reallocated");
  world[3]^=1; sources.PublishWorld(owner,world);
  Require(*original_world!=*sources.WorldRegisters(source,21000),"world publication mutated an older retained value");
  reader.StoreWord(owner+404,1);
  reader.StoreWord(owner+412,19000); reader.StoreWord(owner+416,19028);
  sources.Observe(owner,ReadNativeStaticSceneParts(reader,owner));
  Require(!sources.Find(17000) && !sources.Find(18000) && sources.Find(19000),
    "model replacement retained removed/relocated LOD records");
  reader.StoreWord(owner+416,19029);
  Reject([&] { ReadNativeStaticSceneParts(reader,owner); });
  Require(sources.Find(19000),"invalid model publication changed previous source catalog");
  reader.StoreWord(owner+404,4);
  Reject([&] { ReadNativeStaticSceneParts(reader,owner); });
  reader.StoreWord(owner+404,0);
  sources.Observe(owner,ReadNativeStaticSceneParts(reader,owner));
  Require(sources.HasOwner(owner) && !sources.Find(19000),"empty model did not clear parts while preserving lifetime");
  sources.Retire(owner); Require(!sources.HasOwner(owner),"static source survived retirement");
  Require(!sources.LodParts(owner+408),"retired native LOD still accepted queue selections");
  Require(!sources.WorldRegisters(source,21000) && !sources.PublishWorld(owner,world),"retired source accepted a world publication");
  sources.Born(owner); sources.PublishWorld(owner,world);
  Require(!sources.WorldRegisters(source,21000),"reused owner address accepted an old world generation");
  // These addresses describe an entirely unselected group: no device or draw
  // state is present, and decoding is read-only.
  reader.StoreWord(26012,27000);
  reader.StoreWord(27072,28000); reader.StoreWord(27076,28100);
  reader.StoreWord(28004,28200); reader.StoreWord(28128,28300);
  reader.StoreWord(27060,32); reader.StoreWord(27140,8);
  reader.StoreWord(27000,28400); reader.StoreWord(28416,29000);
  reader.StoreWord(29108,29200); reader.StoreWord(29200,29300); reader.StoreWord(29300,29400);
  const auto untouched=reader.bytes;
  Require(ReadNativeSceneGeometrySource(reader,26000)==NativeSceneGeometrySource{27004,27084,28300,32,6,29000,29400} &&
    untouched==reader.bytes,"native geometry descriptor changed resource roles, triangle count or guest state");
  reader.StoreWord(27076,28200); Reject([&] { ReadNativeSceneGeometrySource(reader,26000); });
  reader.StoreWord(27076,28100); reader.StoreWord(27140,0xffffffff);
  Reject([&] { ReadNativeSceneGeometrySource(reader,26000); });
  reader.StoreWord(27140,6); reader.StoreWord(27060,3);
  Reject([&] { ReadNativeSceneGeometrySource(reader,26000); });
  NativeMaterialParameters::Groups material_schema;
  material_schema[0].push_back({"local",31000,1,0});
  material_schema[1].push_back({"global",31500,1,4});
  material_schema[2].push_back({"unused",UINT32_MAX,1,0});
  reader.StoreWord(31004,31200); reader.StoreWord(31500,31400);
  reader.StoreWord(31400,31300); reader.StoreWord(31408,1);
  reader.StoreWord(31200,0x3f800000); reader.StoreWord(31300,0x40000000);
  reader.StoreWord(29204,29440); reader.StoreWord(29444,29500);
  material_schema.textures[0].push_back({"local_image",30000});
  material_schema.textures[1].push_back({"global_image",30040});
  material_schema.textures[1].push_back({"unused_image",UINT32_MAX});
  reader.StoreWord(30004,30300); reader.StoreWord(30008,2);
  reader.StoreWord(30040,30100); reader.StoreWord(30044,3); reader.StoreWord(30128,30400);
  for(uint32_t i=0;i<4;++i) { reader.StoreWord(30012+i*4,i+1); reader.StoreWord(30132+i*4,i+5); }
  reader.StoreWord(30344,0x3c0); reader.StoreWord(30444,0x3c4);
  reader.StoreWord(29096,30600); reader.StoreWord(29104,2);
  reader.StoreWord(30600,0x44); reader.StoreWord(30604,7);
  reader.StoreWord(30608,0x48); reader.StoreWord(30612,8);
  const auto load_material=[&] {
    return ReadNativeSceneMaterialInputs(reader,29000,material_schema,
      [](bool,const std::string& name) { return name=="unused"?size_t(0):size_t(16); },
      [](const std::string& name) { return name!="unused_image"; });
  };
  const auto material_memory=reader.bytes;
  const auto material_inputs=load_material();
  Require(material_inputs.vertex==29400 && material_inputs.pixel==29500 && material_inputs.constants.size()==2 &&
    !material_inputs.constants[0].global && material_inputs.constants[1].global &&
    GuestBlockWord(material_inputs.constants[1].registers.data())==0x40000000 && reader.bytes==material_memory,
    "material input publication read wrong values or changed guest state");
  Require(material_inputs.textures.size()==2 && material_inputs.textures[0].settings==std::array<uint32_t,4>{1,2,3,4} &&
    !material_inputs.textures[0].global && material_inputs.textures[1].global &&
    material_inputs.textures[1].settings==std::array<uint32_t,4>{5,6,7,8} && material_inputs.textures[1].lod_range==0x3c4 &&
    material_inputs.state_overrides==std::vector<std::array<uint32_t,2>>{{0x44,7},{0x48,8}},
    "material input publication lost sampler or ordered state overrides");
  reader.StoreWord(31300,0); reader.StoreWord(30132,99);
  Require(load_material()!=material_inputs && GuestBlockWord(material_inputs.constants[1].registers.data())==0x40000000,
    "material input snapshot borrowed source values");
  reader.StoreWord(31408,0); Reject(load_material); reader.StoreWord(31408,1);
  reader.StoreWord(30044,16); Reject(load_material); reader.StoreWord(30044,3);
  reader.StoreWord(29104,4097); Reject(load_material);
}
std::span<const uint8_t> Bytes(ID3DBlob* code) {
  return {static_cast<const uint8_t*>(code->GetBufferPointer()),code->GetBufferSize()};
}
void Run(std::shared_ptr<NativeRenderBackend> backend,bool column_major) {
  Effect effect;
  effect.source=std::string(column_major?"column_major":"row_major")+R"( float4x4 g_mWorld : WORLD;
    row_major float4x4 g_mViewProjection;
    row_major float4x4 g_mViewTranspose;
    struct V { float4 position:SV_Position; };
    V VS(float3 position:POSITION0) { V o; o.position=mul(mul(float4(position,1),g_mWorld),g_mViewProjection); o.position.x*=g_mViewTranspose._11; return o; }
    float4 tint;
    Texture2D image; SamplerState imageSampler;
    float4 PS(V v):SV_Target { return tint*image.Sample(imageSampler,float2(.5,.5)); }
  )";
  auto vs=CompileNativeShader(nullptr,effect,{false,"VS","vs_3_0"},"native-scene-test.fx");
  auto ps=CompileNativeShader(nullptr,effect,{true,"PS","ps_3_0"},"native-scene-test.fx");
  Require(AddNativeWorldInstancing(vs,effect,"native-scene-test.fx"),"scene test instance shader");
  std::vector<uint8_t> declaration(12),vertices(48),indices{0,0,0,1,0,2,0,0,0,2,0,3};
  Word(declaration,4,0x2a23b9);
  const float points[]{-.125f,-.25f,.5f, -.125f,.25f,.5f, .125f,.25f,.5f, .125f,-.25f,.5f};
  for(size_t i=0;i<12;++i) Word(vertices,i*4,std::bit_cast<uint32_t>(points[i]));
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  {
    NativeIndexedMesh mesh(*backend,vs,declaration,12,vertices,indices,2);
    geometry=std::make_shared<const NativeIndexedMesh::RetainedDraw>(mesh.RetainDraw(backend,0,6));
  }
  NativeBackendPipelineDesc desc;
  desc.vertex=Bytes(vs.bytecode.Get()); desc.pixel=Bytes(ps.bytecode.Get());
  desc.vertex_id=100+uint64_t(column_major); desc.pixel_id=102;
  desc.state={0x10001,0,0,0,15,0}; desc.render_targets=1; desc.rtv_format[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.input_layout=geometry->input_layout().elements(); desc.input_layout_id=geometry->input_layout().fingerprint();
  auto& pipeline=backend->CreatePipeline(desc);
  NativeOwnedInputLayout instanced;
  for(const auto& e:desc.input_layout) instanced.Add(e.semantic,e.semantic_index,e.format,e.slot,e.offset,e.per_instance,e.step_rate);
  for(uint32_t row=0;row<4;++row) instanced.Add("EDFINSTANCE",row,DXGI_FORMAT_R32G32B32A32_FLOAT,15,row*16,true,1);
  desc.vertex=Bytes(vs.instanced_bytecode.Get()); desc.vertex_id|=uint64_t(1)<<63;
  desc.input_layout=instanced.elements(); desc.input_layout_id=instanced.fingerprint();
  pipeline.world_instanced=&backend->CreatePipeline(desc);
  pipeline.instance_world_slot=vs.instance_world_slot; pipeline.instance_world_offset=vs.instance_world_offset;
  auto constants=[&](std::array<float,4> color) {
    std::vector<NativeSceneConstant> result;
    for(auto [stage,shader]:{std::pair{NativeBackendStage::Vertex,&vs},std::pair{NativeBackendStage::Pixel,&ps}}) {
      D3D11_SHADER_DESC shader_desc{}; shader->reflection->GetDesc(&shader_desc);
      for(UINT b=0;b<shader_desc.ConstantBuffers;++b) {
        auto* buffer=shader->reflection->GetConstantBufferByIndex(b);
        D3D11_SHADER_BUFFER_DESC cb{}; buffer->GetDesc(&cb);
        D3D11_SHADER_INPUT_BIND_DESC binding{}; shader->reflection->GetResourceBindingDescByName(cb.Name,&binding);
        NativeSceneConstant image; image.stage=stage; image.slot=binding.BindPoint; image.bytes.resize(cb.Size);
        for(UINT v=0;v<cb.Variables;++v) {
          D3D11_SHADER_VARIABLE_DESC variable{}; buffer->GetVariableByIndex(v)->GetDesc(&variable);
          if(!(variable.uFlags&D3D_SVF_USED)) continue;
          const std::string name=variable.Name;
          if(name=="g_mWorld") image.matrices.push_back({NativeSceneMatrixSource::World,variable.StartOffset,column_major});
          else if(name=="g_mViewProjection") image.matrices.push_back({NativeSceneMatrixSource::ViewProjection,variable.StartOffset,false});
          else if(name=="g_mViewTranspose") image.matrices.push_back({NativeSceneMatrixSource::ViewTranspose,variable.StartOffset,false});
          else if(name=="tint") std::memcpy(image.bytes.data()+variable.StartOffset,color.data(),16);
        }
        result.push_back(std::move(image));
      }
    }
    return result;
  };
  ShaderBindings source_vertex(nullptr,vs),source_pixel(nullptr,ps);
  std::vector<uint8_t> matrix_bytes(64),tint_bytes(16);
  for(size_t i=0;i<16;++i) Word(matrix_bytes,i*4,std::bit_cast<uint32_t>(kNativeSceneIdentity[i]));
  Word(tint_bytes,0,std::bit_cast<uint32_t>(1.f)); Word(tint_bytes,12,std::bit_cast<uint32_t>(1.f));
  source_vertex.SetGuestFloatRegisters("g_mWorld",matrix_bytes);
  source_vertex.SetGuestFloatRegisters("g_mViewProjection",matrix_bytes);
  source_vertex.SetGuestFloatRegisters("g_mViewTranspose",matrix_bytes);
  source_pixel.SetGuestFloatRegisters("tint",tint_bytes);
  NativeBackendTextureDesc texture_desc; texture_desc.width=texture_desc.height=1;
  texture_desc.format=DXGI_FORMAT_R8G8B8A8_UNORM;
  const uint8_t white_pixel[]{255,255,255,255};
  std::shared_ptr<NativeBackendTexture> white=backend->CreateTexture(texture_desc,white_pixel);
  source_pixel.SetTexture("image",white);
  source_pixel.SetSampler("imageSampler",&backend->CreateSampler({}));
  const auto captured=CaptureNativeSceneMaterial(backend,pipeline,source_vertex,source_pixel);
  auto material_program=std::make_shared<NativeSceneMaterialProgram>();
  material_program->backend=backend; material_program->vertex=material_program->reversed_vertex=vs; material_program->pixel=ps;
  for(const auto* name:{"g_mWorld","g_mViewProjection","g_mViewTranspose"})
    material_program->inputs.constants.push_back({false,name,matrix_bytes});
  material_program->inputs.constants.push_back({true,"tint",tint_bytes});
  material_program->inputs.textures.push_back({"image"});
  material_program->inputs.textures.push_back({"imageSampler"});
  material_program->textures={white,{}};
  NativeBackendSampler* program_samplers[]{nullptr,&backend->CreateSampler({})};
  const auto program_capture=material_program->Capture(pipeline,false,program_samplers);
  Require(program_capture.material->Equivalent(*captured.material),"owned material program disagrees with live binding capture");
  Reject([&] { material_program->Capture(pipeline,false,{}); });
  Require(captured.world==kNativeSceneIdentity && captured.camera.view_projection==kNativeSceneIdentity,
    "retail material capture lost canonical matrix layout");
  auto red=captured.material;
  const size_t translation_offset=column_major?12:48;
  Word(matrix_bytes,translation_offset,std::bit_cast<uint32_t>(.25f));
  source_vertex.SetGuestFloatRegisters("g_mWorld",matrix_bytes);
  const auto refreshed=CaptureNativeSceneMaterial(backend,pipeline,source_vertex,source_pixel,{},red);
  Require(refreshed.material==red && refreshed.world[12]==.25f && captured.world[12]==0,
    "retained material refresh copied stale world state or rebuilt the material");
  Word(matrix_bytes,translation_offset,0); source_vertex.SetGuestFloatRegisters("g_mWorld",matrix_bytes);
  // Mutation of the bridge bindings cannot alter the published native material.
  Word(tint_bytes,0,0); Word(tint_bytes,8,std::bit_cast<uint32_t>(1.f));
  source_pixel.SetGuestFloatRegisters("tint",tint_bytes);
  auto blue=CaptureNativeSceneMaterial(backend,pipeline,source_vertex,source_pixel,{},red).material;
  Require(blue!=red,"retained material refresh ignored changed pixel constants");
  const std::weak_ptr<NativeBackendTexture> retained_texture=white;
  white.reset(); source_pixel.ClearTextures(); source_pixel.ClearSamplers();
  Require(!retained_texture.expired(),"native material failed to retain texture generation");
  auto malformed=constants({1,0,0,1}); malformed.front().matrices.push_back({NativeSceneMatrixSource::World,UINT32_MAX,false});
  Reject([&] { NativeSceneMaterial bad(backend,pipeline,malformed); });
  NativeSceneObject object; object.geometry=geometry; object.material=red;
  object.bounds=NativeSceneBounds{{-.125f,-.25f,.5f},{.125f,.25f,.5f}};
  NativeSceneDatabase scene;
  object.world[12]=-.5f; const auto left=scene.Create(object);
  object.world[12]=.5f; const auto right=scene.Create(object);
  const auto initial=scene.Publish(0);
  NativeBackendTextureDesc target_desc; target_desc.width=64; target_desc.height=32; target_desc.format=DXGI_FORMAT_R8G8B8A8_UNORM;
  auto target=backend->CreateRenderTarget(target_desc);
  NativeSceneView view; view.viewport={0,0,64,32,0,1};
  NativeSceneRenderer renderer;
  std::vector<uint8_t> pixels;
  auto render=[&](const NativeSceneSnapshot& snapshot,float fraction=1) {
    backend->BeginFrame(); auto& recorder=backend->Recorder();
    NativeBackendRenderTarget* targets[]{target.get()}; recorder.SetRenderTargets(targets,nullptr);
    recorder.ClearColor(*target,{0,0,0,1});
    const auto stats=renderer.Render(*backend,snapshot,view,fraction);
    backend->Submit(); pixels=backend->ReadRenderTarget(*target); return stats;
  };
  auto pixel=[&](size_t x,size_t channel) { return pixels[(16*64+x)*4+channel]; };
  auto stats=render(*initial);
  Require(stats.draws==1 && stats.instanced_draws==1 && pixel(16,0)==255 && pixel(48,0)==255 && pixel(32,0)==0,
    "native scene did not directly instance retained geometry");
  object.world[12]=0; scene.Update(left,object); const auto moved=scene.Publish(1);
  Require(initial->instances[0]->object.world[12]==-.5f,"published scene mutated");
  Require(initial->instances[1]==moved->instances[1],"unchanged scene object was rebuilt");
  render(*moved,.5f); Require(pixel(24,0)==255 && pixel(32,0)==0,"native scene transform interpolation");
  render(*moved,1); Require(pixel(32,0)==255 && pixel(16,0)==0,"native scene current transform");
  const auto unchanged=scene.Publish(2); render(*unchanged,0);
  Require(pixel(32,0)==255 && pixel(16,0)==0,"stationary object replayed old interpolation");
  view.view[12]=-.5f; render(*unchanged);
  Require(pixel(16,0)==255 && pixel(32,0)==255 && pixel(48,0)==0,"native camera could not render same scene again");
  view.view=kNativeSceneIdentity;
  object.world[12]=4; scene.Update(right,object); auto outside=scene.Publish(3);
  stats=render(*outside); Require(stats.visible==1 && stats.culled==1 && stats.draws==1,"native frustum culling");
  object.world[12]=0; object.material=blue; object.order=1; scene.Update(right,object);
  auto overlap=scene.Publish(4); stats=render(*overlap);
  Require(stats.draws==2 && pixel(32,2)==255 && pixel(32,0)==0,"native scene material/order separation");
  Require(scene.Remove(right),"remove native object"); auto removed=scene.Publish(5);
  render(*removed); Require(pixel(32,0)==255 && pixel(32,2)==0,"removed object remained visible");
  render(*overlap); Require(pixel(32,2)==255,"old scene lost retired object generation");
  Reject([&] { scene.Publish(5); }); Reject([&] { scene.Update(right,object); });
  scene.Clear(); auto empty=scene.Publish(6); Require(render(*empty).draws==0 && pixel(32,0)==0,"scene clear");
  const auto replacement=scene.Create(object); Require(replacement>right,"scene reused retired identity");
  // A gap in simulation publications must not invent motion through missing ticks.
  scene.Publish(7); object.world[12]=.5f; scene.Update(replacement,object);
  auto gap=scene.Publish(10); render(*gap,0);
  Require(pixel(48,2)==255 && pixel(32,2)==0,"scene interpolated across a publication gap");
  scene.Clear(); object.material=red; object.world=kNativeSceneIdentity;
  for(size_t i=0;i<257;++i) scene.Create(object);
  auto many=scene.Publish(11); stats=render(*many);
  Require(stats.visible==257 && stats.draws==2 && stats.instanced_draws==1 && pixel(32,0)==255,
    "native scene lost instances across batch capacity");
  NativeSceneAdapter adapter;
  Require(!adapter.PreviousGroupMaterial(500),"empty material history returned an asset");
  {
    auto temporary=std::make_shared<NativeSceneMaterial>(backend,pipeline,blue->constants());
    adapter.RememberGroupMaterial(500,temporary);
    Require(adapter.PreviousGroupMaterial(500)==temporary,"group material history lost a live asset");
  }
  Require(!adapter.PreviousGroupMaterial(500),"group material hint kept a retired asset alive");
  adapter.RememberGroupMaterial(500,captured.material);
  Require(adapter.PreviousGroupMaterial(500)==captured.material,"recycled group could not replace its material hint");
  uint64_t adapter_left=0,adapter_right=0;
  {
    NativeIndexedMesh mesh(*backend,vs,declaration,12,vertices,indices,2);
    auto left_capture=captured; left_capture.world[12]=-.5f;
    auto right_capture=captured; right_capture.world[12]=.5f;
    right_capture.material=std::make_shared<NativeSceneMaterial>(backend,pipeline,constants({1,0,0,1}),
      std::vector<NativeSceneTexture>{{source_pixel.TextureImages().front().slot,retained_texture.lock()}},
      std::vector<NativeSceneSampler>{{source_pixel.SamplerImages().front().slot,&backend->CreateSampler({})}});
    Require(right_capture.material->Equivalent(*captured.material) &&
      right_capture.material->fingerprint()==captured.material->fingerprint() && !right_capture.material->Equivalent(*blue),
      "material identity omitted bindings or changed equivalent captures");
    adapter_left=adapter.Observe({1,100,0,0},backend,mesh,0,6,0,left_capture);
    adapter_right=adapter.Observe({2,200,0,0},backend,mesh,0,6,0,right_capture);
    const auto before=adapter.SelectOne(adapter_left);
    Require(adapter.Observe({1,100,0,0},backend,mesh,0,6,0,left_capture)==adapter_left &&
      before==adapter.SelectOne(adapter_left),"unchanged adapter object lost native identity");
    auto changed=left_capture; changed.world[12]=0;
    adapter.Observe({1,100,0,0},backend,mesh,0,6,0,changed);
    Require(before->object.world[12]==-.5f && adapter.SelectOne(adapter_left)->object.world[12]==0,
      "adapter changed an already selected object");
    adapter.Observe({1,100,0,0},backend,mesh,0,6,0,left_capture);
  }
  const uint64_t selection[]{adapter_left,adapter_right};
  const auto retained_left=adapter.SelectOne(adapter_left);
  auto retained_capture=captured; retained_capture.world=retained_left->object.world;
  Require(adapter.Observe({1,100,0,0},retained_left->object.geometry,retained_capture)==adapter_left &&
    adapter.SelectOne(adapter_left)==retained_left,"retained scene group lost identity after mesh destruction");
  auto selected=adapter.Select(selection);
  Require(selected.instances[0]->object.geometry==selected.instances[1]->object.geometry,
    "adapter did not share retained geometry across owners");
  Require(selected.instances[0]->object.material==selected.instances[1]->object.material,
    "adapter did not intern independently captured equivalent materials");
  stats=render(selected);
  Require(stats.instanced_draws==1 && pixel(16,0)==255 && pixel(48,0)==255,"adapter did not render selected native objects");
  const auto publication=adapter.Publish(1);
  Require(adapter.AcquirePublication()==publication && publication->Find(adapter_left) &&
    !publication->Find(UINT64_MAX),"native publication lookup failed");
  NativeSceneSources::World world{};
  auto event_world=retained_left->object.world; event_world[12]=0;
  bool column=false;
  for(const auto& constant:retained_left->object.material->constants()) for(const auto& matrix:constant.matrices)
    if(matrix.source==NativeSceneMatrixSource::World) column=matrix.column_major;
  for(size_t row=0;row<4;++row) for(size_t col=0;col<4;++col) {
    const auto bits=std::bit_cast<uint32_t>(event_world[row*4+col]);
    const auto offset=(column?col*4+row:row*4+col)*4;
    for(size_t b=0;b<4;++b) world[offset+b]=uint8_t(bits>>(24-b*8));
  }
  Require(adapter.UpdateWorld(100,999,world)==0,"stale generation updated native scene");
  Require(adapter.UpdateWorld(100,1,world)==1,"simulation event did not update native object");
  const auto updated=adapter.Publish(2);
  Require(updated->Find(adapter_left)->object.world==event_world &&
    publication->Find(adapter_left)->object.world[12]==-.5f &&
    updated->Find(adapter_right)==publication->Find(adapter_right),"publication mutated history or copied unchanged objects");
  render(*updated->snapshot); Require(pixel(32,0)==255,"simulation publication did not render updated object");
  adapter.Retire(100); Reject([&] { adapter.SelectOne(adapter_left); });
  const auto retired=adapter.Publish(3);
  Require(!retired->Find(adapter_left) && updated->Find(adapter_left),"retirement changed an older publication");
  Require(adapter.objects()==1,"adapter retirement removed another owner");
  render(selected); Require(pixel(16,0)==255,"adapter retirement invalidated an in-flight scene");
  // Populate a part which has never been observed/drawn, then render solely
  // from its publication after the producer and its source records retire.
  NativeSceneAdapter populated;
  NativeSceneSources catalog;
  catalog.Born(300); catalog.Born(400);
  const NativeSceneSources::Part unseen[]{ {3000,0,0,9000,500} };
  const NativeSceneSources::Part unsupported[]{ {4000,0,0,9004,500} };
  catalog.Observe(300,unseen); catalog.Observe(400,unsupported);
  catalog.PublishWorld(300,world); catalog.PublishWorld(400,world);
  const auto populate=[&] {
    return populated.PopulateGroup(catalog,500,retained_left->object.geometry,captured,
      [](const auto& part) { return part.instance==3000; });
  };
  const auto population=populate();
  Require(population.examined==2 && population.created==1 && population.rejected==1 && populated.objects()==1,
    "group assets did not populate unseen eligible parts exclusively");
  Require(populate().examined==0,"unchanged group repeated asset population");
  populated.Retire(999);
  Require(populate().examined==0,"unrelated retirement invalidated populated groups");
  const auto unseen_publication=populated.Publish(1);
  render(*unseen_publication->snapshot);
  Require(pixel(32,0)==255 && unseen_publication->snapshot->instances[0]->object.world==event_world,
    "unseen part did not render from its event-published transform");
  populated.Retire(300);
  Require(populate().created==1,"same-address model replacement failed to repopulate assets");
  const auto before_rebirth=populated.Publish(2);
  populated.Retire(300); catalog.Born(300); catalog.Observe(300,unseen); catalog.PublishWorld(300,world);
  Require(populate().created==1,"reused owner did not acquire group assets");
  const auto after_rebirth=populated.Publish(3);
  Require(before_rebirth->snapshot->instances[0]->id!=after_rebirth->snapshot->instances[0]->id,
    "group population reused a retired native identity");
  populated.Retire(300); catalog.Retire(300); catalog.Retire(400);
  Require(!catalog.FindGroup(500),"retired parts kept a live group index");
  render(*unseen_publication->snapshot); Require(pixel(32,0)==255,"unseen publication borrowed retired producer assets");
  NativeSceneAdapter geometry_loader;
  NativeSceneSources geometry_sources; geometry_sources.Born(900);
  const NativeSceneSources::Part geometry_part[]{ {9000,0,0,0,500} };
  geometry_sources.Observe(900,geometry_part);
  const auto geometry_revision=geometry_sources.FindGroup(500)->revision;
  {
    NativeIndexedMesh unloaded(*backend,vs,declaration,12,vertices,indices,2);
    const auto geometry=geometry_loader.RetainGeometry(backend,unloaded,0,6);
    Require(geometry_loader.RetainGeometry(backend,unloaded,0,6)==geometry,"preloaded geometry was not interned");
    Reject([&] { geometry_loader.RetainGeometry({},unloaded,0,6); });
    geometry_loader.PublishGroupGeometry(500,geometry_revision,geometry);
  }
  const auto geometry_publication=geometry_loader.Publish(1);
  Require(geometry_publication->snapshot->instances.empty() && geometry_publication->group_geometry.size()==1,
    "geometry loading required a visible object or material");
  const auto geometry_record=geometry_publication->group_geometry[0];
  geometry_loader.PublishGroupGeometry(500,geometry_revision,geometry_record->geometry);
  Require(geometry_loader.GroupGeometry(500,geometry_revision)==geometry_record,
    "unchanged geometry publication did not share its record");
  geometry_sources.Retire(900); geometry_loader.PruneGroupGeometry(geometry_sources);
  Require(!geometry_loader.geometry_groups(),"retired source retained preloaded geometry");
  geometry_sources.Born(900); geometry_sources.Observe(900,geometry_part);
  const auto replacement_revision=geometry_sources.FindGroup(500)->revision;
  geometry_loader.PublishGroupGeometry(500,replacement_revision,geometry_record->geometry);
  Require(!geometry_loader.GroupGeometry(500,geometry_revision) &&
    geometry_record->revision==geometry_revision,"reused group inherited an old geometry identity");
  NativeSceneDatabase loaded_scene;
  NativeSceneObject loaded_object; loaded_object.geometry=geometry_record->geometry; loaded_object.material=captured.material;
  loaded_scene.Create(loaded_object);
  render(*loaded_scene.Publish(1));
  Require(pixel(32,0)==255,"published geometry could not render after its source and mesh retired");
  geometry_loader.PublishGroupMaterial(500,replacement_revision,material_program);
  const auto material_publication=geometry_loader.Publish(2);
  geometry_sources.Retire(900); geometry_loader.PruneGroupGeometry(geometry_sources);
  Require(!geometry_loader.material_groups() && material_publication->group_materials.size()==1,
    "material retirement changed a published program or retained a live group");
  // Producer bindings now contain blue and no textures; the owned program
  // still constructs and renders its original red material without guest reads.
  loaded_object.material=material_publication->group_materials[0]->program->Capture(pipeline,false,program_samplers).material;
  NativeSceneDatabase material_scene; material_scene.Create(loaded_object);
  render(*material_scene.Publish(1));
  Require(pixel(32,0)==255 && pixel(32,2)==0,"owned material program borrowed producer constants or textures");
  for(const auto& message:backend->DrainValidationMessages()) throw std::runtime_error(message);
}
}
int main() {
  try {
    Visibility();
    QueuedGuestState();
    NativeSceneSources sources;
    const NativeSceneSources::Part first[]{ {1000,0,0,0,500},{1028,0,1,0,500} };
    Require(!sources.Observe(100,first),"scene inferred lifetime from an observation");
    const auto generation=sources.Born(100);
    Require(sources.Observe(100,first) && sources.Find(1028)->generation==generation,"scene source registration");
    const auto group_revision=sources.FindGroup(500)->revision;
    Require(sources.FindGroup(500)->parts.size()==2,"group index omitted a source part");
    sources.Observe(100,first);
    Require(sources.FindGroup(500)->revision==group_revision,"unchanged parts invalidated group assets");
    const NativeSceneSources::Part relocated[]{ {2000,1,0} };
    sources.Observe(100,relocated);
    Require(!sources.Find(1000) && sources.Find(2000)->lod==1,"scene source vector relocation");
    Require(!sources.FindGroup(500),"relocated sources retained old group membership");
    sources.Born(200); Reject([&] { sources.Observe(200,relocated); });
    Require(sources.Find(2000)->owner==100,"invalid source ownership changed mapping");
    sources.Retire(100); Require(!sources.Find(2000),"scene source survived destructor");
    Require(sources.Born(100)>generation,"scene source reused a dead generation");
    for(bool column:{false,true}) {
      Run(CreateNativeD3D11Backend({true,true}),column);
      NativeD3D12Options options; options.prefer_warp=true; options.debug_layer=true;
      options.geometry_workers=2; options.geometry_minimum_draws=1;
      Run(CreateNativeD3D12Backend(options),column);
    }
    std::cout<<"Native scene lifetime, publication, interpolation, camera, culling, materials and direct instancing passed on D3D11/D3D12\n";
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}

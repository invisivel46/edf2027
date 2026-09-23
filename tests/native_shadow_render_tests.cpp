// edf_native_shadow_render: the schedule, the guest-word snapshot/restore, the
// draw-list tap and its serialization, with fakes only (no GPU, no guest).
#include "native_graphics/native_frame_dispatch.h"
#include "native_graphics/native_shadow_render.h"
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace edf::native;
namespace {
int failures=0;
void Check(bool ok,const char* what) {
  if(!ok) { ++failures; std::cerr<<"FAILED: "<<what<<"\n"; }
}
// Guest words by address; unset words read as 0. Counts stores.
struct FakeMemory {
  mutable std::map<uint32_t,uint32_t> words;
  mutable uint32_t stores=0;
  uint32_t Word(uint32_t address) const { const auto found=words.find(address); return found==words.end()?0:found->second; }
  void StoreWord(uint32_t address,uint32_t value) const { words[address]=value; ++stores; }
  uint32_t Add(uint32_t address,uint32_t offset) const { return address+offset; }
  const uint8_t* Bytes(uint32_t address,size_t) const {
    static thread_local uint8_t byte=0;
    byte=uint8_t(Word(address&~3u)>>(24-8*(address&3u)));
    return &byte;
  }
};
struct FakeTexture final : NativeBackendTexture {
  uint32_t w,h;
  FakeTexture(uint32_t width,uint32_t height):w(width),h(height) {}
  uint32_t width() const override { return w; }
  uint32_t height() const override { return h; }
};
struct FakeBuffer final : NativeBackendBuffer { size_t bytes() const override { return 64; } };
struct FakePipeline final : NativeBackendPipeline {};
// Records every call it receives, so forwarding can be checked call by call.
struct FakeRecorder final : NativeBackendRecorder {
  std::vector<std::string> calls;
  void SetPipeline(NativeBackendPipeline&) override { calls.push_back("pipeline"); }
  void SetWorldInstancing(bool enabled,bool) override { calls.push_back(enabled?"instancing:on":"instancing:off"); }
  void SetTransientBatching(bool) override { calls.push_back("batching"); }
  void SetVertexBuffer(uint32_t slot,NativeBackendBuffer&,uint32_t,uint32_t) override { calls.push_back("vb"+std::to_string(slot)); }
  void SetIndexBuffer(NativeBackendBuffer&,NativeBackendIndexFormat,uint32_t) override { calls.push_back("ib"); }
  void SetTransientVertices(uint32_t slot,std::span<const uint8_t> bytes,uint32_t) override {
    calls.push_back("transient"+std::to_string(slot)+":"+std::to_string(bytes.size()));
  }
  void SetTransientVerticesOwned(uint32_t slot,std::vector<uint8_t>& bytes,uint32_t) override {
    calls.push_back("owned"+std::to_string(slot)+":"+std::to_string(bytes.size()));
  }
  void SetTopology(NativeBackendTopology) override { calls.push_back("topology"); }
  void SetBlendFactor(const std::array<float,4>&) override { calls.push_back("blend"); }
  void SetConstants(NativeBackendStage stage,uint32_t slot,std::span<const uint8_t>) override {
    calls.push_back("constants"+std::to_string(uint32_t(stage))+"."+std::to_string(slot));
  }
  void SetTexture(NativeBackendStage,uint32_t slot,NativeBackendTexture*) override { calls.push_back("texture"+std::to_string(slot)); }
  void SetSampler(NativeBackendStage,uint32_t,NativeBackendSampler*) override { calls.push_back("sampler"); }
  void SetRenderTargets(std::span<NativeBackendRenderTarget* const>,NativeBackendRenderTarget*) override { calls.push_back("targets"); }
  void SetViewport(const NativeBackendViewport&) override { calls.push_back("viewport"); }
  void SetScissor(const NativeBackendScissor&,bool) override { calls.push_back("scissor"); }
  void ClearColor(NativeBackendRenderTarget&,const std::array<float,4>&) override { calls.push_back("clear"); }
  void ClearDepthStencil(NativeBackendRenderTarget&,bool,bool,float,uint8_t) override { calls.push_back("clear_depth"); }
  void Draw(uint32_t count,uint32_t) override { calls.push_back("draw"+std::to_string(count)); }
  void DrawIndexed(uint32_t count,uint32_t,int32_t) override { calls.push_back("indexed"+std::to_string(count)); }
  void DrawIndexedInstanced(uint32_t count,uint32_t instances,uint32_t,int32_t,uint32_t) override {
    calls.push_back("instanced"+std::to_string(count)+"x"+std::to_string(instances));
  }
  void CopyTexture(NativeBackendTexture&,NativeBackendTexture&) override { calls.push_back("copy"); }
  void CopyToShared(NativeBackendSharedSurface&,NativeBackendRenderTarget&) override { calls.push_back("shared"); }
  void ResolveTarget(NativeBackendTexture&,NativeBackendRenderTarget&) override { calls.push_back("resolve"); }
  void UpdateBuffer(NativeBackendBuffer&,uint32_t,std::span<const uint8_t>) override { calls.push_back("update_buffer"); }
  void UpdateTexture(NativeBackendTexture&,std::span<const uint8_t>) override { calls.push_back("update_texture"); }
  void BeginQuery(NativeBackendQuery&) override { calls.push_back("begin_query"); }
  void EndQuery(NativeBackendQuery&) override { calls.push_back("end_query"); }
  void PushState() override { calls.push_back("push"); }
  void PopState() override { calls.push_back("pop"); }
};
class FakeBackend final : public NativeRenderBackend {
 public:
  FakeRecorder recorder;
  std::string_view name() const override { return "fake"; }
  std::unique_ptr<NativeBackendBuffer> CreateBuffer(const NativeBackendBufferDesc&,std::span<const uint8_t>) override { return {}; }
  std::unique_ptr<NativeBackendTexture> CreateTexture(const NativeBackendTextureDesc&,std::span<const uint8_t>) override { return {}; }
  std::unique_ptr<NativeBackendRenderTarget> CreateRenderTarget(const NativeBackendTextureDesc&) override { return {}; }
  bool SupportsSamples(uint32_t,uint32_t samples) override { return samples==1; }
  std::unique_ptr<NativeBackendQuery> CreateQuery(NativeBackendQueryKind) override { return {}; }
  bool ReadQuery(NativeBackendQuery&,std::span<uint8_t>) override { return false; }
  NativeBackendPipeline& CreatePipeline(const NativeBackendPipelineDesc&) override { throw std::runtime_error("fake"); }
  NativeBackendSampler& CreateSampler(const NativeBackendSamplerDesc&) override { throw std::runtime_error("fake"); }
  NativeBackendRecorder& Recorder(uint32_t) override { return recorder; }
  uint32_t RecorderCount() const override { return 1; }
  bool SupportsParallelRecording() const override { return false; }
  void BeginFrame() override {}
  void Submit() override {}
  std::vector<uint8_t> ReadRenderTarget(NativeBackendRenderTarget&) override { return {}; }
  std::vector<uint8_t> ReadTexture(NativeBackendTexture&) override { return {}; }
  std::vector<std::string> DrainValidationMessages() override { return {}; }
  void AttachWindow(void*,uint32_t,uint32_t) override {}
  NativeBackendRenderTarget* BackBuffer() override { return nullptr; }
  void Present(bool) override {}
  std::unique_ptr<NativeBackendTexture> OpenSharedTexture(void*,const NativeBackendTextureDesc&) override { return {}; }
  bool WaitSharedFence(void*,uint64_t) override { return false; }
  std::unique_ptr<NativeBackendSharedSurface> CreateSharedSurface(const NativeBackendTextureDesc&) override { return {}; }
  uint64_t SignalShared(NativeBackendSharedSurface&) override { return 0; }
};

void Schedule() {
  const NativeShadowSchedule every3{3,0,16};
  Check(NativeShadowRenderDue(0,every3,0) && NativeShadowRenderDue(3,every3,0) && NativeShadowRenderDue(9,every3,0),"every 3rd frame from 0");
  Check(!NativeShadowRenderDue(1,every3,0) && !NativeShadowRenderDue(5,every3,0),"frames between are not shadow frames");
  const NativeShadowSchedule from100{5,100,2};
  Check(!NativeShadowRenderDue(95,from100,0) && NativeShadowRenderDue(100,from100,0) && NativeShadowRenderDue(105,from100,1),"start frame");
  Check(!NativeShadowRenderDue(110,from100,2),"limit reached");
  Check(!NativeShadowRenderDue(100,{0,0,16},0) && !NativeShadowRenderDue(100,{-1,0,16},0),"period 0 or less is off");
  Check(NativeShadowRenderDue(4,{2,-7,4},0),"negative start is frame 0");
}

void Snapshot() {
  FakeMemory memory;
  for(uint32_t i=0;i<8;++i) memory.words[0x1000+i*4]=100+i;
  memory.words[0x2000]=7;
  NativeGuestSnapshot snapshot;
  snapshot.Capture(memory,"block",0x1000,8);
  snapshot.Capture(memory,"serial",0x2000,1);
  Check(snapshot.words()==9 && snapshot.ranges().size()==2,"captured words");
  // Nothing changed: nothing written back.
  memory.stores=0;
  auto report=snapshot.Restore(memory);
  Check(memory.stores==0 && report.size()==2 && !report[0].changed && !report[1].changed,"an unchanged range is not written");
  // The shadow's writes, then the restore.
  memory.words[0x1004]=0xdead; memory.words[0x101c]=0xbeef; memory.words[0x2000]=8;
  memory.words[0x3000]=5;  // Outside every range: left alone.
  memory.stores=0;
  report=snapshot.Restore(memory);
  Check(memory.stores==3,"only changed words are written");
  Check(report[0].name=="block" && report[0].address==0x1000 && report[0].words==8 && report[0].changed==2,"block report");
  Check(report[1].name=="serial" && report[1].changed==1,"serial report");
  Check(memory.Word(0x1004)==101 && memory.Word(0x101c)==107 && memory.Word(0x2000)==7,"restored values");
  Check(memory.Word(0x3000)==5,"words outside the snapshot are untouched");
  // Overlapping ranges end with the earliest capture's value.
  FakeMemory overlap;
  overlap.words[0x10]=1;
  NativeGuestSnapshot twice;
  twice.Capture(overlap,"first",0x10,1);
  overlap.words[0x10]=2;
  twice.Capture(overlap,"second",0x10,1);
  overlap.words[0x10]=3;
  twice.Restore(overlap);
  Check(overlap.Word(0x10)==1,"overlap restores the earliest capture");
  // The shadow's serial handling: capture the native result, store the value
  // the native frame started from, run, restore.
  FakeMemory owner;
  const uint32_t serial=0x4000+136;
  owner.words[serial]=41;  // Before the native frame.
  const auto before=owner.Word(serial);
  owner.words[serial]=42;  // The native frame's one view.
  NativeGuestSnapshot frame;
  frame.Capture(owner,"serial",serial,1);
  owner.StoreWord(serial,before);
  owner.words[serial]=owner.Word(serial)+1;  // The guest view.
  Check(owner.Word(serial)==42,"the guest view had the native view's serial");
  frame.Restore(owner);
  Check(owner.Word(serial)==42,"the native frame's serial is kept");
}

void Dispatch() {
  // DispatchNativeFrame without the phase loop: the finish stage runs, the
  // phase listeners do not.
  FakeMemory memory;
  const uint32_t owner=0x10000,context=0x20000,core=0x30000,core_vtable=0x31000,listener=0x40000,listener_vtable=0x41000;
  memory.words[0x820009a4]=0; memory.words[0x820008cc]=0x3f800000;
  memory.words[owner+132]=core; memory.words[core]=core_vtable;
  memory.words[core_vtable+12]=0x820B0B80; memory.words[core_vtable+16]=0x820AFEE8;
  // No views (flags clear); one phase listener in the owner+2232 list.
  const uint32_t sentinel=0x50000,node=0x50100;
  memory.words[owner+2232]=sentinel; memory.words[sentinel]=node; memory.words[node]=sentinel;
  memory.words[node+12]=listener; memory.words[listener]=listener_vtable; memory.words[listener_vtable+16]=0x8216E630;
  memory.words[owner+140]=0; memory.words[owner+144]=2;
  std::vector<uint32_t> called;
  const auto call=[&](uint32_t function,uint32_t,uint32_t,uint32_t,uint32_t) { called.push_back(function); };
  DispatchNativeFrame(memory,owner,context,call,false);
  Check(called==std::vector<uint32_t>{0x820B0B80,0x820AFEE8},"no phases: finish +12 and +16 only");
  called.clear();
  DispatchNativeFrame(memory,owner,context,call);
  Check(called==std::vector<uint32_t>{0x820B0B80,0x820AFEE8,0x8216E630,0x8216E630},"default: the phase loop too");
}

void Tap() {
  FakeRecorder inner;
  NativeDrawListRecorder tap;
  tap.Wrap(inner);
  FakeTexture texture(64,32);
  FakeBuffer vertices,indices;
  FakePipeline pipeline,instanced;
  pipeline.identity=0x1111; pipeline.identity_vertex=0xaa; pipeline.identity_pixel=0xbb; pipeline.identity_layout=0xcc;
  pipeline.world_instanced=&instanced; pipeline.instance_world_slot=1; pipeline.instance_world_offset=16;
  // Unarmed: forwarded, not recorded.
  tap.SetPipeline(pipeline);
  tap.Draw(3,0);
  Check(tap.size()==0 && inner.calls.size()==2,"unarmed tap forwards and records nothing");
  tap.Arm("native.sky");
  tap.SetPipeline(pipeline);
  tap.SetTopology(NativeBackendTopology::TriangleStrip);
  tap.SetTexture(NativeBackendStage::Pixel,2,&texture);
  std::vector<uint8_t> constants(96,0);
  for(uint32_t i=0;i<16;++i) {  // g_mWorld at offset 16: 1..16
    const float value=float(i+1);
    std::memcpy(constants.data()+16+i*4,&value,4);
  }
  tap.SetConstants(NativeBackendStage::Vertex,1,constants);
  tap.SetConstants(NativeBackendStage::Pixel,0,std::vector<uint8_t>(16,7));
  tap.SetVertexBuffer(0,vertices,32,64);
  tap.SetIndexBuffer(indices,NativeBackendIndexFormat::Uint16,0);
  tap.SetViewport({0,0,1280,720,0,1});
  tap.DrawIndexed(36,6,-2);
  tap.SetLabel("native.models");
  const std::vector<uint8_t> quad(64,9);
  tap.SetTransientVertices(3,quad,16);
  tap.SetBlendFactor({0.5f,0.25f,0,1});
  tap.DrawIndexedInstanced(6,4,0,0,0);
  // PushState/PopState put the tracked bindings back.
  tap.PushState();
  tap.SetTexture(NativeBackendStage::Pixel,2,nullptr);
  tap.PopState();
  tap.Draw(4,0);
  // Another thread's draw is forwarded but not in the list.
  std::thread other([&] { tap.Draw(5,0); });
  other.join();
  tap.Pause(true); tap.Draw(6,0); tap.Pause(false);
  const auto draws=tap.Take();
  tap.Draw(7,0);
  Check(draws.size()==3,"three draws recorded on the arming thread");
  Check(inner.calls.back()=="draw7","every call still reaches the inner recorder");
  size_t forwarded_draws=0;
  for(const auto& call:inner.calls) forwarded_draws+=call.starts_with("draw") || call.starts_with("indexed") || call.starts_with("instanced");
  Check(forwarded_draws==7,"all draws forwarded, recorded or not");
  if(draws.size()==3) {
    const auto& first=draws[0];
    Check(first.index==0 && first.label=="native.sky" && first.pipeline==0x1111 && first.vertex_shader==0xaa &&
          first.pixel_shader==0xbb && first.layout==0xcc,"pipeline identity");
    Check(first.kind==NativeDrawKind::Indexed && first.count==36 && first.first==6 && first.base==-2 && first.instances==1,"counts");
    Check(first.topology==uint32_t(NativeBackendTopology::TriangleStrip),"topology");
    Check(first.textures.size()==1 && first.textures[0].stage==1 && first.textures[0].slot==2 && first.textures[0].width==64 &&
          first.textures[0].height==32,"texture");
    Check(first.constants.size()==2 && first.constants[0].stage==0 && first.constants[0].slot==1 && first.constants[0].bytes==96 &&
          first.constants[0].hash==NativeShadowHash(constants.data(),constants.size()),"constants");
    Check(first.world.has_value() && (*first.world)[0]==1.f && (*first.world)[15]==16.f,"world matrix from the pipeline's g_mWorld registers");
    Check(first.geometry.find("0=b:")==0 && first.geometry.find(" i:")!=std::string::npos,"buffer geometry");
    Check(first.viewport[2]==1280.f && !first.blend,"viewport, no blend factor");
    const auto& second=draws[1];
    Check(second.label=="native.models" && second.kind==NativeDrawKind::Instanced && second.instances==4,"instanced draw");
    Check(second.geometry.find(std::format("3=t:{:016x}:64/16",NativeShadowHash(quad.data(),quad.size())))!=std::string::npos,"transient geometry hash");
    Check(second.blend && (*second.blend)[0]==0.5f,"blend factor");
    Check(second.constant_hash==first.constant_hash,"same constants, same combined hash");
    const auto& third=draws[2];
    Check(third.kind==NativeDrawKind::Draw && third.count==4 && third.textures.size()==1,"state after PopState");
  }
  // A different constant changes the combined hash; a pipeline without a
  // world declaration has no world.
  FakePipeline plain;
  tap.Arm("guest.finish");
  tap.SetPipeline(plain);
  tap.SetConstants(NativeBackendStage::Vertex,1,constants);
  tap.Draw(3,0);
  constants[0]=1;
  tap.SetConstants(NativeBackendStage::Vertex,1,constants);
  tap.Draw(3,0);
  const auto again=tap.Take();
  Check(again.size()==2 && again[0].constant_hash!=again[1].constant_hash,"constant change changes the hash");
  Check(again.size()==2 && !again[0].world && again[0].pipeline==uint64_t(reinterpret_cast<uintptr_t>(&plain)),"unstamped pipeline: address, no world");
}

void BackendRouting() {
  FakeBackend fake;
  NativeRenderBackend& backend=fake;  // Recorder() is the base's (the override hides it by name).
  auto& inner=fake.recorder;
  NativeDrawListRecorder tap;
  Check(&backend.Recorder()==&inner,"no tap: Recorder() is Recorder(0)");
  backend.recorder_tap=&tap;
  tap.Arm("native.post");
  auto& recorder=backend.Recorder();
  Check(&recorder==&tap,"a tap: Recorder() hands out the tap");
  Check(&backend.Recorder(0)==&inner,"Recorder(index) is unchanged");
  recorder.Draw(3,0);
  Check(inner.calls==std::vector<std::string>{"draw3"} && tap.size()==1,"the tap wraps Recorder(0)");
  backend.recorder_tap=nullptr;
  Check(&backend.Recorder()==&inner,"tap removed");
}

void Identity() {
  NativeBackendPipelineDesc desc{};
  desc.vertex_id=1; desc.pixel_id=2; desc.input_layout_id=3; desc.state={1,2,3,4,5,6};
  FakePipeline a,b,c;
  StampNativeBackendPipelineIdentity(a,desc);
  StampNativeBackendPipelineIdentity(b,desc);
  desc.state[4]=0;
  StampNativeBackendPipelineIdentity(c,desc);
  Check(a.identity && a.identity==b.identity && a.identity!=c.identity,"identity follows the description");
  Check(a.identity_vertex==1 && a.identity_pixel==2 && a.identity_layout==3,"shader and layout ids");
}

void Serialization() {
  NativeDrawRecord draw;
  draw.index=4; draw.label="guest.world:820077d8 \"q\"";
  draw.pipeline=0x1234; draw.vertex_shader=0xa; draw.pixel_shader=0xb; draw.layout=0xc;
  draw.topology=1; draw.kind=NativeDrawKind::Indexed; draw.count=36; draw.first=6; draw.base=-2;
  draw.textures.push_back({1,2,0xabc,64,32});
  draw.constants.push_back({0,1,96,0xfeed});
  draw.constant_hash=0x99;
  std::array<float,16> world{};
  world[0]=1.5f; world[5]=NAN; world[10]=INFINITY;
  draw.world=world;
  draw.targets={0x10}; draw.depth=0x20;
  draw.viewport={0,0,1280,720,0,1};
  draw.geometry="0=b:1+0/32";
  const auto line=SerializeNativeDrawRecord(draw);
  const std::string expected=
    "{\"i\":4,\"label\":\"guest.world:820077d8 \\\"q\\\"\",\"pipeline\":\"0000000000001234\",\"vs\":\"000000000000000a\","
    "\"ps\":\"000000000000000b\",\"layout\":\"000000000000000c\",\"topology\":1,\"kind\":\"indexed\",\"count\":36,\"first\":6,"
    "\"base\":-2,\"instances\":1,\"first_instance\":0,\"textures\":[{\"stage\":1,\"slot\":2,\"id\":\"0000000000000abc\",\"w\":64,\"h\":32}],"
    "\"constants\":[{\"stage\":0,\"slot\":1,\"bytes\":96,\"hash\":\"000000000000feed\"}],\"constant_hash\":\"0000000000000099\","
    "\"world\":[1.5,0,0,0,0,\"nan\",0,0,0,0,\"inf\",0,0,0,0,0],\"targets\":[\"0000000000000010\"],\"depth\":\"0000000000000020\","
    "\"viewport\":[0,0,1280,720,0,1],\"blend\":null,\"geometry\":\"0=b:1+0/32\"}";
  Check(line==expected,"draw line");
  if(line!=expected) std::cerr<<line<<"\n"<<expected<<"\n";
  const auto list=SerializeNativeDrawList("native",12,{draw,draw});
  const auto header=std::string("{\"format\":\"edf-shadow-draws\",\"version\":1,\"side\":\"native\",\"frame\":12,\"draws\":2}\n");
  Check(list.starts_with(header) && list==header+expected+"\n"+expected+"\n","list: header then one line per draw");
  Check(NativeShadowJsonString("a\nb\\\x01")=="\"a\\nb\\\\\\u0001\"","escapes");
}
}

int main() {
  Schedule();
  Snapshot();
  Dispatch();
  Tap();
  BackendRouting();
  Identity();
  Serialization();
  if(failures) { std::cerr<<failures<<" failure(s)\n"; return 1; }
  std::cout<<"native shadow render tests passed\n";
  return 0;
}

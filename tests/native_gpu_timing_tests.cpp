// GPU pass timings (edf_native_gpu_timings) and frame-time distribution
// (edf_native_frame_times): the timestamp ring and its aggregation against a
// fake backend whose GPU the test advances by hand, the frame-time recorder's
// percentiles and spike rule, and the D3D12 timestamp path on WARP in both
// direct and draw-packet recording.
#include "native_graphics/d3d12_backend.h"
#include "native_graphics/native_frame_times.h"
#include "native_graphics/native_gpu_pass_timings.h"
#include "native_graphics/native_render_backend.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace edf::native;
using Microsoft::WRL::ComPtr;

namespace {
int failures=0;
void Check(bool ok,const std::string& message) {
  if(ok) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}
bool Near(double a,double b) { return std::abs(a-b)<1e-6; }

// A GPU the test drives: timestamps take the fake clock's value when written,
// a resolve copies them for the frame being recorded, and a range is readable
// once the test has "completed" that frame.
class FakeTimestamps final : public NativeBackendTimestamps {
 public:
  explicit FakeTimestamps(uint32_t capacity) : written(capacity,UINT64_MAX),readback(capacity),resolved(capacity) {}
  uint32_t capacity() const override { return uint32_t(written.size()); }
  std::vector<uint64_t> written,readback,resolved;  // resolved: frame of the last resolve, 0 = never.
};
class FakeRecorder final : public NativeBackendRecorder {
 public:
  uint64_t* clock=nullptr;
  uint64_t* frame=nullptr;
  uint64_t writes=0,resolves=0;
  void SetPipeline(NativeBackendPipeline&) override {}
  void SetVertexBuffer(uint32_t,NativeBackendBuffer&,uint32_t,uint32_t) override {}
  void SetIndexBuffer(NativeBackendBuffer&,NativeBackendIndexFormat,uint32_t) override {}
  void SetTransientVertices(uint32_t,std::span<const uint8_t>,uint32_t) override {}
  void SetTopology(NativeBackendTopology) override {}
  void SetBlendFactor(const std::array<float,4>&) override {}
  void SetConstants(NativeBackendStage,uint32_t,std::span<const uint8_t>) override {}
  void SetTexture(NativeBackendStage,uint32_t,NativeBackendTexture*) override {}
  void SetSampler(NativeBackendStage,uint32_t,NativeBackendSampler*) override {}
  void SetRenderTargets(std::span<NativeBackendRenderTarget* const>,NativeBackendRenderTarget*) override {}
  void SetViewport(const NativeBackendViewport&) override {}
  void SetScissor(const NativeBackendScissor&,bool) override {}
  void ClearColor(NativeBackendRenderTarget&,const std::array<float,4>&) override {}
  void ClearDepthStencil(NativeBackendRenderTarget&,bool,bool,float,uint8_t) override {}
  void Draw(uint32_t,uint32_t) override {}
  void DrawIndexed(uint32_t,uint32_t,int32_t) override {}
  void DrawIndexedInstanced(uint32_t,uint32_t,uint32_t,int32_t,uint32_t) override {}
  void CopyTexture(NativeBackendTexture&,NativeBackendTexture&) override {}
  void CopyToShared(NativeBackendSharedSurface&,NativeBackendRenderTarget&) override {}
  void ResolveTarget(NativeBackendTexture&,NativeBackendRenderTarget&) override {}
  void UpdateBuffer(NativeBackendBuffer&,uint32_t,std::span<const uint8_t>) override {}
  void UpdateTexture(NativeBackendTexture&,std::span<const uint8_t>) override {}
  void BeginQuery(NativeBackendQuery&) override {}
  void EndQuery(NativeBackendQuery&) override {}
  void PushState() override {}
  void PopState() override {}
  void WriteTimestamp(NativeBackendTimestamps& set,uint32_t slot) override {
    auto& fake=static_cast<FakeTimestamps&>(set);
    fake.written.at(slot)=*clock;
    ++writes;
  }
  void ResolveTimestamps(NativeBackendTimestamps& set,uint32_t first,uint32_t count) override {
    auto& fake=static_cast<FakeTimestamps&>(set);
    for(uint32_t slot=first;slot<first+count;++slot) {
      Check(fake.written.at(slot)!=UINT64_MAX,"a resolved timestamp slot was never written");
      fake.readback[slot]=fake.written[slot];
      fake.resolved[slot]=*frame;
    }
    ++resolves;
  }
};
class FakeBackend final : public NativeRenderBackend {
 public:
  explicit FakeBackend(bool timestamps,uint64_t frequency=1000) : timestamps_(timestamps),frequency_(frequency) {
    recorder.clock=&clock; recorder.frame=&frame;
  }
  uint64_t clock=0,frame=1,completed=0;
  FakeRecorder recorder;
  FakeTimestamps* set=nullptr;
  std::string_view name() const override { return "fake"; }
  std::unique_ptr<NativeBackendBuffer> CreateBuffer(const NativeBackendBufferDesc&,std::span<const uint8_t>) override { return {}; }
  std::unique_ptr<NativeBackendTexture> CreateTexture(const NativeBackendTextureDesc&,std::span<const uint8_t>) override { return {}; }
  std::unique_ptr<NativeBackendRenderTarget> CreateRenderTarget(const NativeBackendTextureDesc&) override { return {}; }
  bool SupportsSamples(uint32_t,uint32_t samples) override { return samples==1; }
  std::unique_ptr<NativeBackendQuery> CreateQuery(NativeBackendQueryKind) override { return {}; }
  bool ReadQuery(NativeBackendQuery&,std::span<uint8_t>) override { return false; }
  uint64_t TimestampFrequency() const override { return frequency_; }
  std::unique_ptr<NativeBackendTimestamps> CreateTimestamps(uint32_t capacity) override {
    if(!timestamps_) return nullptr;
    auto created=std::make_unique<FakeTimestamps>(capacity);
    set=created.get();
    return created;
  }
  bool ReadTimestamps(NativeBackendTimestamps& timestamps,uint32_t first,std::span<uint64_t> out) override {
    auto& fake=static_cast<FakeTimestamps&>(timestamps);
    for(size_t index=0;index<out.size();++index) {
      const auto resolved=fake.resolved.at(first+index);
      if(!resolved || resolved>completed) return false;
    }
    for(size_t index=0;index<out.size();++index) out[index]=fake.readback[first+index];
    return true;
  }
  NativeBackendPipeline& CreatePipeline(const NativeBackendPipelineDesc&) override { throw std::runtime_error("fake"); }
  NativeBackendSampler& CreateSampler(const NativeBackendSamplerDesc&) override { throw std::runtime_error("fake"); }
  NativeBackendRecorder& Recorder(uint32_t) override { return recorder; }
  uint32_t RecorderCount() const override { return 1; }
  bool SupportsParallelRecording() const override { return false; }
  void BeginFrame() override {}
  void Submit() override { ++frame; }
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
 private:
  bool timestamps_;
  uint64_t frequency_;
};
const NativeGpuPassTiming* Find(const NativeGpuPassTimingWindow& window,std::string_view name) {
  for(const auto& pass:window.passes) if(pass.name==name) return &pass;
  return nullptr;
}
// One frame: sky 2 ticks, models twice (3 + 4 ticks), `extra` ticks after,
// then the frame ends and the fake GPU submits it.
bool RecordFrame(NativeGpuPassTimings& timings,FakeBackend& backend,uint64_t start,uint64_t extra=0) {
  backend.clock=start;
  auto& r=backend.recorder;
  if(!timings.BeginFrame(backend,r)) { backend.Submit(); return false; }
  backend.clock+=1;
  const auto sky=timings.BeginSpan("sky",r); backend.clock+=2; timings.EndSpan(sky,r);
  const auto models=timings.BeginSpan("models",r); backend.clock+=3; timings.EndSpan(models,r);
  const auto again=timings.BeginSpan("models",r); backend.clock+=4; timings.EndSpan(again,r);
  backend.clock+=extra;
  timings.EndFrame(r);
  backend.Submit();
  return true;
}

void GpuPassTimingsUnsupported() {
  FakeBackend backend(false);
  NativeGpuPassTimings timings;
  Check(!timings.BeginFrame(backend,backend.recorder),"a backend without timestamps opened a timed frame");
  Check(!timings.supported(),"a backend without timestamps still reports GPU timings as supported");
  Check(timings.BeginSpan("sky",backend.recorder)<0 && backend.recorder.writes==0,
        "a span was written without a timed frame");
  timings.EndFrame(backend.recorder);
  Check(backend.recorder.resolves==0,"a frame was resolved without a timed frame");
}

void GpuPassTimingsAggregate() {
  FakeBackend backend(true);
  NativeGpuPassTimings timings({4,16,3});
  // Frames start 100 ticks apart (1 tick = 1 ms) and are completed as soon
  // as they are submitted, so each is read at the next BeginFrame.
  for(uint64_t index=0;index<3;++index) {
    Check(RecordFrame(timings,backend,1000+index*100,index),"a frame with a free ring entry was not timed");
    backend.completed=backend.frame;
  }
  Check(backend.recorder.resolves==3,"every timed frame resolves its range once");
  NativeGpuPassTimingWindow window;
  Check(!timings.TakeReport(window),"a window was reported before its frames were read");
  // The first two were read by the BeginFrame after them; the last is read here.
  Check(timings.Poll(backend)==1,"the last finished frame was not read");
  Check(timings.TakeReport(window),"three read frames did not close a three-frame window");
  Check(window.frames==3 && window.skipped==0 && window.invalid_spans==0 && window.dropped_spans==0,
        "the window's frame counts are wrong");
  const auto* sky=Find(window,"sky");
  const auto* models=Find(window,"models");
  const auto* frame=Find(window,"frame");
  const auto* interval=Find(window,"interval");
  Check(sky && sky->frames==3 && Near(sky->total_ms,6) && Near(sky->max_ms,2),"sky was not 2 ms a frame");
  // Two model spans a frame add up within the frame.
  Check(models && models->frames==3 && Near(models->total_ms,21) && Near(models->max_ms,7),
        "a pass that occurs twice in a frame was not summed per frame");
  // Frame: 1 + 2 + 3 + 4 + extra (0, 1, 2).
  Check(frame && frame->frames==3 && Near(frame->total_ms,33) && Near(frame->max_ms,12),"frame spans are wrong");
  Check(interval && interval->frames==2 && Near(interval->total_ms,200) && Near(interval->max_ms,100),
        "the interval is not the begin-to-begin time of consecutive frames");
  Check(Find(window,"frame")==&window.passes[0],"the frame span is not reported first");
  Check(!timings.TakeReport(window),"an empty window was reported twice");
}

void GpuPassTimingsRingFull() {
  FakeBackend backend(true);
  NativeGpuPassTimings timings({4,16,1000});
  // The GPU finishes nothing: four frames fill the ring and the next two are
  // skipped rather than waited for.
  uint32_t timed=0;
  for(uint64_t index=0;index<6;++index) timed+=RecordFrame(timings,backend,index*10)?1:0;
  Check(timed==4,"a full ring did not skip frames: timed "+std::to_string(timed));
  Check(timings.pending()==4,"the ring does not hold four frames in flight");
  // The GPU catches up with the first two only; reading stops at the first
  // unfinished frame even though later ones are also unfinished.
  backend.completed=2;
  Check(timings.Poll(backend)==2,"frames were read past the first unfinished one");
  backend.completed=backend.frame;
  Check(timings.Poll(backend)==2,"the rest of the finished frames were not read");
  NativeGpuPassTimingWindow window;
  Check(timings.TakeReport(window,true),"a forced report of four frames failed");
  Check(window.frames==4 && window.skipped==2,"skipped frames were not counted: "+std::to_string(window.skipped));
  const auto* interval=Find(window,"interval");
  Check(interval && interval->frames==3,"interval spans consecutive frames only");
  // A skipped frame breaks the interval chain: the next timed frame follows
  // two untimed ones, so no interval is taken across them.
  Check(RecordFrame(timings,backend,1000),"a frame after the ring drained was not timed");
  backend.completed=backend.frame;
  timings.Poll(backend);
  Check(timings.TakeReport(window,true) && !Find(window,"interval"),"an interval was taken across skipped frames");
}

void GpuPassTimingsSpanBudget() {
  FakeBackend backend(true);
  NativeGpuPassTimings timings({2,6,1});  // Frame + two spans.
  auto& r=backend.recorder;
  backend.clock=0;
  Check(timings.BeginFrame(backend,r),"frame not opened");
  const auto a=timings.BeginSpan("a",r); backend.clock+=5; timings.EndSpan(a,r);
  const auto open=timings.BeginSpan("open",r); backend.clock+=5;
  Check(timings.BeginSpan("over",r)<0,"a span past the slot budget was accepted");
  timings.EndSpan(-1,r);  // Harmless.
  timings.EndFrame(r);  // Closes "open" itself so every resolved slot is written.
  (void)open;
  backend.Submit(); backend.completed=backend.frame;
  timings.Poll(backend);
  NativeGpuPassTimingWindow window;
  Check(timings.TakeReport(window),"budget window not reported");
  Check(window.dropped_spans==1 && window.invalid_spans==1,"dropped/unclosed spans were not counted");
  Check(Find(window,"a") && !Find(window,"open") && !Find(window,"over"),"an unclosed span was reported as a time");
  // A frame that never ends (a pass threw) is abandoned by the next BeginFrame.
  Check(timings.BeginFrame(backend,r),"frame not opened");
  const auto abandoned=backend.recorder.resolves;
  Check(timings.BeginFrame(backend,r),"a frame after an abandoned one was not opened");
  Check(backend.recorder.resolves==abandoned,"an abandoned frame was resolved");
  timings.EndFrame(r); backend.Submit(); backend.completed=backend.frame;
  Check(timings.Poll(backend)==1,"the frame after an abandoned one was not read");
  Check(timings.pending()==0,"an abandoned frame stayed in flight");
}

void FrameTimeRecorderSpikes() {
  NativeFrameTimeRecorder recorder;
  auto first=recorder.Record(30);
  Check(first.over_limit && !first.over_median && first.median_ms==0,"the first frame's spike is not the absolute limit");
  for(int index=0;index<40;++index) {
    const auto sample=recorder.Record(8);
    Check(!sample.spike(),"a steady 8 ms frame was reported as a spike");
  }
  const auto doubled=recorder.Record(17);
  Check(doubled.over_median && !doubled.over_limit && Near(doubled.median_ms,8) &&
        std::string(doubled.reason())=="median","a frame over twice the median was not a median spike");
  const auto both=recorder.Record(26);
  Check(both.over_median && both.over_limit && std::string(both.reason())=="limit+median","26 ms is over both bars");
  Check(!recorder.Record(15).spike(),"a frame under twice the median was a spike");
  Check(recorder.frames()==44,"frames were not counted");
}

void FrameTimeRecorderWindow() {
  NativeFrameTimeRecorder recorder({100,1e9,120,30,2.0,1000});
  NativeFrameTimeWindow window;
  for(int ms=100;ms>=1;--ms) {
    Check(!recorder.TakeReport(window),"a window closed early");
    recorder.Record(double(ms));
  }
  Check(recorder.TakeReport(window),"a full window did not close");
  Check(window.frames==100 && Near(window.p50_ms,50) && Near(window.p90_ms,90) && Near(window.p99_ms,99) &&
        Near(window.p999_ms,100) && Near(window.max_ms,100) && Near(window.mean_ms,50.5) && Near(window.span_ms,5050),
        "nearest-rank percentiles over 1..100 ms are wrong");
  uint64_t counted=0;
  for(const auto count:window.histogram) counted+=count;
  Check(counted==100 && window.histogram.size()==kNativeFrameTimeBuckets,"the histogram does not hold every frame");
  Check(!recorder.TakeReport(window),"an empty window was reported");
  // report_ms closes a slow window before report_frames.
  NativeFrameTimeRecorder slow({600,1000,120,30,2.0,25});
  for(int index=0;index<5;++index) slow.Record(200);
  Check(slow.TakeReport(window) && window.frames==5 && window.spikes==5,"a 1 s window of 200 ms frames did not close");
  slow.Record(8);
  Check(!slow.TakeReport(window) && slow.TakeReport(window,true) && window.frames==1,"force did not close a short window");
}

void FrameTimeHistogram() {
  Check(NativeFrameTimeBucket(8.1)==32 && Near(NativeFrameTimeBucketLower(32),8.0),"0.25 ms buckets");
  Check(NativeFrameTimeBucket(49.99)==199 && NativeFrameTimeBucket(50)==200,"the 50 ms bucket edge");
  Check(NativeFrameTimeBucket(60.5)==210 && Near(NativeFrameTimeBucketLower(210),60),"1 ms buckets");
  Check(NativeFrameTimeBucket(150)==255 && Near(NativeFrameTimeBucketLower(255),150),"10 ms buckets");
  Check(NativeFrameTimeBucket(999.9)==339 && NativeFrameTimeBucket(5000)==kNativeFrameTimeBuckets-1 &&
        Near(NativeFrameTimeBucketLower(kNativeFrameTimeBuckets-1),1000),"the overflow bucket");
  Check(NativeFrameTimeBucket(-1)==0 && NativeFrameTimeBucket(0)==0,"non-positive times");
  std::vector<uint32_t> histogram(kNativeFrameTimeBuckets,0);
  Check(FormatNativeFrameTimeHistogram(histogram)=="none","an empty histogram");
  histogram[32]=120; histogram[33]=300; histogram[255]=1;
  Check(FormatNativeFrameTimeHistogram(histogram)=="8.00:120,8.25:300,150.00:1",
        "histogram format: "+FormatNativeFrameTimeHistogram(histogram));
}

void FrameCounterDeltas() {
  NativeFrameCounterDeltas deltas;
  NativeFrameCounter counters[]{{"pipelines",5},{"buffers",10}};
  deltas.Update(counters);
  Check(deltas.Describe()=="pipelines=0 buffers=0","the first frame has no deltas: "+deltas.Describe());
  counters[0].value=7; counters[1].value=3;  // A recreated backend's counter goes backwards.
  deltas.Update(counters);
  Check(deltas.Describe()=="pipelines=2 buffers=0","per-frame deltas: "+deltas.Describe());
  counters[1].value=4;
  deltas.Update(counters);
  Check(deltas.Describe()=="pipelines=0 buffers=1","deltas after a reset counter: "+deltas.Describe());
}

// --- D3D12 on WARP ----------------------------------------------------------
ComPtr<ID3DBlob> Compile(const char* source,const char* entry,const char* profile) {
  ComPtr<ID3DBlob> code,errors;
  if(FAILED(D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,entry,profile,0,0,&code,&errors)))
    throw std::runtime_error(std::string("shader compile failed: ")+
                             (errors?static_cast<const char*>(errors->GetBufferPointer()):"no detail"));
  return code;
}
std::span<const uint8_t> Bytes(ID3DBlob& blob) {
  return {static_cast<const uint8_t*>(blob.GetBufferPointer()),blob.GetBufferSize()};
}
constexpr uint32_t kFormat=28;  // DXGI_FORMAT_R8G8B8A8_UNORM
const char* kShader=R"(
  cbuffer Tint : register(b0) { float4 color; };
  float4 VS(uint id:SV_VertexID):SV_Position {
    float2 p=float2((id<<1)&2,id&2); return float4(p*float2(2,-2)+float2(-1,1),0,1);
  }
  float4 PS():SV_Target { return color; }
)";
struct Scene {
  std::unique_ptr<NativeRenderBackend> backend;
  NativeBackendPipeline* pipeline=nullptr;
  std::unique_ptr<NativeBackendRenderTarget> target;
};
Scene MakeScene(uint32_t workers,uint32_t minimum) {
  NativeD3D12Options options; options.prefer_warp=true; options.debug_layer=true;
  options.geometry_workers=workers; options.geometry_minimum_draws=minimum;
  Scene scene;
  scene.backend=CreateNativeD3D12Backend(options);
  static const auto vs=Compile(kShader,"VS","vs_5_0"),ps=Compile(kShader,"PS","ps_5_0");
  NativeBackendPipelineDesc desc{}; desc.vertex=Bytes(*vs.Get()); desc.pixel=Bytes(*ps.Get());
  desc.vertex_id=900; desc.pixel_id=901; desc.state={0x10001,0,0,0,15,0};
  desc.render_targets=1; desc.rtv_format[0]=kFormat;
  scene.pipeline=&scene.backend->CreatePipeline(desc);
  NativeBackendTextureDesc target{}; target.width=64; target.height=16; target.format=kFormat; target.render_target=true;
  scene.target=scene.backend->CreateRenderTarget(target);
  return scene;
}
void Draws(NativeBackendRecorder& r,Scene& scene,unsigned first,unsigned count) {
  NativeBackendRenderTarget* targets[]{scene.target.get()};
  r.SetRenderTargets(targets,nullptr);
  r.SetViewport({0,0,64,16,0,1});
  r.SetPipeline(*scene.pipeline);
  for(unsigned x=first;x<first+count;++x) {
    const std::array<float,4> color{float(x)/63,0,1,1};
    r.SetConstants(NativeBackendStage::Pixel,0,{reinterpret_cast<const uint8_t*>(color.data()),sizeof(color)});
    r.SetScissor({int(x),0,int(x+1),16},true);
    r.Draw(3,0);
  }
}
bool ReadWithin(NativeRenderBackend& backend,NativeBackendTimestamps& set,uint32_t first,std::span<uint64_t> out) {
  for(int attempt=0;attempt<10000;++attempt) {
    if(backend.ReadTimestamps(set,first,out)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

// Timestamps between draws, read back without a wait on the backend's part:
// unreadable while the frame is open, readable once the GPU passes it, and
// in the order they were recorded.
void D3D12Timestamps(uint32_t workers,uint32_t minimum,const char* label) {
  auto scene=MakeScene(workers,minimum);
  auto& backend=*scene.backend;
  Check(backend.TimestampFrequency()>0,std::string(label)+": no timestamp frequency");
  auto set=backend.CreateTimestamps(16);
  Check(set && set->capacity()==16,std::string(label)+": timestamp set not created");
  if(!set) return;
  const auto before=backend.Statistics();
  backend.BeginFrame();
  auto& r=backend.Recorder();
  r.ClearColor(*scene.target,{0,0,0,0});
  // Markers at 8..12 of a 16-slot set, between four runs of sixteen draws.
  for(uint32_t run=0;run<4;++run) {
    r.WriteTimestamp(*set,8+run);
    Draws(r,scene,run*16,16);
  }
  r.WriteTimestamp(*set,12);
  r.ResolveTimestamps(*set,8,5);
  std::array<uint64_t,5> ticks{};
  Check(!backend.ReadTimestamps(*set,8,ticks),std::string(label)+": a range was readable before its frame was submitted");
  Check(!backend.ReadTimestamps(*set,0,std::span<uint64_t>(ticks).first(4)),
        std::string(label)+": a never-resolved range was readable");
  bool threw=false;
  try { r.WriteTimestamp(*set,16); } catch(const std::exception&) { threw=true; }
  Check(threw,std::string(label)+": a slot past the set was accepted");
  backend.Submit();
  Check(ReadWithin(backend,*set,8,ticks),std::string(label)+": the resolved range never became readable");
  for(size_t index=1;index<ticks.size();++index)
    Check(ticks[index]>=ticks[index-1],std::string(label)+": timestamps went backwards at "+std::to_string(index));
  Check(ticks[4]>ticks[0],std::string(label)+": sixty-four draws took no GPU time");
  const auto after=backend.Statistics();
  if(workers) {
    // The markers kept their places among the packets without flushing them:
    // one worker batch for the whole frame when 64 draws reach the minimum,
    // else the single serial replay at Submit.
    const bool batched=minimum<=64;
    Check(after.geometry_batches-before.geometry_batches==(batched?1u:0u),
          std::string(label)+": timestamps split the packets into "+
          std::to_string(after.geometry_batches-before.geometry_batches)+" batches");
    Check(after.geometry_serial_flushes-before.geometry_serial_flushes==(batched?0u:1u),
          std::string(label)+": timestamps forced a flush of their own");
  }
  const auto pixels=backend.ReadRenderTarget(*scene.target);
  bool drawn=true;
  for(unsigned x=0;x<64;++x) drawn=drawn && std::abs(int(pixels[x*4])-int(x*255/63))<=1 && pixels[x*4+2]==255;
  Check(drawn,std::string(label)+": the draws around the markers did not all land");
  for(const auto& message:backend.DrainValidationMessages()) Check(false,std::string(label)+" validation: "+message);
  std::cout << label << ": " << (ticks[4]-ticks[0]) << " ticks at " << backend.TimestampFrequency() << " Hz\n";
}

// The whole pass timer on WARP: frames with spans, resolved and read back by
// later frames, reported with a frame, interval and each pass.
void D3D12PassTimings(uint32_t workers,const char* label) {
  auto scene=MakeScene(workers,4);
  auto& backend=*scene.backend;
  NativeGpuPassTimings timings({4,16,1000});
  uint32_t timed=0;
  for(unsigned frame=0;frame<12;++frame) {
    backend.BeginFrame();
    auto& r=backend.Recorder();
    if(timings.BeginFrame(backend,r)) ++timed;
    r.ClearColor(*scene.target,{0,0,0,0});
    const auto sky=timings.BeginSpan("sky",r); Draws(r,scene,0,8); timings.EndSpan(sky,r);
    const auto models=timings.BeginSpan("models",r); Draws(r,scene,8,40); timings.EndSpan(models,r);
    timings.EndFrame(r);
    backend.Submit();
  }
  for(int attempt=0;attempt<10000 && timings.pending();++attempt) {
    timings.Poll(backend);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  NativeGpuPassTimingWindow window;
  Check(timings.TakeReport(window,true),std::string(label)+": no GPU timing window");
  Check(window.frames==timed && timed>=4 && window.frames+window.skipped==12,
        std::string(label)+": read "+std::to_string(window.frames)+" of "+std::to_string(timed)+" timed frames");
  const auto* frame=Find(window,"frame");
  const auto* models=Find(window,"models");
  Check(frame && models && models->frames==window.frames && frame->total_ms>=models->total_ms,
        std::string(label)+": the frame span does not contain its passes");
  Check(window.invalid_spans==0,std::string(label)+": invalid spans on a real GPU");
  for(const auto& message:backend.DrainValidationMessages()) Check(false,std::string(label)+" validation: "+message);
  if(frame) std::cout << label << ": " << window.frames << " frames, frame avg " << frame->average_ms() << " ms\n";
}
}  // namespace

int main() {
  std::cout << std::unitbuf;
  try {
    GpuPassTimingsUnsupported();
    GpuPassTimingsAggregate();
    GpuPassTimingsRingFull();
    GpuPassTimingsSpanBudget();
    FrameTimeRecorderSpikes();
    FrameTimeRecorderWindow();
    FrameTimeHistogram();
    FrameCounterDeltas();
    D3D12Timestamps(0,32,"direct timestamps");
    D3D12Timestamps(4,1,"packet timestamps");
    D3D12Timestamps(4,128,"serial packet timestamps");
    D3D12PassTimings(0,"direct pass timings");
    D3D12PassTimings(4,"packet pass timings");
  } catch(const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "native GPU timing tests: " << failures << " failures\n";
  return failures?1:0;
}

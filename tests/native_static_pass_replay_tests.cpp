#include "native_graphics/native_scene_adapter.h"
#include "native_graphics/native_scene_handoff.h"
#include "native_graphics/d3d11_backend.h"
#include "native_graphics/d3d12_backend.h"
#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <set>

using namespace edf::native;
namespace {
constexpr unsigned Width=64,Height=32;
void Require(bool value,const std::string& why) { if(!value) throw std::runtime_error(why); }
void Word(std::vector<uint8_t>& bytes,size_t offset,uint32_t value) {
  for(unsigned i=0;i<4;++i) bytes.at(offset+i)=uint8_t(value>>(24-i*8));
}
std::span<const uint8_t> Bytes(ID3DBlob* blob) {
  return {static_cast<const uint8_t*>(blob->GetBufferPointer()),blob->GetBufferSize()};
}
std::vector<uint8_t> Blank() {
  std::vector<uint8_t> pixels(Width*Height*4);
  for(size_t i=3;i<pixels.size();i+=4) pixels[i]=255;
  return pixels;
}
void Save(const std::filesystem::path& path,const std::vector<uint8_t>& pixels) {
  std::ofstream file(path,std::ios::binary);
  file<<"P6\n"<<Width<<' '<<Height<<"\n255\n";
  for(size_t i=0;i<pixels.size();i+=4) file.write(reinterpret_cast<const char*>(pixels.data()+i),3);
  Require(bool(file),"could not write replay artifact "+path.string());
}
size_t Differences(const std::vector<uint8_t>& actual,const std::vector<uint8_t>& expected) {
  Require(actual.size()==expected.size(),"readback size mismatch");
  size_t count=0;
  for(size_t i=0;i<actual.size();i+=4)
    if(!std::equal(actual.begin()+i,actual.begin()+i+4,expected.begin()+i)) ++count;
  return count;
}
struct Frame {
  std::shared_ptr<const NativeScenePublication> publication;
  std::vector<uint8_t> expected=Blank();
  size_t count=0,rectangles=0,renders=0;
};
void Replay(std::shared_ptr<NativeRenderBackend> backend,const std::string& name,
            const std::filesystem::path& fixture,const std::filesystem::path& artifacts) {
  Effect effect;
  effect.source=R"(
    row_major float4x4 g_mWorld;
    row_major float4x4 g_mViewProjection;
    struct V { float4 position:SV_Position; };
    V VS(float3 position:POSITION0) {
      V o; o.position=mul(mul(float4(position,1),g_mWorld),g_mViewProjection); return o;
    }
    float4 tint;
    float4 PS(V v):SV_Target { return tint; }
  )";
  auto vs=CompileNativeShader(nullptr,effect,{false,"VS","vs_3_0"},"static-pass-replay.fx");
  auto ps=CompileNativeShader(nullptr,effect,{true,"PS","ps_3_0"},"static-pass-replay.fx");
  auto producer=std::make_unique<NativeSceneAdapter>();
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  {
    // Exercise production big-endian geometry ingestion, then discard its input and mesh wrapper.
    std::vector<uint8_t> declaration(12),vertices(48),indices{0,0,0,1,0,2,0,0,0,2,0,3};
    Word(declaration,4,0x2a23b9);
    const float points[]{-.5f,-.5f,.5f, -.5f,.5f,.5f, .5f,.5f,.5f, .5f,-.5f,.5f};
    for(size_t i=0;i<12;++i) Word(vertices,i*4,std::bit_cast<uint32_t>(points[i]));
    NativeIndexedMesh mesh(*backend,vs,declaration,12,vertices,indices,2);
    geometry=producer->RetainGeometry(backend,mesh,0,6);
  }
  const std::weak_ptr<const NativeIndexedMesh::RetainedDraw> weak_geometry=geometry;
  std::vector<std::weak_ptr<const NativeSceneMaterial>> weak_materials;
  NativeBackendPipelineDesc desc;
  desc.vertex=Bytes(vs.bytecode.Get()); desc.pixel=Bytes(ps.bytecode.Get());
  desc.vertex_id=800; desc.pixel_id=801;
  desc.state={0x10001,0,0,0,15,0}; desc.render_targets=1;
  desc.rtv_format[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.input_layout=geometry->input_layout().elements();
  desc.input_layout_id=geometry->input_layout().fingerprint();
  auto& pipeline=backend->CreatePipeline(desc);
  auto material=[&](std::array<float,4> color) {
    std::vector<NativeSceneConstant> constants;
    for(auto [stage,shader]:{std::pair{NativeBackendStage::Vertex,&vs},std::pair{NativeBackendStage::Pixel,&ps}}) {
      D3D11_SHADER_DESC sd{}; Require(SUCCEEDED(shader->reflection->GetDesc(&sd)),"shader reflection");
      for(UINT b=0;b<sd.ConstantBuffers;++b) {
        auto* buffer=shader->reflection->GetConstantBufferByIndex(b);
        D3D11_SHADER_BUFFER_DESC cb{}; Require(SUCCEEDED(buffer->GetDesc(&cb)),"buffer reflection");
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        Require(SUCCEEDED(shader->reflection->GetResourceBindingDescByName(cb.Name,&binding)),"binding reflection");
        NativeSceneConstant image; image.stage=stage; image.slot=binding.BindPoint; image.bytes.resize(cb.Size);
        for(UINT v=0;v<cb.Variables;++v) {
          D3D11_SHADER_VARIABLE_DESC variable{};
          Require(SUCCEEDED(buffer->GetVariableByIndex(v)->GetDesc(&variable)),"variable reflection");
          if(!(variable.uFlags&D3D_SVF_USED)) continue;
          const std::string variable_name=variable.Name;
          if(variable_name=="g_mWorld") image.matrices.push_back({NativeSceneMatrixSource::World,variable.StartOffset,false});
          else if(variable_name=="g_mViewProjection") image.matrices.push_back({NativeSceneMatrixSource::ViewProjection,variable.StartOffset,false});
          else if(variable_name=="tint") std::memcpy(image.bytes.data()+variable.StartOffset,color.data(),16);
          else throw std::runtime_error("unexpected shader constant");
        }
        constants.push_back(std::move(image));
      }
    }
    auto result=std::make_shared<const NativeSceneMaterial>(backend,pipeline,std::move(constants));
    weak_materials.push_back(result);
    return result;
  };
  NativeBackendTextureDesc target_desc; target_desc.width=Width; target_desc.height=Height;
  target_desc.format=DXGI_FORMAT_R8G8B8A8_UNORM;
  auto target=backend->CreateRenderTarget(target_desc);
  NativeSceneView view; view.viewport={0,0,float(Width),float(Height),0,1};
  NativeSceneRenderer renderer;
  std::map<std::string,Frame> frames;
  std::map<std::pair<uint32_t,uint64_t>,uint64_t> ids;
  std::set<uint64_t> distinct_ids;
  std::ifstream input(fixture); Require(bool(input),"cannot open replay fixture");
  std::string line; size_t line_number=0; bool version=false,released=false;
  auto rgb=[](int r,int g,int b) { Require(r>=0&&r<=255&&g>=0&&g<=255&&b>=0&&b<=255,"invalid RGB"); };
  while(std::getline(input,line)) {
    ++line_number;
    std::istringstream row(line.substr(0,line.find('#')));
    std::string op; if(!(row>>op)) continue;
    try {
      if(op=="version") {
        int value=0; row>>value; Require(!version&&value==1,"unsupported/duplicate version"); version=true;
      } else {
        Require(version,"missing fixture version");
        if(op=="observe") {
          uint32_t owner=0; uint64_t generation=0; float x=0; int r=0,g=0,b=0;
          row>>owner>>generation>>x>>r>>g>>b; rgb(r,g,b);
          Require(producer&&geometry&&x>=-1&&x<=1,"invalid observation after release or world");
          NativeSceneMaterialCapture capture; capture.material=material({r/255.f,g/255.f,b/255.f,1});
          capture.world[12]=x;
          const auto id=producer->Observe({generation,owner,0,0},geometry,std::move(capture));
          const auto key=std::pair{owner,generation};
          if(ids.contains(key)) Require(ids.at(key)==id,"update changed native lifetime id");
          else { Require(distinct_ids.insert(id).second,"recycled owner reused native lifetime id"); ids.emplace(key,id); }
        } else if(op=="retire") {
          uint32_t owner=0; row>>owner; Require(bool(producer),"retire after release"); producer->Retire(owner);
        } else if(op=="publish") {
          std::string label; uint64_t tick=0; size_t count=0; row>>label>>tick>>count;
          Require(producer&&!frames.contains(label),"invalid publication");
          auto& frame=frames[label]; frame.publication=producer->Publish(tick); frame.count=count;
          Require(frame.publication->snapshot->instances.size()==count,"publication instance count");
        } else if(op=="rect") {
          std::string label; unsigned x0=0,y0=0,x1=0,y1=0; int r=0,g=0,b=0;
          row>>label>>x0>>y0>>x1>>y1>>r>>g>>b; rgb(r,g,b);
          Require(frames.contains(label)&&x0<x1&&y0<y1&&x1<=Width&&y1<=Height,"invalid oracle rectangle");
          auto& frame=frames.at(label); Require(!frame.renders,"oracle modified after render"); ++frame.rectangles;
          // Independent integer raster oracle: no renderer transforms or backend output used.
          for(unsigned y=y0;y<y1;++y) for(unsigned x=x0;x<x1;++x) {
            const size_t offset=(y*Width+x)*4;
            frame.expected[offset]=uint8_t(r); frame.expected[offset+1]=uint8_t(g); frame.expected[offset+2]=uint8_t(b);
          }
        } else if(op=="release_producer") {
          Require(bool(producer),"duplicate producer release"); producer.reset(); geometry.reset(); released=true;
          Require(!weak_geometry.expired(),"publications failed to retain geometry");
        } else if(op=="render") {
          std::string label; row>>label;
          Require(released&&frames.contains(label),"render must consume retained publication after producer release");
          auto& frame=frames.at(label);
          backend->BeginFrame(); auto& recorder=backend->Recorder();
          NativeBackendRenderTarget* targets[]{target.get()}; recorder.SetRenderTargets(targets,nullptr);
          recorder.ClearColor(*target,{0,0,0,1});
          NativeSceneGeometryHandoff handoff;
          handoff.Defer({});
          for(size_t i=0;i<frame.count;++i) handoff.DrawAccepted();
          NativeSceneRenderStatistics stats;
          std::vector<int> handoff_trace;
          // The same group boundary as the live bridge. CPU callbacks here
          // trace ordering only; the indexed differential suite owns CPU parity.
          FinishNativeSceneGroupHandoff(handoff,
            [&] { handoff_trace.push_back(1); },
            [&] {
              handoff_trace.push_back(2);
              stats=renderer.Render(*backend,*frame.publication->snapshot,view,1);
              backend->Submit();
            },[&](const auto&) { handoff_trace.push_back(3); },
            [] {},[&] { handoff_trace.push_back(4); },
            [&] { handoff_trace.push_back(5); });
          Require(handoff_trace==(frame.count?std::vector<int>{1,2,3,4,5}:std::vector<int>{1,2,3,5}),
            "retained replay bypassed group handoff ordering");
          auto actual=backend->ReadRenderTarget(*target);
          Require(stats.draws==frame.count&&stats.visible==frame.count&&stats.culled==0,"draw statistics mismatch");
          // Negative control proves even one changed color byte is rejected by this oracle.
          auto corrupt=frame.expected; corrupt[0]^=255;
          Require(Differences(corrupt,frame.expected)==1,"pixel oracle negative control failed");
          const auto differences=Differences(actual,frame.expected);
          if(differences) {
            std::filesystem::create_directories(artifacts);
            auto diff=Blank();
            for(size_t i=0;i<actual.size();++i) diff[i]=uint8_t(std::abs(int(actual[i])-int(frame.expected[i])));
            Save(artifacts/(name+"-"+label+"-actual.ppm"),actual);
            Save(artifacts/(name+"-"+label+"-expected.ppm"),frame.expected);
            Save(artifacts/(name+"-"+label+"-diff.ppm"),diff);
            throw std::runtime_error(std::to_string(differences)+" pixels differ; artifacts: "+artifacts.string());
          }
          ++frame.renders;
          std::cout<<name<<" frame="<<label<<" draws="<<stats.draws<<" pixels="<<Width*Height<<" exact-match\n";
        } else throw std::runtime_error("unknown replay operation "+op);
      }
      Require(!row.fail(),"missing or malformed field");
      std::string extra; Require(!(row>>extra),"unexpected trailing field");
    } catch(const std::exception& error) {
      throw std::runtime_error(fixture.string()+":"+std::to_string(line_number)+": "+error.what());
    }
  }
  Require(input.eof()&&version&&released&&frames.size()==4&&ids.size()==3,"incomplete replay fixture");
  for(const auto& [label,frame]:frames) Require(frame.renders&&frame.rectangles==frame.count,"unrendered frame or missing oracle");
  frames.clear();
  Require(weak_geometry.expired(),"geometry leaked after producer and all publications were released");
  for(const auto& weak:weak_materials) Require(weak.expired(),"material leaked after publications released");
  for(const auto& message:backend->DrainValidationMessages()) throw std::runtime_error(message);
  std::cout<<name<<" retained geometry/material release checks passed\n";
}
}
int main(int argc,char** argv) {
  try {
    Require(argc==3,"usage: edf_native_static_pass_replay_tests <fixture> <artifact-directory>");
    Replay(CreateNativeD3D11Backend({true,true}),"d3d11",argv[1],argv[2]);
    NativeD3D12Options options; options.prefer_warp=true; options.debug_layer=true;
    options.geometry_workers=2; options.geometry_minimum_draws=1;
    Replay(CreateNativeD3D12Backend(options),"d3d12",argv[1],argv[2]);
    std::cout<<"Static-pass replay v1 passed (synthetic; no guest hooks or gameplay exercised)\n";
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}

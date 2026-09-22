#include "native_static_group_gpu.h"
#include "native_graphics/native_scene_adapter.h"
#include "native_graphics/d3d11_backend.h"
#include "native_graphics/d3d12_backend.h"
#include <cstring>
#include <iostream>

using namespace edf::native;
namespace {
constexpr unsigned Width=64,Height=32;
void Require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
std::span<const uint8_t> Bytes(ID3DBlob* blob) {
  return {static_cast<const uint8_t*>(blob->GetBufferPointer()),blob->GetBufferSize()};
}
std::vector<uint8_t> Expected(unsigned draws,bool changed) {
  std::vector<uint8_t> result(Width*Height*4);
  for(size_t i=3;i<result.size();i+=4) result[i]=255;
  // Independently specified integer coverage, not a transform of supplied input.
  if(draws) for(unsigned y=8;y<24;++y) for(unsigned x=16;x<48;++x)
    result[(y*Width+x)*4+(changed?2:0)]=255;
  return result;
}
class Replay {
 public:
  explicit Replay(std::shared_ptr<NativeRenderBackend> backend):backend_(std::move(backend)) {
    Effect effect;
    effect.source=R"(
      row_major float4x4 g_mWorld;
      struct V { float4 position:SV_Position; };
      V VS(float3 position:POSITION0) { V o; o.position=mul(float4(position,1),g_mWorld); return o; }
      float4 tint;
      float4 PS(V v):SV_Target { return tint; }
    )";
    vs_=CompileNativeShader(nullptr,effect,{false,"VS","vs_3_0"},"static-group-cpu-gpu.fx");
    ps_=CompileNativeShader(nullptr,effect,{true,"PS","ps_3_0"},"static-group-cpu-gpu.fx");
    NativeBackendTextureDesc desc; desc.width=Width; desc.height=Height;
    desc.format=DXGI_FORMAT_R8G8B8A8_UNORM;
    target_=backend_->CreateRenderTarget(desc);
    view_.viewport={0,0,float(Width),float(Height),0,1};
  }
  void Run(const NativeStaticGroupGpuInputs& input,unsigned draws,bool changed) {
    std::shared_ptr<const NativeScenePublication> publication;
    std::weak_ptr<const NativeIndexedMesh::RetainedDraw> weak_geometry;
    std::weak_ptr<const NativeSceneMaterial> weak_material;
    {
      NativeSceneAdapter producer;
      NativeIndexedMesh mesh(*backend_,vs_,input.declaration,input.stride,input.vertices,input.indices,2);
      auto geometry=producer.RetainGeometry(backend_,mesh,0,6);
      weak_geometry=geometry;
      NativeBackendPipelineDesc desc;
      desc.vertex=Bytes(vs_.bytecode.Get()); desc.pixel=Bytes(ps_.bytecode.Get());
      desc.vertex_id=0x60000; desc.pixel_id=0x68000;
      desc.state=input.state; desc.render_targets=1;
      desc.rtv_format[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
      desc.input_layout=geometry->input_layout().elements();
      desc.input_layout_id=geometry->input_layout().fingerprint();
      auto& pipeline=backend_->CreatePipeline(desc);
      std::vector<NativeSceneConstant> constants;
      for(auto [stage,shader]:{std::pair{NativeBackendStage::Vertex,&vs_},std::pair{NativeBackendStage::Pixel,&ps_}}) {
        D3D11_SHADER_DESC sd{};
        Require(SUCCEEDED(shader->reflection->GetDesc(&sd)),"group shader reflection failed");
        for(UINT b=0;b<sd.ConstantBuffers;++b) {
          auto* buffer=shader->reflection->GetConstantBufferByIndex(b);
          D3D11_SHADER_BUFFER_DESC cb{};
          Require(SUCCEEDED(buffer->GetDesc(&cb)),"group buffer reflection failed");
          D3D11_SHADER_INPUT_BIND_DESC binding{};
          Require(SUCCEEDED(shader->reflection->GetResourceBindingDescByName(cb.Name,&binding)),"group binding reflection failed");
          NativeSceneConstant image; image.stage=stage; image.slot=binding.BindPoint; image.bytes.resize(cb.Size);
          for(UINT v=0;v<cb.Variables;++v) {
            D3D11_SHADER_VARIABLE_DESC variable{};
            Require(SUCCEEDED(buffer->GetVariableByIndex(v)->GetDesc(&variable)),"group variable reflection failed");
            if(!(variable.uFlags&D3D_SVF_USED)) continue;
            const std::string name=variable.Name;
            if(name=="g_mWorld") image.matrices.push_back({NativeSceneMatrixSource::World,variable.StartOffset,false});
            else if(name=="tint") std::memcpy(image.bytes.data()+variable.StartOffset,input.tint.data(),16);
            else throw std::runtime_error("unexpected group shader variable");
          }
          constants.push_back(std::move(image));
        }
      }
      auto material=std::make_shared<const NativeSceneMaterial>(backend_,pipeline,std::move(constants));
      weak_material=material;
      for(unsigned i=0;i<draws;++i) {
        NativeSceneMaterialCapture capture; capture.material=material; capture.world=input.world;
        producer.Observe({++generation_,0x90000+i,0,0},geometry,std::move(capture));
      }
      publication=producer.Publish(generation_);
      // All guest-derived input storage, producer and wrapper ownership may go
      // away before rendering. The publication must retain the actual assets.
    }
    const auto expected=Expected(draws,changed);
    auto damaged=expected; damaged[0]^=255;
    Require(damaged!=expected,"group pixel negative control failed");
    Render(publication,draws,expected);
    if(previous_) Render(previous_,previous_draws_,previous_expected_);
    Render(publication,draws,expected);
    // Release old frames only after replaying them out of order. The backend may
    // retain command resources, but scene ownership must not leak the wrappers.
    previous_.reset();
    Require(previous_geometry_.expired() && previous_material_.expired(),"old group publication leaked scene assets");
    previous_=std::move(publication); previous_draws_=draws; previous_expected_=expected;
    previous_geometry_=weak_geometry; previous_material_=weak_material;
    for(const auto& message:backend_->DrainValidationMessages()) throw std::runtime_error(message);
  }
  void Finish() {
    previous_.reset();
    Require(previous_geometry_.expired() && previous_material_.expired(),"final group publication leaked scene assets");
  }
 private:
  void Render(const std::shared_ptr<const NativeScenePublication>& publication,unsigned draws,const std::vector<uint8_t>& expected) {
    backend_->BeginFrame(); auto& recorder=backend_->Recorder();
    NativeBackendRenderTarget* targets[]{target_.get()}; recorder.SetRenderTargets(targets,nullptr);
    recorder.ClearColor(*target_,{0,0,0,1});
    const auto stats=renderer_.Render(*backend_,*publication->snapshot,view_,1);
    backend_->Submit();
    Require(stats.draws==draws && stats.visible==draws && !stats.culled,"integrated group draw count differs");
    const auto pixels=backend_->ReadRenderTarget(*target_);
    const auto compare=[&](const auto& oracle) {
      Require(pixels==oracle,"integrated group pixels differ from independent oracle");
    };
    compare(expected);
    auto wrong=expected; wrong[0]^=255;
    bool rejected=false;
    try { compare(wrong); } catch(const std::runtime_error&) { rejected=true; }
    Require(rejected,"integrated pixel comparator accepted a changed expected byte");
  }
  std::shared_ptr<NativeRenderBackend> backend_;
  NativeShader vs_,ps_;
  std::unique_ptr<NativeBackendRenderTarget> target_;
  NativeSceneView view_;
  NativeSceneRenderer renderer_;
  uint64_t generation_=0;
  std::shared_ptr<const NativeScenePublication> previous_;
  unsigned previous_draws_=0;
  std::vector<uint8_t> previous_expected_;
  std::weak_ptr<const NativeIndexedMesh::RetainedDraw> previous_geometry_;
  std::weak_ptr<const NativeSceneMaterial> previous_material_;
};
Replay& Backend(unsigned index) {
  static Replay d3d11(CreateNativeD3D11Backend({true,true}));
  static Replay d3d12([] {
    NativeD3D12Options options; options.prefer_warp=true; options.debug_layer=true;
    options.geometry_workers=2; options.geometry_minimum_draws=1;
    return CreateNativeD3D12Backend(options);
  }());
  return index?d3d12:d3d11;
}
}
void VerifyNativeStaticGroupGpu(const NativeStaticGroupGpuInputs& input,unsigned draws,bool changed) {
  Backend(0).Run(input,draws,changed); Backend(1).Run(input,draws,changed);
}
void FinishNativeStaticGroupGpu() {
  Backend(0).Finish(); Backend(1).Finish();
  std::cout<<"Static group CPU/GPU: D3D11 and D3D12 WARP exact pixels, repeated/out-of-order publications, final release passed\n";
}

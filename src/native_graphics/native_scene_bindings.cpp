#include "native_scene_bindings.h"
#include "native_queued_scene.h"
#include <algorithm>
#include <cstring>

namespace edf::native {
void ApplyNativeScenePublishedWorld(NativeSceneMaterialCapture& capture,std::span<const uint8_t,64> registers) {
  if(!capture.material) throw std::runtime_error("published world requires a native material");
  const NativeSceneMatrixBinding* world=nullptr;
  for(const auto& constant:capture.material->constants()) for(const auto& matrix:constant.matrices)
    if(matrix.source==NativeSceneMatrixSource::World) {
      if(world || constant.stage!=NativeBackendStage::Vertex)
        throw std::runtime_error("published world requires one vertex world matrix");
      world=&matrix;
    }
  if(!world) throw std::runtime_error("published world matrix missing");
  capture.world=DecodeNativeQueuedWorld(registers,world->column_major);
}
bool NativeSceneCaptureBindsWorld(const NativeSceneMaterialCapture& capture) {
  if(!capture.material) return false;
  for(const auto& constant:capture.material->constants()) for(const auto& matrix:constant.matrices)
    if(matrix.source==NativeSceneMatrixSource::World) return true;
  return false;
}
namespace {
bool Matches(const NativeSceneMaterial& material,const ShaderBindings& vertex,const ShaderBindings& pixel) {
  if(material.constants().size()!=vertex.ConstantImages().size()+pixel.ConstantImages().size() ||
     material.textures().size()!=pixel.TextureImages().size() || material.samplers().size()!=pixel.SamplerImages().size()) return false;
  for(const auto& constant:material.constants()) {
    const auto images=(constant.stage==NativeBackendStage::Vertex?vertex:pixel).ConstantImages();
    const auto image=std::find_if(images.begin(),images.end(),[&](const auto& i) { return i.slot==constant.slot; });
    if(image==images.end() || image->bytes.size()!=constant.bytes.size()) return false;
    size_t cursor=0;
    // Reflection order need not be offset order. Find the next matrix range
    // without allocating or copying a full constant-buffer image.
    while(cursor<constant.bytes.size()) {
      size_t next=constant.bytes.size();
      for(const auto& matrix:constant.matrices) if(matrix.offset>=cursor) next=(std::min)(next,size_t(matrix.offset));
      if(!std::equal(constant.bytes.begin()+cursor,constant.bytes.begin()+next,image->bytes.begin()+cursor)) return false;
      if(next==constant.bytes.size()) break;
      cursor=next+64;
    }
  }
  for(size_t i=0;i<material.textures().size();++i) {
    const auto& expected=material.textures()[i]; const auto& actual=pixel.TextureImages()[i];
    if(expected.slot!=actual.slot || expected.texture.get()!=actual.texture) return false;
  }
  for(size_t i=0;i<material.samplers().size();++i) {
    const auto& expected=material.samplers()[i]; const auto& actual=pixel.SamplerImages()[i];
    if(expected.first!=actual.slot || expected.second!=actual.sampler) return false;
  }
  return true;
}
NativeSceneMaterialCapture Refresh(std::shared_ptr<const NativeSceneMaterial> material,
    const ShaderBindings& vertex,const ShaderBindings& pixel) {
  NativeSceneMaterialCapture result; result.material=std::move(material);
  std::optional<NativeSceneMatrix> world,view,projection,vp;
  const auto assign=[](std::optional<NativeSceneMatrix>& destination,const NativeSceneMatrix& value) {
    if(destination && *destination!=value) throw std::runtime_error("scene shader stages disagree on matrix state");
    destination=value;
  };
  for(const auto& constant:result.material->constants()) {
    const auto images=(constant.stage==NativeBackendStage::Vertex?vertex:pixel).ConstantImages();
    const auto image=std::find_if(images.begin(),images.end(),[&](const auto& i) { return i.slot==constant.slot; });
    for(const auto& matrix:constant.matrices) {
      NativeSceneMatrix value;
      std::memcpy(value.data(),image->bytes.data()+matrix.offset,64);
      if(matrix.column_major) value=NativeSceneTranspose(value);
      switch(matrix.source) {
        case NativeSceneMatrixSource::World: assign(world,value); break;
        case NativeSceneMatrixSource::View: assign(view,value); break;
        case NativeSceneMatrixSource::Projection: assign(projection,value); break;
        case NativeSceneMatrixSource::ViewProjection: assign(vp,value); break;
        case NativeSceneMatrixSource::ViewTranspose: assign(view,NativeSceneTranspose(value)); break;
        default: throw std::runtime_error("unsupported retained scene capture matrix");
      }
    }
  }
  if(!world || (!vp && !(view && projection))) throw std::runtime_error("retained scene capture has incomplete matrices");
  result.world=*world; result.camera.view=view.value_or(kNativeSceneIdentity);
  result.camera.projection=projection.value_or(kNativeSceneIdentity); result.camera.view_projection=vp;
  return result;
}
}
NativeSceneMaterialCapture CaptureNativeSceneMaterial(
    std::shared_ptr<NativeRenderBackend> backend,NativeBackendPipeline& pipeline,
    const ShaderBindings& vertex,const ShaderBindings& pixel,
    std::optional<std::array<float,4>> blend_factor,std::shared_ptr<const NativeSceneMaterial> previous,bool palette) {
  if(vertex.BindsResources()) throw std::runtime_error("native scene capture does not support vertex textures");
  if(!palette && previous && previous->backend()==backend.get() && previous->pipeline()==&pipeline &&
     previous->blend_factor()==blend_factor && Matches(*previous,vertex,pixel))
    return Refresh(std::move(previous),vertex,pixel);
  const auto world=vertex.ReadFloat4x4("g_mWorld");
  if(!world && !palette) throw std::runtime_error("native rigid scene capture requires g_mWorld");
  NativeSceneMaterialCapture result; result.world=world.value_or(kNativeSceneIdentity);
  std::vector<NativeSceneConstant> constants;
  std::optional<NativeSceneMatrix> view,projection,view_projection;
  for(auto [stage,bindings]:{std::pair{NativeBackendStage::Vertex,&vertex},std::pair{NativeBackendStage::Pixel,&pixel}}) {
    for(const auto& image:bindings->ConstantImages()) {
      NativeSceneConstant constant;
      constant.stage=stage; constant.slot=image.slot;
      constant.bytes.assign(image.bytes.begin(),image.bytes.end());
      constants.push_back(std::move(constant));
    }
    const auto assign=[](std::optional<NativeSceneMatrix>& destination,const NativeSceneMatrix& value) {
      if(destination && *destination!=value) throw std::runtime_error("scene shader stages disagree on camera state");
      destination=value;
    };
    D3D11_SHADER_DESC shader_desc{};
    auto* reflection=bindings->shader().reflection.Get();
    if(!reflection || FAILED(reflection->GetDesc(&shader_desc))) throw std::runtime_error("missing scene shader reflection");
    for(UINT b=0;b<shader_desc.ConstantBuffers;++b) {
      auto* buffer=reflection->GetConstantBufferByIndex(b);
      D3D11_SHADER_BUFFER_DESC cb{};
      if(FAILED(buffer->GetDesc(&cb))) throw std::runtime_error("scene constant reflection failed");
      D3D11_SHADER_INPUT_BIND_DESC binding{};
      if(FAILED(reflection->GetResourceBindingDescByName(cb.Name,&binding))) throw std::runtime_error("scene constant slot reflection failed");
      auto constant=std::find_if(constants.begin(),constants.end(),[&](const auto& c) { return c.stage==stage && c.slot==binding.BindPoint; });
      if(constant==constants.end()) throw std::runtime_error("scene constant image missing");
      for(UINT v=0;v<cb.Variables;++v) {
        auto* variable=buffer->GetVariableByIndex(v);
        D3D11_SHADER_VARIABLE_DESC desc{}; D3D11_SHADER_TYPE_DESC type{};
        if(FAILED(variable->GetDesc(&desc)) || FAILED(variable->GetType()->GetDesc(&type)))
          throw std::runtime_error("scene variable reflection failed");
        if(!(desc.uFlags&D3D_SVF_USED)) continue;
        const std::string name=desc.Name;
        const bool matrix=type.Class==D3D_SVC_MATRIX_COLUMNS || type.Class==D3D_SVC_MATRIX_ROWS;
        if(!matrix) continue;
        // The palette is object data the caller bound as registers; it stays in the image.
        if(palette && stage==NativeBackendStage::Vertex && name=="g_mWorldArray" && type.Elements) continue;
        if(type.Rows!=4 || type.Columns!=4 || type.Elements || type.Type!=D3D_SVT_FLOAT)
          throw std::runtime_error("unsupported scene matrix shape: "+name);
        const auto value=bindings->ReadFloat4x4(name);
        if(!value) throw std::runtime_error("missing scene matrix value: "+name);
        NativeSceneMatrixSource source;
        if(name=="g_mWorld") {
          if(*value!=result.world) throw std::runtime_error("scene shader stages disagree on object transform");
          source=NativeSceneMatrixSource::World;
        } else if(name=="g_mView") { source=NativeSceneMatrixSource::View; assign(view,*value); }
        else if(name=="g_mViewTranspose") { source=NativeSceneMatrixSource::ViewTranspose; assign(view,NativeSceneTranspose(*value)); }
        else if(name=="g_mProjection") { source=NativeSceneMatrixSource::Projection; assign(projection,*value); }
        else if(name=="g_mViewProjection") { source=NativeSceneMatrixSource::ViewProjection; assign(view_projection,*value); }
        else throw std::runtime_error("unsupported native scene matrix: "+name);
        constant->matrices.push_back({source,desc.StartOffset,type.Class==D3D_SVC_MATRIX_COLUMNS});
        // Object/camera values do not belong to the immutable material image.
        if(desc.StartOffset>constant->bytes.size() || constant->bytes.size()-desc.StartOffset<64)
          throw std::runtime_error("native scene matrix exceeds constant image");
        std::fill_n(constant->bytes.begin()+desc.StartOffset,64,uint8_t(0));
      }
    }
  }
  if(view) result.camera.view=*view;
  if(projection) result.camera.projection=*projection;
  result.camera.view_projection=view_projection;
  // Culling needs the complete camera even when the shader exposes separate
  // matrices. A partial camera must not silently use identity for the missing half.
  if(!view_projection && !(view && projection)) throw std::runtime_error("scene capture has no complete camera transform");
  std::vector<NativeSceneTexture> textures;
  std::vector<NativeSceneSampler> samplers;
  for(const auto& image:pixel.TextureImages()) textures.push_back({image.slot,pixel.RetainTexture(image.slot)});
  for(const auto& image:pixel.SamplerImages()) samplers.push_back({image.slot,image.sampler});
  result.material=std::make_shared<NativeSceneMaterial>(std::move(backend),pipeline,
    std::move(constants),std::move(textures),std::move(samplers),blend_factor);
  return result;
}
}

#include "native_immediate_drawer.h"
#include <bit>
#include <cmath>
#include <stdexcept>

namespace edf::native {
namespace {
struct Texture final : rex::ui::ImmediateTexture {
  Texture(UINT width,UINT height,NativeUiTexture value)
      : ImmediateTexture(width,height),native(std::move(value)) {}
  NativeUiTexture native;
};
static_assert(sizeof(rex::ui::ImmediateVertex)==sizeof(NativeUiVertex));
static_assert(offsetof(rex::ui::ImmediateVertex,color)==offsetof(NativeUiVertex,color));
static_assert(offsetof(rex::ui::ImmediateVertex,u)==offsetof(NativeUiVertex,u));
}
NativeImmediateDrawer::NativeImmediateDrawer(ID3D11Device& device,ID3D11DeviceContext& context)
    : device_(&device),context_(&context),renderer_(device) {
  Microsoft::WRL::ComPtr<ID3D11Device> owner; context.GetDevice(&owner);
  if(owner.Get()!=&device || context.GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)
    throw std::runtime_error("native immediate drawer context mismatch");
}
std::unique_ptr<rex::ui::ImmediateTexture> NativeImmediateDrawer::CreateTexture(
    uint32_t width,uint32_t height,rex::ui::ImmediateTextureFilter filter,bool repeated,const uint8_t* data) {
  if(!data || !width || !height || width>16384 || height>16384 ||
     (filter!=rex::ui::ImmediateTextureFilter::kNearest && filter!=rex::ui::ImmediateTextureFilter::kLinear))
    throw std::runtime_error("invalid SDK native UI texture request");
  return std::make_unique<Texture>(width,height,CreateNativeUiTexture(*device_.Get(),width,height,
    {data,size_t(width)*height*4},filter==rex::ui::ImmediateTextureFilter::kLinear,repeated));
}
void NativeImmediateDrawer::Begin(rex::ui::UIDrawContext& context,float width,float height) {
  if(ui_draw_context()) throw std::runtime_error("native UI Begin nested");
  if(!context.render_target_width() || !context.render_target_height() ||
     context.render_target_width()>16384 || context.render_target_height()>16384 ||
     !std::isfinite(width) || !std::isfinite(height))
    throw std::runtime_error("invalid native UI coordinate space");
  context_->OMGetRenderTargets(1,target_.ReleaseAndGetAddressOf(),nullptr);
  if(!target_) throw std::runtime_error("native UI has no bound target");
  ImmediateDrawer::Begin(context,width,height);
}
void NativeImmediateDrawer::BeginDrawBatch(const rex::ui::ImmediateDrawBatch& batch) {
  if(!ui_draw_context() || batch_active_) throw std::runtime_error("native UI batch lifecycle violation");
  if(batch.vertex_count<0 || batch.index_count<0 || batch.vertex_count>1048576 || batch.index_count>4194304 ||
     (batch.vertex_count && !batch.vertices) || (batch.index_count && !batch.indices))
    throw std::runtime_error("invalid SDK native UI batch");
  std::vector<NativeUiVertex> vertices; vertices.reserve(size_t(batch.vertex_count));
  // Matching layout alone does not permit aliasing two unrelated C++ structs.
  for(int i=0;i<batch.vertex_count;++i) vertices.push_back(std::bit_cast<NativeUiVertex>(batch.vertices[i]));
  renderer_.Upload(*context_.Get(),vertices,{batch.indices,size_t(batch.index_count)});
  batch_active_=true;
}
void NativeImmediateDrawer::Draw(const rex::ui::ImmediateDraw& draw) {
  if(!ui_draw_context() || !batch_active_) throw std::runtime_error("native UI draw outside batch");
  if(draw.count<0 || draw.index_offset<0 ||
     (draw.primitive_type!=rex::ui::ImmediatePrimitiveType::kTriangles && draw.primitive_type!=rex::ui::ImmediatePrimitiveType::kLines))
    throw std::runtime_error("invalid SDK native UI draw");
  NativeUiDraw native;
  native.count=UINT(draw.count); native.index_offset=UINT(draw.index_offset); native.base_vertex=draw.base_vertex;
  native.lines=draw.primitive_type==rex::ui::ImmediatePrimitiveType::kLines;
  if(draw.texture) {
    const auto* texture=dynamic_cast<const Texture*>(draw.texture);
    if(!texture) throw std::runtime_error("native UI received a foreign SDK texture");
    native.texture=&texture->native;
  }
  uint32_t left,top,width,height;
  if(!ScissorToRenderTarget(draw,left,top,width,height)) return;
  native.scissor={LONG(left),LONG(top),LONG(left+width),LONG(top+height)};
  renderer_.Draw(*context_.Get(),*target_.Get(),ui_draw_context()->render_target_width(),
    ui_draw_context()->render_target_height(),coordinate_space_width(),coordinate_space_height(),native);
}
void NativeImmediateDrawer::EndDrawBatch() {
  if(!ui_draw_context() || !batch_active_) throw std::runtime_error("native UI batch end without begin");
  batch_active_=false;
  renderer_.Upload(*context_.Get(),{},{});
}
void NativeImmediateDrawer::End() {
  // Also usable by the app's exception cleanup when a batch failed midway.
  batch_active_=false; target_.Reset();
  ImmediateDrawer::End();
}
}

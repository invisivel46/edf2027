#include "native_backend_immediate_drawer.h"
#include <bit>
#include <cmath>
#include <stdexcept>

namespace edf::native {
namespace {
struct Texture final : rex::ui::ImmediateTexture {
  Texture(uint32_t width,uint32_t height,NativeBackendUiTexture value)
      : ImmediateTexture(width,height),native(std::move(value)) {}
  NativeBackendUiTexture native;
};
static_assert(sizeof(rex::ui::ImmediateVertex)==sizeof(NativeBackendUiVertex));
static_assert(offsetof(rex::ui::ImmediateVertex,color)==offsetof(NativeBackendUiVertex,color));
static_assert(offsetof(rex::ui::ImmediateVertex,u)==offsetof(NativeBackendUiVertex,u));
}
NativeBackendImmediateDrawer::NativeBackendImmediateDrawer(std::shared_ptr<NativeRenderBackend> backend)
    : backend_(std::move(backend)),renderer_(*backend_) {}
void NativeBackendImmediateDrawer::SetTarget(NativeBackendRenderTarget* target) {
  if(ui_draw_context()) throw std::runtime_error("backend UI target changed during drawing");
  target_=target;
}
std::unique_ptr<rex::ui::ImmediateTexture> NativeBackendImmediateDrawer::CreateTexture(
    uint32_t width,uint32_t height,rex::ui::ImmediateTextureFilter filter,bool repeated,const uint8_t* data) {
  if(!data || !width || !height || width>16384 || height>16384 ||
     (filter!=rex::ui::ImmediateTextureFilter::kNearest && filter!=rex::ui::ImmediateTextureFilter::kLinear))
    throw std::runtime_error("invalid SDK native UI texture request");
  return std::make_unique<Texture>(width,height,renderer_.CreateTexture(width,height,
    {data,size_t(width)*height*4},filter==rex::ui::ImmediateTextureFilter::kLinear,repeated));
}
void NativeBackendImmediateDrawer::Begin(rex::ui::UIDrawContext& context,float width,float height) {
  if(ui_draw_context()) throw std::runtime_error("native UI Begin nested");
  if(!context.render_target_width() || !context.render_target_height() ||
     context.render_target_width()>16384 || context.render_target_height()>16384 ||
     !std::isfinite(width) || !std::isfinite(height))
    throw std::runtime_error("invalid native UI coordinate space");
  if(!target_ || target_->width()!=context.render_target_width() || target_->height()!=context.render_target_height())
    throw std::runtime_error("backend UI target dimensions mismatch");
  ImmediateDrawer::Begin(context,width,height);
}
void NativeBackendImmediateDrawer::BeginDrawBatch(const rex::ui::ImmediateDrawBatch& batch) {
  if(!ui_draw_context() || batch_active_) throw std::runtime_error("native UI batch lifecycle violation");
  if(batch.vertex_count<0 || batch.index_count<0 || batch.vertex_count>1048576 || batch.index_count>4194304 ||
     (batch.vertex_count && !batch.vertices) || (batch.index_count && !batch.indices))
    throw std::runtime_error("invalid SDK native UI batch");
  std::vector<NativeBackendUiVertex> vertices; vertices.reserve(size_t(batch.vertex_count));
  // Matching layout alone does not permit aliasing two unrelated C++ structs.
  for(int i=0;i<batch.vertex_count;++i) vertices.push_back(std::bit_cast<NativeBackendUiVertex>(batch.vertices[i]));
  renderer_.Upload(backend_->Recorder(),vertices,{batch.indices,size_t(batch.index_count)});
  batch_active_=true;
}
void NativeBackendImmediateDrawer::Draw(const rex::ui::ImmediateDraw& draw) {
  if(!ui_draw_context() || !batch_active_) throw std::runtime_error("native UI draw outside batch");
  if(draw.count<0 || draw.index_offset<0 ||
     (draw.primitive_type!=rex::ui::ImmediatePrimitiveType::kTriangles && draw.primitive_type!=rex::ui::ImmediatePrimitiveType::kLines))
    throw std::runtime_error("invalid SDK native UI draw");
  NativeBackendUiDraw native;
  native.count=uint32_t(draw.count); native.index_offset=uint32_t(draw.index_offset); native.base_vertex=draw.base_vertex;
  native.lines=draw.primitive_type==rex::ui::ImmediatePrimitiveType::kLines;
  if(draw.texture) {
    const auto* texture=dynamic_cast<const Texture*>(draw.texture);
    if(!texture) throw std::runtime_error("native UI received a foreign SDK texture");
    native.texture=&texture->native;
  }
  uint32_t left,top,width,height;
  if(!ScissorToRenderTarget(draw,left,top,width,height)) return;
  native.scissor={int32_t(left),int32_t(top),int32_t(left+width),int32_t(top+height)};
  renderer_.Draw(backend_->Recorder(),*target_,coordinate_space_width(),coordinate_space_height(),native);
}
void NativeBackendImmediateDrawer::EndDrawBatch() {
  if(!ui_draw_context() || !batch_active_) throw std::runtime_error("native UI batch end without begin");
  batch_active_=false;
  renderer_.Upload(backend_->Recorder(),{},{});
}
void NativeBackendImmediateDrawer::End() {
  // Also usable by the app's exception cleanup when a batch failed midway.
  batch_active_=false;
  ImmediateDrawer::End();
}
}

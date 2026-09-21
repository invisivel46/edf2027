#pragma once
#include "native_backend_ui.h"
#include <rex/ui/immediate_drawer.h>

namespace edf::native {
// SDK overlays recorded on the host backend. SetTarget brackets each UI frame;
// the host owns submission and the target lifetime.
class NativeBackendImmediateDrawer final : public rex::ui::ImmediateDrawer {
 public:
  explicit NativeBackendImmediateDrawer(std::shared_ptr<NativeRenderBackend> backend);
  void SetTarget(NativeBackendRenderTarget* target);
  std::unique_ptr<rex::ui::ImmediateTexture> CreateTexture(uint32_t width,uint32_t height,
      rex::ui::ImmediateTextureFilter filter,bool repeated,const uint8_t* data) override;
  void Begin(rex::ui::UIDrawContext& context,float width,float height) override;
  void BeginDrawBatch(const rex::ui::ImmediateDrawBatch& batch) override;
  void Draw(const rex::ui::ImmediateDraw& draw) override;
  void EndDrawBatch() override;
  void End() override;
 private:
  std::shared_ptr<NativeRenderBackend> backend_;
  NativeBackendRenderTarget* target_=nullptr;
  NativeBackendUiRenderer renderer_;
  bool batch_active_=false;
};
}

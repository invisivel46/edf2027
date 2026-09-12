#pragma once
#include "d3d11_ui.h"
#include <rex/ui/immediate_drawer.h>

namespace edf::native {
// SDK host overlays on app-owned D3D11 targets. The app binds the target before
// Begin and invokes the whole UI draw inside the isolated presentation scope.
class NativeImmediateDrawer final : public rex::ui::ImmediateDrawer {
 public:
  NativeImmediateDrawer(ID3D11Device& device,ID3D11DeviceContext& context);
  std::unique_ptr<rex::ui::ImmediateTexture> CreateTexture(uint32_t width,uint32_t height,
      rex::ui::ImmediateTextureFilter filter,bool repeated,const uint8_t* data) override;
  void Begin(rex::ui::UIDrawContext& context,float width,float height) override;
  void BeginDrawBatch(const rex::ui::ImmediateDrawBatch& batch) override;
  void Draw(const rex::ui::ImmediateDraw& draw) override;
  void EndDrawBatch() override;
  void End() override;
 private:
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
  NativeUiRenderer renderer_;
  bool batch_active_=false;
};
}

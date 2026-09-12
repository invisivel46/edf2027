#pragma once
#include "guest_draw_state.h"
#include <map>
#include <optional>
#include <stdexcept>

namespace edf::native {
// Shader identity is published by setters, not inferred from material activation.
// External synchronization is the bridge submission/state lock pair.
class NativeShaderState {
 public:
  void Set(uint32_t device,uint32_t shader,bool pixel) {
    auto& binding=bindings_[device];
    (pixel?binding.pixel:binding.vertex)=shader;
  }
  uint32_t Vertex(uint32_t device) const {
    const auto& binding=bindings_.at(device);
    if(!binding.vertex) throw std::runtime_error("native vertex shader binding not published");
    return *binding.vertex;
  }
  GuestShaderPair Pair(uint32_t device) const {
    const auto& binding=bindings_.at(device);
    if(!binding.pixel || !binding.vertex) throw std::runtime_error("native shader pair not published");
    return {*binding.pixel,*binding.vertex};
  }
 private:
  struct Binding { std::optional<uint32_t> pixel,vertex; };
  std::map<uint32_t,Binding> bindings_;
};
}

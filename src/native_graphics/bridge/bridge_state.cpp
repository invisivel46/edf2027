#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "bridge_state.h"

namespace edf::native {
ShaderBindings& VertexBindingsForDraw(RegisteredShader& shader,bool reverse_depth) {
  if(!reverse_depth) return *shader.bindings;
  if(shader.reversed_mirrors) shader.reversed_bindings->MirrorConstantsFrom(*shader.bindings);
  return *shader.reversed_bindings;
}
NativeBufferWrites& BufferWrites() {
  static NativeBufferWrites writes;
  return writes;
}
thread_local MovieDecodeLocks* active_movie_decode=nullptr;
NativePacingState& PacingState() {
  static NativePacingState state;
  return state;
}
Bridge& State() { static Bridge state; return state; }
}  // namespace edf::native

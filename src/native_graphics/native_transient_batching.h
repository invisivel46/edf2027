#pragma once
#include "native_render_backend.h"
#include <d3d11shader.h>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

namespace edf::native {
// What makes a pipeline's list draws concatenable (NativeBackendPipeline::
// transient_batchable). A packet recorder appends one transient list draw's
// vertices to the draw before it only when the result cannot tell the
// difference, and three things could:
//
//   * a second vertex stream: a merged draw's second half would read slot 1+
//     from where the first half's elements end, not from the start. Every
//     element must come from slot 0, per vertex;
//   * SV_VertexID / SV_InstanceID in the vertex stage, which the merged draw
//     numbers on from the first half's count;
//   * SV_PrimitiveID in the pixel stage, for the same reason.
//
// Anything else a list draw computes is a function of its vertices and its
// bound state, which the recorder compares before it appends.
inline bool NativeInputLayoutSlotZeroOnly(std::span<const NativeBackendInputElement> layout) {
  for(const auto& element:layout) if(element.slot!=0 || element.per_instance) return false;
  return true;
}
// A shader without reflection is assumed to number them: nothing proves it
// does not.
inline bool NativeShaderNumbersPrimitives(ID3D11ShaderReflection* reflection) {
  if(!reflection) return true;
  D3D11_SHADER_DESC description{};
  if(FAILED(reflection->GetDesc(&description))) return true;
  for(UINT index=0;index<description.InputParameters;++index) {
    D3D11_SIGNATURE_PARAMETER_DESC input{};
    if(FAILED(reflection->GetInputParameterDesc(index,&input))) return true;
    if(input.SystemValueType==D3D_NAME_VERTEX_ID || input.SystemValueType==D3D_NAME_INSTANCE_ID ||
       input.SystemValueType==D3D_NAME_PRIMITIVE_ID) return true;
  }
  return false;
}
inline bool NativePipelineTransientBatchable(std::span<const NativeBackendInputElement> layout,
                                             ID3D11ShaderReflection* vertex,ID3D11ShaderReflection* pixel) {
  return NativeInputLayoutSlotZeroOnly(layout) && !NativeShaderNumbersPrimitives(vertex) &&
         !NativeShaderNumbersPrimitives(pixel);
}
// An indexed list draw as the non-indexed list draw with the same result: the
// vertices written out in index order. Draw(count) over `out` assembles the
// same primitives, from the same vertex values, in the same order, as
// DrawIndexed(count,first,base) over `vertices` - list topologies only, whose
// primitives do not share vertices in the assembler, and with a vertex stage
// that does not read SV_VertexID. A non-indexed draw is what lets consecutive
// identical-state draws be appended to each other (a generated index buffer
// only covers its own draw's vertex count).
inline void ExpandIndexedVertices(std::span<const uint8_t> vertices,uint32_t stride,
                                  std::span<const uint32_t> indices,uint32_t first,uint32_t count,
                                  int32_t base,std::vector<uint8_t>& out) {
  if(!stride || vertices.size()%stride || first>indices.size() || count>indices.size()-first)
    throw std::runtime_error("invalid indexed vertex expansion");
  const auto vertex_count=int64_t(vertices.size()/stride);
  out.resize(size_t(count)*stride);
  for(uint32_t index=0;index<count;++index) {
    const int64_t vertex=int64_t(indices[size_t(first)+index])+base;
    if(vertex<0 || vertex>=vertex_count) throw std::runtime_error("expanded index outside its vertices");
    std::memcpy(out.data()+size_t(index)*stride,vertices.data()+size_t(vertex)*stride,stride);
  }
}
}  // namespace edf::native

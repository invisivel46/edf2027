// The same rendering, run through every backend, and the results compared to
// each other rather than to a description of what they should be.
//
// This is the check the migration note promised: a backend that drops draws or
// gets state wrong shows up as a pixel difference against the backend it
// replaces, not as someone eventually noticing a missing mesh. Run on WARP so
// it works on a machine with no usable GPU.
#include "native_graphics/d3d11_backend.h"
#include "native_graphics/d3d12_backend.h"
#include "native_graphics/native_render_backend.h"
#include "native_graphics/d3d11_effect.h"
#include "native_graphics/d3d11_bindings.h"
#include "native_graphics/native_input_layout.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <thread>
#include <barrier>
#include <exception>

using Microsoft::WRL::ComPtr;
using namespace edf::native;

namespace {
int failures = 0;
void Check(bool ok, const std::string& message) {
  if (ok) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}
ComPtr<ID3DBlob> Compile(const char* source, const char* entry, const char* profile) {
  ComPtr<ID3DBlob> code, errors;
  if (FAILED(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, entry, profile, 0, 0,
                        &code, &errors)))
    throw std::runtime_error(std::string("shader compile failed: ") +
                             (errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?"));
  return code;
}
std::span<const uint8_t> Bytes(ID3DBlob& blob) {
  return {static_cast<const uint8_t*>(blob.GetBufferPointer()), blob.GetBufferSize()};
}

constexpr uint32_t kSize = 64;
constexpr uint32_t kFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

// What one backend produced. Compared against every other backend's.
struct Rendered {
  std::vector<uint8_t> flat, textured, instanced, mipped, resolved, compressed, blended;
  // The uploaded texture read straight back, with no draw in between: it
  // separates "the upload is wrong" from "the sampling is wrong", which a
  // rendered surface alone cannot.
  std::vector<uint8_t> uploaded;
  std::vector<uint8_t> depth_tested, target0, target1;
  // Two targets drawn from one dynamic vertex buffer, rewritten between them.
  std::vector<uint8_t> dynamic_before, dynamic_after;
  std::vector<uint8_t> transient_before, transient_after;
  // A target drawn into and then sampled, in one frame, without a resolve.
  // Twice, because one round trip does not diverge a per-view state tracker.
  std::vector<uint8_t> sampled_target, sampled_target_again;
  bool refused_missing_blend_factor=false;
  bool refused_partial_dynamic_update=false;
  uint64_t query_samples = 0;
  std::vector<std::string> validation;
};

const char* kFlat = R"(
cbuffer PixelData : register(b0) { float4 tint; };
float4 VS(float3 position : POSITION) : SV_POSITION { return float4(position, 1); }
float4 PS(float4 position : SV_POSITION) : SV_TARGET { return tint; }
)";
const char* kTextured = R"(
Texture2D image : register(t0);
SamplerState filtering : register(s0);
struct Varying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Varying VS(float3 position : POSITION) {
  Varying output;
  output.position = float4(position, 1);
  output.uv = float2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
  return output;
}
float4 PS(Varying input) : SV_TARGET { return image.Sample(filtering, input.uv); }
)";
const char* kMipped = R"(
Texture2D image : register(t0);
SamplerState filtering : register(s0);
struct Varying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Varying VS(float3 position : POSITION) {
  Varying output;
  output.position = float4(position, 1);
  output.uv = float2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
  return output;
}
float4 PS(Varying input) : SV_TARGET { return image.SampleLevel(filtering, input.uv, 1); }
)";
// Depth comes from a vertex constant so one vertex buffer can be drawn at
// several depths; the guest supplies depth the same way, through constants.
const char* kDepth = R"(
cbuffer VertexData : register(b0) { float4 depth; };
cbuffer PixelData : register(b0) { float4 tint; };
float4 VS(float3 position : POSITION) : SV_POSITION { return float4(position.xy, depth.x, 1); }
float4 PS(float4 position : SV_POSITION) : SV_TARGET { return tint; }
)";
// Two outputs, which the seam allows and nothing had ever asked for.
const char* kTwoTargets = R"(
struct Outputs { float4 first : SV_TARGET0; float4 second : SV_TARGET1; };
float4 VS(float3 position : POSITION) : SV_POSITION { return float4(position, 1); }
Outputs PS(float4 position : SV_POSITION) {
  Outputs outputs;
  outputs.first = float4(1, 0, 0, 1);
  outputs.second = float4(0, 0, 1, 1);
  return outputs;
}
)";
const char* kInstanced = R"(
cbuffer PixelData : register(b0) { float4 tint; };
float4 VS(float3 position : POSITION, float2 offset : TEXCOORD0) : SV_POSITION {
  return float4(position.xy + offset, 0, 1);
}
float4 PS(float4 position : SV_POSITION) : SV_TARGET { return tint; }
)";

Rendered Render(NativeRenderBackend& backend) {
  Rendered out;

  NativeBackendTextureDesc target_desc{};
  target_desc.width = target_desc.height = kSize;
  target_desc.levels = 1;
  target_desc.format = kFormat;
  target_desc.render_target = true;
  const auto target = backend.CreateRenderTarget(target_desc);
  NativeBackendRenderTarget* colors[] = {target.get()};

  const auto flat_vs = Compile(kFlat, "VS", "vs_5_0");
  const auto flat_ps = Compile(kFlat, "PS", "ps_5_0");
  const NativeBackendInputElement position[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, false, 0}};
  NativeBackendPipelineDesc flat_desc{};
  flat_desc.vertex = Bytes(*flat_vs.Get());
  flat_desc.pixel = Bytes(*flat_ps.Get());
  flat_desc.vertex_id = 1;
  flat_desc.pixel_id = 2;
  flat_desc.input_layout = position;
  flat_desc.input_layout_id = 3;
  flat_desc.state = {0x10001, 0, 0, 0, 15, 0};
  flat_desc.topology = NativeBackendTopology::TriangleList;
  flat_desc.render_targets = 1;
  flat_desc.rtv_format[0] = kFormat;
  auto& flat_pipeline = backend.CreatePipeline(flat_desc);

  // A triangle covering exactly the lower-left half, not the usual full-screen
  // covering one: that covers everything, so a wrong viewport would pass.
  const float half[] = {-1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, -1.0f, 1.0f, 0.0f};
  NativeBackendBufferDesc vertex_desc{};
  vertex_desc.bytes = sizeof(half);
  vertex_desc.vertex = true;
  const auto half_buffer =
      backend.CreateBuffer(vertex_desc, {reinterpret_cast<const uint8_t*>(half), sizeof(half)});
  const std::array<float, 4> green{0.0f, 1.0f, 0.0f, 1.0f};

  const auto query = backend.CreateQuery(NativeBackendQueryKind::Occlusion);
  backend.BeginFrame();
  {
    auto& recorder = backend.Recorder();
    recorder.SetRenderTargets(colors, nullptr);
    recorder.SetViewport({0, 0, kSize, kSize, 0, 1});
    recorder.ClearColor(*target, {1.0f, 0.0f, 0.0f, 1.0f});
    recorder.SetPipeline(flat_pipeline);
    recorder.SetVertexBuffer(0, *half_buffer, sizeof(float) * 3, 0);
    recorder.SetConstants(NativeBackendStage::Pixel, 0,
                          {reinterpret_cast<const uint8_t*>(green.data()), sizeof(float) * 4});
    recorder.BeginQuery(*query);
    recorder.Draw(3, 0);
    recorder.EndQuery(*query);
  }
  backend.Submit();
  out.flat = backend.ReadRenderTarget(*target);

  std::span<uint8_t> into{reinterpret_cast<uint8_t*>(&out.query_samples), sizeof(out.query_samples)};
  for (uint32_t attempt = 0; attempt < 10000 && !backend.ReadQuery(*query, into); ++attempt) {}

  // Turning scissor off without changing the viewport must restore the full
  // viewport. D3D11 does that in its raster state; D3D12 needs a new rectangle.
  auto clipped_desc=flat_desc;
  clipped_desc.state[5]=1;
  auto& clipped_pipeline=backend.CreatePipeline(clipped_desc);
  backend.BeginFrame();
  {
    auto& recorder=backend.Recorder();
    recorder.SetRenderTargets(colors,nullptr);
    recorder.SetViewport({0,0,kSize,kSize,0,1});
    recorder.ClearColor(*target,{1,0,0,1});
    recorder.SetPipeline(clipped_pipeline);
    recorder.SetVertexBuffer(0,*half_buffer,sizeof(float)*3,0);
    recorder.SetConstants(NativeBackendStage::Pixel,0,
      {reinterpret_cast<const uint8_t*>(green.data()),sizeof(green)});
    recorder.SetScissor({0,0,1,1},true);
    recorder.Draw(3,0);
    recorder.SetPipeline(flat_pipeline);
    recorder.SetScissor({},false);
    recorder.Draw(3,0);
  }
  backend.Submit();
  Check(backend.ReadRenderTarget(*target)==out.flat,std::string(backend.name())+": disabled scissor retained its previous rectangle");

  // A dynamic vertex buffer rewritten between two draws in one frame.
  //
  // This is the one thing a dynamic buffer has to promise: the draw recorded
  // before the rewrite keeps the vertices it was recorded with. A backend that
  // overwrote the storage in place would show the second triangle on both
  // targets, and would do so only under load, in a frame with enough draws
  // queued for the write to land first.
  const float upper[] = {1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f, -1.0f, 0.0f};
  NativeBackendBufferDesc dynamic_desc{};
  dynamic_desc.bytes = sizeof(upper);
  dynamic_desc.vertex = true;
  dynamic_desc.dynamic = true;
  const auto dynamic_buffer =
      backend.CreateBuffer(dynamic_desc, {reinterpret_cast<const uint8_t*>(upper), sizeof(upper)});
  const auto before_target = backend.CreateRenderTarget(target_desc);
  const auto after_target = backend.CreateRenderTarget(target_desc);
  NativeBackendRenderTarget* before_colors[] = {before_target.get()};
  NativeBackendRenderTarget* after_colors[] = {after_target.get()};
  backend.BeginFrame();
  {
    auto& recorder = backend.Recorder();
    recorder.SetViewport({0, 0, kSize, kSize, 0, 1});
    recorder.SetPipeline(flat_pipeline);
    recorder.SetConstants(NativeBackendStage::Pixel, 0,
                          {reinterpret_cast<const uint8_t*>(green.data()), sizeof(float) * 4});
    recorder.SetRenderTargets(before_colors, nullptr);
    recorder.ClearColor(*before_target, {1.0f, 0.0f, 0.0f, 1.0f});
    recorder.SetVertexBuffer(0, *dynamic_buffer, sizeof(float) * 3, 0);
    recorder.Draw(3, 0);

    recorder.UpdateBuffer(*dynamic_buffer, 0,
                          {reinterpret_cast<const uint8_t*>(half), sizeof(half)});

    recorder.SetRenderTargets(after_colors, nullptr);
    recorder.ClearColor(*after_target, {1.0f, 0.0f, 0.0f, 1.0f});
    recorder.SetVertexBuffer(0, *dynamic_buffer, sizeof(float) * 3, 0);
    recorder.Draw(3, 0);

    // Whole or nothing: the untouched part of a renamed buffer is undefined,
    // so a partial update must be refused rather than quietly returning
    // whatever the driver handed back.
    try {
      recorder.UpdateBuffer(*dynamic_buffer, 12,
                            {reinterpret_cast<const uint8_t*>(half), sizeof(float) * 3});
    } catch (const std::runtime_error&) { out.refused_partial_dynamic_update = true; }
  }
  backend.Submit();
  out.dynamic_before = backend.ReadRenderTarget(*before_target);
  out.dynamic_after = backend.ReadRenderTarget(*after_target);

  // The same two triangles through transient vertices: no buffer at all, the
  // bytes staged by the backend for this frame. The draw recorded first must
  // keep its triangle after the second one's bytes are staged - what the
  // dynamic buffer promised, without the flush that promise cost.
  backend.BeginFrame();
  {
    auto& recorder = backend.Recorder();
    recorder.SetViewport({0, 0, kSize, kSize, 0, 1});
    recorder.SetPipeline(flat_pipeline);
    recorder.SetConstants(NativeBackendStage::Pixel, 0,
                          {reinterpret_cast<const uint8_t*>(green.data()), sizeof(float) * 4});
    recorder.SetRenderTargets(before_colors, nullptr);
    recorder.ClearColor(*before_target, {1.0f, 0.0f, 0.0f, 1.0f});
    recorder.SetTransientVertices(0, {reinterpret_cast<const uint8_t*>(upper), sizeof(upper)},
                                  sizeof(float) * 3);
    recorder.Draw(3, 0);
    recorder.SetRenderTargets(after_colors, nullptr);
    recorder.ClearColor(*after_target, {1.0f, 0.0f, 0.0f, 1.0f});
    recorder.SetTransientVertices(0, {reinterpret_cast<const uint8_t*>(half), sizeof(half)},
                                  sizeof(float) * 3);
    recorder.Draw(3, 0);
  }
  backend.Submit();
  out.transient_before = backend.ReadRenderTarget(*before_target);
  out.transient_after = backend.ReadRenderTarget(*after_target);

  // Textured, with a 2x2 texture whose four texels differ, so a flipped or
  // row-swapped upload cannot match the other backend by accident.
  const uint8_t texels[] = {255, 0, 0, 255,   0, 255, 0, 255,
                            0, 0, 255, 255,   255, 255, 0, 255};
  NativeBackendTextureDesc texture_desc{};
  texture_desc.width = texture_desc.height = 2;
  texture_desc.levels = 1;
  texture_desc.format = kFormat;
  const auto texture = backend.CreateTexture(texture_desc, texels);
  NativeBackendSamplerDesc point{};
  point.min = point.mag = point.mip = NativeBackendFilter::Point;
  point.u = point.v = point.w = NativeBackendAddress::Clamp;
  auto& sampler = backend.CreateSampler(point);

  const float cover[] = {-1.0f, -1.0f, 0.0f, 3.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f};
  NativeBackendBufferDesc cover_desc{};
  cover_desc.bytes = sizeof(cover);
  cover_desc.vertex = true;
  const auto cover_buffer =
      backend.CreateBuffer(cover_desc, {reinterpret_cast<const uint8_t*>(cover), sizeof(cover)});

  const auto textured_vs = Compile(kTextured, "VS", "vs_5_0");
  const auto textured_ps = Compile(kTextured, "PS", "ps_5_0");
  NativeBackendPipelineDesc textured_desc = flat_desc;
  textured_desc.vertex = Bytes(*textured_vs.Get());
  textured_desc.pixel = Bytes(*textured_ps.Get());
  textured_desc.vertex_id = 4;
  textured_desc.pixel_id = 5;
  auto& textured_pipeline = backend.CreatePipeline(textured_desc);

  backend.BeginFrame();
  {
    auto& recorder = backend.Recorder();
    recorder.SetRenderTargets(colors, nullptr);
    recorder.SetViewport({0, 0, kSize, kSize, 0, 1});
    recorder.ClearColor(*target, {0.0f, 0.0f, 0.0f, 1.0f});
    recorder.SetPipeline(textured_pipeline);
    recorder.SetVertexBuffer(0, *cover_buffer, sizeof(float) * 3, 0);
    recorder.SetTexture(NativeBackendStage::Pixel, 0, texture.get());
    recorder.SetSampler(NativeBackendStage::Pixel, 0, &sampler);
    recorder.Draw(3, 0);
  }
  backend.Submit();
  out.textured = backend.ReadRenderTarget(*target);
  out.uploaded = backend.ReadTexture(*texture);

  // A render target sampled in the same frame it was drawn into.
  //
  // One resource behind two views. On a backend with explicit barriers the two
  // have to agree about the state it is in: if the sampled handle carried its
  // own copy of that state, the transition from "being written" to "being
  // read" would be skipped and this would read a target the GPU has not
  // finished writing. The post chain's conversion passes are exactly this
  // shape, which is why it is checked rather than assumed.
  NativeBackendTextureDesc sampled_desc = target_desc;
  sampled_desc.sampled = true;
  const auto sampled = backend.CreateRenderTarget(sampled_desc);
  Check(sampled->texture() != nullptr, std::string(backend.name()) +
        ": a target declared sampled has no texture handle");
  const auto second = backend.CreateRenderTarget(target_desc);
  const auto third = backend.CreateRenderTarget(target_desc);
  NativeBackendRenderTarget* sampled_colors[] = {sampled.get()};
  NativeBackendRenderTarget* second_colors[] = {second.get()};
  NativeBackendRenderTarget* third_colors[] = {third.get()};
  backend.BeginFrame();
  {
    auto& recorder = backend.Recorder();
    recorder.SetViewport({0, 0, kSize, kSize, 0, 1});
    // Write it: a flat green half-triangle over a red clear.
    recorder.SetRenderTargets(sampled_colors, nullptr);
    recorder.ClearColor(*sampled, {1.0f, 0.0f, 0.0f, 1.0f});
    recorder.SetPipeline(flat_pipeline);
    recorder.SetVertexBuffer(0, *half_buffer, sizeof(float) * 3, 0);
    recorder.SetConstants(NativeBackendStage::Pixel, 0,
                          {reinterpret_cast<const uint8_t*>(green.data()), sizeof(float) * 4});
    recorder.Draw(3, 0);
    // Read it, in the same frame, into a different target, covering all of it.
    recorder.SetRenderTargets(second_colors, nullptr);
    recorder.ClearColor(*second, {0.0f, 0.0f, 1.0f, 1.0f});
    recorder.SetPipeline(textured_pipeline);
    recorder.SetVertexBuffer(0, *cover_buffer, sizeof(float) * 3, 0);
    recorder.SetTexture(NativeBackendStage::Pixel, 0, sampled->texture());
    recorder.SetSampler(NativeBackendStage::Pixel, 0, &sampler);
    recorder.Draw(3, 0);

    // Round two, which is the one that matters. After the read above, the
    // resource is in a read state. Writing it again has to transition it back,
    // and a backend holding one state per *view* rather than per resource now
    // believes it is still writable and emits no barrier at all - which the
    // pixels may survive on a software rasteriser but the debug layer will
    // not, and real hardware will not either.
    recorder.SetRenderTargets(sampled_colors, nullptr);
    recorder.SetPipeline(flat_pipeline);
    recorder.SetVertexBuffer(0, *cover_buffer, sizeof(float) * 3, 0);
    const std::array<float, 4> white{1.0f, 1.0f, 1.0f, 1.0f};
    recorder.SetConstants(NativeBackendStage::Pixel, 0,
                          {reinterpret_cast<const uint8_t*>(white.data()), sizeof(float) * 4});
    recorder.Draw(3, 0);

    recorder.SetRenderTargets(third_colors, nullptr);
    recorder.ClearColor(*third, {0.0f, 0.0f, 1.0f, 1.0f});
    recorder.SetPipeline(textured_pipeline);
    recorder.SetVertexBuffer(0, *cover_buffer, sizeof(float) * 3, 0);
    recorder.SetTexture(NativeBackendStage::Pixel, 0, sampled->texture());
    recorder.SetSampler(NativeBackendStage::Pixel, 0, &sampler);
    recorder.Draw(3, 0);
  }
  backend.Submit();
  out.sampled_target = backend.ReadRenderTarget(*second);
  out.sampled_target_again = backend.ReadRenderTarget(*third);

  // Mipped: level 0 red, level 1 blue, and the shader samples level 1, so a
  // backend that uploaded only the top level renders something else.
  const uint8_t mip_bytes[] = {255, 0, 0, 255,  255, 0, 0, 255,
                               255, 0, 0, 255,  255, 0, 0, 255,
                               0, 0, 255, 255};
  NativeBackendTextureDesc mipped_desc{};
  mipped_desc.width = mipped_desc.height = 2;
  mipped_desc.levels = 2;
  mipped_desc.format = kFormat;
  const auto mipped = backend.CreateTexture(mipped_desc, mip_bytes);
  const auto mip_vs = Compile(kMipped, "VS", "vs_5_0");
  const auto mip_ps = Compile(kMipped, "PS", "ps_5_0");
  NativeBackendPipelineDesc mip_pipeline_desc = flat_desc;
  mip_pipeline_desc.vertex = Bytes(*mip_vs.Get());
  mip_pipeline_desc.pixel = Bytes(*mip_ps.Get());
  mip_pipeline_desc.vertex_id = 6;
  mip_pipeline_desc.pixel_id = 7;
  auto& mip_pipeline = backend.CreatePipeline(mip_pipeline_desc);

  backend.BeginFrame();
  {
    auto& recorder = backend.Recorder();
    recorder.SetRenderTargets(colors, nullptr);
    recorder.SetViewport({0, 0, kSize, kSize, 0, 1});
    recorder.ClearColor(*target, {0.0f, 0.0f, 0.0f, 1.0f});
    recorder.SetPipeline(mip_pipeline);
    recorder.SetVertexBuffer(0, *cover_buffer, sizeof(float) * 3, 0);
    recorder.SetTexture(NativeBackendStage::Pixel, 0, mipped.get());
    recorder.SetSampler(NativeBackendStage::Pixel, 0, &sampler);
    recorder.Draw(3, 0);
  }
  backend.Submit();
  out.mipped = backend.ReadRenderTarget(*target);

  // Blending against a constant blend factor. The guest word sets the source
  // factor to the constant colour and the destination to zero, so the result
  // is exactly source x factor: drawing white with (0.5, 0.25, 0.75) must read
  // back as that, which no other blend state produces by accident.
  constexpr uint32_t kFactorBlend = 12u | (1u << 16);  // src=BLEND_FACTOR, dst=ZERO, alpha src=ONE
  NativeBackendPipelineDesc blended_desc = flat_desc;
  blended_desc.state = {kFactorBlend, 0, 0, 0, 15, 0};
  blended_desc.vertex_id = 13;
  blended_desc.pixel_id = 14;
  auto& blended_pipeline = backend.CreatePipeline(blended_desc);
  if(!blended_pipeline.requires_blend_factor())
    throw std::runtime_error("pipeline lost its constant blend-factor requirement");
  const std::array<float, 4> opaque_white{1.0f, 1.0f, 1.0f, 1.0f};
  const std::array<float, 4> factor{0.5f, 0.25f, 0.75f, 1.0f};

  backend.BeginFrame();
  {
    auto& recorder = backend.Recorder();
    recorder.SetRenderTargets(colors, nullptr);
    recorder.SetViewport({0, 0, kSize, kSize, 0, 1});
    recorder.ClearColor(*target, {0.0f, 0.0f, 0.0f, 1.0f});
    recorder.SetPipeline(blended_pipeline);
    recorder.SetVertexBuffer(0, *cover_buffer, sizeof(float) * 3, 0);
    recorder.SetConstants(NativeBackendStage::Pixel, 0,
                          {reinterpret_cast<const uint8_t*>(opaque_white.data()), sizeof(float) * 4});
    // A draw needing a factor and given none must be refused, not drawn in the
    // wrong colour. Checked here, inside the frame, before supplying one.
    try { recorder.Draw(3, 0); }
    catch (const std::runtime_error&) { out.refused_missing_blend_factor = true; }
    recorder.SetBlendFactor(factor);
    recorder.Draw(3, 0);
  }
  backend.Submit();
  out.blended = backend.ReadRenderTarget(*target);

  // Depth testing. Three full-screen draws at different depths with LESS:
  // the middle one must be rejected and the nearest must win, so the result
  // is the third colour rather than the last one drawn. A depth buffer that
  // was never bound, never cleared, or never written shows a different colour
  // here, and each of those is a distinct way for the port to go wrong.
  NativeBackendTextureDesc depth_desc{};
  depth_desc.width = depth_desc.height = kSize;
  depth_desc.levels = 1;
  depth_desc.format = 40;  // DXGI_FORMAT_D32_FLOAT
  depth_desc.depth = true;
  depth_desc.render_target = true;
  const auto depth_target = backend.CreateRenderTarget(depth_desc);

  const auto depth_vs = Compile(kDepth, "VS", "vs_5_0");
  const auto depth_ps = Compile(kDepth, "PS", "ps_5_0");
  NativeBackendPipelineDesc depth_pipeline_desc = flat_desc;
  depth_pipeline_desc.vertex = Bytes(*depth_vs.Get());
  depth_pipeline_desc.pixel = Bytes(*depth_ps.Get());
  depth_pipeline_desc.vertex_id = 15;
  depth_pipeline_desc.pixel_id = 16;
  // depth enable | depth write | func LESS, in the guest encoding.
  depth_pipeline_desc.state = {0x10001, 0x16, 0, 0, 15, 0};
  depth_pipeline_desc.dsv_format = 40;
  auto& depth_pipeline = backend.CreatePipeline(depth_pipeline_desc);

  const std::array<float, 4> depth_red{1, 0, 0, 1}, depth_green{0, 1, 0, 1}, depth_blue{0, 0, 1, 1};
  const std::array<float, 4> near_z{0.2f, 0, 0, 0}, mid_z{0.5f, 0, 0, 0}, far_z{0.8f, 0, 0, 0};
  backend.BeginFrame();
  {
    auto& recorder = backend.Recorder();
    recorder.SetRenderTargets(colors, depth_target.get());
    recorder.SetViewport({0, 0, kSize, kSize, 0, 1});
    recorder.ClearColor(*target, {0.0f, 0.0f, 0.0f, 1.0f});
    recorder.ClearDepthStencil(*depth_target, true, false, 1.0f, 0);
    recorder.SetPipeline(depth_pipeline);
    recorder.SetVertexBuffer(0, *cover_buffer, sizeof(float) * 3, 0);
    const auto draw_at = [&](const std::array<float, 4>& z, const std::array<float, 4>& tint) {
      recorder.SetConstants(NativeBackendStage::Vertex, 0,
                            {reinterpret_cast<const uint8_t*>(z.data()), sizeof(float) * 4});
      recorder.SetConstants(NativeBackendStage::Pixel, 0,
                            {reinterpret_cast<const uint8_t*>(tint.data()), sizeof(float) * 4});
      recorder.Draw(3, 0);
    };
    draw_at(mid_z, depth_red);     // passes against the cleared depth
    draw_at(far_z, depth_green);   // must be rejected
    draw_at(near_z, depth_blue);   // must win
  }
  backend.Submit();
  out.depth_tested = backend.ReadRenderTarget(*target);

  // Two render targets at once, written by one draw.
  const auto second_target = backend.CreateRenderTarget(target_desc);
  const auto two_vs = Compile(kTwoTargets, "VS", "vs_5_0");
  const auto two_ps = Compile(kTwoTargets, "PS", "ps_5_0");
  NativeBackendPipelineDesc two_desc = flat_desc;
  two_desc.vertex = Bytes(*two_vs.Get());
  two_desc.pixel = Bytes(*two_ps.Get());
  two_desc.vertex_id = 17;
  two_desc.pixel_id = 18;
  two_desc.render_targets = 2;
  two_desc.rtv_format[0] = kFormat;
  two_desc.rtv_format[1] = kFormat;
  auto& two_pipeline = backend.CreatePipeline(two_desc);
  NativeBackendRenderTarget* both[] = {target.get(), second_target.get()};

  backend.BeginFrame();
  {
    auto& recorder = backend.Recorder();
    recorder.SetRenderTargets(both, nullptr);
    recorder.SetViewport({0, 0, kSize, kSize, 0, 1});
    recorder.ClearColor(*target, {0.0f, 0.0f, 0.0f, 1.0f});
    recorder.ClearColor(*second_target, {0.0f, 0.0f, 0.0f, 1.0f});
    recorder.SetPipeline(two_pipeline);
    recorder.SetVertexBuffer(0, *cover_buffer, sizeof(float) * 3, 0);
    recorder.Draw(3, 0);
  }
  backend.Submit();
  out.target0 = backend.ReadRenderTarget(*target);
  out.target1 = backend.ReadRenderTarget(*second_target);

  // Block-compressed, which is what the game's textures actually are. A BC1
  // block is two RGB565 endpoints and four bytes of 2-bit indices; setting
  // both endpoints to the same colour and all indices to zero gives a solid
  // block, which is enough to tell whether the blocks landed where they were
  // meant to. 8x8 is 2x2 blocks, so each quadrant is one block of its own
  // colour -- a backend that computed the row pitch in texels instead of
  // blocks gets the second row of blocks from the wrong offset and shows it.
  const auto bc1_block=[](uint16_t colour,uint8_t* out) {
    out[0]=uint8_t(colour&0xff); out[1]=uint8_t(colour>>8);
    out[2]=out[0]; out[3]=out[1];
    out[4]=out[5]=out[6]=out[7]=0;
  };
  uint8_t bc1[32]{};
  bc1_block(0xf800,bc1);       // red    (RGB565)
  bc1_block(0x07e0,bc1+8);     // green
  bc1_block(0x001f,bc1+16);    // blue
  bc1_block(0xffe0,bc1+24);    // yellow
  NativeBackendTextureDesc compressed_desc{};
  compressed_desc.width=compressed_desc.height=8;
  compressed_desc.levels=1;
  compressed_desc.format=71;  // DXGI_FORMAT_BC1_UNORM
  const auto compressed=backend.CreateTexture(compressed_desc,bc1);

  backend.BeginFrame();
  {
    auto& recorder = backend.Recorder();
    recorder.SetRenderTargets(colors, nullptr);
    recorder.SetViewport({0, 0, kSize, kSize, 0, 1});
    recorder.ClearColor(*target, {0.0f, 0.0f, 0.0f, 1.0f});
    recorder.SetPipeline(textured_pipeline);
    recorder.SetVertexBuffer(0, *cover_buffer, sizeof(float) * 3, 0);
    recorder.SetTexture(NativeBackendStage::Pixel, 0, compressed.get());
    recorder.SetSampler(NativeBackendStage::Pixel, 0, &sampler);
    recorder.Draw(3, 0);
  }
  backend.Submit();
  out.compressed = backend.ReadRenderTarget(*target);

  // Indexed instanced, with a per-instance input element.
  const float quad[] = {-0.2f, -0.2f, 0.0f, 0.2f, -0.2f, 0.0f, -0.2f, 0.2f, 0.0f, 0.2f, 0.2f, 0.0f};
  const uint16_t indices[] = {0, 2, 1, 1, 2, 3};
  const float offsets[] = {-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};
  NativeBackendBufferDesc quad_desc{};
  quad_desc.bytes = sizeof(quad);
  quad_desc.vertex = true;
  const auto quad_buffer =
      backend.CreateBuffer(quad_desc, {reinterpret_cast<const uint8_t*>(quad), sizeof(quad)});
  quad_desc.bytes = sizeof(offsets);
  const auto instance_buffer =
      backend.CreateBuffer(quad_desc, {reinterpret_cast<const uint8_t*>(offsets), sizeof(offsets)});
  NativeBackendBufferDesc index_desc{};
  index_desc.bytes = sizeof(indices);
  index_desc.index = true;
  const auto index_buffer =
      backend.CreateBuffer(index_desc, {reinterpret_cast<const uint8_t*>(indices), sizeof(indices)});

  const auto instanced_vs = Compile(kInstanced, "VS", "vs_5_0");
  const auto instanced_ps = Compile(kInstanced, "PS", "ps_5_0");
  const NativeBackendInputElement instanced_layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, false, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0, true, 1}};
  NativeBackendPipelineDesc instanced_desc = flat_desc;
  instanced_desc.vertex = Bytes(*instanced_vs.Get());
  instanced_desc.pixel = Bytes(*instanced_ps.Get());
  instanced_desc.vertex_id = 8;
  instanced_desc.pixel_id = 9;
  instanced_desc.input_layout = instanced_layout;
  instanced_desc.input_layout_id = 10;
  auto& instanced_pipeline = backend.CreatePipeline(instanced_desc);

  const std::array<float, 4> white{1.0f, 1.0f, 1.0f, 1.0f};
  backend.BeginFrame();
  {
    auto& recorder = backend.Recorder();
    recorder.SetRenderTargets(colors, nullptr);
    recorder.SetViewport({0, 0, kSize, kSize, 0, 1});
    recorder.ClearColor(*target, {0.0f, 0.0f, 0.0f, 1.0f});
    recorder.SetPipeline(instanced_pipeline);
    recorder.SetVertexBuffer(0, *quad_buffer, sizeof(float) * 3, 0);
    recorder.SetVertexBuffer(1, *instance_buffer, sizeof(float) * 2, 0);
    recorder.SetIndexBuffer(*index_buffer, NativeBackendIndexFormat::Uint16, 0);
    recorder.SetConstants(NativeBackendStage::Pixel, 0,
                          {reinterpret_cast<const uint8_t*>(white.data()), sizeof(float) * 4});
    recorder.DrawIndexedInstanced(6, 4, 0, 0, 0);
  }
  backend.Submit();
  out.instanced = backend.ReadRenderTarget(*target);

  // 4x multisampled, resolved, then sampled through a single-sample target.
  NativeBackendTextureDesc msaa_desc = target_desc;
  msaa_desc.samples = 4;
  const auto msaa_target = backend.CreateRenderTarget(msaa_desc);
  NativeBackendTextureDesc resolve_desc{};
  resolve_desc.width = resolve_desc.height = kSize;
  resolve_desc.levels = 1;
  resolve_desc.format = kFormat;
  const auto resolved = backend.CreateTexture(resolve_desc, {});
  NativeBackendPipelineDesc msaa_pipeline_desc = flat_desc;
  msaa_pipeline_desc.sample_count = 4;
  msaa_pipeline_desc.vertex_id = 11;
  msaa_pipeline_desc.pixel_id = 12;
  auto& msaa_pipeline = backend.CreatePipeline(msaa_pipeline_desc);
  NativeBackendRenderTarget* msaa_colors[] = {msaa_target.get()};

  backend.BeginFrame();
  {
    auto& recorder = backend.Recorder();
    recorder.SetRenderTargets(msaa_colors, nullptr);
    recorder.SetViewport({0, 0, kSize, kSize, 0, 1});
    recorder.ClearColor(*msaa_target, {0.0f, 0.0f, 0.0f, 1.0f});
    recorder.SetPipeline(msaa_pipeline);
    recorder.SetVertexBuffer(0, *half_buffer, sizeof(float) * 3, 0);
    recorder.SetConstants(NativeBackendStage::Pixel, 0,
                          {reinterpret_cast<const uint8_t*>(green.data()), sizeof(float) * 4});
    recorder.Draw(3, 0);
    recorder.ResolveTarget(*resolved, *msaa_target);
  }
  backend.Submit();

  backend.BeginFrame();
  {
    auto& recorder = backend.Recorder();
    recorder.SetRenderTargets(colors, nullptr);
    recorder.SetViewport({0, 0, kSize, kSize, 0, 1});
    recorder.ClearColor(*target, {0.0f, 0.0f, 0.0f, 1.0f});
    recorder.SetPipeline(textured_pipeline);
    recorder.SetVertexBuffer(0, *cover_buffer, sizeof(float) * 3, 0);
    recorder.SetTexture(NativeBackendStage::Pixel, 0, resolved.get());
    recorder.SetSampler(NativeBackendStage::Pixel, 0, &sampler);
    recorder.Draw(3, 0);
  }
  backend.Submit();
  out.resolved = backend.ReadRenderTarget(*target);

  out.validation = backend.DrainValidationMessages();
  return out;
}

// Largest per-channel difference, and how many pixels differ at all.
struct Difference { uint32_t worst = 0, pixels = 0; };
Difference Compare(const std::vector<uint8_t>& left, const std::vector<uint8_t>& right) {
  Difference difference;
  if (left.size() != right.size()) { difference.worst = 255; difference.pixels = 0xffffffff; return difference; }
  for (size_t at = 0; at < left.size(); at += 4) {
    uint32_t worst = 0;
    for (size_t channel = 0; channel < 4; ++channel)
      worst = (std::max)(worst, static_cast<uint32_t>(
                                    std::abs(static_cast<int>(left[at + channel]) - right[at + channel])));
    if (worst) { ++difference.pixels; difference.worst = (std::max)(difference.worst, worst); }
  }
  return difference;
}
}  // namespace

// CPU recording order must not choose resource states or GPU draw order.
// All four workers write the same resource, then copy their own result. The
// lists execute 0..3, even when list 3 is completely recorded before list 0.
void ParallelResourceOrdering() {
  NativeD3D12Options options; options.prefer_warp=true; options.debug_layer=true;
  options.recorders=4;
  auto backend=CreateNativeD3D12Backend(options);
  NativeBackendTextureDesc desc{}; desc.width=desc.height=16; desc.format=kFormat;
  auto source=backend->CreateRenderTarget(desc);
  std::array<std::unique_ptr<NativeBackendTexture>,4> outputs;
  for(auto& output:outputs) output=backend->CreateTexture(desc,{});
  const std::array<std::array<float,4>,4> colors{{{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,0,1}}};
  for(unsigned frame=0;frame<12;++frame) {
    backend->BeginFrame();
    auto record=[&](unsigned index) {
      auto& recorder=backend->Recorder(index);
      recorder.ClearColor(*source,colors[index]);
      recorder.ResolveTarget(*outputs[index],*source);
    };
    if(frame%2==0) {
      for(unsigned index=4;index--;) record(index);
    } else {
      std::barrier start(4);
      std::array<std::exception_ptr,4> errors{};
      std::vector<std::jthread> workers;
      for(unsigned index=0;index<4;++index) workers.emplace_back([&,index] {
        start.arrive_and_wait();
        try { record(index); } catch(...) { errors[index]=std::current_exception(); }
      });
      workers.clear();
      for(auto& error:errors) if(error) std::rethrow_exception(error);
    }
    backend->Submit();
    for(unsigned index=0;index<4;++index) {
      const auto pixels=backend->ReadTexture(*outputs[index]);
      for(size_t pixel=0;pixel<pixels.size();pixel+=4)
        for(unsigned channel=0;channel<4;++channel)
          Check(pixels[pixel+channel]==uint8_t(colors[index][channel]*255),
                "parallel resource transition followed CPU recording order");
    }
  }
  // A resource wrapper may die before Submit: first-use state and the native
  // resource must remain available to the submission's barrier preamble.
  backend->BeginFrame();
  backend->Recorder(2).ClearColor(*source,colors[2]);
  source.reset();
  backend->Submit();
  backend->ReadTexture(*outputs[0]);
  for(const auto& message:backend->DrainValidationMessages())
    Check(message.find("error")==std::string::npos && message.find("ERROR")==std::string::npos,
          "parallel validation: "+message);
}

void ParallelGeometryPackets() {
  NativeD3D12Options options; options.prefer_warp=true; options.debug_layer=true; options.geometry_workers=4;
  auto backend=CreateNativeD3D12Backend(options);
  const char* shader=R"(
    cbuffer Tint : register(b0) { float4 color; };
    Texture2D image : register(t0); SamplerState sample_image : register(s0);
    float4 VS(uint id:SV_VertexID):SV_Position {
      float2 p=float2((id<<1)&2,id&2); return float4(p*float2(2,-2)+float2(-1,1),0,1);
    }
    float4 PS():SV_Target { return color*image.SampleLevel(sample_image,float2(.5,.5),0); }
  )";
  auto vs=Compile(shader,"VS","vs_5_0"),ps=Compile(shader,"PS","ps_5_0");
  NativeBackendPipelineDesc pipeline{}; pipeline.vertex=Bytes(*vs.Get()); pipeline.pixel=Bytes(*ps.Get());
  pipeline.vertex_id=100; pipeline.pixel_id=101; pipeline.state={0x10001,0,0,0,15,0};
  pipeline.render_targets=1; pipeline.rtv_format[0]=kFormat;
  auto& pso=backend->CreatePipeline(pipeline);
  NativeBackendTextureDesc desc{}; desc.width=64; desc.height=16; desc.format=kFormat;
  auto target=backend->CreateRenderTarget(desc);
  const uint16_t indices[]{0,1,2};
  NativeBackendBufferDesc ib{}; ib.bytes=sizeof(indices); ib.index=true;
  auto buffer=backend->CreateBuffer(ib,{reinterpret_cast<const uint8_t*>(indices),sizeof(indices)});
  NativeBackendTextureDesc white_desc{}; white_desc.width=white_desc.height=1; white_desc.format=kFormat;
  const std::array<uint8_t,4> white{255,255,255,255};
  auto texture=backend->CreateTexture(white_desc,white);
  auto& sampler=backend->CreateSampler({});
  for(unsigned frame=0;frame<4;++frame) {
    backend->BeginFrame(); auto& r=backend->Recorder();
    r.ClearColor(*target,{0,0,0,0});
    NativeBackendRenderTarget* targets[]{target.get()}; r.SetRenderTargets(targets,nullptr);
    r.SetViewport({0,0,64,16,0,1}); r.SetPipeline(pso);
    r.SetIndexBuffer(*buffer,NativeBackendIndexFormat::Uint16,0);
    r.SetTexture(NativeBackendStage::Pixel,0,texture.get());
    r.SetSampler(NativeBackendStage::Pixel,0,&sampler);
    for(unsigned x=0;x<64;++x) {
      std::array<float,4> color{float(x)/63,0,float(frame)/3,1};
      r.SetConstants(NativeBackendStage::Pixel,0,{reinterpret_cast<const uint8_t*>(color.data()),sizeof(color)});
      r.SetScissor({int(x),0,int(x+1),16},true);
      r.DrawIndexed(3,0,0);
      color.fill(0); // The worker must own a snapshot, not this caller's bytes.
      if(x==31) {
        // A saved constant image must survive an intermediate worker flush,
        // deque growth, and a replacement binding. Restore and redraw the same
        // column so a dangling or overwritten snapshot changes the readback.
        r.PushState();
        r.UpdateBuffer(*buffer,0,{reinterpret_cast<const uint8_t*>(indices),sizeof(indices)});
        for(unsigned replacement=0;replacement<128;++replacement)
          r.SetConstants(NativeBackendStage::Pixel,0,
            {reinterpret_cast<const uint8_t*>(color.data()),sizeof(color)});
        r.PopState();
        r.DrawIndexed(3,0,0);
      }
    }
    if(frame==3) {
      // Destruction must drain deferred CPU work while both wrappers and their
      // descriptors still exist, then retire GPU resources behind the fence.
      texture.reset(); buffer.reset();
    }
    backend->Submit();
    const auto pixels=backend->ReadRenderTarget(*target);
    for(unsigned y=0;y<16;++y) for(unsigned x=0;x<64;++x) {
      const auto offset=(y*64+x)*4;
      Check(std::abs(int(pixels[offset])-int(x*255/63))<=1 &&
            std::abs(int(pixels[offset+2])-int(frame*255/3))<=1 && pixels[offset+3]==255,
            "parallel indexed draw lost its constants, scissor, or frame lifetime");
    }
  }
  const auto statistics=backend->Statistics();
  Check(statistics.geometry_batches>=4 && statistics.geometry_worker_mask==15,
        "geometry test did not execute all four recording workers");
  for(const auto& message:backend->DrainValidationMessages()) Check(false,"parallel geometry validation: "+message);
  // Worker errors must reach the producer, join every job, and poison retries:
  // submitting a partially recorded batch on a later call would lose draws.
  backend->BeginFrame();
  auto& bad=backend->Recorder(); bad.SetPipeline(pso);
  bad.SetTexture(NativeBackendStage::Vertex,0,nullptr); // Outside this root signature.
  for(unsigned draw=0;draw<64;++draw) bad.Draw(3,0);
  bool rejected=false,retry_rejected=false;
  try { backend->Submit(); } catch(const std::exception&) { rejected=true; }
  try { backend->Submit(); } catch(const std::exception&) { retry_rejected=true; }
  Check(rejected && retry_rejected,"failed worker recording was silently retried or submitted");

}

// Transient vertices exist so that immediate geometry stops being a flush
// boundary: with a dynamic buffer, each rewrite joined the workers and
// replayed whatever was pending on the producer. Sixty-four transient draws
// must therefore reach the workers as one batch, and none of them serially.
void TransientVerticesDoNotFlush() {
  NativeD3D12Options options; options.prefer_warp=true; options.debug_layer=true; options.geometry_workers=4;
  auto backend=CreateNativeD3D12Backend(options);
  auto vs=Compile(kFlat,"VS","vs_5_0"),ps=Compile(kFlat,"PS","ps_5_0");
  const NativeBackendInputElement position[]{{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,false,0}};
  NativeBackendPipelineDesc pipeline{}; pipeline.vertex=Bytes(*vs.Get()); pipeline.pixel=Bytes(*ps.Get());
  pipeline.vertex_id=200; pipeline.pixel_id=201; pipeline.input_layout=position; pipeline.input_layout_id=202;
  pipeline.state={0x10001,0,0,0,15,0}; pipeline.topology=NativeBackendTopology::TriangleList;
  pipeline.render_targets=1; pipeline.rtv_format[0]=kFormat;
  auto& pso=backend->CreatePipeline(pipeline);
  NativeBackendTextureDesc desc{}; desc.width=64; desc.height=16; desc.format=kFormat;
  auto target=backend->CreateRenderTarget(desc);
  const auto before=backend->Statistics();
  backend->BeginFrame();
  {
    auto& r=backend->Recorder();
    r.ClearColor(*target,{0,0,0,0});
    NativeBackendRenderTarget* targets[]{target.get()}; r.SetRenderTargets(targets,nullptr);
    r.SetViewport({0,0,64,16,0,1}); r.SetPipeline(pso);
    for(unsigned x=0;x<64;++x) {
      // A full-covering triangle whose bytes are overwritten after the draw:
      // the worker must own the staged copy, not this caller's array.
      float cover[]{-1,-1,0, 3,-1,0, -1,3,0};
      std::array<float,4> color{float(x)/63,0,1,1};
      r.SetConstants(NativeBackendStage::Pixel,0,{reinterpret_cast<const uint8_t*>(color.data()),sizeof(color)});
      r.SetTransientVertices(0,{reinterpret_cast<const uint8_t*>(cover),sizeof(cover)},sizeof(float)*3);
      r.SetScissor({int(x),0,int(x+1),16},true);
      r.Draw(3,0);
      for(auto& value:cover) value=0;
    }
  }
  backend->Submit();
  const auto pixels=backend->ReadRenderTarget(*target);
  for(unsigned y=0;y<16;++y) for(unsigned x=0;x<64;++x) {
    const auto offset=(y*64+x)*4;
    Check(std::abs(int(pixels[offset])-int(x*255/63))<=1 && pixels[offset+2]==255 && pixels[offset+3]==255,
          "a transient-vertex draw lost its vertices or its constants");
  }
  const auto after=backend->Statistics();
  Check(after.geometry_draws-before.geometry_draws==64,"transient draws were not all recorded as packets");
  Check(after.geometry_serial_flushes==before.geometry_serial_flushes,
        "transient vertices flushed the recorder: "+std::to_string(after.geometry_serial_flushes-before.geometry_serial_flushes)+" serial flushes");
  Check(after.geometry_batches>before.geometry_batches && after.geometry_worker_mask==15,
        "transient draws did not reach the recording workers as a batch");
  for(const auto& message:backend->DrainValidationMessages()) Check(false,"transient vertex validation: "+message);
}

void WorldInstancePackets() {
  for(uint32_t minimum:{1u,128u}) {
    NativeD3D12Options options; options.prefer_warp=true; options.debug_layer=true;
    options.geometry_workers=4; options.geometry_minimum_draws=minimum;
    auto backend=CreateNativeD3D12Backend(options);
    for(bool row_major:{false,true}) for(bool reversed:{false,true}) {
      Effect effect;
      effect.source=std::string(row_major?"row_major":"column_major")+R"( float4x4 g_mWorld : WORLD < bool SasUiVisible=false; >;
        float4 gain;
        struct V { float4 position:SV_Position; float4 color:COLOR0; };
        V VS(float3 position:POSITION0) {
          V o; o.position=mul(float4(position,1),g_mWorld); o.color=gain; return o;
        }
        float4 tint;
        float4 PS(V v):SV_Target { return v.color*tint; }
      )";
      auto vs=CompileNativeShader(nullptr,effect,{false,"VS","vs_3_0"},"instance-test.fx",reversed);
      Check(AddNativeWorldInstancing(vs,effect,"instance-test.fx",reversed),"world instance variant rejected supported matrix");
      if(!vs.instanced_bytecode) throw std::runtime_error("missing test instance variant");
      auto ps=CompileNativeShader(nullptr,effect,{true,"PS","ps_3_0"},"instance-test.fx");
      ShaderBindings vertex(nullptr,vs),pixel(nullptr,ps);
      NativeOwnedInputLayout ordinary,instanced;
      ordinary.Add("POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0);
      instanced.Add("POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0);
      for(uint32_t row=0;row<4;++row) instanced.Add("EDFINSTANCE",row,DXGI_FORMAT_R32G32B32A32_FLOAT,15,row*16,true,1);
      NativeBackendPipelineDesc desc{};
      desc.vertex=Bytes(*vs.bytecode.Get()); desc.pixel=Bytes(*ps.bytecode.Get());
      desc.vertex_id=200+uint64_t(row_major)*4+uint64_t(reversed)*2; desc.pixel_id=210;
      desc.state={0x10001,0,0,0,15,1}; desc.render_targets=1; desc.rtv_format[0]=kFormat;
      desc.input_layout=ordinary.elements(); desc.input_layout_id=ordinary.fingerprint();
      auto& pipeline=backend->CreatePipeline(desc);
      desc.vertex=Bytes(*vs.instanced_bytecode.Get()); desc.vertex_id|=uint64_t(1)<<63;
      desc.input_layout=instanced.elements(); desc.input_layout_id=instanced.fingerprint();
      pipeline.world_instanced=&backend->CreatePipeline(desc);
      pipeline.instance_world_slot=vs.instance_world_slot;
      pipeline.instance_world_offset=vs.instance_world_offset;
      NativeBackendTextureDesc target_desc{}; target_desc.width=64; target_desc.height=16; target_desc.format=kFormat;
      auto target=backend->CreateRenderTarget(target_desc);
      const float positions[]{-1,-1,0, -1,1,0, -1+2.f/64,1,0, -1+2.f/64,-1,0};
      const uint16_t indices[]{0,1,2,0,2,3};
      NativeBackendBufferDesc vb{}; vb.bytes=sizeof(positions); vb.vertex=true;
      NativeBackendBufferDesc ib{}; ib.bytes=sizeof(indices); ib.index=true;
      auto vertices=backend->CreateBuffer(vb,{reinterpret_cast<const uint8_t*>(positions),sizeof(positions)});
      auto index=backend->CreateBuffer(ib,{reinterpret_cast<const uint8_t*>(indices),sizeof(indices)});
      std::vector<uint8_t> reference;
      uint64_t reference_snapshot_bytes=0;
      const auto before=backend->Statistics();
      for(bool enabled:{false,true}) {
        const auto snapshot_start=backend->Statistics().geometry_constant_snapshot_bytes;
        backend->BeginFrame(); auto& r=backend->Recorder();
        r.ClearColor(*target,{0,0,0,0}); NativeBackendRenderTarget* targets[]{target.get()};
        r.SetRenderTargets(targets,nullptr); r.SetViewport({0,0,64,16,0,1}); r.SetPipeline(pipeline);
        r.SetVertexBuffer(0,*vertices,12,0); r.SetIndexBuffer(*index,NativeBackendIndexFormat::Uint16,0);
        r.SetWorldInstancing(enabled);
        for(unsigned draw=0;draw<300;++draw) {
          r.SetWorldInstancing(enabled && draw!=298); // Explicit ordinary-draw boundary.
          // Non-symmetric transforms expose a row/column transpose error.
          std::array<float,16> matrix{1,0,0,0,0,.8f,0,0,0,0,1,0,float(draw%64)/32,.1f,0,1};
          if(!row_major) for(unsigned row=0;row<4;++row) for(unsigned col=row+1;col<4;++col)
            std::swap(matrix[row*4+col],matrix[col*4+row]);
          vertex.SetConstant("g_mWorld",{reinterpret_cast<const uint8_t*>(matrix.data()),sizeof(matrix)});
          const std::array<float,4> gain{1,draw<280?1.f:.25f,1,1},tint{1,1,draw<295?1.f:.5f,1};
          vertex.SetConstant("gain",{reinterpret_cast<const uint8_t*>(gain.data()),sizeof(gain)});
          pixel.SetConstant("tint",{reinterpret_cast<const uint8_t*>(tint.data()),sizeof(tint)});
          for(const auto& c:vertex.ConstantImages()) r.SetConstants(NativeBackendStage::Vertex,c.slot,c.bytes);
          for(const auto& c:pixel.ConstantImages()) r.SetConstants(NativeBackendStage::Pixel,c.slot,c.bytes);
          r.SetScissor({0,0,64,draw<290?16:8},true);
          r.DrawIndexed(6,0,0);
        }
        // Save a deferred matrix after a merged draw, overwrite it with a
        // second instance, then restore and force ordinary rendering. The
        // red redraw must land on the saved instance, not the latest one.
        for(const auto& c:vertex.ConstantImages()) if(c.slot==vs.instance_world_slot) {
          std::vector<uint8_t> constants(c.bytes.begin(),c.bytes.end());
          for(unsigned extra=0;extra<2;++extra) {
            const float x=float(44+extra)/32;
            std::memcpy(constants.data()+vs.instance_world_offset+(row_major?12:3)*4,&x,4);
            r.SetConstants(NativeBackendStage::Vertex,c.slot,constants);
            r.DrawIndexed(6,0,0);
            if(!extra) r.PushState();
          }
        }
        r.PopState(); r.SetWorldInstancing(false);
        const std::array<float,4> red{1,0,0,1};
        pixel.SetConstant("tint",{reinterpret_cast<const uint8_t*>(red.data()),sizeof(red)});
        for(const auto& c:pixel.ConstantImages()) r.SetConstants(NativeBackendStage::Pixel,c.slot,c.bytes);
        r.DrawIndexed(6,0,0);
        backend->Submit(); const auto pixels=backend->ReadRenderTarget(*target);
        const auto snapshot_bytes=backend->Statistics().geometry_constant_snapshot_bytes-snapshot_start;
        if(!enabled) { reference=pixels; reference_snapshot_bytes=snapshot_bytes; }
        else {
          Check(pixels==reference,"instanced world matrices/state breaks changed rendered pixels");
          Check(snapshot_bytes<reference_snapshot_bytes,"world reuse did not reduce constant snapshot copies");
        }
      }
      const auto after=backend->Statistics();
      Check(after.geometry_folded_draws-before.geometry_folded_draws==295 &&
          after.geometry_instanced_draws-before.geometry_instanced_draws==6,
          "world instance cap, constant/scissor changes, or explicit fallback boundary failed");
      Check(after.geometry_world_constant_reuses-before.geometry_world_constant_reuses>=295,
            "world-only updates failed to reuse shared constants");
      Check(std::any_of(reference.begin(),reference.end(),[](auto value){return value!=0;}),"world instance reference was empty");
      for(const auto& message:backend->DrainValidationMessages()) Check(false,"world instance validation: "+message);
    }
  }
}

int main() {
  try { WorldInstancePackets(); }
  catch(const std::exception& error) { std::cerr << "world instancing: " << error.what() << "\n"; return 1; }
  try { ParallelResourceOrdering(); ParallelGeometryPackets(); TransientVerticesDoNotFlush(); }
  catch(const std::exception& error) { std::cerr << "parallel ordering: " << error.what() << "\n"; return 1; }
  std::cout << std::unitbuf;
  try {
    RegisterNativeD3D11Backend();
    RegisterNativeD3D12Backend();

    RegisterNativeRenderBackend("d3d12-packets-1",[]() -> std::unique_ptr<NativeRenderBackend> {
      NativeD3D12Options options; options.prefer_warp=true; options.debug_layer=true;
      options.geometry_workers=1; options.geometry_minimum_draws=1;
      return CreateNativeD3D12Backend(options);
    });
    RegisterNativeRenderBackend("d3d12-packets-4",[]() -> std::unique_ptr<NativeRenderBackend> {
      NativeD3D12Options options; options.prefer_warp=true; options.debug_layer=true;
      options.geometry_workers=4; options.geometry_minimum_draws=1;
      return CreateNativeD3D12Backend(options);
    });
    RegisterNativeRenderBackend("d3d12-packets-mixed",[]() -> std::unique_ptr<NativeRenderBackend> {
      NativeD3D12Options options; options.prefer_warp=true; options.debug_layer=true;
      options.geometry_workers=4;
      return CreateNativeD3D12Backend(options);
    });
    std::map<std::string, Rendered> results;
    for (const char* name : {"d3d11-warp", "d3d12-warp", "d3d12-packets-1", "d3d12-packets-4", "d3d12-packets-mixed"}) {
      const auto backend = CreateNativeRenderBackend(name);
      results.emplace(name, Render(*backend));
      for (const auto& message : results.at(name).validation)
        Check(false, std::string(name) + " validation error: " + message);
    }

    // Every backend must agree with every other. Two wrong backends agreeing
    // is possible, which is why each surface is also checked against what it
    // should be, below.
    const auto& reference = results.at("d3d11-warp");
    for (const auto& [name, rendered] : results) {
      if (name == "d3d11-warp") continue;
      const std::pair<const char*, const std::vector<uint8_t>*> surfaces[] = {
          {"flat", &rendered.flat}, {"textured", &rendered.textured},
          {"instanced", &rendered.instanced}, {"mipped", &rendered.mipped},
          {"compressed", &rendered.compressed}, {"blended", &rendered.blended},
          {"depth", &rendered.depth_tested}, {"target0", &rendered.target0},
          {"target1", &rendered.target1},
          {"dynamic-before", &rendered.dynamic_before},
          {"dynamic-after", &rendered.dynamic_after},
          {"transient-before", &rendered.transient_before},
          {"transient-after", &rendered.transient_after},
          {"sampled-target", &rendered.sampled_target},
          {"sampled-target-again", &rendered.sampled_target_again},
          {"uploaded", &rendered.uploaded}};
      const std::vector<uint8_t>* references[] = {&reference.flat, &reference.textured,
                                                  &reference.instanced, &reference.mipped,
                                                  &reference.compressed, &reference.blended,
                                                  &reference.depth_tested, &reference.target0,
                                                  &reference.target1, &reference.dynamic_before,
                                                  &reference.dynamic_after, &reference.transient_before,
                                                  &reference.transient_after, &reference.sampled_target,
                                                  &reference.sampled_target_again,
                                                  &reference.uploaded};
      for (size_t index = 0; index < 16; ++index) {
        const auto difference = Compare(*references[index], *surfaces[index].second);
        Check(difference.pixels == 0,
              std::string(name) + " differs from d3d11-warp on the " + surfaces[index].first +
                  " surface: " + std::to_string(difference.pixels) + " pixels, worst channel " +
                  std::to_string(difference.worst));
      }
      // The resolve is compared with a tolerance: the two runtimes are allowed
      // to place multisample positions differently, and on a diagonal edge
      // that changes the blend weights. Anything more than a nudge is not a
      // sample pattern difference.
      const auto resolve = Compare(reference.resolved, rendered.resolved);
      Check(resolve.worst <= 96, std::string(name) + " differs from d3d11-warp on the resolved " +
                                     "surface by " + std::to_string(resolve.worst) +
                                     " on some channel, which is more than a sample pattern");
      Check(rendered.query_samples == reference.query_samples,
            std::string(name) + " counted " + std::to_string(rendered.query_samples) +
                " occlusion samples where d3d11-warp counted " +
                std::to_string(reference.query_samples));
      std::cout << name << " vs d3d11-warp: identical on every surface including depth and MRT, resolve "
                << "differs by at most " << resolve.worst << " on " << resolve.pixels << " pixels\n";
    }

    // What the surfaces should be, so that two backends that are wrong in the
    // same way are still caught.
    for (const auto& [name, rendered] : results) {
      const auto at = [&](const std::vector<uint8_t>& image, uint32_t x, uint32_t y, uint32_t channel) {
        return image[(static_cast<size_t>(y) * kSize + x) * 4 + channel];
      };
      uint32_t covered = 0;
      for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x)
          if (at(rendered.flat, x, y, 1) > 200) ++covered;
      Check(covered > kSize * kSize * 4 / 10 && covered < kSize * kSize * 6 / 10,
            name + " covered " + std::to_string(covered) + " pixels with a half-covering triangle");
      // The dynamic buffer's second draw used the same triangle the flat
      // surface did, so that surface is the reference for it - exactly, not
      // approximately. The first draw used the complementary triangle and must
      // still show it: if the rewrite reached the storage those vertices were
      // already recorded against, this is where it shows.
      Check(Compare(rendered.dynamic_after, rendered.flat).pixels == 0,
            name + ": the draw after the dynamic rewrite did not use the new vertices");
      // Transient vertices are the dynamic buffer's contract without the
      // buffer, so the two pairs of targets must be pixel-identical.
      Check(Compare(rendered.transient_before, rendered.dynamic_before).pixels == 0,
            name + ": transient vertices drew differently from the dynamic buffer before its rewrite");
      Check(Compare(rendered.transient_after, rendered.dynamic_after).pixels == 0,
            name + ": transient vertices drew differently from the dynamic buffer after its rewrite");
      uint32_t before_covered = 0, both_green = 0;
      for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x) {
          const bool before = at(rendered.dynamic_before, x, y, 1) > 200;
          if (before) ++before_covered;
          if (before && at(rendered.flat, x, y, 1) > 200) ++both_green;
        }
      Check(before_covered > kSize * kSize * 4 / 10 && before_covered < kSize * kSize * 6 / 10,
            name + ": the draw before the dynamic rewrite covered " + std::to_string(before_covered) +
                " pixels, which is not a half-covering triangle");
      // The two triangles are complementary, so they may share only the
      // diagonal. Substantial overlap means the first draw got the second
      // draw's vertices.
      Check(both_green < kSize * 2,
            name + ": the draws either side of the dynamic rewrite overlap on " +
                std::to_string(both_green) + " pixels, so the rewrite reached the first one");
      // Sampling the target that was just drawn into reproduces it: the same
      // half-covering green triangle, over red rather than the blue clear the
      // second target started with. Blue anywhere means the sample read the
      // clear instead of the draw.
      uint32_t sampled_green = 0, sampled_red = 0, sampled_blue = 0;
      for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x) {
          if (at(rendered.sampled_target, x, y, 1) > 200) ++sampled_green;
          else if (at(rendered.sampled_target, x, y, 0) > 200) ++sampled_red;
          if (at(rendered.sampled_target, x, y, 2) > 200 &&
              at(rendered.sampled_target, x, y, 0) < 60) ++sampled_blue;
        }
      Check(sampled_blue == 0,
            name + ": " + std::to_string(sampled_blue) + " pixels of the second target were never " +
                "written, so sampling a target drawn in the same frame did not happen");
      Check(sampled_green == covered && sampled_red == kSize * kSize - covered,
            name + ": sampling a target drawn in the same frame gave " + std::to_string(sampled_green) +
                " green and " + std::to_string(sampled_red) + " red pixels, not " +
                std::to_string(covered) + " and " + std::to_string(kSize * kSize - covered));
      // The second round trip covered the whole target in white.
      uint32_t sampled_white = 0;
      for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x)
          if (at(rendered.sampled_target_again, x, y, 0) > 200 &&
              at(rendered.sampled_target_again, x, y, 1) > 200 &&
              at(rendered.sampled_target_again, x, y, 2) > 200) ++sampled_white;
      Check(sampled_white == kSize * kSize,
            name + ": the second write-then-sample round trip left " + std::to_string(sampled_white) +
                " of " + std::to_string(kSize * kSize) + " pixels white");
      Check(rendered.refused_partial_dynamic_update,
            name + " accepted a partial update of a dynamic buffer, whose untouched bytes are undefined");
      Check(rendered.query_samples == covered,
            name + " counted " + std::to_string(rendered.query_samples) +
                " occlusion samples for a draw covering " + std::to_string(covered) + " pixels");
      // The 2x2 texture's four texels, each in its own quadrant.
      Check(at(rendered.textured, kSize / 4, kSize / 4, 0) > 200 &&
                at(rendered.textured, kSize / 4, kSize / 4, 1) < 60,
            name + ": top-left quadrant is not the texture's red texel");
      Check(at(rendered.textured, kSize * 3 / 4, kSize * 3 / 4, 0) > 200 &&
                at(rendered.textured, kSize * 3 / 4, kSize * 3 / 4, 1) > 200,
            name + ": bottom-right quadrant is not the texture's yellow texel");
      // Each quadrant is one BC1 block, so a pitch computed in texels rather
      // than blocks reads the lower row from the wrong place and fails here.
      Check(at(rendered.compressed, kSize / 4, kSize / 4, 0) > 200 &&
                at(rendered.compressed, kSize / 4, kSize / 4, 1) < 60,
            name + ": BC1 top-left block is not red");
      Check(at(rendered.compressed, kSize * 3 / 4, kSize / 4, 1) > 200 &&
                at(rendered.compressed, kSize * 3 / 4, kSize / 4, 0) < 60,
            name + ": BC1 top-right block is not green");
      Check(at(rendered.compressed, kSize / 4, kSize * 3 / 4, 2) > 200 &&
                at(rendered.compressed, kSize / 4, kSize * 3 / 4, 0) < 60,
            name + ": BC1 bottom-left block is not blue");
      Check(at(rendered.compressed, kSize * 3 / 4, kSize * 3 / 4, 0) > 200 &&
                at(rendered.compressed, kSize * 3 / 4, kSize * 3 / 4, 1) > 200,
            name + ": BC1 bottom-right block is not yellow");
      // Blue is the nearest draw; red would mean the nearest was rejected and
      // green would mean depth testing did nothing at all.
      Check(at(rendered.depth_tested, kSize / 2, kSize / 2, 2) > 200 &&
                at(rendered.depth_tested, kSize / 2, kSize / 2, 1) < 60 &&
                at(rendered.depth_tested, kSize / 2, kSize / 2, 0) < 60,
            name + ": depth testing did not keep the nearest draw");
      Check(at(rendered.target0, kSize / 2, kSize / 2, 0) > 200 &&
                at(rendered.target0, kSize / 2, kSize / 2, 2) < 60,
            name + ": the first of two render targets is not red");
      Check(at(rendered.target1, kSize / 2, kSize / 2, 2) > 200 &&
                at(rendered.target1, kSize / 2, kSize / 2, 0) < 60,
            name + ": the second of two render targets is not blue");
      Check(rendered.refused_missing_blend_factor,
            name + ": a draw needing a constant blend factor was allowed without one");
      // source x factor, within the rounding of an 8-bit target.
      const auto blended = at(rendered.blended, kSize / 2, kSize / 2, 0);
      Check(std::abs(int(blended) - 128) <= 2,
            name + ": blend factor red is " + std::to_string(blended) + ", expected ~128");
      Check(std::abs(int(at(rendered.blended, kSize / 2, kSize / 2, 1)) - 64) <= 2,
            name + ": blend factor green is not ~64");
      Check(std::abs(int(at(rendered.blended, kSize / 2, kSize / 2, 2)) - 191) <= 2,
            name + ": blend factor blue is not ~191");
      Check(at(rendered.mipped, kSize / 2, kSize / 2, 2) > 200 &&
                at(rendered.mipped, kSize / 2, kSize / 2, 0) < 60,
            name + ": level 1 of the mipped texture is not the colour it was given");
      uint32_t instanced_area = 0;
      for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x)
          if (at(rendered.instanced, x, y, 0) > 200) ++instanced_area;
      Check(instanced_area > 500 && instanced_area < 850,
            name + ": four instances covered " + std::to_string(instanced_area) + " pixels");
      uint32_t partial = 0;
      for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x) {
          const uint8_t green = at(rendered.resolved, x, y, 1);
          if (green > 20 && green <= 200) ++partial;
        }
      Check(partial > 20, name + ": no partially covered pixels survived the resolve, so nothing " +
                              "was multisampled");
      std::cout << name << ": " << covered << " covered, " << rendered.query_samples
                << " samples, " << instanced_area << " instanced px, " << partial << " edge px\n";
    }
  } catch (const std::exception& error) {
    std::cerr << "unexpected: " << error.what() << '\n';
    return 1;
  }
  std::cout << "native backend conformance: " << failures << " failures\n";
  return failures ? 1 : 0;
}

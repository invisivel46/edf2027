// Does D3D12 actually cost less CPU per draw than D3D11, for this game's draw
// shape? The whole migration rests on that being true, so it is measured
// before 3,529 lines are rewritten against the answer.
//
// The workload is the measured one: ~2,370 draws a frame, a material
// activation roughly every 1.6 draws, one texture per draw, small indexed
// meshes. GPU work is kept trivial on purpose - this measures submission, not
// shading, because submission is what the profile said was the constraint.
//
// Both paths do the same thing the same number of times. Where they cannot,
// it is named in the output rather than hidden in the average.
//
// State variety matters more than it looks. A loop that binds one texture and
// one pipeline over and over is the best case for D3D11, whose runtime filters
// redundant binds cheaply, and it flatters it against an API that has less to
// filter. A real frame of this game changes material constantly, so the loop
// cycles textures every draw and pipelines every sixteen.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "native_graphics/d3d12_backend.h"
#include "native_graphics/native_render_backend.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace edf::native;
using Clock = std::chrono::steady_clock;

namespace {
void Require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }

ComPtr<ID3DBlob> Compile(const char* source, const char* entry, const char* profile) {
  ComPtr<ID3DBlob> code, errors;
  if (FAILED(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, entry, profile, 0, 0,
                        &code, &errors)))
    throw std::runtime_error(std::string("shader compile failed: ") +
                             (errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?"));
  return code;
}

const char* kSource = R"(
cbuffer Material : register(b0) { float4 tint; float4 spare[3]; };
Texture2D image : register(t0);
SamplerState filtering : register(s0);
struct Varying { float4 position : SV_POSITION; };
Varying VS(float3 position : POSITION) { Varying o; o.position = float4(position, 1); return o; }
float4 PS(Varying input) : SV_TARGET { return image.Sample(filtering, input.position.xy) * tint; }
)";

// A tiny triangle near the origin: it must rasterise something, or a driver is
// free to discard the draw and the measurement becomes a measurement of
// nothing. Small enough that shading cost stays irrelevant.
const float kVertices[] = {-0.01f, -0.01f, 0.0f, 0.01f, -0.01f, 0.0f, -0.01f, 0.01f, 0.0f};
const uint16_t kIndices[] = {0, 1, 2};

struct Result { double per_draw_us=0,per_frame_ms=0; uint64_t draws=0; };

// Enough variety that neither runtime is just filtering repeats, and close to
// what a frame of this game does: many materials, a handful of blend states.
constexpr uint32_t kTextures=64,kPipelines=4;

Result RunD3D11(uint32_t draws_per_frame, uint32_t frames) {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1,
                                      D3D11_SDK_VERSION, &device, nullptr, &context)),
          "D3D11 device creation");

  const auto vertex_code = Compile(kSource, "VS", "vs_5_0");
  const auto pixel_code = Compile(kSource, "PS", "ps_5_0");
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11PixelShader> pixel_shader;
  Require(SUCCEEDED(device->CreateVertexShader(vertex_code->GetBufferPointer(),
                                               vertex_code->GetBufferSize(), nullptr, &vertex_shader)),
          "vertex shader");
  Require(SUCCEEDED(device->CreatePixelShader(pixel_code->GetBufferPointer(),
                                              pixel_code->GetBufferSize(), nullptr, &pixel_shader)),
          "pixel shader");

  const D3D11_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
  ComPtr<ID3D11InputLayout> input_layout;
  Require(SUCCEEDED(device->CreateInputLayout(layout, 1, vertex_code->GetBufferPointer(),
                                              vertex_code->GetBufferSize(), &input_layout)),
          "input layout");

  const D3D11_BUFFER_DESC vertex_desc{sizeof(kVertices), D3D11_USAGE_IMMUTABLE,
                                      D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
  const D3D11_SUBRESOURCE_DATA vertex_data{kVertices, 0, 0};
  ComPtr<ID3D11Buffer> vertex_buffer;
  Require(SUCCEEDED(device->CreateBuffer(&vertex_desc, &vertex_data, &vertex_buffer)), "vertex buffer");
  const D3D11_BUFFER_DESC index_desc{sizeof(kIndices), D3D11_USAGE_IMMUTABLE,
                                     D3D11_BIND_INDEX_BUFFER, 0, 0, 0};
  const D3D11_SUBRESOURCE_DATA index_data{kIndices, 0, 0};
  ComPtr<ID3D11Buffer> index_buffer;
  Require(SUCCEEDED(device->CreateBuffer(&index_desc, &index_data, &index_buffer)), "index buffer");
  // Dynamic + WRITE_DISCARD is what this renderer does today, and it is the
  // path the upload ring replaces, so it is the one that must be compared.
  const D3D11_BUFFER_DESC constant_desc{64, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER,
                                        D3D11_CPU_ACCESS_WRITE, 0, 0};
  ComPtr<ID3D11Buffer> constant_buffer;
  Require(SUCCEEDED(device->CreateBuffer(&constant_desc, nullptr, &constant_buffer)), "constant buffer");

  D3D11_TEXTURE2D_DESC texture_desc{};
  texture_desc.Width = texture_desc.Height = 4;
  texture_desc.MipLevels = texture_desc.ArraySize = 1;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc = {1, 0};
  texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  std::vector<uint8_t> texels(4 * 4 * 4, 255);
  const D3D11_SUBRESOURCE_DATA texture_data{texels.data(), 16, 0};
  ComPtr<ID3D11Texture2D> texture;
  Require(SUCCEEDED(device->CreateTexture2D(&texture_desc, &texture_data, &texture)), "texture");
  ComPtr<ID3D11ShaderResourceView> texture_view;
  Require(SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &texture_view)), "srv");
  D3D11_SAMPLER_DESC sampler_desc{};
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = sampler_desc.AddressV = sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
  ComPtr<ID3D11SamplerState> sampler;
  Require(SUCCEEDED(device->CreateSamplerState(&sampler_desc, &sampler)), "sampler");

  // Extra textures and blend states so the loop is not measuring one bind
  // repeated; see the note at the top of the file.
  std::vector<ComPtr<ID3D11ShaderResourceView>> textures{texture_view};
  for (uint32_t index = 1; index < kTextures; ++index) {
    ComPtr<ID3D11Texture2D> extra;
    Require(SUCCEEDED(device->CreateTexture2D(&texture_desc, &texture_data, &extra)), "texture");
    ComPtr<ID3D11ShaderResourceView> view;
    Require(SUCCEEDED(device->CreateShaderResourceView(extra.Get(), nullptr, &view)), "srv");
    textures.push_back(view);
  }
  std::vector<ComPtr<ID3D11BlendState>> blends;
  for (uint32_t index = 0; index < kPipelines; ++index) {
    D3D11_BLEND_DESC blend{};
    auto& rt = blend.RenderTarget[0];
    rt.BlendEnable = index != 0;
    rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    rt.DestBlend = index < 2 ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_ONE;
    rt.BlendOp = index < 3 ? D3D11_BLEND_OP_ADD : D3D11_BLEND_OP_SUBTRACT;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_ZERO;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = 15;
    ComPtr<ID3D11BlendState> state;
    Require(SUCCEEDED(device->CreateBlendState(&blend, &state)), "blend state");
    blends.push_back(state);
  }

  texture_desc.Width = texture_desc.Height = 256;
  texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> target;
  Require(SUCCEEDED(device->CreateTexture2D(&texture_desc, nullptr, &target)), "render target");
  ComPtr<ID3D11RenderTargetView> target_view;
  Require(SUCCEEDED(device->CreateRenderTargetView(target.Get(), nullptr, &target_view)), "rtv");

  const UINT stride = sizeof(float) * 3, offset = 0;
  const D3D11_VIEWPORT viewport{0, 0, 256, 256, 0, 1};
  float tint[16]{1, 1, 1, 1};

  Result result;
  const auto start = Clock::now();
  for (uint32_t frame = 0; frame < frames; ++frame) {
    context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
    context->RSSetViewports(1, &viewport);
    context->IASetInputLayout(input_layout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex_shader.Get(), nullptr, 0);
    context->PSSetShader(pixel_shader.Get(), nullptr, 0);
    context->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &stride, &offset);
    context->IASetIndexBuffer(index_buffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    context->PSSetSamplers(0, 1, sampler.GetAddressOf());
    for (uint32_t draw = 0; draw < draws_per_frame; ++draw) {
      // A material activation every 1.6 draws, as measured: 44,917 activations
      // against 142,385 indexed draws a second.
      if (draw % 8 < 5) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context->Map(constant_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
          tint[0] = static_cast<float>(draw & 255) / 255.0f;
          std::memcpy(mapped.pData, tint, 64);
          context->Unmap(constant_buffer.Get(), 0);
        }
        context->PSSetConstantBuffers(0, 1, constant_buffer.GetAddressOf());
      }
      if (draw % 16 == 0) {
        const float factor[4]{1, 1, 1, 1};
        context->OMSetBlendState(blends[(draw / 16) % kPipelines].Get(), factor, 0xffffffff);
      }
      context->PSSetShaderResources(0, 1, textures[draw % kTextures].GetAddressOf());
      context->DrawIndexed(3, 0, 0);
      ++result.draws;
    }
    context->Flush();
  }
  const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
  result.per_draw_us = seconds * 1e6 / static_cast<double>(result.draws);
  result.per_frame_ms = seconds * 1e3 / frames;
  return result;
}

Result RunD3D12(uint32_t draws_per_frame, uint32_t frames, bool warp) {
  NativeD3D12Options options;
  options.prefer_warp = warp;
  const auto backend = CreateNativeD3D12Backend(options);

  NativeBackendTextureDesc target_desc{};
  target_desc.width = target_desc.height = 256;
  target_desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
  target_desc.render_target = true;
  const auto target = backend->CreateRenderTarget(target_desc);

  NativeBackendBufferDesc vertex_desc{};
  vertex_desc.bytes = sizeof(kVertices);
  vertex_desc.vertex = true;
  const auto vertex_buffer = backend->CreateBuffer(
      vertex_desc, {reinterpret_cast<const uint8_t*>(kVertices), sizeof(kVertices)});
  NativeBackendBufferDesc index_desc{};
  index_desc.bytes = sizeof(kIndices);
  index_desc.index = true;
  const auto index_buffer = backend->CreateBuffer(
      index_desc, {reinterpret_cast<const uint8_t*>(kIndices), sizeof(kIndices)});

  NativeBackendTextureDesc texture_desc{};
  texture_desc.width = texture_desc.height = 4;
  texture_desc.levels = 1;
  texture_desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
  std::vector<uint8_t> texels(4 * 4 * 4, 255);
  std::vector<std::unique_ptr<NativeBackendTexture>> textures;
  for (uint32_t index = 0; index < kTextures; ++index)
    textures.push_back(backend->CreateTexture(texture_desc, texels));

  NativeBackendSamplerDesc sampler_desc{};
  sampler_desc.min = sampler_desc.mag = sampler_desc.mip = NativeBackendFilter::Point;
  sampler_desc.u = sampler_desc.v = sampler_desc.w = NativeBackendAddress::Clamp;
  auto& sampler = backend->CreateSampler(sampler_desc);

  const auto vertex_code = Compile(kSource, "VS", "vs_5_0");
  const auto pixel_code = Compile(kSource, "PS", "ps_5_0");
  const NativeBackendInputElement layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, false, 0}};
  NativeBackendPipelineDesc pipeline_desc{};
  pipeline_desc.vertex = {static_cast<const uint8_t*>(vertex_code->GetBufferPointer()),
                          vertex_code->GetBufferSize()};
  pipeline_desc.pixel = {static_cast<const uint8_t*>(pixel_code->GetBufferPointer()),
                         pixel_code->GetBufferSize()};
  pipeline_desc.vertex_id = 1;
  pipeline_desc.pixel_id = 2;
  pipeline_desc.input_layout = layout;
  pipeline_desc.input_layout_id = 3;
  pipeline_desc.state = {0x10001, 0, 0, 0, 15, 0};
  pipeline_desc.topology = NativeBackendTopology::TriangleList;
  pipeline_desc.render_targets = 1;
  pipeline_desc.rtv_format[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  std::vector<NativeBackendPipeline*> pipelines;
  for (uint32_t index = 0; index < kPipelines; ++index) {
    // The same blend words the D3D11 side builds its states from, so the two
    // sides are switching between equivalent state, not merely switching.
    pipeline_desc.state[0] = index == 0 ? 0x10001u : (0x00060004u | (index << 5));
    pipeline_desc.pixel_id = 2 + index;
    pipelines.push_back(&backend->CreatePipeline(pipeline_desc));
  }

  NativeBackendRenderTarget* colors[] = {target.get()};
  float tint[16]{1, 1, 1, 1};

  Result result;
  const auto start = Clock::now();
  for (uint32_t frame = 0; frame < frames; ++frame) {
    backend->BeginFrame();
    auto& recorder = backend->Recorder();
    recorder.SetRenderTargets(colors, nullptr);
    recorder.SetViewport({0, 0, 256, 256, 0, 1});
    recorder.SetPipeline(*pipelines[0]);
    recorder.SetVertexBuffer(0, *vertex_buffer, sizeof(float) * 3, 0);
    recorder.SetIndexBuffer(*index_buffer, NativeBackendIndexFormat::Uint16, 0);
    recorder.SetSampler(NativeBackendStage::Pixel, 0, &sampler);
    for (uint32_t draw = 0; draw < draws_per_frame; ++draw) {
      if (draw % 8 < 5) {
        tint[0] = static_cast<float>(draw & 255) / 255.0f;
        recorder.SetConstants(NativeBackendStage::Pixel, 0,
                              {reinterpret_cast<const uint8_t*>(tint), 64});
      }
      if (draw % 16 == 0) recorder.SetPipeline(*pipelines[(draw / 16) % kPipelines]);
      recorder.SetTexture(NativeBackendStage::Pixel, 0, textures[draw % kTextures].get());
      recorder.DrawIndexed(3, 0, 0);
      ++result.draws;
    }
    backend->Submit();
  }
  const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
  result.per_draw_us = seconds * 1e6 / static_cast<double>(result.draws);
  result.per_frame_ms = seconds * 1e3 / frames;
  return result;
}
}  // namespace

int main(int argc, char** argv) {
  std::cout << std::unitbuf;
  uint32_t draws = 2370, frames = 200;
  bool warp = false;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--warp") warp = true;
    else if (argument.rfind("--draws=", 0) == 0) draws = std::stoul(argument.substr(8));
    else if (argument.rfind("--frames=", 0) == 0) frames = std::stoul(argument.substr(9));
    else { std::cerr << "usage: edf_native_backend_bench [--draws=N] [--frames=N] [--warp]\n"; return 2; }
  }
  try {
    std::cout << draws << " draws x " << frames << " frames on "
              << (warp ? "WARP" : "hardware") << '\n';
    // A warm-up run each, discarded: the first frames pay for pipeline
    // creation, allocator growth and driver state that a steady frame does not.
    RunD3D11(draws, 10);
    RunD3D12(draws, 10, warp);

    const auto eleven = RunD3D11(draws, frames);
    const auto twelve = RunD3D12(draws, frames, warp);
    std::cout << "D3D11: " << eleven.per_draw_us << " us/draw, " << eleven.per_frame_ms << " ms/frame\n";
    std::cout << "D3D12: " << twelve.per_draw_us << " us/draw, " << twelve.per_frame_ms << " ms/frame\n";
    const double change = (eleven.per_draw_us - twelve.per_draw_us) / eleven.per_draw_us * 100.0;
    std::cout << "D3D12 is " << (change >= 0 ? "" : "-") << std::abs(change) << "% "
              << (change >= 0 ? "cheaper" : "more expensive") << " per draw on the CPU\n";
    // Said plainly, because the number above is the premise of the migration
    // and a reader should not have to work out which direction is good.
    std::cout << (change > 10 ? "The migration's premise holds for this workload.\n"
                              : "The migration's premise does NOT clearly hold for this workload.\n");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}

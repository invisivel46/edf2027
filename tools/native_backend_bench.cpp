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
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cstring>
#include <iostream>
#include <functional>
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

// Per-draw work that is not the API. In the game the draw hook costs 1.83 us
// per draw while the API costs 0.30 us, so roughly 1.5 us of every draw is our
// own: guest reads, parameter decode, mesh lookup. Whether that work can move
// onto other threads is a different question from whether the API calls can,
// and it is the one that decides if stage 3 is worth building - so it is
// simulated here rather than assumed away.
uint64_t g_bridge_spin=0;
volatile uint64_t g_sink=0;
inline void SimulateBridgeWork() {
  uint64_t accumulator=g_sink;
  for(uint64_t index=0;index<g_bridge_spin;++index) accumulator=accumulator*6364136223846793005ull+1;
  g_sink=accumulator;
}
// Calibrated by measurement rather than by a guessed iteration count, because
// the loop's cost per iteration is whatever this machine makes it.
void CalibrateBridgeWork(double microseconds) {
  if(microseconds<=0) { g_bridge_spin=0; return; }
  g_bridge_spin=1000;
  for(int attempt=0;attempt<8;++attempt) {
    const auto start=Clock::now();
    for(int repeat=0;repeat<1000;++repeat) SimulateBridgeWork();
    const double each=std::chrono::duration<double>(Clock::now()-start).count()*1e6/1000.0;
    if(each<=0) break;
    const double scaled=g_bridge_spin*microseconds/each;
    g_bridge_spin=static_cast<uint64_t>(std::max(1.0,scaled));
  }
}

// Workers created once and parked on a condition variable. Creating a thread
// per frame costs tens of microseconds each, which is the same order as the
// thing being measured, and would turn this into a benchmark of std::thread.
class WorkerPool {
 public:
  explicit WorkerPool(uint32_t workers) {
    for (uint32_t index = 1; index < workers; ++index)
      threads_.emplace_back([this, index] { Run(index); });
  }
  ~WorkerPool() {
    { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; }
    wake_.notify_all();
    for (auto& thread : threads_) thread.join();
  }
  // Runs body(index) on every worker plus this thread, and returns when all of
  // them are done.
  void RunFrame(const std::function<void(uint32_t)>& body) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      body_ = &body;
      ++generation_;
      outstanding_ = static_cast<uint32_t>(threads_.size());
    }
    wake_.notify_all();
    body(0);
    std::unique_lock<std::mutex> lock(mutex_);
    done_.wait(lock, [this] { return outstanding_ == 0; });
    body_ = nullptr;
  }

 private:
  void Run(uint32_t index) {
    uint64_t seen = 0;
    for (;;) {
      const std::function<void(uint32_t)>* body = nullptr;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this, &seen] { return stop_ || generation_ != seen; });
        if (stop_) return;
        seen = generation_;
        body = body_;
      }
      (*body)(index);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        --outstanding_;
      }
      done_.notify_one();
    }
  }
  std::vector<std::thread> threads_;
  std::mutex mutex_;
  std::condition_variable wake_, done_;
  const std::function<void(uint32_t)>* body_ = nullptr;
  uint64_t generation_ = 0;
  uint32_t outstanding_ = 0;
  bool stop_ = false;
};

Result RunD3D11(uint32_t draws_per_frame, uint32_t frames, uint32_t work_threads) {
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

  // D3D11 cannot submit from more than one thread, but nothing stops the work
  // that is not submission from running on several and the results being
  // submitted serially. That is the fair comparison: if most of the draw
  // thread's cost is our own decode, this restructuring alone may capture most
  // of the win without any of the migration.
  WorkerPool pool(std::max(1u, work_threads));
  const uint32_t per_thread = (draws_per_frame + std::max(1u, work_threads) - 1) / std::max(1u, work_threads);
  const auto decode = [&](uint32_t index) {
    const uint32_t first = index * per_thread;
    const uint32_t last = std::min(first + per_thread, draws_per_frame);
    for (uint32_t draw = first; draw < last; ++draw) SimulateBridgeWork();
  };

  Result result;
  const auto start = Clock::now();
  for (uint32_t frame = 0; frame < frames; ++frame) {
    if (work_threads > 1) pool.RunFrame(decode);
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
      // Already done on the pool when the work was split off.
      if (work_threads <= 1) SimulateBridgeWork();
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

Result RunD3D12(uint32_t draws_per_frame, uint32_t frames, bool warp, uint32_t threads) {
  NativeD3D12Options options;
  options.prefer_warp = warp;
  options.recorders = threads;
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

  Require(backend->RecorderCount() == threads, "the backend did not give one recorder per thread");
  WorkerPool pool(threads);

  // Each recorder takes a contiguous slice of the frame's draws. Contiguous
  // rather than interleaved because that is how real work would be split -
  // by pass or by object range - and because interleaving would share cache
  // lines between threads and measure that instead.
  const uint32_t per_thread = (draws_per_frame + threads - 1) / threads;
  const auto record = [&](uint32_t index) {
    auto& recorder = backend->Recorder(index);
    recorder.SetRenderTargets(colors, nullptr);
    recorder.SetViewport({0, 0, 256, 256, 0, 1});
    recorder.SetPipeline(*pipelines[0]);
    recorder.SetVertexBuffer(0, *vertex_buffer, sizeof(float) * 3, 0);
    recorder.SetIndexBuffer(*index_buffer, NativeBackendIndexFormat::Uint16, 0);
    recorder.SetSampler(NativeBackendStage::Pixel, 0, &sampler);
    float tint[16]{1, 1, 1, 1};
    const uint32_t first = index * per_thread;
    const uint32_t last = std::min(first + per_thread, draws_per_frame);
    for (uint32_t draw = first; draw < last; ++draw) {
      if (draw % 8 < 5) {
        tint[0] = static_cast<float>(draw & 255) / 255.0f;
        recorder.SetConstants(NativeBackendStage::Pixel, 0,
                              {reinterpret_cast<const uint8_t*>(tint), 64});
      }
      if (draw % 16 == 0) recorder.SetPipeline(*pipelines[(draw / 16) % kPipelines]);
      SimulateBridgeWork();
      recorder.SetTexture(NativeBackendStage::Pixel, 0, textures[draw % kTextures].get());
      recorder.DrawIndexed(3, 0, 0);
    }
  };

  Result result;
  const auto start = Clock::now();
  for (uint32_t frame = 0; frame < frames; ++frame) {
    backend->BeginFrame();
    pool.RunFrame(record);
    backend->Submit();
    result.draws += draws_per_frame;
  }
  const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
  result.per_draw_us = seconds * 1e6 / static_cast<double>(result.draws);
  result.per_frame_ms = seconds * 1e3 / frames;
  return result;
}
}  // namespace

int main(int argc, char** argv) {
  std::cout << std::unitbuf;
  uint32_t draws = 2370, frames = 200, threads = 1;
  double bridge_us = 0;
  bool warp = false;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--warp") warp = true;
    else if (argument.rfind("--draws=", 0) == 0) draws = std::stoul(argument.substr(8));
    else if (argument.rfind("--frames=", 0) == 0) frames = std::stoul(argument.substr(9));
    else if (argument.rfind("--threads=", 0) == 0) threads = std::max(1ul, std::stoul(argument.substr(10)));
    else if (argument.rfind("--bridge-us=", 0) == 0) bridge_us = std::stod(argument.substr(12));
    else {
      std::cerr << "usage: edf_native_backend_bench [--draws=N] [--frames=N] [--threads=N]"
                   " [--bridge-us=F] [--warp]\n";
      return 2;
    }
  }
  try {
    CalibrateBridgeWork(bridge_us);
    std::cout << draws << " draws x " << frames << " frames on "
              << (warp ? "WARP" : "hardware");
    if (bridge_us > 0)
      std::cout << ", simulating " << bridge_us << " us of non-API work per draw";
    std::cout << '\n';
    // A warm-up run each, discarded: the first frames pay for pipeline
    // creation, allocator growth and driver state that a steady frame does not.
    RunD3D11(draws, 10, 1);
    RunD3D12(draws, 10, warp, 1);

    const auto eleven = RunD3D11(draws, frames, 1);
    const auto twelve = RunD3D12(draws, frames, warp, 1);
    std::cout << "D3D11 (1 thread, the only option): " << eleven.per_draw_us << " us/draw, "
              << eleven.per_frame_ms << " ms/frame\n";
    std::cout << "D3D12 (1 thread): " << twelve.per_draw_us << " us/draw, " << twelve.per_frame_ms
              << " ms/frame\n";
    if (threads > 1) {
      RunD3D12(draws, 10, warp, threads);
      const auto many = RunD3D12(draws, frames, warp, threads);
      std::cout << "D3D12 (" << threads << " threads): " << many.per_draw_us << " us/draw, "
                << many.per_frame_ms << " ms/frame -- "
                << (twelve.per_frame_ms / many.per_frame_ms) << "x over one thread, "
                << (eleven.per_frame_ms / many.per_frame_ms) << "x over D3D11\n";

      // The honest control: D3D11 with the non-API work moved onto the same
      // number of threads and submission left serial. If this lands close to
      // the D3D12 number, then the win is in restructuring our own work and
      // not in the API, and the migration is buying much less than it looks.
      RunD3D11(draws, 10, threads);
      const auto split = RunD3D11(draws, frames, threads);
      std::cout << "D3D11 (" << threads << "-thread decode, serial submit): " << split.per_draw_us
                << " us/draw, " << split.per_frame_ms << " ms/frame -- "
                << (eleven.per_frame_ms / split.per_frame_ms) << "x over one thread\n";
      const double extra = (split.per_frame_ms - many.per_frame_ms);
      std::cout << "What D3D12 adds beyond restructuring alone: " << extra << " ms/frame ("
                << (extra / split.per_frame_ms * 100.0) << "% of the restructured frame)\n";
    }
    if (bridge_us <= 0) {
      const double change = (eleven.per_draw_us - twelve.per_draw_us) / eleven.per_draw_us * 100.0;
      std::cout << "D3D12 is " << std::abs(change) << "% "
                << (change >= 0 ? "cheaper" : "more expensive") << " per draw on the CPU\n";
      std::cout << (change > 10
                        ? "Per-draw API cost: the migration helps.\n"
                        : "Per-draw API cost: the migration does NOT clearly help.\n");
      std::cout << "Run again with --bridge-us=1.5 --threads=N: per-draw API cost is only 0.30 us of "
                   "the 1.83 us this game spends per draw, so it is not what decides this.\n";
    } else {
      // With simulated work in the loop, a per-draw average is mostly that
      // work and says nothing. The frame numbers above are the comparison.
      std::cout << "Per-draw averages are not meaningful with --bridge-us set; compare the frame "
                   "times above.\n";
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}

// A triangle drawn through the backend seam by the D3D12 backend, on WARP, and
// read back pixel by pixel. This is the check that the seam is an interface a
// real backend can be built behind rather than a shape that merely compiles.
#include "native_graphics/d3d12_backend.h"
#include "native_graphics/native_render_backend.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <chrono>
#include <thread>
#include <vector>

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
                             (errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no detail"));
  return code;
}
std::span<const uint8_t> Bytes(ID3DBlob& blob) {
  return {static_cast<const uint8_t*>(blob.GetBufferPointer()), blob.GetBufferSize()};
}
}  // namespace

int main() {
  // Unbuffered: if a later stage kills the process, the output that says how
  // far it got must not die in the buffer with it.
  std::cout << std::unitbuf;
  HWND present_window = nullptr;
  WNDCLASSEXW present_class{};
  try {
    RegisterNativeD3D12Backend();
    const auto& names = NativeRenderBackendNames();
    Check(std::find(names.begin(), names.end(), "d3d12") != names.end(),
          "the D3D12 backend did not register itself");
    Check(std::find(names.begin(), names.end(), "d3d12-warp") != names.end(),
          "the WARP backend name is missing, so a driver bug cannot be attributed");
    // Registering twice must not throw: the entry point is callable from more
    // than one place and a duplicate registration would otherwise be fatal.
    RegisterNativeD3D12Backend();

    // Selected by name through the registry, so the whole path the game will
    // use is what gets exercised - not a direct constructor call the shipping
    // code never makes.
    auto backend = CreateNativeRenderBackend("d3d12-warp");
    Check(backend != nullptr, "the registry did not produce a backend");
    Check(backend->name() == "d3d12", "the backend reported the wrong name");
    Check(!backend->SupportsParallelRecording(),
          "the backend claims parallel recording it does not yet have");

    constexpr uint32_t kSize = 64;
    NativeBackendTextureDesc target_desc{};
    target_desc.width = target_desc.height = kSize;
    target_desc.levels = 1;
    target_desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    target_desc.render_target = true;
    const auto target = backend->CreateRenderTarget(target_desc);
    Check(target && target->width() == kSize, "the render target was not created at the size asked for");

    // A triangle covering exactly the lower-left half of the target, so the
    // readback can tell "drawn" from "cleared" by position and by area. Not the
    // usual (-1,-1)(3,-1)(-1,3) full-screen triangle: that covers everything,
    // which would make a completely wrong viewport look like a pass.
    const float vertices[] = {-1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, -1.0f, 1.0f, 0.0f};
    NativeBackendBufferDesc buffer_desc{};
    buffer_desc.bytes = sizeof(vertices);
    buffer_desc.vertex = true;
    const auto vertex_buffer = backend->CreateBuffer(
        buffer_desc, {reinterpret_cast<const uint8_t*>(vertices), sizeof(vertices)});

    const char* kSource = R"(
cbuffer PixelData : register(b0) { float4 tint; };
float4 VS(float3 position : POSITION) : SV_POSITION { return float4(position, 1); }
float4 PS(float4 position : SV_POSITION) : SV_TARGET { return tint; }
)";
    const auto vertex = Compile(kSource, "VS", "vs_5_0");
    const auto pixel = Compile(kSource, "PS", "ps_5_0");

    const NativeBackendInputElement layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, false, 0}};
    NativeBackendPipelineDesc pipeline_desc{};
    pipeline_desc.vertex = Bytes(*vertex.Get());
    pipeline_desc.pixel = Bytes(*pixel.Get());
    pipeline_desc.vertex_id = 0x11;
    pipeline_desc.pixel_id = 0x22;
    pipeline_desc.input_layout = layout;
    pipeline_desc.input_layout_id = 0x33;
    pipeline_desc.state = {0x10001, 0, 0, 0, 15, 0};  // No blending, no depth, solid, all channels.
    pipeline_desc.topology = NativeBackendTopology::TriangleList;
    pipeline_desc.render_targets = 1;
    pipeline_desc.rtv_format[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    auto& pipeline = backend->CreatePipeline(pipeline_desc);
    Check(&pipeline == &backend->CreatePipeline(pipeline_desc),
          "an identical pipeline description produced a second pipeline object");

    const std::array<float, 4> tint{0.0f, 1.0f, 0.0f, 1.0f};
    NativeBackendRenderTarget* colors[] = {target.get()};

    backend->BeginFrame();
    auto& recorder = backend->Recorder();
    recorder.SetRenderTargets(colors, nullptr);
    recorder.SetViewport({0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1});
    recorder.ClearColor(*target, {1.0f, 0.0f, 0.0f, 1.0f});
    recorder.SetPipeline(pipeline);
    recorder.SetVertexBuffer(0, *vertex_buffer, sizeof(float) * 3, 0);
    recorder.SetConstants(NativeBackendStage::Pixel, 0,
                          {reinterpret_cast<const uint8_t*>(tint.data()), sizeof(float) * 4});
    recorder.Draw(3, 0);
    backend->Submit();

    const auto pixels = backend->ReadRenderTarget(*target);
    Check(pixels.size() == static_cast<size_t>(kSize) * kSize * 4,
          "the readback was not tightly packed: " + std::to_string(pixels.size()) + " bytes");
    const auto at = [&](uint32_t x, uint32_t y) {
      const size_t index = (static_cast<size_t>(y) * kSize + x) * 4;
      return std::array<uint8_t, 4>{pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]};
    };
    // The triangle covers the lower-left; the opposite corner stays cleared.
    const auto inside = at(2, kSize - 3), outside = at(kSize - 2, 1);
    Check(inside[1] > 200 && inside[0] < 60,
          "the triangle did not draw: covered pixel is not the tint colour");
    Check(outside[0] > 200 && outside[1] < 60,
          "the clear did not happen: uncovered pixel is not the clear colour");

    uint32_t drawn = 0;
    for (uint32_t y = 0; y < kSize; ++y)
      for (uint32_t x = 0; x < kSize; ++x)
        if (at(x, y)[1] > 200) ++drawn;
    // Half the surface, give or take the diagonal. A wildly different number
    // means the viewport or the vertex buffer went somewhere unintended, which
    // a two-pixel spot check would not notice.
    const uint32_t total = kSize * kSize;
    Check(drawn > total * 4 / 10 && drawn < total * 6 / 10,
          "the triangle covered " + std::to_string(drawn) + " of " + std::to_string(total) +
              " pixels, which is not the half it should");
    std::cout << "triangle covered " << drawn << " of " << total << " pixels\n";

    {
      // A textured draw: upload a 2x2 texture, sample it with point filtering
      // and clamping, and read the quadrants back. This is the path every real
      // material takes, and none of it exists in a triangle of flat colour.
      const uint8_t texels[] = {
          255, 0,   0,   255,   0,   255, 0,   255,     // top row:    red,  green
          0,   0,   255, 255,   255, 255, 0,   255};    // bottom row: blue, yellow
      NativeBackendTextureDesc texture_desc{};
      texture_desc.width = texture_desc.height = 2;
      texture_desc.levels = 1;
      texture_desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
      const auto texture = backend->CreateTexture(texture_desc, texels);

      NativeBackendSamplerDesc sampler_desc{};
      sampler_desc.min = sampler_desc.mag = sampler_desc.mip = NativeBackendFilter::Point;
      sampler_desc.u = sampler_desc.v = sampler_desc.w = NativeBackendAddress::Clamp;
      auto& sampler = backend->CreateSampler(sampler_desc);
      Check(&sampler == &backend->CreateSampler(sampler_desc),
            "an identical sampler description produced a second sampler");

      const char* kTextured = R"(
Texture2D image : register(t0);
SamplerState filtering : register(s0);
struct Varying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Varying VS(float3 position : POSITION) {
  Varying output;
  output.position = float4(position, 1);
  output.uv = position.xy * 0.5 + 0.5;
  return output;
}
float4 PS(Varying input) : SV_TARGET { return image.Sample(filtering, input.uv); }
)";
      const auto textured_vs = Compile(kTextured, "VS", "vs_5_0");
      const auto textured_ps = Compile(kTextured, "PS", "ps_5_0");

      // A full-screen covering triangle is right here: every pixel should come
      // from the texture, so there is nothing for it to hide.
      const float cover[] = {-1.0f, -1.0f, 0.0f, 3.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f};
      NativeBackendBufferDesc cover_desc{};
      cover_desc.bytes = sizeof(cover);
      cover_desc.vertex = true;
      const auto cover_buffer = backend->CreateBuffer(
          cover_desc, {reinterpret_cast<const uint8_t*>(cover), sizeof(cover)});

      NativeBackendPipelineDesc textured{};
      textured.vertex = Bytes(*textured_vs.Get());
      textured.pixel = Bytes(*textured_ps.Get());
      textured.vertex_id = 0x44;
      textured.pixel_id = 0x55;
      textured.input_layout = layout;
      textured.input_layout_id = 0x33;
      textured.state = {0x10001, 0, 0, 0, 15, 0};
      textured.topology = NativeBackendTopology::TriangleList;
      textured.render_targets = 1;
      textured.rtv_format[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
      auto& textured_pipeline = backend->CreatePipeline(textured);

      backend->BeginFrame();
      auto& textured_recorder = backend->Recorder();
      textured_recorder.SetRenderTargets(colors, nullptr);
      textured_recorder.SetViewport({0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1});
      textured_recorder.ClearColor(*target, {0.0f, 0.0f, 0.0f, 1.0f});
      textured_recorder.SetPipeline(textured_pipeline);
      textured_recorder.SetVertexBuffer(0, *cover_buffer, sizeof(float) * 3, 0);
      textured_recorder.SetTexture(NativeBackendStage::Pixel, 0, texture.get());
      textured_recorder.SetSampler(NativeBackendStage::Pixel, 0, &sampler);
      textured_recorder.Draw(3, 0);
      backend->Submit();

      const auto sampled = backend->ReadRenderTarget(*target);
      const auto texel = [&](uint32_t x, uint32_t y) {
        const size_t index = (static_cast<size_t>(y) * kSize + x) * 4;
        return std::array<uint8_t, 4>{sampled[index], sampled[index + 1], sampled[index + 2]};
      };
      // uv maps NDC to [0,1] with v increasing upward, so the target's top rows
      // hold the texture's bottom row. Checking all four quadrants catches a
      // flip or a swapped row, which a single sample would not.
      const auto top_left = texel(kSize / 4, kSize / 4), top_right = texel(kSize * 3 / 4, kSize / 4);
      const auto bottom_left = texel(kSize / 4, kSize * 3 / 4),
                 bottom_right = texel(kSize * 3 / 4, kSize * 3 / 4);
      Check(top_left[2] > 200 && top_left[0] < 60, "top-left quadrant is not the texture's blue texel");
      Check(top_right[0] > 200 && top_right[1] > 200,
            "top-right quadrant is not the texture's yellow texel");
      Check(bottom_left[0] > 200 && bottom_left[1] < 60,
            "bottom-left quadrant is not the texture's red texel");
      Check(bottom_right[1] > 200 && bottom_right[0] < 60,
            "bottom-right quadrant is not the texture's green texel");
      std::cout << "textured draw: four quadrants sampled\n";
    }

    {
      // Indexed, instanced drawing: the stage-0 primitive. 77.4% of this game's
      // draws are a repeat of the one before differing only in per-instance
      // constants, so this is the call that is meant to replace them, and the
      // per-instance input element is how the differing data gets there.
      const float quad[] = {-0.2f, -0.2f, 0.0f, 0.2f, -0.2f, 0.0f,
                            -0.2f, 0.2f,  0.0f, 0.2f, 0.2f,  0.0f};
      const uint16_t indices[] = {0, 2, 1, 1, 2, 3};
      const float offsets[] = {-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};

      NativeBackendBufferDesc desc{};
      desc.bytes = sizeof(quad);
      desc.vertex = true;
      const auto quad_buffer =
          backend->CreateBuffer(desc, {reinterpret_cast<const uint8_t*>(quad), sizeof(quad)});
      desc.bytes = sizeof(offsets);
      const auto instance_buffer =
          backend->CreateBuffer(desc, {reinterpret_cast<const uint8_t*>(offsets), sizeof(offsets)});
      NativeBackendBufferDesc index_desc{};
      index_desc.bytes = sizeof(indices);
      index_desc.index = true;
      const auto index_buffer = backend->CreateBuffer(
          index_desc, {reinterpret_cast<const uint8_t*>(indices), sizeof(indices)});

      const char* kInstanced = R"(
cbuffer PixelData : register(b0) { float4 tint; };
float4 VS(float3 position : POSITION, float2 offset : TEXCOORD0) : SV_POSITION {
  return float4(position.xy + offset, 0, 1);
}
float4 PS(float4 position : SV_POSITION) : SV_TARGET { return tint; }
)";
      const auto instanced_vs = Compile(kInstanced, "VS", "vs_5_0");
      const auto instanced_ps = Compile(kInstanced, "PS", "ps_5_0");

      const NativeBackendInputElement instanced_layout[] = {
          {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, false, 0},
          {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0, true, 1}};
      NativeBackendPipelineDesc instanced{};
      instanced.vertex = Bytes(*instanced_vs.Get());
      instanced.pixel = Bytes(*instanced_ps.Get());
      instanced.vertex_id = 0x66;
      instanced.pixel_id = 0x77;
      instanced.input_layout = instanced_layout;
      instanced.input_layout_id = 0x88;
      instanced.state = {0x10001, 0, 0, 0, 15, 0};
      instanced.topology = NativeBackendTopology::TriangleList;
      instanced.render_targets = 1;
      instanced.rtv_format[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
      auto& instanced_pipeline = backend->CreatePipeline(instanced);

      const std::array<float, 4> white{1.0f, 1.0f, 1.0f, 1.0f};
      backend->BeginFrame();
      auto& r = backend->Recorder();
      r.SetRenderTargets(colors, nullptr);
      r.SetViewport({0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1});
      r.ClearColor(*target, {0.0f, 0.0f, 0.0f, 1.0f});
      r.SetPipeline(instanced_pipeline);
      r.SetVertexBuffer(0, *quad_buffer, sizeof(float) * 3, 0);
      r.SetVertexBuffer(1, *instance_buffer, sizeof(float) * 2, 0);
      r.SetIndexBuffer(*index_buffer, NativeBackendIndexFormat::Uint16, 0);
      r.SetConstants(NativeBackendStage::Pixel, 0,
                     {reinterpret_cast<const uint8_t*>(white.data()), sizeof(float) * 4});
      r.DrawIndexedInstanced(6, 4, 0, 0, 0);
      backend->Submit();

      const auto drawn = backend->ReadRenderTarget(*target);
      const auto lit = [&](uint32_t x, uint32_t y) {
        return drawn[(static_cast<size_t>(y) * kSize + x) * 4] > 200;
      };
      // One quad per quadrant, and nothing in the middle or the corners: four
      // instances that all landed on top of each other would light one spot,
      // and one instance drawn four times would light none of the others.
      // Not `near`/`far`: windef.h still defines those as macros.
      const uint32_t low = kSize / 4, high = kSize * 3 / 4;
      Check(lit(low, low) && lit(high, low) && lit(low, high) && lit(high, high),
            "not all four instances drew: a quadrant centre is unlit");
      Check(!lit(kSize / 2, kSize / 2), "the instances collapsed onto the centre");
      Check(!lit(1, 1) && !lit(kSize - 2, kSize - 2), "something drew outside the four quads");
      uint32_t area = 0;
      for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x)
          if (lit(x, y)) ++area;
      // Four quads of 0.4 x 0.4 in a 2.0 clip range: 4 x (0.2 x 64)^2 = ~655.
      Check(area > 500 && area < 850,
            "the four instances covered " + std::to_string(area) + " pixels, not the ~655 expected");
      std::cout << "instanced draw: 4 instances, " << area << " pixels\n";
    }

    {
      // Presentation, against a real but never-shown window. A swap chain is
      // the one part that cannot be checked by rendering to a texture, and its
      // characteristic failure - a back buffer presented in the wrong resource
      // state - is exactly what validation exists to catch.
      Check(backend->BackBuffer() == nullptr, "a back buffer existed before a window was attached");
      bool refused = false;
      try { backend->Present(false); } catch (const std::runtime_error&) { refused = true; }
      Check(refused, "Present with no window attached was accepted");

      // Declared outside the block that uses it: the window must outlive the
      // backend, because the swap chain holds it, and a window destroyed from
      // under a live swap chain is a crash rather than an error.
      WNDCLASSEXW window_class{};
      window_class.cbSize = sizeof(window_class);
      window_class.lpfnWndProc = DefWindowProcW;
      window_class.hInstance = GetModuleHandleW(nullptr);
      window_class.lpszClassName = L"EdfD3D12PresentTest";
      RegisterClassExW(&window_class);
      HWND window = CreateWindowExW(0, window_class.lpszClassName, L"", WS_POPUP, 0, 0, kSize, kSize,
                                    nullptr, nullptr, window_class.hInstance, nullptr);
      Check(window != nullptr, "the test could not create a window to present to");
      if (window) {
        backend->AttachWindow(window, kSize, kSize);
        auto* back = backend->BackBuffer();
        Check(back != nullptr, "no back buffer after attaching a window");
        Check(back->width() == kSize && back->height() == kSize,
              "the back buffer is not the size the window was attached at");

        // More frames than there are buffers, so the chain rotates and each
        // buffer is rendered into, presented, and come back to.
        for (uint32_t frame = 0; frame < 8; ++frame) {
          auto* buffer = backend->BackBuffer();
          NativeBackendRenderTarget* chain[] = {buffer};
          backend->BeginFrame();
          auto& present_recorder = backend->Recorder();
          present_recorder.SetRenderTargets(chain, nullptr);
          present_recorder.SetViewport({0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1});
          present_recorder.ClearColor(*buffer, {0.0f, 0.0f, 1.0f, 1.0f});
          backend->Submit();
          backend->Present(false);
        }
        for (const auto& message : backend->DrainValidationMessages())
          Check(false, "D3D12 validation error while presenting: " + message);

        // The buffer handed out must rotate; always returning the same one
        // would mean writing to a buffer the display is still showing.
        auto* first = backend->BackBuffer();
        backend->BeginFrame();
        backend->Submit();
        backend->Present(false);
        Check(first != backend->BackBuffer(), "the swap chain handed out the same buffer twice running");

        std::cout << "presented 9 frames across " << 3 << " buffers\n";
        DestroyWindow(window);
      }
      UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
    }

    {
      // Parallel recording, from real threads. Two recorders, two command
      // lists, each writing its own half of the target. The lists execute in
      // recorder order, so recorder 0 clears and recorder 1 can rely on it.
      NativeD3D12Options parallel_options;
      parallel_options.prefer_warp = true;
      parallel_options.debug_layer = true;
      parallel_options.recorders = 2;
      const auto parallel = CreateNativeD3D12Backend(parallel_options);
      Check(parallel->RecorderCount() == 2, "the backend did not create two recorders");
      Check(parallel->SupportsParallelRecording(),
            "a two-recorder backend still reports it cannot record in parallel");

      const auto split_target = parallel->CreateRenderTarget(target_desc);
      NativeBackendRenderTarget* split_colors[] = {split_target.get()};

      // Two triangles, each covering one half, so a recorder writing the wrong
      // half or not writing at all is visible rather than averaged away.
      const float left[] = {-1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, -1.0f, 1.0f, 0.0f};
      const float right[] = {0.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f};
      NativeBackendBufferDesc half_desc{};
      half_desc.bytes = sizeof(left);
      half_desc.vertex = true;
      const auto left_buffer =
          parallel->CreateBuffer(half_desc, {reinterpret_cast<const uint8_t*>(left), sizeof(left)});
      const auto right_buffer =
          parallel->CreateBuffer(half_desc, {reinterpret_cast<const uint8_t*>(right), sizeof(right)});

      NativeBackendPipelineDesc split_pipeline_desc = pipeline_desc;
      auto& split_pipeline = parallel->CreatePipeline(split_pipeline_desc);

      const std::array<float, 4> green{0.0f, 1.0f, 0.0f, 1.0f};
      const std::array<float, 4> blue{0.0f, 0.0f, 1.0f, 1.0f};

      parallel->BeginFrame();
      const auto record = [&](uint32_t index) {
        auto& recorder = parallel->Recorder(index);
        recorder.SetRenderTargets(split_colors, nullptr);
        recorder.SetViewport({0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1});
        if (index == 0) recorder.ClearColor(*split_target, {1.0f, 0.0f, 0.0f, 1.0f});
        recorder.SetPipeline(split_pipeline);
        recorder.SetVertexBuffer(0, index == 0 ? *left_buffer : *right_buffer, sizeof(float) * 3, 0);
        const auto& colour = index == 0 ? green : blue;
        recorder.SetConstants(NativeBackendStage::Pixel, 0,
                              {reinterpret_cast<const uint8_t*>(colour.data()), sizeof(float) * 4});
        recorder.Draw(3, 0);
      };
      std::thread worker([&] { record(1); });
      record(0);
      worker.join();
      parallel->Submit();

      const auto halves = parallel->ReadRenderTarget(*split_target);
      const auto channel = [&](uint32_t x, uint32_t y, uint32_t component) {
        return halves[(static_cast<size_t>(y) * kSize + x) * 4 + component];
      };
      // Counted rather than spot-checked. The first version of this sampled
      // points that sat exactly on a triangle edge, where coverage is a coin
      // flip, and blamed the backend for it.
      uint32_t green_left = 0, green_right = 0, blue_left = 0, blue_right = 0, cleared = 0;
      for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x) {
          const bool left_half = x < kSize / 2;
          if (channel(x, y, 1) > 200) ++(left_half ? green_left : green_right);
          else if (channel(x, y, 2) > 200) ++(left_half ? blue_left : blue_right);
          else if (channel(x, y, 0) > 200) ++cleared;
        }
      Check(green_left > 0, "recorder 0 drew nothing");
      Check(blue_right > 0, "recorder 1 drew nothing");
      // Each recorder owns a half, so its colour must not appear in the other.
      Check(green_right == 0, "recorder 0 drew into recorder 1's half");
      Check(blue_left == 0, "recorder 1 drew into recorder 0's half");
      // Recorder 0 issued the clear and recorder 1 relies on the lists
      // executing in order; red surviving on the right would mean they did not.
      Check(cleared > 0, "nothing was left cleared, so the triangles are not the shape this expects");
      for (const auto& message : parallel->DrainValidationMessages())
        Check(false, "D3D12 validation error while recording in parallel: " + message);
      std::cout << "parallel recording: " << green_left << " px from recorder 0, " << blue_right
                << " px from recorder 1\n";
    }

    {
      // An occlusion query around a draw whose coverage is already known from
      // the first test: 2,016 pixels. A query that returns a plausible-looking
      // number is not enough - it has to return that one.
      const auto query = backend->CreateQuery(NativeBackendQueryKind::Occlusion);
      Check(query != nullptr, "the occlusion query was not created");

      uint64_t samples = 0;
      std::span<uint8_t> into{reinterpret_cast<uint8_t*>(&samples), sizeof(samples)};
      backend->BeginFrame();
      auto& counted = backend->Recorder();
      counted.SetRenderTargets(colors, nullptr);
      counted.SetViewport({0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1});
      counted.ClearColor(*target, {0.0f, 0.0f, 0.0f, 1.0f});
      counted.SetPipeline(pipeline);
      counted.SetVertexBuffer(0, *vertex_buffer, sizeof(float) * 3, 0);
      counted.SetConstants(NativeBackendStage::Pixel, 0,
                           {reinterpret_cast<const uint8_t*>(tint.data()), sizeof(float) * 4});
      counted.BeginQuery(*query);
      counted.Draw(3, 0);
      counted.EndQuery(*query);
      backend->Submit();

      // Never blocks, so the caller polls. Reading it before the GPU has
      // reached the resolve must say "not ready", not hand back stale bytes.
      uint32_t attempts = 0;
      while (!backend->ReadQuery(*query, into) && attempts < 10000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ++attempts;
      }
      Check(attempts < 10000, "the occlusion query never became readable");
      Check(samples == 2016, "the occlusion query counted " + std::to_string(samples) +
                                 " samples, not the 2016 pixels that draw covers");
      std::cout << "occlusion query: " << samples << " samples after " << attempts << " polls\n";
    }
    {
      // A mipped texture, uploaded level by level. Level 1 is a different
      // colour from level 0, and the draw samples level 1 explicitly - so if
      // only the top level were uploaded, this reads back whatever level 1
      // happened to contain, which is not the colour asked for.
      NativeBackendTextureDesc mipped_desc{};
      mipped_desc.width = mipped_desc.height = 2;
      mipped_desc.levels = 2;
      mipped_desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
      // Level 0 is 2x2 red, level 1 is 1x1 blue, packed tightly one after the
      // other, which is how the disc assets arrive.
      const uint8_t mip_bytes[] = {255, 0, 0, 255,  255, 0, 0, 255,
                                   255, 0, 0, 255,  255, 0, 0, 255,
                                   0,   0, 255, 255};
      const auto mipped = backend->CreateTexture(mipped_desc, mip_bytes);

      NativeBackendSamplerDesc mip_sampler_desc{};
      mip_sampler_desc.min = mip_sampler_desc.mag = mip_sampler_desc.mip = NativeBackendFilter::Point;
      mip_sampler_desc.u = mip_sampler_desc.v = mip_sampler_desc.w = NativeBackendAddress::Clamp;
      auto& mip_sampler = backend->CreateSampler(mip_sampler_desc);

      const char* kMipped = R"(
Texture2D image : register(t0);
SamplerState filtering : register(s0);
struct Varying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Varying VS(float3 position : POSITION) {
  Varying output;
  output.position = float4(position, 1);
  output.uv = position.xy * 0.5 + 0.5;
  return output;
}
float4 PS(Varying input) : SV_TARGET { return image.SampleLevel(filtering, input.uv, 1); }
)";
      const auto mip_vs = Compile(kMipped, "VS", "vs_5_0");
      const auto mip_ps = Compile(kMipped, "PS", "ps_5_0");
      const float cover[] = {-1.0f, -1.0f, 0.0f, 3.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f};
      NativeBackendBufferDesc cover_desc{};
      cover_desc.bytes = sizeof(cover);
      cover_desc.vertex = true;
      const auto cover_buffer = backend->CreateBuffer(
          cover_desc, {reinterpret_cast<const uint8_t*>(cover), sizeof(cover)});

      NativeBackendPipelineDesc mip_pipeline_desc = pipeline_desc;
      mip_pipeline_desc.vertex = Bytes(*mip_vs.Get());
      mip_pipeline_desc.pixel = Bytes(*mip_ps.Get());
      mip_pipeline_desc.vertex_id = 0x99;
      mip_pipeline_desc.pixel_id = 0xAA;
      auto& mip_pipeline = backend->CreatePipeline(mip_pipeline_desc);

      backend->BeginFrame();
      auto& mip_recorder = backend->Recorder();
      mip_recorder.SetRenderTargets(colors, nullptr);
      mip_recorder.SetViewport({0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1});
      mip_recorder.ClearColor(*target, {0.0f, 0.0f, 0.0f, 1.0f});
      mip_recorder.SetPipeline(mip_pipeline);
      mip_recorder.SetVertexBuffer(0, *cover_buffer, sizeof(float) * 3, 0);
      mip_recorder.SetTexture(NativeBackendStage::Pixel, 0, mipped.get());
      mip_recorder.SetSampler(NativeBackendStage::Pixel, 0, &mip_sampler);
      mip_recorder.Draw(3, 0);
      backend->Submit();

      const auto sampled_mip = backend->ReadRenderTarget(*target);
      const size_t middle = (static_cast<size_t>(kSize / 2) * kSize + kSize / 2) * 4;
      Check(sampled_mip[middle + 2] > 200 && sampled_mip[middle] < 60,
            "level 1 of the mipped texture is not the colour it was uploaded with");
      std::cout << "mipped upload: level 1 sampled correctly\n";
    }

    {
      // A 4x multisampled target, resolved and then sampled. The renderer does
      // use 2x and 4x targets, so this is not a hypothetical path.
      //
      // Checked by looking for partially covered pixels along the diagonal.
      // Single-sample rasterisation can only produce the triangle colour or
      // the clear colour; intermediate values exist only if four samples were
      // taken and averaged. A resolve that silently degraded to a copy would
      // produce none, and would pass any check that only looked at corners.
      NativeBackendTextureDesc msaa_desc{};
      msaa_desc.width = msaa_desc.height = kSize;
      msaa_desc.levels = 1;
      msaa_desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
      msaa_desc.samples = 4;
      msaa_desc.render_target = true;
      const auto msaa_target = backend->CreateRenderTarget(msaa_desc);

      NativeBackendTextureDesc resolved_desc{};
      resolved_desc.width = resolved_desc.height = kSize;
      resolved_desc.levels = 1;
      resolved_desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
      const auto resolved = backend->CreateTexture(resolved_desc, {});

      NativeBackendPipelineDesc msaa_pipeline_desc = pipeline_desc;
      msaa_pipeline_desc.sample_count = 4;
      msaa_pipeline_desc.vertex_id = 0xB1;
      msaa_pipeline_desc.pixel_id = 0xB2;
      auto& msaa_pipeline = backend->CreatePipeline(msaa_pipeline_desc);

      NativeBackendRenderTarget* msaa_colors[] = {msaa_target.get()};
      backend->BeginFrame();
      auto& msaa_recorder = backend->Recorder();
      msaa_recorder.SetRenderTargets(msaa_colors, nullptr);
      msaa_recorder.SetViewport({0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1});
      msaa_recorder.ClearColor(*msaa_target, {0.0f, 0.0f, 0.0f, 1.0f});
      msaa_recorder.SetPipeline(msaa_pipeline);
      msaa_recorder.SetVertexBuffer(0, *vertex_buffer, sizeof(float) * 3, 0);
      msaa_recorder.SetConstants(NativeBackendStage::Pixel, 0,
                                 {reinterpret_cast<const uint8_t*>(tint.data()), sizeof(float) * 4});
      msaa_recorder.Draw(3, 0);
      msaa_recorder.ResolveTarget(*resolved, *msaa_target);
      backend->Submit();

      // Sampled through the normal single-sample target, which is the only
      // surface this seam can read back.
      const char* kBlit = R"(
Texture2D image : register(t0);
SamplerState filtering : register(s0);
struct Varying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Varying VS(float3 position : POSITION) {
  Varying output;
  output.position = float4(position, 1);
  output.uv = float2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
  return output;
}
float4 PS(Varying input) : SV_TARGET { return image.SampleLevel(filtering, input.uv, 0); }
)";
      const auto blit_vs = Compile(kBlit, "VS", "vs_5_0");
      const auto blit_ps = Compile(kBlit, "PS", "ps_5_0");
      const float cover[] = {-1.0f, -1.0f, 0.0f, 3.0f, -1.0f, 0.0f, -1.0f, 3.0f, 0.0f};
      NativeBackendBufferDesc cover_desc{};
      cover_desc.bytes = sizeof(cover);
      cover_desc.vertex = true;
      const auto cover_buffer = backend->CreateBuffer(
          cover_desc, {reinterpret_cast<const uint8_t*>(cover), sizeof(cover)});
      NativeBackendSamplerDesc point_desc{};
      point_desc.min = point_desc.mag = point_desc.mip = NativeBackendFilter::Point;
      point_desc.u = point_desc.v = point_desc.w = NativeBackendAddress::Clamp;
      auto& point_sampler = backend->CreateSampler(point_desc);

      NativeBackendPipelineDesc blit_desc = pipeline_desc;
      blit_desc.vertex = Bytes(*blit_vs.Get());
      blit_desc.pixel = Bytes(*blit_ps.Get());
      blit_desc.vertex_id = 0xB3;
      blit_desc.pixel_id = 0xB4;
      auto& blit_pipeline = backend->CreatePipeline(blit_desc);

      backend->BeginFrame();
      auto& blit_recorder = backend->Recorder();
      blit_recorder.SetRenderTargets(colors, nullptr);
      blit_recorder.SetViewport({0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1});
      blit_recorder.ClearColor(*target, {0.0f, 0.0f, 0.0f, 1.0f});
      blit_recorder.SetPipeline(blit_pipeline);
      blit_recorder.SetVertexBuffer(0, *cover_buffer, sizeof(float) * 3, 0);
      blit_recorder.SetTexture(NativeBackendStage::Pixel, 0, resolved.get());
      blit_recorder.SetSampler(NativeBackendStage::Pixel, 0, &point_sampler);
      blit_recorder.Draw(3, 0);
      backend->Submit();

      const auto edges = backend->ReadRenderTarget(*target);
      uint32_t partial = 0, full = 0;
      for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x) {
          const uint8_t green = edges[(static_cast<size_t>(y) * kSize + x) * 4 + 1];
          if (green > 200) ++full;
          else if (green > 20) ++partial;
        }
      Check(full > 1500, "the multisampled draw did not cover the target");
      Check(partial > 20, "no partially covered pixels survived the resolve: " +
                              std::to_string(partial) + ", so nothing was multisampled");
      for (const auto& message : backend->DrainValidationMessages())
        Check(false, "D3D12 validation error around the resolve: " + message);
      std::cout << "4x resolve: " << full << " covered, " << partial << " edge pixels\n";
    }

    // A missing barrier would not change a single pixel above; only the API's
    // own validation sees it, so it is read rather than left in the debugger.
    for (const auto& message : backend->DrainValidationMessages())
      Check(false, "D3D12 validation error: " + message);

    // Misuse of the frame lifecycle must be loud.
    bool caught = false;
    try { backend->Recorder(); } catch (const std::runtime_error&) { caught = true; }
    Check(caught, "the recorder was handed out with no frame open");
    caught = false;
    try { backend->Submit(); } catch (const std::runtime_error&) { caught = true; }
    Check(caught, "Submit outside a frame was accepted");

    // The backend goes first, then the window it was presenting to.
    backend.reset();
  } catch (const std::exception& error) {
    std::cerr << "unexpected: " << error.what() << '\n';
    return 1;
  }
  std::cout << "native d3d12 backend tests: " << failures << " failures\n";
  return failures ? 1 : 0;
}

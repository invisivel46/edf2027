// D3D12 device, frame ring and upload path against WARP, so the suite runs on
// a machine with no usable GPU exactly as the D3D11 tests already do. Nothing
// here loads ReXGlue or emulates Xbox GPU commands.
#include "native_graphics/d3d12_device.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
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
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

ComPtr<ID3D12Resource> MakeBuffer(ID3D12Device& device, uint64_t bytes, D3D12_HEAP_TYPE heap_type,
                                  D3D12_RESOURCE_STATES state) {
  const D3D12_HEAP_PROPERTIES heap{heap_type, D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                   D3D12_MEMORY_POOL_UNKNOWN, 0, 0};
  const D3D12_RESOURCE_DESC desc{D3D12_RESOURCE_DIMENSION_BUFFER, 0, bytes, 1, 1, 1,
                                 DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                 D3D12_RESOURCE_FLAG_NONE};
  ComPtr<ID3D12Resource> buffer;
  Require(SUCCEEDED(device.CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state,
                                                   nullptr, IID_PPV_ARGS(&buffer))),
          "buffer creation");
  return buffer;
}
void Transition(ID3D12GraphicsCommandList& commands, ID3D12Resource& resource,
                D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = &resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = from;
  barrier.Transition.StateAfter = to;
  commands.ResourceBarrier(1, &barrier);
}
}  // namespace

int main() {
  try {
    NativeD3D12Options options;
    options.prefer_warp = true;
    options.frames_in_flight = 3;
    options.upload_bytes = 1u << 20;
    // Validation catches the missing barriers that consequence 5 of the
    // migration note calls the hardest class of bug here. It is an optional
    // Windows feature, so the test reports whether it really engaged instead
    // of assuming it did.
    options.debug_layer = true;
    NativeD3D12Device gpu(options);
    Check(gpu.is_warp(), "the test did not get the WARP adapter it asked for");
    Check(!gpu.adapter_name().empty(), "the adapter did not report a name");
    std::cout << "adapter: " << gpu.adapter_name() << ", debug layer "
              << (gpu.debug_layer_active() ? "active" : "NOT AVAILABLE (barriers unvalidated)")
              << '\n';

    constexpr uint32_t kValues = 256;
    constexpr uint64_t kBytes = kValues * sizeof(uint32_t);
    auto target = MakeBuffer(*gpu.device(), kBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
    auto readback = MakeBuffer(*gpu.device(), kBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);

    // More frames than there are slots, so allocators and upload memory are
    // recycled many times over rather than each being used once.
    constexpr uint32_t kFrames = 32;
    for (uint32_t frame = 1; frame <= kFrames; ++frame) {
      auto* commands = gpu.BeginFrame();
      // Several allocations per frame, as a real frame's constants would be,
      // so the ring is exercised rather than just touched.
      std::vector<NativeD3D12Device::Upload> slices;
      for (uint32_t chunk = 0; chunk < 4; ++chunk) {
        const auto slice = gpu.Allocate(kBytes / 4);
        Check(slice.cpu != nullptr, "upload allocation returned no pointer");
        Check(slice.offset % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT == 0,
              "upload allocation was not aligned for a constant buffer view");
        auto* values = reinterpret_cast<uint32_t*>(slice.cpu);
        for (uint32_t i = 0; i < kValues / 4; ++i) values[i] = frame * 1000 + chunk * 64 + i;
        slices.push_back(slice);
      }
      for (uint32_t chunk = 0; chunk < 4; ++chunk)
        commands->CopyBufferRegion(target.Get(), chunk * (kBytes / 4), slices[chunk].resource,
                                   slices[chunk].offset, kBytes / 4);
      Transition(*commands, *target.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
      commands->CopyResource(readback.Get(), target.Get());
      Transition(*commands, *target.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
      gpu.EndFrame();
    }
    gpu.WaitIdle();

    // The readback holds the last frame's pattern. If the ring had handed out
    // memory a queued frame was still reading, this is where an earlier frame's
    // values would show up.
    void* mapped = nullptr;
    const D3D12_RANGE whole{0, kBytes};
    Require(SUCCEEDED(readback->Map(0, &whole, &mapped)), "readback map");
    const auto* values = static_cast<const uint32_t*>(mapped);
    uint32_t wrong = 0;
    for (uint32_t chunk = 0; chunk < 4; ++chunk)
      for (uint32_t i = 0; i < kValues / 4; ++i) {
        const uint32_t expected = kFrames * 1000 + chunk * 64 + i;
        if (values[chunk * (kValues / 4) + i] != expected) ++wrong;
      }
    const D3D12_RANGE none{0, 0};
    readback->Unmap(0, &none);
    Check(wrong == 0, "the GPU read back " + std::to_string(wrong) + " of " + std::to_string(kValues) +
                          " values that did not match the frame that wrote them");

    // The debug layer only helps if something reads it. A missing barrier in
    // this file, or later in the backend built on this device, fails here.
    for (const auto& message : gpu.DrainValidationErrors())
      Check(false, "D3D12 validation error: " + message);

    Check(gpu.frames_submitted() == kFrames, "the device did not count the frames it submitted");
    Check(gpu.upload_ring().high_water() <= options.upload_bytes, "the ring exceeded its own capacity");
    std::cout << "upload ring high water " << gpu.upload_ring().high_water() << " of "
              << options.upload_bytes << ", " << gpu.upload_stalls() << " stalls\n";

    {
      // Views are ring-allocated per frame; the same combination of samplers
      // must come back as the same table, because the sampler heap is far too
      // small to hand out a fresh one per draw.
      auto describe = [](float lod) {
        D3D12_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MaxLOD = lod;
        return sampler;
      };
      const D3D12_SAMPLER_DESC one[] = {describe(1.0f), describe(2.0f)};
      const D3D12_SAMPLER_DESC two[] = {describe(1.0f), describe(3.0f)};
      const auto first = gpu.samplers().Table(one);
      const auto again = gpu.samplers().Table(one);
      const auto other = gpu.samplers().Table(two);
      Check(first.ptr == again.ptr, "the sampler cache did not reuse an identical combination");
      Check(first.ptr != other.ptr, "two different sampler combinations shared one table");
      Check(gpu.samplers().hits() == 1 && gpu.samplers().misses() == 2,
            "the sampler cache did not account for its hits and misses");

      gpu.BeginFrame();
      const auto views = gpu.AllocateViews(7);
      const auto next = gpu.AllocateViews(7);
      Check(views.cpu.ptr != 0 && views.gpu.ptr != 0, "a view table came back null");
      Check(next.cpu.ptr - views.cpu.ptr == 7ull * gpu.views().increment(),
            "consecutive view tables were not laid out contiguously");
      gpu.EndFrame();
      gpu.WaitIdle();
    }
    {
      // A table wider than the root signature's sampler width has to be
      // refused, and a heap over the hardware limit has to be refused at
      // construction rather than at the first draw that overflows it.
      bool refused = false;
      try {
        NativeD3D12Options narrow = options;
        narrow.sampler_slots = 8;
        narrow.sampler_tables = 4096;  // 32,768 descriptors; the limit is 2,048.
        NativeD3D12Device impossible(narrow);
      } catch (const std::runtime_error&) { refused = true; }
      Check(refused, "a sampler heap larger than the hardware limit was accepted");
    }


    // Lifecycle misuse must be loud, not merely wrong.
    bool caught = false;
    try { gpu.EndFrame(); } catch (const std::runtime_error&) { caught = true; }
    Check(caught, "EndFrame outside a frame was accepted");
    caught = false;
    gpu.BeginFrame();
    try { gpu.BeginFrame(); } catch (const std::runtime_error&) { caught = true; }
    Check(caught, "a second frame was opened while one was still open");
    caught = false;
    try { gpu.Allocate(options.upload_bytes * 2); } catch (const std::runtime_error&) { caught = true; }
    Check(caught, "an upload larger than the whole ring was accepted");
    gpu.EndFrame();
    gpu.WaitIdle();
  } catch (const std::exception& error) {
    std::cerr << "unexpected: " << error.what() << '\n';
    return 1;
  }
  std::cout << "native d3d12 tests: " << failures << " failures\n";
  return failures ? 1 : 0;
}

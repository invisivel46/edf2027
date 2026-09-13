#include "native_graphics/native_render_backend.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
int failures = 0;
void Check(bool ok, const char* message) {
  if (ok) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

// A backend only has to satisfy the interface; nothing here touches a GPU, so
// selection and registration stay testable on a machine with no device.
class StubBackend final : public edf::native::NativeRenderBackend {
 public:
  explicit StubBackend(std::string name, bool parallel)
      : name_(std::move(name)), parallel_(parallel) {}
  std::string_view name() const override { return name_; }
  std::unique_ptr<edf::native::NativeBackendBuffer> CreateBuffer(
      const edf::native::NativeBackendBufferDesc&, std::span<const uint8_t>) override { return {}; }
  std::unique_ptr<edf::native::NativeBackendTexture> CreateTexture(
      const edf::native::NativeBackendTextureDesc&, std::span<const uint8_t>) override { return {}; }
  std::unique_ptr<edf::native::NativeBackendRenderTarget> CreateRenderTarget(
      const edf::native::NativeBackendTextureDesc&) override { return {}; }
  std::unique_ptr<edf::native::NativeBackendQuery> CreateQuery(
      edf::native::NativeBackendQueryKind) override { return {}; }
  edf::native::NativeBackendSampler& CreateSampler(
      const edf::native::NativeBackendSamplerDesc&) override {
    throw std::runtime_error("stub backend builds no samplers");
  }
  edf::native::NativeBackendPipeline& CreatePipeline(
      const edf::native::NativeBackendPipelineDesc&) override {
    throw std::runtime_error("stub backend builds no pipelines");
  }
  edf::native::NativeBackendRecorder& Recorder(uint32_t) override {
    throw std::runtime_error("stub backend records nothing");
  }
  uint32_t RecorderCount() const override { return 1; }
  bool ReadQuery(edf::native::NativeBackendQuery&, std::span<uint8_t>) override { return false; }
  bool SupportsParallelRecording() const override { return parallel_; }
  void BeginFrame() override {}
  std::vector<uint8_t> ReadRenderTarget(edf::native::NativeBackendRenderTarget&) override { return {}; }
  std::vector<uint8_t> ReadTexture(edf::native::NativeBackendTexture&) override { return {}; }
  std::vector<std::string> DrainValidationMessages() override { return {}; }
  void AttachWindow(void*, uint32_t, uint32_t) override {}
  edf::native::NativeBackendRenderTarget* BackBuffer() override { return nullptr; }
  void Present(bool) override {}
  std::unique_ptr<edf::native::NativeBackendTexture> OpenSharedTexture(
      void*, const edf::native::NativeBackendTextureDesc&) override { return {}; }
  bool WaitSharedFence(void*, uint64_t) override { return false; }
  // A backend that cannot share says so, which is what a caller has to handle.
  std::unique_ptr<edf::native::NativeBackendSharedSurface> CreateSharedSurface(
      const edf::native::NativeBackendTextureDesc&) override { return {}; }
  uint64_t SignalShared(edf::native::NativeBackendSharedSurface&) override { return 0; }
  bool SupportsSamples(uint32_t, uint32_t samples) override { return samples==1; }
  void Submit() override {}
 private:
  std::string name_;
  bool parallel_;
};
}  // namespace

int main() {
  using namespace edf::native;
  try {
    // An unknown name must fail loudly. Falling back to whatever backend
    // happens to be registered would make an A/B comparison silently compare
    // the same backend against itself.
    bool rejected = false;
    try { CreateNativeRenderBackend("no-such-backend"); }
    catch (const std::runtime_error& error) {
      rejected = std::string(error.what()).find("no-such-backend") != std::string::npos;
    }
    Check(rejected, "unknown backend name was not rejected by name");

    RegisterNativeRenderBackend("stub-serial", []() -> std::unique_ptr<NativeRenderBackend> {
      return std::make_unique<StubBackend>("stub-serial", false);
    });
    RegisterNativeRenderBackend("alpha-parallel", []() -> std::unique_ptr<NativeRenderBackend> {
      return std::make_unique<StubBackend>("alpha-parallel", true);
    });

    const auto& names = NativeRenderBackendNames();
    Check(names.size() == 2, "registered backends were not both listed");
    Check(names.front() == "alpha-parallel",
          "backend listing is not ordered, so an A/B run is not reproducible");

    const auto serial = CreateNativeRenderBackend("stub-serial");
    Check(serial && serial->name() == "stub-serial", "selection returned the wrong backend");
    Check(serial && !serial->SupportsParallelRecording(),
          "serial backend claimed parallel recording");
    const auto parallel = CreateNativeRenderBackend("alpha-parallel");
    Check(parallel && parallel->SupportsParallelRecording(),
          "parallel backend lost its capability through selection");

    // Two selections must be independent instances: a second backend will hold
    // per-instance device state and sharing one would alias it.
    Check(serial.get() != parallel.get(), "selection returned a shared instance");

    bool duplicate_rejected = false;
    try {
      RegisterNativeRenderBackend("stub-serial", []() -> std::unique_ptr<NativeRenderBackend> {
        return std::make_unique<StubBackend>("stub-serial", false);
      });
    } catch (const std::runtime_error&) { duplicate_rejected = true; }
    Check(duplicate_rejected, "a duplicate backend name silently replaced the original");

    bool invalid_rejected = false;
    try { RegisterNativeRenderBackend("", nullptr); }
    catch (const std::runtime_error&) { invalid_rejected = true; }
    Check(invalid_rejected, "an empty registration was accepted");
  } catch (const std::exception& error) {
    std::cerr << "unexpected: " << error.what() << '\n';
    return 1;
  }
  std::cout << "native backend tests: " << failures << " failures\n";
  return failures ? 1 : 0;
}

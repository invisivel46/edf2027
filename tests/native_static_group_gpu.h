#pragma once
#include <array>
#include <cstdint>
#include <vector>

struct NativeStaticGroupGpuInputs {
  std::vector<uint8_t> declaration,vertices,indices;
  uint32_t stride=0;
  std::array<float,4> tint{};
  std::array<float,16> world{};
  std::array<uint32_t,6> state{};
};
// Called from the real CPU differential's submission callback. Pixel expectations
// are selected separately from the supplied data, so corrupt ingestion fails.
void VerifyNativeStaticGroupGpu(const NativeStaticGroupGpuInputs&,unsigned draws,bool changed_material);
void FinishNativeStaticGroupGpu();

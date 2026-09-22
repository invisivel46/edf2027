#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace edf::native {
// Named boundaries, not a certification of all transitive code. In particular,
// a compatibility wrapper may itself choose a native implementation.
enum class NativeSceneBoundary : uint32_t {
  OriginalGroup, GeometrySetup, MaterialActivation, InstanceSetup, IndexedDraw,
  RetirementAllocation, AliasedConstant, StateCallback, AuditOracle,
  TextureBinding, ShaderBinding, UnsupportedConfiguration, Count
};
inline constexpr std::array<const char*,static_cast<size_t>(NativeSceneBoundary::Count)> kNativeSceneBoundaryNames{
  "original group", "geometry setup", "material activation", "instance setup", "indexed draw",
  "retirement allocation", "aliased constant", "state callback", "audit oracle",
  "texture binding", "shader binding", "unsupported configuration"
};
class NativeSceneExecution {
 public:
  void Enter(NativeSceneBoundary boundary,bool reject=false) {
    const auto index=static_cast<size_t>(boundary);
    ++calls_.at(index);
    mask_|=uint32_t(1)<<index;
    // Record the attempted boundary even when a strict diagnostic run rejects
    // it. Callers invoke Enter before entering the compatibility routine.
    if(reject) throw std::runtime_error(std::string("static group compatibility boundary: ")+kNativeSceneBoundaryNames.at(index));
  }
  uint64_t Calls(NativeSceneBoundary boundary) const { return calls_.at(static_cast<size_t>(boundary)); }
  uint64_t Total() const { uint64_t total=0; for(auto count:calls_) total+=count; return total; }
  uint32_t mask() const { return mask_; }
  void Recorded() { ++recordings_; }
  template<class Record>
  auto RecordBatch(Record&& record) {
    if(recording_failed_) throw std::runtime_error("static group recording failed; partial GPU work cannot be retried");
    try {
      auto result=record();
      Recorded();
      return result;
    } catch(...) { recording_failed_=true; throw; }
  }
  bool recording_failed() const { return recording_failed_; }
  uint64_t recordings() const { return recordings_; }
  void Complete() {
    if(recording_failed_) throw std::runtime_error("failed static group cannot complete");
    complete_=true;
  }
  bool complete() const { return complete_; }
 private:
  std::array<uint64_t,static_cast<size_t>(NativeSceneBoundary::Count)> calls_{};
  uint64_t recordings_=0;
  uint32_t mask_=0;
  bool complete_=false,recording_failed_=false;
};
}

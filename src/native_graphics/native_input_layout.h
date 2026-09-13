#pragma once
#include "native_render_backend.h"
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

namespace edf::native {
// A vertex layout that owns its semantic names.
//
// NativeBackendInputElement holds a `const char*`, which is right for a
// description built and consumed in one call. A mesh has to keep its layout
// so a pipeline can be built from it later, and the names it would otherwise
// borrow come from shader reflection and live only as long as that reflection
// object. Storing the borrowed pointer would work until a shader was freed and
// then read freed memory, which surfaces as a mangled semantic name and an
// input layout that fails for no visible reason.
//
// Names go in a deque because its element addresses are stable across growth;
// a vector would move them and every element already added would dangle.
class NativeOwnedInputLayout {
 public:
  void Add(std::string semantic, uint32_t semantic_index, uint32_t format, uint32_t slot,
           uint32_t offset, bool per_instance=false, uint32_t step_rate=0) {
    names_.push_back(std::move(semantic));
    elements_.push_back({names_.back().c_str(),semantic_index,format,slot,offset,
                         per_instance,step_rate});
  }
  std::span<const NativeBackendInputElement> elements() const { return elements_; }
  size_t size() const { return elements_.size(); }
  bool empty() const { return elements_.empty(); }

  // Identity for a pipeline cache key. Hashing the description rather than
  // using the object's address, because two meshes with the same layout must
  // share a pipeline and two layouts that differ must not.
  uint64_t fingerprint() const {
    uint64_t hash=1469598103934665603ull;
    const auto mix=[&hash](uint64_t value) { hash^=value; hash*=1099511628211ull; };
    for(size_t index=0;index<elements_.size();++index) {
      for(const char* at=names_[index].c_str();*at;++at) mix(uint64_t(uint8_t(*at)));
      const auto& element=elements_[index];
      mix(element.semantic_index); mix(element.format); mix(element.slot);
      mix(element.offset); mix(element.per_instance?1:0); mix(element.step_rate);
    }
    return hash;
  }

 private:
  std::deque<std::string> names_;
  std::vector<NativeBackendInputElement> elements_;
};
}  // namespace edf::native

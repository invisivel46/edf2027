#pragma once
#include "guest_block.h"
#include "utility_layout.h"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <vector>

namespace edf::native {
class NativeDeclaration {
 public:
  static std::shared_ptr<const NativeDeclaration> Create(std::span<const uint8_t> bytes) {
    if(bytes.size()%12 || bytes.size()>64*12)
      throw std::runtime_error("invalid native declaration extent");
    return std::shared_ptr<const NativeDeclaration>(new NativeDeclaration(bytes));
  }
  std::span<const uint8_t> bytes() const { return bytes_; }
  uint32_t count() const { return uint32_t(bytes_.size()/12); }
  // Specialized Utility layout conversion belongs to this source generation,
  // not to a draw. Exceptions leave the variant unpublished and retryable.
  std::shared_ptr<const NativeDeclaration> Utility2D(bool textured) const {
    const size_t variant=textured?1:0;
    std::call_once(utility_once_[variant],[&] {
      utility_[variant]=Create(Utility2DDeclaration(bytes_,textured));
    });
    return utility_[variant];
  }
  template<size_t N> std::array<uint32_t,N> Words(size_t offset=0) const {
    static_assert(N<=64*3);
    if(offset>bytes_.size() || N*4>bytes_.size()-offset)
      throw std::runtime_error("native declaration read outside owned extent");
    std::array<uint32_t,N> result{};
    for(size_t i=0;i<N;++i) result[i]=GuestBlockWord(bytes_.data()+offset+i*4);
    return result;
  }
 private:
  explicit NativeDeclaration(std::span<const uint8_t> bytes):bytes_(bytes.begin(),bytes.end()) {}
  const std::vector<uint8_t> bytes_;
  mutable std::once_flag utility_once_[2];
  mutable std::shared_ptr<const NativeDeclaration> utility_[2];
};
// Publish only at completed declaration constructors / FVF conversion. A new
// identity replaces the old handle, while queued/cache users retain old storage.
class NativeDeclarations {
 public:
  void Publish(uint32_t handle,std::span<const uint8_t> bytes) {
    if(!handle) throw std::runtime_error("null native declaration owner");
    auto fresh=NativeDeclaration::Create(bytes);
    declarations_.insert_or_assign(handle,std::move(fresh));
  }
  std::shared_ptr<const NativeDeclaration> Get(uint32_t handle) const { return declarations_.at(handle); }
  void Retire(uint32_t handle) { declarations_.erase(handle); }
 private:
  std::map<uint32_t,std::shared_ptr<const NativeDeclaration>> declarations_;
};
}

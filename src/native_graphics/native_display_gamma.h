#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace edf::native {
// Own the CPU display table, not its guest address or GPU packet encoding.
// Input consists of three consecutive channels of 256 big-endian halfwords.
// Table mode discards six low bits. PWL mode interprets pairs as base/delta,
// also with six hardwired-zero low bits, and uses an eight-code segment width.
class NativeDisplayGamma {
 public:
  enum class Mode { Table256, Piecewise128 };
  static NativeDisplayGamma Decode(std::span<const uint8_t> bytes,Mode mode) {
    if(bytes.size()!=1536) throw std::invalid_argument("display gamma table must contain 1536 bytes");
    if(mode!=Mode::Table256 && mode!=Mode::Piecewise128)
      throw std::invalid_argument("unknown display gamma mode");
    NativeDisplayGamma result;
    result.mode_=mode;
    for(size_t channel=0;channel<3;++channel) for(size_t entry=0;entry<256;++entry) {
      const auto offset=(channel*256+entry)*2;
      const auto value=uint16_t((uint16_t(bytes[offset])<<8)|bytes[offset+1]);
      result.values_[channel][entry]=mode==Mode::Table256?value>>6:value&0xffc0;
    }
    return result;
  }
  Mode mode() const { return mode_; }
  float EvaluateNormalizedCode(uint32_t channel,uint32_t code) const {
    const float value=float(EvaluateCode(channel,code))/(mode_==Mode::Table256?1023.f:65472.f);
    return value>1.f?1.f:value;
  }
  // An 8-bit source selects an exact table entry. A 10-bit source selects one
  // of 128 PWL segments plus its 3-bit fractional position. No host gamma curve
  // is substituted. Output stays in the mode's native integer precision.
  uint32_t EvaluateCode(uint32_t channel,uint32_t code) const {
    if(channel>=3) throw std::out_of_range("display gamma channel");
    if(mode_==Mode::Table256) {
      if(code>255) throw std::out_of_range("8-bit display gamma input");
      return values_[channel][code];
    }
    if(code>1023) throw std::out_of_range("10-bit display gamma input");
    const auto entry=(code>>3)*2;
    return values_[channel][entry]+((code&7)*values_[channel][entry+1])/8;
  }
 private:
  Mode mode_=Mode::Table256;
  std::array<std::array<uint16_t,256>,3> values_{};
};
}

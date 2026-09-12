#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <cstring>

namespace edf::native {
// Ordinary CPU-owned state only. The caller validates writable memory and
// supplies exclusive ownership; this is not atomic completion publication.
template<size_t N>
void StoreGuestCpuWords(std::span<uint8_t> destination,const std::array<uint32_t,N>& words) {
  static_assert(N>0);
  if(destination.size()!=N*4) throw std::runtime_error("CPU guest state block size mismatch");
  std::array<uint8_t,N*4> encoded{};
  for(size_t i=0;i<N;++i) for(size_t byte=0;byte<4;++byte)
    encoded[i*4+byte]=uint8_t(words[i]>>(24-byte*8));
  std::memcpy(destination.data(),encoded.data(),encoded.size());
}
// Caller must validate the containing block before decoding a word from it.
inline uint32_t GuestBlockWord(const uint8_t* p) {
  return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
}
// Short-lived read window for fields in one already allocated guest object.
// Construct inside a native draw; never retain it across a guest call, release,
// or allocation. Out-of-window reads still use the validating backing reader.
template<class Reader>
class GuestReadWindow {
 public:
  GuestReadWindow(const Reader& reader,uint32_t address,size_t size)
      : reader_(reader),address_(address) {
    if(!address || size>0x100000000ull-address)
      throw std::runtime_error("invalid guest read window");
    bytes_={reader.Bytes(address,size),size};
  }
  const uint8_t* Bytes(uint32_t address,size_t size) const {
    if(address>=address_) {
      const size_t offset=size_t(address-address_);
      if(offset<=bytes_.size() && size<=bytes_.size()-offset)
        return bytes_.data()+offset;
    }
    return reader_.Bytes(address,size);
  }
  uint32_t Add(uint32_t address,uint32_t offset) const {return reader_.Add(address,offset);}
  uint32_t Word(uint32_t address) const {return GuestBlockWord(Bytes(address,4));}
  // Strings may continue outside the containing object; keep their original
  // bounded/page-aware validation instead of assuming the window owns them.
  std::string String(uint32_t address,size_t limit) const {return reader_.String(address,limit);}
 private:
  const Reader& reader_;
  uint32_t address_;
  std::span<const uint8_t> bytes_;
};
template<std::size_t Count,class Reader>
std::array<uint32_t,Count> ReadGuestWords(const Reader& reader,uint32_t address) {
  const auto* bytes=reader.Bytes(address,Count*4);
  std::array<uint32_t,Count> words{};
  for(std::size_t i=0;i<Count;++i) words[i]=GuestBlockWord(bytes+i*4);
  return words;
}
}  // namespace edf::native

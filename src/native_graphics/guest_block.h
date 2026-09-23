#pragma once
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <new>
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
  // For a range the caller may not read to its end (a list it stops reading
  // at a terminator): the range is proven when it can be, and otherwise the
  // window is empty and every read goes to the reader, which then validates,
  // and refuses, exactly the reads that are really made.
  GuestReadWindow(const Reader& reader,uint32_t address,size_t size,std::nothrow_t)
      : reader_(reader),address_(address) {
    if(!address || !size || size>0x100000000ull-address) return;
    try { bytes_={reader.Bytes(address,size),size}; }
    catch(const std::exception&) { bytes_={}; }
  }
  const uint8_t* Bytes(uint32_t address,size_t size) const {
    if(address>=address_ && bytes_.data()) {
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
// Live read/write window over one guest object that a run of CPU mirror
// accesses both reads and writes: the material activation's device block,
// where every state override re-reads the render pass (twenty-odd words) and
// every setter stores words and dirty halves, each a separately validated
// guest access before this. The range is proven writable (so readable) once;
// inside it a read is a plain load and a store the reader's own interlocked
// big-endian exchange, both on the live bytes, so nothing is cached and a
// guest call that writes the block in between is seen by the next read.
// Accesses outside the range, and every access when the proof failed, go to
// the reader unchanged: a window that cannot be proven costs speed only, and
// throws only where the reader itself would. Only for a block that outlives
// the run (the guest device); never across its release.
template<class Reader>
class GuestWritableWindow {
 public:
  GuestWritableWindow(const Reader& reader,uint32_t address,size_t size)
      : reader_(reader),address_(address) {
    if(!address || !size || size>0x100000000ull-address) return;
    try { bytes_={const_cast<uint8_t*>(reader.WritableBytes(address,size,4)),size}; }
    catch(const std::exception&) { bytes_={}; }
  }
  bool proven() const { return !bytes_.empty(); }
  const uint8_t* Bytes(uint32_t address,size_t size) const {
    if(auto* inside=Inside(address,size)) return inside;
    return reader_.Bytes(address,size);
  }
  uint32_t Add(uint32_t address,uint32_t offset) const {return reader_.Add(address,offset);}
  uint32_t Word(uint32_t address) const {return GuestBlockWord(Bytes(address,4));}
  uint64_t DoubleWord(uint32_t address) const {
    const auto* p=Bytes(address,8);
    return (uint64_t(GuestBlockWord(p))<<32)|GuestBlockWord(p+4);
  }
  std::string String(uint32_t address,size_t limit) const {return reader_.String(address,limit);}
  // The reader's alignment contract: 4 or 8, on the guest address and the host
  // pointer alike. Anything the window cannot answer exactly goes to the reader.
  const uint8_t* WritableBytes(uint32_t address,size_t size,size_t alignment) const {
    if(size && (alignment==4 || alignment==8) && !(address&(alignment-1)))
      if(auto* inside=Inside(address,size); inside && !(reinterpret_cast<uintptr_t>(inside)&(alignment-1)))
        return inside;
    return reader_.WritableBytes(address,size,alignment);
  }
  void StoreWord(uint32_t address,uint32_t value) const {
    if(auto* inside=Aligned(address,4)) {
      std::atomic_ref<uint32_t>(*reinterpret_cast<uint32_t*>(inside)).exchange(Guest(value));
      return;
    }
    reader_.StoreWord(address,value);
  }
  void StoreDoubleWord(uint32_t address,uint64_t value) const {
    if(auto* inside=Aligned(address,8)) {
      std::atomic_ref<uint64_t>(*reinterpret_cast<uint64_t*>(inside)).exchange(Guest(value));
      return;
    }
    reader_.StoreDoubleWord(address,value);
  }
  void StoreByte(uint32_t address,uint8_t value) const {
    if(auto* inside=Aligned(address&~3u,4)) { inside[address&3u]=value; return; }
    reader_.StoreByte(address,value);
  }
 private:
  template<class T> static T Guest(T value) {
    if constexpr(std::endian::native==std::endian::little) return std::byteswap(value);
    else return value;
  }
  uint8_t* Inside(uint32_t address,size_t size) const {
    if(address<address_) return nullptr;
    const size_t offset=size_t(address-address_);
    if(offset>bytes_.size() || size>bytes_.size()-offset || bytes_.empty()) return nullptr;
    return bytes_.data()+offset;
  }
  uint8_t* Aligned(uint32_t address,size_t size) const {
    if(address&(size-1)) return nullptr;
    auto* inside=Inside(address,size);
    return inside && !(reinterpret_cast<uintptr_t>(inside)&(size-1))?inside:nullptr;
  }
  const Reader& reader_;
  uint32_t address_;
  std::span<uint8_t> bytes_;
};
template<std::size_t Count,class Reader>
std::array<uint32_t,Count> ReadGuestWords(const Reader& reader,uint32_t address) {
  const auto* bytes=reader.Bytes(address,Count*4);
  std::array<uint32_t,Count> words{};
  for(std::size_t i=0;i<Count;++i) words[i]=GuestBlockWord(bytes+i*4);
  return words;
}
}  // namespace edf::native

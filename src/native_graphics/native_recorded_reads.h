#pragma once
#include "guest_block.h"
#include <optional>
#include <vector>

namespace edf::native {
// Every guest byte a pure input reader consumed, in read order. Descriptor and
// material readers are deterministic functions of these bytes (pointers are
// read before they are followed): while each recorded range still holds the
// recorded bytes, a re-read would visit the same addresses and return the same
// result. Guest stores to these objects are untracked, so this is the change
// signal. Host-side identities (schemas, shaders, textures) are checked apart.
class NativeRecordedReads {
 public:
  static constexpr size_t kLimit=1u<<20;
  void Append(uint32_t address,const uint8_t* data,size_t size) {
    if(overflow_ || !size) return;
    if(bytes_.size()+size>kLimit) { overflow_=true; ranges_.clear(); bytes_.clear(); return; }
    if(!ranges_.empty() && uint64_t(ranges_.back().address)+ranges_.back().bytes==address &&
       ranges_.back().bytes+size<=4096) ranges_.back().bytes+=uint32_t(size);
    else ranges_.push_back({address,uint32_t(size)});
    bytes_.insert(bytes_.end(),data,data+size);
  }
  // Compares only the recorded ranges; no pointer is followed and nothing is
  // decoded. An unreadable range, an overflowed or empty record is a change.
  template<class Reader> bool Unchanged(const Reader& reader) const {
    if(overflow_ || ranges_.empty()) return false;
    try {
      size_t offset=0;
      for(const auto& range:ranges_) {
        if(std::memcmp(reader.Bytes(range.address,range.bytes),bytes_.data()+offset,range.bytes)) return false;
        offset+=range.bytes;
      }
    } catch(const std::exception&) { return false; }
    return true;
  }
  // Diagnostic: the first recorded range whose bytes differ, or cannot be read.
  template<class Reader> std::optional<uint32_t> FirstChange(const Reader& reader) const {
    size_t offset=0;
    for(const auto& range:ranges_) {
      try { if(std::memcmp(reader.Bytes(range.address,range.bytes),bytes_.data()+offset,range.bytes)) return range.address; }
      catch(const std::exception&) { return range.address; }
      offset+=range.bytes;
    }
    return {};
  }
  size_t ranges() const { return ranges_.size(); }
 private:
  struct Range { uint32_t address,bytes; };
  std::vector<Range> ranges_;
  std::vector<uint8_t> bytes_;
  bool overflow_=false;
};
template<class Reader>
class NativeRecordingReader {
 public:
  NativeRecordingReader(const Reader& reader,NativeRecordedReads& reads):reader_(reader),reads_(reads) {}
  uint32_t Add(uint32_t address,uint32_t offset) const { return reader_.Add(address,offset); }
  const uint8_t* Bytes(uint32_t address,size_t size) const {
    const auto* data=reader_.Bytes(address,size);
    reads_.Append(address,data,size);
    return data;
  }
  uint32_t Word(uint32_t address) const { return GuestBlockWord(Bytes(address,4)); }
  // For inputs a caller proves by predicate instead of recorded bytes.
  const Reader& Unrecorded() const { return reader_; }
 private:
  const Reader& reader_;
  NativeRecordedReads& reads_;
};
}

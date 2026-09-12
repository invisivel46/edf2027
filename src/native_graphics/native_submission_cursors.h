#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>

namespace edf::native {
// Caller serializes access. Generation rejects publication across device reset.
class NativeSubmissionCursors {
 public:
  struct Snapshot { uint32_t cursor,mask; uint64_t generation; };
  void Initialize(uint32_t device,uint32_t mask) {
    if(next_generation_==UINT64_MAX) throw std::runtime_error("native cursor generation exhausted");
    entries_.insert_or_assign(device,Snapshot{0,mask,++next_generation_});
  }
  void Retire(uint32_t device) { entries_.erase(device); }
  Snapshot Get(uint32_t device) const {
    const auto found=entries_.find(device);
    if(found==entries_.end()) throw std::runtime_error("native submission cursor is not initialized");
    return found->second;
  }
  void Validate(uint32_t device,Snapshot expected) const {
    const auto found=entries_.find(device);
    if(found==entries_.end() || found->second.generation!=expected.generation)
      throw std::runtime_error("native submission cursor reset during submission");
  }
  void Publish(uint32_t device,Snapshot expected,uint32_t cursor) {
    Validate(device,expected);
    entries_.at(device).cursor=cursor;
  }
 private:
  uint64_t next_generation_=0;
  std::unordered_map<uint32_t,Snapshot> entries_;
};

// Only these two ABI fields are native-authoritative. The publisher writes a
// compatibility mirror after committing native state; it never imports it back.
template<class Reader,class Publish>
class NativeSubmissionCursorAccess {
 public:
  NativeSubmissionCursorAccess(const Reader& reader,uint32_t device,
      NativeSubmissionCursors::Snapshot snapshot,Publish publish)
      : reader_(reader),device_(device),snapshot_(snapshot),publish_(publish) {}
  uint32_t Add(uint32_t address,uint32_t offset) const { return reader_.Add(address,offset); }
  const uint8_t* Bytes(uint32_t address,std::size_t bytes) const { return reader_.Bytes(address,bytes); }
  uint32_t Word(uint32_t address) const {
    if(address==Add(device_,10820)) return snapshot_.cursor;
    if(address==Add(device_,13480)) return snapshot_.mask;
    return reader_.Word(address);
  }
  void StoreWord(uint32_t address,uint32_t value) const {
    if(address==Add(device_,10820)) publish_(value);
    else reader_.StoreWord(address,value);
  }
 private:
  const Reader& reader_;
  uint32_t device_;
  NativeSubmissionCursors::Snapshot snapshot_;
  Publish publish_;
};
}

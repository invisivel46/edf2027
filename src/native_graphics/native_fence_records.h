#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include "native_wait_progress.h"

namespace edf::native {
struct NativeFenceRecord : std::array<uint32_t,6> {
  NativeWaitProgress progress;
};
// One registry per host thread. TLS distinguishes guest contexts hosted there.
class NativeFenceRecords {
  std::unordered_map<uint64_t,NativeFenceRecord> records_;
  static uint64_t Key(uint32_t tls,uint32_t token) { return (uint64_t(tls)<<32)|token; }
 public:
  void Publish(uint32_t tls,uint32_t token,const NativeFenceRecord& record) {
    if(!records_.emplace(Key(tls,token),record).second)
      throw std::runtime_error("native fence record already active");
  }
  NativeFenceRecord& Find(uint32_t tls,uint32_t token) {
    const auto found=records_.find(Key(tls,token));
    if(found==records_.end()) throw std::runtime_error("native fence record is not active on this thread");
    return found->second;
  }
  NativeFenceRecord Take(uint32_t tls,uint32_t token) {
    auto result=Find(tls,token);
    records_.erase(Key(tls,token));
    return result;
  }
  size_t size() const { return records_.size(); }
  void Discard(uint32_t tls,uint32_t token) noexcept { records_.erase(Key(tls,token)); }
};

// Translate only the opaque record token's six word fields to native storage.
// Device/thread/counter accesses remain explicit guest compatibility accesses.
template<class Reader>
class NativeFenceRecordAccess {
  const Reader& guest_;
  uint32_t token_;
  NativeFenceRecord& record_;
  bool IsRecord(uint32_t address) const {
    return address>=token_ && uint64_t(address)<uint64_t(token_)+24;
  }
  size_t Index(uint32_t address) const {
    if((address-token_)%4) throw std::runtime_error("unaligned native fence record field");
    return (address-token_)/4;
  }
  void RequireGuestRange(uint32_t address,size_t size) const {
    if(uint64_t(address)<uint64_t(token_)+24 &&
       (address>=token_ || size>uint64_t(token_)-address))
      throw std::runtime_error("unsupported native fence record access");
  }
 public:
  NativeFenceRecordAccess(const Reader& guest,uint32_t token,NativeFenceRecord& record)
      :guest_(guest),token_(token),record_(record) {
    if(token%4 || uint64_t(token)+24>(uint64_t(1)<<32))
      throw std::runtime_error("invalid native fence record token");
  }
  uint32_t Add(uint32_t address,uint32_t offset) const { return guest_.Add(address,offset); }
  uint32_t Word(uint32_t address) const {
    return IsRecord(address)?record_[Index(address)]:guest_.Word(address);
  }
  void StoreWord(uint32_t address,uint32_t value) const {
    if(IsRecord(address)) record_[Index(address)]=value;
    else guest_.StoreWord(address,value);
  }
  uint64_t DoubleWord(uint32_t address) const { RequireGuestRange(address,8); return guest_.DoubleWord(address); }
  void StoreDoubleWord(uint32_t address,uint64_t value) const { RequireGuestRange(address,8); guest_.StoreDoubleWord(address,value); }
  auto Bytes(uint32_t address,size_t size) const { RequireGuestRange(address,size); return guest_.Bytes(address,size); }
};
}

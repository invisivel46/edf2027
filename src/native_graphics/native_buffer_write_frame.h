#pragma once
#include "native_buffer_writes.h"

namespace edf::native {
// Stack-owned transaction for an audited producer whose writes occur between
// separate lock/unlock calls. Nesting restores the previous producer; unwinding
// the producer releases a pending scope even if its unlock was never reached.
class NativeBufferWriteFrame {
 public:
  NativeBufferWriteFrame():previous_(current_) { current_=this; }
  NativeBufferWriteFrame(const NativeBufferWriteFrame&)=delete;
  NativeBufferWriteFrame& operator=(const NativeBufferWriteFrame&)=delete;
  ~NativeBufferWriteFrame() { current_=previous_; }
  static NativeBufferWriteFrame& Current() {
    if(!current_) throw std::runtime_error("native inline writer has no producer frame");
    return *current_;
  }
  void Begin(uint32_t owner,NativeBufferWrites* queue,uint32_t destination=0,
      std::optional<NativeBufferWrites::Range> range=std::nullopt) {
    if(!owner || owner_) throw std::runtime_error("native inline writer lock sequence changed");
    writer_.emplace(queue,range,NativeBufferWrites::WriterKind::InlineIndices); owner_=owner; destination_=destination;
  }
  void RequireOwner(uint32_t owner) const {
    if(!owner_ || owner_!=owner) throw std::runtime_error("native inline writer unlock owner changed");
  }
  template<class Completed> void Finish(uint32_t owner,Completed completed) {
    RequireOwner(owner);
    // Queue the exact completed extent before allowing snapshots. This callback
    // must not acquire renderer locks; renderer bookkeeping follows Finish.
    // If notification throws, retain the scope for conservative unwind handling.
    completed(destination_);
    writer_.reset(); owner_=0; destination_=0;
  }
  void Finish(uint32_t owner) { Finish(owner,[](uint32_t) {}); }
  void RequireFinished() const {
    if(owner_) throw std::runtime_error("native inline writer returned without unlock");
  }
 private:
  inline static thread_local NativeBufferWriteFrame* current_=nullptr;
  NativeBufferWriteFrame* previous_;
  uint32_t owner_=0;
  uint32_t destination_=0;
  std::optional<NativeBufferWrites::WriterScope> writer_;
};
}

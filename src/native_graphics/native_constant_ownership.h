#pragma once
#include <cstdint>

namespace edf::native {
// Stack-scoped routing for audited Xbox packet helpers in a native CPU tail.
// This is not evidence of successful rendering. A zero device masks an outer
// scope for nonnative nested calls; native failures still omit Xbox packets.
// Construct and destroy on the same thread. No guest pointers are retained.
class NativeConstantOwnership {
 public:
  static NativeConstantOwnership ForCpuOnlyTail(uint32_t device,bool native_host) {
    // CPU-only tails never request guest immediate storage. Do not inherit an
    // outer legacy allocation suppression extent, including on nested draws.
    return NativeConstantOwnership(native_host?device:0);
  }
  explicit NativeConstantOwnership(uint32_t device,uint64_t immediate_bytes=0)
      : previous_(device_),previous_words_(immediate_words_) {
    device_=device;
    immediate_words_=device && immediate_bytes && immediate_bytes<=128*1024*1024 &&
      !(immediate_bytes%4) ? uint32_t(immediate_bytes/4) : 0;
  }
  ~NativeConstantOwnership() { device_=previous_; immediate_words_=previous_words_; }
  NativeConstantOwnership(const NativeConstantOwnership&)=delete;
  NativeConstantOwnership& operator=(const NativeConstantOwnership&)=delete;
  static bool Owns(uint32_t device,uint32_t bank,uint32_t source) {
    if(!device_ || device!=device_) return false;
    return (bank==16384 && device<=UINT32_MAX-1792 && source==device+1792) ||
           (bank==17408 && device<=UINT32_MAX-5888 && source==device+5888);
  }
  static bool OwnsRenderWords(uint32_t device,uint32_t bank,uint32_t source,uint64_t dirty) {
    // DB60 is a pure packet copy. These are the exact source extents used by
    // the audited FD428/FE358 CPU prefixes; their dirty clears and derived
    // state calls stay in those callers. The high bit denotes the first word.
    if(!device_ || device!=device_ || !dirty) return false;
    struct Bank { uint32_t number,offset,words; };
    constexpr Bank banks[]{{18688,9984,40},{8192,10240,16},{8448,10316,21},
      {8576,10400,5},{8704,10420,12},{8832,10468,21},{8960,10552,38},{9088,10704,8}};
    for(const auto& entry:banks) if(bank==entry.number) {
      const uint64_t covered=UINT64_MAX<<(64-entry.words);
      return device<=UINT32_MAX-(entry.offset+entry.words*4-1) &&
        source==device+entry.offset && !(dirty&~covered);
    }
    return false;
  }
  static bool OwnsFetchWords(uint32_t device,uint64_t dirty) {
    // DDA0 copies up to32 six-word fetch descriptors from device+1024.
    // CPU descriptor updates remain in the caller, independently of draw success.
    return device_ && device==device_ && device<=UINT32_MAX-(1024+32*24-1) &&
      dirty && !(dirty&0xffffffffull);
  }
  static bool OwnsImmediateAllocation(uint32_t device,uint32_t caller,uint32_t words,uint32_t alignment) {
    // Exact allocation inside FD428, called by the native-submitted FD8F8 scope.
    // Do not suppress allocations from other consumers of this shared allocator.
    return device_ && device==device_ && immediate_words_ && words==immediate_words_ &&
      caller==0x821fd6e8 && alignment==16;
  }
  static bool OwnsVectorStatePackets(uint32_t device,uint64_t dirty) {
    // DC20 copies up to six float4 records at10144 and a control word at10268
    // into packets. It does not modify either CPU source; callers clear bank32.
    return device_ && device==device_ && device<=UINT32_MAX-10271 && dirty &&
      !(dirty&~0xfc00000000000000ull);
  }
  static bool OwnsSpecialRenderPacket(uint32_t device,uint32_t caller,uint64_t dirty,uint32_t mode) {
    // Exact D938 call sites in the draw CPU prefixes. The helper only encodes
    // packets and returns dirty with bit0x100 cleared; mode is zero here.
    return device_ && device==device_ && mode==0 && (dirty&0x100) &&
      (caller==0x821fd52c || caller==0x821fe424);
  }
  static bool OwnsShaderLoadPackets(uint32_t device,uint32_t caller) {
    // EAB0 only builds program-load packets. ECB0's surrounding CPU metadata
    // updates remain essential and are deliberately not skipped.
    return device_ && device==device_ &&
      (caller==0x8213edc8 || caller==0x8213ef3c || caller==0x8213f04c);
  }
  static bool OwnsDerivedStatePackets(uint32_t device,uint32_t caller) {
    // D750 is mixed CPU/GPU code: use its extracted CPU-only variant, not a no-op.
    return device_ && device==device_ && caller==0x8213f368;
  }
  static bool OwnsShaderMicrocodePatch(uint32_t strides,uint32_t caller) {
    // E070 receives the device stride table, not the device itself. Its only
    // non-stack writes patch Xbox shader instructions at the destination in r4.
    return device_ && device_<=UINT32_MAX-12256 && strides==device_+12256 &&
      (caller==0x8213ec30 || caller==0x8213ea8c);
  }
  static bool OwnsShaderOutputPatch(uint32_t device,uint32_t caller) {
    // At E950's E800 call, nonvolatile r29 still holds E950's device argument.
    return device_ && device==device_ && caller==0x8213ea50;
  }
  static bool OwnsShaderUpload(uint32_t device,uint32_t caller) {
    return device_ && device==device_ && (caller==0x8213efb8 || caller==0x8213f070);
  }
  static bool OwnsMainStatePackets(uint32_t device,uint32_t caller) {
    return device_ && device==device_ && (caller==0x821fd4fc || caller==0x821fe3f4);
  }
 private:
  inline static thread_local uint32_t device_=0;
  inline static thread_local uint32_t immediate_words_=0;
  uint32_t previous_;
  uint32_t previous_words_;
};
}

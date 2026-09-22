#pragma once
#include <cstdint>

namespace edf::native {
// CPU effects of the indexed prefix, after native geometry has been accepted.
// Packet-only encoders and the guest index-buffer draw loop are not consumers
// here. The main-state callback still owns mixed shader/cache/derived effects.
template<class Memory,class MainState>
void CompleteNativeIndexedState(const Memory& memory,uint32_t device,MainState&& main_state) {
  // Snapshot every bank before invoking helpers, matching FE358. A helper may
  // publish a bank which was initially clean; that new work must survive.
  const auto vertex=memory.Wide(device);
  const auto vectors=memory.Wide(device+32);
  const auto render=memory.Wide(device+24);
  const auto main=memory.Wide(device+16);
  const auto pixel=memory.Wide(device+8);
  if(vertex) memory.StoreWide(device,0);
  if(pixel) memory.StoreWide(device+8,0);
  if(main) {
    if(main&(uint64_t(15)<<49)) main_state(main);
    // Remaining returned bits drive packet-only encoders. No CPU outputs from
    // those encoders are owed before clearing this originally dirty bank.
    memory.StoreWide(device+16,0);
  }
  if(render) {
    if(render&2) memory.StoreWide(device+11568,0xffffffffff000000ull);
    memory.StoreWide(device+24,0);
  }
  if(vectors) memory.StoreWide(device+32,0);
}
}

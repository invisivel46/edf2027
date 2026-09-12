#include "native_graphics/native_display_gamma.h"
#include <iostream>

int main() {
  using Gamma=edf::native::NativeDisplayGamma;
  std::array<uint8_t,1536> bytes{};
  auto store=[&](size_t channel,size_t entry,uint16_t value) {
    const auto at=(channel*256+entry)*2;
    bytes[at]=uint8_t(value>>8); bytes[at+1]=uint8_t(value);
  };
  int failures=0;
  auto check=[&](bool ok) { if(!ok) ++failures; };
  for(uint32_t c=0;c<3;++c) for(uint32_t i=0;i<256;++i)
    store(c,i,uint16_t(i*193+c*7001));
  const auto table=Gamma::Decode(bytes,Gamma::Mode::Table256);
  for(uint32_t i=0;i<256;++i) {
    // Independently reproduce 42050's packed RGB word from its three source
    // halfwords and compare each decoded component to native table ownership.
    const uint32_t r=uint16_t(i*193),g=uint16_t(i*193+7001),b=uint16_t(i*193+14002);
    const uint32_t packed=((r>>6)<<20)|((g>>6)<<10)|(b>>6);
    check(table.EvaluateCode(0,i)==((packed>>20)&1023));
    check(table.EvaluateCode(1,i)==((packed>>10)&1023));
    check(table.EvaluateCode(2,i)==(packed&1023));
  }
  for(uint32_t c=0;c<3;++c) for(uint32_t i=0;i<128;++i) {
    store(c,i*2,uint16_t(i*311+c*177));
    store(c,i*2+1,uint16_t(i*71+c*231));
  }
  const auto pwl=Gamma::Decode(bytes,Gamma::Mode::Piecewise128);
  for(uint32_t c=0;c<3;++c) for(uint32_t code=0;code<1024;++code) {
    const auto segment=code/8;
    const uint32_t packed=uint16_t(segment*311+c*177)|(uint32_t(uint16_t(segment*71+c*231))<<16);
    const auto base=packed&0xffc0,delta=(packed>>16)&0xffc0;
    check(pwl.EvaluateCode(c,code)==base+(code%8)*delta/8);
  }
  bytes.fill(0); // Decoder must not retain borrowed source memory.
  check(table.EvaluateCode(2,255)==(uint16_t(255*193+14002)>>6));
  for(auto mode:{Gamma::Mode::Table256,Gamma::Mode::Piecewise128}) {
    try { Gamma::Decode(std::span<const uint8_t>(bytes).first(1535),mode); ++failures; }
    catch(const std::invalid_argument&) {}
  }
  try { Gamma::Decode(bytes,static_cast<Gamma::Mode>(99)); ++failures; }
  catch(const std::invalid_argument&) {}
  try { table.EvaluateCode(3,0); ++failures; } catch(const std::out_of_range&) {}
  try { table.EvaluateCode(0,256); ++failures; } catch(const std::out_of_range&) {}
  try { pwl.EvaluateCode(0,1024); ++failures; } catch(const std::out_of_range&) {}
  std::cout << "Native display gamma failures: " << failures << '\n';
  return failures?1:0;
}

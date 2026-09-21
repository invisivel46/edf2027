#include "native_guest_vertex_stream.h"
#include <bit>
#include <cmath>
#include <stdexcept>

namespace edf::native {
std::vector<uint8_t> ConvertGuestPositionTriangles(std::span<const uint8_t> guest) {
  std::vector<uint8_t> host;
  ConvertGuestPositionTrianglesInto(guest,host);
  return host;
}
void ConvertGuestPositionTrianglesInto(std::span<const uint8_t> guest,std::vector<uint8_t>& host) {
  if(guest.empty() || guest.size()%24 || guest.size()>16384*8)
    throw std::runtime_error("invalid position triangle span");
  // Validated before anything is written, so a rejected span cannot leave a
  // partially converted buffer for the next draw to pick up.
  for(size_t at=0;at<guest.size();at+=4) {
    const uint32_t word=(uint32_t(guest[at])<<24)|(uint32_t(guest[at+1])<<16)|
                        (uint32_t(guest[at+2])<<8)|guest[at+3];
    if(!std::isfinite(std::bit_cast<float>(word)))
      throw std::runtime_error("nonfinite position triangle vertex");
  }
  host.resize(guest.size());
  for(size_t at=0;at<guest.size();at+=4)
    for(size_t byte=0;byte<4;++byte) host[at+byte]=guest[at+3-byte];
}

std::vector<uint8_t> ConvertGuestQuads(std::span<const uint8_t> guest, bool strip) {
  std::vector<uint8_t> host;
  ConvertGuestQuadsInto(guest,strip,host);
  return host;
}
void ConvertGuestQuadsInto(std::span<const uint8_t> guest,bool strip,std::vector<uint8_t>& host) {
  // Four 16-byte vertices per quad; bounded before arithmetic or allocation.
  if(guest.empty() || guest.size()>4096*64 ||
     (strip ? guest.size()<48 || guest.size()%16 : guest.size()%64!=0))
    throw std::runtime_error("invalid immediate quad vertex span");
  const size_t bytes=strip?guest.size():guest.size()/64*96;
  host.resize(bytes);
  constexpr unsigned order[]{0,1,2,0,2,3};
  for(size_t vertex=0;vertex<bytes/16;++vertex) {
    const size_t source=strip?vertex:vertex/6*4+order[vertex%6];
    for(size_t word=0;word<4;++word)
      for(size_t byte=0;byte<4;++byte)
        host[vertex*16+word*4+byte]=guest[source*16+word*4+3-byte];
  }
}
}  // namespace edf::native

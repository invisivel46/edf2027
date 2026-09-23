// EDF2027 - fonts built into the executable for the F1 menu (third_party/fonts, embedded
// by tools/embed-fonts.cmake). The data lives for the whole process; ImGui must not free
// it (ImFontConfig::FontDataOwnedByAtlas = false).
#pragma once

#include <cstddef>

namespace edf::ui {

struct EmbeddedFont {
  const unsigned char* data;
  size_t size;
};
EmbeddedFont InterRegular();      // Inter 4.001, Latin subset (OFL 1.1)
EmbeddedFont InterSemiBold();     // Inter 4.001 SemiBold, Latin subset (OFL 1.1)
EmbeddedFont FontAwesomeSolid();  // Font Awesome Free 6.7.2 solid (OFL 1.1)

}  // namespace edf::ui

#include "version.h"

#include "edf_version_info.h"  // generated into the build tree (tools/write-version.cmake)

namespace edf::version {
const char* Version() { return EDF2027_VERSION; }
const char* GitHash() { return EDF2027_GIT_HASH; }
bool Dirty() { return EDF2027_GIT_DIRTY != 0; }
const char* Full() { return EDF2027_VERSION_FULL; }
}  // namespace edf::version

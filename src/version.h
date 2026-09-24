// EDF2027 - the build's identity: release version, git commit and dirty flag.
// The release version is EDF2027_VERSION in CMakeLists.txt; the commit is read at
// build time (tools/write-version.cmake), so it is never stale.
#pragma once

namespace edf::version {
const char* Version();   // "0.3.0-beta"
const char* GitHash();   // "abc1234", or "nogit" when built outside a git checkout
bool Dirty();            // tracked files differed from the commit when this was built
const char* Full();      // "0.3.0-beta+abc1234", with ".dirty" appended when Dirty()
}  // namespace edf::version

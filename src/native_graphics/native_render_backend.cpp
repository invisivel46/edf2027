#include "native_render_backend.h"
#include <algorithm>
#include <map>
#include <stdexcept>

namespace edf::native {
namespace {
// Ordered so the reported list and any A/B run are reproducible between runs.
std::map<std::string,std::unique_ptr<NativeRenderBackend>(*)()>& Registry() {
  static std::map<std::string,std::unique_ptr<NativeRenderBackend>(*)()> registry;
  return registry;
}
std::vector<std::string>& Names() {
  static std::vector<std::string> names;
  return names;
}
}  // namespace

void RegisterNativeRenderBackend(std::string name,
                                 std::unique_ptr<NativeRenderBackend>(*factory)()) {
  if(name.empty() || !factory) throw std::runtime_error("invalid native render backend registration");
  if(!Registry().emplace(name,factory).second)
    throw std::runtime_error("duplicate native render backend: "+name);
  Names().push_back(std::move(name));
  std::sort(Names().begin(),Names().end());
}

const std::vector<std::string>& NativeRenderBackendNames() { return Names(); }

std::unique_ptr<NativeRenderBackend> CreateNativeRenderBackend(std::string_view name) {
  const auto found=Registry().find(std::string(name));
  if(found==Registry().end()) {
    // Fail loudly. Silently falling back to a different backend than the one
    // asked for would make an A/B comparison quietly meaningless.
    std::string known;
    for(const auto& candidate:Names()) known+=(known.empty()?"":", ")+candidate;
    throw std::runtime_error("unknown native render backend '"+std::string(name)+
                             "'; registered: "+(known.empty()?"none":known));
  }
  return found->second();
}
}  // namespace edf::native

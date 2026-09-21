#include <Windows.h>
#include <delayimp.h>
#include <rex/cvar.h>
#include <cstring>
#include <stdexcept>

REXCVAR_DECLARE(std::string,edf_native_scene_backend);
namespace {
FARPROC WINAPI CheckFallbackImport(unsigned notification,PDelayLoadInfo import) {
  if(notification==dliNotePreGetProcAddress && !_stricmp(import->szDll,"d3d11.dll") &&
     REXCVAR_GET(edf_native_scene_backend).starts_with("d3d12"))
    throw std::runtime_error("D3D12 runtime attempted to call a D3D11 fallback export");
  return nullptr;
}
}
extern "C" const PfnDliHook __pfnDliNotifyHook2=CheckFallbackImport;

#include "hooks.h"

#include "../MinHook.h"
#include "log.h"

namespace sf {

bool HooksInit() {
  const MH_STATUS status = MH_Initialize();  // Classic Rebirth may have initialised it already
  if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
    Log("MinHook: initialisation failed (%s)", MH_StatusToString(status));
    return false;
  }
  return true;
}

bool Hook(const char* name, uintptr_t target, void* detour, void** original) {
  void* address = reinterpret_cast<void*>(target);
  MH_STATUS status = MH_CreateHook(address, detour, original);
  if (status == MH_OK) status = MH_EnableHook(address);
  if (status != MH_OK) {
    Log("hook %s @ %08X: ERROR %s", name, static_cast<unsigned>(target), MH_StatusToString(status));
    return false;
  }
  Log("hook %s @ %08X: on", name, static_cast<unsigned>(target));
  return true;
}

}  // namespace sf

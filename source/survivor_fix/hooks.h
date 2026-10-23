#pragma once

#include <cstdint>

namespace sf {

bool HooksInit();

// Hooks the function at address `target` and enables it.
// `original` receives the trampoline to call the original function.
bool Hook(const char* name, uintptr_t target, void* detour, void** original);

}  // namespace sf

#pragma once
#include <windows.h>

namespace MciHook
{
    // Installs MCI hooks (winmm.dll mciSendString/mciSendCommand) for EXE + game-dir modules.
    // Safe to call multiple times.
    bool Install();
}

#pragma once
#include <windows.h>

namespace AviFfmpegHook
{
    // Call once from your InitThread (NOT directly in DllMain).
    bool Install();
}

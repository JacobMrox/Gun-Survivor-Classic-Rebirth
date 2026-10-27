#pragma once
#include <windows.h>
#include "globals.h"

namespace AviFfmpegHook
{
    // Call once from your InitThread (NOT directly in DllMain).
    bool Install();

    // DirectShow owns the game's movie lifecycle and audio. These functions replace
    // only its video surface with the FFmpeg texture drawn by IDirect3DDevice8.
    bool StartForDirectShow(const wchar_t* filename);
    void StopForDirectShow();
    bool StartForGameOverlay(const char* filename);
    bool StartForGameOverlay(const wchar_t* filename);
    void StopForGameOverlay();
    bool IsGameOverlayActive();
}

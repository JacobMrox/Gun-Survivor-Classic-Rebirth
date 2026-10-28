#pragma once
#include <windows.h>
#include "globals.h"

namespace AviFfmpegHook
{
    // Call once from your InitThread (NOT directly in DllMain).
    bool Install();

    // DirectShow owns the game's movie lifecycle and audio. These functions replace
    // only its video surface with the FFmpeg texture drawn by IDirect3DDevice8.
    // A non-zero generation identifies the exact DirectShow movie that owns
    // the shared D3D/FFmpeg decoder. Old graph cleanup must supply its own
    // generation so it cannot stop a newer movie.
    unsigned long StartForDirectShow(const wchar_t* filename);
    void StopForDirectShow(unsigned long generation = 0);
    bool IsDirectShowActive(unsigned long generation);
    unsigned long StartForGameOverlay(const char* filename);
    unsigned long StartForGameOverlay(const wchar_t* filename);
    void StopForGameOverlay(unsigned long generation = 0);
    bool IsGameOverlayActive(unsigned long generation = 0);
}

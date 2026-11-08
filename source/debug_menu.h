#pragma once
struct IDirect3DDevice8;
#ifdef __cplusplus
extern "C" {
#endif
// RE2-style debug menu overlay. Called from dllmain EndScene each frame.
void DebugMenu_Draw(IDirect3DDevice8* dev);
#ifdef __cplusplus
}
#endif

// alpha_fix.h
#pragma once
#include <d3d8.h>

enum AlphaFixTag { AFT_None = 0, AFT_Shadow, AFT_Blood1, AFT_Blood2 };

void AlphaFix_OnFileOpenA(const char* path);
void AlphaFix_OnFileOpenW(const wchar_t* path);

void AlphaFix_OnTextureCreated(IDirect3DDevice8* dev, IDirect3DTexture8* tex);
void AlphaFix_ApplyPNGToTexture(IDirect3DTexture8* tex, AlphaFixTag tag);

// premult blend state helpers
void ApplyAlphaFixBlendState(IDirect3DDevice8* dev, DWORD& oldAlpha, DWORD& oldSrc, DWORD& oldDst, DWORD& oldATest);
void RestoreAlphaFixBlendState(IDirect3DDevice8* dev, DWORD oldAlpha, DWORD oldSrc, DWORD oldDst, DWORD oldATest);

AlphaFixTag AlphaFix_PeekNextTag();
AlphaFixTag AlphaFix_ConsumeNextTag();

/**
* Copyright (C) 2020 Elisha Riedlinger, Jacob Qarooni
*
* This software is  provided 'as-is', without any express  or implied  warranty. In no event will the
* authors be held liable for any damages arising from the use of this software.
* Permission  is granted  to anyone  to use  this software  for  any  purpose,  including  commercial
* applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*   1. The origin of this software must not be misrepresented; you must not claim that you  wrote the
*      original  software. If you use this  software  in a product, an  acknowledgment in the product
*      documentation would be appreciated but is not required.
*   2. Altered source versions must  be plainly  marked as such, and  must not be  misrepresented  as
*      being the original software.
*   3. This notice may not be removed or altered from any source distribution.
*/
#define DIRECT3D_VERSION 0x0800 // Force headers to prefer D3D8 definitions
#include <cstdio>
#include <windows.h>
#include <string>
// Use the project's D3D9 compatibility header instead of including <d3d9.h>
#include "d3d8types.hpp"
#pragma comment(lib, "d3d9.lib") // link d3d9.lib

#include "iathook.h"
#include "helpers.h"
#include "cd_bypass.h"
#include "file_events.h"
#include "aim_shared.h"
#include "debug.h"
#include "survivor_pc.h"
#include "survivor_fix/survivor_fix.h"
#include "survivor_fix/features/features.h"
//#include "alpha_fix.h"
#include "DirectShowHook.h"
#include "ConfigWindow.h"
#include "resource.h"
#include "AviFfmpegHook.h" // Access to g_Video
#include "globals.h"
#include <string.h>
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cmath>
#include <excpt.h>
#include <list>
#include <string.h>
#include <utility>
#include <vector>
#include "d3d8.h"  // D3DCAPS8 comes from d3d8types.hpp
#include "d3dx8core.h"
#include "d3dx8math.h"
#include "d3dx8tex.h"
#include "IDirect3D8.h"
#include "IDirect3DCubeTexture8.h"
#include "IDirect3DDevice8.h"
#include "IDirect3DTexture8.h"
#include "IDirect3DVolumeTexture8.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/packet.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"

#pragma once
#pragma comment(lib, "d3d9.lib")
#pragma comment (lib, "d3dx8.lib")
#pragma comment (lib, "legacy_stdio_definitions.lib")
#pragma comment(lib, "winmm.lib") // needed for timeBeginPeriod()/timeEndPeriod()

Direct3D8EnableMaximizedWindowedModeShimProc m_pDirect3D8EnableMaximizedWindowedModeShim;
ValidatePixelShaderProc m_pValidatePixelShader;
ValidateVertexShaderProc m_pValidateVertexShader;
DebugSetMuteProc m_pDebugSetMute;
Direct3DCreate8Proc m_pDirect3DCreate8;

HMODULE g_hWrapperModule = NULL;
char gConfigPath[MAX_PATH] = {};//config.ini next to d3d8.dll (Survivor Fix reads it again after the config window)

HMODULE d3d8dll = NULL;

static void RestoreEmbeddedMiscIfMissing()
{
    char modulePath[MAX_PATH]{};
    if (!GetModuleFileNameA(g_hWrapperModule, modulePath, MAX_PATH)) return;
    char* slash = strrchr(modulePath, '\\');
    if (!slash) return;
    strcpy_s(slash + 1, MAX_PATH - static_cast<size_t>(slash + 1 - modulePath), "FIXRES");
    CreateDirectoryA(modulePath, nullptr);
    strcat_s(modulePath, "\\MISC.BMP");
    if (GetFileAttributesA(modulePath) != INVALID_FILE_ATTRIBUTES) return;

    HRSRC info = FindResourceA(g_hWrapperModule, MAKEINTRESOURCEA(IDR_MISC_BMP), RT_RCDATA);
    HGLOBAL resource = info ? LoadResource(g_hWrapperModule, info) : nullptr;
    const void* bytes = resource ? LockResource(resource) : nullptr;
    const DWORD size = info ? SizeofResource(g_hWrapperModule, info) : 0;
    if (!bytes || !size) return;
    HANDLE file = CreateFileA(modulePath, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, bytes, size, &written, nullptr);
    CloseHandle(file);
    if (written != size) DeleteFileA(modulePath);
}

// Use the D3D9 texture type from d3d9.h
IDirect3DTexture9* g_MovieTex = nullptr;

bool bForceWindowedMode;
bool bDirect3D8DisableMaximizedWindowedModeShim;
bool bUsePrimaryMonitor;
bool bCenterWindow;
bool bBorderlessFullscreen;
bool bAlwaysOnTop;
bool bDoNotNotifyOnTaskSwitch;
bool bDisplayFPSCounter;
bool bShowWindowBorder;//Survivor Exclusive
bool bLockCursorToWindow;//Survivor Exclusive
int ForceWidth;//Survivor Exclusive
int ForceHeight;//Survivor Exclusive
//bool bAlphaFix;//Survivor Exclusive; whether to use PNG shadows reminiscent of PS1 shadows or not
bool bSkipMovies;//Survivor Exclusive; skip movies
bool gEnableLogging;//Survivor Exclusive; whether to log to file or not
bool bLaserTrail = false;//Optional fading dot trail behind the overlay crosshair
COLORREF gLaserColor = RGB(255, 35, 20);
float fFPSLimit;
int nFullScreenRefreshRateInHz;

int ReadIniIntWithFallback(const char* path, const char* section, const char* key,
                           const char* oldSection, const char* oldKey, int fallback)
{
    char probe[2] = {};
    if (GetPrivateProfileStringA(section, key, nullptr, probe, sizeof probe, path) != 0)
        return GetPrivateProfileIntA(section, key, fallback, path);
    return GetPrivateProfileIntA(oldSection, oldKey, fallback, path);
}

char WinDir[MAX_PATH + 1];

    // List of registered window classes and procedures
    // WORD classAtom, ULONG_PTR WndProcPtr
    std::vector<std::pair<WORD, ULONG_PTR>> WndProcList;

    // Hook the target module's IAT to redirect Direct3D8Create to our function
    void HookModule(HMODULE hmod);

    static bool  gDrawOverlayCrosshair = true;

    extern float gAimSensX, gAimSensY; // from dinputhook.cpp
    extern bool  gUseDIMouseForCrosshair; // from dinputhook.cpp
    extern "C" void InstallDInputHook();// from dinputhook.cpp
    std::string sCrosshairPath = "FIXRES/crosshair.png";//ConfinWindow.cpp; path to crosshair texture, can be set in config.ini or via config window. 
    extern float fCrosshairScale = 1.0f;
    extern "C" IDirect3D8* WINAPI Direct3D8_Create(UINT SDKVersion);    // Tells the linker that this function comes from d3d8to9.cpp

    //bool gUseRelativeDIForCrosshair = false;

    LPDIRECT3DTEXTURE8 gCrossTex = nullptr;
    bool gCrossLoaded = false;
    LPDIRECT3DTEXTURE8 gHotkeyFontTex = nullptr;
    //bool gStage0IsAlphaFix = false;

    // Forward declarations (prevents "undefined" if order changes)
    void EnsureCrosshairTexture(LPDIRECT3DDEVICE8 dev);
    void DrawTexturedQuad(LPDIRECT3DDEVICE8 dev, LPDIRECT3DTEXTURE8 tex, float x, float y, float w, float h);

    struct TrailPoint { float x, y; };
    std::vector<TrailPoint> gLaserTrailPoints;

    void DrawLaserTrail(LPDIRECT3DDEVICE8 dev, float x, float y)
    {
        gLaserTrailPoints.push_back({x, y});
        if (gLaserTrailPoints.size() > 18) gLaserTrailPoints.erase(gLaserTrailPoints.begin());
        if (!bLaserTrail || gLaserTrailPoints.size() < 2) return;

        DWORD state = 0;
        if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &state))) return;
        dev->SetTexture(0, nullptr);
        dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        dev->SetRenderState(D3DRS_ZENABLE, FALSE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

        struct DotVertex { float x, y, z, rhw; DWORD color; };
        for (size_t i = 0; i + 1 < gLaserTrailPoints.size(); ++i) {
            const auto& a = gLaserTrailPoints[i];
            const auto& b = gLaserTrailPoints[i + 1];
            float dx = b.x - a.x, dy = b.y - a.y;
            const float length = sqrtf(dx * dx + dy * dy);
            if (length < 0.01f) continue;
            dx /= length; dy /= length;
            const float half = 0.7f + 1.8f * static_cast<float>(i + 1) / gLaserTrailPoints.size();
            const float nx = -dy * half, ny = dx * half;
            const BYTE alpha = static_cast<BYTE>(20 + 180 * (i + 1) / gLaserTrailPoints.size());
            const DWORD color = D3DCOLOR_ARGB(alpha, GetRValue(gLaserColor), GetGValue(gLaserColor), GetBValue(gLaserColor));
            DotVertex v[4] = {{a.x + nx, a.y + ny, 0, 1, color}, {a.x - nx, a.y - ny, 0, 1, color},
                              {b.x + nx, b.y + ny, 0, 1, color}, {b.x - nx, b.y - ny, 0, 1, color}};
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(DotVertex));
        }
        dev->ApplyStateBlock(state);
        dev->DeleteStateBlock(state);
    }

    void DrawLaserImpactDot(LPDIRECT3DDEVICE8 dev, float x, float y, float xCompensation)
    {
        DWORD state = 0;
        if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &state))) return;
        dev->SetTexture(0, nullptr);
        dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        dev->SetRenderState(D3DRS_ZENABLE, TRUE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        // A clear depth pixel is 1.0 and fails GREATER. Any rendered surface is nearer and
        // accepts this z=1 probe, making the glow appear only at the first visible hit.
        dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_GREATER);
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        struct GlowVertex { float x, y, z, rhw; DWORD color; };
        const BYTE r = GetRValue(gLaserColor), g = GetGValue(gLaserColor), b = GetBValue(gLaserColor);
        constexpr int segments = 32;
        auto drawDisc = [&](float radius, float centreZ, float rimZ, BYTE centreAlpha) {
            GlowVertex vertices[segments + 2]{};
            vertices[0] = {x, y, centreZ, 1.0f, D3DCOLOR_ARGB(centreAlpha, r, g, b)};
            for (int i = 0; i <= segments; ++i) {
                const float angle = 6.283185307f * static_cast<float>(i) / segments;
                vertices[i + 1] = {x + cosf(angle) * radius * xCompensation, y + sinf(angle) * radius,
                                   rimZ, 1.0f, D3DCOLOR_ARGB(0, r, g, b)};
            }
            dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, segments, vertices, sizeof(GlowVertex));
        };
        // The glow's probe depth now changes continuously from centre to rim.
        // Against a nearer depth-buffer value progressively more of the disc
        // passes, producing a smoothly larger circular impact instead of three
        // discrete square states. Clear depth (1.0) rejects the entire dot.
        drawDisc(20.0f, 0.9999f, 0.55f, 150);
        drawDisc(3.5f, 0.9999f, 0.9999f, 245);
        dev->ApplyStateBlock(state);
        dev->DeleteStateBlock(state);
    }


    std::string lastLoadedPath = "";

    int EmbeddedCrosshairId(const std::string& path)
    {
        if (path.find("crosshair_pc.png") != std::string::npos) return IDR_CROSSHAIR_PC;
        if (path.find("crosshair_old.png") != std::string::npos) return IDR_CROSSHAIR_OLD;
        if (path.find("crosshair_psx.png") != std::string::npos) return IDR_CROSSHAIR_PSX;
        if (path.find("crosshair_uc_big.png") != std::string::npos) return IDR_CROSSHAIR_UC_BIG;
        return 0;
    }

    HRESULT LoadEmbeddedTexture(LPDIRECT3DDEVICE8 dev, int id, LPDIRECT3DTEXTURE8* texture)
    {
        HRSRC info = FindResourceA(g_hWrapperModule, MAKEINTRESOURCEA(id), RT_RCDATA);
        HGLOBAL data = info ? LoadResource(g_hWrapperModule, info) : nullptr;
        const void* bytes = data ? LockResource(data) : nullptr;
        const DWORD size = info ? SizeofResource(g_hWrapperModule, info) : 0;
        return bytes && size ? D3DXCreateTextureFromFileInMemory(dev, bytes, size, texture) : E_FAIL;
    }

    void EnsureCrosshairTexture(LPDIRECT3DDEVICE8 dev)
    {
        // Reload if not loaded OR if the path has changed in the config window
        if (gCrossLoaded && sCrosshairPath == lastLoadedPath) return;

        if (gCrossTex) {
            gCrossTex->Release();
            gCrossTex = nullptr;
        }

        HRESULT hr = D3DXCreateTextureFromFileA(dev, sCrosshairPath.c_str(), &gCrossTex);
        if (FAILED(hr)) {
            const int embedded = EmbeddedCrosshairId(sCrosshairPath);
            if (embedded) hr = LoadEmbeddedTexture(dev, embedded, &gCrossTex);
        }
        if (FAILED(hr)) {
            hr = D3DXCreateTextureFromFileA(dev, "FIXRES/crosshair.png", &gCrossTex);
            if (FAILED(hr)) hr = LoadEmbeddedTexture(dev, IDR_CROSSHAIR_PSX, &gCrossTex);
        }
        if (SUCCEEDED(hr)) {
            lastLoadedPath = sCrosshairPath;
            gCrossLoaded = true;
        }
        else {
            gCrossLoaded = false;
        }
    }

    struct TLVERTEX8
    {
        float x, y, z, rhw;
        float u, v;
    };
    #define FVF_TLVERTEX8 (D3DFVF_XYZRHW | D3DFVF_TEX1)

    void DrawTexturedQuad(LPDIRECT3DDEVICE8 dev, LPDIRECT3DTEXTURE8 tex, float x, float y, float w, float h)
    {
        TLVERTEX8 v[4] =
        {
            { x,     y,     0.0f, 1.0f, 0.0f, 0.0f },
            { x + w,   y,     0.0f, 1.0f, 1.0f, 0.0f },
            { x,     y + h,   0.0f, 1.0f, 0.0f, 1.0f },
            { x + w,   y + h,   0.0f, 1.0f, 1.0f, 1.0f },
        };

        // Save a few states (minimal; good enough for HUD)
        DWORD oldZ = 0, oldAlpha = 0, oldSrc = 0, oldDst = 0;
        dev->GetRenderState(D3DRS_ZENABLE, &oldZ);
        dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlpha);
        dev->GetRenderState(D3DRS_SRCBLEND, &oldSrc);
        dev->GetRenderState(D3DRS_DESTBLEND, &oldDst);

        dev->SetTexture(0, tex);
        dev->SetVertexShader(FVF_TLVERTEX8);

        dev->SetRenderState(D3DRS_ZENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);

        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(TLVERTEX8));

        // Restore states
        dev->SetRenderState(D3DRS_ZENABLE, oldZ);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlpha);
        dev->SetRenderState(D3DRS_SRCBLEND, oldSrc);
        dev->SetRenderState(D3DRS_DESTBLEND, oldDst);
    }

    // The game normally renders a 4:3 backbuffer and the wrapper presents it at the selected
    // output aspect. Convert an X size in backbuffer pixels so sprites and glyphs stay square
    // after that final stretch. This also follows arbitrary future window/resolution sizes.
    float BackBufferXCompensation(UINT backWidth, UINT backHeight)
    {
        if (!g_hFocusWindow || !backWidth || !backHeight) return 1.0f;
        RECT client = {};
        if (!GetClientRect(g_hFocusWindow, &client)) return 1.0f;
        const int clientWidth = client.right - client.left;
        const int clientHeight = client.bottom - client.top;
        if (clientWidth <= 0 || clientHeight <= 0) return 1.0f;
        const float xStretch = (static_cast<float>(clientWidth) / backWidth) /
                               (static_cast<float>(clientHeight) / backHeight);
        return xStretch > 0.01f ? 1.0f / xStretch : 1.0f;
    }

    struct FontVertex8
    {
        float x, y, z, rhw;
        DWORD color;
        float u, v;
    };
    #define FVF_FONTVERTEX8 (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

    int NativeFontGlyph(char ch)
    {
        if (ch >= '0' && ch <= '9') return 12 + (ch - '0');
        if (ch >= 'A' && ch <= 'Z') return 29 + (ch - 'A');
        if (ch >= 'a' && ch <= 'z') return 61 + (ch - 'a');
        switch (ch)
        {
        case ':': return 22;
        case '.': return 23;
        case ',': return 24;
        case '"': return 25;
        case '!': return 26;
        case '?': return 27;
        case '[': return 55;
        case '/': return 56;
        case ']': return 57;
        case '\'': return 58;
        case '-': return 59;
        default: return -1;
        }
    }

    void EnsureHotkeyFontTexture(LPDIRECT3DDEVICE8 dev)
    {
        if (gHotkeyFontTex || !dev) return;
        char fontPath[MAX_PATH] = "Resource\\FONT000.bmp";
        HRESULT hr = D3DXCreateTextureFromFileExA(
            dev, fontPath, D3DX_DEFAULT, D3DX_DEFAULT, 1, 0, D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED, D3DX_FILTER_POINT, D3DX_FILTER_POINT,
            D3DCOLOR_ARGB(255, 0, 0, 0), nullptr, nullptr, &gHotkeyFontTex);
        if (SUCCEEDED(hr)) return;

        if (GetModuleFileNameA(g_hWrapperModule, fontPath, MAX_PATH))
        {
            char* slash = strrchr(fontPath, '\\');
            if (slash)
            {
                strcpy_s(slash + 1,
                    MAX_PATH - static_cast<size_t>(slash + 1 - fontPath),
                    "Resource\\FONT000.bmp");
                D3DXCreateTextureFromFileExA(
                    dev, fontPath, D3DX_DEFAULT, D3DX_DEFAULT, 1, 0, D3DFMT_A8R8G8B8,
                    D3DPOOL_MANAGED, D3DX_FILTER_POINT, D3DX_FILTER_POINT,
                    D3DCOLOR_ARGB(255, 0, 0, 0), nullptr, nullptr, &gHotkeyFontTex);
            }
        }
    }

    void DrawNativeFontGlyph(LPDIRECT3DDEVICE8 dev, int glyph, float x, float y,
                             float width, float height, UINT textureWidth, UINT textureHeight, DWORD color)
    {
        if (glyph < 0) return;
        constexpr float cell = 14.0f;
        constexpr int columns = 18;
        const float left = static_cast<float>((glyph % columns) * 14);
        const float top = static_cast<float>((glyph / columns) * 14);
        // Half-texel inset keeps neighbouring atlas glyphs out when the notice is scaled.
        const float u0 = (left + 0.5f) / textureWidth;
        const float v0 = (top + 0.5f) / textureHeight;
        const float u1 = (left + cell - 0.5f) / textureWidth;
        const float v1 = (top + cell - 0.5f) / textureHeight;
        FontVertex8 vertices[4] =
        {
            {x,        y,        0.0f, 1.0f, color, u0, v0},
            {x + width, y,          0.0f, 1.0f, color, u1, v0},
            {x,         y + height, 0.0f, 1.0f, color, u0, v1},
            {x + width, y + height, 0.0f, 1.0f, color, u1, v1},
        };
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(FontVertex8));
    }

    void DrawHotkeyNotification(LPDIRECT3DDEVICE8 dev)
    {
        char text[64] = {};
        float alpha = 0.0f;
        if (!sf::features::QuickSaveGetNotification(text, sizeof text, &alpha) || alpha <= 0.0f)
            return;
        EnsureHotkeyFontTexture(dev);
        if (!gHotkeyFontTex) return;

        IDirect3DSurface8* backBuffer = nullptr;
        D3DSURFACE_DESC8 backDesc = {};
        D3DSURFACE_DESC8 fontDesc = {};
        if (FAILED(dev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)) || !backBuffer)
            return;
        const HRESULT backResult = backBuffer->GetDesc(&backDesc);
        backBuffer->Release();
        if (FAILED(backResult) || FAILED(gHotkeyFontTex->GetLevelDesc(0, &fontDesc))) return;

        const float scale = std::clamp(backDesc.Height / 720.0f, 0.9f, 1.8f);
        const float xCompensation = BackBufferXCompensation(backDesc.Width, backDesc.Height);
        const float glyphHeight = 14.0f * scale;
        const float glyphWidth = glyphHeight * xCompensation;
        const float marginY = 18.0f * scale;
        const float marginX = marginY * xCompensation;
        const size_t length = strlen(text);
        float x = static_cast<float>(backDesc.Width) - marginX - glyphWidth * static_cast<float>(length);
        const float y = marginY;
        if (x < marginX) x = marginX;

        DWORD state = 0;
        if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &state))) return;
        dev->SetTexture(0, gHotkeyFontTex);
        dev->SetVertexShader(FVF_FONTVERTEX8);
        dev->SetRenderState(D3DRS_ZENABLE, FALSE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(D3DTSS_MINFILTER), D3DTEXF_POINT);
        dev->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(D3DTSS_MAGFILTER), D3DTEXF_POINT);
        dev->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(D3DTSS_ADDRESSU), D3DTADDRESS_CLAMP);
        dev->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(D3DTSS_ADDRESSV), D3DTADDRESS_CLAMP);

        const BYTE a = static_cast<BYTE>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        const DWORD shadow = D3DCOLOR_ARGB(static_cast<BYTE>(a * 0.75f), 0, 0, 0);
        const DWORD white = D3DCOLOR_ARGB(a, 255, 255, 255);
        const float shadowOffsetY = (std::max)(1.0f, scale);
        const float shadowOffsetX = shadowOffsetY * xCompensation;
        for (size_t i = 0; i < length; ++i)
        {
            const int glyph = NativeFontGlyph(text[i]);
            if (glyph >= 0)
                DrawNativeFontGlyph(dev, glyph, x + glyphWidth * i + shadowOffsetX,
                    y + shadowOffsetY, glyphWidth, glyphHeight,
                    fontDesc.Width, fontDesc.Height, shadow);
        }
        for (size_t i = 0; i < length; ++i)
        {
            const int glyph = NativeFontGlyph(text[i]);
            if (glyph >= 0)
                DrawNativeFontGlyph(dev, glyph, x + glyphWidth * i, y, glyphWidth, glyphHeight,
                    fontDesc.Width, fontDesc.Height, white);
        }
        dev->ApplyStateBlock(state);
        dev->DeleteStateBlock(state);
    }

// ---------------------------------------------------------
// Elisha Riedlinger Frame Limiter Class
// ---------------------------------------------------------

class FrameLimiter
{
private:
    static inline double TIME_Frequency = 0.0;
    static inline double TIME_Ticks = 0.0;
    static inline double TIME_Frametime = 0.0;

public:
    static inline ID3DXFont* pFPSFont = nullptr;
    static inline ID3DXFont* pTimeFont = nullptr;

public:
	// Elisha Riedlinger Frame Limiter Methods
    enum FPSLimitMode { FPS_NONE, FPS_REALTIME, FPS_ACCURATE };
    static void Init(FPSLimitMode mode)
    {
        LARGE_INTEGER frequency;

        QueryPerformanceFrequency(&frequency);
        static constexpr auto TICKS_PER_FRAME = 1;
        auto TICKS_PER_SECOND = (TICKS_PER_FRAME * fFPSLimit);
        if (mode == FPS_ACCURATE)
        {
            TIME_Frametime = 1000.0 / (double)fFPSLimit;
            TIME_Frequency = (double)frequency.QuadPart / 1000.0; // ticks are milliseconds
        }
        else // FPS_REALTIME
        {
            TIME_Frequency = (double)frequency.QuadPart / (double)TICKS_PER_SECOND; // ticks are 1/n frames (n = fFPSLimit)
        }
        Ticks();
    }
    static DWORD Sync_RT()
    {
        DWORD lastTicks, currentTicks;
        LARGE_INTEGER counter;

        QueryPerformanceCounter(&counter);
        lastTicks = (DWORD)TIME_Ticks;
        TIME_Ticks = (double)counter.QuadPart / TIME_Frequency;
        currentTicks = (DWORD)TIME_Ticks;

        return (currentTicks > lastTicks) ? currentTicks - lastTicks : 0;
    }
    static DWORD Sync_SLP()
    {
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        double millis_current = (double)counter.QuadPart / TIME_Frequency;
        double millis_delta = millis_current - TIME_Ticks;
        if (TIME_Frametime <= millis_delta)
        {
            TIME_Ticks = millis_current;
            return 1;
        }
        else if (TIME_Frametime - millis_delta > 2.0) // > 2ms
            Sleep(1); // Sleep for ~1ms
        else
            Sleep(0); // yield thread's time-slice (does not actually sleep)

        return 0;
    }
    static void ShowFPS(LPDIRECT3DDEVICE8 device)
    {
        static std::list<int> m_times;

        //https://github.com/microsoft/VCSamples/blob/master/VC2012Samples/Windows%208%20samples/C%2B%2B/Windows%208%20app%20samples/Direct2D%20geometry%20realization%20sample%20(Windows%208)/C%2B%2B/FPSCounter.cpp#L279
        LARGE_INTEGER frequency;
        LARGE_INTEGER time;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&time);

        if (m_times.size() == 50)
            m_times.pop_front();
        m_times.push_back(static_cast<int>(time.QuadPart));

        uint32_t fps = 0;
        if (m_times.size() >= 2)
            fps = static_cast<uint32_t>(0.5f + (static_cast<float>(m_times.size() - 1) * static_cast<float>(frequency.QuadPart)) / static_cast<float>(m_times.back() - m_times.front()));

        static int space = 0;
        if (!pFPSFont || !pTimeFont)
        {
            D3DDEVICE_CREATION_PARAMETERS cparams;
            RECT rect;
            device->GetCreationParameters(&cparams);
            GetClientRect(cparams.hFocusWindow, &rect);
            
            LOGFONT fps_font = { rect.bottom / 20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Arial" };
            LOGFONT time_font = { rect.bottom / 35, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Arial" };
            space = rect.bottom / 20 + 5;
            
            if (D3DXCreateFontIndirect(device, &fps_font, &pFPSFont) != D3D_OK)
                return;

            if (D3DXCreateFontIndirect(device, &time_font, &pTimeFont) != D3D_OK)
                return;
        }
        else
        {
            auto DrawTextOutline = [](ID3DXFont* pFont, FLOAT X, FLOAT Y, D3DXCOLOR dColor, CONST PCHAR cString, ...)
            {
                const D3DXCOLOR BLACK(D3DCOLOR_XRGB(0, 0, 0));
                CHAR cBuffer[101] = "";

                va_list oArgs;
                va_start(oArgs, cString);
                _vsnprintf((cBuffer + strlen(cBuffer)), (sizeof(cBuffer) - strlen(cBuffer)), cString, oArgs);
                va_end(oArgs);

                RECT Rect[5] =
                {
                    { X - 1, Y, X + 500.0f, Y + 50.0f },
                    { X, Y - 1, X + 500.0f, Y + 50.0f },
                    { X + 1, Y, X + 500.0f, Y + 50.0f },
                    { X, Y + 1, X + 500.0f, Y + 50.0f },
                    { X, Y, X + 500.0f, Y + 50.0f },
                };

                pFont->Begin();
                if (dColor != BLACK)
                {
                    for (auto i = 0; i < 4; i++)
                        pFont->DrawText(cBuffer, -1, &Rect[i], DT_NOCLIP, BLACK);
                }

                pFont->DrawText(cBuffer, -1, &Rect[4], DT_NOCLIP, dColor);
                pFont->End();
            };
            
            static char str_format_fps[] = "%02d";
            static char str_format_time[] = "%.01f ms";
            static const D3DXCOLOR YELLOW(D3DCOLOR_XRGB(0xF7, 0xF7, 0));
            DrawTextOutline(pFPSFont, 10, 10, YELLOW, str_format_fps, fps);
            DrawTextOutline(pTimeFont, 10, space, YELLOW, str_format_time, (1.0f / fps) * 1000.0f);
        }
    }

private:
    static void Ticks()
    {
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        TIME_Ticks = (double)counter.QuadPart / TIME_Frequency;
    }
};

FrameLimiter::FPSLimitMode mFPSLimitMode = FrameLimiter::FPSLimitMode::FPS_NONE;

// ---------------------------------------------------------
// --------------------- Helper methods --------------------
// ----------------------------------------------------------

//Present params logger

static void LogPP(const char* tag, const D3DPRESENT_PARAMETERS8* p)
{
    if (!p) return;

    char b[512];
    wsprintfA(b,
        "[RES] %s: BB=%lux%lu  Win=%d  Swap=%lu  MS=%lu  Interval=%lu  hWnd=%p",
        tag,
        (unsigned long)p->BackBufferWidth,
        (unsigned long)p->BackBufferHeight,
        (int)p->Windowed,
        (unsigned long)p->SwapEffect,
        (unsigned long)p->MultiSampleType,
        (unsigned long)p->FullScreen_PresentationInterval,
        p->hDeviceWindow
    );
    LogLine(b);
}

//Viewport logger
static void LogVP(const char* tag, const D3DVIEWPORT8* vp)
{
    if (!vp) { LogLine("[VP] %s (null)", tag); return; }
    LogLine("[VP] %s X=%u Y=%u W=%u H=%u MinZ=%.3f MaxZ=%.3f",
        tag, vp->X, vp->Y, vp->Width, vp->Height, vp->MinZ, vp->MaxZ);
}

//Log back buffer once 
static UINT lastW = 0, lastH = 0;

static void LogBackbufferOncePerChange(IDirect3DDevice8* dev)
{
    IDirect3DSurface8* bb = nullptr;
    if (SUCCEEDED(dev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
    {
        D3DSURFACE_DESC8 d{};
        bb->GetDesc(&d);
        bb->Release();

        if (d.Width != lastW || d.Height != lastH)
        {
            lastW = d.Width; lastH = d.Height;
            LogLine("[BB] Backbuffer now %ux%u fmt=%d", d.Width, d.Height, (int)d.Format);
        }
    }
}

// Helper; Resize window to get desired client area size

static void ResizeClient(HWND hwnd, int clientW, int clientH)
{
    if (!hwnd || clientW <= 0 || clientH <= 0) return;

    RECT rc = { 0, 0, clientW, clientH };
    DWORD style = (DWORD)GetWindowLongPtr(hwnd, GWL_STYLE);
    DWORD exStyle = (DWORD)GetWindowLongPtr(hwnd, GWL_EXSTYLE);

    AdjustWindowRectEx(&rc, style, FALSE, exStyle);

    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    SetWindowPos(hwnd, nullptr, 0, 0, winW, winH,
        SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
}

// Helper; Apply custom window title
static void ApplyWindowTitle(HWND hwnd)
{
    if (hwnd && IsWindow(hwnd))
        SetWindowTextA(hwnd, "RESIDENT EVIL: SURVIVOR PC");
}

void StealIconFromExe(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return;

    // 1. Get the path of the current process (.exe)
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    // 2. Extract the icon from the exe
    // Index 0 is almost always the main game icon
    HICON hIconLarge = nullptr;
    HICON hIconSmall = nullptr;

    // ExtractIconEx gets both the 32x32 and 16x16 versions
    if (ExtractIconExA(exePath, 0, &hIconLarge, &hIconSmall, 1) != 0) {
        // 3. Force the window to use them
        if (hIconLarge) {
            SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIconLarge);
        }
        if (hIconSmall) {
            SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
        }

        // 4. Update the Window Class so it stays persistent
        if (hIconLarge) SetClassLongPtr(hWnd, GCLP_HICON, (LONG_PTR)hIconLarge);
        if (hIconSmall) SetClassLongPtr(hWnd, GCLP_HICONSM, (LONG_PTR)hIconSmall);
    }
}

// IDirect3DDevice8 Hooks

// Fix Resident Evil Survivor Crosshair issue by adjusting scissor rect
/*
HRESULT m_IDirect3DDevice8::SetScissorRect(CONST RECT* pRect)
{
    if (!pRect)
        return ProxyInterface->SetScissorRect(pRect);

    RECT r = *pRect;

    // If scissor is half width, expand it
    if (gViewportFixEnabled && gBBWidth && (r.right - r.left) == (LONG)(gBBWidth / 2))
    {
        r.left = 0;
        r.right = (LONG)gBBWidth;
    }

    return ProxyInterface->SetScissorRect(&r);
}
*/
HRESULT m_IDirect3DDevice8::Present(CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion)
{
    // --- FPS Limiter Logic ---
    if (mFPSLimitMode == FrameLimiter::FPSLimitMode::FPS_REALTIME)
        while (!FrameLimiter::Sync_RT());
    else if (mFPSLimitMode == FrameLimiter::FPSLimitMode::FPS_ACCURATE)
        while (!FrameLimiter::Sync_SLP());

    // --- DX9 COMPATIBILITY: Window Handle Safety ---
    // If the game passes NULL for the window override (very common in DX8),
    // we use the g_hFocusWindow we captured during CreateDevice.
    // This ensures the DX9 swapchain knows exactly which window to draw into.
    HWND targetWnd = hDestWindowOverride;
    if (targetWnd == NULL) targetWnd = g_hFocusWindow;
    if (targetWnd == NULL) targetWnd = GetActiveWindow();

    LogBackbufferOncePerChange(ProxyInterface); // debug

    SurvivorPc_DrawScreenFx(this, ProxyInterface); // PS1 fades and screen effects requested by Survivor Fix

    // Call the actual DX9-backed Present
    HRESULT hr = ProxyInterface->Present(pSourceRect, pDestRect, targetWnd, pDirtyRegion);
    SurvivorPc_OnPresentResult(hr);

    // --- DX9 COMPATIBILITY: Device State Management ---
    // If Present returns an error, the game engine usually panics and shuts down.
    // We catch common DX9 errors here to prevent the 'Black Screen -> Close' loop.
    if (FAILED(hr))
    {
        // D3DERR_DEVICELOST: Occurs if the user Alt-Tabs or during resolution changes.
        // We return S_OK to the game engine so it doesn't crash, allowing us to 
        // wait for the device to become ready again.
        if (hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET)
        {
            return S_OK;
        }
    }

    return hr;
}

// -----------------------------------------------------------------
// Update cursor clipping to window
// -----------------------------------------------------------------

static void UpdateCursorClip()
{
    if (!bLockCursorToWindow || !g_hFocusWindow)
    {
        ClipCursor(NULL);
        return;
    }

    if (GetForegroundWindow() != g_hFocusWindow)
    {
        ClipCursor(NULL);
        return;
    }

    RECT rc;
    GetClientRect(g_hFocusWindow, &rc);

    POINT tl{ rc.left, rc.top };
    POINT br{ rc.right, rc.bottom };
    ClientToScreen(g_hFocusWindow, &tl);
    ClientToScreen(g_hFocusWindow, &br);

    RECT clip{ tl.x, tl.y, br.x, br.y };
    ClipCursor(&clip);
}

// -----------------------------------------------------------------
// Modified EndScene to include overlay crosshair
// (function header and back buffer query restored from V1.0, 9db2112)
// -----------------------------------------------------------------
HRESULT m_IDirect3DDevice8::EndScene()
{
    sf::features::QuickSavePollWindowToggle();
    UpdateCursorClip();

    if (bDisplayFPSCounter)
        FrameLimiter::ShowFPS(ProxyInterface);

    const bool movieWasPlaying = g_Video.isPlaying;
    if (g_hFocusWindow && g_Video.isPlaying)
    {
        UINT bbW = 0, bbH = 0;
        IDirect3DSurface8* bb = nullptr;
        if (SUCCEEDED(ProxyInterface->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
        {
            D3DSURFACE_DESC8 d{};
            if (SUCCEEDED(bb->GetDesc(&d))) { bbW = d.Width; bbH = d.Height; }
            bb->Release();
        }
        if (bbW && bbH) RenderMovie(bbW, bbH);
    }

    // Native FONT000.BMP feedback for function-key actions and actionable failures.
    // Draw after the movie surface so the notice is never hidden by an overlay frame.
    if (g_hFocusWindow)
        DrawHotkeyNotification(ProxyInterface);

    if (gDrawOverlayCrosshair && g_hFocusWindow && !movieWasPlaying)
    {
        // Query backbuffer size (the coordinate space that DrawPrimitiveUP uses here)
        UINT bbW = 0, bbH = 0;
        IDirect3DSurface8* bb = nullptr;
        if (SUCCEEDED(ProxyInterface->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
        {
            D3DSURFACE_DESC8 d{};
            if (SUCCEEDED(bb->GetDesc(&d)))
            {
                bbW = d.Width;
                bbH = d.Height;
            }
            bb->Release();
        }

        // Fallback (should rarely happen)
        if (bbW == 0 || bbH == 0)
        {
            RECT r{};
            if (GetClientRect(g_hFocusWindow, &r))
            {
                bbW = (UINT)(r.right - r.left);
                bbH = (UINT)(r.bottom - r.top);
            }
        }

        if (bbW > 0 && bbH > 0)
        {
            const float xCompensation = BackBufferXCompensation(bbW, bbH);
            if (!gAimInit)
            {
                gAimX = (float)bbW * 0.5f;
                gAimY = (float)bbH * 0.5f;
                gAimInit = true;
            }

            EnsureCrosshairTexture(ProxyInterface);
            const bool drawCross = SurvivorPc_UpdateCrosshair(g_hFocusWindow, &gAimX, &gAimY, (int)bbW, (int)bbH) &&
                                   !SurvivorPc_ScreenFxHidesCrosshair();
            if (!drawCross || !bLaserTrail) gLaserTrailPoints.clear();

            if (gCrossTex)
            {
                // clamp and persist IN BACKBUFFER SPACE
                if (gAimX < 0) gAimX = 0;
                if (gAimY < 0) gAimY = 0;
                if (gAimX > (float)bbW - 1) gAimX = (float)bbW - 1;
                if (gAimY > (float)bbH - 1) gAimY = (float)bbH - 1;
                SurvivorPc_SyncGameAim(gAimX, gAimY);//the game's aim follows the crosshair
                if (drawCross && bLaserTrail) {
                    DrawLaserTrail(ProxyInterface, gAimX, gAimY);
                    DrawLaserImpactDot(ProxyInterface, gAimX, gAimY, xCompensation);
                }

                // --- PATCHED SCALE LOGIC ---
                D3DSURFACE_DESC8 texDesc;
                if (drawCross && SUCCEEDED(gCrossTex->GetLevelDesc(0, &texDesc)))
                {
                    // Use original PNG size multiplied by the scale from the Config Window
                    float finalW = (float)texDesc.Width * fCrosshairScale * xCompensation;
                    float finalH = (float)texDesc.Height * fCrosshairScale;

                    // Draw centered on current Aim coordinates
                    DrawTexturedQuad(ProxyInterface, gCrossTex,
                        gAimX - (finalW * 0.5f),
                        gAimY - (finalH * 0.5f),
                        finalW, finalH);
                }
            }
        }
        return ProxyInterface->EndScene();
    }

    // Call the DX9-backed EndScene
    HRESULT hr = ProxyInterface->EndScene();

    // If the engine fails here, we return S_OK to try and keep the game process alive 
    // during the transition.
    if (FAILED(hr)) return S_OK;

    return hr;
}

HRESULT m_IDirect3DDevice8::CreateTexture(
    UINT Width, UINT Height, UINT Levels, DWORD Usage,
    D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture8** ppTexture)
{
    if (!ProxyInterface)
        return D3DERR_INVALIDCALL;

    HRESULT hr = ProxyInterface->CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture);
    if (FAILED(hr) || !ppTexture || !*ppTexture)
        return hr;

    auto* wrapped = new m_IDirect3DTexture8(*ppTexture, this);
    // Native impact/headshot sheets may be expanded by the loader to a
    // power-of-two 256x64 allocation. Pair the exact preceding file-open event
    // with either the source or padded size instead of guessing globally.
    const bool nativeBloodSize = (Width == 240 && Height == 40) ||
        (Width == 193 && Height == 32) || (Width == 256 && Height == 64);
    if (nativeBloodSize && HasPendingBloodTextureOpen()) {
        wrapped->MarkBloodTextureCandidate();
        ConsumePendingBloodTextureOpen();
    }
    *ppTexture = wrapped;

    return hr;
}

/*
HRESULT m_IDirect3DTexture8::UnlockRect(UINT Level)
{
    HRESULT hr = ProxyInterface->UnlockRect(Level);

    if (SUCCEEDED(hr) && Level == 0 && alphaFixTag != AFT_None)
    {
        AlphaFix_ApplyPNGToTexture(ProxyInterface, alphaFixTag);
        alphaFixTag = AFT_None; // apply once
    }

    return hr;
}
*/

void ForceWindowed(D3DPRESENT_PARAMETERS8* pPresentationParameters)
{
    HWND hwnd = pPresentationParameters->hDeviceWindow ? pPresentationParameters->hDeviceWindow : g_hFocusWindow;
    HMONITOR monitor = MonitorFromWindow((!bUsePrimaryMonitor && hwnd) ? hwnd : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO info;
    info.cbSize = sizeof(MONITORINFO);
    GetMonitorInfo(monitor, &info);
    int DesktopResX = info.rcMonitor.right - info.rcMonitor.left;
    int DesktopResY = info.rcMonitor.bottom - info.rcMonitor.top;

    int left = (int)info.rcMonitor.left;
    int top = (int)info.rcMonitor.top;

	// Adjust window style and position

    if (!bBorderlessFullscreen)
    {
        LONG style = GetWindowLong(hwnd, GWL_STYLE);

        left += (int)(((float)DesktopResX / 2.0f) - ((float)pPresentationParameters->BackBufferWidth / 2.0f));
        top += (int)(((float)DesktopResY / 2.0f) - ((float)pPresentationParameters->BackBufferHeight / 2.0f));

        style = (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW;   // border + title
        //if (bShowWindowBorder)
        //    style = (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW;   // border + title
        //else
        //    style = (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP;   // no border

        SetWindowLong(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
    }

    pPresentationParameters->Windowed = 1;

    // These must be set to default (0) on windowed mode as per spec to prevent app freezing
    pPresentationParameters->FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    pPresentationParameters->FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

    if (hwnd != NULL)
    {
        UINT uFlags = SWP_SHOWWINDOW;
        if (bBorderlessFullscreen)
        {
            LONG lOldStyle = GetWindowLong(hwnd, GWL_STYLE);
            LONG lOldExStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
            LONG lNewStyle = lOldStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_DLGFRAME);
            lNewStyle |= (lOldStyle & WS_CHILD) ? 0 : WS_POPUP;
            LONG lNewExStyle = lOldExStyle & ~(WS_EX_CONTEXTHELP | WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE | WS_EX_TOOLWINDOW);
            lNewExStyle |= WS_EX_APPWINDOW;

            if (lNewStyle != lOldStyle)
            {
                SetWindowLong(hwnd, GWL_STYLE, lNewStyle);
                uFlags |= SWP_FRAMECHANGED;
            }
            if (lNewExStyle != lOldExStyle)
            {
                SetWindowLong(hwnd, GWL_EXSTYLE, lNewExStyle);
                uFlags |= SWP_FRAMECHANGED;
            }
            SetWindowPos(hwnd, bAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, left, top, DesktopResX, DesktopResY, uFlags);
        }

        else
        {
            if (!bCenterWindow)
                uFlags |= SWP_NOMOVE;

            SetWindowPos(hwnd, bAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, left, top, pPresentationParameters->BackBufferWidth, pPresentationParameters->BackBufferHeight, uFlags);
        }
    }
}

void ForceFullScreenRefreshRateInHz(D3DPRESENT_PARAMETERS8* pPresentationParameters)
{
    if (!pPresentationParameters->Windowed)
    {
        std::vector<int> list;
        DISPLAY_DEVICE dd;
        dd.cb = sizeof(DISPLAY_DEVICE);
        DWORD deviceNum = 0;
        while (EnumDisplayDevices(NULL, deviceNum, &dd, 0))
        {
            DISPLAY_DEVICE newdd = { 0 };
            newdd.cb = sizeof(DISPLAY_DEVICE);
            DWORD monitorNum = 0;
            DEVMODE dm = { 0 };
            while (EnumDisplayDevices(dd.DeviceName, monitorNum, &newdd, 0))
            {
                for (auto iModeNum = 0; EnumDisplaySettings(NULL, iModeNum, &dm) != 0; iModeNum++)
                    list.emplace_back(dm.dmDisplayFrequency);
                monitorNum++;
            }
            deviceNum++;
        }

        std::sort(list.begin(), list.end());
        if (nFullScreenRefreshRateInHz > list.back() || nFullScreenRefreshRateInHz < list.front() || nFullScreenRefreshRateInHz < 0)
            pPresentationParameters->FullScreen_RefreshRateInHz = list.back();
        else
            pPresentationParameters->FullScreen_RefreshRateInHz = nFullScreenRefreshRateInHz;
    }
}

HRESULT m_IDirect3D8::CreateDevice(
    UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS8* p, IDirect3DDevice8** ppReturnedDeviceInterface)
{
    // --- 1. Fix Window Handle ---
    HWND actualWnd = hFocusWindow;
    if (actualWnd == NULL) actualWnd = (p ? p->hDeviceWindow : NULL);
    if (actualWnd == NULL) actualWnd = GetActiveWindow();
    g_hFocusWindow = actualWnd;

    ApplyWindowTitle(g_hFocusWindow);
    LogPP("CreateDevice IN:", p);

    // --- 2. Apply Wrapper & DX9 Compatibility Options ---
    if (bForceWindowedMode)
        ForceWindowed(p);

    if (nFullScreenRefreshRateInHz)
        ForceFullScreenRefreshRateInHz(p);

    if (p)
    {
        // DX9 translation requires DISCARD and hates Lockable Backbuffers
        p->SwapEffect = D3DSWAPEFFECT_DISCARD;
        p->BackBufferFormat = D3DFMT_X8R8G8B8; // Match the Format 22 from your log
        p->Flags &= ~D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    }

    // --- 3. Create the Device ---
    HRESULT hr = ProxyInterface->CreateDevice(
        Adapter, DeviceType, actualWnd, BehaviorFlags, p, ppReturnedDeviceInterface);

    // --- 4. Cleanup UI Resources ---
    if (bDisplayFPSCounter)
    {
        if (FrameLimiter::pFPSFont)  FrameLimiter::pFPSFont->Release();
        if (FrameLimiter::pTimeFont) FrameLimiter::pTimeFont->Release();
        FrameLimiter::pFPSFont = nullptr;
        FrameLimiter::pTimeFont = nullptr;
    }

    LogPP("CreateDevice OUT:", p);

    // --- 5. Wrap and Finalize ---
    if (SUCCEEDED(hr) && ppReturnedDeviceInterface && *ppReturnedDeviceInterface)
    {
        // Wrap it FIRST so the game uses your proxy for subsequent calls
        *ppReturnedDeviceInterface = new m_IDirect3DDevice8(*ppReturnedDeviceInterface, this);

        if (p && p->Windowed && g_hFocusWindow && ForceWidth > 0 && ForceHeight > 0)
        {
            ResizeClient(g_hFocusWindow, ForceWidth, ForceHeight);
            SurvivorPc_OnDevice(g_hFocusWindow, p->BackBufferWidth, p->BackBufferHeight, ForceWidth, ForceHeight);
        }

        ApplyWindowTitle(g_hFocusWindow);
        StealIconFromExe(g_hFocusWindow);
    }
    return hr;
}

HRESULT m_IDirect3DDevice8::Reset(D3DPRESENT_PARAMETERS8* p)
{
    LogPP("Reset IN:", p);

    // Always apply title
    ApplyWindowTitle(g_hFocusWindow);

    // --- DX9 COMPATIBILITY FIX (MUST MATCH CreateDevice) ---
    if (p)
    {
        if (bForceWindowedMode)
            ForceWindowed(p);

        if (nFullScreenRefreshRateInHz)
            ForceFullScreenRefreshRateInHz(p);

        // Force these for DX9 translation stability
        p->SwapEffect = D3DSWAPEFFECT_DISCARD;
        p->BackBufferFormat = D3DFMT_X8R8G8B8;
        p->Flags &= ~D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    }

    // FPS resources MUST be released before Reset
    if (bDisplayFPSCounter)
    {
        if (FrameLimiter::pFPSFont)  FrameLimiter::pFPSFont->Release();
        if (FrameLimiter::pTimeFont) FrameLimiter::pTimeFont->Release();
        FrameLimiter::pFPSFont = nullptr;
        FrameLimiter::pTimeFont = nullptr;
    }

    SurvivorPc_ReleaseScreenFx(); // its frame copy is in D3DPOOL_DEFAULT

    // Call the actual DX9 Reset
    HRESULT hr = ProxyInterface->Reset(p);

    LogPP("Reset OUT:", p);

    if (SUCCEEDED(hr))
    {
        if (p && p->Windowed && g_hFocusWindow && ForceWidth > 0 && ForceHeight > 0)
        {
            ResizeClient(g_hFocusWindow, ForceWidth, ForceHeight);
            SurvivorPc_OnDevice(g_hFocusWindow, p->BackBufferWidth, p->BackBufferHeight, ForceWidth, ForceHeight);
            ApplyWindowTitle(g_hFocusWindow);
        }
    }

    return hr;
}

HRESULT m_IDirect3DDevice8::SetViewport(CONST D3DVIEWPORT8* pViewport)
{
    LogVP("SetViewport IN", pViewport);
    HRESULT hr = ProxyInterface->SetViewport(pViewport);
    LogVP("SetViewport OUT", pViewport);
    return hr;
}

LRESULT WINAPI CustomWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, int idx)
{
    if (hWnd == g_hFocusWindow || _fnIsTopLevelWindow(hWnd)) // skip child windows like buttons, edit boxes, etc. 
    {
        if (hWnd == g_hFocusWindow &&
            sf::features::QuickSaveHandleWindowMessage(uMsg, wParam, lParam))
            return 0;
        if (bAlwaysOnTop)
        {
            if ((GetWindowLong(hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0)
                SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
        }
        switch (uMsg)
        {
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE)
            {
                if ((HWND)lParam == NULL)
                    return 0;
                DWORD dwPID = 0;
                GetWindowThreadProcessId((HWND)lParam, &dwPID);
                if (dwPID != GetCurrentProcessId())
                    return 0;
            }
            break;
        case WM_NCACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE)
                return 0;
            break;
        case WM_ACTIVATEAPP:
            if (wParam == FALSE)
                return 0;
            break;
        case WM_KILLFOCUS:
        {
            if ((HWND)wParam == NULL)
                return 0;
            DWORD dwPID = 0;
            GetWindowThreadProcessId((HWND)wParam, &dwPID);
            if (dwPID != GetCurrentProcessId())
                return 0;
        }
        break;
        default:
            break;
        }
    }
    WNDPROC OrigProc = WNDPROC(WndProcList[idx].second);
    return OrigProc(hWnd, uMsg, wParam, lParam);
}

LRESULT WINAPI CustomWndProcA(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    WORD wClassAtom = GetClassWord(hWnd, GCW_ATOM);
    if (wClassAtom)
    {
        for (unsigned int i = 0; i < WndProcList.size(); i++) {
            if (WndProcList[i].first == wClassAtom) {
                return CustomWndProc(hWnd, uMsg, wParam, lParam, i);
            }
        }
    }
    // We should never reach here, but having safeguards anyway is good
    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

LRESULT WINAPI CustomWndProcW(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    WORD wClassAtom = GetClassWord(hWnd, GCW_ATOM);
    if (wClassAtom)
    {
        for (unsigned int i = 0; i < WndProcList.size(); i++) {
            if (WndProcList[i].first == wClassAtom) {
                return CustomWndProc(hWnd, uMsg, wParam, lParam, i);
            }
        }
    }
    // We should never reach here, but having safeguards anyway is good
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

typedef ATOM(__stdcall* RegisterClassA_fn)(const WNDCLASSA*);
typedef ATOM(__stdcall* RegisterClassW_fn)(const WNDCLASSW*);
typedef ATOM(__stdcall* RegisterClassExA_fn)(const WNDCLASSEXA*);
typedef ATOM(__stdcall* RegisterClassExW_fn)(const WNDCLASSEXW*);
RegisterClassA_fn oRegisterClassA = NULL;
RegisterClassW_fn oRegisterClassW = NULL;
RegisterClassExA_fn oRegisterClassExA = NULL;
RegisterClassExW_fn oRegisterClassExW = NULL;
ATOM __stdcall hk_RegisterClassA(WNDCLASSA* lpWndClass)
{
    if (!IsValueIntAtom(DWORD(lpWndClass->lpszClassName))) { // argument is a class name
        if (IsSystemClassNameA(lpWndClass->lpszClassName)) { // skip system classes like buttons, common controls, etc.
            return oRegisterClassA(lpWndClass);
        }
    }
    ULONG_PTR pWndProc = ULONG_PTR(lpWndClass->lpfnWndProc);
    lpWndClass->lpfnWndProc = CustomWndProcA;
    WORD wClassAtom = oRegisterClassA(lpWndClass);
    if (wClassAtom != 0)
    {
        WndProcList.emplace_back(wClassAtom, pWndProc);
    }
    return wClassAtom;
}
ATOM __stdcall hk_RegisterClassW(WNDCLASSW* lpWndClass)
{
    if (!IsValueIntAtom(DWORD(lpWndClass->lpszClassName))) { // argument is a class name
        if (IsSystemClassNameW(lpWndClass->lpszClassName)) { // skip system classes like buttons, common controls, etc.
            return oRegisterClassW(lpWndClass);
        }
    }
    ULONG_PTR pWndProc = ULONG_PTR(lpWndClass->lpfnWndProc);
    lpWndClass->lpfnWndProc = CustomWndProcW;
    WORD wClassAtom = oRegisterClassW(lpWndClass);
    if (wClassAtom != 0)
    {
        WndProcList.emplace_back(wClassAtom, pWndProc);
    }
    return wClassAtom;
}
ATOM __stdcall hk_RegisterClassExA(WNDCLASSEXA* lpWndClass)
{
    if (!IsValueIntAtom(DWORD(lpWndClass->lpszClassName))) { // argument is a class name
        if (IsSystemClassNameA(lpWndClass->lpszClassName)) { // skip system classes like buttons, common controls, etc.
            return oRegisterClassExA(lpWndClass);
        }
    }
    ULONG_PTR pWndProc = ULONG_PTR(lpWndClass->lpfnWndProc);
    lpWndClass->lpfnWndProc = CustomWndProcA;
    WORD wClassAtom = oRegisterClassExA(lpWndClass);
    if (wClassAtom != 0)
    {
        WndProcList.emplace_back(wClassAtom, pWndProc);
    }
    return wClassAtom;
}
ATOM __stdcall hk_RegisterClassExW(WNDCLASSEXW* lpWndClass)
{
    if (!IsValueIntAtom(DWORD(lpWndClass->lpszClassName))) { // argument is a class name
        if (IsSystemClassNameW(lpWndClass->lpszClassName)) { // skip system classes like buttons, common controls, etc.
            return oRegisterClassExW(lpWndClass);
        }
    }
    ULONG_PTR pWndProc = ULONG_PTR(lpWndClass->lpfnWndProc);
    lpWndClass->lpfnWndProc = CustomWndProcW;
    WORD wClassAtom = oRegisterClassExW(lpWndClass);
    if (wClassAtom != 0)
    {
        WndProcList.emplace_back(wClassAtom, pWndProc);
    }
    return wClassAtom;
}

typedef HWND(__stdcall* GetForegroundWindow_fn)(void);
GetForegroundWindow_fn oGetForegroundWindow = NULL;

HWND __stdcall hk_GetForegroundWindow()
{
    if (g_hFocusWindow && IsWindow(g_hFocusWindow))
        return g_hFocusWindow;
    return oGetForegroundWindow();
}

typedef HWND(__stdcall* GetActiveWindow_fn)(void);
GetActiveWindow_fn oGetActiveWindow = NULL;

HWND __stdcall hk_GetActiveWindow(void)
{
    HWND hWndActive = oGetActiveWindow();
    if (g_hFocusWindow && hWndActive == NULL && IsWindow(g_hFocusWindow))
    {
        if (GetCurrentThreadId() == GetWindowThreadProcessId(g_hFocusWindow, NULL))
            return g_hFocusWindow;
    }
    return hWndActive;
}

typedef HWND(__stdcall* GetFocus_fn)(void);
GetFocus_fn oGetFocus = NULL;

HWND __stdcall hk_GetFocus(void)
{
    HWND hWndFocus = oGetFocus();
    if (g_hFocusWindow && hWndFocus == NULL && IsWindow(g_hFocusWindow))
    {
        if (GetCurrentThreadId() == GetWindowThreadProcessId(g_hFocusWindow, NULL))
            return g_hFocusWindow;
    }
    return hWndFocus;
}

typedef HMODULE(__stdcall* LoadLibraryA_fn)(LPCSTR lpLibFileName);
LoadLibraryA_fn oLoadLibraryA;

HMODULE __stdcall hk_LoadLibraryA(LPCSTR lpLibFileName)
{
    HMODULE hmod = oLoadLibraryA(lpLibFileName);
    if (hmod)
    {
        HookModule(hmod);
    }
    return hmod;
}

typedef HMODULE(__stdcall* LoadLibraryW_fn)(LPCWSTR lpLibFileName);
LoadLibraryW_fn oLoadLibraryW;

HMODULE __stdcall hk_LoadLibraryW(LPCWSTR lpLibFileName)
{
    HMODULE hmod = oLoadLibraryW(lpLibFileName);
    if (hmod)
    {
        HookModule(hmod);
    }
    return hmod;
}

typedef HMODULE(__stdcall* LoadLibraryExA_fn)(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
LoadLibraryExA_fn oLoadLibraryExA;

HMODULE __stdcall hk_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    HMODULE hmod = oLoadLibraryExA(lpLibFileName, hFile, dwFlags);
    if (hmod && ((dwFlags & (LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)) == 0))
    {
        HookModule(hmod);
    }
    return hmod;
}

typedef HMODULE(__stdcall* LoadLibraryExW_fn)(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
LoadLibraryExW_fn oLoadLibraryExW;

HMODULE __stdcall hk_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    HMODULE hmod = oLoadLibraryExW(lpLibFileName, hFile, dwFlags);
    if (hmod && ((dwFlags & (LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)) == 0))
    {
        HookModule(hmod);
    }
    return hmod;
}

typedef BOOL(__stdcall* FreeLibrary_fn)(HMODULE hLibModule);
FreeLibrary_fn oFreeLibrary;

BOOL __stdcall hk_FreeLibrary(HMODULE hLibModule)
{
    if (hLibModule == g_hWrapperModule)
        return TRUE; // We will stay in memory, thank you very much

    return oFreeLibrary(hLibModule);
}

FARPROC __stdcall hk_GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{
    __try
    {
        if (!lstrcmpA(lpProcName, "RegisterClassA"))
        {
            if (oRegisterClassA == NULL)
                oRegisterClassA = (RegisterClassA_fn)GetProcAddress(hModule, lpProcName);
            return (FARPROC)hk_RegisterClassA;
        }
        if (!lstrcmpA(lpProcName, "RegisterClassW"))
        {
            if (oRegisterClassW == NULL)
                oRegisterClassW = (RegisterClassW_fn)GetProcAddress(hModule, lpProcName);
            return (FARPROC)hk_RegisterClassW;
        }
        if (!lstrcmpA(lpProcName, "RegisterClassExA"))
        {
            if (oRegisterClassExA == NULL)
                oRegisterClassExA = (RegisterClassExA_fn)GetProcAddress(hModule, lpProcName);
            return (FARPROC)hk_RegisterClassExA;
        }
        if (!lstrcmpA(lpProcName, "RegisterClassExW"))
        {
            if (oRegisterClassExW == NULL)
                oRegisterClassExW = (RegisterClassExW_fn)GetProcAddress(hModule, lpProcName);
            return (FARPROC)hk_RegisterClassExW;
        }
        if (!lstrcmpA(lpProcName, "GetForegroundWindow"))
        {
            if (oGetForegroundWindow == NULL)
                oGetForegroundWindow = (GetForegroundWindow_fn)GetProcAddress(hModule, lpProcName);
            return (FARPROC)hk_GetForegroundWindow;
        }
        if (!lstrcmpA(lpProcName, "GetActiveWindow"))
        {
            if (oGetActiveWindow == NULL)
                oGetActiveWindow = (GetActiveWindow_fn)GetProcAddress(hModule, lpProcName);
            return (FARPROC)hk_GetActiveWindow;
        }
        if (!lstrcmpA(lpProcName, "GetFocus"))
        {
            if (oGetFocus == NULL)
                oGetFocus = (GetFocus_fn)GetProcAddress(hModule, lpProcName);
            return (FARPROC)hk_GetFocus;
        }
        if (!lstrcmpA(lpProcName, "LoadLibraryA"))
        {
            if (oLoadLibraryA == NULL)
                oLoadLibraryA = (LoadLibraryA_fn)GetProcAddress(hModule, lpProcName);
            return (FARPROC)hk_LoadLibraryA;
        }
        if (!lstrcmpA(lpProcName, "LoadLibraryW"))
        {
            if (oLoadLibraryW == NULL)
                oLoadLibraryW = (LoadLibraryW_fn)GetProcAddress(hModule, lpProcName);
            return (FARPROC)hk_LoadLibraryW;
        }
        if (!lstrcmpA(lpProcName, "LoadLibraryExA"))
        {
            if (oLoadLibraryExA == NULL)
                oLoadLibraryExA = (LoadLibraryExA_fn)GetProcAddress(hModule, lpProcName);
            return (FARPROC)hk_LoadLibraryExA;
        }
        if (!lstrcmpA(lpProcName, "LoadLibraryExW"))
        {
            if (oLoadLibraryExW == NULL)
                oLoadLibraryExW = (LoadLibraryExW_fn)GetProcAddress(hModule, lpProcName);
            return (FARPROC)hk_LoadLibraryExW;
        }
        if (!lstrcmpA(lpProcName, "FreeLibrary"))
        {
            if (oFreeLibrary == NULL)
                oFreeLibrary = (FreeLibrary_fn)GetProcAddress(hModule, lpProcName);
            return (FARPROC)hk_FreeLibrary;
        }
    }
    __except ((GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
    }

    return GetProcAddress(hModule, lpProcName);
}

void HookModule(HMODULE hmod)
{
    char modpath[MAX_PATH + 1];
    if (hmod == g_hWrapperModule) // Yeah, let's not go and hook ourselves
        return;
    if (GetModuleFileNameA(hmod, modpath, MAX_PATH)) {
        if (!_strnicmp(modpath, WinDir, strlen(WinDir))) { // skip system modules
            return;
        }
    }
    if (oRegisterClassA == NULL)
        oRegisterClassA = (RegisterClassA_fn)Iat_hook::detour_iat_ptr("RegisterClassA", (void*)hk_RegisterClassA, hmod);
    else
        Iat_hook::detour_iat_ptr("RegisterClassA", (void*)hk_RegisterClassA, hmod);

    if (oRegisterClassW == NULL)
        oRegisterClassW = (RegisterClassW_fn)Iat_hook::detour_iat_ptr("RegisterClassW", (void*)hk_RegisterClassW, hmod);
    else
        Iat_hook::detour_iat_ptr("RegisterClassW", (void*)hk_RegisterClassW, hmod);

    if (oRegisterClassExA == NULL)
        oRegisterClassExA = (RegisterClassExA_fn)Iat_hook::detour_iat_ptr("RegisterClassExA", (void*)hk_RegisterClassExA, hmod);
    else
        Iat_hook::detour_iat_ptr("RegisterClassExA", (void*)hk_RegisterClassExA, hmod);

    if (oRegisterClassExW == NULL)
        oRegisterClassExW = (RegisterClassExW_fn)Iat_hook::detour_iat_ptr("RegisterClassExW", (void*)hk_RegisterClassExW, hmod);
    else
        Iat_hook::detour_iat_ptr("RegisterClassExW", (void*)hk_RegisterClassExW, hmod);

    if (oGetForegroundWindow == NULL)
        oGetForegroundWindow = (GetForegroundWindow_fn)Iat_hook::detour_iat_ptr("GetForegroundWindow", (void*)hk_GetForegroundWindow, hmod);
    else
        Iat_hook::detour_iat_ptr("GetForegroundWindow", (void*)hk_GetForegroundWindow, hmod);

    if (oGetActiveWindow == NULL)
        oGetActiveWindow = (GetActiveWindow_fn)Iat_hook::detour_iat_ptr("GetActiveWindow", (void*)hk_GetActiveWindow, hmod);
    else
        Iat_hook::detour_iat_ptr("GetActiveWindow", (void*)hk_GetActiveWindow, hmod);

    if (oGetFocus == NULL)
        oGetFocus = (GetFocus_fn)Iat_hook::detour_iat_ptr("GetFocus", (void*)hk_GetFocus, hmod);
    else
        Iat_hook::detour_iat_ptr("GetFocus", (void*)hk_GetFocus, hmod);

    if (oLoadLibraryA == NULL)
        oLoadLibraryA = (LoadLibraryA_fn)Iat_hook::detour_iat_ptr("LoadLibraryA", (void*)hk_LoadLibraryA, hmod);
    else
        Iat_hook::detour_iat_ptr("LoadLibraryA", (void*)hk_LoadLibraryA, hmod);

    if (oLoadLibraryW == NULL)
        oLoadLibraryW = (LoadLibraryW_fn)Iat_hook::detour_iat_ptr("LoadLibraryW", (void*)hk_LoadLibraryW, hmod);
    else
        Iat_hook::detour_iat_ptr("LoadLibraryW", (void*)hk_LoadLibraryW, hmod);

    if (oLoadLibraryExA == NULL)
        oLoadLibraryExA = (LoadLibraryExA_fn)Iat_hook::detour_iat_ptr("LoadLibraryExA", (void*)hk_LoadLibraryExA, hmod);
    else
        Iat_hook::detour_iat_ptr("LoadLibraryExA", (void*)hk_LoadLibraryExA, hmod);

    if (oLoadLibraryExW == NULL)
        oLoadLibraryExW = (LoadLibraryExW_fn)Iat_hook::detour_iat_ptr("LoadLibraryExW", (void*)hk_LoadLibraryExW, hmod);
    else
        Iat_hook::detour_iat_ptr("LoadLibraryExW", (void*)hk_LoadLibraryExW, hmod);

    if (oFreeLibrary == NULL)
        oFreeLibrary = (FreeLibrary_fn)Iat_hook::detour_iat_ptr("FreeLibrary", (void*)hk_FreeLibrary, hmod);
    else
        Iat_hook::detour_iat_ptr("FreeLibrary", (void*)hk_FreeLibrary, hmod);

    Iat_hook::detour_iat_ptr("GetProcAddress", (void*)hk_GetProcAddress, hmod);
}

void HookImportedModules()
{
    HMODULE hModule;
    HMODULE hm;

    hModule = GetModuleHandle(0);

    PIMAGE_DOS_HEADER img_dos_headers = (PIMAGE_DOS_HEADER)hModule;
    PIMAGE_NT_HEADERS img_nt_headers = (PIMAGE_NT_HEADERS)((BYTE*)img_dos_headers + img_dos_headers->e_lfanew);
    PIMAGE_IMPORT_DESCRIPTOR img_import_desc = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)img_dos_headers + img_nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    if (img_dos_headers->e_magic != IMAGE_DOS_SIGNATURE)
        return;

    for (IMAGE_IMPORT_DESCRIPTOR* iid = img_import_desc; iid->Name != 0; iid++) {
        char* mod_name = (char*)((size_t*)(iid->Name + (size_t)hModule));
        hm = GetModuleHandleA(mod_name);
        // ual check
        if (hm && !(GetProcAddress(hm, "DirectInput8Create") != NULL && GetProcAddress(hm, "DirectSoundCreate8") != NULL && GetProcAddress(hm, "InternetOpenA") != NULL)) {
            HookModule(hm);
        }
    }
}

DWORD WINAPI InitThread(LPVOID)
{
    LogLine("[INIT] InitThread ENTER");

    // Small delay is optional. If things are stable without it, remove it.
    // Sleep(200);

    // Install DirectShow hook once.
    DirectShowHook::Install();

    // bSkipMovies should already be set in DllMain after reading the config.ini
    DirectShowHook::SetSkipMovies(bSkipMovies != 0);
    //AviFfmpegHook::Install();

    LogLine("[INIT] SkipMovies=%d", (int)(bSkipMovies != 0));
    LogLine("[INIT] InitThread EXIT");
    return 0;
}

// -----------------------------------------------------------------
// DllMain: Simplified for d3d8to9 Stability
// -----------------------------------------------------------------
bool WINAPI DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
    static bool bInitialized = false;

    switch (dwReason)
    {
    case DLL_PROCESS_ATTACH:
    {
        if (bInitialized) return TRUE;
        bInitialized = true;
        g_hWrapperModule = hModule;

        // --- Resident Evil Survivor Fixes ---
        InstallCDBypass();
        InstallDInputHook();

        // --- Config Reading ---
        char path[MAX_PATH]{};
        GetModuleFileNameA(hModule, path, sizeof(path));
        char* lastSlash = strrchr(path, '\\');
        if (lastSlash) strcpy(lastSlash, "\\config.ini");

        // Load Settings
        // --- GRAPHICS ---
        fFPSLimit = static_cast<float>(GetPrivateProfileInt("Graphics", "FPSLimit", 0, path));
        nFullScreenRefreshRateInHz = GetPrivateProfileInt("Graphics", "FullScreenRefreshRateInHz", 0, path);
        ForceWidth = GetPrivateProfileInt("Graphics", "ForceWidth", 0, path);
        ForceHeight = GetPrivateProfileInt("Graphics", "ForceHeight", 0, path);
        bDisplayFPSCounter = GetPrivateProfileInt("Graphics", "DisplayFPSCounter", 0, path) != 0;

        // --- WINDOW ---
        bForceWindowedMode = GetPrivateProfileInt("Window", "ForceWindowedMode", 0, path) != 0;
        bBorderlessFullscreen = GetPrivateProfileInt("Window", "BorderlessFullscreen", 0, path) != 0;
        bCenterWindow = GetPrivateProfileInt("Window", "CenterWindow", 1, path) != 0;
        bAlwaysOnTop = GetPrivateProfileInt("Window", "AlwaysOnTop", 0, path) != 0;
        bLockCursorToWindow = GetPrivateProfileInt("Window", "LockCursorToWindow", 0, path) != 0;
        bUsePrimaryMonitor = GetPrivateProfileInt("Window", "UsePrimaryMonitor", 0, path) != 0;
        //bDoNotNotifyOnTaskSwitch = GetPrivateProfileInt("Window", "DoNotNotifyOnTaskSwitch", 0, path) != 0;

        // --- FIXES ---
        //bAlphaFix = GetPrivateProfileInt("Fixes", "AlphaFix", 0, path) != 0;
        bSkipMovies = GetPrivateProfileInt("Fixes", "SkipMovies", 0, path) != 0;
        //bDirect3D8DisableMaximizedWindowedModeShim = GetPrivateProfileInt("Fixes", "Direct3D8DisableMaximizedWindowedModeShim", 0, path) != 0;

        // --- DEBUG ---
        gEnableLogging = ReadIniIntWithFallback(path, "Application", "EnableLogging", "Debug", "EnableLogging", 0) != 0;

        // --- SURVIVOR FIX (survivor_pc.cpp, survivor_fix/) ---
        strcpy(gConfigPath, path);
        SurvivorPc_LoadConfig(path);
        SurvivorPc_ApplyDpiAware();//before the game creates its window
        SurvivorFix_Attach(hModule, path);//start-up fixes, before the game's code runs

        // Initialize Limiter
        if (fFPSLimit > 0.0f) {
            FrameLimiter::Init(FrameLimiter::FPS_REALTIME);
            mFPSLimitMode = FrameLimiter::FPS_REALTIME;
        }

        // --- Start Movie/DirectShow Thread ---
        //HANDLE h = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        //if (h) CloseHandle(h);

        break;
    }
    case DLL_PROCESS_DETACH:
        ClipCursor(NULL);
        break;
    }
    return TRUE;
}

/*
bool WINAPI DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
    static bool bInitialized = false; // Guard variable

    switch (dwReason)
    {
    case DLL_PROCESS_ATTACH:
    {
        if (bInitialized) return TRUE;
        bInitialized = true;

        g_hWrapperModule = hModule; // Store the DLL handle

        // ---- Load real system d3d8.dll FIRST (critical) ----
        char path[MAX_PATH]{};
        GetSystemDirectoryA(path, MAX_PATH);
        strcat_s(path, "\\d3d8.dll");

        d3d8dll = LoadLibraryA(path);
        LogLine("[DLL] LoadLibraryA(%s) -> %p err=%lu", path, d3d8dll, GetLastError());
        if (!d3d8dll)
            return FALSE;

        // ---- Resolve exports (at least Direct3DCreate8 must exist) ----
        m_pDirect3D8EnableMaximizedWindowedModeShim =
            (Direct3D8EnableMaximizedWindowedModeShimProc)GetProcAddress(d3d8dll, "Direct3D8EnableMaximizedWindowedModeShim");
        m_pValidatePixelShader =
            (ValidatePixelShaderProc)GetProcAddress(d3d8dll, "ValidatePixelShader");
        m_pValidateVertexShader =
            (ValidateVertexShaderProc)GetProcAddress(d3d8dll, "ValidateVertexShader");
        m_pDebugSetMute =
            (DebugSetMuteProc)GetProcAddress(d3d8dll, "DebugSetMute");
        m_pDirect3DCreate8 =
            (Direct3DCreate8Proc)GetProcAddress(d3d8dll, "Direct3DCreate8");

        LogLine("[DLL] GetProcAddress(Direct3DCreate8) -> %p", (void*)m_pDirect3DCreate8);
        if (!m_pDirect3DCreate8)
            return FALSE;

        // ---- Resident Evil Survivor Fixes (safe here) ----
        InstallCDBypass();
        InstallDInputHook();
        // AviFfmpegHook::Install();   // DO NOT call here; call from InitThread

        // ---- INI path ----
        HMODULE hm = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&Direct3DCreate8, &hm);

        GetModuleFileNameA(hm, path, sizeof(path));
        char* lastSlash = strrchr(path, '\\');
        if (lastSlash) strcpy(lastSlash, "\\config.ini");

        // --- GRAPHICS ---
        fFPSLimit = static_cast<float>(GetPrivateProfileInt("Graphics", "FPSLimit", 0, path));
        nFullScreenRefreshRateInHz = GetPrivateProfileInt("Graphics", "FullScreenRefreshRateInHz", 0, path);
        ForceWidth = GetPrivateProfileInt("Graphics", "ForceWidth", 0, path);
        ForceHeight = GetPrivateProfileInt("Graphics", "ForceHeight", 0, path);
        bDisplayFPSCounter = GetPrivateProfileInt("Graphics", "DisplayFPSCounter", 0, path) != 0;

        // --- WINDOW ---
        bForceWindowedMode = GetPrivateProfileInt("Window", "ForceWindowedMode", 0, path) != 0;
        bBorderlessFullscreen = GetPrivateProfileInt("Window", "BorderlessFullscreen", 0, path) != 0;
        bCenterWindow = GetPrivateProfileInt("Window", "CenterWindow", 1, path) != 0;
        bAlwaysOnTop = GetPrivateProfileInt("Window", "AlwaysOnTop", 0, path) != 0;
        bLockCursorToWindow = GetPrivateProfileInt("Window", "LockCursorToWindow", 0, path) != 0;
        bUsePrimaryMonitor = GetPrivateProfileInt("Window", "UsePrimaryMonitor", 0, path) != 0;
        bDoNotNotifyOnTaskSwitch = GetPrivateProfileInt("Window", "DoNotNotifyOnTaskSwitch", 0, path) != 0;

        // --- FIXES ---
        bAlphaFix = GetPrivateProfileInt("Fixes", "AlphaFix", 0, path) != 0;
        bSkipMovies = GetPrivateProfileInt("Fixes", "SkipMovies", 0, path) != 0;
        bDirect3D8DisableMaximizedWindowedModeShim = GetPrivateProfileInt("Fixes", "Direct3D8DisableMaximizedWindowedModeShim", 0, path) != 0;

        // --- DEBUG ---
        gEnableLogging = ReadIniIntWithFallback(path, "Application", "EnableLogging", "Debug", "EnableLogging", 0) != 0;

        // ---- FPS limiter ----
        if (fFPSLimit > 0.0f)
        {
            FrameLimiter::FPSLimitMode mode =
                (GetPrivateProfileInt("MAIN", "FPSLimitMode", 1, path) == 2)
                ? FrameLimiter::FPSLimitMode::FPS_ACCURATE
                : FrameLimiter::FPSLimitMode::FPS_REALTIME;

            if (mode == FrameLimiter::FPSLimitMode::FPS_ACCURATE)
                timeBeginPeriod(1);

            FrameLimiter::Init(mode);
            mFPSLimitMode = mode;
        }
        else
        {
            mFPSLimitMode = FrameLimiter::FPSLimitMode::FPS_NONE;
        }

        // ---- Disable maximized windowed mode shim ----
        if (bDirect3D8DisableMaximizedWindowedModeShim)
        {
            auto addr = (uintptr_t)GetProcAddress(d3d8dll, "Direct3D8EnableMaximizedWindowedModeShim");
            if (addr)
            {
                DWORD Protect;
                VirtualProtect((LPVOID)(addr + 6), 4, PAGE_EXECUTE_READWRITE, &Protect);
                *(unsigned*)(addr + 6) = 0;
                *(unsigned*)(*(unsigned*)(addr + 2)) = 0;
                VirtualProtect((LPVOID)(addr + 6), 4, Protect, &Protect);
                bForceWindowedMode = false;
            }
        }

        // ---- Task switch notify hooks ----
        if (bDoNotNotifyOnTaskSwitch)
        {
            GetSystemWindowsDirectoryA(WinDir, MAX_PATH);

            oRegisterClassA = (RegisterClassA_fn)Iat_hook::detour_iat_ptr("RegisterClassA", (void*)hk_RegisterClassA);
            oRegisterClassW = (RegisterClassW_fn)Iat_hook::detour_iat_ptr("RegisterClassW", (void*)hk_RegisterClassW);
            oRegisterClassExA = (RegisterClassExA_fn)Iat_hook::detour_iat_ptr("RegisterClassExA", (void*)hk_RegisterClassExA);
            oRegisterClassExW = (RegisterClassExW_fn)Iat_hook::detour_iat_ptr("RegisterClassExW", (void*)hk_RegisterClassExW);
            oGetForegroundWindow = (GetForegroundWindow_fn)Iat_hook::detour_iat_ptr("GetForegroundWindow", (void*)hk_GetForegroundWindow);
            oGetActiveWindow = (GetActiveWindow_fn)Iat_hook::detour_iat_ptr("GetActiveWindow", (void*)hk_GetActiveWindow);
            oGetFocus = (GetFocus_fn)Iat_hook::detour_iat_ptr("GetFocus", (void*)hk_GetFocus);
            oLoadLibraryA = (LoadLibraryA_fn)Iat_hook::detour_iat_ptr("LoadLibraryA", (void*)hk_LoadLibraryA);
            oLoadLibraryW = (LoadLibraryW_fn)Iat_hook::detour_iat_ptr("LoadLibraryW", (void*)hk_LoadLibraryW);
            oLoadLibraryExA = (LoadLibraryExA_fn)Iat_hook::detour_iat_ptr("LoadLibraryExA", (void*)hk_LoadLibraryExA);
            oLoadLibraryExW = (LoadLibraryExW_fn)Iat_hook::detour_iat_ptr("LoadLibraryExW", (void*)hk_LoadLibraryExW);
            oFreeLibrary = (FreeLibrary_fn)Iat_hook::detour_iat_ptr("FreeLibrary", (void*)hk_FreeLibrary);

            Iat_hook::detour_iat_ptr("GetProcAddress", (void*)hk_GetProcAddress);
            Iat_hook::detour_iat_ptr("GetProcAddress", (void*)hk_GetProcAddress, d3d8dll);

            if (oGetForegroundWindow == NULL)
                oGetForegroundWindow = (GetForegroundWindow_fn)Iat_hook::detour_iat_ptr("GetForegroundWindow", (void*)hk_GetForegroundWindow, d3d8dll);
            else
                Iat_hook::detour_iat_ptr("GetForegroundWindow", (void*)hk_GetForegroundWindow, d3d8dll);

            HMODULE ole32 = GetModuleHandleA("ole32.dll");
            if (ole32)
            {
                if (oRegisterClassA == NULL)
                    oRegisterClassA = (RegisterClassA_fn)Iat_hook::detour_iat_ptr("RegisterClassA", (void*)hk_RegisterClassA, ole32);
                else
                    Iat_hook::detour_iat_ptr("RegisterClassA", (void*)hk_RegisterClassA, ole32);

                if (oRegisterClassW == NULL)
                    oRegisterClassW = (RegisterClassW_fn)Iat_hook::detour_iat_ptr("RegisterClassW", (void*)hk_RegisterClassW, ole32);
                else
                    Iat_hook::detour_iat_ptr("RegisterClassW", (void*)hk_RegisterClassW, ole32);

                if (oRegisterClassExA == NULL)
                    oRegisterClassExA = (RegisterClassExA_fn)Iat_hook::detour_iat_ptr("RegisterClassExA", (void*)hk_RegisterClassExA, ole32);
                else
                    Iat_hook::detour_iat_ptr("RegisterClassExA", (void*)hk_RegisterClassExA, ole32);

                if (oRegisterClassExW == NULL)
                    oRegisterClassExW = (RegisterClassExW_fn)Iat_hook::detour_iat_ptr("RegisterClassExW", (void*)hk_RegisterClassExW, ole32);
                else
                    Iat_hook::detour_iat_ptr("RegisterClassExW", (void*)hk_RegisterClassExW, ole32);

                if (oGetActiveWindow == NULL)
                    oGetActiveWindow = (GetActiveWindow_fn)Iat_hook::detour_iat_ptr("GetActiveWindow", (void*)hk_GetActiveWindow, ole32);
                else
                    Iat_hook::detour_iat_ptr("GetActiveWindow", (void*)hk_GetActiveWindow, ole32);
            }

            HookImportedModules();
        }

        // ---- Start InitThread LAST (after real d3d8 is ready) ----
        HANDLE h = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        LogLine("[DLL] CreateThread InitThread -> %p err=%lu", h, GetLastError());
        if (h) CloseHandle(h);

        break;
    }

    case DLL_PROCESS_DETACH:
    {
        ClipCursor(NULL);

        if (mFPSLimitMode == FrameLimiter::FPSLimitMode::FPS_ACCURATE)
            timeEndPeriod(1);

        if (d3d8dll)
            FreeLibrary(d3d8dll);

        break;
    }
    }

    return TRUE;
}*/

int WINAPI Direct3D8EnableMaximizedWindowedModeShim(BOOL mEnable)
{
    if (!m_pDirect3D8EnableMaximizedWindowedModeShim)
    {
        return E_FAIL;
    }

    return m_pDirect3D8EnableMaximizedWindowedModeShim(mEnable);
}

HRESULT WINAPI ValidatePixelShader(DWORD* pixelshader, DWORD* reserved1, BOOL flag, DWORD* toto)
{
    if (!m_pValidatePixelShader)
    {
        return E_FAIL;
    }

    return m_pValidatePixelShader(pixelshader, reserved1, flag, toto);
}

HRESULT WINAPI ValidateVertexShader(DWORD* vertexshader, DWORD* reserved1, DWORD* reserved2, BOOL flag, DWORD* toto)
{
    if (!m_pValidateVertexShader)
    {
        return E_FAIL;
    }

    return m_pValidateVertexShader(vertexshader, reserved1, reserved2, flag, toto);
}

void WINAPI DebugSetMute()
{
    if (!m_pDebugSetMute)
    {
        return;
    }

    return m_pDebugSetMute();
}

// -----------------------------------------------------------------
// Direct3DCreate8: The Entry Point
// -----------------------------------------------------------------
IDirect3D8* WINAPI Direct3DCreate8(UINT SDKVersion)
{
    // The game's original MISC sheet contains the broken crosshair.  Preserve
    // user-supplied replacements, but restore our masked sheet when a package
    // has accidentally omitted the loose FIXRES file.
    static bool restoredEmbeddedAssets = false;
    if (!restoredEmbeddedAssets) {
        RestoreEmbeddedMiscIfMissing();
        restoredEmbeddedAssets = true;
    }
    // 1. Config Window (Only once)
    static bool bConfigShown = false;
    if (!bConfigShown) {
        ShowConfigWindow(g_hWrapperModule);
        bConfigShown = true;
        // Survivor Fix: the options as saved by the window
        SurvivorPc_LoadConfig(gConfigPath);
        SurvivorPc_InstallGameHooks();//HudAspect: game function 0042ABE0
        SurvivorFix_Start(gConfigPath, gEnableLogging);//PS1 behaviour: zombies, player, camera, death, doors
    }

    LogLine("Direct3DCreate8 called");

    // Call it here! This is safe because the loader lock is usually released 
    // by the time the game calls an export.
    static bool hooked = false;
    if (!hooked) {
        DirectShowHook::Install();
        DirectShowHook::SetSkipMovies(bSkipMovies != 0);
        AviFfmpegHook::Install();
        hooked = true;
    }

    // 2. Create the translation layer interface (d3d8to9)
    // This function is provided by your d3d8to9.cpp file.
    IDirect3D8* pD3D8to9 = Direct3D8_Create(SDKVersion);

    if (pD3D8to9) {
        // 3. Wrap the translated device in your proxy class
        // This enables your custom EndScene/Crosshair logic.
        return new m_IDirect3D8(pD3D8to9);
    }

    // This is a common fix for "Not Responding" hangs in wrappers
    if (pD3D8to9 != nullptr) {
        DisableProcessWindowsGhosting();
    }

    return nullptr;
}

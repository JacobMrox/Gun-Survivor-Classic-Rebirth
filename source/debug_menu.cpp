// RE2-style debug menu overlay (F2/F10). Called from dllmain EndScene.
// State (open/selected/row) lives in quick_save.cpp; this file only draws.
#include <windows.h>
#include <windowsx.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include "d3d8types.hpp"
#include "d3dx8tex.h"
#include "debug_menu.h"
#include "survivor_fix/features/features.h"

extern HMODULE g_hWrapperModule;

namespace {

// Copied from dllmain.cpp (native FONT000.BMP glyphs).
int NativeFontGlyph(char ch) {
    if (ch >= '0' && ch <= '9') return 12 + (ch - '0');
    if (ch >= 'A' && ch <= 'Z') return 29 + (ch - 'A');
    if (ch >= 'a' && ch <= 'z') return 61 + (ch - 'a');
    switch (ch) {
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

struct FontVertex8 { float x, y, z, rhw; DWORD color; float u, v; };

IDirect3DTexture8* g_debugFontTex = nullptr;

void EnsureDebugFont(IDirect3DDevice8* dev) {
    if (g_debugFontTex || !dev) return;
    char path[MAX_PATH] = "Resource\\FONT000.bmp";
    HRESULT hr = D3DXCreateTextureFromFileExA(dev, path, D3DX_DEFAULT, D3DX_DEFAULT, 1, 0,
        D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, D3DX_FILTER_POINT, D3DX_FILTER_POINT,
        D3DCOLOR_ARGB(255, 0, 0, 0), nullptr, nullptr, &g_debugFontTex);
    if (SUCCEEDED(hr)) return;
    if (GetModuleFileNameA(g_hWrapperModule, path, MAX_PATH)) {
        char* s = strrchr(path, '\\');
        if (s) {
            strcpy_s(s + 1, MAX_PATH - static_cast<size_t>(s + 1 - path), "Resource\\FONT000.bmp");
            D3DXCreateTextureFromFileExA(dev, path, D3DX_DEFAULT, D3DX_DEFAULT, 1, 0,
                D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, D3DX_FILTER_POINT, D3DX_FILTER_POINT,
                D3DCOLOR_ARGB(255, 0, 0, 0), nullptr, nullptr, &g_debugFontTex);
        }
    }
}

void DrawGlyph(IDirect3DDevice8* dev, int glyph, float x, float y, float w, float h,
               UINT tw, UINT th, DWORD color) {
    if (glyph < 0) return;
    constexpr float cell = 14.0f;
    constexpr int columns = 18;
    const float left = static_cast<float>((glyph % columns) * 14);
    const float top = static_cast<float>((glyph / columns) * 14);
    const float u0 = (left + 0.5f) / tw, v0 = (top + 0.5f) / th;
    const float u1 = (left + cell - 0.5f) / tw, v1 = (top + cell - 0.5f) / th;
    FontVertex8 v[4] = {
        {x, y, 0.0f, 1.0f, color, u0, v0}, {x + w, y, 0.0f, 1.0f, color, u1, v0},
        {x, y + h, 0.0f, 1.0f, color, u0, v1}, {x + w, y + h, 0.0f, 1.0f, color, u1, v1},
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(FontVertex8));
}

float BackBufferXCompensation(UINT w, UINT h) {
    const float a = (h > 0) ? (static_cast<float>(w) / static_cast<float>(h)) / (4.0f / 3.0f) : 1.0f;
    return (std::max)(0.7f, (std::min)(1.6f, a));
}

}  // namespace

void DebugMenu_Draw(IDirect3DDevice8* dev) {
    if (!dev || !sf::features::DebugMenuVisible()) return;
    EnsureDebugFont(dev);
    if (!g_debugFontTex) return;
    IDirect3DSurface8* back = nullptr;
    D3DSURFACE_DESC8 backDesc = {}, fontDesc = {};
    if (FAILED(dev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &back)) || !back) return;
    HRESULT br = back->GetDesc(&backDesc);
    back->Release();
    if (FAILED(br) || FAILED(g_debugFontTex->GetLevelDesc(0, &fontDesc))) return;

    const float scale = std::clamp(backDesc.Height / 720.0f, 0.9f, 1.8f);
    const float xComp = BackBufferXCompensation(backDesc.Width, backDesc.Height);
    const float glyphH = 14.0f * scale, glyphW = glyphH * xComp;
    const float marginY = 18.0f * scale, marginX = marginY * xComp;
    const float boxW = 430.0f * scale, boxH = 300.0f * scale;
    const float boxX = (static_cast<float>(backDesc.Width) - boxW) * 0.5f;
    const float boxY = (static_cast<float>(backDesc.Height) - boxH) * 0.32f;

    DWORD state = 0;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &state))) return;
    const DWORD black = D3DCOLOR_ARGB(220, 0, 0, 0);
    FontVertex8 box[4] = {
        {boxX, boxY, 0, 1, black, 0, 0}, {boxX + boxW, boxY, 0, 1, black, 0, 0},
        {boxX, boxY + boxH, 0, 1, black, 0, 0}, {boxX + boxW, boxY + boxH, 0, 1, black, 0, 0},
    };
    dev->SetTexture(0, nullptr);
    dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, box, sizeof(FontVertex8));
    dev->SetTexture(0, g_debugFontTex);

    auto text = [&](const char* value, float x, float y, DWORD color) {
        for (size_t i = 0; value[i]; ++i) {
            int g = NativeFontGlyph(value[i]);
            if (g >= 0) DrawGlyph(dev, g, x + glyphW * i, y, glyphW, glyphH, fontDesc.Width, fontDesc.Height, color);
        }
    };
    const DWORD cyan = D3DCOLOR_ARGB(255, 0, 255, 255);
    const DWORD grey = D3DCOLOR_ARGB(255, 90, 90, 90);
    const DWORD white = D3DCOLOR_ARGB(255, 255, 255, 255);
    const bool utility = sf::features::DebugMenuUtilityVisible();
    text(utility ? "- UTILITY MENU -" : "- DEBUG MENU -", boxX + marginX, boxY + marginY, cyan);
    char line[48] = {};
    auto roomName = [](int sc) -> const char* {
        switch (sc) {
            case 0x00: return "00/00 HELICOPTER CRASH";
            case 0x01: return "00/01 BACK ALLEY";
            case 0x02: return "00/02 CHURCH HALL";
            case 0x03: return "00/03 BACK ALLEY B";
            case 0x04: return "00/04 CHURCH CORRIDOR";
            case 0x05: return "00/05 CHURCH PLAYROOM";
            case 0x06: return "00/06 CHURCH BASEMENT";
            case 0x07: return "00/07 CHURCH STOREROOM";
            case 0x08: return "00/08 FACTORY OFFICE";
            case 0x09: return "00/09 VIEWING ROOM";
            case 0x0A: return "00/0A CROW HALLWAY";
            case 0x0B: return "00/0B PROJECTOR ROOM";
            case 0x0C: return "00/0C KITCHEN";
            case 0x0D: return "00/0D WINE CELLAR";
            case 0x0E: return "00/0E FREEZER";
            case 0x0F: return "00/0F COAST ROAD B";
            case 0x10: return "00/10 SHOPPING STREET A";
            case 0x11: return "00/11 LOWER BRIDGE PROMENADE";
			case 0x12: return "00/12 SHOPPING STREET B";
            case 0x1D: return "0E/01 SEWERS";
            case 0x1E: return "07/00 HELICOPTER CRASH";
            case 0x1F: return "0C/00 OUTSKIRTS";
            case 0x20: return "0C/01 OUTSKIRTS B";
            case 0x23: return "0C/03 OUTSKIRTS ROAD";
            case 0x24: return "0C/01 OUTSKIRTS C";
            case 0x35: return "03/08 CHURCH BASEMENT";
            case 0x38: return "03/0A CHURCH WAKE";
            case 0x4A: return "09/02 PROJECTOR ROOM";
            case 0x54: return "0B/00 HOSPITAL";
            case 0x73: return "02/0F RESTAURANT KITCHEN";
            case 0x74: return "11/00 UMBRELLA HQ";
            case 0x75: return "02/0E RESTAURANT";
            case 0x77: return "02/0E RESTAURANT B";
            case 0x79: return "HELIPORT FINAL BOSS";
            default: return "ROOM";
        }
    };
    const int sel = sf::features::DebugMenuSelected();
    const int row = sf::features::DebugMenuRow();
    const int cur = sf::features::DebugMenuCurrentScene();
    if (utility) {
        const char* options[] = {"QUICK SAVE", "QUICK LOAD", "SAVE GAME", "LOAD GAME", "INFINITE AMMO", "INFINITE HEALTH"};
        for (int i = 0; i < 6; ++i) {
            const char* status = "";
            if (i >= 4) status = !sf::features::DebugUtilityCheatAvailable(i) ? " NOT READY" :
                sf::features::DebugUtilityCheatEnabled(i) ? " ON" : " OFF";
            snprintf(line, sizeof(line), "%c %s%s", row == i ? '-' : ' ', options[i], status);
            text(line, boxX + marginX, boxY + marginY + glyphH * (2 + i), row == i ? cyan : grey);
        }
    } else {
        snprintf(line, sizeof(line), "%c JUMP %02X %-16s", (row == 0) ? '-' : ' ', sel, roomName(sel));
        text(line, boxX + marginX, boxY + marginY + glyphH * 2, (row == 0) ? cyan : grey);
        const char* dummies[] = {"UTILITY MENU", "MEMORY OVERWRITE", "XA PLAY TEST", "CP MAP HIR", "SHORT CHECK DISP", "MONOSASHI"};
        for (int i = 0; i < 6; ++i) {
            snprintf(line, sizeof(line), "%c %s", ((row == i + 1) ? '-' : ' '), dummies[i]);
            text(line, boxX + marginX, boxY + marginY + glyphH * (3 + i), ((row == i + 1) ? cyan : grey));
        }
    }
    snprintf(line, sizeof(line), "CUR %02X %-16s", cur, roomName(cur));
    text(line, boxX + marginX, boxY + marginY + glyphH * 10, white);
    static DWORD t0 = GetTickCount();
    const DWORD el = GetTickCount() - t0;
    snprintf(line, sizeof(line), "%02lu:%02lu:%02lu", (el / 3600000) % 24, (el / 60000) % 60, (el / 1000) % 60);
    text(line, boxX + boxW - glyphW * 10, boxY + boxH - glyphH * 2, D3DCOLOR_ARGB(255, 255, 0, 255));
    snprintf(line, sizeof(line), "UP DOWN LEFT RIGHT ENTER ESC F2");
    text(line, boxX + marginX, boxY + marginY + glyphH * 11, white);
    dev->ApplyStateBlock(state);
    dev->DeleteStateBlock(state);
}

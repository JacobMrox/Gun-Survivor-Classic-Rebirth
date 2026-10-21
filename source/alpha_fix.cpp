// alpha_fix.cpp
#include <windows.h>
#include <d3d8.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <cstdint>

#include "debug.h"
#include "alpha_fix.h"

// If stb_image is already implemented elsewhere, do NOT define this here.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ---------------- tags driven by file opens ----------------
static volatile AlphaFixTag gNextTag = AFT_None;

static bool ends_with_i(const char* s, const char* suffix)
{
    if (!s || !suffix) return false;
    size_t ls = strlen(s), lf = strlen(suffix);
    if (lf > ls) return false;
    return _stricmp(s + (ls - lf), suffix) == 0;
}

void AlphaFix_OnFileOpenA(const char* path)
{
    if (!path) return;

    if (ends_with_i(path, "shadow.bmp"))      gNextTag = AFT_Shadow;
    else if (ends_with_i(path, "bloodx1.bmp")) gNextTag = AFT_Blood1;
    else if (ends_with_i(path, "bloodx2.bmp")) gNextTag = AFT_Blood2;
}

void AlphaFix_OnFileOpenW(const wchar_t* /*path*/)
{
    // optional: only needed if the game uses CreateFileW for these assets
}

// ---------------- image cache ----------------
struct Img
{
    int w = 0, h = 0;
    std::vector<unsigned char> rgba; // 4 bytes per pixel
};

static const char* TagToPngPath(AlphaFixTag tag)
{
    switch (tag)
    {
    case AFT_Shadow: return "Resource\\shadow.png";
    case AFT_Blood1: return "Resource\\bloodx1.png";
    case AFT_Blood2: return "Resource\\bloodx2.png";
    default: return nullptr;
    }
}

static const Img* LoadCached(AlphaFixTag tag)
{
    static std::unordered_map<int, Img> cache;

    auto it = cache.find((int)tag);
    if (it != cache.end())
        return &it->second;

    const char* path = TagToPngPath(tag);
    if (!path) return nullptr;

    int w = 0, h = 0, comp = 0;
    unsigned char* pixels = stbi_load(path, &w, &h, &comp, 4);
    if (!pixels)
    {
        char buf[256];
        sprintf_s(buf, "[ALPHA] Failed to load %s", path);
        LogLine(buf);
        return nullptr;
    }

    Img img;
    img.w = w;
    img.h = h;
    img.rgba.assign(pixels, pixels + (w * h * 4));
    stbi_image_free(pixels);

    char buf[256];
    sprintf_s(buf, "[ALPHA] Loaded %s (%dx%d)", path, w, h);
    LogLine(buf);

    auto ins = cache.emplace((int)tag, std::move(img));
    return &ins.first->second;
}

// ---------------- tagging textures at creation time ----------------
void AlphaFix_OnTextureCreated(IDirect3DDevice8* /*dev*/, IDirect3DTexture8* tex)
{
    if (!tex) return;
    if (gNextTag == AFT_None) return;

    // We don't directly store tag on the texture here because your wrapper is storing alphaFixTag.
    // So this function is only useful if you want to validate formats/logs later.
    // Actual tag assignment should happen in your wrapper (m_IDirect3DTexture8) at creation time.
}

// ---------------- premultiplied alpha copy into A8R8G8B8 ----------------

static inline uint16_t PackA4R4G4B4_PM(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    // r,g,b expected premultiplied already (0..255)
    // a is straight alpha (0..255)
    uint16_t A = (uint16_t)(a >> 4);
    uint16_t R = (uint16_t)(r >> 4);
    uint16_t G = (uint16_t)(g >> 4);
    uint16_t B = (uint16_t)(b >> 4);
    return (uint16_t)((A << 12) | (R << 8) | (G << 4) | (B));
}

static inline unsigned short PackA4R4G4B4(unsigned char a, unsigned char r, unsigned char g, unsigned char b)
{
    // a,r,g,b are already premultiplied 0..255
    unsigned short A = (unsigned short)(a >> 4);
    unsigned short R = (unsigned short)(r >> 4);
    unsigned short G = (unsigned short)(g >> 4);
    unsigned short B = (unsigned short)(b >> 4);
    return (unsigned short)((A << 12) | (R << 8) | (G << 4) | (B));
}

void AlphaFix_ApplyPNGToTexture(IDirect3DTexture8* tex, AlphaFixTag tag)
{
    if (!tex || tag == AFT_None) return;

    const Img* img = LoadCached(tag);
    if (!img) return;

    D3DSURFACE_DESC8 desc{};
    if (FAILED(tex->GetLevelDesc(0, &desc)))
        return;

    D3DLOCKED_RECT lr{};
    if (FAILED(tex->LockRect(0, &lr, nullptr, 0)))
        return;

    const int copyW = (img->w < (int)desc.Width) ? img->w : (int)desc.Width;
    const int copyH = (img->h < (int)desc.Height) ? img->h : (int)desc.Height;

    for (int y = 0; y < copyH; y++)
    {
        const unsigned char* src = &img->rgba[(y * img->w) * 4];

        if (desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8)
        {
            unsigned int* dst = (unsigned int*)((unsigned char*)lr.pBits + y * lr.Pitch);

            for (int x = 0; x < copyW; x++)
            {
                unsigned char srcR = src[x * 4 + 0];
                unsigned char srcG = src[x * 4 + 1];
                unsigned char srcB = src[x * 4 + 2];
                unsigned char srcA = src[x * 4 + 3];

                unsigned char r = (unsigned char)((srcR * srcA + 127) / 255);
                unsigned char g = (unsigned char)((srcG * srcA + 127) / 255);
                unsigned char b = (unsigned char)((srcB * srcA + 127) / 255);

                dst[x] = (srcA << 24) | (r << 16) | (g << 8) | (b);
            }
        }
        else if (desc.Format == D3DFMT_A4R4G4B4)
        {
            unsigned short* dst = (unsigned short*)((unsigned char*)lr.pBits + y * lr.Pitch);

            for (int x = 0; x < copyW; x++)
            {
                unsigned char srcR = src[x * 4 + 0];
                unsigned char srcG = src[x * 4 + 1];
                unsigned char srcB = src[x * 4 + 2];
                unsigned char srcA = src[x * 4 + 3];

                unsigned char r = (unsigned char)((srcR * srcA + 127) / 255);
                unsigned char g = (unsigned char)((srcG * srcA + 127) / 255);
                unsigned char b = (unsigned char)((srcB * srcA + 127) / 255);

                dst[x] = PackA4R4G4B4(srcA, r, g, b);
            }
        }
    }

    tex->UnlockRect(0);
    LogLine("[ALPHA] Applied PNG -> texture (premultiplied)");
}

// ---------------- premult blend state helpers ----------------
void ApplyAlphaFixBlendState(IDirect3DDevice8* dev, DWORD& oldAlpha, DWORD& oldSrc, DWORD& oldDst, DWORD& oldATest)
{
    dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlpha);
    dev->GetRenderState(D3DRS_SRCBLEND, &oldSrc);
    dev->GetRenderState(D3DRS_DESTBLEND, &oldDst);
    dev->GetRenderState(D3DRS_ALPHATESTENABLE, &oldATest);

    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);

    // Premultiplied alpha: ONE / INVSRCALPHA
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    // avoid hard edge ¡§alpha test¡¨ behavior
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
}

void RestoreAlphaFixBlendState(IDirect3DDevice8* dev, DWORD oldAlpha, DWORD oldSrc, DWORD oldDst, DWORD oldATest)
{
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlpha);
    dev->SetRenderState(D3DRS_SRCBLEND, oldSrc);
    dev->SetRenderState(D3DRS_DESTBLEND, oldDst);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, oldATest);
}

//static volatile AlphaFixTag gNextTag = AFT_None;

AlphaFixTag AlphaFix_PeekNextTag()
{
    return (AlphaFixTag)gNextTag;
}

AlphaFixTag AlphaFix_ConsumeNextTag()
{
    AlphaFixTag t = (AlphaFixTag)gNextTag;
    gNextTag = AFT_None;
    return t;
}

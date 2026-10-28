#include "ConfigWindow.h"
#include "resource.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <commdlg.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <memory>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

using namespace Gdiplus;

// External variables from dllmain.cpp
extern bool bForceWindowedMode, bDirect3D8DisableMaximizedWindowedModeShim, bUsePrimaryMonitor;
extern bool bCenterWindow, bBorderlessFullscreen, bAlwaysOnTop, bDisplayFPSCounter;
extern bool bLockCursorToWindow, bSkipMovies, gEnableLogging;
extern int ForceWidth, ForceHeight, nFullScreenRefreshRateInHz;
extern float fFPSLimit;
extern std::string sCrosshairPath;
extern float fCrosshairScale;
extern bool bLaserTrail;
extern COLORREF gLaserColor;
extern HMODULE g_hWrapperModule;

static int mouseSensitivityPercent = 100;
static bool keepAspect = true;
static bool smoothModelLighting = true;
static bool lethalZombieHeadshots = false;
static bool detachedZombieHeads = true;
static COLORREF bloodColor = RGB(255, 255, 255);

static char cfgPath[MAX_PATH];
bool bShowConfigOnLaunch = false;
ULONG_PTR gdiplusToken;
static bool gUseFluentDesign = false;
static WNDPROC gCrosshairPreviewProc = nullptr;
constexpr UINT WM_RENDER_CROSSHAIR_PREVIEW = WM_APP + 1;

const char* FluentComboText(HWND combo, UINT controlId, UINT itemId);

namespace {

constexpr COLORREF kSurfaceColor = RGB(32, 32, 32);
constexpr COLORREF kCardColor = RGB(40, 40, 40);
constexpr COLORREF kInputColor = RGB(49, 49, 49);
constexpr COLORREF kTextColor = RGB(245, 245, 245);
constexpr COLORREF kBorderColor = RGB(84, 84, 84);
constexpr COLORREF kAccentColor = RGB(0, 120, 212);

struct SectionCard {
    RECT rect;
    std::wstring title;
};

std::vector<SectionCard> sectionCards;

HBRUSH SurfaceBrush() {
    static HBRUSH brush = CreateSolidBrush(kSurfaceColor);
    return brush;
}

HBRUSH InputBrush() {
    static HBRUSH brush = CreateSolidBrush(kInputColor);
    return brush;
}

HBRUSH CardBrush() {
    static HBRUSH brush = CreateSolidBrush(kCardColor);
    return brush;
}

void CreateSectionCards(HWND h) {
    sectionCards.clear();
    for (HWND child = GetWindow(h, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        wchar_t className[32] = {};
        GetClassNameW(child, className, _countof(className));
        const LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
        if (wcscmp(className, L"Button") != 0 || (style & BS_GROUPBOX) != BS_GROUPBOX) continue;

        SectionCard card = {};
        GetWindowRect(child, &card.rect);
        MapWindowPoints(nullptr, h, reinterpret_cast<POINT*>(&card.rect), 2);
        wchar_t title[64] = {};
        GetWindowTextW(child, title, _countof(title));
        card.title = title;
        sectionCards.push_back(card);
        ShowWindow(child, SW_HIDE);
    }
}

void PaintSectionCards(HWND h, HDC dc) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kTextColor);
    HPEN border = CreatePen(PS_SOLID, 1, kBorderColor);
    HGDIOBJ oldPen = SelectObject(dc, border);
    HGDIOBJ oldBrush = SelectObject(dc, CardBrush());
    for (const SectionCard& card : sectionCards) {
        RoundRect(dc, card.rect.left, card.rect.top, card.rect.right, card.rect.bottom, 10, 10);
        RECT titleRect = { card.rect.left + 12, card.rect.top + 10, card.rect.right - 12, card.rect.top + 28 };
        DrawTextW(dc, card.title.c_str(), -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(border);
}

bool IsFluentCombo(int id) {
    return id == IDC_RES_COMBO || id == IDC_FPS_COMBO || id == IDC_CROSS_COMBO;
}

void DrawFluentCombo(const DRAWITEMSTRUCT& item) {
    HDC dc = item.hDC;
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    const bool focused = (item.itemState & ODS_FOCUS) != 0;
    const COLORREF background = selected ? kAccentColor : kInputColor;
    HBRUSH fill = CreateSolidBrush(background);
    FillRect(dc, &item.rcItem, fill);
    DeleteObject(fill);

    HPEN border = CreatePen(PS_SOLID, 1, focused ? kAccentColor : kBorderColor);
    HGDIOBJ oldPen = SelectObject(dc, border);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(border);

    const char* text = FluentComboText(item.hwndItem, item.CtlID, item.itemID);
    RECT textRect = item.rcItem;
    textRect.left += 9;
    textRect.right -= 24;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kTextColor);
    DrawTextA(dc, text, -1, &textRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    if ((item.itemState & ODS_COMBOBOXEDIT) != 0) {
        const int x = item.rcItem.right - 14;
        const int y = (item.rcItem.top + item.rcItem.bottom) / 2 - 2;
        POINT arrow[] = { { x - 4, y - 1 }, { x + 4, y - 1 }, { x, y + 4 } };
        HBRUSH arrowBrush = CreateSolidBrush(kTextColor);
        HGDIOBJ oldArrowBrush = SelectObject(dc, arrowBrush);
        Polygon(dc, arrow, _countof(arrow));
        SelectObject(dc, oldArrowBrush);
        DeleteObject(arrowBrush);
    }
}

void DrawFluentButton(const DRAWITEMSTRUCT& item) {
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool focused = (item.itemState & ODS_FOCUS) != 0;
    const COLORREF fillColor = pressed ? RGB(0, 92, 164) : (focused ? kAccentColor : kInputColor);
    HBRUSH fill = CreateSolidBrush(fillColor);
    HPEN border = CreatePen(PS_SOLID, 1, focused ? RGB(95, 184, 255) : kBorderColor);
    HGDIOBJ oldBrush = SelectObject(item.hDC, fill);
    HGDIOBJ oldPen = SelectObject(item.hDC, border);
    RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom, 8, 8);
    SelectObject(item.hDC, oldPen);
    SelectObject(item.hDC, oldBrush);
    DeleteObject(border);
    DeleteObject(fill);

    wchar_t caption[64] = {};
    GetWindowTextW(item.hwndItem, caption, _countof(caption));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, kTextColor);
    DrawTextW(item.hDC, caption, -1, const_cast<RECT*>(&item.rcItem), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

bool IsWindows11OrLater() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOEXW*);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))) {
            OSVERSIONINFOEXW version = { sizeof(version) };
            return rtlGetVersion(&version) == 0 && version.dwMajorVersion == 10 && version.dwBuildNumber >= 22000;
        }
    }
    return false;
}

void EnableOwnerDraw(HWND h, int id, bool comboBox) {
    HWND control = GetDlgItem(h, id);
    if (!control) return;
    LONG_PTR style = GetWindowLongPtrW(control, GWL_STYLE);
    style = comboBox ? (style | CBS_OWNERDRAWFIXED) : ((style & ~BS_TYPEMASK) | BS_OWNERDRAW);
    SetWindowLongPtrW(control, GWL_STYLE, style);
    SetWindowPos(control, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

bool EnableFluentTheme(HWND h) {
    if (!IsWindows11OrLater()) return false;
    // These exports are intentionally loaded at runtime so the wrapper retains
    // compatibility with Windows versions that pre-date dark native controls.
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);
    if (HMODULE uxtheme = GetModuleHandleW(L"uxtheme.dll")) {
        if (auto setPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)))) {
            setPreferredAppMode(2); // PreferredAppMode::AllowDark
        }
        if (auto allowDarkModeForWindow = reinterpret_cast<AllowDarkModeForWindowFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(133)))) {
            allowDarkModeForWindow(h, TRUE);
        }
    }

    const BOOL dark = TRUE;
    const int immersiveDarkModeAttribute = 20;
    const int systemBackdropAttribute = 38;
    const int micaBackdrop = 2; // DWMSBT_MAINWINDOW
    DwmSetWindowAttribute(h, immersiveDarkModeAttribute, &dark, sizeof(dark));
    DwmSetWindowAttribute(h, systemBackdropAttribute, &micaBackdrop, sizeof(micaBackdrop));
    SetWindowTheme(h, L"DarkMode_Explorer", nullptr);

    for (const int id : {
        IDC_RES_COMBO, IDC_FPS_COMBO, IDC_REFRESH, IDC_FPS_COUNTER, IDC_WINDOWED,
        IDC_BORDERLESS, IDC_CENTER, IDC_ALWAYS_ONTOP, IDC_LOCK_CURSOR, IDC_PRIMARY_MON,
        IDC_KEEP_ASPECT, IDC_SKIP_MOVIES, IDC_SMOOTH_MODEL_LIGHTING, IDC_ZOMBIE_HEADSHOTS, IDC_DETACHED_HEADS, IDC_BLOOD_COLOR, IDC_MOUSE_SENSITIVITY, IDC_LASER_TRAIL, IDC_LASER_COLOR, IDC_ENABLE_LOG,
        IDC_SHOW_CONFIG, IDC_CROSS_COMBO, IDC_CROSS_BROWSE, IDC_CROSS_SCALE, IDOK, IDCANCEL
    }) {
        if (HWND control = GetDlgItem(h, id)) SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
    }
    // The themed edit background remains light on some Windows 11 builds; let the
    // dialog's colour handler draw the compact dark input instead.
    if (HWND refreshRate = GetDlgItem(h, IDC_REFRESH)) SetWindowTheme(refreshRate, L"", L"");
    for (const int id : { IDC_RES_COMBO, IDC_FPS_COMBO, IDC_CROSS_COMBO }) EnableOwnerDraw(h, id, true);
    for (const int id : { IDC_CROSS_BROWSE, IDC_LASER_COLOR, IDOK, IDCANCEL }) EnableOwnerDraw(h, id, false);
    return true;
}

HANDLE ActivateVisualStyles(HINSTANCE instance, ULONG_PTR& cookie) {
    ACTCTXW context = { sizeof(context) };
    context.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
    context.hModule = instance;
    context.lpResourceName = MAKEINTRESOURCEW(1);

    HANDLE activationContext = CreateActCtxW(&context);
    if (activationContext == INVALID_HANDLE_VALUE) return nullptr;
    if (!ActivateActCtx(activationContext, &cookie)) {
        ReleaseActCtx(activationContext);
        return nullptr;
    }

    INITCOMMONCONTROLSEX commonControls = { sizeof(commonControls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&commonControls);
    return activationContext;
}

} // namespace

struct ResItem { int w, h; const char* name; };
std::vector<ResItem> resList = {
    {320,  240,  "320 x 240 (4:3)"},
    {640,  480,  "640 x 480 (4:3)"},
    {800,  600,  "800 x 600 (4:3)"},
    {960,  720,  "960 x 720 (4:3)"},
    {1024, 768,  "1024 x 768 (4:3)"},
    {1280, 960,  "1280 x 960 (4:3)"},
    {1280, 720,  "1280 x 720 (16:9)"},
    {1600, 900,  "1600 x 900 (16:9)"},
    {1920, 1080, "1920 x 1080 (16:9)"},
    {2560, 1440, "2560 x 1440 (16:9)"},
    {3840, 2160, "3840 x 2160 (16:9)"}
};

const char* FluentComboText(HWND combo, UINT controlId, UINT itemId) {
    int index = itemId == static_cast<UINT>(-1)
        ? static_cast<int>(SendMessageA(combo, CB_GETCURSEL, 0, 0))
        : static_cast<int>(itemId);
    static constexpr const char* fpsOptions[] = { "Stable (60)", "Unlimited" };
    static constexpr const char* crosshairOptions[] = {
        "PC (Default Red)", "PC (Yellow Old)", "PSX (Classic)", "Umbrella Chronicles", "Custom..."
    };

    if (controlId == IDC_RES_COMBO && index >= 0 && index < static_cast<int>(resList.size())) return resList[index].name;
    if (controlId == IDC_FPS_COMBO && index >= 0 && index < static_cast<int>(_countof(fpsOptions))) return fpsOptions[index];
    if (controlId == IDC_CROSS_COMBO && index >= 0 && index < static_cast<int>(_countof(crosshairOptions))) return crosshairOptions[index];
    return "";
}

void PaintCrosshairPreview(HWND hStatic, HDC hdc) {
    RECT rect;
    GetClientRect(hStatic, &rect);

    Graphics graphics(hdc);
    Color backColor;
    backColor.SetFromCOLORREF(gUseFluentDesign ? kInputColor : GetSysColor(COLOR_3DFACE));
    SolidBrush backBrush(backColor);
    graphics.FillRectangle(&backBrush, 0, 0, rect.right, rect.bottom);

    std::string previewPath = sCrosshairPath;
    if (PathIsRelativeA(previewPath.c_str())) {
        std::string configDirectory = cfgPath;
        const size_t slash = configDirectory.find_last_of("\\/");
        if (slash != std::string::npos) previewPath = configDirectory.substr(0, slash + 1) + previewPath;
    }
    std::wstring widestr = std::wstring(previewPath.begin(), previewPath.end());
    std::unique_ptr<Image> img(new Image(widestr.c_str()));
    IStream* embeddedStream = nullptr;
    if (img->GetLastStatus() != Ok) {
        int id = 0;
        if (sCrosshairPath.find("crosshair_pc.png") != std::string::npos) id = IDR_CROSSHAIR_PC;
        else if (sCrosshairPath.find("crosshair_old.png") != std::string::npos) id = IDR_CROSSHAIR_OLD;
        else if (sCrosshairPath.find("crosshair_psx.png") != std::string::npos) id = IDR_CROSSHAIR_PSX;
        else if (sCrosshairPath.find("crosshair_uc_big.png") != std::string::npos) id = IDR_CROSSHAIR_UC_BIG;
        HRSRC info = id ? FindResourceA(g_hWrapperModule, MAKEINTRESOURCEA(id), RT_RCDATA) : nullptr;
        HGLOBAL resource = info ? LoadResource(g_hWrapperModule, info) : nullptr;
        const BYTE* bytes = resource ? static_cast<const BYTE*>(LockResource(resource)) : nullptr;
        const DWORD size = info ? SizeofResource(g_hWrapperModule, info) : 0;
        if (bytes && size) {
            embeddedStream = SHCreateMemStream(bytes, size);
            if (embeddedStream) img.reset(new Image(embeddedStream));
        }
    }

    if (img->GetLastStatus() == Ok) {
        int newW = (int)(img->GetWidth() * fCrosshairScale);
        int newH = (int)(img->GetHeight() * fCrosshairScale);

        // Center the scaled image
        int x = (rect.right - newW) / 2;
        int y = (rect.bottom - newH) / 2;
        graphics.DrawImage(img.get(), x, y, newW, newH);
    }
    img.reset();
    if (embeddedStream) embeddedStream->Release();
}

LRESULT CALLBACK CrosshairPreviewProc(HWND hStatic, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint = {};
        HDC hdc = BeginPaint(hStatic, &paint);
        PaintCrosshairPreview(hStatic, hdc);
        EndPaint(hStatic, &paint);
        return 0;
    }
    return CallWindowProc(gCrosshairPreviewProc, hStatic, message, wParam, lParam);
}

void RenderCrosshairPreview(HWND hDlg) {
    HWND hStatic = GetDlgItem(hDlg, IDC_CROSS_PREVIEW);
    if (!hStatic) return;
    InvalidateRect(hStatic, nullptr, TRUE);
    UpdateWindow(hStatic);
}

void SyncConfig(bool save) {
    auto SetI = [&](const char* s, const char* k, int v) { WritePrivateProfileStringA(s, k, std::to_string(v).c_str(), cfgPath); };
    auto GetI = [&](const char* s, const char* k, int d) { return GetPrivateProfileIntA(s, k, d, cfgPath); };

    if (save) {
        SetI("Graphics", "FPSLimit", (int)fFPSLimit);
        SetI("Graphics", "ForceWidth", ForceWidth);
        SetI("Graphics", "ForceHeight", ForceHeight);
        SetI("Graphics", "DisplayFPSCounter", bDisplayFPSCounter);
        SetI("Graphics", "FullScreenRefreshRateInHz", nFullScreenRefreshRateInHz);
        SetI("Window", "ForceWindowedMode", bForceWindowedMode);
        SetI("Window", "BorderlessFullscreen", bBorderlessFullscreen);
        SetI("Window", "CenterWindow", bCenterWindow);
        SetI("Window", "AlwaysOnTop", bAlwaysOnTop);
        SetI("Window", "LockCursorToWindow", bLockCursorToWindow);
        SetI("Window", "UsePrimaryMonitor", bUsePrimaryMonitor);
        SetI("Fixes", "SkipMovies", bSkipMovies);
        SetI("Fixes", "Direct3D8DisableMaximizedWindowedModeShim", bDirect3D8DisableMaximizedWindowedModeShim);
        SetI("Window", "KeepAspectRatio", keepAspect);
        SetI("Fixes", "SmoothModelLighting", smoothModelLighting);
        SetI("Fixes", "LethalZombieHeadshots", lethalZombieHeadshots);
        SetI("Fixes", "UncensoredHeadshots", detachedZombieHeads);
        const std::string blood = std::to_string(GetRValue(bloodColor)) + "," +
            std::to_string(GetGValue(bloodColor)) + "," + std::to_string(GetBValue(bloodColor));
        WritePrivateProfileStringA("Fixes", "BloodColor", blood.c_str(), cfgPath);
        const std::string sensitivity = std::to_string(static_cast<float>(mouseSensitivityPercent) / 100.0f);
        WritePrivateProfileStringA("Crosshair", "MouseSensitivity", sensitivity.c_str(), cfgPath);
        WritePrivateProfileStringA("Crosshair", "TexturePath", sCrosshairPath.c_str(), cfgPath);
        SetI("Crosshair", "Scale", (int)(fCrosshairScale * 100.0f));
        SetI("Crosshair", "LaserTrail", bLaserTrail);
        const std::string color = std::to_string(GetRValue(gLaserColor)) + "," +
            std::to_string(GetGValue(gLaserColor)) + "," + std::to_string(GetBValue(gLaserColor));
        WritePrivateProfileStringA("Crosshair", "LaserColor", color.c_str(), cfgPath);
        SetI("Application", "EnableLogging", gEnableLogging);
        SetI("Application", "ShowConfigOnLaunch", bShowConfigOnLaunch);
        // Remove only the obsolete locations after writing their canonical replacements.
        for (const auto& old : { std::pair<const char*, const char*>("SurvivorFix", "KeepAspect"),
                                {"SurvivorFix", "SmoothModelLighting"}, {"SurvivorFix", "MouseSensitivity"},
                                {"Debug", "EnableLogging"}, {"Debug", "ShowConfig"},
                                {"Fixes", "CrosshairPath"}, {"Fixes", "CrosshairScale"} })
            WritePrivateProfileStringA(old.first, old.second, nullptr, cfgPath);
        // Windows leaves an empty section header behind after its last key is removed.
        char remaining[4] = {};
        for (const char* section : { "Debug", "SurvivorFix" }) {
            if (GetPrivateProfileSectionA(section, remaining, sizeof remaining, cfgPath) == 0)
                WritePrivateProfileStringA(section, nullptr, nullptr, cfgPath);
        }
    }
	else {//defaults
        fFPSLimit = (float)GetI("Graphics", "FPSLimit", 60);
        ForceWidth = GetI("Graphics", "ForceWidth", 1024);
        ForceHeight = GetI("Graphics", "ForceHeight", 768);
        nFullScreenRefreshRateInHz = GetI("Graphics", "FullScreenRefreshRateInHz", 60);
        bDisplayFPSCounter = GetI("Graphics", "DisplayFPSCounter", 0) != 0;
        bForceWindowedMode = GetI("Window", "ForceWindowedMode", 1) != 0;
        bBorderlessFullscreen = GetI("Window", "BorderlessFullscreen", 0) != 0;
        bCenterWindow = GetI("Window", "CenterWindow", 1) != 0;
        bAlwaysOnTop = GetI("Window", "AlwaysOnTop", 0) != 0;
        bLockCursorToWindow = GetI("Window", "LockCursorToWindow", 1) != 0;
        bUsePrimaryMonitor = GetI("Window", "UsePrimaryMonitor", 0) != 0;
        bSkipMovies = GetI("Fixes", "SkipMovies", 0) != 0;
        //bDirect3D8DisableMaximizedWindowedModeShim = GetI("Fixes", "Direct3D8DisableMaximizedWindowedModeShim", 0) != 0;
        auto Has = [&](const char* s, const char* k) { char v[2] = {}; return GetPrivateProfileStringA(s, k, nullptr, v, sizeof v, cfgPath) != 0; };
        auto MigratedI = [&](const char* s, const char* k, const char* oldS, const char* oldK, int d) {
            return Has(s, k) ? GetI(s, k, d) : GetI(oldS, oldK, d);
        };
        gEnableLogging = MigratedI("Application", "EnableLogging", "Debug", "EnableLogging", 0) != 0;
        bShowConfigOnLaunch = MigratedI("Application", "ShowConfigOnLaunch", "Debug", "ShowConfig", 0) != 0;

        char temp[MAX_PATH];
        GetPrivateProfileStringA(Has("Crosshair", "TexturePath") ? "Crosshair" : "Fixes",
            Has("Crosshair", "TexturePath") ? "TexturePath" : "CrosshairPath", "FIXRES/crosshair_psx.png", temp, MAX_PATH, cfgPath);
        sCrosshairPath = temp;
        fCrosshairScale = (float)MigratedI("Crosshair", "Scale", "Fixes", "CrosshairScale", 100) / 100.0f;
        bLaserTrail = GetI("Crosshair", "LaserTrail", 0) != 0;
        GetPrivateProfileStringA("Crosshair", "LaserColor", "255,35,20", temp, MAX_PATH, cfgPath);
        int red = 255, green = 35, blue = 20;
        if (sscanf_s(temp, "%d,%d,%d", &red, &green, &blue) == 3)
            gLaserColor = RGB(std::clamp(red, 0, 255), std::clamp(green, 0, 255), std::clamp(blue, 0, 255));
        keepAspect = MigratedI("Window", "KeepAspectRatio", "SurvivorFix", "KeepAspect", 1) != 0;
        smoothModelLighting = MigratedI("Fixes", "SmoothModelLighting", "SurvivorFix", "SmoothModelLighting", 1) != 0;
        lethalZombieHeadshots = GetI("Fixes", "LethalZombieHeadshots", 0) != 0;
        detachedZombieHeads = Has("Fixes", "UncensoredHeadshots")
            ? GetI("Fixes", "UncensoredHeadshots", 1) != 0
            : (Has("Fixes", "ExplodingZombieHeads")
                ? GetI("Fixes", "ExplodingZombieHeads", 1) != 0
                : GetI("Fixes", "DetachedZombieHeads", 1) != 0);
        GetPrivateProfileStringA("Fixes", "BloodColor", "255,255,255", temp, MAX_PATH, cfgPath);
        red = green = blue = 255;
        if (sscanf_s(temp, "%d,%d,%d", &red, &green, &blue) == 3)
            bloodColor = RGB(std::clamp(red, 0, 255), std::clamp(green, 0, 255), std::clamp(blue, 0, 255));
        GetPrivateProfileStringA(Has("Crosshair", "MouseSensitivity") ? "Crosshair" : "SurvivorFix",
            "MouseSensitivity", "1.0", temp, MAX_PATH, cfgPath);
        mouseSensitivityPercent = std::clamp(static_cast<int>(std::atof(temp) * 100.0 + 0.5), 10, 500);
    }
}

INT_PTR CALLBACK ConfigDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_INITDIALOG: {
        gUseFluentDesign = EnableFluentTheme(h);
        if (gUseFluentDesign) CreateSectionCards(h);
        if (HWND preview = GetDlgItem(h, IDC_CROSS_PREVIEW)) {
            WNDPROC previous = reinterpret_cast<WNDPROC>(SetWindowLongPtr(preview, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(CrosshairPreviewProc)));
            if (!gCrosshairPreviewProc) gCrosshairPreviewProc = previous;
        }

        HWND hFPS = GetDlgItem(h, IDC_FPS_COMBO);
        SendMessage(hFPS, CB_ADDSTRING, 0, (LPARAM)"Stable (60)");
        SendMessage(hFPS, CB_ADDSTRING, 0, (LPARAM)"Unlimited");
        SendMessage(hFPS, CB_SETCURSEL, (fFPSLimit >= 59.0f) ? 0 : 1, 0);

        HWND hRes = GetDlgItem(h, IDC_RES_COMBO);
        int sel = 2;
        for (int i = 0; i < (int)resList.size(); i++) {
            SendMessage(hRes, CB_ADDSTRING, 0, (LPARAM)resList[i].name);
            if (resList[i].w == ForceWidth && resList[i].h == ForceHeight) sel = i;
        }
        SendMessage(hRes, CB_SETCURSEL, sel, 0);

        HWND hCross = GetDlgItem(h, IDC_CROSS_COMBO);
        SendMessage(hCross, CB_ADDSTRING, 0, (LPARAM)"PC (Default Red)");
        SendMessage(hCross, CB_ADDSTRING, 0, (LPARAM)"PC (Yellow Old)");
        SendMessage(hCross, CB_ADDSTRING, 0, (LPARAM)"PSX (Classic)");
        SendMessage(hCross, CB_ADDSTRING, 0, (LPARAM)"Umbrella Chronicles");
        SendMessage(hCross, CB_ADDSTRING, 0, (LPARAM)"Custom...");

        if (sCrosshairPath.find("crosshair_pc.png") != std::string::npos) SendMessage(hCross, CB_SETCURSEL, 0, 0);
        else if (sCrosshairPath.find("crosshair_old.png") != std::string::npos) SendMessage(hCross, CB_SETCURSEL, 1, 0);
        else if (sCrosshairPath.find("crosshair_psx.png") != std::string::npos) SendMessage(hCross, CB_SETCURSEL, 2, 0);
        else if (sCrosshairPath.find("crosshair_uc_big.png") != std::string::npos) SendMessage(hCross, CB_SETCURSEL, 3, 0);
        else SendMessage(hCross, CB_SETCURSEL, 4, 0);

        HWND hTrack = GetDlgItem(h, IDC_CROSS_SCALE);
        SendMessage(hTrack, TBM_SETRANGE, TRUE, MAKELONG(50, 300));
        SendMessage(hTrack, TBM_SETPOS, TRUE, (LPARAM)(fCrosshairScale * 100.0f));
        SetDlgItemTextA(h, IDC_CROSS_SCALE_LABEL, (std::to_string((int)(fCrosshairScale * 100)) + "%").c_str());

        HWND hMouseSensitivity = GetDlgItem(h, IDC_MOUSE_SENSITIVITY);
        SendMessage(hMouseSensitivity, TBM_SETRANGE, TRUE, MAKELONG(10, 500));
        SendMessage(hMouseSensitivity, TBM_SETPOS, TRUE, mouseSensitivityPercent);
        SetDlgItemTextA(h, IDC_MOUSE_SENSITIVITY_LABEL, (std::to_string(mouseSensitivityPercent) + "%").c_str());

        SetDlgItemInt(h, IDC_REFRESH, nFullScreenRefreshRateInHz, FALSE);
        CheckDlgButton(h, IDC_FPS_COUNTER, bDisplayFPSCounter);
        CheckDlgButton(h, IDC_WINDOWED, bForceWindowedMode);
        CheckDlgButton(h, IDC_BORDERLESS, bBorderlessFullscreen);
        CheckDlgButton(h, IDC_CENTER, bCenterWindow);
        CheckDlgButton(h, IDC_ALWAYS_ONTOP, bAlwaysOnTop);
        CheckDlgButton(h, IDC_LOCK_CURSOR, bLockCursorToWindow);
        CheckDlgButton(h, IDC_PRIMARY_MON, bUsePrimaryMonitor);
        CheckDlgButton(h, IDC_KEEP_ASPECT, keepAspect);
        CheckDlgButton(h, IDC_SKIP_MOVIES, bSkipMovies);
        CheckDlgButton(h, IDC_SMOOTH_MODEL_LIGHTING, smoothModelLighting);
        CheckDlgButton(h, IDC_ZOMBIE_HEADSHOTS, lethalZombieHeadshots);
        CheckDlgButton(h, IDC_DETACHED_HEADS, detachedZombieHeads);
        CheckDlgButton(h, IDC_LASER_TRAIL, bLaserTrail);
        CheckDlgButton(h, IDC_ENABLE_LOG, gEnableLogging);
        //CheckDlgButton(h, IDC_DISABLE_SHIM, bDirect3D8DisableMaximizedWindowedModeShim);
        CheckDlgButton(h, IDC_SHOW_CONFIG, bShowConfigOnLaunch);
        // Let the static control complete its initial paint before drawing the image.
        PostMessage(h, WM_RENDER_CROSSHAIR_PREVIEW, 0, 0);
        return TRUE;
    }
    case WM_RENDER_CROSSHAIR_PREVIEW:
        RenderCrosshairPreview(h);
        return TRUE;
    case WM_CTLCOLORDLG: {
        if (!gUseFluentDesign) break;
        HDC dc = reinterpret_cast<HDC>(w);
        SetBkColor(dc, kSurfaceColor);
        return reinterpret_cast<INT_PTR>(SurfaceBrush());
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        if (!gUseFluentDesign) break;
        HDC dc = reinterpret_cast<HDC>(w);
        SetTextColor(dc, kTextColor);
        SetBkColor(dc, kCardColor);
        return reinterpret_cast<INT_PTR>(CardBrush());
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        if (!gUseFluentDesign) break;
        HDC dc = reinterpret_cast<HDC>(w);
        SetTextColor(dc, kTextColor);
        SetBkColor(dc, kInputColor);
        return reinterpret_cast<INT_PTR>(InputBrush());
    }
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(l);
        if (gUseFluentDesign && item && item->CtlType == ODT_COMBOBOX && IsFluentCombo(item->CtlID)) {
            DrawFluentCombo(*item);
            return TRUE;
        }
        if (gUseFluentDesign && item && item->CtlType == ODT_BUTTON && (item->CtlID == IDC_CROSS_BROWSE || item->CtlID == IDC_LASER_COLOR || item->CtlID == IDC_BLOOD_COLOR || item->CtlID == IDOK || item->CtlID == IDCANCEL)) {
            DrawFluentButton(*item);
            return TRUE;
        }
        break;
    }
    case WM_PAINT: {
        if (gUseFluentDesign) {
            PAINTSTRUCT paint = {};
            HDC dc = BeginPaint(h, &paint);
            PaintSectionCards(h, dc);
            EndPaint(h, &paint);
        }
        break;
    }
    case WM_HSCROLL: {
        if ((HWND)l == GetDlgItem(h, IDC_CROSS_SCALE)) {
            int pos = (int)SendMessage((HWND)l, TBM_GETPOS, 0, 0);
            fCrosshairScale = (float)pos / 100.0f;
            SetDlgItemTextA(h, IDC_CROSS_SCALE_LABEL, (std::to_string(pos) + "%").c_str());
            RenderCrosshairPreview(h);
        }
        else if ((HWND)l == GetDlgItem(h, IDC_MOUSE_SENSITIVITY)) {
            mouseSensitivityPercent = (int)SendMessage((HWND)l, TBM_GETPOS, 0, 0);
            SetDlgItemTextA(h, IDC_MOUSE_SENSITIVITY_LABEL, (std::to_string(mouseSensitivityPercent) + "%").c_str());
        }
        break;
    }
    case WM_COMMAND: {
        if (LOWORD(w) == IDOK) {
            int fpsSel = (int)SendMessage(GetDlgItem(h, IDC_FPS_COMBO), CB_GETCURSEL, 0, 0);
            fFPSLimit = (fpsSel == 0) ? 60.0f : 0.0f;
            int resSel = (int)SendMessage(GetDlgItem(h, IDC_RES_COMBO), CB_GETCURSEL, 0, 0);
            ForceWidth = resList[resSel].w;
            ForceHeight = resList[resSel].h;
            nFullScreenRefreshRateInHz = GetDlgItemInt(h, IDC_REFRESH, NULL, FALSE);
            bDisplayFPSCounter = IsDlgButtonChecked(h, IDC_FPS_COUNTER);
            bForceWindowedMode = IsDlgButtonChecked(h, IDC_WINDOWED);
            bBorderlessFullscreen = IsDlgButtonChecked(h, IDC_BORDERLESS);
            bCenterWindow = IsDlgButtonChecked(h, IDC_CENTER);
            bAlwaysOnTop = IsDlgButtonChecked(h, IDC_ALWAYS_ONTOP);
            bLockCursorToWindow = IsDlgButtonChecked(h, IDC_LOCK_CURSOR);
            bUsePrimaryMonitor = IsDlgButtonChecked(h, IDC_PRIMARY_MON);
            keepAspect = IsDlgButtonChecked(h, IDC_KEEP_ASPECT);
            bSkipMovies = IsDlgButtonChecked(h, IDC_SKIP_MOVIES);
            smoothModelLighting = IsDlgButtonChecked(h, IDC_SMOOTH_MODEL_LIGHTING);
            lethalZombieHeadshots = IsDlgButtonChecked(h, IDC_ZOMBIE_HEADSHOTS);
            detachedZombieHeads = IsDlgButtonChecked(h, IDC_DETACHED_HEADS);
            bLaserTrail = IsDlgButtonChecked(h, IDC_LASER_TRAIL);
            gEnableLogging = IsDlgButtonChecked(h, IDC_ENABLE_LOG);
            //bDirect3D8DisableMaximizedWindowedModeShim = IsDlgButtonChecked(h, IDC_DISABLE_SHIM);
            bShowConfigOnLaunch = IsDlgButtonChecked(h, IDC_SHOW_CONFIG);
            SyncConfig(true);
            EndDialog(h, IDOK);
        }
        else if (LOWORD(w) == IDCANCEL) {
            EndDialog(h, IDCANCEL);
        }
        else if (LOWORD(w) == IDC_CROSS_COMBO && HIWORD(w) == CBN_SELCHANGE) {
            int sel = (int)SendMessage((HWND)l, CB_GETCURSEL, 0, 0);
            if (sel == 0) sCrosshairPath = "FIXRES/crosshair_pc.png";
            else if (sel == 1) sCrosshairPath = "FIXRES/crosshair_old.png";
            else if (sel == 2) sCrosshairPath = "FIXRES/crosshair_psx.png";
            else if (sel == 3) sCrosshairPath = "FIXRES/crosshair_uc_big.png";
            RenderCrosshairPreview(h);
        }
        else if (LOWORD(w) == IDC_CROSS_BROWSE) {
            char szFile[MAX_PATH] = { 0 };
            OPENFILENAMEA ofn = { sizeof(ofn) };
            ofn.hwndOwner = h;
            ofn.lpstrFilter = "PNG Images\0*.png\0All Files\0*.*\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn)) {
                sCrosshairPath = szFile;
                SendMessage(GetDlgItem(h, IDC_CROSS_COMBO), CB_SETCURSEL, 4, 0);
                RenderCrosshairPreview(h);
            }
        }
        else if (LOWORD(w) == IDC_LASER_COLOR) {
            static COLORREF customColors[16] = {};
            CHOOSECOLORA picker = { sizeof(picker) };
            picker.hwndOwner = h;
            picker.rgbResult = gLaserColor;
            picker.lpCustColors = customColors;
            picker.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorA(&picker)) gLaserColor = picker.rgbResult;
        }
        else if (LOWORD(w) == IDC_BLOOD_COLOR) {
            static COLORREF customBloodColors[16] = {};
            CHOOSECOLORA picker = { sizeof(picker) };
            picker.hwndOwner = h;
            picker.rgbResult = bloodColor;
            picker.lpCustColors = customBloodColors;
            picker.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorA(&picker)) bloodColor = picker.rgbResult;
        }
        break;
    }
    }
    return FALSE;
}

void ShowConfigWindow(HINSTANCE hInst) {
    GetModuleFileNameA(GetModuleHandleA("d3d8.dll"), cfgPath, MAX_PATH);
    std::string p = cfgPath;
    size_t last = p.find_last_of("\\/");
    if (last != std::string::npos) p = p.substr(0, last);
    p += "\\config.ini";
    strcpy(cfgPath, p.c_str());

    ULONG_PTR activationCookie = 0;
    HANDLE activationContext = ActivateVisualStyles(hInst, activationCookie);

    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    SyncConfig(false);

    if (bShowConfigOnLaunch || (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
        INT_PTR result = DialogBox(hInst, MAKEINTRESOURCE(IDD_CONFIG_DIALOG), NULL, ConfigDlgProc);

        if (result == IDCANCEL) {
            TerminateProcess(GetCurrentProcess(), 0); // User hit cancel, kill process
        }
    }

    // --- FIX FOR BACKGROUND/TASKBAR ISSUE ---
    // Wait a brief moment for the dialog to fully destroy
    Sleep(100);

    // Find the game window (Replace "Biohazard Gun Survivor" with the actual window class or name if different)
    HWND hGameWnd = FindWindowA(NULL, "Resident Evil Survivor");
    if (hGameWnd) {
        ShowWindow(hGameWnd, SW_SHOW);
        SetForegroundWindow(hGameWnd);
        SetActiveWindow(hGameWnd);
        SetFocus(hGameWnd);
    }

    GdiplusShutdown(gdiplusToken);
    if (activationContext) {
        DeactivateActCtx(0, activationCookie);
        ReleaseActCtx(activationContext);
    }
}

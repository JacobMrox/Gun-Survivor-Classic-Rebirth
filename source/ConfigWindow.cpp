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

static int mouseSensitivityPercent = 100;
static bool keepAspect = true;

static char cfgPath[MAX_PATH];
bool bShowConfigOnLaunch = false;
ULONG_PTR gdiplusToken;

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

void RenderCrosshairPreview(HWND hDlg) {
    HWND hStatic = GetDlgItem(hDlg, IDC_CROSS_PREVIEW);
    if (!hStatic) return;

    RECT rect;
    GetClientRect(hStatic, &rect);
    HDC hdc = GetDC(hStatic);

    Graphics graphics(hdc);
    // Use the standard Dialog background color (Face color)
    Color backColor;
    backColor.SetFromCOLORREF(GetSysColor(COLOR_3DFACE));
    SolidBrush backBrush(backColor);
    graphics.FillRectangle(&backBrush, 0, 0, rect.right, rect.bottom);

    std::wstring widestr = std::wstring(sCrosshairPath.begin(), sCrosshairPath.end());
    Image img(widestr.c_str());

    if (img.GetLastStatus() == Ok) {
        int newW = (int)(img.GetWidth() * fCrosshairScale);
        int newH = (int)(img.GetHeight() * fCrosshairScale);

        // Center the scaled image
        int x = (rect.right - newW) / 2;
        int y = (rect.bottom - newH) / 2;
        graphics.DrawImage(&img, x, y, newW, newH);
    }
    ReleaseDC(hStatic, hdc);
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
        SetI("SurvivorFix", "KeepAspect", keepAspect);
        const std::string sensitivity = std::to_string(static_cast<float>(mouseSensitivityPercent) / 100.0f);
        WritePrivateProfileStringA("SurvivorFix", "MouseSensitivity", sensitivity.c_str(), cfgPath);
        SetI("Debug", "EnableLogging", gEnableLogging);
        SetI("Debug", "ShowConfig", bShowConfigOnLaunch);
        WritePrivateProfileStringA("Fixes", "CrosshairPath", sCrosshairPath.c_str(), cfgPath);
        SetI("Fixes", "CrosshairScale", (int)(fCrosshairScale * 100.0f));
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
        gEnableLogging = GetI("Debug", "EnableLogging", 0) != 0;
        bShowConfigOnLaunch = GetI("Debug", "ShowConfig", 0) != 0;

        char temp[MAX_PATH];
        GetPrivateProfileStringA("Fixes", "CrosshairPath", "FIXRES/crosshair_psx.png", temp, MAX_PATH, cfgPath);
        sCrosshairPath = temp;
        fCrosshairScale = (float)GetI("Fixes", "CrosshairScale", 100) / 100.0f;
        keepAspect = GetI("SurvivorFix", "KeepAspect", 1) != 0;
        GetPrivateProfileStringA("SurvivorFix", "MouseSensitivity", "1.0", temp, MAX_PATH, cfgPath);
        mouseSensitivityPercent = std::clamp(static_cast<int>(std::atof(temp) * 100.0 + 0.5), 10, 500);
    }
}

INT_PTR CALLBACK ConfigDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_INITDIALOG: {
        // Use the native Explorer control style and the Windows 11 transient-window
        // backdrop when it is available. Older Windows versions ignore these calls.
        const int immersiveDarkModeAttribute = 20;
        const int systemBackdropAttribute = 38;
        const BOOL useDarkTitleBar = FALSE;
        const int transientWindowBackdrop = 3;
        DwmSetWindowAttribute(h, immersiveDarkModeAttribute, &useDarkTitleBar, sizeof(useDarkTitleBar));
        DwmSetWindowAttribute(h, systemBackdropAttribute, &transientWindowBackdrop, sizeof(transientWindowBackdrop));
        SetWindowTheme(h, L"Explorer", nullptr);
        for (const int id : { IDC_RES_COMBO, IDC_FPS_COMBO, IDC_CROSS_COMBO, IDC_CROSS_SCALE, IDC_MOUSE_SENSITIVITY }) {
            if (HWND control = GetDlgItem(h, id)) SetWindowTheme(control, L"Explorer", nullptr);
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

        if (sCrosshairPath.find("crosshair_pc.png") != std::string::npos) SendMessage(hCross, CB_SETCURSEL, 1, 0);
        else if (sCrosshairPath.find("crosshair_old.png") != std::string::npos) SendMessage(hCross, CB_SETCURSEL, 2, 0);
        else if (sCrosshairPath.find("crosshair_psx.png") != std::string::npos) SendMessage(hCross, CB_SETCURSEL, 3, 0);
        else if (sCrosshairPath.find("crosshair_uc_big.png") != std::string::npos) SendMessage(hCross, CB_SETCURSEL, 0, 0);
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
        CheckDlgButton(h, IDC_ENABLE_LOG, gEnableLogging);
        //CheckDlgButton(h, IDC_DISABLE_SHIM, bDirect3D8DisableMaximizedWindowedModeShim);
        CheckDlgButton(h, IDC_SHOW_CONFIG, bShowConfigOnLaunch);
        return TRUE;
    }
    case WM_PAINT: {
        RenderCrosshairPreview(h);
        break;
    }
    case WM_HSCROLL: {
        if ((HWND)l == GetDlgItem(h, IDC_CROSS_SCALE)) {
            int pos = (int)SendMessage((HWND)l, TBM_GETPOS, 0, 0);
            fCrosshairScale = (float)pos / 100.0f;
            SetDlgItemTextA(h, IDC_CROSS_SCALE_LABEL, (std::to_string(pos) + "%").c_str());
            InvalidateRect(GetDlgItem(h, IDC_CROSS_PREVIEW), NULL, TRUE);
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
            InvalidateRect(GetDlgItem(h, IDC_CROSS_PREVIEW), NULL, TRUE);
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
                InvalidateRect(GetDlgItem(h, IDC_CROSS_PREVIEW), NULL, TRUE);
            }
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
}

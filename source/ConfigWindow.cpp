#include "ConfigWindow.h"
#include "resource.h"
#include <string>
#include <vector>

// External variables from dllmain.cpp
extern bool bForceWindowedMode, bDirect3D8DisableMaximizedWindowedModeShim, bUsePrimaryMonitor;
extern bool bCenterWindow, bBorderlessFullscreen, bAlwaysOnTop, bDisplayFPSCounter;
extern bool bLockCursorToWindow, bAlphaFix, bSkipMovies, gEnableLogging;
extern int ForceWidth, ForceHeight, nFullScreenRefreshRateInHz;
extern float fFPSLimit;

static char cfgPath[MAX_PATH];

struct ResItem { int w, h; const char* name; };
std::vector<ResItem> resList = {
    {640, 480, "640 x 480 (4:3)"}, {800, 600, "800 x 600 (4:3)"}, {1024, 768, "1024 x 768 (4:3)"},
    {1280, 720, "1280 x 720 (16:9)"}, {1600, 900, "1600 x 900 (16:9)"}, {1920, 1080, "1920 x 1080 (16:9)"},
    {2560, 1440, "2560 x 1440 (16:9)"}, {3840, 2160, "3840 x 2160 (16:9)"}
};

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
        SetI("Fixes", "AlphaFix", bAlphaFix);
        SetI("Fixes", "SkipMovies", bSkipMovies);
        SetI("Fixes", "Direct3D8DisableMaximizedWindowedModeShim", bDirect3D8DisableMaximizedWindowedModeShim);
        SetI("Debug", "EnableLogging", gEnableLogging);
    }
    else {
        fFPSLimit = (float)GetI("Graphics", "FPSLimit", 60);
        ForceWidth = GetI("Graphics", "ForceWidth", 1024);
        ForceHeight = GetI("Graphics", "ForceHeight", 768);
        nFullScreenRefreshRateInHz = GetI("Graphics", "FullScreenRefreshRateInHz", 60);
        bDisplayFPSCounter = GetI("Graphics", "DisplayFPSCounter", 0) != 0;
        bForceWindowedMode = GetI("Window", "ForceWindowedMode", 1) != 0;
        bBorderlessFullscreen = GetI("Window", "BorderlessFullscreen", 0) != 0;
        bCenterWindow = GetI("Window", "CenterWindow", 1) != 0;
        bAlwaysOnTop = GetI("Window", "AlwaysOnTop", 0) != 0;
        bLockCursorToWindow = GetI("Window", "LockCursorToWindow", 0) != 0;
        bUsePrimaryMonitor = GetI("Window", "UsePrimaryMonitor", 0) != 0;
        bAlphaFix = GetI("Fixes", "AlphaFix", 0) != 0;
        bSkipMovies = GetI("Fixes", "SkipMovies", 0) != 0;
        bDirect3D8DisableMaximizedWindowedModeShim = GetI("Fixes", "Direct3D8DisableMaximizedWindowedModeShim", 0) != 0;
        gEnableLogging = GetI("Debug", "EnableLogging", 0) != 0;
    }
}

INT_PTR CALLBACK ConfigDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_INITDIALOG) {
        HWND hFPS = GetDlgItem(h, IDC_FPS_COMBO);
        SendMessage(hFPS, CB_ADDSTRING, 0, (LPARAM)"Stable (60)");
        SendMessage(hFPS, CB_ADDSTRING, 0, (LPARAM)"Unlimited");
        SendMessage(hFPS, CB_SETCURSEL, (fFPSLimit >= 59.0f) ? 0 : 1, 0);

        HWND hRes = GetDlgItem(h, IDC_RES_COMBO);
        int sel = 2; // Default 1024x768
        for (int i = 0; i < (int)resList.size(); i++) {
            SendMessage(hRes, CB_ADDSTRING, 0, (LPARAM)resList[i].name);
            if (resList[i].w == ForceWidth && resList[i].h == ForceHeight) sel = i;
        }
        SendMessage(hRes, CB_SETCURSEL, sel, 0);

        SetDlgItemInt(h, IDC_REFRESH, nFullScreenRefreshRateInHz, FALSE);
        CheckDlgButton(h, IDC_FPS_COUNTER, bDisplayFPSCounter);
        CheckDlgButton(h, IDC_WINDOWED, bForceWindowedMode);
        CheckDlgButton(h, IDC_BORDERLESS, bBorderlessFullscreen);
        CheckDlgButton(h, IDC_CENTER, bCenterWindow);
        CheckDlgButton(h, IDC_ALWAYS_ONTOP, bAlwaysOnTop);
        CheckDlgButton(h, IDC_LOCK_CURSOR, bLockCursorToWindow);
        CheckDlgButton(h, IDC_PRIMARY_MON, bUsePrimaryMonitor);
        CheckDlgButton(h, IDC_ALPHA_FIX, bAlphaFix);
        CheckDlgButton(h, IDC_SKIP_MOVIES, bSkipMovies);
        CheckDlgButton(h, IDC_ENABLE_LOG, gEnableLogging);
        CheckDlgButton(h, IDC_DISABLE_SHIM, bDirect3D8DisableMaximizedWindowedModeShim);
        return TRUE;
    }
    if (m == WM_COMMAND) {
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
            bAlphaFix = IsDlgButtonChecked(h, IDC_ALPHA_FIX);
            bSkipMovies = IsDlgButtonChecked(h, IDC_SKIP_MOVIES);
            gEnableLogging = IsDlgButtonChecked(h, IDC_ENABLE_LOG);
            bDirect3D8DisableMaximizedWindowedModeShim = IsDlgButtonChecked(h, IDC_DISABLE_SHIM);
            SyncConfig(true);
            EndDialog(h, IDOK);
        }
        else if (LOWORD(w) == IDCANCEL) EndDialog(h, IDCANCEL);
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

    SyncConfig(false); // LOAD FROM INI

    if (GetPrivateProfileIntA("Debug", "ShowConfig", 1, cfgPath) || (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
        DialogBox(hInst, MAKEINTRESOURCE(IDD_CONFIG_DIALOG), NULL, ConfigDlgProc);
    }
}
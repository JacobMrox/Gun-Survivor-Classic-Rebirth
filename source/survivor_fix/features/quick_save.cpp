#include <windows.h>

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "../game.h"
#include "../log.h"
#include "features.h"
#include "../../aim_shared.h"

namespace sf { extern std::wstring g_dir; }
extern int ForceWidth, ForceHeight;
extern bool bForceWindowedMode, bBorderlessFullscreen;

namespace sf::features {
namespace {

#pragma pack(push, 1)
struct QuickState {
  uint32_t magic = 0x31565351;  // QSV1
  uint32_t version = 1;
  int32_t scene = 0;
  uint8_t difficulty = 0;
  uint8_t reserved[3] = {};
  std::array<uint8_t, game::kLiveSaveRecordSize> record = {};
  uint32_t checksum = 0;
};
#pragma pack(pop)

bool g_f5Down = false;
bool g_f6Down = false;
bool g_f8Down = false;
RECT g_windowedRect = {};
bool g_haveWindowedRect = false;
std::array<char, 64> g_notice = {};
DWORD g_noticeStarted = 0;

constexpr DWORD kNoticeFadeInMs = 220;
constexpr DWORD kNoticeHoldMs = 1500;
constexpr DWORD kNoticeFadeOutMs = 420;

void Notify(const char* text) {
  strncpy_s(g_notice.data(), g_notice.size(), text ? text : "", _TRUNCATE);
  g_noticeStarted = GetTickCount();
}

bool Foreground() {
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  return pid == GetCurrentProcessId();
}

std::wstring StatePath() { return sf::g_dir + L"Save\\QUICK.SAV"; }

uint32_t Checksum(const QuickState& state) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&state);
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < offsetof(QuickState, checksum); ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

bool WriteState(const QuickState& state) {
  CreateDirectoryW((sf::g_dir + L"Save").c_str(), nullptr);
  HANDLE file = CreateFileW(StatePath().c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const BOOL ok = WriteFile(file, &state, sizeof state, &written, nullptr);
  CloseHandle(file);
  return ok && written == sizeof state;
}

bool ReadState(QuickState* state) {
  HANDLE file = CreateFileW(StatePath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD read = 0;
  const BOOL ok = ReadFile(file, state, sizeof *state, &read, nullptr);
  CloseHandle(file);
  return ok && read == sizeof *state && state->magic == 0x31565351 && state->version == 1 &&
         state->checksum == Checksum(*state) && state->scene >= 0 && state->scene <= 0x79;
}

void SaveNow() {
  void* stats = *reinterpret_cast<void**>(game::kGameStats);
  void* gameState = *reinterpret_cast<void**>(game::kGameState);
  if (!stats || !gameState) {
    Notify("F5  QUICK SAVE UNAVAILABLE");
    Log("QuickSave: F5 ignored because gameplay state is unavailable");
    return;
  }
  QuickState state;
  state.scene = *reinterpret_cast<int*>(game::kSceneId);
  state.difficulty = *game::Field<uint8_t>(gameState, 0x42);
  std::memcpy(state.record.data(), game::Field<uint8_t>(stats, game::kLiveSaveRecord), state.record.size());
  state.checksum = Checksum(state);
  if (WriteState(state)) {
    Notify("F5  QUICK SAVE SAVED");
    Log("QuickSave: saved scene %02X to Save\\QUICK.SAV", state.scene);
  } else {
    Notify("F5  QUICK SAVE FAILED");
    Log("QuickSave: failed to write Save\\QUICK.SAV (%lu)", GetLastError());
  }
}

void LoadNow() {
  QuickState state;
  void* stats = *reinterpret_cast<void**>(game::kGameStats);
  void* gameObject = *reinterpret_cast<void**>(game::kGameState);
  if (!stats || !gameObject) {
    Notify("F6  QUICK LOAD UNAVAILABLE");
    Log("QuickSave: F6 ignored because gameplay state is unavailable");
    return;
  }
  if (!ReadState(&state)) {
    Notify("F6  QUICK LOAD INVALID");
    Log("QuickSave: QUICK.SAV missing or invalid");
    return;
  }
  const int currentScene = *reinterpret_cast<int*>(game::kSceneId);
  if (state.scene != currentScene) {
    Notify("F6  QUICK LOAD WRONG ROOM");
    Log("QuickSave: refused cross-room restore from scene %02X while in scene %02X",
        state.scene, currentScene);
    return;
  }
  // Restore the game's native POD record after its update. Do not return zero
  // from RoomTick: zero is explicitly the game's YOU DIED exit.
  std::memcpy(game::Field<uint8_t>(stats, game::kLiveSaveRecord),
              state.record.data(), state.record.size());
  *game::Field<uint8_t>(gameObject, 0x42) = state.difficulty;
  Notify("F6  QUICK LOAD RESTORED");
  Log("QuickSave: restored native record in scene %02X (saved scene %02X)",
      currentScene, state.scene);
}

void ResizeClient(HWND window, int width, int height) {
  RECT rect = {0, 0, width, height};
  AdjustWindowRectEx(&rect, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE,
                     static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)));
  SetWindowPos(window, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void ToggleFullscreen() {
  HWND window = g_hFocusWindow;
  if (!window || !IsWindow(window)) {
    Notify("F8  DISPLAY SWITCH FAILED");
    Log("QuickSave: F8 ignored because the game window is unavailable");
    return;
  }
  if (bBorderlessFullscreen) {
    SetWindowLongPtrW(window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    SetWindowLongPtrW(window, GWL_EXSTYLE, WS_EX_APPWINDOW);
    if (g_haveWindowedRect) {
      SetWindowPos(window, HWND_NOTOPMOST, g_windowedRect.left, g_windowedRect.top,
                   g_windowedRect.right - g_windowedRect.left, g_windowedRect.bottom - g_windowedRect.top,
                   SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
      MONITORINFO info = {sizeof info};
      GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info);
      const int workW = info.rcWork.right - info.rcWork.left;
      const int workH = info.rcWork.bottom - info.rcWork.top;
      int clientW = ForceWidth > 0 ? ForceWidth : 1280;
      int clientH = ForceHeight > 0 ? ForceHeight : 720;
      const double scaleW = workW * 0.85 / clientW;
      const double scaleH = workH * 0.85 / clientH;
      double scale = scaleW < scaleH ? scaleW : scaleH;
      if (scale > 1.0) scale = 1.0;
      clientW = static_cast<int>(clientW * scale + 0.5);
      clientH = static_cast<int>(clientH * scale + 0.5);
      ResizeClient(window, clientW, clientH);
      RECT placed = {};
      GetWindowRect(window, &placed);
      const int outerW = placed.right - placed.left;
      const int outerH = placed.bottom - placed.top;
      SetWindowPos(window, HWND_NOTOPMOST,
                   info.rcWork.left + (workW - outerW) / 2,
                   info.rcWork.top + (workH - outerH) / 2,
                   0, 0, SWP_NOSIZE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    bBorderlessFullscreen = false;
  } else {
    GetWindowRect(window, &g_windowedRect);
    g_haveWindowedRect = true;
    MONITORINFO info = {sizeof info};
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info);
    SetWindowLongPtrW(window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowLongPtrW(window, GWL_EXSTYLE, WS_EX_APPWINDOW);
    SetWindowPos(window, HWND_NOTOPMOST, info.rcMonitor.left, info.rcMonitor.top,
                 info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    bBorderlessFullscreen = true;
  }
  bForceWindowedMode = true;
  const std::wstring ini = sf::g_dir + L"config.ini";
  WritePrivateProfileStringW(L"Window", L"ForceWindowedMode", L"1", ini.c_str());
  WritePrivateProfileStringW(L"Window", L"BorderlessFullscreen",
                             bBorderlessFullscreen ? L"1" : L"0", ini.c_str());
  Notify(bBorderlessFullscreen ? "F8  FULLSCREEN ENABLED" : "F8  WINDOWED MODE ENABLED");
  Log("QuickSave: F8 switched to %s", bBorderlessFullscreen ? "borderless fullscreen" : "windowed mode");
}

}  // namespace

int QuickSaveAfterRoomTick(void* gameObject, int nativeResult) {
  const bool active = Foreground();
  const bool f5 = active && (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
  const bool f6 = active && (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
  if (f5 && !g_f5Down) SaveNow();
  if (f6 && !g_f6Down) LoadNow();
  g_f5Down = f5;
  g_f6Down = f6;
  return nativeResult;
}

void QuickSavePollWindowToggle() {
  const bool f8 = Foreground() && (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
  if (f8 && !g_f8Down) ToggleFullscreen();
  g_f8Down = f8;
}

bool QuickSaveHandleWindowMessage(unsigned message, uintptr_t wparam, intptr_t lparam) {
  const bool isHotkey = wparam == VK_F1 || wparam == VK_F5 || wparam == VK_F6 ||
                        wparam == VK_F8 || wparam == VK_F9;
  if (!isHotkey) return false;
  if (message == WM_KEYUP || message == WM_SYSKEYUP) {
    if (wparam == VK_F5) g_f5Down = false;
    if (wparam == VK_F6) g_f6Down = false;
    if (wparam == VK_F8) g_f8Down = false;
    return wparam == VK_F5 || wparam == VK_F6 || wparam == VK_F8;
  }
  if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN) return false;
  const bool repeated = (static_cast<uintptr_t>(lparam) & (uintptr_t{1} << 30)) != 0;
  if (repeated) return wparam == VK_F5 || wparam == VK_F6 || wparam == VK_F8;
  switch (wparam) {
    case VK_F1:
      Notify("F1  OPTIONS");
      return false;  // Let the game keep its native Options handling.
    case VK_F5:
      if (!g_f5Down) SaveNow();
      g_f5Down = true;
      return true;
    case VK_F6:
      if (!g_f6Down) LoadNow();
      g_f6Down = true;
      return true;
    case VK_F8:
      if (!g_f8Down) ToggleFullscreen();
      g_f8Down = true;
      return true;
    case VK_F9:
      Notify("F9  QUIT GAME");
      return false;  // The native quit path must still receive F9.
    default:
      return false;
  }
}

bool QuickSaveGetNotification(char* text, size_t capacity, float* alpha) {
  if (!text || capacity == 0 || !alpha || g_notice[0] == '\0') return false;
  const DWORD elapsed = GetTickCount() - g_noticeStarted;
  const DWORD fadeOutStart = kNoticeFadeInMs + kNoticeHoldMs;
  const DWORD end = fadeOutStart + kNoticeFadeOutMs;
  if (elapsed >= end) return false;
  float value = 1.0f;
  if (elapsed < kNoticeFadeInMs) {
    value = static_cast<float>(elapsed) / static_cast<float>(kNoticeFadeInMs);
  } else if (elapsed > fadeOutStart) {
    value = 1.0f - static_cast<float>(elapsed - fadeOutStart) /
                       static_cast<float>(kNoticeFadeOutMs);
  }
  strncpy_s(text, capacity, g_notice.data(), _TRUNCATE);
  *alpha = std::clamp(value, 0.0f, 1.0f);
  return true;
}

bool InstallQuickSave() {
  Log("QuickSave: F1/F5/F6/F8/F9 notifications active; F5/F6/F8 handled by window and render paths");
  return true;
}

}  // namespace sf::features

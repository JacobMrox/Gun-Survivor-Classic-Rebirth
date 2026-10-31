#include <windows.h>

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "../game.h"
#include "../hooks.h"
#include "../log.h"
#include "features.h"
#include "../../aim_shared.h"
#include "../../survivor_pc.h"

namespace sf { extern std::wstring g_dir; }
extern int ForceWidth, ForceHeight;
extern bool bForceWindowedMode, bBorderlessFullscreen;

namespace sf::features {
namespace {

#pragma pack(push, 1)
struct QuickState {
  uint32_t magic = 0x31565351;  // QSV1
  uint32_t version = 6;
  int32_t scene = 0;
  int32_t roomArgument = 0;
  uint8_t difficulty = 0;
  uint8_t playerValid = 0;
  uint8_t reserved[2] = {};
  PlayerQuickState player = {};
  int32_t playerHp = 0;
  std::array<uint8_t, game::kLiveSaveRecordSize> saveRecord = {};
  std::array<uint8_t, game::kPersistentRoomStateSize> roomState = {};
  uint32_t zombieCount = 0;
  std::array<zombie::QuickState, 30> zombies = {};
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
enum class ReloadStage {
  None,
  Requested,
  Fading,
  CleanupInProgress,
  NativeLoadQueued,
  WaitingForRoom
};
ReloadStage g_reloadStage = ReloadStage::None;
QuickState g_pendingReload = {};
QuickState g_menuSaveState = {};
bool g_menuSaveStateValid = false;
bool g_openLoadMenu = false;
bool g_skipSaveIntro = false;
bool g_mouseConfirm = false;
bool g_leftMouseDown = false;
void* g_saveMenuObject = nullptr;
void* g_loadMenuObject = nullptr;
enum class PendingMenu { None, Save, Load };
PendingMenu g_pendingMenu = PendingMenu::None;

using RoomDispatcherFn = void(__fastcall*)(void* gameObject, void* edx);
RoomDispatcherFn g_roomDispatcher = nullptr;
using NativeSaveFn = bool(__fastcall*)(void* saves, void* edx, int slot);
using MetadataLoadFn = bool(__fastcall*)(void* saves, void* edx);
NativeSaveFn g_nativeSave = nullptr;
MetadataLoadFn g_metadataLoad = nullptr;
using MenuConstructorFn = void*(__fastcall*)(void* object, void* edx);
using InputQueryFn = bool(__fastcall*)(void* input, void* edx, int button);
MenuConstructorFn g_saveMenuConstructor = nullptr;
MenuConstructorFn g_titleMenuConstructor = nullptr;
InputQueryFn g_inputQuery = nullptr;
using NativeMenuControllerFn = int(__thiscall*)(void* context, int initialState);
const auto g_nativeMenuController = reinterpret_cast<NativeMenuControllerFn>(0x005896B0);

constexpr uintptr_t kRoomArgument = 0x005F3DB0;

constexpr DWORD kNoticeFadeInMs = 220;
constexpr DWORD kNoticeHoldMs = 1500;
constexpr DWORD kNoticeFadeOutMs = 420;

void SaveNow();
void LoadNow();

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

std::wstring SlotStatePath(int slot) {
  wchar_t name[40] = {};
  _snwprintf_s(name, _TRUNCATE, L"Save\\GBIO_%02d.SAV", slot + 1);
  return sf::g_dir + name;
}

uint32_t Checksum(const QuickState& state) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&state);
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < offsetof(QuickState, checksum); ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

bool WriteStateFile(const std::wstring& path, const QuickState& state) {
  CreateDirectoryW((sf::g_dir + L"Save").c_str(), nullptr);
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const BOOL ok = WriteFile(file, &state, sizeof state, &written, nullptr);
  CloseHandle(file);
  return ok && written == sizeof state;
}

bool ReadStateFile(const std::wstring& path, QuickState* state) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD read = 0;
  const BOOL ok = ReadFile(file, state, sizeof *state, &read, nullptr);
  CloseHandle(file);
  return ok && read == sizeof *state && state->magic == 0x31565351 && state->version == 6 &&
         state->checksum == Checksum(*state) && state->scene >= 0 && state->scene <= 0x79;
}

bool CaptureState(QuickState* state) {
  if (!state) return false;
  void* stats = *reinterpret_cast<void**>(game::kGameStats);
  void* gameState = *reinterpret_cast<void**>(game::kGameState);
  if (!stats || !gameState) return false;
  *state = {};
  state->scene = *reinterpret_cast<int*>(game::kSceneId);
  state->roomArgument = *reinterpret_cast<int*>(kRoomArgument);
  state->difficulty = *game::Field<uint8_t>(gameState, 0x42);
  state->playerValid = CapturePlayerQuickState(&state->player) ? 1 : 0;
  state->playerHp = game::Read<int>(stats, game::kGameStatsHp);
  std::memcpy(state->saveRecord.data(), game::Field<uint8_t>(stats, game::kLiveSaveRecord),
              state->saveRecord.size());
  std::memcpy(state->roomState.data(),
              game::Field<uint8_t>(gameState, game::kPersistentRoomState),
              state->roomState.size());
  state->zombieCount = static_cast<uint32_t>(
      zombie::CaptureQuickStates(state->zombies.data(), state->zombies.size()));
  state->checksum = Checksum(*state);
  return true;
}

bool PatchByte(uintptr_t address, uint8_t expected, uint8_t replacement) {
  auto* target = reinterpret_cast<uint8_t*>(address);
  if (*target != expected) return false;
  DWORD old = 0;
  if (!VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &old)) return false;
  *target = replacement;
  FlushInstructionCache(GetCurrentProcess(), target, 1);
  DWORD ignored = 0;
  VirtualProtect(target, 1, old, &ignored);
  return true;
}

bool PatchDword(uintptr_t address, uint32_t expected, uint32_t replacement) {
  auto* target = reinterpret_cast<uint32_t*>(address);
  if (*target != expected) return false;
  DWORD old = 0;
  if (!VirtualProtect(target, sizeof *target, PAGE_EXECUTE_READWRITE, &old)) return false;
  *target = replacement;
  FlushInstructionCache(GetCurrentProcess(), target, sizeof *target);
  DWORD ignored = 0;
  VirtualProtect(target, sizeof *target, old, &ignored);
  return true;
}

bool __fastcall MetadataLoadDetour(void* saves, void* edx) {
  const bool ok = g_metadataLoad(saves, edx);
  if (!ok || !saves) return ok;
  auto* records = game::Field<uint8_t>(saves, 0x90);
  for (int slot = 10; slot < 15; ++slot) {
    uint8_t* record = records + slot * 0x34;
    bool allZero = true;
    for (int i = 0; i < 0x34; ++i) allZero = allZero && record[i] == 0;
    if (allZero) record[0] = 1;  // empty native slot
  }
  return ok;
}

bool __fastcall NativeSaveDetour(void* saves, void* edx, int slot) {
  const bool ok = g_nativeSave(saves, edx, slot);
  if (!ok || slot < 0 || slot >= 15) return ok;
  QuickState state;
  const bool captured = g_menuSaveStateValid ? (state = g_menuSaveState, true) : CaptureState(&state);
  if (captured && WriteStateFile(SlotStatePath(slot), state)) {
    char notice[48] = {};
    _snprintf_s(notice, _TRUNCATE, "SLOT %d  SAVED", slot + 1);
    Notify(notice);
    Log("QuickSave: native slot %d saved with extended room/player state", slot + 1);
  } else {
    Notify("SAVE SLOT EXTENDED DATA FAILED");
    Log("QuickSave: native slot %d saved, but extended state could not be written", slot + 1);
  }
  g_menuSaveStateValid = false;
  return ok;
}

void* __fastcall SaveMenuConstructorDetour(void* object, void* edx) {
  void* result = g_saveMenuConstructor(object, edx);
  g_saveMenuObject = result;
  g_leftMouseDown = false;
  if (result && g_skipSaveIntro) {
    // Native state 7 normally shows the end-of-game RESULT page first.  F5
    // already comes from live gameplay, so enter the slot-list substate.
    *game::Field<int>(result, 0x04) = 2;
    g_skipSaveIntro = false;
    Log("QuickSave: skipped RESULT intro and entered native Save Game slots");
  }
  return result;
}

void* __fastcall TitleMenuConstructorDetour(void* object, void* edx) {
  void* result = g_titleMenuConstructor(object, edx);
  g_loadMenuObject = result;
  g_leftMouseDown = false;
  if (result && g_openLoadMenu) {
    // State 4 is the native Load Game slot list.  Its constructor already
    // initialized the one-based selection and first visible slot.
    *game::Field<int>(result, 0x00) = 4;
    *game::Field<int>(result, 0x1C) = 1;
    *game::Field<int>(result, 0x20) = 1;
    g_openLoadMenu = false;
    Log("QuickSave: title menu entered directly at native Load Game slots");
  }
  return result;
}

bool NativeSaveMenuActive() {
  return *reinterpret_cast<int*>(0x005FCFE4) == 7 && g_saveMenuObject;
}

bool NativeLoadMenuActive() {
  return *reinterpret_cast<int*>(0x005FCFE4) == 1 && g_loadMenuObject &&
         game::Read<int>(g_loadMenuObject, 0x00) == 4;
}

bool SelectNativeMenuRowFromCursor() {
  const bool save = NativeSaveMenuActive();
  const bool load = NativeLoadMenuActive();
  if (!save && !load) return false;

  void* menu = save ? g_saveMenuObject : g_loadMenuObject;
  const size_t selectionOffset = save ? 0x0C : 0x1C;
  const size_t scrollOffset = save ? 0x08 : 0x20;
  POINT cursor = {};
  RECT client = {};
  if (!g_hFocusWindow || !GetCursorPos(&cursor) ||
      !ScreenToClient(g_hFocusWindow, &cursor) ||
      !GetClientRect(g_hFocusWindow, &client)) {
    return false;
  }
  const int height = (std::max)(1L, client.bottom - client.top);
  const int nativeY = cursor.y * 240 / height;
  // The native list displays five rows.  Preserve its current scroll window
  // and translate the clicked row to the corresponding one-based slot.
  const int row = std::clamp((nativeY - 24) / 42, 0, 4);
  const int scroll = game::Read<int>(menu, scrollOffset);
  const int selection = std::clamp(scroll + row, 1, 15);
  *game::Field<int>(menu, selectionOffset) = selection;
  Log("QuickSave: mouse selected native %s slot %d (cursor y=%d, native y=%d)",
      save ? "save" : "load", selection, cursor.y, nativeY);
  return true;
}

bool __fastcall InputQueryDetour(void* input, void* edx, int button) {
  const bool menuActive = NativeSaveMenuActive() || NativeLoadMenuActive();
  if (!menuActive) g_leftMouseDown = false;
  if (menuActive) {
    auto* mapping = *reinterpret_cast<uint8_t**>(0x005FD0D4);
    if (mapping && button == mapping[0x0E]) {
      if (g_mouseConfirm) {
        g_mouseConfirm = false;
        return true;
      }
      // Gun Survivor normally consumes the mouse through DirectInput, so some
      // systems never deliver WM_LBUTTONDOWN to the wrapper window procedure.
      // Poll only during the native confirm query to provide an edge-triggered
      // fallback without producing repeated confirmations while held.
      const SHORT leftState = GetAsyncKeyState(VK_LBUTTON);
      const bool leftDown = (leftState & 0x8000) != 0;
      // Bit 0 records a press since the previous query, which matters when the
      // native Load Game screen is updating at 14-16 Hz and a short click is
      // fully released between two confirm polls.
      const bool pressed = (leftState & 1) != 0 || (leftDown && !g_leftMouseDown);
      g_leftMouseDown = leftDown;
      const bool directInputPress = SurvivorPc_ConsumeMouseLeftPress();
      if ((pressed || directInputPress) && SelectNativeMenuRowFromCursor()) return true;
    }
  }
  return g_inputQuery(input, edx, button);
}

bool OpenNativeMenu(bool save) {
  void* gameObject = *reinterpret_cast<void**>(game::kGameState);
  if (!gameObject || game::Read<uint8_t>(gameObject, game::kMainState) != 6) {
    Notify(save ? "F5  SAVE MENU UNAVAILABLE" : "F6  LOAD MENU UNAVAILABLE");
    return false;
  }
  if (save) {
    SurvivorPc_ConsumeMouseLeftPress();  // discard clicks queued during gameplay
    g_menuSaveStateValid = CaptureState(&g_menuSaveState);
    if (!g_menuSaveStateValid) {
      Notify("F5  SAVE MENU STATE UNAVAILABLE");
      return false;
    }
    g_saveMenuObject = nullptr;
    g_skipSaveIntro = true;
    g_pendingMenu = PendingMenu::Save;
    Notify("F5  SAVE MENU");
    Log("QuickSave: F5 requested native Save Game menu with a pre-transition extended snapshot");
  } else {
    SurvivorPc_ConsumeMouseLeftPress();
    g_loadMenuObject = nullptr;
    g_openLoadMenu = true;
    g_pendingMenu = PendingMenu::Load;
    Notify("F6  LOAD MENU");
    Log("QuickSave: F6 requested native Load Game menu");
  }
  return true;
}

void HandleSaveHotkey() {
  if (GetAsyncKeyState(VK_SHIFT) & 0x8000) SaveNow();
  else OpenNativeMenu(true);
}

void HandleLoadHotkey() {
  if (GetAsyncKeyState(VK_SHIFT) & 0x8000) LoadNow();
  else OpenNativeMenu(false);
}

void SaveNow() {
  QuickState state;
  if (!CaptureState(&state)) {
    Notify("F5  QUICK SAVE UNAVAILABLE");
    Log("QuickSave: F5 ignored because gameplay state is unavailable");
    return;
  }
  if (WriteStateFile(StatePath(), state)) {
    Notify("F5  QUICK SAVE SAVED");
    Log("QuickSave: saved scene %02X with %zu-byte native record, %zu bytes of room state, player %s, %u zombies",
        state.scene, state.saveRecord.size(), state.roomState.size(),
        state.playerValid ? "captured" : "unavailable", state.zombieCount);
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
  if (!ReadStateFile(StatePath(), &state)) {
    Notify("F6  QUICK LOAD INVALID");
    Log("QuickSave: QUICK.SAV missing or invalid");
    return;
  }
  if (state.scene == 0) {
    // Zero is also the native room-loop death sentinel.  It needs a separate
    // dispatcher entry hook before it can safely be used as a reload target.
    Notify("F6  QUICK LOAD UNSAFE ROOM");
    Log("QuickSave: scene 00 reload withheld because zero is the native death sentinel");
    return;
  }
  g_pendingReload = state;
  g_reloadStage = ReloadStage::Requested;
  Notify("F6  QUICK LOAD RESTORING");
  Log("QuickSave: queued native room reload to scene %02X argument %d",
      state.scene, state.roomArgument);
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
  if (f5 && !g_f5Down) HandleSaveHotkey();
  if (f6 && !g_f6Down) HandleLoadHotkey();
  g_f5Down = f5;
  g_f6Down = f6;
  if (g_pendingMenu != PendingMenu::None && gameObject) {
    const PendingMenu requested = g_pendingMenu;
    g_pendingMenu = PendingMenu::None;
    void* context = game::Read<void*>(gameObject, 0x35E0);
    if (context) {
      const int initialState = requested == PendingMenu::Save ? 7 : 1;
      Log("QuickSave: entering native modal menu controller at state %d", initialState);
      g_nativeMenuController(context, initialState);
      Log("QuickSave: native modal menu controller returned to gameplay");
    } else {
      g_openLoadMenu = false;
      g_skipSaveIntro = false;
      g_menuSaveStateValid = false;
      Notify("SAVE/LOAD MENU CONTEXT UNAVAILABLE");
      Log("QuickSave: native modal menu context at game+35E0 was unavailable");
    }
  }
  return nativeResult;
}

void QuickSavePollWindowToggle() {
  const bool f8 = Foreground() && (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
  if (f8 && !g_f8Down) ToggleFullscreen();
  g_f8Down = f8;
}

bool QuickSaveHandleWindowMessage(unsigned message, uintptr_t wparam, intptr_t lparam) {
  if (message == WM_LBUTTONUP) g_leftMouseDown = false;
  if ((NativeSaveMenuActive() || NativeLoadMenuActive()) && message == WM_MOUSEWHEEL) {
    void* menu = NativeSaveMenuActive() ? g_saveMenuObject : g_loadMenuObject;
    const size_t selectionOffset = NativeSaveMenuActive() ? 0x0C : 0x1C;
    const size_t scrollOffset = NativeSaveMenuActive() ? 0x08 : 0x20;
    int selection = game::Read<int>(menu, selectionOffset);
    const short wheel = static_cast<short>((wparam >> 16) & 0xFFFF);
    selection = std::clamp(selection + (wheel < 0 ? 1 : -1), 1, 15);
    int scroll = game::Read<int>(menu, scrollOffset);
    if (selection < scroll) scroll = selection;
    if (selection > scroll + 4) scroll = selection - 4;
    *game::Field<int>(menu, selectionOffset) = selection;
    *game::Field<int>(menu, scrollOffset) = std::clamp(scroll, 1, 11);
    return true;
  }
  if ((NativeSaveMenuActive() || NativeLoadMenuActive()) && message == WM_LBUTTONDOWN) {
    if (SelectNativeMenuRowFromCursor()) {
      g_mouseConfirm = true;  // consumed by the native confirm-button query
      return true;
    }
  }
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
      if (!g_f5Down) HandleSaveHotkey();
      g_f5Down = true;
      return true;
    case VK_F6:
      if (!g_f6Down) HandleLoadHotkey();
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

bool QuickSaveReloadPending() {
  return g_reloadStage == ReloadStage::Requested || g_reloadStage == ReloadStage::Fading;
}

bool QuickSaveReloadFadeStarted() { return g_reloadStage == ReloadStage::Fading; }

void QuickSaveStartReloadFade() {
  if (g_reloadStage == ReloadStage::Requested) g_reloadStage = ReloadStage::Fading;
}

bool QuickSaveApplyPendingReload(void* gameObject, int* scene) {
  if (!QuickSaveReloadPending() || !gameObject || !scene) return false;
  // Only request the native room exit here. The room function still has to
  // serialize and destroy the current room after RoomTick returns. Restoring
  // gameplay state before that cleanup lets the cleanup overwrite the save.
  *reinterpret_cast<int*>(kRoomArgument) = g_pendingReload.roomArgument;
  *scene = g_pendingReload.scene;
  g_reloadStage = ReloadStage::CleanupInProgress;
  Log("QuickSave: native room exit requested for scene %02X argument %d; restore deferred until cleanup",
      g_pendingReload.scene, g_pendingReload.roomArgument);
  return true;
}

void __fastcall RoomDispatcherDetour(void* gameObject, void* edx) {
  if (g_reloadStage == ReloadStage::NativeLoadQueued && gameObject) {
    void* stats = *reinterpret_cast<void**>(game::kGameStats);
    if (stats) {
      std::memcpy(game::Field<uint8_t>(stats, game::kLiveSaveRecord),
                  g_pendingReload.saveRecord.data(), g_pendingReload.saveRecord.size());
      *game::Field<int>(stats, game::kGameStatsHp) = g_pendingReload.playerHp;
      std::memcpy(game::Field<uint8_t>(gameObject, game::kPersistentRoomState),
                  g_pendingReload.roomState.data(), g_pendingReload.roomState.size());
      *game::Field<uint8_t>(gameObject, 0x42) = g_pendingReload.difficulty;
      *reinterpret_cast<int*>(kRoomArgument) = g_pendingReload.roomArgument;
      *reinterpret_cast<int*>(game::kSceneId) = g_pendingReload.scene;
      zombie::QueueQuickStateRestore(g_pendingReload.zombies.data(),
                                     (std::min<size_t>)(g_pendingReload.zombieCount,
                                                        g_pendingReload.zombies.size()));
      g_reloadStage = ReloadStage::WaitingForRoom;
      Log("QuickSave: extended native slot state applied before scene %02X construction",
          g_pendingReload.scene);
    } else {
      g_reloadStage = ReloadStage::None;
      Log("QuickSave: extended native slot state unavailable at room construction");
    }
  }
  g_roomDispatcher(gameObject, edx);
  if (g_reloadStage != ReloadStage::CleanupInProgress || !gameObject) return;
  void* stats = *reinterpret_cast<void**>(game::kGameStats);
  if (!stats) {
    g_reloadStage = ReloadStage::None;
    Notify("F6  QUICK LOAD FAILED");
    Log("QuickSave: native stats unavailable at the post-room restore boundary");
    return;
  }
  // The old room has now finished its own persistence and destruction. This
  // is the first boundary at which its writes can no longer corrupt the saved
  // inventory, event flags, or enemy-presence state.
  std::memcpy(game::Field<uint8_t>(stats, game::kLiveSaveRecord),
              g_pendingReload.saveRecord.data(), g_pendingReload.saveRecord.size());
  *game::Field<int>(stats, game::kGameStatsHp) = g_pendingReload.playerHp;
  std::memcpy(game::Field<uint8_t>(gameObject, game::kPersistentRoomState),
              g_pendingReload.roomState.data(), g_pendingReload.roomState.size());
  *game::Field<uint8_t>(gameObject, 0x42) = g_pendingReload.difficulty;
  *reinterpret_cast<int*>(kRoomArgument) = g_pendingReload.roomArgument;
  *reinterpret_cast<int*>(game::kSceneId) = g_pendingReload.scene;
  zombie::QueueQuickStateRestore(g_pendingReload.zombies.data(),
                                 (std::min<size_t>)(g_pendingReload.zombieCount,
                                                    g_pendingReload.zombies.size()));
  g_reloadStage = ReloadStage::WaitingForRoom;
  Notify("F6  QUICK LOAD RESTORED");
  Log("QuickSave: restored native record and room/event state after cleanup; scene %02X queued",
      g_pendingReload.scene);
}

bool QuickSaveConsumeRoomEntry() {
  if (g_reloadStage != ReloadStage::WaitingForRoom) return false;
  g_reloadStage = ReloadStage::None;
  if (g_pendingReload.playerValid) QueuePlayerQuickStateRestore(g_pendingReload.player);
  Log("QuickSave: reconstructed scene %02X through the native room dispatcher",
      *reinterpret_cast<int*>(game::kSceneId));
  return true;
}

void QuickSaveOnNativeLoad(int slot, bool succeeded) {
  if (!succeeded || slot < 0 || slot >= 15) return;
  QuickState state;
  if (!ReadStateFile(SlotStatePath(slot), &state)) {
    Log("QuickSave: native slot %d has no valid extended sidecar; native state kept", slot + 1);
    return;
  }
  g_pendingReload = state;
  g_reloadStage = ReloadStage::NativeLoadQueued;
  char notice[48] = {};
  _snprintf_s(notice, _TRUNCATE, "SLOT %d  LOADING", slot + 1);
  Notify(notice);
  Log("QuickSave: native slot %d queued extended state for scene %02X", slot + 1, state.scene);
}

bool InstallQuickSave() {
  const game::ByteSignature dispatcher = {game::kRoomDispatcher, game::kRoomDispatcherBytes,
                                          sizeof(game::kRoomDispatcherBytes)};
  const uint8_t metadataBytes[] = {0x53, 0x8B, 0xD9, 0x56, 0x57};
  const uint8_t nativeSaveBytes[] = {0x53, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10};
  const uint8_t saveMenuCtorBytes[] = {0x8B, 0xC1, 0xB9, 0x01, 0x00, 0x00, 0x00};
  const uint8_t titleMenuCtorBytes[] = {0x8B, 0xC1, 0x33, 0xD2, 0xB9, 0x01, 0x00, 0x00, 0x00};
  const uint8_t inputQueryBytes[] = {0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C, 0x8B, 0xF1};
  const bool slotsPatched =
      game::CheckBytes({0x004646C0, metadataBytes, sizeof metadataBytes}) &&
      game::CheckBytes({0x004648B0, nativeSaveBytes, sizeof nativeSaveBytes}) &&
      PatchDword(0x004646CC, 0x82, 0xC3) &&
      PatchByte(0x004646D9, 0x0A, 0x0F) &&
      PatchByte(0x00585837, 0x0A, 0x0F) &&
      PatchByte(0x0058626E, 0x0A, 0x0F) &&
      Hook("SaveMetadataLoad", 0x004646C0, reinterpret_cast<void*>(&MetadataLoadDetour),
           reinterpret_cast<void**>(&g_metadataLoad)) &&
      Hook("NativeSaveSlot", 0x004648B0, reinterpret_cast<void*>(&NativeSaveDetour),
           reinterpret_cast<void**>(&g_nativeSave)) &&
      game::CheckBytes({0x00585A60, saveMenuCtorBytes, sizeof saveMenuCtorBytes}) &&
      game::CheckBytes({0x00584A40, titleMenuCtorBytes, sizeof titleMenuCtorBytes}) &&
      game::CheckBytes({0x00583C00, inputQueryBytes, sizeof inputQueryBytes}) &&
      Hook("SaveMenuConstructor", 0x00585A60, reinterpret_cast<void*>(&SaveMenuConstructorDetour),
           reinterpret_cast<void**>(&g_saveMenuConstructor)) &&
      Hook("TitleMenuConstructor", 0x00584A40, reinterpret_cast<void*>(&TitleMenuConstructorDetour),
           reinterpret_cast<void**>(&g_titleMenuConstructor)) &&
      Hook("MenuMouseConfirm", 0x00583C00, reinterpret_cast<void*>(&InputQueryDetour),
           reinterpret_cast<void**>(&g_inputQuery));
  if (!slotsPatched) {
    Log("QuickSave: 15-slot native save integration unavailable; original menu retained");
  } else {
    Log("QuickSave: native Save/Load menus expanded to 15 slots with extended sidecars");
  }
  if (!game::CheckBytes(dispatcher) ||
      !Hook("RoomDispatcher", game::kRoomDispatcher,
            reinterpret_cast<void*>(&RoomDispatcherDetour),
            reinterpret_cast<void**>(&g_roomDispatcher))) {
    Log("QuickSave: room dispatcher hook unavailable; F5/F6 disabled");
    return false;
  }
  Log("QuickSave: F5/F6 open native Save/Load menus; mouse wheel/click enabled; Shift+F5/F6 keep immediate quick actions");
  return true;
}

}  // namespace sf::features

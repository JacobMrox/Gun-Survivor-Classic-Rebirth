#include <windows.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include "../game.h"
#include "../hooks.h"
#include "../log.h"
#include "features.h"
#include "debug_menu_input.h"
#include "debug_utility.h"
#include "../../aim_shared.h"
#include "../survivor_pc.h"
#include "../config.h"
#include "../xinput_logic.h"

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
bool g_f2Down = false;
bool g_f10Down = false;
bool g_debugOpen = false;
bool g_debugUtility = false;
UtilityAction g_utilityPending = UtilityAction::None;
std::atomic<bool> g_infiniteAmmo{false}, g_infiniteHealth{false}, g_invincible{false};
bool g_ammoAvailable = false, g_healthAvailable = false;
using AmmoCountFn = int(__fastcall*)(void*, void*);
using AmmoUseFn = void(__fastcall*)(void*, void*);
using DamageFn = void(__fastcall*)(void*, void*, int);
using InventoryDrawFn = void(__fastcall*)(void*, void*, int, int, int, int, int, int);
AmmoCountFn g_ammoCount = nullptr;
AmmoUseFn g_ammoUse = nullptr;
DamageFn g_damage = nullptr;
InventoryDrawFn g_inventoryDraw = nullptr;

bool IsPlayerStats(void* stats) {
  return stats && stats == *reinterpret_cast<void**>(game::kGameStats);
}
int __fastcall AmmoCountDetour(void* stats, void* edx) {
  return UtilityAmmoCount(g_infiniteAmmo.load(), IsPlayerStats(stats), g_ammoCount(stats, edx));
}
void __fastcall AmmoUseDetour(void* stats, void* edx) {
  const bool playerStats = IsPlayerStats(stats);
  if (!UtilitySuppressAmmoUse(g_infiniteAmmo.load(), playerStats)) g_ammoUse(stats, edx);
  // Both PC callers (0042C83D and 0046A99D) have passed the native firing checks.
  if (playerStats) ScreenFxOnPlayerShot();
}
void __fastcall DamageDetour(void* stats, void* edx, int damage) {
  // Infinite health refills HP after the room tick; invincibility never lets the damage land.
  const bool blocked = g_infiniteHealth.load() || g_invincible.load();
  if (!UtilitySuppressDamage(blocked, IsPlayerStats(stats), damage)) g_damage(stats, edx, damage);
}
void __fastcall InventoryDrawDetour(void* menu, void* edx, int item, int amount,
                                    int variant, int row, int y, int x) {
  g_inventoryDraw(menu, edx, item, UtilityInventoryAmount(g_infiniteAmmo.load(), item, amount),
                  variant, row, y, x);
}
DebugMenuInputGate g_debugInputGate;
bool g_debugUpDown = false, g_debugDownDown = false, g_debugLeftDown = false, g_debugRightDown = false, g_debugEnterDown = false, g_debugEscDown = false;
int g_debugSel = 1;
int g_debugRow = 0;  // 0 JUMP, 1..6 dummy rows (RE2 layout)
int g_spawnChoice = 0;  // utility row 9: index into kDebugSpawnChoices
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
// In-game F6 bookkeeping. g_inGameLoadMenu is set from the moment gameplay
// requests the native Load menu until its modal controller returns, and
// g_loadMenuSawSlots once the slot list (state 4) has actually been shown.
// Together they let the tick detour release the controller on back-out
// instead of parking on the title root over live gameplay.
bool g_inGameLoadMenu = false;
bool g_loadMenuSawSlots = false;

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
    g_loadMenuSawSlots = true;
    Log("QuickSave: title menu entered directly at native Load Game slots");
  }
  return result;
}

using TitleMenuTickFn = void(__thiscall*)(void* menu);
TitleMenuTickFn g_titleMenuTick = nullptr;

void __fastcall TitleMenuTickDetour(void* menu, void*) {
  g_titleMenuTick(menu);  // draw first (still the state the tick began in), then input
  if (!g_inGameLoadMenu || !g_loadMenuSawSlots || !menu) return;
  if (game::Read<int>(menu, 0x00) == 4) return;  // still on the slot list
  auto* runFlag = reinterpret_cast<uint8_t*>(game::kMenuRunFlag);
  if (*runFlag == 0) return;  // confirm or F9 is already leaving the controller
  // Back-out from the in-game Load menu: the native controller would keep
  // looping on the title root (states 1/2) over live gameplay and its BGM.
  // The draw of this tick already ran for the slot list, so clearing the run
  // flag returns to the room with no title frame in between.
  *runFlag = 0;
  g_inGameLoadMenu = false;
  g_loadMenuSawSlots = false;
  Log("QuickSave: in-game Load menu backed out at state %d; controller released to gameplay",
      game::Read<int>(menu, 0x00));
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
    if (g_inGameLoadMenu) {
      // The modal Load menu is already open; a second F6 must not queue
      // another menu session behind the current one.
      Log("QuickSave: F6 ignored because the in-game Load menu is already open");
      return false;
    }
    SurvivorPc_ConsumeMouseLeftPress();
    g_loadMenuObject = nullptr;
    g_openLoadMenu = true;
    g_inGameLoadMenu = true;
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

// One press of a menu input. The keyboard poll and the pad both feed these bits, so the row
// changes and the confirm handling exist once instead of twice.
enum MenuPress : unsigned {
  kMenuUp = 1u << 0,
  kMenuDown = 1u << 1,
  kMenuLeft = 1u << 2,
  kMenuRight = 1u << 3,
  kMenuConfirm = 1u << 4,
  kMenuBack = 1u << 5,
};

void DebugMenuHandlePresses(unsigned presses) {
  if (presses & kMenuBack) {
    if (g_debugUtility) { g_debugUtility = false; g_debugRow = 1; }
    else { g_debugOpen = false; Log("DebugMenu: closed via ESC"); }
  }
  const int lastRow = 11;
  if (presses & kMenuUp) g_debugRow = (g_debugRow > 0) ? g_debugRow - 1 : lastRow;
  if (presses & kMenuDown) g_debugRow = (g_debugRow < lastRow) ? g_debugRow + 1 : 0;
  if (!g_debugUtility && g_debugRow == 0 && (presses & kMenuLeft))
    g_debugSel = (g_debugSel > 1) ? g_debugSel - 1 : 0x79;
  if (!g_debugUtility && g_debugRow == 0 && (presses & kMenuRight))
    g_debugSel = (g_debugSel < 0x79) ? g_debugSel + 1 : 1;
  if (g_debugUtility && g_debugRow == 9 && (presses & kMenuLeft)) DebugSpawnChoiceCycle(-1);
  if (g_debugUtility && g_debugRow == 9 && (presses & kMenuRight)) DebugSpawnChoiceCycle(1);
  if (!(presses & kMenuConfirm)) return;
  if (g_debugUtility) {
    const UtilityAction action = UtilityActionForRow(g_debugRow);
    if (g_debugRow == 4 && g_ammoAvailable) {
      const bool enabled = !g_infiniteAmmo.load();
      g_infiniteAmmo.store(enabled);
      Notify(enabled ? "INFINITE AMMO ON" : "INFINITE AMMO OFF");
    } else if (g_debugRow == 5 && g_healthAvailable) {
      const bool enabled = !g_infiniteHealth.load();
      g_infiniteHealth.store(enabled);
      Notify(enabled ? "INFINITE HEALTH ON" : "INFINITE HEALTH OFF");
    } else if (g_debugRow == 6) {
      const bool enabled = !SurvivorPc_DynamicModelShadows();
      SurvivorPc_SetDynamicModelShadows(enabled);
      Notify(enabled ? "DYNAMIC SHADOWS ON" : "DYNAMIC SHADOWS OFF");
      Log("DebugMenu: dynamic model shadows %s", enabled ? "on" : "off");
    } else if (g_debugRow == 7) {
      const bool enabled = !SurvivorPc_ShootFlash();
      SurvivorPc_SetShootFlash(enabled);
      Notify(enabled ? "SHOOTING FLASH ON" : "SHOOTING FLASH OFF");
      Log("DebugMenu: shooting flash %s", enabled ? "on" : "off");
    } else if (g_debugRow == 8 && g_healthAvailable) {
      const bool enabled = !g_invincible.load();
      g_invincible.store(enabled);
      Notify(enabled ? "INVINCIBLE ON" : "INVINCIBLE OFF");
      Log("DebugMenu: invincibility %s", enabled ? "on" : "off");
    } else if (g_debugRow == 9) {
      const char* name = kDebugSpawnChoices[g_spawnChoice].name;
      char notice[48] = {};
      _snprintf_s(notice, _TRUNCATE, DebugSpawnChosen() ? "SPAWNED %s" : "SPAWN %s FAILED", name);
      Notify(notice);
      Log("DebugMenu: spawn row chose %s", name);
    } else if (g_debugRow == 10) {
      DebugReturnToTitle();
    } else if (g_debugRow == 11) {
      DebugQuitGame();
    } else if (action == UtilityAction::Unavailable) Notify("UNIMPLEMENTED");
    else { g_utilityPending = action; g_debugOpen = false; }
  } else if (g_debugRow == 1) {
    g_debugUtility = true;
    g_debugRow = 0;
  } else if (g_debugRow != 0) {
    Notify("UNIMPLEMENTED");
    Log("DebugMenu: row %d unimplemented", g_debugRow);
  } else {
    const int target = g_debugSel;
    g_debugOpen = false;
    DebugQueueJump(target);
  }
}

// ---- quit to title -----------------------------------------------------------------------------
// A room can only be left by its own exit: the room function then serializes and destroys the room
// and the dispatcher runs the scene entry 0046C110 afterwards. For the title the wrapper takes that
// entry over, because scene 0 is the NEW GAME entry: main state 0 (0046A076) is the boot state, and
// it sets the title up (0058AC40) and enters state 2 by itself.
bool g_returnToTitle = false;
DWORD g_returnToTitleTick = 0;
using RoomSceneEntryFn = void(__cdecl*)(int scene, int roomArgument);
RoomSceneEntryFn g_roomSceneEntry = nullptr;

void __cdecl RoomSceneEntryDetour(int scene, int roomArgument) {
  const bool requested = g_returnToTitle && GetTickCount() - g_returnToTitleTick < 3000;
  if (!requested || scene != 0) {
    if (g_returnToTitle && !requested) g_returnToTitle = false;  // stale request: native entry
    g_roomSceneEntry(scene, roomArgument);
    return;
  }
  g_returnToTitle = false;
  void* game = *reinterpret_cast<void**>(game::kGameState);
  if (game) {
    *reinterpret_cast<int*>(game::kSceneId) = 0;
    *reinterpret_cast<int*>(kRoomArgument) = 0;
    *game::Field<uint8_t>(game, game::kMainState) = static_cast<uint8_t>(game::kMainStateBoot);
    *game::Field<uint8_t>(game, game::kMainStateFirstFrame) = 0;
    *game::Field<uint8_t>(game, game::kMainStateFrameFlag) = 0;
  }
  Log("DebugMenu: room closed for the title; native boot state queued (scene entry %d/%d)",
      scene, roomArgument);
}

void ToggleDebugMenuInternal() {
  g_debugOpen = !g_debugOpen;
  if (!g_debugOpen) {
    Log("DebugMenu: closed");
    Notify("DEBUG CLOSE");
    return;
  }
  g_debugInputGate.Capture();
  g_debugUtility = false;
  const int current = *reinterpret_cast<int*>(game::kSceneId);
  g_debugSel = (current > 0 && current <= 0x79) ? current : 1;
  g_debugRow = 0;
  Log("DebugMenu: opened, current scene %02X", current);
  Notify("DEBUG OPEN");
}

}  // namespace

int QuickSaveAfterRoomTick(void* gameObject, int nativeResult) {
  if (g_infiniteHealth.load()) {
    void* stats = *reinterpret_cast<void**>(game::kGameStats);
    if (stats) {
      int* hp = game::Field<int>(stats, game::kGameStatsHp);
      const int restored = UtilityRestoredHealth(true, *hp);
      if (restored != *hp) *hp = restored;
    }
  }
  if (UtilityActionReady(g_utilityPending, g_debugInputGate.Captured())) {
    const UtilityAction action = g_utilityPending;
    g_utilityPending = UtilityAction::None;
    switch (action) {
      case UtilityAction::QuickSave: SaveNow(); break;
      case UtilityAction::QuickLoad: LoadNow(); break;
      case UtilityAction::SaveMenu: OpenNativeMenu(true); break;
      case UtilityAction::LoadMenu: OpenNativeMenu(false); break;
      default: break;
    }
  }
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
      // The controller frees its menu objects on exit; the mouse helpers must
      // not keep the dangling pointers, and every modal session ends with the
      // in-game Load bookkeeping cleared regardless of how it was left.
      g_loadMenuObject = nullptr;
      g_saveMenuObject = nullptr;
      g_inGameLoadMenu = false;
      g_loadMenuSawSlots = false;
    } else {
      g_openLoadMenu = false;
      g_skipSaveIntro = false;
      g_menuSaveStateValid = false;
      g_inGameLoadMenu = false;
      g_loadMenuSawSlots = false;
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
  // RE2-style debug menu: F2 or F10 toggle via the EndScene poll (DirectInput
  // eats WM_KEYDOWN for F-keys, so the window-message path never fires).
  const bool fg = Foreground();
  const bool f2 = fg && (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
  const bool f10 = fg && (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
  // The menu is opt-in ([SurvivorFix] DebugEnable): without it F2 and F10 keep doing nothing,
  // exactly as before the menu existed.
  if (sf::g_config.debugEnable && (f2 || f10) && !g_f2Down && !g_f10Down) ToggleDebugMenuInternal();
  g_f2Down = f2;
  g_f10Down = f10;
  if (g_debugOpen && fg) {
    const bool up = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
    const bool down = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
    const bool left = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
    const bool right = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
    const bool enter = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    const bool esc = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    unsigned presses = 0;
    if (esc && !g_debugEscDown) presses |= kMenuBack;
    if (up && !g_debugUpDown) presses |= kMenuUp;
    if (down && !g_debugDownDown) presses |= kMenuDown;
    if (left && !g_debugLeftDown) presses |= kMenuLeft;
    if (right && !g_debugRightDown) presses |= kMenuRight;
    if (enter && !g_debugEnterDown) presses |= kMenuConfirm;
    g_debugEscDown = esc;
    g_debugUpDown = up;
    g_debugDownDown = down;
    g_debugLeftDown = left;
    g_debugRightDown = right;
    g_debugEnterDown = enter;
    if (presses) DebugMenuHandlePresses(presses);
  }
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
                        wparam == VK_F8 || wparam == VK_F9 || wparam == VK_F10 || wparam == VK_F2;
  if (!isHotkey) return false;
  if (message == WM_KEYUP || message == WM_SYSKEYUP) {
    if (wparam == VK_F5) g_f5Down = false;
    if (wparam == VK_F6) g_f6Down = false;
    if (wparam == VK_F8) g_f8Down = false;
    return wparam == VK_F5 || wparam == VK_F6 || wparam == VK_F8;
  }
  if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN) return false;
  const bool repeated = (static_cast<uintptr_t>(lparam) & (uintptr_t{1} << 30)) != 0;
  if (repeated) return wparam == VK_F5 || wparam == VK_F6 || wparam == VK_F8 ||
                        (sf::g_config.debugEnable && (wparam == VK_F10 || wparam == VK_F2));
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
    case VK_F2:
    case VK_F10:
      // The toggle itself is handled by the EndScene poll (DirectInput). When the menu is off
      // the key stays the game's own.
      return sf::g_config.debugEnable;
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

void ToggleDebugMenu() {
  if (!Foreground() || !sf::g_config.debugEnable) return;
  ToggleDebugMenuInternal();
}

void DebugQuitGame() {
  if (!g_hFocusWindow || !IsWindow(g_hFocusWindow)) {
    Notify("QUIT GAME UNAVAILABLE");
    Log("DebugMenu: quit ignored because the game window is unavailable");
    return;
  }
  Notify("QUIT GAME");
  Log("DebugMenu: quit requested; the game's own window shutdown runs");
  PostMessageW(g_hFocusWindow, WM_CLOSE, 0, 0);
}

void DebugReturnToTitle() {
  void* game = *reinterpret_cast<void**>(game::kGameState);
  if (!g_roomSceneEntry || !game || game::Read<uint8_t>(game, game::kMainState) != 6) {
    Notify("QUIT TO TITLE NEEDS A ROOM");
    Log("DebugMenu: quit to title ignored (scene entry %s, game object %s)",
        g_roomSceneEntry ? "ready" : "unavailable", game ? "present" : "missing");
    return;
  }
  g_returnToTitle = true;
  g_returnToTitleTick = GetTickCount();
  g_debugOpen = false;
  Notify("RETURNING TO TITLE");
  Log("DebugMenu: requested the title from scene %02X", *reinterpret_cast<int*>(game::kSceneId));
}

int DebugSpawnChoice() { return g_spawnChoice; }

void DebugSpawnChoiceCycle(int delta) {
  g_spawnChoice = DebugSpawnChoiceWrap(g_spawnChoice + delta);
  char notice[48] = {};
  _snprintf_s(notice, _TRUNCATE, "SPAWN: %s", kDebugSpawnChoices[g_spawnChoice].name);
  Notify(notice);
}

bool DebugSpawnChosen() {
  const SpawnChoice& choice = kDebugSpawnChoices[g_spawnChoice];
  if (!choice.item && !DebugSpawnTypeHasModel(choice.type)) {
    // The spawn table's default block still indexes the enemy manager with a -1 handle, so a type
    // without a case crashes instead of drawing nothing. Refuse it and say why.
    Log("DebugSpawn: type %02X has no NAB model in this install; spawn refused", choice.type);
    Notify("NO MODEL FOR THAT TYPE");
    return false;
  }
  return choice.item ? DebugSpawnItemInFront() : DebugSpawnEntity(choice.type);
}

bool DebugReturnToTitleRequested() {
  if (!g_returnToTitle) return false;
  if (GetTickCount() - g_returnToTitleTick <= 3000) return true;
  g_returnToTitle = false;  // the room never reached its exit: drop the request
  return false;
}

// The pad is polled by the DirectInput hook, which never touches Win32 key state, so the menu
// cannot see pad keys through its GetAsyncKeyState poll. The hook hands the pad state here
// instead and only a fresh press acts, so a held d-pad does not run down the list.
void DebugMenuPadNav(uint32_t nav) {
  static uint32_t held = 0;
  const uint32_t pressed = nav & ~held;
  held = nav;
  if (!pressed || !g_debugOpen || !Foreground()) return;
  unsigned presses = 0;
  if (pressed & sf::pad::menunav::Up) presses |= kMenuUp;
  if (pressed & sf::pad::menunav::Down) presses |= kMenuDown;
  if (pressed & sf::pad::menunav::Left) presses |= kMenuLeft;
  if (pressed & sf::pad::menunav::Right) presses |= kMenuRight;
  if (pressed & sf::pad::menunav::Confirm) presses |= kMenuConfirm;
  if (pressed & sf::pad::menunav::Back) presses |= kMenuBack;
  if (presses) DebugMenuHandlePresses(presses);
}

void QuickSaveHotkey(bool save) {
  if (!Foreground()) return;
  if (save) HandleSaveHotkey();
  else HandleLoadHotkey();
}

void ToggleFullscreenWindow() {
  if (!Foreground()) return;
  ToggleFullscreen();
}

bool DebugMenuVisible() { return g_debugOpen; }
bool DebugMenuUtilityVisible() { return g_debugUtility; }
bool DebugUtilityCheatAvailable(int row) {
  switch (row) {
    case 6: return true;               // dynamic shadows can always be switched
    case 7: return true;               // the shooting flash can always be switched
    case 8: return g_healthAvailable;  // invincibility rides on the damage hook
    default: break;
  }
  return row == 4 ? g_ammoAvailable : row == 5 && g_healthAvailable;
}
bool DebugUtilityCheatEnabled(int row) {
  switch (row) {
    case 6: return SurvivorPc_DynamicModelShadows();
    case 7: return SurvivorPc_ShootFlash();
    case 8: return g_invincible.load();
    default: break;
  }
  return row == 4 ? g_infiniteAmmo.load() : row == 5 && g_infiniteHealth.load();
}
bool DebugMenuSuppressPad(uint32_t pad) {
  const int keys[] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_ESCAPE, VK_F2, VK_F10};
  bool keyDown = false;
  for (int key : keys) keyDown |= (GetAsyncKeyState(key) & 0x8000) != 0;
  return g_debugInputGate.Filter(g_debugOpen, keyDown, pad);
}
int DebugMenuSelected() { return g_debugSel; }
int DebugMenuRow() { return g_debugRow; }
int DebugMenuCurrentScene() { return *reinterpret_cast<int*>(game::kSceneId); }

// Room jump through the same scene-load path as quick load (QuickSaveApplyPendingReload):
// snapshot live state so inventory/flags survive, only the scene changes.
// Exit inventory/status (mode 6) to gameplay (3) first, otherwise the modal
// menu state rides across the room exit and pops back up in the new room.
// Player origin is NOT restored: the old XZ is outside new-room geometry and
// strands the player; let the new room place them at its entry.
void DebugQueueJump(int scene) {
  if (scene <= 0 || scene > 0x79) { Log("DebugMenu: invalid scene %02X ignored", scene); return; }
  void* gameObject = *reinterpret_cast<void**>(game::kGameState);
  if (gameObject) {
    // +351C main state 6 = rooms, +41 mode 3 = gameplay (see screen_fx/game.h).
    *game::Field<uint8_t>(gameObject, game::kMainState) = 6;
    *game::Field<uint8_t>(gameObject, game::kGameModeOffset) = 3;
  }
  QuickState state;
  if (!CaptureState(&state)) { Notify("F10  JUMP UNAVAILABLE"); Log("DebugMenu: jump ignored, state unavailable"); return; }
  state.scene = scene;
  state.roomArgument = 0;
  state.playerValid = 0;
  memset(&state.player, 0, sizeof(state.player));
  state.checksum = Checksum(state);
  g_pendingReload = state;
  g_reloadStage = ReloadStage::Requested;
  char notice[48] = {};
  snprintf(notice, sizeof(notice), "F10  SCENE %02X", scene);
  Notify(notice);
  Log("DebugMenu: queued room jump to scene %02X (safe spawn)", scene);
}

bool QuickSaveReloadFadeStarted() { return g_reloadStage == ReloadStage::Fading; }

void QuickSaveStartReloadFade() {
  if (g_reloadStage == ReloadStage::Requested) g_reloadStage = ReloadStage::Fading;
}

bool QuickSaveApplyPendingReload(void* gameObject, int* scene) {
  if (!gameObject || !scene) return false;
  if (g_returnToTitle) {
    if (GetTickCount() - g_returnToTitleTick <= 3000) {
      *scene = 0;  // the room's own exit; RoomSceneEntryDetour turns it into the title
      Log("DebugMenu: room exit requested for the title");
      return true;
    }
    g_returnToTitle = false;
  }
  if (!QuickSaveReloadPending()) return false;
  // Only request the native room exit here. The room function still has to
  // serialize and destroy the current room after RoomTick returns. Restoring
  // gameplay state before that cleanup lets the cleanup overwrite the save.
  *reinterpret_cast<int*>(kRoomArgument) = g_pendingReload.roomArgument;
  // Scene 0 through 0x46C110 is the return-to-title sentinel, never a legal
  // transition target. A zero scene (native-only slot, or a sidecar captured
  // during the bootstrap) exits to the current room instead; the post-cleanup
  // then parks kSceneId at 0 so the next dispatch runs the scene-0 bootstrap,
  // which routes to the scene the restored native record itself names.
  const int target =
      g_pendingReload.scene != 0 ? g_pendingReload.scene : *reinterpret_cast<int*>(game::kSceneId);
  *scene = target;
  g_reloadStage = ReloadStage::CleanupInProgress;
  Log("QuickSave: native room exit requested for scene %02X argument %d; restore deferred until cleanup",
      target, g_pendingReload.roomArgument);
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
  Notify("LOADED SUCCESSFULLY.");
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
  void* gameObject = *reinterpret_cast<void**>(game::kGameState);
  void* stats = *reinterpret_cast<void**>(game::kGameStats);
  // Computed before the menu flags are cleared: the in-game F6 confirm runs
  // with kMainState 6, the title confirm with kMainState 3.
  const bool inGame =
      g_inGameLoadMenu || (gameObject && game::Read<uint8_t>(gameObject, game::kMainState) == 6);
  g_inGameLoadMenu = false;
  g_loadMenuSawSlots = false;
  char notice[48] = {};
  _snprintf_s(notice, _TRUNCATE, "SLOT %d  LOADING", slot + 1);
  Notify(notice);
  QuickState state;
  if (ReadStateFile(SlotStatePath(slot), &state)) {
    g_pendingReload = state;
    // In-game loads ride the Shift+F6 reload machinery (fade, room exit,
    // post-cleanup restore); the title confirm keeps the pre-construction
    // queue that the scene-0 bootstrap consumes.
    g_reloadStage = inGame ? ReloadStage::Requested : ReloadStage::NativeLoadQueued;
    Log("QuickSave: native slot %d queued extended state for scene %02X (%s)", slot + 1,
        state.scene, inGame ? "in-game reload" : "title bootstrap");
    return;
  }
  // Native-only slot: no valid QSV1 sidecar. No save format stores the scene,
  // so an in-game load cannot name a room directly; it captures the record the
  // native loader just read and drops through the vanilla scene-0 bootstrap,
  // which routes to the scene that record names, exactly like a title load.
  if (!inGame || !stats || !gameObject) {
    Log("QuickSave: native slot %d has no valid extended sidecar; native state kept", slot + 1);
    return;
  }
  g_pendingReload = QuickState{};
  std::memcpy(g_pendingReload.saveRecord.data(), game::Field<uint8_t>(stats, game::kLiveSaveRecord),
              g_pendingReload.saveRecord.size());
  g_pendingReload.playerHp = game::Read<int>(stats, game::kGameStatsHp);
  g_pendingReload.difficulty = *game::Field<uint8_t>(
      stats, game::kSlotRecords + slot * game::kSlotRecordSize + game::kSlotRecordDifficulty);
  // roomState, player and zombies stay zero: a native-only slot carries no
  // room, player or enemy state, so the room restarts as a fresh title load.
  g_pendingReload.scene = 0;
  g_pendingReload.roomArgument = 0;
  g_pendingReload.playerValid = 0;
  g_pendingReload.zombieCount = 0;
  g_pendingReload.checksum = Checksum(g_pendingReload);
  g_reloadStage = ReloadStage::Requested;
  Log("QuickSave: native slot %d has no sidecar; in-game reload captured the native record "
      "(difficulty %u) and falls back to the scene-0 bootstrap",
      slot + 1, g_pendingReload.difficulty);
}

bool InstallQuickSave() {
  // PC shot path 0046A61C queries the selected ammunition and 0046A99D
  // consumes it after a shot; 00463E70 subtracts health for enemy damage.
  const uint8_t ammoCountBytes[] = {0x8B, 0x41, 0x1C, 0x8B, 0x44, 0x81, 0x20, 0xC3};
  const uint8_t ammoUseBytes[] = {0x8B, 0x41, 0x1C, 0x8D, 0x44, 0x81, 0x20, 0xFF, 0x08, 0xC3};
  const uint8_t damageBytes[] = {0xA0, 0x78, 0x3D, 0x5F, 0x00, 0x84, 0xC0};
  const uint8_t inventoryDrawBytes[] = {0x83, 0xEC, 0x5C, 0x8B, 0x44, 0x24, 0x6C};
  if (game::CheckBytes({0x004645A0, ammoCountBytes, sizeof ammoCountBytes}) &&
      game::CheckBytes({0x00464630, ammoUseBytes, sizeof ammoUseBytes}) &&
      Hook("UtilityAmmoCount", 0x004645A0, reinterpret_cast<void*>(&AmmoCountDetour), reinterpret_cast<void**>(&g_ammoCount)) &&
      Hook("UtilityAmmoUse", 0x00464630, reinterpret_cast<void*>(&AmmoUseDetour), reinterpret_cast<void**>(&g_ammoUse)))
    g_ammoAvailable = true;
  if (game::CheckBytes({0x00463E70, damageBytes, sizeof damageBytes}) &&
      Hook("UtilityDamage", 0x00463E70, reinterpret_cast<void*>(&DamageDetour), reinterpret_cast<void**>(&g_damage)))
    g_healthAvailable = true;
  if (!game::CheckBytes({0x00587A70, inventoryDrawBytes, sizeof inventoryDrawBytes}) ||
      !Hook("UtilityInventoryDraw", 0x00587A70, reinterpret_cast<void*>(&InventoryDrawDetour),
            reinterpret_cast<void**>(&g_inventoryDraw)))
    Log("DebugUtility: inventory infinity display unavailable; ammo toggle remains usable");
  Log("DebugUtility: ammo hook %s; health hook %s", g_ammoAvailable ? "ready" : "unavailable", g_healthAvailable ? "ready" : "unavailable");
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
  // Bug 1 fix: release the modal controller when the in-game Load menu is
  // backed out of, so it cannot park on the title root over live gameplay.
  if (!game::CheckBytes({game::kTitleMenuTick, game::kTitleMenuTickBytes,
                         sizeof(game::kTitleMenuTickBytes)}) ||
      !Hook("TitleMenuTick", game::kTitleMenuTick, reinterpret_cast<void*>(&TitleMenuTickDetour),
            reinterpret_cast<void**>(&g_titleMenuTick))) {
    Log("QuickSave: title menu tick hook unavailable; in-game Load menu back-out keeps native behaviour");
  }
  // Quit to title: the scene entry runs after the room has cleaned itself up, which is exactly the
  // boundary the title needs.
  if (game::CheckBytes({game::kRoomSceneEntry, game::kRoomSceneEntryBytes,
                        sizeof(game::kRoomSceneEntryBytes)}) &&
      Hook("RoomSceneEntry", game::kRoomSceneEntry,
           reinterpret_cast<void*>(&RoomSceneEntryDetour),
           reinterpret_cast<void**>(&g_roomSceneEntry))) {
    Log("DebugMenu: quit to title ready (scene entry 0046C110 hooked)");
  } else {
    Log("DebugMenu: scene entry 0046C110 unavailable; quit to title disabled");
  }
  Log("QuickSave: F5/F6 open native Save/Load menus; mouse wheel/click enabled; Shift+F5/F6 keep immediate quick actions");
  return true;
}

}  // namespace sf::features

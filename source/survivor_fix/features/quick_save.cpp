#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

#include "../game.h"
#include "../hooks.h"
#include "../log.h"
#include "features.h"

namespace sf { extern std::wstring g_dir; }
extern int ForceWidth, ForceHeight;
extern bool bForceWindowedMode, bBorderlessFullscreen;

namespace sf::features {
namespace {

using RoomTickFn = int(__fastcall*)(void*, void*, int, int, int, int, int, int, int);
using RoomDispatcherFn = void(__fastcall*)(void*, void*);
RoomTickFn g_roomTick = nullptr;
RoomDispatcherFn g_roomDispatcher = nullptr;

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

QuickState g_pending = {};
bool g_reloadAtNextDispatch = false;
bool g_f5Down = false;
bool g_f6Down = false;
bool g_f8Down = false;

struct Resolution { int width, height; };
constexpr Resolution kResolutions[] = {
    {320, 240}, {640, 480}, {800, 600}, {960, 720}, {1024, 768}, {1280, 960},
    {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};

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
  const std::wstring path = StatePath();
  CreateDirectoryW((sf::g_dir + L"Save").c_str(), nullptr);
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
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
  if (!stats || !gameState) return;
  QuickState state;
  state.scene = *reinterpret_cast<int*>(game::kSceneId);
  state.difficulty = *game::Field<uint8_t>(gameState, 0x42);
  std::memcpy(state.record.data(), game::Field<uint8_t>(stats, game::kLiveSaveRecord), state.record.size());
  state.checksum = Checksum(state);
  if (WriteState(state)) {
    MessageBeep(MB_OK);
    Log("QuickSave: saved scene %02X to Save\\QUICK.SAV", state.scene);
  } else {
    MessageBeep(MB_ICONHAND);
    Log("QuickSave: failed to write Save\\QUICK.SAV (%lu)", GetLastError());
  }
}

bool QueueLoad() {
  QuickState state;
  if (!ReadState(&state)) {
    MessageBeep(MB_ICONHAND);
    Log("QuickSave: QUICK.SAV missing or invalid");
    return false;
  }
  g_pending = state;
  g_reloadAtNextDispatch = true;
  MessageBeep(MB_OK);
  Log("QuickSave: queued load of scene %02X", state.scene);
  return true;
}

void ResizeClient(HWND window, int width, int height) {
  RECT rect = {0, 0, width, height};
  AdjustWindowRectEx(&rect, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE,
                     static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)));
  SetWindowPos(window, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CycleResolution() {
  size_t current = 0;
  for (size_t i = 0; i < _countof(kResolutions); ++i) {
    if (kResolutions[i].width == ForceWidth && kResolutions[i].height == ForceHeight) {
      current = i;
      break;
    }
  }
  const Resolution next = kResolutions[(current + 1) % _countof(kResolutions)];
  ForceWidth = next.width;
  ForceHeight = next.height;
  const std::wstring ini = sf::g_dir + L"config.ini";
  wchar_t value[16] = {};
  _itow_s(ForceWidth, value, 10);
  WritePrivateProfileStringW(L"Graphics", L"ForceWidth", value, ini.c_str());
  _itow_s(ForceHeight, value, 10);
  WritePrivateProfileStringW(L"Graphics", L"ForceHeight", value, ini.c_str());
  if (bForceWindowedMode && !bBorderlessFullscreen) ResizeClient(GetForegroundWindow(), ForceWidth, ForceHeight);
  MessageBeep(MB_OK);
  Log("QuickSave: F8 resolution %dx%d%s", ForceWidth, ForceHeight,
      (bForceWindowedMode && !bBorderlessFullscreen) ? " applied" : " saved for next device reset");
}

int __fastcall RoomTick(void* gameObject, void* edx, int a1, int a2, int a3, int a4, int a5, int a6, int a7) {
  const int result = g_roomTick(gameObject, edx, a1, a2, a3, a4, a5, a6, a7);
  const bool active = Foreground();
  const bool f5 = active && (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
  const bool f6 = active && (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
  const bool f8 = active && (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
  if (f5 && !g_f5Down) SaveNow();
  if (f8 && !g_f8Down) CycleResolution();
  if (f6 && !g_f6Down && QueueLoad()) {
    // Every room treats a zero RoomTick result as its normal exit path and
    // releases models, effects, audio and collision before the dispatcher returns.
    g_f5Down = f5;
    g_f6Down = f6;
    g_f8Down = f8;
    return 0;
  }
  g_f5Down = f5;
  g_f6Down = f6;
  g_f8Down = f8;
  return result;
}

void __fastcall RoomDispatcher(void* gameObject, void* edx) {
  if (g_reloadAtNextDispatch) {
    void* stats = *reinterpret_cast<void**>(game::kGameStats);
    if (stats) {
      std::memcpy(game::Field<uint8_t>(stats, game::kLiveSaveRecord),
                  g_pending.record.data(), g_pending.record.size());
      *reinterpret_cast<int*>(game::kSceneId) = g_pending.scene;
      *game::Field<uint8_t>(gameObject, 0x42) = g_pending.difficulty;
      *game::Field<uint8_t>(gameObject, game::kMainState) = 6;
      Log("QuickSave: restored native record; dispatching scene %02X", g_pending.scene);
    }
    g_reloadAtNextDispatch = false;
  }
  g_roomDispatcher(gameObject, edx);
}

}  // namespace

bool InstallQuickSave() {
  const game::ByteSignature checks[] = {
      {game::kRoomTick, game::kRoomTickBytes, sizeof game::kRoomTickBytes},
      {game::kRoomDispatcher, game::kRoomDispatcherBytes, sizeof game::kRoomDispatcherBytes}};
  for (const auto& check : checks) {
    if (!game::CheckBytes(check)) {
      Log("QuickSave: unexpected executable bytes; F5/F6/F8 disabled");
      return false;
    }
  }
  return Hook("QuickSaveRoomTick", game::kRoomTick, reinterpret_cast<void*>(&RoomTick),
              reinterpret_cast<void**>(&g_roomTick)) &&
         Hook("QuickSaveDispatcher", game::kRoomDispatcher, reinterpret_cast<void*>(&RoomDispatcher),
              reinterpret_cast<void**>(&g_roomDispatcher));
}

}  // namespace sf::features

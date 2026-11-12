// DualShock 4: the lightbar breathes with the player's health, the pad's own buttons and sticks
// drive the existing pad bindings, and the controller speaker plays the gunshots.
//
// A DualShock 4 is a DirectInput HID gamepad, which the game and the XInput layer both ignore, so
// the input reports are read here and handed to the pad pipeline (dinputhook.cpp) as a PadState.
// The lightbar is HID output: USB report 0x05 (32 bytes) or Bluetooth 0x11 (78 bytes, CRC32).
// The speaker is the USB audio endpoint the pad exposes; Bluetooth has none on Windows.
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <thread>

#include "../config.h"
#include "../ds4_logic.h"
#include "../game.h"
#include "../log.h"
#include "../xinput_logic.h"
#include "features.h"

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "propsys.lib")

namespace sf::features {
namespace {

using ds4::Rgb;

constexpr USHORT kSonyVendor = 0x054C;
constexpr USHORT kDs4V1 = 0x05C4;      // no USB audio
constexpr USHORT kDs4V2 = 0x09CC;      // USB audio (speaker / jack)
constexpr USHORT kDs4Adapter = 0x0BA0; // wireless adapter, enumerates as a DS4
constexpr USHORT kDualSense = 0x0CE6;  // recognised so the log can say why it is not driven

constexpr DWORD kLightIntervalMs = 40;  // 25 Hz is smooth for a breath and cheap on the wire
constexpr DWORD kScanIntervalMs = 2000;

bool g_installed = false;
HANDLE g_write = INVALID_HANDLE_VALUE;
HANDLE g_read = INVALID_HANDLE_VALUE;
bool g_bluetooth = false;
bool g_dualSense = false;
uint16_t g_outputLength = 0;
uint16_t g_inputLength = 0;
DWORD g_lightStart = 0;
DWORD g_lastLight = 0;
DWORD g_lastScan = 0;
DWORD g_lastShotAt = 0;    // rumble bursts are timed from the game's own events
DWORD g_lastDamageAt = 0;
DWORD g_lastFlashAt = 0;
int g_lastHealth = -1;
bool g_scanLogged = false;
bool g_writeErrorLogged = false;
bool g_readerStarted = false;
int g_candidatesLogged = 0;  // a handful of lines per process, never one per scan
char g_candidatePaths[4][1024] = {};  // every Sony interface of the pad, for feature reports
int g_candidateCount = 0;
bool g_speakerRoutingTried = false;

// ---- input ------------------------------------------------------------------------------------
// The reader thread blocks on ReadFile and publishes the newest PadState; the DirectInput hook
// copies it whenever the game polls. A report older than a second counts as disconnected.
std::mutex g_padLock;
sf::pad::PadState g_padState;
DWORD g_padReportAt = 0;

float Axis(int value) {
  const float scaled = (static_cast<float>(value) - 128.0f) / 127.0f;
  return scaled < -1.0f ? -1.0f : (scaled > 1.0f ? 1.0f : scaled);
}

void ParseReport(const uint8_t* data, DWORD length) {
  if (length < 10) return;
  const uint8_t id = data[0];
  size_t offset = 0;
  if (id == 0x01) {
    offset = 1;  // USB 0x01 and the short Bluetooth 0x01 share the payload after the id
  } else if (id == 0x11 && length >= 12 && data[1] == 0xC0) {
    offset = 3;  // Bluetooth 0x11: id, 0xC0, 0x00, then the same payload
  } else {
    return;
  }
  if (static_cast<size_t>(length) < offset + 9) return;
  const uint8_t* p = data + offset;
  sf::pad::PadState pad;
  pad.leftX = Axis(p[0]);
  pad.leftY = -Axis(p[1]);  // the report's Y grows downwards, XInput's grows upwards
  pad.rightX = Axis(p[2]);
  pad.rightY = -Axis(p[3]);
  const uint8_t face = p[4];
  const uint8_t shoulders = p[5];
  if (face & 0x20) pad.buttons |= sf::pad::padbutton::A;  // cross
  if (face & 0x40) pad.buttons |= sf::pad::padbutton::B;  // circle
  if (face & 0x10) pad.buttons |= sf::pad::padbutton::X;  // square
  if (face & 0x80) pad.buttons |= sf::pad::padbutton::Y;  // triangle
  if (shoulders & 0x01) pad.buttons |= sf::pad::padbutton::LeftShoulder;
  if (shoulders & 0x02) pad.buttons |= sf::pad::padbutton::RightShoulder;
  if (shoulders & 0x10) pad.buttons |= sf::pad::padbutton::Back;   // share
  if (shoulders & 0x20) pad.buttons |= sf::pad::padbutton::Start;  // options
  if (shoulders & 0x40) pad.buttons |= sf::pad::padbutton::LeftThumb;
  if (shoulders & 0x80) pad.buttons |= sf::pad::padbutton::RightThumb;
  pad.leftTrigger = p[7];
  pad.rightTrigger = p[8];
  const ds4::HatDirections hat = ds4::HatDirection(face);
  if (hat.up) pad.buttons |= sf::pad::padbutton::DpadUp;
  if (hat.down) pad.buttons |= sf::pad::padbutton::DpadDown;
  if (hat.left) pad.buttons |= sf::pad::padbutton::DpadLeft;
  if (hat.right) pad.buttons |= sf::pad::padbutton::DpadRight;
  {
    std::lock_guard<std::mutex> lock(g_padLock);
    g_padState = pad;
    g_padReportAt = GetTickCount();
  }
  static bool loggedArrival = false;
  if (!loggedArrival) {
    loggedArrival = true;
    Log("DS4: input reports arriving (report 0x%02X, %lu bytes); the pad drives the bindings", id,
        length);
  }
}

void ReaderLoop() {
  // The Bluetooth collection declares a 547-byte input report, so the buffer has to be that large
  // even though the gamepad report that arrives is 78 bytes; a short buffer fails on every read.
  uint8_t buffer[1024];
  int failures = 0;
  while (true) {
    if (g_read == INVALID_HANDLE_VALUE) {
      failures = 0;
      Sleep(200);
      continue;
    }
    DWORD read = 0;
    DWORD want = static_cast<DWORD>(g_inputLength ? g_inputLength : sizeof buffer);
    if (want > sizeof buffer) want = sizeof buffer;
    if (!ReadFile(g_read, buffer, want, &read, nullptr)) {
      if (++failures == 20) {
        Log("DS4: controller stopped answering; looking for it again");
        CloseHandle(g_read);
        g_read = INVALID_HANDLE_VALUE;
      }
      Sleep(50);
      continue;
    }
    failures = 0;
    ParseReport(buffer, read);
  }
}

// ---- device discovery -------------------------------------------------------------------------
struct Candidate {
  bool found = false;
  int score = -1;
  USHORT pid = 0;
  uint16_t inputLength = 0;
  uint16_t outputLength = 0;
  bool bluetooth = false;
  bool dualSense = false;
  char path[1024] = {};
  wchar_t product[128] = {};
};

// A DualShock 4 exposes several HID collections under the same product name. The gamepad one has a
// 64-byte input report and a 32-byte output report over USB (78/78 over Bluetooth); a config
// collection reports 547 bytes either way. Real hardware showed the first build picking the config
// collection, where every 32-byte lightbar write is rejected with error 87 (the report is shorter
// than that collection's output length), so the caps are scored instead of trusting the enumeration
// order.
// A Bluetooth DualShock 4 is reached through the Bluetooth HID service, and that single collection
// declares its 547-byte config feature report as the input and output size: the caps say 547/547 while
// the real gamepad reports are the 78-byte 0x11 pair. Over USB the same pad exposes a small gamepad
// collection (64 in / 32 out) plus a separate 547-byte config collection, so the device path has to be
// part of the decision, not only the caps.
bool IsBluetoothPath(const char* path) {
  return std::strstr(path, "00001124-0000-1000-8000-00805f9b34fb") != nullptr ||
         std::strstr(path, "BTHENUM") != nullptr;
}

int ScoreCaps(const char* path, uint16_t inputLength, uint16_t outputLength, bool sonyDs4) {
  if (sonyDs4 && IsBluetoothPath(path)) return 3;
  const bool gamepadOutput = outputLength == ds4::kUsbReportSize || outputLength == ds4::kBtReportSize;
  const bool gamepadInput = inputLength == 64 || inputLength == ds4::kBtReportSize;
  if (gamepadOutput && gamepadInput) return 3;
  if (gamepadOutput) return 2;
  if (gamepadInput) return 1;
  return 0;
}

// The device path names the USB IDs even when the interface cannot be opened.
bool IsSonyPath(const char* path) {
  return std::strstr(path, "vid_054c") || std::strstr(path, "VID_054C");
}

const char* PathTail(const char* path) {
  const char* tail = std::strrchr(path, '\\');
  return tail ? tail + 1 : path;
}

// A wired DualShock 4 v2 sends its USB audio to the headphone jack. Feature report 0xE0 00 03 moves
// it to the built-in speaker (psdevwiki, DS4-USB); without it the pad's audio device plays into an
// empty jack and the speaker stays silent. The feature lives on the pad's audio/config collection,
// which on USB is a different interface from the gamepad one, so every Sony interface is tried.
bool RouteAudioToSpeaker() {
  uint8_t report[3] = {0xE0, 0x00, 0x03};
  if (g_write != INVALID_HANDLE_VALUE && HidD_SetFeature(g_write, report, sizeof report)) {
    Log("DS4: USB audio routed to the built-in speaker (feature 0xE0)");
    return true;
  }
  for (int i = 0; i < g_candidateCount; ++i) {
    HANDLE handle = CreateFileA(g_candidatePaths[i], GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) continue;
    const bool ok = HidD_SetFeature(handle, report, sizeof report) != FALSE;
    CloseHandle(handle);
    if (ok) {
      Log("DS4: USB audio routed to the built-in speaker (feature 0xE0)");
      return true;
    }
  }
  Log("DS4: speaker routing feature 0xE0 was not accepted (%lu); audio may stay on the jack",
      GetLastError());
  return false;
}

bool OpenController() {
  GUID hid = {};
  HidD_GetHidGuid(&hid);
  HDEVINFO set = SetupDiGetClassDevsA(&hid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (set == INVALID_HANDLE_VALUE) return false;
  g_candidateCount = 0;
  Candidate best;
  for (DWORD index = 0;; ++index) {
    SP_DEVICE_INTERFACE_DATA iface = {};
    iface.cbSize = sizeof iface;
    if (!SetupDiEnumDeviceInterfaces(set, nullptr, &hid, index, &iface)) break;
    DWORD needed = 0;
    SetupDiGetDeviceInterfaceDetailA(set, &iface, nullptr, 0, &needed, nullptr);
    if (!needed || needed > sizeof best.path) continue;
    alignas(SP_DEVICE_INTERFACE_DETAIL_DATA_A) char buffer[1300] = {};
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_A*>(buffer);
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
    if (!SetupDiGetDeviceInterfaceDetailA(set, &iface, detail, sizeof buffer, &needed, nullptr)) continue;
    HANDLE handle = CreateFileA(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      if (IsSonyPath(detail->DevicePath) && g_candidatesLogged < 8) {
        ++g_candidatesLogged;
        Log("DS4: candidate %s would not open (%lu); something else holds it",
            PathTail(detail->DevicePath), GetLastError());
      }
      continue;
    }
    Candidate candidate;
    bool matched = false;
    HIDD_ATTRIBUTES attributes = {};
    attributes.Size = sizeof attributes;
    if (HidD_GetAttributes(handle, &attributes)) {
      wchar_t product[128] = {};
      HidD_GetProductString(handle, product, sizeof product);
      const bool sony = attributes.VendorID == kSonyVendor &&
                        (attributes.ProductID == kDs4V1 || attributes.ProductID == kDs4V2 ||
                         attributes.ProductID == kDs4Adapter || attributes.ProductID == kDualSense);
      // Clones often report another vendor but keep the retail product string.
      const bool named = sony || std::wcsstr(product, L"Wireless Controller") != nullptr;
      if (named) {
        PHIDP_PREPARSED_DATA preparsed = nullptr;
        HIDP_CAPS caps = {};
        if (HidD_GetPreparsedData(handle, &preparsed) && preparsed) {
          if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
            candidate.inputLength = caps.InputReportByteLength;
            candidate.outputLength = caps.OutputReportByteLength;
          }
          HidD_FreePreparsedData(preparsed);
        }
        candidate.found = true;
        candidate.pid = attributes.ProductID;
        candidate.dualSense = attributes.ProductID == kDualSense;
        candidate.bluetooth = candidate.outputLength == ds4::kBtReportSize;
        candidate.score = ScoreCaps(detail->DevicePath, candidate.inputLength,
                                    candidate.outputLength, sony);
        std::memcpy(candidate.product, product, sizeof candidate.product);
        std::strncpy(candidate.path, detail->DevicePath, sizeof candidate.path - 1);
        matched = true;
      }
    }
    CloseHandle(handle);
    if (!matched) continue;
    if (g_candidatesLogged < 8) {
      ++g_candidatesLogged;
      Log("DS4: candidate %s %04X:%04X out %u in %u (%s)", PathTail(candidate.path), kSonyVendor,
          candidate.pid, candidate.outputLength, candidate.inputLength,
          candidate.score == 3 ? "gamepad" : "other collection, not driven");
    }
    if (g_candidateCount < 4) {
      std::strncpy(g_candidatePaths[g_candidateCount], candidate.path,
                   sizeof g_candidatePaths[0] - 1);
      ++g_candidateCount;
    }
    // Both links can be up at once (cable plugged in while Bluetooth stays paired). USB is the one
    // with an audio endpoint and the small report pair, so it wins a tie.
    if (candidate.score > best.score ||
        (candidate.score == best.score && best.found && IsBluetoothPath(best.path) &&
         !IsBluetoothPath(candidate.path))) {
      best = candidate;
    }
  }
  SetupDiDestroyDeviceInfoList(set);
  if (!best.found) return false;
  if (best.score < 2 && g_candidatesLogged < 8) {
    ++g_candidatesLogged;
    Log("DS4: only a non-gamepad collection is free; close DS4Windows or Steam Input so the wrapper "
        "can read the pad and drive the lightbar");
  }
  if (g_write == INVALID_HANDLE_VALUE) {
    g_write = CreateFileA(best.path, GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (g_write == INVALID_HANDLE_VALUE) {
      Log("DS4: the controller interface would not open for output (%lu)", GetLastError());
    }
  }
  if (g_read == INVALID_HANDLE_VALUE) {
    g_read = CreateFileA(best.path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, 0, nullptr);
    if (g_read == INVALID_HANDLE_VALUE) {
      Log("DS4: the controller interface would not open for input (%lu)", GetLastError());
    }
  }
  g_bluetooth = best.bluetooth || IsBluetoothPath(best.path);
  g_outputLength = best.outputLength;
  g_inputLength = best.inputLength;
  g_writeErrorLogged = false;
  g_dualSense = best.dualSense;
  if (!g_bluetooth && !g_speakerRoutingTried) {
    g_speakerRoutingTried = true;
    RouteAudioToSpeaker();
  }
  static uint16_t loggedOut = 0;
  static uint16_t loggedIn = 0;
  if (loggedOut != g_outputLength || loggedIn != g_inputLength) {
    loggedOut = g_outputLength;
    loggedIn = g_inputLength;
    char product[128] = {};
    WideCharToMultiByte(CP_ACP, 0, best.product, -1, product, sizeof product, nullptr, nullptr);
    Log("DS4: '%s' 054C:%04X on %s (out %u, in %u)", product[0] ? product : "controller", best.pid,
        g_bluetooth ? "Bluetooth" : "USB", g_outputLength, g_inputLength);
  }
  if (best.dualSense) {
    Log("DS4: DualSense (054C:%04X); its lightbar report is not implemented, input only", best.pid);
  }
  return true;
}

// ---- lightbar ---------------------------------------------------------------------------------
bool WriteReport(const uint8_t* report, size_t length) {
  if (g_write == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  if (WriteFile(g_write, report, static_cast<DWORD>(length), &written, nullptr)) return true;
  if (HidD_SetOutputReport(g_write, const_cast<uint8_t*>(report), static_cast<ULONG>(length))) return true;
  // A collection that numbers its reports wants a leading 0x00 "unnumbered" id.
  uint8_t shifted[96] = {};
  if (length + 1 <= sizeof shifted) {
    std::memcpy(shifted + 1, report, length);
    if (WriteFile(g_write, shifted, static_cast<DWORD>(length + 1), &written, nullptr)) return true;
    if (HidD_SetOutputReport(g_write, shifted, static_cast<ULONG>(length + 1))) return true;
  }
  return false;
}

bool SendLight(Rgb color, ds4::Rumble rumble) {
  if (g_dualSense) return true;  // recognised, not driven: do not fail and rescan every frame
  if (g_bluetooth) {
    // Windows rejects a report shorter than the collection's output length with error 87, so the
    // 78-byte 0x11 packet is padded up to whatever the collection declares (547 on Bluetooth).
    uint8_t report[640] = {};
    const size_t length = g_outputLength > ds4::kBtReportSize ? g_outputLength : ds4::kBtReportSize;
    if (length > sizeof report) return false;
    ds4::BuildBtLightReport(report, color, rumble);
    const bool ok = WriteReport(report, length);
    if (ok) {
      static bool loggedOk = false;
      if (!loggedOk) {
        loggedOk = true;
        Log("DS4: lightbar reports accepted over Bluetooth");
      }
    } else if (!g_writeErrorLogged) {
      g_writeErrorLogged = true;
      Log("DS4: Bluetooth output report failed (%lu); output %u bytes", GetLastError(), g_outputLength);
    }
    return ok;
  }
  uint8_t report[640] = {};
  const size_t length = g_outputLength > ds4::kUsbReportSize ? g_outputLength : ds4::kUsbReportSize;
  if (length > sizeof report) return false;
  ds4::BuildUsbLightReport(report, color, rumble);
  const bool ok = WriteReport(report, length);
  if (ok) {
    static bool loggedOk = false;
    if (!loggedOk) {
      loggedOk = true;
      Log("DS4: lightbar reports accepted over USB");
    }
  } else if (!g_writeErrorLogged) {
    g_writeErrorLogged = true;
    Log("DS4: USB output report failed (%lu); output %u bytes", GetLastError(), g_outputLength);
  }
  return ok;
}

int PlayerHealth() {
  void* game = *reinterpret_cast<void**>(game::kGameState);
  void* stats = *reinterpret_cast<void**>(game::kGameStats);
  if (!game || !stats) return -1;
  if (game::Read<uint8_t>(game, game::kMainState) != 6) return -1;
  return game::Read<int>(stats, game::kGameStatsHp);
}

// ---- controller speaker -----------------------------------------------------------------------
IAudioClient* g_audio = nullptr;
IAudioRenderClient* g_render = nullptr;
WAVEFORMATEX* g_format = nullptr;
UINT32 g_audioFrames = 0;
bool g_audioStarted = false;
bool g_audioFloat = false;
int g_audioAttempts = 0;
DWORD g_lastAudioAttempt = 0;
std::atomic<bool> g_shotRequested{false};
uint32_t g_burstFrame = 0;
uint32_t g_burstLength = 0;

const char* EndpointStateName(DWORD state) {
  if (state & DEVICE_STATE_ACTIVE) return "active";
  if (state & DEVICE_STATE_DISABLED) return "disabled";
  if (state & DEVICE_STATE_UNPLUGGED) return "unplugged";
  if (state & DEVICE_STATE_NOTPRESENT) return "not present";
  return "unknown";
}

bool IsControllerSpeakerName(const wchar_t* name) {
  if (!name) return false;
  if (sf::g_config.ds4SpeakerDevice[0]) {
    wchar_t wanted[64] = {};
    MultiByteToWideChar(CP_ACP, 0, sf::g_config.ds4SpeakerDevice, -1, wanted,
                        static_cast<int>(sizeof wanted / sizeof wanted[0]));
    return wanted[0] && std::wcsstr(name, wanted) != nullptr;
  }
  return std::wcsstr(name, L"Wireless Controller") != nullptr ||
         std::wcsstr(name, L"DUALSHOCK") != nullptr ||
         std::wcsstr(name, L"DualShock") != nullptr ||
         std::wcsstr(name, L"DualSense") != nullptr;
}

IMMDevice* FindControllerSpeaker(IMMDeviceEnumerator* enumerator, bool verbose) {
  // A disabled or unplugged endpoint still proves the device exists; only an active one can play.
  const DWORD states[] = {DEVICE_STATE_ACTIVE, DEVICE_STATE_DISABLED, DEVICE_STATE_UNPLUGGED};
  IMMDevice* active = nullptr;
  for (DWORD state : states) {
    IMMDeviceCollection* collection = nullptr;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, state, &collection))) continue;
    UINT count = 0;
    collection->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
      IMMDevice* device = nullptr;
      if (FAILED(collection->Item(i, &device))) continue;
      IPropertyStore* store = nullptr;
      PROPVARIANT name;
      PropVariantInit(&name);
      const bool read = SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) &&
                        SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &name)) &&
                        name.vt == VT_LPWSTR && name.pwszVal;
      if (read) {
        if (verbose) {
          char text[96] = {};
          WideCharToMultiByte(CP_ACP, 0, name.pwszVal, -1, text, sizeof text, nullptr, nullptr);
          Log("DS4: render endpoint '%s' (%s)", text, EndpointStateName(state));
        }
        if (IsControllerSpeakerName(name.pwszVal)) {
          if (!(state & DEVICE_STATE_ACTIVE)) {
            Log("DS4: the controller endpoint is %s; enable it in Windows sound settings",
                EndpointStateName(state));
          } else if (!active) {
            active = device;
            device = nullptr;  // keep the reference
          }
        }
      }
      PropVariantClear(&name);
      if (store) store->Release();
      if (device) device->Release();
    }
    collection->Release();
  }
  return active;
}

bool OpenSpeaker(bool verbose) {
  if (g_audio) return true;
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // fails harmlessly if already initialised
  IMMDeviceEnumerator* enumerator = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&enumerator)))) {
    Log("DS4: MMDeviceEnumerator unavailable; speaker silent");
    return false;
  }
  IMMDevice* device = FindControllerSpeaker(enumerator, verbose);
  enumerator->Release();
  if (!device) {
    if (g_bluetooth) {
      // Windows has no audio path to a DualShock 4 over plain Bluetooth: the speaker needs the USB
      // audio device a v2 pad (or Sony's wireless adapter) exposes.
      Log("DS4: the pad is on Bluetooth, where Windows has no speaker path for a DualShock 4; "
          "connect the cable to use the built-in speaker");
    } else {
      Log("DS4: no controller audio endpoint for this pad; speaker silent");
    }
    return false;
  }
  IAudioClient* client = nullptr;
  const HRESULT activated = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                             reinterpret_cast<void**>(&client));
  device->Release();
  if (FAILED(activated) || !client) {
    Log("DS4: controller audio endpoint could not be activated (0x%08lX)", activated);
    return false;
  }
  WAVEFORMATEX* format = nullptr;
  if (FAILED(client->GetMixFormat(&format)) || !format) {
    client->Release();
    Log("DS4: controller audio mix format unavailable; speaker silent");
    return false;
  }
  IAudioRenderClient* render = nullptr;
  const HRESULT init = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 400000, 0, format, nullptr);
  if (FAILED(init) || FAILED(client->GetService(IID_PPV_ARGS(&render)))) {
    CoTaskMemFree(format);
    client->Release();
    Log("DS4: controller audio stream could not start (0x%08lX); speaker silent", init);
    return false;
  }
  UINT32 frames = 0;
  client->GetBufferSize(&frames);
  g_audio = client;
  g_render = render;
  g_format = format;
  g_audioFrames = frames;
  g_audioFloat = format->wBitsPerSample == 32;
  Log("DS4: controller speaker ready (%lu Hz, %u channels, %u frames)", format->nSamplesPerSec,
      format->nChannels, frames);
  return true;
}

float GunshotSample(uint32_t index, uint32_t rate) {
  const float t = static_cast<float>(index) / static_cast<float>(rate ? rate : 48000);
  if (t > 0.16f) return 0.0f;
  uint32_t state = index * 1664525u + 1013904223u;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  const float noise = static_cast<float>(state & 0xFFFFu) / 32768.0f - 1.0f;
  const float thump = std::sin(6.28318531f * 85.0f * t) * std::exp(-t * 45.0f);
  return (noise * 0.65f + thump * 0.55f) * std::exp(-t * 30.0f) * 0.45f;
}

void FillAudio(uint8_t* data, UINT32 frames) {
  const uint32_t rate = g_format && g_format->nSamplesPerSec ? g_format->nSamplesPerSec : 48000;
  const int channels = g_format && g_format->nChannels ? g_format->nChannels : 2;
  if (g_shotRequested.exchange(false)) {
    g_burstFrame = 0;
    g_burstLength = rate * 16 / 100;  // 160 ms
  }
  const float volume = static_cast<float>(sf::g_config.ds4SpeakerVolume) / 100.0f;
  for (UINT32 frame = 0; frame < frames; ++frame) {
    float sample = 0.0f;
    if (g_burstFrame < g_burstLength) sample = GunshotSample(g_burstFrame++, rate) * volume;
    for (int channel = 0; channel < channels; ++channel) {
      const size_t index = static_cast<size_t>(frame) * static_cast<size_t>(channels) + channel;
      if (g_audioFloat) {
        reinterpret_cast<float*>(data)[index] = sample;
      } else {
        reinterpret_cast<int16_t*>(data)[index] = static_cast<int16_t>(sample * 32767.0f);
      }
    }
  }
}

void AudioTick() {
  if (!g_audio || !g_render) return;
  if (!g_audioStarted) {
    if (!g_shotRequested.load()) return;  // silence until the first shot
    if (FAILED(g_audio->Start())) return;
    g_audioStarted = true;
  }
  UINT32 padding = 0;
  if (FAILED(g_audio->GetCurrentPadding(&padding))) return;
  UINT32 available = g_audioFrames > padding ? g_audioFrames - padding : 0;
  if (!available) return;
  if (available > 2048) available = 2048;
  BYTE* data = nullptr;
  if (FAILED(g_render->GetBuffer(available, &data)) || !data) return;
  FillAudio(data, available);
  g_render->ReleaseBuffer(available, 0);
}

}  // namespace

bool Ds4PadState(sf::pad::PadState* out) {
  if (!out || !g_installed) return false;
  std::lock_guard<std::mutex> lock(g_padLock);
  if (g_read == INVALID_HANDLE_VALUE) return false;
  if (GetTickCount() - g_padReportAt > 1000) return false;
  *out = g_padState;
  return true;
}

void Ds4OnPlayerShot() {
  if (!g_installed) return;
  g_lastShotAt = GetTickCount();
  if (!sf::g_config.ds4Speaker) return;
  g_shotRequested.store(true);
}

void Ds4OnPlayerDamage() {
  if (!g_installed) return;
  g_lastDamageAt = GetTickCount();
}

void Ds4OnScreenFlash() {
  if (!g_installed) return;
  g_lastFlashAt = GetTickCount();
}

// ---- XInput rumble ----------------------------------------------------------------------------
// A GameSir (or any other pad the game sees as XInput) never reads the DS4's HID output report, so the
// same mix is mirrored to its motors. Nothing here depends on a DualShock 4 being attached.
using XInputGetStateFn = DWORD(WINAPI*)(DWORD, void*);
using XInputSetStateFn = DWORD(WINAPI*)(DWORD, void*);
struct XVibration {
  WORD left;
  WORD right;
};
struct XGamepad {
  WORD buttons;
  BYTE leftTrigger, rightTrigger;
  SHORT thumbLX, thumbLY, thumbRX, thumbRY;
};
struct XState {
  DWORD packet;
  XGamepad pad;
};
XInputGetStateFn g_xiGetState = nullptr;
XInputSetStateFn g_xiSetState = nullptr;
bool g_xiLoaded = false;
bool g_xiPad[4] = {};
DWORD g_xiScanAt = 0;
DWORD g_xiSentAt = 0;
bool g_xiStopped = true;

void LoadXInputRumble() {
  if (g_xiLoaded) return;
  g_xiLoaded = true;
  static const wchar_t* const kNames[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput1_2.dll",
                                          L"xinput9_1_0.dll"};
  for (const wchar_t* name : kNames) {
    HMODULE module = GetModuleHandleW(name);
    if (!module) module = LoadLibraryW(name);
    if (!module) continue;
    g_xiSetState = reinterpret_cast<XInputSetStateFn>(GetProcAddress(module, "XInputSetState"));
    g_xiGetState = reinterpret_cast<XInputGetStateFn>(GetProcAddress(module, "XInputGetState"));
    if (!g_xiSetState) continue;
    Log("XInput: rumble through %ls", name);
    return;
  }
  Log("XInput: no vibration entry point found; only the DualShock 4 rumbles");
}

void XInputRumbleTick(const ds4::Rumble& rumble) {
  LoadXInputRumble();
  if (!g_xiSetState) return;
  const DWORD now = GetTickCount();
  const ds4::MotorLevels motors = ds4::XInputMotors(rumble);
  if (!motors.left && !motors.right) {
    if (g_xiStopped) return;  // the motors are already off: one zero report is enough
    g_xiStopped = true;
  } else {
    if (now - g_xiSentAt < 25) return;  // the bursts decay at the same 25 Hz the DS4 report runs at
    g_xiStopped = false;
  }
  g_xiSentAt = now;
  if (now - g_xiScanAt >= 1000) {
    g_xiScanAt = now;
    for (DWORD slot = 0; slot < 4; ++slot) {
      if (!g_xiGetState) {
        g_xiPad[slot] = true;  // no way to ask: the SetState call on an empty slot is harmless
        continue;
      }
      XState state = {};
      g_xiPad[slot] = g_xiGetState(slot, &state) == ERROR_SUCCESS;
    }
  }
  XVibration vibration = {motors.left, motors.right};
  for (DWORD slot = 0; slot < 4; ++slot) {
    if (g_xiPad[slot]) g_xiSetState(slot, &vibration);
  }
}

// The mix both pads receive; the DS4 output report and the XInput motors read it every tick.
ds4::Rumble CurrentRumble(DWORD now) {
  ds4::Rumble rumble{0, 0};
  if (!sf::g_config.ds4 || !sf::g_config.ds4Rumble) return rumble;
  // A shot is a short sharp burst; taking damage is a longer, stronger one. Both decay to zero, so the
  // next report simply stops the motors.
  const uint8_t shotWeak = ds4::RumbleLevel(now - g_lastShotAt, 120, 140);
  const uint8_t shotStrong = ds4::RumbleLevel(now - g_lastShotAt, 120, 90);
  const uint8_t hit = ds4::RumbleLevel(now - g_lastDamageAt, 300, 210);
  // The title flash is one strong pulse, not a bed under the whole fade.
  const uint8_t flashWeak = ds4::RumbleLevel(now - g_lastFlashAt, 180, 40);
  const uint8_t flashStrong = ds4::RumbleLevel(now - g_lastFlashAt, 180, 190);
  uint8_t weak = shotWeak > hit / 3 ? shotWeak : static_cast<uint8_t>(hit / 3);
  if (flashWeak > weak) weak = flashWeak;
  uint8_t strong = shotStrong > hit ? shotStrong : hit;
  if (flashStrong > strong) strong = flashStrong;
  rumble.weak = weak;
  rumble.strong = strong;
  return rumble;
}

void Ds4Tick() {
  const DWORD now = GetTickCount();
  // Before the DS4 check: an XInput pad rumbles even when no DualShock 4 is attached.
  XInputRumbleTick(CurrentRumble(now));
  if (!g_installed) return;
  if (g_write == INVALID_HANDLE_VALUE && now - g_lastScan >= kScanIntervalMs) {
    g_lastScan = now;
    const bool opened = OpenController();
    if (opened) {
      g_lightStart = now;
      g_lastLight = 0;
      g_scanLogged = false;
      if (sf::g_config.ds4Speaker && !g_audio && g_audioAttempts < 3) {
        ++g_audioAttempts;
        g_lastAudioAttempt = now;
        OpenSpeaker(g_audioAttempts == 1);
      }
    } else if (!g_scanLogged) {
      g_scanLogged = true;
      Log("DS4: no controller found yet; it is picked up as soon as it is connected");
    }
  }
  if (g_write != INVALID_HANDLE_VALUE && now - g_lastLight >= kLightIntervalMs) {
    g_lastLight = now;
    const int health = PlayerHealth();
    if (health >= 0 && g_lastHealth >= 0 && health < g_lastHealth) g_lastDamageAt = now;
    g_lastHealth = health;
    Rgb color;
    if (health < 0) {
      const ds4::Pulse idle{ds4::kIdleColor.r, ds4::kIdleColor.g, ds4::kIdleColor.b, 2000,
                            ds4::kIdleLevel};
      color = ds4::ScaledColor(idle, ds4::kIdleLevel);
    } else {
      const ds4::Pulse pulse = ds4::HealthPulse(health);
      color = ds4::ScaledColor(pulse, ds4::BreathLevel(pulse, now - g_lightStart));
    }
    if (!SendLight(color, CurrentRumble(now))) {
      // Keep the input handle: only the lightbar needs the output path, and a pad held by another
      // program can often still be read.
      static DWORD lastFailureLog = 0;
      if (now - lastFailureLog >= 30000) {
        lastFailureLog = now;
        Log("DS4: lightbar write failed; retrying the output path");
      }
      CloseHandle(g_write);
      g_write = INVALID_HANDLE_VALUE;
      g_lastScan = now;
    }
  }
  AudioTick();
}

bool InstallDs4() {
  if (!sf::g_config.ds4) return false;
  g_installed = true;
  Log("DS4: lightbar, HID input and speaker enabled");
  if (!g_readerStarted) {
    g_readerStarted = true;
    std::thread(ReaderLoop).detach();
  }
  return true;
}

}  // namespace sf::features

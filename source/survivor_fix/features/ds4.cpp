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
bool g_scanLogged = false;
bool g_writeErrorLogged = false;
bool g_readerStarted = false;

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
  const uint8_t* p = data + offset;
  if (static_cast<size_t>(length) < offset + 9) return;
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
  switch ((face >> 4) & 0x0F) {  // hat, 8 = released
    case 0: pad.buttons |= sf::pad::padbutton::DpadUp; break;
    case 1: pad.buttons |= sf::pad::padbutton::DpadUp | sf::pad::padbutton::DpadRight; break;
    case 2: pad.buttons |= sf::pad::padbutton::DpadRight; break;
    case 3: pad.buttons |= sf::pad::padbutton::DpadRight | sf::pad::padbutton::DpadDown; break;
    case 4: pad.buttons |= sf::pad::padbutton::DpadDown; break;
    case 5: pad.buttons |= sf::pad::padbutton::DpadDown | sf::pad::padbutton::DpadLeft; break;
    case 6: pad.buttons |= sf::pad::padbutton::DpadLeft; break;
    case 7: pad.buttons |= sf::pad::padbutton::DpadLeft | sf::pad::padbutton::DpadUp; break;
    default: break;
  }
  {
    std::lock_guard<std::mutex> lock(g_padLock);
    g_padState = pad;
    g_padReportAt = GetTickCount();
  }
}

void ReaderLoop() {
  uint8_t buffer[128];
  int failures = 0;
  while (true) {
    if (g_read == INVALID_HANDLE_VALUE) {
      failures = 0;
      Sleep(200);
      continue;
    }
    DWORD read = 0;
    if (!ReadFile(g_read, buffer, static_cast<DWORD>(g_inputLength ? g_inputLength : sizeof buffer),
                  &read, nullptr)) {
      if (++failures == 20) {
        Log("DS4: controller stopped answering; looking for it again");
        CloseHandle(g_read);
        g_read = INVALID_HANDLE_VALUE;
        if (g_write != INVALID_HANDLE_VALUE) CloseHandle(g_write);
        g_write = INVALID_HANDLE_VALUE;
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
  USHORT pid = 0;
  uint16_t inputLength = 0;
  uint16_t outputLength = 0;
  bool bluetooth = false;
  bool dualSense = false;
  char path[1024] = {};
  wchar_t product[128] = {};
};

// The DS4 exposes more than one HID collection; only the gamepad one carries the 32/78-byte output
// report the lightbar needs. The caps decide, not the enumeration order: taking the first writable
// interface is what made every lightbar write fail on a real 09CC.
bool InspectInterface(HDEVINFO set, SP_DEVICE_INTERFACE_DATA* iface, Candidate* out) {
  DWORD needed = 0;
  SetupDiGetDeviceInterfaceDetailA(set, iface, nullptr, 0, &needed, nullptr);
  if (!needed || needed > sizeof out->path) return false;
  alignas(SP_DEVICE_INTERFACE_DETAIL_DATA_A) char buffer[sizeof(Candidate::path) + 64] = {};
  auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_A*>(buffer);
  detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
  if (!SetupDiGetDeviceInterfaceDetailA(set, iface, detail, sizeof buffer, &needed, nullptr)) return false;
  HANDLE handle = CreateFileA(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
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
    const bool named = std::wcsstr(product, L"Wireless Controller") != nullptr;
    if (sony || named) {
      PHIDP_PREPARSED_DATA preparsed = nullptr;
      HIDP_CAPS caps = {};
      if (HidD_GetPreparsedData(handle, &preparsed) && preparsed) {
        if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
          out->inputLength = caps.InputReportByteLength;
          out->outputLength = caps.OutputReportByteLength;
        }
        HidD_FreePreparsedData(preparsed);
      }
      out->found = true;
      out->pid = attributes.ProductID;
      out->dualSense = attributes.ProductID == kDualSense;
      out->bluetooth = out->outputLength == ds4::kBtReportSize;
      std::memcpy(out->product, product, sizeof out->product);
      std::strncpy(out->path, detail->DevicePath, sizeof out->path - 1);
      matched = true;
    }
  }
  CloseHandle(handle);
  return matched;
}

bool OpenController() {
  GUID hid = {};
  HidD_GetHidGuid(&hid);
  HDEVINFO set = SetupDiGetClassDevsA(&hid, nullptr, nullptr,
                                  DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (set == INVALID_HANDLE_VALUE) return false;
  Candidate best;
  Candidate fallback;
  for (DWORD index = 0;; ++index) {
    SP_DEVICE_INTERFACE_DATA iface = {};
    iface.cbSize = sizeof iface;
    if (!SetupDiEnumDeviceInterfaces(set, nullptr, &hid, index, &iface)) break;
    Candidate candidate;
    if (!InspectInterface(set, &iface, &candidate)) continue;
    // The gamepad collection is the one with a usable output report; anything else only serves as a
    // last resort for a clone with unusual caps.
    if (candidate.outputLength == ds4::kUsbReportSize || candidate.outputLength == ds4::kBtReportSize) {
      best = candidate;
      break;
    }
    if (!fallback.found) fallback = candidate;
  }
  SetupDiDestroyDeviceInfoList(set);
  const Candidate& chosen = best.found ? best : fallback;
  if (!chosen.found) return false;

  g_write = CreateFileA(chosen.path, GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (g_write == INVALID_HANDLE_VALUE) {
    Log("DS4: the controller interface would not open for output (%lu)", GetLastError());
    return false;
  }
  if (chosen.dualSense) {
    // A DualSense has a different lightbar report; it is recognised but not driven yet.
    g_dualSense = true;
    Log("DS4: DualSense (054C:%04X) found; its lightbar report is not implemented, input only", chosen.pid);
  }
  g_read = CreateFileA(chosen.path, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (g_read == INVALID_HANDLE_VALUE) {
    Log("DS4: the controller interface would not open for input (%lu); lightbar only", GetLastError());
  }
  g_bluetooth = chosen.bluetooth;
  g_outputLength = chosen.outputLength;
  g_inputLength = chosen.inputLength;
  g_writeErrorLogged = false;
  char product[128] = {};
  WideCharToMultiByte(CP_ACP, 0, chosen.product, -1, product, sizeof product, nullptr, nullptr);
  Log("DS4: '%s' 054C:%04X on %s (out %u, in %u)", product[0] ? product : "controller", chosen.pid,
      g_bluetooth ? "Bluetooth" : "USB", g_outputLength, g_inputLength);
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

bool SendLight(Rgb color) {
  if (g_dualSense) return true;  // recognised, not driven: do not fail and rescan every frame
  if (g_bluetooth) {
    uint8_t report[ds4::kBtReportSize];
    ds4::BuildBtLightReport(report, color);
    const bool ok = WriteReport(report, sizeof report);
    if (!ok && !g_writeErrorLogged) {
      g_writeErrorLogged = true;
      Log("DS4: Bluetooth output report failed (%lu); output %u bytes", GetLastError(), g_outputLength);
    }
    return ok;
  }
  uint8_t report[ds4::kUsbReportSize];
  ds4::BuildUsbLightReport(report, color);
  const bool ok = WriteReport(report, sizeof report);
  if (!ok && !g_writeErrorLogged) {
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
bool g_audioTried = false;
bool g_audioMissing = false;
std::atomic<bool> g_shotRequested{false};
uint32_t g_burstFrame = 0;
uint32_t g_burstLength = 0;

IMMDevice* FindControllerSpeaker(IMMDeviceEnumerator* enumerator) {
  IMMDeviceCollection* collection = nullptr;
  if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection))) return nullptr;
  UINT count = 0;
  collection->GetCount(&count);
  IMMDevice* found = nullptr;
  for (UINT i = 0; i < count; ++i) {
    IMMDevice* device = nullptr;
    if (FAILED(collection->Item(i, &device))) continue;
    IPropertyStore* store = nullptr;
    PROPVARIANT name;
    PropVariantInit(&name);
    const bool named = SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) &&
                       SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &name)) &&
                       name.vt == VT_LPWSTR && name.pwszVal &&
                       (std::wcsstr(name.pwszVal, L"Wireless Controller") ||
                        std::wcsstr(name.pwszVal, L"DUALSHOCK") ||
                        std::wcsstr(name.pwszVal, L"DualShock"));
    if (named && !found) {
      found = device;
    } else if (name.vt == VT_LPWSTR && name.pwszVal && count <= 6) {
      // Only a handful of endpoints: showing their names once makes a missing match diagnosable.
      char text[96] = {};
      WideCharToMultiByte(CP_ACP, 0, name.pwszVal, -1, text, sizeof text, nullptr, nullptr);
      Log("DS4: render endpoint '%s'", text);
    }
    PropVariantClear(&name);
    if (store) store->Release();
    if (found != device) device->Release();
  }
  collection->Release();
  return found;
}

bool OpenSpeaker() {
  if (g_audio) return true;
  if (g_audioTried) return !g_audioMissing;
  g_audioTried = true;
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // fails harmlessly if already initialised
  IMMDeviceEnumerator* enumerator = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&enumerator)))) {
    g_audioMissing = true;
    Log("DS4: MMDeviceEnumerator unavailable; speaker silent");
    return false;
  }
  IMMDevice* device = FindControllerSpeaker(enumerator);
  enumerator->Release();
  if (!device) {
    g_audioMissing = true;
    Log("DS4: no controller audio endpoint; speaker silent (USB v2 or the wireless adapter "
        "exposes one, Bluetooth does not)");
    return false;
  }
  IAudioClient* client = nullptr;
  const HRESULT activated = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                             reinterpret_cast<void**>(&client));
  device->Release();
  if (FAILED(activated) || !client) {
    g_audioMissing = true;
    Log("DS4: controller audio endpoint could not be activated (0x%08lX)", activated);
    return false;
  }
  WAVEFORMATEX* format = nullptr;
  if (FAILED(client->GetMixFormat(&format)) || !format) {
    client->Release();
    g_audioMissing = true;
    Log("DS4: controller audio mix format unavailable; speaker silent");
    return false;
  }
  IAudioRenderClient* render = nullptr;
  const HRESULT init = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 400000, 0, format, nullptr);
  if (FAILED(init) || FAILED(client->GetService(IID_PPV_ARGS(&render)))) {
    CoTaskMemFree(format);
    client->Release();
    g_audioMissing = true;
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
  for (UINT32 frame = 0; frame < frames; ++frame) {
    float sample = 0.0f;
    if (g_burstFrame < g_burstLength) sample = GunshotSample(g_burstFrame++, rate);
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
  if (!g_installed || !sf::g_config.ds4Speaker) return;
  g_shotRequested.store(true);
}

void Ds4Tick() {
  if (!g_installed) return;
  const DWORD now = GetTickCount();
  if (g_write == INVALID_HANDLE_VALUE) {
    if (now - g_lastScan < kScanIntervalMs) return;
    g_lastScan = now;
    if (!OpenController()) {
      if (!g_scanLogged) {
        g_scanLogged = true;
        Log("DS4: no controller found yet; it is picked up as soon as it is connected");
      }
      return;
    }
    g_lightStart = now;
    g_lastLight = 0;
    g_scanLogged = false;
  }
  if (now - g_lastLight >= kLightIntervalMs) {
    g_lastLight = now;
    const int health = PlayerHealth();
    Rgb color;
    if (health < 0) {
      const ds4::Pulse idle{ds4::kIdleColor.r, ds4::kIdleColor.g, ds4::kIdleColor.b, 2000,
                            ds4::kIdleLevel};
      color = ds4::ScaledColor(idle, ds4::kIdleLevel);
    } else {
      const ds4::Pulse pulse = ds4::HealthPulse(health);
      color = ds4::ScaledColor(pulse, ds4::BreathLevel(pulse, now - g_lightStart));
    }
    if (!SendLight(color)) {
      Log("DS4: lightbar write failed; looking for the controller again");
      if (g_read != INVALID_HANDLE_VALUE) CloseHandle(g_read);
      g_read = INVALID_HANDLE_VALUE;
      CloseHandle(g_write);
      g_write = INVALID_HANDLE_VALUE;
      g_scanLogged = false;
      return;
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
  if (sf::g_config.ds4Speaker) OpenSpeaker();
  return true;
}

}  // namespace sf::features

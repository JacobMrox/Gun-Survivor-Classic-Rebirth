// DualShock 4: the lightbar breathes with the player's health and the controller speaker plays the
// gunshots. Both are best effort: no controller, no controller audio endpoint (Bluetooth has none on
// Windows) or a write failure simply leaves the feature idle, and each cause says so once in the log.
//
// The lightbar is driven with HID output reports (USB 0x05, Bluetooth 0x11); the speaker is the USB
// audio endpoint the controller exposes, rendered through WASAPI. Audio is written from the same
// render tick that drives the light, so no thread is created.
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

#include "../config.h"
#include "../ds4_logic.h"
#include "../game.h"
#include "../log.h"
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
constexpr USHORT kDs4V1 = 0x05C4;  // no USB audio
constexpr USHORT kDs4V2 = 0x09CC;  // USB audio (speaker / jack)
constexpr USHORT kDs4Adapter = 0x0BA0;

constexpr DWORD kLightIntervalMs = 40;  // 25 Hz is smooth for a breath and cheap on the wire
constexpr DWORD kScanIntervalMs = 2000;

bool g_installed = false;
HANDLE g_light = INVALID_HANDLE_VALUE;
bool g_bluetooth = false;
DWORD g_lightStart = 0;
DWORD g_lastLight = 0;
DWORD g_lastScan = 0;
bool g_scanLogged = false;

int PlayerHealth() {
  void* game = *reinterpret_cast<void**>(game::kGameState);
  void* stats = *reinterpret_cast<void**>(game::kGameStats);
  if (!game || !stats) return -1;
  // 6 = rooms; the stats object holds HP at +08 (00463E70 subtracts from it).
  if (game::Read<uint8_t>(game, game::kMainState) != 6) return -1;
  return game::Read<int>(stats, game::kGameStatsHp);
}

bool IsBluetoothPath(const char* path) {
  return path && (std::strstr(path, "BTHENUM") || std::strstr(path, "bthhfenum"));
}

// The first Sony gamepad with a writable HID interface. The same controller can expose several HID
// collections; the first one that opens for output is the one the lightbar report goes to.
bool OpenController() {
  GUID hid = {};
  HidD_GetHidGuid(&hid);
  HDEVINFO set = SetupDiGetClassDevsA(&hid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (set == INVALID_HANDLE_VALUE) return false;
  bool opened = false;
  for (DWORD index = 0; !opened; ++index) {
    SP_DEVICE_INTERFACE_DATA iface = {};
    iface.cbSize = sizeof iface;
    if (!SetupDiEnumDeviceInterfaces(set, nullptr, &hid, index, &iface)) break;
    DWORD needed = 0;
    SetupDiGetDeviceInterfaceDetailA(set, &iface, nullptr, 0, &needed, nullptr);
    if (!needed || needed > 2048) continue;
    alignas(SP_DEVICE_INTERFACE_DETAIL_DATA_A) char buffer[2048] = {};
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_A*>(buffer);
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
    if (!SetupDiGetDeviceInterfaceDetailA(set, &iface, detail, sizeof buffer, &needed, nullptr)) continue;
    HANDLE handle = CreateFileA(detail->DevicePath, GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) continue;
    HIDD_ATTRIBUTES attributes = {};
    attributes.Size = sizeof attributes;
    if (HidD_GetAttributes(handle, &attributes) && attributes.VendorID == kSonyVendor &&
        (attributes.ProductID == kDs4V1 || attributes.ProductID == kDs4V2 ||
         attributes.ProductID == kDs4Adapter)) {
      g_light = handle;
      g_bluetooth = IsBluetoothPath(detail->DevicePath);
      opened = true;
      Log("DS4: controller %04X:%04X on %s", attributes.VendorID, attributes.ProductID,
          g_bluetooth ? "Bluetooth" : "USB");
      break;
    }
    CloseHandle(handle);
  }
  SetupDiDestroyDeviceInfoList(set);
  return opened;
}

bool SendLight(Rgb color) {
  if (g_light == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  if (g_bluetooth) {
    uint8_t report[ds4::kBtReportSize];
    ds4::BuildBtLightReport(report, color);
    if (WriteFile(g_light, report, sizeof report, &written, nullptr)) return true;
    return HidD_SetOutputReport(g_light, report, sizeof report) != FALSE;
  }
  uint8_t report[ds4::kUsbReportSize];
  ds4::BuildUsbLightReport(report, color);
  if (WriteFile(g_light, report, sizeof report, &written, nullptr)) return true;
  return HidD_SetOutputReport(g_light, report, sizeof report) != FALSE;
}

// ---- controller speaker -----------------------------------------------------------------------
// Windows exposes the DualShock 4 as a USB audio device (v2 and the wireless adapter); over plain
// Bluetooth there is no render endpoint, so the speaker stays silent and says so once.
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
  for (UINT i = 0; i < count && !found; ++i) {
    IMMDevice* device = nullptr;
    if (FAILED(collection->Item(i, &device))) continue;
    IPropertyStore* store = nullptr;
    PROPVARIANT name;
    PropVariantInit(&name);
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) &&
        SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR &&
        name.pwszVal && std::wcsstr(name.pwszVal, L"Wireless Controller")) {
      found = device;
    }
    PropVariantClear(&name);
    if (store) store->Release();
    if (!found) device->Release();
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
    Log("DS4: no 'Wireless Controller' audio endpoint; speaker silent "
        "(USB v2 or the wireless adapter exposes one, Bluetooth does not)");
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
  // 40 ms shared buffer: the render tick runs at least once per frame, so it never starves.
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
  g_audioFloat = format->wBitsPerSample == 32;  // shared mode is float32 on current Windows
  Log("DS4: controller speaker ready (%lu Hz, %u channels, %u frames)",
      format->nSamplesPerSec, format->nChannels, frames);
  return true;
}

// One gunshot: a noise burst with a low thump, decaying in about 160 ms.
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

void Ds4OnPlayerShot() {
  if (!g_installed || !sf::g_config.ds4Speaker) return;
  g_shotRequested.store(true);
}

void Ds4Tick() {
  if (!g_installed) return;
  const DWORD now = GetTickCount();
  if (g_light == INVALID_HANDLE_VALUE) {
    if (now - g_lastScan < kScanIntervalMs) return;
    g_lastScan = now;
    if (!OpenController()) {
      if (!g_scanLogged) {
        g_scanLogged = true;
        Log("DS4: no controller found yet; the lightbar starts when one is connected");
      }
      return;
    }
    g_lightStart = now;
    g_lastLight = 0;
  }
  if (now - g_lastLight >= kLightIntervalMs) {
    g_lastLight = now;
    const int health = PlayerHealth();
    Rgb color;
    if (health < 0) {
      color = ds4::ScaledColor(ds4::Pulse{ds4::kIdleColor.r, ds4::kIdleColor.g, ds4::kIdleColor.b,
                                         2000, ds4::kIdleLevel},
                               ds4::kIdleLevel);
    } else {
      const ds4::Pulse pulse = ds4::HealthPulse(health);
      color = ds4::ScaledColor(pulse, ds4::BreathLevel(pulse, now - g_lightStart));
    }
    if (!SendLight(color)) {
      Log("DS4: lightbar write failed; looking for the controller again");
      CloseHandle(g_light);
      g_light = INVALID_HANDLE_VALUE;
      g_scanLogged = false;
      return;
    }
  }
  AudioTick();
}

bool InstallDs4() {
  if (!sf::g_config.ds4) return false;
  g_installed = true;
  Log("DS4: health lightbar enabled; controller speaker %s",
      sf::g_config.ds4Speaker ? "enabled" : "off");
  if (sf::g_config.ds4Speaker) OpenSpeaker();
  return true;
}

}  // namespace sf::features

// DualShock 4 logic that is not hardware-dependent: the health-driven lightbar and the exact
// output-report bytes for USB (0x05) and Bluetooth (0x11).
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sf::ds4 {

// 00463D1B initialises the player's HP to 0xC8; the CONDITION display changes band at 60% and 30%.
constexpr int kMaxHealth = 200;

// One breath: the colour of the RE condition band and how fast it breathes. The lower the health,
// the shorter the period, so danger reads as the fastest pulse.
struct Pulse {
  uint8_t r, g, b;
  uint32_t periodMs;
  float floorLevel;  // brightness at the bottom of the breath
};

constexpr Pulse HealthPulse(int health) {
  if (health <= 0) return Pulse{255, 0, 0, 600, 0.05f};      // dead: fast, almost black
  if (health >= 120) return Pulse{0, 255, 64, 2600, 0.25f};  // FINE
  if (health >= 60) return Pulse{255, 150, 0, 1500, 0.18f};  // CAUTION
  return Pulse{255, 0, 0, 650, 0.10f};                       // DANGER
}

// Cosine breath in 0..1: dark at the start of the period, full brightness half a period later.
inline float BreathLevel(const Pulse& pulse, uint32_t elapsedMs) {
  // A zero period must not reach the modulus: it would divide by zero before the float guard.
  const uint32_t periodMs = pulse.periodMs ? pulse.periodMs : 1;
  const float phase = static_cast<float>(elapsedMs % periodMs) / static_cast<float>(periodMs);
  const float wave = 0.5f - 0.5f * std::cos(phase * 6.28318531f);
  const float floorLevel =
      pulse.floorLevel < 0.0f ? 0.0f : (pulse.floorLevel > 1.0f ? 1.0f : pulse.floorLevel);
  return floorLevel + (1.0f - floorLevel) * wave;
}

struct Rgb {
  uint8_t r, g, b;
};

inline uint8_t ScaledChannel(uint8_t channel, float level) {
  if (level <= 0.0f) return 0;
  if (level >= 1.0f) return channel;
  const float value = static_cast<float>(channel) * level + 0.5f;
  return static_cast<uint8_t>(value < 0.0f ? 0.0f : (value > 255.0f ? 255.0f : value));
}

inline Rgb ScaledColor(const Pulse& pulse, float level) {
  return Rgb{ScaledChannel(pulse.r, level), ScaledChannel(pulse.g, level),
             ScaledChannel(pulse.b, level)};
}

// Outside a room the controller still shows that the wrapper is alive: a dim blue, steady.
constexpr Rgb kIdleColor{0, 48, 120};
constexpr float kIdleLevel = 0.35f;

// The d-pad is the LOW nibble of the buttons byte: 0..7 are directions, 8 is centred. Reading the
// high nibble instead turns the centred value 0x08 into Up and every face button into a direction,
// which is exactly how the first real-hardware build behaved.
struct HatDirections {
  bool up, right, down, left;
};

constexpr HatDirections HatDirection(uint8_t hat) {
  switch (hat & 0x0F) {
    case 0: return HatDirections{true, false, false, false};
    case 1: return HatDirections{true, true, false, false};
    case 2: return HatDirections{false, true, false, false};
    case 3: return HatDirections{false, true, true, false};
    case 4: return HatDirections{false, false, true, false};
    case 5: return HatDirections{false, false, true, true};
    case 6: return HatDirections{false, false, false, true};
    case 7: return HatDirections{true, false, false, true};
    default: return HatDirections{false, false, false, false};  // 8..15: centred
  }
}

// The pad's two motors: weak is the small high-frequency one, strong the large one.
struct Rumble {
  uint8_t weak, strong;
};

// A burst decays linearly and is silent once its duration has passed, so a 25 Hz writer needs no
// stop event: the next light report simply carries zero motors.
constexpr uint8_t RumbleLevel(uint32_t elapsedMs, uint32_t durationMs, uint8_t peak) {
  if (durationMs == 0 || elapsedMs >= durationMs) return 0;
  const uint32_t left = durationMs - elapsedMs;
  return static_cast<uint8_t>((static_cast<uint32_t>(peak) * left) / durationMs);
}

// ---- output reports --------------------------------------------------------------------------
// DS4-USB: report 0x05, 32 bytes, RGB at 6/7/8. DS4-BT: report 0x11, 78 bytes, two extra header
// bytes (hw control 0xC0 + audio control), RGB at 8/9/10 and a CRC32 in the last four.
constexpr size_t kUsbReportSize = 32;
constexpr size_t kBtReportSize = 78;

// IEEE 802.3 CRC-32 (reflected 0xEDB88320), the raw running value without the final complement.
inline uint32_t Crc32Update(uint32_t crc, const uint8_t* data, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return crc;
}

// The Bluetooth report is signed over a 0xA2 transaction byte plus everything but its own CRC
// (Linux hid-playstation.c: crc = crc32_le(0xFFFFFFFF, &0xA2, 1); crc = ~crc32_le(crc, data, len - 4)).
inline uint32_t BluetoothChecksum(const uint8_t* report, size_t length) {
  constexpr uint8_t seed = 0xA2;
  uint32_t crc = Crc32Update(0xFFFFFFFFu, &seed, 1);
  crc = Crc32Update(crc, report, length);
  return ~crc;
}

inline void BuildUsbLightReport(uint8_t* report, Rgb color, Rumble rumble = Rumble{0, 0}) {
  for (size_t i = 0; i < kUsbReportSize; ++i) report[i] = 0;
  report[0] = 0x05;  // output report id
  report[1] = 0x07;  // rumble, lightbar and flash are valid
  report[4] = rumble.weak;    // small motor
  report[5] = rumble.strong;  // large motor
  report[6] = color.r;
  report[7] = color.g;
  report[8] = color.b;
}

inline void BuildBtLightReport(uint8_t* report, Rgb color, Rumble rumble = Rumble{0, 0}) {
  for (size_t i = 0; i < kBtReportSize; ++i) report[i] = 0;
  report[0] = 0x11;  // output report id
  report[1] = 0xC0;  // HID + CRC enable (the two top bits)
  report[3] = 0x07;  // valid flags, one byte later than in the USB layout
  report[6] = rumble.weak;    // small motor, two bytes before the colour
  report[7] = rumble.strong;  // large motor
  report[8] = color.r;
  report[9] = color.g;
  report[10] = color.b;
  const uint32_t crc = BluetoothChecksum(report, kBtReportSize - 4);
  report[kBtReportSize - 4] = static_cast<uint8_t>(crc & 0xFF);
  report[kBtReportSize - 3] = static_cast<uint8_t>((crc >> 8) & 0xFF);
  report[kBtReportSize - 2] = static_cast<uint8_t>((crc >> 16) & 0xFF);
  report[kBtReportSize - 1] = static_cast<uint8_t>((crc >> 24) & 0xFF);
}

}  // namespace sf::ds4

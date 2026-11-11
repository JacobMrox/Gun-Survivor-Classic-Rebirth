#include "../source/survivor_fix/ds4_logic.h"

#include <cassert>
#include <cmath>
#include <cstring>

int main() {
  using namespace sf::ds4;

  // ---- health bands ---------------------------------------------------------------------------
  const Pulse fine = HealthPulse(kMaxHealth);
  const Pulse caution = HealthPulse(100);   // 50%
  const Pulse danger = HealthPulse(30);     // 15%
  const Pulse dead = HealthPulse(0);
  assert(fine.r == 0 && fine.g == 255 && fine.b < 100);
  assert(caution.r == 255 && caution.g == 150 && caution.b == 0);
  assert(danger.r == 255 && danger.g == 0 && danger.b == 0);
  assert(dead.r == 255 && dead.g == 0 && dead.b == 0);
  // Danger breathes faster than caution, which breathes faster than fine.
  assert(danger.periodMs < caution.periodMs);
  assert(caution.periodMs < fine.periodMs);
  assert(fine.periodMs > 0);
  // The band edges are inclusive on the healthy side.
  assert(HealthPulse(120).g == 255);
  assert(HealthPulse(119).g == 150);
  assert(HealthPulse(60).g == 150);
  assert(HealthPulse(59).g == 0);

  // ---- breath ---------------------------------------------------------------------------------
  assert(BreathLevel(fine, 0) == fine.floorLevel);           // dark at the start of the period
  assert(std::fabs(BreathLevel(fine, fine.periodMs / 2) - 1.0f) < 0.001f);  // full at half
  assert(std::fabs(BreathLevel(fine, fine.periodMs) - fine.floorLevel) < 0.001f);
  assert(BreathLevel(fine, fine.periodMs / 4) > fine.floorLevel);
  assert(BreathLevel(fine, fine.periodMs / 4) < 1.0f);
  // A zero period cannot divide by zero.
  assert(std::isfinite(BreathLevel(Pulse{0, 0, 0, 0, 0.5f}, 123)));
  // The floor stays inside the range even for a negative or too large setting.
  assert(BreathLevel(Pulse{1, 1, 1, 1000, -3.0f}, 0) >= 0.0f);
  assert(BreathLevel(Pulse{1, 1, 1, 1000, 9.0f}, 0) <= 1.0f);

  // ---- colour scaling -------------------------------------------------------------------------
  const Rgb full = ScaledColor(danger, 1.0f);
  assert(full.r == 255 && full.g == 0 && full.b == 0);
  const Rgb dark = ScaledColor(danger, 0.0f);
  assert(dark.r == 0 && dark.g == 0 && dark.b == 0);
  const Rgb half = ScaledColor(Pulse{200, 100, 50, 1, 0.0f}, 0.5f);
  assert(half.r == 100 && half.g == 50 && half.b == 25);
  assert(ScaledChannel(255, 2.0f) == 255);
  assert(ScaledChannel(255, -1.0f) == 0);

  // ---- CRC32 ----------------------------------------------------------------------------------
  // Standard check value, so the polynomial and the reflection are right.
  const char* check = "123456789";
  assert(~Crc32Update(0xFFFFFFFFu, reinterpret_cast<const uint8_t*>(check), 9) == 0xCBF43926u);

  // ---- reports --------------------------------------------------------------------------------
  const Rgb red{255, 0, 0};
  uint8_t usb[kUsbReportSize];
  BuildUsbLightReport(usb, red);
  assert(usb[0] == 0x05);
  assert(usb[1] == 0x07);
  assert(usb[6] == 255 && usb[7] == 0 && usb[8] == 0);
  bool usbZeroTail = true;
  for (size_t i = 9; i < kUsbReportSize; ++i) usbZeroTail = usbZeroTail && usb[i] == 0;
  assert(usbZeroTail);

  uint8_t bt[kBtReportSize];
  BuildBtLightReport(bt, Rgb{0, 255, 64});
  assert(bt[0] == 0x11);
  assert(bt[1] == 0xC0);
  assert(bt[3] == 0x07);
  assert(bt[8] == 0 && bt[9] == 255 && bt[10] == 64);
  bool btZeroBody = true;
  for (size_t i = 11; i < kBtReportSize - 4; ++i) btZeroBody = btZeroBody && bt[i] == 0;
  assert(btZeroBody);
  // The checksum is the little-endian CRC over 0xA2 + everything before it.
  const uint32_t expected = BluetoothChecksum(bt, kBtReportSize - 4);
  const uint32_t stored = static_cast<uint32_t>(bt[74]) | (static_cast<uint32_t>(bt[75]) << 8) |
                          (static_cast<uint32_t>(bt[76]) << 16) |
                          (static_cast<uint32_t>(bt[77]) << 24);
  assert(stored == expected);
  // A different colour produces a different checksum, so the CRC actually covers the payload.
  uint8_t other[kBtReportSize];
  BuildBtLightReport(other, Rgb{0, 0, 255});
  assert(std::memcmp(other, bt, kBtReportSize) != 0);
  assert(*reinterpret_cast<uint32_t*>(other + 74) != *reinterpret_cast<uint32_t*>(bt + 74));

  return 0;
}

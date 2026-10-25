// Original zombie locomotion audio: ENEMY_SLOT40 80104108/80104660.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace ps1 {
inline int ZombieLocomotionCue(uint32_t tick, int lane, int state, int animation, bool sameHeading) {
  if (lane != 1 || state == 0 || state == 10 || (tick & 31) != 0) return -1;
  switch (animation) { case 10: case 11: case 14: case 15: case 16: case 17: return -1; }
  return (tick & 32) == 0 && sameHeading ? 10 : 9;
}
// SPU ADPCM, low nibble first; histories continue between blocks of one sample.
inline bool DecodeSpu(const uint8_t* data, size_t size, std::vector<int16_t>& pcm) {
  pcm.clear();
  if (!data || !size || size % 16 || size > 65536) return false;
  constexpr int filters[5][2] = {{0,0},{60,0},{115,-52},{98,-55},{122,-60}};
  int previous = 0, previous2 = 0;
  std::vector<int16_t> decoded;
  for (size_t block = 0; block < size; block += 16) {
    const unsigned filter = data[block] >> 4, shift = data[block] & 15;
    if (filter > 4 || shift > 12) return false;
    for (size_t i = 2; i < 16; ++i) for (unsigned nibble = 0; nibble < 2; ++nibble) {
      int value = (data[block+i] >> (nibble*4)) & 15;
      if (value >= 8) value -= 16;
      value = ((value * 4096) >> shift) + ((previous*filters[filter][0] + previous2*filters[filter][1] + 32) >> 6);
      value = (std::clamp)(value, -32768, 32767);
      decoded.push_back(static_cast<int16_t>(value)); previous2 = previous; previous = value;
    }
    if (data[block+1] & 1) { pcm.swap(decoded); return true; }
  }
  return false; // Shared one-shot samples must have their SPU end flag.
}
// NOISE.BIN bank 2, entry 1A in the PS1 directory, also present unchanged in PC noise.bin.
// Fingerprint the complete 49152-byte bank before using its verified sample offsets.
inline bool SharedLocomotionClips(const uint8_t* data, size_t size, std::array<std::vector<int16_t>,2>& clips) {
  clips = {};
  constexpr size_t bankSize = 49152;
  if (!data || size < bankSize || size > 64*1024*1024) return false;
  for (size_t offset = 0; offset <= size-bankSize; offset += 2048) {
    const auto* b = data+offset;
    if (b[0]!=2 || b[4]!=12 || b[8]!=0x2c || b[9]!=0x10 || b[12]!='p' || b[13]!='B' || b[14]!='A' || b[15]!='V') continue;
    uint64_t fingerprint = 14695981039346656037ull;
    for (size_t i=0;i<bankSize;++i) fingerprint = (fingerprint ^ b[i]) * 1099511628211ull;
    if (fingerprint != 0xecdfc35248d255f4ull) continue;
    std::array<std::vector<int16_t>,2> decoded;
    if (!DecodeSpu(b+14044,1376,decoded[0]) || !DecodeSpu(b+15420,2720,decoded[1])) return false;
    clips.swap(decoded); return true;
  }
  return false;
}
inline std::vector<uint8_t> MonoWave(const std::vector<int16_t>& pcm) {
  if (pcm.empty() || pcm.size()>65536) return {};
  std::vector<uint8_t> wav(44+pcm.size()*2);
  const auto le=[&](size_t at,uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)wav[at+i]=static_cast<uint8_t>(v>>(i*8));};
  const char header[]="RIFF0000WAVEfmt ";std::copy(header,header+16,wav.begin());
  le(4,static_cast<uint32_t>(wav.size()-8),4);le(16,16,4);le(20,1,2);le(22,1,2);
  // VAB centre note 84, request note 60: 44100 / 4 = 11025 Hz.
  le(24,11025,4);le(28,22050,4);le(32,2,2);le(34,16,2);
  std::copy_n("data",4,wav.begin()+36);le(40,static_cast<uint32_t>(pcm.size()*2),4);
  for(size_t i=0;i<pcm.size();++i)le(44+i*2,static_cast<uint16_t>(pcm[i]),2);
  return wav;
}
}

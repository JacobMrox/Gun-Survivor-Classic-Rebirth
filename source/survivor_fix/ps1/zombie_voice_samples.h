// Verified original NOISE entries27..35, also present unchanged in PC noise.bin.
#pragma once
#include "zombie_audio_logic.h"
namespace ps1 {
inline bool DecodeZombieDeathVoices(const uint8_t* data,size_t size,
                                   std::array<std::vector<int16_t>,9>& out) {
  out={};
  if(!data || size<38912 || size>64*1024*1024)return false;
  struct Bank {size_t size;uint64_t fingerprint;};
  constexpr Bank banks[]={
    {49152, 0x59B0011683444350ull},
    {49152, 0x3EC9B86BF01F10F6ull},
    {47104, 0x638126F02C4DE83Dull},
    {38912, 0xADC7A7D003A8ECD3ull},
    {47104, 0x50F90E20302ACE71ull},
    {38912, 0x412C746FAE5D63D5ull},
    {45056, 0x33A9F44CAA8BCDFEull},
    {47104, 0xDD8D93409E5CA301ull},
    {47104, 0xDD8D93409E5CA301ull},
  };
  std::array<std::vector<int16_t>,9> decoded;
  for(size_t variant=0;variant<9;++variant) {
    const uint8_t* found=nullptr;
    const auto bank=banks[variant];
    for(size_t at=0;at<=size && bank.size<=size-at;at+=2048) {
      const uint8_t* p=data+at;
      if(p[0]!=2||p[4]!=12||p[12]!='p'||p[13]!='B'||p[14]!='A'||p[15]!='V')continue;
      uint64_t hash=14695981039346656037ull;
      for(size_t i=0;i<bank.size;++i)hash=(hash^p[i])*1099511628211ull;
      if(hash==bank.fingerprint){found=p;break;}
    }
    if(!found)return false;
    // VAB header at12, one program, twelve tones, four samples. Death mono1
    // and stereo6/7 all address VAG2, centre84; 8008038C requests note60.
    constexpr size_t tones=12+32+2048;
    constexpr size_t sizes=tones+512;
    const auto le16=[](const uint8_t* p){return unsigned(p[0])|(unsigned(p[1])<<8);};
    const auto le32=[](const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);};
    if(le16(found+12+18)!=1||le16(found+12+20)!=12||le16(found+12+22)!=4)return false;
    for(int cue:{1,6,7})if(le16(found+tones+cue*32+22)!=2||found[tones+cue*32+4]!=84)return false;
    const size_t body=le32(found+8);
    const size_t before=(le16(found+sizes)+le16(found+sizes+2))*8;
    const size_t bytes=le16(found+sizes+4)*8;
    if(body>bank.size||before>bank.size-body||bytes>bank.size-body-before)return false;
    if(!DecodeSpu(found+body+before,bytes,decoded[variant]))return false;
  }
  out.swap(decoded);return true;
}
} // namespace ps1

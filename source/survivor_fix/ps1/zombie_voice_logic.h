// Original zombie voice selection and binding: 8008DC94, 80080A28, 801047B4, 801005BC.
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ps1 {

inline int VoiceSelection(int nibble, int randomByte) {
  if (nibble >= 0 && nibble <= 8) return nibble;
  if (nibble != 15 || randomByte < 0 || randomByte > 255) return -1;
  constexpr int cumulative[] = {11,22,33,44,55,66,77,88};
  for (int i=0;i<8;++i) if (randomByte <= cumulative[i]*0x33/0x14) return i;
  return 8; // Original ninth fall-through, not an eight-way modulo.
}

struct VoiceProfileActor {int kind=0;int32_t x=0,z=0;int ordinal=0;};
struct VoiceProfile {uint8_t control=0;std::vector<VoiceProfileActor> actors;};
struct VoiceSpawn {uintptr_t life;int kind;float x,z;};
struct VoiceProfileMatch {bool valid=false;uint8_t control=0;std::vector<int> ordinals;};
inline uint32_t VoiceRead32(const uint8_t* p) {
  return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
inline int VoiceKind(int kind) {return kind==0x22||kind==0x23 ? 0x10 : kind;}

// 8007428C/80073F88: four-byte profile header, 24-byte spawn records,
// then the next Fxxx marker/header. Preserve ordinals of non-enemy objects.
inline std::vector<VoiceProfile> ParseVoiceProfiles(const uint8_t* data,size_t size) {
  std::vector<VoiceProfile> result;
  if (!data || size<4 || size>65536) return result;
  size_t at=0;
  while (at+4<size) {
    VoiceProfile profile;profile.control=data[at];at+=4;
    while (at+2<=size) {
      const unsigned kind=unsigned(data[at])|(unsigned(data[at+1])<<8);
      if ((kind&0xF000)==0xF000) break;
      if (size-at<24 || profile.actors.size()>=256) return {};
      profile.actors.push_back({int(kind),int32_t(VoiceRead32(data+at+4)),
                               int32_t(VoiceRead32(data+at+12)),int(profile.actors.size())});
      at+=24;
    }
    if (!profile.actors.empty()) result.push_back(profile);
    if (size-at<4) return {};
    if (at+4==size) return result;
  }
  return {};
}

inline VoiceProfileMatch MatchVoiceProfile(const std::vector<VoiceProfile>& profiles,
                                          const std::vector<VoiceSpawn>& native) {
  for(const auto& actor:native)if(!std::isfinite(actor.x)||!std::isfinite(actor.z))return {};
  struct Candidate {VoiceProfileMatch plan;bool exact=true;};
  std::vector<Candidate> candidates;
  for (const auto& profile:profiles) {
    Candidate candidate;candidate.plan.control=profile.control;
    size_t index=0;bool match=true;
    for (const auto& actor:profile.actors) {
      if (actor.kind>=0x32) continue; // Native manager contains enemy logic, not static props.
      if (index==native.size() || VoiceKind(actor.kind)!=native[index].kind) {match=false;break;}
      const auto& pc=native[index++];
      candidate.exact=candidate.exact && std::isfinite(pc.x) && std::isfinite(pc.z) &&
                      double(pc.x)==double(actor.x) && -double(pc.z)==double(actor.z);
      candidate.plan.ordinals.push_back(actor.ordinal);
    }
    if (match && index==native.size() && index!=0) candidates.push_back(candidate);
  }
  bool anyExact=false;for(const auto& c:candidates)anyExact=anyExact||c.exact;
  VoiceProfileMatch result;
  for (const auto& c:candidates) {
    if (anyExact && !c.exact) continue;
    if (result.valid && (result.control!=c.plan.control || result.ordinals!=c.plan.ordinals)) return {};
    result=c.plan;result.valid=true;
  }
  return result;
}

class ZombieVoiceState {
  std::array<int,2> resources_{{-1,-1}};
  std::array<bool,2> used_{{false,false}};
  std::array<int,7> banks_{{-1,-1,-1,-1,-1,-1,-1}};
public:
  // 8007428C clears resource-use flags; 800809B4 unloads room banks2..6.
  void BeginRoom(){used_.fill(false);banks_.fill(-1);}
  // 80088D4C releases unused code-resource keys after construction.
  void FinishRoom(){for(int i=0;i<2;++i)if(!used_[i])resources_[i]=-1;}
  int BindResource(int kind) {
    kind=VoiceKind(kind);
    if(kind<0 || kind>=0x32)return -1;
    if(kind==0x11)kind=0x10;
    if(kind==0x0E)kind=0x0D;
    if(kind==0x17)kind=0x16;
    if(kind<0x0B)kind=0;
    for(int i=1;i>=0;--i)if(resources_[i]==kind){used_[i]=true;return i;}
    const int slot=!used_[1]?1:(!used_[0]?0:-1);
    if(slot>=0){resources_[slot]=kind;used_[slot]=true;}
    return slot;
  }
  template<class Draw> bool LoadVoices(int resource,uint8_t control,Draw&& draw) {
    if(resource<0 || resource>1)return false;
    if(banks_[resource+3]>=0)return true;
    const int high=control>>4,low=control&15;
    if((high>8 && high!=15)||(low>8 && low!=15))return false;
    const int first=VoiceSelection(high,high==15?draw():0);
    const int second=VoiceSelection(low,low==15?draw():0);
    if(first<0 || second<0)return false;
    banks_[resource+3]=first;banks_[resource+4]=second;
    return true;
  }
  // 801005BC: E+84E + context[90] +3; 801047B4 sets context[90]=E+314&1.
  int Voice(int resource,int originalOrdinal)const {
    if(resource<0 || resource>1 || originalOrdinal<0)return -1;
    return banks_[resource+3+(originalOrdinal&1)];
  }
};
} // namespace ps1

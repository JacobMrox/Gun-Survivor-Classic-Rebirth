// PS1 resource-slot binding and room-controlled voices, not a random choice at death.
#include "zombie_shared.h"
#include "../../ps1/zombie_voice_logic.h"
#include <algorithm>

namespace sf::features::zombie {
namespace {
using BuildEnemiesFn=void(__stdcall*)(int context,int stage,int room);
BuildEnemiesFn nativeBuild=nullptr;
ps1::ZombieVoiceState voiceState;
struct Observation {ps1::VoiceSpawn spawn;uint8_t* object;};
struct Assignment {int variant;int kind;uint8_t* object;};
std::vector<Observation> observed;
std::unordered_map<void*,Assignment> assignments;
bool collecting=false;

void __stdcall BuildEnemies(int context,int stage,int room) {
  // 00430FB2 skips construction when5F2C14 is nonzero; this is not a new life.
  if(*reinterpret_cast<const uint8_t*>(game::kSkipRoomEnemyBuild)!=0) {
    nativeBuild(context,stage,room);return;
  }
  // Same lifecycle as8007428C and800809B4: new construction, no elapsed-time reset.
  assignments.clear();observed.clear();voiceState.BeginRoom();collecting=true;
  nativeBuild(context,stage,room);
  collecting=false;
  std::vector<ps1::VoiceSpawn> ordered;
  for(const auto& observation:observed)ordered.push_back(observation.spawn);
  const auto plan=ps1::MatchVoiceProfile(CurrentRoomVoiceProfiles(),ordered);
  // Resource keys also follow native construction in rooms with no supported profile.
  // No audio draws or invented assignment are made when profile metadata is ambiguous.
  for(size_t i=0;i<observed.size();++i) {
    const auto& observation=observed[i];const auto& actor=observation.spawn;
    const int resource=voiceState.BindResource(actor.kind);
    if(!plan.valid || !ps1::IsPcZombie(actor.kind))continue;
    if(!voiceState.LoadVoices(resource,plan.control,[](){return NextRngByte();}))continue;
    const int ordinal=plan.ordinals[i],variant=voiceState.Voice(resource,ordinal);
    if(variant<0)continue;
    auto* s=reinterpret_cast<void*>(actor.life);
    assignments.emplace(s,Assignment{variant,actor.kind,observation.object});
    Log("ZombieVoice: S=%p type=%d ordinal=%d resource=%d bank=%d variant=%d control=%02X",
        s,actor.kind,ordinal,resource,resource+3+(ordinal&1),variant,plan.control);
  }
  voiceState.FinishRoom();
  Log("ZombieVoice: native encounter %02X/%02X actors=%u profile=%d assigned=%u",
      stage,room,static_cast<unsigned>(ordered.size()),plan.valid,static_cast<unsigned>(assignments.size()));
}
} // namespace

void ObserveZombieVoiceLife(void* s) {
  assignments.erase(s); // Reused S never inherits a voice from its previous life.
  if(!collecting || !g_config.ps1ZombieVoiceBanks || !s)return;
  const int kind=game::Read<int>(s,game::S::kType);
  const int slot=game::Read<int>(s,game::S::kAiSlot);
  if(kind<0 || kind>=0x32 || slot<0 || slot>=30)return;
  uint8_t *o,*t;if(!GetObjects(s,o,t))return;
  const auto life=reinterpret_cast<uintptr_t>(s);
  observed.erase(std::remove_if(observed.begin(),observed.end(),[&](const Observation& v){return v.spawn.life==life;}),observed.end());
  const auto* pos=reinterpret_cast<const float*>(o+game::O::kPos);
  observed.push_back({{life,kind,pos[0],pos[2]},o});
}
int ZombieVoiceVariant(void* s) {
  if(!g_config.ps1ZombieVoiceBanks)return -1;
  const auto it=assignments.find(s);if(it==assignments.end())return -1;
  uint8_t *o,*t;
  if(game::Read<int>(s,game::S::kType)!=it->second.kind || !GetObjects(s,o,t) || o!=it->second.object)return -1;
  return it->second.variant;
}
bool InstallZombieVoiceBanks() {
  if(!g_config.ps1ZombieVoiceBanks || !g_config.ps1ZombieDeathAudio)return false;
  if(!game::CheckBytes({game::kBuildRoomEnemies,game::kBuildRoomEnemiesBytes,sizeof(game::kBuildRoomEnemiesBytes)}) ||
     !game::CheckBytes({game::kBuildRoomEnemiesRet,game::kBuildRoomEnemiesRetBytes,sizeof(game::kBuildRoomEnemiesRetBytes)}) ||
     !game::CheckBytes({game::kBuildRoomEnemyGate,game::kBuildRoomEnemyGateBytes,sizeof(game::kBuildRoomEnemyGateBytes)})) {
    Log("ZombieVoice: unexpected room-construction ABI; native voices retained");return false;
  }
  return Hook("ZombieVoiceRoom",game::kBuildRoomEnemies,reinterpret_cast<void*>(BuildEnemies),reinterpret_cast<void**>(&nativeBuild));
}
} // namespace sf::features::zombie

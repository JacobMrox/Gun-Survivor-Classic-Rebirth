// Locomotion sound independent of RequestAnim restarts (PS1 80104108/80104660).
#include "zombie_shared.h"
#include "../../ps1/zombie_audio_logic.h"
#include "../../ps1/zombie_voice_samples.h"
#include <fstream>
#include <iterator>
#include <intrin.h>

namespace sf::features::zombie {
namespace {
using SoundFn = unsigned(__fastcall*)(void*,void*,int,int);
using LoadFn = bool(__fastcall*)(void*,void*,const char*,char,int,char);
SoundFn nativeSound = nullptr;
LoadFn nativeLoad = nullptr;
bool ready = false;
bool voiceSamplesReady = false;
thread_local void* subject = nullptr;
thread_local int headingBefore = 0;
thread_local const char* replacement = nullptr;
enum class ReplacementKind {None,Walk,DeathVoice};
thread_local ReplacementKind replacementKind=ReplacementKind::None;
struct ReplacementScope {
  const char* previous=replacement;
  ReplacementKind kind=replacementKind;
  ReplacementScope(const char* path,ReplacementKind next){replacement=path;replacementKind=next;}
  ~ReplacementScope(){replacement=previous;replacementKind=kind;}
};
std::string paths[2];
std::array<std::string,9> voicePaths;
unsigned emitted = 0;

bool Managed(void* s) {
  uint32_t tick;
  if (!ready || !s || !g_config.ps1ZombieWalkAudio || !WorldEnabled() ||
      !g_config.ps1ZombieWalk || !g_config.ps1ZombieNavigation || !Ps1LogicFrame(&tick) ||
      !ps1::IsPcZombie(game::Read<int>(s, game::S::kType)) ||
      !IsLocomotion(game::Read<int>(s,game::S::kRequest))) return false;
  const auto* context=*reinterpret_cast<const uint8_t* const*>(game::kGameState);
  if (!context || context[game::kGameModeOffset]!=3) return false;
  auto entry = g_entries.find(s);
  if (entry == g_entries.end() || entry->second.phase != Phase::Idle) return false;
  uint8_t *o,*t;
  return GetObjects(s,o,t) && game::Read<float>(o,0x11C)>0;
}
int Heading(const uint8_t* o) {
  const auto* f=reinterpret_cast<const float*>(o+game::O::kForward);
  // The world bridge already steers in PS1 integer yaw units.
  return AngleUnits(std::atan2(f[0],-f[2])) & 4095;
}
bool __fastcall Load(void* sound,void* edx,const char* path,char mode,int flags,char algorithm) {
  const bool custom=replacement!=nullptr;
  const bool ok=nativeLoad(sound,edx,custom?replacement:path,mode,flags,algorithm);
  if(!custom)return ok;
  if(replacementKind==ReplacementKind::DeathVoice) {
    Log("ZombieVoice: original sample load=%d",ok);
    if(!ok)voiceSamplesReady=false;
  } else {
    if(emitted<16||!ok)Log("ZombieWalkAudio: load=%d",ok);
    if(!ok)ready=false;
  }
  // A missing or unusable private cache must not silence the stock sound.
  return ok || nativeLoad(sound,edx,path,mode,flags,algorithm);
}
unsigned __fastcall Sound(void* manager,void* edx,int family,int cue) {
  // 43D550 calls 471260(13,5) only when a walking animation is reapplied. Keep its
  // voices and all other callers; the PS1 event below owns this walking sound.
  if (reinterpret_cast<uintptr_t>(_ReturnAddress())==0x0043D5F2 &&
      subject && Managed(subject) && family==13 && cue==5) return 0;
  return nativeSound(manager,edx,family,cue);
}
bool CacheWave(const std::vector<int16_t>& pcm,int cue,std::string& path,const char* category="walk") {
  char temp[MAX_PATH];const auto length=GetTempPathA(MAX_PATH,temp);
  if(!length || length>=MAX_PATH)return false;
  path=std::string(temp)+"SurvivorFix-"+category+"-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(cue)+".wav";
  const auto bytes=ps1::MonoWave(pcm);if(bytes.empty())return false;
  HANDLE h=CreateFileA(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,nullptr);
  if(h==INVALID_HANDLE_VALUE) {
    if(GetLastError()!=ERROR_FILE_EXISTS)return false;
    std::ifstream existing(path,std::ios::binary|std::ios::ate);
    if(!existing || existing.tellg()!=static_cast<std::streamoff>(bytes.size()))return false;
    std::vector<uint8_t> found(bytes.size());existing.seekg(0);
    existing.read(reinterpret_cast<char*>(found.data()),found.size());
    return existing.good() && found==bytes;
  }
  DWORD written=0;const bool ok=WriteFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size();
  CloseHandle(h);return ok;
}
bool ReadNoise(std::vector<uint8_t>& bytes) {
  const std::wstring source=g_dir+L"PSX_DATA\\noise\\noise.bin";
  HANDLE h=CreateFileW(source.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(h==INVALID_HANDLE_VALUE)return false;
  LARGE_INTEGER size;bool ok=GetFileSizeEx(h,&size)&&size.QuadPart>0&&size.QuadPart<=64*1024*1024;
  bytes.resize(ok?static_cast<size_t>(size.QuadPart):0);
  DWORD read=0;if(ok)ok=ReadFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)&&read==bytes.size();
  CloseHandle(h);if(!ok)bytes.clear();return ok;
}
bool PrepareWalking(const std::vector<uint8_t>& bytes) {
  std::array<std::vector<int16_t>,2> clips;
  if(!ps1::SharedLocomotionClips(bytes.data(),bytes.size(),clips))return false;
  if(GetFileAttributesW((g_dir+L"PSX_DATA\\WAV\\ENEMY\\RICKER\\SND-005.WAV").c_str())==INVALID_FILE_ATTRIBUTES)return false;
  return CacheWave(clips[0],9,paths[0])&&CacheWave(clips[1],10,paths[1]);
}
bool PrepareVoices(const std::vector<uint8_t>& bytes) {
  std::array<std::vector<int16_t>,9> clips;
  if(!ps1::DecodeZombieDeathVoices(bytes.data(),bytes.size(),clips))return false;
  // The native dispatcher validates its stock path before entering pooled loading.
  for(const auto* family:{L"ZOMBIE0",L"ZOMBIE1"})
    if(GetFileAttributesW((g_dir+L"PSX_DATA\\WAV\\ENEMY\\"+family+L"\\SND-001.WAV").c_str())==INVALID_FILE_ATTRIBUTES)return false;
  for(int i=0;i<9;++i)if(!CacheWave(clips[i],i,voicePaths[i],"voice"))return false;
  return true;
}

}
void WalkAudioBefore(void* s) {
  subject=nullptr;
  if(!Managed(s))return;
  uint8_t *o,*t;if(!GetObjects(s,o,t))return;
  subject=s;headingBefore=Heading(o);
}
void WalkAudioAfter(void* s) {
  const bool active=subject==s;subject=nullptr;
  if(!active || !Managed(s))return;
  uint32_t tick;if(!Ps1LogicFrame(&tick))return;
  auto& entry=g_entries[s];if(entry.walkAudioTick==tick)return;
  uint8_t *o,*t;if(!GetObjects(s,o,t))return;
  // This bridge owns the ported state-4 chase in update lane 1. Other PS1 update
  // lanes and attacks are deliberately not inferred from PC selector numbers.
  const int cue=ps1::ZombieLocomotionCue(tick,1,4,game::Read<int>(s,game::S::kSelector),Heading(o)==headingBefore);
  if(cue<0)return;
  entry.walkAudioTick=tick;
  ReplacementScope replacementScope(paths[cue-9].c_str(),ReplacementKind::Walk);
  nativeSound(*reinterpret_cast<void**>(game::kGameState),nullptr,13,5);
  if(emitted++<16)Log("ZombieWalkAudio: S=%p tick=%u cue=%d selector=%d",s,tick,cue,game::Read<int>(s,game::S::kSelector));
}
bool PlayZombieDeathVoice(void* s) {
  if(!voiceSamplesReady||!nativeSound||!g_config.ps1ZombieDeathAudio)return false;
  const int variant=ZombieVoiceVariant(s);
  if(variant<0||variant>=9)return false;
  void* manager=*reinterpret_cast<void**>(game::kGameState);if(!manager)return false;
  ReplacementScope replacementScope(voicePaths[variant].c_str(),ReplacementKind::DeathVoice);
  nativeSound(manager,nullptr,0,1);
  Log("ZombieVoice: S=%p death variant=%d",s,variant);
  return true;
}
bool InstallZombieAudio() {
  const bool wantVoice=g_config.ps1ZombieDeath&&g_config.ps1ZombieDeathAudio&&g_config.ps1ZombieVoiceBanks;
  if(!g_config.ps1ZombieWalkAudio&&!wantVoice)return false;
  uint32_t tick;std::vector<uint8_t> bytes;
  if(!ReadNoise(bytes)){Log("ZombieAudio: original data unavailable; native audio retained");return false;}
  const bool walking=g_config.ps1ZombieWalkAudio&&Ps1LogicFrame(&tick)&&PrepareWalking(bytes);
  const bool voices=wantVoice&&PrepareVoices(bytes);
  if(!walking&&!voices){Log("ZombieAudio: original banks unavailable; native audio retained");return false;}
  const unsigned char soundBytes[]={0x83,0xec,0x74,0xa0,0x70,0x24,0x5f,0x00};
  const unsigned char loadBytes[]={0x64,0xa1,0x00,0x00,0x00,0x00,0x6a,0xff};
  if(!game::CheckBytes({0x00471260,soundBytes,sizeof(soundBytes)})||!game::CheckBytes({0x0057F970,loadBytes,sizeof(loadBytes)})) {
    Log("ZombieAudio: unexpected native ABI; unchanged");return false;
  }
  const bool hooked=Hook("ZombieSound",0x00471260,reinterpret_cast<void*>(Sound),reinterpret_cast<void**>(&nativeSound))&&
                    Hook("ZombieSoundLoad",0x0057F970,reinterpret_cast<void*>(Load),reinterpret_cast<void**>(&nativeLoad));
  ready=hooked&&walking;
  voiceSamplesReady=hooked&&voices&&InstallZombieVoiceBanks();
  Log("ZombieWalkAudio: candidate=%d; shared PS1 bank; native pooled playback",ready);
  Log("ZombieVoice: candidate=%d; original room assignment; verified local banks",voiceSamplesReady);
  return ready||voiceSamplesReady;
}

} // namespace sf::features::zombie

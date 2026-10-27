// Compile the production managed-death mapper against bounded native objects.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>
#include "../source/survivor_fix/config.h"
#include "../source/survivor_fix/ps1/death_logic.h"
#include "../source/survivor_fix/ps1/zombie_profile.h"
#ifndef _MSC_VER
#define __fastcall
#endif
using DWORD = unsigned long;
namespace game {
namespace S { constexpr size_t kSelector=0x28, kType=0x14, kNextSpeed=0x30; }
namespace O { constexpr size_t kForward=0x40, kHealth=0x11C; }
template<class T> T Read(const void* p,size_t off) { T v;std::memcpy(&v,static_cast<const uint8_t*>(p)+off,sizeof v);return v; }
template<class T> T* Field(void* p,size_t off) {return reinterpret_cast<T*>(static_cast<uint8_t*>(p)+off);}
uintptr_t kGameState;
}
sf::Config g_config;
enum class Phase {Idle,Death};
struct Entry {Phase phase=Phase::Idle;int reactClip=0,phaseBlend=0,deathClip=0;};
struct Death {bool native=false;int clips[3]={};int count=0;DWORD seen=0;};
struct Geometry {float toX,toZ;};
constexpr size_t kPool=0x5C;
std::unordered_map<void*,Death> g_deaths;
bool g_deathReady=true;
using DeathSoundFn=unsigned(__fastcall*)(void*,void*,int,int);
DeathSoundFn g_deathSound=nullptr;
ps1::DeathPart part=ps1::DeathPart::Body;
int missingClip=-1;
struct Objects {alignas(16) std::array<uint8_t,512> s{},o{},t{};};
std::unordered_map<void*,Objects*> actors;
struct Sound {int family,cue,type;};
std::vector<Sound> sounds;
void* activeSubject=nullptr;
void* manager=reinterpret_cast<void*>(uintptr_t{0x1234});
void require(bool ok,const char* message) {if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
unsigned __fastcall RecordSound(void* context,void*,int family,int cue) {
 require(context==manager,"native sound receives the game context");
 sounds.push_back({family,cue,game::Read<int>(activeSubject,game::S::kType)});return 0;
}
template<class... T> void Log(const char*,T...) {}
DWORD GetTickCount(){return 1;}
bool IsDeath(int request){return request==0x0F||request==0x10;}
bool GetObjects(void* s,uint8_t*& o,uint8_t*& t){auto it=actors.find(s);if(it==actors.end())return false;o=it->second->o.data();t=it->second->t.data();return true;}
Geometry Measure(const uint8_t*,const uint8_t*){return {0,1};}
ps1::DeathPart PartOf(const uint8_t*){return part;}
int NextRngByte(){return 255;}
bool ClipExists(const uint8_t*,int clip){return clip!=missingClip;}
void SetPhase(void* s,Entry& e,Phase phase,const char*){activeSubject=s;e.phase=phase;}
void HoldSequence(void*,Entry& e,int& result){result=e.deathClip;}
float DeathSpeed(int){return g_config.biteSpeed;}
bool PlayZombieDeathVoice(void*){return false;}
#include "zombie_death_voice_test.inc"
#include "zombie_death_map_test.inc"
void Init(Objects& a,int type=0,int clip=2) {
 a={};actors[a.s.data()]=&a;*game::Field<int>(a.s.data(),game::S::kType)=type;
 *game::Field<int>(a.s.data(),game::S::kSelector)=clip;
 *game::Field<float>(a.o.data(),game::O::kHealth)=0;
}
void Reset(){g_deaths.clear();actors.clear();sounds.clear();g_config={};g_config.ps1ZombieDeathAudio=true;g_deathReady=true;g_deathSound=RecordSound;missingClip=-1;part=ps1::DeathPart::Body;manager=reinterpret_cast<void*>(uintptr_t{0x1234});game::kGameState=reinterpret_cast<uintptr_t>(&manager);}
int main(){
 require(sf::Config{}.ps1ZombieDeathAudio,"managed death voice defaults to on");
 Reset();Objects a;Init(a);Entry e;int result=-1;
 require(DeathMap(a.s.data(),e,0x0F,result),"managed torso death selected");
 require(result==0x0B&&e.phase==Phase::Death,"PS1 torso sequence preserved");
 require(sounds.size()==1&&sounds[0].family==0&&sounds[0].cue==1,"one native death voice at managed death entry");
 for(int i=0;i<200;++i)DeathMap(a.s.data(),e,0x0F,result);
 require(sounds.size()==1,"repeated mapper calls and held corpse do not repeat the voice");
 for(int type:{0,1,2,4,5,6,8,9,10}) {
  Reset();Init(a,type);Entry next;DeathMap(a.s.data(),next,0x10,result);
  require(sounds.size()==1&&sounds[0].type==type,"every shared zombie variant gets the death voice");
 }
 for(auto hit:{ps1::DeathPart::Head,ps1::DeathPart::Body})for(int clip:{2,8,12,13,14,23,35}) {
  Reset();Init(a,0,clip);part=hit;Entry next;DeathMap(a.s.data(),next,0x0F,result);
  require(sounds.size()==1,"head, torso and grounded managed death plans emit once");
 }
 for(int gate=0;gate<9;++gate) {
  Reset();Init(a);Entry next;
  switch(gate){case 0:g_config.ps1ZombieDeathAudio=false;break;case 1:g_deathSound=nullptr;break;
  case 2:manager=nullptr;break;case 3:*game::Field<float>(a.o.data(),game::O::kHealth)=20;break;
  case 4:*game::Field<int>(a.s.data(),kPool)=1;break;case 5:*game::Field<int>(a.s.data(),game::S::kType)=3;break;
  case 6:g_config.ps1ZombieDeath=false;break;case 7:g_deathReady=false;break;case 8:*game::Field<float>(a.o.data(),game::O::kHealth)=std::numeric_limits<float>::quiet_NaN();break;}
  DeathMap(a.s.data(),next,0x0F,result);require(sounds.empty(),"disabled, living, corpse, unsupported and unavailable paths stay silent");
 }
 for(int clip:{9,21}) {Reset();Init(a,0,clip);Entry next;require(!DeathMap(a.s.data(),next,0x0F,result)&&sounds.empty(),"native fallback retains native audio ownership");}
 Reset();Init(a);missingClip=12;Entry missing;require(!DeathMap(a.s.data(),missing,0x0F,result)&&sounds.empty(),"missing follow-up keeps native fallback without duplicate audio");
 Reset();Init(a);Entry idle;require(!DeathMap(a.s.data(),idle,2,result)&&sounds.empty(),"living locomotion unchanged");
 Reset();Init(a);Objects b;Init(b,1);Entry ea,eb;DeathMap(a.s.data(),ea,15,result);DeathMap(b.s.data(),eb,15,result);
 require(sounds.size()==2,"independent actors have independent death events");
 g_deaths.erase(a.s.data());ea={};DeathMap(a.s.data(),ea,15,result);require(sounds.size()==3,"native actor reset permits a fresh death event");
 std::puts("PASS: production zombie death voice timing and ownership");
}

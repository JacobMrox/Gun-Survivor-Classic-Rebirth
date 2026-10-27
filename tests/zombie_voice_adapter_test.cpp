// Compile the production construction/lifetime bridge with bounded native objects.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>
#include "../source/survivor_fix/config.h"
#include "../source/survivor_fix/ps1/zombie_voice_logic.h"
#include "../source/survivor_fix/ps1/zombie_profile.h"
#ifndef _MSC_VER
#define __stdcall
#endif
namespace sf {Config g_config;}
namespace game {
namespace S {constexpr size_t kType=0x14,kAiSlot=4;}
namespace O {constexpr size_t kPos=8;}
template<class T>T Read(const void* p,size_t off){T value;std::memcpy(&value,static_cast<const uint8_t*>(p)+off,sizeof value);return value;}
struct ByteSignature {uintptr_t address;const uint8_t* bytes;size_t size;};
bool signatures=true;
bool CheckBytes(ByteSignature){return signatures;}
constexpr uintptr_t kBuildRoomEnemies=0x430F90,kBuildRoomEnemiesRet=0x4346B4;
constexpr uint8_t kBuildRoomEnemiesBytes[]={0},kBuildRoomEnemiesRetBytes[]={0};
uint8_t skip=0;uintptr_t kSkipRoomEnemyBuild=reinterpret_cast<uintptr_t>(&skip);
constexpr uintptr_t kBuildRoomEnemyGate=0x430FB2;
constexpr uint8_t kBuildRoomEnemyGateBytes[]={0};
}
struct Object {alignas(16)std::array<uint8_t,128> s{};alignas(16)std::array<uint8_t,512> o{},t{};};
std::unordered_map<void*,Object*> objects;
std::vector<Object*> construction;
std::vector<ps1::VoiceProfile> profiles;
std::vector<int> randomBytes;
size_t drawCount=0;
int NextRngByte(){return randomBytes.at(drawCount++);}
const std::vector<ps1::VoiceProfile>& CurrentRoomVoiceProfiles(){return profiles;}
bool GetObjects(void* s,uint8_t*& o,uint8_t*& t){auto it=objects.find(s);if(it==objects.end())return false;o=it->second->o.data();t=it->second->t.data();return true;}
template<class...T>void Log(const char*,T...){}
int hookAttempts=0;
bool Hook(const char*,uintptr_t,void*,void**){++hookAttempts;return false;}
#include "zombie_voice_adapter_test.inc"
using namespace sf::features::zombie;
void need(bool b,const char* m){if(!b){std::fprintf(stderr,"FAIL: %s\n",m);std::exit(1);}}
void Init(Object& a,int kind,int slot,float x,float z){a={};std::memcpy(a.s.data()+0x14,&kind,4);std::memcpy(a.s.data()+4,&slot,4);std::memcpy(a.o.data()+8,&x,4);std::memcpy(a.o.data()+16,&z,4);objects[a.s.data()]=&a;}
void __stdcall NativeBuild(int,int,int){if(game::skip)return;for(auto* a:construction)ObserveZombieVoiceLife(a->s.data());}
void Setup(){sf::g_config={};sf::g_config.ps1ZombieDeathAudio=true;sf::g_config.ps1ZombieVoiceBanks=true;nativeBuild=NativeBuild;voiceState={};observed.clear();assignments.clear();objects.clear();drawCount=0;randomBytes={0,255,100,200};game::skip=0;}
int main(){
 Setup();Object a,b;Init(a,0,0,100,-200);Init(b,1,1,300,-400);construction={&a,&b};
 profiles={ps1::VoiceProfile{255,{{0,100,200,0},{1,300,400,1}}}};
 BuildEnemies(0,3,7);need(ZombieVoiceVariant(a.s.data())==0&&ZombieVoiceVariant(b.s.data())==8,"native constructor snapshot gets original paired voices");need(drawCount==2,"only bank creation consumes the two voice draws");
 for(int i=0;i<100;++i){ZombieVoiceVariant(a.s.data());ZombieVoiceVariant(b.s.data());}need(drawCount==2,"lookups and repeated death/update requests do not draw again");
 game::skip=1;BuildEnemies(0,3,7);need(ZombieVoiceVariant(a.s.data())==0&&ZombieVoiceVariant(b.s.data())==8,"skipped native construction preserves existing voice assignments");need(drawCount==2,"skipped native construction consumes no RNG");game::skip=0;
 ObserveZombieVoiceLife(a.s.data());need(ZombieVoiceVariant(a.s.data())==-1&&ZombieVoiceVariant(b.s.data())==8,"same S reinitialised outside construction loses only its own old voice");
 BuildEnemies(0,3,7);need(ZombieVoiceVariant(a.s.data())==3&&ZombieVoiceVariant(b.s.data())==7,"real reconstruction reloads banks once");need(drawCount==4,"reconstruction consumes exactly another two draws");
 Setup();Init(a,0,0,100,-200);Init(b,1,1,300,-400);construction={&a,&b};
 profiles={ps1::VoiceProfile{255,{{0,100,200,0},{50,0,0,1},{1,300,400,2}}}};
 BuildEnemies(0,3,7);need(ZombieVoiceVariant(a.s.data())==0&&ZombieVoiceVariant(b.s.data())==0,"original props retain actor parity rather than PC manager-slot parity");
 profiles.push_back(ps1::VoiceProfile{0,{{0,100,200,0},{50,0,0,1},{1,300,400,2}}});drawCount=0;
 BuildEnemies(0,3,7);need(ZombieVoiceVariant(a.s.data())==-1&&drawCount==0,"ambiguous original encounter retains stock voice without extra draws");
 profiles.resize(1);construction={&a};BuildEnemies(0,3,7);need(ZombieVoiceVariant(a.s.data())==-1&&drawCount==0,"incomplete PC encounter is not guessed");
 sf::g_config.ps1ZombieVoiceBanks=false;need(!InstallZombieVoiceBanks()&&hookAttempts==0,"default/disabled bank adapter installs no hook");
 sf::g_config.ps1ZombieVoiceBanks=true;game::signatures=false;need(!InstallZombieVoiceBanks()&&hookAttempts==0,"unrecognised native construction ABI installs no hook");
 std::puts("PASS: production voice construction, lifetime, ambiguity and skipped native path");
}

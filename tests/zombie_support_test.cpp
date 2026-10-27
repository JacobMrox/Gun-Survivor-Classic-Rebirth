// Recorded 03/07 support: centre -388, radius 390, floor -198 instead of 2.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#ifndef _MSC_VER
#define __fastcall
#endif
namespace game {
namespace C {constexpr size_t kObjects=0,kCount=sizeof(void*);}
namespace O {constexpr size_t kPos=8,kAimPoint=0xB8,kCollisionMode=0xF8,kCollisionFlags=0x108;}
template<class T>T Read(const void* p,size_t off){T v;std::memcpy(&v,static_cast<const uint8_t*>(p)+off,sizeof v);return v;}
template<class T>T* Field(void* p,size_t off){return reinterpret_cast<T*>(static_cast<uint8_t*>(p)+off);}
}
namespace sf::features::zombie {
struct {bool ps1ZombieHeightSupport=true;} g_config;
bool owned=true;int syncs=0;
const uint8_t* actorMesh=nullptr;
void* FindObject(const uint8_t* o){return o==actorMesh?reinterpret_cast<void*>(1):nullptr;}
bool WorldOwnsObject(const uint8_t*){return owned;}
void SyncObjectPose(uint8_t* o){++syncs;game::Field<float>(o,game::O::kAimPoint)[1]=game::Field<float>(o,game::O::kPos)[1]-400;}
#include "zombie_support_test.inc"
}
using namespace sf::features::zombie;
std::array<uint8_t,0x140> actor{},meshA{},meshB{},box{};
std::array<uint8_t,32> collection{};
uint8_t* objects[]={actor.data(),meshA.data(),meshB.data(),box.data()};
float floorA=2,floorB=2;bool hit=true,mutateOrigin=false;int calls=0,failures=0;
#define CHECK(c) do{if(!(c)){std::printf("FAIL line %d: %s\n",__LINE__,#c);++failures;}}while(0)
unsigned char __fastcall Sweep(void* m,void*,float* origin,float radius,float* step,int* face,float*){
 ++calls;CHECK(radius==0);CHECK(step[0]==0&&step[2]==0&&step[1]==780);
 const float floor=m==meshA.data()?floorA:floorB;step[1]=floor-origin[1];*face=230;
 if(mutateOrigin)origin[1]=9000;return hit?1:0;
}
void Reset(){actor.fill(0);meshA.fill(0);meshB.fill(0);box.fill(0);collection.fill(0);
 *game::Field<uint8_t**>(collection.data(),game::C::kObjects)=objects;
 *game::Field<int>(collection.data(),game::C::kCount)=4;
 *game::Field<int8_t>(actor.data(),game::O::kCollisionMode)=1;
 *game::Field<int8_t>(meshA.data(),game::O::kCollisionMode)=3;
 *game::Field<int8_t>(meshB.data(),game::O::kCollisionMode)=3;
 *game::Field<int8_t>(box.data(),game::O::kCollisionMode)=2;
 auto* p=game::Field<float>(actor.data(),8);p[0]=3300;p[1]=12;p[2]=-1590;
 auto* a=game::Field<float>(actor.data(),0xB8);a[0]=3300;a[1]=-388;a[2]=-1590;
 *game::Field<float>(actor.data(),0xC4)=390;
 *game::Field<float>(actor.data(),0xC8)=17; // Native velocity is not replaced.
 actorMesh=nullptr;owned=true;g_config.ps1ZombieHeightSupport=true;g_supportSweep=Sweep;syncs=calls=0;floorA=floorB=2;hit=true;mutateOrigin=false;
}
int main(){
 Reset();floorA=-198;CHECK(ResolveNativeSupport(collection.data(),actor.data()));
 CHECK(game::Read<float>(actor.data(),12)==-188);CHECK(game::Read<float>(actor.data(),0xBC)==-588);
 CHECK(game::Read<float>(actor.data(),8)==3300&&game::Read<float>(actor.data(),16)==-1590);
 CHECK(game::Read<float>(actor.data(),0xC8)==17&&syncs==1);
 Reset();CHECK(!ResolveNativeSupport(collection.data(),actor.data()));CHECK(game::Read<float>(actor.data(),12)==12&&syncs==0);
 Reset();g_config.ps1ZombieHeightSupport=false;floorA=-198;CHECK(!ResolveNativeSupport(collection.data(),actor.data())&&calls==0);
 Reset();owned=false;floorA=-198;CHECK(!ResolveNativeSupport(collection.data(),actor.data())&&calls==0);
 Reset();g_supportSweep=nullptr;CHECK(!ResolveNativeSupport(collection.data(),actor.data())&&calls==0);
 Reset();floorA=-198;*game::Field<uint32_t>(meshA.data(),0x108)=2;CHECK(!ResolveNativeSupport(collection.data(),actor.data())&&calls==1);
 Reset();hit=false;floorA=-198;CHECK(!ResolveNativeSupport(collection.data(),actor.data())&&syncs==0);
 Reset();floorA=200;floorB=300;CHECK(!ResolveNativeSupport(collection.data(),actor.data())); // No downward snap.
 Reset();floorA=-400;floorB=1000;CHECK(!ResolveNativeSupport(collection.data(),actor.data())); // Outside the native sweep.
 Reset();floorA=std::numeric_limits<float>::quiet_NaN();CHECK(!ResolveNativeSupport(collection.data(),actor.data()));
 Reset();*game::Field<float>(actor.data(),0xC4)=0;CHECK(!ResolveNativeSupport(collection.data(),actor.data())&&calls==0);
 Reset();*game::Field<float>(actor.data(),0xBC)=std::numeric_limits<float>::infinity();CHECK(!ResolveNativeSupport(collection.data(),actor.data())&&calls==0);
 Reset();floorA=-198;mutateOrigin=true;CHECK(ResolveNativeSupport(collection.data(),actor.data()));CHECK(game::Read<float>(actor.data(),12)==-188);
 Reset();floorA=-198;*game::Field<uint32_t>(actor.data(),0x108)=1;CHECK(!ResolveNativeSupport(collection.data(),actor.data())&&calls==0);
 Reset();floorA=-198;*game::Field<int8_t>(actor.data(),game::O::kCollisionMode)=2;CHECK(!ResolveNativeSupport(collection.data(),actor.data())&&calls==0);
 Reset();floorA=-198;actorMesh=meshA.data();CHECK(!ResolveNativeSupport(collection.data(),actor.data())&&calls==1);
 if(failures)return 1;std::puts("PASS: zombie native support, recorded step, gates and fall preservation");
}

// Exercises the production collision adapter and its existing doorway regressions.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../source/survivor_fix/ps1/wall_logic.h"
#ifndef _MSC_VER
#define __fastcall
#endif
namespace game {
namespace C { constexpr size_t kObjects=0,kCount=sizeof(void*),kTarget=sizeof(void*)+4; }
namespace O { constexpr size_t kPos=8,kCollisionMode=0xF8,kCollisionFlags=0x108,
 kContactNormal=0xEC,kContactIndex=0xFC,kMatrix=0x3C,kMatrixRowFloats=4; }
constexpr uintptr_t kNative0700DoorMinX=0x005AD65C,kNative0700DoorMaxX=0x005AD658,
 kNative0700DoorZ=0x005AD654;
constexpr uintptr_t kNative0801DoorMinX=0x005AD648,kNative0801DoorMaxX=0x005AD8E8,
 kNative0801StorageMinZ=0x005AD8E4,kNative0801StorageMaxZ=0x005AD8E0;
float storageMinX=-3352.1001f,storageMaxX=-3338.f,storageMinZ=-5014.2798f,storageMaxZ=-2911.7600f;
float doorMin=7298,doorMax=8302,doorZ=-5838;
struct Override {uintptr_t address;const void* data;size_t size;};
std::vector<Override> readOverrides;
template<class T> T Read(const void* p,size_t off) {
 uintptr_t a=reinterpret_cast<uintptr_t>(p)+off; T value{};
 for(const auto& overrideValue:readOverrides)if(a==overrideValue.address && sizeof(T)==overrideValue.size){std::memcpy(&value,overrideValue.data,sizeof(T));return value;}
 if(a==kNative0700DoorMinX)std::memcpy(&value,&doorMin,sizeof(T));
 else if(a==kNative0700DoorMaxX)std::memcpy(&value,&doorMax,sizeof(T));
 else if(a==kNative0700DoorZ)std::memcpy(&value,&doorZ,sizeof(T));
 else if(a==kNative0801DoorMinX)std::memcpy(&value,&storageMinX,sizeof(T));
 else if(a==kNative0801DoorMaxX)std::memcpy(&value,&storageMaxX,sizeof(T));
 else if(a==kNative0801StorageMinZ)std::memcpy(&value,&storageMinZ,sizeof(T));
 else if(a==kNative0801StorageMaxZ)std::memcpy(&value,&storageMaxZ,sizeof(T));
 else std::memcpy(&value,reinterpret_cast<const void*>(a),sizeof(T)); return value;
}
template<class T> T* Field(void* p,size_t o) { return reinterpret_cast<T*>(static_cast<uint8_t*>(p)+o); }
}
namespace sf::features::zombie {void WorldBeforeQuery(void*,int) {}}
namespace sf::features {
struct TestConfig { bool ps1PlayerWalls=true,ps1PlayerWallsMesh=true,ps1PlayerHeightSupport=false; } g_config;
std::vector<ps1::Wall> testWalls={{10304,5056,-11680,5056}};
int stage=7,room=0;
const std::vector<ps1::Wall>& CurrentRoomWalls(){return testWalls;}
std::vector<ps1::DoorEvent> testDoors;
const std::vector<ps1::DoorEvent>& CurrentRoomDoors(){return testDoors;}
bool IsCurrentRoom(int s,int r){return s==stage&&r==room;}
void Log(const char*,...){}
#include "player_walls_test.inc"
}
using namespace sf::features;
static int failures=0;
#define CHECK(c) do{if(!(c)){std::printf("FAIL line%d: %s\n",__LINE__,#c);++failures;}}while(0)
alignas(16) std::array<uint8_t,0x140> self{},box{},mesh{};
alignas(16) std::array<uint8_t,32> collection{};
uint8_t* objects[]={self.data(),box.data(),mesh.data()};
uint32_t flagsSeen=0;
int nativeCalls=0;
float meshNormalX=0;
int __fastcall NativeQuery(void*,void*,int,float* disp,float* resp,float*){
 ++nativeCalls;flagsSeen=game::Read<uint32_t>(box.data(),game::O::kCollisionFlags);
 disp[2]*=.25f;resp[0]=.5f;
 *game::Field<int>(self.data(),game::O::kContactIndex)=2;
 float* normal=game::Field<float>(self.data(),game::O::kContactNormal);
 normal[0]=meshNormalX;normal[1]=0;normal[2]=meshNormalX==0?1.0f:0.0f;return 1;
}
void Reset(float x=8031,float z=-5056){
 self.fill(0);box.fill(0);mesh.fill(0);collection.fill(0);
 *game::Field<uint8_t**>(collection.data(),game::C::kObjects)=objects;
 *game::Field<int>(collection.data(),game::C::kCount)=3;
 *game::Field<int>(collection.data(),game::C::kTarget)=0;
 *game::Field<int8_t>(box.data(),game::O::kCollisionMode)=2;
 *game::Field<int8_t>(mesh.data(),game::O::kCollisionMode)=3;
 float* pos=game::Field<float>(self.data(),game::O::kPos);pos[0]=x;pos[1]=-1588;pos[2]=z;
 g_query=NativeQuery;stage=7;room=0;g_config={true,true};meshNormalX=0;
 game::doorMin=7298;game::doorMax=8302;game::doorZ=-5838;
 testWalls={{10304,5056,-11680,5056}};
#ifdef SF_TEST_NATIVE_DOOR_ACCESS
 g_nativeDoorAccessReady=true;
#endif
}
void CrossingDoorKeepsNativeResult(){
 Reset(8031,-5000);float before[]={8031,-1588,-5000};
 float* pos=game::Field<float>(self.data(),game::O::kPos);pos[2]=-5080;
 ApplyPs1Walls(self.data(),before);CHECK(pos[2]==-5080);
}
void ReturningFromStepKeepsNativeResult(){
 Reset(8031,-5080);float before[]={8031,-1588,-5080};
 float* pos=game::Field<float>(self.data(),game::O::kPos);pos[2]=-5000;
 ApplyPs1Walls(self.data(),before);CHECK(pos[2]==-5000);
}
void InsideDoorUsesNativeCollision(){
 Reset(8031,-5430);float disp[]={0,0,-80},resp[]={0,0,0},dir[]={0,0,-1};
 int r=QueryDetour(collection.data(),nullptr,0,disp,resp,dir);
 CHECK(r==1);CHECK(flagsSeen==0);CHECK(disp[2]==-20);CHECK(resp[0]==.5f);
 CHECK(game::Read<uint32_t>(box.data(),game::O::kCollisionFlags)==0);
}
void OtherRoomStillStopsAndFilters(){
 Reset();room=4;float before[]={8031,-1588,-5000};
 game::Field<float>(self.data(),game::O::kPos)[2]=-5080;
 ApplyPs1Walls(self.data(),before);CHECK(game::Field<float>(self.data(),game::O::kPos)[2]==-5056);
 float d[]={0,0,-80},r[]={0,0,0},v[]={0,0,-1};
 QueryDetour(collection.data(),nullptr,0,d,r,v);CHECK((flagsSeen&2)!=0);CHECK(d[2]==-80);CHECK(r[0]==0);
 CHECK(game::Read<uint32_t>(box.data(),game::O::kCollisionFlags)==0);
}
void PerpendicularMeshFaceStaysNative(){

 Reset();room=4;meshNormalX=1;float d[]={0,0,-80},r[]={0,0,0},v[]={0,0,-1};
 QueryDetour(collection.data(),nullptr,0,d,r,v);CHECK(d[2]!=-80);
}
void OutsideDoorStillStops(){
 for(float x:{7297.f,8303.f}){Reset(x,-5000);float b[]={x,-1588,-5000};
 game::Field<float>(self.data(),game::O::kPos)[2]=-5080;ApplyPs1Walls(self.data(),b);
 CHECK(game::Field<float>(self.data(),game::O::kPos)[2]==-5056);}
}
void MissingGeometryDoesNotDisableNormalQuery(){
 Reset();testWalls.clear();float d[]={0,0,-80},r[]={0,0,0},v[]={0,0,-1};
 QueryDetour(collection.data(),nullptr,0,d,r,v);CHECK((flagsSeen&2)!=0);
}
void DisabledFeatureUsesNativeQuery(){
 Reset();g_config.ps1PlayerWalls=false;float d[]={0,0,-80},r[]={0,0,0},v[]={0,0,-1};
 QueryDetour(collection.data(),nullptr,0,d,r,v);CHECK(flagsSeen==0);CHECK(d[2]==-20);
}
void UnverifiedMetadataKeepsOldResponse(){
 Reset();
#ifdef SF_TEST_NATIVE_DOOR_ACCESS
 g_nativeDoorAccessReady=false;
#endif
 float before[]={8031,-1588,-5000};game::Field<float>(self.data(),game::O::kPos)[2]=-5080;
 ApplyPs1Walls(self.data(),before);CHECK(game::Field<float>(self.data(),game::O::kPos)[2]==-5056);
}
void InvalidDoorBoundsKeepNormalQuery(){
 Reset();game::doorMin=9000;game::doorMax=8000;
 float d[]={0,0,-80},r[]={0,0,0},v[]={0,0,-1};
 QueryDetour(collection.data(),nullptr,0,d,r,v);CHECK((flagsSeen&2)!=0);
}
void SideEntryKeepsWall(){
 Reset();float before[]={8500,-1588,-5000};game::Field<float>(self.data(),game::O::kPos)[2]=-5080;
 ApplyPs1Walls(self.data(),before);CHECK(game::Field<float>(self.data(),game::O::kPos)[2]==-5056);
}

void StorageReset(float x=-2600,float z=-3764){
 Reset(x,z);stage=8;room=1;testWalls={{-2656,8960,-2656,1696}};
 game::storageMinX=-3352.1001f;game::storageMaxX=-3338.f;
 game::storageMinZ=-5014.2798f;game::storageMaxZ=-2911.7600f;
#ifdef SF_TEST_STORAGE_DOOR_ACCESS
 g_native0801DoorAccessReady=true;
#endif
}
void StorageCrossingAndReturnKeepNativePosition(){
 for(bool returning:{false,true}) {
  StorageReset();float before[]={returning?-2700.f:-2600.f,-1588,-3764};
  float* pos=game::Field<float>(self.data(),game::O::kPos);pos[0]=returning?-2600.f:-2700.f;
  ApplyPs1Walls(self.data(),before);CHECK(pos[0]==(returning?-2600.f:-2700.f));
 }
}
void StorageNativeQueryPreservesWallResponse(){
 StorageReset(-3000);float d[]={-80,0,0},r[]={0,0,0},v[]={-1,0,0};
 CHECK(QueryDetour(collection.data(),nullptr,0,d,r,v)==1);
 CHECK(flagsSeen==0);CHECK(r[0]==.5f);
 CHECK(game::Read<uint32_t>(box.data(),game::O::kCollisionFlags)==0);
}
void StorageOutsideSpanAndUnrelatedDoorKeepWalls(){
 for(float z:{-5015.f,-2911.f,-7000.f}) {
  StorageReset(-2600,z);float before[]={-2600,-1588,z};
  game::Field<float>(self.data(),game::O::kPos)[0]=-2700;
  ApplyPs1Walls(self.data(),before);CHECK(game::Field<float>(self.data(),game::O::kPos)[0]==-2656);
 }
 StorageReset();room=2;float before[]={-2600,-1588,-3764};
 game::Field<float>(self.data(),game::O::kPos)[0]=-2700;
 ApplyPs1Walls(self.data(),before);CHECK(game::Field<float>(self.data(),game::O::kPos)[0]==-2656);
 StorageReset();float side[]={-2600,-1588,-5100};
 game::Field<float>(self.data(),game::O::kPos)[0]=-2700;
 ApplyPs1Walls(self.data(),side);CHECK(game::Field<float>(self.data(),game::O::kPos)[0]==-2656);
}
void StorageInvalidMetadataKeepsNormalQuery(){
 for(int fault:{0,1,2,3}) {
  StorageReset(-3000);
  if(fault==0)testWalls.clear();
  if(fault==1)game::storageMinZ=game::storageMaxZ+1;
  if(fault==2)game::storageMinX=game::storageMaxX+1;
#ifdef SF_TEST_STORAGE_DOOR_ACCESS
  if(fault==3)g_native0801DoorAccessReady=false;
#endif
  float d[]={-80,0,0},r[]={0,0,0},v[]={-1,0,0};
  QueryDetour(collection.data(),nullptr,0,d,r,v);CHECK((flagsSeen&2)!=0);
 }
}


// Native 0040CA40 mode 4 redirects a box contact towards the vertical response.
// 004A4870 assigns this mode to the six step boxes in physical room 07/03.
int __fastcall NativeStepQuery(void*, void*, int, float* disp, float* resp, float*) {
  flagsSeen = game::Read<uint32_t>(box.data(), game::O::kCollisionFlags);
  *game::Field<int>(self.data(), game::O::kContactIndex) = 1;
  resp[0] = resp[1] = resp[2] = 0.0f;
  if ((flagsSeen & 2) == 0) {
    disp[1] = -40.0f;
  } else {
    disp[2] = 0.0f;
  }
  return 1;
}
void NativeStepsRemainAvailableOutsideDoorEvents() {
  for (int testRoom : {3, 4}) {
    Reset(0, 0);
    stage = 7;
    room = testRoom;
    testDoors.clear();
    *game::Field<uint8_t>(box.data(), 0x111) = 4;
    g_query = NativeStepQuery;
    float d[] = {0, 0, 80}, r[] = {0, 0, 0}, v[] = {0, 0, 1};
    CHECK(QueryDetour(collection.data(), nullptr, 0, d, r, v) == 1);
    CHECK((flagsSeen & 2) == 0);
    CHECK(d[1] == -40.0f && d[2] == 80.0f);
    CHECK(game::Read<uint32_t>(box.data(), game::O::kCollisionFlags) == 0);
  }
}
void OriginallyDisabledStepsRemainDisabled() {
  Reset(0, 0);
  room = 3;
  testDoors.clear();
  *game::Field<uint8_t>(box.data(), 0x111) = 4;
  *game::Field<uint32_t>(box.data(), game::O::kCollisionFlags) = 2;
  g_query = NativeStepQuery;
  float d[] = {0, 0, 80}, r[] = {0, 0, 0}, v[] = {0, 0, 1};
  QueryDetour(collection.data(), nullptr, 0, d, r, v);
  CHECK(flagsSeen == 2);
  CHECK(d[1] == 0.0f && d[2] == 0.0f);
  CHECK(game::Read<uint32_t>(box.data(), game::O::kCollisionFlags) == 2);
}


std::vector<uint8_t> AccessRecord(int16_t type,int32_t x,int32_t z,int16_t radius) {
 std::vector<uint8_t> bytes(ps1::kEventRecordSize);
 std::memcpy(bytes.data(),&type,sizeof(type));
 std::memcpy(bytes.data()+4,&x,sizeof(x));
 std::memcpy(bytes.data()+12,&z,sizeof(z));
 std::memcpy(bytes.data()+30,&radius,sizeof(radius));
 return bytes;
}
void ScriptedAccessMetadataIsNotDiscarded() {
 std::vector<uint8_t> bytes;
 for(int16_t type:{3,4,2,1,0,-1}) {
  const auto record=AccessRecord(type,1500,-2752,600);
  bytes.insert(bytes.end(),record.begin(),record.end());
 }
 auto invalid=AccessRecord(3,0,0,0);bytes.insert(bytes.end(),invalid.begin(),invalid.end());
 bytes.push_back(3);bytes.push_back(0); // incomplete record is never accepted
 const auto events=ps1::ParseDoorEvents(bytes.data(),bytes.size());
 CHECK(events.size()==2);
 if(events.size()==2){CHECK(events[0].scripted);CHECK(!events[1].scripted);}
 CHECK(ps1::ParseDoorEvents(nullptr,100).empty());
 CHECK(ps1::ParseDoorEvents(bytes.data(),ps1::kEventRecordSize-1).empty());
}
void ScriptedApproachAndReturnUseNativePosition() {
 Reset(1500,2250);room=3;
 testWalls={{-17952,-2304,19456,-2304}};
 const auto bytes=AccessRecord(3,1500,-2752,600);
 testDoors=ps1::ParseDoorEvents(bytes.data(),bytes.size());
 auto* pos=game::Field<float>(self.data(),game::O::kPos);
 const float before[]={1500,-1588,2250};pos[2]=2330;
 ApplyPs1Walls(self.data(),before);CHECK(pos[2]==2330);
 const float returning[]={1500,-1588,2330};pos[2]=2250;
 ApplyPs1Walls(self.data(),returning);CHECK(pos[2]==2250);
 // An unrelated approach to the same wall must remain blocked.
 pos[0]=10000;pos[2]=2330;const float side[]={10000,-1588,2250};
 ApplyPs1Walls(self.data(),side);CHECK(pos[2]==2304);
 testDoors.clear();
}


void SetBoxGeometry(float top, float bottom, float centerY) {
 float* matrix=game::Field<float>(box.data(),game::O::kMatrix);
 matrix[0]=matrix[5]=matrix[10]=matrix[15]=1.0f;
 float* a=game::Field<float>(box.data(),0x20);
 float* b=game::Field<float>(box.data(),0x2c);
 a[0]=7000;a[1]=bottom;a[2]=4000;
 b[0]=-7000;b[1]=top;b[2]=-4000;
 game::Field<float>(self.data(),0xB8)[1]=centerY;
 *game::Field<uint8_t>(box.data(),0x111)=2;
}
void NativeSupportFloorsAreNotFiltered() {
 for(int testRoom:{7,17}) {
  Reset(0,0);stage=3;room=testRoom;testWalls.clear();testDoors.clear();
  g_config.ps1PlayerHeightSupport=true;SetBoxGeometry(-200,0,-360);
  g_query=NativeStepQuery;
  float d[]={-80,10,12},r[3]={},v[]={-1,0,0};
  QueryDetour(collection.data(),nullptr,0,d,r,v);
  CHECK((flagsSeen&2)==0);CHECK(d[1]==-40);CHECK(d[0]==-80 && d[2]==12);
  CHECK(game::Read<uint32_t>(box.data(),game::O::kCollisionFlags)==0);
 }
}
void SupportFilterUsesWorldHeightAndKeepsWalls() {
 for(int scenario:{0,1,2,3,4}) {
  Reset(0,0);room=7;stage=3;testDoors.clear();testWalls.clear();
  g_config.ps1PlayerHeightSupport=true;SetBoxGeometry(-200,0,-360);
  if(scenario==0)SetBoxGeometry(-2000,0,-360); // wall extends above the sphere centre
  if(scenario==1)g_config.ps1PlayerHeightSupport=false;
  if(scenario==2)game::Field<float>(box.data(),game::O::kMatrix)[13]=-1000;
  if(scenario==3)game::Field<float>(self.data(),0xB8)[1]=std::nanf("");
  if(scenario==4)*game::Field<uint32_t>(box.data(),game::O::kCollisionFlags)=2;
  float d[]={-80,10,12},r[3]={},v[]={-1,0,0};
  QueryDetour(collection.data(),nullptr,0,d,r,v);
  CHECK((flagsSeen&2)!=0);
  CHECK(game::Read<uint32_t>(box.data(),game::O::kCollisionFlags)==(scenario==4?2u:0u));
 }
}


int __fastcall OrderedContactQuery(void* c, void*, int index, float* disp, float* resp, float*) {
 ++nativeCalls;
 auto** list=game::Read<uint8_t**>(c,game::C::kObjects);
 const int count=game::Read<int>(c,game::C::kCount);
 bool hit=false;
 for(int i=0;i<count;++i) {
  if(i==index || !list[i] || (game::Read<uint32_t>(list[i],game::O::kCollisionFlags)&2))continue;
  // 00461000 passes the already-shortened displacement to the next collider.
  if(disp[0]==0 && disp[1]==0 && disp[2]==0)continue;
  const int mode=game::Read<int8_t>(list[i],game::O::kCollisionMode);
  if(mode!=2 && mode!=3)continue;
  disp[0]=disp[1]=disp[2]=0;
  resp[0]=resp[2]=0;resp[1]=mode==2?-1.0f:0.0f;
  *game::Field<int>(list[index],game::O::kContactIndex)=i;hit=true;
 }
 return hit?1:0;
}
void NativeStepResponsePrecedesCoincidentMesh() {
 for(int target:{0,1}) {
  Reset(0,0);stage=7;room=3;testDoors.clear();testWalls.clear();
  g_config.ps1PlayerHeightSupport=true;nativeCalls=0;
  uint8_t* order0[]={self.data(),mesh.data(),box.data()};
  uint8_t* order1[]={mesh.data(),self.data(),box.data()};
  auto** original=target==0?order0:order1;
  *game::Field<uint8_t**>(collection.data(),game::C::kObjects)=original;
  *game::Field<int>(collection.data(),game::C::kTarget)=target;
  *game::Field<uint8_t>(box.data(),0x111)=4;
  g_query=OrderedContactQuery;
  float d[]={-58,46,-55},r[3]={},v[]={0,1,0};
  CHECK(QueryDetour(collection.data(),nullptr,target,d,r,v)==1);
  CHECK(r[1]==-1);CHECK(nativeCalls==1);
  CHECK(game::Read<int>(self.data(),game::O::kContactIndex)==2);
  CHECK(game::Read<uint8_t**>(collection.data(),game::C::kObjects)==original);
  CHECK(original[target]==self.data() && original[2]==box.data());
 }
}
void DisabledSupportDoesNotReorderContacts() {
 Reset(0,0);stage=7;room=3;testDoors.clear();testWalls.clear();
 uint8_t* original[]={self.data(),mesh.data(),box.data()};
 *game::Field<uint8_t**>(collection.data(),game::C::kObjects)=original;
 *game::Field<uint8_t>(box.data(),0x111)=4;g_query=OrderedContactQuery;
 float d[]={-58,46,-55},r[3]={},v[]={0,1,0};
 QueryDetour(collection.data(),nullptr,0,d,r,v);CHECK(r[1]==0);
 CHECK(game::Read<int>(self.data(),game::O::kContactIndex)==1);
 CHECK(game::Read<uint8_t**>(collection.data(),game::C::kObjects)==original);
}


int __fastcall NoContactQuery(void*,void*,int,float*,float*,float*) {++nativeCalls;return 0;}
void ReorderedNoHitKeepsPreviousContact() {
 Reset(8031,-5430);g_config.ps1PlayerHeightSupport=true;
 uint8_t* original[]={self.data(),mesh.data(),box.data()};
 *game::Field<uint8_t**>(collection.data(),game::C::kObjects)=original;
 *game::Field<uint8_t>(box.data(),0x111)=4;
 *game::Field<int>(self.data(),game::O::kContactIndex)=1;
 nativeCalls=0;g_query=NoContactQuery;
 float d[]={0,0,-80},r[3]={},v[]={0,0,-1};
 CHECK(QueryDetour(collection.data(),nullptr,0,d,r,v)==0);
 CHECK(nativeCalls==1);CHECK(game::Read<int>(self.data(),game::O::kContactIndex)==1);
 CHECK(game::Read<uint8_t**>(collection.data(),game::C::kObjects)==original);
}
void DisabledStepIsNotPrioritised() {
 Reset(0,0);room=3;g_config.ps1PlayerHeightSupport=true;testDoors.clear();testWalls.clear();
 uint8_t* original[]={self.data(),mesh.data(),box.data()};
 *game::Field<uint8_t**>(collection.data(),game::C::kObjects)=original;
 *game::Field<uint8_t>(box.data(),0x111)=4;
 *game::Field<uint32_t>(box.data(),game::O::kCollisionFlags)=2;
 g_query=OrderedContactQuery;
 float d[]={-58,46,-55},r[3]={},v[]={0,1,0};
 QueryDetour(collection.data(),nullptr,0,d,r,v);
 CHECK(r[1]==0);CHECK(game::Read<int>(self.data(),game::O::kContactIndex)==1);
 CHECK(game::Read<uint32_t>(box.data(),game::O::kCollisionFlags)==2);
 CHECK(game::Read<uint8_t**>(collection.data(),game::C::kObjects)==original);
}


// Observed native scene54 arrival from56: (0,-1600,5500), radius150.
// With Forward held, the settled pose (0,-1588,5500) remains on mesh2/face197,
// normal +Z, although PS1 800785C0 -> 8007CB9C accepts the inward step.
alignas(16) std::array<uint8_t,0x120> triangleModel{},triangleParts{},triangleBone{};
const uint8_t* modelPointer=triangleModel.data();
const uint8_t* partsPointer=triangleParts.data();
const uint8_t* bonePointer=triangleBone.data();
const uint8_t* boneParent=nullptr;
unsigned partCount=1,firstTriangle=191,triangleCount=8;
void SetupTriangleModel() {
 triangleModel.fill(0);triangleParts.fill(0);triangleBone.fill(0);boneParent=nullptr;
 for(size_t off:{size_t(0x40),size_t(0xC0)})for(int k:{0,5,10,15})game::Field<float>(triangleBone.data(),off)[k]=1;
 game::readOverrides={{reinterpret_cast<uintptr_t>(mesh.data())+0x7C,&modelPointer,sizeof modelPointer},
  {reinterpret_cast<uintptr_t>(modelPointer)+0x14,&partsPointer,sizeof partsPointer},
  {reinterpret_cast<uintptr_t>(modelPointer)+0x18,&partCount,sizeof partCount},
  {reinterpret_cast<uintptr_t>(partsPointer)+8,&firstTriangle,sizeof firstTriangle},
  {reinterpret_cast<uintptr_t>(partsPointer)+12,&triangleCount,sizeof triangleCount},
  {reinterpret_cast<uintptr_t>(partsPointer)+16,&bonePointer,sizeof bonePointer},
  {reinterpret_cast<uintptr_t>(bonePointer)+0x100,&boneParent,sizeof boneParent}};
}
void __fastcall NativeTriangleRecord(void*, void*, int, float* a, float* b, float* c) {
 const float pa[3]={-5,-3000,5406},pb[3]={5,-3000,5406},pc[3]={-5,-2400,5406};
 std::memcpy(a,pa,sizeof pa);std::memcpy(b,pb,sizeof pb);std::memcpy(c,pc,sizeof pc);
}
int __fastcall DoorReturnMeshQuery(void*, void*, int, float* disp, float* resp, float*) {
 ++nativeCalls;
 if(g_readTriangle){float a[10]={},b[10]={},c[10]={};ReadTriangleDetour(mesh.data(),nullptr,197,a,b,c);}
 disp[0]=disp[1]=disp[2]=0;
 resp[0]=resp[1]=resp[2]=0;
 *game::Field<int>(self.data(),game::O::kContactIndex)=2;
 *game::Field<int>(self.data(),0x100)=197;
 float* normal=game::Field<float>(self.data(),game::O::kContactNormal);
 normal[0]=meshNormalX;normal[1]=0;normal[2]=meshNormalX==0?1.0f:0.0f;
 return 1;
}
void CorridorReturnSetup() {
 Reset(0,5500);stage=3;room=8;
 testWalls={{1504,5568,-1568,5568},{-1152,5952,-1152,-6048},
            {-1568,-5536,1504,-5536},{832,-6048,832,5984}};
 testDoors={{0,-5994,600}};
 *game::Field<int8_t>(box.data(),game::O::kCollisionMode)=0;
 *game::Field<float>(self.data(),0xC4)=150;
 SetupTriangleModel();g_query=DoorReturnMeshQuery;g_readTriangle=NativeTriangleRecord;nativeCalls=0;
}
void ReturningFromDoorMeshKeepsPs1InwardStep() {
 for(bool heightSupport:{false,true}) for(float pcZ:{5500.0f,5420.0f,5340.0f}) {
  CorridorReturnSetup();g_config.ps1PlayerHeightSupport=heightSupport;game::Field<float>(self.data(),8)[2]=pcZ;
  const ps1::WallPoint from{0,-static_cast<int>(pcZ)};ps1::WallPoint to{0,-static_cast<int>(pcZ-80)};
  CHECK(ps1::StepAgainstWalls(testWalls,from,to)==0);
  if(pcZ==5500)CHECK(NativeDoorPassage(game::Field<float>(self.data(),game::O::kPos)));
  float d[]={0,49.98f,-80},r[3]={},v[]={0,1,0};
  CHECK(QueryDetour(collection.data(),nullptr,0,d,r,v)==1);
  CHECK(nativeCalls==1);CHECK(d[0]==0 && d[2]==-80);CHECK(d[1]==0);
  CHECK(game::Read<int>(self.data(),game::O::kContactIndex)==2);
  CHECK(game::Read<int>(self.data(),0x100)==197);
 }
}
void OutgoingDoorMeshResponseStaysNative() {
 CorridorReturnSetup();float d[]={0,49.98f,80},r[3]={},v[]={0,1,0};
 QueryDetour(collection.data(),nullptr,0,d,r,v);CHECK(d[2]==0);CHECK(d[1]==0);
}
void UnmatchedDoorMeshResponseStaysNative() {
 CorridorReturnSetup();meshNormalX=1;
 float d[]={0,49.98f,-80},r[3]={},v[]={0,1,0};
 QueryDetour(collection.data(),nullptr,0,d,r,v);CHECK(d[2]==0);CHECK(d[1]==0);
}


void DoorReturnDirectionUsesOriginalWallSide() {
 struct Case {ps1::Wall wall;float x,z,nx,nz,dx,dz;};
 const Case cases[]={{{-100,-100,100,-100},0,100,0,1,0,-80},
                     {{100,-100,100,100},100,0,1,0,-80,0},
                     {{100,100,-100,100},0,-100,0,-1,0,80},
                     {{-100,100,-100,-100},-100,0,-1,0,80,0},
                     {{-100,-100,100,100},0,0,1,1,-80,-80}};
 for(const auto& c:cases){
  std::vector<ps1::Wall> walls={c.wall};
  CHECK(ps1::MeshWallEntersRoom(walls,c.x,c.z,c.nx,c.nz,c.dx,c.dz,200));
  CHECK(!ps1::MeshWallEntersRoom(walls,c.x,c.z,c.nx,c.nz,-c.dx,-c.dz,200));
  CHECK(!ps1::MeshWallEntersRoom(walls,c.x,c.z,c.nx,c.nz,0,0,200));
  CHECK(!ps1::MeshWallEntersRoom(walls,c.x,c.z,c.nx,c.nz,std::nanf(""),0,200));
 }
}

void TranslatedOrInvalidDoorTriangleStaysNative() {
 for(int scenario:{0,1,2}){
  CorridorReturnSetup();
  if(scenario==0)game::Field<float>(triangleBone.data(),0x40)[14]=1000; // real plane is not near the PS1 wall
  if(scenario==1)boneParent=bonePointer; // cyclic graph must not leave a partial transform
  if(scenario==2)g_readTriangle=nullptr;
  float d[]={0,49.98f,-80},r[3]={},v[]={0,1,0};
  QueryDetour(collection.data(),nullptr,0,d,r,v);CHECK(d[2]==0);CHECK(d[1]==0);
 }
}

int main(){TranslatedOrInvalidDoorTriangleStaysNative();DoorReturnDirectionUsesOriginalWallSide();ReturningFromDoorMeshKeepsPs1InwardStep();OutgoingDoorMeshResponseStaysNative();UnmatchedDoorMeshResponseStaysNative();ReorderedNoHitKeepsPreviousContact();DisabledStepIsNotPrioritised();NativeStepResponsePrecedesCoincidentMesh();DisabledSupportDoesNotReorderContacts();NativeSupportFloorsAreNotFiltered();SupportFilterUsesWorldHeightAndKeepsWalls();ScriptedAccessMetadataIsNotDiscarded();ScriptedApproachAndReturnUseNativePosition();NativeStepsRemainAvailableOutsideDoorEvents();OriginallyDisabledStepsRemainDisabled();StorageCrossingAndReturnKeepNativePosition();StorageNativeQueryPreservesWallResponse();StorageOutsideSpanAndUnrelatedDoorKeepWalls();StorageInvalidMetadataKeepsNormalQuery();UnverifiedMetadataKeepsOldResponse();InvalidDoorBoundsKeepNormalQuery();SideEntryKeepsWall();CrossingDoorKeepsNativeResult();ReturningFromStepKeepsNativeResult();InsideDoorUsesNativeCollision();OtherRoomStillStopsAndFilters();
 PerpendicularMeshFaceStaysNative();OutsideDoorStillStops();MissingGeometryDoesNotDisableNormalQuery();DisabledFeatureUsesNativeQuery();
 std::printf("player_wall_adapter: %d failures\n",failures);return failures?1:0;}

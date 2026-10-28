// Native Normal/Easy confirmation waits, replaced by the original TITLE.BIN curve.
#include <windows.h>
#include "../config.h"
#include "../game.h"
#include "../hooks.h"
#include "../log.h"
#include "../ps1/title_fade_logic.h"
#include "../../survivor_pc.h"
#include "features.h"
#include <intrin.h>
namespace sf::features {
namespace {
bool ready=false;
void* loadMenuWait=nullptr;void* normalWait=nullptr;void* easyWait=nullptr;
using LoadSaveFn=void(__fastcall*)(void*,void*,int);
LoadSaveFn loadSave=nullptr;
void* normalDone=reinterpret_cast<void*>(0x00585553);
void* easyDone=reinterpret_cast<void*>(0x005855BC);
void* loadMenuDone=reinterpret_cast<void*>(0x0058534A);
using EndFrameFn=int(__fastcall*)(void*,void*,int);
using BeginFrameFn=void(__fastcall*)(void*,void*,int,int,int);
using PumpFn=void(__cdecl*)();
const auto endFrame=reinterpret_cast<EndFrameFn>(game::kEndFrame);
const auto beginFrame=reinterpret_cast<BeginFrameFn>(0x00466D50);
const auto pump=reinterpret_cast<PumpFn>(0x0046FEC0);
bool ContextAlive(void* context){return context && *reinterpret_cast<void**>(game::kGameState)==context && *reinterpret_cast<void**>(0x005FD0C8);}

bool __cdecl RunTitleFade(){
  if(!ready||!g_config.ps1TitleFade)return false;
  auto* context=*reinterpret_cast<void**>(game::kGameState);
  LARGE_INTEGER frequency,start,now;
  if(!ContextAlive(context)||!QueryPerformanceFrequency(&frequency)||
     frequency.QuadPart<1000||frequency.QuadPart>1000000000||
     !QueryPerformanceCounter(&start)||!SurvivorPc_BeginTitleTransition())return false;
  Log("TitleFade: begin original PS1 flash/fade; native 4779 ms wait replaced");
  // Confirmation runs after the native title draw and before EndScene/Present.
  // Finish that scene once to capture the confirmed title. Held-frame presents
  // then have their own balanced BeginScene/EndScene; no nested native scene.
  endFrame(context,nullptr,0);
  bool ok=SurvivorPc_TitleFrameReady();unsigned presented=0;
  ps1::TitleFadeTimeline timeline;ps1::TitleFadeFrame frame;
  while(ok&&!frame.finished){
    const auto deadline=timeline.NextDeadline(frequency.QuadPart);
    do {
      pump();
      if(!ContextAlive(context)||!SurvivorPc_TitleFrameReady()||!QueryPerformanceCounter(&now)||now.QuadPart<start.QuadPart){ok=false;break;}
      if(now.QuadPart-start.QuadPart<deadline)YieldProcessor();
    }while(now.QuadPart-start.QuadPart<deadline);
    if(!ok)break;
    frame=timeline.Advance(now.QuadPart-start.QuadPart,frequency.QuadPart);
    SurvivorPc_SetTitleTransition(frame.brightness,frame.flash);
    if(!SurvivorPc_PresentHeldFrame()){ok=false;break;}
    ++presented;
  }
  SurvivorPc_EndTitleTransition();
  // The interrupted caller expects an open native scene and will finish it
  // before leaving its menu loop. Restore that contract even on capture failure.
  if(ContextAlive(context))beginFrame(context,nullptr,0,0,0);else ok=false;
  QueryPerformanceCounter(&now);
  Log("TitleFade: %s; updates=%u presents=%u elapsed_ms=%u; native loading follows",
      ok?"complete":"unavailable, native wait kept",timeline.Updates(),presented,
      static_cast<unsigned>((now.QuadPart-start.QuadPart)*1000/frequency.QuadPart));
  return ok;
}
void __fastcall LoadSaveDetour(void* saves,void* edx,int slot){
  // Only the confirmed slot path at 005858C7 should receive the title-style transition.
  if(reinterpret_cast<uintptr_t>(_ReturnAddress())==0x005858CC && g_config.ps1TitleFade){
    Log("TitleFade: confirmed Load Game slot; flash/fade before native save loader");
    RunTitleFade();
  }
  loadSave(saves,edx,slot);
}
__declspec(naked) void NormalWait(){
 __asm {
  pushfd
  pushad
  call RunTitleFade
  test al,al
  jz native_normal
  popad
  popfd
  jmp dword ptr [normalDone]
 native_normal:
  popad
  popfd
  jmp dword ptr [normalWait]
 }
}
__declspec(naked) void LoadMenuWait(){
 __asm {
  pushfd
  pushad
  call RunTitleFade
  test al,al
  jz native_load
  popad
  popfd
  jmp dword ptr [loadMenuDone]
 native_load:
  popad
  popfd
  jmp dword ptr [loadMenuWait]
 }
}
__declspec(naked) void EasyWait(){
 __asm {
  pushfd
  pushad
  call RunTitleFade
  test al,al
  jz native_easy
  popad
  popfd
  jmp dword ptr [easyDone]
 native_easy:
  popad
  popfd
  jmp dword ptr [easyWait]
 }
}
}
bool InstallTitleFade(){
 if(!g_config.ps1TitleFade)return false;
 // Verified gun_bio.exe: both waits follow the original flags, stats reset and
 // GUN.WAV call. Their done addresses pop EDI, ESI, EBX and return, with no args.
 const uint8_t wait[]={0xff,0xd7,0x8b,0xf0,0xff,0xd7,0x2b,0xc6,0x3d,0xab,0x12,0,0};
 const uint8_t done[]={0x5f,0x5e,0x5b,0xc3};
 const uint8_t begin[]={0xa1,0xc8,0xd0,0x5f,0,0x6a,0,0x68};
 const uint8_t pumpBytes[]={0x83,0xec,0x1c,0x8d,0x44,0x24,0,0x6a};
 const game::ByteSignature checks[]={
  {0x00585321,wait,sizeof(wait)},
  {0x0058552A,wait,sizeof(wait)},{0x00585593,wait,sizeof(wait)},
  {0x0058534A,done,sizeof(done)},
  {0x00585553,done,sizeof(done)},{0x005855BC,done,sizeof(done)},
  {0x00466D50,begin,sizeof(begin)}, {0x0046FEC0,pumpBytes,sizeof(pumpBytes)},
  {game::kEndFrame,game::kEndFrameBytes,sizeof(game::kEndFrameBytes)}};
 for(const auto& check:checks)if(!game::CheckBytes(check)){Log("TitleFade: unexpected native ABI; original wait kept");return false;}
 ready=Hook("TitleLoadMenuWait",0x00585321,reinterpret_cast<void*>(LoadMenuWait),&loadMenuWait)&&
       Hook("TitleNormalWait",0x0058552A,reinterpret_cast<void*>(NormalWait),&normalWait)&&
       Hook("TitleEasyWait",0x00585593,reinterpret_cast<void*>(EasyWait),&easyWait)&&
       Hook("TitleLoadSave",0x00464940,reinterpret_cast<void*>(LoadSaveDetour),reinterpret_cast<void**>(&loadSave));
 Log("TitleFade: candidate=%d; original title curve for New Game, Load Game selection and confirmed slots",ready);
 return ready;
}
}

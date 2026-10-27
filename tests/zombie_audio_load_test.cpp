// The production native-load detour must retain stock playback on a failed replacement.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#ifndef _MSC_VER
#define __fastcall
#endif
using LoadFn=bool(__fastcall*)(void*,void*,const char*,char,int,char);
std::vector<std::string> calls;
bool __fastcall NativeLoad(void*,void*,const char* path,char,int,char){calls.push_back(path);return std::string(path)=="stock.wav";}
LoadFn nativeLoad=NativeLoad;
bool ready=true,voiceSamplesReady=true;
thread_local const char* replacement=nullptr;
enum class ReplacementKind {None,Walk,DeathVoice};
thread_local ReplacementKind replacementKind=ReplacementKind::None;
unsigned emitted=0;
template<class...T>void Log(const char*,T...){}
#include "zombie_audio_load_test.inc"
void need(bool b,const char* m){if(!b){std::fprintf(stderr,"FAIL: %s\n",m);std::exit(1);}}
int main(){
 replacement="missing-voice.wav";replacementKind=ReplacementKind::DeathVoice;
 need(Load(nullptr,nullptr,"stock.wav",0,0,0),"failed voice replacement retries the original sample");
 need(calls==std::vector<std::string>{"missing-voice.wav","stock.wav"},"replacement and native fallback order");
 need(!voiceSamplesReady&&ready,"failed voice does not disable walking");
 calls.clear();replacement="missing-walk.wav";replacementKind=ReplacementKind::Walk;voiceSamplesReady=true;
 need(Load(nullptr,nullptr,"stock.wav",0,0,0),"failed walk replacement retains native fallback");
 need(!ready&&voiceSamplesReady,"failed walk does not disable voice banks");
 calls.clear();replacement=nullptr;replacementKind=ReplacementKind::None;
 need(Load(nullptr,nullptr,"stock.wav",0,0,0)&&calls.size()==1,"other native loads unchanged");
 std::puts("PASS: independent audio replacement fallback");
}

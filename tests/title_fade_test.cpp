#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "../source/survivor_fix/ps1/title_fade_logic.h"
void require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){
 for(unsigned parity:{0u,1u}){
  ps1::TitleFade fade(parity);ps1::TitleFadeFrame frame;
  for(int i=0;i<24;++i){frame=fade.Step();require(frame.brightness==128,"brightness held through PS1 confirm flash");if(i<3)require(frame.flash==255,"original opening flash saturates white");if(i==3)require(frame.flash==240,"PS1 flash decay uses integer division by 18");if(i>=20)require(frame.flash==0,"last four flash updates draw no flash quad");}
  unsigned steps=24;int previous=128;
  do{frame=fade.Step();++steps;require(frame.flash==0,"flash does not restart");require(frame.brightness==previous||frame.brightness==previous-1,"one brightness unit at most per title tick");previous=frame.brightness;require(steps<=280,"PS1 fade completes without the PC wait");}while(!frame.finished);
  require(frame.brightness==0&&steps==279+parity,"exact PS1 completion for either initial parity");
  require(fade.Step().finished,"completed transition stays complete");
 }
 require(ps1::TitleShadeColor(0)==0&&ps1::TitleShadeColor(64)==128&&ps1::TitleShadeColor(128)==255,"brightness multiplies colour instead of subtracting a constant");
 require(ps1::TitleShadeColor(-1)==0&&ps1::TitleShadeColor(129)==255,"render factor bounds");
 require(ps1::TitleVBlankTicks(1,53693175)==897619,"one title step is one hardware VBlank, not a gameplay tick");
 require(ps1::TitleVBlankTicks(280,53693175)==280ll*897619,"duration follows original display timing");
 require(ps1::TitleVBlankTicks(0,1000000)==0&&ps1::TitleVBlankTicks(281,1000000)==0,"bounded transition deadlines");
 for(unsigned fps:{30u,60u,144u}){
  constexpr int64_t frequency=53693175;
  const int64_t end=ps1::TitleVBlankTicks(279,frequency);
  ps1::TitleFadeTimeline timeline;
  require(!timeline.Advance(0,frequency).finished&&timeline.Updates()==0,"no title update before its first VBlank");
  for(int64_t elapsed=0;elapsed<end;elapsed+=frequency/fps)require(!timeline.Advance(elapsed,frequency).finished,"render rate cannot finish the curve early");
  require(timeline.Advance(end,frequency).finished&&timeline.Updates()==279,"original duration is independent of presentation rate");
  require(timeline.Advance(end+frequency,frequency).finished&&timeline.Updates()==279,"late presentation cannot restart the sequence");
 }
 std::puts("PASS: original title flash and fade");
}

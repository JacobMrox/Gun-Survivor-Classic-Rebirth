// TITLE.BIN 8006FA64 (confirm), 8006C9D8 (flash), 8006F180 (leave).
#pragma once
#include <algorithm>
#include <cstdint>
namespace ps1 {
struct TitleFadeFrame { int brightness=128; int flash=0; bool finished=false; };
class TitleFade {
 public:
  explicit TitleFade(unsigned parity=0): tick_(parity&1) {}
  TitleFadeFrame Step(){
    if(done_)return {0,0,true};
    // Update precedes drawing. Flash holds the title brightness; afterward only
    // even title updates subtract one. Title sprites multiply by brightness/128.
    if(flash_==0 && (tick_&1)==0)brightness_=(std::max)(0,brightness_-1);
    int white=0;
    if(flash_!=0){if(flash_-1>3)white=(std::clamp)((flash_-4)*255/18,0,255);--flash_;}
    ++tick_;done_=brightness_==0;
    return {brightness_,white,done_};
  }
 private:
  unsigned tick_=0;int brightness_=128,flash_=24;bool done_=false;
};
// Nearest 8-bit blend factor: fixed-function rendering differs from the PS1
// integer texel*brightness>>7 by at most one output colour unit.
inline int TitleShadeColor(int brightness){return ((std::clamp)(brightness,0,128)*255+64)/128;}
inline int64_t TitleVBlankTicks(unsigned n,int64_t frequency){
  if(!n||n>280||frequency<1000||frequency>1000000000)return 0;
  constexpr uint64_t gpu=53693175,clocks=3413*263;
  return static_cast<int64_t>((uint64_t(n)*clocks*uint64_t(frequency)+gpu-1)/gpu);
}

class TitleFadeTimeline {
 public:
  TitleFadeFrame Advance(int64_t elapsed,int64_t frequency){
    if(elapsed<0)return frame_;
    while(!frame_.finished){
      const auto deadline=NextDeadline(frequency);
      if(!deadline||elapsed<deadline)break;
      frame_=curve_.Step();++updates_;
    }
    return frame_;
  }
  int64_t NextDeadline(int64_t frequency)const{return frame_.finished?0:TitleVBlankTicks(updates_+1,frequency);}
  unsigned Updates()const{return updates_;}
 private:
  TitleFade curve_;TitleFadeFrame frame_;unsigned updates_=0;
};
}

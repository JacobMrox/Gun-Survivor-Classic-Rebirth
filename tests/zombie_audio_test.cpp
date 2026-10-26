#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include "../source/survivor_fix/ps1/zombie_audio_logic.h"
void require(bool ok,const char* message) { if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);} }
int main(int argc,char** argv){
 require(ps1::ZombieLocomotionCue(32,1,4,2,true)==9,"held walk emits PS1 cue 9 without a selector change");
 require(ps1::ZombieLocomotionCue(64,1,4,2,true)==10,"straight walk selects alternate PS1 sound");
 require(ps1::ZombieLocomotionCue(64,1,4,2,false)==9,"rotation selects PS1 cue 9");
 for(unsigned tick=1;tick<128;++tick)if(tick%32)require(ps1::ZombieLocomotionCue(tick,1,4,2,true)==-1,"no extra sound between logic gates");
 for(int lane:{0,2,3})require(ps1::ZombieLocomotionCue(64,lane,4,2,true)==-1,"other PS1 update lanes stay silent");
 for(int state:{0,10})require(ps1::ZombieLocomotionCue(64,1,state,2,true)==-1,"excluded movement states stay silent");
 for(int clip:{10,11,14,15,16,17})require(ps1::ZombieLocomotionCue(64,1,4,clip,true)==-1,"original animation exclusions");
 require(ps1::ZombieLocomotionCue(64,1,5,27,true)==10,"do not invent additional state exclusions");
 uint8_t block[16]={12,1,0x71,0x8f};std::vector<int16_t> pcm;
 require(ps1::DecodeSpu(block,sizeof block,pcm),"decode one complete SPU block");
 require(pcm.size()==28&&pcm[0]==1&&pcm[1]==7&&pcm[2]==-1&&pcm[3]==-8,"signed SPU nibbles in playback order");
 block[0]=0x5c;require(!ps1::DecodeSpu(block,sizeof block,pcm)&&pcm.empty(),"invalid SPU filter is rejected atomically");
 block[0]=12;require(!ps1::DecodeSpu(block,15,pcm),"truncated SPU block is rejected");
 const auto wav=ps1::MonoWave({1,-8});
 require(wav.size()==48&&wav[24]==0x11&&wav[25]==0x2b&&wav[40]==4&&wav[44]==1&&wav[46]==0xf8&&wav[47]==0xff,"PCM WAV header and signed samples");
 std::array<std::vector<int16_t>,2> clips;
 require(!ps1::SharedLocomotionClips(nullptr,0,clips),"no asset means no replacement");
 if(argc>1){
   std::ifstream input(argv[1],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
   require(ps1::SharedLocomotionClips(bytes.data(),bytes.size(),clips),"existing PC file contains the verified original bank");
   require(clips[0].size()==2380&&clips[1].size()==4732,"original SPU end markers and sample lengths");
   if(argc>2)for(unsigned i=0;i<2;++i){const auto wave=ps1::MonoWave(clips[i]);std::ofstream out(std::string(argv[2])+"-"+std::to_string(i)+".wav",std::ios::binary);out.write(reinterpret_cast<const char*>(wave.data()),wave.size());require(out.good(),"save private decoded evidence");}
   for(size_t i=12;i<bytes.size();i+=2048)bytes[i]^=1;
   require(!ps1::SharedLocomotionClips(bytes.data(),bytes.size(),clips),"altered bank is rejected instead of guessing samples");
 }
 std::puts("PASS: PS1 locomotion audio and SPU decoding");
}

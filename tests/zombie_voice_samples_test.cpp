#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <vector>
#include "../source/survivor_fix/ps1/zombie_voice_samples.h"
void need(bool b,const char* m){if(!b){std::fprintf(stderr,"FAIL: %s\n",m);std::exit(1);}}
int main(int argc,char** argv){
 std::array<std::vector<int16_t>,9> clips;
 need(!ps1::DecodeZombieDeathVoices(nullptr,0,clips),"missing voice data retains native audio");
 std::vector<uint8_t> shortData(2048);need(!ps1::DecodeZombieDeathVoices(shortData.data(),shortData.size(),clips),"truncated bank rejected");
 if(argc>1){
  std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});need(!bytes.empty(),"optional existing PC input available");
  need(ps1::DecodeZombieDeathVoices(bytes.data(),bytes.size(),clips),"all nine original banks found unchanged in existing PC data");
  const size_t counts[]={21812,15512,26880,16240,26908,17024,28616,23408,23408};
  for(size_t i=0;i<9;++i)need(clips[i].size()==counts[i],"original death sample length and end marker");
  std::set<std::vector<int16_t>> unique(clips.begin(),clips.end());need(unique.size()==8&&clips[7]==clips[8],"eight distinct recordings, original duplicate retained");
  for(size_t i=12;i<bytes.size();i+=2048)bytes[i]^=1;
  need(!ps1::DecodeZombieDeathVoices(bytes.data(),bytes.size(),clips),"changed original banks rejected");
  for(const auto& c:clips)need(c.empty(),"failure does not leak partially decoded selection");
 }
 std::puts("PASS: original zombie death sample validation");
}

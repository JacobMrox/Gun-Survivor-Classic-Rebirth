#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <fstream>
#include <iterator>
#include "../source/survivor_fix/roomenv_index.h"
#include <vector>
#include "../source/survivor_fix/ps1/zombie_voice_logic.h"
void need(bool b,const char* m){if(!b){std::fprintf(stderr,"FAIL: %s\n",m);std::exit(1);}}
void put(std::vector<uint8_t>& b,uint32_t v,int n){for(int i=0;i<n;++i)b.push_back(uint8_t(v>>(8*i)));}
void actor(std::vector<uint8_t>& b,int kind,int x,int z){put(b,kind,2);put(b,0,2);put(b,x,4);put(b,0,4);put(b,z,4);put(b,0,4);put(b,0,4);}
int main(int argc,char** argv){
 std::array<int,9> histogram{};
 for(int byte=0;byte<256;++byte){int v=ps1::VoiceSelection(15,byte);need(v>=0&&v<9,"weighted voice selector is bounded");++histogram[v];}
 need(histogram==std::array<int,9>{29,28,28,28,28,28,28,28,31},"PS1 inclusive weighted byte boundaries, including ninth fall-through");
 for(int i=0;i<9;++i)for(int b=0;b<256;++b)need(ps1::VoiceSelection(i,b)==i,"explicit room voice does not depend on random byte");
 need(ps1::VoiceSelection(9,0)==-1,"unverified voice index rejected");
 ps1::ZombieVoiceState state;int draws=0;auto draw=[&](){return draws++==0?0:255;};
 state.BeginRoom();need(state.BindResource(11)==1,"first resource uses PS1 slot1");need(state.BindResource(0)==0,"second resource uses slot0");
 need(state.LoadVoices(0,0xFF,draw),"random paired voices loaded");need(draws==2,"one draw per F nibble, at load only");
 need(state.Voice(0,0)==0&&state.Voice(0,1)==8&&state.Voice(0,2)==0,"actor ordinal parity selects paired bank");
 for(int type:{1,2,4,5,6,8,9,10}){need(state.BindResource(type)==0,"all zombie models share original code resource");need(state.LoadVoices(0,0xFF,draw),"loaded banks reused");}
 need(draws==2,"no extra random calls for model, repeated update or death");
 state.FinishRoom();state.BeginRoom();need(state.BindResource(1)==0,"resource key survives a room transition");
 need(state.LoadVoices(0,0,draw)&&state.Voice(0,1)==0,"room unload drops banks and reloads fixed control");need(draws==2,"fixed nibble consumes no RNG");state.FinishRoom();
 state.BeginRoom();need(state.BindResource(12)==1,"unused resource key is released");need(state.BindResource(17)==0,"PS1 resource17 aliases16");need(state.BindResource(16)==0,"alias reuses its slot");need(state.BindResource(30)==-1,"third live code resource does not evict an active slot");
 need(state.Voice(-1,0)==-1&&state.Voice(0,-1)==-1,"unknown actor binding stays unavailable");
 std::vector<uint8_t> raw;put(raw,0,4);actor(raw,0,100,200);actor(raw,50,0,0);actor(raw,1,300,400);
 put(raw,0xFFFFFFFF,4);actor(raw,0,500,600);actor(raw,1,700,800);put(raw,0xFFFFFFFF,4);
 auto profiles=ps1::ParseVoiceProfiles(raw.data(),raw.size());need(profiles.size()==2,"original profile groups and terminal markers");
 std::vector<ps1::VoiceSpawn> native{{1,0,100,-200},{2,1,300,-400}};
 auto plan=ps1::MatchVoiceProfile(profiles,native);need(plan.valid&&plan.control==0&&plan.ordinals==std::vector<int>{0,2},"exact native X/-Z match retains original actor ordinals including non-enemies");
 native={{1,0,500,-600},{2,1,700,-800}};plan=ps1::MatchVoiceProfile(profiles,native);need(plan.valid&&plan.control==255,"different exact profile keeps original room voice control");
 native={{1,0,111,-222},{2,1,333,-444}};need(!ps1::MatchVoiceProfile(profiles,native).valid,"ambiguous differing control/parity is not guessed");
 native={{1,2,0,0}};need(!ps1::MatchVoiceProfile(profiles,native).valid,"mismatched spawn sequence is not assigned");
 profiles.resize(1);native={{1,0,111,-222},{2,1,333,-444}};need(ps1::MatchVoiceProfile(profiles,native).valid,"unique complete construction sequence identifies profile even if PC spawn positions differ");
 native[0].x=std::numeric_limits<float>::quiet_NaN();need(!ps1::MatchVoiceProfile(profiles,native).valid,"invalid native coordinates cannot identify an encounter");
 for(size_t n=1;n<28;++n)need(ps1::ParseVoiceProfiles(raw.data(),n).empty(),"truncated group rejected atomically");
 need(ps1::ParseVoiceProfiles(nullptr,40).empty(),"missing encounter data rejected");
 if(argc>1){
  std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
  need(!bytes.empty(),"optional original encounter file available");
  size_t rooms=0,groups=0,actors=0;
  for(const auto& room:game::roomenv::kRooms){
   need(room.offset+28<=bytes.size(),"room header in bounds");
   const auto* record=bytes.data()+room.offset;
   const size_t begin=ps1::VoiceRead32(record+8),end=ps1::VoiceRead32(record+12);
   need(begin<=end&&end<=bytes.size()-room.offset,"encounter section in bounds");
   auto profiles=ps1::ParseVoiceProfiles(record+begin,end-begin);++rooms;groups+=profiles.size();
   for(const auto& p:profiles){
    need(p.control==0||p.control==255,"verified original fixed or F/F room voice control");actors+=p.actors.size();
    std::vector<ps1::VoiceSpawn> native;
    for(const auto& a:p.actors)if(a.kind<50)native.push_back({uintptr_t(a.ordinal+1),ps1::VoiceKind(a.kind),float(a.x),-float(a.z)});
    const auto match=ps1::MatchVoiceProfile(profiles,native);
    if(match.valid)need(match.control==p.control,"recognised original profile retains its voice control");
   }
  }
  need(rooms==102&&groups==140&&actors==378,"all original encounter profiles parsed independently");
 }
 std::puts("PASS: PS1 voice bank assignment, RNG consumption and profile recognition");
}

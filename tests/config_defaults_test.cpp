#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include "../source/survivor_fix/config.h"

namespace {
int failures=0;
struct Option {const char* key;bool sf::Config::* field;const char* group;};
const Option options[]={
 {"Ps1ZombieWalkAudio",&sf::Config::ps1ZombieWalkAudio,"Ps1Zombies"},
 {"Ps1ZombieDeathAudio",&sf::Config::ps1ZombieDeathAudio,"Ps1Zombies"},
 {"Ps1ZombieVoiceBanks",&sf::Config::ps1ZombieVoiceBanks,"Ps1Zombies"},
 {"Ps1ZombieHeightSupport",&sf::Config::ps1ZombieHeightSupport,"Ps1ZombieChase"},
 {"Ps1ScriptedAccess",&sf::Config::ps1ScriptedAccess,"Ps1PlayerMovement"},
 {"Ps1PlayerHeightSupport",&sf::Config::ps1PlayerHeightSupport,"Ps1PlayerMovement"},
 {"Ps1TitleFade",&sf::Config::ps1TitleFade,nullptr},
};
void check(bool ok,const std::string& message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message.c_str());++failures;}}
struct Ini {
 char path[MAX_PATH]{};
 Ini(){char directory[MAX_PATH]{};if(!GetTempPathA(MAX_PATH,directory)||!GetTempFileNameA(directory,"sfc",0,path))std::exit(2);}
 ~Ini(){WritePrivateProfileStringA(nullptr,nullptr,nullptr,path);DeleteFileA(path);}
 void load(const std::string& contents){
  {std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<contents;if(!file.good())std::exit(2);}
  WritePrivateProfileStringA(nullptr,nullptr,nullptr,path);
  sf::LoadConfig(path);
 }
};
void enabled(const sf::Config& config,const char* context){
 for(const auto& option:options)check(config.*(option.field),std::string(context)+": "+option.key+" enabled");
 check(config.ps1ZombieWalk&&config.ps1ZombieWorld&&config.ps1ZombieNavigation&&config.ps1ZombieDeath&&
       config.ps1PlayerWalls&&config.ps1DoorFade&&config.ps1Cadence,"existing prerequisite fixes stay enabled");
 check(!config.ps1ZombieBackFall,"unsupported backward-fall trigger stays disabled");
}
}
int main(){
 enabled(sf::Config{},"initial defaults");
 Ini ini;
 ini.load("");enabled(sf::g_config,"empty configuration");
 ini.load("[Graphics]\nFPSLimit=60\n[Fixes]\nSkipMovies=0\n[SurvivorFix]\nDpiAware=1\nHudAspect=1\n");
 enabled(sf::g_config,"existing configuration without new keys");
 std::string disabled="[SurvivorFix]\n";
 for(const auto& option:options)disabled+=std::string(option.key)+"=0\n";
 ini.load(disabled);
 for(const auto& option:options)check(!(sf::g_config.*(option.field)),std::string("explicit opt-out: ")+option.key);
 for(const auto* group:{"Ps1Zombies","Ps1ZombieChase","Ps1PlayerMovement"}){
  ini.load(std::string("[SurvivorFix]\n")+group+"=0\n");
  for(const auto& option:options){const bool expected=!option.group||std::string(option.group)!=group;
   check((sf::g_config.*(option.field))==expected,std::string("group inheritance: ")+group+" / "+option.key);}
 }
 for(const auto& option:options)if(option.group){
  ini.load(std::string("[SurvivorFix]\n")+option.group+"=0\n"+option.key+"=1\n");
  check(sf::g_config.*(option.field),std::string("explicit per-fix override: ")+option.key);
 }
 ini.load("[SurvivorFix]\n");enabled(sf::g_config,"reload restores defaults");
 if(failures)return 1;
 std::puts("PASS: production INI defaults, existing configurations and diagnostic overrides");
}

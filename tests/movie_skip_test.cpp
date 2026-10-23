#include <cstdio>
#include "../source/survivor_fix/movie_skip_logic.h"
int main() {
 int fails=0;
 #define CHECK(x) do { if(!(x)) {std::printf("FAIL line %d: %s\n",__LINE__,#x);++fails;} } while(0)
 sf::MovieSkipGate gate;
 constexpr unsigned enter=1,space=2;
 gate.Begin(0);
 CHECK(!gate.Poll(false,true,0,false));
 CHECK(gate.Poll(true,false,0,false));
 gate.Begin(enter); // The title confirmation must not skip the next movie while held.
 CHECK(!gate.Poll(false,true,enter,false));
 CHECK(!gate.Poll(false,true,0,false));
 CHECK(gate.Poll(false,true,enter,false));
 CHECK(!gate.Poll(false,true,enter,false));
 CHECK(gate.Poll(false,true,enter|space,false));
 gate.Begin(0);
 CHECK(!gate.Poll(false,false,space,false));
 CHECK(!gate.Poll(false,true,space,false)); // A background press stays consumed.
 CHECK(!gate.Poll(false,true,0,false));
 CHECK(gate.Poll(false,true,space,false));
 CHECK(gate.Poll(false,false,0,true)); // Natural completion works without focus.
 gate.Begin(space); // Every movie gets fresh input state, even when graph addresses are reused.
 CHECK(!gate.Poll(false,true,space,false));
 CHECK(!gate.Poll(false,true,0,false));
 CHECK(gate.Poll(false,true,space,false));
 return fails?1:0;
}

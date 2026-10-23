#include <cstdio>
#include <vector>
#include "../source/survivor_fix/mouse_drain_logic.h"
int main() {
 int fails=0;
 #define CHECK(x) do {if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);++fails;}}while(0)
 unsigned left=80,consumed=0;std::vector<unsigned> order;
 auto read=[&] {unsigned n=left<16?left:16;for(unsigned i=0;i<n;++i)order.push_back(consumed++);left-=n;return sf::MouseBatch{1,n};};
 const auto drained=sf::DrainMouse(read);CHECK(left==0);CHECK(drained.reads==6);CHECK(drained.result==1);CHECK(order.size()==80);
 for(unsigned i=0;i<order.size();++i)CHECK(order[i]==i);
 unsigned calls=0;auto empty=sf::DrainMouse([&]{++calls;return sf::MouseBatch{1,0};});CHECK(calls==1&&empty.reads==1);
 calls=0;auto failed=sf::DrainMouse([&]{++calls;return sf::MouseBatch{0,16};});CHECK(calls==1&&failed.result==0);
 calls=0;auto bounded=sf::DrainMouse([&]{++calls;return sf::MouseBatch{1,16};});CHECK(calls==64&&bounded.reads==64);
 calls=0;auto retry=sf::DrainMouse([&]{++calls;return sf::MouseBatch{calls==1?1:0,16};});CHECK(calls==2&&retry.result==1);
 return fails?1:0;
}

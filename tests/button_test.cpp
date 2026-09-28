#include <cassert>
#include <cstdio>
#include <vector>
#include "../firmware/thermal_v2/button_gestures.h"
void test(int clicks,unsigned expected,uint32_t offset=0){
 ButtonGestures g;std::vector<unsigned> events;
 for(uint32_t t=0;t<1500;++t){
   bool down=false;for(int n=0;n<clicks;++n)if(t>=50+n*160&&t<130+n*160)down=true;
   unsigned e=g.update(down,t+offset);if(e)events.push_back(e);
 }
 assert(events.size()==1&&events[0]==expected);
}
int main(){
 test(1,1);test(2,2);test(3,3);test(2,2,0xffffff80);
 ButtonGestures bounce;for(unsigned t=0;t<800;++t)assert(!bounce.update(t<100&&t%10<5,t));
 ButtonGestures hold;for(unsigned t=0;t<2000;++t)assert(!hold.update(t>=50&&t<1200,t));
 puts("PASS: single/double/triple, bounce, long press, timestamp wrap");
}

#include "../firmware/thermal_v2/thermal_layout.h"
#include "../firmware/thermal_v2/ui_logic.h"
#include <cassert>
#include <cstdio>
int main(){
 using namespace ThermalLayout;
 for(int i=0;i<1024;++i)assert(sample(xOf(i),yOf(i))==i);
 for(int y=0;y<heat;++y)for(int x=0;x<heat;++x){assert(sample(x,y)>=0&&sample(x,y)<1024);Rect occupied[3];for(int i=0;i<3;++i){occupied[i]=place(x,y,i?74:60,17,occupied,i);const auto r=occupied[i];assert(r.x>=0&&r.y>=0&&r.x+r.w<=heat&&r.y+r.h<=heat);for(int j=0;j<i;++j)assert(!overlaps(r,occupied[j]));}}
 assert(batteryAppearance(80).color==0x4DE38A);assert(batteryAppearance(79).color==0x51B4FF);assert(batteryAppearance(40).color==0x51B4FF);assert(batteryAppearance(39).color==0xFFE066);assert(batteryAppearance(20).color==0xFFE066);assert(!batteryAppearance(19).blink);assert(!batteryAppearance(6).blink);assert(batteryAppearance(5).blink&&batteryAppearance(0).blink);
 puts("PASS: 1024 point round trips; all pixel positions with 3 coincident labels; battery boundaries");
}

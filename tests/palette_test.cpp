#include <cassert>
#include <cstdio>
#include "../firmware/thermal_v2/palette_toggle.h"
int main(){PaletteToggle p;for(int mode=1;mode<7;++mode){int next=p.toggle(mode);assert(next==0);assert(p.toggle(next)==mode);}assert(p.select(3,0)==0);assert(p.toggle(0)==3);assert(p.select(0,1)==1);assert(p.toggle(1)==0);assert(p.toggle(0)==1);puts("PASS: triple-click palette roundtrip and manual selection");}

#include <cassert>
#include <cstdio>
#include "../firmware/thermal_v2/extrema_tracker.h"
int main(){
 float data[1024];ExtremaTracker high(true),low(false);
 for(int frame=0;frame<1024;++frame){for(float &v:data)v=25;data[frame]=40;data[(frame+512)%1024]=10;high.update(data,.128f);low.update(data,.128f);assert(high.index()==frame);assert(low.index()==(frame+512)%1024);}
 for(float &v:data)v=25;data[0]=30;high.update(data,.128f);data[1000]=30.1f;high.update(data,.128f);assert(high.index()==1000);
 puts("PASS: every-frame hot/cold relocation across all 1024 pixels; 0.1C new peak accepted immediately");
}

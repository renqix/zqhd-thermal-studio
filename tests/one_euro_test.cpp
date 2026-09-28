#include <cassert>
#include <cstdio>
#include <cmath>
#include <random>
#include "../firmware/thermal_v2/one_euro_pixel.h"
extern "C" {
#include "reference/SF1eFilter.h"
}
int main(){
 SF1eFilter ref;ref.config={7.5f,.45f,.6f,1.f};SF1eFilterInit(&ref);
 float value=20,derivative=0,maxError=0;
 std::mt19937 random(13);std::normal_distribution<float> noise(0,.25);
 for(int i=0;i<10000;++i){
   float input=i?20+10*std::sin(i*.021f)+noise(random):20;
   float hz=5.f+(i%40)*.1f;ref.frequency=hz;ref.config.frequency=hz;
   float expected=SF1eFilterDo(&ref,input);
   float actual=i?OneEuroPixel::step(input,value,derivative,hz,OneEuroPixel::alpha(1,hz)):value;
   maxError=std::fmax(maxError,std::fabs(actual-expected));
   assert(std::fabs(actual-expected)<.0001f);
 }
 printf("PASS: upstream C equivalence 10000 samples, max_error=%.8f C\n",maxError);
}

#include <cassert>
#include <cstdio>
#include <random>
#include "../firmware/thermal_v2/thermal_processing.h"
int main(){
 MappingLimits m; assert(!m.setLower(41));assert(!m.setUpper(19));assert(m.setLower(-30));assert(m.setUpper(300));assert(!m.setLower(-31));assert(!m.setUpper(301));
 assert(m.normalize(-50,-40,300)==0);assert(m.normalize(301,-40,300)==1);assert(m.normalize(20,20,20)==.5f);
 float input[1024], output[1024]; ThermalResampler r;
 for(float &x:input)x=20;
 {int count=0;r.renderNearest(input,0,40,[&](int,int,uint16_t v){assert(v==512);++count;});assert(count==236*236);}
 for(int i=0;i<1024;++i)input[i]=i/32;
 r.renderNearest(input,0,31,[&](int,int y,uint16_t v){assert(v<=1023);if(y==0)assert(v==1023);if(y==235)assert(v==0);});
 for(float &x:input)x=0;input[12*32+16]=100;
 r.renderNearest(input,0,100,[&](int,int,uint16_t v){assert(v<=1023);});
 // 完整四角映射，不裁切；原始32x32矩阵等比映射为236x236。
 for(int i=0;i<1024;++i)input[i]=i;
 r.renderNearest(input,0,1023,[&](int x,int y,uint16_t v){
   assert(x>=0&&x<236&&y>=0&&y<236);
   if(x==0&&y==0)assert(v==992);
   if(x==235&&y==0)assert(v==1023);
   if(x==0&&y==235)assert(v==0);
   if(x==235&&y==235)assert(v==31);
 });
 DisplayFilter f;for(float &x:input)x=20;f.apply(input,output,1);for(float x:output)assert(x==20);
 for(float &x:input)x=50;f.apply(input,output,1);for(float x:output)assert(x>20 && x<50);
 for(int frame=0;frame<20;++frame)f.apply(input,output,1);for(float x:output)assert(x>49 && x<=50);
 for(float &x:input)x=0;f.apply(input,output,0);for(float x:output)assert(x==0);
 std::mt19937 rng(42);std::normal_distribution<float> noise(0,.25);double raw=0,filtered=0;DisplayFilter smooth;
 for(int frame=0;frame<100;++frame){for(float &x:input)x=30+noise(rng);smooth.apply(input,output,1);if(frame>10)for(int i=0;i<1024;++i){raw+=(input[i]-30)*(input[i]-30);filtered+=(output[i]-30)*(output[i]-30);}}
 assert(filtered<raw);printf("PASS: limits, fixed mapping, flat field, orientation, bounded interpolation, step response, synthetic noise (variance ratio %.3f)\n",filtered/raw);
}

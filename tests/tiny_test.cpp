#include <cassert>
#include <cstdio>
#include <random>
#include "../firmware/thermal_v2/tiny_student.h"
int main(){
 uint8_t input[1024];uint16_t output[4096];
 for(int level=0;level<256;++level){for(auto &x:input)x=level;TinyStudent::infer(input,output);for(auto x:output)assert(x==(level*4096+127)/255);}
 std::mt19937 rng(42);for(auto &x:input)x=rng()%256;TinyStudent::infer(input,output);
 for(int y=0;y<32;++y)for(int x=0;x<32;++x){int lo=255,hi=0;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){int v=TinyStudent::sample(input,x+dx,y+dy);if(v<lo)lo=v;if(v>hi)hi=v;}
 for(int yy=0;yy<2;++yy)for(int xx=0;xx<2;++xx){int v=output[(2*y+yy)*64+2*x+xx];assert(v>=(lo*4096+127)/255&&v<=(hi*4096+127)/255);}}
 TinyThermalResampler r;float raw[1024];for(int i=0;i<1024;++i)raw[i]=i%32;int count=0;r.render(raw,0,31,[&](int x,int y,uint16_t v){assert(x>=0&&x<236&&y>=0&&y<236&&v<=1023);++count;});assert(count==236*236);
 puts("PASS: Tiny constants, neighborhood bounds and rendering");
}

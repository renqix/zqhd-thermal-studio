#include <cstdio>
#include <cassert>
#include <random>
#include <cstring>
#include "../firmware/thermal_v2/tiny_student.h"
int main(int argc,char**argv){
 TinyThermalResampler r;float p[1024];uint8_t mask[236*236],previous[236*236];
 if(argc==2){FILE*out=fopen(argv[1],"wb");assert(out);while(fread(p,4,1024,stdin)==1024){memset(mask,0,sizeof(mask));r.prepareContours(p);r.overlay([&](int x,int y){assert(x>=0&&x<236&&y>=0&&y<236);mask[y*236+x]=255;});fwrite(mask,1,sizeof(mask),out);}fclose(out);return 0;}
 for(auto&v:p)v=25;r.prepareContours(p);int n=0;r.overlay([&](int x,int y){++n;});assert(n==0);
 std::mt19937 rng(1140);
 for(int trial=0;trial<80;++trial){
  for(int y=0;y<32;++y)for(int x=0;x<32;++x)p[y*32+x]=trial<40?25+(x<16?0:5)+(int(rng()%21)-10)*.01f:-40+(rng()%34000)*.01f;
  uint16_t native[1024],reference[4096];
  for(int y=0;y<32;++y)for(int x=0;x<32;++x)native[y*32+x]=std::lround((p[(31-y)*32+x]+40)*16);
  auto sample=[&](int x,int y){return native[TinyStudent::bound(y,0,31)*32+TinyStudent::bound(x,0,31)];};
  for(int y=0;y<64;++y)for(int x=0;x<64;++x){int ix=(x-1)/2,iy=(y-1)/2,fx=x&1?1:3,fy=y&1?1:3;if(x==0){ix=0;fx=0;}if(y==0){iy=0;fy=0;}reference[y*64+x]=(sample(ix,iy)*(4-fx)*(4-fy)+sample(ix+1,iy)*fx*(4-fy)+sample(ix,iy+1)*(4-fx)*fy+sample(ix+1,iy+1)*fx*fy+8)/16;}
  DetailContour independent;independent.detect(reference,DetailContour::temperatureNoise(native));memset(previous,0,sizeof(previous));independent.overlay([&](int x,int y){previous[y*236+x]=255;});
  int j=0;for(const auto range: {std::pair<float,float>(20,40),{24,26},{-30,300},{25,25}}){
   r.render(p,range.first,range.second,[](int,int,uint16_t){});memset(mask,0,sizeof(mask));r.prepareContours(p);r.overlay([&](int x,int y){assert(x>=0&&x<236&&y>=0&&y<236);mask[y*236+x]=255;});assert(memcmp(mask,previous,sizeof(mask))==0);++j;
  }
 }
 puts("PASS 恒温无轮廓；80组场景×4映射范围与独立非原地参考位级一致；极端温区及连线无越界");
}

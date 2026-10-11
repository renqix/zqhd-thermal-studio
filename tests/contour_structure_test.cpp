#include <initializer_list>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../firmware/thermal_v2/detail_contour.h"
int main(){
 uint16_t q[4096];unsigned char plain[4096]={};DetailContour c;
 for(bool warm:{false,true}){
  for(int y=0;y<64;++y)for(int x=0;x<64;++x){int d=(x-32)*(x-32)+(y-32)*(y-32);q[y*64+x]=1040+(d<400?(warm?64:-64):0);}
  c.detect(q,0);int outer=0;for(int y=0;y<64;++y)for(int x=0;x<64;++x){plain[y*64+x]=c.edge(x,y);outer+=c.edge(x,y);}assert(outer>80);
  // 同一目标内部有冷热纹理及背景温度孔洞，应保持外边缘一致。
  for(int y=20;y<45;++y)for(int x=20;x<45;++x){int d=(x-32)*(x-32)+(y-32)*(y-32);if(d<110)q[y*64+x]=1040+((x+y)%5==0?0:(warm?1:-1)*(24+(x%4)*20));}
  c.detect(q,0);for(int y=0;y<64;++y)for(int x=0;x<64;++x)assert(plain[y*64+x]==c.edge(x,y));
 }
 // 主体越过画面边缘，仍描出画面内的剩余外边界。
 for(int y=0;y<64;++y)for(int x=0;x<64;++x)q[y*64+x]=1040+((x*x+(y-32)*(y-32)<225)?64:0);
 c.detect(q,0);int n=0;c.overlay([&](int x,int y){assert(x>=0&&x<236&&y>=0&&y<236);++n;});assert(n>30);
 // 单个坏点不应产生独立轮廓。
 for(auto&v:q)v=1040;q[2000]=5440;c.detect(q,0);n=0;c.overlay([&](int,int){++n;});assert(n==0);
 puts("PASS 冷暖主体的内部纹理及孔洞不描线，外轮廓逐点保持；越界主体安全；孤立坏点无轮廓");
}

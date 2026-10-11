#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
// 温差区域外轮廓，参考OpenCV RETR_EXTERNAL的外层轮廓思路。
// 此处为面向64×64热图重新实现的固定内存整数版本，未链接OpenCV。
// https://docs.opencv.org/4.x/d9/d8b/tutorial_py_contours_hierarchy.html
class DetailContour {
 union {uint16_t magnitude[4096];uint16_t queue[4096];} work;
 uint8_t map[1024]; // 每点两位：0内部背景，1候选前景，2主体，3外部背景。
 static int clamp(int v){return v<0?0:v>63?63:v;}
 static int at(const uint16_t*q,int x,int y){return q[clamp(y)*64+clamp(x)];}
 static int absq(int v){return v<0?-v:v;}
 int get(int i)const{return (map[i>>2]>>((i&3)*2))&3;}
 void set(int i,int v){const int shift=(i&3)*2;map[i>>2]=(map[i>>2]&~(3<<shift))|(v<<shift);}
 static int grad(const uint16_t*q,int x,int y,int&dx,int&dy){
  dx=at(q,x+1,y-1)+2*at(q,x+1,y)+at(q,x+1,y+1)-at(q,x-1,y-1)-2*at(q,x-1,y)-at(q,x-1,y+1);
  dy=at(q,x-1,y+1)+2*at(q,x,y+1)+at(q,x+1,y+1)-at(q,x-1,y-1)-2*at(q,x,y-1)-at(q,x+1,y-1);
  return absq(dx)+absq(dy);
 }
public:
 // 输入为实际温度Q4，与屏幕上下限、色板和Tiny灰度映射无关。
 static int temperatureNoise(const uint16_t*p){
  uint16_t histogram[256]={};int total=0;
  for(int y=1;y<31;++y)for(int x=1;x<31;++x){int i=y*32+x,v=absq(4*p[i]-p[i-1]-p[i+1]-p[i-32]-p[i+32]);++histogram[v>255?255:v];++total;}
  int sum=0;for(int i=0;i<256;++i){sum+=histogram[i];if(sum*5>=total*2)return i;}return 255;
 }
 // 40%分位二阶差分，降低长边界对全图噪声估计的影响。
 static int noise(const uint8_t*p){
  uint16_t histogram[256]={};int total=0;
  for(int y=1;y<31;++y)for(int x=1;x<31;++x){int i=y*32+x,v=absq(4*p[i]-p[i-1]-p[i+1]-p[i-32]-p[i+32]);++histogram[v>255?255:v];++total;}
  int cumulative=0;for(int i=0;i<256;++i){cumulative+=histogram[i];if(cumulative*5>=total*2)return i;}
  return 255;
 }
 // 小幅增强可信结构，平坦区旁路；限制在原邻域幅度内以避免光晕。
 void enhance(uint16_t*q,int noiseLevel,int gain){
  if(gain<=0)return;memcpy(work.magnitude,q,8192);
  const int threshold=noiseLevel*24+48;
  for(int y=1;y<63;++y)for(int x=1;x<63;++x){
   int dx,dy,i=y*64+x,m=grad(work.magnitude,x,y,dx,dy);if(m<=threshold)continue;
   int lo=q[i],hi=q[i];for(int yy=-1;yy<=1;++yy)for(int xx=-1;xx<=1;++xx){int v=work.magnitude[i+yy*64+xx];if(v<lo)lo=v;if(v>hi)hi=v;}
   const int c=work.magnitude[i],d=4*c-work.magnitude[i-1]-work.magnitude[i+1]-work.magnitude[i-64]-work.magnitude[i+64];
   const int value=c+d*gain/512;q[i]=value<lo?lo:value>hi?hi:value;
  }
 }
 // 参考只提取外层轮廓的思路，用温差分区及外部背景连通重新实现。
 // 不描区域内部的温差纹理，也不描被主体包围的孔洞。
 void detect(const uint16_t*q,int noiseLevel){
  memset(map,0,sizeof(map));int count=0;
  for(int x=0;x<64;++x){work.queue[count++]=q[x];work.queue[count++]=q[4032+x];}
  for(int y=1;y<63;++y){work.queue[count++]=q[y*64];work.queue[count++]=q[y*64+63];}
  for(int i=1;i<count;++i){uint16_t v=work.queue[i];int j=i;while(j>0&&work.queue[j-1]>v){work.queue[j]=work.queue[j-1];--j;}work.queue[j]=v;}
  const int background=work.queue[count/2],coldAnchor=work.queue[count*3/4],hotAnchor=work.queue[count/4];
  int minimum=q[0],maximum=q[0];for(int i=1;i<4096;++i){if(q[i]<minimum)minimum=q[i];if(q[i]>maximum)maximum=q[i];}
  if(maximum==minimum)return;
  uint16_t histogram[256]={};const int span=maximum-minimum;
  for(int i=0;i<4096;++i)++histogram[(q[i]-minimum)*255/span];
  int cumulative=0,p05=minimum,p95=maximum;bool found=false;
  for(int i=0;i<256;++i){cumulative+=histogram[i];if(!found&&cumulative>=205){p05=minimum+(i*span+127)/255;found=true;}if(cumulative>=3892){p95=minimum+(i*span+127)/255;break;}}
  const bool warm=p95-background>=background-p05;
  const int anchor=warm?hotAnchor:coldAnchor;
  const int threshold=noiseLevel*2+8>20?noiseLevel*2+8:20; // 最少约1.25°C。
  if((warm?p95-anchor:anchor-p05)<threshold)return;
  for(int i=0;i<4096;++i)if((warm?int(q[i])-anchor:anchor-int(q[i]))>=threshold)set(i,1);
  // 去除面积不足16个64×64点的孤立区域；每点最多入队一次。
  for(int seed=0;seed<4096;++seed)if(get(seed)==1){
   int head=0,tail=1;work.queue[0]=seed;set(seed,2);
   while(head<tail){int i=work.queue[head++],x=i&63,y=i>>6;
    for(int yy=-1;yy<=1;++yy)for(int xx=-1;xx<=1;++xx){if(!(xx||yy))continue;int nx=x+xx,ny=y+yy;if(nx<0||nx>63||ny<0||ny>63)continue;int j=ny*64+nx;if(get(j)==1){set(j,2);work.queue[tail++]=j;}}
   }
   if(tail<16)for(int k=0;k<tail;++k)set(work.queue[k],0);
  }
  // 四邻域填充外部背景。主体包围的内部区域保持0，不作为轮廓。
  int head=0,tail=0;
  for(int i=0;i<4096;++i)if(((i&63)==0||(i&63)==63||i<64||i>=4032)&&get(i)==0){set(i,3);work.queue[tail++]=i;}
  while(head<tail){int i=work.queue[head++],x=i&63,y=i>>6;
   const int neighbors[4]={x>0?i-1:-1,x<63?i+1:-1,y>0?i-64:-1,y<63?i+64:-1};
   for(int j:neighbors)if(j>=0&&get(j)==0){set(j,3);work.queue[tail++]=j;}
  }
 }
 bool edge(int x,int y)const{
  if(x<1||x>62||y<1||y>62||get(y*64+x)!=2)return false;
  const int i=y*64+x;
  return (x>0&&get(i-1)==3)||(x<63&&get(i+1)==3)||(y>0&&get(i-64)==3)||(y<63&&get(i+64)==3);
 }
 template<class Pixel>void overlay(Pixel pixel)const{
  // 以1像素细线连接邻近边缘点；连线只改变显示索引。
  for(int y=1;y<63;++y)for(int x=1;x<63;++x)if(edge(x,y)){
   int sx=((x*256-128)*235+7936)/15872,sy=((y*256-128)*235+7936)/15872;pixel(sx,sy);
   for(int dy=0;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){if(dy==0&&dx!=1)continue;if(!edge(x+dx,y+dy))continue;
    int ex=(((x+dx)*256-128)*235+7936)/15872,ey=(((y+dy)*256-128)*235+7936)/15872;
    int ax=absq(ex-sx),ay=absq(ey-sy),steps=ax>ay?ax:ay;
    for(int k=1;k<=steps;++k)pixel(sx+(ex-sx)*k/steps,sy+(ey-sy)*k/steps);
   }
  }
 }
};

#pragma once
// 280×240屏幕：236×236完整等比热图，44px控制栏，4px底部提示区。
namespace ThermalLayout {
constexpr int heat=236,last=heat-1;
struct Rect {int x,y,w,h;};
inline int clamp(int x,int low,int high){return x<low?low:x>high?high:x;}
inline int sample(int x,int y){return (31-(clamp(y,0,last)*31+last/2)/last)*32+(clamp(x,0,last)*31+last/2)/last;}
inline int xOf(int index){return ((index%32)*last+15)/31;}
inline int yOf(int index){return ((31-index/32)*last+15)/31;}
inline bool overlaps(Rect a,Rect b){return a.x<b.x+b.w+2&&a.x+a.w+2>b.x&&a.y<b.y+b.h+2&&a.y+a.h+2>b.y;}
inline Rect place(int x,int y,int w,int h,const Rect* occupied,int count){
  const int dx[4]={6,6,-w-6,-w-6},dy[4]={6,-h-6,6,-h-6};
  for(int i=0;i<4;++i){Rect r={clamp(x+dx[i],0,heat-w),clamp(y+dy[i],0,heat-h),w,h};bool fits=true;for(int j=0;j<count;++j)if(overlaps(r,occupied[j]))fits=false;if(fits)return r;}
  for(int yy=0;yy<=heat-h;yy+=h+3)for(int xx=0;xx<=heat-w;xx+=w+3){Rect r={xx,yy,w,h};bool fits=true;for(int j=0;j<count;++j)if(overlaps(r,occupied[j]))fits=false;if(fits)return r;}
  return {0,0,w,h};
}

} // namespace ThermalLayout

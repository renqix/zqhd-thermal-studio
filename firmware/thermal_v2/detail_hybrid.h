#pragma once
#include "guided_motion.h"
#include "detail_temporal.h"
#include "detail_blend_weights.h"
// 使用已有滤波抑制平坦区噪声，在有结构的位置更多保留时间分支。
class DetailHybrid {
 GuidedMotion baseline;
 DetailTemporal temporal;

 static int clamp(int x){return x<0?0:x>31?31:x;}
public:
 void apply(const float *in,float *out,float *values,float *state,bool reset,float interval){
  baseline.apply(in,out,values,state,reset,interval);
  temporal.apply(in,nullptr,nullptr,nullptr,reset,interval);
  const int32_t *detail=temporal.data();
  for(int y=0;y<32;++y)for(int x=0;x<32;++x){
   int sum=0,lo=76800,hi=-10240;
   for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){int q=detail[clamp(y+dy)*32+clamp(x+dx)];sum+=q;if(q<lo)lo=q;if(q>hi)hi=q;}
   int i=y*32+x,d=detail[i],b=std::lround(out[i]*256);
   int edge=9*d-sum;if(edge<0)edge=-edge;edge/=72;int span=(hi-lo)/16;
   if(edge>15)edge=15;if(span>15)span=15;
   int correction=detailBlend[edge*16+span]*(d-b);
   out[i]=(b+(correction+(correction<0?-128:128))/256)/256.0f;
  }
 }
};

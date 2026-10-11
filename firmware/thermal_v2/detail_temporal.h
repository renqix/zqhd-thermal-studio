#pragma once
#include <cmath>
#include <cstdint>
#include "detail_gain_weights.h"
// 只在时间轴融合，不混合相邻像素的温度；邻域仅用于判断是否运动。
class DetailTemporal {
  int32_t current[1024]={},previous[1024]={},history[1024]={};
  static int clamp(int x){return x<0?0:x>31?31:x;}
  static int absq(int x){return x<0?-x:x;}
public:
  const int32_t *data() const {return history;}
  void apply(const float *input,float *output,float *values,float *state,bool reset,float interval){
    if(!std::isfinite(interval)||interval<=0||interval>.5f)reset=true;
    for(int i=0;i<1024;++i){float t=input[i];if(!std::isfinite(t))t=reset?20:history[i]/256.0f;
      t=t<-40?-40:t>300?300:t;current[i]=std::lround(t*256);}
    for(int y=0;y<32;++y)for(int x=0;x<32;++x){
      int i=y*32+x;
      if(reset)history[i]=current[i];
      else {
        int sum=0;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){int j=clamp(y+dy)*32+clamp(x+dx);sum+=current[j]-previous[j];}
        int a=absq(current[i]-history[i])/16,b=absq(sum)/144;
        if(a>31)a=31;if(b>15)b=15;
        // 长采样间隔不延长历史残留；正常约 0.13 秒保持训练增益。
        int gain=detailGain[a*16+b];if(interval>.25f)gain=256;
        history[i]+=(gain*(current[i]-history[i]))/256;
      }
      if(output)output[i]=history[i]/256.0f;
    }
    for(int i=0;i<1024;++i){previous[i]=current[i];if(values)values[i]=history[i]/256.0f;if(state)state[i]=current[i]/256.0f;}
  }
};

#pragma once
#include <cmath>
#include <cstdint>
#include "mapping_logic.h"

#include "detail_hybrid.h"
// 显示滤波不改变测温读数；状态数组在两种滤波之间复用。
class DisplayFilter {
  float values[1024]={},state[1024]={};
  DetailHybrid guided;
  bool initialized=false;
  int previousMode=-1;
public:
  void apply(const float *input,float *output,int mode,float interval=.133333333f){
    if(!std::isfinite(interval)||interval<=0)interval=.133333333f;
    const bool reset=!initialized||mode!=previousMode||interval>.5f;
    if(mode==3){guided.apply(input,output,values,state,reset,interval);initialized=true;previousMode=mode;return;}
    for(int i=0;i<1024;++i){
      if(reset||mode==0){values[i]=input[i];state[i]=mode==1?.1f:0;}
      else if(mode==1){
        state[i]+=1.0f;const float gain=state[i]/(state[i]+1.5f);
        values[i]+=gain*(input[i]-values[i]);state[i]*=1.0f-gain;
      }else {values[i]=input[i];state[i]=0;}
      output[i]=values[i];
    }
    initialized=true;previousMode=mode;
  }
};

// 最近邻仅保留归一化缓冲；L3的坐标、权重和水平插值缓存已移除。
class ThermalResampler {
  uint16_t normalized[1024] = {};
public:
  template<class Pixel> void renderNearest(const float *input,float low,float high,Pixel pixel) {
    for(int i=0;i<1024;++i)normalized[i]=std::lround(MappingLimits::normalize(input[i],low,high)*1023);
    for(int y=0;y<236;++y)for(int x=0;x<236;++x)pixel(x,y,normalized[((235-y)*31/235)*32+x*31/235]);
  }
};

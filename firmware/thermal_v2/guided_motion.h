#pragma once
#include <cmath>
#include <cstdint>
// Q5温度、Q12系数、Q8历史；整数内循环避免RP2040软件浮点开销。
class GuidedMotion {
  int16_t inputQ[1024],coefficient[1024],clean[1024],previousQ[1024];
  int32_t intercept[1024],historyQ[1024];
  static int clamp(int p){return p<0?0:p>31?31:p;}
  static int absolute(int p){return p<0?-p:p;}
  static int gate(int numerator,int denominator){if(numerator<=0)return 0;if(numerator>=denominator)return 4096;return numerator*4096/denominator;}
public:
  void apply(const float *input,float *output,float *history,float *previous,
             bool reset,float interval){
    for(int i=0;i<1024;++i){
      float t=input[i];if(!std::isfinite(t))t=reset?20:history[i];
      t=t<-40?-40:t>300?300:t;inputQ[i]=std::lround(t*32);
      if(!reset){historyQ[i]=std::lround(history[i]*256);previousQ[i]=std::lround(previous[i]*32);}
    }
    for(int y=0;y<32;++y)for(int x=0;x<32;++x){
      const int i=y*32+x,center=inputQ[i];int sum=0;int64_t square=0;
      for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
        const int d=inputQ[clamp(y+dy)*32+clamp(x+dx)]-center;sum+=d;square+=d*d;
      }
      const int mean=sum/9;int variance=square/9-mean*mean;if(variance<0)variance=0;
      const int a=(int64_t(variance)*4096)/(variance+82);
      coefficient[i]=a;intercept[i]=(4096-a)*(center+mean);
    }
    for(int y=0;y<32;++y)for(int x=0;x<32;++x){
      int a=0;int32_t b=0;
      for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const int j=clamp(y+dy)*32+clamp(x+dx);a+=coefficient[j];b+=intercept[j];}
      clean[y*32+x]=(a*inputQ[y*32+x]+b)/(9*4096);
    }
    const int rate=std::lround(interval/.125f*4096);
    for(int y=0;y<32;++y)for(int x=0;x<32;++x){
      const int i=y*32+x;
      if(reset){output[i]=clean[i]/32.0f;continue;}
      int difference=0,residual=0;
      for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const int j=clamp(y+dy)*32+clamp(x+dx);difference+=inputQ[j]-previousQ[j];residual+=clean[j]*8-historyQ[j];}
      int motion=gate(absolute(difference)-29,158),local=gate(absolute(inputQ[i]-previousQ[i])-22,38),historic=gate(absolute(residual)-138,691);
      if(local>motion)motion=local;if(historic>motion)motion=historic;
      int alpha=655+((3441*motion)>>12);const int scaled=(alpha*rate)>>12;alpha=scaled*4096/(4096-alpha+scaled);
      const int value=historyQ[i]+(alpha*(clean[i]*8-historyQ[i]))/4096;
      output[i]=value/256.0f;
    }
    for(int i=0;i<1024;++i){history[i]=output[i];previous[i]=inputQ[i]/32.0f;}
  }
};

#pragma once
#include <cmath>
#include <cstdint>
#include "mapping_logic.h"

#include "one_euro_pixel.h"
// 显示滤波不改变测温读数；状态数组在两种滤波之间复用。
class DisplayFilter {
  float values[1024]={},state[1024]={};
  int16_t spatial[1024]={};
  uint16_t rangeWeights[129]={};
  bool initialized=false;
  int previousMode=-1;
public:
  DisplayFilter(){for(int i=0;i<129;++i){float d=i/64.0f;rangeWeights[i]=std::lround(1024*std::exp(-d*d/(2*.35f*.35f)));}}
  void apply(const float *input,float *output,int mode,float interval=.133333333f){
    if(!std::isfinite(interval)||interval<=0)interval=.133333333f;
    const bool reset=!initialized||mode!=previousMode||interval>.5f;
    const float frequency=1.0f/interval,da=OneEuroPixel::alpha(1.0f,frequency);
    for(int i=0;i<1024;++i){
      if(reset||mode==0){values[i]=input[i];state[i]=mode==1?.1f:0;}
      else if(mode==1){
        state[i]+=1.0f;const float gain=state[i]/(state[i]+1.5f);
        values[i]+=gain*(input[i]-values[i]);state[i]*=1.0f-gain;
      }else OneEuroPixel::step(input[i],values[i],state[i],frequency,da);
      output[i]=values[i];
    }
    // 温度域3×3双边滤波：只处理显示副本，温差超过2°C不混合。
    // 与时间滤波分开，空间结果不回灌时间状态，避免反复累积模糊。
    if(mode==3){
      // Q6温度：1/64°C量化仅用于显示；内循环整数乘加，原始读数不变。
      for(int i=0;i<1024;++i){float v=output[i];spatial[i]=!std::isfinite(v)?INT16_MIN:std::lround((v<-40?-40:v>300?300:v)*64);}
      for(int y=0;y<32;++y)for(int x=0;x<32;++x){
        const int center=spatial[y*32+x];if(center==INT16_MIN)continue;
        int32_t sum=0;uint32_t total=0;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
          const int xx=x+dx<0?0:x+dx>31?31:x+dx,yy=y+dy<0?0:y+dy>31?31:y+dy;
          const int v=spatial[yy*32+xx],delta=v-center,d=delta<0?-delta:delta;
          if(v==INT16_MIN||d>128)continue;
          const int weight=rangeWeights[d]*(dx==0?2:1)*(dy==0?2:1);
          sum+=v*weight;total+=weight;
        }
        output[y*32+x]=total?sum/(64.0f*total):center/64.0f;
      }
    }
    initialized=true;previousMode=mode;
  }
};

// 可分离 Catmull-Rom 插值，权重预计算为 Q12，内循环不做浮点计算。
// 每一段夹紧到两个中心采样点之间，抑制双三次过冲产生的假热/冷斑。
class ThermalResampler {
  struct Axis { uint8_t index[4]; int16_t weight[4]; uint16_t fraction; } xs[236], ys[236];
  struct LanczosAxis {uint8_t index[6];int16_t weight[6];} lanczos[236];
  uint16_t horizontal[32][236] = {};
  uint16_t normalized[1024] = {};
  static int bound(int v,int low,int high){return v<low?low:(v>high?high:v);}
  static void axis(Axis &a,float position,int size) {
    const int base=static_cast<int>(position);const float t=position-base,t2=t*t,t3=t2*t;
    const float w[4]={-.5f*t+t2-.5f*t3,1-2.5f*t2+1.5f*t3,.5f*t+2*t2-1.5f*t3,-.5f*t2+.5f*t3};
    int sum=0;
    for(int i=0;i<4;++i){a.index[i]=bound(base+i-1,0,size-1);a.weight[i]=std::lround(w[i]*4096);sum+=a.weight[i];}
    a.weight[1]+=4096-sum;a.fraction=std::lround(t*4096);
  }
  static float lanczosKernel(float x){
    x=std::fabs(x);if(x<1e-6f)return 1;if(x>=3)return 0;
    const float p=3.14159265358979323846f*x;return std::sin(p)*std::sin(p/3)/(p*p/3);
  }
  void prepare(const float *input,float low,float high){
    if(std::isfinite(low)&&std::isfinite(high)&&high>low){
      const float scale=1023.0f/(high-low);
      for(int i=0;i<1024;++i)normalized[i]=!std::isfinite(input[i])||input[i]<=low?0:input[i]>=high?1023:std::lround((input[i]-low)*scale);
    }else for(int i=0;i<1024;++i)normalized[i]=std::lround(MappingLimits::normalize(input[i],low,high)*1023);
  }
  static uint16_t sample(const uint16_t p[4],const Axis &a,bool cubic) {
    int v;
    if(cubic){int32_t sum=0;for(int i=0;i<4;++i)sum+=static_cast<int32_t>(p[i])*a.weight[i];v=(sum+2368)>>12;}
    else v=(static_cast<int32_t>(p[1])*(4096-a.fraction)+static_cast<int32_t>(p[2])*a.fraction+2368)>>12;
    return bound(v,p[1]<p[2]?p[1]:p[2],p[1]>p[2]?p[1]:p[2]);
  }
public:
  ThermalResampler(){for(int x=0;x<236;++x){
    axis(xs[x],x*31.0f/235,32);axis(ys[x],(235-x)*31.0f/235,32);
    const float position=x*31.0f/235;const int base=(int)position;float weights[6],total=0;int sum=0;
    for(int i=0;i<6;++i){weights[i]=lanczosKernel(position-(base+i-2));total+=weights[i];lanczos[x].index[i]=bound(base+i-2,0,31);}
    for(int i=0;i<6;++i){lanczos[x].weight[i]=std::lround(weights[i]/total*4096);sum+=lanczos[x].weight[i];}
    lanczos[x].weight[2]+=4096-sum;
  }}
  template<class Pixel>void renderLanczos(const float *input,float low,float high,Pixel pixel){
    prepare(input,low,high);
    for(int y=0;y<32;++y)for(int x=0;x<236;++x){const auto &a=lanczos[x];const uint16_t *row=normalized+y*32;int sum=0;
      for(int i=0;i<6;++i)sum+=row[a.index[i]]*a.weight[i];
      const int lo=row[a.index[2]],hi=row[a.index[3]];horizontal[y][x]=bound((sum+2368)>>12,lo<hi?lo:hi,lo>hi?lo:hi);
    }
    for(int y=0;y<236;++y){const auto &a=lanczos[235-y];for(int x=0;x<236;++x){int sum=0;
      for(int i=0;i<6;++i)sum+=horizontal[a.index[i]][x]*a.weight[i];
      const int lo=horizontal[a.index[2]][x],hi=horizontal[a.index[3]][x];pixel(x,y,bound((sum+2368)>>12,lo<hi?lo:hi,lo>hi?lo:hi));
    }}
  }
  template<class Pixel> void renderNearest(const float *input,float low,float high,Pixel pixel) {
    for(int i=0;i<1024;++i)normalized[i]=std::lround(MappingLimits::normalize(input[i],low,high)*1023);
    for(int y=0;y<236;++y)for(int x=0;x<236;++x)pixel(x,y,normalized[((235-y)*31/235)*32+x*31/235]);
  }
  template<bool Cubic,class Pixel> void renderPrepared(Pixel pixel){
    for(int y=0;y<32;++y)for(int x=0;x<236;++x){
      const Axis &a=xs[x];const uint16_t *row=normalized+y*32;
      const uint16_t p[4]={row[a.index[0]],row[a.index[1]],row[a.index[2]],row[a.index[3]]};
      horizontal[y][x]=sample(p,a,Cubic);
    }
    for(int y=0;y<236;++y){
      const Axis &a=ys[y];const uint16_t *p0=horizontal[a.index[0]],*p1=horizontal[a.index[1]],*p2=horizontal[a.index[2]],*p3=horizontal[a.index[3]];
      const int w0=a.weight[0],w1=a.weight[1],w2=a.weight[2],w3=a.weight[3],fraction=a.fraction;
      for(int x=0;x<236;++x){
        const int v=Cubic?(p0[x]*w0+p1[x]*w1+p2[x]*w2+p3[x]*w3+2368)>>12:(p1[x]*(4096-fraction)+p2[x]*fraction+2368)>>12;
        pixel(x,y,bound(v,p1[x]<p2[x]?p1[x]:p2[x],p1[x]>p2[x]?p1[x]:p2[x]));
      }
    }
  }
  template<class Pixel> void render(const float *input,float low,float high,bool cubic,Pixel pixel){
    prepare(input,low,high);
    if(cubic)renderPrepared<true>(pixel);else renderPrepared<false>(pixel);
  }
};

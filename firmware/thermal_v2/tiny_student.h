#pragma once
#include <stdint.h>
#include <cmath>
#include "tiny_weights.h"
#include "mapping_logic.h"

// 332参数的试验学生网络：仅处理显示副本，权重常驻Flash，不分配堆内存。
namespace TinyStudent {
static inline int bound(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
static inline int sample(const uint8_t *p,int x,int y){return p[bound(y,0,31)*32+bound(x,0,31)];}
static inline int32_t requant(int32_t v,int32_t multiplier){return int32_t((int64_t(v)*multiplier+32768)>>16);}
template<int In,int Out> static inline void layer(const int16_t *input,int16_t *output,const int8_t *weight,const int32_t *bias,int32_t multiplier,bool relu){
  for(int o=0;o<Out;++o){int32_t sum=bias[o];for(int i=0;i<In;++i)sum+=input[i]*weight[i*Out+o];
    const int v=requant(sum,multiplier);output[o]=relu?bound(v,0,127):bound(v,-614,614);
  }
}
// 输入32×32 uint8，输出64×64 Q12；以像素中心对齐的2倍子像素排列。
static void infer(const uint8_t *input,uint16_t *output){
  int16_t patch[9],hidden[16],hidden2[8],residual[4];
  for(int y=0;y<32;++y)for(int x=0;x<32;++x){
    const int center=input[y*32+x];int lo=center,hi=center,n=0;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
      const int v=sample(input,x+dx,y+dy),d=(v-center)*127;
      patch[n++]=d>=0?(d+127)/255:-((-d+127)/255);lo=v<lo?v:lo;hi=v>hi?v:hi;
    }
    layer<9,16>(patch,hidden,tiny_weight0,tiny_bias0,tiny_multiplier0,true);
    layer<16,8>(hidden,hidden2,tiny_weight1,tiny_bias1,tiny_multiplier1,true);
    layer<8,4>(hidden2,residual,tiny_weight2,tiny_bias2,tiny_multiplier2,false);
    for(int yy=0;yy<2;++yy)for(int xx=0;xx<2;++xx){
      const int dx=xx?1:-1,dy=yy?1:-1;
      const int sum=9*center+3*sample(input,x+dx,y)+3*sample(input,x,y+dy)+sample(input,x+dx,y+dy);
      const int value=(sum*256+127)/255+residual[yy*2+xx];
      output[(y*2+yy)*64+x*2+xx]=bound(value,(lo*4096+127)/255,(hi*4096+127)/255);
    }
  }
}
}

class TinyThermalResampler {
  uint8_t input[1024];uint16_t output[4096],position[236];
public:
  TinyThermalResampler(){for(int i=0;i<236;++i)position[i]=128+(i*62*256+117)/235;}
  template<class Pixel>void render(const float *temperatures,float low,float high,Pixel pixel){
    // 网络在屏幕朝向训练；先翻转再推理，不改温度坐标和跟踪器。
    for(int y=0;y<32;++y)for(int x=0;x<32;++x)input[y*32+x]=std::lround(MappingLimits::normalize(temperatures[(31-y)*32+x],low,high)*255);
    TinyStudent::infer(input,output);
    for(int y=0;y<236;++y){int iy=position[y]>>8,fy=position[y]&255;
      for(int x=0;x<236;++x){int ix=position[x]>>8,fx=position[x]&255;
        const uint16_t *row=output+iy*64;
        const int a=row[ix]*(256-fx)+row[ix+1]*fx,b=row[ix+64]*(256-fx)+row[ix+65]*fx;
        const int q=(a*(256-fy)+b*fy+32768)>>16;
        pixel(x,y,TinyStudent::bound((q*1023+2048)>>12,0,1023));
      }
    }
  }
};

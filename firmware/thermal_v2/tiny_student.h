#pragma once
#include <stdint.h>
#include <cmath>
#include "tiny_weights.h"
#include "mapping_logic.h"
#include "detail_contour.h"

// 320参数的试验学生网络：仅处理显示副本，权重常驻Flash，不分配堆内存。
namespace TinyStudent {
static inline int bound(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
static inline int sample(const uint8_t *p,int x,int y){return p[bound(y,0,31)*32+bound(x,0,31)];}
// 穷尽输入幅度的保守上界：权重改变导致32位乘法不安全时拒绝编译。
template<int In,int Out> constexpr bool safeLayer(const int8_t (&w)[In*Out],const int32_t (&bias)[Out],int32_t multiplier){
  for(int o=0;o<Out;++o){int64_t limit=bias[o]<0?-int64_t(bias[o]):bias[o];
    for(int i=0;i<In;++i){int v=w[i*Out+o];limit+=127*(v<0?-v:v);}
    if(limit*multiplier+32768>2147483647)return false;
  }return true;
}
static_assert(safeLayer<14,12>(tiny_weight0,tiny_bias0,tiny_multiplier0),"Tiny layer0 overflow");
static_assert(safeLayer<12,8>(tiny_weight1,tiny_bias1,tiny_multiplier1),"Tiny layer1 overflow");
static_assert(safeLayer<8,4>(tiny_weight2,tiny_bias2,tiny_multiplier2),"Tiny layer2 overflow");
static inline int32_t requant(int32_t v,int32_t multiplier){return (v*multiplier+32768)>>16;}
template<int In,int Out> static inline void layer(const int16_t *input,int16_t *output,const int8_t *weight,const int32_t *bias,int32_t multiplier,bool relu){
  for(int o=0;o<Out;++o){int32_t sum=bias[o];for(int i=0;i<In;++i)sum+=input[i]*weight[i*Out+o];
    const int v=requant(sum,multiplier);output[o]=relu?bound(v,0,127):bound(v,-614,614);
  }
}
// 输入32×32 uint8，输出64×64 Q12；以像素中心对齐的2倍子像素排列。
static void infer(const uint8_t *input,uint16_t *output){
  int16_t patch[14],hidden[12],hidden2[8],residual[4];
  // 全图二阶差分的平均幅度作为噪声提示；整数计算，不增加帧缓存。
  int noiseSum=0;
  for(int y=0;y<32;++y)for(int x=0;x<32;++x){
    int d=4*sample(input,x,y)-sample(input,x-1,y)-sample(input,x+1,y)-sample(input,x,y-1)-sample(input,x,y+1);
    noiseSum+=d<0?-d:d;
  }
  const int noiseHint=bound((noiseSum*127+65536)/131072,0,127);
  for(int y=0;y<32;++y)for(int x=0;x<32;++x){
    const int center=input[y*32+x];int lo=center,hi=center,n=0,neighborLo=255,neighborHi=0;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
      const int v=sample(input,x+dx,y+dy),d=(v-center)*127;
      patch[n++]=d>=0?(d+127)/255:-((-d+127)/255);lo=v<lo?v:lo;hi=v>hi?v:hi;
      if(dx||dy){neighborLo=v<neighborLo?v:neighborLo;neighborHi=v>neighborHi?v:neighborHi;}
    }
    // 扩大到5×5范围的稀疏十字取样；原3×3限幅和小热点保护保持一致。
    const int farValues[4]={sample(input,x,y-2),sample(input,x,y+2),sample(input,x-2,y),sample(input,x+2,y)};
    for(int j=0;j<4;++j){int d=(farValues[j]-center)*127;patch[9+j]=d>=0?(d+127)/255:-((-d+127)/255);}
    patch[13]=noiseHint;
    // 平坦邻域里的孤立弱热点/冷点使用基础插值，防止模型残差抹掉小目标。
    const bool isolated=neighborHi-neighborLo<=2&&(center-neighborHi>=2||neighborLo-center>=2);
    layer<14,12>(patch,hidden,tiny_weight0,tiny_bias0,tiny_multiplier0,true);
    layer<12,8>(hidden,hidden2,tiny_weight1,tiny_bias1,tiny_multiplier1,true);
    layer<8,4>(hidden2,residual,tiny_weight2,tiny_bias2,tiny_multiplier2,false);
    for(int yy=0;yy<2;++yy)for(int xx=0;xx<2;++xx){
      const int dx=xx?1:-1,dy=yy?1:-1;
      const int sum=9*center+3*sample(input,x+dx,y)+3*sample(input,x,y+dy)+sample(input,x+dx,y+dy);
      const int value=(sum*256+127)/255+(isolated?0:residual[yy*2+xx]);
      output[(y*2+yy)*64+x*2+xx]=bound(value,(lo*4096+127)/255,(hi*4096+127)/255);
    }
  }
}
}

class TinyThermalResampler {
  uint8_t input[1024];uint16_t output[4096],position[236];
  DetailContour contours;
public:
  TinyThermalResampler(){for(int i=0;i<236;++i)position[i]=128+(i*62*256+117)/235;}
  template<class Pixel>void render(const float *temperatures,float low,float high,Pixel pixel,bool showContours=false){
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
  void prepareContours(const float *temperatures){
    // 显示完成后复用输出缓冲，使用(-40°C偏移的)温度Q4提取边界。
    for(int y=0;y<32;++y)for(int x=0;x<32;++x){float t=temperatures[(31-y)*32+x];if(!std::isfinite(t))t=20;t=t<-40?-40:t>300?300:t;output[y*32+x]=std::lround((t+40)*16);}
    const int noiseLevel=DetailContour::temperatureNoise(output);
    // 从后向前扩展，避免覆盖尚未读取的32×32源数据；首行单独保存。
    uint16_t firstRows[64];memcpy(firstRows,output,sizeof(firstRows));
    for(int y=63;y>=0;--y)for(int x=0;x<64;++x){
      int ix=(x-1)/2,iy=(y-1)/2,fx=(x&1)?1:3,fy=(y&1)?1:3;if(x==0){ix=0;fx=0;}if(y==0){iy=0;fy=0;}
      const uint16_t *src=y==0?firstRows:output;
      int xx=TinyStudent::bound(ix,0,31),xn=TinyStudent::bound(ix+1,0,31),yy=TinyStudent::bound(iy,0,31),yn=TinyStudent::bound(iy+1,0,31);
      int sum=src[yy*32+xx]*(4-fx)*(4-fy)+src[yy*32+xn]*fx*(4-fy)+src[yn*32+xx]*(4-fx)*fy+src[yn*32+xn]*fx*fy;
      output[y*64+x]=(sum+8)/16;
    }
    contours.detect(output,noiseLevel);
  }
  template<class Pixel>void overlay(Pixel pixel)const{contours.overlay(pixel);}
};

#pragma once
#include <cmath>
#include <cstdint>
// 可调附加背景降噪：只处理显示副本，仅用本帧，不增加历史拖影。
// 0为完全旁路；固定温度差权重保护强边缘，孤立明显冷热点保持原值。
class BackgroundDenoise {
  int32_t source[1024];
  static int clamp(int v){return v<0?0:v>31?31:v;}
  static int absq(int v){return v<0?-v:v;}
public:
  void apply(float *image,int amount){
    if(amount<=0)return;if(amount>100)amount=100;
    // 白墙回放显示75%混合已达到最优平稳区，将其映射为滑块满档。
    const int strength=amount*3/4;
    for(int i=0;i<1024;++i){float v=image[i];v=std::isfinite(v)?v:20;v=v<-40?-40:v>300?300:v;source[i]=std::lround(v*256);}
    for(int y=0;y<32;++y)for(int x=0;x<32;++x){
      const int i=y*32+x,center=source[i];int delta=0,weights=0,lo=76800,hi=-10240;
      for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
        const int q=source[clamp(y+dy)*32+clamp(x+dx)],d=q-center;
        int range=256-absq(d)/3;if(range<0)range=0;
        const int spatial=(dx==0?2:1)*(dy==0?2:1),w=range*spatial;
        delta+=d*w;weights+=w;if(dx||dy){if(q<lo)lo=q;if(q>hi)hi=q;}
      }
      // 比全部邻点高/低至少1°C的孤立目标，不使用附加平滑。
      if(center-hi>=256||lo-center>=256)continue;
      const int correction=delta/weights;const int scaled=(correction*strength+(correction<0?-50:50))/100;
      image[i]=(center+scaled)/256.0f;
    }
  }
};

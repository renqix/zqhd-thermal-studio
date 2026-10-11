#pragma once
#include <cstdint>
#include "denoise_weights.h"
#include "forecast_weights.h"
namespace TinyModels {
constexpr int32_t magnitude(int32_t v){return v<0?-v:v;}
template<int I,int H>constexpr bool safe(const int8_t *w0,const int32_t *b0,int32_t m0,const int8_t *w1,const int32_t *b1,int32_t m1){
 for(int j=0;j<H;++j){int64_t a=magnitude(b0[j]);for(int i=0;i<I;++i)a+=128*magnitude(w0[i*H+j]);if(a*m0+32768>INT32_MAX)return false;}
 int64_t a=magnitude(b1[0]);for(int j=0;j<H;++j)a+=127*magnitude(w1[j]);return a*m1+32768<=INT32_MAX;
}
static_assert(safe<9,8>(denoise_model::w0,denoise_model::b0,denoise_model::m0,denoise_model::w1,denoise_model::b1,denoise_model::m1),"降噪32位乘积界限");
static_assert(safe<18,6>(forecast_model::w0,forecast_model::b0,forecast_model::m0,forecast_model::w1,forecast_model::b1,forecast_model::m1),"预测32位乘积界限");
template<int I,int H>inline int16_t infer(const int8_t *x,const int8_t *w0,const int32_t *b0,int32_t m0,const int8_t *w1,const int32_t *b1,int32_t m1,int limit){
 int8_t h[H];for(int j=0;j<H;++j){int32_t a=b0[j];for(int i=0;i<I;++i)a+=int32_t(x[i])*w0[i*H+j];int32_t v=(a*m0+32768)>>16;h[j]=v<0?0:v>127?127:v;}
 int32_t a=b1[0];for(int j=0;j<H;++j)a+=int32_t(h[j])*w1[j];int32_t v=(a*m1+32768)>>16;return v<-limit?-limit:v>limit?limit:v;
}
inline int16_t denoise(const int8_t *x){using namespace denoise_model;return infer<9,8>(x,w0,b0,m0,w1,b1,m1,205);}
inline int16_t forecast(const int8_t *x){using namespace forecast_model;return infer<18,6>(x,w0,b0,m0,w1,b1,m1,512);}
inline int clamp(int p){return p<0?0:p>31?31:p;}
inline int8_t feature(int q){int v=q*127/768;return v<-127?-127:v>127?127:v;}
// 调用者提供1KiB工作区；输入Q8温度，数组互不重叠。无堆分配。
inline void frame(const int32_t *prev,const int32_t *cur,int32_t *out,bool predict,uint8_t *protectedMask){
 for(int i=0;i<1024;++i)protectedMask[i]=0;
 for(int y=0;y<32;++y)for(int x=0;x<32;++x){int low=INT32_MAX,high=INT32_MIN;
  for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy){int t=cur[clamp(y+dy)*32+clamp(x+dx)];if(t<low)low=t;if(t>high)high=t;}
  int c=cur[y*32+x];if(c>high+256||c<low-256)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)protectedMask[clamp(y+dy)*32+clamp(x+dx)]=1;
 }
 for(int y=0;y<32;++y)for(int x=0;x<32;++x){int i=y*32+x,k=0,low=INT32_MAX,high=INT32_MIN;bool unchanged=true;int8_t f[18];
  for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){int j=clamp(y+dy)*32+clamp(x+dx),c=cur[j];f[k]=feature(c-cur[i]);if(predict){f[k+9]=feature(prev[j]-cur[i]);if(prev[j]!=c)unchanged=false;}++k;if(c<low)low=c;if(c>high)high=c;}
  if(protectedMask[i]||low==high||(predict&&unchanged))out[i]=cur[i];else out[i]=cur[i]+(predict?forecast(f):denoise(f));
 }
}
}

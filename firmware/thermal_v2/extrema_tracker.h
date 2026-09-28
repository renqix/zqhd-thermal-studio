#pragma once
// 每个新传感器帧重新定位显示滤波矩阵的极值，不添加等待帧、迟滞或位置动画。
// 跟踪去噪矩阵，顶部数值仍由调用方取原始全图MIN/MAX。
class ExtremaTracker {
  int selected=0;bool hottest;
public:
  explicit ExtremaTracker(bool high):hottest(high){}
  void update(const float *data,float){
    selected=0;for(int i=1;i<1024;++i)if(hottest?data[i]>data[selected]:data[i]<data[selected])selected=i;
  }
  int index()const{return selected;}
  float x()const{return selected%32;}
  float y()const{return selected/32;}
};

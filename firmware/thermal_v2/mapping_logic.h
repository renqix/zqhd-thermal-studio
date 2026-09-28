#pragma once
#include <cmath>
// 纯逻辑可在 Mac 上独立测试；下限不高于上限；相等时作为双色分界，避免除以零。
struct MappingLimits {
  int lower=20, upper=40;
  bool setLower(int value){if(value < -30 || value > upper)return false;lower=value;return true;}
  bool setUpper(int value){if(value > 300 || value < lower)return false;upper=value;return true;}
  static float normalize(float value,float minimum,float maximum){
    if(!std::isfinite(value)||!std::isfinite(minimum)||!std::isfinite(maximum))return 0;
    if(maximum<minimum)return .5f;
    if(maximum==minimum)return value<minimum?0:(value>maximum?1:.5f);
    const float n=(value-minimum)/(maximum-minimum);
    return n<0?0:(n>1?1:n);
  }
};

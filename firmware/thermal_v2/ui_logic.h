#pragma once
#include <cstdint>
#include <cstdlib>
// 5% 同时落在两个用户区间时，优先使用低电量闪烁；0% 也保持警示。
struct BatteryAppearance { uint32_t color; bool blink; };
inline BatteryAppearance batteryAppearance(int percent) {
  if(percent>=80)return {0x4DE38A,false};
  if(percent>=40)return {0x51B4FF,false};
  if(percent>=20)return {0xFFE066,false};
  return {0xFF5353,percent<=5};
}
struct BottomSwipe {
  int startX=0,startY=0;bool active=false,fired=false;
  void begin(int x,int y){startX=x;startY=y;active=true;fired=false;}
  bool move(int x,int y){
    if(active&&!fired&&startY>=180&&startY-y>=48&&std::abs(x-startX)<=65){fired=true;return true;}
    return false;
  }
  void end(){active=false;}
};

#include <cassert>
#include <cstdio>
#include "../firmware/thermal_v2/ui_logic.h"
#include "../firmware/thermal_v2/mapping_logic.h"
int main(){
 for(int p=0;p<=100;++p){auto a=batteryAppearance(p);assert(a.blink==(p<=5));assert(a.color==(p>=80?0x4DE38A:p>=40?0x51B4FF:p>=20?0xFFE066:0xFF5353));}
 MappingLimits m;assert(m.setLower(-30));assert(!m.setLower(-31));assert(m.setUpper(300));assert(!m.setUpper(301));assert(m.setLower(300));assert(!m.setUpper(299));assert(m.setUpper(300));assert(m.normalize(299,300,300)==0);assert(m.normalize(300,300,300)==.5);assert(m.normalize(301,300,300)==1);
 BottomSwipe s;s.begin(120,230);assert(!s.move(120,200));assert(s.move(123,175));assert(!s.move(123,150));s.end();assert(!s.move(123,130));
 s.begin(50,100);assert(!s.move(50,20));s.begin(50,230);assert(!s.move(130,150));s.begin(100,230);assert(!s.move(100,240));
 puts("PASS: 全部电量边界、闪烁优先级、-30..300温度限制、相等分界、底部上滑阈值");
}

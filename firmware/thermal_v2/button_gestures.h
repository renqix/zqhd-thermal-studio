#pragma once
#include <cstdint>
// 实体按键：30ms去抖，释放后300ms等待连击；长按不算单击。
class ButtonGestures {
  bool raw=false,stable=false,held=false;
  uint8_t taps=0;
  uint32_t changed=0,pressed=0,released=0;
public:
  unsigned update(bool down,uint32_t now){
    if(down!=raw){raw=down;changed=now;}
    if(raw!=stable&&uint32_t(now-changed)>=30){
      stable=raw;
      if(stable){pressed=now;held=false;}
      else if(!held){released=now;if(++taps==3){taps=0;return 3;}}
    }
    if(stable&&uint32_t(now-pressed)>=900){held=true;taps=0;}
    if(!raw&&!stable&&taps&&uint32_t(now-released)>=300){unsigned n=taps;taps=0;return n;}
    return 0;
  }
};

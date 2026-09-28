#pragma once
// 保留最近的非白热模式；三击可以往返，而非每次只进入白热。
class PaletteToggle {
  int remembered=4;
public:
  int select(int current,int next){if(current!=0)remembered=current;if(next!=0)remembered=next;return next;}
  int toggle(int current){if(current==0)return remembered;remembered=current;return 0;}
};

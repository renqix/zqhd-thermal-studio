#pragma once
// 保留协议编号，避免电脑端旧参数指向错误档位。
static constexpr int filterIds[3]={0,1,3};
static constexpr int interpolationIds[2]={0,4};
static constexpr bool validFilter(int x){return x==0||x==1||x==3;}
static constexpr bool validInterpolation(int x){return x==0||x==4;}

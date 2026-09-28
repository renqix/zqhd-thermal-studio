/**
 One Euro Filter, C version
 
 Copyright (c) 2014 Jonathan Aceituno <join@oin.name>

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.

 07/12/20: added MIT license
 03/08/23: fixed version (Géry Casiez)
 
 For details, see https://gery.casiez.net/1euro/
 */

#pragma once
#include <cmath>
// 基于上游 C/SF1eFilter.c 移植：合并全图公共参数，逐像素仅存温度和导数。
// 与上游相同，导数以当前输入减去上次滤波输出计算；不做空间混合。
struct OneEuroPixel {
  static float alpha(float cutoff,float frequency){
    const float rate=6.283185307179586f*cutoff;
    return rate/(rate+frequency);
  }
  static float step(float input,float &value,float &derivative,float frequency,
                    float derivativeAlpha,float minimum=.45f,float beta=.6f){
    const float dx=(input-value)*frequency;
    derivative=derivativeAlpha*dx+(1.0f-derivativeAlpha)*derivative;
    const float gain=alpha(minimum+beta*std::fabs(derivative),frequency);
    value=gain*input+(1.0f-gain)*value;
    return value;
  }
};

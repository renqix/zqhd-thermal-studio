/**************************************************************************/
/*!
  @file     CST816T.cpp
  Author: Atsushi Sasaki (https://github.com/aselectroworks)
  License: MIT (see LICENSE)
*/
/**************************************************************************/

#include "CST816T.h"
#include "Arduino.h"
#include <Wire.h>

CST816T::CST816T()
{
}

CST816T::CST816T(uint8_t rst_n_pin, uint8_t int_n_pin)
{
    rst_n = rst_n_pin;
    int_n = int_n_pin;
}

CST816T::CST816T(uint8_t sda_pin, uint8_t scl_pin, uint8_t rst_n_pin, uint8_t int_n_pin)
{
    sda = sda_pin;
    scl = scl_pin;
    rst_n = rst_n_pin;
    int_n = int_n_pin;
}

CST816T::~CST816T() {
}

void CST816T::begin(void) {
    // Initialize I2C
    if(sda != 255 && scl != 255) {
        Serial1.printf("I2C initializing (SDA %d, SCL %d)\n", sda, scl);
        Wire1.setSDA(sda);
        Wire1.setSCL(scl);
        Wire1.begin();
        Wire1.setClock(100000);
        Wire1.setTimeout(5);
        // Wire1.begin(sda, scl); 
        Serial1.printf("I2C initialized (SDA %d, SCL %d)\n", sda, scl);
    }
    else {
        Wire1.begin(); 
        Serial.println("I2C initialized (SDA, SCL)1");
    }

    // Int Pin Configuration
    if(int_n != 255) {
        pinMode(int_n, INPUT); 
    }
    // Reset Pin Configuration
    if(rst_n != 255) {
        pinMode(rst_n, OUTPUT); 
        digitalWrite(rst_n, LOW); 
        delay(10); 
        digitalWrite(rst_n, HIGH); 
        delay(500); 
    }
    // Initialize Touch
    Serial1.printf("Initialize Touch\n");
    chipId=readByte(0xa7);
    // 按 CST816T 寄存器说明关闭自动休眠，以便连续轮询滑块。
    writeByte(0xFE, 1);
    delay(1);
    writeByte(0xEC, 0b00000101);
    delay(1);
    writeByte(0xFA, 0b01110000);
    delay(1);
}

/**
 * @设置旋转方向,默认为Rotation_0
 * @Rotation：方向 0~3
*/
void CST816T::setRotation(TouchRotation Rotation)
{
    switch (Rotation) {
    case Rotation_0:
      touch_rotation = Rotation_0;
      break;
    case Rotation_1:
      touch_rotation = Rotation_1;
      break;
    case Rotation_2:
      touch_rotation = Rotation_2;
      break;
    case Rotation_3:
      touch_rotation = Rotation_3;
      break;
    }
}

//coordinate diagram（FPC downwards）
TouchInfos CST816T::GetTouchInfo(void){
    // Serial.println("GetTouchInfo");
    TouchInfos info = {};
    uint8_t touchData[7] = {};
    Wire1.beginTransmission(I2C_ADDR_CST816T);
    Wire1.write(0);
    lastError=Wire1.endTransmission(false);
    lastRead=0;
    if(lastError)return info;
    lastRead=Wire1.requestFrom(I2C_ADDR_CST816T,sizeof(touchData));
    if(lastRead!=sizeof(touchData))return info;
    ++goodReads;
    for(unsigned i=0;i<sizeof(touchData);++i)touchData[i]=Wire1.read();

    // This can only be 0 or 1
    uint8_t nbTouchPoints = touchData[touchPointNumIndex] & 0x0f;
    uint8_t xHigh = touchData[touchXHighIndex] & 0x0f;
    uint8_t xLow = touchData[touchXLowIndex];
    uint16_t x = (xHigh << 8) | xLow;
    uint8_t yHigh = touchData[touchYHighIndex] & 0x0f;
    uint8_t yLow = touchData[touchYLowIndex];
    uint16_t y = (yHigh << 8) | yLow;
    Gestures gesture = static_cast<Gestures>(touchData[gestureIndex]);

    rawX=x;rawY=y;
    // 释放事件的坐标可能过期；先识别释放，再检查按下坐标。
    const uint8_t event=touchData[touchXHighIndex]>>6;
    info.touching=nbTouchPoints==1 && event!=1 && gesture!=0xAA;
    if(info.touching&&(x>=TouchWidth||y>=TouchHeight))return info;
    if(info.touching)++pressReads;
    info.x=x;info.y=y;
    info.gesture = gesture;
    info.isValid = true;
    return info;
}

//coordinate diagram（FPC downwards）
int CST816T::update(void){
    TouchInfos data = GetTouchInfo();

    tp.x = data.x;
    tp.y = data.y;
    tp.touching = data.touching;
    tp.gesture = data.gesture;
    return 0;
}

// Private Function
uint8_t CST816T::readByte(uint8_t addr) {
    Wire1.beginTransmission(I2C_ADDR_CST816T);
    Wire1.write(addr);
    lastError=Wire1.endTransmission(false);
    if(lastError)return 0;
    lastRead=Wire1.requestFrom(I2C_ADDR_CST816T,1);
    return lastRead==1?Wire1.read():0;

}

void CST816T::writeByte(uint8_t addr, uint8_t data) {
  Wire1.beginTransmission(I2C_ADDR_CST816T); 
    Wire1.write(addr); 
    Wire1.write(data); 
    Wire1.endTransmission(); 
}
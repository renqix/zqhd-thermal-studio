#include <Arduino.h>
#include <Wire.h>
#include "sensor_backend.h"
#include "heimann/heimann_driver.hpp"
static unsigned long calibrationPixels[1024];
static unsigned completedFrames=0;
static unsigned long factoryPixels[1024];
static unsigned factoryEmissivity=100;

int thermalSensorBegin() {
  sensor_power_on();delay(500);
  Wire.setSDA(12);Wire.setSCL(13);Wire.begin();Wire.setClock(400000);
  uint8_t status=0;
  // 热重启后首次探头探测可能暂时失败；仅在读取状态阶段有限重试。
  // 不重写校准数据，也不掩盖持续失败。
  for(int attempt=0;attempt<3;++attempt){
    sensorIoError=false;read_sensor_register(STATUS_REGISTER,&status,1);
    if(!sensorIoError)break;
    if(attempt<2){Wire.end();delay(100);Wire.begin();Wire.setClock(400000);}
  }
  if(sensorIoError){Serial.println("HEIMANN: 0x1A no response");return -10;}
  pixc2_0=calibrationPixels;pixc2=calibrationPixels;
  read_eeprom();
  Serial.printf("HEIMANN id=%lu table=%u bad=%u ptat=%u,%u gain=%u\n",id,tablenumber,nrofdefpix,ptatth1,ptatth2,globalgain);
  if(sensorIoError || tablenumber!=TABLENUMBER || nrofdefpix>ALLOWED_DEADPIX ||
     ptatth1==ptatth2 || gradscale>30 || vddscgrad>30 || vddscoff>30 ||
     !isfinite(pixcmin)||!isfinite(pixcmax)||!epsilon||!globalgain)return -11;
  gradscale_div=1UL<<gradscale;vddscgrad_div=1UL<<vddscgrad;vddscoff_div=1UL<<vddscoff;
  calcPixC();for(unsigned long value:calibrationPixels)if(value==0)return -12;
  memcpy(factoryPixels,calibrationPixels,sizeof(factoryPixels));factoryEmissivity=epsilon;
  write_sensor_byte(SENSOR_ADDRESS,CONFIGURATION_REGISTER,0x01);
  write_calibration_settings_to_sensor();
  // 先取盲像元偏置块，与 read_block_num=START_WITH_BLOCK 配对。
  write_sensor_byte(SENSOR_ADDRESS,CONFIGURATION_REGISTER,0x0B);
  Wire.setClock(1000000);
  Serial.println("HEIMANN: warming up");
  return sensorIoError?-13:0;
}

int thermalSensorRead(float *output) {
  sensorIoError=false;readblockinterrupt();
  if(sensorIoError)return -14;
  if(!state)return 0;
  state=0;sort_data();++completedFrames;
  // 等待 PTAT/VDD 平均缓存建立后再公布测温数据。
  if(completedFrames<24)return 0;
  calculate_pixel_temp();if(sensorIoError)return -15;
  for(int i=0;i<1024;++i)output[i]=(static_cast<float>(data_pixel_flatten[i])-2732.0f)/10.0f;
  return 1;
}

// 仅在采样核心、帧边界调整内存系数，不写入探头 EEPROM。
int thermalSensorEmissivity(){return epsilon;}
bool thermalSensorSetEmissivity(int percent){
  if(percent<5||percent>100||factoryEmissivity==0)return false;
  for(int i=0;i<1024;++i)if((uint64_t(factoryPixels[i])*percent)/factoryEmissivity==0)return false;
  for(int i=0;i<1024;++i)calibrationPixels[i]=(uint64_t(factoryPixels[i])*percent+factoryEmissivity/2)/factoryEmissivity;
  epsilon=percent;lastepsilon=percent;return true;
}

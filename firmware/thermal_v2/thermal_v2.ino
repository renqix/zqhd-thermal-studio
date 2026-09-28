// 第二版测试：One Euro滤波、实体连击；沿用第一版250MHz启动配置。
#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <pico/mutex.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include "palettes.h"
#include "sensor_backend.h"
#include "CST816T.h"
#include "mapping_logic.h"
#include "thermal_processing.h"
#include "tiny_student.h"
#include "ui_logic.h"
#include "thermal_layout.h"
#include "extrema_tracker.h"
#include "button_gestures.h"
#include "palette_toggle.h"
#include <hardware/structs/ssi.h>

TFT_eSPI tft;
CST816T touch(2,3,-1,-1);
static mutex_t frameMutex;
static volatile bool ready=false;
static volatile int sensorState=0,desiredEmissivity=0,actualEmissivity=0;
static float sharedPixels[1024],pixels[1024],filteredPixels[1024];
static uint32_t sharedSequence=0,shownSequence=0,receivedAt=0;
static lv_color_t displayBuffer[280*12];
// 索引画布使用原有180色板，腾出大热图所需内存，色板颜色不变。
alignas(4) static uint8_t imageBuffer[236*236+256*4];
static uint8_t levelIndex[1024];
static lv_disp_draw_buf_t displayDrawBuffer;
static lv_obj_t *canvas,*lowLabel,*highLabel,*maxLabel,*minLabel,*batteryHandle,*maxCross,*minCross,*pointCross;
static lv_obj_t *messageLabel,*pointLabel,*pauseLabel,*modeSwitch,*dialog=nullptr,*rangeSlider,*rangeValue;
static lv_obj_t *emissivitySlider,*emissivityValue,*brightnessValue,*filterButtons[4],*interpButtons[5];
static MappingLimits limits;
static MappingLimits manualLimits;
static bool captureRangeOnEnable=false;
static ThermalResampler resampler;
static TinyThermalResampler tinyResampler;
static DisplayFilter displayFilter;
static ExtremaTracker hotTracker(true),coldTracker(false);
static BottomSwipe bottomSwipe;
static bool customMapping=false,recolor=true,editingLower=true,settingsOpen=false,openSettingsRequested=false;
static bool paused=false,screenLocked=false;
static ButtonGestures buttonGestures;
static lv_obj_t *lockLabel;
static uint32_t freezeCount=0;
static bool usbStreaming=false,usbMirror=false,usbCommandMode=false;
static char usbCommand[96];
static uint8_t usbCommandLength=0;
static float usbPixels[1024];
static uint32_t usbSequence=0,lastMirror=0,lastUsbState=0;
static bool touchDown=false,suppressPoint=false,capturePending=false,capturing=false;
static int paletteMode=4,settingsTab=0;
static PaletteToggle paletteToggle;
static uint16_t mirrorRuns[280*12*2];
static uint16_t mirrorPalette[256];
static int16_t mirrorHash[512];
static uint32_t mirrorBytes=0,lastCaptureUs=0;
static int filterMode=3,interpolationMode=4,pendingTemperature=20,pointX=118,pointY=118,brightness=50;
static int batteryPercent=100,lastValidState=-1;
static uint32_t lastTick=0,lastBattery=0,lastDiagnostic=0,lastPaint=0;
static uint32_t filterUs=0,processingUs=0,renderUs=0,flushUs=0,maxLoopUs=0,maxTouchUs=0,invalidTouches=0;
extern "C" const lv_font_t thermal_cn_font;
static const lv_font_t *chineseFont=&thermal_cn_font;

static void flushDisplay(lv_disp_drv_t *driver,const lv_area_t *area,lv_color_t *color){
  const uint32_t start=micros(),width=area->x2-area->x1+1,height=area->y2-area->y1+1;
  // 调试截图取真实 LVGL 绘制块，无额外整屏缓存；仅串口显式请求时启用。
  if(capturing&&Serial){
    // 每块保存原始RGB565色表和8位索引，颜色完全可逆；噪点多时也能减少传输。
    const uint16_t *source=(const uint16_t*)color;const uint32_t count=width*height;
    uint8_t *indices=(uint8_t*)mirrorRuns,*runs=indices+280*12;
    memset(mirrorHash,0xff,sizeof(mirrorHash));unsigned colors=0;bool indexed=true;
    for(uint32_t i=0;i<count;++i){
      const uint16_t value=source[i];unsigned slot=(uint32_t(value)*40503u)&511u;
      while(mirrorHash[slot]>=0&&mirrorPalette[mirrorHash[slot]]!=value)slot=(slot+1)&511u;
      if(mirrorHash[slot]<0){if(colors==256){indexed=false;break;}mirrorPalette[colors]=value;mirrorHash[slot]=colors++;}
      indices[i]=mirrorHash[slot];
    }
    if(indexed){
      unsigned used=0;for(uint32_t i=0;i<count;){unsigned run=1;while(i+run<count&&indices[i+run]==indices[i]&&run<255)++run;runs[used++]=run;runs[used++]=indices[i];i+=run;}
      const bool packed=used<count;const unsigned bytes=packed?used:count;
      Serial.printf("#PAL %d %d %lu %lu %u %u %u\n",area->x1,area->y1,(unsigned long)width,(unsigned long)height,colors,packed,bytes);
      Serial.write((uint8_t*)mirrorPalette,colors*2);Serial.write(packed?runs:indices,bytes);mirrorBytes+=colors*2+bytes;
    }else{Serial.printf("#RECT %d %d %lu %lu\n",area->x1,area->y1,(unsigned long)width,(unsigned long)height);Serial.write((uint8_t*)color,count*2);mirrorBytes+=count*2;}
  }
  tft.startWrite();
  // CGRAM_OFFSET 已由屏幕库处理，不能再次叠加 20 像素。
  tft.setAddrWindow(area->x1,area->y1,width,height);
  tft.pushColors((uint16_t*)color,width*height,true);tft.endWrite();
  if(capturing&&lv_disp_flush_is_last(driver)){capturing=false;Serial.println("#END");}
  flushUs+=micros()-start;lv_disp_flush_ready(driver);
}

static void readTouch(lv_indev_drv_t *,lv_indev_data_t *data){
  static uint32_t lastGood=0;static lv_point_t lastPoint={0,0};
  const uint32_t start=micros();TouchInfos info=touch.GetTouchInfo();
  maxTouchUs=max(maxTouchUs,micros()-start);
  if(screenLocked){data->state=LV_INDEV_STATE_REL;touchDown=false;bottomSwipe.end();return;}
  if(!info.isValid){
    ++invalidTouches;
    if(touchDown&&millis()-lastGood<40){data->state=LV_INDEV_STATE_PR;data->point=lastPoint;return;}
    data->state=LV_INDEV_STATE_REL;touchDown=false;bottomSwipe.end();return;
  }
  lastGood=millis();
  if(!info.touching){data->state=LV_INDEV_STATE_REL;touchDown=false;bottomSwipe.end();return;}
  lastPoint.x=constrain((int)info.y,0,279);lastPoint.y=constrain(239-(int)info.x,0,239);
  if(!touchDown){bottomSwipe.begin(lastPoint.x,lastPoint.y);suppressPoint=false;}
  touchDown=true;
  if(!dialog&&bottomSwipe.move(lastPoint.x,lastPoint.y)){openSettingsRequested=true;suppressPoint=true;}
  data->state=suppressPoint?LV_INDEV_STATE_REL:LV_INDEV_STATE_PR;data->point=lastPoint;
}

static lv_obj_t *label(lv_obj_t *parent,const char *text,int x,int y,const lv_font_t *font=&lv_font_montserrat_14){
  lv_obj_t *o=lv_label_create(parent);lv_label_set_text(o,text);lv_obj_set_pos(o,x,y);
  lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(0xEAF2FF),0);return o;
}
static void fitLabel(lv_obj_t *o,int width){lv_obj_set_width(o,width);lv_label_set_long_mode(o,LV_LABEL_LONG_CLIP);lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);}
static void setText(lv_obj_t *o,const char *text){if(strcmp(lv_label_get_text(o),text))lv_label_set_text(o,text);}
static lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int w,int h,const lv_font_t *font){
  lv_obj_t *b=lv_btn_create(parent);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);
  lv_obj_set_style_pad_all(b,0,0);lv_obj_set_style_radius(b,6,0);lv_obj_set_style_shadow_width(b,0,0);
  lv_obj_set_style_bg_color(b,lv_color_hex(0x26394B),0);lv_obj_t *t=label(b,text,0,0,font);lv_obj_center(t);return b;
}
static void updateLimitLabels(){lv_label_set_text_fmt(lowLabel,"%d\nC",limits.lower);lv_label_set_text_fmt(highLabel,"%d\nC",limits.upper);recolor=true;}
// 使用原始测温矩阵抓取边界；整数下限向下取整，上限向上取整。
static bool currentRange(int &low,int &high){
  if(!shownSequence)return false;
  float lo=pixels[0],hi=pixels[0];for(int i=1;i<1024;++i){lo=min(lo,pixels[i]);hi=max(hi,pixels[i]);}
  low=constrain((int)floorf(lo),-30,300);high=constrain((int)ceilf(hi),-30,300);return true;
}
static bool setCustomMapping(bool enabled){
  if(enabled&&!customMapping){if(captureRangeOnEnable){int lo,hi;if(!currentRange(lo,hi))return false;limits.lower=lo;limits.upper=hi;}else limits=manualLimits;updateLimitLabels();}
  customMapping=enabled;if(enabled)lv_obj_add_state(modeSwitch,LV_STATE_CHECKED);else lv_obj_clear_state(modeSwitch,LV_STATE_CHECKED);recolor=true;return true;
}
static lv_obj_t *createDialog(){
  lv_obj_t *o=lv_obj_create(lv_scr_act());lv_obj_set_pos(o,0,0);lv_obj_set_size(o,280,240);
  lv_obj_set_style_pad_all(o,0,0);lv_obj_set_style_radius(o,0,0);lv_obj_set_style_border_width(o,0,0);
  lv_obj_set_style_bg_color(o,lv_color_hex(0x111922),0);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
static void closeDialog(){if(dialog)lv_obj_del_async(dialog);dialog=nullptr;settingsOpen=false;recolor=true;}
static void showSliderValue(){
  pendingTemperature=constrain(pendingTemperature,-30,300);
  pendingTemperature=editingLower?min(pendingTemperature,limits.upper):max(pendingTemperature,limits.lower);
  lv_slider_set_value(rangeSlider,pendingTemperature,LV_ANIM_OFF);lv_label_set_text_fmt(rangeValue,"%d C",pendingTemperature);lv_obj_align(rangeValue,LV_ALIGN_TOP_MID,0,49);
}
static void sliderChanged(lv_event_t *){pendingTemperature=lv_slider_get_value(rangeSlider);showSliderValue();}
static void openPicker(bool lower){
  if(dialog)return;editingLower=lower;pendingTemperature=lower?limits.lower:limits.upper;dialog=createDialog();
  lv_obj_t *title=label(dialog,lower?"映射最低温度":"映射最高温度",0,15,chineseFont);lv_obj_align(title,LV_ALIGN_TOP_MID,0,15);
  rangeValue=label(dialog,"",0,49,&lv_font_montserrat_22);
  rangeSlider=lv_slider_create(dialog);lv_obj_set_pos(rangeSlider,26,103);lv_obj_set_size(rangeSlider,228,12);
  lv_slider_set_range(rangeSlider,-30,300);lv_obj_set_ext_click_area(rangeSlider,18);
  lv_obj_add_event_cb(rangeSlider,sliderChanged,LV_EVENT_VALUE_CHANGED,nullptr);
  label(dialog,"-30 C",21,124,&lv_font_montserrat_12);label(dialog,"300 C",219,124,&lv_font_montserrat_12);
  for(int i=0;i<2;++i){
    lv_obj_t *step=button(dialog,i?"+1":"-1",20+i*180,150,60,32,&lv_font_montserrat_18);
    lv_obj_add_event_cb(step,[](lv_event_t *e){pendingTemperature+=(intptr_t)lv_event_get_user_data(e);showSliderValue();},LV_EVENT_CLICKED,(void*)(intptr_t)(i?1:-1));
    lv_obj_t *b=button(dialog,i?"确定":"取消",12+i*132,193,124,36,chineseFont);
    lv_obj_add_event_cb(b,[](lv_event_t *e){if((intptr_t)lv_event_get_user_data(e)){if(editingLower)limits.setLower(pendingTemperature);else limits.setUpper(pendingTemperature);manualLimits=limits;updateLimitLabels();}closeDialog();},LV_EVENT_CLICKED,(void*)(intptr_t)i);
  }
  lv_obj_t *match=button(dialog,"取当前值",86,150,108,32,chineseFont);
  if(!shownSequence)lv_obj_add_state(match,LV_STATE_DISABLED);
  lv_obj_add_event_cb(match,[](lv_event_t *){int lo,hi;if(currentRange(lo,hi)){pendingTemperature=editingLower?lo:hi;showSliderValue();}},LV_EVENT_CLICKED,nullptr);
  showSliderValue();
}
static void styleChoice(lv_obj_t **buttons,int choice,int count=4){for(int i=0;i<count;++i)lv_obj_set_style_bg_color(buttons[i],lv_color_hex(i==choice?0x147AB8:0x26394B),0);}
static void rebuildColors(){
  for(int i=0;i<1024;++i)levelIndex[i]=i*179/1023;
  if(canvas)for(int i=0;i<256;++i){lv_color_t color;color.full=paletteTables[paletteMode][i<180?i:179];lv_canvas_set_palette(canvas,i,color);}
  recolor=true;
}
static void openSettings();
static void openPalettes(){
  if(dialog)closeDialog();settingsOpen=true;dialog=createDialog();
  label(dialog,"显示模式",16,14,chineseFont);
  lv_obj_t *back=button(dialog,"返回",209,8,58,32,chineseFont);
  lv_obj_add_event_cb(back,[](lv_event_t *){closeDialog();openSettings();},LV_EVENT_CLICKED,nullptr);
  const char *names[]={"白热","黑热","经典","铁红","彩虹","熔岩","翠绿"};
  for(int i=0;i<7;++i){
    lv_obj_t *b=button(dialog,names[i],12+(i%2)*134,48+(i/2)*46,122,38,chineseFont);
    lv_obj_set_style_bg_color(b,lv_color_hex(i==paletteMode?0x147AB8:0x26394B),0);
    lv_obj_add_event_cb(b,[](lv_event_t *e){paletteMode=paletteToggle.select(paletteMode,(intptr_t)lv_event_get_user_data(e));rebuildColors();closeDialog();},LV_EVENT_CLICKED,(void*)(intptr_t)i);
  }
}
static void openSettings(){
  if(dialog)return;settingsOpen=true;dialog=createDialog();
  label(dialog,"设置",16,14,chineseFont);
  lv_obj_t *back=button(dialog,"返回",209,8,58,32,chineseFont);lv_obj_add_event_cb(back,[](lv_event_t *){closeDialog();},LV_EVENT_CLICKED,nullptr);
  const char *tabs[]={"图像","测温","映射"};
  for(int i=0;i<3;++i){
    lv_obj_t *tab=button(dialog,tabs[i],12+i*88,45,80,32,chineseFont);
    lv_obj_set_style_bg_color(tab,lv_color_hex(i==settingsTab?0x147AB8:0x26394B),0);
    lv_obj_add_event_cb(tab,[](lv_event_t *e){settingsTab=(intptr_t)lv_event_get_user_data(e);closeDialog();openSettings();},LV_EVENT_CLICKED,(void*)(intptr_t)i);
  }
  if(settingsTab==2){
    label(dialog,"开启自定义时",17,92,chineseFont);
    for(int i=0;i<2;++i){
      lv_obj_t *choice=button(dialog,i?"取当前最低最高":"使用手动设置",12,124+i*48,256,38,chineseFont);
      lv_obj_set_style_bg_color(choice,lv_color_hex(captureRangeOnEnable==bool(i)?0x147AB8:0x26394B),0);
      lv_obj_add_event_cb(choice,[](lv_event_t *e){captureRangeOnEnable=(intptr_t)lv_event_get_user_data(e);closeDialog();openSettings();},LV_EVENT_CLICKED,(void*)(intptr_t)i);
    }
    return;
  }
  if(settingsTab==0){
  label(dialog,"滤波",15,97,chineseFont);label(dialog,"插值",15,137,chineseFont);
  const char *filters[]={"关闭","平滑","清晰","增强"};const char *interps[]={"关闭","线性","三次","L3","Tiny"};
  for(int i=0;i<4;++i){
    filterButtons[i]=button(dialog,filters[i],68+i*51,89,48,32,chineseFont);
    lv_obj_add_event_cb(filterButtons[i],[](lv_event_t *e){filterMode=(intptr_t)lv_event_get_user_data(e);if(shownSequence)displayFilter.apply(pixels,filteredPixels,filterMode);styleChoice(filterButtons,filterMode);recolor=true;},LV_EVENT_CLICKED,(void*)(intptr_t)i);
  }
  for(int i=0;i<5;++i){
    interpButtons[i]=button(dialog,interps[i],64+i*42,129,40,32,i==4?&lv_font_montserrat_12:chineseFont);
    lv_obj_add_event_cb(interpButtons[i],[](lv_event_t *e){interpolationMode=(intptr_t)lv_event_get_user_data(e);styleChoice(interpButtons,interpolationMode,5);recolor=true;},LV_EVENT_CLICKED,(void*)(intptr_t)i);
  }
  styleChoice(filterButtons,filterMode);styleChoice(interpButtons,interpolationMode,5);
    lv_obj_t *pal=button(dialog,"显示模式",12,181,256,42,chineseFont);
    lv_obj_add_event_cb(pal,[](lv_event_t *){openPalettes();},LV_EVENT_CLICKED,nullptr);
    return;
  }
  label(dialog,"发射率",17,96,chineseFont);emissivityValue=label(dialog,"--",213,97,&lv_font_montserrat_14);
  emissivitySlider=lv_slider_create(dialog);lv_obj_set_pos(emissivitySlider,25,131);lv_obj_set_size(emissivitySlider,230,10);lv_obj_set_ext_click_area(emissivitySlider,12);
  lv_slider_set_range(emissivitySlider,5,100);lv_slider_set_value(emissivitySlider,actualEmissivity?actualEmissivity:100,LV_ANIM_OFF);
  if(actualEmissivity)lv_label_set_text_fmt(emissivityValue,"%.2f",actualEmissivity/100.0f);else lv_obj_add_state(emissivitySlider,LV_STATE_DISABLED);
  lv_obj_add_event_cb(emissivitySlider,[](lv_event_t *e){int v=lv_slider_get_value(emissivitySlider);lv_label_set_text_fmt(emissivityValue,"%.2f",v/100.0f);if(lv_event_get_code(e)==LV_EVENT_RELEASED)desiredEmissivity=v;},LV_EVENT_VALUE_CHANGED,nullptr);
  lv_obj_add_event_cb(emissivitySlider,[](lv_event_t *){desiredEmissivity=lv_slider_get_value(emissivitySlider);},LV_EVENT_RELEASED,nullptr);
  label(dialog,"亮度",17,163,chineseFont);brightnessValue=label(dialog,"",213,164,&lv_font_montserrat_14);lv_label_set_text_fmt(brightnessValue,"%d%%",brightness);
  lv_obj_t *brightSlider=lv_slider_create(dialog);lv_obj_set_pos(brightSlider,25,200);lv_obj_set_size(brightSlider,230,10);lv_obj_set_ext_click_area(brightSlider,12);lv_slider_set_range(brightSlider,5,100);lv_slider_set_value(brightSlider,brightness,LV_ANIM_OFF);
  lv_obj_add_event_cb(brightSlider,[](lv_event_t *e){brightness=lv_slider_get_value(lv_event_get_target(e));analogWrite(4,brightness*255/100);lv_label_set_text_fmt(brightnessValue,"%d%%",brightness);},LV_EVENT_VALUE_CHANGED,nullptr);
}
static lv_obj_t *thermalTag(lv_obj_t *screen,const char *text,uint32_t color,const lv_font_t *font){
  lv_obj_t *o=label(screen,text,0,0,font);lv_obj_set_style_text_color(o,lv_color_hex(color),0);
  lv_obj_set_style_bg_color(o,lv_color_black(),0);lv_obj_set_style_bg_opa(o,LV_OPA_60,0);
  lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
static lv_obj_t *triangleMarker(lv_obj_t *screen,bool hot){
  lv_obj_t *o=lv_obj_create(screen);lv_obj_remove_style_all(o);lv_obj_set_size(o,11,10);lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(o,[](lv_event_t *e){
    const bool hot=(intptr_t)lv_event_get_user_data(e);lv_area_t area;lv_obj_get_coords(lv_event_get_target(e),&area);
    lv_point_t points[3]={{(lv_coord_t)(area.x1+5),(lv_coord_t)(hot?area.y1:area.y2)},{area.x1,(lv_coord_t)(hot?area.y2:area.y1)},{area.x2,(lv_coord_t)(hot?area.y2:area.y1)}};
    lv_draw_rect_dsc_t style;lv_draw_rect_dsc_init(&style);style.bg_color=lv_color_hex(hot?0xFF4D4D:0x459BFF);style.bg_opa=LV_OPA_COVER;style.border_width=0;
    lv_draw_polygon(lv_event_get_draw_ctx(e),&style,points,3);
  },LV_EVENT_DRAW_MAIN,(void*)(intptr_t)hot);return o;
}
static void moveMarker(lv_obj_t *o,int x,int y){lv_obj_set_pos(o,x,y);}
static void createUI(){
  lv_obj_t *screen=lv_scr_act();lv_obj_set_style_bg_color(screen,lv_color_hex(0x080B10),0);lv_obj_clear_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
  canvas=lv_canvas_create(screen);lv_canvas_set_buffer(canvas,imageBuffer,236,236,LV_IMG_CF_INDEXED_8BIT);lv_obj_set_pos(canvas,0,0);lv_obj_add_flag(canvas,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(canvas,[](lv_event_t *){if(suppressPoint||dialog)return;lv_point_t p;lv_indev_get_point(lv_indev_get_act(),&p);pointX=constrain((int)p.x,0,235);pointY=constrain((int)p.y,0,235);recolor=true;},LV_EVENT_CLICKED,nullptr);
  for(int i=0;i<2;++i){
    lv_obj_t *b=button(screen,"",238,i?48:137,40,78,chineseFont);lv_obj_set_style_radius(b,5,0);
    lv_obj_set_style_bg_color(b,lv_color_hex(i?0xEF3D4D:0x1747B8),0);
    lv_obj_t *v=label(b,"",0,0,&lv_font_montserrat_18);lv_obj_set_style_text_align(v,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_color(v,lv_color_white(),0);lv_obj_align(v,LV_ALIGN_CENTER,0,0);if(i)highLabel=v;else lowLabel=v;
    lv_obj_add_event_cb(b,[](lv_event_t *e){openPicker((intptr_t)lv_event_get_user_data(e)==0);},LV_EVENT_CLICKED,(void*)(intptr_t)i);
  }
  modeSwitch=lv_switch_create(screen);lv_obj_set_pos(modeSwitch,240,15);lv_obj_set_size(modeSwitch,36,18);
  lv_obj_t *switchArea=lv_obj_create(screen);lv_obj_set_pos(switchArea,236,0);lv_obj_set_size(switchArea,44,43);lv_obj_set_style_bg_opa(switchArea,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(switchArea,0,0);lv_obj_clear_flag(switchArea,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(switchArea,[](lv_event_t *){setCustomMapping(!customMapping);},LV_EVENT_CLICKED,nullptr);
  maxCross=triangleMarker(screen,true);minCross=triangleMarker(screen,false);pointCross=thermalTag(screen,"+",0xFFFFFF,&lv_font_montserrat_12);
  lv_obj_t *band=lv_obj_create(screen);lv_obj_set_pos(band,0,0);lv_obj_set_size(band,236,22);lv_obj_set_style_bg_color(band,lv_color_hex(0xD6D6D6),0);lv_obj_set_style_bg_opa(band,LV_OPA_60,0);lv_obj_set_style_border_width(band,0,0);lv_obj_set_style_radius(band,0,0);lv_obj_clear_flag(band,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  minLabel=label(screen,"",0,3,&lv_font_montserrat_14);maxLabel=label(screen,"",118,3,&lv_font_montserrat_14);
  fitLabel(minLabel,118);fitLabel(maxLabel,118);lv_obj_set_style_text_color(minLabel,lv_color_black(),0);lv_obj_set_style_text_color(maxLabel,lv_color_black(),0);
  pointLabel=thermalTag(screen,"",0xFFFFFF,&lv_font_montserrat_14);
  messageLabel=label(screen,"等待数据",76,112,chineseFont);
  batteryHandle=lv_obj_create(screen);lv_obj_set_pos(batteryHandle,122,237);lv_obj_set_size(batteryHandle,36,3);lv_obj_set_style_pad_all(batteryHandle,0,0);lv_obj_set_style_radius(batteryHandle,2,0);lv_obj_set_style_bg_color(batteryHandle,lv_color_hex(0x4DE38A),0);lv_obj_set_style_border_width(batteryHandle,0,0);lv_obj_clear_flag(batteryHandle,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  pauseLabel=lv_obj_create(screen);lockLabel=lv_obj_create(screen);
  lv_obj_t *indicators[2]={pauseLabel,lockLabel};
  for(int i=0;i<2;++i){lv_obj_t *o=indicators[i];lv_obj_set_pos(o,238+i*21,220);lv_obj_set_size(o,19,15);lv_obj_set_style_bg_color(o,lv_color_hex(0x25303A),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_radius(o,3,0);lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);}
  updateLimitLabels();
}
static void drawThermal(){
  const uint32_t start=micros();int minIndex=0,maxIndex=0;
  for(int i=1;i<1024;++i){if(pixels[i]<pixels[minIndex])minIndex=i;if(pixels[i]>pixels[maxIndex])maxIndex=i;}
  const float low=pixels[minIndex],high=pixels[maxIndex];
  const float mapLow=customMapping?limits.lower:low,mapHigh=customMapping?limits.upper:high;
  auto putPixel=[](int x,int y,uint16_t level){imageBuffer[256*4+y*236+x]=levelIndex[level];};
  if(interpolationMode==4)tinyResampler.render(filteredPixels,mapLow,mapHigh,putPixel);else if(interpolationMode==0)resampler.renderNearest(filteredPixels,mapLow,mapHigh,putPixel);else if(interpolationMode==3)resampler.renderLanczos(filteredPixels,mapLow,mapHigh,putPixel);else resampler.render(filteredPixels,mapLow,mapHigh,interpolationMode==2,putPixel);
  lv_obj_invalidate(canvas);char text[24];snprintf(text,sizeof(text),"MAX %.1f",high);setText(maxLabel,text);snprintf(text,sizeof(text),"MIN %.1f",low);setText(minLabel,text);
  snprintf(text,sizeof(text),"%.1f",pixels[ThermalLayout::sample(pointX,pointY)]);setText(pointLabel,text);
  const int xx[3]={pointX,(int)std::lround(hotTracker.x()*235/31),(int)std::lround(coldTracker.x()*235/31)},yy[3]={pointY,235-(int)std::lround(hotTracker.y()*235/31),235-(int)std::lround(coldTracker.y()*235/31)};
  ThermalLayout::Rect band={0,0,236,22};const auto pointBox=ThermalLayout::place(pointX,pointY,60,17,&band,1);lv_obj_set_pos(pointLabel,pointBox.x,pointBox.y);
  lv_obj_set_pos(pointCross,constrain(pointX-4,0,227),constrain(pointY-6,0,222));
  moveMarker(maxCross,constrain(xx[1]-5,0,225),constrain(yy[1],0,226));moveMarker(minCross,constrain(xx[2]-5,0,225),constrain(yy[2]-9,0,226));
  renderUs=micros()-start;
}
static void updateBattery(uint32_t now){
  static int displayed=-1;static bool visible=true;
  if(now-lastBattery>=1000){lastBattery=now;float voltage=analogRead(26)/4095.0f*3.3f*(980.0f/680.0f);batteryPercent=constrain((int)((voltage-3.3f)/.9f*100),0,100);}
  const BatteryAppearance appearance=batteryAppearance(batteryPercent);
  if(displayed!=batteryPercent){displayed=batteryPercent;lv_obj_set_style_bg_color(batteryHandle,lv_color_hex(appearance.color),0);}
  const bool nextVisible=!appearance.blink||(now/500)%2==0;
  if(nextVisible!=visible){visible=nextVisible;lv_obj_set_style_bg_opa(batteryHandle,visible?LV_OPA_COVER:LV_OPA_20,0);}
}
static void togglePause(){
  paused=!paused;++freezeCount;
  lv_obj_set_style_bg_color(pauseLabel,lv_color_hex(paused?0x4DE38A:0x25303A),0);if(!paused)recolor=true;
}
static void pollButton(uint32_t now){
  switch(buttonGestures.update(digitalRead(24)==LOW,now)){
    case 1:togglePause();break;
    case 2:
      screenLocked=!screenLocked;touchDown=false;bottomSwipe.end();
      if(screenLocked)closeDialog();lv_obj_set_style_bg_color(lockLabel,lv_color_hex(screenLocked?0xFFE066:0x25303A),0);
      break;
    case 3:paletteMode=paletteToggle.toggle(paletteMode);rebuildColors();closeDialog();break;
  }
}
// USB网页协议：仅在电脑显式开启时输出原始矩阵；不修改采样和测温值。
static void usbState(){
  Serial.printf("#STATE {\"protocol\":1,\"cpu\":%lu,\"seq\":%lu,\"sensor\":%d,\"lower\":%d,\"upper\":%d,\"custom\":%d,\"palette\":%d,\"filter\":%d,\"interpolation\":%d,\"emissivity\":%d,\"brightness\":%d,\"paused\":%d,\"locked\":%d,\"battery\":%d,\"touch_error\":%u,\"render_us\":%lu,\"mirror\":%d,\"activation\":%d,\"capture_us\":%lu,\"mirror_bytes\":%lu,\"filter_us\":%lu,\"processing_us\":%lu,\"tiny_model\":1,\"enhancement_modes\":1,\"heat_size\":236,\"layout\":4,\"tracking\":2}\n",
    (unsigned long)(clock_get_hz(clk_sys)/1000000),(unsigned long)sharedSequence,(int)sensorState,limits.lower,limits.upper,customMapping,paletteMode,filterMode,interpolationMode,(int)actualEmissivity,brightness,paused,screenLocked,batteryPercent,touch.lastError,(unsigned long)renderUs,usbMirror,captureRangeOnEnable,(unsigned long)lastCaptureUs,(unsigned long)mirrorBytes,(unsigned long)filterUs,(unsigned long)processingUs);
}
static void usbExecute(){
  unsigned long id=0;char name[20]={};int a=0,b=0;bool ok=false;
  const int count=sscanf(usbCommand,"%lu %19s %d %d",&id,name,&a,&b);
  if(count>=3){
    if(!strcmp(name,"stream")&&(a==0||a==1)){usbStreaming=a;ok=true;}
    else if(!strcmp(name,"mirror")&&(a==0||a==1)){usbMirror=a;ok=true;}
    else if(!strcmp(name,"range")&&count==4&&a>=-30&&b<=300&&a<=b){limits.lower=a;limits.upper=b;manualLimits=limits;updateLimitLabels();ok=true;}
    else if(!strcmp(name,"custom")&&(a==0||a==1)){ok=setCustomMapping(a);}
    else if(!strcmp(name,"activation")&&(a==0||a==1)){captureRangeOnEnable=a;ok=true;}
    else if(!strcmp(name,"match")&&(a==0||a==1||a==2)){int lo,hi;if(currentRange(lo,hi)){if(a==0)limits.lower=min(lo,limits.upper);if(a==1)limits.upper=max(hi,limits.lower);if(a==2){limits.lower=lo;limits.upper=hi;}manualLimits=limits;updateLimitLabels();ok=true;}}
    else if(!strcmp(name,"palette")&&a>=0&&a<7){paletteMode=paletteToggle.select(paletteMode,a);rebuildColors();ok=true;}
    else if(!strcmp(name,"filter")&&a>=0&&a<4){filterMode=a;displayFilter.apply(pixels,filteredPixels,filterMode);recolor=true;ok=true;}
    else if(!strcmp(name,"interpolation")&&a>=0&&a<5){interpolationMode=a;recolor=true;ok=true;}
    else if(!strcmp(name,"emissivity")&&a>=10&&a<=100){desiredEmissivity=a;ok=true;}
    else if(!strcmp(name,"brightness")&&a>=5&&a<=100){brightness=a;analogWrite(4,a*255/100);ok=true;}
    else if(!strcmp(name,"paused")&&(a==0||a==1)){if(paused!=bool(a))togglePause();ok=true;}
    else if(!strcmp(name,"locked")&&(a==0||a==1)){screenLocked=a;touchDown=false;bottomSwipe.end();if(a)closeDialog();lv_obj_set_style_bg_color(lockLabel,lv_color_hex(a?0xFFE066:0x25303A),0);ok=true;}
    else if(!strcmp(name,"point")&&count==4&&a>=0&&a<32&&b>=0&&b<32){pointX=ThermalLayout::xOf(b*32+a);pointY=ThermalLayout::yOf(b*32+a);recolor=true;ok=true;}
    else if(!strcmp(name,"state")){ok=true;}
  }
  Serial.printf("#ACK %lu %d\n",id,ok);usbState();
}
static void usbPublish(uint32_t now){
  if(!Serial){usbStreaming=false;usbMirror=false;return;}
  if(usbStreaming){
    uint32_t sequence;mutex_enter_blocking(&frameMutex);sequence=sharedSequence;
    if(sequence!=usbSequence)memcpy(usbPixels,sharedPixels,sizeof(usbPixels));mutex_exit(&frameMutex);
    if(sequence!=usbSequence){usbSequence=sequence;Serial.printf("#FRAME %lu %lu 4096\n",(unsigned long)sequence,(unsigned long)now);Serial.write((uint8_t*)usbPixels,sizeof(usbPixels));Serial.write('\n');}
  }
  if(usbMirror&&now-lastMirror>=500){lastMirror=now;capturePending=true;}
  if(usbStreaming&&now-lastUsbState>=1000){lastUsbState=now;usbState();}
}

void setup(){
  Serial.begin(115200);mutex_init(&frameMutex);pinMode(4,OUTPUT);digitalWrite(4,LOW);pinMode(24,INPUT_PULLUP);
  pinMode(5,OUTPUT);digitalWrite(5,LOW);analogWrite(4,brightness*255/100);analogReadResolution(12);pinMode(26,INPUT);
  tft.begin();tft.setRotation(1);tft.fillScreen(TFT_BLACK);delay(100);touch.begin();lv_init();lv_disp_draw_buf_init(&displayDrawBuffer,displayBuffer,nullptr,280*12);
  static lv_disp_drv_t display;lv_disp_drv_init(&display);display.hor_res=280;display.ver_res=240;display.flush_cb=flushDisplay;display.draw_buf=&displayDrawBuffer;lv_disp_drv_register(&display);
  static lv_indev_drv_t input;lv_indev_drv_init(&input);input.type=LV_INDEV_TYPE_POINTER;input.read_cb=readTouch;lv_indev_drv_register(&input);
  createUI();rebuildColors();lastTick=millis();__dmb();ready=true;
}
void loop(){
  const uint32_t start=micros(),now=millis();
  lv_tick_inc(now-lastTick);lastTick=now;pollButton(now);
  // 输入和控件动画优先；设置/滑块打开时跳过被遮挡的热图渲染。
  lv_timer_handler();if(openSettingsRequested){openSettingsRequested=false;openSettings();}
  bool newFrame=false;mutex_enter_blocking(&frameMutex);if(!paused&&sharedSequence!=shownSequence){memcpy(pixels,sharedPixels,sizeof(pixels));shownSequence=sharedSequence;newFrame=true;}mutex_exit(&frameMutex);
  if(newFrame){const uint32_t filterStart=micros();const float interval=receivedAt?(now-receivedAt)/1000.0f:.133333333f;displayFilter.apply(pixels,filteredPixels,filterMode,interval);hotTracker.update(filteredPixels,interval);coldTracker.update(filteredPixels,interval);filterUs=micros()-filterStart;receivedAt=now;recolor=true;}
  const bool valid=shownSequence&&(paused||now-receivedAt<2000);
  if((int)valid!=lastValidState){lastValidState=valid;lv_obj_t *tags[]={maxCross,minCross,pointCross,maxLabel,minLabel};for(auto *tag:tags){if(valid)lv_obj_clear_flag(tag,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(tag,LV_OBJ_FLAG_HIDDEN);}if(valid){lv_obj_add_flag(messageLabel,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(pointLabel,LV_OBJ_FLAG_HIDDEN);}else{lv_obj_clear_flag(messageLabel,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(pointLabel,LV_OBJ_FLAG_HIDDEN);setText(messageLabel,sensorState<0?"探头无数据":"等待数据");setText(maxLabel,"--");setText(minLabel,"--");}}
  if(!dialog&&recolor&&valid&&now-lastPaint>=30){drawThermal();processingUs=filterUs+renderUs;lastPaint=now;recolor=false;}
  updateBattery(now);
  for(int budget=0;Serial.available()&&budget<100;++budget){
    const char command=Serial.read();
    if(command=='@'){usbCommandMode=true;usbCommandLength=0;continue;}
    if(usbCommandMode){
      if(command=='\n'){usbCommand[usbCommandLength]=0;usbExecute();usbCommandMode=false;}
      else if(command!='\r'){if(usbCommandLength<sizeof(usbCommand)-1)usbCommand[usbCommandLength++]=command;else{usbCommandMode=false;usbCommandLength=0;}}
      continue;
    }
    if(command=='S')capturePending=true;
    if(command=='F')togglePause();
    if(command=='D'){
      for(int pin=0;pin<30;++pin)Serial.printf("GPIO %d function=%u level=%u\n",pin,gpio_get_function(pin),gpio_get(pin));
      Serial.printf("I2C1 status=%lx enable=%lx abort=%lx\n",(unsigned long)i2c_get_hw(i2c1)->status,(unsigned long)i2c_get_hw(i2c1)->enable,(unsigned long)i2c_get_hw(i2c1)->tx_abrt_source);
    }
    if(command=='T'){
      // 仅恢复已知触摸总线：开漏释放和 9 个时钟，避免复位整机。
      Wire1.end();pinMode(2,INPUT_PULLUP);pinMode(3,INPUT_PULLUP);
      for(int n=0;n<9;++n){digitalWrite(3,LOW);pinMode(3,OUTPUT);delayMicroseconds(10);pinMode(3,INPUT_PULLUP);delayMicroseconds(10);}
      digitalWrite(2,LOW);pinMode(2,OUTPUT);delayMicroseconds(10);pinMode(3,INPUT_PULLUP);delayMicroseconds(10);pinMode(2,INPUT_PULLUP);delay(5);
      touch.begin();
    }
    if(command=='U')openSettings();
    if(command=='P')openPalettes();
    if(command=='L')openPicker(true);
    if(command=='R')openPicker(false);
    if(command=='X')closeDialog();
  }
  usbPublish(now);
  if(capturePending&&!capturing&&Serial){capturePending=false;capturing=true;mirrorBytes=0;uint32_t captureStart=micros();lv_obj_invalidate(lv_scr_act());lv_refr_now(nullptr);lastCaptureUs=micros()-captureStart;}
  maxLoopUs=max(maxLoopUs,micros()-start);
  if(Serial&&!capturing&&now-lastDiagnostic>=1000){
    lastDiagnostic=now;lv_mem_monitor_t memory;lv_mem_monitor(&memory);
    Serial.printf("THERMAL_V2 seq=%lu sensor=%d valid=%d custom=%d range=%d,%d filter=%d interp=%d eps=%d page=%d render_us=%lu flush_us=%lu loop_max_us=%lu touch_max_us=%lu badtouch=%lu ui_free=%lu\n",(unsigned long)shownSequence,(int)sensorState,valid,customMapping,limits.lower,limits.upper,filterMode,interpolationMode,(int)actualEmissivity,dialog?(settingsOpen?2:1):0,(unsigned long)renderUs,(unsigned long)flushUs,(unsigned long)maxLoopUs,(unsigned long)maxTouchUs,(unsigned long)invalidTouches,(unsigned long)memory.free_size);
    Serial.printf("V2_TOUCH cpu=%lu id=%02X err=%u bytes=%u good=%lu press=%lu raw=%u,%u pal=%d paused=%d freezes=%lu btn=%d locked=%d flashdiv=%lu\n",(unsigned long)(clock_get_hz(clk_sys)/1000000),touch.chipId,touch.lastError,touch.lastRead,(unsigned long)touch.goodReads,(unsigned long)touch.pressReads,touch.rawX,touch.rawY,paletteMode,paused,(unsigned long)freezeCount,digitalRead(24),screenLocked,(unsigned long)ssi_hw->baudr);
    flushUs=0;maxLoopUs=0;maxTouchUs=0;invalidTouches=0;
  }
  delay(1);
}
void setup1(){while(!ready)delay(1);sensorState=2;int result=thermalSensorBegin();sensorState=result==0?1:result;if(result==0){actualEmissivity=thermalSensorEmissivity();desiredEmissivity=actualEmissivity;}}
void loop1(){
  
  if(sensorState<=-10&&sensorState>=-13){delay(1000);return;}
  if(desiredEmissivity!=actualEmissivity&&thermalSensorSetEmissivity(desiredEmissivity))actualEmissivity=desiredEmissivity;
  static float working[1024];int result=thermalSensorRead(working);if(result<0){sensorState=result;delay(20);return;}if(!result)return;
  mutex_enter_blocking(&frameMutex);memcpy(sharedPixels,working,sizeof(working));++sharedSequence;mutex_exit(&frameMutex);sensorState=1;
}

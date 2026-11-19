#include "focus_view.h"
#include "focus_art.h"
#include "garden_art.h"
#include <Arduino.h>
namespace focusview {
namespace {
const lv_color_t INK=lv_color_hex(0x502C24), PEACH=lv_color_hex(0xDBA38C), PINK=lv_color_hex(0xF49CA9);
const lv_color_t CREAM=lv_color_hex(0xFFF0DE), MUTED=lv_color_hex(0xA5786E);
uint8_t *pixels=nullptr;
bool gardenPixels=false;
lv_image_dsc_t image={};
lv_obj_t *digits=nullptr,*clockLabel=nullptr,*dateLabel=nullptr,*ring=nullptr,*choices[3]={};
unsigned selected=25;
Callback onAction=nullptr;

lv_obj_t *text(lv_obj_t *p,const char *value,int x,int y,int w,const lv_font_t *font,lv_color_t color) {
  auto *l=lv_label_create(p);lv_obj_set_pos(l,x,y);lv_obj_set_width(l,w);
  lv_obj_set_style_text_font(l,font,0);lv_obj_set_style_text_color(l,color,0);
  lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
  lv_label_set_long_mode(l,LV_LABEL_LONG_CLIP);lv_label_set_text(l,value);return l;
}
lv_obj_t *button(lv_obj_t *p,const char *value,int x,int y,int w,int h,bool primary,lv_event_cb_t cb,uintptr_t data) {
  auto *b=lv_button_create(p);lv_obj_remove_style_all(b);
  lv_obj_remove_flag(b,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);
  lv_obj_set_style_bg_opa(b,LV_OPA_COVER,0);lv_obj_set_style_bg_color(b,primary ? PINK : CREAM,0);
  lv_obj_set_style_bg_color(b,lv_color_hex(0xF0B2B0),LV_STATE_PRESSED);
  lv_obj_set_style_radius(b,18,0);lv_obj_set_style_border_width(b,2,0);lv_obj_set_style_border_color(b,PEACH,0);
  auto *l=text(b,value,0,0,w,&lv_font_montserrat_14,INK);lv_obj_center(l);
  lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,(void *)data);return b;
}
void action(lv_event_t *e) {if(onAction) onAction((Action)(uintptr_t)lv_event_get_user_data(e),selected);}
void paintChoices() {
  const unsigned values[]={15,25,45};
  for(int i=0;i<3;++i) if(choices[i]) {
    bool chosen=values[i]==selected;
    lv_obj_set_style_bg_color(choices[i],chosen ? PINK : CREAM,0);
    lv_obj_set_style_border_color(choices[i],chosen ? lv_color_hex(0xBD626E) : PEACH,0);
  }
}
}
void clear(bool releaseArtwork){if(releaseArtwork){free(pixels);pixels=nullptr;gardenPixels=false;}digits=clockLabel=dateLabel=ring=nullptr;for(auto &p:choices)p=nullptr;onAction=nullptr;}
bool active(){return clockLabel!=nullptr;}
uint32_t artworkBytes(){return pixels ? 153600 : 0;}
void build(lv_obj_t *p,const focus::Timer &timer,Callback callback,bool garden,unsigned sessions) {
  if (garden) {
    onAction=callback;
    if(pixels && !gardenPixels) { free(pixels); pixels=nullptr; }
    if(!pixels) { pixels=gardenart::load(sessions); gardenPixels=true; }
    if(pixels) {
      image={};image.header.magic=LV_IMAGE_HEADER_MAGIC;image.header.cf=LV_COLOR_FORMAT_RGB565;
      image.header.w=320;image.header.h=240;image.header.stride=640;image.data_size=153600;image.data=pixels;
      auto *bg=lv_image_create(p);lv_image_set_src(bg,&image);lv_obj_set_pos(bg,0,0);
    } else { lv_obj_set_style_bg_color(p,lv_color_hex(0x76547B),0);lv_obj_set_style_bg_opa(p,LV_OPA_COVER,0); }
    const lv_color_t light=lv_color_hex(0xFFF0D8);
    text(p,"Your little garden",0,10,320,&lv_font_montserrat_20,light);
    char count[48]; snprintf(count,sizeof count,"%u focus sessions in 7 days",sessions);
    text(p,count,0,36,320,&lv_font_montserrat_14,light);
    button(p,"Back",105,198,110,32,false,action,(uintptr_t)Action::CloseGarden);
    return;
  }
  if(pixels && gardenPixels) { free(pixels); pixels=nullptr; gardenPixels=false; }
  onAction=callback;
  if(!pixels) pixels=focusart::load();
  if(pixels) {
    image={};image.header.magic=LV_IMAGE_HEADER_MAGIC;image.header.cf=LV_COLOR_FORMAT_RGB565;
    image.header.w=320;image.header.h=240;image.header.stride=640;image.data_size=153600;image.data=pixels;
    auto *bg=lv_image_create(p);lv_image_set_src(bg,&image);lv_obj_set_pos(bg,0,0);
  } else {
    // Asset/card failure keeps every timer control available.
    lv_obj_set_style_bg_color(p,lv_color_hex(0xD6BCD7),0);
    auto *paper=lv_obj_create(p);lv_obj_remove_style_all(paper);lv_obj_set_pos(paper,124,14);lv_obj_set_size(paper,184,212);
    lv_obj_set_style_bg_color(paper,CREAM,0);lv_obj_set_style_bg_opa(paper,LV_OPA_COVER,0);lv_obj_set_style_radius(paper,20,0);
    lv_obj_remove_flag(paper,LV_OBJ_FLAG_SCROLLABLE);
    text(p,"A little\nfocus",10,119,104,&lv_font_montserrat_20,INK);
  }
  auto *badge=lv_obj_create(p);lv_obj_remove_style_all(badge);lv_obj_set_pos(badge,10,12);lv_obj_set_size(badge,102,54);
  lv_obj_set_style_bg_color(badge,CREAM,0);lv_obj_set_style_bg_opa(badge,LV_OPA_90,0);lv_obj_set_style_radius(badge,16,0);
  lv_obj_remove_flag(badge,LV_OBJ_FLAG_SCROLLABLE);
  clockLabel=text(badge,"--:--",0,2,102,&lv_font_montserrat_28,INK);
  dateLabel=text(badge,"",0,34,102,&lv_font_montserrat_14,MUTED);
  text(p,timer.state==focus::State::Break ? "Take a breather" : "Study Mode",128,23,177,&lv_font_montserrat_20,INK);
  if(timer.state==focus::State::Idle) {
    button(p,"Garden",10,194,102,30,false,action,(uintptr_t)Action::Garden);
    text(p,"A little time for you",130,55,174,&lv_font_montserrat_14,MUTED);
    digits=text(p,"",129,84,176,&lv_font_montserrat_48,INK);
    const char *names[]={"15 min","25 min","45 min"};const unsigned values[]={15,25,45};
    for(int i=0;i<3;++i) choices[i]=button(p,names[i],131+i*57,143,52,36,false,[](lv_event_t *e){selected=(uintptr_t)lv_event_get_user_data(e);paintChoices();},values[i]);
    paintChoices();button(p,"Start studying",136,188,164,32,true,action,(uintptr_t)Action::Start);
  } else if(timer.running()) {
    ring=lv_arc_create(p);lv_obj_remove_flag(ring,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(ring,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ring,168,168);lv_obj_set_pos(ring,133,57);lv_arc_set_bg_angles(ring,0,360);lv_arc_set_rotation(ring,270);
    lv_arc_set_range(ring,0,1000);lv_obj_remove_style(ring,nullptr,LV_PART_KNOB);
    lv_obj_set_style_arc_width(ring,2,LV_PART_MAIN);lv_obj_set_style_arc_width(ring,3,LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring,lv_color_hex(0xEED5BF),LV_PART_MAIN);lv_obj_set_style_arc_color(ring,lv_color_hex(0xCE7383),LV_PART_INDICATOR);
    text(p,timer.quiet() ? "ONE LITTLE STEP" : "REST A LITTLE",150,79,134,&lv_font_montserrat_14,MUTED);
    digits=text(p,"",130,105,175,&lv_font_montserrat_48,INK);
    text(p,timer.quiet() ? "You have got this" : "Breathe easy",150,154,134,&lv_font_montserrat_14,MUTED);
    button(p,"End",176,181,82,30,false,action,(uintptr_t)Action::End);
  } else {
    text(p,timer.state==focus::State::Complete ? "Nicely done!" : "Break finished",131,66,173,&lv_font_montserrat_20,INK);
    text(p,"A little progress.",132,99,170,&lv_font_montserrat_14,MUTED);
    if(timer.state==focus::State::Complete) button(p,"Take 5 min",143,131,148,36,true,action,(uintptr_t)Action::TakeBreak);
    button(p,"Done",143,179,148,34,false,action,(uintptr_t)Action::Done);
  }
  tick(timer,millis());
}
void tick(const focus::Timer &timer,uint32_t now) {
  if(!active())return;
  time_t epoch=time(nullptr);struct tm t;localtime_r(&epoch,&t);
  char clock[8]="--:--",date[16]="";
  if(epoch>1700000000){strftime(clock,sizeof clock,"%H:%M",&t);strftime(date,sizeof date,"%a, %d %b",&t);}
  if(strcmp(lv_label_get_text(clockLabel),clock))lv_label_set_text(clockLabel,clock);
  if(strcmp(lv_label_get_text(dateLabel),date))lv_label_set_text(dateLabel,date);
  if(digits) {
    uint32_t seconds=timer.state==focus::State::Idle ? selected*60 : (timer.remaining(now)+999)/1000;
    char value[12];snprintf(value,sizeof value,"%02u:%02u",(unsigned)(seconds/60),(unsigned)(seconds%60));
    if(strcmp(lv_label_get_text(digits),value))lv_label_set_text(digits,value);
  }
  if(ring && timer.duration)lv_arc_set_value(ring,(uint64_t)timer.remaining(now)*1000/timer.duration);
}
}

#!/usr/bin/env python3
"""Render/test the real LVGL focus screen on the host. Requires Pillow + LVGL."""
import ast
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
subprocess.run(['python3',str(ROOT/'tools/check_mode_pager.py')],check=True)
tree=ast.parse((ROOT/'tools/check_mode_pager.py').read_text())
config=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='config' for t in n.targets))
cache=Path(tempfile.gettempdir())/('bhoomi-pager-host-'+hashlib.sha256(config.encode()).hexdigest()[:10])
lvgl=Path(os.environ.get('LVGL_ROOT',Path.home()/'Documents/Arduino/libraries/lvgl'))
work=cache/'focus';work.mkdir(exist_ok=True)
(work/'Arduino.h').write_text('#pragma once\n#include <stdint.h>\n#include <stdlib.h>\n#include <stdio.h>\n#include <string.h>\nuint32_t millis();\n')
flags=['-O0','-DLV_FONT_MONTSERRAT_20=1','-DLV_FONT_MONTSERRAT_48=1','-I'+str(lvgl),'-I'+str(work),'-I'+str(ROOT/'firmware/BhoomiClock'),'-DLV_CONF_PATH='+str(cache/'lv_conf.h')]
objs=[o for o in (cache/'objects').glob('*.o') if o.name not in ['src_font_lv_font_montserrat_20.c.o','src_font_lv_font_montserrat_48.c.o']]
for size in [20,48]:
    obj=work/f'font{size}.o'
    subprocess.run(['cc','-std=c99',*flags,f'-DLV_FONT_MONTSERRAT_{size}=1','-c',str(lvgl/f'src/font/lv_font_montserrat_{size}.c'),'-o',str(obj)],check=True)
    objs.append(obj)
art=Image.open(ROOT/'assets/focus-study/sd/ui/focus-study-v1.jpg').convert('RGB')
raw=bytearray()
for r,g,b in art.get_flattened_data():
    v=((r>>3)<<11)|((g>>2)<<5)|(b>>3);raw.extend(v.to_bytes(2,'little'))
(work/'art.rgb565').write_bytes(raw)
ui=(ROOT/'firmware/BhoomiClock/ui.cpp').read_text()
pager=ui[ui.index('void setPagerContentVisible(bool visible) {'):ui.index('\n}  // namespace\n\nvoid begin')]
cpp=r'''
#include <lvgl.h>
#include "focus_view.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <chrono>
#define constrain(x,a,b) std::max(a,std::min(b,x))
namespace display {constexpr int WIDTH=320,HEIGHT=240; void beginScrollMetrics(){} void endScrollMetrics(){}}
enum class Mode {Clock,Focus,Night};Mode mode=Mode::Focus;int requested=-1;
lv_obj_t *scr,*pager,*pages[3],*pageTitles[3];
lv_obj_t *clockView=nullptr,*photoView=nullptr,*noteButton=nullptr,*modeRoot=nullptr;
bool pagerMoving=false;uint32_t revealAt=0,pagerRecoveries=0;
namespace faceview {lv_obj_t *root(){return nullptr;}}
const lv_color_t BG=lv_color_hex(0xFBF7F2),INK=lv_color_hex(0x2B2420);
void requestMode(Mode m){requested=(int)m;}
lv_obj_t *label(lv_obj_t *p,const lv_font_t *f,lv_color_t c){auto *l=lv_label_create(p);lv_obj_set_style_text_font(l,f,0);lv_obj_set_style_text_color(l,c,0);return l;}
void updatePagerMotion();
static uint32_t ticks=0;uint32_t millis(){return ticks;}
unsigned artworkLoads=0;
namespace focusart {uint8_t *load(){++artworkLoads;auto *px=(uint8_t *)malloc(153600);FILE *f=fopen("art.rgb565","rb");assert(f);assert(fread(px,1,153600,f)==153600);fclose(f);return px;}}
static uint16_t framebuffer[320*240];
static lv_point_t point={160,90};static bool down=false;
void readPointer(lv_indev_t *,lv_indev_data_t *d){d->point=point;d->state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;}
void pump(unsigned n=1){while(n--){ticks+=16;updatePagerMotion();lv_tick_inc(16);lv_timer_handler();}}
void drag(int from,int to){point={from,203};down=true;pump(3);int sign=to>from?1:-1;for(int x=from;sign*(to-x)>0;x+=sign*8){point.x=x;pump(2);}point.x=to;pump(2);down=false;pump(70);}
void tap(int x,int y){point={x,y};down=true;pump(3);down=false;pump(3);}
focus::Timer timer;unsigned actions=0;
void action(focusview::Action a,unsigned minutes){++actions;if(a==focusview::Action::Start)timer.start(millis(),minutes);else if(a==focusview::Action::End)timer.done();else if(a==focusview::Action::TakeBreak)timer.takeBreak(millis());else timer.done();}
lv_obj_t *content=nullptr;
void build(bool keep=false){if(content)lv_obj_delete(content);focusview::clear(!keep);content=lv_obj_create(pages[1]);lv_obj_remove_style_all(content);lv_obj_set_size(content,320,240);lv_obj_remove_flag(content,LV_OBJ_FLAG_SCROLLABLE);focusview::build(content,timer,action);modeRoot=content;pump(5);}
void save(const char *path){focusview::tick(timer,millis());pump(5);FILE *f=fopen(path,"wb");assert(f);fwrite(framebuffer,1,sizeof framebuffer,f);fclose(f);}
'''
main=r'''
int main(){
 setenv("TZ","IST-5:30",1);tzset();lv_init();auto *d=lv_display_create(320,240);
 alignas(64) static uint8_t buf[320*32*2];lv_display_set_buffers(d,buf,nullptr,sizeof buf,LV_DISPLAY_RENDER_MODE_PARTIAL);
 lv_display_set_flush_cb(d,[](lv_display_t *d,const lv_area_t *a,uint8_t *p){auto *px=(uint16_t *)p;for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x)framebuffer[y*320+x]=*px++;lv_display_flush_ready(d);});
 scr=lv_screen_active();lv_obj_remove_flag(scr,LV_OBJ_FLAG_SCROLLABLE);buildPager();lv_obj_scroll_to_x(pager,320,LV_ANIM_OFF);
 auto *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,readPointer);build();save("idle.rgb565");
 auto redrawCost=[](){auto start=std::chrono::steady_clock::now();for(int i=0;i<30;++i){lv_obj_invalidate(pager);pump(2);}return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();};
 auto fullCost=redrawCost();setPagerContentVisible(false);auto titleCost=redrawCost();
 assert(lv_obj_get_style_opa(modeRoot,0)==LV_OPA_TRANSP);
 setPagerContentVisible(true);pump(3);
 printf("Software redraw benchmark (30 frames): illustration %lld us, titles %lld us\n",(long long)fullCost,(long long)titleCost);

 drag(280,40);assert(requested==2);assert(actions==0);mode=Mode::Night;requested=-1;
 drag(40,280);assert(requested==1);mode=Mode::Focus;requested=-1;setPagerContentVisible(true);
 drag(200,176);assert(!pagerMoving && requested==-1);assert(lv_obj_get_style_opa(modeRoot,0)==LV_OPA_COVER);
 tap(153,161);assert(actions==0);focusview::tick(timer,millis());save("selected.rgb565");
 tap(216,203);assert(actions==1 && timer.quiet() && timer.duration==900000);build(true);assert(artworkLoads==1);save("active.rgb565");
 drag(40,280);assert(requested==0 && timer.quiet());mode=Mode::Clock;requested=-1;
 drag(280,40);assert(requested==1);mode=Mode::Focus;requested=-1;setPagerContentVisible(true);
 timer.tick(timer.started+timer.duration);build(true);assert(artworkLoads==1);save("complete.rgb565");
 tap(216,148);assert(timer.state==focus::State::Break);build(true);assert(artworkLoads==1);save("break.rgb565");
 tap(215,195);assert(timer.state==focus::State::Idle);build(true);assert(artworkLoads==1);
 puts("PASS: illustrated screen scrolls/cancels; Start/End/break work; same-page rebuilds reuse artwork");
 lv_obj_delete(content);content=nullptr;modeRoot=nullptr;focusview::clear();pump(5);
 lv_mem_monitor_t baseline={};lv_mem_monitor(&baseline);
 for(unsigned i=0;i<200;++i){
   timer.done();build();timer.start(millis(),15);build(true);
   timer.tick(timer.started+timer.duration);build(true);
   lv_obj_delete(content);content=nullptr;modeRoot=nullptr;focusview::clear();pump(5);
 }
 lv_mem_monitor_t finalMemory={};lv_mem_monitor(&finalMemory);
 printf("Focus UI soak: LVGL free bytes before %u, after %u\n",(unsigned)baseline.free_size,(unsigned)finalMemory.free_size);
 assert(finalMemory.free_size==baseline.free_size);

}
'''
(work/'check.cpp').write_text(cpp+pager+main)
subprocess.run(['c++','-std=c++17',*flags,str(work/'check.cpp'),str(ROOT/'firmware/BhoomiClock/focus_view.cpp'),*[str(p) for p in objs],'-o',str(work/'check')],check=True)
subprocess.run([str(work/'check')],cwd=work,check=True,timeout=60)
out=ROOT/'assets/focus-study/previews';out.mkdir(exist_ok=True)
for state in ['idle','selected','active','complete','break']:
    data=(work/f'{state}.rgb565').read_bytes();rgb=bytearray()
    for at in range(0,len(data),2):
        v=int.from_bytes(data[at:at+2],'little');rgb.extend((((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31))
    Image.frombytes('RGB',(320,240),bytes(rgb)).save(out/f'{state}.png')
print(out)

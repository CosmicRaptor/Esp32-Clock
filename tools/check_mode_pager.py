#!/usr/bin/env python3
"""Exercise firmware's actual pager with LVGL pointer drags, without a display."""
import concurrent.futures
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
lvgl = Path(os.environ.get('LVGL_ROOT', Path.home()/'Documents/Arduino/libraries/lvgl'))
config = '''#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_MEM_SIZE (2 * 1024 * 1024U)
#define LV_FONT_MONTSERRAT_28 1
#include <stdio.h>
#include <stdlib.h>
#define LV_USE_LOG 1
#define LV_LOG_PRINTF 1
#define LV_ASSERT_HANDLER abort();
#endif
'''
cache = Path(tempfile.gettempdir())/('bhoomi-pager-host-' + hashlib.sha256(config.encode()).hexdigest()[:10])
cache.mkdir(exist_ok=True)
(cache/'lv_conf.h').write_text(config)
objects = cache/'objects'; objects.mkdir(exist_ok=True)
flags = ['-O0', '-I'+str(lvgl), '-DLV_CONF_PATH='+str(cache/'lv_conf.h')]
def compile_source(source):
    obj = objects/('_'.join(source.relative_to(lvgl).parts)+'.o')
    if not obj.exists() or source.stat().st_mtime > obj.stat().st_mtime:
        subprocess.run(['cc', '-std=c99', *flags, '-c', str(source), '-o', str(obj)], check=True, capture_output=True)
    return obj
try:
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        objs = list(pool.map(compile_source, sorted((lvgl/'src').rglob('*.c'))))
except subprocess.CalledProcessError as error:
    sys.stderr.write(error.stderr.decode()); raise
source = (root/'firmware/BhoomiClock/ui.cpp').read_text()
pager = source[source.index('void buildPager() {'):source.index('\n}  // namespace\n\nvoid begin')]
harness = r'''
#include <lvgl.h>
#include <cassert>
#include <cstdio>
#include <algorithm>
#define constrain(x,a,b) std::max(a,std::min(b,x))
namespace display {constexpr int WIDTH=320, HEIGHT=240;}
enum class Mode {Clock,Focus,Night};
Mode mode=Mode::Clock;
int requested=-1;
lv_obj_t *scr,*pager,*pages[3],*pageTitles[3];
const lv_color_t BG=lv_color_hex(0xFBF7F2),INK=lv_color_hex(0x2B2420);
void requestMode(Mode m){requested=(int)m;}
lv_obj_t *label(lv_obj_t *p,const lv_font_t *font,lv_color_t col){
 auto *l=lv_label_create(p);lv_obj_set_style_text_font(l,font,0);lv_obj_set_style_text_color(l,col,0);return l;
}
'''
main = r'''
static lv_point_t point={160,90};
static bool down=false;
static int clicks=0;
void readPointer(lv_indev_t *,lv_indev_data_t *d){d->point=point;d->state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;}
void pump(unsigned count=1){while(count--){lv_tick_inc(16);lv_timer_handler();}}
void drag(int from,int to){
 point={from,90};down=true;pump(3);
 int sign=to>from?1:-1;
 for(int x=from;sign*(to-x)>0;x+=sign*8){point.x=x;pump(2);}
 point.x=to;pump(2);down=false;pump(70);
}
int main(){
 lv_init();auto *d=lv_display_create(320,240);
 alignas(64) static unsigned char buf[320*32*2];lv_display_set_buffers(d,buf,nullptr,sizeof buf,LV_DISPLAY_RENDER_MODE_PARTIAL);
 lv_display_set_flush_cb(d,[](lv_display_t *d,const lv_area_t *,uint8_t *){lv_display_flush_ready(d);});
 scr=lv_screen_active();lv_obj_remove_flag(scr,LV_OBJ_FLAG_SCROLLABLE);buildPager();
 auto *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,readPointer);
 // Test through nested content and a duration button, matching the live focus tree.
 for(int i=0;i<3;++i){
   auto *content=lv_obj_create(pages[i]);lv_obj_remove_style_all(content);lv_obj_set_size(content,320,240);
   lv_obj_remove_flag(content,LV_OBJ_FLAG_SCROLLABLE);
   auto *button=lv_button_create(content);lv_obj_set_pos(button,20,60);lv_obj_set_size(button,280,60);
   lv_obj_remove_flag(button,LV_OBJ_FLAG_SCROLLABLE);
   lv_obj_add_event_cb(button,[](lv_event_t *){++clicks;},LV_EVENT_CLICKED,nullptr);
 }
 pump(5);
 drag(280,40);assert(requested==1);assert(abs(lv_obj_get_scroll_x(pager)-320)<=1);mode=Mode::Focus;requested=-1;
 drag(280,40);assert(requested==2);assert(abs(lv_obj_get_scroll_x(pager)-640)<=1);mode=Mode::Night;requested=-1;
 drag(40,280);assert(requested==1);mode=Mode::Focus;requested=-1;
 drag(40,280);assert(requested==0);assert(abs(lv_obj_get_scroll_x(pager))<=1);mode=Mode::Clock;requested=-1;
 drag(40,280);assert(lv_obj_get_scroll_x(pager)==0);assert(requested==-1);
 assert(clicks==0); // a horizontal drag must not start/cancel a timer button
 point={160,90};down=true;pump(3);down=false;pump(3);assert(clicks==1);
 puts("PASS: pointer drags Clock -> Focus -> Night -> Focus -> Clock; page bounds respected");
}
'''
(cache/'pager.cpp').write_text(harness+pager+main)
subprocess.run(['c++','-std=c++17',*flags,str(cache/'pager.cpp'),*[str(o) for o in objs],'-o',str(cache/'check')],check=True)
subprocess.run([str(cache/'check')],check=True,timeout=20)

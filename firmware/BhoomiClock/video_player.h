#pragma once
#include <lvgl.h>
namespace video_player {
void begin();
bool play(unsigned mood); // /mindfulness/<mood>/0000.jpg, 0001.jpg, ... at 5 fps
void tick();
void stop();
bool playing();
}

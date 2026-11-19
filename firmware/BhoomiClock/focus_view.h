#pragma once
#include <lvgl.h>
#include <time.h>
#include "focus_timer.h"
namespace focusview {
enum class Action { Start, End, TakeBreak, Done, Garden, CloseGarden };
using Callback = void (*)(Action, unsigned minutes);
void build(lv_obj_t *parent, const focus::Timer &timer, Callback callback, bool garden = false, unsigned sessions = 0);
void tick(const focus::Timer &timer, uint32_t now);
// Parent owns objects; call after deleting parent so the image no longer references pixels.
void clear(bool releaseArtwork = true);
bool active();
uint32_t artworkBytes();
}

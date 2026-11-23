#pragma once

#include <lvgl.h>

#include "clock.pb.h"

namespace musicview {

using Select = void (*)(int side);

void open(lv_obj_t *parent, int side, Select select);
void close();
bool active();
void update(const bhoomi_clock_v1_Listener *track, const lv_image_dsc_t *art, uint32_t accent, uint32_t sampleAge);
void detachCover(const lv_image_dsc_t *art);  // the caller owns the decoded image

}  // namespace musicview

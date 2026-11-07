// LVGL 9 callback contract: bitmap() must return the supplied A8 draw buffer, not raw bits.
#include "../firmware/BhoomiClock/weather_icons.h"
#include <cassert>
#include <cstring>
#include <cstdio>

extern "C" uint32_t lv_draw_buf_width_to_stride(uint32_t w, lv_color_format_t) {
  return w + 4;  // exercise row padding as well as pixels
}

int main() {
  const uint32_t codes[] = {0x2600,0x263e,0x26c5,0x2601,0x224b,0x3df,0x2744,0x2602};
  for (uint32_t code : codes) {
    lv_font_glyph_dsc_t d;
    memset(&d, 0xff, sizeof d);
    assert(weather_icons.get_glyph_dsc(&weather_icons, &d, code, 0));
    assert(!d.is_placeholder && d.entry == nullptr);
    assert(d.box_w == 16 && d.box_h == 16);
    uint8_t memory[322];
    memset(memory, 0xa5, sizeof memory);
    lv_draw_buf_t buf = {};
    buf.data = memory + 1;
    assert(weather_icons.get_glyph_bitmap(&d, &buf) == &buf);
    assert(memory[0] == 0xa5 && memory[321] == 0xa5);
    int opaque = 0;
    for (int y=0; y<16; ++y) for (int x=0; x<20; ++x) {
      uint8_t v = buf.data[y*20+x];
      assert(v == 0 || v == 255);
      if (x >= 16) assert(v == 0);
      opaque += v == 255;
    }
    assert(opaque > 10 && opaque < 240);
  }
  lv_font_glyph_dsc_t d = {};
  assert(!weather_icons.get_glyph_dsc(&weather_icons, &d, 'A', 0));
  puts("8 weather glyphs: LVGL draw-buffer contract and bounds pass");
}

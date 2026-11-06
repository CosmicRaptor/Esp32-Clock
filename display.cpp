#include "display.h"

#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <lvgl.h>

namespace display {
namespace {

// Pins (Waveshare ESP32-S3-Touch-LCD-2.8)
constexpr int PIN_SCLK = 40;
constexpr int PIN_MOSI = 45;
constexpr int PIN_CS = 42;
constexpr int PIN_DC = 41;
constexpr int PIN_RST = 39;
constexpr int PIN_BL = 5;

constexpr int LCD_HZ = 40 * 1000 * 1000;
constexpr int BUF_LINES = 40;

esp_lcd_panel_handle_t panel = nullptr;
lv_display_t *disp = nullptr;

bool IRAM_ATTR onTransDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *ctx) {
  lv_display_flush_ready((lv_display_t *)ctx);
  return false;
}

void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  // LVGL renders little-endian RGB565; the panel wants big-endian over SPI.
  lv_draw_sw_rgb565_swap(px, lv_area_get_width(a) * lv_area_get_height(a));
  esp_lcd_panel_draw_bitmap(panel, a->x1, a->y1, a->x2 + 1, a->y2 + 1, px);
  // flush_ready is signalled from onTransDone when the DMA finishes.
}

uint32_t tick() { return millis(); }

}  // namespace

void setBacklight(uint8_t percent) {
  ledcWrite(PIN_BL, (uint32_t)min<uint8_t>(percent, 100) * 1023 / 100);
}

void begin() {
  ledcAttach(PIN_BL, 5000, 10);
  setBacklight(0);  // keep dark until the first frame is drawn

  spi_bus_config_t bus = {};
  bus.mosi_io_num = PIN_MOSI;
  bus.miso_io_num = -1;
  bus.sclk_io_num = PIN_SCLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = WIDTH * BUF_LINES * 2;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

  lv_init();
  lv_tick_set_cb(tick);
  disp = lv_display_create(WIDTH, HEIGHT);

  esp_lcd_panel_io_handle_t io = nullptr;
  esp_lcd_panel_io_spi_config_t ioc = {};
  ioc.cs_gpio_num = PIN_CS;
  ioc.dc_gpio_num = PIN_DC;
  ioc.spi_mode = 0;
  ioc.pclk_hz = LCD_HZ;
  ioc.trans_queue_depth = 10;
  ioc.on_color_trans_done = onTransDone;
  ioc.user_ctx = disp;
  ioc.lcd_cmd_bits = 8;
  ioc.lcd_param_bits = 8;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &ioc, &io));

  esp_lcd_panel_dev_config_t pc = {};
  pc.reset_gpio_num = PIN_RST;
  pc.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  pc.bits_per_pixel = 16;
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &pc, &panel));
  esp_lcd_panel_reset(panel);
  esp_lcd_panel_init(panel);
  esp_lcd_panel_invert_color(panel, true);  // IPS panel
  esp_lcd_panel_disp_on_off(panel, true);

  size_t bytes = WIDTH * BUF_LINES * 2;
  void *b1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void *b2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  lv_display_set_buffers(disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flush);
}

void loop() {
  static bool lit = false;
  lv_timer_handler();
  if (!lit) {  // first frame is out: now turn the light on (no flash of garbage at boot)
    lit = true;
    setBacklight(80);
  }
}

}  // namespace display

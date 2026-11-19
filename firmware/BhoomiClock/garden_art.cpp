#include "garden_art.h"
#include "garden_jpg.h"
#include <esp_heap_caps.h>
#include <jpeg_decoder.h>
#include <stdlib.h>

namespace gardenart {
uint8_t *load(unsigned sessions) {
  const uint8_t *images[] = {GARDEN_0,GARDEN_1,GARDEN_2,GARDEN_3,GARDEN_4};
  const size_t sizes[] = {sizeof GARDEN_0,sizeof GARDEN_1,sizeof GARDEN_2,sizeof GARDEN_3,sizeof GARDEN_4};
  unsigned stage=sessions<4 ? sessions : 4;
  esp_jpeg_image_cfg_t cfg={}; cfg.indata=const_cast<uint8_t *>(images[stage]); cfg.indata_size=sizes[stage];
  cfg.out_format=JPEG_IMAGE_FORMAT_RGB565; cfg.out_scale=JPEG_IMAGE_SCALE_0;
  esp_jpeg_image_output_t info={};
  constexpr size_t bytes=320*240*2;
  if(esp_jpeg_get_image_info(&cfg,&info)!=ESP_OK || info.width!=320 || info.height!=240 || info.output_len!=bytes) return nullptr;
  auto *pixels=(uint8_t *)heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!pixels) return nullptr;
  cfg.outbuf=pixels; cfg.outbuf_size=bytes;
  if(esp_jpeg_decode(&cfg,&info)!=ESP_OK) { free(pixels); return nullptr; }
  return pixels;
}
}

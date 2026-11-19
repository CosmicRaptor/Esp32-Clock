#include "debug_log.h"
#include "debug_log_ring.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdarg.h>
#include <new>

namespace debuglog {
namespace {
constexpr size_t CAPACITY=8192, RECORD=384;
using Ring=DebugLogRing<CAPACITY>;
Ring *queue=nullptr;
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;

void enqueue(const char *text,size_t n) {
  if(!queue || !n)return;
  portENTER_CRITICAL(&mux);
  queue->push(text,n);
  portEXIT_CRITICAL(&mux);
}
void task(void *) {
  char text[RECORD];
  for(;;) {
    portENTER_CRITICAL(&mux);
    size_t n=queue->pop(text,sizeof text);
    portEXIT_CRITICAL(&mux);
    // This is the only application debug-output writer. USB can stall this
    // low-priority task; it never owns app::Lock or any producer's queue lock.
    if(n)Serial.write((const uint8_t *)text,n);
    vTaskDelay(n ? 1 : pdMS_TO_TICKS(10));
  }
}
}
bool begin() {
  if(queue)return true;
  void *memory=heap_caps_malloc(sizeof(Ring),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!memory)return false;
  queue=new(memory) Ring;
  if(xTaskCreatePinnedToCore(task,"debug-log",3072,nullptr,0,nullptr,1)!=pdPASS) {
    queue->~Ring();free(queue);queue=nullptr;return false;
  }
  return true;
}
void printf(const char *format,...) {
  char text[RECORD];va_list args;va_start(args,format);
  int result=vsnprintf(text,sizeof text,format,args);va_end(args);
  if(result<=0)return;
  size_t n=(size_t)result<sizeof text ? result : sizeof text-1;
  if((size_t)result>=sizeof text)text[n-1]='\n';
  enqueue(text,n);
}
void print(const char *text) {if(text)enqueue(text,strlen(text));}
void println(const char *text) {printf("%s\n",text ? text : "");}
uint32_t dropped(){if(!queue)return 0;portENTER_CRITICAL(&mux);uint32_t n=queue->dropped;portEXIT_CRITICAL(&mux);return n;}
size_t buffered(){if(!queue)return 0;portENTER_CRITICAL(&mux);size_t n=queue->used;portEXIT_CRITICAL(&mux);return n;}
}

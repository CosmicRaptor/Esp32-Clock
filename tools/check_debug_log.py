#!/usr/bin/env python3
"""Compile the real logger with a stopped output task and a forbidden USB writer."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='clock-log-test-') as tmp:
 p=Path(tmp);(p/'freertos').mkdir()
 (p/'Arduino.h').write_text(r'''
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
struct FakeSerial {size_t write(const uint8_t *,size_t){abort();}};
extern FakeSerial Serial;
''')
 (p/'esp_heap_caps.h').write_text(r'''
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
extern unsigned allocations;
inline void *heap_caps_malloc(size_t n,int){++allocations;return malloc(n);}
''')
 (p/'freertos/FreeRTOS.h').write_text(r'''
#pragma once
#include <assert.h>
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
''')
 (p/'freertos/task.h').write_text(r'''
#pragma once
extern unsigned tasks;
inline int xTaskCreatePinnedToCore(void (*)(void*),const char*,unsigned stack,void*,unsigned priority,void*,int core){assert(stack==3072&&priority==0&&core==1);++tasks;return pdPASS;}
inline void vTaskDelay(unsigned){}
''')
 (p/'check.cpp').write_text(r'''
#include "debug_log.h"
#include "debug_log_ring.h"
#include <Arduino.h>
#include <cassert>
#include <chrono>
FakeSerial Serial;unsigned allocations=0,tasks=0;
int main(){
 DebugLogRing<32> ring;char out[32];
 assert(ring.push("first",5));assert(ring.push("second",6));
 assert(ring.pop(out,sizeof out)==5 && !memcmp(out,"first",5));
 assert(ring.push("wrapped payload",15));
 assert(!ring.push("full queue",10));assert(ring.dropped==1);
 assert(ring.pop(out,sizeof out)==6&&!memcmp(out,"second",6));
 assert(ring.pop(out,sizeof out)==15&&!memcmp(out,"wrapped payload",15));assert(ring.used==0);
 for(unsigned i=0;i<10000;++i){assert(ring.push("1234567890",10));assert(ring.pop(out,sizeof out)==10);assert(!memcmp(out,"1234567890",10));}
 assert(debuglog::begin());assert(allocations==1&&tasks==1);
 auto started=std::chrono::steady_clock::now();
 // Output task never runs. USB writes abort. Producers must keep returning
 // when the fixed queue fills instead of calling or waiting for the driver.
 for(unsigned i=0;i<100000;++i)debuglog::printf("[log] blocked output message %u\n",i);
 debuglog::print("tail");debuglog::println("still nonblocking");
 assert(debuglog::buffered()<=8192&&debuglog::dropped()>0);
 assert(allocations==1&&tasks==1);
 auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
 assert(ms<5000);
 printf("PASS: full/stopped logger returns for 100000 writes in %lld ms; fixed queue %zu bytes, %u dropped; no USB call on producer\n",(long long)ms,debuglog::buffered(),debuglog::dropped());
}
''')
 subprocess.run(['c++','-std=c++17','-I'+str(p),'-I'+str(root/'firmware/BhoomiClock'),str(p/'check.cpp'),str(root/'firmware/BhoomiClock/debug_log.cpp'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True,timeout=10)

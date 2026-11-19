#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// External synchronization is required. Storage never grows, and a full queue
// drops the new record rather than waiting for USB or discarding half a line.
template<size_t Capacity> struct DebugLogRing {
  uint8_t data[Capacity] = {};
  size_t head=0, tail=0, used=0;
  uint32_t dropped=0;
  bool push(const char *message,size_t n) {
    if(n>65535 || n+2>Capacity-used){++dropped;return false;}
    put(n&255);put((n>>8)&255);
    for(size_t i=0;i<n;++i)put((uint8_t)message[i]);
    return true;
  }
  size_t pop(char *out,size_t capacity) {
    if(!used)return 0;
    size_t n=get();n|=(size_t)get()<<8;
    if(n>capacity){for(size_t i=0;i<n;++i)get();++dropped;return 0;}
    for(size_t i=0;i<n;++i)out[i]=(char)get();
    return n;
  }
 private:
  void put(uint8_t b){data[head]=b;head=(head+1)%Capacity;++used;}
  uint8_t get(){uint8_t b=data[tail];tail=(tail+1)%Capacity;--used;return b;}
};

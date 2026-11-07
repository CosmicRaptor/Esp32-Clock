"""Run the firmware's actual response helper with a simulated socket and clock."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class CooperativeTransferTests(unittest.TestCase):
    def test_response_yields_times_out_and_restores_read_timeout(self):
        source = (ROOT / 'firmware' / 'BhoomiClock' / 'portal_client.cpp').read_text()
        helper = source[source.index('int writeResponse('):source.index('uint8_t *bigAlloc(')]
        harness = r'''
#include <cstdint>
#include <cstddef>
#include <cassert>
static uint32_t nowMs=0, yields=0;
uint32_t millis(){return nowMs;}
void vTaskDelay(unsigned ticks){nowMs+=ticks;yields++;}
constexpr uint16_t IO_TIMEOUT_MS=15000;
class Stream {
 public:
  virtual size_t write(uint8_t)=0;
  virtual size_t write(const uint8_t*,size_t)=0;
  virtual int available()=0;virtual int read()=0;virtual int peek()=0;
  void setWriteError(){error=1;}int getWriteError(){return error;}
  void setTimeout(unsigned long t){timeout=t;}unsigned long getTimeout(){return timeout;}
 private:int error=0;unsigned long timeout=IO_TIMEOUT_MS;
};
class Buffer:public Stream{
 public:size_t count=0;bool fail=false;
 size_t write(uint8_t b)override{return write(&b,1);}
 size_t write(const uint8_t*,size_t n)override{if(fail){setWriteError();return 0;}count+=n;return n;}
 int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}
};
struct FakeHttp{
 Buffer socket;bool stalled=false;
 Stream*getStreamPtr(){return &socket;}
 int writeToStream(Stream*out){
   assert(socket.getTimeout()==100);
   uint8_t bytes[2]={1,2};int total=0;
   for(int i=0;i<10;i++){
     nowMs+=stalled?4000:50;
     total+=out->write(bytes,stalled?0:2);
     if(out->getWriteError())return -1;
   }
   return total;
 }
};
struct Conn{FakeHttp http;};
'''
        main = r'''
int main(){
 Conn good;Buffer body;assert(writeResponse(good,body)==20);assert(body.count==20);assert(yields==10);assert(good.http.socket.getTimeout()==IO_TIMEOUT_MS);
 nowMs=0;yields=0;Conn stuck;stuck.http.stalled=true;Buffer empty;
 assert(writeResponse(stuck,empty)<0);assert(yields>=4);assert(nowMs<20000);assert(stuck.http.socket.getTimeout()==IO_TIMEOUT_MS);
 nowMs=0;Conn shortWrite;Buffer bad;bad.fail=true;assert(writeResponse(shortWrite,bad)<0);assert(shortWrite.http.socket.getTimeout()==IO_TIMEOUT_MS);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp=Path(tmp)/'transfer.cpp';binary=Path(tmp)/'transfer'
            cpp.write_text(harness+helper+main)
            subprocess.run(['c++','-std=c++17',str(cpp),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

#include <cassert>
#include <cstddef>
#include <cstdint>
static int yields = 0;
void vTaskDelay(unsigned ticks) { assert(ticks == 1); ++yields; }
#include "../firmware/BhoomiClock/cooperative_client.h"
struct FakeSocket {
  virtual ~FakeSocket() = default;
  int value = -1;
  virtual int read() { return value; }
  virtual int read(uint8_t *out, size_t n) { if (n) *out = 42; return n ? 1 : 0; }
};
int main() {
  CooperativeClient<FakeSocket> socket;
  FakeSocket &stream = socket;
  // Model timedRead's virtual dispatch while a header pauses mid-line.
  for (int i = 0; i < 100; ++i) assert(stream.read() == -1);
  assert(yields == 100);
  socket.value = '\n';
  assert(stream.read() == '\n' && yields == 100);
  socket.value = 0;
  assert(stream.read() == 0 && yields == 100);
  uint8_t byte = 0;
  assert(socket.read(&byte, 1) == 1 && byte == 42 && yields == 100);
}

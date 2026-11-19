#include "../firmware/BhoomiClock/ota_signature.h"
#include <cassert>
#include <cstring>
#include <initializer_list>

int main() {
  uint32_t parts[3];
  assert(otasign::version("0.11.2", parts) && parts[2] == 2);
  for (const char *s : {"", "1.2", "1.2.3.4", "01.2.3", "1.-2.3", "1.2.4294967296", "1.2.3\n"})
    assert(!otasign::version(s, parts));
  assert(otasign::newer("0.11.3", "0.11.2"));
  assert(otasign::newer("0.12.0", "0.11.999"));
  assert(!otasign::newer("0.11.2", "0.11.2"));
  assert(!otasign::newer("0.11.1", "0.11.2"));
  uint8_t sig[72]; size_t length;
  assert(otasign::unhex("30ab", sig, sizeof(sig), length) && length == 2 && sig[1] == 0xab);
  assert(!otasign::unhex("30AB", sig, sizeof(sig), length));
  assert(!otasign::unhex("300", sig, sizeof(sig), length));
  assert(!otasign::unhex("", sig, sizeof(sig), length));
  assert(!otasign::unhex("3000", sig, 1, length));
  char body[160];
  otasign::manifest(body, sizeof(body), "0.11.2", "abc", 256);
  assert(!strcmp(body, "bhoomi-clock-ota-v1\n0.11.2\nabc\n256\n"));
}

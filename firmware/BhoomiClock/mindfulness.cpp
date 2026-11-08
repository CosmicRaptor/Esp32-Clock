#include "mindfulness.h"
#include <Preferences.h>
#include <string.h>
namespace mindfulness {
namespace {
constexpr size_t CAP = 256;
struct Queue { uint32_t count; Record records[CAP]; };
Queue q = {};
uint32_t lastSlot = 0;
bool save() {
  Preferences p;
  if (!p.begin("mindful", false)) return false;
  bool ok = p.putBytes("queue", &q, sizeof(q)) == sizeof(q);
  p.end(); return ok;
}
}
void begin() {
  Preferences p;
  if (!p.begin("mindful", false)) return;
  if (p.getBytesLength("queue") == sizeof(q)) p.getBytes("queue", &q, sizeof(q));
  if (q.count > CAP) memset(&q, 0, sizeof(q));
  lastSlot = p.getUInt("lastslot", 0);
  p.end();
}
bool enqueue(uint32_t at, uint32_t mood) {
  if (q.count >= CAP || mood < 1 || mood > 5) return false;
  q.records[q.count++] = {at, mood};
  if (save()) return true;
  --q.count; return false;
}
bool peek(Record &out) { if (!q.count) return false; out = q.records[0]; return true; }
bool acknowledge(const Record &r) {
  if (!q.count || q.records[0].at != r.at || q.records[0].mood != r.mood) return false;
  Queue old = q;
  memmove(q.records, q.records + 1, (--q.count) * sizeof(Record));
  if (save()) return true;
  q = old; return false;
}
bool shouldPrompt(time_t now, const struct tm &local) {
  if (now < 1700000000 || local.tm_hour % 3 || local.tm_min != 0) return false;
  uint32_t slot = uint32_t(now / 60);
  if (slot == lastSlot) return false;
  Preferences p;
  if (!p.begin("mindful", false)) return false;
  bool ok = p.putUInt("lastslot", slot) == sizeof(slot);
  p.end();
  if (!ok) return false;
  lastSlot = slot; return true;
}
}

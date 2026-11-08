#include "../firmware/BhoomiClock/volume_sync.h"
#include <cassert>
int main() {
  VolumeState base{1,18,18,0}, pending{1,8,12,1}, out{};
  assert(volumeSyncResult(base, base, 3, 7, out) && out.alarm == 3 && out.video == 7 && !out.pending);
  assert(!volumeSyncResult(pending, pending, 18, 18, out)); // stale cloud cannot erase a pending local edit
  assert(volumeSyncResult(pending, pending, 8, 12, out) && !out.pending);
  assert(!volumeSyncResult(pending, base, 3, 7, out)); // new local edit during a remote request
  assert(!volumeSyncResult(base, base, 22, 3, out));
}

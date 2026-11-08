#pragma once
#include <stdint.h>

// Pending local edits are never replaced by a stale remote read or an in-flight response.
struct VolumeState { uint8_t version, alarm, video, pending; };
inline bool volumeSyncResult(const VolumeState &current, const VolumeState &sent,
                             int alarm, int video, VolumeState &next) {
  if (alarm < 0 || alarm > 21 || video < 0 || video > 21) return false;
  if (current.version != sent.version || current.alarm != sent.alarm ||
      current.video != sent.video || current.pending != sent.pending) return false;
  if (sent.pending && (alarm != sent.alarm || video != sent.video)) return false;
  next = {1, static_cast<uint8_t>(alarm), static_cast<uint8_t>(video), 0};
  return true;
}

#pragma once
namespace mood_clip {
// Install a complete BCV1 file from the SD card. Returns false without replacing an existing clip on failure.
bool install(const char *path, unsigned mood);
}

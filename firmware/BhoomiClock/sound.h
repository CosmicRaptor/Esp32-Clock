#pragma once
#include <Arduino.h>

// Plays alarm sounds through the PCM5101 amp (I2S), using the ESP32-audioI2S library to decode
// mp3 / m4a / aac / flac / ogg / opus / wav.
//
// play() loops a file from /sounds on the SD card until stop() is called. An empty name, a missing
// file, no card or a file that won't decode all fall back to the built-in beep, which lives in the
// internal flash so it works without a card. Everything runs in the sound task; play() and stop()
// only leave a note for it, so they are cheap and safe to call from any task.
namespace sound {

void begin();  // after storage::begin() is not required; the card is checked at play time

void play(const char *fileName);  // "" = built-in beep; plays on repeat
void stop();
bool playing();

}  // namespace sound

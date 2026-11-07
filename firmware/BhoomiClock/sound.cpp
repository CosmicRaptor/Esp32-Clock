#include "sound.h"

#include <Audio.h>
#include <FFat.h>
#include <SD_MMC.h>
#include <math.h>

#include "storage.h"

namespace sound {
namespace {

// Pins (Waveshare ESP32-S3-Touch-LCD-2.8, PCM5101 DAC; from Waveshare's demo)
constexpr int PIN_BCLK = 48;
constexpr int PIN_LRC = 38;
constexpr int PIN_DOUT = 47;

constexpr uint8_t VOLUME = 18;               // 0..21
constexpr uint32_t MIN_GOOD_PLAY_MS = 1200;  // a file that ends sooner than this didn't really play
constexpr int MAX_SHORT_PLAYS = 2;           // ...twice in a row and we give up on it and beep
constexpr uint32_t REPEAT_GAP_MS = 1000;     // pause between repeats of a user's sound

// The built-in beep: four quick beeps and a pause, 16 kHz mono 16-bit WAV, written once to the
// internal flash. Bump the number in the name if the pattern changes.
constexpr const char *BEEP_FILE = "/beep1.wav";
constexpr uint32_t BEEP_RATE = 16000;
constexpr int BEEP_COUNT = 4;
constexpr int BEEP_ON_MS = 120;
constexpr int BEEP_OFF_MS = 100;
constexpr int BEEP_PAUSE_MS = 600;
constexpr float BEEP_HZ = 1000.0f;

Audio *audio = nullptr;
SemaphoreHandle_t reqLock = nullptr;

// ---- request from other tasks (reqLock) ----
bool reqPlay = false, reqStop = false;
char reqFile[72] = "";

// ---- task-private ----
volatile bool active = false;
bool trackRunning = false;
bool usingBeep = false;
bool giveUpOnFile = false;
int shortPlays = 0;
uint32_t trackStartedAt = 0;
uint32_t restartAt = 0;  // millis() deadline for the next repeat; 0 = none pending
char current[72] = "";

void put16(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

// Writes the beep WAV if it isn't there yet. Returns true when the file is usable.
bool ensureBeep() {
  if (!FFat.begin(true)) {  // formats only our own (otherwise unused) partition if it was never set up
    Serial.println("[sound] flash storage unavailable; no built-in beep");
    return false;
  }
  if (FFat.exists(BEEP_FILE)) return true;

  const int on = BEEP_RATE * BEEP_ON_MS / 1000, off = BEEP_RATE * BEEP_OFF_MS / 1000;
  const int pause = BEEP_RATE * BEEP_PAUSE_MS / 1000;
  const uint32_t samples = (uint32_t)BEEP_COUNT * (on + off) + pause;
  const uint32_t dataBytes = samples * 2;

  String tmp = String(BEEP_FILE) + ".tmp";
  File f = FFat.open(tmp, FILE_WRITE);
  if (!f) return false;
  uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0};
  put32(h + 4, 36 + dataBytes);
  put32(h + 24, BEEP_RATE);
  put32(h + 28, BEEP_RATE * 2);  // byte rate
  put16(h + 32, 2);              // block align
  put16(h + 34, 16);             // bits per sample
  memcpy(h + 36, "data", 4);
  put32(h + 40, dataBytes);
  bool ok = f.write(h, sizeof h) == sizeof h;

  int16_t chunk[256];
  int n = 0;
  const int ramp = BEEP_RATE / 200;  // 5 ms fade in/out, so the beeps don't click
  for (uint32_t i = 0; ok && i < samples; i++) {
    int16_t s = 0;
    uint32_t t = i % (on + off);
    if (i < (uint32_t)BEEP_COUNT * (on + off) && t < (uint32_t)on) {
      float env = 1.0f;
      if ((int)t < ramp) env = (float)t / ramp;
      else if ((int)t > on - ramp) env = (float)(on - t) / ramp;
      s = (int16_t)(sinf(2.0f * (float)M_PI * BEEP_HZ * t / BEEP_RATE) * env * 26000.0f);
    }
    chunk[n++] = s;
    if (n == 256 || i + 1 == samples) {
      ok = f.write((const uint8_t *)chunk, n * 2) == (size_t)n * 2;
      n = 0;
    }
  }
  f.close();
  if (ok) {
    ok = FFat.rename(tmp, BEEP_FILE);
  }
  if (!ok) FFat.remove(tmp);
  Serial.printf("[sound] built-in beep %s\n", ok ? "written to flash" : "could not be written");
  return ok;
}

void onAudioEvent(Audio::msg_t m) {
  if (m.e == Audio::evt_info || m.e == Audio::evt_log || m.e == Audio::evt_eof)
    Serial.printf("[audio] %s: %s\n", m.s ? m.s : "", m.msg ? m.msg : "");
}

bool startTrack() {
  bool ok = false;
  usingBeep = true;
  if (current[0] && !giveUpOnFile && storage::mounted()) {
    String path = String("/sounds/") + current;
    if (SD_MMC.exists(path)) {
      ok = audio->connecttoFS(SD_MMC, path.c_str());
      usingBeep = false;
      if (!ok) Serial.printf("[sound] %s: could not open\n", current);
    } else {
      Serial.printf("[sound] %s not found on the card\n", current);
      giveUpOnFile = true;
    }
  }
  if (!ok) {
    usingBeep = true;
    ok = audio->connecttoFS(FFat, BEEP_FILE);
    if (!ok) Serial.println("[sound] built-in beep unavailable");
  }
  trackRunning = ok;
  trackStartedAt = millis();
  if (ok) Serial.printf("[sound] playing %s\n", usingBeep ? "built-in beep" : current);
  return ok;
}

void handleRequests() {
  bool doPlay, doStop;
  char file[sizeof reqFile];
  xSemaphoreTake(reqLock, portMAX_DELAY);
  doPlay = reqPlay;
  doStop = reqStop;
  strlcpy(file, reqFile, sizeof file);
  reqPlay = reqStop = false;
  xSemaphoreGive(reqLock);

  if (doStop || doPlay) {
    if (trackRunning || audio->isRunning()) audio->stopSong();
    trackRunning = false;
    active = false;
    restartAt = 0;
    if (doStop) Serial.println("[sound] stopped");
  }
  if (doPlay) {  // a play after a stop in the same tick wins
    strlcpy(current, file, sizeof current);
    giveUpOnFile = false;
    shortPlays = 0;
    active = true;
    startTrack();
    if (!trackRunning) active = false;
  }
}

// Called when a track finished on its own: loop it, unless it never really played.
void trackEnded() {
  trackRunning = false;
  uint32_t len = millis() - trackStartedAt;
  if (!usingBeep) {
    if (len < MIN_GOOD_PLAY_MS) {
      if (++shortPlays >= MAX_SHORT_PLAYS) {
        Serial.printf("[sound] %s keeps ending at once (unsupported?); using the beep instead\n", current);
        giveUpOnFile = true;
      }
    } else {
      shortPlays = 0;
    }
  }
  restartAt = (millis() + (usingBeep ? 0 : REPEAT_GAP_MS)) | 1;
}

void task(void *) {
  for (;;) {
    handleRequests();
    if (active) {
      audio->loop();
      if (trackRunning && !audio->isRunning()) trackEnded();
      if (!trackRunning && restartAt && (int32_t)(millis() - restartAt) >= 0) {
        restartAt = 0;
        if (!startTrack()) active = false;
      }
    }
    vTaskDelay(1);
  }
}

}  // namespace

void begin() {
  reqLock = xSemaphoreCreateMutex();
  Audio::audio_info_callback = onAudioEvent;
  audio = new Audio(I2S_NUM_0);
  audio->setPinout(PIN_BCLK, PIN_LRC, PIN_DOUT);
  audio->setVolume(VOLUME);
  ensureBeep();
  // Core 1 like the Arduino loop, but above it (priority 2 vs 1) so playback never starves while
  // the screen is drawing. The library runs its decoder in its own task on core 0.
  xTaskCreatePinnedToCore(task, "sound", 8 * 1024, nullptr, 2, nullptr, 1);
}

void play(const char *fileName) {
  if (!reqLock) return;
  xSemaphoreTake(reqLock, portMAX_DELAY);
  strlcpy(reqFile, fileName ? fileName : "", sizeof reqFile);
  reqPlay = true;
  xSemaphoreGive(reqLock);
}

void stop() {
  if (!reqLock) return;
  xSemaphoreTake(reqLock, portMAX_DELAY);
  reqStop = true;
  reqPlay = false;  // a stop cancels a play that hasn't started yet
  xSemaphoreGive(reqLock);
}

bool playing() { return active; }

}  // namespace sound

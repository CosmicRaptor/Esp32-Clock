#pragma once
#include <stdint.h>

// Monotonic and wrap-safe; independent of NTP changes and the rendered screen.
namespace focus {
enum class State { Idle, Studying, Complete, Break, Finished };
struct Timer {
  State state = State::Idle;
  uint32_t started = 0, duration = 0;
  void start(uint32_t now, unsigned minutes) {
    if (minutes != 15 && minutes != 25 && minutes != 45) return;
    started = now; duration = minutes * 60000u; state = State::Studying;
  }
  void takeBreak(uint32_t now) { if (state == State::Complete) { started = now; duration = 300000; state = State::Break; } }
  void done() { state = State::Idle; duration = 0; }
  bool quiet() const { return state == State::Studying; }
  bool running() const { return quiet() || state == State::Break; }
  uint32_t remaining(uint32_t now) const { uint32_t elapsed = now - started; return running() && elapsed < duration ? duration - elapsed : 0; }
  bool tick(uint32_t now) {
    if (!running() || now - started < duration) return false;
    state = quiet() ? State::Complete : State::Finished;
    return true;
  }
};
}

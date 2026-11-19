#include "../firmware/BhoomiClock/focus_timer.h"
#include <cassert>
#include <cstdint>
#include <initializer_list>
int main() {
  focus::Timer timer;
  timer.start(0, 10); assert(timer.state == focus::State::Idle);
  for (unsigned minutes : {15u, 25u, 45u}) {
    uint32_t start = UINT32_MAX - 1000;
    timer.start(start, minutes);
    assert(timer.quiet()); assert(timer.remaining(start) == minutes * 60000);
    assert(!timer.tick(start + minutes * 60000 - 1));
    assert(timer.tick(start + minutes * 60000));
    assert(timer.state == focus::State::Complete && !timer.quiet());
    assert(!timer.tick(start + minutes * 60000 + 1)); // only one completion chime
    assert(!timer.running()); // break never starts automatically
    timer.takeBreak(123);
    assert(timer.state == focus::State::Break && !timer.quiet());
    assert(!timer.tick(300122)); assert(timer.tick(300123));
    assert(timer.state == focus::State::Finished);
    timer.done(); assert(timer.state == focus::State::Idle);
  }
  timer.takeBreak(0); assert(timer.state == focus::State::Idle);
  timer.start(0, 45); timer.done(); assert(!timer.tick(2700000));
}

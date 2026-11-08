#pragma once

// Arduino Stream::timedRead spins on virtual read() until its timeout. A partial
// HTTP header can otherwise starve IDLE0 for the whole 15-second socket timeout.
// Keep that timeout and the client's bulk-read overload, but let idle run when
// a single-byte read has no data. Base and FreeRTOS must be included first.
template <typename Base>
class CooperativeClient : public Base {
 public:
  using Base::read;
  int read() override {
    int value = Base::read();
    if (value < 0) vTaskDelay(1);
    return value;
  }
};

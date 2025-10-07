#ifndef POWER_H
#define POWER_H

#include <Arduino.h>
#include "defines/pinconf.h"

// Enter deep sleep until the given wake pin (RTC IO) is asserted.
// The implementation uses esp_deep_sleep API indirectly provided by Arduino-ESP32.
// Enter deep sleep until the given wake pin is asserted.
// - wakePin: GPIO number connected to wake button
// - activeLow: whether the button pulls the line LOW when pressed (default true)
// - timeoutSec: optional backup timeout in seconds (0 = disabled)
void enterDeepSleepUntilWakePin(int wakePin, bool activeLow = true, uint32_t timeoutSec = 0);

#endif

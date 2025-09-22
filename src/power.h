#ifndef POWER_H
#define POWER_H

#include <Arduino.h>
#include "defines/pinconf.h"

// Enter deep sleep until the given wake pin (RTC IO) is asserted.
// The implementation uses esp_deep_sleep API indirectly provided by Arduino-ESP32.
void enterDeepSleepUntilWakePin(int wakePin);

#endif

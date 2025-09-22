#include "power.h"
#include "defines/pinconf.h"
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <Arduino.h>

// Use GPIO-based wakeup because some cores/headers can make ext0/ext1
// linkage fragile. gpio_wakeup_enable + esp_sleep_enable_gpio_wakeup
// is portable across the cores supported by the Arduino framework.

void enterDeepSleepUntilWakePin(int wakePin) {
  Serial.println("Entering deep sleep until button press on pin " + String(wakePin));
  // Configure the wakeup pin as RTC IO if necessary
  // Use ext0 wakeup which supports a single RTC IO pin and level
  // Note: on some boards not all GPIOs are RTC-capable. The user should
  // ensure WAKE_BUTTON_PIN maps to a RTC-capable pin or use GPIO wakeup.

  // Prepare pin: detach from any active pulls and set as input
  pinMode(wakePin, INPUT_PULLUP);
  // small delay to allow state to settle
  delay(10);

  // Diagnostic: print current input level before sleep
  int currentLevel = digitalRead(wakePin);
  Serial.println("Wake pin level before sleep: " + String(currentLevel));

  // Enable wake on the GPIO line (low level). gpio_wakeup_enable configures
  // the driver to wake on the specified level, then esp_sleep_enable_gpio_wakeup
  // arms the wake source for deep sleep.
  gpio_wakeup_enable((gpio_num_t)wakePin, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();

  // Print planned wakeup cause query (will be valid after wake)
  Serial.println("About to sleep now... (will wake on LOW of pin)");
  esp_deep_sleep_start();
}

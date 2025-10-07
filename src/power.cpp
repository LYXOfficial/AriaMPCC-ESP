#include "power.h"
#include "defines/pinconf.h"
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

// enterDeepSleepUntilWakePin:
// - wakePin: GPIO number connected to wake button
// - activeLow: whether the button pulls the line LOW when pressed (default:
// true)
// - timeoutSec: optional backup timeout (seconds) to wake if button doesn't
// work; 0 disables (default: 0)
void enterDeepSleepUntilWakePin(int wakePin, bool activeLow /*= true*/,
                                uint32_t timeoutSec /*= 0*/) {
  Serial.println("Entering deep sleep (pin " + String(wakePin) + ")");

  // Force the wake pin into a digital input (not ADC) and configure internal
  // pull This helps ensure the pin behaves as a proper GPIO for RTC/ext0 wake.
  Serial.println("Configuring wake pin as digital input (reset pin, set "
                 "direction and pull)");
  gpio_reset_pin((gpio_num_t)wakePin);
  gpio_set_direction((gpio_num_t)wakePin, GPIO_MODE_INPUT);
  if (activeLow) {
    gpio_set_pull_mode((gpio_num_t)wakePin, GPIO_PULLUP_ONLY);
  } else {
    gpio_set_pull_mode((gpio_num_t)wakePin, GPIO_PULLDOWN_ONLY);
  }
  // small settle delay
  delay(10);

  int currentLevel = digitalRead(wakePin);
  Serial.println("Wake pin level before sleep: " + String(currentLevel) +
                 " (expected " + String(activeLow ? 0 : 1) + ")");

  // Prefer ext0 (RTC IO single-pin) if the pin is RTC-capable. ext0 provides
  // robust wake for deep-sleep using RTC IO. If ext0 is not appropriate, fall
  // back to gpio_wakeup_enable + esp_sleep_enable_gpio_wakeup.

  int level = activeLow ? 0 : 1;

  Serial.println("Using esp_deep_sleep_enable_gpio_wakeup bitmask fallback");
  // create a bitmask for the given wakePin (1 << gpio)
  uint64_t wake_mask = (1ULL << (uint64_t)wakePin);
  Serial.println(String("Wakeup bitmask for pin ") + String(wakePin) + ": " +
                 String((unsigned long long)wake_mask, DEC));

  // Disable deep-sleep pad hold for digital GPIOs to allow pad state to change
  // during deep sleep
  gpio_deep_sleep_hold_dis();

  // Configure direction to input to be safe
  gpio_set_direction((gpio_num_t)wakePin, GPIO_MODE_INPUT);

  esp_err_t r = esp_deep_sleep_enable_gpio_wakeup(
      wake_mask,
      activeLow ? ESP_GPIO_WAKEUP_GPIO_LOW : ESP_GPIO_WAKEUP_GPIO_HIGH);
  Serial.println(String("esp_deep_sleep_enable_gpio_wakeup returned: ") +
                 String((int)r));

  // Optional timeout fallback: wake up after timeoutSec seconds if requested
  if (timeoutSec > 0) {
    Serial.println("Also arming timer wakeup in " + String(timeoutSec) +
                   "s as backup");
    esp_sleep_enable_timer_wakeup((uint64_t)timeoutSec * 1000000ULL);
  }

  // Defensive: disable any previously enabled wakeup sources that might have
  // been configured for light-sleep or earlier code paths. This prevents those
  // sources from accidentally waking the device when we intend deep-sleep
  // behavior.
  Serial.println(
      "Disabling common wakeup sources before deep sleep: TIMER, GPIO, EXT0");
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

  Serial.println("About to enter deep sleep now...");
  esp_deep_sleep_start();
}

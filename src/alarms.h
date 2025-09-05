#pragma once

#include <Arduino.h>
#include <Preferences.h>

// Alarm data model
struct Alarm {
  uint8_t hour;
  uint8_t minute;
  uint8_t weekdays; // bitmask: bit0=Sun ... bit6=Sat
  uint8_t tone;     // 1..5
  bool enabled;
};

// Public API
void alarmsInit();
void loadAlarms();
void saveAlarms();

// Render and input handlers
void renderAlarmPage(bool full = false);
void handleAlarmRightButton();
void handleAlarmLeftButton();
void handleAlarmCenterButton();

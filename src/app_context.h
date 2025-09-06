// Shared application context for modules
#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <NTPClient.h>
#include <U8g2_for_Adafruit_GFX.h>

// E-paper display instance (defined in main.cpp)
extern GxEPD2_BW<GxEPD2_213_B72, GxEPD2_213_B72::HEIGHT> display;

// U8g2 fonts wrapper (defined in main.cpp)
extern U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

// NTP time client (defined in main.cpp)
extern NTPClient timeClient;

// Chinese week day labels (defined in main.cpp)
extern const char *weekDaysChinese[];

// Current page index and total pages (defined in main.cpp)
extern int currentPage;
extern const int totalPages;

// Refresh control flags (defined in main.cpp)
extern volatile bool refreshInProgress;

// Page switching helper (defined in main.cpp)
void switchPageAndFullRefresh(int page);
void renderPlaceholderPartial(int page);

// Alarm engine control: start/stop and state
void startAlarmNow(int idx);
extern volatile bool alarmRinging;

// Forward declare PageManager and extern global instance (defined in main.cpp)
class PageManager;
extern PageManager gPageMgr;

// Interaction timing and partial-before-full counter (defined in main.cpp)
extern unsigned long lastInteraction;
extern int pageSwitchCount;
// timestamp of last page switch (ms)
extern unsigned long lastPageSwitchMs;

// Home/time related state (moved to pages/time_page.cpp)
extern String lastDisplayedTime;
extern String lastDisplayedDate;
extern String currentHitokoto;
extern String lastDisplayedHitokoto;
extern unsigned long lastFullRefresh;
extern unsigned long lastHitokotoUpdate;
extern const unsigned long fullRefreshInterval;
extern const unsigned long hitokotoUpdateInterval;
// Weather
extern String currentCity;
extern String currentWeather;
extern String currentTemp;
extern unsigned long lastWeatherUpdate;
extern const unsigned long weatherUpdateInterval;

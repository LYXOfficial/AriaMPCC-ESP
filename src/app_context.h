// Shared application context for modules
#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <NTPClient.h>

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

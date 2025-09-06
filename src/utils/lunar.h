// lunar.h - Solar/Lunar conversion API
#pragma once

struct Solar {
  int year;
  int month;
  int day;
};

struct Lunar {
  int year;
  int month;
  int day;
  bool isLeap;
};

Lunar SolarToLunar(const Solar &s);
Solar LunarToSolar(const Lunar &l);

// Supported lunar data table in this file covers years 1900..2049.
constexpr int LUNAR_MIN_YEAR = 1900;
constexpr int LUNAR_MAX_YEAR = 2049;

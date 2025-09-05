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

// Convert solar date to lunar date. Valid for years roughly 1900..2100.
Lunar SolarToLunar(const Solar &s);

// Convert lunar date to solar date. If lunar is leap month, set isLeap accordingly.
Solar LunarToSolar(const Lunar &l);

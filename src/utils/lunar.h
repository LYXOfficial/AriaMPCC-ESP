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

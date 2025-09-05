/* see previous implementation moved from src/lunar.cpp */
#include "lunar.h"

// lunar data table and helpers copied from previous file
static const unsigned int lunarInfo[] = {
    0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0,
    0x09ad0, 0x055d2, 0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540,
    0x0d6a0, 0x0ada2, 0x095b0, 0x14977, 0x04970, 0x0a4b0, 0x0b4b5, 0x06a50,
    0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970, 0x06566, 0x0d4a0,
    0x0ea50, 0x06e95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950,
    0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2,
    0x0a950, 0x0b557, 0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5d0, 0x14573,
    0x052d0, 0x0a9a8, 0x0e950, 0x06aa0, 0x0aea6, 0x0ab50, 0x04b60, 0x0aae4,
    0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0, 0x096d0, 0x04dd5,
    0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b5a0, 0x195a6,
    0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46,
    0x0ab60, 0x09570, 0x04afb, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58,
    0x05ac0, 0x0ab60, 0x096d5, 0x092e0, 0x0c960, 0x0d954, 0x0d4a0, 0x0da50,
    0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5, 0x0a950, 0x0b4a0,
    0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930,
    0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260,
    0x0ea65, 0x0d530, 0x05aa0, 0x076a3, 0x096d0, 0x04bd7, 0x04ad0, 0x0a4d0,
    0x1d0b6, 0x0d250, 0x0d520, 0x0dd45, 0x0b5a0, 0x056d0, 0x055b2, 0x049b0,
    0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0};

static int yearDays(int y) {
  int i, sum = 348; // 29*12
  unsigned int info = lunarInfo[y - 1900];
  for (i = 0x8000; i > 0x8; i >>= 1) if (info & i) sum += 1;
  int leap = info & 0xf; if (leap) sum += ((info & 0x10000) ? 30 : 29);
  return sum;
}
static int monthDays(int y, int m) {
  unsigned int info = lunarInfo[y - 1900];
  return (info & (0x10000 >> m)) ? 30 : 29;
}
static int leapMonth(int y) { return lunarInfo[y - 1900] & 0xf; }
static long daysBetween1900(int y, int m, int d) {
  long days = 0;
  for (int i = 1900; i < y; i++) days += (365 + ((i % 4 == 0 && i % 100 != 0) || (i % 400 == 0)));
  static const int mdays[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
  for (int i = 1; i < m; i++) {
    days += mdays[i];
    if (i == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))) days += 1;
  }
  days += d - 1;
  return days - 30; // base 1900-01-31
}

Lunar SolarToLunar(const Solar &s) {
  Lunar out = {0,0,0,false};
  long offset = daysBetween1900(s.year, s.month, s.day);
  int y = 1900;
  int daysInYear = yearDays(y);
  while (offset >= daysInYear) { offset -= daysInYear; y++; if (y > 2100) break; daysInYear = yearDays(y);} 
  out.year = y;
  int leap = leapMonth(y);
  bool isLeap = false;
  int m = 1;
  while (true) {
    int mdays = 0;
    if (m == leap + 1 && leap > 0 && !isLeap) { mdays = (lunarInfo[y - 1900] & 0x10000) ? 30 : 29; isLeap = true; }
    else { mdays = monthDays(y, m); if (isLeap) isLeap = false; }
    if (offset < mdays) break;
    offset -= mdays;
    if (!isLeap) m++;
  }
  out.month = m; out.day = offset + 1; out.isLeap = (leap > 0 && isLeap);
  return out;
}

Solar LunarToSolar(const Lunar &l) {
  Solar s = {0,0,0};
  long offset = 0;
  for (int i = 1900; i < l.year; i++) offset += yearDays(i);
  int leap = leapMonth(l.year); bool usedLeap = false;
  int m = 1;
  while (m < l.month) {
    if (leap > 0 && m == leap + 1 && !usedLeap) { offset += ((lunarInfo[l.year - 1900] & 0x10000) ? 30 : 29); usedLeap = true; }
    else { offset += monthDays(l.year, m); if (usedLeap) usedLeap = false; else m++; }
  }
  offset += (l.day - 1);
  long total = offset + 30;
  int y = 1900;
  while (true) {
    int daysy = 365 + ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0));
    if (total < daysy) break; total -= daysy; y++;
  }
  static const int mdays[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
  int month = 1; while (true) {
    int dm = mdays[month]; if (month == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))) dm++;
    if (total < dm) break; total -= dm; month++;
  }
  s.year = y; s.month = month; s.day = total + 1; return s;
}

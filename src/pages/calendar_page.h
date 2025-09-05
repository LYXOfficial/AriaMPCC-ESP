#pragma once
#include "page.h"

class CalendarPage : public Page {
public:
  CalendarPage();
  void render(bool full) override;
  void onLeft() override;
  void onRight() override;
  void onCenter() override;
  const char *name() const override { return "calendar"; }

private:
  enum Cursor { CURSOR_YEAR = 0, CURSOR_MONTH, CURSOR_DAY, CURSOR_NAVIGATE };
  int selectedYear;
  int selectedMonth;
  int selectedDay;
  Cursor currentCursor;

  // helpers
  bool isLeapYear(int year);
  int getDaysInMonth(int year, int month);
  int getFirstDayOfWeek(int year, int month);
  String lunarDayToChinese(int d);
  String getLunarDate(int y, int m, int d);
};

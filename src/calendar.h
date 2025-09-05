#pragma once

enum CalendarCursor {
  CURSOR_YEAR = 0,
  CURSOR_MONTH,
  CURSOR_DAY,
  CURSOR_NAVIGATE
};

void renderCalendarPage(bool full = false);
void handleCalendarRightButton();
void handleCalendarLeftButton();
void handleCalendarCenterButton();

// Set initial selected date (year, month, day)
void calendarSetSelectedDate(int year, int month, int day);


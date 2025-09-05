#include "calendar.h"
#include "app_context.h"
#include "lunar.h"

#include <Arduino.h>

// State moved from main.cpp
static int selectedYear = 2025;
static int selectedMonth = 9;
static int selectedDay = 5;
static CalendarCursor currentCursor = CURSOR_NAVIGATE;

// Forwards to helpers previously in main.cpp
static bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}
static int getDaysInMonth(int year, int month) {
  int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) {
    return 29;
  }
  return daysInMonth[month - 1];
}
static int getFirstDayOfWeek(int year, int month) {
  if (month < 3) {
    month += 12;
    year--;
  }
  int k = year % 100;
  int j = year / 100;
  int q = 1;
  int h = (q + (13 * (month + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;
  return (h + 5) % 7;
}

// Lunar helpers from main.cpp
static const char *lunarMonthNamesChinese[] = {"正月", "二月", "三月", "四月",
                                                "五月", "六月", "七月", "八月",
                                                "九月", "十月", "冬月", "腊月"};
static const char *stems[] = {"甲", "乙", "丙", "丁", "戊",
                              "己", "庚", "辛", "壬", "癸"};
static const char *branches[] = {"子", "丑", "寅", "卯", "辰", "巳",
                                 "午", "未", "申", "酉", "戌", "亥"};

static String lunarDayToChinese(int d) {
  if (d <= 0)
    return String("");
  const char *digits[] = {"零", "一", "二", "三", "四",
                          "五", "六", "七", "八", "九"};
  if (d == 10)
    return String("初十");
  if (d < 10)
    return String("初") + String(digits[d]);
  if (d < 20) {
    if (d == 20)
      return String("二十");
    return String("十") + String(digits[d - 10]);
  }
  if (d == 20)
    return String("二十");
  if (d < 30)
    return String("二十") + String(digits[d - 20]);
  return String("三十");
}

static String getLunarDate(int y, int m, int d) {
  if (y < 1900 || y > 2100) {
    return String("农历") + String(y) + String("年") + String(m) +
           String("月") + String(d) + String("日");
  }
  Solar s{y, m, d};
  Lunar L = SolarToLunar(s);
  int idxStem = (L.year - 4) % 10;
  if (idxStem < 0)
    idxStem += 10;
  int idxBranch = (L.year - 4) % 12;
  if (idxBranch < 0)
    idxBranch += 12;
  String gzYear = String(stems[idxStem]) + String(branches[idxBranch]);
  String monthName = "";
  int mi = L.month - 1;
  if (mi >= 0 && mi < 12) {
    monthName = String((L.isLeap ? "闰" : "")) + String(lunarMonthNamesChinese[mi]);
  } else {
    monthName = String(L.month) + "月";
  }
  String dayName = lunarDayToChinese(L.day);
  return gzYear + String("年") + monthName + dayName;
}

void renderCalendarPage(bool full) {
  if (full) {
    refreshInProgress = true;
    display.setFullWindow();
  } else {
    display.setPartialWindow(0, 0, display.width(), display.height());
  }
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    int leftX = 5;
    int startY = 35;
    u8g2Fonts.setFont(u8g2_font_unifont_t_latin);
    int yearY = startY;
    if (currentCursor == CURSOR_YEAR) {
      int yearW = u8g2Fonts.getUTF8Width((String(selectedYear) + "年").c_str());
      display.fillRect(leftX - 2, yearY - 13, yearW + 4, 16, GxEPD_BLACK);
      u8g2Fonts.setForegroundColor(GxEPD_WHITE);
      u8g2Fonts.setBackgroundColor(GxEPD_BLACK);
      u8g2Fonts.setFontMode(1);
    }
    u8g2Fonts.setCursor(leftX, yearY);
    u8g2Fonts.print(String(selectedYear) + "年");
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
    u8g2Fonts.setFontMode(1);

    int monthY = yearY + 20;
    if (currentCursor == CURSOR_MONTH) {
      int monthW = u8g2Fonts.getUTF8Width((String(selectedMonth) + "月").c_str());
      display.fillRect(leftX - 2, monthY - 13, monthW + 4, 16, GxEPD_BLACK);
      u8g2Fonts.setForegroundColor(GxEPD_WHITE);
      u8g2Fonts.setBackgroundColor(GxEPD_BLACK);
      u8g2Fonts.setFontMode(1);
    }
    u8g2Fonts.setCursor(leftX, monthY);
    u8g2Fonts.print(String(selectedMonth) + "月");
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
    u8g2Fonts.setFontMode(1);

    int dayY = monthY + 20;
    if (currentCursor == CURSOR_DAY) {
      int dayW = u8g2Fonts.getUTF8Width((String(selectedDay) + "日").c_str());
      display.fillRect(leftX - 2, dayY - 13, dayW + 4, 16, GxEPD_BLACK);
      u8g2Fonts.setForegroundColor(GxEPD_WHITE);
      u8g2Fonts.setBackgroundColor(GxEPD_BLACK);
      u8g2Fonts.setFontMode(1);
    }
    u8g2Fonts.setCursor(leftX, dayY);
    u8g2Fonts.print(String(selectedDay) + "日");
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
    u8g2Fonts.setFontMode(1);

    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    String lunarStr = getLunarDate(selectedYear, selectedMonth, selectedDay);
    int lunarY = dayY + 20; // 与 main_old.cpp 保持一致
    u8g2Fonts.setCursor(leftX, lunarY);
    u8g2Fonts.print(lunarStr);

    // 右侧日历区域与坐标（参照 main_old.cpp）
    int calendarX = 110;
    int calendarY = 15;
    int cellW = 14;
    int cellH = 12;

    // 标题（年月）
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    String ymStr = String(selectedYear) + "年 " + String(selectedMonth) + "月";
    u8g2Fonts.setCursor(calendarX, calendarY);
    u8g2Fonts.print(ymStr);

    // 星期标题
    const char *weekHeader[] = {"日", "一", "二", "三", "四", "五", "六"};
    int headerY = calendarY + 18;
    for (int i = 0; i < 7; i++) {
      u8g2Fonts.setCursor(calendarX + i * cellW, headerY);
      u8g2Fonts.print(weekHeader[i]);
    }
    // 网格与日期
    int gridStartY = headerY + 5;
    int daysInMonth = getDaysInMonth(selectedYear, selectedMonth);
    int firstDay = getFirstDayOfWeek(selectedYear, selectedMonth);
    for (int week = 0; week < 6; week++) {
      for (int day = 0; day < 7; day++) {
        int dayNum = week * 7 + day - firstDay + 1;
        if (dayNum > 0 && dayNum <= daysInMonth) {
          int cellX = calendarX + day * cellW;
          int cellY = gridStartY + week * cellH;
          if (dayNum == selectedDay) {
            display.fillRect(cellX, cellY - 3, cellW, cellH, GxEPD_BLACK);
            u8g2Fonts.setForegroundColor(GxEPD_WHITE);
            u8g2Fonts.setBackgroundColor(GxEPD_BLACK);
            u8g2Fonts.setFontMode(1);
            u8g2Fonts.setCursor(cellX + 2, cellY + 6);
            u8g2Fonts.print(String(dayNum));
            u8g2Fonts.setForegroundColor(GxEPD_BLACK);
            u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
            u8g2Fonts.setFontMode(1);
          } else {
            u8g2Fonts.setCursor(cellX + 2, cellY + 6);
            u8g2Fonts.print(String(dayNum));
          }
        }
      }
    }

    int dividerY = display.height() - 18;
    display.drawFastHLine(0, dividerY, display.width(), GxEPD_BLACK);

    timeClient.update();
    time_t rawtime = timeClient.getEpochTime();
    struct tm *timeinfo = localtime(&rawtime);
    int year = timeinfo->tm_year + 1900;
    int month = timeinfo->tm_mon + 1;
    int day = timeinfo->tm_mday;
    int weekday = timeinfo->tm_wday;
    int hours = timeClient.getHours();
    int minutes = timeClient.getMinutes();

    String currentTimeStr = String(year) + "-" + String(month < 10 ? "0" : "") + String(month) +
                            "-" + String(day < 10 ? "0" : "") + String(day) + " " +
                            String(weekDaysChinese[weekday]) + " " + String(hours < 10 ? "0" : "") +
                            String(hours) + ":" + String(minutes < 10 ? "0" : "") + String(minutes);

    int timeW = u8g2Fonts.getUTF8Width(currentTimeStr.c_str());
    int timeX = (display.width() - timeW) / 2 - 20;
    int timeY = dividerY + 15;
    u8g2Fonts.setCursor(timeX, timeY);
    u8g2Fonts.print(currentTimeStr);

    int toleftX = 5;
    int toleftY = dividerY + 15;
    u8g2Fonts.setCursor(toleftX, toleftY);
    u8g2Fonts.print("< 主页");

    int torightX = 172;
    int torightY = dividerY + 15;
    u8g2Fonts.setCursor(torightX, torightY);
    u8g2Fonts.print("闹钟 >");

  } while (display.nextPage());
  if (full)
    refreshInProgress = false;
}

void handleCalendarRightButton() {
  if (currentCursor == CURSOR_NAVIGATE) {
    int next = (currentPage + 1) % totalPages;
    switchPageAndFullRefresh(next);
  } else if (currentCursor == CURSOR_YEAR) {
    selectedYear++;
    if (selectedYear > 2030)
      selectedYear = 2030;
    int maxDay = getDaysInMonth(selectedYear, selectedMonth);
    if (selectedDay > maxDay)
      selectedDay = maxDay;
    renderCalendarPage(false);
  } else if (currentCursor == CURSOR_MONTH) {
    selectedMonth++;
    if (selectedMonth > 12) {
      selectedMonth = 1;
      selectedYear++;
      if (selectedYear > 2030) {
        selectedYear = 2030;
        selectedMonth = 12;
      }
    }
    int maxDay = getDaysInMonth(selectedYear, selectedMonth);
    if (selectedDay > maxDay)
      selectedDay = maxDay;
    renderCalendarPage(false);
  } else if (currentCursor == CURSOR_DAY) {
    selectedDay++;
    int maxDay = getDaysInMonth(selectedYear, selectedMonth);
    if (selectedDay > maxDay) {
      selectedDay = 1;
      selectedMonth++;
      if (selectedMonth > 12) {
        selectedMonth = 1;
        selectedYear++;
        if (selectedYear > 2030) {
          selectedYear = 2030;
          selectedMonth = 12;
          selectedDay = getDaysInMonth(selectedYear, selectedMonth);
        }
      }
    }
    renderCalendarPage(false);
  }
}

void handleCalendarLeftButton() {
  if (currentCursor == CURSOR_NAVIGATE) {
    int prev = (currentPage - 1 + totalPages) % totalPages;
    switchPageAndFullRefresh(prev);
  } else if (currentCursor == CURSOR_YEAR) {
    selectedYear--;
    if (selectedYear < 2020)
      selectedYear = 2020;
    int maxDay = getDaysInMonth(selectedYear, selectedMonth);
    if (selectedDay > maxDay)
      selectedDay = maxDay;
    renderCalendarPage(false);
  } else if (currentCursor == CURSOR_MONTH) {
    selectedMonth--;
    if (selectedMonth < 1) {
      selectedMonth = 12;
      selectedYear--;
      if (selectedYear < 2020) {
        selectedYear = 2020;
      }
    }
    int maxDay = getDaysInMonth(selectedYear, selectedMonth);
    if (selectedDay > maxDay)
      selectedDay = maxDay;
    renderCalendarPage(false);
  } else if (currentCursor == CURSOR_DAY) {
    selectedDay--;
    if (selectedDay < 1) {
      selectedMonth--;
      if (selectedMonth < 1) {
        selectedMonth = 12;
        selectedYear--;
        if (selectedYear < 2020) {
          selectedYear = 2020;
        }
      }
      selectedDay = getDaysInMonth(selectedYear, selectedMonth);
    }
    renderCalendarPage(false);
  }
}

void handleCalendarCenterButton() {
  currentCursor = (CalendarCursor)((currentCursor + 1) % 4);
  renderCalendarPage(false);
}

void calendarSetSelectedDate(int year, int month, int day) {
  selectedYear = year;
  selectedMonth = month;
  selectedDay = day;
}

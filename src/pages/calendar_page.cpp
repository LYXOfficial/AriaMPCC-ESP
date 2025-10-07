#include "calendar_page.h"
#include "../app_context.h"
#include "../utils/lunar.h"

CalendarPage::CalendarPage() {
  // default to today
  timeClient.update();
  time_t rawtime = timeClient.getEpochTime();
  struct tm *timeinfo = localtime(&rawtime);
  selectedYear = timeinfo->tm_year + 1900;
  selectedMonth = timeinfo->tm_mon + 1;
  selectedDay = timeinfo->tm_mday;
  currentCursor = CURSOR_NAVIGATE;
}

bool CalendarPage::isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}
int CalendarPage::getDaysInMonth(int year, int month) {
  int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year))
    return 29;
  return daysInMonth[month - 1];
}
int CalendarPage::getFirstDayOfWeek(int year, int month) {
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

String CalendarPage::lunarDayToChinese(int d) {
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
String CalendarPage::getLunarDate(int y, int m, int d) {
  if (y < LUNAR_MIN_YEAR || y > LUNAR_MAX_YEAR) {
    return String("农历") + String(y) + String("年") + String(m) +
           String("月") + String(d) + String("日");
  }
  Solar s{y, m, d};
  Lunar L = SolarToLunar(s);
  static const char *stems[] = {"甲", "乙", "丙", "丁", "戊",
                                "己", "庚", "辛", "壬", "癸"};
  static const char *branches[] = {"子", "丑", "寅", "卯", "辰", "巳",
                                   "午", "未", "申", "酉", "戌", "亥"};
  int idxStem = (L.year - 4) % 10;
  if (idxStem < 0)
    idxStem += 10;
  int idxBranch = (L.year - 4) % 12;
  if (idxBranch < 0)
    idxBranch += 12;
  String gzYear = String(stems[idxStem]) + String(branches[idxBranch]);
  static const char *lunarMonthNamesChinese[] = {
      "正月", "二月", "三月", "四月", "五月", "六月",
      "七月", "八月", "九月", "十月", "冬月", "腊月"};
  String monthName = "";
  int mi = L.month - 1;
  if (mi >= 0 && mi < 12)
    monthName =
        String((L.isLeap ? "闰" : "")) + String(lunarMonthNamesChinese[mi]);
  else
    monthName = String(L.month) + "月";
  String dayName = lunarDayToChinese(L.day);
  return gzYear + String("年") + monthName + dayName;
}

void CalendarPage::render(bool full) {
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
      int monthW =
          u8g2Fonts.getUTF8Width((String(selectedMonth) + "月").c_str());
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
    int lunarY = dayY + 20;
    u8g2Fonts.setCursor(leftX, lunarY);
    u8g2Fonts.print(lunarStr);

    int calendarX = 110;
    int calendarY = 15;
    int cellW = 14;
    int cellH = 12;
    String ymStr = String(selectedYear) + "年 " + String(selectedMonth) + "月";
    u8g2Fonts.setCursor(calendarX, calendarY);
    u8g2Fonts.print(ymStr);
    const char *weekHeader[] = {"日", "一", "二", "三", "四", "五", "六"};
    int headerY = calendarY + 18;
    for (int i = 0; i < 7; i++) {
      u8g2Fonts.setCursor(calendarX + i * cellW, headerY);
      u8g2Fonts.print(weekHeader[i]);
    }
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
    String currentTimeStr =
        String(year) + "-" + String(month < 10 ? "0" : "") + String(month) +
        "-" + String(day < 10 ? "0" : "") + String(day) + " " +
        String(weekDaysChinese[weekday]) + " " + String(hours < 10 ? "0" : "") +
        String(hours) + ":" + String(minutes < 10 ? "0" : "") + String(minutes);
    int timeW = u8g2Fonts.getUTF8Width(currentTimeStr.c_str());
    int timeX = (display.width() - timeW) / 2 - 20;
    int timeY = dividerY + 15;
    u8g2Fonts.setCursor(timeX, timeY);
    u8g2Fonts.print(currentTimeStr);
    u8g2Fonts.setCursor(5, dividerY + 15);
    u8g2Fonts.print("< 主页");
    u8g2Fonts.setCursor(172, dividerY + 15);
    u8g2Fonts.print("闹钟 >");
  } while (display.nextPage());
  if (full)
    refreshInProgress = false;
}

bool CalendarPage::onRight() {
  if (currentCursor == CURSOR_NAVIGATE) {
    int next = currentPage + 1; // wrapping handled by switchPageAndFullRefresh
    switchPageAndFullRefresh(next);
  } else if (currentCursor == CURSOR_YEAR) {
  selectedYear = std::min(selectedYear + 1, LUNAR_MAX_YEAR);
    selectedDay =
        std::min(selectedDay, getDaysInMonth(selectedYear, selectedMonth));
    render(false);
  } else if (currentCursor == CURSOR_MONTH) {
    selectedMonth++;
    if (selectedMonth > 12) {
      selectedMonth = 1;
  selectedYear = std::min(selectedYear + 1, LUNAR_MAX_YEAR);
    }
    selectedDay =
        std::min(selectedDay, getDaysInMonth(selectedYear, selectedMonth));
    render(false);
  } else if (currentCursor == CURSOR_DAY) {
    selectedDay++;
    int maxDay = getDaysInMonth(selectedYear, selectedMonth);
    if (selectedDay > maxDay) {
      selectedDay = 1;
      selectedMonth++;
      if (selectedMonth > 12) {
        selectedMonth = 1;
  selectedYear = std::min(selectedYear + 1, LUNAR_MAX_YEAR);
      }
    }
    render(false);
  }
  return true;
}

bool CalendarPage::onLeft() {
  if (currentCursor == CURSOR_NAVIGATE) {
    int prev = currentPage - 1; // wrapping handled by switchPageAndFullRefresh
    switchPageAndFullRefresh(prev);
  } else if (currentCursor == CURSOR_YEAR) {
  selectedYear = std::max(selectedYear - 1, LUNAR_MIN_YEAR);
    selectedDay =
        std::min(selectedDay, getDaysInMonth(selectedYear, selectedMonth));
    render(false);
  } else if (currentCursor == CURSOR_MONTH) {
    selectedMonth--;
    if (selectedMonth < 1) {
      selectedMonth = 12;
  selectedYear = std::max(selectedYear - 1, LUNAR_MIN_YEAR);
    }
    selectedDay =
        std::min(selectedDay, getDaysInMonth(selectedYear, selectedMonth));
    render(false);
  } else if (currentCursor == CURSOR_DAY) {
    selectedDay--;
    if (selectedDay < 1) {
      selectedMonth--;
      if (selectedMonth < 1) {
        selectedMonth = 12;
  selectedYear = std::max(selectedYear - 1, LUNAR_MIN_YEAR);
      }
      selectedDay = getDaysInMonth(selectedYear, selectedMonth);
    }
    render(false);
  }
  return true;
}

bool CalendarPage::onCenter() {
  currentCursor = static_cast<Cursor>((currentCursor + 1) % 4);
  render(false);
  return true;
}

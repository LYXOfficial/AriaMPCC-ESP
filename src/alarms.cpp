#include "alarms.h"
#include "app_context.h"
#include "pinconf.h"

// State (moved from main.cpp)
static Alarm alarms[5];
static const char *ALARM_NS = "alarms_cfg";

// Ex-constants from main.cpp
static const int ALARM_FIELD_HOUR = 0;
static const int ALARM_FIELD_MIN = 1;
static const int ALARM_FIELD_WEEK_START = 2;
static const int ALARM_FIELD_TONE = 9;
static const int ALARM_FIELD_ENABLED = 10;

static int alarmHighlightedRow = -1;
static int alarmFieldCursor = -1;

// Externs from main
extern void switchPageAndFullRefresh(int page);
extern int pageSwitchCount;
extern unsigned long lastInteraction;

// Helpers
static Alarm getAlarm(int idx) {
  if (idx < 0)
    idx = 0;
  if (idx > 4)
    idx = 4;
  return alarms[idx];
}
static void setAlarm(int idx, const Alarm &a) {
  if (idx < 0 || idx > 4)
    return;
  alarms[idx] = a;
}

void alarmsInit() {
  // default values
  for (int i = 0; i < 5; i++) {
    alarms[i] = {7, 0, 0, 1, false};
  }
}

void loadAlarms() {
  Preferences p;
  if (p.begin(ALARM_NS, true)) {
    size_t need = sizeof(alarms);
    size_t got = p.getBytes("alarms", &alarms, need);
    p.end();
    if (got != need) {
      // keep defaults
    }
  }
}

void saveAlarms() {
  Preferences p;
  if (p.begin(ALARM_NS, false)) {
    p.putBytes("alarms", &alarms, sizeof(alarms));
    p.end();
  }
}

// drawing
static void drawAlarmRow(int rowIndex, int x, int y, int rowW, int rowH,
                         int fieldCursorLocal, bool highlightRow) {
  Alarm a = alarms[rowIndex];
  int colX = x;
  u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
  String th = (a.hour < 10 ? "0" : "") + String(a.hour);
  String tm = (a.minute < 10 ? "0" : "") + String(a.minute);
  if (highlightRow) {
    display.fillRect(x, y - rowH - 1, rowW, rowH, GxEPD_BLACK);
    u8g2Fonts.setForegroundColor(GxEPD_WHITE);
  } else {
    display.fillRect(x, y - rowH + 2, rowW, rowH, GxEPD_WHITE);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  }
  auto drawField = [&](int fx, const String &txt, bool reverse) {
    int w = u8g2Fonts.getUTF8Width(txt.c_str());
    int tx = fx;
    int ty = y - 4;
    if (reverse) {
      display.fillRect(tx - 2, ty - 12, w + 4, 16, GxEPD_WHITE);
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);
      u8g2Fonts.setCursor(tx, ty);
      u8g2Fonts.print(txt);
      u8g2Fonts.setForegroundColor(highlightRow ? GxEPD_WHITE : GxEPD_BLACK);
    } else {
      u8g2Fonts.setCursor(tx, ty);
      u8g2Fonts.print(txt);
    }
  };

  int fx = colX;
  bool rev = (fieldCursorLocal == ALARM_FIELD_HOUR);
  drawField(fx, th, rev);
  fx += 20;
  rev = (fieldCursorLocal == ALARM_FIELD_MIN);
  drawField(fx, tm, rev);
  fx += 28;
  const char *wdnames[] = {"日", "一", "二", "三", "四", "五", "六"};
  for (int i = 0; i < 7; i++) {
    bool on = (a.weekdays & (1 << i));
    String txt = String(wdnames[i]);
    if (on) {
      int w = u8g2Fonts.getUTF8Width(txt.c_str());
      int tx = fx;
      int ty = y - 4;
      int rectX = tx - 2;
      int rectY = ty - 12;
      int rectW = w + 4;
      int rectH = 16;
      int rectColor = highlightRow ? GxEPD_WHITE : GxEPD_BLACK;
      display.drawRect(rectX, rectY, rectW, rectH, rectColor);
      u8g2Fonts.setForegroundColor(highlightRow ? GxEPD_WHITE : GxEPD_BLACK);
      u8g2Fonts.setCursor(tx, ty);
      u8g2Fonts.print(txt);
      u8g2Fonts.setForegroundColor(highlightRow ? GxEPD_WHITE : GxEPD_BLACK);
    } else {
      bool revwd = (fieldCursorLocal == (ALARM_FIELD_WEEK_START + i));
      drawField(fx, txt, revwd);
    }
    fx += 18;
  }
  bool revTone = (fieldCursorLocal == ALARM_FIELD_TONE);
  drawField(fx, String(a.tone), revTone);
  fx += 24;
  bool revEn = (fieldCursorLocal == ALARM_FIELD_ENABLED);
  drawField(fx, String(a.enabled ? "开" : "关"), revEn);
}

void renderAlarmPage(bool full) {
  const int startY = 20;                 // 与 main_old.cpp 一致
  const int rowH = 15;                   // 行高统一为 15
  const int rowW = display.width() - 20; // 留出左右内边距
  const int dividerY = display.height() - 18;
  if (full) {
    refreshInProgress = true;
    display.setFullWindow();
  } else {
    int ph = dividerY - startY + 20;  // 覆盖列表至分割线下方
    int py = startY - 4;              // 轻微上移以包含高亮背景
    int px = 0;
    int pw = display.width();
    display.setPartialWindow(px, py, pw, ph);
  }
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    for (int r = 0; r < 5; r++) {
      int y = startY + r * rowH + rowH;
      bool h = (alarmHighlightedRow == r);
      int fieldLocal = (h && alarmFieldCursor >= 0) ? alarmFieldCursor : -1;
      drawAlarmRow(r, 0, y, rowW, rowH, fieldLocal, h);
    }
    display.drawFastHLine(0, dividerY, display.width(), GxEPD_BLACK);
    u8g2Fonts.setCursor(5, dividerY + 15);
    u8g2Fonts.print("< 日历");
    u8g2Fonts.setCursor(172, dividerY + 15);
    u8g2Fonts.print("文件 >");
    String title = "闹钟";
    int tw = u8g2Fonts.getUTF8Width(title.c_str());
    int cx = (display.width() - tw) / 2 - 20;
    u8g2Fonts.setCursor(cx, dividerY + 15);
    u8g2Fonts.print(title);
  } while (display.nextPage());
  if (full)
    refreshInProgress = false;
}

void handleAlarmRightButton() {
  if (alarmHighlightedRow < 0) {
    int next = (currentPage + 1) % totalPages;
    switchPageAndFullRefresh(next);
    return;
  }
  if (alarmFieldCursor < 0) {
    alarmFieldCursor = ALARM_FIELD_HOUR;
    renderAlarmPage(false);
    lastInteraction = millis();
    return;
  }
  alarmFieldCursor++;
  if (alarmFieldCursor > ALARM_FIELD_ENABLED) {
    alarmFieldCursor = -1;
  }
  renderAlarmPage(false);
  lastInteraction = millis();
}

void handleAlarmLeftButton() {
  if (alarmHighlightedRow < 0) {
    int prev = (currentPage - 1 + totalPages) % totalPages;
    switchPageAndFullRefresh(prev);
    return;
  }
  if (alarmFieldCursor < 0) {
    alarmFieldCursor = ALARM_FIELD_ENABLED;
    renderAlarmPage(false);
    lastInteraction = millis();
    return;
  }
  alarmFieldCursor--;
  if (alarmFieldCursor < ALARM_FIELD_HOUR) {
    alarmFieldCursor = -1;
  }
  renderAlarmPage(false);
  lastInteraction = millis();
}

void handleAlarmCenterButton() {
  if (alarmHighlightedRow < 0) {
    int oldRow = alarmHighlightedRow;
    alarmHighlightedRow = 0;
    alarmFieldCursor = -1;
    if (oldRow != alarmHighlightedRow && alarmHighlightedRow >= 0) {
      pageSwitchCount++;
    }
    renderAlarmPage(false);
    lastInteraction = millis();
    return;
  }
  if (alarmFieldCursor < 0) {
    int oldRow = alarmHighlightedRow;
    int nextRow = alarmHighlightedRow + 1;
    if (nextRow >= 5) {
      alarmHighlightedRow = -1;
      alarmFieldCursor = -1;
    } else {
      alarmHighlightedRow = nextRow;
      alarmFieldCursor = -1;
    }
    if (oldRow != alarmHighlightedRow && alarmHighlightedRow >= 0) {
      pageSwitchCount++;
    }
    renderAlarmPage(false);
    lastInteraction = millis();
    return;
  }

  Alarm &a = alarms[alarmHighlightedRow];
  if (alarmFieldCursor == ALARM_FIELD_HOUR) {
    a.hour = (a.hour + 1) % 24;
  } else if (alarmFieldCursor == ALARM_FIELD_MIN) {
    a.minute = (a.minute + 1) % 60;
  } else if (alarmFieldCursor >= ALARM_FIELD_WEEK_START &&
             alarmFieldCursor < ALARM_FIELD_WEEK_START + 7) {
    int wd = alarmFieldCursor - ALARM_FIELD_WEEK_START;
    a.weekdays ^= (1 << wd);
  } else if (alarmFieldCursor == ALARM_FIELD_TONE) {
    a.tone = (a.tone % 5) + 1;
  } else if (alarmFieldCursor == ALARM_FIELD_ENABLED) {
    a.enabled = !a.enabled;
  }
  setAlarm(alarmHighlightedRow, a);
  saveAlarms();
  renderAlarmPage(false);
  lastInteraction = millis();
}

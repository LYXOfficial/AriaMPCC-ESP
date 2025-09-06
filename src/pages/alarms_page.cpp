#include "alarms_page.h"
#include "../app_context.h"
#include "../defines/pinconf.h"

// Internal state same as legacy
static Alarm s_alarms[5];
static const char *ALARM_NS = "alarms_cfg";

// field indices
static const int ALARM_FIELD_HOUR = 0;
static const int ALARM_FIELD_MIN = 1;
static const int ALARM_FIELD_WEEK_START = 2;
static const int ALARM_FIELD_TONE = 9;
static const int ALARM_FIELD_ENABLED = 10;

AlarmsPage::AlarmsPage() {
  // defaults
  for (int i = 0; i < 5; i++) s_alarms[i] = {7, 0, 0, 1, false};
  load();
}

void AlarmsPage::load() {
  Preferences p;
  if (p.begin(ALARM_NS, true)) {
    size_t need = sizeof(s_alarms); size_t got = p.getBytes("alarms", &s_alarms, need); p.end();
    if (got != need) { /* keep defaults */ }
  }
}

void AlarmsPage::save() {
  Preferences p;
  if (p.begin(ALARM_NS, false)) { p.putBytes("alarms", &s_alarms, sizeof(s_alarms)); p.end(); }
}

static Alarm getAlarm(int idx) { if (idx < 0) idx = 0; if (idx > 4) idx = 4; return s_alarms[idx]; }
static void setAlarm(int idx, const Alarm &a) { if (idx < 0 || idx > 4) return; s_alarms[idx] = a; }

void AlarmsPage::render(bool full) {
  const int startY = 20; const int rowH = 15; const int rowW = display.width() - 20; const int dividerY = display.height() - 18;
  if (full) { refreshInProgress = true; display.setFullWindow(); } else {
    int ph = dividerY - startY + 20; int py = startY - 4; int px = 0; int pw = display.width(); display.setPartialWindow(px, py, pw, ph);
  }
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    for (int r = 0; r < 5; r++) {
      int y = startY + r * rowH + rowH; bool h = (highlightedRow == r); int fieldLocal = (h && fieldCursor >= 0) ? fieldCursor : -1;
      drawRow(r, 0, y, rowW, rowH, fieldLocal, h);
    }
    display.drawFastHLine(0, dividerY, display.width(), GxEPD_BLACK);
    u8g2Fonts.setCursor(5, dividerY + 15); u8g2Fonts.print("< 日历");
    u8g2Fonts.setCursor(172, dividerY + 15); u8g2Fonts.print("文件 >");
    String title = "闹钟"; int tw = u8g2Fonts.getUTF8Width(title.c_str()); int cx = (display.width() - tw) / 2 - 20; u8g2Fonts.setCursor(cx, dividerY + 15); u8g2Fonts.print(title);
  } while (display.nextPage());
  if (full) refreshInProgress = false;
}

void AlarmsPage::onRight() {
  if (highlightedRow < 0) { switchPageAndFullRefresh(currentPage + 1); return; }
  if (fieldCursor < 0) { fieldCursor = ALARM_FIELD_HOUR; render(false); lastInteraction = millis(); return; }
  fieldCursor++; if (fieldCursor > ALARM_FIELD_ENABLED) fieldCursor = -1; render(false); lastInteraction = millis();
}

void AlarmsPage::onLeft() {
  if (highlightedRow < 0) { switchPageAndFullRefresh(currentPage - 1); return; }
  if (fieldCursor < 0) { fieldCursor = ALARM_FIELD_ENABLED; render(false); lastInteraction = millis(); return; }
  fieldCursor--; if (fieldCursor < ALARM_FIELD_HOUR) fieldCursor = -1; render(false); lastInteraction = millis();
}

void AlarmsPage::onCenter() {
  if (highlightedRow < 0) {
    int old = highlightedRow; highlightedRow = 0; fieldCursor = -1; if (old != highlightedRow && highlightedRow >= 0) { /* in-page selection, do not count as page switch */ } render(false); lastInteraction = millis(); return;
  }
  if (fieldCursor < 0) {
    int oldRow = highlightedRow; int nextRow = highlightedRow + 1; if (nextRow >= 5) { highlightedRow = -1; fieldCursor = -1; } else { highlightedRow = nextRow; fieldCursor = -1; }
    if (oldRow != highlightedRow && highlightedRow >= 0) { /* row navigation inside page; do not count */ } render(false); lastInteraction = millis(); return;
  }
  Alarm &a = s_alarms[highlightedRow];
  if (fieldCursor == ALARM_FIELD_HOUR) a.hour = (a.hour + 1) % 24;
  else if (fieldCursor == ALARM_FIELD_MIN) a.minute = (a.minute + 1) % 60;
  else if (fieldCursor >= ALARM_FIELD_WEEK_START && fieldCursor < ALARM_FIELD_WEEK_START + 7) { int wd = fieldCursor - ALARM_FIELD_WEEK_START; a.weekdays ^= (1 << wd); }
  else if (fieldCursor == ALARM_FIELD_TONE) a.tone = (a.tone % 5) + 1;
  else if (fieldCursor == ALARM_FIELD_ENABLED) a.enabled = !a.enabled;
  setAlarm(highlightedRow, a); save(); render(false); lastInteraction = millis();
}

void AlarmsPage::drawRow(int rowIndex, int x, int y, int rowW, int rowH, int fieldCursorLocal, bool highlightRow) {
  Alarm a = s_alarms[rowIndex]; int colX = x; u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
  String th = (a.hour < 10 ? "0" : "") + String(a.hour); String tm = (a.minute < 10 ? "0" : "") + String(a.minute);
  if (highlightRow) { display.fillRect(x, y - rowH - 1, rowW, rowH, GxEPD_BLACK); u8g2Fonts.setForegroundColor(GxEPD_WHITE); }
  else { display.fillRect(x, y - rowH + 2, rowW, rowH, GxEPD_WHITE); u8g2Fonts.setForegroundColor(GxEPD_BLACK); }
  auto drawField = [&](int fx, const String &txt, bool reverse) {
    int w = u8g2Fonts.getUTF8Width(txt.c_str()); int tx = fx; int ty = y - 4;
    if (reverse) { display.fillRect(tx - 2, ty - 12, w + 4, 16, GxEPD_WHITE); u8g2Fonts.setForegroundColor(GxEPD_BLACK); u8g2Fonts.setCursor(tx, ty); u8g2Fonts.print(txt); u8g2Fonts.setForegroundColor(highlightRow ? GxEPD_WHITE : GxEPD_BLACK); }
    else { u8g2Fonts.setCursor(tx, ty); u8g2Fonts.print(txt); }
  };
  int fx = colX; bool rev = (fieldCursorLocal == ALARM_FIELD_HOUR); drawField(fx, th, rev); fx += 20;
  rev = (fieldCursorLocal == ALARM_FIELD_MIN); drawField(fx, tm, rev); fx += 28;
  const char *wdnames[] = {"日", "一", "二", "三", "四", "五", "六"};
  for (int i = 0; i < 7; i++) { bool on = (a.weekdays & (1 << i)); String txt = String(wdnames[i]); if (on) { int w = u8g2Fonts.getUTF8Width(txt.c_str()); int tx = fx; int ty = y - 4; int rectX = tx - 2; int rectY = ty - 12; int rectW = w + 4; int rectH = 16; int rectColor = highlightRow ? GxEPD_WHITE : GxEPD_BLACK; display.drawRect(rectX, rectY, rectW, rectH, rectColor); u8g2Fonts.setForegroundColor(highlightRow ? GxEPD_WHITE : GxEPD_BLACK); u8g2Fonts.setCursor(tx, ty); u8g2Fonts.print(txt); u8g2Fonts.setForegroundColor(highlightRow ? GxEPD_WHITE : GxEPD_BLACK);} else { bool revwd = (fieldCursorLocal == (ALARM_FIELD_WEEK_START + i)); drawField(fx, txt, revwd);} fx += 18; }
  bool revTone = (fieldCursorLocal == ALARM_FIELD_TONE); drawField(fx, String(a.tone), revTone); fx += 24; bool revEn = (fieldCursorLocal == ALARM_FIELD_ENABLED); drawField(fx, String(a.enabled ? "开" : "关"), revEn);
}

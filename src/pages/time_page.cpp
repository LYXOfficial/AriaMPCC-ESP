#include "time_page.h"
#include "app_context.h"
#include "defines/RightImage.h"
#include "pages/page_manager.h"
#include "utils/utils.h"

// Define moved globals (declared extern in app_context.h)
String lastDisplayedTime = "";
String lastDisplayedDate = "";
String currentHitokoto = "";
String lastDisplayedHitokoto = "";
unsigned long lastFullRefresh = 0;
unsigned long lastHitokotoUpdate = 0;
const unsigned long fullRefreshInterval = 8UL * 60UL * 1000UL; // 8分钟全刷一次
const unsigned long hitokotoUpdateInterval =
    5UL * 60UL * 1000UL; // 5分钟更新一次一言

// Weather related
String currentCity = "";
String currentWeather = "";
String currentTemp = "";
unsigned long lastWeatherUpdate = 0;
const unsigned long weatherUpdateInterval = 10UL * 60UL * 1000UL; // 10分钟

volatile bool refreshInProgress = false;

extern PageManager gPageMgr;

// Forward to utils
String getHitokoto();

HomeTimePage::HomeTimePage(SwitchPageFn switcher) : switchPage(switcher) {}

void HomeTimePage::render(bool full) {
  // Minimal render: log page and requestedFull only. Other heuristics
  // were computed here but not used; removed to reduce redundant logic.
  Serial.println("HomeTimePage::render page=" + String(currentPage) +
                 " requestedFull=" + String(full ? 1 : 0));
  if (full) {
    renderFull();
  } else {
    renderPartial();
  }
}

void HomeTimePage::onLeft() {
  if (switchPage)
    switchPage(currentPage - 1);
}

void HomeTimePage::onRight() {
  if (switchPage)
    switchPage(currentPage + 1);
}

void HomeTimePage::onCenter() {
  const String msg = "刷新信息中...";
  u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
  int textW = u8g2Fonts.getUTF8Width(msg.c_str());
  const int padX = 8;
  const int padY = 6;
  int pw = textW + padX * 2;
  int ph = 12 + padY * 2;
  if (pw > display.width())
    pw = display.width();
  if (ph > display.height())
    ph = display.height();
  int px = (display.width() - pw) / 2;
  int py = 30;
  display.setPartialWindow(px, py, pw, ph);
  display.firstPage();
  do {
    display.fillRect(px, py, pw, ph, GxEPD_WHITE);
    display.drawRect(px, py, pw, ph, GxEPD_BLACK);
    int tx = px + (pw - textW) / 2;
    int ty = py + padY + 12;
    u8g2Fonts.setCursor(tx, ty);
    u8g2Fonts.print(msg);
  } while (display.nextPage());
}

void HomeTimePage::renderFull() {
  timeClient.update();
  int hours = timeClient.getHours();
  int minutes = timeClient.getMinutes();
  String currentTime = String(hours < 10 ? "0" : "") + String(hours) + ":" +
                       String(minutes < 10 ? "0" : "") + String(minutes);
  time_t rawtime = timeClient.getEpochTime();
  struct tm *timeinfo = localtime(&rawtime);
  int year = timeinfo->tm_year + 1900;
  int month = timeinfo->tm_mon + 1;
  int day = timeinfo->tm_mday;
  int weekday = timeinfo->tm_wday;
  String currentDate = String(year) + "-" + String(month < 10 ? "0" : "") +
                       String(month) + "-" + String(day < 10 ? "0" : "") +
                       String(day) + " " + String(weekDaysChinese[weekday]);

  if (millis() - lastHitokotoUpdate > hitokotoUpdateInterval ||
      currentHitokoto == "") {
    currentHitokoto = getHitokoto();
    lastHitokotoUpdate = millis();
  }
  String oneLine = currentHitokoto;
  oneLine.replace('\n', ' ');
  oneLine.trim();

  if (millis() - lastWeatherUpdate > weatherUpdateInterval ||
      currentCity == "") {
    double lat = 0, lon = 0;
    String cityEn;
    String w, t;
    bool ok = false;
    bool locOk = getLocationByIP(lat, lon, cityEn);
    if (locOk) {
      ok = getWeatherByCoordsOpenMeteo(lat, lon, w, t);
      String cityCN = getCityByIP();
      if (cityCN.length() > 0)
        currentCity = cityCN;
      else if (cityEn.length() > 0)
        currentCity = cityEn;
    }
    if (!ok) {
      String city = getCityByIP();
      if (city.length() > 0)
        currentCity = city;
      ok = getWeatherForCity(currentCity, w, t);
    }
    if (ok) {
      currentWeather = w;
      currentTemp = t;
      lastWeatherUpdate = millis();
    }
  }

  refreshInProgress = true;
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8g2Fonts.setFont(u8g2_font_logisoso32_tf);
    int timeX = 10;
    int timeBaseline = display.height() / 2;
    u8g2Fonts.setCursor(timeX, timeBaseline);
    u8g2Fonts.print(currentTime);

    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    int dateY = timeBaseline + 15;
    int dateX = 12;
    u8g2Fonts.setCursor(dateX, dateY);
    u8g2Fonts.print(currentDate);

    String weatherLine = "";
    if (currentCity.length() > 0)
      weatherLine += currentCity;
    if (currentTemp.length() > 0) {
      if (weatherLine.length() > 0)
        weatherLine += " ";
      weatherLine += currentTemp + String("°C");
    }
    if (currentWeather.length() > 0) {
      if (weatherLine.length() > 0)
        weatherLine += " ";
      weatherLine += currentWeather;
    }
    int weatherY = dateY + 18;
    u8g2Fonts.setCursor(dateX, weatherY);
    u8g2Fonts.print(weatherLine);

    int iconX_full = 115;
    int iconY_full = timeBaseline - (RIGHT_IMAGE_H / 2);
    if (iconY_full < 0)
      iconY_full = 0;
    display.drawBitmap(iconX_full, iconY_full, RightImage, RIGHT_IMAGE_W,
                       RIGHT_IMAGE_H, GxEPD_BLACK);

    int dividerY = display.height() - 18;
    display.drawFastHLine(0, dividerY, display.width(), GxEPD_BLACK);
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    int availWidth = display.width() - 65;
    String fitted = fitToWidthSingleLine(oneLine, availWidth);
    int hitokotoX = 15;
    int hitokotoY = dividerY + 15;
    u8g2Fonts.setCursor(hitokotoX, hitokotoY);
    u8g2Fonts.print(fitted);
  } while (display.nextPage());

  lastFullRefresh = millis();
  lastDisplayedTime = currentTime;
  lastDisplayedDate = currentDate;
  lastDisplayedHitokoto = oneLine;
  refreshInProgress = false;
}

void HomeTimePage::renderPartial() {
  timeClient.update();
  int hours = timeClient.getHours();
  int minutes = timeClient.getMinutes();
  String currentTime = String(hours < 10 ? "0" : "") + String(hours) + ":" +
                       String(minutes < 10 ? "0" : "") + String(minutes);
  time_t rawtime = timeClient.getEpochTime();
  struct tm *timeinfo = localtime(&rawtime);
  int year = timeinfo->tm_year + 1900;
  int month = timeinfo->tm_mon + 1;
  int day = timeinfo->tm_mday;
  int weekday = timeinfo->tm_wday;
  String currentDate = String(year) + "-" + String(month < 10 ? "0" : "") +
                       String(month) + "-" + String(day < 10 ? "0" : "") +
                       String(day) + " " + String(weekDaysChinese[weekday]);

  if (currentTime == lastDisplayedTime && currentDate == lastDisplayedDate)
    return;

  String oneLine = currentHitokoto;
  oneLine.replace('\n', ' ');
  oneLine.trim();

  int timeAreaX = 0;
  int timeBaseline = display.height() / 2;
  int iconTopEstimate = display.height() / 2 - (RIGHT_IMAGE_H / 2);
  int timeAreaY = display.height() / 2 - 40;
  if (iconTopEstimate < timeAreaY) {
    timeAreaY = iconTopEstimate;
    if (timeAreaY < 0)
      timeAreaY = 0;
  }
  int timeAreaW = display.width();
  int timeAreaH = 96;
  int iconBottom = iconTopEstimate + RIGHT_IMAGE_H;
  int areaBottom = timeAreaY + timeAreaH;
  if (iconBottom > areaBottom) {
    timeAreaH = iconBottom - timeAreaY;
    if (timeAreaY + timeAreaH > display.height())
      timeAreaH = display.height() - timeAreaY;
  }

  display.setPartialWindow(timeAreaX, timeAreaY, timeAreaW, timeAreaH);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8g2Fonts.setFont(u8g2_font_logisoso32_tf);
    u8g2Fonts.setCursor(10, timeBaseline);
    u8g2Fonts.print(currentTime);
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    int dateY = timeBaseline + 15;
    int dateX = 12;
    u8g2Fonts.setCursor(dateX, dateY);
    u8g2Fonts.print(currentDate);

    String weatherLine = "";
    if (currentCity.length() > 0)
      weatherLine += currentCity;
    if (currentTemp.length() > 0) {
      if (weatherLine.length() > 0)
        weatherLine += " ";
      weatherLine += currentTemp + String("°C");
    }
    if (currentWeather.length() > 0) {
      if (weatherLine.length() > 0)
        weatherLine += " ";
      weatherLine += currentWeather;
    }
    int weatherY = dateY + 18;
    u8g2Fonts.setCursor(dateX, weatherY);
    u8g2Fonts.print(weatherLine);

    int iconX_part = 115;
    int iconY_part = timeBaseline - (RIGHT_IMAGE_H / 2);
    if (iconY_part < 0)
      iconY_part = 0;
    display.drawBitmap(iconX_part, iconY_part, RightImage, RIGHT_IMAGE_W,
                       RIGHT_IMAGE_H, GxEPD_BLACK);

    int dividerY = display.height() - 18;
    display.drawFastHLine(0, dividerY, display.width(), GxEPD_BLACK);
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    int availWidth = display.width() - 60;
    String fitted = fitToWidthSingleLine(oneLine, availWidth);
    int hitokotoX = 15;
    int hitokotoY = dividerY + 15;
    u8g2Fonts.setCursor(hitokotoX, hitokotoY);
    u8g2Fonts.print(fitted);
  } while (display.nextPage());

  lastDisplayedTime = currentTime;
  lastDisplayedDate = currentDate;
}

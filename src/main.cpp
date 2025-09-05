#include "Audio.h"
#include "RightImage.h"
#include "pinconf.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <SD.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <time.h>
GxEPD2_BW<GxEPD2_213_B72, GxEPD2_213_B72::HEIGHT>
    display(GxEPD2_213_B72(EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN));

Audio audio;
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;
// TinyLunar removed: using internal lunar conversion helper
#include "lunar.h"

// NTP 相关
WiFiUDP ntpUDP;
NTPClient
    timeClient(ntpUDP, "ntp.aliyun.com", 8 * 3600,
               60000); // 使用阿里云NTP服务器，中国时区(+8)，每分钟更新一次

// 时钟显示相关变量
String lastDisplayedTime = "";
String lastDisplayedDate = "";
String currentHitokoto = "";
String lastDisplayedHitokoto = "";
unsigned long lastFullRefresh = 0;
unsigned long lastHitokotoUpdate = 0;
const unsigned long fullRefreshInterval = 8 * 60 * 1000;    // 8分钟全刷一次
const unsigned long hitokotoUpdateInterval = 5 * 60 * 1000; // 5分钟更新一次一言
// 天气相关
String currentCity = "";
String currentWeather = ""; // 描述
String currentTemp = "";    // 摄氏度字符串
unsigned long lastWeatherUpdate = 0;
const unsigned long weatherUpdateInterval = 10 * 60 * 1000; // 10分钟

// 刷新进行中标志（在全刷期间屏蔽按键）
volatile bool refreshInProgress = false;

// 中文星期数组（放在时间正下方）
const char *weekDaysChinese[] = {"周日", "周一", "周二", "周三",
                                 "周四", "周五", "周六"};

// 日历页面相关变量
int selectedYear = 2025;
int selectedMonth = 9;
int selectedDay = 5;
enum CalendarCursor {
  CURSOR_YEAR = 0,
  CURSOR_MONTH,
  CURSOR_DAY,
  CURSOR_NAVIGATE
};
CalendarCursor currentCursor = CURSOR_NAVIGATE;

// 判断是否为闰年
bool isLeapYear(int year) {
  // ... existing code ...
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// 获取月份天数
int getDaysInMonth(int year, int month) {
  int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) {
    return 29;
  }
  return daysInMonth[month - 1];
}

// 获取某年某月1日是星期几（0=周日, 1=周一, ..., 6=周六）
int getFirstDayOfWeek(int year, int month) {
  // 使用Zeller公式计算
  if (month < 3) {
    month += 12;
    year--;
  }
  int k = year % 100;
  int j = year / 100;
  int q = 1; // 月份第一天
  int h = (q + (13 * (month + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;
  return (h + 5) % 7; // 转换为周日=0的格式
}

// 中文月份、天干地支
const char *lunarMonthNamesChinese[] = {"正月", "二月", "三月", "四月",
                                        "五月", "六月", "七月", "八月",
                                        "九月", "十月", "冬月", "腊月"};

static const char *stems[] = {"甲", "乙", "丙", "丁", "戊",
                              "己", "庚", "辛", "壬", "癸"};
static const char *branches[] = {"子", "丑", "寅", "卯", "辰", "巳",
                                 "午", "未", "申", "酉", "戌", "亥"};

// 将数字日转为中文表示（初一..三十）
String lunarDayToChinese(int d) {
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

// 使用移植后的 SolarToLunar 实现，支持 1900-2100（我们关心 1970-2050）
String getLunarDate(int y, int m, int d) {
  if (y < 1900 || y > 2100) {
    return String("农历") + String(y) + String("年") + String(m) +
           String("月") + String(d) + String("日");
  }
  Solar s;
  s.year = y;
  s.month = m;
  s.day = d;
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
    monthName =
        String((L.isLeap ? "闰" : "")) + String(lunarMonthNamesChinese[mi]);
  } else {
    monthName = String(L.month) + "月";
  }
  String dayName = lunarDayToChinese(L.day);
  return gzYear + String("年") + monthName + dayName;
}

// 防抖相关全局变量
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 100; // ms

// 获取一言
String getHitokoto() {
  HTTPClient http;
  http.begin("https://v1.hitokoto.cn/?encode=text&max_length=15"); // 返回纯文本

  int httpCode = http.GET();
  String hitokoto = "";

  if (httpCode == HTTP_CODE_OK) {
    hitokoto = http.getString();
    hitokoto.trim(); // 去除首尾空白字符
    Serial.println("获取一言成功: " + hitokoto);
  } else {
    Serial.println("获取一言失败，HTTP Code: " + String(httpCode));
    hitokoto = "QwQ"; // 默认文本
  }

  http.end();
  return hitokoto;
}

// 根据 IP 获取城市（使用 ip-api.com）
String getCityByIP() {
  HTTPClient http;
  http.begin("http://my.ip.cn/json/");
  int httpCode = http.GET();
  String city = "";
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    auto err = deserializeJson(doc, payload);
    if (!err) {
      // my.ip.cn 返回结构中城市位于 data.city
      if (doc.containsKey("data") && doc["data"].is<JsonObject>()) {
        JsonObject d = doc["data"];
        if (d.containsKey("city"))
          city = String((const char *)d["city"]);
      } else if (doc.containsKey("city")) {
        city = String((const char *)doc["city"]);
      }
    }
  }
  http.end();
  city.trim();
  return city;
}

// 使用 wttr.in 获取城市天气（JSON j1）
// 获取基于 IP 的经纬度（使用 ip-api.com）
bool getLocationByIP(double &outLat, double &outLon, String &outCityEn) {
  HTTPClient http;
  http.begin("http://ip-api.com/json");
  int httpCode = http.GET();
  bool ok = false;
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    auto err = deserializeJson(doc, payload);
    if (!err) {
      if (doc.containsKey("lat") && doc.containsKey("lon")) {
        outLat = doc["lat"].as<double>();
        outLon = doc["lon"].as<double>();
        if (doc.containsKey("city"))
          outCityEn = String((const char *)doc["city"]);
        ok = true;
      }
    }
  }
  http.end();
  return ok;
}

// 使用 Open-Meteo（基于经纬度，无需 API key）优先获取天气；失败则回退到 wttr.in
bool getWeatherForCity(const String &city, String &outWeather,
                       String &outTemp) {
  if (city.length() == 0)
    return false;

  // 先尝试通过 ip-api 获取经纬度
  double lat = 0.0, lon = 0.0;
  String cityEn = "";
  if (getLocationByIP(lat, lon, cityEn)) {
    char urlBuf[256];
    // 请求当前天气
    snprintf(urlBuf, sizeof(urlBuf),
             "https://api.open-meteo.com/v1/"
             "forecast?latitude=%.6f&longitude=%.6f&current_weather=true&"
             "timezone=Asia/Shanghai",
             lat, lon);
    String url = String(urlBuf);
    Serial.println("Open-Meteo URL: " + url);
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      DynamicJsonDocument doc(2048);
      auto err = deserializeJson(doc, payload);
      if (!err) {
        if (doc.containsKey("current_weather")) {
          JsonObject cw = doc["current_weather"];
          double temp = cw["temperature"] | 0.0;
          int wcode = cw["weathercode"] | 0;
          outTemp = String((int)round(temp));
          // 简单映射 weathercode 到中文描述
          String desc = "";
          if (wcode == 0)
            desc = "晴";
          else if (wcode == 1 || wcode == 2 || wcode == 3)
            desc = "多云";
          else if (wcode >= 45 && wcode <= 48)
            desc = "雾霾";
          else if ((wcode >= 51 && wcode <= 67) || (wcode >= 80 && wcode <= 82))
            desc = "下雨";
          else if (wcode >= 71 && wcode <= 77)
            desc = "下雪";
          else
            desc = "多变";
          outWeather = desc;
          http.end();
          return true;
        }
      }
    }
    http.end();
  } else {
    Serial.println("getLocationByIP failed, will try fallback API");
  }

  // 回退：使用 wttr.in（保持原有逻辑，但对城市名进行编码）
  // 对城市名进行 UTF-8 字节级 URL 编码，确保中文/特殊字符被正确转义
  auto urlEncode = [](const String &s) {
    String encoded = "";
    for (size_t i = 0; i < s.length(); i++) {
      uint8_t c = s[i];
      if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
          (c >= 'a' && c <= 'z') || c == '-' || c == '_' || c == '.' ||
          c == '~') {
        encoded += (char)c;
      } else {
        char buf[4];
        sprintf(buf, "%%%02X", c);
        encoded += String(buf);
      }
    }
    return encoded;
  };

  String encCity = urlEncode(city);
  String url = "http://wttr.in/" + encCity + "?format=j1";
  Serial.println("Fallback wttr.in URL: " + url);
  HTTPClient http2;
  http2.begin(url);
  int httpCode2 = http2.GET();
  bool ok2 = false;
  if (httpCode2 == HTTP_CODE_OK) {
    String payload = http2.getString();
    DynamicJsonDocument doc(6 * 1024);
    auto err = deserializeJson(doc, payload);
    if (!err) {
      if (doc.containsKey("current_condition")) {
        JsonArray cc = doc["current_condition"].as<JsonArray>();
        if (cc.size() > 0) {
          const char *t = cc[0]["temp_C"] | "";
          const char *desc = cc[0]["weatherDesc"][0]["value"] | "";
          outTemp = String(t);
          outWeather = String(desc);
          ok2 = true;
        }
      }
    }
  }
  http2.end();
  return ok2;
}

// 估算单字符宽度并根据可用宽度截断（中文按宽度12，ASCII按6），超出添加省略号
String fitToWidthSingleLine(const String &s, int maxWidth) {
  const int wChinese = 12;
  const int wAscii = 6;
  const String ell = "...";
  int used = 0;
  int i = 0;
  int len = s.length();
  int lastPos = 0;
  while (i < len) {
    uint8_t c = s[i];
    int charBytes = 1;
    int charWidth = wAscii;
    if ((c & 0x80) != 0) {
      if ((c & 0xE0) == 0xC0)
        charBytes = 2;
      else if ((c & 0xF0) == 0xE0)
        charBytes = 3;
      else if ((c & 0xF8) == 0xF0)
        charBytes = 4;
      charWidth = wChinese;
    }
    if (used + charWidth > maxWidth)
      break;
    used += charWidth;
    lastPos = i + charBytes;
    i += charBytes;
  }
  if (lastPos >= len)
    return s; // whole fits
  int ellWidth = 3 * wAscii;
  while (lastPos > 0 && used + ellWidth > maxWidth) {
    int j = lastPos - 1;
    while (j > 0 && (s[j] & 0xC0) == 0x80)
      j--;
    uint8_t cc = s[j];
    int cb = 1;
    int cw = wAscii;
    if ((cc & 0x80) != 0) {
      if ((cc & 0xE0) == 0xC0)
        cb = 2;
      else if ((cc & 0xF0) == 0xE0)
        cb = 3;
      else if ((cc & 0xF8) == 0xF0)
        cb = 4;
      cw = wChinese;
    }
    used -= cw;
    lastPos = j;
  }
  String out = s.substring(0, lastPos);
  out += ell;
  return out;
}

// 按钮状态枚举和读取函数（基于 BTN_ADC_PIN 的 ADC 值）
enum ButtonState { BTN_NONE = 0, BTN_RIGHT, BTN_LEFT, BTN_CENTER };

// forward-declare currentPage so displayTime() can check it
extern int currentPage;
// forward-declare page switching helper used by alarm handlers
void switchPageAndFullRefresh(int page);
// forward-declare placeholder partial renderer
void renderPlaceholderPartial(int page);

ButtonState readButtonState() {
  int v = analogRead(BTN_ADC_PIN);
  // 打印调试：可在需要时取消注释
  // Serial.printf("BTN ADC: %d\n", v);
  if (v > 4000)
    return BTN_NONE;
  if (v >= 3000 && v <= 3300)
    return BTN_RIGHT;
  if (v >= 2000 && v <= 2500)
    return BTN_LEFT;
  if (v < 100)
    return BTN_CENTER;
  return BTN_NONE;
}

void displayTime() {
  // 仅在主页（page 0）时允许 displayTime 真正更新屏幕，
  // 避免在切页期间或其他页被误调用导致覆盖当前页内容。
  if (currentPage != 0)
    return;
  timeClient.update();

  // 获取当前时间并格式化（只到分钟）
  int hours = timeClient.getHours();
  int minutes = timeClient.getMinutes();
  String currentTime = String(hours < 10 ? "0" : "") + String(hours) + ":" +
                       String(minutes < 10 ? "0" : "") + String(minutes);

  // 获取日期信息
  time_t rawtime = timeClient.getEpochTime();
  struct tm *timeinfo = localtime(&rawtime);
  int year = timeinfo->tm_year + 1900;
  int month = timeinfo->tm_mon + 1;
  int day = timeinfo->tm_mday;
  int weekday = timeinfo->tm_wday;

  String currentDate = String(year) + "-" + String(month < 10 ? "0" : "") +
                       String(month) + "-" + String(day < 10 ? "0" : "") +
                       String(day) + " " + String(weekDaysChinese[weekday]);

  // 检查是否需要更新一言
  if (millis() - lastHitokotoUpdate > hitokotoUpdateInterval ||
      currentHitokoto == "") {
    currentHitokoto = getHitokoto();
    lastHitokotoUpdate = millis();
  }

  // 规范化一言为单行（去掉换行）
  String oneLine = currentHitokoto;
  oneLine.replace('\n', ' ');
  oneLine.trim();

  // 如果时间和日期都没有变化，不需要更新（但如果一言发生变化且处于底部，也许需要重画；此处只在全刷周期触发）
  if (currentTime == lastDisplayedTime && currentDate == lastDisplayedDate) {
    return;
  }

  bool isFirstTime = (lastDisplayedTime == "");
  bool needFullRefresh =
      isFirstTime || ((millis() - lastFullRefresh) > fullRefreshInterval);

  if (needFullRefresh) {
    refreshInProgress = true;
    // 在全刷前检查并更新天气信息（按 IP 获取城市，然后获取该城市天气）
    if (millis() - lastWeatherUpdate > weatherUpdateInterval ||
        currentCity == "") {
      String city = getCityByIP();
      if (city.length() > 0)
        currentCity = city;
      String w, t;
      if (getWeatherForCity(currentCity, w, t)) {
        currentWeather = w;
        currentTemp = t;
        lastWeatherUpdate = millis();
        Serial.println("Weather updated: " + currentCity + " " + currentTemp +
                       "C " + currentWeather);
      } else {
        Serial.println("Weather fetch failed for city: " + currentCity);
      }
    }
    // 全刷新 - 显示时间、日期和底部一行一言（带分割线和省略号）
    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);

      // 使用绝对坐标：时间基线为屏幕垂直中心，日期在其下固定偏移
      u8g2Fonts.setFont(u8g2_font_logisoso32_tf);
      int timeX = 10;
      int timeBaseline = display.height() / 2; // 绝对垂直居中
      u8g2Fonts.setCursor(timeX, timeBaseline);
      u8g2Fonts.print(currentTime);

      // 日期：中等字号，放在时间正下方（固定偏移）
      u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
      int dateY = timeBaseline + 15;
      int dateX = 12;
      u8g2Fonts.setCursor(dateX, dateY);
      u8g2Fonts.print(currentDate);

      // 天气：城市 + 温度 + 描述，显示在日期下方
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
      // 在时间右侧空白区域显示右侧图片
      int iconX_full = 115; // 右边留少许间距
      int iconY_full = timeBaseline - (RIGHT_IMAGE_H / 2);
      if (iconY_full < 0)
        iconY_full = 0;
      display.drawBitmap(iconX_full, iconY_full, RightImage, RIGHT_IMAGE_W,
                         RIGHT_IMAGE_H, GxEPD_BLACK);

      // 底部：绘制一条分割线，上方保留一小块间隔，分割线下方显示单行一言
      int dividerY = display.height() - 18; // 更靠近底部，确保在屏幕最下方
      // 画分割线（使用细线）
      display.drawFastHLine(0, dividerY, display.width(), GxEPD_BLACK);

      // 一言：使用生成/小号字体，底部居中，仅一行，超出截断并加省略号
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
    Serial.println("全刷新 - 时间: " + currentTime + ", 日期: " + currentDate);
    refreshInProgress = false;
  } else {
    // 局部刷新 - 只更新时间区域（左上部分）
    // 局部刷新 - 只更新时间/日期/星期区域（垂直居中区域，排除底部保留区）
    // 局部刷新 - 使用绝对坐标，覆盖时间与日期区域
    int timeAreaX = 0;
    int timeAreaY = display.height() / 2 - 40;
    int timeAreaW = display.width();
    int timeAreaH = 96; // 包含时间与日期
    // 确保局部刷新区域包含右侧图片的垂直范围，防止图片被裁切
    int iconTopEstimate = display.height() / 2 - (RIGHT_IMAGE_H / 2);
    if (iconTopEstimate < timeAreaY) {
      timeAreaY = iconTopEstimate;
      if (timeAreaY < 0)
        timeAreaY = 0;
    }
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
      int timeBaseline = display.height() / 2;
      u8g2Fonts.setCursor(10, timeBaseline);
      u8g2Fonts.print(currentTime);
      u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
      int dateY = timeBaseline + 15;
      int dateWidth = u8g2Fonts.getUTF8Width(currentDate.c_str());
      int dateX = 12;
      u8g2Fonts.setCursor(dateX, dateY);
      u8g2Fonts.print(currentDate);
      // 局部刷新时也要显示天气（城市 + 温度 + 描述）
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
      // 在时间右侧空白区域显示右侧图片（局部刷新）
      int iconX_part = 115;
      int iconY_part = timeBaseline - (RIGHT_IMAGE_H / 2);
      if (iconY_part < 0)
        iconY_part = 0;
      display.drawBitmap(iconX_part, iconY_part, RightImage, RIGHT_IMAGE_W,
                         RIGHT_IMAGE_H, GxEPD_BLACK);
      int dividerY = display.height() - 18; // 更靠近底部，确保在屏幕最下方
      // 画分割线（使用细线）
      display.drawFastHLine(0, dividerY, display.width(), GxEPD_BLACK);
      //  一言：使用生成/小号字体，底部居中，仅一行，超出截断并加省略号
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
    Serial.println("局部刷新 - 时间: " + currentTime);
  }
}

// ---------- 页面翻页逻辑 ----------
int currentPage = 0; // 0..5
const int totalPages = 6;
unsigned long lastPageSwitch = 0;
int pageSwitchCount = 0;
const int partialBeforeFull = 5; // 局刷次数达到后执行一次全刷（改为每8次，减少残影）
unsigned long lastInteraction = 0;
const unsigned long inactivityTimeout = 30 * 1000; // 30秒无操作回主页
ButtonState lastButtonState = BTN_NONE;

// ----------------- Alarm 数据与 API -----------------
// 闹钟数量固定为5
struct Alarm {
  uint8_t hour;
  uint8_t minute;
  uint8_t weekdays; // bit0=Sun ... bit6=Sat
  uint8_t tone;     // 1..5
  bool enabled;
};

Alarm alarms[5];

// Preferences 存储键
#include <Preferences.h>
static const char *ALARM_NS = "alarms_cfg";

void loadAlarms() {
  Preferences p;
  if (!p.begin(ALARM_NS, true))
    return; // read-only begin
  size_t need = sizeof(alarms);
  if (p.isKey("data")) {
    // 读取原始二进制（如果存在）
    size_t sz = p.getBytesLength("data");
    if (sz == need) {
      p.getBytes("data", (void *)alarms, need);
    } else {
      // 版本不匹配或不存在，使用默认值
      for (int i = 0; i < 5; i++) {
        alarms[i].hour = 7;
        alarms[i].minute = 30;
        alarms[i].weekdays = 0; // 默认不重复
        alarms[i].tone = 1;
        alarms[i].enabled = false;
      }
    }
  } else {
    for (int i = 0; i < 5; i++) {
      alarms[i].hour = 7;
      alarms[i].minute = 30;
      alarms[i].weekdays = 0;
      alarms[i].tone = 1;
      alarms[i].enabled = false;
    }
  }
  // 修正 tone 的有效范围（历史数据可能有 0）
  for (int i = 0; i < 5; i++) {
    if (alarms[i].tone == 0 || alarms[i].tone > 5) alarms[i].tone = 1;
  }
  p.end();
}

void saveAlarms() {
  Preferences p;
  if (!p.begin(ALARM_NS, false))
    return; // write
  p.putBytes("data", (const void *)alarms, sizeof(alarms));
  p.end();
}

Alarm getAlarm(int idx) {
  if (idx < 0)
    idx = 0;
  if (idx > 4)
    idx = 4;
  return alarms[idx];
}
void setAlarm(int idx, const Alarm &a) {
  if (idx < 0 || idx > 4)
    return;
  alarms[idx] = a;
}
void toggleAlarmEnabled(int idx) {
  if (idx < 0 || idx > 4)
    return;
  alarms[idx].enabled = !alarms[idx].enabled;
}
void toggleAlarmWeekday(int idx, int wd) {
  if (idx < 0 || idx > 4)
    return;
  if (wd < 0 || wd > 6)
    return;
  alarms[idx].weekdays ^= (1 << wd);
}

// ------------- Alarm 页面渲染与输入状态 -------------
int alarmHighlightedRow = -1; // -1 表示不在五行区域
int alarmFieldCursor = -1;    // -1 表示不在行内编辑，字段定义见下
// 字段索引:
// 0=hour,1=minute,2=dayLabelStart(placeholder),3..9=weekday(日..六),10=tone,11=enabled
const int ALARM_FIELD_HOUR = 0;
const int ALARM_FIELD_MIN = 1;
const int ALARM_FIELD_WEEK_START =
    2; // weekday start index maps to weekday 0..6
const int ALARM_FIELD_TONE = 9;
const int ALARM_FIELD_ENABLED = 10;

// 辅助：绘制单行闹钟（rowIndex 0..4），x/y 为起点，fieldCursor 指示反色字段
void drawAlarmRow(int rowIndex, int x, int y, int rowW, int rowH,
                  int fieldCursorLocal, bool highlightRow) {
  Alarm a = alarms[rowIndex];
  // 基本列布局
  // hh mm 日 一 二 三 四 五 六 铃声 开/关
  int colX = x;
  u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
  // 时间
  String th = (a.hour < 10 ? "0" : "") + String(a.hour);
  String tm = (a.minute < 10 ? "0" : "") + String(a.minute);
  // draw background if highlighted row
  if (highlightRow) {
    // 修复：向上移动 3 像素，避免高亮矩形偏下
    display.fillRect(x, y - rowH - 1, rowW, rowH, GxEPD_BLACK);
    u8g2Fonts.setForegroundColor(GxEPD_WHITE);
  } else {
    display.fillRect(x, y - rowH + 2, rowW, rowH, GxEPD_WHITE);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  }
  // 将字段单独反色显示（白底黑字）
  auto drawField = [&](int fx, const String &txt, bool reverse) {
    int w = u8g2Fonts.getUTF8Width(txt.c_str());
    int tx = fx;
    int ty = y - 4;
    if (reverse) {
      // draw white rect and black text
      display.fillRect(tx - 2, ty - 12, w + 4, 16, GxEPD_WHITE);
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);
      u8g2Fonts.setCursor(tx, ty);
      u8g2Fonts.print(txt);
      // restore color
      u8g2Fonts.setForegroundColor(highlightRow ? GxEPD_WHITE : GxEPD_BLACK);
    } else {
      u8g2Fonts.setCursor(tx, ty);
      u8g2Fonts.print(txt);
    }
  };

  int fx = colX;
  // hour
  bool rev = (fieldCursorLocal == ALARM_FIELD_HOUR);
  drawField(fx, th, rev);
  fx += 20;
  // minute
  rev = (fieldCursorLocal == ALARM_FIELD_MIN);
  drawField(fx, tm, rev);
  fx += 28;
  // weekdays labels
  const char *wdnames[] = {"日", "一", "二", "三", "四", "五", "六"};
  for (int i = 0; i < 7; i++) {
    bool on = (a.weekdays & (1 << i));
    bool revwd = (fieldCursorLocal == (ALARM_FIELD_WEEK_START + i));
  String txt = String(wdnames[i]);
    // 如果该星期已被启用（on），用空心矩形框住（表示已选中该星期）
    if (on) {
      int w = u8g2Fonts.getUTF8Width(txt.c_str());
      int tx = fx;
      int ty = y - 4;
      int rectX = tx - 2;
      int rectY = ty - 12;
      int rectW = w + 4;
      int rectH = 16;
      // 线框颜色：在高亮行上用白色以便可见，否则用黑色
      int rectColor = highlightRow ? GxEPD_WHITE : GxEPD_BLACK;
      display.drawRect(rectX, rectY, rectW, rectH, rectColor);
      // 文本颜色：如果整行高亮则与线框同色，否则默认黑色
      u8g2Fonts.setForegroundColor(highlightRow ? GxEPD_WHITE : GxEPD_BLACK);
      u8g2Fonts.setCursor(tx, ty);
      u8g2Fonts.print(txt);
      // 恢复字体颜色为行高亮/非高亮默认
      u8g2Fonts.setForegroundColor(highlightRow ? GxEPD_WHITE : GxEPD_BLACK);
    } else {
      // 非启用状态，若该字段被选中则以反色显示，否则正常显示
      bool revwd = (fieldCursorLocal == (ALARM_FIELD_WEEK_START + i));
      drawField(fx, txt, revwd);
    }
    fx += 18;
  }
  // tone
  bool revTone = (fieldCursorLocal == ALARM_FIELD_TONE);
  drawField(fx, String(a.tone), revTone);
  fx += 24;
  // enabled
  bool revEn = (fieldCursorLocal == ALARM_FIELD_ENABLED);
  drawField(fx, String(a.enabled ? "开" : "关"), revEn);
}

// 渲染闹钟页面（支持 full / partial）
void renderAlarmPage(bool full = false) {
  int rowH = 15;
  int startY = 20;
  int rowW = display.width() - 20;

  if (full) {
    // 全屏全刷：阻塞按键并重绘所有元素（标题、底部导航与列表）
    refreshInProgress = true;
    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);

      // 五行列表
      for (int r = 0; r < 5; r++) {
        int y = startY + r * rowH + rowH;
        bool h = (alarmHighlightedRow == r);
        int fieldLocal = -1;
        if (h && alarmFieldCursor >= 0)
          fieldLocal = alarmFieldCursor;
        drawAlarmRow(r, 0, y, rowW, rowH, fieldLocal, h);
      }

      // 底部和标题
      int dividerY = display.height() - 18;
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
    refreshInProgress = false;
    return;
  }

  // 局部刷新：只更新闹钟列表区域，避免整屏全刷
  int rowsTotalH = rowH * 5 + 4;
  int px = 0;
  int py = startY - 4;
  int pw = display.width();
  int ph = rowsTotalH + 8;
  if (py < 0)
    py = 0;
  // 为避免分割线/底部提示符在局部刷新时丢失，略微扩展局部窗口以包含分割线并在局部刷新中重绘底部导航
  int dividerY = display.height() - 18;
  const int bottomPad = 20; // 包含分割线及其下方的提示区域
  if (py + ph < dividerY + bottomPad) {
    ph = (dividerY + bottomPad) - py;
  }
  if (py + ph > display.height())
    ph = display.height() - py;

  display.setPartialWindow(px, py, pw, ph);
  display.firstPage();
  do {
    // 仅在局部窗口内清空并绘制行
    display.fillScreen(GxEPD_WHITE);
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    for (int r = 0; r < 5; r++) {
      int y = startY + r * rowH + rowH;
      bool h = (alarmHighlightedRow == r);
      int fieldLocal = -1;
      if (h && alarmFieldCursor >= 0)
        fieldLocal = alarmFieldCursor;
      drawAlarmRow(r, 0, y, rowW, rowH, fieldLocal, h);
    }
    // 在局部刷新期间也显式重绘分割线和底部导航，避免因不同局刷/全刷波形导致可见性不稳定
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
}

// 按键处理：在闹钟页的左/右/中按钮行为
void handleAlarmRightButton() {
  if (alarmHighlightedRow < 0) {
    // 非行区，切页
    int next = (currentPage + 1) % totalPages;
    switchPageAndFullRefresh(next);
    return;
  }
  // 在行区：如果未进入字段编辑，右键进入字段编辑并选择第一个字段（hour）
  if (alarmFieldCursor < 0) {
    alarmFieldCursor = ALARM_FIELD_HOUR; // 进入字段编辑，从小时开始
    renderAlarmPage();
    lastInteraction = millis();
    return;
  }
  // 已进入字段编辑：右键在字段间向右移动，超出最后字段则退出编辑状态
  alarmFieldCursor++;
  if (alarmFieldCursor > ALARM_FIELD_ENABLED) {
    alarmFieldCursor = -1; // 退出字段编辑
  }
  renderAlarmPage();
  lastInteraction = millis();
}

void handleAlarmLeftButton() {
  if (alarmHighlightedRow < 0) {
    int prev = (currentPage - 1 + totalPages) % totalPages;
    switchPageAndFullRefresh(prev);
    return;
  }
  // 在行区：如果未进入字段编辑，左键进入字段编辑并选择最后一个字段（enabled）
  if (alarmFieldCursor < 0) {
    alarmFieldCursor = ALARM_FIELD_ENABLED; // 进入字段编辑，从末尾字段开始
    renderAlarmPage();
    lastInteraction = millis();
    return;
  }
  // 已进入字段编辑：左键在字段间向左移动，越界则退出编辑状态
  alarmFieldCursor--;
  if (alarmFieldCursor < ALARM_FIELD_HOUR) {
    alarmFieldCursor = -1; // 退出字段编辑
  }
  renderAlarmPage();
  lastInteraction = millis();
}

void handleAlarmCenterButton() {
  // 中键切换选中/不选中状态；当处于字段编辑模式时，中键用于修改当前字段
  if (alarmHighlightedRow < 0) {
    // 未选中 -> 选中第一行
    int oldRow = alarmHighlightedRow;
    alarmHighlightedRow = 0;
    alarmFieldCursor = -1;
    if (oldRow != alarmHighlightedRow && alarmHighlightedRow >= 0) {
      pageSwitchCount++; // 行切换计入局刷次数
    }
    renderAlarmPage();
    lastInteraction = millis();
    return;
  }

  // 已选中但未进入字段编辑：中键切换到下一行，超过最后一行则变为未选中
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
    // 仅当变为有效行索引时计入局刷
    if (oldRow != alarmHighlightedRow && alarmHighlightedRow >= 0) {
      pageSwitchCount++;
    }
    renderAlarmPage();
    lastInteraction = millis();
    return;
  }

  // 在字段编辑模式：中键修改当前字段（保持原有行为）
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
    // numeric tone cycle 1..5
    a.tone = (a.tone % 5) + 1;
  } else if (alarmFieldCursor == ALARM_FIELD_ENABLED) {
    a.enabled = !a.enabled;
  }
  setAlarm(alarmHighlightedRow, a);
  saveAlarms();
  renderAlarmPage();
  lastInteraction = millis();
}

// 局部渲染：时间区域（复制 displayTime() 中的局部刷新绘制，但不做时间变更判断）
void renderTimePartial() {
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

  // 一言（可能是旧的）
  String oneLine = currentHitokoto;
  oneLine.replace('\n', ' ');
  oneLine.trim();

  // 计算局部窗口（我们之前已经扩展到包含右侧图片）
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
    int dateX = 10;
    u8g2Fonts.setCursor(dateX, dateY);
    u8g2Fonts.print(currentDate);
    // 天气行
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
    // 右侧图片
    int iconX_part = 115;
    int iconY_part = timeBaseline - (RIGHT_IMAGE_H / 2);
    if (iconY_part < 0)
      iconY_part = 0;
    display.drawBitmap(iconX_part, iconY_part, RightImage, RIGHT_IMAGE_W,
                       RIGHT_IMAGE_H, GxEPD_BLACK);
    // 底部一言（不覆盖在此局部窗口外部）
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

  lastDisplayedTime = currentTime;
  lastDisplayedDate = currentDate;
}

// 局部渲染：占位页（仅显示页码）

// 渲染日历页面；full=true 时使用全屏全刷，false 时使用部分窗口渲染
void renderCalendarPage(bool full = false) {
  if (full) {
    refreshInProgress = true;
    display.setFullWindow();
  } else {
    display.setPartialWindow(0, 0, display.width(), display.height());
  }
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // 左侧信息区域（收紧垂直间距）
    int leftX = 5;
    int startY = 35; // 顶部更靠近

    // 显示年份（中等字体，使用 ASCII/unifont）
    u8g2Fonts.setFont(u8g2_font_unifont_t_latin);
    int yearY = startY;
    if (currentCursor == CURSOR_YEAR) {
      // 高亮年份 - 先画黑底，然后以实底白字绘制，避免透明模式失效导致方块
      int yearW = u8g2Fonts.getUTF8Width((String(selectedYear) + "年").c_str());
      display.fillRect(leftX - 2, yearY - 13, yearW + 4, 16, GxEPD_BLACK);
      // 暂存并设置颜色/模式
      u8g2Fonts.setForegroundColor(GxEPD_WHITE);
      u8g2Fonts.setBackgroundColor(GxEPD_BLACK);
      u8g2Fonts.setFontMode(1); // 实心背景
    }
    u8g2Fonts.setCursor(leftX, yearY);
    u8g2Fonts.print(String(selectedYear) + "年");
    // 恢复默认绘制颜色和模式
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
    u8g2Fonts.setFontMode(1);

    // 显示月份（中等字体）
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

    // 显示日期（中等字体）
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

    // 显示农历（小字体）
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    int lunarY = dayY + 20;
    String lunarStr = getLunarDate(selectedYear, selectedMonth, selectedDay);
    u8g2Fonts.setCursor(leftX, lunarY);
    u8g2Fonts.print(lunarStr);

    // 右侧日历区域
    int calendarX = 110;
    int calendarY = 15;
    int cellW = 14;
    int cellH = 12;

    // 显示年月标题
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    String titleStr =
        String(selectedYear) + "年 " + String(selectedMonth) + "月";
    u8g2Fonts.setCursor(calendarX, calendarY);
    u8g2Fonts.print(titleStr);

    // 显示星期标题
    const char *weekHeaders[] = {"日", "一", "二", "三", "四", "五", "六"};
    int headerY = calendarY + 18;
    for (int i = 0; i < 7; i++) {
      u8g2Fonts.setCursor(calendarX + i * cellW, headerY);
      u8g2Fonts.print(weekHeaders[i]);
    }

    // 绘制日历网格
    int gridStartY = headerY + 5;
    int daysInMonth = getDaysInMonth(selectedYear, selectedMonth);
    int firstDay = getFirstDayOfWeek(selectedYear, selectedMonth);

    for (int week = 0; week < 6; week++) {
      for (int day = 0; day < 7; day++) {
        int dayNum = week * 7 + day - firstDay + 1;
        if (dayNum > 0 && dayNum <= daysInMonth) {
          int cellX = calendarX + day * cellW;
          int cellY = gridStartY + week * cellH;

          // 如果是选中的日期，高亮显示（先画黑底，再以透明模式绘制白字）
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

    // 底部分割线和时间
    int dividerY = display.height() - 18;
    display.drawFastHLine(0, dividerY, display.width(), GxEPD_BLACK);

    // 显示当前实际时间
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

    int toleftX = 5;
    int toleftY = dividerY + 15;
    String toleftStr = "< 主页";
    u8g2Fonts.setCursor(toleftX, toleftY);
    u8g2Fonts.print(toleftStr);

    int torightX = 172;
    int torightY = dividerY + 15;
    String torightStr = "闹钟 >";
    u8g2Fonts.setCursor(torightX, torightY);
    u8g2Fonts.print(torightStr);

  } while (display.nextPage());
  if (full)
    refreshInProgress = false;
}
// 延迟全刷控制：先局刷，0.5s 内无继续切页再全刷
unsigned long lastPageSwitchMs = 0;
int pendingFullRefreshPage = -1; // -1 表示无待全刷
const unsigned long deferredFullDelay = 500; // ms

// Helper: 切换到某页，立即局刷；0.5s 内若无再次切页，则执行一次全刷
void switchPageAndFullRefresh(int page) {
  currentPage = page;
  lastInteraction = millis();

  // 进入闹钟页时默认不选中任何行
  if (currentPage == 2) {
    alarmHighlightedRow = -1;
    alarmFieldCursor = -1;
  }

  // 先做一次局部渲染（快速反馈）
  if (currentPage == 0) {
    renderTimePartial();
  } else if (currentPage == 1) {
    renderCalendarPage(false);
  } else if (currentPage == 2) {
    renderAlarmPage(false);
  } else {
    renderPlaceholderPartial(currentPage);
  }

  // 标记延迟全刷
  pendingFullRefreshPage = currentPage;
  lastPageSwitchMs = millis();
}
void renderPlaceholderPartial(int page) {
  if (page == 1) {
    renderCalendarPage();
    return;
  }

  // 其他页面显示页码
  int px = 0, py = 0, pw = display.width(), ph = display.height();
  display.setPartialWindow(px, py, pw, ph);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8g2Fonts.setFont(u8g2_font_logisoso32_tf);
    // 在屏幕中间显示页码
    int cx = display.width() / 2;
    int cy = display.height() / 2;
    String s = String("Page ") + String(page);
    int w = u8g2Fonts.getUTF8Width(s.c_str());
    u8g2Fonts.setCursor(cx - w / 2, cy);
    u8g2Fonts.print(s);
  } while (display.nextPage());
}

// 日历页面的按钮处理函数
void handleCalendarRightButton() {
  if (currentCursor == CURSOR_NAVIGATE) {
    // 导航模式：切换到下一页并全刷
    int next = (currentPage + 1) % totalPages;
    switchPageAndFullRefresh(next);
  } else if (currentCursor == CURSOR_YEAR) {
    // 年份增加
    selectedYear++;
    if (selectedYear > 2030)
      selectedYear = 2030; // 限制上限
    // 检查日期有效性
    int maxDay = getDaysInMonth(selectedYear, selectedMonth);
    if (selectedDay > maxDay)
      selectedDay = maxDay;
    renderCalendarPage();
  } else if (currentCursor == CURSOR_MONTH) {
    // 月份增加
    selectedMonth++;
    if (selectedMonth > 12) {
      selectedMonth = 1;
      selectedYear++;
      if (selectedYear > 2030) {
        selectedYear = 2030;
        selectedMonth = 12;
      }
    }
    // 检查日期有效性
    int maxDay = getDaysInMonth(selectedYear, selectedMonth);
    if (selectedDay > maxDay)
      selectedDay = maxDay;
    renderCalendarPage();
  } else if (currentCursor == CURSOR_DAY) {
    // 日期增加
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
    renderCalendarPage();
  }
}

void handleCalendarLeftButton() {
  if (currentCursor == CURSOR_NAVIGATE) {
    // 导航模式：切换到上一页并全刷
    int prev = (currentPage - 1 + totalPages) % totalPages;
    switchPageAndFullRefresh(prev);
  } else if (currentCursor == CURSOR_YEAR) {
    // 年份减少
    selectedYear--;
    if (selectedYear < 2020)
      selectedYear = 2020; // 限制下限
    // 检查日期有效性
    int maxDay = getDaysInMonth(selectedYear, selectedMonth);
    if (selectedDay > maxDay)
      selectedDay = maxDay;
    renderCalendarPage();
  } else if (currentCursor == CURSOR_MONTH) {
    // 月份减少
    selectedMonth--;
    if (selectedMonth < 1) {
      selectedMonth = 12;
      selectedYear--;
      if (selectedYear < 2020) {
        selectedYear = 2020;
        selectedMonth = 1;
      }
    }
    // 检查日期有效性
    int maxDay = getDaysInMonth(selectedYear, selectedMonth);
    if (selectedDay > maxDay)
      selectedDay = maxDay;
    renderCalendarPage();
  } else if (currentCursor == CURSOR_DAY) {
    // 日期减少
    selectedDay--;
    if (selectedDay < 1) {
      selectedMonth--;
      if (selectedMonth < 1) {
        selectedMonth = 12;
        selectedYear--;
        if (selectedYear < 2020) {
          selectedYear = 2020;
          selectedMonth = 1;
          selectedDay = 1;
        } else {
          selectedDay = getDaysInMonth(selectedYear, selectedMonth);
        }
      } else {
        selectedDay = getDaysInMonth(selectedYear, selectedMonth);
      }
    }
    renderCalendarPage();
  }
}

void handleCalendarCenterButton() {
  // 切换光标状态
  currentCursor = (CalendarCursor)((currentCursor + 1) % 4);
  renderCalendarPage();
}

void setup() {
  Serial.begin(9600);
  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, EPD_CS_PIN);
  display.init();
  display.setRotation(1);
  u8g2Fonts.begin(display);
  u8g2Fonts.setFontMode(1);
  u8g2Fonts.setFontDirection(0);
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
  u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);

  display.clearScreen();

  WiFiManager wm;

  // Customizing the portal
  wm.setAPCallback([](WiFiManager *myWiFiManager) {
    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      u8g2Fonts.setCursor(0, 15);
      u8g2Fonts.println("WiFi配网模式");
      u8g2Fonts.print("请连接AP: ");
      u8g2Fonts.println(myWiFiManager->getConfigPortalSSID());
      u8g2Fonts.println("IP: 192.168.4.1");
    } while (display.nextPage());
  });

  // Set a timeout for connecting, otherwise it will block forever
  wm.setConnectTimeout(60);

  if (!wm.autoConnect("AriaMPCC-ESP")) {
    Serial.println("Failed to connect and hit timeout");
    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      u8g2Fonts.setCursor(0, 15);
      u8g2Fonts.println("配网失败,请重启.");
    } while (display.nextPage());
    // ESP.restart();
    // Or go into deep sleep
    return;
  }

  Serial.println("Connected to WiFi");
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8g2Fonts.setCursor(0, 15);
    u8g2Fonts.println("WiFi已连接!");
    u8g2Fonts.print("IP: ");
    u8g2Fonts.println(WiFi.localIP());
    u8g2Fonts.println("正在同步时间...");
  } while (display.nextPage());

  // 初始化NTP客户端
  timeClient.begin();
  timeClient.update();
  Serial.println("NTP时间同步完成");

  // 初始化全刷新时间戳
  lastFullRefresh = millis();

  // 获取第一条一言
  currentHitokoto = getHitokoto();
  lastHitokotoUpdate = millis();

  // 初始化日历页面的选中日期为当前日期
  time_t rawtime = timeClient.getEpochTime();
  struct tm *timeinfo = localtime(&rawtime);
  selectedYear = timeinfo->tm_year + 1900;
  selectedMonth = timeinfo->tm_mon + 1;
  selectedDay = timeinfo->tm_mday;

  // 加载闹钟配置
  loadAlarms();

  // 首次启动时立即渲染主页（触发天气获取等）
  displayTime();
  lastInteraction = millis();
  lastButtonState = readButtonState();

  delay(2000);

  Serial.println("Initializing SD card...");
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("initialization failed!");
    return;
  }
  Serial.println("initialization done.");
  audio.setPinout(I2S_BCLK_PIN, I2S_WS_PIN, I2S_DOUT_PIN);
  audio.setI2SCommFMT_LSB(true);
  audio.setVolume(255);
}

void loop() {
  unsigned long now = millis();

  // 读取按钮状态（拨杆）
  ButtonState bs = readButtonState();
  // 如果当前处于全刷过程中，忽略按键输入
  if (refreshInProgress) {
    // 更新 lastButtonState 以避免边沿触发后续逻辑
    lastButtonState = bs;
    vTaskDelay(50);
    return; // 跳过本次 loop 的剩余处理
  }
  if (bs != lastButtonState && bs != BTN_NONE) {
    Serial.println("Button state changed to: " + String(bs));
  }
  if (bs != lastButtonState) {
    // 按键边沿变化 - 先做防抖（忽略短时间内的抖动）
    if (now - lastButtonPress < debounceDelay) {
      // 忽略此次变化
      lastButtonState = bs;
    } else {
      // 记录此次为有效按键时间
      lastButtonPress = now;
      // 状态变化视为一次交互（消抖、只在确认为方向/按下时触发）
      if (bs == BTN_RIGHT) {
        if (currentPage == 1) {
          // 日历页面的右按钮处理
          handleCalendarRightButton();
        } else if (currentPage == 2) {
          handleAlarmRightButton();
        } else {
          int next = (currentPage + 1) % totalPages;
          switchPageAndFullRefresh(next);
        }
      } else if (bs == BTN_LEFT) {
        if (currentPage == 1) {
          // 日历页面的左按钮处理
          handleCalendarLeftButton();
        } else if (currentPage == 2) {
          handleAlarmLeftButton();
        } else {
          int prev = (currentPage - 1 + totalPages) % totalPages;
          switchPageAndFullRefresh(prev);
        }
      } else if (bs == BTN_CENTER) {
        Serial.println("BTN_CENTER pressed, currentPage=" +
                       String(currentPage));
        if (currentPage == 1) {
          // 日历页面的中按钮处理
          handleCalendarCenterButton();
        } else if (currentPage == 2) {
          handleAlarmCenterButton();
        } else if (currentPage != 0) {
          Serial.println("Not on homepage, going to page 0");
          switchPageAndFullRefresh(0);
        } else {
          Serial.println("On homepage, starting manual refresh");
          // 在主页：局部显示获取信息中...（屏幕上方中央）
          {
            const String msg = "刷新信息中...";
            u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
            int textW = u8g2Fonts.getUTF8Width(msg.c_str());
            // 水平内边距与垂直高度
            const int padX = 8;
            const int padY = 6;
            int pw = textW + padX * 2;
            int ph = 12 + padY * 2; // 字高约16，外加垂直内边距
            if (pw > display.width())
              pw = display.width();
            if (ph > display.height())
              ph = display.height();
            int px = (display.width() - pw) / 2;
            int py = 30; // 靠近顶部，保留少量顶部空白

            // 使用紧凑的部分窗口以保证绘制对齐
            display.setPartialWindow(px, py, pw, ph);
            display.firstPage();
            do {
              // 先用白底清空部分区域，再画黑边框增加可见性
              display.fillRect(px, py, pw, ph, GxEPD_WHITE);
              display.drawRect(px, py, pw, ph, GxEPD_BLACK);
              // 文字居中绘制
              int tx = px + (pw - textW) / 2;
              int ty = py + padY + 12; // 基线位置，12 为字体基线偏移经验值
              u8g2Fonts.setCursor(tx, ty);
              u8g2Fonts.print(msg);
            } while (display.nextPage());
          }

          // 执行网络获取（同步）
          Serial.println("Fetching hitokoto...");
          currentHitokoto = getHitokoto();
          lastHitokotoUpdate = millis();
          Serial.println("Fetching city and weather...");
          String city = getCityByIP();
          if (city.length() > 0)
            currentCity = city;
          String w, t;
          if (getWeatherForCity(currentCity, w, t)) {
            currentWeather = w;
            currentTemp = t;
            lastWeatherUpdate = millis();
            Serial.println("Weather updated by manual fetch: " + currentCity +
                           " " + currentTemp + "C " + currentWeather);
          } else {
            Serial.println("Manual weather fetch failed for city: " +
                           currentCity);
          }

          // 完成后执行一次全刷来更新屏幕
          // 强制 displayTime() 进行刷新（避免因时间/日期未变化而提前返回）
          Serial.println("Forcing full refresh...");
          lastDisplayedTime = "";
          lastFullRefresh = 0;
          displayTime();
          lastInteraction = now;
          pendingFullRefreshPage = -1; // 避免紧随的延迟全刷
        }
      }
      lastButtonState = bs;
    }
  }

  // 如果超过无操作超时，返回主页
  if (millis() - lastInteraction > inactivityTimeout && currentPage != 0) {
    // 超时返回主页并做一次全刷
    switchPageAndFullRefresh(0);
  }

  // 局刷计数达到阈值，触发一次全刷
  if (pageSwitchCount >= partialBeforeFull) {
    if (currentPage == 0) {
      lastFullRefresh = 0; // 触发 displayTime 的全刷分支
      displayTime();
    } else if (currentPage == 1) {
      // 如果在日历页，进行日历的全刷渲染
      renderCalendarPage(true);
    } else if (currentPage == 2) {
      // 如果在闹钟页，进行闹钟页的全刷渲染
      renderAlarmPage(true);
    } else {
      display.setFullWindow();
      display.firstPage();
      do {
        display.fillScreen(GxEPD_WHITE);
        u8g2Fonts.setFont(u8g2_font_logisoso32_tf);
        String s = String("Page ") + String(currentPage);
        int w = u8g2Fonts.getUTF8Width(s.c_str());
        int cx = display.width() / 2;
        int cy = display.height() / 2;
        u8g2Fonts.setCursor(cx - w / 2, cy);
        u8g2Fonts.print(s);
      } while (display.nextPage());
    }
    pageSwitchCount = 0;
    lastFullRefresh = now;
  // 本次已经全刷，避免紧接着的延迟全刷再次触发
  pendingFullRefreshPage = -1;
  }

  // 延迟全刷：若存在待全刷页面且超过延迟且页面未变化，则执行一次全刷
  if (pendingFullRefreshPage >= 0 && (millis() - lastPageSwitchMs) >= deferredFullDelay) {
    // 仅当当前页仍是待全刷页时执行
    int pageToFull = pendingFullRefreshPage;
    pendingFullRefreshPage = -1; // 清除挂起标记，避免重复全刷
    // 强制一次全刷（走各页面的全刷渲染）
    lastDisplayedTime = "";
    lastFullRefresh = 0;
    if (currentPage == 0) {
      displayTime();
    } else if (currentPage == 1) {
      renderCalendarPage(true);
    } else if (currentPage == 2) {
      renderAlarmPage(true);
    } else {
      // 其他页面：全屏显示页码
      refreshInProgress = true;
      display.setFullWindow();
      display.firstPage();
      do {
        display.fillScreen(GxEPD_WHITE);
        u8g2Fonts.setFont(u8g2_font_logisoso32_tf);
        String s = String("Page ") + String(currentPage);
        int w = u8g2Fonts.getUTF8Width(s.c_str());
        int cx = display.width() / 2;
        int cy = display.height() / 2;
        u8g2Fonts.setCursor(cx - w / 2, cy);
        u8g2Fonts.print(s);
      } while (display.nextPage());
      refreshInProgress = false;
    }
  }

  // 定期检查时间变化（每秒检查一次，当分钟变化时更新显示）
  static unsigned long lastTimeCheck = 0;
  if (now - lastTimeCheck > 1000) { // 每秒检查一次
    lastTimeCheck = now;
    if (currentPage == 0) { // 只在主页时检查时间更新
      displayTime();        // 这个函数内部会判断是否需要更新
    }
  }

  audio.loop();
  vTaskDelay(80);
}

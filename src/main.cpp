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
#include "app_context.h"
#include "alarms.h"
#include "calendar.h"
#include "utils.h"

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

// main 不再保存日历实现/状态，移至 calendar.cpp

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

// main 不再实现闹钟页面，移至 alarms.cpp

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

// ----------------- Alarm 页面实现已迁移到 alarms.cpp -----------------

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
  int dateX = 12; // 对齐 main_old.cpp 的日期 X 坐标
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
  int availWidth = display.width() - 60; // 局刷时可用宽度与 main_old.cpp 保持一致
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
// 延迟全刷控制：先局刷，1s 内无继续切页再全刷
unsigned long lastPageSwitchMs = 0;
int pendingFullRefreshPage = -1; // -1 表示无待全刷
const unsigned long deferredFullDelay = 1000; // ms

// Helper: 切换到某页，立即局刷；1s 内若无再次切页，则执行一次全刷
void switchPageAndFullRefresh(int page) {
  currentPage = page;
  lastInteraction = millis();

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

// 日历页面的按钮处理函数已在 calendar.cpp 中实现

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
  calendarSetSelectedDate(timeinfo->tm_year + 1900, timeinfo->tm_mon + 1,
                          timeinfo->tm_mday);

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

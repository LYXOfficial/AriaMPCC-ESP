#include "Audio.h"
#include "defines/RightImage.h"
#include "defines/pinconf.h"
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
#include "utils/lunar.h"
#include "app_context.h"
#include "utils/utils.h"
#include "pages/page_manager.h"
#include "pages/hometime.h"
#include "pages/calendar_page.h"
#include "pages/alarms_page.h"

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
extern const unsigned long fullRefreshInterval = 8 * 60 * 1000;    // 8分钟全刷一次
extern const unsigned long hitokotoUpdateInterval = 5 * 60 * 1000; // 5分钟更新一次一言
// 天气相关
String currentCity = "";
String currentWeather = ""; // 描述
String currentTemp = "";    // 摄氏度字符串
unsigned long lastWeatherUpdate = 0;
extern const unsigned long weatherUpdateInterval = 10 * 60 * 1000; // 10分钟

// 刷新进行中标志（在全刷期间屏蔽按键）
volatile bool refreshInProgress = false;

// 中文星期数组（放在时间正下方）
const char *weekDaysChinese[] = {"周日", "周一", "周二", "周三",
                                 "周四", "周五", "周六"};

// main 不再保存日历实现/状态，移至 calendar.cpp

// 防抖相关全局变量



// 根据 IP 获取城市：使用 utils/getCityByIP()

// main 不再实现闹钟页面，移至 alarms.cpp

// 按钮状态读取（类型由 PageManager 定义）

extern int currentPage;
void switchPageAndFullRefresh(int page);
void renderPlaceholderPartial(int page);

// button reading and debounce helpers are implemented in utils.cpp

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
    // 在全刷前检查并更新天气信息：优先 Open-Meteo（按经纬度），失败再回退 wttr.in（按城市）
    if (millis() - lastWeatherUpdate > weatherUpdateInterval || currentCity == "") {
      double lat=0, lon=0; String cityEn;
      bool locOk = getLocationByIP(lat, lon, cityEn);
      String w, t;
      bool ok = false;
      if (locOk) {
        ok = getWeatherByCoordsOpenMeteo(lat, lon, w, t);
        // 同步城市名称用于 UI 展示（优先中文 my.ip.cn，其次 ip-api 返回的英文名）
        String cityCN = getCityByIP();
        if (cityCN.length() > 0) currentCity = cityCN; else if (cityEn.length() > 0) currentCity = cityEn;
      }
      if (!ok) {
        String city = getCityByIP();
        if (city.length() > 0) currentCity = city;
        ok = getWeatherForCity(currentCity, w, t);
      }
      if (ok) {
        currentWeather = w; currentTemp = t; lastWeatherUpdate = millis();
        Serial.println("Weather updated: " + currentCity + " " + currentTemp + "C " + currentWeather);
      } else {
        Serial.println("Weather fetch failed (both Open-Meteo and wttr.in)");
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

// ---------- 页面翻页逻辑（由 PageManager 接管） ----------
int currentPage = 0;         // 暂时保留供模块访问
const int totalPages = 6;    // 页面总数
unsigned long lastInteraction = 0; // 供少量模块使用
int pageSwitchCount = 0;     // 保留计数逻辑
const int partialBeforeFull = 5;
PageManager gPageMgr;
static Page *gPages[6] = {nullptr};
static PageButton lastButtonState = BTN_NONE;
// single global definition for lastPageSwitchMs
unsigned long lastPageSwitchMs = 0;

// ----------------- Alarm 页面实现已迁移到 alarms.cpp -----------------

// renderTimePartial, switchPageAndFullRefresh and renderPlaceholderPartial
// have been moved to page_manager.cpp to reduce main.cpp size.

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

  // 日历页初始化由 CalendarPage 自行完成

  // 闹钟配置由 AlarmsPage 管理

  // Page system setup
  gPages[0] = new HomeTimePage([&](int p) { gPageMgr.switchPage(p); currentPage = gPageMgr.currentIndex(); });
  // Use dedicated Page wrappers
  gPages[1] = new CalendarPage();
  gPages[2] = new AlarmsPage();
  // Placeholder pages 3..5
  class Placeholder : public Page {
   public:
    int index;
    explicit Placeholder(int idx): index(idx) {}
    void render(bool full) override { renderPlaceholderPartial(index); }
    void onLeft() override { switchPageAndFullRefresh(index - 1); }
    void onRight() override { switchPageAndFullRefresh(index + 1); }
    void onCenter() override {}
    const char *name() const override { return "placeholder"; }
  };
  gPages[3] = new Placeholder(3);
  gPages[4] = new Placeholder(4);
  gPages[5] = new Placeholder(5);
  gPageMgr.setPages(gPages, 6);
  gPageMgr.begin();
  lastInteraction = millis();
  lastButtonState = (PageButton)readButtonStateRaw();

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
  PageButton bs = (PageButton)readButtonStateRaw();
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
      if (bs == BTN_RIGHT || bs == BTN_LEFT || bs == BTN_CENTER) {
        int oldPage = gPageMgr.currentIndex();
        gPageMgr.handleButtonEdge(bs);
        currentPage = gPageMgr.currentIndex();
        if (currentPage != oldPage) {
          pageSwitchCount++; // only count when actual page switch occurred
        }
      }
      if (bs == BTN_CENTER) {
        Serial.println("BTN_CENTER pressed, currentPage=" +
                       String(currentPage));
        if (currentPage == 0) {
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
          {
            double lat=0, lon=0; String cityEn; String w, t; bool ok=false;
            bool locOk = getLocationByIP(lat, lon, cityEn);
            if (locOk) {
              ok = getWeatherByCoordsOpenMeteo(lat, lon, w, t);
              String cityCN = getCityByIP();
              if (cityCN.length() > 0) currentCity = cityCN; else if (cityEn.length() > 0) currentCity = cityEn;
            }
            if (!ok) {
              String city = getCityByIP(); if (city.length() > 0) currentCity = city;
              ok = getWeatherForCity(currentCity, w, t);
            }
            if (ok) {
              currentWeather = w; currentTemp = t; lastWeatherUpdate = millis();
              Serial.println("Weather updated by manual fetch: " + currentCity + " " + currentTemp + "C " + currentWeather);
            } else {
              Serial.println("Manual weather fetch failed (both Open-Meteo and wttr.in)");
            }
          }

          // 完成后执行一次全刷来更新屏幕
          // 强制 HomeTimePage 进行刷新（避免因时间/日期未变化而提前返回）
          Serial.println("Forcing full refresh...");
          lastDisplayedTime = "";
          lastFullRefresh = 0;
          gPageMgr.requestRender(true);
          lastInteraction = now;
          gPageMgr.cancelPendingFull(); // 避免紧随的延迟全刷
        }
      }
      lastButtonState = bs;
    }
  }

  // 交由 PageManager 处理无操作超时与延迟全刷
  gPageMgr.loop();

  // Safety: 防止刷新标志被卡住导致无法切回主页
  static unsigned long refreshStuckSince = 0;
  if (refreshInProgress) {
    if (refreshStuckSince == 0) refreshStuckSince = now;
    else if (now - refreshStuckSince > 10000) { // 10s 超时，强制清除
      Serial.println("Warning: refreshInProgress stuck >10s, clearing flag");
      refreshInProgress = false;
      refreshStuckSince = 0;
    }
  } else {
    refreshStuckSince = 0;
  }

  // 局刷计数达到阈值，触发一次全刷
  if (pageSwitchCount >= partialBeforeFull) {
    // 统一交给 PageManager 执行当前页全刷
    Serial.println("pageSwitchCount threshold reached, requesting full render for current page=" + String(gPageMgr.currentIndex()));
    gPageMgr.requestRender(true);
    pageSwitchCount = 0;
    lastFullRefresh = now;
  // 本次已经全刷，避免紧接着的延迟全刷再次触发
  gPageMgr.cancelPendingFull();
  }

  // 定期检查时间变化（每秒检查一次，当分钟变化时更新显示）
  static unsigned long lastTimeCheck = 0;
  if (now - lastTimeCheck > 1000) { // 每秒检查一次
    lastTimeCheck = now;
    if (currentPage == 0) { // 只在主页时检查时间更新
  // 触发主页的局部渲染；HomeTimePage 内部会自行判断是否需要全刷/局刷
  Serial.println("Periodic time check -> request partial render for page=" + String(gPageMgr.currentIndex()));
  gPageMgr.requestRender(false);
    }
  }

  audio.loop();
  vTaskDelay(80);
}

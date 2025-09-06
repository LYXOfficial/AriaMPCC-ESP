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
#include "app_context.h"
#include "pages/alarms_page.h"
#include "pages/calendar_page.h"
#include "pages/page_manager.h"
#include "pages/time_page.h"
#include "utils/lunar.h"
#include "utils/utils.h"

// NTP 相关
WiFiUDP ntpUDP;
NTPClient
    timeClient(ntpUDP, "ntp.aliyun.com", 8 * 3600,
               60000); // 使用阿里云NTP服务器，中国时区(+8)，每分钟更新一次

// home/time related globals moved to pages/time_page.cpp

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

// ---------- 页面翻页逻辑（由 PageManager 接管） ----------
int currentPage = 0;               // 暂时保留供模块访问
const int totalPages = 6;          // 页面总数
unsigned long lastInteraction = 0; // 供少量模块使用
int pageSwitchCount = 0;           // 保留计数逻辑
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
  gPages[0] = new HomeTimePage([&](int p) {
    gPageMgr.switchPage(p);
    currentPage = gPageMgr.currentIndex();
  });
  // Use dedicated Page wrappers
  gPages[1] = new CalendarPage();
  gPages[2] = new AlarmsPage();
  // Placeholder pages 3..5
  class Placeholder : public Page {
  public:
    int index;
    explicit Placeholder(int idx) : index(idx) {}
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

  // keep per-alarm last triggered minute to avoid retriggering within same minute
  static int lastAlarmTriggerMinute[5] = {-1, -1, -1, -1, -1};

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
              Serial.println("Weather updated by manual fetch: " + currentCity +
                             " " + currentTemp + "C " + currentWeather);
            } else {
              Serial.println(
                  "Manual weather fetch failed (both Open-Meteo and wttr.in)");
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
    if (refreshStuckSince == 0)
      refreshStuckSince = now;
    else if (now - refreshStuckSince > 10000) { // 10s 超时，强制清除
      Serial.println("Warning: refreshInProgress stuck >10s, clearing flag");
      refreshInProgress = false;
      refreshStuckSince = 0;
    }
  } else {
    refreshStuckSince = 0;
  }

  // 定期检查时间变化（每秒检查一次，当分钟变化时更新显示）
  static unsigned long lastTimeCheck = 0;
  if (now - lastTimeCheck > 1000) { // 每秒检查一次
    lastTimeCheck = now;
    if (currentPage == 0) { // 只在主页时检查时间更新
      // 触发主页的局部渲染；HomeTimePage 内部会自行判断是否需要全刷/局刷
      Serial.println("Periodic time check -> request partial render for page=" +
                     String(gPageMgr.currentIndex()));
      gPageMgr.requestRender(false);
    }
    // Alarm check: run every second but only trigger once per minute per alarm
    // Use NTP time for correctness
    time_t raw = timeClient.getEpochTime();
    struct tm *tm = localtime(&raw);
    int curHour = tm->tm_hour;
    int curMin = tm->tm_min;
    int curWday = tm->tm_wday; // 0 = Sunday .. 6 = Saturday
    for (int i = 0; i < 5; i++) {
      Alarm a = getAlarmCfg(i);
      if (!a.enabled) continue;
      // check weekday mask: if weekdays == 0 treat as everyday
      bool weekdayMatch = (a.weekdays == 0) || ((a.weekdays & (1 << curWday)) != 0);
      if (!weekdayMatch) continue;
      if (a.hour == curHour && a.minute == curMin) {
        if (lastAlarmTriggerMinute[i] != curMin) {
          lastAlarmTriggerMinute[i] = curMin;
          Serial.println("Triggering alarm " + String(i) + " at " + String(curHour) + ":" + String(curMin));
          startAlarmNow(i);
        }
      } else {
        // reset per-minute lock when time moved away
        if (lastAlarmTriggerMinute[i] == curMin)
          lastAlarmTriggerMinute[i] = -1;
      }
    }
  }

  audio.loop();
  vTaskDelay(10);
}

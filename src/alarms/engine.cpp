#include "../app_context.h"
#include "../defines/pinconf.h"
#include "../pages/alarms_page.h"
#include "../pages/page_manager.h"
#include "../utils/utils.h"
#include <Arduino.h>
#include <Wire.h>

volatile bool alarmRinging = false;
static volatile int activeAlarmIdx = -1;

// helper to manage buzzer without using Arduino tone API
static const int BUZZER_LEDC_CHANNEL = 0;
static bool buzzerLEDCInited = false;

static void initBuzzerLEDC(int freq) {
#ifdef BUZZER_PIN
  // configure LEDC channel for buzzer
  ledcSetup(BUZZER_LEDC_CHANNEL, freq, 8); // 8-bit resolution
  ledcAttachPin(BUZZER_PIN, BUZZER_LEDC_CHANNEL);
  buzzerLEDCInited = true;
#endif
}

static void startBuzzerFreq(int freq) {
#ifdef BUZZER_PIN
  if (!buzzerLEDCInited)
    initBuzzerLEDC(freq);
  else
    ledcSetup(BUZZER_LEDC_CHANNEL, freq, 8);
  // 50% duty
  ledcWrite(BUZZER_LEDC_CHANNEL, 128);
#endif
}

// stop buzzer and drive pin low to avoid floating/noise
static inline void stopBuzzer() {
#ifdef BUZZER_PIN
  if (buzzerLEDCInited) {
    ledcWrite(BUZZER_LEDC_CHANNEL, 0);
    // detach to leave pin controllable
    ledcDetachPin(BUZZER_PIN);
    buzzerLEDCInited = false;
  }
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
#endif
}

// Simple melody representation: array of {freq, ms}
struct Note {
  int freq;
  int dur;
};

// Five simple melodies (short excerpts) using frequency in Hz.
// Melody 1: "Haruhikage" from MyGO!!!!! (first 8 bars)
// 可以和我组一辈子乐队吗 为什么要演奏春日影！
// Converted from Jianpu score, 1=C, 6/8, dotted quarter = 97bpm
// Eighth note duration is approx. 206ms
static const Note melody1[] = {
    // Bar 1: 3' 2' 1' 2' (quarter, eighth, quarter, eighth)
    {622, 412},
    {554, 206},
    {466, 412},
    {554, 206},
    // Bar 2: 3'. 4'3' 2' (dotted quarter, two eighths, quarter) -> Corrected to
    // fit 6/8
    {622, 309},
    {698, 103},
    {622, 206},
    {554, 618}};
static const int melody1_len = sizeof(melody1) / sizeof(melody1[0]);
// Melody 2: 你从丹东来 \😭/
// 1=E, 4/4, approx 120bpm (quarter note = 500ms)
static const Note melody2[] = {
    // 你从丹东来
    {554, 125},
    {659, 125},
    {740, 125},
    {831, 125},
    {740, 250},
    // 换我一城雪白
    {659, 125},
    {622, 125},
    {554, 125},
    {622, 250},
    {494, 125},
    {554, 375},
    // 想吃广东菜
    {554, 125},
    {659, 125},
    {740, 125},
    {831, 125},
    {740, 125},
    {659, 125},
    {740, 125},
    {831, 125},
    {831, 750}};
static const int melody2_len = sizeof(melody2) / sizeof(melody2[0]);
// Melody 3: dididididi
static const Note melody3[] = {{600, 125}, {0, 125}};
static const int melody3_len = sizeof(melody3) / sizeof(melody3[0]);
// Melody 4: Canon in D (fragment)
static const Note melody4[] = {{523, 300}, {494, 300}, {440, 300}, {392, 300},
                               {440, 300}, {494, 300}, {523, 600}};
static const int melody4_len = sizeof(melody4) / sizeof(melody4[0]);
// Melody 5: "Kong Xin Ren Zha Ji" (excerpt)
// 1=C, 4/4, 145bpm (quarter note approx 414ms)
static const Note melody5[] = {
    // Bar 1-2
    {220, 414},
    {262, 414},
    {349, 207},
    {330, 207},
    {294, 207},
    {262, 207},
    // Bar 3-4
    {220, 207},
    {262, 414},
    {196, 414},
    {220, 414},
    {196, 414},
    {220, 414},
    {262, 207},
    {220, 207},
    {262, 414}};
static const int melody5_len = sizeof(melody5) / sizeof(melody5[0]);

static const Note *melodies[] = {melody1, melody2, melody3, melody4, melody5};
static const int melody_lens[] = {melody1_len, melody2_len, melody3_len,
                                  melody4_len, melody5_len};

// play melody using LEDC PWM (no Arduino tone API)
static void playMelody(int toneIdx) {
  if (toneIdx < 1 || toneIdx > 5)
    toneIdx = 1;
  const Note *m = melodies[toneIdx - 1];
  int len = melody_lens[toneIdx - 1];
  for (int r = 0; r < 3 && alarmRinging;
       r++) { // repeat 3 times or until stopped
    for (int i = 0; i < len && alarmRinging; i++) {
      int f = m[i].freq;
      int d = m[i].dur;
#ifdef BUZZER_PIN
      // use LEDC PWM to generate tone without Arduino tone API
      startBuzzerFreq(f);
      // poll button state in small slices while letting hardware play the
      // note uninterrupted
      const unsigned long slice = 20; // ms, shorter for better button response
      unsigned long waited = 0;
      while (waited < (unsigned long)d && alarmRinging) {
        delay(slice);
        waited += slice;
        int bs = readButtonStateRaw();
        if (bs != BTN_NONE) {
          alarmRinging = false;
          break;
        }
      }
      // stop the note cleanly
      stopBuzzer();
      // short gap between notes, still checking buttons
      if (alarmRinging) {
        unsigned long gapWait = 0;
        while (gapWait < 100 && alarmRinging) {
          delay(20);
          gapWait += 20;
          int bs = readButtonStateRaw();
          if (bs != BTN_NONE) {
            alarmRinging = false;
            break;
          }
        }
      }
#endif
    }
    // brief pause between melody repeats; allow button checking
    unsigned long pauseMs = 100;
    unsigned long pWait = 0;
    while (pWait < pauseMs && alarmRinging) {
      delay(20);
      pWait += 20;
      int bs = readButtonStateRaw();
      if (bs != BTN_NONE) {
        alarmRinging = false;
        break;
      }
    }
  }
  // ensure buzzer is stopped when leaving melody playback loop
  stopBuzzer();
}

void startAlarmNow(int idx) {
  if (idx < 0 || idx > 4)
    return;
  // ensure buzzer not left on
  stopBuzzer();
  Alarm a = getAlarmCfg(idx);
  if (!a.enabled) {
    stopBuzzer();
    return;
  }
  // set ringing state
  activeAlarmIdx = idx;
  alarmRinging = true;
  // Spawn simple blocking ring routine on same thread: show UI and play
  // Note: This blocks until user presses a key; acceptable for simple device.
  // Save current page to restore later
  int savedPage = currentPage;
  // full screen show
  refreshInProgress = true;
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    String hh = (a.hour < 10 ? "0" : "") + String(a.hour);
    String mm = (a.minute < 10 ? "0" : "") + String(a.minute);
    String title = "闹钟 " + hh + ":" + mm;
    int tw = u8g2Fonts.getUTF8Width(title.c_str());
    u8g2Fonts.setCursor((display.width() - tw) / 2 - 20,
                        display.height() / 2 - 6);
    u8g2Fonts.print(title);
    String tip = "按任意键关闭";
    int tw2 = u8g2Fonts.getUTF8Width(tip.c_str());
    u8g2Fonts.setCursor((display.width() - tw2) / 2 - 20,
                        display.height() / 2 + 12);
    u8g2Fonts.print(tip);
  } while (display.nextPage());
  refreshInProgress = false;
  // Play melody in background loop until key pressed
  // We'll poll buttons here
  while (alarmRinging) {
    // play one melody iteration non-blocking-ish
    Alarm cur = getAlarmCfg(activeAlarmIdx);
    int toneId = cur.tone;
    // play melody (this uses blocking tone+delay but checks alarmRinging
    // between notes)
    playMelody(toneId);
    // check buttons
    PageButton bs = (PageButton)readButtonStateRaw();
    if (bs != BTN_NONE) {
      // consume press and stop
      alarmRinging = false;
      break;
    }
    delay(50);
  }
  // ensure buzzer stopped
  stopBuzzer();
  // restore previous page via full refresh
  gPageMgr.switchPage(savedPage);
  gPageMgr.requestRender(true);
}

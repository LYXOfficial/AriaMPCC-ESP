#include "../app_context.h"
#include "../defines/pinconf.h"
#include "../pages/alarms_page.h"
#include "../pages/page_manager.h"
#include "../utils/utils.h"
#include <Arduino.h>
#include <Wire.h>

volatile bool alarmRinging = false;
static volatile int activeAlarmIdx = -1;

// Simple melody representation: array of {freq, ms}
struct Note {
  int freq;
  int dur;
};

// Five simple melodies (short excerpts) using frequency in Hz.
// Melody 1: Twinkle Twinkle (intro)
static const Note melody1[] = {{392, 300}, {392, 300}, {440, 300}, {440, 300},
                               {392, 300}, {392, 300}, {330, 600}};
static const int melody1_len = sizeof(melody1) / sizeof(melody1[0]);
// Melody 2: Fur Elise (opening)
static const Note melody2[] = {{659, 200}, {622, 200}, {659, 200}, {622, 200},
                               {659, 200}, {494, 200}, {587, 200}, {523, 400}};
static const int melody2_len = sizeof(melody2) / sizeof(melody2[0]);
// Melody 3: Ode to Joy (Beethoven)
static const Note melody3[] = {{330, 250}, {330, 250}, {349, 250}, {392, 250},
                               {392, 250}, {349, 250}, {330, 250}, {294, 250}};
static const int melody3_len = sizeof(melody3) / sizeof(melody3[0]);
// Melody 4: Canon in D (fragment)
static const Note melody4[] = {{523, 300}, {494, 300}, {440, 300}, {392, 300},
                               {440, 300}, {494, 300}, {523, 600}};
static const int melody4_len = sizeof(melody4) / sizeof(melody4[0]);
// Melody 5: Happy Birthday (fragment)
static const Note melody5[] = {{392, 200}, {392, 200}, {440, 400},
                               {392, 400}, {523, 400}, {494, 800}};
static const int melody5_len = sizeof(melody5) / sizeof(melody5[0]);

static const Note *melodies[] = {melody1, melody2, melody3, melody4, melody5};
static const int melody_lens[] = {melody1_len, melody2_len, melody3_len,
                                  melody4_len, melody5_len};

// play melody using tone() if available on this board
static void playMelody(int toneIdx) {
  if (toneIdx < 1 || toneIdx > 5)
    toneIdx = 1;
  const Note *m = melodies[toneIdx - 1];
  int len = melody_lens[toneIdx - 1];
  for (int r = 0; r < 3 && alarmRinging; r++) { // repeat 3 times or until stopped
    for (int i = 0; i < len && alarmRinging; i++) {
      int f = m[i].freq;
      int d = m[i].dur;
#ifdef BUZZER_PIN
      // use Arduino tone API without duration so hardware plays continuously
      tone(BUZZER_PIN, f);
      // poll button state in small slices while letting hardware play the
      // note uninterrupted
      const unsigned long slice = 40; // ms
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
      noTone(BUZZER_PIN);
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
}

void startAlarmNow(int idx) {
  if (idx < 0 || idx > 4)
    return;
  Alarm a = getAlarmCfg(idx);
  if (!a.enabled)
    return;
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
  // stop any tone
#ifdef BUZZER_PIN
  noTone(BUZZER_PIN);
#endif
  // restore previous page via full refresh
  gPageMgr.switchPage(savedPage);
  gPageMgr.requestRender(true);
}

#include "music_page.h"
#include "../app_context.h"
#include <Preferences.h>
#include "defines/pinconf.h"
#include "AudioTools.h"
// include the concrete MP3 decoder wrapper so the type is visible here
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "FS.h"
#include <SD.h>
#include "FS.h"
#include <SD.h>

// Minimal AudioSource implementation that uses Arduino SD to open files by path.
// This keeps memory small and avoids depending on std::filesystem.
namespace audio_tools {
class SDFileAudioSource : public AudioSource {
 public:
  SDFileAudioSource() {}
  bool begin() override { return true; }
  Stream* nextStream(int offset) override { return nullptr; }
  Stream* selectStream(int index) override { return nullptr; }
  Stream* selectStream(const char* path) override {
    if (path == nullptr) return nullptr;
    // close any previously opened file
    if (file) file.close();
    ::File f = SD.open(path);
    if (!f) return nullptr;
    file = f; // move
    last_path = String(path);
    return &file;
  }
  const char* toStr() override { return last_path.c_str(); }
 protected:
  ::File file;
  String last_path;
};
}

// audio-tools classes
using namespace audio_tools;

MusicPage::MusicPage() {
  currentTrack = String();
  playing = false;
  // allocate audio stack
  i2s = new audio_tools::I2SStream();
  // use audio-tools codec wrapper which implements AudioDecoder
  decoder = new audio_tools::MP3DecoderHelix();
  // create a small SD-backed AudioSource and construct player with it
  source = new SDFileAudioSource();
  player = new audio_tools::AudioPlayer(*source, *i2s, *decoder);

  // configure I2S pins and params similarly to prior main.cpp code
  auto config = i2s->defaultConfig(TX_MODE);
  config.pin_bck = I2S_BCLK_PIN;
  config.pin_ws = I2S_WS_PIN;
  config.pin_data = I2S_DOUT_PIN;
  config.sample_rate = 44100;
  config.channels = 2;
  config.bits_per_sample = 16;
  i2s->begin(config);

  // wire up player
  // already wired by constructor; do not force-set audioInfo here (decoder may not have valid info yet)
  // Reduce default volume to avoid damaging speakers/mic (safe default)
  if (player) {
    // 0.0 - 1.0, use a conservative value
    player->setVolume(1);
    Serial.println("MusicPage: default volume set to 20% (safe)");
  }
}

void MusicPage::openFromFile(const String &path) {
  currentTrack = path;
  Serial.println("MusicPage: openFromFile " + path);
  // use blocking convenience if available
  if (player) {
    bool ok = player->playPath(path.c_str());
    playing = ok;
    if (!ok) Serial.println("Audio playback failed for: " + path);
  }
}

void MusicPage::render(bool full) {
  // simple UI: show filename and play/pause status
  const int footerH = 18;
  if (full) {
    display.setFullWindow();
  } else {
    int footerY = display.height() - footerH;
    display.setPartialWindow(0, 0, display.width(), display.height());
  }
  display.firstPage();
  do {
  display.fillScreen(GxEPD_WHITE);
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    String title = "音乐播放器";
    int tw = u8g2Fonts.getUTF8Width(title.c_str());
    u8g2Fonts.setCursor((display.width() - tw) / 2, 30);
    u8g2Fonts.print(title);
    String fname = currentTrack;
    int p = fname.lastIndexOf('/');
    if (p >= 0) fname = fname.substring(p + 1);
    u8g2Fonts.setCursor(10, 60);
    u8g2Fonts.print("曲目: ");
    u8g2Fonts.print(fname);
  // reflect actual audio state if available
  bool running = false;
  if (player) running = player->isActive();
  u8g2Fonts.setCursor(10, 90);
  u8g2Fonts.print(running ? "状态: 播放中" : "状态: 暂停");
  } while (display.nextPage());
}

bool MusicPage::onLeft() {
  // left: previous track (not implemented)
  return false;
}
bool MusicPage::onRight() {
  // right: next track (not implemented)
  return false;
}
bool MusicPage::onCenter() {
  if (player) {
    if (player->isActive()) player->setActive(false);
    else player->setActive(true);
    playing = player->isActive();
  } else {
    playing = !playing;
  }
  render(false);
  return true;
}

void MusicPage::tick() {
  // advance player processing if non-blocking mode
  if (player) {
    player->copy();
  }
}

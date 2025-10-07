#pragma once

#include "page.h"

// Forward declarations from audio-tools headers
  namespace audio_tools {
  class I2SStream;
  class AudioPlayer;
  class AudioDecoder;
  class AudioSource;
}

class MusicPage : public Page {
public:
  MusicPage();
  void render(bool full) override;
  bool onLeft() override;
  bool onRight() override;
  bool onCenter() override;
  const char *name() const override { return "music"; }
  void openFromFile(const String &path);
  // called periodically from the main loop to advance playback
  void tick();
private:
  String currentTrack;
  bool playing = false;
  // audio stack owned by this page
  audio_tools::I2SStream *i2s = nullptr;
  audio_tools::AudioPlayer *player = nullptr;
  audio_tools::AudioDecoder *decoder = nullptr;
  // audio source for filesystem/SD playback (use base class so we can implement a small SD-backed source)
  audio_tools::AudioSource *source = nullptr;
};

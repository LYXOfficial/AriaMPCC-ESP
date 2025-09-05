#pragma once
#include "page.h"
#include <Preferences.h>

struct Alarm { uint8_t hour; uint8_t minute; uint8_t weekdays; uint8_t tone; bool enabled; };

class AlarmsPage : public Page {
public:
  AlarmsPage();
  void render(bool full) override;
  void onLeft() override;
  void onRight() override;
  void onCenter() override;
  const char *name() const override { return "alarms"; }

private:
  int highlightedRow = -1;
  int fieldCursor = -1;

  void drawRow(int rowIndex, int x, int y, int rowW, int rowH, int fieldCursorLocal, bool highlightRow);
  void load();
  void save();
};

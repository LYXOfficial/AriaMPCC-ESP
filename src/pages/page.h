// Page interface and manager contracts
#pragma once

#include <Arduino.h>

// Abstract Page interface
class Page {
public:
  virtual ~Page() {}
  virtual void render(bool full) = 0; // full: full refresh or partial
  // Event handlers should return true if the page handled the event and no
  // global page switching should occur. Default implementations return false
  // (not handled) to preserve existing behavior.
  virtual bool onLeft() { return false; }
  virtual bool onRight() { return false; }
  virtual bool onCenter() { return false; }
  virtual const char *name() const { return ""; }
};

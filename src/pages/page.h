// Page interface and manager contracts
#pragma once

#include <Arduino.h>

// Abstract Page interface
class Page {
public:
  virtual ~Page() {}
  virtual void render(bool full) = 0; // full: full refresh or partial
  virtual void onLeft() {}
  virtual void onRight() {}
  virtual void onCenter() {}
  virtual const char *name() const { return ""; }
};

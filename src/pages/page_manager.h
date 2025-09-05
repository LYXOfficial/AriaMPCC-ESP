#pragma once

#include <Arduino.h>
#include "page.h"

enum PageButton { BTN_NONE = 0, BTN_RIGHT, BTN_LEFT, BTN_CENTER };

class PageManager {
public:
  PageManager();
  void setPages(Page **pages, int count);
  void begin();

  void handleButtonEdge(PageButton bs);
  void loop();

  int currentIndex() const { return currentPage; }
  int pageCount() const { return totalPages; }

  void requestRender(bool full);

  // Paging helper exposed for modules
  void switchPage(int page);

  // Configs
  void setInactivityTimeout(unsigned long ms) { inactivityTimeout = ms; }
  void setDeferredFullDelay(unsigned long ms) { deferredFullDelay = ms; }

private:
  Page **pages = nullptr;
  int totalPages = 0;
  int currentPage = 0;
  unsigned long lastInteraction = 0;

  // deferred full refresh
  int pendingFullRefreshPage = -1;
  unsigned long lastPageSwitchMs = 0;
  unsigned long deferredFullDelay = 500;
  unsigned long inactivityTimeout = 30 * 1000;
};

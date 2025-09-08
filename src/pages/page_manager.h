#pragma once

#include "page.h"
#include <Arduino.h>

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
  unsigned long getDeferredFullDelay() const { return deferredFullDelay; }
  void cancelPendingFull() { pendingFullRefreshPage = -1; }
  int pendingPage() const { return pendingFullRefreshPage; }

private:
  Page **pages = nullptr;
  int totalPages = 0;
  int currentPage = 0;
  unsigned long lastInteraction = 0;

  // deferred full refresh
  int pendingFullRefreshPage = -1;
  // unsigned long lastPageSwitchMs; // Keep as a private member only, remove
  // initializer
  // Time (ms) to wait after a quick partial page switch before performing a
  // deferred full refresh. Reduced from 1000 to 500 to shorten perceived delay.
  unsigned long deferredFullDelay = 500;
  unsigned long inactivityTimeout = 30 * 1000;
};

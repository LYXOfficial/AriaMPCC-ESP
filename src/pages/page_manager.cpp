#include "page_manager.h"
#include "../app_context.h"
#include <algorithm>

// Helper: placeholder renderer moved here so main can be smaller
void renderPlaceholderPartial(int page) {
  int px = 0, py = 0, pw = ::display.width(), ph = ::display.height();
  ::display.setPartialWindow(px, py, pw, ph);
  ::display.firstPage();
  do {
    ::display.fillScreen(GxEPD_WHITE);
    ::u8g2Fonts.setFont(u8g2_font_logisoso32_tf);
    int cx = ::display.width() / 2;
    int cy = ::display.height() / 2;
    String s = String("Page ") + String(page);
    int w = ::u8g2Fonts.getUTF8Width(s.c_str());
    ::u8g2Fonts.setCursor(cx - w / 2, cy);
    ::u8g2Fonts.print(s);
  } while (::display.nextPage());
}

void switchPageAndFullRefresh(int page) {
  gPageMgr.switchPage(page);
  currentPage = gPageMgr.currentIndex();
}

PageManager::PageManager() {}

void PageManager::setPages(Page **pagesIn, int count) {
  pages = pagesIn;
  totalPages = count;
}

void PageManager::begin() {
  lastInteraction = millis();
  // also keep global lastInteraction in sync so other modules and main see recent activity
  ::lastInteraction = lastInteraction;
  if (pages && totalPages > 0) {
    // initial render full
  Serial.println("PageManager begin: initial page=" + String(currentPage));
  pages[currentPage]->render(true);
  }
}

void PageManager::switchPage(int page) {
  if (!pages || totalPages == 0)
    return;
  if (page < 0)
    page = (page % totalPages + totalPages) % totalPages;
  if (page >= totalPages)
    page = page % totalPages;

  currentPage = page;
  lastInteraction = millis();
  ::lastInteraction = lastInteraction;
  // quick partial render for feedback
  Serial.println("PageManager.switchPage -> partial render page=" + String(currentPage));
  pages[currentPage]->render(false);
  // mark deferred full and record switch time (global)
  pendingFullRefreshPage = currentPage;
  ::lastPageSwitchMs = millis();
}

void PageManager::requestRender(bool full) {
  if (!pages || totalPages == 0)
    return;
  pages[currentPage]->render(full);
}

void PageManager::handleButtonEdge(PageButton bs) {
  if (!pages || totalPages == 0)
    return;
  lastInteraction = millis();
  // Always dispatch the button event to the current page. Pages decide whether to
  // handle it locally or perform page switching (they can call switchPage...).
  if (bs == BTN_LEFT) {
    pages[currentPage]->onLeft();
  } else if (bs == BTN_RIGHT) {
    pages[currentPage]->onRight();
  } else if (bs == BTN_CENTER) {
    pages[currentPage]->onCenter();
  }
}

void PageManager::loop() {
  unsigned long now = millis();
  // deferred full
  if (pendingFullRefreshPage >= 0 && (now - lastPageSwitchMs) >= deferredFullDelay) {
    int pageToFull = pendingFullRefreshPage;
    pendingFullRefreshPage = -1;
    Serial.println("PageManager.loop: performing deferred full refresh for page=" + String(pageToFull));
    if (pages && totalPages > 0 && pageToFull >=0 && pageToFull < totalPages) {
      pages[pageToFull]->render(true);
    }
  }
  // inactivity auto-home: respect global lastInteraction updated by pages
  if ((now - ::lastInteraction) > inactivityTimeout && currentPage != 0) {
    switchPage(0);
  }
}

#include "page_manager.h"
#include "../app_context.h"
#include <algorithm>

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
  // mark deferred full
  pendingFullRefreshPage = currentPage;
  lastPageSwitchMs = millis();
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
  if (bs == BTN_LEFT) {
    if (currentPage == 0) {
      pages[currentPage]->onLeft();
    } else {
      switchPage(currentPage - 1);
    }
  } else if (bs == BTN_RIGHT) {
    if (currentPage == 0) {
      pages[currentPage]->onRight();
    } else {
      switchPage(currentPage + 1);
    }
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

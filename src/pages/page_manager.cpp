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
 
// initialize default directSwitchAllowed in a separate init step to avoid
// depending on totalPages at construction time
// (we assume pages are set immediately after construction via setPages)


void PageManager::setPages(Page **pagesIn, int count) {
  pages = pagesIn;
  totalPages = count;
  // initialize directSwitchAllowed defaults: allow all pages except 5 and 6
  for (int i = 0; i < MAX_PAGES; ++i) directSwitchAllowed[i] = true;
  // if configured pages fewer than indexes 5/6, ignore
  if (totalPages > 5) directSwitchAllowed[5] = false; // music page
  if (totalPages > 6) directSwitchAllowed[6] = false; // ebook page
}

void PageManager::begin() {
  lastInteraction = millis();
  // also keep global lastInteraction in sync so other modules and main see
  // recent activity
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

  // if the target page is marked as not directly reachable, ignore the
  // request. This prevents normal left/right swipes from landing on pages
  // intended to be entered only via explicit actions (e.g., files open).
  if (page >= 0 && page < MAX_PAGES && !directSwitchAllowed[page]) {
    Serial.println("PageManager.switchPage: direct switch to page " + String(page) + " blocked");
    return;
  }

  currentPage = page;
  lastInteraction = millis();
  ::lastInteraction = lastInteraction;
  // quick partial render for feedback
  Serial.println("PageManager.switchPage -> partial render page=" +
                 String(currentPage));
  // If switching to the home page, clear the cached lastDisplayedTime so
  // HomeTimePage::renderPartial() will not early-return and will update the
  // displayed time immediately.
  if (currentPage == 0) {
    ::lastDisplayedTime = "";
  }
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

void PageManager::setDirectSwitchAllowed(int page, bool allowed) {
  if (page < 0 || page >= MAX_PAGES) return;
  directSwitchAllowed[page] = allowed;
}

bool PageManager::isDirectSwitchAllowed(int page) const {
  if (page < 0 || page >= MAX_PAGES) return false;
  return directSwitchAllowed[page];
}

void PageManager::handleButtonEdge(PageButton bs) {
  if (!pages || totalPages == 0)
    return;
  lastInteraction = millis();
  // Always dispatch the button event to the current page. Pages decide whether
  // to handle it locally or perform page switching (they can call
  // switchPage...).
  int prevPage = currentPage;
  if (bs == BTN_LEFT) {
    bool handled = pages[currentPage]->onLeft();
    // If page didn't handle switching, perform default circular left move
    if (!handled && currentPage == prevPage) {
      // find previous allowed page (skip pages with directSwitchAllowed == false)
      if (totalPages > 1) {
        int found = -1;
        for (int i = 1; i < totalPages; ++i) {
          int cand = (prevPage - i) % totalPages;
          if (cand < 0) cand += totalPages;
          if (cand >= 0 && cand < totalPages && directSwitchAllowed[cand]) {
            found = cand;
            break;
          }
        }
        if (found >= 0) switchPage(found);
      }
    }
  } else if (bs == BTN_RIGHT) {
    bool handled = pages[currentPage]->onRight();
    // If page didn't handle switching, perform default circular right move
    if (!handled && currentPage == prevPage) {
      // find next allowed page (skip pages with directSwitchAllowed == false)
      if (totalPages > 1) {
        int found = -1;
        for (int i = 1; i < totalPages; ++i) {
          int cand = (prevPage + i) % totalPages;
          if (cand >= 0 && cand < totalPages && directSwitchAllowed[cand]) {
            found = cand;
            break;
          }
        }
        if (found >= 0) switchPage(found);
      }
    }
  } else if (bs == BTN_CENTER) {
    pages[currentPage]->onCenter();
  }
}

void PageManager::loop() {
  unsigned long now = millis();
  // deferred full
  if (pendingFullRefreshPage >= 0 && (now - lastPageSwitchMs) >= deferredFullDelay) {
    // If a full refresh is currently in progress elsewhere, postpone the
    // deferred full refresh to avoid overlapping or immediate successive
    // full refreshes. Keep the pendingFullRefreshPage so it will be
    // retried on the next loop.
    if (::refreshInProgress) {
      Serial.println("PageManager.loop: deferred full postponed because refreshInProgress");
      return;
    }
    int pageToFull = pendingFullRefreshPage;
    // Only perform the deferred full if the page to full is still the current
    // page. This avoids performing a full refresh for a stale page when the
    // user has navigated elsewhere quickly.
    if (pageToFull != currentPage) {
      Serial.println("PageManager.loop: deferred full skipped because currentPage=" + String(currentPage) + " != pending=" + String(pageToFull));
      // clear pending to avoid repeated checks
      pendingFullRefreshPage = -1;
    } else {
      pendingFullRefreshPage = -1;
      Serial.println("PageManager.loop: performing deferred full refresh for page=" + String(pageToFull));
      if (pages && totalPages > 0 && pageToFull >= 0 && pageToFull < totalPages) {
        pages[pageToFull]->render(true);
      }
    }
  }
  // inactivity auto-home: respect global lastInteraction updated by pages
  if ((now - ::lastInteraction) > inactivityTimeout && currentPage != 0) {
  switchPage(0);
  // ensure the global currentPage (used by main and other modules) is
  // synchronized when auto-navigating home
  ::currentPage = currentPage;
  }

  // periodic footer update for ebook page: update right-bottom time every minute
  static unsigned long lastEbookFooterMs = 0;
  if (!::refreshInProgress && pages && currentPage >= 0 && currentPage < totalPages) {
    const char *pname = pages[currentPage]->name();
    if (pname && strcmp(pname, "ebook") == 0) {
      if (now - lastEbookFooterMs >= 60UL * 1000UL) {
        // refresh NTP and request a light partial render for footer
        ::timeClient.update();
        pages[currentPage]->render(false);
        lastEbookFooterMs = now;
      }
    } else {
      // reset timer when leaving ebook page
      lastEbookFooterMs = now;
    }
  }
}

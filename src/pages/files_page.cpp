#include "files_page.h"
#include "../app_context.h"
#include "../utils/utils.h"
#include "Audio.h"
#include "defines/pinconf.h"
#include "ebook_page.h"
#include "page_manager.h"
#include <FS.h>
#include <SD.h>

// detect SD insertion/removal and update internal state. Rate-limited.
void FilesPage::detectSdChange() {
  static unsigned long lastSdPollMs = 0;
  unsigned long now = millis();
  if (now - lastSdPollMs < 500)
    return; // poll at most twice per second
  lastSdPollMs = now;

  bool current = SD.begin(SD_CS_PIN);
  if (current != sdAvailable) {
    sdAvailable = current;
    if (sdAvailable) {
      // newly inserted -> go to root and refresh
      currentDir = "/";
      topIndex = 0;
      highlightedRow = -1;
      refreshEntries();
    } else {
      // removed -> clear listing
      allEntries.clear();
      totalEntries = 0;
      topIndex = 0;
      highlightedRow = -1;
    }
  }
}

FilesPage::FilesPage() {
  currentDir = "/";
  totalEntries = 0;
  topIndex = 0;
  highlightedRow = -1;
  selectionActive = false;
  sdAvailable = false;
  lastCenterTapMs = 0;
}

String FilesPage::getEntryNameAt(int absIndex) {
  if (absIndex < 0)
    return String();
  bool hasParent = (currentDir != "/");
  if (hasParent) {
    if (absIndex == 0)
      return String("../");
    absIndex -= 1; // skip parent when scanning dir
  }
  File root = SD.open(currentDir.c_str());
  if (!root)
    return String();
  int idx = 0;
  File entry = root.openNextFile();
  while (entry) {
    if (idx == absIndex) {
      String name = String(entry.name());
      if (entry.isDirectory())
        name += '/';
      root.close();
      return name;
    }
    idx++;
    entry = root.openNextFile();
  }
  root.close();
  return String();
}

void FilesPage::pollSd() { detectSdChange(); }

void FilesPage::refreshEntries() {
  // Lazy counting of entries to avoid loading large directories into RAM
  totalEntries = 0;
  sdAvailable = SD.begin(SD_CS_PIN);
  if (!sdAvailable) {
    totalEntries = 0;
    topIndex = 0;
    highlightedRow = -1;
    return;
  }
  // count entries
  File dir = SD.open(currentDir.c_str());
  if (!dir) {
    totalEntries = 0;
  } else {
    int cnt = 0;
    File f = dir.openNextFile();
    while (f) {
      cnt++;
      f = dir.openNextFile();
    }
    dir.close();
    // include parent entry if not root
    totalEntries = cnt + ((currentDir == "/") ? 0 : 1);
  }

  // clamp indices
  if (topIndex < 0)
    topIndex = 0;
  if (highlightedRow < 0)
    highlightedRow = (currentDir == "/") ? -1 : 0;
  if (topIndex > max(0, totalEntries - 4))
    topIndex = max(0, totalEntries - 4);
  if (highlightedRow > 3)
    highlightedRow = min(3, max(0, totalEntries - 1 - topIndex));
  // invalidate visible cache so it will be rebuilt on next render
  visibleCache.clear();
  visibleCacheStart = -1;
}

void FilesPage::fillVisibleCache() {
  if (totalEntries == 0) {
    visibleCache.clear();
    visibleCacheStart = -1;
    return;
  }
  if (visibleCacheStart == topIndex && (int)visibleCache.size() >= visibleRows)
    return;
  visibleCache.clear();
  visibleCacheStart = topIndex;
  for (int r = 0; r < visibleRows; ++r) {
    int idx = topIndex + r;
    if (idx >= totalEntries)
      break;
    String nm = getEntryNameAt(idx);
    visibleCache.push_back(nm);
  }
}

void FilesPage::render(bool full) {
  // detect SD insertion/removal before rendering
  pollSd();
  const int startY = 20;
  const int rowH = 18;
  const int rowW = display.width() - 20;
  const int footerHeight = 18;

  // Full refresh: draw everything in one pass (use same entry layout as
  // partial)
  if (full) {
    refreshInProgress = true;
    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
      // ensure foreground is black by default for messages/footer
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);
      if (!sdAvailable) {
        u8g2Fonts.setCursor(display.width() / 2 - 20, display.height() / 2);
        u8g2Fonts.print("无卡或读取失败");
      } else if (totalEntries == 0) {
        u8g2Fonts.setCursor(display.width() / 2 - 20, display.height() / 2);
        u8g2Fonts.print("(无文件)");
      } else {
        if (topIndex < 0)
          topIndex = 0;
        if (totalEntries == 0)
          topIndex = 0;
        else if (topIndex > totalEntries - 1)
          topIndex = totalEntries - 1;
        // prepare visible cache
        fillVisibleCache();
        for (int r = 0; r < visibleRows; r++) {
          int idx = topIndex + r;
          int y = startY + r * rowH + rowH;
          bool h = (highlightedRow == r);
          if (idx < totalEntries) {
            String nm = (r < (int)visibleCache.size()) ? visibleCache[r]
                                                       : getEntryNameAt(idx);
            if (h) {
              display.fillRect(0, y - rowH - 1, rowW, rowH, GxEPD_BLACK);
              u8g2Fonts.setForegroundColor(GxEPD_WHITE);
            } else {
              // use same rectangle geometry for non-highlighted rows to avoid
              // misalignment
              display.fillRect(0, y - rowH - 1, rowW, rowH, GxEPD_WHITE);
              u8g2Fonts.setForegroundColor(GxEPD_BLACK);
            }
            u8g2Fonts.setCursor(5, y - 4);
            u8g2Fonts.print(nm);
          } else {
            if (r == 3) {
              u8g2Fonts.setCursor(5, y - 4);
              u8g2Fonts.print("...");
            }
          }
        }
      }
      // footer
      int dividerY = display.height() - footerHeight;
      display.drawFastHLine(0, dividerY, display.width(), GxEPD_BLACK);
      // ensure footer text uses black foreground
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);
      u8g2Fonts.setCursor(5, dividerY + 15);
      u8g2Fonts.print("< 闹钟");
      u8g2Fonts.setCursor(172, dividerY + 15);
      u8g2Fonts.print("设置 >");
      String title = "文件浏览";
      int tw = u8g2Fonts.getUTF8Width(title.c_str());
      u8g2Fonts.setCursor((display.width() - tw) / 2 - 20, dividerY + 15);
      u8g2Fonts.print(title);
    } while (display.nextPage());
    refreshInProgress = false;
    return;
  }

  // Partial refresh: draw list area and footer separately so footer isn't left
  // as previous page's content
  int listPartialY = startY - 4;
  int listPartialH =
      display.height() - startY - footerHeight - 2; // leave space for footer
  if (listPartialH > 0) {
    display.setPartialWindow(0, listPartialY, display.width(), listPartialH);
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
      // ensure default foreground is black for the list area
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);
      // if no SD
      if (!sdAvailable) {
        u8g2Fonts.setCursor(display.width() / 2 - 20, display.height() / 2);
        u8g2Fonts.print("无卡或读取失败");
      } else if (totalEntries == 0) {
        u8g2Fonts.setCursor(display.width() / 2 - 20, display.height() / 2);
        u8g2Fonts.print("(无文件)");
      } else {
        // ensure topIndex and highlightedRow within bounds
        if (topIndex < 0)
          topIndex = 0;
        if (topIndex > max(0, totalEntries - 4))
          topIndex = max(0, totalEntries - 4);
        // prepare visible cache
        fillVisibleCache();
        for (int r = 0; r < visibleRows; r++) {
          int idx = topIndex + r;
          int y = startY + r * rowH + rowH;
          bool h = (highlightedRow == r);
          if (idx < totalEntries) {
            String nm = (r < (int)visibleCache.size()) ? visibleCache[r]
                                                       : getEntryNameAt(idx);
            if (h) {
              display.fillRect(0, y - rowH - 1, rowW, rowH, GxEPD_BLACK);
              u8g2Fonts.setForegroundColor(GxEPD_WHITE);
            } else {
              // match geometry used for highlighted rows
              display.fillRect(0, y - rowH - 1, rowW, rowH, GxEPD_WHITE);
              u8g2Fonts.setForegroundColor(GxEPD_BLACK);
            }
            u8g2Fonts.setCursor(5, y - 4);
            u8g2Fonts.print(nm);
          } else {
            // if past end, show placeholder '...'
            if (r == 3) {
              u8g2Fonts.setCursor(5, y - 4);
              u8g2Fonts.print("...");
            }
          }
        }
      }
    } while (display.nextPage());
  }

  // Draw footer area with a separate partial update to ensure it's correct for
  // this page
  int dividerY = display.height() - footerHeight;
  int footerPartialY = dividerY - 4;
  int footerPartialH = footerHeight + 8;
  display.setPartialWindow(0, footerPartialY, display.width(), footerPartialH);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawFastHLine(0, dividerY, display.width(), GxEPD_BLACK);
    // explicitly set font and foreground for footer
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setCursor(5, dividerY + 15);
    u8g2Fonts.print("< 闹钟");
    u8g2Fonts.setCursor(172, dividerY + 15);
    u8g2Fonts.print("设置 >");
    String title = "文件浏览";
    int tw = u8g2Fonts.getUTF8Width(title.c_str());
    u8g2Fonts.setCursor((display.width() - tw) / 2 - 20, dividerY + 15);
    u8g2Fonts.print(title);
  } while (display.nextPage());
}

void FilesPage::onLeft() {
  // poll SD state first
  pollSd();
  // New behavior: left should navigate up (to parent directory) when an
  // entry is highlighted or selection is active. Only when nothing is
  // highlighted should left switch pages.
  if (sdAvailable && totalEntries > 0 &&
      (selectionActive || highlightedRow >= 0)) {
    // If at root and an entry is highlighted (or selection active), do nothing
    // (user wanted no response instead of triggering a page switch).
    if (currentDir == "/") {
      // If we're at root, clear any selection/highlight instead of doing
      // nothing so user gets feedback that left collapsed selection.
      highlightedRow = -1;
      selectionActive = false;
      render(false); // partial refresh to update list/footer quickly
      lastInteraction = millis();
      return;
    }
    // navigate up one directory
    if (currentDir != "/") {
      String tmp = currentDir;
      if (tmp.endsWith("/"))
        tmp = tmp.substring(0, tmp.length() - 1);
      int id = tmp.lastIndexOf('/');
      if (id >= 0)
        tmp = tmp.substring(0, id + 1);
      if (tmp.length() == 0)
        tmp = "/";
      currentDir = tmp;
      selectionActive = false;
      topIndex = 0;
      highlightedRow = 0;
      refreshEntries();
      // clamp
      if (totalEntries == 0) {
        highlightedRow = -1;
        topIndex = 0;
      } else {
        if (topIndex < 0)
          topIndex = 0;
        int maxTop = max(0, totalEntries - 4);
        if (topIndex > maxTop)
          topIndex = maxTop;
        int maxRow = min(3, max(0, totalEntries - 1 - topIndex));
        if (highlightedRow < 0)
          highlightedRow = 0;
        if (highlightedRow > maxRow)
          highlightedRow = maxRow;
      }
      render(true);
      lastInteraction = millis();
      return;
    }
  }
  // no highlight -> switch to previous app page
  switchPageAndFullRefresh(currentPage - 1);
}

void FilesPage::onRight() {
  // poll SD state first
  pollSd();
  // If selection active, right opens (confirm). If not active, right switches
  // page.
  if (selectionActive) {
    openSelected();
    return;
  }
  // if there's a highlighted row, treat right as open (user intends to open
  // entry)
  if (sdAvailable && totalEntries > 0 && highlightedRow >= 0) {
    openSelected();
    return;
  }
  // selection not active and no highlighted entry -> right means next app page
  switchPageAndFullRefresh(currentPage + 1);
}

void FilesPage::onCenter() {
  // poll SD state first
  pollSd();
  // If no SD or no entries, center toggles nothing
  if (!sdAvailable || totalEntries == 0) {
    // do a small flash
    render(true);
    lastInteraction = millis();
    return;
  }
  unsigned long now = millis();
  // If already in selection mode, center is a no-op (could be changed later)
  if (selectionActive) {
    render(false);
    lastInteraction = now;
    lastCenterTapMs = now;
    return;
  }

  // Double-tap center to enter selection mode. Single tap cycles highlighted
  // row.
  const unsigned long doubleTapMs = 400;
  if (now - lastCenterTapMs <= doubleTapMs) {
    // double tap -> enter selection mode if we have a highlighted row
    if (highlightedRow < 0)
      highlightedRow = 0;
    selectionActive = true;
    render(false);
    lastInteraction = now;
    lastCenterTapMs = 0;
    return;
  }

  // single tap -> advance highlightedRow (cycle through entries visible and
  // beyond) compute absolute index
  int absIdx = (highlightedRow < 0) ? -1 : topIndex + highlightedRow;
  if (absIdx < 0) {
    // start at first visible and return view to top (wrap to top)
    if (totalEntries > 0) {
      highlightedRow = 0;
      topIndex = 0; // explicitly go back to top so user can see beginning
    }
  } else {
    // move to next entry if available
    if (absIdx + 1 < totalEntries) {
      absIdx++;
      // if next is outside current window, slide window
      if (absIdx >= topIndex + 4) {
        topIndex = absIdx - 3;
      }
      highlightedRow = absIdx - topIndex;
    } else {
      // past last -> wrap to no highlight
      highlightedRow = -1;
    }
  }
  // always perform a partial refresh (render(false)) to keep UX snappy;
  // render(false) also redraws footer area in this page implementation
  render(false);
  lastInteraction = now;
  lastCenterTapMs = now;
}

void FilesPage::openSelected() {
  // poll SD state first
  pollSd();
  if (!sdAvailable || totalEntries == 0) {
    render(true);
    lastInteraction = millis();
    return;
  }
  int idx = topIndex + highlightedRow;
  if (idx < 0 || idx >= totalEntries) {
    render(false);
    lastInteraction = millis();
    return;
  }
  String fname = getEntryNameAt(idx);
  String lower = fname;
  lower.toLowerCase();
  Serial.println("CurrentDir:" + currentDir);
  // directories end with '/'
  if (fname.endsWith("/")) {
    String nameNoSlash = fname.substring(0, fname.length() - 1);
    if (nameNoSlash == "..") {
      // go up
      if (currentDir != "/") {
        String tmp = currentDir;
        if (tmp.endsWith("/"))
          tmp = tmp.substring(0, tmp.length() - 1);
        int id = tmp.lastIndexOf('/');
        if (id >= 0)
          tmp = tmp.substring(0, id + 1);
        if (tmp.length() == 0)
          tmp = "/";
        currentDir = tmp;
      }
    } else {
      // enter subdir: construct candidate path and test it before committing
      String candidate;
      if (currentDir == "/")
        candidate = String("/") + nameNoSlash;
      else
        candidate = currentDir + String("/") + nameNoSlash;
      bool found = false;
      String path = candidate;
      File testDir = SD.open(path.c_str());
      if (testDir) {
        testDir.close();
        found = true;
      }
  if (!found) {
        // cannot open directory -> show error and abort
        display.setFullWindow();
        display.firstPage();
        do {
          display.fillScreen(GxEPD_WHITE);
          // explicitly set font and black foreground before printing error
          u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
          u8g2Fonts.setForegroundColor(GxEPD_BLACK);
          u8g2Fonts.setCursor(10, display.height() / 2 - 6);
          u8g2Fonts.print("无法进入目录");
        } while (display.nextPage());
        delay(800);
        // keep currentDir unchanged
        render(true);
        lastInteraction = millis();
        return;
      }
      candidate = path;
      currentDir = candidate;
      // show a small loading prompt when entering a directory to indicate work
      int px = 10;
      int py = 30;
      int pw = display.width() - 80;
      int ph = 28;
      display.setPartialWindow(px, py, pw, ph);
      display.firstPage();
      do {
        // draw white background and black border for a compact popup
        display.fillRect(px, py, pw, ph, GxEPD_WHITE);
        display.drawRect(px, py, pw, ph, GxEPD_BLACK);
        u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
        u8g2Fonts.setForegroundColor(GxEPD_BLACK);
        u8g2Fonts.setCursor(px + 8, py + 18);
        u8g2Fonts.print("加载中...");
      } while (display.nextPage());
    }
    // refresh and reset window
    selectionActive = false;
    topIndex = 0;
      // after refresh, set highlightedRow to first real entry (if ../ was present
      // it will be at index 0, so we prefer to highlight the first real file
      // which comes after ../). For root directory, keep no selection (-1).
      if (currentDir == "/")
        highlightedRow = -1;
      else
        highlightedRow = 0;
    refreshEntries();
    // ensure indices are valid after entries refresh to avoid drawing an
    // invalid highlighted row which can cause glyphs to render incorrectly
    if (totalEntries == 0) {
      highlightedRow = -1;
      topIndex = 0;
    } else {
      if (topIndex < 0)
        topIndex = 0;
      int maxTop = max(0, totalEntries - 4);
      if (topIndex > maxTop)
        topIndex = maxTop;
      int maxRow = min(3, max(0, totalEntries - 1 - topIndex));
      if (highlightedRow < 0)
        highlightedRow = 0;
      if (highlightedRow > maxRow)
        highlightedRow = maxRow;
    }
    // entering a new directory is a substantial layout change; do a full
    // refresh to ensure all regions (list + footer) are correctly drawn
  render(true);
    lastInteraction = millis();
    return;
  }

  // for files: only accept open action for supported types, otherwise show name
  if (lower.endsWith(".mp3") || lower.endsWith(".flac") ||
      lower.endsWith(".aac") || lower.endsWith(".wav") ||
      lower.endsWith(".m4a") || lower.endsWith(".txt")) {
    // If it's a text file, open in ebook viewer
    if (lower.endsWith(".txt")) {
      // construct absolute path for file (currentDir + name)
      String apath;
      if (currentDir == "/")
        apath = String("/") + fname;
      else
        apath = currentDir + String("/") + fname;
      // attempt to open via main-provided helper
      extern bool openEbookFromPath(const String &path);
      if (openEbookFromPath(apath)) {
        lastInteraction = millis();
        return;
      }
      // failed to open: show error
      display.setFullWindow();
      display.firstPage();
      do {
        display.fillScreen(GxEPD_WHITE);
        u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
        u8g2Fonts.setForegroundColor(GxEPD_BLACK);
        u8g2Fonts.setCursor(10, display.height() / 2 - 6);
        u8g2Fonts.print("无法打开文本文件");
      } while (display.nextPage());
      delay(800);
      selectionActive = false;
      render(true);
      return;
    }
    // For other supported types, just show filename for now
    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      // ensure font and foreground set for file name display
      u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);
      u8g2Fonts.setCursor(10, display.height() / 2 - 6);
      u8g2Fonts.print("打开: ");
      u8g2Fonts.print(fname);
    } while (display.nextPage());
    delay(800);
    selectionActive = false;
    render(true);
  } else {
    // unsupported: flash
    render(false);
  }
  lastInteraction = millis();
}

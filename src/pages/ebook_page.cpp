#include "ebook_page.h"
#include "../app_context.h"
#include "pages/page_manager.h"
#include <SD.h>

#include <Preferences.h>
#include "../utils/utils.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

extern int currentPage;
extern PageManager gPageMgr;

// forward declare helper used during pagination
static int countWrappedLines(const String &paragraph, int maxWidth);

// mutex to protect pageOffsets vector between main task and precompute task
static SemaphoreHandle_t s_pageOffsetsMutex = NULL;
// handle for currently running precompute task (only one at a time)
static TaskHandle_t s_precomputeTaskHandle = NULL;

// entry for precompute task
static void precomputeTaskEntry(void *arg) {
  EBookPage *page = (EBookPage *)arg;
  if (!page) {
    vTaskDelete(NULL);
    return;
  }
  // compute pages around current index (center +/- 1..2)
  int center = page->getPageIndex();
  page->ensurePageIndexUpTo(center + 2);

  // clear task handle under mutex
  if (s_pageOffsetsMutex) {
    if (xSemaphoreTake(s_pageOffsetsMutex, pdMS_TO_TICKS(100))) {
      s_precomputeTaskHandle = NULL;
      xSemaphoreGive(s_pageOffsetsMutex);
    }
  }
  vTaskDelete(NULL);
}

EBookPage::EBookPage() { pageIndex = 0; hasFile = false; }

bool EBookPage::buildPageIndex(const String &absPath) {
  // Lazy indexing: only set up initial state (first page). Heavy scanning is deferred.
  pageOffsets.clear();
  openedPath = absPath;
  File f = SD.open(absPath.c_str());
  if (!f) return false;
  pageOffsets.push_back(0); // first page starts at byte 0
  f.close();
  // compute first couple pages synchronously to ensure pagination is available
  // immediately after opening (avoid showing only one page for large files)
  ensurePageIndexUpTo(2);
  return true;
}

// compute the byte offset where the next page starts given a start offset in the file
unsigned long EBookPage::computeNextPageOffset(unsigned long startOffset) {
  File f = SD.open(openedPath.c_str());
  if (!f) return startOffset;
  unsigned long offset = startOffset;
  f.seek(startOffset);
  const int maxWidth = display.width() - 40;
  int lineCount = 0;
  // use a fixed buffer for current visual line to avoid String churn
  char lineBuf[512];
  size_t lineLen = 0;
  lineBuf[0] = '\0';

  auto charWidth = [&](const uint8_t *bytes, int cb) -> int {
    char tmp[5];
    if (cb > 4) cb = 4;
    for (int i = 0; i < cb; ++i) tmp[i] = (char)bytes[i];
    tmp[cb] = '\0';
    return u8g2Fonts.getUTF8Width(tmp);
  };

  while (f.available()) {
    uint8_t first = f.read();
    offset++;
    int cb = 1;
    if ((first & 0x80) != 0) {
      if ((first & 0xE0) == 0xC0) cb = 2;
      else if ((first & 0xF0) == 0xE0) cb = 3;
      else if ((first & 0xF8) == 0xF0) cb = 4;
    }
    uint8_t bytes[4];
    bytes[0] = first;
    for (int i = 1; i < cb; ++i) {
      if (!f.available()) { cb = i; break; }
      bytes[i] = f.read();
      offset++;
    }
    // newline as line break
    if (cb == 1 && bytes[0] == '\n') {
      lineCount++;
      lineLen = 0;
      lineBuf[0] = '\0';
      if (lineCount >= linesPerPage) {
        f.close();
        return offset;
      }
      continue;
    }

    // if line buffer would overflow, force a wrap
    if (lineLen + (size_t)cb >= sizeof(lineBuf) - 1) {
      lineCount++;
      // start next line with this char if it fits width, else drop
      int cw = charWidth(bytes, cb);
      if (cw > maxWidth) {
        lineLen = 0;
        lineBuf[0] = '\0';
      } else {
        memcpy(lineBuf, bytes, cb);
        lineLen = cb;
        lineBuf[lineLen] = '\0';
      }
      if (lineCount >= linesPerPage) {
        f.close();
        return offset;
      }
      continue;
    }

    // probe width with this char appended
    memcpy(lineBuf + lineLen, bytes, cb);
    lineBuf[lineLen + cb] = '\0';
    int w = u8g2Fonts.getUTF8Width(lineBuf);
    if (w > maxWidth) {
      // wrap before this char
      lineCount++;
      int cw = charWidth(bytes, cb);
      if (cw > maxWidth) {
        // too wide alone: skip to avoid infinite loop
        lineLen = 0;
        lineBuf[0] = '\0';
      } else {
        memcpy(lineBuf, bytes, cb);
        lineLen = cb;
        lineBuf[lineLen] = '\0';
      }
      if (lineCount >= linesPerPage) {
        f.close();
        return offset;
      }
    } else {
      // keep appended
      lineLen += cb;
    }
  }
  // EOF reached: next page is EOF
  unsigned long eof = (unsigned long)f.size();
  f.close();
  return eof;
}

// ensure pageOffsets contains index idx by computing pages lazily
bool EBookPage::ensurePageIndexUpTo(int idx) {
  if (idx < (int)pageOffsets.size()) return true;
  // compute pages until we have idx+1 start offsets or reach EOF
  // fetch file size once to avoid repeated open/close
  unsigned long fsz = 0xFFFFFFFF;
  {
    File ff = SD.open(openedPath.c_str());
    if (ff) { fsz = (unsigned long)ff.size(); ff.close(); }
  }
  while ((int)pageOffsets.size() <= idx) {
    unsigned long last = pageOffsets.back();
    unsigned long next = computeNextPageOffset(last);
    if (next == last) return false; // stuck
    // protect vector modification
    if (!s_pageOffsetsMutex) s_pageOffsetsMutex = xSemaphoreCreateMutex();
    if (s_pageOffsetsMutex) xSemaphoreTake(s_pageOffsetsMutex, pdMS_TO_TICKS(200));
    pageOffsets.push_back(next);
    if (s_pageOffsetsMutex) xSemaphoreGive(s_pageOffsetsMutex);
    // stop if we've reached EOF
    if (next >= fsz) break;
  }
  return idx < (int)pageOffsets.size();
}

void EBookPage::startPrecomputeAsync(int centerIndex) {
  // create mutex if needed
  if (!s_pageOffsetsMutex) s_pageOffsetsMutex = xSemaphoreCreateMutex();
  // if a precompute task is already running, don't start another
  if (s_pageOffsetsMutex) {
    if (!xSemaphoreTake(s_pageOffsetsMutex, pdMS_TO_TICKS(50))) return;
    if (s_precomputeTaskHandle != NULL) {
      xSemaphoreGive(s_pageOffsetsMutex);
      return;
    }
    // start task
    BaseType_t r = xTaskCreatePinnedToCore(precomputeTaskEntry, "ebook_precompute", 4096, this, tskIDLE_PRIORITY + 1, &s_precomputeTaskHandle, 1);
    xSemaphoreGive(s_pageOffsetsMutex);
    (void)r;
  } else {
    // fallback: run synchronously
    ensurePageIndexUpTo(centerIndex + 2);
  }
}

int EBookPage::estimateTotalPagesApprox() {
  if (openedPath.length() == 0) return 0;
  File f = SD.open(openedPath.c_str());
  if (!f) return 0;
  unsigned long fsz = (unsigned long)f.size();
  f.close();
  // compute first page length in bytes (ensure it's available)
  if (!ensurePageIndexUpTo(0)) return 1;
  unsigned long firstStart = pageOffsets.size() > 0 ? pageOffsets[0] : 0;
  unsigned long firstNext = 0;
  if (pageOffsets.size() > 1) firstNext = pageOffsets[1];
  else firstNext = computeNextPageOffset(firstStart);
  unsigned long firstLen = (firstNext > firstStart) ? (firstNext - firstStart) : 1;
  int approx = (int)(fsz / firstLen);
  if (approx < 1) approx = 1;
  return approx;
}

String EBookPage::loadPageContent(int idx) {
  String out;
  if (idx < 0 || idx >= (int)pageOffsets.size()) return out;
  File f = SD.open(openedPath.c_str());
  if (!f) return out;
  // ensure we have the end offset for this page to avoid reading to EOF
  if (idx + 1 >= (int)pageOffsets.size()) {
    ensurePageIndexUpTo(idx + 1);
  }
  unsigned long start = pageOffsets[idx];
  unsigned long end = (idx + 1 < (int)pageOffsets.size()) ? pageOffsets[idx + 1] : (unsigned long)f.size();
  // seek and read range
  f.seek(start);
  unsigned long toRead = end - start;
  // reserve a reasonable capacity to reduce reallocations (page sizes are small)
  size_t cap = (size_t)toRead;
  if (cap > 4096) cap = 4096;
  out.reserve(cap);
  const unsigned long chunk = 256;
  while (toRead > 0 && f.available()) {
    unsigned long r = (toRead > chunk) ? chunk : toRead;
    char buf[257];
    size_t got = f.read((uint8_t *)buf, r);
    if (got == 0) break;
    buf[got] = 0;
    // avoid creating temporary String objects; append C string directly
    out += (const char *)buf;
    toRead -= got;
  }
  f.close();
  return out;
}

bool EBookPage::openFromFile(const String &absPath) {
  // detect encoding heuristically and store to prefs if user hasn't configured
  ETextEncoding enc = detectEncodingFromFile(absPath, 8192);
  // store as integer in preferences under key provided by encoding helper
  Preferences encpf; encpf.begin("ebook", false);
  const char *k = ebookEncodingPrefKey();
  int existing = encpf.getInt(k, -1); // -1 means unset
  if (existing == -1) {
    encpf.putInt(k, (int)enc);
  }
  encpf.end();

  bool ok = buildPageIndex(absPath);
  if (!ok) return false;
  hasFile = true;
  // restore saved page index from Preferences if exists
  prefs.begin("ebook", false);
  // key: use simple hash of path (length-limited)
  uint16_t h = 0;
  for (size_t i = 0; i < absPath.length(); ++i) h = h * 31 + (uint8_t)absPath[i];
  String key = String("p") + String(h);
  pageIndex = prefs.getUShort(key.c_str(), 0);
  prefs.end();
  // clamp restored pageIndex to valid range
  if (pageOffsets.empty()) pageIndex = 0;
  else if (pageIndex >= (int)pageOffsets.size()) pageIndex = (int)pageOffsets.size() - 1;
  promptVisible = false;
  // disable auto-home while reading ebook
  origInactivityTimeout = 30000; // fallback store
  gPageMgr.setInactivityTimeout(0xFFFFFFFF);
  return true;
}

// helper: draw text with wrapping given available width; returns y after last drawn line
static int drawWrappedText(int x, int y, int maxWidth, int lineH, const String &text) {
  int pos = 0;
  int len = text.length();
  while (pos < len) {
    // handle explicit newlines: extract until next '\n'
    int nl = text.indexOf('\n', pos);
    String line;
    if (nl >= 0) {
      line = text.substring(pos, nl);
    } else {
      line = text.substring(pos);
    }
    int cur = 0;
    int llen = line.length();
    while (cur < llen) {
      // find how many bytes fit
      int end = cur;
      int lastGood = cur;
      while (end < llen) {
        // advance by utf8 char bytes
        uint8_t c = (uint8_t)line[end];
        int cb = 1;
        if ((c & 0x80) != 0) {
          if ((c & 0xE0) == 0xC0) cb = 2;
          else if ((c & 0xF0) == 0xE0) cb = 3;
          else if ((c & 0xF8) == 0xF0) cb = 4;
        }
        if (end + cb > llen) break;
        int probeEnd = end + cb;
        String probe = line.substring(cur, probeEnd);
        int w = u8g2Fonts.getUTF8Width(probe.c_str());
        if (w > maxWidth) break;
        lastGood = probeEnd;
        end = probeEnd;
      }
      if (lastGood == cur) {
        // single char too wide? force at least one char
        lastGood = cur + 1;
      }
      String out = line.substring(cur, lastGood);
      u8g2Fonts.setCursor(x, y);
      u8g2Fonts.print(out);
      y += lineH;
      cur = lastGood;
    }
    // skip the newline
    if (nl >= 0) pos = nl + 1;
    else pos = len;
  }
  return y;
}

// helper: count how many visual lines the paragraph will occupy with given width
static int countWrappedLines(const String &paragraph, int maxWidth) {
  int lines = 0;
  int cur = 0;
  int plen = paragraph.length();
  while (cur < plen) {
    int end = cur;
    int lastGood = cur;
    while (end < plen) {
      uint8_t c = (uint8_t)paragraph[end];
      int cb = 1;
      if ((c & 0x80) != 0) {
        if ((c & 0xE0) == 0xC0) cb = 2;
        else if ((c & 0xF0) == 0xE0) cb = 3;
        else if ((c & 0xF8) == 0xF0) cb = 4;
      }
      if (end + cb > plen) break;
      int probeEnd = end + cb;
      String probe = paragraph.substring(cur, probeEnd);
      int w = u8g2Fonts.getUTF8Width(probe.c_str());
      if (w > maxWidth) break;
      lastGood = probeEnd;
      end = probeEnd;
    }
    if (lastGood == cur) lastGood = cur + 1;
    cur = lastGood;
    lines++;
  }
  return lines;
}

void EBookPage::render(bool full) {
  if (!hasFile) {
    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setCursor(0, 30);
      u8g2Fonts.print("(无打开的电子书)");
    } while (display.nextPage());
    return;
  }

  // If full==false: do a lightweight footer partial update (time + filename)
  const int footerH = 20;
  const int footerY = display.height() - footerH;
  if (!full) {
    // partial footer: update time and filename
    // use the exact footer area to avoid overlapping the content above
    display.setPartialWindow(0, footerY, display.width(), footerH);
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      display.drawFastHLine(0, footerY, display.width(), GxEPD_BLACK);
      u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);
      // time
      time_t raw = timeClient.getEpochTime();
      struct tm *tm = localtime(&raw);
      char timestr[6];
      sprintf(timestr, "%02d:%02d", tm->tm_hour, tm->tm_min);
          // Determine page total to display: prefer actual if we have more than 1
          // page computed; otherwise show estimate. Preserve estimate until actual
          // page count strictly exceeds estimate or EOF is reached.
          int actualCount = 0;
          if (s_pageOffsetsMutex && xSemaphoreTake(s_pageOffsetsMutex, pdMS_TO_TICKS(50))) {
            actualCount = (int)pageOffsets.size();
            xSemaphoreGive(s_pageOffsetsMutex);
          } else {
            actualCount = (int)pageOffsets.size();
          }
          int approx = estimateTotalPagesApprox();
          int totalShown = (actualCount > approx) ? actualCount : approx;
          // If actualCount==0 fallback to approx
          if (actualCount == 0) totalShown = approx;
          String pageinfo = String(timestr) + " " + String(pageIndex + 1) + "/" + String(totalShown);
      int tw = u8g2Fonts.getUTF8Width(pageinfo.c_str());
      u8g2Fonts.setCursor(display.width() - tw - 40, display.height() - 4);
      u8g2Fonts.print(pageinfo);
      // filename on left (basename)
      String fname = openedPath;
      int p = fname.lastIndexOf('/');
      if (p >= 0) fname = fname.substring(p + 1);
      int avail = display.width() - tw - 12; // keep margin before time
      String left = fitToWidthSingleLine(fname, avail - 8);
      u8g2Fonts.setCursor(6, display.height() - 4);
      u8g2Fonts.print(left);
    } while (display.nextPage());
    return;
  }

  // Full refresh: draw text content with wrapping
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    // ensure current page start and next page start exist to bound reads
    ensurePageIndexUpTo(pageIndex + 1);
  // draw text content (load on-demand to avoid large RAM usage)
  String p = loadPageContent(pageIndex);
  int x = 0;
  int y = 30;
  int lineH = 14; // slightly smaller line spacing per request
  // Limit drawing area height so at most linesPerPage visual lines are drawn
  int contentH = lineH * linesPerPage;
  // drawWrappedText currently returns y after drawing; we don't need the final y
  drawWrappedText(x, y, display.width() - 40, lineH, p);

  // footer: right-bottom page/time hh:mm cur/total and filename left
    display.drawFastHLine(0, display.height() - 18, display.width(), GxEPD_BLACK);
    // time and page
    time_t raw = timeClient.getEpochTime();
    struct tm *tm = localtime(&raw);
    char timestr[6];
    sprintf(timestr, "%02d:%02d", tm->tm_hour, tm->tm_min);
      // prefer showing actual if > estimate; otherwise show estimate until it is
      // outgrown. Use mutex for safe read of pageOffsets when available.
      int actualCountFull = 0;
      if (s_pageOffsetsMutex && xSemaphoreTake(s_pageOffsetsMutex, pdMS_TO_TICKS(50))) {
        actualCountFull = (int)pageOffsets.size();
        xSemaphoreGive(s_pageOffsetsMutex);
      } else {
        actualCountFull = (int)pageOffsets.size();
      }
      int approxFull = estimateTotalPagesApprox();
      int totalShownFull = (actualCountFull > approxFull) ? actualCountFull : approxFull;
      if (actualCountFull == 0) totalShownFull = approxFull;
      String pageinfo = String(timestr) + " " + String(pageIndex + 1) + "/" + String(totalShownFull);
  int tw = u8g2Fonts.getUTF8Width(pageinfo.c_str());
  u8g2Fonts.setCursor(display.width() - tw - 40, display.height() - 4);
  u8g2Fonts.print(pageinfo);
    // filename left, truncated to avoid overlapping pageinfo
    String fname = openedPath;
    int pos = fname.lastIndexOf('/');
    if (pos >= 0) fname = fname.substring(pos + 1);
    int avail = display.width() - tw - 12;
    String left = fitToWidthSingleLine(fname, avail - 8);
    u8g2Fonts.setCursor(6, display.height() - 4);
    u8g2Fonts.print(left);
  } while (display.nextPage());

  // if prompt visible, draw it as partial overlay
  if (promptVisible) showPromptPartial();
  // start async precompute of nearby pages
  startPrecomputeAsync(pageIndex);
}

void EBookPage::showPromptPartial() {
  // small centered box with "长按2秒退出..."
  int pw = 120;
  int ph = 40;
  int px = (display.width() - pw) / 2;
  int py = (display.height() - ph) / 2;
  display.setPartialWindow(px, py, pw, ph);
  display.firstPage();
  do {
    display.fillRect(px, py, pw, ph, GxEPD_WHITE);
    display.drawRect(px, py, pw, ph, GxEPD_BLACK);
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2Fonts.setCursor(px + 8, py + 22);
    u8g2Fonts.print("长按2秒退出...");
  } while (display.nextPage());
}

void EBookPage::hidePromptFull() {
  // full refresh to clear prompt
  render(true);
}

void EBookPage::exitToFiles() {
  // restore inactivity timeout
  gPageMgr.setInactivityTimeout(30000);
  // persist current page
  if (hasFile) {
    prefs.begin("ebook", false);
    uint16_t h = 0;
    for (size_t i = 0; i < openedPath.length(); ++i) h = h * 31 + (uint8_t)openedPath[i];
    String key = String("p") + String(h);
    prefs.putUShort(key.c_str(), (uint16_t)pageIndex);
    prefs.end();
  }
  // switch back to files page (assumed index 3)
  switchPageAndFullRefresh(3);
  hasFile = false;
}

void EBookPage::onLeft() {
  // left = previous page (natural mapping)
  if (!hasFile) { switchPageAndFullRefresh(currentPage - 1); return; }
  if (promptVisible) {
    // if prompt visible, ignore
    return;
  }
  if (pageIndex > 0) {
    pageIndex--;
    // save progress
    prefs.begin("ebook", false);
    uint16_t h = 0;
    for (size_t i = 0; i < openedPath.length(); ++i) h = h * 31 + (uint8_t)openedPath[i];
    String key = String("p") + String(h);
    prefs.putUShort(key.c_str(), (uint16_t)pageIndex);
    prefs.end();
    render(true);
    lastInteraction = millis();
  }
}

void EBookPage::onRight() {
  // right = next page (natural mapping)
  if (!hasFile) { switchPageAndFullRefresh(currentPage + 1); return; }
  if (promptVisible) {
    // if prompt visible, ignore
    return;
  }
  // ensure next page offset is available (compute lazily)
  ensurePageIndexUpTo(pageIndex + 1);
  if (pageIndex + 1 < (int)pageOffsets.size()) {
    pageIndex++;
    prefs.begin("ebook", false);
    uint16_t h = 0;
    for (size_t i = 0; i < openedPath.length(); ++i) h = h * 31 + (uint8_t)openedPath[i];
    String key = String("p") + String(h);
    prefs.putUShort(key.c_str(), (uint16_t)pageIndex);
    prefs.end();
    render(true);
    lastInteraction = millis();
  }
  // precompute next pages asynchronously
  startPrecomputeAsync(pageIndex);
}

void EBookPage::onCenter() {
  if (!hasFile) return;
  // show prompt as partial overlay when tapped
  unsigned long t0 = millis();
  promptVisible = true;
  promptShownAt = t0;
  showPromptPartial();

  const unsigned long requiredHold = 2000; // ms
  // poll raw button state until either released or requiredHold elapsed
  while (millis() - t0 < requiredHold) {
    int bs = readButtonStateRaw();
    if (bs != BTN_CENTER) {
      // released early -> hide prompt and return
      promptVisible = false;
      hidePromptFull();
      lastInteraction = millis();
      return;
    }
    vTaskDelay(20);
  }

  // held for required duration -> exit to files
  promptVisible = false;
  exitToFiles();
}

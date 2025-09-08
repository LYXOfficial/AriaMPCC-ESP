#include "encoding.h"
#include <SD.h>
#include <Preferences.h>

static bool isValidUtf8(const uint8_t *b, size_t n) {
  size_t i = 0;
  while (i < n) {
    uint8_t c = b[i];
    if (c <= 0x7F) { i++; continue; }
    int len = 0;
    if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    else return false;
    if (i + len > n) return false;
    for (int k = 1; k < len; ++k) if ((b[i+k] & 0xC0) != 0x80) return false;
    i += len;
  }
  return true;
}

static bool looksLikeGb2312(const uint8_t *b, size_t n) {
  size_t i = 0, pairs = 0, bad = 0;
  while (i < n) {
    uint8_t c = b[i];
    if (c <= 0x7F) { i++; continue; }
    if (i + 1 >= n) { bad++; break; }
    uint8_t d = b[i+1];
    // GB2312 lead: 0xA1-0xF7; trail: 0xA1-0xFE
    if (c >= 0xA1 && c <= 0xF7 && d >= 0xA1 && d <= 0xFE) { pairs++; i += 2; }
    else { bad++; i++; }
  }
  if (pairs == 0) return false;
  return ((double)pairs / (pairs + bad)) > 0.6;
}

ETextEncoding detectEncodingFromBuffer(const uint8_t *buf, size_t len) {
  if (!buf || len == 0) return ETextEncoding::ENC_UNKNOWN;
  // BOM checks
  if (len >= 3 && buf[0]==0xEF && buf[1]==0xBB && buf[2]==0xBF) return ETextEncoding::ENC_UTF8_BOM;
  if (len >= 2 && buf[0]==0xFF && buf[1]==0xFE) return ETextEncoding::ENC_UTF16_LE;
  if (len >= 2 && buf[0]==0xFE && buf[1]==0xFF) return ETextEncoding::ENC_UTF16_BE;

  // UTF-16 heuristic: many zero bytes on even/odd positions
  size_t zeroEven=0, zeroOdd=0;
  for (size_t i=0;i+1<len;i+=2){ if (buf[i]==0) zeroEven++; if (buf[i+1]==0) zeroOdd++; }
  if (zeroEven + zeroOdd > 4 && (zeroEven > zeroOdd * 3 || zeroOdd > zeroEven * 3))
    return (zeroEven>zeroOdd) ? ETextEncoding::ENC_UTF16_BE : ETextEncoding::ENC_UTF16_LE;

  if (isValidUtf8(buf, len)) return ETextEncoding::ENC_UTF8;
  if (looksLikeGb2312(buf, len)) return ETextEncoding::ENC_GB2312;

  size_t high = 0;
  for (size_t i=0;i<len;i++) if (buf[i] >= 0x80) high++;
  if (high > len/10) return ETextEncoding::ENC_LATIN1;
  return ETextEncoding::ENC_BINARY;
}

ETextEncoding detectEncodingFromFile(const String &absPath, size_t maxRead) {
  File f = SD.open(absPath.c_str());
  if (!f) return ETextEncoding::ENC_UNKNOWN;
  size_t toRead = (size_t)f.size();
  if (toRead > maxRead) toRead = maxRead;
  uint8_t *buf = (uint8_t *)malloc(toRead);
  if (!buf) { f.close(); return ETextEncoding::ENC_UNKNOWN; }
  size_t got = f.read(buf, toRead);
  f.close();
  ETextEncoding r = detectEncodingFromBuffer(buf, got);
  free(buf);
  return r;
}

const char *ebookEncodingPrefKey() { return "ebook_encoding"; }

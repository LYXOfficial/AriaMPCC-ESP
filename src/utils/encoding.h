#pragma once
#include <Arduino.h>

enum class ETextEncoding { ENC_UNKNOWN = 0, ENC_UTF8, ENC_UTF8_BOM, ENC_UTF16_LE, ENC_UTF16_BE, ENC_GB2312, ENC_LATIN1, ENC_BINARY };

// Detect encoding from buffer (heuristic). Supply pointer to bytes and length.
ETextEncoding detectEncodingFromBuffer(const uint8_t *buf, size_t len);

// Convenience: detect from file by reading up to `maxRead` bytes (default 8KB)
ETextEncoding detectEncodingFromFile(const String &absPath, size_t maxRead = 8192);

// Preference key helpers
const char *ebookEncodingPrefKey();

#include "utils.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <U8g2_for_Adafruit_GFX.h>

// We need access to font metrics; declare provided elsewhere
extern U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

String fitToWidthSingleLine(const String &sIn, int maxWidth) {
  String s = sIn;
  const String ell = "...";
  int wAscii = u8g2Fonts.getUTF8Width(".");
  int wChinese = u8g2Fonts.getUTF8Width("测");
  int used = 0;
  int i = 0;
  int len = s.length();
  int lastPos = 0;
  while (i < len) {
    uint8_t c = s[i];
    int charBytes = 1;
    int charWidth = wAscii;
    if ((c & 0x80) != 0) {
      if ((c & 0xE0) == 0xC0)
        charBytes = 2;
      else if ((c & 0xF0) == 0xE0)
        charBytes = 3;
      else if ((c & 0xF8) == 0xF0)
        charBytes = 4;
      charWidth = wChinese;
    }
    if (used + charWidth > maxWidth)
      break;
    used += charWidth;
    lastPos = i + charBytes;
    i += charBytes;
  }
  if (lastPos >= len)
    return s;
  int ellWidth = 3 * wAscii;
  while (lastPos > 0 && used + ellWidth > maxWidth) {
    int j = lastPos - 1;
    while (j > 0 && (s[j] & 0xC0) == 0x80)
      j--;
    uint8_t cc = s[j];
    int cw = wAscii;
    if ((cc & 0x80) != 0)
      cw = wChinese;
    used -= cw;
    lastPos = j;
  }
  String out = s.substring(0, lastPos);
  out += ell;
  return out;
}

bool getLocationByIP(double &outLat, double &outLon, String &outCityEn) {
  HTTPClient http;
  http.begin("http://ip-api.com/json");
  int httpCode = http.GET();
  bool ok = false;
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    auto err = deserializeJson(doc, payload);
    if (!err) {
      if (doc.containsKey("lat") && doc.containsKey("lon")) {
        outLat = doc["lat"].as<double>();
        outLon = doc["lon"].as<double>();
        outCityEn = String((const char *)doc["city"]);
        ok = true;
      }
    }
  }
  http.end();
  return ok;
}

bool getWeatherForCity(const String &city, String &outWeather, String &outTempC) {
  // Use wttr.in simplified endpoint
  if (city.length() == 0)
    return false;
  String url = String("http://wttr.in/") + city + String("?format=j1");
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();
  DynamicJsonDocument doc(2048);
  auto err = deserializeJson(doc, payload);
  if (err)
    return false;
  // parse current_condition[0]
  if (!doc.containsKey("current_condition"))
    return false;
  JsonArray cc = doc["current_condition"].as<JsonArray>();
  if (cc.size() == 0)
    return false;
  JsonObject c0 = cc[0];
  outTempC = String((const char *)c0["temp_C"]);
  // weatherDesc is array of objects with value
  if (c0.containsKey("weatherDesc")) {
    JsonArray wd = c0["weatherDesc"].as<JsonArray>();
    if (wd.size() > 0) {
      JsonObject w0 = wd[0];
      outWeather = String((const char *)w0["value"]);
    }
  }
  return true;
}

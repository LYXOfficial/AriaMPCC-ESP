#include "utils.h"
#include "../defines/pinconf.h"
#include "../pages/page_manager.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <U8g2_for_Adafruit_GFX.h>

// font metrics provided by main
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
      if ((c & 0xE0) == 0xC0) charBytes = 2;
      else if ((c & 0xF0) == 0xE0) charBytes = 3;
      else if ((c & 0xF8) == 0xF0) charBytes = 4;
      charWidth = wChinese;
    }
    if (used + charWidth > maxWidth) break;
    used += charWidth;
    lastPos = i + charBytes;
    i += charBytes;
  }
  if (lastPos >= len) return s;
  int ellWidth = 3 * wAscii;
  while (lastPos > 0 && used + ellWidth > maxWidth) {
    int j = lastPos - 1;
    while (j > 0 && (s[j] & 0xC0) == 0x80) j--;
    uint8_t cc = s[j];
    int cw = ((cc & 0x80) != 0) ? wChinese : wAscii;
    used -= cw;
    lastPos = j;
  }
  String out = s.substring(0, lastPos);
  out += ell;
  return out;
}

// ---- Hitokoto (one-line quote) ----
String getHitokoto() {
  HTTPClient http;
  http.begin("https://v1.hitokoto.cn/?encode=text&max_length=15");
  int httpCode = http.GET();
  String hitokoto = "";
  if (httpCode == HTTP_CODE_OK) {
    hitokoto = http.getString();
    hitokoto.trim();
  } else {
    hitokoto = "QwQ";
  }
  http.end();
  return hitokoto;
}

// ---- Button raw read / debounce globals ----
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 100; // ms

int readButtonStateRaw() {
  int v = analogRead(BTN_ADC_PIN);
  if (v > 4000) return BTN_NONE;
  if (v >= 3000 && v <= 3300) return BTN_RIGHT;
  if (v >= 2000 && v <= 2500) return BTN_LEFT;
  if (v < 100) return BTN_CENTER;
  return BTN_NONE;
}

bool getLocationByIP(double &outLat, double &outLon, String &outCityEn) {
  HTTPClient http;
  http.begin("http://ip-api.com/json");
  int httpCode = http.GET();
  bool ok = false;
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(1536);
    if (!deserializeJson(doc, payload)) {
      if (doc.containsKey("lat") && doc.containsKey("lon")) {
        outLat = doc["lat"].as<double>();
        outLon = doc["lon"].as<double>();
        if (doc.containsKey("city")) outCityEn = String((const char*)doc["city"]);
        ok = true;
      }
    }
  }
  http.end();
  return ok;
}

String getCityByIP() {
  HTTPClient http;
  http.begin("http://my.ip.cn/json/");
  int httpCode = http.GET();
  String city = "";
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    if (!deserializeJson(doc, payload)) {
      if (doc.containsKey("data") && doc["data"].is<JsonObject>()) {
        JsonObject d = doc["data"];
        if (d.containsKey("city")) city = String((const char*)d["city"]);
      } else if (doc.containsKey("city")) {
        city = String((const char*)doc["city"]);
      }
    }
  }
  http.end();
  city.trim();
  return city;
}

static String urlEncode(const String &s) {
  String encoded = "";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += (char)c;
    } else {
      char buf[4];
      sprintf(buf, "%%%02X", c);
      encoded += String(buf);
    }
  }
  return encoded;
}

bool getWeatherForCity(const String &city, String &outWeather, String &outTempC) {
  if (city.length() == 0) return false;
  String encCity = urlEncode(city);
  String url = String("http://wttr.in/") + encCity + String("?format=j1&lang=zh-cn");
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) { http.end(); return false; }
  String payload = http.getString();
  http.end();
  DynamicJsonDocument doc(6 * 1024);
  if (deserializeJson(doc, payload)) return false;
  if (!doc.containsKey("current_condition")) return false;
  JsonArray cc = doc["current_condition"].as<JsonArray>();
  if (cc.size() == 0) return false;
  JsonObject c0 = cc[0];
  const char *t = c0["temp_C"] | "";
  const char *desc = "";
  if (c0.containsKey("weatherDesc")) {
    JsonArray wd = c0["weatherDesc"].as<JsonArray>();
    if (wd.size() > 0) desc = wd[0]["value"] | "";
  }
  outTempC = String(t);
  outWeather = String(desc);
  return outTempC.length() > 0;
}

// Map Open-Meteo weathercode to Chinese description
static const char* omDesc(int code) {
  switch (code) {
    case 0: return "晴";
    case 1: case 2: case 3: return "多云";
    case 45: case 48: return "雾";
    case 51: case 53: case 55: return "细雨";
    case 56: case 57: return "冻雨";
    case 61: case 63: case 65: return "小/中/大雨";
    case 66: case 67: return "冻雨";
    case 71: case 73: case 75: return "小/中/大雪";
    case 77: return "雪粒";
    case 80: case 81: case 82: return "阵雨";
    case 85: case 86: return "阵雪";
    case 95: return "雷阵雨";
    case 96: case 99: return "强雷阵雨伴冰雹";
    default: return "";
  }
}

bool getWeatherByCoordsOpenMeteo(double lat, double lon, String &outWeather, String &outTempC) {
  char url[256];
  // Use metric units and current weather; timezone auto works for China; language handled by mapping
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%f&longitude=%f&current=temperature_2m,weather_code&timezone=auto",
           lat, lon);
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) { http.end(); return false; }
  String payload = http.getString();
  http.end();
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, payload)) return false;
  if (!doc.containsKey("current")) return false;
  JsonObject cur = doc["current"].as<JsonObject>();
  if (!cur.containsKey("temperature_2m") || !cur.containsKey("weather_code")) return false;
  double t = cur["temperature_2m"].as<double>();
  int code = cur["weather_code"].as<int>();
  outTempC = String((int)round(t));
  outWeather = String(omDesc(code));
  return true;
}

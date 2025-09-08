#pragma once
#include <Arduino.h>

// Text fitting for single line with ellipsis
String fitToWidthSingleLine(const String &s, int maxWidth);

// IP-based location helpers
bool getLocationByIP(double &outLat, double &outLon, String &outCityEn);
String getCityByIP();

// Weather
// Returns true and fills outWeather (CN desc) and outTempC (integer as string)
bool getWeatherForCity(const String &city, String &outWeather,
                       String &outTempC);

// Open-Meteo: query by coordinates; returns Chinese description and temperature
// in Celsius (string)
bool getWeatherByCoordsOpenMeteo(double lat, double lon, String &outWeather,
                                 String &outTempC);

// Hitokoto (one-line quote)
String getHitokoto();

// Button/raw input helpers
int readButtonStateRaw();
extern unsigned long lastButtonPress;
extern const unsigned long debounceDelay;

// Encoding detection helpers
#include "encoding.h"


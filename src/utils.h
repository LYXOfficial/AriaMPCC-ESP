#pragma once
#include <Arduino.h>

// Text fitting for single line with ellipsis
String fitToWidthSingleLine(const String &s, int maxWidth);

// Weather/location helpers
bool getLocationByIP(double &outLat, double &outLon, String &outCityEn);
bool getWeatherForCity(const String &city, String &outWeather, String &outTempC);

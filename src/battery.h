#ifndef __BATTERY_H__
#define __BATTERY_H__

#include <Arduino.h>

class BatteryMonitor {
public:
  BatteryMonitor();
  void begin(int adcPin = -1, float vref = 3.3f, int adcMax = 4095, float dividerFactor = 1.5f);
  // call periodically (can be in UI render) to refresh reading
  void update();
  // instantaneous smoothed battery voltage in volts
  float voltage() const;
  // estimated percentage 0..100 (non-linear lookup)
  int percent() const;
  // optional calibration multiplier to correct systematic error (set absolute)
  void setCalibration(float cal);
  // force reinitialize internal state (useful for tests)
  void reset();
  // optional fixed offset (Volts) to compensate hardware drop
  void setOffset(float offset);

private:
  int _adcPin;
  float _vref;
  int _adcMax;
  float _dividerFactor; // multiply measured ADC voltage to battery voltage
  float _cal;
  float _offset;
  float _filteredV;
  uint8_t _initialized;
  // sampling params
  static const int SAMPLE_N = 7;
  // sliding window for optional slope computation
  static const int WIN_SZ = 6;
  float _winV[WIN_SZ];
  unsigned long _winT[WIN_SZ];
  int _winIdx;
};

extern BatteryMonitor gBattery;

#endif

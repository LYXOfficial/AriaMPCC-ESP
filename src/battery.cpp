
#include "battery.h"
#include "defines/pinconf.h"
#include <Arduino.h>

#include <algorithm>
#include <numeric>

BatteryMonitor gBattery;

BatteryMonitor::BatteryMonitor()
    : _adcPin(BAT_ADC_PIN), _vref(3.3f), _adcMax(4095), _dividerFactor(1.0f),
      _cal(1.0f), _offset(0.9f), _filteredV(0.0f), _initialized(0), _winIdx(0) {
  for (int i = 0; i < WIN_SZ; ++i) {
    _winV[i] = 0.0f;
    _winT[i] = 0;
  }
}

void BatteryMonitor::begin(int adcPin, float vref, int adcMax,
                           float dividerFactor) {
  // guard: allow configuration only before first init to avoid accidental
  // reconfig
  if (_initialized)
    return;
  if (adcPin != -1)
    _adcPin = adcPin;
  _vref = vref;
  _adcMax = adcMax;
  _dividerFactor = dividerFactor;
  analogReadResolution(12); // esp32-c3 default 12 bits
  pinMode(_adcPin, INPUT);
  // do not set _initialized here; set after first valid sample
}

void BatteryMonitor::reset() {
  _filteredV = 0.0f;
  _initialized = 0;
  _winIdx = 0;
  for (int i = 0; i < WIN_SZ; ++i) {
    _winV[i] = 0.0f;
    _winT[i] = 0;
  }
}

void BatteryMonitor::setCalibration(float cal) {
  if (cal <= 0.0f)
    return;
  _cal = cal; // set absolute, do not multiply repeatedly
}

void BatteryMonitor::setOffset(float offset) {
  _offset = offset;
}

static float readAnalogVoltageRaw(int pin, int adcMax, float vref) {
  if (pin < 0)
    return 0.0f;
  int raw = analogRead(pin);
  if (raw < 0)
    raw = 0;
  if (raw > adcMax)
    raw = adcMax;
  return ((float)raw / (float)adcMax) * vref;
}

void BatteryMonitor::update() {
  // require begin() to have set pin info
  if (_adcPin < 0)
    return;

  // perform multiple quick samples, drop min/max and average the rest
  const int N = SAMPLE_N;
  float samples[N];
  for (int i = 0; i < N; ++i) {
    float vadc = readAnalogVoltageRaw(_adcPin, _adcMax, _vref);
    samples[i] = vadc * _dividerFactor * _cal;
    delay(3); // small spacing to allow ADC capacitor to recharge
  }
  std::sort(samples, samples + N);
  float sum = 0.0f;
  for (int i = 1; i < N - 1; ++i)
    sum += samples[i];
  float vbatt = sum / (N - 2);
  // apply fixed hardware offset compensation (user-specified)
  vbatt += _offset;

  // update sliding window
  _winV[_winIdx] = vbatt;
  _winT[_winIdx] = millis();
  _winIdx = (_winIdx + 1) % WIN_SZ;

  // IIR smoothing: initialize on first valid read to avoid ramp-up
  const float alpha = 0.12f;
  if (!_initialized) {
    _filteredV = vbatt;
    _initialized = 1;
  } else {
    _filteredV = alpha * vbatt + (1.0f - alpha) * _filteredV;
  }
}

float BatteryMonitor::voltage() const { return _filteredV; }

int BatteryMonitor::percent() const {
  // non-linear lookup table (OCV -> SOC) 3.30V -> 0% , 4.20V ->100%
  static const float vin[] = {3.30f, 3.50f, 3.60f, 3.66f, 3.70f, 3.74f,
                              3.78f, 3.82f, 3.86f, 3.95f, 4.10f, 4.20f};
  static const int soc[] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 97, 100};
  const int N = sizeof(vin) / sizeof(vin[0]);
  float v = _filteredV;
  if (v <= vin[0])
    return 0;
  if (v >= vin[N - 1])
    return 100;
  for (int i = 0; i < N - 1; ++i) {
    if (v >= vin[i] && v <= vin[i + 1]) {
      float t = (v - vin[i]) / (vin[i + 1] - vin[i]);
      float val = soc[i] + t * (soc[i + 1] - soc[i]);
      int ival = (int)round(val);
      if (ival < 0)
        ival = 0;
      if (ival > 100)
        ival = 100;
      return ival;
    }
  }
  return 0;
}

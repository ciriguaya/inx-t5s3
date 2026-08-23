/**
 * @file BatteryMonitor.cpp
 * @brief T5S3 implementation of the Inx BatteryMonitor interface.
 *
 * The T5S3 has no battery ADC divider; it uses a BQ27220 fuel gauge instead.
 * The (adcPin, dividerMultiplier) constructor arguments are kept for interface
 * compatibility and ignored.
 */

#include "BatteryMonitor.h"

#include <BoardT5S3.h>

BatteryMonitor::BatteryMonitor(const uint8_t adcPin, const float dividerMultiplier)
    : _adcPin(adcPin), _dividerMultiplier(dividerMultiplier) {}

uint16_t BatteryMonitor::readPercentage() const {
  uint16_t soc = 0;
  if (BoardT5S3::readBatteryStateOfCharge(&soc)) {
    return soc <= 100 ? soc : 100;
  }
  return 0;
}

uint16_t BatteryMonitor::readMillivolts() const {
  BoardT5S3::BatteryState state;
  if (BoardT5S3::readBatteryState(&state)) {
    return state.batteryVoltageMv;
  }
  return 0;
}

uint16_t BatteryMonitor::readRawMillivolts() const { return readMillivolts(); }

double BatteryMonitor::readVolts() const { return readMillivolts() / 1000.0; }

uint16_t BatteryMonitor::percentageFromMillivolts(const uint16_t millivolts) {
  // Li-ion 4.2V curve approximation (fallback; the gauge normally reports SOC directly).
  if (millivolts >= 4100) return 100;
  if (millivolts >= 4000) return 90;
  if (millivolts >= 3900) return 75;
  if (millivolts >= 3800) return 55;
  if (millivolts >= 3700) return 35;
  if (millivolts >= 3600) return 20;
  if (millivolts >= 3500) return 10;
  if (millivolts >= 3300) return 5;
  return 0;
}

uint16_t BatteryMonitor::millivoltsFromRawAdc(const uint16_t adc_raw) { return adc_raw; }

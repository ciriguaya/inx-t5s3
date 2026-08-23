/**
 * @file HalGPIO.cpp
 * @brief T5S3 implementation of the Inx HalGPIO interface.
 *
 * - Boot button (GPIO0) -> BTN_POWER, PCA9535 side button -> BTN_BACK
 * - GT911 touch panel (exposed through the touch getters)
 * - BQ27220 fuel gauge for battery percentage
 * - PCF85063 RTC for date/time (enables Inx's clock features)
 */

#include <HalGPIO.h>
#include <Logging.h>
#include <esp_sleep.h>
#include <time.h>

#include <BoardT5S3.h>
#include <Wire.h>

namespace {

constexpr uint8_t kRtcAddress = T5S3_PCF85063_ADDR;  // 0x51
constexpr uint8_t kRtcStartReg = 0x04;               // PCF85063 seconds register
constexpr uint8_t kRtcRegisterCount = 7;
constexpr time_t kValidSystemTimeEpoch = 1704067200;  // 2024-01-01 00:00:00 UTC

constexpr uint64_t POWER_WAKE_MASK = 1ULL << T5S3_BOOT_BTN;
constexpr uint64_t TOUCH_WAKE_MASK = 1ULL << T5S3_TOUCH_INT;

uint8_t bcdToDec(const uint8_t bcd) { return static_cast<uint8_t>(((bcd >> 4) * 10) + (bcd & 0x0F)); }

uint8_t decToBcd(const uint8_t dec) { return static_cast<uint8_t>(((dec / 10) << 4) | (dec % 10)); }

bool isValidBcd(const uint8_t bcd, const uint8_t maxValue) {
  if ((bcd & 0x0F) > 9 || ((bcd >> 4) & 0x0F) > 9) {
    return false;
  }
  return bcdToDec(bcd) <= maxValue;
}

bool isLeapYear(const int year) {
  if ((year % 4) != 0) return false;
  if ((year % 100) != 0) return true;
  return (year % 400) == 0;
}

uint8_t daysInMonth(const int year, const uint8_t month) {
  static constexpr uint8_t kDaysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  if (month == 2 && isLeapYear(year)) return 29;
  return kDaysPerMonth[month - 1];
}

bool readRtcRegisters(uint8_t* data, const size_t len) {
  BoardT5S3::ScopedI2CLock lock;
  Wire.beginTransmission(kRtcAddress);
  Wire.write(kRtcStartReg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const uint8_t requested = static_cast<uint8_t>(len);
  if (Wire.requestFrom(kRtcAddress, requested) != requested) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    data[i] = Wire.read();
  }
  return true;
}

bool writeRtcRegisters(const uint8_t* data, const size_t len) {
  BoardT5S3::ScopedI2CLock lock;
  Wire.beginTransmission(kRtcAddress);
  Wire.write(kRtcStartReg);
  Wire.write(data, len);
  return Wire.endTransmission() == 0;
}

bool decodeRtcRegisters(const uint8_t* data, HalGPIO::DateTime& dateTime) {
  const uint8_t rawSeconds = data[0];
  const uint8_t rawMinutes = data[1] & 0x7F;
  const uint8_t rawHours = data[2] & 0x3F;
  const uint8_t rawDays = data[3] & 0x3F;
  const uint8_t rawWeekdays = data[4] & 0x07;
  const uint8_t rawMonths = data[5] & 0x1F;
  const uint8_t rawYears = data[6];

  if ((rawSeconds & 0x80) != 0) {
    return false;  // Clock integrity flag: time not set / lost power.
  }
  if (!isValidBcd(rawSeconds & 0x7F, 59) || !isValidBcd(rawMinutes, 59) || !isValidBcd(rawHours, 23) ||
      !isValidBcd(rawDays, 31) || !isValidBcd(rawMonths, 12) || !isValidBcd(rawYears, 99)) {
    return false;
  }

  dateTime.year = static_cast<uint16_t>(2000 + bcdToDec(rawYears));
  dateTime.month = bcdToDec(rawMonths);
  dateTime.day = bcdToDec(rawDays);
  dateTime.weekday = rawWeekdays;
  dateTime.hour = bcdToDec(rawHours);
  dateTime.minute = bcdToDec(rawMinutes);
  dateTime.second = bcdToDec(rawSeconds & 0x7F);

  if (dateTime.month == 0 || dateTime.month > 12 || dateTime.day == 0 ||
      dateTime.day > daysInMonth(dateTime.year, dateTime.month) || dateTime.weekday > 6) {
    return false;
  }
  return true;
}

bool encodeRtcRegisters(const HalGPIO::DateTime& dateTime, uint8_t* data, const size_t len) {
  if (data == nullptr || len < kRtcRegisterCount) {
    return false;
  }
  if (dateTime.year < 2000 || dateTime.year > 2099 || dateTime.month < 1 || dateTime.month > 12 || dateTime.day < 1 ||
      dateTime.day > daysInMonth(dateTime.year, dateTime.month) || dateTime.weekday > 6 || dateTime.hour > 23 ||
      dateTime.minute > 59 || dateTime.second > 59) {
    return false;
  }

  data[0] = decToBcd(dateTime.second);
  data[1] = decToBcd(dateTime.minute);
  data[2] = decToBcd(dateTime.hour);
  data[3] = decToBcd(dateTime.day);
  data[4] = static_cast<uint8_t>(dateTime.weekday & 0x07);
  data[5] = decToBcd(dateTime.month);
  data[6] = decToBcd(static_cast<uint8_t>(dateTime.year - 2000));
  return true;
}

}  // namespace

void HalGPIO::begin() {
  inputMgr.begin();
  update();
  LOG_INF("GPIO", "T5S3 input initialized (boot button, PCA button, GT911 touch)");
}

void HalGPIO::update() { inputMgr.update(); }

bool HalGPIO::isPressed(const uint8_t buttonIndex) const { return inputMgr.isPressed(buttonIndex); }

bool HalGPIO::wasPressed(const uint8_t buttonIndex) const { return inputMgr.wasPressed(buttonIndex); }

bool HalGPIO::wasAnyPressed() const { return inputMgr.wasAnyPressed(); }

bool HalGPIO::wasReleased(const uint8_t buttonIndex) const { return inputMgr.wasReleased(buttonIndex); }

bool HalGPIO::wasAnyReleased() const { return inputMgr.wasAnyReleased(); }

unsigned long HalGPIO::getHeldTime() const { return inputMgr.getHeldTime(); }

HalGPIO::MotionGesture HalGPIO::readMotionGesture(const uint8_t orientation, const uint8_t mode,
                                                  const uint8_t sensitivity) {
  (void)orientation;
  (void)mode;
  (void)sensitivity;
  return MotionGesture::None;  // No motion sensor on the T5S3.
}

void HalGPIO::setBacklightLevel(const uint8_t level) { BoardT5S3::setBacklightLevel(level); }

void HalGPIO::startDeepSleep() {
  while (isPressed(BTN_POWER)) {
    delay(50);
    update();
  }

  BoardT5S3::deinitForSleep();
  pinMode(T5S3_BOOT_BTN, INPUT_PULLUP);
  pinMode(T5S3_TOUCH_INT, INPUT_PULLUP);
  const uint64_t wakeMask = POWER_WAKE_MASK | TOUCH_WAKE_MASK;
  LOG_DBG("GPIO", "Entering deep sleep, wake on power button + touch");
#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_deep_sleep_enable_gpio_wakeup(wakeMask, ESP_GPIO_WAKEUP_GPIO_LOW);
#else
  esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
  esp_deep_sleep_start();
}

int HalGPIO::getBatteryPercentage() const {
  const unsigned long now = millis();
  if (batteryLastPollMs != 0 && (now - batteryLastPollMs) < BATTERY_POLL_MS) {
    return batteryCachedPercent;
  }

  uint16_t soc = 0;
  const bool ok = BoardT5S3::readBatteryStateOfCharge(&soc);
  batteryLastPollMs = now;
  if (ok && soc <= 100) {
    batteryCachedPercent = static_cast<int>(soc);
  }
  return batteryCachedPercent;
}

bool HalGPIO::isUsbConnected() const { return BoardT5S3::isUsbConnected(); }

bool HalGPIO::readDateTime(DateTime& outDateTime) const {
  uint8_t regs[kRtcRegisterCount] = {};
  if (!readRtcRegisters(regs, sizeof(regs))) {
    return false;
  }
  return decodeRtcRegisters(regs, outDateTime);
}

bool HalGPIO::writeDateTime(const DateTime& dateTime) const {
  uint8_t regs[kRtcRegisterCount] = {};
  if (!encodeRtcRegisters(dateTime, regs, sizeof(regs))) {
    return false;
  }
  return writeRtcRegisters(regs, sizeof(regs));
}

bool HalGPIO::syncRtcFromSystemTime() const {
  const time_t now = time(nullptr);
  if (now < kValidSystemTimeEpoch) {
    return false;
  }

  struct tm utcTime {};
  if (gmtime_r(&now, &utcTime) == nullptr) {
    return false;
  }

  DateTime dt;
  dt.year = static_cast<uint16_t>(utcTime.tm_year + 1900);
  dt.month = static_cast<uint8_t>(utcTime.tm_mon + 1);
  dt.day = static_cast<uint8_t>(utcTime.tm_mday);
  dt.weekday = static_cast<uint8_t>(utcTime.tm_wday);
  dt.hour = static_cast<uint8_t>(utcTime.tm_hour);
  dt.minute = static_cast<uint8_t>(utcTime.tm_min);
  dt.second = static_cast<uint8_t>(utcTime.tm_sec);
  return writeDateTime(dt);
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();
  const bool usbConnected = isUsbConnected();

  if (wakeupCause == ESP_SLEEP_WAKEUP_EXT1) {
    const uint64_t wakeStatus = esp_sleep_get_ext1_wakeup_status();
    if ((wakeStatus & POWER_WAKE_MASK) != 0) {
      return WakeupReason::PowerButton;
    }
    if ((wakeStatus & TOUCH_WAKE_MASK) != 0) {
      return WakeupReason::Touch;
    }
  }

  if (wakeupCause == ESP_SLEEP_WAKEUP_GPIO) {
    if (digitalRead(T5S3_BOOT_BTN) == LOW) {
      return WakeupReason::PowerButton;
    }
    if (digitalRead(T5S3_TOUCH_INT) == LOW) {
      return WakeupReason::Touch;
    }
    return WakeupReason::Other;
  }

  if (resetReason == ESP_RST_POWERON && !usbConnected) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && usbConnected) {
    return WakeupReason::AfterFlash;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && usbConnected) {
    return WakeupReason::AfterUSBPower;
  }
  return WakeupReason::Other;
}

bool HalGPIO::isTouchAvailable() const { return inputMgr.isTouchAvailable(); }

bool HalGPIO::hadTouchActivity() const { return inputMgr.hadTouchActivity(); }

bool HalGPIO::getTouchTap(TouchPoint& point) const {
  InputManager::TouchPoint raw;
  if (!inputMgr.getTouchTap(raw)) {
    return false;
  }
  point = {raw.x, raw.y};
  return true;
}

bool HalGPIO::getTouchHold(TouchPoint& point, unsigned long& heldMs) const {
  InputManager::TouchPoint raw;
  if (!inputMgr.getTouchHold(raw, heldMs)) {
    return false;
  }
  point = {raw.x, raw.y};
  return true;
}

bool HalGPIO::getTouchPosition(TouchPoint& point) const {
  InputManager::TouchPoint raw;
  if (!inputMgr.getTouchPosition(raw)) {
    return false;
  }
  point = {raw.x, raw.y};
  return true;
}

bool HalGPIO::getTouchSwipe(TouchPoint& start, TouchPoint& end) const {
  InputManager::TouchPoint rawStart, rawEnd;
  if (!inputMgr.getTouchSwipe(rawStart, rawEnd)) {
    return false;
  }
  start = {rawStart.x, rawStart.y};
  end = {rawEnd.x, rawEnd.y};
  return true;
}

bool HalGPIO::wasTouchHomeButtonPressed() const { return inputMgr.wasTouchHomeButtonPressed(); }

void HalGPIO::suppressTouchContact() { inputMgr.suppressTouchContact(); }

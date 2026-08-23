#pragma once

/**
 * @file HalGPIO.h
 * @brief Public interface and types for HalGPIO (T5S3 backend).
 *
 * The interface is identical to Inx's X4/X3 HalGPIO so the application code
 * compiles unchanged. Implementation drives the LilyGo T5S3: boot button
 * (power), PCA9535 side button, GT911 touch, BQ27220 battery and PCF85063 RTC.
 */

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <InputManager.h>

// E-paper pins (kept for interface compatibility; the T5S3 driver uses its own
// fixed pins through M5GFX — see pin.hpp).
#define EPD_SCLK 14
#define EPD_MOSI 13
#define EPD_CS 12
#define EPD_DC 9
#define EPD_RST 8
#define EPD_BUSY 6

#define SPI_MISO 21

#define BAT_GPIO0 0

#define UART0_RXD 20

#define X3_I2C_SDA 39
#define X3_I2C_SCL 40
#define X3_I2C_FREQ 400000

#define I2C_ADDR_BQ27220 0x55
#define BQ27220_SOC_REG 0x2C
#define BQ27220_CUR_REG 0x0C
#define BQ27220_VOLT_REG 0x08

#define I2C_ADDR_DS3231 0x68
#define DS3231_SEC_REG 0x00

#define I2C_ADDR_QMI8658 0x6B
#define I2C_ADDR_QMI8658_ALT 0x6A
#define QMI8658_WHO_AM_I_REG 0x00
#define QMI8658_WHO_AM_I_VALUE 0x05
#define QMI8658_CTRL1_REG 0x02
#define QMI8658_CTRL3_REG 0x04
#define QMI8658_CTRL7_REG 0x08
#define QMI8658_GYRO_X_L_REG 0x3B

class HalGPIO {
#if CROSSPOINT_EMULATED == 0
  InputManager inputMgr;
#endif

 public:
  enum class DeviceType : uint8_t { T5S3 };

  struct DateTime {
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t weekday = 0;
  };

 private:
  DeviceType deviceType = DeviceType::T5S3;
  mutable int batteryCachedPercent = 0;
  mutable unsigned long batteryLastPollMs = 0;

 public:
  static constexpr unsigned long BATTERY_POLL_MS = 1500;
  enum class MotionGesture : uint8_t { None, Previous, Next };

  HalGPIO() = default;

  bool deviceIsX3() const { return true; }   // Enables Inx's RTC/clock features (PCF85063 on T5S3).
  bool deviceIsX4() const { return false; }
  bool deviceIsT5S3() const { return true; }
  const char* getDeviceName() const { return "T5S3"; }

  void begin();

  void update();
  void injectOneShotPress(uint8_t buttonIndex) {
#if CROSSPOINT_EMULATED == 0
    inputMgr.injectOneShotPress(buttonIndex);
#else
    (void)buttonIndex;
#endif
  }
  bool isPressed(uint8_t buttonIndex) const;
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  MotionGesture readMotionGesture(uint8_t orientation, uint8_t mode, uint8_t sensitivity);

  void startDeepSleep();

  /** Front-light/backlight brightness 0-10 (0 = off), applied to the T5S3 PWM output. */
  void setBacklightLevel(uint8_t level);

  int getBatteryPercentage() const;

  bool isUsbConnected() const;

  bool readDateTime(DateTime& outDateTime) const;
  bool writeDateTime(const DateTime& dateTime) const;
  bool syncRtcFromSystemTime() const;

  enum class WakeupReason { PowerButton, Touch, AfterFlash, AfterUSBPower, Other };

  WakeupReason getWakeupReason() const;

  // ---- Touch (T5S3 GT911) ----
  struct TouchPoint {
    uint16_t x = 0;
    uint16_t y = 0;
  };

  bool isTouchAvailable() const;
  bool hadTouchActivity() const;
  bool getTouchTap(TouchPoint& point) const;
  bool getTouchHold(TouchPoint& point, unsigned long& heldMs) const;
  bool getTouchPosition(TouchPoint& point) const;
  bool getTouchSwipe(TouchPoint& start, TouchPoint& end) const;
  bool wasTouchHomeButtonPressed() const;

  /** Suppresses the tap synthesized when the currently held touch is released. */
  void suppressTouchContact();

  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
  static constexpr uint8_t BTN_PCA = 7;
};

extern HalGPIO gpio;

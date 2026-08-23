#pragma once

/**
 * @file InputManager.h
 * @brief Public interface and types for InputManager (T5S3 backend).
 *
 * Drop-in replacement for the open-x4-sdk InputManager used by Inx on the
 * Xteink X4/X3. Keeps the same public interface so Inx's lib/hal/HalGPIO
 * compiles unchanged, but reads the LilyGo T5S3 inputs instead:
 *  - BTN_POWER  -> boot button (GPIO0)
 *  - BTN_BACK   -> PCA9535 side button
 *  - the other logical buttons are synthesized from the GT911 touch panel
 *    by higher layers (HalGPIO/MappedInputManager) via injectOneShotPress.
 */

#include <Arduino.h>

class InputManager {
 public:
  InputManager();
  ~InputManager();
  void begin();

  /**
   * Updates the button states. Should be called regularly in the main loop.
   */
  void update();

  /** Queue a one-shot press for the next update() (e.g. touch-synthesized button). */
  void injectOneShotPress(uint8_t buttonIndex);

  /**
   * Returns true if the button was being held at the time of the last #update() call.
   */
  bool isPressed(uint8_t buttonIndex) const;

  /**
   * Returns true if the button went from unpressed to pressed between the last two #update() calls.
   */
  bool wasPressed(uint8_t buttonIndex) const;

  /**
   * Returns true if any button started being pressed between the last two #update() calls.
   */
  bool wasAnyPressed() const;

  /**
   * Returns true if the button went from pressed to unpressed between the last two #update() calls.
   */
  bool wasReleased(uint8_t buttonIndex) const;

  /**
   * Returns true if any button was released between the last two #update() calls.
   */
  bool wasAnyReleased() const;

  /**
   * Returns the time between any button starting to be depressed and all buttons being released.
   */
  unsigned long getHeldTime() const;

  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;

  // T5S3 boot button GPIO (wake source for deep sleep).
  static constexpr int POWER_BUTTON_PIN = 0;

  bool isPowerButtonPressed() const;

  static const char* getButtonName(uint8_t buttonIndex);

  // ---- Touch (T5S3 GT911) ----
  struct TouchPoint {
    uint16_t x = 0;
    uint16_t y = 0;
  };

  bool isTouchAvailable() const;
  /** One-shot tap (finger down then up without moving) since the last update(). */
  bool getTouchTap(TouchPoint& point) const;
  /** While a touch is active and unmoved, returns its position and how long it has been held. */
  bool getTouchHold(TouchPoint& point, unsigned long& heldMs) const;
  /** Current live touch position while a touch is active (regardless of movement). */
  bool getTouchPosition(TouchPoint& point) const;
  /** Reports the start/end of the most recent moved touch (swipe), one-shot per release. */
  bool getTouchSwipe(TouchPoint& start, TouchPoint& end) const;
  bool wasTouchHomeButtonPressed() const;
  bool hadTouchActivity() const;

  /**
   * Suppresses the tap that would otherwise be synthesized when the current
   * (still held) unmoved touch is released. Call this after a long-press was
   * consumed by an application handler so the finger lift does not double-fire
   * as a tap on the same spot (which would e.g. immediately dismiss a popup the
   * long-press just opened).
   */
  void suppressTouchContact();

 private:
  uint8_t currentState = 0;
  uint8_t lastState = 0;
  uint8_t pressedEvents = 0;
  uint8_t releasedEvents = 0;
  uint8_t pendingInjectPress = 0;
  bool pendingInjectActive = false;
  unsigned long lastDebounceTime = 0;
  unsigned long buttonPressStart = 0;
  unsigned long buttonPressFinish = 0;

  bool touchActive = false;
  uint16_t touchStartX = 0;
  uint16_t touchStartY = 0;
  unsigned long touchStartTime = 0;
  TouchPoint currentTouchPoint;
  unsigned long lastTouchSeenTime = 0;
  bool touchMoved = false;
  bool touchTapEvent = false;
  TouchPoint touchTapPoint;
  bool touchSwipeEvent = false;
  TouchPoint touchSwipeStart;
  TouchPoint touchSwipeEnd;
  bool touchHomeButtonEvent = false;
  bool touchHomeButtonHeld = false;
  unsigned long lastTouchHomeButtonEventTime = 0;
  /** Set by suppressTouchContact(): drop the release-synthesized tap/swipe for this contact. */
  bool suppressTouchTap_ = false;

  uint8_t getState();
  void readTouchState();

  class TouchBackend;
  TouchBackend* touchBackend = nullptr;

  static constexpr unsigned long DEBOUNCE_DELAY = 5;
  static constexpr uint16_t TOUCH_SWIPE_THRESHOLD = 25;
  static constexpr unsigned long TOUCH_RELEASE_GRACE_MS = 300;
  static constexpr unsigned long TOUCH_HOME_BUTTON_DEBOUNCE_MS = 40;
};

/**
 * @file InputManager.cpp
 * @brief T5S3 implementation of the Inx InputManager interface.
 *
 * Reads the two physical T5S3 buttons (boot button GPIO0, PCA9535 side
 * button) and the GT911 capacitive touch panel. The logical CrossPoint-style
 * buttons (Confirm/Left/Right/Up/Down) are never set by physical hardware;
 * higher layers synthesize them from touch via injectOneShotPress().
 */

#include "InputManager.h"

#include <BoardT5S3.h>
#include <Logging.h>

namespace {

constexpr uint8_t kBootButtonPin = 0;  // T5S3_BOOT_BTN (GPIO0)

uint8_t buttonBit(uint8_t button) { return static_cast<uint8_t>(1U << button); }

}  // namespace

class InputManager::TouchBackend {
 public:
  BoardT5S3::GT911Touch touch;
};

InputManager::InputManager() = default;

InputManager::~InputManager() { delete touchBackend; }

void InputManager::begin() {
  pinMode(kBootButtonPin, INPUT_PULLUP);
  BoardT5S3::begin();
  touchBackend = new TouchBackend();
  if (!touchBackend->touch.begin()) {
    LOG_INF("INP", "GT911 touch not detected");
  }
  update();
}

void InputManager::readTouchState() {
  BoardT5S3::TouchPoint point;
  bool touchHomeButtonPressed = false;
  const bool havePoint = touchBackend && touchBackend->touch.readPoint(&point, &touchHomeButtonPressed);

  // Edge-detect the touch home button: emit a single event on each press.
  if (touchHomeButtonPressed) {
    if (!touchHomeButtonHeld && millis() - lastTouchHomeButtonEventTime >= TOUCH_HOME_BUTTON_DEBOUNCE_MS) {
      touchHomeButtonEvent = true;
      lastTouchHomeButtonEventTime = millis();
    }
    touchHomeButtonHeld = true;
  } else {
    touchHomeButtonHeld = false;
  }

  if (!havePoint) {
    if (touchActive && millis() - lastTouchSeenTime > TOUCH_RELEASE_GRACE_MS) {
      if (!touchMoved) {
        // The release-tap after a consumed long-press is suppressed so it cannot dismiss e.g. the
        // delete-highlight popup the press just opened.
        if (!suppressTouchTap_) {
          touchTapPoint = {touchStartX, touchStartY};
          touchTapEvent = true;
        }
      } else {
        // A moved release is a drag-end, never a tap: always report it, even after a long-press
        // consumed the hold (text-selection dragging relies on this swipe to commit the range).
        touchSwipeStart = {touchStartX, touchStartY};
        touchSwipeEnd = currentTouchPoint;
        touchSwipeEvent = true;
      }
      suppressTouchTap_ = false;
      touchActive = false;
    }
    return;
  }

  lastTouchSeenTime = millis();

  if (!touchActive) {
    touchActive = true;
    suppressTouchTap_ = false;
    touchStartX = point.x;
    touchStartY = point.y;
    currentTouchPoint = {point.x, point.y};
    touchStartTime = millis();
    touchMoved = false;
  } else {
    currentTouchPoint = {point.x, point.y};
    const int dx = static_cast<int>(point.x) - static_cast<int>(touchStartX);
    const int dy = static_cast<int>(point.y) - static_cast<int>(touchStartY);
    if (abs(dx) >= TOUCH_SWIPE_THRESHOLD || abs(dy) >= TOUCH_SWIPE_THRESHOLD) {
      touchMoved = true;
    }
  }
}

uint8_t InputManager::getState() {
  uint8_t state = 0;

  if (BoardT5S3::readButton()) {
    state |= buttonBit(BTN_BACK);
  }
  if (digitalRead(kBootButtonPin) == LOW) {
    state |= buttonBit(BTN_POWER);
  }

  readTouchState();
  return state;
}

void InputManager::update() {
  const unsigned long currentTime = millis();
  touchTapEvent = false;
  touchSwipeEvent = false;
  touchHomeButtonEvent = false;
  const uint8_t state = getState();

  pressedEvents = 0;
  releasedEvents = 0;

  if (pendingInjectActive) {
    pressedEvents |= pendingInjectPress;
    releasedEvents |= pendingInjectPress;
    pendingInjectActive = false;
    pendingInjectPress = 0;
  }

  if (state != lastState) {
    lastDebounceTime = currentTime;
    lastState = state;
  }

  if ((currentTime - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (state != currentState) {
      pressedEvents |= state & ~currentState;
      releasedEvents |= currentState & ~state;

      if (pressedEvents > 0 && currentState == 0) {
        buttonPressStart = currentTime;
      }
      if (releasedEvents > 0 && state == 0) {
        buttonPressFinish = currentTime;
      }

      currentState = state;
    }
  }
}

bool InputManager::isPressed(const uint8_t buttonIndex) const { return currentState & buttonBit(buttonIndex); }

bool InputManager::wasPressed(const uint8_t buttonIndex) const { return pressedEvents & buttonBit(buttonIndex); }

bool InputManager::wasAnyPressed() const { return pressedEvents > 0; }

bool InputManager::wasReleased(const uint8_t buttonIndex) const { return releasedEvents & buttonBit(buttonIndex); }

bool InputManager::wasAnyReleased() const { return releasedEvents > 0; }

unsigned long InputManager::getHeldTime() const {
  if (currentState > 0) {
    return millis() - buttonPressStart;
  }
  return buttonPressFinish - buttonPressStart;
}

void InputManager::injectOneShotPress(const uint8_t buttonIndex) {
  pendingInjectPress = buttonBit(buttonIndex);
  pendingInjectActive = true;
}

void InputManager::suppressTouchContact() {
  if (touchActive) {
    suppressTouchTap_ = true;
  }
}

bool InputManager::isPowerButtonPressed() const { return isPressed(BTN_POWER); }

const char* InputManager::getButtonName(const uint8_t buttonIndex) {
  switch (buttonIndex) {
    case BTN_BACK:
      return "Back";
    case BTN_CONFIRM:
      return "Confirm";
    case BTN_LEFT:
      return "Left";
    case BTN_RIGHT:
      return "Right";
    case BTN_UP:
      return "Up";
    case BTN_DOWN:
      return "Down";
    case BTN_POWER:
      return "Power";
    default:
      return "Unknown";
  }
}

bool InputManager::isTouchAvailable() const { return touchBackend && touchBackend->touch.isAvailable(); }

bool InputManager::getTouchTap(TouchPoint& point) const {
  if (!touchTapEvent) {
    return false;
  }
  point = touchTapPoint;
  return true;
}

bool InputManager::getTouchHold(TouchPoint& point, unsigned long& heldMs) const {
  if (!touchActive || touchMoved) {
    return false;
  }
  point = currentTouchPoint;
  heldMs = millis() - touchStartTime;
  return true;
}

bool InputManager::getTouchPosition(TouchPoint& point) const {
  if (!touchActive) {
    return false;
  }
  point = currentTouchPoint;
  return true;
}

bool InputManager::getTouchSwipe(TouchPoint& start, TouchPoint& end) const {
  if (!touchSwipeEvent) {
    return false;
  }
  start = touchSwipeStart;
  end = touchSwipeEnd;
  return true;
}

bool InputManager::wasTouchHomeButtonPressed() const { return touchHomeButtonEvent; }

bool InputManager::hadTouchActivity() const { return touchActive || touchTapEvent || touchHomeButtonEvent; }

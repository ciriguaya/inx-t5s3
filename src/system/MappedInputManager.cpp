/**
 * @file MappedInputManager.cpp
 * @brief Definitions for MappedInputManager.
 */

#include "system/MappedInputManager.h"

#include <GfxRenderer.h>
#include <utility>

#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"

namespace {
using ButtonIndex = uint8_t;

struct FrontLayoutMap {
  ButtonIndex back;
  ButtonIndex confirm;
  ButtonIndex left;
  ButtonIndex right;
};

struct SideLayoutMap {
  ButtonIndex pageBack;
  ButtonIndex pageForward;
};

constexpr FrontLayoutMap kFrontLayouts[] = {
    {HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM, HalGPIO::BTN_LEFT, HalGPIO::BTN_RIGHT},
    {HalGPIO::BTN_LEFT, HalGPIO::BTN_RIGHT, HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM},
    {HalGPIO::BTN_CONFIRM, HalGPIO::BTN_LEFT, HalGPIO::BTN_BACK, HalGPIO::BTN_RIGHT},
    {HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM, HalGPIO::BTN_RIGHT, HalGPIO::BTN_LEFT},
    {HalGPIO::BTN_RIGHT, HalGPIO::BTN_LEFT, HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM},
};

constexpr SideLayoutMap kSideLayouts[] = {
    {HalGPIO::BTN_UP, HalGPIO::BTN_DOWN},
    {HalGPIO::BTN_DOWN, HalGPIO::BTN_UP},
};

MappedInputManager::Button remapDirectional180(const MappedInputManager::Button button) {
  switch (button) {
    case MappedInputManager::Button::Up:
      return MappedInputManager::Button::Down;
    case MappedInputManager::Button::Down:
      return MappedInputManager::Button::Up;
    case MappedInputManager::Button::Left:
      return MappedInputManager::Button::Right;
    case MappedInputManager::Button::Right:
      return MappedInputManager::Button::Left;
    case MappedInputManager::Button::PageBack:
      return MappedInputManager::Button::PageForward;
    case MappedInputManager::Button::PageForward:
      return MappedInputManager::Button::PageBack;
    default:
      return button;
  }
}

MappedInputManager::TouchPoint orientTouchPoint(const uint16_t rawX, const uint16_t rawY, const GfxRenderer& renderer) {
  MappedInputManager::TouchPoint point{};
#ifndef SIMULATOR
  switch (renderer.getOrientation()) {
    case GfxRenderer::Orientation::Portrait:
      point = {static_cast<int16_t>(rawX), static_cast<int16_t>(rawY)};
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      point = {static_cast<int16_t>(renderer.getScreenWidth() - 1 - rawY), static_cast<int16_t>(rawX)};
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      point = {static_cast<int16_t>(renderer.getScreenWidth() - 1 - rawX),
               static_cast<int16_t>(renderer.getScreenHeight() - 1 - rawY)};
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      point = {static_cast<int16_t>(rawY), static_cast<int16_t>(renderer.getScreenHeight() - 1 - rawX)};
      break;
  }

  if (point.x < 0) point.x = 0;
  if (point.y < 0) point.y = 0;
  if (point.x >= renderer.getScreenWidth()) point.x = static_cast<int16_t>(renderer.getScreenWidth() - 1);
  if (point.y >= renderer.getScreenHeight()) point.y = static_cast<int16_t>(renderer.getScreenHeight() - 1);
#endif
  return point;
}

#ifdef SIMULATOR
/**
 * Converts the simulator's panel-normalized touch coordinates back to logical
 * (GfxRenderer) pixels. The crosspoint-simulator HalGPIO reports touches in
 * physical panel space (see its logicalToPanelNormalized()); inverting that
 * transform yields the same coordinates the on-device GT911 driver provides
 * after orientTouchPoint().
 */
MappedInputManager::TouchPoint simPanelNormalizedToLogical(const float nx, const float ny,
                                                           const GfxRenderer& renderer) {
  const int lw = renderer.getScreenWidth();
  const int lh = renderer.getScreenHeight();
  int lx = 0;
  int ly = 0;
  switch (renderer.getOrientation()) {
    case GfxRenderer::Orientation::Portrait:
      lx = static_cast<int>((1.0f - ny) * (lw - 1));
      ly = static_cast<int>(nx * (lh - 1));
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      lx = static_cast<int>(ny * (lw - 1));
      ly = static_cast<int>((1.0f - nx) * (lh - 1));
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      lx = static_cast<int>((1.0f - nx) * (lw - 1));
      ly = static_cast<int>((1.0f - ny) * (lh - 1));
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
    default:
      lx = static_cast<int>(nx * (lw - 1));
      ly = static_cast<int>(ny * (lh - 1));
      break;
  }
  return {static_cast<int16_t>(lx), static_cast<int16_t>(ly)};
}
#endif
}  // namespace

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const) const {
  const auto frontLayout = static_cast<SystemSetting::FRONT_BUTTON_LAYOUT>(SETTINGS.frontButtonLayout);
  const auto sideLayout = static_cast<SystemSetting::SIDE_BUTTON_LAYOUT>(SETTINGS.sideButtonLayout);
  const auto& front = kFrontLayouts[frontLayout];
  const auto& side = kSideLayouts[sideLayout];

  const Button effective = invertDirectionalAxes180_ ? remapDirectional180(button) : button;

  switch (effective) {
    case Button::Back:
      return (gpio.*fn)(front.back);
    case Button::Confirm:
      return (gpio.*fn)(front.confirm);
    case Button::Left:
      return (gpio.*fn)(front.left);
    case Button::Right:
      return (gpio.*fn)(front.right);
    case Button::Up:
      return (gpio.*fn)(HalGPIO::BTN_UP);
    case Button::Down:
      return (gpio.*fn)(HalGPIO::BTN_DOWN);
    case Button::Power:
      return (gpio.*fn)(HalGPIO::BTN_POWER);
    case Button::PageBack:
      return (gpio.*fn)(side.pageBack);
    case Button::PageForward:
      return (gpio.*fn)(side.pageForward);
  }

  return false;
}

bool MappedInputManager::wasPressed(const Button button) const {
  return mapButton(button, &HalGPIO::wasPressed) || (hasInjectedButtonTap_ && injectedButtonTap_ == button);
}

bool MappedInputManager::wasReleased(const Button button) const {
  return mapButton(button, &HalGPIO::wasReleased) || (hasInjectedButtonTap_ && injectedButtonTap_ == button);
}

bool MappedInputManager::isPressed(const Button button) const {
  if (hasInjectedButtonTap_ && injectedButtonTap_ == button) {
    return false;
  }
  return mapButton(button, &HalGPIO::isPressed);
}

bool MappedInputManager::wasAnyPressed() const { return gpio.wasAnyPressed() || hasInjectedButtonTap_; }

bool MappedInputManager::wasAnyReleased() const { return gpio.wasAnyReleased() || hasInjectedButtonTap_; }

unsigned long MappedInputManager::getHeldTime() const { return hasInjectedButtonTap_ ? 0 : gpio.getHeldTime(); }

MappedInputManager::MotionGesture MappedInputManager::readMotionGesture(const uint8_t orientation, const uint8_t mode,
                                                                        const uint8_t sensitivity) const {
#ifdef SIMULATOR
  return MotionGesture::None;
#else
  switch (gpio.readMotionGesture(orientation, mode, sensitivity)) {
    case HalGPIO::MotionGesture::Previous:
      return MotionGesture::Previous;
    case HalGPIO::MotionGesture::Next:
      return MotionGesture::Next;
    case HalGPIO::MotionGesture::None:
    default:
      return MotionGesture::None;
  }
#endif
}

bool MappedInputManager::rawHalIsPressed(const uint8_t halButtonIndex) const { return gpio.isPressed(halButtonIndex); }

MappedInputManager::SideLabels MappedInputManager::mapSideLabels() const {
  const auto sideLayout = static_cast<SystemSetting::SIDE_BUTTON_LAYOUT>(SETTINGS.sideButtonLayout);

  static constexpr const char* kPrev = "\xC2\xAB";
  static constexpr const char* kNext = "\xC2\xBB";
  if (sideLayout == SystemSetting::NEXT_PREV) {
    return {kNext, kPrev};
  }
  return {kPrev, kNext};
}

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  const char* p = previous;
  const char* n = next;
  if (invertDirectionalAxes180_) {
    std::swap(p, n);
  }

  const auto layout = static_cast<SystemSetting::FRONT_BUTTON_LAYOUT>(SETTINGS.frontButtonLayout);

  switch (layout) {
    case SystemSetting::LEFT_RIGHT_BACK_CONFIRM:
      return {p, n, back, confirm};
    case SystemSetting::LEFT_BACK_CONFIRM_RIGHT:
      return {p, back, confirm, n};
    case SystemSetting::BACK_CONFIRM_RIGHT_LEFT:
      return {back, confirm, n, p};
    case SystemSetting::LEFT_RIGHT_CONFIRM_BACK:
      return {p, n, confirm, back};
    case SystemSetting::BACK_CONFIRM_LEFT_RIGHT:
    default:
      return {back, confirm, p, n};
  }
}

MappedInputManager::Labels MappedInputManager::mapLabelsWithReaderNav(const char* back, const char* confirm,
                                                                      const char* prevSym, const char* nextSym,
                                                                      bool landscapeDrawer) const {
  const char* p = prevSym;
  const char* n = nextSym;

  if (landscapeDrawer) {
    std::swap(p, n);
  } else {
    using RM = SystemSetting::READER_DIRECTION_MAPPING;
    switch (static_cast<RM>(READER_SETTINGS.readerDirectionMapping)) {
      case RM::MAP_RIGHT_LEFT:
        std::swap(p, n);
        break;
      case RM::MAP_UP_DOWN:
        p = "Up";
        n = "Down";
        break;
      case RM::MAP_DOWN_UP:
        p = "Down";
        n = "Up";
        break;
      case RM::MAP_LEFT_RIGHT:
      case RM::MAP_NONE:
      default:
        break;
    }
  }

  return mapLabels(back, confirm, p, n);
}

bool MappedInputManager::wasTouchTapped(TouchPoint& point, const GfxRenderer& renderer) const {
#ifdef SIMULATOR
  float nx = 0.0f;
  float ny = 0.0f;
  if (!gpio.wasTouchTap(nx, ny)) {
    return false;
  }
  point = simPanelNormalizedToLogical(nx, ny, renderer);
  return true;
#else
  HalGPIO::TouchPoint raw;
  if (!gpio.getTouchTap(raw)) {
    return false;
  }
  point = orientTouchPoint(raw.x, raw.y, renderer);
  return true;
#endif
}

bool MappedInputManager::getTouchHold(TouchPoint& point, unsigned long& heldMs, const GfxRenderer& renderer) const {
#ifdef SIMULATOR
  float nx = 0.0f;
  float ny = 0.0f;
  if (!gpio.isTouchTapCandidate(nx, ny, heldMs)) {
    return false;
  }
  point = simPanelNormalizedToLogical(nx, ny, renderer);
  return true;
#else
  HalGPIO::TouchPoint raw;
  if (!gpio.getTouchHold(raw, heldMs)) {
    return false;
  }
  point = orientTouchPoint(raw.x, raw.y, renderer);
  return true;
#endif
}

bool MappedInputManager::getTouchPosition(TouchPoint& point, const GfxRenderer& renderer) const {
#ifdef SIMULATOR
  float nx = 0.0f;
  float ny = 0.0f;
  if (!gpio.isTouchHeldAt(nx, ny)) {
    return false;
  }
  point = simPanelNormalizedToLogical(nx, ny, renderer);
  return true;
#else
  HalGPIO::TouchPoint raw;
  if (!gpio.getTouchPosition(raw)) {
    return false;
  }
  point = orientTouchPoint(raw.x, raw.y, renderer);
  return true;
#endif
}

bool MappedInputManager::getTouchSwipe(TouchPoint& start, TouchPoint& end, const GfxRenderer& renderer) const {
#ifdef SIMULATOR
  float nxStart = 0.0f, nyStart = 0.0f, nxEnd = 0.0f, nyEnd = 0.0f;
  if (!gpio.wasSwipe(nxStart, nyStart, nxEnd, nyEnd)) {
    return false;
  }
  start = simPanelNormalizedToLogical(nxStart, nyStart, renderer);
  end = simPanelNormalizedToLogical(nxEnd, nyEnd, renderer);
  return true;
#else
  HalGPIO::TouchPoint rawStart, rawEnd;
  if (!gpio.getTouchSwipe(rawStart, rawEnd)) {
    return false;
  }
  start = orientTouchPoint(rawStart.x, rawStart.y, renderer);
  end = orientTouchPoint(rawEnd.x, rawEnd.y, renderer);
  return true;
#endif
}

bool MappedInputManager::wasTouchHomeButtonPressed() const {
#ifdef SIMULATOR
  return gpio.wasHomeKeyTapped();
#else
  return gpio.wasTouchHomeButtonPressed();
#endif
}

void MappedInputManager::injectButtonTap(const Button button) {
  injectedButtonTap_ = button;
  hasInjectedButtonTap_ = true;
}

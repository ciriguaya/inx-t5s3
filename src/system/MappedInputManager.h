#pragma once

/**
 * @file MappedInputManager.h
 * @brief Public interface and types for MappedInputManager.
 */

#include <HalGPIO.h>

class GfxRenderer;

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };
  enum class MotionGesture : uint8_t { None, Previous, Next };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  /** Labels for the physical page (side) buttons, top then bottom, per Side Button Layout setting. */
  struct SideLabels {
    const char* top;
    const char* bottom;
  };

  /** Oriented (flip-corrected) logical touch coordinates, matching GfxRenderer screen space. */
  struct TouchPoint {
    int16_t x = 0;
    int16_t y = 0;
  };

  explicit MappedInputManager(HalGPIO& gpio) : gpio(gpio) {}

  /**
   * When true, Up/Down, Left/Right, and PageBack/PageForward are swapped before GPIO lookup.
   * Use with GfxRenderer::LandscapeClockwise (180° vs panel) so physical directions match the
   * rotated framebuffer; clear when leaving that mode or the reader.
   */
  void setInvertDirectionalAxes180(bool invert) { invertDirectionalAxes180_ = invert; }
  bool invertDirectionalAxes180() const { return invertDirectionalAxes180_; }

  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  MotionGesture readMotionGesture(uint8_t orientation, uint8_t mode, uint8_t sensitivity) const;
  unsigned long getHeldTime() const;

  /** Raw GPIO read (layout + invert still apply to HalGPIO indices). For fixed chords use HalGPIO::BTN_* ). */
  bool rawHalIsPressed(uint8_t halButtonIndex) const;

  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;

  /**
   * Like mapLabels, but Left/Right slot text follows Settings → Next & Previous Mapping and drawer
   * orientation (portrait vs landscape list uses different prev/next buttons). Used for TOC lists.
   */
  Labels mapLabelsWithReaderNav(const char* back, const char* confirm, const char* prevSym, const char* nextSym,
                                bool landscapeDrawer) const;

  /** « / » order follows which GPIO is wired as page-back vs page-forward (see Side Button Layout). */
  SideLabels mapSideLabels() const;

  // ---- Touch (T5S3 GT911; synthesized onto the logical button model) ----
  /** One-shot tap in oriented logical coordinates since the last update(). */
  bool wasTouchTapped(TouchPoint& point, const GfxRenderer& renderer) const;
  /** Live unmoved touch position + held duration (long-press detection). */
  bool getTouchHold(TouchPoint& point, unsigned long& heldMs, const GfxRenderer& renderer) const;
  /** Current live touch position regardless of movement. */
  bool getTouchPosition(TouchPoint& point, const GfxRenderer& renderer) const;
  /** Start/end of the most recent moved touch, in oriented logical coordinates. */
  bool getTouchSwipe(TouchPoint& start, TouchPoint& end, const GfxRenderer& renderer) const;
  bool wasTouchHomeButtonPressed() const;
  /** Queue a synthetic button press+release consumed by the next wasPressed()/wasReleased() call. */
  void injectButtonTap(Button button);
  void clearInjectedButtonTap() { hasInjectedButtonTap_ = false; }
  bool hasInjectedButtonTap() const { return hasInjectedButtonTap_; }

  /**
   * After a long-press was consumed by an activity, prevents the finger-lift from
   * being synthesized as a tap on the same spot (which would otherwise dismiss
   * popups the long-press just opened, e.g. the in-book delete-highlight popup).
   */
  void suppressCurrentTouch() { gpio.suppressTouchContact(); }

 private:
  HalGPIO& gpio;
  bool invertDirectionalAxes180_ = false;

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;

  bool hasInjectedButtonTap_ = false;
  Button injectedButtonTap_ = Button::Back;
};

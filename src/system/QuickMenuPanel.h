#pragma once

/**
 * @file QuickMenuPanel.h
 * @brief Global quick-settings overlay (backlight, night mode, sleep).
 *
 * A modal, Inx-styled panel drawn over the current screen (the underlying
 * framebuffer is snapshotted first and restored on close). Opened with a
 * swipe-down gesture from the top edge of the screen (see main.cpp).
 */

#include <GfxRenderer.h>

#include <functional>

#include "system/MappedInputManager.h"

class QuickMenuPanel {
 public:
  explicit QuickMenuPanel(std::function<void()> onSleep) : onSleep_(std::move(onSleep)) {}

  /** Opens the overlay over the current screen (idempotent). */
  void open(GfxRenderer& renderer);
  /** Closes the overlay, restoring the captured screen. Returns true if the underlying activity must
   *  re-render afterwards (night mode changed while the menu was open - the restored frame has the old
   *  polarity and would otherwise stay on screen until the next interaction). */
  bool close(GfxRenderer& renderer);
  bool isActive() const { return active_; }

  /** Routes a tap (logical coords) to the panel. Returns true when consumed. */
  bool handleTouchTap(GfxRenderer& renderer, int16_t x, int16_t y);

  /** True after close() when a night-mode change requires the caller to re-render the activity. */
  bool needsRedrawAfterClose() const { return needsRedrawAfterClose_; }

 private:
  std::function<void()> onSleep_;
  bool active_ = false;
  bool stored_ = false;
  bool needsRedrawAfterClose_ = false;
  int backlight_ = 0;
  int panelX_ = 0;
  int panelY_ = 0;
  int panelW_ = 0;
  int panelH_ = 0;
  /** Hit rects of the backlight row's "-" / "+" buttons (set in render()). */
  int backlightMinusX_ = 0;
  int backlightPlusX_ = 0;
  constexpr static int kStepBtnW = 56;

  int rowCount() const { return 3; }  // Backlight, Night mode, Sleep
  int rowY(int row) const;
  void render(GfxRenderer& renderer);
  void applyBacklight(GfxRenderer& renderer, int delta);
  void toggleNightMode(GfxRenderer& renderer);
};

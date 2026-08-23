#include "system/QuickMenuPanel.h"

#include "state/SystemSetting.h"
#include "system/Fonts.h"

#include <algorithm>

namespace {
constexpr int kTitleHeight = 52;
constexpr int kRowHeight = 64;
constexpr int kPanelWidth = 340;
constexpr int kPanelPad = 12;
constexpr int kTitleFont = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
constexpr int kRowFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
/** Fixed width reserved for the backlight value between the two step buttons, so the buttons never
 *  shift horizontally when the value changes between 1 and 10 (single vs double digit). */
constexpr int kValueSlotW = 44;
}  // namespace

int QuickMenuPanel::rowY(const int row) const { return panelY_ + kTitleHeight + row * kRowHeight; }

void QuickMenuPanel::open(GfxRenderer& renderer) {
  if (active_) {
    return;
  }
  active_ = true;
  needsRedrawAfterClose_ = false;
  backlight_ = SETTINGS.backlightLevel;
  stored_ = renderer.storeBwBuffer();

  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  panelW_ = std::min(sw - 40, kPanelWidth);
  panelH_ = kTitleHeight + rowCount() * kRowHeight + kPanelPad;
  panelX_ = (sw - panelW_) / 2;
  panelY_ = (sh - panelH_) / 2;

  render(renderer);
}

bool QuickMenuPanel::close(GfxRenderer& renderer) {
  if (!active_) {
    return false;
  }
  active_ = false;
  if (stored_) {
    renderer.restoreBwBuffer();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    stored_ = false;
  }
  return needsRedrawAfterClose_;
}

void QuickMenuPanel::applyBacklight(GfxRenderer& renderer, const int delta) {
  backlight_ += delta;
  backlight_ = std::max(0, std::min(10, backlight_));
  SETTINGS.backlightLevel = static_cast<uint8_t>(backlight_);
  gpio.setBacklightLevel(static_cast<uint8_t>(backlight_));
  SETTINGS.saveToFile();
  render(renderer);
}

void QuickMenuPanel::toggleNightMode(GfxRenderer& renderer) {
  SETTINGS.nightMode = SETTINGS.nightMode ? 0 : 1;
  renderer.setNightMode(SETTINGS.nightMode != 0);
  SETTINGS.saveToFile();
  needsRedrawAfterClose_ = true;
  render(renderer);
}

bool QuickMenuPanel::handleTouchTap(GfxRenderer& renderer, const int16_t x, const int16_t y) {
  if (!active_) {
    return false;
  }
  if (x < panelX_ || x >= panelX_ + panelW_ || y < panelY_ || y >= panelY_ + panelH_) {
    // Tap outside the panel dismisses it.
    close(renderer);
    return true;
  }
  for (int row = 0; row < rowCount(); ++row) {
    if (y >= rowY(row) && y < rowY(row) + kRowHeight) {
      switch (row) {
        case 0:  // Backlight: explicit "-" and "+" buttons flanking the level.
          if (x >= backlightMinusX_ && x < backlightMinusX_ + kStepBtnW) {
            applyBacklight(renderer, -1);
          } else if (x >= backlightPlusX_ && x < backlightPlusX_ + kStepBtnW) {
            applyBacklight(renderer, 1);
          }
          break;
        case 1:  // Night mode toggle.
          toggleNightMode(renderer);
          break;
        case 2:  // Sleep.
          close(renderer);
          if (onSleep_) {
            onSleep_();
          }
          break;
      }
      return true;
    }
  }
  return true;
}

void QuickMenuPanel::render(GfxRenderer& renderer) {
  // Paper (light) card with an ink outline - the same treatment as the other popups. Night mode
  // inverts every pixel, so the card reads light in day mode and dark in night mode (the reverse of
  // the old always-ink card, which stayed dark in day mode and went light in night mode).
  renderer.rectangle.fill(panelX_ - 2, panelY_ - 2, panelW_ + 4, panelH_ + 4, false, true);
  renderer.rectangle.render(panelX_ - 2, panelY_ - 2, panelW_ + 4, panelH_ + 4, true, true);
  renderer.rectangle.fill(panelX_, panelY_, panelW_, panelH_, false, true);
  renderer.rectangle.render(panelX_, panelY_, panelW_, panelH_, true, true);

  renderer.text.render(kTitleFont, panelX_ + 18, panelY_ + 14, "Quick settings", true, EpdFontFamily::BOLD);
  renderer.line.render(panelX_ + 12, panelY_ + kTitleHeight - 8, panelX_ + panelW_ - 12, panelY_ + kTitleHeight - 8,
                       true);

  const int valueX = panelX_ + panelW_ - 18;

  // Row 0: Backlight - two fixed-position outline buttons with a triangle direction indicator. The
  // value sits centered in a reserved slot between them, so the buttons never shift between 1-10.
  {
    const int y = rowY(0);
    const int textY = y + (kRowHeight - renderer.text.getLineHeight(kRowFont)) / 2;
    renderer.text.render(kRowFont, panelX_ + 18, textY, "Backlight", true);

    char buf[24];
    snprintf(buf, sizeof(buf), "%d", backlight_);
    const int vw = renderer.text.getWidth(kRowFont, buf);
    const int rightEdge = panelX_ + panelW_ - 18;
    const int btnH = kRowHeight - 16;
    const int btnY = y + (kRowHeight - btnH) / 2;
    backlightPlusX_ = rightEdge - kStepBtnW;
    backlightMinusX_ = backlightPlusX_ - 12 - kValueSlotW - 12 - kStepBtnW;
    const int valueCenter = backlightMinusX_ + kStepBtnW + ((backlightPlusX_ - (backlightMinusX_ + kStepBtnW)) / 2);
    const int valueX = valueCenter - vw / 2;

    auto drawStepBtn = [&](const int x, const char* label) {
      // Same visual language as the font-size stepper in the settings drawer: a solid paper-filled,
      // rounded button with an ink outline and a bold "-"/"+" label - always visible, never washes
      // out on the partial-refresh e-ink update (outline-only buttons could look like they vanished).
      renderer.rectangle.fill(x, btnY, kStepBtnW, btnH, false, true);
      renderer.rectangle.render(x, btnY, kStepBtnW, btnH, true, true);
      const int lw = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, label);
      const int ly = btnY + (btnH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
      renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, x + (kStepBtnW - lw) / 2, ly, label, true,
                           EpdFontFamily::BOLD);
    };
    drawStepBtn(backlightMinusX_, "-");
    drawStepBtn(backlightPlusX_, "+");
    renderer.text.render(kRowFont, valueX, textY, buf, true);
    renderer.line.render(panelX_ + 12, y + kRowHeight - 1, panelX_ + panelW_ - 12, y + kRowHeight - 1, true,
                         LineRender::Style::Dotted);
  }

  // Row 1: Night mode
  {
    const int y = rowY(1);
    const int textY = y + (kRowHeight - renderer.text.getLineHeight(kRowFont)) / 2;
    renderer.text.render(kRowFont, panelX_ + 18, textY, "Night mode", true);
    const char* state = SETTINGS.nightMode ? "ON" : "OFF";
    const int w = renderer.text.getWidth(kRowFont, state);
    renderer.text.render(kRowFont, valueX - w, textY, state, true);
    renderer.line.render(panelX_ + 12, y + kRowHeight - 1, panelX_ + panelW_ - 12, y + kRowHeight - 1, true,
                         LineRender::Style::Dotted);
  }

  // Row 2: Sleep
  {
    const int y = rowY(2);
    const int textY = y + (kRowHeight - renderer.text.getLineHeight(kRowFont)) / 2;
    renderer.text.render(kRowFont, panelX_ + 18, textY, "Sleep", true);
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

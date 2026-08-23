/**
 * @file PresetPickerUi.cpp
 * @brief Definitions for PresetPickerUi.
 */

#include "PresetPickerUi.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <string>

#include "EpubActivity.h"
#include "state/BookSetting.h"
#include "state/ReaderPreset.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiTheme.h"

namespace {
constexpr int kVisibleRows = 6;
}  // namespace

void PresetPickerUi::enter(EpubActivity& act) {
  mode_ = true;
  const int presetCount = std::max(1, READER_PRESETS.count());
  selected_ =
      act.bookSettings.readerPresetIndex == BookSettings::kNoReaderPreset ? 0 : act.bookSettings.readerPresetIndex;
  selected_ = std::max(0, std::min(selected_, presetCount - 1));
  scroll_ = std::max(0, selected_ - kVisibleRows / 2);
  clampScroll();
  render(act);
}

void PresetPickerUi::handleInput(EpubActivity& act) {
  const MappedInputManager& m = act.mappedInput;

  if (m.wasReleased(MappedInputManager::Button::Back)) {
    mode_ = false;
    act.renderScreen(true);
    return;
  }

  if (m.wasReleased(MappedInputManager::Button::Confirm)) {
    // Apply the currently-marked preset (touch users apply via handleTouchTap).
    mode_ = false;
    act.settingsDrawerSnapshot_ = act.bookSettings;
    act.hasSettingsDrawerSnapshot_ = true;
    READER_PRESETS.applyToBook(selected_, act.bookSettings);
    act.saveBookSettings();
    act.applyBookSettings();
    act.startPageTimer();
    return;
  }
}

bool PresetPickerUi::handleTouchTap(EpubActivity& act, const int16_t x, const int16_t y) {
  if (!mode_) {
    return false;
  }
  const int screenW = act.renderer.getScreenWidth();
  const int screenH = act.renderer.getScreenHeight();
  const int presetCount = std::max(1, READER_PRESETS.count());
  const int rows = std::min(kVisibleRows, presetCount);
  const int boxW = std::min(screenW - 60, 320);
  constexpr int rowH = UiTheme::DRAWER_LIST_ITEM_HEIGHT - 4;
  const int overlayHeaderH = INX_THEME.drawerHeaderHeight() - 4;
  const int boxH = overlayHeaderH + rows * rowH;
  const int boxX = (screenW - boxW) / 2;
  const int boxY = (screenH - boxH) / 2;

  if (x < boxX || x >= boxX + boxW || y < boxY + overlayHeaderH || y >= boxY + boxH) {
    mode_ = false;  // Tap outside the list (header included) cancels, like the other popup selectors.
    act.renderScreen(true);
    return true;
  }

  clampScroll();
  const int row = (y - (boxY + overlayHeaderH)) / rowH;
  const int presetIndex = scroll_ + row;
  if (presetIndex < 0 || presetIndex >= presetCount) {
    return true;
  }

  // Tap a row = apply that preset immediately (same save/rebuild path as Confirm).
  selected_ = presetIndex;
  mode_ = false;
  act.settingsDrawerSnapshot_ = act.bookSettings;
  act.hasSettingsDrawerSnapshot_ = true;
  READER_PRESETS.applyToBook(selected_, act.bookSettings);
  act.saveBookSettings();
  act.applyBookSettings();
  act.startPageTimer();
  return true;
}

bool PresetPickerUi::handleTouchSwipe(EpubActivity& act, const int16_t dx, const int16_t dy) {
  (void)dx;
  if (!mode_) {
    return false;
  }
  const int presetCount = std::max(1, READER_PRESETS.count());
  const int rows = std::min(kVisibleRows, presetCount);
  const int maxScroll = std::max(0, presetCount - rows);
  if (maxScroll == 0) {
    return true;
  }
  constexpr int kSwipeThreshold = 40;
  if (dy <= -kSwipeThreshold) {
    scroll_ = std::min(scroll_ + rows, maxScroll);  // swipe up = next page
    render(act);
  } else if (dy >= kSwipeThreshold) {
    scroll_ = std::max(scroll_ - rows, 0);  // swipe down = previous page
    render(act);
  }
  return true;
}

void PresetPickerUi::clampScroll() {
  const int presetCount = std::max(1, READER_PRESETS.count());
  const int rows = std::min(kVisibleRows, presetCount);
  const int maxScroll = std::max(0, presetCount - rows);
  scroll_ = std::max(0, std::min(scroll_, maxScroll));
}

void PresetPickerUi::render(EpubActivity& act) {
  GfxRenderer& renderer = act.renderer;
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int presetCount = std::max(1, READER_PRESETS.count());
  const int rows = std::min(kVisibleRows, presetCount);

  const int boxW = std::min(screenW - 60, 320);
  constexpr int rowH = UiTheme::DRAWER_LIST_ITEM_HEIGHT - 4;
  const int overlayHeaderH = INX_THEME.drawerHeaderHeight() - 4;
  const int boxH = overlayHeaderH + rows * rowH;
  const int boxX = (screenW - boxW) / 2;
  const int boxY = (screenH - boxH) / 2;

  renderer.rectangle.fill(boxX, boxY, boxW, boxH, false);

  const int titleY = boxY + (overlayHeaderH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, boxX + 16, titleY, "Apply Preset", true,
                       EpdFontFamily::BOLD);

  clampScroll();
  // The applied preset is shown with a dot + bold name (informative "you are here", not a selection
  // highlight - rows are always paper with ink text, tap to apply).
  const int currentPreset = act.bookSettings.readerPresetIndex == BookSettings::kNoReaderPreset
                                ? 0
                                : act.bookSettings.readerPresetIndex;
  for (int i = 0; i < rows; ++i) {
    const int presetIndex = scroll_ + i;
    if (presetIndex >= presetCount) {
      break;
    }
    const int rowY = boxY + overlayHeaderH + i * rowH;
    const bool isCurrent = (presetIndex == currentPreset);
    const int textY = rowY + (rowH - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

    if (isCurrent) {
      const int dotX = boxX + 15;
      const int dotY = rowY + rowH / 2;
      renderer.rectangle.fill(dotX - 3, dotY - 3, 6, 6, true, /*rounded=*/true);
    }

    const std::string name =
        renderer.text.truncate(ATKINSON_HYPERLEGIBLE_10_FONT_ID, READER_PRESETS.nameOf(presetIndex).c_str(), boxW - 40);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, boxX + 24, textY, name.c_str(), true,
                         isCurrent ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    if (i + 1 < rows) {
      renderer.line.render(boxX, rowY + rowH, boxX + boxW, rowY + rowH, true, LineRender::Style::Dotted);
    }
  }

  if (presetCount > rows) {
    const int maxScroll = std::max(1, presetCount - rows);
    const int trackX = boxX + boxW - 10;
    const int trackY = boxY + overlayHeaderH;
    const int trackH = rows * rowH;
    const int thumbH = std::max(8, trackH * rows / presetCount);
    const int thumbY = trackY + scroll_ * std::max(1, trackH - thumbH) / maxScroll;
    renderer.rectangle.fill(trackX, trackY, 2, trackH, true);
    renderer.rectangle.fill(trackX - 2, thumbY, 6, thumbH, true);
  }

  renderer.line.render(boxX, boxY + overlayHeaderH, boxX + boxW, boxY + overlayHeaderH, true);
  renderer.rectangle.render(boxX, boxY, boxW, boxH, true);
  renderer.rectangle.render(boxX + 1, boxY + 1, boxW - 2, boxH - 2, true);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

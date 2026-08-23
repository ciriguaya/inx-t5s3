#pragma once

#include <cstdint>

/**
 * @file PresetPickerUi.h
 * @brief Popup for quickly applying a saved reader preset from the EPUB reader.
 *
 * Touch-first (T5S3): tapping a row applies that preset immediately; swiping
 * scrolls the list. The currently-applied preset is marked with a dot + bold
 * name instead of a button-style Ink highlight (see the TOC/bookmarks menus).
 */

class EpubActivity;

class PresetPickerUi {
 public:
  bool isActive() const { return mode_; }

  void enter(EpubActivity& act);
  void handleInput(EpubActivity& act);
  bool handleTouchTap(EpubActivity& act, int16_t x, int16_t y);
  bool handleTouchSwipe(EpubActivity& act, int16_t dx, int16_t dy);

 private:
  void clampScroll();
  void render(EpubActivity& act);

  bool mode_ = false;
  int selected_ = 0;
  int scroll_ = 0;
};

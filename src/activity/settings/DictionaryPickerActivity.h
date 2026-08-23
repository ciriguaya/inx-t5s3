#pragma once

/**
 * @file DictionaryPickerActivity.h
 * @brief Picks the active dictionary from /dictionaries/<folder> for EPUB dictionary lookup.
 */

#include <functional>
#include <string>
#include <vector>

#include "activity/ActivityWithSubactivity.h"

class DictionaryPickerActivity final : public ActivityWithSubactivity {
 public:
  explicit DictionaryPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                    const std::function<void()>& goBack)
      : ActivityWithSubactivity("DictionaryPicker", renderer, mappedInput), goBack_(goBack) {}

  void onEnter() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  bool onTouchSwipe(int16_t dx, int16_t dy, int16_t endX, int16_t endY) override;

 private:
  std::vector<std::string> folders_;
  int selectedIndex_ = 0;
  int scrollOffset_ = 0;
  const std::function<void()> goBack_;

  void scanDictionaryFolders();
  void render();
};

#pragma once

/**
 * @file QuotesActivity.h
 * @brief Browser for the T5S3 reader's custom highlight/quotes system.
 *
 * Shows the saved quotes (from /highlights/*.json, the same format the
 * user's T5S3 reader fork writes) one at a time, with prev/next navigation,
 * delete, and jump-to-book. Touch-first on the T5S3 port.
 */

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "HighlightEntry.h"

struct Rect {
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;
  bool contains(const int px, const int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

class QuotesActivity final : public Activity {
 public:
  explicit QuotesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                          const std::function<void()>& onGoBack, const std::function<void(const std::string&)>& onOpenBook)
      : Activity("Quotes", renderer, mappedInput), onGoBack(onGoBack), onOpenBook(onOpenBook) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  bool onTouchSwipe(int16_t dx, int16_t dy, int16_t endX, int16_t endY) override;
  void requestRedraw() override { updateRequired = true; }

 private:
  const std::function<void()> onGoBack;
  const std::function<void(const std::string& path)> onOpenBook;

  std::vector<HighlightEntry> quotes;
  int currentIndex = 0;

  bool showDeleteConfirm = false;
  Rect deleteYesRect;
  Rect deleteNoRect;
  Rect prevBtn;
  Rect nextBtn;
  Rect openBtn;
  Rect deleteBtn;

  bool updateRequired = true;

  /** Cached display metadata (author, chapter · page label) for the quote at metadataForIndex. */
  std::string currentAuthor;
  std::string currentLocation;
  int metadataForIndex = -1;

  void loadAllQuotes();
  void clampIndex();
  void navigate(int delta);
  void refreshCurrentMetadata();
  void render();
  void renderConfirmOverlay();
  void requestDelete();
  void confirmDelete();
  void cancelDelete();
  void openBookAtCurrent();
  const HighlightEntry* current() const;
};

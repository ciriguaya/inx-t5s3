/**
 * @file QuotesActivity.cpp
 * @brief T5S3 quotes browser (see QuotesActivity.h).
 */

#include "QuotesActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>

#include <cctype>
#include <cstdlib>

#include "HighlightPersistence.h"
#include "Epub/EpubAnnotations.h"
#include "state/PendingQuoteJump.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiTheme.h"

namespace {
constexpr int kBtnH = 56;
constexpr int kBtnGap = 14;
constexpr int kBtnBottomInset = 26;
constexpr int kFontQuote = ATKINSON_HYPERLEGIBLE_14_FONT_ID;
constexpr int kFontBody = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
constexpr int kFontSmall = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
}  // namespace

void QuotesActivity::loadAllQuotes() {
  quotes = HighlightPersistence::loadAllHighlights();
  // Newest first (highest sequence).
  std::sort(quotes.begin(), quotes.end(),
            [](const HighlightEntry& a, const HighlightEntry& b) { return a.sequence > b.sequence; });
  clampIndex();
  updateRequired = true;
}

void QuotesActivity::clampIndex() {
  if (quotes.empty()) {
    currentIndex = 0;
    return;
  }
  if (currentIndex < 0) currentIndex = 0;
  if (currentIndex >= static_cast<int>(quotes.size())) currentIndex = static_cast<int>(quotes.size()) - 1;
}

void QuotesActivity::onEnter() {
  Activity::onEnter();
  loadAllQuotes();
}

void QuotesActivity::onExit() { Activity::onExit(); }

const HighlightEntry* QuotesActivity::current() const {
  return (currentIndex >= 0 && currentIndex < static_cast<int>(quotes.size())) ? &quotes[static_cast<size_t>(currentIndex)]
                                                                               : nullptr;
}

void QuotesActivity::render() {
  renderer.clearScreen();

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  const int headerH = INX_THEME.mainHeaderHeight();
  renderer.rectangle.fill(0, 0, screenW, headerH, false);
  renderer.text.render(kFontBody, 20, (headerH - renderer.text.getLineHeight(kFontBody)) / 2, "Quotes", true,
                       EpdFontFamily::BOLD);

  // "N / total" position indicator, right-aligned in the header.
  if (!quotes.empty()) {
    char posBuf[24];
    snprintf(posBuf, sizeof(posBuf), "%d / %d", currentIndex + 1, static_cast<int>(quotes.size()));
    const int posW = renderer.text.getWidth(kFontSmall, posBuf);
    renderer.text.render(kFontSmall, screenW - 20 - posW, (headerH - renderer.text.getLineHeight(kFontSmall)) / 2,
                         posBuf, true);
  }
  renderer.line.render(0, headerH, screenW, headerH, true);

  const int bodyTop = headerH + 10;

  if (quotes.empty()) {
    renderer.text.centered(kFontBody, bodyTop + 60, "No highlights yet", true, EpdFontFamily::BOLD);
    renderer.text.centered(kFontSmall, bodyTop + 90,
                           "Highlight text in a book to save a quote here", true);
    renderer.displayBuffer();
    updateRequired = false;
    return;
  }

  const HighlightEntry& entry = *current();

  // Quote text.
  const int textTop = bodyTop + 16;
  const int textW = screenW - 48;
  int y = textTop;
  std::string quoteText = entry.selectedText;
  while (!quoteText.empty() && y < screenH - 320) {
    const std::string line = renderer.text.truncate(kFontQuote, quoteText.c_str(), textW);
    if (line.empty()) break;
    renderer.text.render(kFontQuote, 24, y, line.c_str(), true);
    y += renderer.text.getLineHeight(kFontQuote);
    if (quoteText.size() <= line.size()) break;
    quoteText = quoteText.substr(line.size());
  }
  if (!quoteText.empty()) {
    renderer.text.render(kFontSmall, 24, y, "\xC2\xB7 \xC2\xB7 \xC2\xB7", true);
  }

  // Book / chapter metadata.
  const int metaTop = y + 24;
  const std::string title = renderer.text.truncate(kFontBody, entry.bookTitle.c_str(), textW);
  renderer.text.render(kFontBody, 24, metaTop, title.c_str(), true, EpdFontFamily::BOLD);
  if (!entry.chapter.empty() && entry.chapter != "0") {
    const std::string chapter = renderer.text.truncate(kFontSmall, entry.chapter.c_str(), textW);
    renderer.text.render(kFontSmall, 24, metaTop + renderer.text.getLineHeight(kFontBody) + 4, chapter.c_str(), true);
  }

  // Bottom button row.
  const int btnY = screenH - kBtnBottomInset - kBtnH;
  const int btnW = (screenW - 24 * 2 - kBtnGap * 3) / 4;
  prevBtn = {24, btnY, btnW, kBtnH};
  nextBtn = {24 + (btnW + kBtnGap), btnY, btnW, kBtnH};
  openBtn = {24 + 2 * (btnW + kBtnGap), btnY, btnW, kBtnH};
  deleteBtn = {24 + 3 * (btnW + kBtnGap), btnY, btnW, kBtnH};

  auto drawBtn = [&](const Rect& r, const char* label) {
    // All four buttons share the same outlined look - none is highlighted by default.
    renderer.rectangle.fill(r.x, r.y, r.w, r.h, false, true);
    renderer.rectangle.render(r.x, r.y, r.w, r.h, true, true);
    const int lw = renderer.text.getWidth(kFontSmall, label);
    renderer.text.render(kFontSmall, r.x + (r.w - lw) / 2, r.y + (r.h - renderer.text.getLineHeight(kFontSmall)) / 2,
                         label, true);
  };
  drawBtn(prevBtn, "Prev");
  drawBtn(nextBtn, "Next");
  drawBtn(openBtn, "Open");
  drawBtn(deleteBtn, "Delete");

  renderer.displayBuffer();
  updateRequired = false;
}

void QuotesActivity::renderConfirmOverlay() {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int panelW = std::min(screenW - 80, 360);
  const int panelH = 170;
  const int panelX = (screenW - panelW) / 2;
  const int panelY = (screenH - panelH) / 2;

  renderer.rectangle.fill(panelX, panelY, panelW, panelH, false);
  renderer.rectangle.render(panelX, panelY, panelW, panelH, true, true);

  renderer.text.centered(kFontBody, panelY + 34, "Delete this quote?", true, EpdFontFamily::BOLD);

  const int btnY = panelY + panelH - 74;
  const int btnW = (panelW - 56) / 2;
  deleteNoRect = {panelX + 24, btnY, btnW, 50};
  deleteYesRect = {panelX + 24 + btnW + 8, btnY, btnW, 50};
  renderer.rectangle.fill(deleteNoRect.x, deleteNoRect.y, deleteNoRect.w, deleteNoRect.h, false, true);
  renderer.rectangle.fill(deleteYesRect.x, deleteYesRect.y, deleteYesRect.w, deleteYesRect.h, true, true);
  const int nlw = renderer.text.getWidth(kFontBody, "Cancel");
  renderer.text.render(kFontBody, deleteNoRect.x + (deleteNoRect.w - nlw) / 2,
                       deleteNoRect.y + (deleteNoRect.h - renderer.text.getLineHeight(kFontBody)) / 2, "Cancel", true);
  const int ylw = renderer.text.getWidth(kFontBody, "Delete");
  renderer.text.render(kFontBody, deleteYesRect.x + (deleteYesRect.w - ylw) / 2,
                       deleteYesRect.y + (deleteYesRect.h - renderer.text.getLineHeight(kFontBody)) / 2, "Delete",
                       false);
  renderer.displayBuffer();
}

void QuotesActivity::loop() {
  if (updateRequired) {
    if (showDeleteConfirm) {
      renderConfirmOverlay();
    } else {
      render();
    }
  }

  if (showDeleteConfirm) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      cancelDelete();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      confirmDelete();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    auto back = onGoBack;
    if (back) back();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    navigate(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    navigate(1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    openBookAtCurrent();
  }
}

void QuotesActivity::navigate(const int delta) {
  if (quotes.empty()) {
    return;
  }
  currentIndex = (currentIndex + delta + static_cast<int>(quotes.size())) % static_cast<int>(quotes.size());
  updateRequired = true;
}

bool QuotesActivity::onTouchTap(const int16_t x, const int16_t y) {
  if (showDeleteConfirm) {
    if (deleteYesRect.contains(x, y)) {
      confirmDelete();
    } else if (deleteNoRect.contains(x, y)) {
      cancelDelete();
    }
    return true;
  }

  if (prevBtn.contains(x, y)) {
    navigate(-1);
    return true;
  }
  if (nextBtn.contains(x, y)) {
    navigate(1);
    return true;
  }
  if (openBtn.contains(x, y)) {
    openBookAtCurrent();
    return true;
  }
  if (deleteBtn.contains(x, y)) {
    requestDelete();
    return true;
  }
  if (x < renderer.getScreenWidth() / 2) {
    navigate(-1);
  } else {
    navigate(1);
  }
  return true;
}

bool QuotesActivity::onTouchSwipe(const int16_t dx, const int16_t dy, const int16_t endX, const int16_t endY) {
  (void)endX;
  (void)endY;
  if (showDeleteConfirm) {
    return true;
  }
  constexpr int kSwipeThreshold = 40;
  if (dx <= -kSwipeThreshold) {
    navigate(1);
  } else if (dx >= kSwipeThreshold) {
    navigate(-1);
  }
  return true;
}

void QuotesActivity::requestDelete() {
  if (!current()) {
    return;
  }
  showDeleteConfirm = true;
  updateRequired = true;
}

void QuotesActivity::confirmDelete() {
  const HighlightEntry* entry = current();
  showDeleteConfirm = false;
  if (!entry) {
    return;
  }

  // Remove the quote from every store it may live in (the derived /highlights/<book>.json and the
  // fork's *_pages.json master). The displayed title can differ from the on-disk file stems (case,
  // "(N)" suffixes), so match through the same candidate list the importer uses - a plain
  // sanitizeFilename(bookTitle) lookup silently misses those and the quote would come right back.
  const std::vector<std::string> candidates =
      HighlightPersistence::bookTitleCandidates(entry->bookTitle, entry->bookPath);
  HighlightPersistence::deleteHighlight(candidates, entry->chapter, entry->chapter, entry->selectedText);
  // Text-only sweep of the fork masters as well: if the quote's chapter is stored as a title in the
  // derived file but as a spine number in the master, deleteHighlight's chapter/spine checks miss the
  // master and the quote would resurrect on the next merge.
  for (const std::string& cand : candidates) {
    HighlightPersistence::deleteFromPagesMaster(cand, entry->selectedText);
  }

  loadAllQuotes();
}

void QuotesActivity::cancelDelete() {
  showDeleteConfirm = false;
  updateRequired = true;
}

void QuotesActivity::openBookAtCurrent() {
  const HighlightEntry* entry = current();
  if (!entry || entry->bookPath.empty()) {
    return;
  }

  // Resolve where this quote lives on the book's current pagination: the ANN3 annotation shards
  // hold the re-located (spine, page) written when the quote was imported or saved. The stored
  // "chapter" is the spine number for fork-master quotes (a fast-path hint) and the chapter title
  // for derived ones (full scan). When the shard doesn't exist yet (a fork quote on a spine that was
  // never built), fall back to the chapter's first page - close, and the reader re-locates on build.
  int spine = -1;
  int page = 0;
  bool numericChapter = true;
  for (const char c : entry->chapter) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      numericChapter = false;
      break;
    }
  }
  const int spineHint = numericChapter && !entry->chapter.empty() ? std::atoi(entry->chapter.c_str()) : -1;

  const std::string cachePath = "/.metadata/epub/" +
                                std::to_string(std::hash<std::string>{}(entry->bookPath));
  const bool located = EpubAnnotations::findQuoteLocation(cachePath, entry->selectedText, spineHint, &spine, &page);
  if (located && spine >= 0) {
    pendingQuoteJump.set(entry->bookPath, spine, page, true);
  } else if (spineHint >= 0) {
    // No ANN3 shard yet (spine never built): land on the chapter's first page and let the reader
    // search for the exact page once the spine is built.
    pendingQuoteJump.set(entry->bookPath, spineHint, 0, false, entry->selectedText);
  }

  auto open = onOpenBook;
  if (open) {
    open(entry->bookPath);
  }
}

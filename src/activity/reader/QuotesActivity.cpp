/**
 * @file QuotesActivity.cpp
 * @brief T5S3 quotes browser (see QuotesActivity.h).
 */

#include "QuotesActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>

#include <cctype>
#include <cstdlib>

#include "Epub/BookMetadataCache.h"
#include "Epub/Page.h"
#include "Epub/Section.h"
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
constexpr int kFontTitle = ATKINSON_HYPERLEGIBLE_16_FONT_ID;

/** Collapses whitespace runs to single spaces (no lowercasing - keeps the original text for display). */
std::string collapseWs(const std::string& text) {
  std::string out;
  bool pendingSpace = false;
  for (const char c : text) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      pendingSpace = !out.empty();
    } else {
      if (pendingSpace) {
        out += ' ';
        pendingSpace = false;
      }
      out += c;
    }
  }
  return out;
}

/** Case-insensitive substring search. Returns std::string::npos when not found. */
size_t ciFind(const std::string& hay, const std::string& needle) {
  if (needle.empty()) {
    return 0;
  }
  if (needle.size() > hay.size()) {
    return std::string::npos;
  }
  for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
    size_t j = 0;
    while (j < needle.size() &&
           std::tolower(static_cast<unsigned char>(hay[i + j])) ==
               std::tolower(static_cast<unsigned char>(needle[j]))) {
      ++j;
    }
    if (j == needle.size()) {
      return i;
    }
  }
  return std::string::npos;
}
}  // namespace

void QuotesActivity::loadAllQuotes() {
  quotes = HighlightPersistence::loadAllHighlights();
  // Newest first (highest sequence).
  std::sort(quotes.begin(), quotes.end(),
            [](const HighlightEntry& a, const HighlightEntry& b) { return a.sequence > b.sequence; });
  clampIndex();
  refreshCurrentMetadata();
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

void QuotesActivity::refreshCurrentMetadata() {
  if (metadataForIndex == currentIndex) {
    return;
  }
  metadataForIndex = currentIndex;
  currentAuthor.clear();
  currentLocation.clear();
  quotePageText.clear();
  if (quotes.empty() || currentIndex < 0 || currentIndex >= static_cast<int>(quotes.size())) {
    return;
  }
  const HighlightEntry& entry = quotes[static_cast<size_t>(currentIndex)];
  if (entry.bookPath.empty()) {
    return;
  }

  const std::string cachePath =
      "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(entry.bookPath));

  // Author from the book's cached metadata (when the book was opened on this firmware).
  BookMetadataCache metadata(cachePath);
  if (metadata.load()) {
    currentAuthor = metadata.coreMetadata.author;
  }

  // Chapter/page label: reuse the ANN3 relocation used by Open. Numeric chapters (fork-master
  // quotes) read as "Chapter N"; derived quotes keep their stored chapter title.
  bool numericChapter = !entry.chapter.empty();
  for (const char c : entry.chapter) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      numericChapter = false;
      break;
    }
  }
  std::string chapterLabel;
  if (numericChapter) {
    chapterLabel = "Chapter " + entry.chapter;
  } else if (!entry.chapter.empty() && entry.chapter != "0") {
    chapterLabel = entry.chapter;
  }
  int spine = -1;
  int page = 0;
  const int spineHint = numericChapter && !entry.chapter.empty() ? std::atoi(entry.chapter.c_str()) : -1;
  if (EpubAnnotations::findQuoteLocation(cachePath, entry.selectedText, spineHint, &spine, &page) && spine >= 0) {
    // Book-page context: the cached page the quote lives on, so the quote is shown in context
    // (the Inx "book page highlight" look) instead of as an isolated snippet.
    std::unique_ptr<Page> cached = Section::loadCachedPage(cachePath, spine, page);
    if (cached) {
      quotePageText = cached->extractPlainText(2000);
    }
    char pageBuf[16];
    snprintf(pageBuf, sizeof(pageBuf), "p. %d", page + 1);
    currentLocation = chapterLabel.empty() ? pageBuf : chapterLabel + " \xC2\xB7 " + pageBuf;
  } else {
    currentLocation = chapterLabel;
  }
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

  const int textW = screenW - 48;

  // Book header (Inx export-card cues: bold title + byline; chapter · page goes into the
  // page-card footer, like Inx's book-page highlight cards).
  int y = bodyTop;
  const std::string title = renderer.text.truncate(kFontTitle, entry.bookTitle.c_str(), textW);
  renderer.text.render(kFontTitle, 24, y, title.c_str(), true, EpdFontFamily::BOLD);
  y += renderer.text.getLineHeight(kFontTitle);
  if (!currentAuthor.empty()) {
    std::string byline = renderer.text.truncate(kFontBody, ("by " + currentAuthor).c_str(), textW);
    renderer.text.render(kFontBody, 24, y, byline.c_str(), true);
    y += renderer.text.getLineHeight(kFontBody) + 2;
  }
  const int dividerY = y + 6;
  renderer.line.render(24, dividerY, screenW - 24, dividerY, true);

  // ---- Book-page highlight card (Inx "book page" style) ----
  // The quote is shown in the context of the book page (cached page text, or the stored
  // paragraph as a fallback) with the quoted span marked by a lattice band, plus the
  // chapter · page footer - so the quote reads like a real highlighted page passage.
  const int btnY = screenH - kBtnBottomInset - kBtnH;
  const int cardX = 24;
  const int cardW = screenW - 48;
  const int cardTop = dividerY + 14;
  const int cardBottom = btnY - 18;
  const int cardH = cardBottom - cardTop;

  renderer.rectangle.fill(cardX, cardTop, cardW, cardH, false);
  renderer.rectangle.render(cardX, cardTop, cardW, cardH, true);

  // Book-page margin line.
  const int marginX = cardX + 14;
  renderer.line.render(marginX, cardTop + 8, marginX, cardBottom - 8, true);

  const int pageFont = kFontSmall;
  const int textX = marginX + 10;
  const int pageTextW = cardX + cardW - textX - 16;
  const int lineH = renderer.text.getLineHeight(pageFont);
  const int footerH = currentLocation.empty() ? 12 : lineH + 14;
  const int bodyTopY = cardTop + 10;
  const int bodyBottomY = cardBottom - footerH;

  // Body text: prefer the cached page context, fall back to the stored paragraph, then the
  // quote itself. Find the quoted span (case-insensitive, whitespace-collapsed).
  std::string body;
  size_t qpos = std::string::npos;
  const std::string qn = collapseWs(entry.selectedText);
  auto tryCandidate = [&](const std::string& cand) {
    if (cand.empty()) {
      return false;
    }
    const std::string norm = collapseWs(cand);
    const size_t p = ciFind(norm, qn);
    if (p != std::string::npos) {
      body = norm;
      qpos = p;
      return true;
    }
    return false;
  };
  if (!tryCandidate(quotePageText) && !tryCandidate(entry.paragraphText)) {
    body = qn.empty() ? entry.selectedText : qn;
    qpos = 0;
  }
  if (body.empty()) {
    body = "\xC2\xB7 \xC2\xB7 \xC2\xB7";
  }

  // Word-wrap the whole body (offset per line into `body`), then window it around the quote
  // line so the highlighted span is always visible with a little context above it.
  std::vector<std::pair<std::string, size_t>> allLines;
  {
    size_t i = 0;
    const size_t n = body.size();
    while (i < n) {
      const size_t lineStart = i;
      std::string line;
      while (i < n) {
        size_t we = i;
        while (we < n && body[we] != ' ') {
          ++we;
        }
        const std::string word = body.substr(i, we - i);
        const std::string cand = line.empty() ? word : line + " " + word;
        if (renderer.text.getWidth(pageFont, cand.c_str()) <= pageTextW || line.empty()) {
          line = cand;
          i = (we < n) ? we + 1 : we;
        } else {
          break;
        }
      }
      allLines.emplace_back(line, lineStart);
    }
  }

  int quoteLine = 0;
  for (size_t li = 0; li < allLines.size(); ++li) {
    if (qpos >= allLines[li].second) {
      quoteLine = static_cast<int>(li);
    }
  }
  const int capacity = std::max(3, (bodyBottomY - bodyTopY) / lineH);
  int windowStart = std::max(0, quoteLine - 1);  // one context line above the quote
  const int windowEnd = std::min(static_cast<int>(allLines.size()), windowStart + capacity);
  if (quoteLine >= windowEnd) {
    windowStart = std::max(0, quoteLine - capacity + 1);
  }

  int ty = bodyTopY;
  const int textAlignPad = 2;
  for (int li = windowStart; li < windowEnd && ty + lineH <= bodyBottomY; ++li, ty += lineH) {
    const std::string& line = allLines[static_cast<size_t>(li)].first;
    const size_t lineStart = allLines[static_cast<size_t>(li)].second;
    const size_t lineEnd = lineStart + line.size();
    const size_t bs = std::max(qpos, lineStart);
    const size_t be = std::min(qpos + qn.size(), lineEnd);
    if (bs < be) {
      const std::string prefix = line.substr(0, bs - lineStart);
      const std::string span = line.substr(bs - lineStart, be - bs);
      const int bandX = textX + renderer.text.getWidth(pageFont, prefix.c_str());
      const int bandW = renderer.text.getWidth(pageFont, span.c_str()) + textAlignPad;
      // Two offset lattice passes = the same ~50% checkerboard band as the in-book highlight.
      renderer.ui.fillSparseInkLatticeInRect(bandX - 1, ty - 1, bandW, lineH + 2, 2);
      renderer.ui.fillSparseInkLatticeInRect(bandX, ty, bandW, lineH + 1, 2);
    }
    renderer.text.render(pageFont, textX, ty, line.c_str(), true);
  }

  // Chapter · page footer inside the page card (Inx's book-page footer).
  if (!currentLocation.empty()) {
    const std::string footer = renderer.text.truncate(kFontSmall, currentLocation.c_str(), pageTextW);
    renderer.text.render(kFontSmall, textX, cardBottom - lineH - 6, footer.c_str(), true);
  }

  // Bottom button row.
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
  refreshCurrentMetadata();
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

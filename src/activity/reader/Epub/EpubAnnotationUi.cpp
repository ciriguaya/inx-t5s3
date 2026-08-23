#include "EpubAnnotationUi.h"

#include <Epub/Page.h>
#include <Epub/PageWordIndex.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <ctime>
#include <new>

#include "EpubActivity.h"
#include "../HighlightPersistence.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {

constexpr unsigned long kChordHoldMs = 600;
constexpr int kHighlightLatticeStepPx = 2;
/** ADC/button bounce can deliver two wasPressed edges ~ms apart; loop has no delay — suppress 2nd edge. */
constexpr unsigned long kNavEdgeDebounceMs = 130;
constexpr unsigned long kNavRepeatInitialMs = 700;
constexpr unsigned long kNavRepeatIntervalMs = 95;

// Highlight-delete popup geometry (Inx-styled: paper card, rounded, bold title, button row).
constexpr int kDeletePopupW = 380;
constexpr int kDeletePopupH = 210;
constexpr int kDeletePopupBtnH = 50;
constexpr int kDeletePopupFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
constexpr int kDeletePopupTitleFont = ATKINSON_HYPERLEGIBLE_12_FONT_ID;

/** Case-insensitive whitespace-collapsed compare used to match stored highlight texts. */
std::string normalizeHighlightText(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  bool pendingSpace = false;
  for (const char c : s) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      pendingSpace = !out.empty();
      continue;
    }
    if (pendingSpace) {
      out += ' ';
      pendingSpace = false;
    }
    out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

}  // namespace

EpubAnnotationUi::EpubAnnotationUi() = default;

void EpubAnnotationUi::setWordIndexCache(const int spine, const int page, const int fontId, const int headerFontId,
                                         const int marginL, const int marginT) {
  wordIndexCacheSpine_ = spine;
  wordIndexCachePage_ = page;
  wordIndexCacheFontId_ = fontId;
  wordIndexCacheHeaderFontId_ = headerFontId;
  wordIndexCacheMarginL_ = marginL;
  wordIndexCacheMarginT_ = marginT;
}

void EpubAnnotationUi::clearWordIndexCache() {
  wordIndexCacheSpine_ = -1;
  wordIndexCachePage_ = -1;
  wordIndexCacheFontId_ = -1;
  wordIndexCacheHeaderFontId_ = -1;
  wordIndexCacheMarginL_ = INT_MIN;
  wordIndexCacheMarginT_ = INT_MIN;
}

void EpubAnnotationUi::clearSessionAndCapture() {
  annotations_.clearSession();
  std::vector<std::pair<size_t, size_t>>().swap(pendingSpans_);
  for (auto& ch : captureChunks_) {
    ch.reset();
  }
  std::vector<std::unique_ptr<uint8_t[]>>().swap(captureChunks_);
  captureMonolithic_.reset();
  captureUsesMonolithic_ = false;
  captureBytes_ = 0;
  captureValid_ = false;
  clearWordIndexCache();
}

void EpubAnnotationUi::tryChordEnter(EpubActivity& act) {
  if (!act.epub || !act.section || mode_) {
    return;
  }
  const bool down = act.mappedInput.rawHalIsPressed(HalGPIO::BTN_DOWN);
  const bool right = act.mappedInput.rawHalIsPressed(HalGPIO::BTN_RIGHT);
  if (down && right) {
    if (chordStartMs_ == 0) {
      chordStartMs_ = millis();
    }
    if (!chordConsumed_ && millis() - chordStartMs_ >= kChordHoldMs) {
      enter(act);
      chordConsumed_ = true;
    }
  } else {
    chordStartMs_ = 0;
    chordConsumed_ = false;
  }
}

bool EpubAnnotationUi::isDuplicateNavEdge(const int dir, const unsigned long now) {
  if (annLastNavEdgeDir_ == dir && (now - annLastNavEdgeMs_) < kNavEdgeDebounceMs) {
    return true;
  }
  annLastNavEdgeMs_ = now;
  annLastNavEdgeDir_ = dir;
  return false;
}

bool EpubAnnotationUi::hasSaveableContent() const {
  if (!pendingSpans_.empty()) {
    return true;
  }
  if (!selectingStarted_ || words_.empty()) {
    return false;
  }
  const size_t lo = std::min(anchor_, focus_);
  const size_t hi = std::max(anchor_, focus_);
  return lo <= hi;
}

void EpubAnnotationUi::resetSelectionToStart(EpubActivity& act) {
  pendingSpans_.clear();
  selectingStarted_ = false;
  focus_ = 0;
  anchor_ = 0;
  act.updateRequired = true;
}

void EpubAnnotationUi::clearAllStoredHighlightsOnCurrentPage(EpubActivity& act) {
  if (!act.epub || !act.section) {
    return;
  }
  annotations_.clearPageShard(act.epub->getCachePath(), act.currentSpineIndex, act.section->currentPage);
  storedRanges_.clear();
  pendingSpans_.clear();
  selectingStarted_ = false;
  focus_ = 0;
  anchor_ = 0;
  // Force full word-index rebuild so merge/geometry cannot reuse state tied to the deleted highlights.
  clearWordIndexCache();
  // Full redraw clears lattice from the framebuffer; then re-capture for annotation repaint path.
  // Suppress drawUiOverlay() during this specific render - otherwise it redraws a cursor box at
  // focus_ (word 0) before the capture, baking a stale highlight permanently into the "clean"
  // snapshot that every later repaint() restores from, regardless of where focus_ moves next.
  suppressOverlayDraw_ = true;
  act.renderScreen(true);
  suppressOverlayDraw_ = false;
  captureFramebuffer(act);
  act.updateRequired = true;
}

void EpubAnnotationUi::normalizeSpans(std::vector<std::pair<size_t, size_t>>& spans) {
  if (spans.empty()) {
    return;
  }
  std::sort(spans.begin(), spans.end());
  size_t write = 0;
  auto cur = spans[0];
  for (size_t i = 1; i < spans.size(); ++i) {
    if (spans[i].first <= cur.second + 1) {
      cur.second = std::max(cur.second, spans[i].second);
    } else {
      spans[write++] = cur;
      cur = spans[i];
    }
  }
  spans[write++] = cur;
  spans.resize(write);
}

void EpubAnnotationUi::enter(EpubActivity& act) {
  if (!act.section || !act.epub) {
    return;
  }
  // The Down+Right entry chord (and a plain long-press Down) leave the button held while
  // handleInput() is about to stop running for the whole overlay session - reset its per-button
  // state now so it doesn't misfire a stale long-press the instant this overlay exits.
  act.btnBindings_.reset();
  mode_ = true;
  selectingStarted_ = false;
  pendingSpans_.clear();
  annLastNavEdgeDir_ = -1;
  annNavRepeatDir_ = -1;
  anchor_ = 0;
  focus_ = 0;
  // Build word index first, then capture the framebuffer. Allocating the 48k capture before the word index
  // (many strings + PageWordHit) spikes heap usage and can abort() on OOM on ESP32.
  prepareWordGeometry(act);
  if (words_.empty()) {
    act.readerPopup("No text to highlight");
    exit(act);
    return;
  }
  captureFramebuffer(act);
  if (!captureValid_) {
    act.readerPopup("Could not capture page");
    exit(act);
    return;
  }
  act.updateRequired = true;
}

void EpubAnnotationUi::exit(EpubActivity& act) {
  mode_ = false;
  deletePopupActive_ = false;
  deleteText_.clear();
  selectingStarted_ = false;
  // Keep the word index + cache keys across gestures: re-entering highlight mode on the same page then
  // skips the expensive full-page word-index rebuild (prepareWordGeometry's cache-hit path), which was
  // a big part of the perceived long-press lag. The index is rebuilt automatically when the page, font
  // or margins change (cache-key mismatch).
  std::vector<std::pair<size_t, size_t>>().swap(pendingSpans_);
  std::vector<std::pair<size_t, size_t>>().swap(storedRanges_);
  annLastNavEdgeDir_ = -1;
  annNavRepeatDir_ = -1;
  for (auto& ch : captureChunks_) {
    ch.reset();
  }
  std::vector<std::unique_ptr<uint8_t[]>>().swap(captureChunks_);
  captureMonolithic_.reset();
  captureUsesMonolithic_ = false;
  captureBytes_ = 0;
  captureValid_ = false;
  act.updateRequired = true;
}

bool EpubAnnotationUi::tryNavigationHoldRepeat(EpubActivity& act) {
  using Btn = MappedInputManager::Button;
  const MappedInputManager& m = act.mappedInput;
  const unsigned long now = millis();

  // One edge = one move. Holding the same direction starts repeat only after a long enough delay
  // that a normal click cannot jump two words/lines.
  if (m.wasPressed(Btn::Left)) {
    if (isDuplicateNavEdge(0, now)) {
      return true;
    }
    moveFocusWord(-1);
    annNavRepeatDir_ = 0;
    annNavRepeatNextMs_ = now + kNavRepeatInitialMs;
    act.updateRequired = true;
    return true;
  }
  if (m.wasPressed(Btn::Right)) {
    if (isDuplicateNavEdge(1, now)) {
      return true;
    }
    moveFocusWord(1);
    annNavRepeatDir_ = 1;
    annNavRepeatNextMs_ = now + kNavRepeatInitialMs;
    act.updateRequired = true;
    return true;
  }
  if (m.wasPressed(Btn::Up)) {
    if (isDuplicateNavEdge(2, now)) {
      return true;
    }
    moveFocusLine(-1);
    annNavRepeatDir_ = 2;
    annNavRepeatNextMs_ = now + kNavRepeatInitialMs;
    act.updateRequired = true;
    return true;
  }
  if (m.wasPressed(Btn::Down)) {
    if (isDuplicateNavEdge(3, now)) {
      return true;
    }
    moveFocusLine(1);
    annNavRepeatDir_ = 3;
    annNavRepeatNextMs_ = now + kNavRepeatInitialMs;
    act.updateRequired = true;
    return true;
  }
  const bool leftHeld = m.isPressed(Btn::Left);
  const bool rightHeld = m.isPressed(Btn::Right);
  const bool upHeld = m.isPressed(Btn::Up);
  const bool downHeld = m.isPressed(Btn::Down);
  if (!leftHeld && !rightHeld && !upHeld && !downHeld) {
    annNavRepeatDir_ = -1;
    return false;
  }
  if (annNavRepeatDir_ < 0 || now < annNavRepeatNextMs_) {
    return false;
  }
  if (annNavRepeatDir_ == 0 && leftHeld) {
    moveFocusWord(-1);
  } else if (annNavRepeatDir_ == 1 && rightHeld) {
    moveFocusWord(1);
  } else if (annNavRepeatDir_ == 2 && upHeld) {
    // Auto-repeat covers 2 lines/tick (vs. 1 for the initial press) - holding Up/Down would
    // otherwise take forever to cross a full page at kNavRepeatIntervalMs.
    moveFocusLine(-1);
    moveFocusLine(-1);
  } else if (annNavRepeatDir_ == 3 && downHeld) {
    moveFocusLine(1);
    moveFocusLine(1);
  } else {
    annNavRepeatDir_ = -1;
    return false;
  }
  annNavRepeatNextMs_ = now + kNavRepeatIntervalMs;
  act.updateRequired = true;
  return true;
}

std::string EpubAnnotationUi::extractRangeText(const size_t anchorFlat, const size_t focusFlat) const {
  if (words_.empty()) {
    return {};
  }
  const size_t lo = std::min(anchorFlat, focusFlat);
  const size_t hi = std::max(anchorFlat, focusFlat);
  std::string out;
  for (size_t i = lo; i <= hi && i < words_.size(); ++i) {
    if (!out.empty()) {
      out += ' ';
    }
    out += words_[i].text;
  }
  return out;
}

std::string EpubAnnotationUi::buildParagraphTextForRange(const size_t lo, const size_t hi) const {
  if (words_.empty()) {
    return {};
  }
  // Find the first and last line that overlap the selected word range.
  auto lineForWord = [this](const size_t wordIdx) -> int {
    for (size_t li = 0; li < lineFirst_.size(); ++li) {
      const size_t lineStart = lineFirst_[li];
      const size_t lineEnd = (li + 1 < lineFirst_.size()) ? lineFirst_[li + 1] : words_.size();
      if (wordIdx >= lineStart && wordIdx < lineEnd) {
        return static_cast<int>(li);
      }
    }
    return -1;
  };
  const int firstLine = lineForWord(std::min(lo, words_.size() - 1));
  const int lastLine = lineForWord(std::min(hi, words_.size() - 1));
  if (firstLine < 0 || lastLine < 0) {
    return {};
  }
  const size_t startWord = lineFirst_[static_cast<size_t>(firstLine)];
  const size_t endWord = (static_cast<size_t>(lastLine) + 1 < lineFirst_.size())
                             ? lineFirst_[static_cast<size_t>(lastLine) + 1]
                             : words_.size();
  std::string out;
  for (size_t i = startWord; i < endWord && i < words_.size(); ++i) {
    if (!out.empty()) {
      out += ' ';
    }
    out += words_[i].text;
  }
  return out;
}

void EpubAnnotationUi::drawLatticeHighlightRect(EpubActivity& act, const int x, const int y, const int width,
                                                const int height) {
  if (width <= 0 || height <= 0) {
    return;
  }
  const int topY = std::max(0, y);
  // Two offset lattice passes (even/even + odd/odd) = a ~50% checkerboard. The single 25% pass was too
  // pale on the 194 PPI panel; 50% reads as a solid light-gray band (the classic e-reader highlight)
  // while the ink glyphs stay legible on top.
  act.renderer.ui.fillSparseInkLatticeInRect(x, topY, width, height, kHighlightLatticeStepPx);
  act.renderer.ui.fillSparseInkLatticeInRect(x + 1, topY + 1, width, height, kHighlightLatticeStepPx);
}

void EpubAnnotationUi::drawLatticeHighlightForWordIndexRange(EpubActivity& act, const size_t lo, const size_t hi) {
  if (words_.empty() || lo > hi || hi >= words_.size()) {
    return;
  }
  size_t a = lo;
  while (a <= hi) {
    const int lineY = words_[a].screenY;
    size_t b = a + 1;
    int minX = words_[a].screenX;
    int maxR = words_[a].screenX + words_[a].screenW;
    const int fid0 = words_[a].fontId > 0 ? words_[a].fontId : act.bookSettings.getReaderFontId();
    int rowH = std::max(3, words_[a].screenH > 0 ? words_[a].screenH : act.renderer.text.getLineHeight(fid0));
    while (b <= hi && words_[b].screenY == lineY) {
      minX = std::min(minX, words_[b].screenX);
      maxR = std::max(maxR, words_[b].screenX + words_[b].screenW);
      const int fid = words_[b].fontId > 0 ? words_[b].fontId : act.bookSettings.getReaderFontId();
      const int lh = std::max(3, words_[b].screenH > 0 ? words_[b].screenH : act.renderer.text.getLineHeight(fid));
      rowH = std::max(rowH, lh);
      ++b;
    }
    drawLatticeHighlightRect(act, minX, lineY, std::max(1, maxR - minX), rowH);
    a = b;
  }
}

void EpubAnnotationUi::ensureDiskListLoaded(EpubActivity& act) {
  if (!act.epub || !act.section) {
    return;
  }
  annotations_.ensurePageLoaded(act.epub->getCachePath(), act.currentSpineIndex, act.section->currentPage);
}

void EpubAnnotationUi::updateStoredRangesForPage(const EpubActivity& act) {
  if (!act.section) {
    storedRanges_.clear();
    return;
  }
  EpubAnnotations::mergeStoredRangesForPage(annotations_.records(), act.currentSpineIndex, act.section->currentPage,
                                            words_, storedRanges_);
}

void EpubAnnotationUi::clampSelectionToValidWords() {
  if (words_.empty()) {
    pendingSpans_.clear();
    return;
  }
  const size_t last = words_.size() - 1;
  focus_ = std::min(focus_, last);
  if (selectingStarted_) {
    anchor_ = std::min(anchor_, last);
  }
  for (auto& pr : pendingSpans_) {
    pr.first = std::min(pr.first, last);
    pr.second = std::min(pr.second, last);
    if (pr.first > pr.second) {
      std::swap(pr.first, pr.second);
    }
  }
  pendingSpans_.erase(std::remove_if(pendingSpans_.begin(), pendingSpans_.end(),
                                     [](const std::pair<size_t, size_t>& p) { return p.first > p.second; }),
                      pendingSpans_.end());
}

void EpubAnnotationUi::prepareWordGeometry(EpubActivity& act) {
  if (!act.section || !act.epub) {
    return;
  }
  ensureDiskListLoaded(act);
  const ViewportInfo info = act.calculateViewport();
  const int fontId = act.bookSettings.getReaderFontId();
  const int headerFontId = FontManager::getNextFont(fontId);
  const int mt = info.totalMarginTop;
  const int ml = info.totalMarginLeft;

  const bool wordIndexCacheHit = wordIndexCacheSpine_ == act.currentSpineIndex &&
                                 wordIndexCachePage_ == act.section->currentPage && wordIndexCacheFontId_ == fontId &&
                                 wordIndexCacheHeaderFontId_ == headerFontId && wordIndexCacheMarginL_ == ml &&
                                 wordIndexCacheMarginT_ == mt;

  const bool anyWordText =
      std::any_of(words_.begin(), words_.end(), [](const PageWordHit& w) { return !w.text.empty(); });

  // Reading mode may build the index with omitStoredWordStrings (no per-word strings). Annotation needs strings for
  // extractRangeText / save — force rebuild when the cache has words but no text.
  if (wordIndexCacheHit && !words_.empty() && anyWordText) {
    storedRanges_.clear();
    focus_ = std::min(focus_, words_.size() - 1);
    if (selectingStarted_) {
      anchor_ = std::min(anchor_, words_.size() - 1);
    }
    return;
  }

  storedRanges_.clear();
  auto page = act.section->loadPageFromSectionFile();
  if (!page) {
    words_.clear();
    lineFirst_.clear();
    return;
  }
  constexpr bool omitStoredWordStrings = false;
  buildPageWordIndex(*page, act.renderer, fontId, headerFontId, ml, mt, words_, &lineFirst_, omitStoredWordStrings);
  setWordIndexCache(act.currentSpineIndex, act.section->currentPage, fontId, headerFontId, ml, mt);
}

void EpubAnnotationUi::captureFramebuffer(EpubActivity& act) {
  for (auto& ch : captureChunks_) {
    ch.reset();
  }
  captureMonolithic_.reset();
  captureUsesMonolithic_ = false;
  captureBytes_ = 0;
  captureValid_ = false;

  // Free GfxRenderer's grayscale/BW-shadow chunks (~48KB) if a prior path left them allocated — otherwise capture often
  // fails trying to duplicate the framebuffer while heap is still holding that copy.
  act.renderer.resetTransientReaderState();

  uint8_t* fb = act.renderer.getFrameBuffer();
  const size_t n = act.renderer.getBufferSize();
  if (!fb || n == 0) {
    return;
  }

  const size_t chunkCount = (n + kCaptureChunkBytes - 1) / kCaptureChunkBytes;
  captureChunks_.resize(chunkCount);

  bool chunkedOk = true;
  for (size_t i = 0; i < chunkCount; ++i) {
    const size_t offset = i * kCaptureChunkBytes;
    const size_t chunkBytes = std::min(kCaptureChunkBytes, n - offset);
    uint8_t* const buf = new (std::nothrow) uint8_t[chunkBytes];
    if (!buf) {
      chunkedOk = false;
      for (size_t j = 0; j < i; ++j) {
        captureChunks_[j].reset();
      }
      break;
    }
    memcpy(buf, fb + offset, chunkBytes);
    captureChunks_[i].reset(buf);
  }

  if (chunkedOk) {
    captureBytes_ = n;
    captureValid_ = true;
    return;
  }

  captureMonolithic_.reset(new (std::nothrow) uint8_t[n]);
  if (!captureMonolithic_) {
    return;
  }
  memcpy(captureMonolithic_.get(), fb, n);
  captureUsesMonolithic_ = true;
  captureBytes_ = n;
  captureValid_ = true;
}

void EpubAnnotationUi::repaint(EpubActivity& act) {
  if (!mode_) {
    return;
  }
  const size_t n = act.renderer.getBufferSize();
  if (!captureValid_ || captureBytes_ != n) {
    act.renderScreen(true);
    return;
  }
  uint8_t* fb = act.renderer.getFrameBuffer();
  if (!fb) {
    act.renderScreen(true);
    return;
  }
  act.renderer.setRenderMode(GfxRenderer::BW);
  if (captureUsesMonolithic_) {
    if (!captureMonolithic_) {
      act.renderScreen(true);
      return;
    }
    memcpy(fb, captureMonolithic_.get(), n);
  } else {
    const size_t chunkCount = (n + kCaptureChunkBytes - 1) / kCaptureChunkBytes;
    if (captureChunks_.size() != chunkCount) {
      act.renderScreen(true);
      return;
    }
    for (size_t i = 0; i < chunkCount; ++i) {
      const size_t offset = i * kCaptureChunkBytes;
      const size_t chunkBytes = std::min(kCaptureChunkBytes, n - offset);
      if (!captureChunks_[i]) {
        act.renderScreen(true);
        return;
      }
      memcpy(fb + offset, captureChunks_[i].get(), chunkBytes);
    }
  }
  drawUiOverlay(act);
}

void EpubAnnotationUi::drawStoredOverlay(EpubActivity& act) {
  if (storedRanges_.empty()) {
    return;
  }
  for (const auto& pr : storedRanges_) {
    drawLatticeHighlightForWordIndexRange(act, pr.first, pr.second);
  }
  act.renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void EpubAnnotationUi::drawHighlights(EpubActivity& act) {
  if (!mode_ || words_.empty()) {
    return;
  }
  for (const auto& pr : pendingSpans_) {
    if (pr.first < words_.size() && pr.second < words_.size() && pr.first <= pr.second) {
      drawLatticeHighlightForWordIndexRange(act, pr.first, pr.second);
    }
  }
  if (selectingStarted_) {
    const size_t lo = std::min(anchor_, focus_);
    const size_t hi = std::max(anchor_, focus_);
    drawLatticeHighlightForWordIndexRange(act, lo, hi);
    return;
  }
  if (focus_ < words_.size()) {
    drawLatticeHighlightForWordIndexRange(act, focus_, focus_);
  }
}

void EpubAnnotationUi::drawUiOverlay(EpubActivity& act) {
  if (!mode_ || suppressOverlayDraw_) {
    return;
  }
  if (deletePopupActive_) {
    drawDeletePopup(act);
    act.renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }
  const GfxRenderer::Orientation o = act.renderer.getOrientation();
  drawHighlights(act);
  act.renderer.setOrientation(GfxRenderer::Portrait);
  const char* backHint = hasSaveableContent() ? "Save" : "Exit";
  const char* mid = selectingStarted_ ? "Stop" : "Start";
  const auto labels = act.mappedInput.mapLabels(backHint, mid, "Prev", "Next");
  act.renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  act.renderer.ui.sideButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, "Reset", "Up", "Down");
  act.renderer.setOrientation(o);
  act.renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void EpubAnnotationUi::moveFocusWord(const int delta) {
  if (words_.empty()) {
    return;
  }
  if (delta < 0) {
    if (focus_ > 0) {
      focus_--;
    }
    return;
  }
  if (focus_ + 1 < words_.size()) {
    focus_++;
  }
}

size_t EpubAnnotationUi::wordIndexAt(const int x, const int y) const {
  // Exact hit first (expanded a bit for fat fingers), then nearest by row/column.
  size_t best = SIZE_MAX;
  int bestDist = INT_MAX;
  for (size_t i = 0; i < words_.size(); ++i) {
    const PageWordHit& w = words_[i];
    const int pad = 6;
    const bool hit = x >= w.screenX - pad && x < w.screenX + w.screenW + pad && y >= w.screenY - pad &&
                     y < w.screenY + w.screenH + pad;
    if (hit) {
      return i;
    }
    // Nearest fallback: distance in (row-major) y then x.
    const int cy = w.screenY + w.screenH / 2;
    const int cx = w.screenX + w.screenW / 2;
    const int d = abs(y - cy) * 64 + abs(x - cx);
    if (d < bestDist) {
      bestDist = d;
      best = i;
    }
  }
  return best;
}

void EpubAnnotationUi::enterAtTouch(EpubActivity& act, const int x, const int y) {
  if (mode_) {
    return;
  }
  enter(act);
  if (words_.empty()) {
    return;
  }
  const size_t idx = wordIndexAt(x, y);
  focus_ = (idx == SIZE_MAX) ? 0 : idx;
  anchor_ = focus_;
  selectingStarted_ = true;
  act.updateRequired = true;
}

bool EpubAnnotationUi::tryOpenHighlightDeleteAt(EpubActivity& act, const int x, const int y) {
  if (mode_ || deletePopupActive_ || !act.section || !act.epub) {
    return false;
  }
  // Build the word index and capture the page (same prep as highlight mode), then check whether the
  // long-pressed word sits inside an already-saved highlight. If it does, show the delete popup.
  enter(act);
  if (words_.empty()) {
    exit(act);
    return false;
  }
  updateStoredRangesForPage(act);
  const size_t idx = wordIndexAt(x, y);
  if (idx == SIZE_MAX) {
    exit(act);
    return false;
  }
  for (const auto& pr : storedRanges_) {
    if (idx >= pr.first && idx <= pr.second) {
      openDeletePopup(act, pr.first, pr.second);
      return true;
    }
  }
  exit(act);
  return false;
}

void EpubAnnotationUi::openDeletePopup(EpubActivity& act, const size_t lo, const size_t hi) {
  deleteRangeLo_ = lo;
  deleteRangeHi_ = hi;
  deleteText_ = extractRangeText(lo, hi);
  if (deleteText_.empty()) {
    exit(act);
    return;
  }
  deletePopupActive_ = true;
  act.updateRequired = true;  // loop() repaints the captured page + popup overlay.
}

void EpubAnnotationUi::dismissDeletePopup(EpubActivity& act) {
  deletePopupActive_ = false;
  deleteText_.clear();
  exit(act);
}

void EpubAnnotationUi::confirmDeleteHighlight(EpubActivity& act) {
  if (!act.epub || !act.section) {
    dismissDeletePopup(act);
    return;
  }
  const std::string text = deleteText_;
  deletePopupActive_ = false;
  deleteText_.clear();

  // 1) Remove the ANN3 shard record(s) for this page so the highlight disappears from the book.
  annotations_.removeHighlightOnPage(act.epub->getCachePath(), act.currentSpineIndex, act.section->currentPage,
                                     text);

  // 2) Remove the quote from the derived /highlights JSON and the fork's *_pages.json master so it does
  //    not resurrect in the Quotes browser / next import. Title candidates mirror the import matching.
  const std::vector<std::string> candidates =
      HighlightPersistence::bookTitleCandidates(act.epub->getTitle(), act.epub->getPath());
  HighlightPersistence::deleteHighlight(candidates, act.getCurrentChapterTitle(),
                                        std::to_string(act.currentSpineIndex), text);

  exit(act);
  act.renderScreen(true);
}

void EpubAnnotationUi::drawDeletePopup(EpubActivity& act) {
  const int sw = act.renderer.getScreenWidth();
  const int sh = act.renderer.getScreenHeight();
  deletePopupW_ = std::min(sw - 40, kDeletePopupW);
  deletePopupH_ = kDeletePopupH;
  deletePopupX_ = (sw - deletePopupW_) / 2;
  deletePopupY_ = (sh - deletePopupH_) / 2;

  // Paper card with a rounded ink outline (same visual language as QuotesActivity's confirm overlay).
  act.renderer.rectangle.fill(deletePopupX_, deletePopupY_, deletePopupW_, deletePopupH_, false);
  act.renderer.rectangle.render(deletePopupX_, deletePopupY_, deletePopupW_, deletePopupH_, true, true);

  act.renderer.text.centered(kDeletePopupTitleFont, deletePopupY_ + 22, "Delete highlight?", true,
                             EpdFontFamily::BOLD);

  // Quote text snippet, up to two truncated lines.
  std::string snippet = deleteText_;
  const int textW = deletePopupW_ - 48;
  int ty = deletePopupY_ + 52;
  const int lh = act.renderer.text.getLineHeight(kDeletePopupFont);
  for (int line = 0; line < 2 && !snippet.empty(); ++line) {
    const std::string l = act.renderer.text.truncate(kDeletePopupFont, snippet.c_str(), textW);
    if (l.empty()) break;
    act.renderer.text.render(kDeletePopupFont, deletePopupX_ + 24, ty, l.c_str(), true);
    ty += lh;
    if (snippet.size() <= l.size()) {
      snippet.clear();
      break;
    }
    snippet = snippet.substr(l.size());
  }

  // Buttons: Cancel (outlined) / Delete (filled).
  const int btnY = deletePopupY_ + deletePopupH_ - kDeletePopupBtnH - 22;
  const int btnW = (deletePopupW_ - 24 * 2 - 12) / 2;
  deleteNoX_ = deletePopupX_ + 24;
  deleteNoY_ = btnY;
  deleteNoW_ = btnW;
  deleteNoH_ = kDeletePopupBtnH;
  deleteYesX_ = deleteNoX_ + btnW + 12;
  deleteYesY_ = btnY;
  deleteYesW_ = btnW;
  deleteYesH_ = kDeletePopupBtnH;

  act.renderer.rectangle.fill(deleteNoX_, deleteNoY_, deleteNoW_, deleteNoH_, false, true);
  act.renderer.rectangle.render(deleteNoX_, deleteNoY_, deleteNoW_, deleteNoH_, true, true);
  act.renderer.rectangle.fill(deleteYesX_, deleteYesY_, deleteYesW_, deleteYesH_, true, true);
  const int nlw = act.renderer.text.getWidth(kDeletePopupFont, "Cancel");
  act.renderer.text.render(kDeletePopupFont, deleteNoX_ + (deleteNoW_ - nlw) / 2,
                           deleteNoY_ + (deleteNoH_ - act.renderer.text.getLineHeight(kDeletePopupFont)) / 2, "Cancel",
                           true);
  const int ylw = act.renderer.text.getWidth(kDeletePopupFont, "Delete");
  act.renderer.text.render(kDeletePopupFont, deleteYesX_ + (deleteYesW_ - ylw) / 2,
                           deleteYesY_ + (deleteYesH_ - act.renderer.text.getLineHeight(kDeletePopupFont)) / 2,
                           "Delete", false);
}

bool EpubAnnotationUi::handleTouchDrag(EpubActivity& act, const int x, const int y) {
  if (!mode_ || !selectingStarted_ || words_.empty()) {
    return false;
  }
  const size_t idx = wordIndexAt(x, y);
  if (idx != SIZE_MAX && idx != focus_) {
    focus_ = idx;
    clampSelectionToValidWords();
    repaint(act);
  }
  return true;
}

bool EpubAnnotationUi::handleTouchTap(EpubActivity& act, const int x, const int y) {
  if (deletePopupActive_) {
    if (x >= deleteYesX_ && x < deleteYesX_ + deleteYesW_ && y >= deleteYesY_ && y < deleteYesY_ + deleteYesH_) {
      confirmDeleteHighlight(act);
    } else if (x >= deleteNoX_ && x < deleteNoX_ + deleteNoW_ && y >= deleteNoY_ && y < deleteNoY_ + deleteNoH_) {
      dismissDeletePopup(act);
    } else if (x < deletePopupX_ || x >= deletePopupX_ + deletePopupW_ || y < deletePopupY_ ||
               y >= deletePopupY_ + deletePopupH_) {
      // Tap outside the popup dismisses it.
      dismissDeletePopup(act);
    }
    return true;
  }
  if (!mode_) {
    return false;
  }
  // Start a new selection at the tapped word, or commit the one in progress. Tapping while selecting
  // finalizes it (drag-then-release and drag-then-tap both commit; matches the old T5S3 reader's
  // tap-to-finalize instead of the confusing Start/Stop cycle).
  if (!selectingStarted_) {
    const size_t idx = wordIndexAt(x, y);
    focus_ = (idx == SIZE_MAX) ? 0 : idx;
    selectingStarted_ = true;
    anchor_ = focus_;
    act.updateRequired = true;
    return true;
  }
  const size_t lo = std::min(anchor_, focus_);
  const size_t hi = std::max(anchor_, focus_);
  if (!words_.empty() && lo <= hi) {
    pendingSpans_.push_back({lo, std::min(hi, words_.size() - 1)});
  }
  saveToStorage(act);  // pushes the span above and exits
  return true;
}

void EpubAnnotationUi::moveFocusLine(const int delta) {
  if (lineFirst_.empty() || words_.empty()) {
    return;
  }
  size_t lineIdx = 0;
  for (size_t i = 0; i < lineFirst_.size(); ++i) {
    const size_t start = lineFirst_[i];
    const size_t end = (i + 1 < lineFirst_.size()) ? lineFirst_[i + 1] : words_.size();
    if (focus_ >= start && focus_ < end) {
      lineIdx = i;
      break;
    }
  }
  if (delta < 0) {
    if (lineIdx == 0) {
      return;
    }
    lineIdx--;
    focus_ = lineFirst_[lineIdx];
  } else {
    if (lineIdx + 1 >= lineFirst_.size()) {
      return;
    }
    lineIdx++;
    focus_ = lineFirst_[lineIdx];
  }
}

void EpubAnnotationUi::handleInput(EpubActivity& act) {
  const MappedInputManager& m = act.mappedInput;

  if (deletePopupActive_) {
    // Modal popup: Back cancels; all other buttons are ignored while it is up.
    if (m.wasReleased(MappedInputManager::Button::Back)) {
      dismissDeletePopup(act);
    }
    return;
  }

  if (m.wasReleased(MappedInputManager::Button::Power)) {
    const unsigned long ht = m.getHeldTime();
    if (ht < 600) {
      const bool hadSavedFile =
          act.epub && act.section &&
          annotations_.pageShardExists(act.epub->getCachePath(), act.currentSpineIndex, act.section->currentPage);
      if (hadSavedFile) {
        clearAllStoredHighlightsOnCurrentPage(act);
      } else {
        resetSelectionToStart(act);
      }
      return;
    }
  }
  if (m.wasReleased(MappedInputManager::Button::Back)) {
    if (hasSaveableContent()) {
      saveToStorage(act);
    } else {
      exit(act);
    }
    act.startPageTimer();
    return;
  }
  if (m.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!selectingStarted_) {
      selectingStarted_ = true;
      anchor_ = focus_;
    } else {
      const size_t lo = std::min(anchor_, focus_);
      const size_t hi = std::max(anchor_, focus_);
      if (!words_.empty() && lo <= hi) {
        pendingSpans_.push_back({lo, std::min(hi, words_.size() - 1)});
      }
      selectingStarted_ = false;
    }
    act.updateRequired = true;
    return;
  }
  if (tryNavigationHoldRepeat(act)) {
    return;
  }
}

void EpubAnnotationUi::saveToStorage(EpubActivity& act) {
  std::vector<std::pair<size_t, size_t>> spans = pendingSpans_;
  if (selectingStarted_ && !words_.empty()) {
    const size_t lo = std::min(anchor_, focus_);
    const size_t hi = std::max(anchor_, focus_);
    if (lo <= hi) {
      spans.push_back({lo, std::min(hi, words_.size() - 1)});
    }
  }
  normalizeSpans(spans);
  if (spans.empty()) {
    act.readerPopup("Nothing to save");
    return;
  }

  if (!act.section) {
    act.readerPopup("Could not save");
    exit(act);
    return;
  }

  const std::string cachePath = act.epub->getCachePath();
  const uint32_t ts = static_cast<uint32_t>(time(nullptr));
  bool anyOk = false;

  for (const auto& sp : spans) {
    const std::string seg = extractRangeText(sp.first, sp.second);
    if (seg.empty()) {
      continue;
    }
    EpubAnnotationRecord neu{};
    neu.timestamp = ts;
    neu.text = seg;
    {
      const uint16_t s = static_cast<uint16_t>(act.currentSpineIndex);
      const uint16_t p = static_cast<uint16_t>(act.section->currentPage);
      neu.startSpine = s;
      neu.startPage = p;
      neu.endSpine = s;
      neu.endPage = p;
    }
    neu.pageWordLo = static_cast<uint16_t>(sp.first);
    neu.pageWordHi = static_cast<uint16_t>(sp.second);
    neu.startPageWordLo = EpubAnnotations::kWildcard;
    neu.startPageWordHi = EpubAnnotations::kWildcard;

    if (annotations_.appendHighlight(cachePath, act.epub->getSpineItemsCount(), neu, act.currentSpineIndex,
                                     act.section->currentPage)) {
      anyOk = true;
      // Also persist into the T5S3 reader's /highlights/*.json quotes system so the
      // home-screen latest-highlight banner and Quotes browser pick it up. Books without an
      // OPF <dc:title> fall back to the sanitized file-name stem so the JSON is still written.
      std::string bookTitle = act.epub->getTitle();
      if (bookTitle.empty()) {
        bookTitle = HighlightPersistence::defaultTitleForPath(act.epub->getPath());
      }
      HighlightPersistence::saveHighlight(bookTitle, act.epub->getPath(), act.getCurrentChapterTitle(), seg,
                                          buildParagraphTextForRange(sp.first, sp.second));
    }
  }

  if (!anyOk) {
    act.readerPopup("Could not save");
    exit(act);
    return;
  }

  annotations_.ensurePageLoaded(cachePath, act.currentSpineIndex, act.section->currentPage);

  act.readerPopup(spans.size() > 1 ? "Highlights saved" : "Highlight saved");
  exit(act);
}

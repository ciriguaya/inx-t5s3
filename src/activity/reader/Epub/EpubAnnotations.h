#pragma once

#include <Epub/PageWordIndex.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../HighlightEntry.h"

/** One highlight row (ANN3 on disk). */
struct EpubAnnotationRecord {
  uint32_t timestamp = 0;
  std::string text;
  uint16_t startSpine = 0xFFFF;
  uint16_t startPage = 0;
  uint16_t endSpine = 0xFFFF;
  uint16_t endPage = 0xFFFF;
  uint16_t pageWordLo = 0xFFFF;
  uint16_t pageWordHi = 0xFFFF;
  uint16_t startPageWordLo = 0xFFFF;
  uint16_t startPageWordHi = 0xFFFF;
};

/**
 * Per-page highlight storage: one ANN3 file per (spine, page). Load/save for the visible page is O(1)
 * (bounded small file); saving a multi-page span touches O(span pages) shard files only.
 */
class EpubAnnotations {
 public:
  static constexpr uint16_t kWildcard = 0xFFFF;
  static constexpr int kMaxPerPage = 100;
  static constexpr const char* kSubdir = "ann";

  void clearSession();

  /** Load shard for (spine, page) if not already cached. */
  void ensurePageLoaded(const std::string& cachePath, int spine, int page);

  /** Deletes the ANN3 shard for this page and clears the in-memory cache when it matches. */
  void clearPageShard(const std::string& cachePath, int spine, int page);

  /** Removes the record(s) whose stored text matches `text` from this page's shard (word-exact match
   *  preferred, normalized-text match as fallback). Rewrites the shard, or deletes it when empty.
   *  Returns true if at least one record was removed. */
  bool removeHighlightOnPage(const std::string& cachePath, int spine, int page, const std::string& text);

  /** Whether the on-disk shard exists for this page (ground truth for saved highlights). */
  bool pageShardExists(const std::string& cachePath, int spine, int page) const;

  const std::vector<EpubAnnotationRecord>& records() const { return records_; }

  /** Enumerate touched pages, append record to each shard. Returns false if no write succeeded. */
  bool appendHighlight(const std::string& cachePath, int spineItemsCount, const EpubAnnotationRecord& rec,
                       int fallbackSpine, int fallbackPage);

  static bool recordTouchesPage(const EpubAnnotationRecord& r, int currentSpine, int currentPage);

  static void mergeStoredRangesForPage(const std::vector<EpubAnnotationRecord>& diskRecs, int currentSpine,
                                       int currentPage, const std::vector<PageWordHit>& annWords,
                                       std::vector<std::pair<size_t, size_t>>& outMerged);

  /**
   * Finds the current-pagination (spine, page) of a stored highlight phrase by scanning the book's
   * ANN3 shards. `spineHint` >= 0 restricts the scan to that spine's shards (fast path for quotes
   * whose stored chapter is the spine number); pass -1 to scan everything. Returns false when the
   * phrase is not found anywhere.
   */
  static bool findQuoteLocation(const std::string& cachePath, const std::string& text, int spineHint,
                                int* outSpine, int* outPage);

  /**
   * Searches a spine's cached pages (current pagination) for a stored phrase and returns the first
   * page that contains it, or -1. Used to land on the exact quote page when no ANN3 shard exists yet
   * (a fork quote on a spine that was never built).
   */
  static int findPageWithText(const std::string& cachePath, int spineIndex, int pageCount, GfxRenderer& renderer,
                              int bodyFontId, int headerFontId, int marginLeft, int marginTop,
                              const std::string& text);

  /** Called right after a spine's section file is freshly (re)built - e.g. a font/size/margin change
   *  repaginated it. A highlight's stored page number and word-index are both tied to one specific
   *  pagination; when that changes, the phrase can land on a different page entirely, not just a different
   *  word index on the same page. Re-locates every single-page annotation for this spine by searching the
   *  new pages for its stored phrase and rewrites the ann/ shards to match. No-ops (cheaply) for spines with
   *  no existing annotations. Multi-page-spanning highlights are left at their stored position (best
   *  effort) rather than risk mis-splitting them. */
  static void migrateSpineAnnotations(const std::string& cachePath, int spineIndex, int newPageCount,
                                      GfxRenderer& renderer, int bodyFontId, int headerFontId, int marginLeft,
                                      int marginTop);

  /**
   * Imports quotes saved by the T5S3 fork's /highlights system (quotes whose chapter field equals
   * `spineIndex`) into this spine's ANN3 shards by re-locating each quote's phrase on the freshly
   * built pages. Skips texts already present in this spine's shards, so re-opening a book never
   * duplicates. This is what makes pre-existing CrossPoint highlights render in the book.
   */
  static void importQuoteHighlights(const std::string& cachePath, int spineIndex, int pageCount,
                                    GfxRenderer& renderer, int bodyFontId, int headerFontId, int marginLeft,
                                    int marginTop, const std::vector<HighlightEntry>& quotes);

 private:
  static bool tryAppendPreciseHighlightRanges(const EpubAnnotationRecord& r, int cs, int cp,
                                              const std::vector<PageWordHit>& annWords,
                                              std::vector<std::pair<size_t, size_t>>& raw);

  std::vector<EpubAnnotationRecord> records_;
  int cacheSpine_ = -1;
  int cachePage_ = -1;
};

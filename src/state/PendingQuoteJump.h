#pragma once

/**
 * @file PendingQuoteJump.h
 * @brief One-shot "open this quote in the book" target shared by the Quotes browser and the reader.
 *
 * The Quotes browser resolves a quote's current spine/page (from the book's ANN3 annotation shards,
 * which hold the re-located position on the current pagination) and sets this global. The next time
 * that book is opened, EpubActivity consumes the jump and lands on the exact page. When the shard
 * does not exist yet (a fork quote on a never-built spine), the browser falls back to the chapter's
 * first page and passes the quote text so the reader can search for the exact page after the spine
 * is built.
 */

#include <string>

struct PendingQuoteJump {
  bool active = false;
  bool exact = false;  ///< true when spine+page came from an ANN3 shard (current pagination)
  std::string path;
  int spine = -1;
  int page = 0;
  std::string text;  ///< quote text, used to refine the exact page when `exact` is false

  void set(const std::string& bookPath, const int spineIndex, const int pageNumber, const bool exactPage,
           const std::string& quoteText = "") {
    active = true;
    exact = exactPage;
    path = bookPath;
    spine = spineIndex;
    page = pageNumber;
    text = quoteText;
  }

  /** Consumes the jump when it targets `bookPath`; fills outSpine/outPage and returns true. */
  bool consumeIfPathMatches(const std::string& bookPath, int& outSpine, int& outPage) {
    if (!active || path != bookPath) {
      return false;
    }
    active = false;
    outSpine = spine;
    outPage = page;
    return true;
  }
};

extern PendingQuoteJump pendingQuoteJump;

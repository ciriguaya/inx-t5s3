#pragma once

#include <HalStorage.h>

#include <string>
#include <vector>

#include "HighlightEntry.h"

// Saves and loads highlights to/from JSON files on the SD card.
// Highlights are stored in /highlights/<book-title-sanitized>.json
class HighlightPersistence {
 public:
  // Save a new highlight entry for the given book.
  // Book title is used as a human-readable filename component.
  static bool saveHighlight(const std::string& bookTitle, const std::string& bookPath,
                            const std::string& chapter, const std::string& selectedText,
                            const std::string& paragraphText = "");

  // Load all highlights for a given book title.
  // Returns empty vector on failure or if no highlights exist.
  static std::vector<HighlightEntry> loadHighlights(const std::string& bookTitle);

  // Load highlights from every /highlights/*.json file on the card.
  static std::vector<HighlightEntry> loadAllHighlights();

  // Load the most recently saved highlight (highest sequence), if any.
  static bool loadLatestHighlight(HighlightEntry& out);

  // Minimal JSON building without an external library.
  static std::string buildHighlightJson(const HighlightEntry& entry);
  static std::string escapeJsonString(const std::string& str);
  static std::string sanitizeFilename(const std::string& title);
  static unsigned long getNextSequence();

  // Scan from a '{' and return a pointer to the matching '}' that closes the
  // JSON object, respecting string literals (so braces inside text values are
  // not mistaken for object boundaries). Returns nullptr if no closing brace
  // is found before the end of the string.
  static const char* findJsonObjectEnd(const char* objStart);

  // Reverse of escapeJsonString for values read back from disk: converts
  // \" -> " and \\ -> \. Literal newlines (written by older firmware) are
  // left untouched.
  static std::string unescapeJsonValue(const std::string& value);

  // Read the current global sequence counter without writing (unlike
  // getNextSequence, which also increments and persists it).
  static unsigned long readSequence();

  // Persist a new sequence value (used after assigning a batch of sequences).
  static void setSequence(unsigned long value);

  // Sanitized, lowercased book-identity candidates for a book path/title, matching how the fork and
  // this reader name the /highlights files: OPF title, file name stem, and stem with a trailing
  // "(N)" disambiguator stripped ("Popol Vuh(1)" -> "Popol Vuh").
  static std::vector<std::string> bookTitleCandidates(const std::string& title, const std::string& path);

  // Fallback book title for books with no <dc:title> in the OPF (e.g. "Popol Vuh(1).epub"): the
  // sanitized file-name stem, so /highlights/<stem>.json still gets written and the Quotes browser
  // and home banner can pick up new highlights from title-less books.
  static std::string defaultTitleForPath(const std::string& path);

  // Removes a highlight (by normalized text; chapter/spine disambiguators) from BOTH the derived
  // /highlights/<title>.json and the fork's <title>_pages.json master, so it disappears from the
  // Quotes browser and can never be re-imported. `titleCandidates` come from bookTitleCandidates().
  // Returns true if at least one store changed.
  static bool deleteHighlight(const std::vector<std::string>& titleCandidates, const std::string& chapterTitle,
                              const std::string& spineIndex, const std::string& text);

  // Removes entries whose stored text matches from a single fork <stem>_pages.json master (used by
  // QuotesActivity's delete, which knows the exact book but not the spine).
  static void deleteFromPagesMaster(const std::string& stem, const std::string& text);

  // Patches the derived /highlights/<title>.json and <title>_pages.json master of every candidate
  // title with the given book path (added as a "path" field when missing/different), so QuotesActivity
  // can resolve where a quote lives in the book. Called by the reader on import.
  static void ensureQuoteBookPaths(const std::vector<std::string>& titleCandidates, const std::string& path);

 private:
  static std::string getHighlightsDir();
  static std::string getFilePath(const std::string& bookTitle);
};

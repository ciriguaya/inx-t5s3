#pragma once

#include <cstdint>
#include <string>

// A single highlight entry saved to the SD card.
struct HighlightEntry {
  std::string bookTitle;
  std::string bookPath;      // full path to the book file
  std::string chapter;       // spine index as string or chapter title fragment
  std::string selectedText;  // the highlighted text content
  std::string paragraphText; // full paragraph containing the highlighted text for context
  unsigned long timestamp = 0;  // millis() at time of highlight creation
  unsigned long sequence = 0;   // global persistent sequence number for correct ordering
};

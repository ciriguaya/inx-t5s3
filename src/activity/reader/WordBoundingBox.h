#pragma once

#include <cstdint>
#include <string>

// A bounding box for a single word on the current page, in logical screen coordinates.
struct WordBoundingBox {
  int16_t x = 0;            // left edge
  int16_t y = 0;            // top edge
  int16_t width = 0;        // pixel width of the word
  int16_t height = 0;       // line height
  uint16_t blockIndex = 0;  // index into HighlightManager's blockParagraphTexts for paragraph extraction
  std::string word;
};

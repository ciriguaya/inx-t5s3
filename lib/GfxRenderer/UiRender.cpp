#include "UiRender.h"

#include <algorithm>

#include "GfxRenderer.h"

void UiRender::buttonHints(const int fontId, const char* btn1, const char* btn2, const char* btn3,
                           const char* btn4, const int topY) const {
  // T5S3 is touch-first: the physical buttons are the power button and one side button, so the Xteink
  // button-reference indicators don't apply. Drawing nothing keeps screens clean and saves a refresh.
  (void)fontId;
  (void)btn1;
  (void)btn2;
  (void)btn3;
  (void)btn4;
  (void)topY;
}

void UiRender::sideButtonHints(const int fontId, const char* powerBtn, const char* topBtn,
                               const char* bottomBtn) const {
  // Touch-first T5S3: no physical side buttons to reference (see buttonHints()).
  (void)fontId;
  (void)powerBtn;
  (void)topBtn;
  (void)bottomBtn;
}

void UiRender::dottedRect(const int x, const int y, const int width, const int height, const bool state) const {
  gfx.rectangle.dotted(x, y, width, height, state);
}

void UiRender::fillSparseInkLatticeInRect(const int x, const int y, const int width, const int height,
                                          const int latticeStep) const {
  if (width <= 0 || height <= 0) {
    return;
  }
  int step = latticeStep;
  if (step < 2) {
    step = 2;
  }
  const bool pow2 = (step & (step - 1)) == 0;
  const int sw = gfx.getScreenWidth();
  const int sh = gfx.getScreenHeight();
  const int x1 = std::max(0, x);
  const int y1 = std::max(0, y);
  const int x2 = std::min(sw, x + width);
  const int y2 = std::min(sh, y + height);
  if (pow2) {
    const int mask = step - 1;
    for (int py = (y1 + step - 1) & ~mask; py < y2; py += step) {
      for (int px = (x1 + step - 1) & ~mask; px < x2; px += step) {
        gfx.drawPixel(px, py, true);
      }
    }
    return;
  }
  for (int py = y1; py < y2; py += step) {
    for (int px = x1; px < x2; px += step) {
      gfx.drawPixel(px, py, true);
    }
  }
}

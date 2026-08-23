/**
 * @file KeyboardEntryActivity.cpp
 * @brief Definitions for KeyboardEntryActivity.
 */

#include "KeyboardEntryActivity.h"

#include <cstring>

#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
/** Key height - halved from 92 to 46 so the keyboard is compact and the (nearly square) keys look
 *  proportional; 54 px between key centers is still a comfortable touch target. */
constexpr int KEY_HEIGHT = 46;
constexpr int KEY_SPACING = 8;
constexpr int BOTTOM_MARGIN = 44;
constexpr int PAGE_MARGIN = 18;
/** Stack size (bytes) for xTaskCreate; 2048 overflowed with render() + GfxRenderer on ESP32-C3. */
constexpr uint32_t kDisplayTaskStackBytes = 8192;
}  // namespace

const char* const KeyboardEntryActivity::keyboard[NUM_ROWS] = {
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm,.",
    "!?.,'\":;-/",
    "special",
};

const char* const KeyboardEntryActivity::keyboardShift[NUM_ROWS] = {
    "QWERTYUIOP",
    "ASDFGHJKL",
    "ZXCVBNM<>",
    "!?.,'\":;-/",
    "special",
};

const char* const KeyboardEntryActivity::keyboardSymbols[NUM_ROWS] = {
    "1234567890",
    "!@#$%^&*()",
    "-_=+[]{}<>",
    "/\\|~`;:'\"?",
    "special",
};

void KeyboardEntryActivity::taskTrampoline(void* param) {
  auto* self = static_cast<KeyboardEntryActivity*>(param);
  self->displayTaskLoop();
}

void KeyboardEntryActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void KeyboardEntryActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  updateRequired = true;

  xTaskCreate(&KeyboardEntryActivity::taskTrampoline, "KeyboardEntryActivity", kDisplayTaskStackBytes, this, 1,
              &displayTaskHandle);
}

void KeyboardEntryActivity::onExit() {
  Activity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

int KeyboardEntryActivity::getRowLength(const int row) const {
  if (row < 0 || row >= NUM_ROWS) return 0;

  if (row == SPECIAL_ROW) {
    return symbolsPage ? 4 : 5;  // ABC/SPACE/DEL/OK or 123/Aa/SPACE/DEL/OK
  }
  const char* const* layout = symbolsPage ? keyboardSymbols : keyboard;
  const char* rowStr = layout[row];
  return rowStr ? static_cast<int>(strlen(rowStr)) : 0;
}

char KeyboardEntryActivity::getSelectedChar() const {
  const char* const* layout = symbolsPage ? keyboardSymbols : ((shiftActive || capsLockActive) ? keyboardShift : keyboard);

  if (selectedRow < 0 || selectedRow >= NUM_ROWS) return '\0';
  if (selectedCol < 0 || selectedCol >= getRowLength(selectedRow)) return '\0';

  return layout[selectedRow][selectedCol];
}

/** Maps a special-row column index (0-based across the row's keys) to the fixed slot semantics. */
int KeyboardEntryActivity::specialSlotForCol(const int col) const {
  if (symbolsPage) {
    switch (col) {
      case 0:
        return SLOT_TOGGLE;  // ABC
      case 1:
        return SLOT_SPACE;
      case 2:
        return SLOT_DEL;
      default:
        return SLOT_OK;
    }
  }
  switch (col) {
    case 0:
      return SLOT_TOGGLE;  // 123
    case 1:
      return SLOT_SHIFT;
    case 2:
      return SLOT_SPACE;
    case 3:
      return SLOT_DEL;
    default:
      return SLOT_OK;  // col 4
  }
}

void KeyboardEntryActivity::handleKeyPress() {
  if (selectedRow == SPECIAL_ROW) {
    switch (specialSlotForCol(selectedCol)) {
      case SLOT_TOGGLE:
        symbolsPage = !symbolsPage;
        shiftActive = false;
        capsLockActive = false;
        selectedCol = 0;
        break;
      case SLOT_SHIFT:
        if (capsLockActive) {
          capsLockActive = false;
          shiftActive = false;
        } else if (shiftActive) {
          capsLockActive = true;
          shiftActive = false;
        } else {
          shiftActive = true;
        }
        break;
      case SLOT_SPACE:
        if (maxLength == 0 || text.length() < maxLength) {
          text += ' ';
        }
        break;
      case SLOT_DEL:
        if (!text.empty()) {
          text.pop_back();
        }
        break;
      case SLOT_OK:
        if (onComplete) {
          onComplete(text);
        }
        break;
    }
    return;
  }

  const char c = getSelectedChar();
  if (c == '\0') {
    return;
  }

  if (maxLength == 0 || text.length() < maxLength) {
    text += c;

    if (!symbolsPage && shiftActive && !capsLockActive && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
      shiftActive = false;
    }
  }
}

bool KeyboardEntryActivity::onTouchTap(int16_t x, int16_t y) {
  int row = -1;
  int col = -1;
  if (!keyAt(x, y, row, col)) {
    return true;  // Taps outside the keyboard are inert.
  }
  selectedRow = row;
  selectedCol = col;
  handleKeyPress();
  updateRequired = true;
  return true;
}

bool KeyboardEntryActivity::keyAt(int x, int y, int& row, int& col) const {
  const int pageWidth = renderer.getScreenWidth();
  const int keyWidth = (pageWidth - PAGE_MARGIN * 2 - (KEYS_PER_ROW - 1) * KEY_SPACING) / KEYS_PER_ROW;
  const int keyboardAreaHeight = NUM_ROWS * (KEY_HEIGHT + KEY_SPACING);
  const int keyboardStartY = renderer.getScreenHeight() - keyboardAreaHeight - BOTTOM_MARGIN;

  if (y < keyboardStartY) {
    return false;
  }
  row = (y - keyboardStartY) / (KEY_HEIGHT + KEY_SPACING);
  if (row < 0 || row >= NUM_ROWS) {
    return false;
  }
  if (row == SPECIAL_ROW) {
    const int unitW = keyWidth + KEY_SPACING;
    int cursor = 0;
    int nSlots = symbolsPage ? 4 : 5;
    int slotWidths[5] = {0, 0, 0, 0, 0};
    if (symbolsPage) {
      slotWidths[0] = 2;
      slotWidths[1] = 4;
      slotWidths[2] = 2;
      slotWidths[3] = 2;
    } else {
      slotWidths[0] = 2;
      slotWidths[1] = 2;
      slotWidths[2] = 4;
      slotWidths[3] = 1;
      slotWidths[4] = 1;
    }
    const int rowY = keyboardStartY + row * (KEY_HEIGHT + KEY_SPACING);
    if (y >= rowY + KEY_HEIGHT) {
      return false;
    }
    for (int i = 0; i < nSlots; ++i) {
      const int slotX = PAGE_MARGIN + cursor;
      const int slotW = slotWidths[i] * unitW - KEY_SPACING;
      if (x >= slotX && x < slotX + slotW) {
        col = i;
        return true;
      }
      cursor += slotWidths[i] * unitW;
    }
    return false;
  }

  const int rowLength = getRowLength(row);
  const int totalRowWidth = rowLength * keyWidth + (rowLength - 1) * KEY_SPACING;
  const int startX = (pageWidth - totalRowWidth) / 2;
  const int rowY = keyboardStartY + row * (KEY_HEIGHT + KEY_SPACING);
  if (y >= rowY + KEY_HEIGHT) {
    return false;
  }
  for (int c = 0; c < rowLength; ++c) {
    const int keyX = startX + c * (keyWidth + KEY_SPACING);
    if (x >= keyX && x < keyX + keyWidth) {
      col = c;
      return true;
    }
  }
  return false;
}

void KeyboardEntryActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (selectedRow > 0) {
      selectedRow--;
    } else {
      selectedRow = NUM_ROWS - 1;
    }
    const int maxCol = getRowLength(selectedRow) - 1;
    if (selectedCol > maxCol) selectedCol = maxCol;
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (selectedRow < NUM_ROWS - 1) {
      selectedRow++;
    } else {
      selectedRow = 0;
    }
    const int maxCol = getRowLength(selectedRow) - 1;
    if (selectedCol > maxCol) selectedCol = maxCol;
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    const int maxCol = getRowLength(selectedRow) - 1;
    if (selectedCol > 0) {
      selectedCol--;
    } else {
      selectedCol = maxCol;
    }
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    const int maxCol = getRowLength(selectedRow) - 1;
    if (selectedCol < maxCol) {
      selectedCol++;
    } else {
      selectedCol = 0;
    }
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleKeyPress();
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (onCancel) {
      onCancel();
    }
    updateRequired = true;
  }
}

void KeyboardEntryActivity::render() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  constexpr int titleFont = ATKINSON_HYPERLEGIBLE_16_FONT_ID;
  constexpr int inputFont = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
  constexpr int keyFont = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
  constexpr int hintFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;

  // Compact layout: the keyboard anchors to the bottom, and the title + input field sit directly
  // above it (no big dead gap in the middle).
  const int keyboardAreaHeight = NUM_ROWS * (KEY_HEIGHT + KEY_SPACING);
  const int keyboardStartY = pageHeight - keyboardAreaHeight - BOTTOM_MARGIN;

  const int inputX = PAGE_MARGIN;
  const int inputW = pageWidth - PAGE_MARGIN * 2;
  constexpr int inputH = 56;
  const int inputY = keyboardStartY - inputH - 24;

  renderer.text.render(titleFont, PAGE_MARGIN, inputY - 40, title.c_str(), true, EpdFontFamily::BOLD);

  std::string displayText;
  if (isPassword) {
    displayText = std::string(text.length(), '*');
  } else {
    displayText = text;
  }

  displayText += "_";

  renderer.rectangle.render(inputX, inputY, inputW, inputH, true, true);

  std::string inputLine = renderer.text.truncate(inputFont, displayText.c_str(), inputW - 24);
  const int inputTextY = inputY + (inputH - renderer.text.getLineHeight(inputFont)) / 2;
  renderer.text.render(inputFont, inputX + 12, inputTextY, inputLine.c_str(), true);

  if (maxLength > 0) {
    char countText[24];
    snprintf(countText, sizeof(countText), "%u/%u", static_cast<unsigned>(text.length()),
             static_cast<unsigned>(maxLength));
    const int countW = renderer.text.getWidth(hintFont, countText);
    renderer.text.render(hintFont, inputX + inputW - countW - 10, inputY + inputH + 8, countText, true);
  }

  const int keyWidth = (pageWidth - PAGE_MARGIN * 2 - (KEYS_PER_ROW - 1) * KEY_SPACING) / KEYS_PER_ROW;
  const int unitW = keyWidth + KEY_SPACING;

  auto drawKey = [&](const int x, const int y, const int w, const int h, const char* label, const bool selected,
                     const bool emphasized = false) {
    const int labelW =
        renderer.text.getWidth(keyFont, label, emphasized ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    const int labelX = x + (w - labelW) / 2;
    const int labelY = y + (h - renderer.text.getLineHeight(keyFont)) / 2;
    if (selected) {
      renderer.rectangle.fill(x, y, w, h, true, true);
      renderer.text.render(keyFont, labelX, labelY, label, false,
                           emphasized ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      return;
    }

    renderer.rectangle.fill(x, y, w, h, false, true);
    renderer.rectangle.render(x, y, w, h, true, true);
    if (emphasized) {
      renderer.rectangle.render(x + 2, y + 2, w - 4, h - 4, true, true);
    }
    renderer.text.render(keyFont, labelX, labelY, label, true,
                         emphasized ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  };

  for (int row = 0; row < NUM_ROWS; row++) {
    const int rowY = keyboardStartY + row * (KEY_HEIGHT + KEY_SPACING);

    if (row == SPECIAL_ROW) {
      int slotWidths[5] = {0, 0, 0, 0, 0};
      int nSlots = 0;
      const char* labels[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
      if (symbolsPage) {
        slotWidths[0] = 2;
        slotWidths[1] = 4;
        slotWidths[2] = 2;
        slotWidths[3] = 2;
        nSlots = 4;
        labels[0] = "ABC";
        labels[1] = "SPACE";
        labels[2] = "DEL";
        labels[3] = "OK";
      } else {
        slotWidths[0] = 2;
        slotWidths[1] = 2;
        slotWidths[2] = 4;
        slotWidths[3] = 1;
        slotWidths[4] = 1;
        nSlots = 5;
        labels[0] = "123";
        labels[1] = capsLockActive ? "CAPS" : (shiftActive ? "SHIFT" : "Aa");
        labels[2] = "SPACE";
        labels[3] = "DEL";
        labels[4] = "OK";
      }
      int cursor = 0;
      for (int i = 0; i < nSlots; ++i) {
        const int keyX = PAGE_MARGIN + cursor;
        const int keyW = slotWidths[i] * unitW - KEY_SPACING;
        const bool isSelected = (selectedRow == SPECIAL_ROW && selectedCol == i);
        const bool emphasized = (labels[i] && strcmp(labels[i], "OK") == 0) ||
                                (labels[i] && strcmp(labels[i], "SHIFT") == 0) ||
                                (labels[i] && strcmp(labels[i], "CAPS") == 0);
        drawKey(keyX, rowY, keyW, KEY_HEIGHT, labels[i] ? labels[i] : "", isSelected, emphasized);
        cursor += slotWidths[i] * unitW;
      }
    } else {
      const int rowLength = getRowLength(row);
      const int totalRowWidth = rowLength * keyWidth + (rowLength - 1) * KEY_SPACING;
      const int startX = (pageWidth - totalRowWidth) / 2;

      const char* const* layout =
          symbolsPage ? keyboardSymbols : ((shiftActive || capsLockActive) ? keyboardShift : keyboard);

      for (int col = 0; col < rowLength; col++) {
        const char c = layout[row][col];
        char keyLabel[2] = {c, '\0'};

        const int keyX = startX + col * (keyWidth + KEY_SPACING);
        const bool isSelected = row == selectedRow && col == selectedCol;
        drawKey(keyX, rowY, keyWidth, KEY_HEIGHT, keyLabel, isSelected);
      }
    }
  }

  const auto labels = mappedInput.mapLabels("Back", "Select", "Prev", "Next");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_12_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

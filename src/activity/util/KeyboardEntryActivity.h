#pragma once

/**
 * @file KeyboardEntryActivity.h
 * @brief Public interface and types for KeyboardEntryActivity.
 */

#include <GfxRenderer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <utility>

#include "../Activity.h"

/**
 * Reusable keyboard entry activity for text input.
 * Can be started from any activity that needs text entry.
 *
 * Usage:
 *   1. Create a KeyboardEntryActivity instance
 *   2. Set callbacks with setOnComplete() and setOnCancel()
 *   3. Call onEnter() to start the activity
 *   4. Call loop() in your main loop
 *   5. When complete or cancelled, callbacks will be invoked
 *
 * T5S3 (touch-first) additions:
 *   - The keyboard is split into a letters page and a numbers/symbols page; a "123"/"ABC" key on
 *     the bottom row switches between them so each page can use large, touch-friendly keys.
 *   - Tapping a key directly types it (onTouchTap).
 */
class KeyboardEntryActivity : public Activity {
 public:
  using OnCompleteCallback = std::function<void(const std::string&)>;
  using OnCancelCallback = std::function<void()>;

  /**
   * Constructor
   * @param renderer Reference to the GfxRenderer for drawing
   * @param mappedInput Reference to MappedInputManager for handling input
   * @param title Title to display above the keyboard
   * @param initialText Initial text to show in the input field
   * @param startY Y position to start rendering the keyboard
   * @param maxLength Maximum length of input text (0 for unlimited)
   * @param isPassword If true, display asterisks instead of actual characters
   * @param onComplete Callback invoked when input is complete
   * @param onCancel Callback invoked when input is cancelled
   */
  explicit KeyboardEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 std::string title = "Enter Text", std::string initialText = "", const int startY = 10,
                                 const size_t maxLength = 0, const bool isPassword = false,
                                 OnCompleteCallback onComplete = nullptr, OnCancelCallback onCancel = nullptr)
      : Activity("KeyboardEntry", renderer, mappedInput),
        title(std::move(title)),
        text(std::move(initialText)),
        startY(startY),
        maxLength(maxLength),
        isPassword(isPassword),
        onComplete(std::move(onComplete)),
        onCancel(std::move(onCancel)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;

 private:
  std::string title;
  int startY;
  std::string text;
  size_t maxLength;
  bool isPassword;
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;

  int selectedRow = 0;
  int selectedCol = 0;
  bool shiftActive = false;
  bool capsLockActive = false;
  bool symbolsPage = false;

  OnCompleteCallback onComplete;
  OnCancelCallback onCancel;

  static constexpr int NUM_ROWS = 5;
  static constexpr int KEYS_PER_ROW = 10;
  static constexpr int SPECIAL_ROW = 4;
  static const char* const keyboard[NUM_ROWS];         // letters page
  static const char* const keyboardShift[NUM_ROWS];    // letters page with shift/caps
  static const char* const keyboardSymbols[NUM_ROWS];  // numbers + symbols page

  /** Special-row slots (letters page). */
  static constexpr int SLOT_TOGGLE = 0;
  static constexpr int SLOT_SHIFT = 1;
  static constexpr int SLOT_SPACE = 2;
  static constexpr int SLOT_DEL = 3;
  static constexpr int SLOT_OK = 4;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  char getSelectedChar() const;
  void handleKeyPress();
  int getRowLength(int row) const;
  void render() const;
  void renderItemWithSelector(int x, int y, const char* item, bool isSelected) const;

  /** Row/col at logical (x, y), or -1/-1 when outside the keyboard. */
  bool keyAt(int x, int y, int& row, int& col) const;
  /** Maps a special-row column to the fixed slot semantics (see SLOT_*). */
  int specialSlotForCol(int col) const;
};

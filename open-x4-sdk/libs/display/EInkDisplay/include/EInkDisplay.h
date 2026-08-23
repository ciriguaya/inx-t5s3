#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// EInkDisplay — T5S3 backend
//
// This is a drop-in replacement for the open-x4-sdk EInkDisplay used by Inx on
// the Xteink X4/X3. It keeps the exact same public interface so Inx's
// lib/hal/HalDisplay and lib/GfxRenderer compile unchanged, but drives the
// LilyGo T5S3 (ESP32-S3) ED047TC1 e-paper panel through M5GFX instead of the
// X4's controller.
//
// Framebuffer geometry follows the T5S3's physical scan orientation:
//   DISPLAY_WIDTH  = 960 (physical panel columns, one bit per pixel)
//   DISPLAY_HEIGHT = 540 (physical panel rows)
// Inx's GfxRenderer defaults to Portrait, which maps logical (540x960)
// portrait coordinates onto this physical framebuffer — the native T5S3
// reading orientation.
// ---------------------------------------------------------------------------

class EInkDisplay {
 public:
  // Constructor with pin configuration (kept for interface compatibility;
  // the T5S3 uses its own fixed pins via M5GFX).
  EInkDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy);

  // Destructor
  ~EInkDisplay();

  // Refresh modes (guarded to avoid redefinition in test builds)
  enum RefreshMode {
    FULL_REFRESH,  // Full refresh with complete waveform
    HALF_REFRESH,  // Half refresh - balanced quality and speed
    FAST_REFRESH,  // Fast refresh (typical page turn)
    STRONG_FAST_REFRESH
  };

  // Set X3 panel geometry and mode (must be called before begin()). No-op on T5S3.
  void setDisplayX3();

  // Initialize the display hardware and driver
  void begin();

  // Compile-time dimensions (T5S3 physical scan orientation).
  static constexpr uint16_t DISPLAY_WIDTH = 960;
  static constexpr uint16_t DISPLAY_HEIGHT = 540;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;
  static constexpr uint16_t X3_DISPLAY_WIDTH = 792;
  static constexpr uint16_t X3_DISPLAY_HEIGHT = 528;
  static constexpr uint16_t X3_DISPLAY_WIDTH_BYTES = X3_DISPLAY_WIDTH / 8;
  static constexpr uint32_t X3_BUFFER_SIZE = X3_DISPLAY_WIDTH_BYTES * X3_DISPLAY_HEIGHT;
  static constexpr uint32_t MAX_BUFFER_SIZE = BUFFER_SIZE;

  // Runtime dimensions
  uint16_t getDisplayWidth() const { return displayWidth; }
  uint16_t getDisplayHeight() const { return displayHeight; }
  uint16_t getDisplayWidthBytes() const { return displayWidthBytes; }
  uint32_t getBufferSize() const { return bufferSize; }
  bool isX3() const { return false; }

  // Frame buffer operations
  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem = false) const;
  void drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            bool fromProgmem = false) const;
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  void swapBuffers();
#endif
  void setFramebuffer(const uint8_t* bwBuffer) const;

  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);
#endif

  void displayBuffer(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false);
  // Windowed update - display only a rectangular region (implemented as full-frame push).
  void displayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool turnOffScreen = false);
  void displayGrayBuffer(bool turnOffScreen = false, const unsigned char* lutData = nullptr, bool quality = false,
                         bool trackForRevert = true);
  void displayGrayBufferFastQuality(bool turnOffScreen = false);
  void prepareQualityGrayscale();
  // Quality grayscale restricted to a pixel rectangle (implemented as full-frame push).
  void displayGrayBufferWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const unsigned char* lutData = nullptr);

  void refreshDisplay(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false);

  // Hint to run a one-shot full resync on next update.
  void requestResync();

  // debug function
  void grayscaleRevert();

  // LUT control (hardware-independent on T5S3; kept as no-ops)
  void setCustomLUT(bool enabled, const unsigned char* lutData = nullptr);

  // Power management
  void deepSleep();

  // Access to frame buffer
  uint8_t* getFrameBuffer() const { return frameBuffer; }

  // Save the current framebuffer to a PBM file (desktop/test builds only; no-op on device)
  void saveFrameBufferAsPBM(const char* filename);

 private:
  // Internal geometry setter kept for interface compatibility.
  void setDisplayDimensions(uint16_t width, uint16_t height);

  // Pin configuration (unused on T5S3; kept for interface compatibility)
  int8_t _sclk, _mosi, _cs, _dc, _rst, _busy;

  // Runtime display geometry
  uint16_t displayWidth = DISPLAY_WIDTH;
  uint16_t displayHeight = DISPLAY_HEIGHT;
  uint16_t displayWidthBytes = DISPLAY_WIDTH_BYTES;
  uint32_t bufferSize = BUFFER_SIZE;

  // Frame buffer storage
  uint8_t* frameBuffer0 = nullptr;
  uint8_t* frameBuffer;

  // Grayscale planes for 2-bit rendering
  uint8_t* grayscaleLsbBuffer = nullptr;
  uint8_t* grayscaleMsbBuffer = nullptr;
  uint8_t* grayscaleBaseBuffer = nullptr;
  bool grayscaleBaseCaptured = false;

  uint32_t allocatedBufferSize = 0;

  // State
  bool isScreenOn = false;
  bool customLutActive = false;
  bool inGrayscaleMode = false;
  bool drawGrayscale = false;
  bool displayReady = false;
  bool forceFullRefresh = true;
  bool resyncRequested = false;
  uint32_t refreshCycleCount = 0;

  // M5GFX backend (defined in the .cpp)
  class M5GfxBackend;
  M5GfxBackend* backend = nullptr;

  uint8_t* allocatePlane();
  void releaseBackend();
  void renderBwToPanelCanvas() const;
  void renderGrayToPanelCanvas() const;
  void pushPanelCanvas(RefreshMode mode);
};

// Kept for interface compatibility (X4 LUTs; unused by the T5S3 backend).
extern const unsigned char lut_x4_quality[];
extern const unsigned char lut_x4_quality_fast[];

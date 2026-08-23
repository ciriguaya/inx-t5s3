/**
 * @file EInkDisplay.cpp
 * @brief T5S3 (LilyGo T5 ePaper S3) implementation of the Inx EInkDisplay interface.
 *
 * Drives the ED047TC1 960x540 e-paper panel via M5GFX (lgfx::Bus_EPD +
 * lgfx::Panel_EPD + TPS65185 power controller through the PCA9535 expander).
 * The 1bpp framebuffer keeps the same layout the Inx GfxRenderer expects
 * (physical 960x540 scan orientation, MSB-first within each byte).
 */

#include "EInkDisplay.h"

#include <BoardT5S3.h>
#include <Logging.h>
#include <M5GFX.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <lgfx/v1/platforms/esp32/Bus_EPD.h>
#include <lgfx/v1/platforms/esp32/Panel_EPD.hpp>

#include <algorithm>
#include <cstring>

extern const unsigned char lut_x4_quality[] = {0};
extern const unsigned char lut_x4_quality_fast[] = {0};

namespace {

constexpr uint32_t kEpdBusHz = 16000000;
constexpr uint32_t kMiddleRefreshThreshold = 8;
constexpr uint32_t kQualityRefreshThreshold = 18;
constexpr int kDefaultVcomMv = -1600;

constexpr uint8_t kTpsRegEnable = 0x01;
constexpr uint8_t kTpsRegVcom = 0x03;
constexpr uint8_t kTpsRegPowerGood = 0x0F;
constexpr uint8_t kTpsEnableOutputs = 0x3F;

constexpr uint8_t kGrayBlack = 0x00;
constexpr uint8_t kGrayDark = 0x55;
constexpr uint8_t kGrayLight = 0xAA;
constexpr uint8_t kGrayWhite = 0xFF;

bool writeTpsRegister(const uint8_t reg, const uint8_t* data, const size_t len) {
  BoardT5S3::ScopedI2CLock lock;
  Wire.beginTransmission(T5S3_TPS65185_ADDR);
  Wire.write(reg);
  if (data != nullptr && len > 0) {
    Wire.write(data, len);
  }
  return Wire.endTransmission() == 0;
}

bool readTpsRegister(const uint8_t reg, uint8_t* data, const size_t len) {
  if (data == nullptr || len == 0) {
    return false;
  }

  BoardT5S3::ScopedI2CLock lock;
  Wire.beginTransmission(T5S3_TPS65185_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const uint8_t requested = static_cast<uint8_t>(len);
  if (Wire.requestFrom(static_cast<uint8_t>(T5S3_TPS65185_ADDR), requested) != requested) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    data[i] = Wire.read();
  }
  return true;
}

bool writeTpsRegister8(const uint8_t reg, const uint8_t value) { return writeTpsRegister(reg, &value, 1); }

bool waitForPcaPinHigh(const uint8_t pin, const uint32_t timeoutMs) {
  const uint32_t start = millis();
  bool high = false;
  while (millis() - start < timeoutMs) {
    if (BoardT5S3::readPca9535Pin(pin, &high) && high) {
      return true;
    }
    delay(1);
  }
  return false;
}

bool waitForTpsReady(const uint32_t timeoutMs) {
  const uint32_t start = millis();
  uint8_t powerGood = 0;
  do {
    if (readTpsRegister(kTpsRegPowerGood, &powerGood, 1) && (powerGood & 0xFA) == 0xFA) {
      return true;
    }
    delay(1);
  } while (millis() - start < timeoutMs);
  return false;
}

class T5S3BusEPD : public lgfx::Bus_EPD {
 public:
  bool init() override {
    if (!preparePowerPins()) {
      LOG_ERR("EPD", "Failed to configure PCA9535 power pins");
      return false;
    }
    return lgfx::Bus_EPD::init();
  }

  bool powerControl(const bool powerOn) override {
    if (_pwr_on == powerOn) {
      return true;
    }

    wait();
    if (powerOn) {
      return powerOnSequence();
    }

    powerOffSequence();
    return true;
  }

 private:
  bool preparePowerPins() {
    BoardT5S3::ScopedI2CLock busLock;
    bool ok = true;
    ok &= BoardT5S3::setPca9535PinMode(PCA9535_IO10_EP_OE, OUTPUT);
    ok &= BoardT5S3::setPca9535PinMode(PCA9535_IO11_EP_MODE, OUTPUT);
    ok &= BoardT5S3::setPca9535PinMode(PCA9535_IO13_TPS_PWRUP, OUTPUT);
    ok &= BoardT5S3::setPca9535PinMode(PCA9535_IO14_VCOM_CTRL, OUTPUT);
    ok &= BoardT5S3::setPca9535PinMode(PCA9535_IO15_TPS_WAKEUP, OUTPUT);
    ok &= BoardT5S3::setPca9535PinMode(PCA9535_IO16_TPS_PWR_GOOD, INPUT);
    ok &= BoardT5S3::setPca9535PinMode(PCA9535_IO17_TPS_INT, INPUT);

    ok &= BoardT5S3::writePca9535Pin(PCA9535_IO10_EP_OE, false);
    ok &= BoardT5S3::writePca9535Pin(PCA9535_IO11_EP_MODE, false);
    ok &= BoardT5S3::writePca9535Pin(PCA9535_IO13_TPS_PWRUP, false);
    ok &= BoardT5S3::writePca9535Pin(PCA9535_IO14_VCOM_CTRL, false);
    ok &= BoardT5S3::writePca9535Pin(PCA9535_IO15_TPS_WAKEUP, false);
    return ok;
  }

  bool powerOnSequence() {
    BoardT5S3::ScopedI2CLock busLock;
    const auto& cfg = config();

    lgfx::gpio_hi(cfg.pin_spv);

    bool ok = true;
    ok &= BoardT5S3::writePca9535Pin(PCA9535_IO10_EP_OE, true);
    ok &= BoardT5S3::writePca9535Pin(PCA9535_IO11_EP_MODE, true);
    ok &= BoardT5S3::writePca9535Pin(PCA9535_IO15_TPS_WAKEUP, true);
    ok &= BoardT5S3::writePca9535Pin(PCA9535_IO13_TPS_PWRUP, true);
    ok &= BoardT5S3::writePca9535Pin(PCA9535_IO14_VCOM_CTRL, true);
    if (!ok) {
      LOG_ERR("EPD", "Failed to assert EPD power rails");
      powerOffSequence();
      return false;
    }

    delay(1);
    if (!waitForPcaPinHigh(PCA9535_IO16_TPS_PWR_GOOD, 400)) {
      LOG_ERR("EPD", "TPS65185 power-good pin did not assert");
      powerOffSequence();
      return false;
    }

    if (!writeTpsRegister8(kTpsRegEnable, kTpsEnableOutputs)) {
      LOG_ERR("EPD", "TPS65185 enable write failed");
      powerOffSequence();
      return false;
    }

    const uint16_t vcomValue = static_cast<uint16_t>(-kDefaultVcomMv / 10);
    const uint8_t vcomBytes[2] = {
        static_cast<uint8_t>(vcomValue & 0xFF),
        static_cast<uint8_t>(vcomValue >> 8),
    };
    if (!writeTpsRegister(kTpsRegVcom, vcomBytes, sizeof(vcomBytes))) {
      LOG_ERR("EPD", "TPS65185 VCOM write failed");
      powerOffSequence();
      return false;
    }

    if (!waitForTpsReady(400)) {
      LOG_ERR("EPD", "TPS65185 rails never reached ready state");
      powerOffSequence();
      return false;
    }

    _pwr_on = true;
    return true;
  }

  void powerOffSequence() {
    BoardT5S3::ScopedI2CLock busLock;
    const auto& cfg = config();

    BoardT5S3::writePca9535Pin(PCA9535_IO10_EP_OE, false);
    BoardT5S3::writePca9535Pin(PCA9535_IO11_EP_MODE, false);
    BoardT5S3::writePca9535Pin(PCA9535_IO13_TPS_PWRUP, false);
    BoardT5S3::writePca9535Pin(PCA9535_IO14_VCOM_CTRL, false);
    delay(1);
    BoardT5S3::writePca9535Pin(PCA9535_IO15_TPS_WAKEUP, false);

    lgfx::gpio_lo(cfg.pin_spv);
    _pwr_on = false;
  }
};

uint8_t grayscaleValueForBit(const uint8_t baseByte, const uint8_t lsbByte, const uint8_t msbByte, const uint8_t mask) {
  if (baseByte & mask) {
    return kGrayWhite;
  }

  const bool lsb = (lsbByte & mask) != 0;
  const bool msb = (msbByte & mask) != 0;

  if (msb && lsb) {
    return kGrayDark;
  }
  if (msb) {
    return kGrayLight;
  }
  if (lsb) {
    return kGrayDark;
  }
  return kGrayBlack;
}

lgfx::epd_mode::epd_mode_t epdModeForRefreshMode(const EInkDisplay::RefreshMode mode) {
  switch (mode) {
    case EInkDisplay::FULL_REFRESH:
      return lgfx::epd_mode::epd_quality;
    case EInkDisplay::HALF_REFRESH:
      return lgfx::epd_mode::epd_text;
    case EInkDisplay::STRONG_FAST_REFRESH:
      return lgfx::epd_mode::epd_quality;
    case EInkDisplay::FAST_REFRESH:
    default:
      return lgfx::epd_mode::epd_fast;
  }
}

class T5S3M5GfxDisplay : public lgfx::LGFX_Device {
 public:
  T5S3M5GfxDisplay() {
    auto busCfg = bus_.config();
    busCfg.bus_speed = kEpdBusHz;
    busCfg.pin_data[0] = EP_D0;
    busCfg.pin_data[1] = EP_D1;
    busCfg.pin_data[2] = EP_D2;
    busCfg.pin_data[3] = EP_D3;
    busCfg.pin_data[4] = EP_D4;
    busCfg.pin_data[5] = EP_D5;
    busCfg.pin_data[6] = EP_D6;
    busCfg.pin_data[7] = EP_D7;
    busCfg.pin_pwr = T5S3_LORA_CS;  // Required by esp_lcd i80 API, not used for real power control.
    busCfg.pin_sph = EP_STH;
    busCfg.pin_spv = EP_STV;
    busCfg.pin_oe = T5S3_LORA_CS;  // Dummy direct GPIO; OE itself is handled through the PCA9535.
    busCfg.pin_le = EP_LEH;
    busCfg.pin_cl = EP_CKH;
    busCfg.pin_ckv = EP_CKV;
    busCfg.bus_width = 8;
    bus_.config(busCfg);

    panel_.setBus(&bus_);

    auto detailCfg = panel_.config_detail();
    detailCfg.line_padding = 8;
    panel_.config_detail(detailCfg);

    auto panelCfg = panel_.config();
    panelCfg.memory_width = EInkDisplay::DISPLAY_WIDTH;
    panelCfg.panel_width = EInkDisplay::DISPLAY_WIDTH;
    panelCfg.memory_height = EInkDisplay::DISPLAY_HEIGHT;
    panelCfg.panel_height = EInkDisplay::DISPLAY_HEIGHT;
    panelCfg.offset_rotation = 0;
    panelCfg.offset_x = 0;
    panelCfg.offset_y = 0;
    panelCfg.bus_shared = false;
    panel_.config(panelCfg);

    setPanel(&panel_);
  }

 private:
  T5S3BusEPD bus_;
  lgfx::Panel_EPD panel_;
};

}  // namespace

class EInkDisplay::M5GfxBackend {
 public:
  T5S3M5GfxDisplay gfx;
  lgfx::LGFX_Sprite* panelCanvas = nullptr;

  ~M5GfxBackend() {
    delete panelCanvas;
    panelCanvas = nullptr;
  }

  bool initializePanelCanvas() {
    panelCanvas = new lgfx::LGFX_Sprite(&gfx);
    if (!panelCanvas) {
      return false;
    }
    panelCanvas->setPsram(true);
    panelCanvas->setColorDepth(lgfx::color_depth_t::grayscale_8bit);
    return panelCanvas->createSprite(EInkDisplay::DISPLAY_WIDTH, EInkDisplay::DISPLAY_HEIGHT) != nullptr;
  }
};

EInkDisplay::EInkDisplay(const int8_t sclk, const int8_t mosi, const int8_t cs, const int8_t dc, const int8_t rst,
                         const int8_t busy)
    : _sclk(sclk),
      _mosi(mosi),
      _cs(cs),
      _dc(dc),
      _rst(rst),
      _busy(busy),
      frameBuffer(nullptr) {}

EInkDisplay::~EInkDisplay() {
  releaseBackend();
  free(frameBuffer);
  free(grayscaleLsbBuffer);
  free(grayscaleMsbBuffer);
  free(grayscaleBaseBuffer);
}

void EInkDisplay::setDisplayX3() {
  // No X3 mode on T5S3.
}

void EInkDisplay::setDisplayDimensions(const uint16_t width, const uint16_t height) {
  displayWidth = width;
  displayHeight = height;
  displayWidthBytes = width / 8;
  bufferSize = static_cast<uint32_t>(displayWidthBytes) * height;
}

uint8_t* EInkDisplay::allocatePlane() {
  auto* plane = static_cast<uint8_t*>(heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!plane) {
    plane = static_cast<uint8_t*>(malloc(BUFFER_SIZE));
  }
  return plane;
}

void EInkDisplay::releaseBackend() {
  delete backend;
  backend = nullptr;
  displayReady = false;
}

void EInkDisplay::begin() {
  releaseBackend();
  BoardT5S3::beginI2C();

  if (!frameBuffer) {
    frameBuffer = allocatePlane();
  }
  if (!frameBuffer) {
    LOG_ERR("EPD", "Failed to allocate logical framebuffer");
    return;
  }
  allocatedBufferSize = BUFFER_SIZE;

  backend = new M5GfxBackend();
  if (!backend) {
    LOG_ERR("EPD", "Failed to allocate M5GFX backend");
    return;
  }

  if (!backend->gfx.init()) {
    LOG_ERR("EPD", "M5GFX init failed");
    releaseBackend();
    return;
  }

  backend->gfx.setRotation(0);
  backend->gfx.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  backend->gfx.setEpdMode(lgfx::epd_mode::epd_fastest);
  backend->gfx.powerSave(false);

  if (!backend->initializePanelCanvas()) {
    LOG_ERR("EPD", "Failed to create M5GFX panel canvas");
    releaseBackend();
    return;
  }

  clearScreen(0xFF);
  memset(backend->panelCanvas->getBuffer(), kGrayWhite, static_cast<size_t>(DISPLAY_WIDTH) * DISPLAY_HEIGHT);

  displayReady = true;
  forceFullRefresh = true;
  resyncRequested = false;
  refreshCycleCount = 0;
  grayscaleBaseCaptured = false;
  isScreenOn = false;
  inGrayscaleMode = false;

  LOG_INF("EPD", "T5S3 e-paper initialized: %ux%u", DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

void EInkDisplay::clearScreen(const uint8_t color) const {
  if (frameBuffer) {
    memset(frameBuffer, color, BUFFER_SIZE);
  }
}

void EInkDisplay::drawImage(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                            const uint16_t h, const bool fromProgmem) const {
  if (!frameBuffer || !imageData || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
    return;
  }

  const uint16_t imageWidthBytes = w / 8;
  const uint16_t destByteX = x / 8;
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= DISPLAY_HEIGHT) {
      break;
    }
    const uint32_t destOffset = static_cast<uint32_t>(destY) * DISPLAY_WIDTH_BYTES + destByteX;
    const uint32_t srcOffset = static_cast<uint32_t>(row) * imageWidthBytes;
    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if ((destByteX + col) >= DISPLAY_WIDTH_BYTES) {
        break;
      }
      frameBuffer[destOffset + col] =
          fromProgmem ? pgm_read_byte(&imageData[srcOffset + col]) : imageData[srcOffset + col];
    }
  }
}

void EInkDisplay::drawImageTransparent(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                                       const uint16_t h, const bool fromProgmem) const {
  if (!frameBuffer || !imageData || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
    return;
  }

  const uint16_t imageWidthBytes = w / 8;
  const uint16_t destByteX = x / 8;
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= DISPLAY_HEIGHT) {
      break;
    }
    const uint32_t destOffset = static_cast<uint32_t>(destY) * DISPLAY_WIDTH_BYTES + destByteX;
    const uint32_t srcOffset = static_cast<uint32_t>(row) * imageWidthBytes;
    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if ((destByteX + col) >= DISPLAY_WIDTH_BYTES) {
        break;
      }
      const uint8_t srcByte = fromProgmem ? pgm_read_byte(&imageData[srcOffset + col]) : imageData[srcOffset + col];
      frameBuffer[destOffset + col] &= srcByte;
    }
  }
}

#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
void EInkDisplay::swapBuffers() {
  std::swap(frameBuffer0, frameBuffer);
}
#endif

void EInkDisplay::setFramebuffer(const uint8_t* bwBuffer) const {
  if (frameBuffer && bwBuffer) {
    memcpy(frameBuffer, bwBuffer, BUFFER_SIZE);
  }
}

void EInkDisplay::renderBwToPanelCanvas() const {
  if (!backend || !backend->panelCanvas || !frameBuffer) {
    return;
  }

  auto* grayBuffer = static_cast<uint8_t*>(backend->panelCanvas->getBuffer());
  if (!grayBuffer) {
    return;
  }

  for (uint16_t y = 0; y < DISPLAY_HEIGHT; ++y) {
    const uint8_t* srcRow = frameBuffer + static_cast<uint32_t>(y) * DISPLAY_WIDTH_BYTES;
    uint8_t* dstRow = grayBuffer + static_cast<uint32_t>(y) * DISPLAY_WIDTH;
    for (uint16_t byteX = 0; byteX < DISPLAY_WIDTH_BYTES; ++byteX) {
      const uint8_t srcByte = srcRow[byteX];
      for (uint8_t bit = 0; bit < 8; ++bit) {
        const uint16_t lx = byteX * 8 + bit;
        dstRow[lx] = (srcByte & (0x80 >> bit)) ? kGrayWhite : kGrayBlack;
      }
    }
  }
}

void EInkDisplay::renderGrayToPanelCanvas() const {
  if (!backend || !backend->panelCanvas || !grayscaleLsbBuffer || !grayscaleMsbBuffer || !grayscaleBaseBuffer) {
    return;
  }

  auto* grayBuffer = static_cast<uint8_t*>(backend->panelCanvas->getBuffer());
  if (!grayBuffer) {
    return;
  }

  for (uint16_t y = 0; y < DISPLAY_HEIGHT; ++y) {
    const uint8_t* baseRow = grayscaleBaseBuffer + static_cast<uint32_t>(y) * DISPLAY_WIDTH_BYTES;
    const uint8_t* lsbRow = grayscaleLsbBuffer + static_cast<uint32_t>(y) * DISPLAY_WIDTH_BYTES;
    const uint8_t* msbRow = grayscaleMsbBuffer + static_cast<uint32_t>(y) * DISPLAY_WIDTH_BYTES;
    uint8_t* dstRow = grayBuffer + static_cast<uint32_t>(y) * DISPLAY_WIDTH;

    for (uint16_t byteX = 0; byteX < DISPLAY_WIDTH_BYTES; ++byteX) {
      const uint8_t baseByte = baseRow[byteX];
      const uint8_t lsbByte = lsbRow[byteX];
      const uint8_t msbByte = msbRow[byteX];

      for (uint8_t bit = 0; bit < 8; ++bit) {
        const uint8_t mask = static_cast<uint8_t>(0x80 >> bit);
        const uint16_t lx = byteX * 8 + bit;
        dstRow[lx] = grayscaleValueForBit(baseByte, lsbByte, msbByte, mask);
      }
    }
  }
}

void EInkDisplay::pushPanelCanvas(const RefreshMode mode) {
  if (!backend || !backend->gfx.getPanel() || !backend->panelCanvas) {
    return;
  }

  backend->gfx.waitDisplay();
  backend->gfx.setEpdMode(epdModeForRefreshMode(mode));
  backend->panelCanvas->pushSprite(0, 0);
  backend->gfx.waitDisplay();
  isScreenOn = true;
}

void EInkDisplay::displayBuffer(const RefreshMode mode, const bool turnOffScreen) {
  (void)turnOffScreen;
  if (!displayReady || !frameBuffer) {
    return;
  }

  renderBwToPanelCanvas();

  if (resyncRequested && mode != FULL_REFRESH) {
    refreshCycleCount = kQualityRefreshThreshold;
  }
  if (forceFullRefresh && mode != FULL_REFRESH) {
    forceFullRefresh = false;
  }

  lgfx::epd_mode::epd_mode_t epdMode = lgfx::epd_mode::epd_fastest;
  if (mode == FULL_REFRESH) {
    epdMode = lgfx::epd_mode::epd_quality;
    refreshCycleCount = 0;
  } else if (mode == HALF_REFRESH) {
    epdMode = lgfx::epd_mode::epd_text;
    refreshCycleCount = 0;
  } else if (mode == STRONG_FAST_REFRESH) {
    epdMode = lgfx::epd_mode::epd_quality;
    refreshCycleCount = 0;
  } else {
    const bool useQualityMode = refreshCycleCount >= kQualityRefreshThreshold;
    const bool useMiddleMode =
        !useQualityMode && refreshCycleCount >= kMiddleRefreshThreshold && (refreshCycleCount % kMiddleRefreshThreshold) == 0;
    if (useQualityMode) {
      epdMode = lgfx::epd_mode::epd_quality;
      refreshCycleCount = 0;
    } else {
      epdMode = useMiddleMode ? lgfx::epd_mode::epd_fast : lgfx::epd_mode::epd_fastest;
      refreshCycleCount++;
    }
  }

  backend->gfx.waitDisplay();
  backend->gfx.setEpdMode(epdMode);
  backend->panelCanvas->pushSprite(0, 0);
  backend->gfx.waitDisplay();
  isScreenOn = true;

  forceFullRefresh = false;
  resyncRequested = false;
  grayscaleBaseCaptured = false;
  inGrayscaleMode = false;
}

void EInkDisplay::displayWindow(const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h,
                                const bool turnOffScreen) {
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  displayBuffer(FAST_REFRESH, turnOffScreen);
}

void EInkDisplay::refreshDisplay(const RefreshMode mode, const bool turnOffScreen) { displayBuffer(mode, turnOffScreen); }

void EInkDisplay::requestResync() { resyncRequested = true; }

void EInkDisplay::grayscaleRevert() {
  // Not applicable on T5S3 (no controller-side grayscale RAM to revert).
}

void EInkDisplay::setCustomLUT(const bool enabled, const unsigned char* lutData) {
  (void)enabled;
  (void)lutData;
  customLutActive = enabled;
}

void EInkDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  copyGrayscaleLsbBuffers(lsbBuffer);
  copyGrayscaleMsbBuffers(msbBuffer);
}

void EInkDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  if (!lsbBuffer) {
    return;
  }
  if (!grayscaleLsbBuffer) {
    grayscaleLsbBuffer = allocatePlane();
  }
  if (grayscaleLsbBuffer) {
    memcpy(grayscaleLsbBuffer, lsbBuffer, BUFFER_SIZE);
  }
}

void EInkDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  if (!msbBuffer) {
    return;
  }
  if (!grayscaleMsbBuffer) {
    grayscaleMsbBuffer = allocatePlane();
  }
  if (grayscaleMsbBuffer) {
    memcpy(grayscaleMsbBuffer, msbBuffer, BUFFER_SIZE);
  }
}

#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
void EInkDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  if (frameBuffer && bwBuffer && frameBuffer != bwBuffer) {
    memcpy(frameBuffer, bwBuffer, BUFFER_SIZE);
  }
  grayscaleBaseCaptured = false;
}
#endif

void EInkDisplay::prepareQualityGrayscale() {
  if (displayReady && frameBuffer && !grayscaleBaseCaptured) {
    if (!grayscaleBaseBuffer) {
      grayscaleBaseBuffer = allocatePlane();
    }
    if (grayscaleBaseBuffer) {
      memcpy(grayscaleBaseBuffer, frameBuffer, BUFFER_SIZE);
      grayscaleBaseCaptured = true;
    }
  }
}

void EInkDisplay::displayGrayBuffer(const bool turnOffScreen, const unsigned char* lutData, const bool quality,
                                    const bool trackForRevert) {
  (void)lutData;
  (void)trackForRevert;
  if (!displayReady || !backend || !backend->panelCanvas || !grayscaleLsbBuffer || !grayscaleMsbBuffer) {
    return;
  }

  if (!grayscaleBaseCaptured) {
    prepareQualityGrayscale();
  }
  if (!grayscaleBaseCaptured || !grayscaleBaseBuffer) {
    return;
  }

  renderGrayToPanelCanvas();

  backend->gfx.waitDisplay();
  backend->gfx.setEpdMode(quality ? lgfx::epd_mode::epd_quality : lgfx::epd_mode::epd_fast);
  backend->panelCanvas->pushSprite(0, 0);
  backend->gfx.waitDisplay();
  isScreenOn = true;

  refreshCycleCount = 0;
  forceFullRefresh = false;
  resyncRequested = false;
  grayscaleBaseCaptured = false;
  inGrayscaleMode = true;
  (void)turnOffScreen;
}

void EInkDisplay::displayGrayBufferFastQuality(const bool turnOffScreen) {
  displayGrayBuffer(turnOffScreen, nullptr, true, true);
}

void EInkDisplay::displayGrayBufferWindow(const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h,
                                          const unsigned char* lutData) {
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)lutData;
  displayGrayBuffer(false, nullptr, true, true);
}

void EInkDisplay::deepSleep() {
  if (backend && backend->gfx.getPanel()) {
    backend->gfx.waitDisplay();
    backend->gfx.powerSave(true);
    backend->gfx.sleep();
  }
  isScreenOn = false;
  BoardT5S3::deinitForSleep();
}

void EInkDisplay::saveFrameBufferAsPBM(const char* filename) {
  (void)filename;  // Desktop/test builds only; no-op on device.
}

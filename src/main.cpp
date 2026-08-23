/**
 * @file main.cpp
 * @brief Firmware entry point, globals, and activity bootstrap.
 */

#include <Arduino.h>
#ifndef SIMULATOR
#include <BoardT5S3.h>
#endif
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <SDCardManager.h>
#include <SPI.h>
#include <esp_task_wdt.h>

#include <cstring>
#include <new>
#include <string>

#include <freertos/task.h>

#ifndef SIMULATOR
/**
 * The prebuilt FreeRTOS library creates the IDLE tasks with a 1024-byte stack
 * (baked into vApplicationGetIdleTaskMemory). During boot the IDLE task's stack
 * drops to within a few hundred bytes of its limit (interrupt nesting while the
 * CPU is idle), which periodically trips the stack-canary watchpoint and panics
 * ("Stack canary watchpoint triggered (IDLE0)") - most often while gpio.begin()
 * is starting the I2C/touch hardware. This wrapper (linked via -Wl,--wrap=...)
 * hands the IDLE tasks a 4x larger stack so the boot-time nesting cannot overflow.
 */
extern "C" void __wrap_vApplicationGetIdleTaskMemory(StaticTask_t** ppxIdleTaskTCBBuffer,
                                                      StackType_t** ppxIdleTaskStackBuffer,
                                                      uint32_t* pulIdleTaskStackSize) {
  constexpr uint32_t kIdleTaskStackWords = 4096;
  *ppxIdleTaskStackBuffer = static_cast<StackType_t*>(heap_caps_malloc(
      kIdleTaskStackWords * sizeof(StackType_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
  *ppxIdleTaskTCBBuffer =
      static_cast<StaticTask_t*>(heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
  *pulIdleTaskStackSize = kIdleTaskStackWords;
}
#endif

#include "activity/OpdsServerListActivity.h"
#include "activity/network/CalibreConnectActivity.h"
#include "activity/network/HotspotActivity.h"
#include "activity/network/LocalNetworkActivity.h"
#include "activity/page/LibraryActivity.h"
#include "activity/page/RecentActivity.h"
#include "activity/page/SettingsActivity.h"
#include "activity/page/StatisticActivity.h"
#include "activity/page/SyncActivity.h"
#include "activity/reader/ImageViewerActivity.h"
#include "activity/reader/QuotesActivity.h"
#include "activity/reader/ReaderActivity.h"
#include "activity/system/BootActivity.h"
#include "activity/system/SleepActivity.h"
#include "activity/util/FullScreenMessageActivity.h"
#include "state/OpdsServerStore.h"
#include "state/PendingQuoteJump.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/QuickMenuPanel.h"
#include "util/StringUtils.h"

#ifdef SIMULATOR
extern HalDisplay display;
extern HalGPIO gpio;
#else
HalDisplay display;
HalGPIO gpio;
#endif
MappedInputManager input(gpio);
GfxRenderer renderer(display);
GfxRenderer& render = renderer;

/** Touch hold duration that triggers a long-press action (highlight mode etc.). */
constexpr unsigned long kTouchHoldMs = 600;

/** Minimum vertical travel for a top-edge swipe-down to open the quick-settings overlay. */
constexpr int kQuickMenuSwipeMinDy = 70;

void enterDeepSleep();

/** Global quick-settings overlay (backlight, night mode, sleep) - opened by a swipe down from the top
 *  edge. The RST button is a hardware reset (not readable by firmware), so the gesture is the trigger. */
QuickMenuPanel quickMenu([]() { enterDeepSleep(); });

Activity* currentActivity = nullptr;
bool sdCardAvailable = false;

/** One-shot "open this quote in the book" target set by the Quotes browser (see PendingQuoteJump.h). */
PendingQuoteJump pendingQuoteJump;

unsigned long t1 = 0;
unsigned long t2 = 0;

void verifyPowerButtonDuration();
void waitForPowerRelease();
void normalizeUnavailableClockSettings();
void enterDeepSleep();
void handleTouchInput();
void onGoToReader(const std::string& path);
void onSelectBook(const std::string& path);
void onGoToRecent();
void onGoToStatistics();
void onGoToFileTransfer();
void onGoToSettings();
void onGoToLibrary(const std::string& path = "/");
void onGoToQuotes();
void setupDisplayAndFonts();
void onNetworkModeSelected(NetworkMode mode);
void openReaderFromCallback(const std::string& path);
bool handleGlobalPowerRefresh();

/**
 * @brief Switches the current activity using standard heap allocation.
 * * This uses 'new' and 'delete' which allows the ReaderActivity to utilize
 * the full 360KB of available heap rather than being stuck in a small static buffer.
 */
template <typename T, typename... Args>
void switchTo(Args&&... args) {
  if (currentActivity) {
    currentActivity->onExit();
    delete currentActivity;
    currentActivity = nullptr;
  }

  currentActivity = new T(std::forward<Args>(args)...);
#ifdef SIMULATOR
  Serial.printf("[%lu] [SIM] Activity: %s\n", millis(), currentActivity->getName());
#endif
  currentActivity->onEnter();
}

/**
 * @brief Navigates to the reader activity for a specific book.
 */
void onGoToReader(const std::string& path) {
  switchTo<ReaderActivity>(render, input, path, [](const std::string&) { onGoToRecent(); });
}

bool isExportedNoteImage(const std::string& path) {
  constexpr const char* root = "/Bookmarks & Annotations";
  const size_t rootLen = strlen(root);
  const bool inRoot = path.compare(0, rootLen, root) == 0 && (path.size() == rootLen || path[rootLen] == '/');
  return inRoot && (StringUtils::checkFileExtension(path, ".bmp") || StringUtils::checkFileExtension(path, ".jpg") ||
                    StringUtils::checkFileExtension(path, ".jpeg") || StringUtils::checkFileExtension(path, ".png"));
}

/**
 * @brief Opens the reader activity and returns to the library when closed.
 */
void openReaderFromCallback(const std::string& path) {
  // Defensive copy: `path` is typically a reference into the calling activity's own state (e.g.
  // LibraryActivity's currentPageItems), but switchTo() deletes that activity before this function's
  // arguments are used to construct the new one - passing `path` itself through would dangle.
  const std::string pathCopy = path;
  if (isExportedNoteImage(pathCopy)) {
    switchTo<ImageViewerActivity>(render, input, pathCopy, [pathCopy]() {
      std::string folderPath = pathCopy.substr(0, pathCopy.find_last_of('/'));
      if (folderPath.empty()) folderPath = "/";
      onGoToLibrary(folderPath);
    });
    return;
  }
  switchTo<ReaderActivity>(render, input, pathCopy, [pathCopy](const std::string&) {
    std::string folderPath = pathCopy.substr(0, pathCopy.find_last_of('/'));
    if (folderPath.empty()) folderPath = "/";
    onGoToLibrary(folderPath);
  });
}

/**
 * @brief Callback wrapper for selecting a book to read.
 */
void onSelectBook(const std::string& path) { onGoToReader(path); }

/**
 * @brief Navigates to the statistics activity.
 */
void onGoToStatistics() {
  switchTo<StatisticActivity>(render, input, onGoToRecent, onGoToFileTransfer,
                             []() { onGoToLibrary("/"); }, onGoToSettings);
}

/**
 * @brief Navigates to the quotes/highlights browser.
 */
void onGoToQuotes() {
  switchTo<QuotesActivity>(render, input, []() { onGoToRecent(); }, openReaderFromCallback);
}

/**
 * @brief Navigates to the recent books activity.
 */
void onGoToRecent() {
  switchTo<RecentActivity>(render, input, []() { onGoToLibrary("/"); }, onGoToStatistics, onSelectBook, onGoToRecent,
                           onGoToQuotes, onGoToSettings, onGoToFileTransfer);
}

/**
 * @brief Handles network mode selection and navigates to appropriate activity.
 */
void onNetworkModeSelected(NetworkMode mode) {
  switch (mode) {
    case NetworkMode::JOIN_NETWORK:
      switchTo<LocalNetworkActivity>(render, input, onGoToFileTransfer);
      break;
    case NetworkMode::CONNECT_CALIBRE:
      switchTo<CalibreConnectActivity>(render, input, onGoToFileTransfer);
      break;
    case NetworkMode::CREATE_HOTSPOT:
      switchTo<HotspotActivity>(render, input, onGoToFileTransfer);
      break;
    case NetworkMode::OPDS_BROWSER:
      switchTo<OpdsServerListActivity>(render, input, onGoToFileTransfer);
      break;
  }
}

/**
 * @brief Navigates to the file transfer/sync activity.
 */
void onGoToFileTransfer() {
  switchTo<SyncActivity>(render, input, onNetworkModeSelected, onGoToRecent, onGoToStatistics, onGoToSettings,
                         []() { onGoToLibrary("/"); });
}

/**
 * @brief Navigates to the settings activity.
 */
void onGoToSettings() {
  switchTo<SettingsActivity>(
      render, input, onGoToRecent, []() { onGoToLibrary("/"); }, onGoToFileTransfer, onGoToStatistics);
}

/**
 * @brief Navigates to the library activity.
 */
void onGoToLibrary(const std::string& path) {
  switchTo<LibraryActivity>(render, input, onGoToRecent, openReaderFromCallback, onGoToRecent, onGoToSettings,
                           onGoToFileTransfer, onGoToStatistics, path);
}

/**
 * @brief Set up application.
 */
void verifyPowerButtonDuration() {
  if (SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::SLEEP) return;
  const auto start = millis();
  bool abort = false;
  gpio.update();
  while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) {
    delay(10);
    gpio.update();
  }

  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() < SETTINGS.getPowerButtonDuration()) {
      delay(10);
      gpio.update();
    }
    abort = gpio.getHeldTime() < SETTINGS.getPowerButtonDuration();
  } else {
    abort = true;
  }

  if (abort) gpio.startDeepSleep();
}

void waitForPowerRelease() {
  // Bounded: never hang setup forever on a stuck-low boot pin. A brief wait is
  // enough to debounce the release of a real button press.
  const unsigned long start = millis();
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER) && (millis() - start) < 2000) {
    delay(50);
    gpio.update();
  }
}

void normalizeUnavailableClockSettings() {
  if (gpio.deviceIsX3()) {
    return;
  }

  bool changed = false;
  if (SETTINGS.sleepScreen == SystemSetting::DATETIME) {
    SETTINGS.sleepScreen = SystemSetting::LIGHT;
    changed = true;
  }
  if (SETTINGS.sleepClockRefreshInterval != SystemSetting::CLOCK_REFRESH_OFF) {
    SETTINGS.sleepClockRefreshInterval = SystemSetting::CLOCK_REFRESH_OFF;
    changed = true;
  }
  if (changed) {
    SETTINGS.saveToFile();
  }
}

void enterDeepSleep() {
  normalizeUnavailableClockSettings();
  switchTo<SleepActivity>(render, input);
  display.deepSleep();
  gpio.startDeepSleep();
}

void setupDisplayAndFonts() {
  display.begin();
  render.begin();
  FontManager::initialize(render);
}

bool handleGlobalPowerRefresh() {
  if (!currentActivity || !currentActivity->allowGlobalPowerRefresh()) {
    return false;
  }
  if (SETTINGS.shortPwrBtn != SystemSetting::SHORT_PWRBTN::PAGE_REFRESH) {
    return false;
  }
  if (!input.wasReleased(MappedInputManager::Button::Power)) {
    return false;
  }

  renderer.displayBuffer(HalDisplay::MANUAL_REFRESH);
  return true;
}

/**
 * @brief Returns the deepest active sub-activity under `activity`, or `activity` itself.
 *
 * Hosts like SettingsActivity delegate the whole screen to a sub-activity
 * (CategorySettingsActivity / ReaderPresetsActivity); the sub-activity owns the
 * rows and the tab bar, so touch events must reach it first.
 */
Activity* deepestSubActivity(Activity* activity) {
  while (activity) {
    Activity* sub = activity->activeSubActivity();
    if (!sub) {
      break;
    }
    activity = sub;
  }
  return activity;
}

/**
 * @brief Dispatches touch events to the active activity before its loop() runs.
 *
 * Taps land on the deepest active sub-activity's onTouchTap() first; if neither
 * it nor the top-level activity consumes the tap, it is translated into a
 * Confirm press (the default "tap to select" behavior on the touch-first T5S3).
 * Swipes go to onTouchSwipe() the same way. Injected button taps are consumed
 * by the activity's loop() in the same iteration and cleared at the start of
 * the next one.
 */
void handleTouchInput() {
  // Clear the previous frame's injected taps so a tap only fires once.
  input.clearInjectedButtonTap();

  // GT911 home-key area: always return to the home page (the side button is the back
  // button, so the home key means "home" - never back).
  if (input.wasTouchHomeButtonPressed()) {
    onGoToRecent();
    return;
  }

  // While the quick-settings overlay is open it owns all touch input.
  if (quickMenu.isActive()) {
    MappedInputManager::TouchPoint tap{};
    if (input.wasTouchTapped(tap, renderer)) {
      quickMenu.handleTouchTap(renderer, tap.x, tap.y);
      if (!quickMenu.isActive() && quickMenu.needsRedrawAfterClose()) {
        // Night mode changed while the menu was open: the restored frame has the old
        // polarity, so the owning activity must repaint in the new one.
        Activity* redrawTarget = deepestSubActivity(currentActivity);
        if (redrawTarget) {
          redrawTarget->requestRedraw();
        }
      }
    }
    return;
  }

  Activity* touchTarget = deepestSubActivity(currentActivity);

  MappedInputManager::TouchPoint tap{};
  if (input.wasTouchTapped(tap, renderer)) {
    if (touchTarget && touchTarget->onTouchTap(tap.x, tap.y)) {
      return;
    }
    // The sub-activity did not consume the tap; let the host activity decide
    // (e.g. EpubActivity re-routes taps while a sync sub-activity is open).
    if (currentActivity && currentActivity != touchTarget && currentActivity->onTouchTap(tap.x, tap.y)) {
      return;
    }
    input.injectButtonTap(MappedInputManager::Button::Confirm);
    return;
  }

  MappedInputManager::TouchPoint swipeStart{}, swipeEnd{};
  if (input.getTouchSwipe(swipeStart, swipeEnd, renderer)) {
    const int dx = swipeEnd.x - swipeStart.x;
    const int dy = swipeEnd.y - swipeStart.y;
    // Swipe down starting from the top edge opens the quick-settings overlay.
    if (dy >= kQuickMenuSwipeMinDy && swipeStart.y <= renderer.getScreenHeight() / 8) {
      quickMenu.open(renderer);
      return;
    }
    if (touchTarget && touchTarget->onTouchSwipe(dx, dy, swipeEnd.x, swipeEnd.y)) {
      return;
    }
    if (currentActivity && currentActivity != touchTarget &&
        currentActivity->onTouchSwipe(dx, dy, swipeEnd.x, swipeEnd.y)) {
      return;
    }
  }

  // Touch hold (long-press) for actions such as entering highlight mode.
  MappedInputManager::TouchPoint hold{};
  unsigned long heldMs = 0;
  if (input.getTouchHold(hold, heldMs, renderer) && heldMs >= kTouchHoldMs) {
    if (touchTarget && touchTarget->onTouchHold(hold.x, hold.y, heldMs)) {
      // The hold was consumed: dropping the finger must not also fire a tap on the
      // same spot (that would immediately dismiss e.g. the delete-highlight popup
      // the long-press just opened).
      input.suppressCurrentTouch();
      return;
    }
    if (currentActivity && currentActivity != touchTarget &&
        currentActivity->onTouchHold(hold.x, hold.y, heldMs)) {
      input.suppressCurrentTouch();
      return;
    }
  }
}

/**
 * @brief Set up application.
 */
void setup() {
  t1 = millis();

  // The stock 5-second IDLE-task watchdog aborts the chip during legitimate
  // long-running work (EPUB metadata build, first-chapter page-cache build,
  // large cover/image decode). The compute loops below yield regularly so the
  // IDLE task stays scheduled; the raised timeout is a safety net so a single
  // slow operation can never panic-and-reboot the device mid-book.
  esp_task_wdt_init(30, true);

  // Start the USB serial as early as possible so boot issues are visible on
  // the host instead of presenting a dead device. Unconditional: it must not
  // depend on battery/charger I2C reads succeeding.
  Serial.begin(115200);
  unsigned long serialStart = millis();
  while (!Serial && (millis() - serialStart) < 8000) delay(10);
  // The boot log calls gpio.isUsbConnected(), which probes the BQ25896 charger over I2C - wire the
  // bus up first (later inits call beginI2C() again, which is idempotent) so the probe isn't the
  // 4 spurious "could not acquire lock"/"NULL TX buffer" errors and a false USB=0.
#ifndef SIMULATOR
  BoardT5S3::beginI2C();
#endif
  Serial.printf("[%lu] [MAIN] Boot start (USB=%d)\n", millis(), gpio.isUsbConnected() ? 1 : 0);

  Serial.printf("[%lu] [MAIN] gpio.begin...\n", millis());
  gpio.begin();
  Serial.printf("[%lu] [MAIN] display/fonts...\n", millis());
  setupDisplayAndFonts();
  Serial.printf("[%lu] [MAIN] SD card...\n", millis());

  sdCardAvailable = SdMan.begin();
  Serial.printf("[%lu] [MAIN] SD ready=%d\n", millis(), sdCardAvailable ? 1 : 0);

  if (sdCardAvailable) {
    SETTINGS.loadFromFile();
    READER_SETTINGS.loadFromFile();
    OPDS_STORE.loadOrMigrate({"Default", SETTINGS.opdsServerUrl, SETTINGS.opdsUsername, SETTINGS.opdsPassword});
  }
  normalizeUnavailableClockSettings();

  // Apply persisted display state so boot matches the last session: backlight level
  // (PWM is initialized off by BoardT5S3::begin) and night-mode polarity.
  gpio.setBacklightLevel(SETTINGS.backlightLevel);
  renderer.setNightMode(SETTINGS.nightMode != 0);

  const auto wakeupReason = gpio.getWakeupReason();
  Serial.printf("[%lu] [MAIN] Wakeup reason: %d\n", millis(), static_cast<int>(wakeupReason));
  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      verifyPowerButtonDuration();
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // Boot normally instead of sleeping: after a USB flash the device would
      // otherwise power down immediately and look bricked.
      Serial.printf("[%lu] [MAIN] AfterUSBPower: booting (not sleeping)\n", millis());
      break;
    default:
      break;
  }

  Serial.printf("[%lu] [MAIN] Boot activity...\n", millis());
  switchTo<BootActivity>(render, input);
  waitForPowerRelease();
  Serial.printf("[%lu] [MAIN] Setup complete, entering loop\n", millis());
}

/**
 * @brief All activity loop.
 */
void loop() {
  gpio.update();
  static unsigned long lastActivityTime = millis();
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat >= 5000) {
    lastHeartbeat = millis();
    Serial.printf("[%lu] [MAIN] heartbeat free=%u\n", millis(),
                  static_cast<unsigned>(ESP.getFreeHeap()));
  }

  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || input.hasInjectedButtonTap() ||
#ifdef SIMULATOR
      gpio.wasTouchActivity() ||
#else
      gpio.hadTouchActivity() ||
#endif
      (currentActivity && currentActivity->preventAutoSleep())) {
    lastActivityTime = millis();
  }

  if (millis() - lastActivityTime >= SETTINGS.getSleepTimeoutMs()) {
    enterDeepSleep();
    return;
  }

  // Power button. Outside readers it is the on/off button (a plain press sleeps - the
  // short-press threshold is 10ms in SLEEP mode). While reading, short presses are owned by the
  // reader (book menu by default); only a held press powers the device off.
  unsigned long powerHoldToSleep = SETTINGS.getPowerButtonDuration();
  if (currentActivity && !currentActivity->allowGlobalPowerRefresh()) {
    powerHoldToSleep = SystemSetting::READER_POWER_LONG_PRESS_MS;
  }
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() > powerHoldToSleep) {
    enterDeepSleep();
    return;
  }

  if (handleGlobalPowerRefresh()) {
    delay(10);
    return;
  }

  // Touch dispatch: clears injected taps from the previous frame, then lets the
  // activity's onTouchTap()/onTouchSwipe() (or the Confirm fallback) synthesize
  // button presses that its loop() consumes in this same iteration.
  handleTouchInput();

  if (currentActivity) {
    currentActivity->loop();
  }

  if (currentActivity && currentActivity->skipLoopDelay()) {
    yield();
  } else {
    delay(10);
  }
}

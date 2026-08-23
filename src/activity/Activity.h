#pragma once

/**
 * @file Activity.h
 * @brief Public interface and types for Activity.
 */

#include <HardwareSerial.h>

#include <string>
#include <utility>

class MappedInputManager;
class GfxRenderer;

/**
 * @brief Base class for all activities in the application
 *
 * Activities represent different screens or modes of the device such as
 * reading, browsing library, viewing settings, etc. Each activity manages
 * its own rendering and input handling.
 */
class Activity {
 protected:
  std::string name;
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;

 public:
  /**
   * @brief Construct a new Activity object
   * @param name The name identifier for this activity
   * @param renderer Reference to the graphics renderer
   * @param mappedInput Reference to the input manager
   */
  explicit Activity(std::string name, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : name(std::move(name)), renderer(renderer), mappedInput(mappedInput) {}

  /**
   * @brief Virtual destructor for proper cleanup of derived classes
   */
  virtual ~Activity() = default;

  /**
   * @brief Called when the activity becomes active
   *
   * Override this method to perform setup operations when the activity is entered,
   * such as initializing variables, loading resources, or setting up the display.
   */
  virtual void onEnter() {}

  /**
   * @brief Called when the activity is being deactivated
   *
   * Override this method to perform cleanup operations when leaving the activity,
   * such as saving state, freeing resources, or unsubscribing from events.
   */
  virtual void onExit() {}

  /**
   * @brief Main update loop for the activity
   *
   * Override this method to implement the activity's main behavior.
   * This is called repeatedly while the activity is active.
   */
  virtual void loop() {}

  /**
   * @brief Touch tap handler, invoked before loop() when the touch panel reports a tap.
   * @param x Oriented logical X coordinate (GfxRenderer screen space)
   * @param y Oriented logical Y coordinate (GfxRenderer screen space)
   * @return true if the tap was consumed, false to fall through to default handling
   */
  virtual bool onTouchTap(int16_t x, int16_t y) {
    (void)x;
    (void)y;
    return false;
  }

  /**
   * @brief Touch swipe handler, invoked before loop() when a moved touch is released.
   * @param dx Oriented logical delta X
   * @param dy Oriented logical delta Y
   * @param endX Oriented logical X of the swipe end point
   * @param endY Oriented logical Y of the swipe end point
   * @return true if the swipe was consumed, false to fall through to default handling
   */
  virtual bool onTouchSwipe(int16_t dx, int16_t dy, int16_t endX, int16_t endY) {
    (void)dx;
    (void)dy;
    (void)endX;
    (void)endY;
    return false;
  }

  /**
   * @brief Touch hold handler, invoked while a touch is held in place (long-press).
   * @param x Oriented logical X coordinate
   * @param y Oriented logical Y coordinate
   * @param heldMs How long the touch has been held
   * @return true if the hold was consumed
   */
  virtual bool onTouchHold(int16_t x, int16_t y, unsigned long heldMs) {
    (void)x;
    (void)y;
    (void)heldMs;
    return false;
  }

  /**
   * @brief Gets the activity's name identifier
   * @return The name as a C-string
   */
  const char* getName() const { return name.c_str(); }

  /**
   * @brief Determines whether to skip the delay between loop iterations
   * @return true to skip the delay, false to use standard delay
   *
   * Override this method to return true for activities that require
   * maximum responsiveness or continuous updates.
   */
  virtual bool skipLoopDelay() { return false; }

  /**
   * @brief Determines whether the activity should prevent auto-sleep
   * @return true to prevent auto-sleep, false to allow auto-sleep
   *
   * Override this method to return true for activities that should keep
   * the device awake, such as during reading or when user interaction is expected.
   */
  virtual bool preventAutoSleep() { return false; }

  /**
   * @brief Allows the global system short-power page refresh handler to run before this activity's loop.
   *
   * ReaderActivity opts out because book readers have their own short-power behavior setting.
   */
  virtual bool allowGlobalPowerRefresh() { return true; }

  /**
   * @brief Returns the currently active sub-activity (e.g. the settings category panel inside
   * SettingsActivity), or nullptr when this activity has none.
   *
   * The touch dispatcher in main.cpp walks this chain so taps/swipes land on the
   * sub-activity that actually owns the screen instead of the top-level activity.
   */
  virtual Activity* activeSubActivity() { return nullptr; }

  /**
   * @brief Requests a full re-render of the activity's screen on its next loop.
   *
   * Used after a global overlay (the quick-settings menu) closes with a night-mode
   * change: the captured frame underneath has the old polarity, so the activity
   * must repaint itself in the new one. Activities with their own dirty flag set it
   * here; the default no-op covers screens that have nothing to repaint outside
   * their normal update flow.
   */
  virtual void requestRedraw() {}
};

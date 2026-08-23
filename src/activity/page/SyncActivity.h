#pragma once

/**
 * @file SyncActivity.h
 * @brief Public interface and types for SyncActivity.
 */

#include <functional>

#include "../ActivityWithSubactivity.h"
#include "../Menu.h"

enum class NetworkMode { JOIN_NETWORK, CONNECT_CALIBRE, CREATE_HOTSPOT, OPDS_BROWSER };

class SyncActivity final : public ActivityWithSubactivity, public Menu {
 public:
  SyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
               const std::function<void(NetworkMode)>& onModeSelected,
               const std::function<void()>& onRecentOpen = nullptr,
               const std::function<void()>& onStatisticsOpen = nullptr,
               const std::function<void()>& onSettingsOpen = nullptr,
               const std::function<void()>& onLibraryOpen = nullptr)
      : ActivityWithSubactivity("Network Settings", renderer, mappedInput),
        Menu(),
        onModeSelected(onModeSelected),
        onRecentOpen(onRecentOpen),
        onStatisticsOpen(onStatisticsOpen),
        onSettingsOpen(onSettingsOpen),
        onLibraryOpen(onLibraryOpen) {
    tabSelectorIndex = 3;
  };

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  bool onTouchSwipe(int16_t dx, int16_t dy, int16_t endX, int16_t endY) override;
  void requestRedraw() override { updateRequired = true; }

 private:
  int selectedIndex = 0;
  bool updateRequired = false;

  const std::function<void(NetworkMode)> onModeSelected;
  const std::function<void()> onRecentOpen;
  const std::function<void()> onStatisticsOpen;
  const std::function<void()> onSettingsOpen;
  const std::function<void()> onLibraryOpen;

  void render() const;

  void navigateToSelectedMenu() override {
    if (tabSelectorIndex == 0 && onRecentOpen) onRecentOpen();
    if (tabSelectorIndex == 1 && onLibraryOpen) onLibraryOpen();
    if (tabSelectorIndex == 2 && onSettingsOpen) onSettingsOpen();
    if (tabSelectorIndex == 4 && onStatisticsOpen) onStatisticsOpen();
  }
};

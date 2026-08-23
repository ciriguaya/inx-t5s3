# Inx — T5S3 port

![Interface overview](docs/overview.gif)

> **Status: work in progress.** This fork is quite reliable for daily reading, but it is **not fully polished** — expect rough edges here and there. Feedback and bug reports are very welcome: open an issue or a pull request on this repository.

**This tree is a port of [Inx](https://github.com/obijuankenobiii/inx) to the LilyGo T5 ePaper S3 (T5S3) 4.7-inch reader, built on the T5S3 hardware layer and highlight/quotes system of the [myT5S3-Reader](https://github.com/ciriguaya/myT5S3-Reader) fork of the [T5S3-Reader](https://github.com/ShallowGreen123/T5S3-Reader) project (itself adapted from [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)).**

The T5S3 is an ESP32-S3 device with a 960×540 (logical 540×960 portrait) ED047TC1 e-paper panel, GT911 touch, BQ27220/BQ25896 battery management, a PCF85063 RTC, and two physical buttons (boot button + PCA9535 side button). Inx targets the Xteink X4/X3 (ESP32-C3) instead, so this port replaces Inx's open-x4-sdk hardware layer with a T5S3 backend while keeping the entire Inx application code (UI, reader, settings, sync, web interface) unchanged.

## Credits & License

This is a **derivative work** of three MIT-licensed upstream projects:

| Project | Author | Relation to this port |
|---|---|---|
| [Inx](https://github.com/obijuankenobiii/inx) | Dave Allie | **Base of this port** — the whole application layer (UI, reader, settings, sync, web interface, simulator) is Inx with the hardware layer swapped. |
| [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) | Dave Allie | Upstream of the T5S3 fork; the **e-paper driver approach (M5GFX), battery/charger/RTC handling and touch pipeline** trace back through it. |
| [T5S3-Reader](https://github.com/ShallowGreen123/T5S3-Reader) / **[myT5S3-Reader](https://github.com/ciriguaya/myT5S3-Reader)** | ShallowGreen123 (T5S3 adaptation) + this project's companion fork | The T5S3 **hardware backend** (`lib/Board_T5S3`, `lib/bq25896`, `lib/bq27220`, `lib/I2CMasterCompat`, `lib/hal`) and the **highlight & quotes system** (`/highlights/*.json`, `*_pages.json`, in-book highlight rendering) are ported from here. |

**License: MIT** (see [`LICENSE`](LICENSE)). The original copyright notice (© 2025 Dave Allie) is preserved in `LICENSE` as required by the MIT terms of the upstream projects; keep it and this attribution when redistributing or modifying this firmware. Third-party libraries used (M5GFX, ArduinoJson, SdFat, Expat, miniz, toojpeg, WebSockets, …) carry their own MIT/BSD-style licenses in their upstream repositories.

## Built with AI

Substantial parts of this port — the touch layer, the T5S3 hardware backend, the highlight & quotes integration, the quick-settings menu and this documentation — were written with the help of **DeepSeek V4 Flash**, an AI coding agent running inside the [Zed](https://zed.dev) editor. Every step was reviewed, tested and iterated on real hardware by a human.

## Controls (touch-first)

This firmware is **touch-first**: the T5S3's GT911 touch panel is the primary input — tap to select/open, swipe to scroll / turn pages, long-press to highlight text. The physical buttons have a small, deliberate set of roles:

| Input | Action |
|---|---|
| **Touch screen** | Everything else: menus, lists, sliders, keyboards, page turns, highlighting, the quick menu (swipe down from the top edge), the book menu (swipe up from the bottom edge). |
| **Boot button** | Power **on / off** with a single press. While **inside a book**: short press opens the book menu, holding ≥ 2 s powers off. |
| **IO48 (side button)** | **Back** (previous screen; cancels popups). |
| **Home key** (touch-panel area) | **Home** — always returns to the home screen, never Back. |
| **RST** | Hardware reset — a full reboot / screen refresh (not readable by firmware; cannot be remapped). |

## What was changed vs. upstream Inx

### Hardware layer (drop-in `open-x4-sdk` replacements, same public interfaces)
- `EInkDisplay` — drives the ED047TC1 through M5GFX (`lgfx::Bus_EPD` + `lgfx::Panel_EPD` + TPS65185 power controller via the PCA9535 expander). The 1bpp framebuffer stays in physical 960×540 scan orientation; Inx's GfxRenderer defaults to Portrait, which yields the native 540×960 logical UI.
- `InputManager` — boot button (GPIO0) = power, PCA9535 side button = Back, GT911 touch state machine (tap / hold / swipe / home key).
- `BatteryMonitor` — BQ27220 fuel gauge.
- `SDCardManager` — SdFat on the T5S3 SD bus (CS12, SPI 21/13/14).
- `lib/hal/HalGPIO` — T5S3 pinout, deep-sleep on the boot button only (touch wake is disabled — the device must not turn itself back on from a stray touch), PCF85063 RTC (menu clock, date/time sleep screen, time sync), cached battery %, USB detection, wake-reason detection.
- `lib/hal/HalStorage` — thin `Storage` facade over Inx's `SDCardManager` so T5S3-fork code (highlight system, crash-report dumper) works unchanged. It deliberately does **not** alias `FsFile`, so Inx's own file code keeps working.
- `src/platform/skip_efuse_blk_check.c` — efuse check bypass (from the T5S3 fork).
- `lib/miniz` — PNG-encoder symbols removed so they do not collide with M5GFX's bundled miniz at link time (the firmware encodes JPEG via `toojpeg`; the T5S3 fork's miniz does the same). Note: `HalSystem` (panic hooks) from the fork is intentionally **not** ported — hidden crash-reset loops made it a brick risk.
- `-Wl,--allow-multiple-definition` — resolves the remaining miniz/M5GFX symbol overlap (both are the same library family; Inx's copy wins).
- `lib/Board_T5S3`, `lib/bq25896`, `lib/bq27220`, `lib/I2CMasterCompat`, `lib/Logging` — copied from the T5S3 fork.
- `platformio.ini` — T5S3 board (`T5-ePaper-S3`), 16 MB QIO flash, M5GFX 0.2.20, C++20.
- **IDLE-task stack workaround** — the prebuilt Arduino-ESP32 FreeRTOS creates the IDLE tasks with only a 1024-byte stack. On the ESP32-S3 the boot-time interrupt nesting (USB-CDC, I2C/touch init in `gpio.begin()`) can push the IDLE task past that limit, which trips the stack-canary watchpoint (`Guru Meditation Error: Stack canary watchpoint triggered (IDLE0)`) and boot-loops the device — the panic is layout-sensitive and appears/disappears with unrelated code changes. `src/main.cpp` provides `__wrap_vApplicationGetIdleTaskMemory` (linked via `-Wl,--wrap=vApplicationGetIdleTaskMemory` in `platformio.ini`) to hand the IDLE tasks a 4096-word stack instead.

### Touch (the T5S3 is touch-first)
- `MappedInputManager` gained the T5S3 fork's touch API (`wasTouchTapped`, `getTouchSwipe`, `getTouchHold`, `getTouchPosition`, `wasTouchHomeButtonPressed`) plus `injectButtonTap`/`clearInjectedButtonTap`, which lets touch translate into the existing button model.
- `Activity` gained virtual `onTouchTap(x, y)` / `onTouchSwipe(dx, dy)` handlers; `main.cpp` dispatches touch events to the **deepest active sub-activity** (e.g. the settings category panel inside Settings) before its `loop()`, with a global fallback of tap = Confirm. This is what makes tap-to-select work inside nested screens (settings rows, selector overlays) instead of falling through to the top-level activity.
- **Recent (home)** — tap a book/folder to open it, tap the tab bar; **horizontal swipes browse the recents carousel** (tabs are reached by tapping, not swiping), vertical swipes scroll list/grid views; the tab bar defaults to the **bottom** of the screen (`uiTheme = UI_THEME_BOTTOM_TABS`).
- **Library** — tap items in list/grid/shelf views, tab-bar taps, swipe navigation.
- **Settings panels** — tap a row to open it, swipe to scroll, tap options inside the selector overlay.
- **Statistics** — tab-bar taps and left/right swipes navigate between all five tabs; the dashboard body is read-only.
- **Sync/Network** — tap a row to open it, tab-bar taps and swipes navigate.
- **Tab bar** — tapping any of the five tabs from any of the five tabbed screens navigates directly to it (Home / Library / Settings / Sync / Statistics).
- **Reader (EPUB/TXT/XTC)** — tap left/right thirds to turn pages (respecting the configured Left/Right button actions), tap the middle to open the menu drawer; **a swipe up from the bottom screen edge opens the book menu**; in drawers, swipe to move the list, tap to select; horizontal swipe turns pages while reading.
- **Long-press a highlighted word in a book** opens an Inx-styled **delete-highlight popup** (Cancel / Delete) instead of starting a new selection; long-pressing un-highlighted text still starts a selection.
- **Swipe down from the top screen edge** opens the global **quick-settings menu** (backlight, night mode, sleep) over any screen; tapping outside closes it. (The physical RST button is a hardware reset and cannot be read by firmware, so the gesture is the trigger.)

### Quick-settings menu (backlight + night mode)
- **Backlight**: the T5S3 front-light (PWM on `T5S3_BL_EN`) is driven through `HalGPIO::setBacklightLevel` (0–10); the level persists in `settings.bin` (`backlightLevel`, default 0) and is restored on boot. The PWM stays at the stock **8-bit / 5000 Hz** config (a higher-resolution attempt failed: at 13 bits the 80 MHz clock needs a pre-divider of ~1.95, which the S3's LEDC cannot represent, so the channel never outputs). Level 1 is now duty 2/255 (0.8%) — the dimmest level that still visibly lights this LED — still ~1.5× dimmer than the original 1.2%; levels 2–10 are the unchanged original curve. (PWM frequency is not a brightness lever — average light = duty × peak current.)
- **Quick-menu card**: a paper (light) card with an ink outline, like every other popup — night mode inverts it to a dark card, so the menu is **light in day mode and dark in night mode** (the original always-ink card had the polarities backwards). The backlight row uses the same solid `−`/`+` stepper buttons as the settings drawer (paper fill + ink outline + bold label), at **fixed positions** with a reserved value slot so the buttons never shift between levels 1 and 10.
- **Night mode**: inverts all black/white UI pixels at the GfxRenderer level (`setNightMode`) **but not images** — photos, covers and embedded pictures stay natural on a dark page. The setting persists (`nightMode`) and is applied on boot and on every render; closing the quick menu after a toggle forces the current screen to repaint in the new polarity.

### Buttons 
- **Boot button = power**: a single short press powers the device off (deep sleep); a single press wakes it. No long press required (`shortPwrBtn` defaults to `SLEEP`).
- **While reading**, the same boot button has a dual role: a **short press opens the book menu** (chapters, presets, bookmarks…) instead of sleeping, and a **hold ≥ 2 s powers off** (`SystemSetting::READER_POWER_LONG_PRESS_MS`). Outside a book it stays on/off. The mapping is `READER_SETTINGS.btnPowerShortAction` (default: new `BTN_ACTION_OPEN_MENU`; older settings files with the old Page Refresh default migrate automatically).
- **PCA side button = Back** (navigate to the previous screen; in highlight mode it saves + exits, in the delete popup it cancels).
- **GT911 home-key area = Home**: always returns to the home screen (never Back).
- **RST** is the hardware reset — not readable by firmware, so it cannot be remapped; swipe-down opens the quick menu instead.
- **No page-turn buttons and no Xteink button-reference hints**: the button-hint indicators are compiled out (touch-first), and the reader's per-button action settings remain configurable for anyone who wants them.

### Custom highlight & quotes system (the T5S3 fork's feature, preserved)
- Saving a highlight in the reader's annotation mode now also writes to **`/highlights/<book>.json`** in exactly the same format the myT5S3-Reader fork uses (`book`, `path`, `chapter`, `text`, `paragraph`, `timestamp`, `sequence`, plus the `.sequence` counter). Books without an OPF `<dc:title>` fall back to the sanitized file-name stem so their highlights still reach the quotes system.
- **Pre-existing fork highlights are imported into books**: every `*_pages.json` master entry matching the open book (by title, file stem, or stem-with-`(N)`-stripped) is re-located on the current pagination and rendered as an in-book highlight (deduped by text, so reopening never duplicates). The Quotes browser merges the derived `.json` files with the `*_pages.json` masters.
- Deleting a highlight in-book (long-press → popup) removes it from **both** the per-page ANN3 shard and the `/highlights` JSON + `*_pages.json` master, so it never resurrects; the Quotes browser delete does the same.
- The **Quotes browser** shows the quote with a `N / total` position counter in the header (e.g. `1 / 64`), four equally-outlined buttons (Prev / Next / Open / Delete — none highlighted), and the **Open button jumps to the exact page** in the book: the quote's ANN3 shard gives the precise (spine, page) on the current pagination; for fork quotes on a never-built spine it falls back to the chapter start and then searches the built pages for the phrase to land exactly. (Stray empty-title `_pages.json` artifacts are ignored.)
- The **home screen** shows a **latest-highlight banner** (the quote text + book title) using the extra vertical space of the 540×960 panel; tapping it opens the quotes browser.

### Settings menus are fully touch-adjustable
- **Reader Presets** (Settings → back button) — the collapsible System / Buttons / **T5S3** (renamed from "XTC") / Presets lists are all touch-driven: tap a header to expand/collapse, tap a toggle row to flip it, tap a value row to open the popup selector (tap an option to commit, tap outside to cancel), tap a preset for Edit / Rename / Delete, and swipes move the selection / scroll. The preset editor has an explicit **Save pill** in the top-right of the live preview (new presets still get the name-keyboard first) — leaving via Back saves the same way.
- **In-book settings drawer** (and the preset editor's embedded copy) — taps adjust values / expand groups, vertical swipes move, horizontal swipes change the selected value.
- **Apply Preset picker** (book menu → Apply Preset) — touch hit-testing was missing, so any tap just fired Confirm and silently applied the pre-selected **Default** (the layout never changed and Default always looked selected). It now hit-tests rows: tapping a preset applies it immediately, swiping scrolls the list, and the currently-applied preset is marked with a dot + bold name instead of a button-style highlight.
- **Quick Actions checklist** — tap a row to toggle its bit.
- `ReaderPresetsActivity`, `ReaderPresetEditorActivity`, `QuickActionsSettingsActivity` and `SettingsDrawer` all gained `requestRedraw()` so a quick-menu night-mode toggle repaints them in the new polarity.

### Touch refinements (menu lists, keyboard, popups, progress)
- **No more button-selection highlights in menus**: TOC / bookmarks / annotations lists (and other list screens) no longer draw an Ink-highlighted selection row — rows are always paper with ink text (the current TOC chapter is marked by a **bold** title instead). Tap a row to select it directly; a swipe scrolls the list by a page. The main drawer menu and the percent slider also accept direct taps.
- **Text highlighting is gesture-driven again** (like the old T5S3 reader): long-press a word to anchor, **drag to extend the selection live word-by-word** (`getTouchPosition` polling in the reader loop — previously only the release point was seen and the drag was suppressed after the long-press, so multi-word selection silently failed), and lift to commit + save. A tap while selecting also commits. The word index is now kept across gestures on the same page, so repeat long-presses no longer rebuild the whole page layout (the old rebuild was the perceived "laggy" part).
- **Highlight contrast**: the in-book highlight band is now a ~50% checkerboard (two offset lattice passes) instead of the pale 25% lattice — reads as a solid light-gray band on the 194 PPI panel.
- **WiFi selection (manage via WiFi)** is now touch-driven and reliable: tap a network row to connect (no button-cursor highlight), swipe scrolls the list, tap the Yes/No / Cancel/Forget buttons and the failed screen. Scans use the old reader's hardened pattern — full STA radio restart + `setSleep(false)` + `persistent(false)` before scanning, hidden-SSID results, and up to 3 attempts with a passive scan fallback — which fixes the intermittent "no networks found" scans and flaky connects.
- **Settings value rows are two-button steppers**: `−` / `+` zones flank the value (font size, backlight, margins…), so both directions are equally reachable by touch; the middle of the row just selects it. Toggle rows flip on a single tap; separators expand/collapse on tap.
- **Keyboard rewrite** (rename fields, preset naming, WiFi password, OPDS/KOReader credentials): two pages — letters and numbers/symbols, switched by a `123` / `ABC` key. Keys are compact (46 px tall, nearly square) so the keyboard stays proportional on the 540×960 panel; tap a key directly to type; `DEL` / `OK` work on both pages.
- **Quick-menu backlight** uses the same fixed-position `−`/`+` stepper buttons as the settings drawer (see the Quick-settings menu section).
- **Quote jump never touches reading progress**: opening a quote's exact page sets a `skipProgressSave` flag that stays set for the whole quote-initiated session — page turns, TOC/bookmark/percent jumps and even an auto page-turn inside that session never write the position. Progress is only persisted when the book is opened directly; a quote visit is a peek that always leaves the saved position untouched.
- **Long-press delete popup stays open**: the finger-lift that ended the long-press no longer synthesizes a tap that instantly dismissed the popup (`InputManager::suppressTouchContact` + `HalGPIO` passthrough).
- **Early I2C init on boot**: `BoardT5S3::beginI2C()` runs before the first `isUsbConnected()` probe (BQ25896 charger read), which removes the 4 spurious `Wire` lock errors at boot and makes `Boot start (USB=1)` report the real USB state.

### Other T5S3 defaults
- `uiTheme = UI_THEME_BOTTOM_TABS` (thumb-reachable tab bar).
- RTC-backed menu clock, date/time sleep screen and time sync are enabled (`gpio.deviceIsX3()` returns true for clock features; the display itself reports X4 geometry).
- Deep sleep wakes on the boot button only — touch is disabled while sleeping, so the device turns on/off solely via the boot button.

## Building / flashing

```bash
pio run                    # default = T5S3 firmware
pio run -t upload          # flash over USB
```

The built firmware is `.pio/build/default/firmware.bin`. Flashing over USB-C with `pio run -t upload` works for development; for OTA-style updates, generate the combined binary with the Xteink web flasher flow (same as upstream).

## Simulator

Inx's native SDL simulator still works and is a quick way to try the UI without hardware:

```bash
CROSSPOINT_SIM_SD=./fs_ pio run -e simulator -t run_simulator
```

`fs_/` is a sample SD-card folder (books + the highlight/quotes data). The highlight banner and Quotes browser read the same `/highlights` layout on the simulated card.

The simulator runs as the **X4 Pro profile** (`-DSIMULATOR_DEVICE_X4_PRO`, which enables the touch panel + home key so the **full touch pipeline can be exercised headlessly**), with the simulated panel overridden to the T5S3's geometry — **960×540 physical, 540×960 portrait logical** — via `EINK_DISPLAY_WIDTH`/`EINK_DISPLAY_HEIGHT` in `src/simulator/SimulatorCompat.h`. The SDL window, screenshots and touch mapping therefore match the real device's aspect ratio and coordinate space exactly.

The crosspoint-simulator lib is used with a small patch set (panel-size override support + touch drag/swipe fixes + a front-light stub) that is required for the sim to compile and render at the T5S3 resolution. It is vendored as [`scripts/simulator-lib.patch`](scripts/simulator-lib.patch); re-apply it after a fresh `pio pkg install`:

```bash
cd .pio/libdeps/simulator/simulator && git apply ../../../../scripts/simulator-lib.patch
```

You can drive the UI with scripted input:

```bash
CROSSPOINT_SIM_SD=./fs_ CROSSPOINT_SIM_INPUT_SCRIPT="3000:TAP:162,930;4000:TAP:54,930;8000:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="2500:/tmp/shot.bmp;5000:/tmp/shot2.bmp" .pio/build/simulator/program
```

Coordinates are logical display pixels (540×960 — the same space as the device). TAP/SWIPE/HOME/key events are supported; see the crosspoint-simulator lib for the full event grammar.

The interface-overview GIF at the top of this page is generated from a scripted simulator tour:

```bash
./scripts/sim_tour.sh                   # drives the sim through the UI, captures BMPs to /tmp/simshots
python3 scripts/make_overview_gif.py /tmp/simshots docs/overview.gif
```

## SD-card compatibility with the myT5S3-Reader fork

- **Books**: any folder layout works, unchanged.
- **Highlights/quotes**: `/highlights/*.json` + `/highlights/.sequence` — identical format, read and written byte-compatibly. Your existing quotes appear in the home banner and Quotes browser immediately.
- **Sleep images**: `/sleep/` (Inx convention; the T5S3 fork used `.sleep/` — copy any custom sleep images if you had them).
- **Fonts**: Inx ships Literata + Atkinson Hyperlegible built in; SD fonts go in `/fonts/` as `.bin` packs (built via the web interface). The T5S3 fork's `.fonts/*.cpfont` files are not read by Inx.
- **Settings/recents/progress**: Inx stores these under `/.system/` and `/.metadata/` (the T5S3 fork used `/.crosspoint/`), so those do **not** carry over automatically — reading progress resets per book. This is expected and safe.

## Known limitations

- Only the default (non-simulator) env is tested against hardware assumptions; the simulator exercises the UI/logic but not the real panel, touch or power paths.
- The e-paper refresh waveform tuning (fast/quality thresholds) follows the T5S3 fork's proven M5GFX settings; expect to fine-tune `kMiddleRefreshThreshold`/`kQualityRefreshThreshold` in `open-x4-sdk/libs/display/EInkDisplay/src/EInkDisplay.cpp` to taste.
- Waking by touch boots to the home screen (same as the T5S3 fork).

## Task-watchdog hardening (important for this board)

The stock ESP-IDF 5.1 task watchdog (5 s timeout, panic) aborts the chip whenever the main loop is busy for more than ~5 s. Upstream Inx was built against an older toolchain that never contended with that, but the T5S3 port's book-open path (EPUB metadata build, first-chapter page-cache build, large cover/image decode) can legitimately take longer than 5 s on-device, which previously caused `task_wdt ... IDLE0` aborts and a reboot back to the home screen ("Book seems corrupted" / "no recent books").

This port fixes it two ways:
- **Periodic yields in the long-running loops**: the EPUB metadata build (`Epub::load`, `BookMetadataCache` CSS scan), the chapter layout/build (`ChapterHtmlSlimParser` Expat loop, `Section::onPageComplete`), and the JPEG/PNG decoders (`JpegRender`, `PngRender`, `PngToBmpConverter`) all yield every N iterations so the IDLE task stays scheduled.
- **A raised task-watchdog timeout** (`esp_task_wdt_init(30, true)` at the top of `setup()`): a safety net so a single slow operation can never panic-and-reboot the device.

The first open of a book builds the metadata cache + first chapter (several seconds, with a loading bar); subsequent opens use the cache and are fast.

## Troubleshooting / first boot

- **Serial diagnostics**: the firmware starts `Serial` (USB-CDC) unconditionally at the very top of `setup()` and prints step-by-step boot logs plus a 5-second heartbeat in `loop()` (`[MAIN] ...` lines). If the device ever appears dead, attach a serial monitor (115200 baud) and reset it — the logs will say exactly where it stops. `scripts/read_boot.py <port>` opens the port and resets the chip for you.
- **Blank screen after flashing**: the TPS65185 display power rail must report all rails good (`POWER_GOOD` register mask `0xFA`). If you see `TPS65185 rails never reached ready state` repeating, the display has no power — check `waitForTpsReady()` in `open-x4-sdk/libs/display/EInkDisplay/src/EInkDisplay.cpp` (must match the T5S3 fork's `(powerGood & 0xFA) == 0xFA` check).
- **Boots straight to sleep when plugged into USB**: `AfterUSBPower` wake-up boots normally (it does not deep-sleep), so flashing over USB never leaves the device looking bricked. A normal power-button press still sleeps/wakes as expected.
- **Recovery**: the device is never locked down — hold the boot button or use `esptool`/`pio run -t upload` to re-flash at any time. The ROM bootloader is always reachable over USB.

---

# Inx (upstream README follows)

Reimagined. Improved. Simplified.

Inx is a community firmware for Xteink e-paper readers. It is focused on a cleaner reading experience, better EPUB support, native image rendering, SD-card fonts, and practical device tools.

*This project is a fork of CrossPoint and is not affiliated with Xteink.*

---

![](./docs/images/cover.jpg)

## What You Can Do

- Read **EPUB**, **XTC / XTCH**, **TXT**, and **MD** files.
- Browse books from **Recent**, **Library**, **Settings**, **File Transfer**, and **Statistics** tabs.
- Use EPUB features such as bookmarks, annotations, dictionary lookup, go-to-percent, table of contents, footnotes, per-book settings, and KOReader sync.
- Render **JPEG**, **PNG**, and **BMP** images directly.
- Use **1-bit** or **2-bit** image rendering.
- Cache rendered images and system data for faster repeat loads.
- Use custom sleep screens, recent-book sleep screens, transparent cover sleep screens, or date/time sleep screens on supported devices.
- Install reader fonts from the SD card instead of baking large fonts into firmware.
- Connect to Wi-Fi, Calibre, OPDS catalogs, KOReader sync, and the local web file manager.
- Tune reader layout, buttons, fonts, status bar, refresh behavior, image quality, and display options.

## Main Features

### Reader

- EPUB paging with saved progress.
- EPUB layout support for tables, drop caps, borders, images, lists, blockquotes, superscript/subscript, and common CSS spacing/alignment.
- EPUB text annotation and highlight support.
- EPUB dictionary lookup using StarDict dictionaries stored on the SD card.
- EPUB bookmarks.
- Go to a specific percentage in an EPUB.
- Table of contents, bookmark, annotation, and footnote navigation from the in-book menu.
- EPUB menu tools for deleting cache/progress, deleting a book, generating full data, and regenerating thumbnails.
- Per-book reader settings and reader presets.
- Reading statistics.
- KOReader sync support.
- TXT / MD reader.
- XTC / XTCH reader with chapter selection.
- Auto page turn support for EPUB and XTC reading.

### Images

- Native JPEG rendering.
- Native PNG rendering.
- BMP rendering.
- 1-bit and 2-bit image modes.
- Low, medium, and high image quality options for reader images.
- Low, medium, and high sleep image quality options.
- Display cache for faster repeated image draws.
- Improved image scaling and dithering.
- Cover, thumbnail, and sleep-screen rendering options.
- Thumbnail generation for EPUB and XTC books.

### Library

- Recent books page.
- Folder-based library browser.
- Flat all-books view.
- Cover shelf view for EPUB and XTC books.
- Tag view when the library index is enabled.
- Favorites.
- Sort options by title, group/folder, reading state, and tag.
- Optional indexed library mode for faster browsing and tag management.
- List and grid library modes.

### Display

- Text anti-aliasing.
- Configurable refresh frequency.
- Optional half refresh when opening main tabs.
- Sleep screen modes:
  - Dark
  - Light
  - Custom image
  - Recent book
  - Transparent cover
  - None
  - Date/time on supported devices
- Custom sleep images from `/sleep/`, `/sleep.bmp`, `/sleep.jpg`, or `/sleep.jpeg`.

### Sync & Network

- Join Wi-Fi networks.
- Create a hotspot.
- Connect to Calibre.
- Browse OPDS catalogs.
- Use KOReader sync.
- Upload files through the local web interface.

### Settings

Settings are split into simple **System** and **Reader** panels.

System settings include:

- Sleep screen.
- Sleep image picker.
- Recent page mode.
- Library mode.
- Button layout.
- Power button behavior.
- Time to sleep.
- Library indexing.
- Library custom sort.
- Cache clearing.
- Thumbnail generation.
- KOReader, OPDS, Calibre, and OTA update tools.
- About page with device memory information.

Reader settings include:

- Font family and size.
- SD-card font families.
- Line height and word spacing.
- Screen margins.
- Paragraph alignment.
- CSS indentation.
- Reading orientation.
- Hyphenation.
- Bionic Reading.
- Page navigation mapping.
- Long-press chapter or page skipping.
- Auto page turn.
- Text anti-aliasing.
- Image grayscale / 2-bit rendering.
- Smart refresh on image-heavy pages.
- Status bar layout.

## Web Interface

The local web interface includes:

- **Dashboard**: device status, IP address, Wi-Fi strength, memory, uptime, and quick links.
- **Files**: browse folders, upload files, create folders, delete files, upload cover art to `/sleep`, and set folder thumbnails.
- **Epub**: drag-and-drop EPUB imports, folder creation, JPEG optimization, optional packaged device thumbnails, and import progress.
- **Tags**: create reusable tags and assign them to indexed books.
- **Fonts**: build SD-card font packs from TTF/OTF files and upload them to `/fonts`.
- **Settings**: edit system settings, reader settings, Wi-Fi networks, KOReader settings, and OPDS servers.

## Dictionary

Inx supports EPUB dictionary lookup with StarDict dictionaries stored on the SD card.

You can download a ready-to-use dictionary pack here:

[Download dictionary pack](https://drive.google.com/file/d/1N7aUdO93xyO8Cvgr_u2sX5-dJ_a-piCK/view?usp=sharing)

To install a dictionary:

1. Download and extract the dictionary pack.
2. Copy the extracted dictionary folder to `/dictionaries/` on the SD card.
3. Make sure the dictionary folder directly contains `.ifo`, `.idx`, and `.dict` files.
4. On the device, open **Sync/Device Management page -> Choose dictionary** and select the dictionary.

Example SD-card layout:

```text
/dictionaries/
  English/
    dictionary.ifo
    dictionary.idx
    dictionary.dict
```

Only uncompressed `.dict` files are supported. Compressed `.dict.dz` dictionaries are not supported/must be uncompressed.

## Fonts

Inx includes built-in **Literata** and **Atkinson Hyperlegible** reader fonts.

You can also install fonts on the SD card:

```text
/fonts/
  MyFont/
    Regular_10.bin
    Regular_12.bin
    Regular_14.bin
    Bold_14.bin
    Italic_14.bin
    BoldItalic_14.bin
```

The web font manager converts TTF/OTF files into the `.bin` format used by the reader. Regular is required; bold, italic, and bold italic are optional.

## Custom Sleep Images

Put sleep images on the SD card:

```text
/sleep/
  image1.bmp
  image2.jpg
  image3.png

/sleep.bmp
/sleep.jpg
/sleep.jpeg
```

You can choose a fixed sleep image from settings, or let the device pick one randomly.

## Cache

Inx uses SD-card cache files to save RAM and speed up repeated work.

Main cache locations:

```text
/.metadata/       EPUB metadata, layout, progress, stats, annotations
/.metadata/xtc/   XTC / XTCH metadata and progress
/.system/cache/   Display and image cache
/.system/         Settings, TXT cache, and system data
/fonts/           SD-card reader fonts
/dictionaries/    StarDict dictionaries for EPUB lookup
/sleep/           Custom sleep images
```

You can clear cache from **Settings -> Actions -> Delete Cache**.

Deleting `/.metadata` will force EPUB layout data to be rebuilt.

## Installing

### Web Flash

1. Connect your Xteink device to your computer with USB-C.
2. Download `firmware.bin` from the [releases page](https://github.com/obijuankenobiii/inx/releases).
3. Open [xteink.dve.al](https://xteink.dve.al/).
4. Flash the firmware using the OTA fast flash controls.

To return to the official firmware, flash the latest official firmware from [xteink.dve.al](https://xteink.dve.al/) or use the debug page to swap boot partitions.

## Development

### Requirements

- PlatformIO Core (`pio`) or VS Code with PlatformIO.
- Python 3.8 or newer.
- USB-C cable.
- SDL2 for simulator builds.

### Clone

```sh
git clone --recursive https://github.com/obijuankenobiii/inx
cd inx
```

If you already cloned without submodules:

```sh
git submodule update --init --recursive
```

### Build

```sh
pio run
```

### Flash

```sh
pio run --target upload
```

### Web Assets

The firmware embeds the HTML and JS from `src/network/html` and `data/js`.

```sh
python3 scripts/build_html.py
```

`pio run` also regenerates these files before compiling.

### Simulator

Inx includes two native simulator targets based on the CrossPoint simulator.

For the full SDL/device UI simulator:

```sh
CROSSPOINT_SIM_SD=./fs_ pio run -e simulator -t run_simulator
```

For dashboard-only testing:

```sh
CROSSPOINT_SIM_SD=./fs_ pio run -e simulator_web -t run_simulator
```

The simulator stores its SD-card data in the folder passed through `CROSSPOINT_SIM_SD`. The firmware web server is exposed at `http://127.0.0.1:8080/` when the simulated device starts a hotspot or local network server.

On macOS, SDL2 is required:

```sh
brew install sdl2
```

For more simulator details, see the [CrossPoint simulator project](https://github.com/crosspoint-reader/crosspoint-simulator).

### Serial Debugging

Install the monitor dependencies:

```sh
python3 -m pip install pyserial colorama matplotlib
```

Run the monitor:

```sh
# Linux
python3 scripts/debugging_monitor.py

# macOS example
python3 scripts/debugging_monitor.py /dev/cu.usbmodem2101
```

## Contributing

Contributions are welcome.

1. Fork the repository.
2. Create a branch.
3. Make your changes.
4. Open a pull request.

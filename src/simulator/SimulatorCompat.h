#pragma once

#ifdef SIMULATOR

// The T5S3 e-ink panel is 960×540 physical (540×960 portrait logical), not the
// crosspoint-simulator's X4 Pro default (800×480). This header is force-included
// into every TU of the simulator build (via -include), so these overrides reach
// the simulator lib's own sources too, keeping the SDL window, framebuffer and
// touch mapping consistent with the project's GfxRenderer at 960×540.
#ifndef EINK_DISPLAY_WIDTH
#define EINK_DISPLAY_WIDTH 960
#endif
#ifndef EINK_DISPLAY_HEIGHT
#define EINK_DISPLAY_HEIGHT 540
#endif

#ifdef __cplusplus
#include <chrono>
#include <cstdint>
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
using TickType_t = uint32_t;
using BaseType_t = int;
using UBaseType_t = unsigned int;
#else
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
#endif

#ifndef pdPASS
#define pdPASS 1
#endif

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
#endif

#ifndef SNTP_SYNC_STATUS_RESET
#define SNTP_SYNC_STATUS_RESET 0
#endif

#ifdef __cplusplus
inline void sntp_set_sync_status(int) {}
#endif

#ifndef vSemaphoreDelete
#define vSemaphoreDelete(sem) delete (sem)
#endif

#ifdef __cplusplus
inline TickType_t xTaskGetTickCount() {
  using namespace std::chrono;
  static const steady_clock::time_point start = steady_clock::now();
  return static_cast<TickType_t>(duration_cast<milliseconds>(steady_clock::now() - start).count());
}

inline uint32_t esp_random() {
  static uint32_t state = 0x9e3779b9U;
  state = state * 1664525U + 1013904223U + xTaskGetTickCount();
  return state;
}
#endif

#ifndef MANUAL_REFRESH
#define MANUAL_REFRESH FAST_REFRESH
#endif

#ifndef STRONG_FAST_REFRESH
#define STRONG_FAST_REFRESH FAST_REFRESH
#endif

#endif

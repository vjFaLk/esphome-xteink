#pragma once

#include <cstdint>
#include <sys/time.h>

namespace esphome::xteink {

// The layout marker says what the application left visible. It carries no
// framebuffer, controller RAM baseline, or pixel history.
struct PanelSleepState {
  uint32_t layout;
  uint32_t clock_updates;
  uint32_t clock_bytes;
  uint32_t clock_awake_ms;
  uint32_t scheduled_overviews;
  uint32_t ha_ready_ms;
  uint8_t clock_minute;
  bool clock_wifi_off;
};

PanelSleepState& panel_sleep_state();

inline uint32_t milliseconds_to_next_minute() {
  timeval now{};
  gettimeofday(&now, nullptr);
  constexpr uint32_t Minute = 60000;
  return Minute - (now.tv_sec % 60) * 1000 - now.tv_usec / 1000 + 100;
}

}  // namespace esphome::xteink

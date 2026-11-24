#pragma once

#include <cstdint>
#include <sys/time.h>

namespace esphome::xteink {

// The layout marker says what the application left visible. It carries no
// framebuffer, controller RAM baseline, or pixel history.
struct PanelSleepState {
  time_t scheduled_epoch;
  time_t displayed_clock_minute;
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

inline uint32_t schedule_next_minute() {
  timeval now{};
  gettimeofday(&now, nullptr);
  auto &state = panel_sleep_state();
  state.scheduled_epoch = now.tv_sec - now.tv_sec % 60 + 60;
  return (state.scheduled_epoch - now.tv_sec) * 1000 - now.tv_usec / 1000 + 100;
}

inline void restore_scheduled_minute() {
  const auto scheduled = panel_sleep_state().scheduled_epoch;
  // A timer wake was scheduled for this boundary. RTC calibration after
  // deep sleep can make the observed clock read earlier than that boundary.
  timeval now{};
  gettimeofday(&now, nullptr);
  if (scheduled > now.tv_sec) {
    const timeval boundary{scheduled, 100000};
    settimeofday(&boundary, nullptr);
  }
}

}  // namespace esphome::xteink

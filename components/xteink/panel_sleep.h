#pragma once

#include <cstdint>
#include <sys/time.h>
#include <esp_timer.h>

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
  uint8_t clock_minute;
  uint8_t clock_commits;
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
  if (!scheduled) return;
  // RTC calibration after deep sleep can shift wall time in either
  // direction. Anchor timer wakes to their programmed deadline. Keep the
  // time spent booting so it is counted in the following sleep interval.
  timeval now{};
  gettimeofday(&now, nullptr);
  const int64_t since_boundary_us = esp_timer_get_time() + 100000;
  const time_t expected = scheduled + since_boundary_us / 1000000;
  if (now.tv_sec < scheduled || now.tv_sec > expected + 2) {
    const timeval boundary{expected, suseconds_t(since_boundary_us % 1000000)};
    settimeofday(&boundary, nullptr);
  }
}

}  // namespace esphome::xteink

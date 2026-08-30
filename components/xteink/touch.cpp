#include "touch.h"

#include "esphome/core/log.h"

namespace esphome {
namespace xteink {

static const char *const TAG = "xteink.touch";

void XteinkTouchscreen::setup() {
  touchscreen::Touchscreen::setup();
  // The SDK reports panel-native coordinates (0..width-1, 0..height-1).
  if (this->x_raw_max_ == this->x_raw_min_) {
    this->x_raw_max_ = this->parent_->display().getDisplayWidth() - 1;
    this->y_raw_max_ = this->parent_->display().getDisplayHeight() - 1;
  }
}

void XteinkTouchscreen::update_touches() {
  const auto snapshot = this->parent_->input().getTouchSnapshot();
  for (uint8_t i = 0; i < snapshot.count; i++) {
    const auto &p = snapshot.points[i];
    if (p.point.valid) {
      this->add_raw_touch_position_(p.id, p.point.x, p.point.y);
    }
  }
}

void XteinkTouchscreen::dump_config() {
  ESP_LOGCONFIG(TAG, "Xteink touchscreen:\n  Touch controller present: %s",
                YESNO(this->parent_->input().hasTouch()));
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace xteink
}  // namespace esphome

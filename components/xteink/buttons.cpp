#include "buttons.h"

#include "esphome/core/log.h"

namespace esphome {
namespace xteink {

static const char *const TAG = "xteink.buttons";

void XteinkButtons::update() {
  auto &input = this->parent_->input();
  for (uint8_t i = 0; i <= InputManager::BTN_POWER; i++) {
    if (this->buttons_[i] != nullptr) {
      this->buttons_[i]->publish_state(input.isPressed(i));
    }
  }
  if (auto *home = this->buttons_[HOME]) {
    // The GT911 only reports the press edge, so the sensor pulses ON for one poll.
    home->publish_state(this->parent_->take_home_press());
  }
}

void XteinkButtons::dump_config() {
  ESP_LOGCONFIG(TAG, "Xteink buttons:");
  LOG_UPDATE_INTERVAL(this);
  for (uint8_t i = 0; i <= HOME; i++) {
    LOG_BINARY_SENSOR("  ", NAMES[i], this->buttons_[i]);
  }
}

}  // namespace xteink
}  // namespace esphome

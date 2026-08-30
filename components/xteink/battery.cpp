#include "battery.h"

#include "esphome/core/log.h"

namespace esphome {
namespace xteink {

static const char *const TAG = "xteink.battery";

void XteinkBattery::update() {
  auto &battery = this->parent_->battery();
  if (this->level_ != nullptr) {
    this->level_->publish_state(battery.readPercentage());
  }
  if (this->voltage_ != nullptr) {
    this->voltage_->publish_state(battery.readMillivolts() / 1000.0f);
  }
}

void XteinkBattery::dump_config() {
  ESP_LOGCONFIG(TAG, "Xteink battery:");
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "Level", this->level_);
  LOG_SENSOR("  ", "Voltage", this->voltage_);
}

}  // namespace xteink
}  // namespace esphome

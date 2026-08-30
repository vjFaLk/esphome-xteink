#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"
#include "xteink.h"

namespace esphome {
namespace xteink {

class XteinkButtons : public PollingComponent {
 public:
  static constexpr uint8_t HOME = 7;  // after InputManager::BTN_POWER (6)
  static constexpr const char *NAMES[HOME + 1] = {"button_1", "button_2", "button_3", "button_4",
                                                  "up",       "down",     "power",    "home"};

  void set_parent(Xteink *parent) { this->parent_ = parent; }
  void set_button(uint8_t idx, binary_sensor::BinarySensor *button) { this->buttons_[idx] = button; }
  void update() override;
  void dump_config() override;

 protected:
  Xteink *parent_{nullptr};
  binary_sensor::BinarySensor *buttons_[HOME + 1]{};
};

}  // namespace xteink
}  // namespace esphome

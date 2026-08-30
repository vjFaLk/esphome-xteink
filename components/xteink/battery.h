#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "xteink.h"

namespace esphome {
namespace xteink {

class XteinkBattery : public PollingComponent {
 public:
  void set_parent(Xteink *parent) { this->parent_ = parent; }
  void set_level(sensor::Sensor *s) { this->level_ = s; }
  void set_voltage(sensor::Sensor *s) { this->voltage_ = s; }
  void update() override;
  void dump_config() override;

 protected:
  Xteink *parent_{nullptr};
  sensor::Sensor *level_{nullptr};
  sensor::Sensor *voltage_{nullptr};
};

}  // namespace xteink
}  // namespace esphome

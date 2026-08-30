#pragma once

#include "esphome/components/touchscreen/touchscreen.h"
#include "xteink.h"

namespace esphome {
namespace xteink {

/// GT911 touch (X4 Pro) read through the SDK InputManager, which the hub polls.
class XteinkTouchscreen : public touchscreen::Touchscreen {
 public:
  void set_parent(Xteink *parent) { this->parent_ = parent; }
  void setup() override;
  void update_touches() override;
  void dump_config() override;

 protected:
  Xteink *parent_{nullptr};
};

}  // namespace xteink
}  // namespace esphome

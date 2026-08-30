#pragma once

#include "esphome/core/component.h"

#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <InputManager.h>

namespace esphome {
namespace xteink {

/// Hub: owns the FreeInk SDK objects and brings the board up in the right order.
/// update() polls the SDK InputManager; the buttons/touch platforms read from it.
class Xteink : public PollingComponent {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;
  /// Runs right before ESP deep sleep: holds the board's power-latch pins.
  void on_powerdown() override;
  float get_setup_priority() const override { return setup_priority::BUS + 10; }

  EInkDisplay &display() { return this->display_; }
  InputManager &input() { return this->input_; }
  BatteryMonitor &battery() { return this->battery_; }
  /// Returns true once per GT911 home-key press (X4 Pro); latched so a slower
  /// consumer cannot miss the one-update edge the SDK reports.
  bool take_home_press();

 protected:
  EInkDisplay display_{BoardConfig::ACTIVE.display.sclk, BoardConfig::ACTIVE.display.mosi,
                       BoardConfig::ACTIVE.display.cs,   BoardConfig::ACTIVE.display.dc,
                       BoardConfig::ACTIVE.display.rst,  BoardConfig::ACTIVE.display.busy};
  InputManager input_;
  BatteryMonitor battery_;
  bool home_pressed_{false};
};

}  // namespace xteink
}  // namespace esphome

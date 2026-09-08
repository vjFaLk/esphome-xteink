#pragma once

#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/component.h"

#include <BoardConfig.h>
#include <FrontlightManager.h>
#include <driver/gpio.h>

namespace esphome {
namespace xteink {

/// X4 Pro warm/cool frontlight as a cold-warm-white light.
class XteinkFrontlight : public light::LightOutput, public Component {
 public:
  void setup() override {
    // Xteink::on_powerdown() holds the LED pads LOW; the hold survives the
    // deep-sleep wake and a held pad silently ignores the LEDC drive begin()
    // attaches (the SDK's own release is compiled out without FREEINK_FRONTLIGHT_LS).
    const auto &fl = BoardConfig::ACTIVE.frontlight;
    for (int8_t pin : {fl.gpio, fl.gpioWarm}) {
      if (pin >= 0)
        gpio_hold_dis(static_cast<gpio_num_t>(pin));
    }
    this->frontlight_.begin();
  }
  float get_setup_priority() const override { return setup_priority::HARDWARE; }  // after the hub's rails
  void dump_config() override;

  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    traits.set_supported_color_modes({light::ColorMode::COLD_WARM_WHITE});
    traits.set_min_mireds(153);  // ~6500 K, cool channel
    traits.set_max_mireds(370);  // ~2700 K, warm channel
    return traits;
  }
  void write_state(light::LightState *state) override;

 protected:
  FrontlightManager frontlight_;
};

}  // namespace xteink
}  // namespace esphome

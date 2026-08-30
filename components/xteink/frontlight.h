#pragma once

#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/component.h"

#include <FrontlightManager.h>

namespace esphome {
namespace xteink {

/// X4 Pro warm/cool frontlight as a cold-warm-white light.
class XteinkFrontlight : public light::LightOutput, public Component {
 public:
  void setup() override { this->frontlight_.begin(); }
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

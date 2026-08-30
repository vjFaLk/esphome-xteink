#include "frontlight.h"

#include "esphome/core/log.h"

namespace esphome {
namespace xteink {

static const char *const TAG = "xteink.frontlight";

void XteinkFrontlight::write_state(light::LightState *state) {
  float cold, warm;
  state->current_values_as_cwww(&cold, &warm, /*constant_brightness=*/true);
  const float total = cold + warm;  // == brightness with constant_brightness
  if (total <= 0.0f) {
    this->frontlight_.off();
    return;
  }
  this->frontlight_.setColorTemperature(static_cast<uint8_t>(warm / total * 100.0f + 0.5f));
  this->frontlight_.setBrightness(static_cast<uint8_t>(total * 100.0f + 0.5f));
}

void XteinkFrontlight::dump_config() {
  ESP_LOGCONFIG(TAG, "Xteink frontlight:\n  Present: %s\n  Color temperature: %s",
                YESNO(this->frontlight_.present()), YESNO(this->frontlight_.hasColorTemperature()));
}

}  // namespace xteink
}  // namespace esphome

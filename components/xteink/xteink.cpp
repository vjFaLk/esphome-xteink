#include "xteink.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <XteinkDetect.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

namespace esphome {
namespace xteink {

static const char *const TAG = "xteink";

void Xteink::setup() {
  // Battery-latched boards (X4 Pro's GPIO1 peripheral rail) power off without this.
  BoardConfig::holdPowerRails();
  // Newer panel batches ship an UltraChip controller in place of the SSD1677/UC8253;
  // probe the bus and promote the profile before the driver is selected in begin().
  if (freeink::applyXteinkDisplayController()) {
    ESP_LOGI(TAG, "UltraChip panel controller detected, driver promoted");
  }
#if FREEINK_DEVICE_X3
  if (BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279) {
    BoardConfig::selectDevice(BoardConfig::Board::XteinkX3Uc8279);
  }
  this->display_.setDisplayX3();
#endif
  this->display_.begin();
  this->input_.begin();
}

void Xteink::update() {
  this->input_.update();
  if (this->input_.wasHomeKeyPressed()) {
    this->home_pressed_ = true;
  }
}

bool Xteink::take_home_press() {
  bool pressed = this->home_pressed_;
  this->home_pressed_ = false;
  return pressed;
}

// Drive `pin` to `level` and latch it so the level survives deep sleep.
// gpio_hold_dis first: a hold left from a previous cycle makes the write a no-op.
static void hold_pin(int8_t pin, int level) {
  if (pin < 0)
    return;
  const auto g = static_cast<gpio_num_t>(pin);
  gpio_hold_dis(g);
  gpio_set_direction(g, GPIO_MODE_OUTPUT);
  gpio_set_level(g, level);
  gpio_hold_en(g);
}

void Xteink::on_powerdown() {
  // Park the panel: booster off + DSLP. The SDK now skips the power-off sequence
  // when the screen is already off (re-issuing it after a full refresh hung on
  // BUSY and tripped the 5 s task watchdog, rebooting instead of sleeping). Feed
  // the watchdog through whatever BUSY wait remains so that can never recur.
  this->display_.setBusyWaitSliceHook([](int8_t, uint8_t) {
    App.feed_wdt();
    delay(1);
    return true;
  });
  this->display_.deepSleep();

  const auto &b = BoardConfig::ACTIVE;
  // Panel RESET must not float through sleep: an unpowered-but-floating RESET can
  // let the controller drift out of DSLP and restart its booster (milliamp drain).
  // Mirrors the SDK PowerManager: LOW alongside a switched-off panel rail, HIGH
  // when the rail stays powered (X4/X3). EpdBus::begin() releases the hold on wake.
  hold_pin(b.display.rst, b.display.powerEnable >= 0 ? 0 : 1);
  hold_pin(b.display.powerEnable, 0);
  // Power latches. X4/X3 (C3): GPIO13 gates the battery MOSFET and driving it LOW
  // is the real power-off, as CrossPoint does — on battery the whole board goes
  // dark (zero drain; the power button bridges the rail again and we cold-boot),
  // on USB the chip stays up and falls through to deep sleep + GPIO3 wake.
  // Other boards' latches are keep-alive enables (X4 Pro GPIO1): hold them HIGH.
  // Measured before this: ~5 %/h drained while "asleep", same as awake.
#if FREEINK_MCU_C3
  const int latch_level = 0;
#else
  const int latch_level = 1;
#endif
  hold_pin(b.power.latch0, latch_level);
  hold_pin(b.power.latch1, latch_level);
  // Everything not held floats isolated (no leakage through SPI/DC/CS pads); the
  // holds themselves survive deep sleep. Same sequence as PowerManager::deepSleep().
  esp_sleep_config_gpio_isolate();
  gpio_deep_sleep_hold_en();
}

void Xteink::dump_config() {
  ESP_LOGCONFIG(TAG, "Xteink:\n  Board: %s\n  Panel: %ux%u", BoardConfig::ACTIVE.name,
                this->display_.getDisplayWidth(), this->display_.getDisplayHeight());
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace xteink
}  // namespace esphome

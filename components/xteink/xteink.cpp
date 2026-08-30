#include "xteink.h"

#include "esphome/core/log.h"

#include <XteinkDetect.h>
#include <driver/gpio.h>

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

void Xteink::on_powerdown() {
  // Deliberately NOT calling display_.deepSleep() here: the SSD1677 power-off
  // sequence busy-waits on BUSY with a 30 s ceiling, which on the X4 outran the
  // 5 s task watchdog and rebooted the device instead of sleeping it. The panel
  // keeps its image unpowered anyway, and a wake re-initialises it.
  //
  // holdPowerRails() drove the board's power-latch pins at boot but armed no
  // hold, and deep sleep releases an unheld pad. On the X4 that pin (GPIO13)
  // gates the battery MOSFET: left floating it can droop and reset the chip,
  // which shows up as the device "waking" right after going to sleep. Hold the
  // latches at their current level for the duration of the sleep, as
  // CrossPoint and the SDK's PowerManager do.
  bool held = false;
  for (const int8_t pin : {BoardConfig::ACTIVE.power.latch0, BoardConfig::ACTIVE.power.latch1}) {
    if (pin < 0)
      continue;
    gpio_hold_en(static_cast<gpio_num_t>(pin));
    held = true;
  }
  if (held) {
    gpio_deep_sleep_hold_en();
  }
}

void Xteink::dump_config() {
  ESP_LOGCONFIG(TAG, "Xteink:\n  Board: %s\n  Panel: %ux%u", BoardConfig::ACTIVE.name,
                this->display_.getDisplayWidth(), this->display_.getDisplayHeight());
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace xteink
}  // namespace esphome

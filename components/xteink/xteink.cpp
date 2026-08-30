#include "xteink.h"

#include "esphome/core/log.h"

#include <XteinkDetect.h>

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

void Xteink::dump_config() {
  ESP_LOGCONFIG(TAG, "Xteink:\n  Board: %s\n  Panel: %ux%u", BoardConfig::ACTIVE.name,
                this->display_.getDisplayWidth(), this->display_.getDisplayHeight());
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace xteink
}  // namespace esphome

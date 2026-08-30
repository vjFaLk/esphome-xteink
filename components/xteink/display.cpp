#include "display.h"

#include "esphome/core/log.h"

namespace esphome {
namespace xteink {

static const char *const TAG = "xteink.display";

void XteinkDisplay::draw_absolute_pixel_internal(int x, int y, Color color) {
  auto &epd = this->parent_->display();
  if (x < 0 || y < 0 || x >= epd.getDisplayWidth() || y >= epd.getDisplayHeight())
    return;
  uint8_t *buf = epd.getFrameBuffer();
  const uint32_t idx = y * epd.getDisplayWidthBytes() + (x >> 3);
  const uint8_t bit = 0x80 >> (x & 7);
  if (color.is_on()) {
    buf[idx] &= ~bit;  // 0 = black
  } else {
    buf[idx] |= bit;  // 1 = white
  }
}

void XteinkDisplay::update() {
  auto &epd = this->parent_->display();
  if (this->writer_local_.has_value()) {
    (*this->writer_local_)(*this);
  }
  EInkDisplay::RefreshMode mode = EInkDisplay::FULL_REFRESH;
  if (this->refresh_mode_ == HALF_REFRESH || (this->refresh_mode_ == FAST_REFRESH && !this->has_drawn_)) {
    mode = EInkDisplay::HALF_REFRESH;  // the very first frame cannot be a fast (diff) refresh
  } else if (this->refresh_mode_ == FAST_REFRESH) {
    mode = EInkDisplay::FAST_REFRESH;
  }
  epd.displayBuffer(mode);
  epd.clearScreen(0xFF);  // every update() repaints the whole frame
  this->has_drawn_ = true;
  this->update_count++;
}

void XteinkDisplay::dump_config() {
  LOG_DISPLAY("", "Xteink e-paper", this);
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace xteink
}  // namespace esphome

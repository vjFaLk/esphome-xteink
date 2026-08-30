#pragma once

#include "esphome/components/display/display_buffer.h"
#include "xteink.h"

namespace esphome {
namespace xteink {

class XteinkDisplay;
using xteink_writer_t = std::function<void(XteinkDisplay &)>;

class XteinkDisplay : public display::DisplayBuffer {
 public:
  // Same numbering as ngxson/esphome-component-xteink: it.set_refresh_mode(0|1|2)
  enum RefreshMode { FULL_REFRESH = 0, HALF_REFRESH = 1, FAST_REFRESH = 2 };

  void set_parent(Xteink *parent) { this->parent_ = parent; }
  void set_writer(xteink_writer_t &&writer) { this->writer_local_ = writer; }
  void set_refresh_mode(int mode) { this->refresh_mode_ = static_cast<RefreshMode>(mode); }
  /// Put the panel controller into deep sleep (do this before ESP deep sleep).
  void sleep() { this->parent_->display().deepSleep(); }

  display::DisplayType get_display_type() override { return display::DisplayType::DISPLAY_TYPE_BINARY; }
  int get_width_internal() override { return this->parent_->display().getDisplayWidth(); }
  int get_height_internal() override { return this->parent_->display().getDisplayHeight(); }
  void draw_absolute_pixel_internal(int x, int y, Color color) override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  uint64_t update_count{0};  ///< number of panel refreshes so far, usable from the lambda

 protected:
  Xteink *parent_{nullptr};
  optional<xteink_writer_t> writer_local_{};
  RefreshMode refresh_mode_{HALF_REFRESH};
  bool has_drawn_{false};
};

}  // namespace xteink
}  // namespace esphome

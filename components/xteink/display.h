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
  enum GrayscaleStrategy { GRAYSCALE_NATIVE = 0, GRAYSCALE_DITHER = 1, GRAYSCALE_ADAPTIVE = 2 };

  void set_parent(Xteink *parent) { this->parent_ = parent; }
  void set_writer(xteink_writer_t &&writer) { this->writer_local_ = writer; }
  void set_refresh_mode(int mode) { this->refresh_mode_ = static_cast<RefreshMode>(mode); }
  void set_full_refresh_every(uint32_t interval) { this->full_refresh_every_ = interval; }
  void set_grayscale(bool enabled) { this->grayscale_enabled_ = enabled; }
  void set_grayscale_strategy(int strategy) {
    this->grayscale_strategy_ = static_cast<GrayscaleStrategy>(strategy);
  }
  void set_grayscale_settle_time(uint32_t settle_time) { this->grayscale_settle_time_ = settle_time; }
  void set_grayscale_windowed_upload(bool enabled) { this->grayscale_windowed_upload_ = enabled; }
  void postpone_grayscale_settle();
  void prepare_page_change();
  // Render into the host framebuffer without touching controller RAM or the
  // panel. This is used for retained-frame reconstruction and for high-rate UI
  // gestures whose intermediate frames are not meaningful on e-paper.
  void begin_virtual_update();
  void end_virtual_update(bool refresh = false);
  bool virtual_update_active() const { return this->virtual_update_active_; }
  void begin_retained_frame_bootstrap();
  void end_retained_frame_bootstrap();
  bool retained_frame_ready() const { return this->retained_frame_adopted_; }
  bool has_retained_frame_tag() const;
  uint32_t get_retained_frame_tag() const;
  void set_retained_frame_tag(uint32_t tag);
  void clear_retained_frame_tag();
  bool settle_grayscale();
  bool flush_for_deep_sleep();
  // Stop deferred display work without starting another panel transaction.
  // Use after the caller has already observed its required refresh complete.
  void freeze_for_deep_sleep();
  // Run manual full-panel conditioning as scheduler-sized transactions. The
  // caller yields between the 30 transactions, then rebuilds its UI after end.
  bool begin_panel_conditioning();
  uint8_t panel_conditioning_transaction_count() const { return CONDITIONING_TRANSACTION_COUNT; }
  bool condition_panel_step(uint8_t step);
  void end_panel_conditioning();
  display::DisplayType get_display_type() override {
    return this->grayscale_enabled_ ? display::DisplayType::DISPLAY_TYPE_GRAYSCALE
                                    : display::DisplayType::DISPLAY_TYPE_BINARY;
  }
  int get_width_internal() override { return this->parent_->display().getDisplayWidth(); }
  int get_height_internal() override { return this->parent_->display().getDisplayHeight(); }
  void setup() override;
  void loop() override;
  void draw_absolute_pixel_internal(int x, int y, Color color) override;
  void draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr, display::ColorOrder order,
                      display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset, int x_pad) override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  uint64_t update_count{0};  ///< number of panel refreshes so far, usable from the lambda

 protected:
  static constexpr uint8_t CONDITIONING_TRANSACTION_COUNT = 30;
  Xteink *parent_{nullptr};
  optional<xteink_writer_t> writer_local_{};
  RefreshMode refresh_mode_{HALF_REFRESH};
  bool has_drawn_{false};
  bool rgb_input_{false};
  bool buffer_dirty_{false};
  bool grayscale_enabled_{false};
  bool grayscale_available_{false};
  bool grayscale_warning_logged_{false};
  bool grayscale_settle_pending_{false};
  bool native_grayscale_on_panel_{false};
  bool grayscale_has_settled_{false};
  bool interactive_preview_requested_{false};
  bool force_full_refresh_{false};
  bool virtual_update_active_{false};
  bool conditioning_active_{false};
  bool sleep_preparing_{false};
  bool retained_frame_bootstrap_pending_{false};
  bool retained_frame_adopted_{false};
  bool grayscale_dirty_valid_{false};
  bool grayscale_dirty_full_{false};
  bool grayscale_windowed_upload_{false};
  int grayscale_dirty_x0_{0};
  int grayscale_dirty_y0_{0};
  int grayscale_dirty_x1_{0};
  int grayscale_dirty_y1_{0};
  uint32_t grayscale_dirty_generation_{0};
  GrayscaleStrategy grayscale_strategy_{GRAYSCALE_NATIVE};
  uint32_t grayscale_settle_time_{5000};
  uint32_t full_refresh_every_{0};
  uint32_t partial_refresh_count_{0};
  bool refresh_pending_{false};
  EInkDisplay::RefreshMode pending_refresh_mode_{EInkDisplay::FULL_REFRESH};
  uint8_t *gray_luma_{nullptr};
  uint8_t *gray_lsb_{nullptr};
  uint8_t *gray_msb_{nullptr};
  uint8_t *gray_flood_visited_{nullptr};
  uint32_t *gray_flood_queue_{nullptr};
  size_t gray_flood_queue_capacity_{0};
  size_t gray_buffer_size_{0};
  size_t gray_luma_size_{0};

  void render_grayscale_planes_();
  void render_grayscale_overlay_();
  bool render_ordered_dither_();
  void mark_grayscale_dirty_(int x, int y);
  void mark_grayscale_dirty_rect_(int x, int y, int w, int h);
  void mark_grayscale_full_();
  void clear_grayscale_dirty_();
  void expand_grayscale_dirty_to_neighbors_();
  bool is_grayscale_neighbor_(int x, int y) const;
  bool uses_native_grayscale_planes_() const { return this->grayscale_strategy_ != GRAYSCALE_DITHER; }
  void schedule_grayscale_settle_();
  void finalize_refresh_(EInkDisplay::RefreshMode mode);
  bool finish_pending_refresh_(bool wait);
};

}  // namespace xteink
}  // namespace esphome

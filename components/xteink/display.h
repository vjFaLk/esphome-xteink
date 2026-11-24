#pragma once
#include <Uc8279X4Transport.h>
#include <xteink/gray_refresh.h>

#include <optional>

#include "esphome/components/display/display_buffer.h"
#include "panel_sleep.h"
#include "xteink.h"

namespace esphome {
namespace xteink {
class XteinkDisplay;
using xteink_writer_t = std::function<void(XteinkDisplay&)>;
class XteinkDisplay : public display::DisplayBuffer {
 public:
  enum RefreshMode { FULL_REFRESH = 0, HALF_REFRESH = 1, FAST_REFRESH = 2 };
  void set_parent(Xteink* p) { parent_ = p; }
  void set_writer(xteink_writer_t&& w) { writer_ = std::move(w); }
  void set_refresh_mode(int m) { refresh_mode_ = m; }
  void set_full_refresh_every(uint32_t n) { full_every_ = n; }
  void set_grayscale(bool v) { grayscale_ = v; }
  void set_grayscale_strategy(int) {
  }  // legacy YAML accepted; new modes await calibration
  void set_grayscale_settle_time(uint32_t) {}
  void set_grayscale_windowed_upload(bool enabled) { windowed_upload_ = enabled; }
  bool set_rendering_mode(int mode);
  void set_initial_rendering_mode(int mode) { requested_mode_ = mode; }
  void set_grayscale_update_margin(uint8_t pixels) {
    grayscale_update_margin_ = std::min<uint8_t>(pixels, 2);
  }
  bool load_panel_calibration(const ::x4render::PanelCalibration& calibration);
  uint32_t last_region_bytes() const { return masked_bytes_; }
  void prepare_page_change();
  // Select a one-shot absolute update in physical panel coordinates.
  bool select_region(uint16_t x, uint16_t y, uint16_t width, uint16_t height, bool bw_only = false);
  void clear_region();
  bool region_update_finished() const;
  bool region_update_succeeded() const;
  bool has_known_frame() const {
    return frames_.knowledge == ::x4render::Knowledge::Known;
  }
  bool refresh_pending() const { return pending_; }
  void begin_touch_preview();
  void end_touch_preview();
  void begin_virtual_update() { virtual_ = true; }
  void end_virtual_update(bool refresh = false) {
    virtual_ = false;
    if (refresh) update();
  }
  bool freeze_for_deep_sleep();

  display::DisplayType get_display_type() override {
    return grayscale_ ? display::DISPLAY_TYPE_GRAYSCALE
                      : display::DISPLAY_TYPE_BINARY;
  }
  int get_width_internal() override {
    return parent_->display().getDisplayWidth();
  }
  int get_height_internal() override {
    return parent_->display().getDisplayHeight();
  }
  void setup() override;
  void loop() override;
  void update() override;
  void draw_absolute_pixel_internal(int x, int y, Color color) override;
  void draw_pixels_at(int, int, int, int, const uint8_t*, display::ColorOrder,
                      display::ColorBitness, bool, int, int, int) override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }
  uint64_t update_count{0};

 protected:
  bool finish_normal_(bool wait);
  bool acquire_controller_();
  bool begin_masked_refresh_(bool force, bool preview);
  void schedule_touch_settle_();
  void reset_touch_preview_();
  bool masked_mode_() const { return requested_mode_ != 0; }
  bool gray_mode_() const { return requested_mode_ == 1 || requested_mode_ == 2; }
  bool poll_masked_refresh_();
  Xteink* parent_{nullptr};
  optional<xteink_writer_t> writer_{};
  ::x4render::Frames frames_;
  ::x4render::GrayRefresh masked_refresh_{frames_};
  std::optional<freeink::Uc8279X4Transport> transport_;
  bool grayscale_{false}, ready_{false}, rgb_{false}, dirty_{false},
      virtual_{false}, frozen_{false};
  bool pending_{false}, has_frame_{false}, force_full_{false};
  enum class TunedState : uint8_t { Idle, Driving, PoweringOff, Held };
  TunedState tuned_state_{TunedState::Idle};
  bool tuned_completed_{false};
  bool redraw_requested_{false};
  enum class TouchPhase : uint8_t { Idle, Dragging, Released };
  TouchPhase touch_phase_{TouchPhase::Idle};
  bool driving_touch_preview_{false};
  enum class RegionState : uint8_t { Normal, Selected, Running, Complete, Failed };
  RegionState region_state_{RegionState::Normal};
  ::x4render::Rect selected_region_{};
  bool region_bw_only_{false};
  bool driving_region_{false};
  bool windowed_upload_{false};
  uint32_t masked_started_{0}, masked_duration_{0}, masked_bytes_{0};
  std::optional<uint8_t> grayscale_update_margin_;
  int refresh_mode_{HALF_REFRESH}, requested_mode_{0};
  uint32_t full_every_{0}, partials_{0};
};
}  // namespace xteink
}  // namespace esphome

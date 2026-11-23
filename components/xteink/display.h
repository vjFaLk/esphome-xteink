#pragma once
#include <Uc8279X4Transport.h>
#include <xteink/calibration.h>

#include <optional>

#include "esphome/components/display/display_buffer.h"
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
  void set_grayscale_windowed_upload(bool) {}
  bool set_rendering_mode(int mode);
  void postpone_grayscale_settle() {}
  void prepare_page_change() { force_full_ = true; }
  void begin_virtual_update() { virtual_ = true; }
  void end_virtual_update(bool refresh = false) {
    virtual_ = false;
    if (refresh) update();
  }
  bool virtual_update_active() const { return virtual_; }
  void begin_retained_frame_bootstrap() { begin_virtual_update(); }
  void end_retained_frame_bootstrap() { end_virtual_update(); }
  bool retained_frame_ready() const { return false; }
  bool has_retained_frame_tag() const { return false; }
  uint32_t get_retained_frame_tag() const { return 0; }
  void set_retained_frame_tag(uint32_t) {}
  void clear_retained_frame_tag() {}
  bool settle_grayscale() { return false; }
  bool flush_for_deep_sleep();
  void freeze_for_deep_sleep();
  bool begin_panel_conditioning() { return begin_calibration(); }
  uint8_t panel_conditioning_transaction_count() const { return 1; }
  bool condition_panel_step(uint8_t) { return run_calibration(false, 0, true); }
  void end_panel_conditioning() { end_calibration(); }
  bool begin_calibration();
  bool run_calibration(bool rebuild = true, bool rotate = false,
                       bool conditioning = false);
  void end_calibration();
  bool calibration_power_off() {
    return calibration_active_ && lab_->powerOff(millis());
  }
  bool calibration_active() const { return calibration_active_; }
  bool calibration_exit_failed() const { return exit_failed_; }
  bool calibration_busy() const { return lab_ && lab_->busy(); }
  void set_candidate_frames(uint8_t i, uint8_t value) {
    if (i < 3) candidate_frames_[i] = value;
  }
  void set_pulse_rail(uint8_t value) { pulse_rail_ = value; }
  void set_pulse_opposite(uint8_t value) { pulse_opposite_ = value; }
  void set_pulse_repeats(uint8_t value) { pulse_repeats_ = value; }
  void set_pulse_neutral(uint8_t value) { pulse_neutral_ = value; }
  void set_hold_rail(uint8_t value) { hold_rail_ = value; }
  void set_bw_mode(uint8_t mode) { bw_mode_ = mode; }
  void set_bw_texture(::x4render::BwTexture value) { bw_texture_ = value; }
  void set_bw_split_guard(bool value) { bw_split_guard_ = value; }
  void set_bw_activation_frames(uint8_t v) { bw_tuning_.shakeFrames = v; }
  void set_bw_activation_cycles(uint8_t v) { bw_tuning_.shakeCycles = v; }
  void set_bw_reference_frames(uint8_t v) { bw_tuning_.whiteReference = v; }
  void set_bw_drive_trim(int8_t v) { bw_tuning_.driveTrim = v; }
  void set_bw_pause_trim(int8_t v) { bw_tuning_.pauseTrim = v; }
  void set_bw_pause(uint16_t v) { bw_preparation_.pauseMs = v; }
  void set_bw_factory_prime(bool v) { bw_preparation_.factoryPrime = v; }
  void set_temp_override(bool v) { temp_override_ = v; }
  void set_panel_temperature(int v) { manual_temp_c_ = v; }
  std::string calibration_temperature() const;
  bool show_calibration_image(uint8_t image, uint32_t argument = 0);
  bool repeat_calibration_image();
  std::string calibration_image() const;
  std::string calibration_bw_load() const;
  std::string calibration_waveform(uint8_t row, bool hex = false,
                                   bool preparation = false) const;
  std::string calibration_assignments() const;
  void log_calibration_waveforms() const;
  void log_calibration_timing();
  void log_panel_mtp();
  void set_panel_power_setup(uint8_t value);
  std::string panel_power_setup_status() const;
  void set_vertical_window(bool v) { vertical_window_ = v; }
  void set_cropped_upload(bool v) { cropped_upload_ = v; }
  void set_detailed_logging(bool v) { detailed_ = v; }
  const char* calibration_result() const {
    return factory_error_ ? factory_error_ : lab_ ? lab_->result() : "idle";
  }
  uint32_t calibration_duration() const { return lab_ ? lab_->duration : 0; }
  uint32_t calibration_bytes() const {
    return lab_ ? lab_->uploadedBytes() : 0;
  }
  uint32_t calibration_driven() const { return lab_ ? lab_->driveCount : 0; }
  uint32_t calibration_cycles() const { return lab_ ? lab_->cycles : 0; }
  std::string calibration_region() const;
  const char* calibration_pipeline() const;
  void set_measured_shade(uint8_t i, uint8_t luma) {
    if (i < 6) {
      measured_[i] = luma;
      measured_mask_ |= 1u << i;
    }
  }
  std::string calibration_order() const;
  bool set_shade_sequence(const std::string& value);
  void set_dither_luminance(uint8_t id, uint8_t luma) { set_measured_shade(id, luma); }
  void set_dither_curve(uint8_t pair, uint8_t step, uint8_t value) {
    if (pair < coverage_.size() && step < coverage_[pair].size()) coverage_[pair][step] = value;
  }


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
  bool configure_lab_();
  int factory_temperature_() const;
  ::x4render::Palette diagnostic_palette_() const;
  Xteink* parent_{nullptr};
  optional<xteink_writer_t> writer_{};
  ::x4render::Frames frames_;
  std::optional<freeink::Uc8279X4Transport> transport_;
  std::optional<::x4render::Calibration<freeink::Uc8279X4Transport>> lab_;
  bool grayscale_{false}, ready_{false}, rgb_{false}, dirty_{false},
      virtual_{false}, frozen_{false};
  bool pending_{false}, has_frame_{false}, force_full_{false},
      calibration_active_{false}, exit_requested_{false};
  bool exit_shutdown_attempted_{false}, exit_failed_{false};
  uint8_t last_test_image_{3};
  uint8_t panel_power_setup_{2}, bw_mode_{2};
  ::x4render::BwTexture bw_texture_{::x4render::BwTexture::Bayer};
  bool bw_split_guard_{true};
  bool temp_override_{false};
  int manual_temp_c_{30};
  const char* factory_error_{nullptr};
  uint32_t last_image_argument_{0};
  std::array<freeink::Uc8279X4Transport::TraceEvent, 96> timing_snapshot_{};
  size_t timing_size_{0}, timing_index_{0};
  bool vertical_window_{false}, cropped_upload_{false}, detailed_{false};
  int refresh_mode_{HALF_REFRESH}, requested_mode_{0};
  uint32_t full_every_{0}, partials_{0};
  ::x4render::BwPreparation bw_preparation_{};
  ::x4render::FactoryBwTuning bw_tuning_{1, 2, 5, 0, 3};
  std::array<uint8_t, 6> measured_{};
  std::array<uint8_t, 6> shade_sequence_{1,2,3,4,5,6};
  std::array<::x4render::CoverageCurve, 7> coverage_{};
  uint8_t measured_mask_{0};
  uint8_t candidate_frames_[3]{2, 3, 4}, pulse_rail_{2}, pulse_opposite_{3},
      pulse_repeats_{1}, pulse_neutral_{1}, hold_rail_{0};
};
}  // namespace xteink
}  // namespace esphome

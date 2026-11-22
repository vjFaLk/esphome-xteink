#include "display.h"

#include <cstdio>
#include <cstring>

#include "esphome/core/log.h"
namespace esphome {
namespace xteink {
static const char* const TAG = "xteink.display";
void XteinkDisplay::setup() {
  disable_loop();
  if (get_width_internal() == 800 && get_height_internal() == 480)
    ready_ = frames_.allocate();
  else
    ready_ = false;
  if (!ready_ && grayscale_) {
    mark_failed();
    ESP_LOGE(TAG, "Rendering storage unavailable");
  }
}
bool XteinkDisplay::set_rendering_mode(int mode) {
  if (mode < 0 || mode > 2) return false;
  requested_mode_ = mode;
  if (mode) {
    ESP_LOGW(TAG,
             "Mode %d awaits measured palette and validated hold; B/W preview "
             "remains active",
             mode);
    return false;
  }
  force_full_ = true;
  return true;
}
void XteinkDisplay::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (calibration_active_ || frozen_ || pending_ || x < 0 || y < 0 ||
      x >= get_width_internal() || y >= get_height_internal())
    return;
  auto& epd = parent_->display();
  const uint8_t luma =
      uint8_t((77U * color.r + 150U * color.g + 29U * color.b + 128U) >> 8);
  const uint8_t output = rgb_ ? luma : uint8_t(255U - luma);
  if (ready_) {
    const size_t i = size_t(y) * 800 + x;
    dirty_ |= frames_.target[i] != output;
    frames_.draw(i, output);
  } else {
    auto* data = epd.getFrameBuffer();
    const size_t i = size_t(y) * epd.getDisplayWidthBytes() + (x >> 3);
    const uint8_t mask = 0x80u >> (x & 7), old = data[i];
    if (rgb_ ? luma < 128 : color.is_on())
      data[i] &= uint8_t(~mask);
    else
      data[i] |= mask;
    dirty_ |= old != data[i];
  }
}
void XteinkDisplay::draw_pixels_at(int x, int y, int w, int h, const uint8_t* p,
                                   display::ColorOrder order,
                                   display::ColorBitness bitness, bool endian,
                                   int xo, int yo, int pad) {
  rgb_ = true;
  display::Display::draw_pixels_at(x, y, w, h, p, order, bitness, endian, xo,
                                   yo, pad);
  rgb_ = false;
}
void XteinkDisplay::update() {
  if (calibration_active_ || frozen_ || pending_) return;
  auto& epd = parent_->display();
  if (writer_.has_value()) {
    if (auto_clear_enabled_) {
      if (ready_)
        frames_.target.clear(255);
      else
        epd.clearScreen(255);
      dirty_ = true;
    }
    (*writer_)(*this);
  }
  if (virtual_ || !dirty_) return;
  if (ready_) {
    ::x4render::Settings s;
    if (!frames_.plan(s)) return;
    if (!frames_.driven && has_frame_ && !force_full_) {
      std::memcpy(frames_.displayed.data(), frames_.target.data(),
                  ::x4render::Pixels);
      dirty_ = false;
      return;
    }
    auto* plane = epd.getFrameBuffer();
    std::memset(plane, 0, ::x4render::PlaneBytes);
    for (uint16_t y = 0; y < 480; ++y)
      for (uint16_t x = 0; x < 800; ++x) {
        const size_t i = size_t(y) * 800 + x;
        if (::x4render::quantize(frames_.target[i], x, y, s).base)
          plane[i / 8] |= 0x80u >> (i % 8);
      }
    frames_.preparation();
  }
  const bool full = !has_frame_ || force_full_ ||
                    refresh_mode_ == FULL_REFRESH ||
                    (full_every_ && ++partials_ >= full_every_);
  if (full) partials_ = 0;
  force_full_ = false;
  dirty_ = false;
  auto mode = full
                  ? EInkDisplay::FULL_REFRESH
                  : (refresh_mode_ == HALF_REFRESH ? EInkDisplay::HALF_REFRESH
                                                   : EInkDisplay::FAST_REFRESH);
  if (epd.supportsAsyncRefresh()) {
    epd.displayBufferAsync(mode);
    pending_ = true;
    enable_loop();
  } else {
    epd.displayBuffer(mode);
    pending_ = true;
    finish_normal_(true);
  }
}
bool XteinkDisplay::finish_normal_(bool wait) {
  if (!pending_) return true;
  auto& epd = parent_->display();
  if (!wait && epd.refreshBusy()) return false;
  epd.waitRefreshComplete();
  epd.syncWriteBufferFromActive();
  if (ready_) {
    frames_.prepared(true);
    frames_.commit();
  }
  pending_ = false;
  has_frame_ = true;
  update_count++;
  if (!calibration_active_) disable_loop();
  return true;
}
bool XteinkDisplay::begin_calibration() {
  if (calibration_active_) return true;
  if (BoardConfig::ACTIVE.displayController !=
          BoardConfig::DisplayController::UC8279 ||
      get_width_internal() != 800 || get_height_internal() != 480)
    return false;
  finish_normal_(true);
  auto* bus = parent_->display().beginControllerSession();
  if (!bus) return false;
  if (!transport_) {
    transport_.emplace(*bus);
    lab_.emplace(*transport_);
  }
  if (!lab_->allocate()) {
    parent_->display().endControllerSession();
    return false;
  }
  transport_->reset();
  lab_->invalidate();
  calibration_active_ = true;
  exit_requested_ = false;
  frozen_ = false;
  enable_loop();
  ESP_LOGW(TAG,
           "Waveform lab owns display. Rail/hold/end semantics UNCALIBRATED. "
           "No automatic cycles.");
  return true;
}
bool XteinkDisplay::run_calibration(bool rebuild, uint8_t rotation,
                                    bool conditioning) {
  if (!calibration_active_ || lab_->busy() || exit_requested_) return false;
  for (uint8_t i = 0; i < 3; ++i) {
    const auto old = lab_->parameters[i];
    if (!rebuild && !conditioning &&
        (old.rail != pulse_rail_ || old.frames != candidate_frames_[i] ||
         old.opposite != pulse_opposite_ || old.repeats != pulse_repeats_ ||
         old.neutral != pulse_neutral_)) {
      ESP_LOGW(TAG,
               "Parameters changed: rebuild B/W starts before repeat so labels "
               "remain accurate");
      return false;
    }
  }
  for (uint8_t i = 0; i < 3; ++i)
    lab_->parameters[i] = {pulse_rail_, candidate_frames_[i], pulse_opposite_,
                           pulse_repeats_, pulse_neutral_};
  lab_->holdRail = hold_rail_;
  lab_->verticalWindowValidated = vertical_window_;
  lab_->croppedUploadValidated = cropped_upload_;
  return lab_->start(millis(), rebuild, rotation, conditioning);
}
void XteinkDisplay::end_calibration() {
  if (!calibration_active_) return;
  if (lab_->busy()) {
    exit_requested_ = true;
    return;
  }
  parent_->display().endControllerSession();
  calibration_active_ = false;
  exit_requested_ = false;
  frames_.resetController();
  force_full_ = true;
  has_frame_ = false;
  dirty_ = true;
  disable_loop();
}
void XteinkDisplay::loop() {
  if (!calibration_active_) {
    finish_normal_(false);
    return;
  }
  if (lab_->poll(millis())) {
    ESP_LOGI(TAG, "Lab %s: %u ms, %u bytes, %u driven, cycle %u",
             lab_->result(), unsigned(lab_->duration),
             unsigned(lab_->uploadedBytes()), unsigned(lab_->driveCount),
             unsigned(lab_->cycles));
    if (detailed_) ESP_LOGI(TAG, "%s", calibration_region().c_str());
    if (exit_requested_) end_calibration();
  }
}
const char* XteinkDisplay::calibration_pipeline() const {
  if (!calibration_active_ || !lab_) return "boiler_bw_preview";
  using J = ::x4render::Calibration<freeink::Uc8279X4Transport>::Job;
  switch (lab_->job()) {
    case J::PowerOn:
      return "calibration_power_on";
    case J::Base:
      return "otp_full_bw_starts";
    case J::Settle:
      return "raw_candidate_settle";
    case J::Conditioning:
      return "raw_full_panel_pulse_sequence";
    case J::PowerOff:
      return "calibration_power_off";
    case J::Failed:
      return "failed_completion_unknown";
    default:
      return "calibration_idle_hold";
  }
}
std::string XteinkDisplay::calibration_region() const {
  if (!lab_) return "idle";
  char text[160];
  auto r = lab_->requested, e = lab_->expanded;
  std::snprintf(text, sizeof(text),
                "requested %u,%u %ux%u; activation %u,%u %ux%u; hold code %u",
                r.x, r.y, r.width, r.height, 0, vertical_window_ ? e.y : 0, 800,
                vertical_window_ ? e.height : 480, hold_rail_);
  return text;
}
std::string XteinkDisplay::calibration_order() const {
  if (measured_mask_ != 63)
    return "Enter six measured luminances: 0=black 255=white. Ordering "
           "unknown.";
  std::array<uint8_t, 6> order{0, 1, 2, 3, 4, 5};
  std::sort(order.begin(), order.end(), [this](uint8_t a, uint8_t b) {
    return measured_[a] < measured_[b];
  });
  std::string result = "B0";
  uint8_t last = 0;
  char item[40];
  for (uint8_t i : order) {
    std::snprintf(item, sizeof(item), " | %c-%c=%u gap%u", char('A' + i % 3),
                  i < 3 ? 'B' : 'W', measured_[i], measured_[i] - last);
    result += item;
    last = measured_[i];
  }
  std::snprintf(item, sizeof(item), " | W255 gap%u", 255 - last);
  return result + item;
}
bool XteinkDisplay::flush_for_deep_sleep() {
  if (calibration_active_ || virtual_) return false;
  finish_normal_(true);
  update();
  finish_normal_(true);
  freeze_for_deep_sleep();
  return has_frame_;
}
void XteinkDisplay::freeze_for_deep_sleep() {
  if (!calibration_active_) {
    finish_normal_(true);
    frozen_ = true;
  }
}
void XteinkDisplay::dump_config() {
  LOG_DISPLAY("", "Xteink display", this);
  ESP_LOGCONFIG(
      TAG,
      "B/W ordered preview; grayscale modes gated on physical calibration");
}
}  // namespace xteink
}  // namespace esphome

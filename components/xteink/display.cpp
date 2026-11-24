#include "display.h"

#include <cstdio>
#include <cstring>
#include <esp_attr.h>
#include <XteinkDetect.h>

#include "esphome/core/application.h"
#include "esphome/core/log.h"
namespace esphome {
namespace xteink {
RTC_DATA_ATTR static PanelSleepState sleep_panel{};
PanelSleepState& panel_sleep_state() { return sleep_panel; }
static const char* const TAG = "xteink.display";
static constexpr uint32_t TouchSettleDelayMs = 160;
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
  if (mode < 0 || mode > 3) return false;
  if (mode && BoardConfig::ACTIVE.displayController != BoardConfig::DisplayController::UC8279)
    return false;
  if (requested_mode_ == mode) return true;
  reset_touch_preview_();
  requested_mode_ = mode;
  force_full_ = true;
  dirty_ = true;
  return true;
}
bool XteinkDisplay::load_panel_calibration(const ::x4render::PanelCalibration& calibration) {
  if (pending_ || frozen_ || tuned_state_ == TunedState::Held ||
      !masked_refresh_.loadCalibration(calibration)) return false;
  reset_touch_preview_();
  force_full_ = true;
  dirty_ |= has_frame_;
  return true;
}
void XteinkDisplay::reset_touch_preview_() {
  touch_phase_ = TouchPhase::Idle;
  cancel_timeout("touch_settle");
}
void XteinkDisplay::prepare_page_change() {
  reset_touch_preview_();
  force_full_ = true;
}
bool XteinkDisplay::select_region(uint16_t x, uint16_t y, uint16_t width, uint16_t height, bool bw_only) {
  if (!ready_ || BoardConfig::ACTIVE.displayController != BoardConfig::DisplayController::UC8279 ||
      frozen_ || pending_ ||
      tuned_state_ == TunedState::Held || !width || !height ||
      x + width > ::x4render::Width || y + height > ::x4render::Height) return false;
  reset_touch_preview_();
  selected_region_ = {x, y, width, height};
  region_bw_only_ = bw_only;
  region_state_ = RegionState::Selected;
  dirty_ = true;
  return true;
}
void XteinkDisplay::clear_region() {
  selected_region_ = {};
  region_bw_only_ = false;
  region_state_ = RegionState::Normal;
  force_full_ = true;
}
bool XteinkDisplay::region_update_finished() const {
  return region_state_ == RegionState::Complete || region_state_ == RegionState::Failed;
}
bool XteinkDisplay::region_update_succeeded() const {
  return region_state_ == RegionState::Complete;
}
void XteinkDisplay::begin_touch_preview() {
  if (!ready_ || frozen_) return;
  cancel_timeout("touch_settle");
  touch_phase_ = TouchPhase::Dragging;
}
void XteinkDisplay::end_touch_preview() {
  if (touch_phase_ != TouchPhase::Dragging) return;
  touch_phase_ = TouchPhase::Released;
  schedule_touch_settle_();
}
void XteinkDisplay::schedule_touch_settle_() {
  if (touch_phase_ != TouchPhase::Released || pending_) return;
  // Let LVGL flush the final slider position after the previous drive releases
  // the display. A second press cancels this settle without losing its mask.
  set_timeout("touch_settle", TouchSettleDelayMs, [this]() {
    touch_phase_ = TouchPhase::Idle;
    dirty_ |= frames_.hasPreview();
    redraw_requested_ = true;
    enable_loop();
  });
}
void XteinkDisplay::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (frozen_ || pending_ || tuned_state_ == TunedState::Held || x < 0 || y < 0 ||
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
  if (frozen_ || pending_ || tuned_state_ == TunedState::Held) return;
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
  if (region_state_ != RegionState::Normal) {
    if (region_state_ == RegionState::Selected) {
      dirty_ = false;
      region_state_ = begin_masked_refresh_(false, false) ? RegionState::Running : RegionState::Failed;
    }
    return;
  }
  if (!force_full_ &&
      (touch_phase_ == TouchPhase::Released ||
       (touch_phase_ == TouchPhase::Dragging && !gray_mode_()))) return;
  if (masked_mode_() && ready_) {
    dirty_ = false;
    const bool preview = gray_mode_() && touch_phase_ == TouchPhase::Dragging && !force_full_ &&
                         frames_.knowledge == ::x4render::Knowledge::Known;
    const bool periodic = !preview && !frames_.hasPreview() && full_every_ && partials_ >= full_every_;
    const bool force = force_full_ || refresh_mode_ == FULL_REFRESH || periodic;
    if (begin_masked_refresh_(force, preview)) {
      if (!preview) {
        force_full_ = false;
        if (pending_) partials_ = force ? 0 : partials_ + 1;
      }
    } else if (preview || frames_.hasPreview()) {
      dirty_ = true;
    }
    return;
  }
  if (ready_) {
    ::x4render::Settings s;
    if (!frames_.plan(s, force_full_)) return;
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
  if (tuned_state_ != TunedState::Idle) {
    while (tuned_state_ != TunedState::Held && !poll_masked_refresh_()) {
      if (!wait) return false;
      App.feed_wdt();
      delay(1);
    }
    return tuned_state_ == TunedState::Idle;
  }
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
  disable_loop();
  return true;
}
bool XteinkDisplay::acquire_controller_() {
  auto* bus = parent_->display().beginControllerSession();
  if (!bus) return false;
  if (!transport_) {
    transport_.emplace(*bus, []() -> uint32_t { return millis(); });
  }
  transport_->clearTrace();
  transport_->reset();
  return true;
}
bool XteinkDisplay::begin_masked_refresh_(bool force, bool preview) {
  using namespace ::x4render;
  const bool regional = region_state_ == RegionState::Selected;
  const bool use_gray = !regional || !region_bw_only_;
  const bool stable = use_gray && requested_mode_ == 1;
  const bool relative = use_gray && requested_mode_ == 2;
  auto settings = masked_refresh_.settings(stable ? Mode::SingleSettle : relative ? Mode::DitherSettle : Mode::BwDither);
  if (grayscale_update_margin_.has_value() && (stable || relative))
    settings.margin = *grayscale_update_margin_;
  const bool planned = regional ? frames_.planRegion(selected_region_, settings)
      : preview ? frames_.planPreview(settings.margin) : frames_.plan(settings, force);
  if (!planned) {
    ESP_LOGW(TAG, "Could not plan %s refresh in render mode %d", regional ? "regional" : "full or differential",
             requested_mode_);
    return false;
  }
  if (!frames_.driven) return preview || frames_.commit();

  const auto snapshot = freeink::getXteinkPanelMtpSnapshot();
  const int temp = snapshot.temperatureC != -128 ? snapshot.temperatureC : 30;
  const auto profile = freeink::observedX4FactoryProfile(
      snapshot.data, snapshot.size, snapshot.verified,
      freeink::getXteinkDisplayProbeDiag().ver[2], temp);
  const auto drive_plan = profile.power.valid
      ? masked_refresh_.planFactory(profile.rows, regional || windowed_upload_, preview)
      : GrayRefresh::FactoryPlan::UnsupportedWaveform;
  if (drive_plan != GrayRefresh::FactoryPlan::Ready) {
    frames_.phase = Phase::Idle;
    ESP_LOGW(TAG, "%s", drive_plan == GrayRefresh::FactoryPlan::OverBudget
        ? "Source budget exceeds three zones" : "Factory table or pulse profile unavailable");
    return false;
  }
  if (!acquire_controller_()) {
    frames_.phase = Phase::Idle;
    return false;
  }
  transport_->setPanelPower(profile.power, masked_refresh_.calibration().factoryPowerSetup);
  transport_->setCalibrationTemperature(temp);
  masked_started_ = millis();
  if (!masked_refresh_.start(*transport_, masked_started_)) {
    parent_->display().endControllerSession();
    frames_.fail();
    return false;
  }
  driving_touch_preview_ = preview;
  driving_region_ = regional;
  tuned_state_ = TunedState::Driving;
  pending_ = true;
  enable_loop();
  return true;
}
bool XteinkDisplay::poll_masked_refresh_() {
  using Status = freeink::Uc8279X4Transport::Status;
  if (tuned_state_ == TunedState::Driving) {
    if (!masked_refresh_.poll(*transport_, millis())) return false;
    tuned_completed_ = masked_refresh_.stage == ::x4render::GrayRefresh::Stage::Done;
    // Power-off is a separate transaction. Keep the useful render result.
    if (transport_->status() != Status::Off && transport_->powerOff(millis())) {
      tuned_state_ = TunedState::PoweringOff;
      return false;
    }
  } else if (transport_->poll(millis()) == freeink::Uc8279X4Transport::Result::Pending) {
    return false;
  }
  const bool preview = driving_touch_preview_;
  const bool regional = driving_region_;
  masked_duration_ = millis() - masked_started_;
  masked_bytes_ = transport_->uploadedBytes();
  driving_touch_preview_ = false;
  driving_region_ = false;
  if (transport_->status() != Status::Off) {
    tuned_state_ = TunedState::Held;
    frames_.fail();
    if (regional && region_state_ == RegionState::Running) region_state_ = RegionState::Failed;
    pending_ = false;
    has_frame_ = false;
    disable_loop();
    ESP_LOGE(TAG, "Masked refresh power-off unconfirmed");
    return true;
  }
  parent_->display().endControllerSession();
  if (regional || !has_frame_ || !tuned_completed_)
    ESP_LOGD(TAG, "%s refresh %s, %u bytes in %u ms",
             regional ? "Regional" : "Frame", tuned_completed_ ? "complete" : "failed",
             unsigned(masked_bytes_), unsigned(masked_duration_));
  if (regional) {
    // A regional update does not establish the appearance of the rest of
    // the display. The next normal update must build a fresh full frame.
    frames_.resetController();
    has_frame_ = false;
    if (region_state_ == RegionState::Running)
      region_state_ = tuned_completed_ ? RegionState::Complete : RegionState::Failed;
    if (tuned_completed_) update_count++;
  } else if (tuned_completed_ && (preview ? frames_.commitPreview() : frames_.commit())) {
    has_frame_ = true;
    update_count++;
  } else {
    frames_.fail();
    has_frame_ = false;
  }
  pending_ = false;
  tuned_state_ = TunedState::Idle;
  disable_loop();
  return true;
}
void XteinkDisplay::loop() {
  if (!finish_normal_(false)) return;
  schedule_touch_settle_();
  if (redraw_requested_) {
    redraw_requested_ = false;
    update();
  }
  if (!pending_) disable_loop();
}
bool XteinkDisplay::freeze_for_deep_sleep() {
  if (!finish_normal_(true)) return false;
  if (frames_.hasPreview()) {
    reset_touch_preview_();
    dirty_ = true;
    update();
    if (!finish_normal_(true) || frames_.hasPreview()) return false;
  }
  reset_touch_preview_();
  frozen_ = true;
  return frozen_;
}
void XteinkDisplay::dump_config() {
  LOG_DISPLAY("", "Xteink display", this);
  ESP_LOGCONFIG(
      TAG,
      "X4 Pro renderer with B/W and calibrated grayscale profiles");
}
}  // namespace xteink
}  // namespace esphome

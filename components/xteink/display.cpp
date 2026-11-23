#include "display.h"

#include <cstdio>
#include <cstring>
#include <XteinkDetect.h>

#include "esphome/core/application.h"
#include "esphome/core/log.h"
namespace esphome {
namespace xteink {
static const char* const TAG = "xteink.display";
static constexpr int TunedBwMode = 3;
static constexpr uint32_t TouchSettleDelayMs = 160;
static constexpr ::x4render::FactoryBwTuning TunedBoilerWaveform{1, 2, 5, 0, 3};
static constexpr std::array<::x4render::Pulse, 3> RelativePulses{{
    {2, 2, 3, 1, 1}, {2, 3, 3, 1, 1}, {2, 4, 3, 1, 1}}};
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
  if (mode < 0 || mode > TunedBwMode) return false;
  if (mode && BoardConfig::ACTIVE.displayController != BoardConfig::DisplayController::UC8279)
    return false;
  if (mode == 1 && !::x4render::measuredStablePalette(stable_luminance_).calibrated) return false;
  if (mode == 2 && !diagnostic_palette_().calibrated) return false;
  if (requested_mode_ == mode) return true;
  reset_touch_preview_();
  requested_mode_ = mode;
  force_full_ = true;
  dirty_ = true;
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
void XteinkDisplay::begin_touch_preview() {
  if (!ready_ || calibration_active_ || frozen_ || render_demo_) return;
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
    update();
  });
}
void XteinkDisplay::refresh_rendering_mode() {
  if (calibration_active_ || !has_frame_) return;
  if (pending_)
    redraw_requested_ = true;
  else
    update();
}
bool XteinkDisplay::set_render_demo(bool enabled) {
  if (render_demo_ == enabled) return false;
  reset_touch_preview_();
  render_demo_ = enabled;
  force_full_ = dirty_ = true;
  if (enabled) refresh_rendering_mode();
  return true;
}
void XteinkDisplay::set_render_demo_marker(bool enabled) {
  if (render_demo_marker_ == enabled) return;
  render_demo_marker_ = enabled;
  if (!render_demo_) return;
  dirty_ = true;
  refresh_rendering_mode();
}
void XteinkDisplay::paint_demo_() {
  using namespace ::x4render;
  for (uint16_t y = 0; y < Height; ++y)
    for (uint16_t x = 0; x < Width; ++x) {
      uint8_t value = 255;
      if (y < 392)
        value = landscapeLuma(size_t(y) * Height / 392 * Width + x);
      else if (y < 436) {
        const uint8_t swatch = x / 160;
        value = swatch == 0 ? 0 : swatch == 4 ? 255 :
            requested_mode_ == 1 ? stable_luminance_[swatch - 1] : swatch * 64;
      }
      else if (y >= 449 && y < 470 && render_demo_marker_ && x >= 742 && x < 764)
        value = requested_mode_ == 1 ? stable_luminance_[1] : 128;
      frames_.draw(size_t(y) * Width + x, value);
    }
}
void XteinkDisplay::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (calibration_active_ || frozen_ || pending_ || render_demo_ || tuned_state_ == TunedState::Held || x < 0 || y < 0 ||
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
  if (calibration_active_ || frozen_ || pending_ || tuned_state_ == TunedState::Held) return;
  auto& epd = parent_->display();
  if (render_demo_ && ready_) {
    if (dirty_) paint_demo_();
  } else if (writer_.has_value()) {
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
  if (!force_full_ &&
      (touch_phase_ == TouchPhase::Released ||
       (touch_phase_ == TouchPhase::Dragging && !gray_mode_()))) return;
  if (gray_mode_() && ready_) {
    dirty_ = false;
    const bool preview = touch_phase_ == TouchPhase::Dragging && !force_full_ &&
                         frames_.knowledge == ::x4render::Knowledge::Known;
    const bool periodic = !preview && !frames_.hasPreview() && full_every_ && partials_ >= full_every_;
    const bool force = force_full_ || refresh_mode_ == FULL_REFRESH || periodic;
    if (begin_gray_(force, preview)) {
      if (!preview) {
        force_full_ = false;
        if (pending_) partials_ = force ? 0 : partials_ + 1;
      }
    } else if (preview || frames_.hasPreview()) {
      dirty_ = true;
    }
    return;
  }
  if (requested_mode_ == TunedBwMode && ready_) {
    dirty_ = false;
    if (!force_full_ && has_frame_ &&
        !std::memcmp(frames_.displayed.data(), frames_.target.data(), ::x4render::Pixels)) return;
    if (begin_tuned_bw_()) force_full_ = false;
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
    while (tuned_state_ != TunedState::Held && !poll_tuned_bw_()) {
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
  if (!calibration_active_) disable_loop();
  return true;
}
bool XteinkDisplay::acquire_controller_(bool for_lab) {
  auto* bus = parent_->display().beginControllerSession();
  if (!bus) return false;
  if (!transport_) {
    transport_.emplace(*bus, []() -> uint32_t { return millis(); });
  }
  if (for_lab && !lab_) lab_.emplace(*transport_);
  if (for_lab && !lab_->allocate()) {
    parent_->display().endControllerSession();
    return false;
  }
  transport_->clearTrace();
  transport_->reset();
  factory_error_ = nullptr;
  if (lab_) lab_->invalidate();
  return true;
}
bool XteinkDisplay::begin_gray_(bool force, bool preview) {
  using namespace ::x4render;
  Settings settings;
  settings.mode = requested_mode_ == 1 ? Mode::SingleSettle : Mode::DitherSettle;
  settings.margin = grayscale_update_margin_;
  const bool relative = settings.mode == Mode::DitherSettle;
  factory_error_ = nullptr;
  settings.palette = relative ? diagnostic_palette_() : measuredStablePalette(stable_luminance_);
  if (preview ? !frames_.planPreview(settings.margin) : !frames_.plan(settings, force)) return false;
  if (!frames_.driven) return preview || frames_.commit();

  const auto snapshot = freeink::getXteinkPanelMtpSnapshot();
  const int temp = snapshot.temperatureC != -128 ? snapshot.temperatureC : 30;
  const auto profile = freeink::observedX4FactoryProfile(
      snapshot.data, snapshot.size, snapshot.verified,
      freeink::getXteinkDisplayProbeDiag().ver[2], temp);
  LutBank bw, gray{};
  if (!profile.power.valid || !tuneFactoryBw(profile.rows, TunedBoilerWaveform, bw) ||
      (!preview && !relative && !stableGrayBank(bw, stable_render_frames_, gray))) {
    frames_.phase = Phase::Idle;
    factory_error_ = "Gray factory table or pulse profile unavailable";
    return false;
  }
  if (!preview && relative) gray = candidates(RelativePulses);
  if (!gray_refresh_.plan(bw, gray, 96, 40)) {
    frames_.phase = Phase::Idle;
    gray_result_ = "Source budget exceeds three zones";
    return false;
  }
  if (!acquire_controller_(false)) {
    frames_.phase = Phase::Idle;
    gray_result_ = "Controller unavailable";
    return false;
  }
  transport_->setPanelPower(profile.power, 2);
  transport_->setCalibrationTemperature(temp);
  gray_started_ = millis();
  gray_driving_ = gray_refresh_.start(*transport_, gray_started_);
  if (!gray_driving_) {
    parent_->display().endControllerSession();
    frames_.fail();
    gray_result_ = "Could not start gray drive";
    return false;
  }
  gray_result_ = "running";
  driving_touch_preview_ = preview;
  tuned_state_ = TunedState::Driving;
  pending_ = true;
  enable_loop();
  return true;
}
bool XteinkDisplay::begin_tuned_bw_() {
  const auto snapshot = freeink::getXteinkPanelMtpSnapshot();
  const int temp = snapshot.temperatureC != -128 ? snapshot.temperatureC : 30;
  const auto profile = freeink::observedX4FactoryProfile(
      snapshot.data, snapshot.size, snapshot.verified,
      freeink::getXteinkDisplayProbeDiag().ver[2], temp);
  ::x4render::LutBank rows;
  if (!profile.power.valid || !::x4render::tuneFactoryBw(profile.rows, TunedBoilerWaveform, rows)) {
    factory_error_ = "Tuned boiler factory table unavailable";
    return false;
  }
  if (!frames_.beginAbsoluteBw()) return false;
  if (!acquire_controller_()) {
    frames_.fail();
    factory_error_ = "Tuned boiler could not acquire controller";
    return false;
  }
  transport_->setPanelPower(profile.power, 2);
  transport_->setCalibrationTemperature(temp);
  lab_->factoryDerivedBw = rows;
  lab_->otpBwEstimate.reset();
  lab_->bwPreparation = {};
  lab_->bwPreparation.programmable = true;
  lab_->boilerLuma = frames_.target.data();
  lab_->bwTexture = ::x4render::BwTexture::Repair;
  lab_->repairStrength = 30;
  lab_->bwBudget = 160;
  lab_->bwLocalBudget = 40;
  lab_->burstHeadroom = 80;
  lab_->bwSplitGuard = true;
  lab_->zoneFinish = ::x4render::ZoneFinish::Hold;
  lab_->zoneGraySettle = false;
  lab_->holdRail = 0;
  lab_->parameters = {{{2, 2, 3, 1, 1}, {2, 3, 3, 1, 1}, {2, 4, 3, 1, 1}}};
  tuned_completed_ = lab_->startImage(millis(), ::x4render::TestImage::BwBoiler);
  tuned_state_ = TunedState::Driving;
  pending_ = true;
  enable_loop();
  return true;
}
bool XteinkDisplay::poll_tuned_bw_() {
  using Status = freeink::Uc8279X4Transport::Status;
  if (tuned_state_ == TunedState::Driving) {
    if (gray_driving_) {
      using Stage = ::x4render::GrayRefresh::Stage;
      if (!gray_refresh_.poll(*transport_, millis())) return false;
      tuned_completed_ = gray_refresh_.stage == Stage::Done;
      factory_error_ = tuned_completed_ ? nullptr : "Gray refresh completion unknown";
    } else {
      if (lab_->busy() && !lab_->poll(millis())) return false;
      tuned_completed_ = tuned_completed_ && std::strcmp(lab_->result(), "complete") == 0;
      factory_error_ = tuned_completed_ ? nullptr : lab_->result();
    }
    // Power-off is a separate transaction. Keep the useful render result.
    if (transport_->status() != Status::Off && transport_->powerOff(millis())) {
      tuned_state_ = TunedState::PoweringOff;
      return false;
    }
  } else if (transport_->poll(millis()) == freeink::Uc8279X4Transport::Result::Pending) {
    return false;
  }
  const bool gray = gray_driving_;
  const bool preview = driving_touch_preview_;
  if (gray) {
    gray_duration_ = millis() - gray_started_;
    gray_bytes_ = transport_->uploadedBytes();
  }
  gray_driving_ = false;
  driving_touch_preview_ = false;
  if (transport_->status() != Status::Off) {
    tuned_state_ = TunedState::Held;
    factory_error_ = "Tuned boiler power-off unconfirmed";
    frames_.fail();
    pending_ = false;
    has_frame_ = false;
    disable_loop();
    ESP_LOGE(TAG, "%s", factory_error_);
    return true;
  }
  parent_->display().endControllerSession();
  if (tuned_completed_ && (gray || frames_.prepared(true)) &&
      (preview ? frames_.commitPreview() : frames_.commit())) {
    has_frame_ = true;
    update_count++;
    if (gray) gray_result_ = "complete";
  } else {
    frames_.fail();
    has_frame_ = false;
    if (gray) gray_result_ = "Gray refresh completion unknown";
  }
  pending_ = false;
  tuned_state_ = TunedState::Idle;
  disable_loop();
  return true;
}
bool XteinkDisplay::begin_calibration() {
  if (calibration_active_) return !exit_requested_;
  if (BoardConfig::ACTIVE.displayController != BoardConfig::DisplayController::UC8279 ||
      get_width_internal() != 800 || get_height_internal() != 480 ||
      !finish_normal_(true) || !acquire_controller_()) return false;
  reset_touch_preview_();
  calibration_active_ = true;
  exit_requested_ = false;
  exit_shutdown_attempted_ = exit_failed_ = false;
  frozen_ = false;
  enable_loop();
  ESP_LOGW(TAG,
           "Waveform lab owns display after driver shutdown and reset. "
           "Rail/hold/end semantics UNCALIBRATED. "
           "No automatic cycles.");
  return true;
}
bool XteinkDisplay::run_calibration(bool rebuild, bool rotate,
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
  if (!configure_lab_()) return false;
  return lab_->start(millis(), rebuild, rotate, conditioning);
}
bool XteinkDisplay::run_stable_gray(bool rebuild, bool rotate) {
  if (!calibration_active_ || lab_->busy() || exit_requested_ || !configure_lab_()) return false;
  return lab_->startStable(millis(), stable_frames_, rebuild, rotate);
}
bool XteinkDisplay::configure_lab_() {
  factory_error_ = nullptr;
  const auto snapshot = freeink::getXteinkPanelMtpSnapshot();
  const auto temp = factory_temperature_();
  const auto profile = freeink::observedX4FactoryProfile(
      snapshot.data, snapshot.size, snapshot.verified,
      freeink::getXteinkDisplayProbeDiag().ver[2], temp);
  lab_->factoryDerivedBw.reset();
  lab_->otpBwEstimate.reset();
  if (bw_mode_) {
    ::x4render::LutBank rows;
    const auto tuning = bw_mode_ == 2 ? bw_tuning_ : ::x4render::FactoryBwTuning{};
    if (!profile.power.valid || !::x4render::tuneFactoryBw(profile.rows, tuning, rows)) {
      factory_error_ = "Factory table unavailable or unsupported";
      ESP_LOGW(TAG, "%s at %d C", factory_error_, temp);
      return false;
    }
    lab_->factoryDerivedBw = rows;
  } else if (profile.power.valid) {
    ::x4render::LutBank rows;
    if (::x4render::tuneFactoryBw(profile.rows, {}, rows)) lab_->otpBwEstimate = rows;
  }
  transport_->setPanelPower(profile.power, panel_power_setup_);
  transport_->setCalibrationTemperature(temp);
  for (uint8_t i = 0; i < 3; ++i)
    lab_->parameters[i] = {pulse_rail_, candidate_frames_[i], pulse_opposite_,
                           pulse_repeats_, pulse_neutral_};
  lab_->holdRail = hold_rail_;
  lab_->imagePalette = diagnostic_palette_();
  lab_->bwPreparation = bw_preparation_;
  lab_->bwPreparation.programmable = bw_mode_ != 0;
  lab_->boilerLuma = ready_ && has_frame_ ? frames_.displayed.data() : nullptr;
  lab_->bwTexture = bw_texture_;
  lab_->columnStrength = bw_column_strength_;
  lab_->repairStrength = bw_repair_strength_;
  lab_->columnGrain = bw_column_grain_;
  lab_->burstHeadroom = bw_burst_headroom_;
  lab_->bwLocalBudget = bw_local_budget_;
  lab_->bwBudget = bw_rail_budget_;
  lab_->grayDither = gray_dither_;
  lab_->zoneFinish = zone_finish_;
  lab_->zoneGraySettle = zone_gray_settle_;
  lab_->bwSplitGuard = bw_split_guard_;
  lab_->grayZoneGuard = bw_split_guard_;
  lab_->verticalWindowValidated = vertical_window_;
  lab_->croppedUploadValidated = cropped_upload_;
  return true;
}
bool XteinkDisplay::show_calibration_image(uint8_t image, uint32_t argument) {
  if (!calibration_active_ || lab_->busy() || exit_requested_ ||
      image < 1 || image > uint8_t(::x4render::TestImage::WhiteCandidates)) return false;
  if (!configure_lab_()) return false;
  const auto test = ::x4render::TestImage(image);
  if (::x4render::usesBwTexture(test)) {
    if (argument < 32 || argument > 160) return false;
    lab_->bwBudget = uint16_t(argument);
  }
  if (!lab_->startImage(millis(), test, argument)) return false;
  last_test_image_ = image;
  last_image_argument_ = argument;
  return true;
}
bool XteinkDisplay::repeat_calibration_image() {
  return show_calibration_image(last_test_image_, last_image_argument_);
}
std::string XteinkDisplay::calibration_image() const {
  if (!calibration_active_ && (gray_mode_() || render_demo_))
    return render_demo_ ? "landscape renderer demo" : "LVGL";
  if (!lab_) return "none";
  if (lab_->stableChart()) return "stable gray chart";
  if (lab_->image() == ::x4render::TestImage::LoadStrip) {
    const char* pattern[] = {"", "; distributed", "; 1 on / 1 off", "; 8 on / 8 off",
                             "; jittered", "; staggered"};
    return "load strip; " + std::to_string(lab_->stripWidth()) + " switching columns" +
           pattern[uint8_t(lab_->loadPattern())];
  }
  if (lab_->image() == ::x4render::TestImage::SplitCheckerboard)
    return "split checkerboard; " + std::to_string(lab_->imageSeed()) + " per rail; " +
           std::to_string(lab_->stripCount()) + " strips";
  if (::x4render::usesBwTexture(lab_->image())) {
    using T = ::x4render::TestImage;
    using M = ::x4render::BwTexture;
    std::string value = lab_->image() == T::BwPhoto ? "B/W photo" :
                        lab_->image() == T::BwChecker ? "B/W checker" : "B/W boiler";
    const auto mode = lab_->renderedBwTexture();
    value += mode == M::Columns ? "; column stable" : mode == M::Repair ? "; row repair" :
             mode == M::OriginalZoned ? "; original zoned" : "; Bayer";
    const bool textured = mode == M::Columns || mode == M::Repair;
    if (textured && lab_->bwStrength() < 100)
      value += " " + std::to_string(lab_->bwStrength()) + "%";
    if (mode == M::Columns && lab_->columnGrain)
      value += "; grain " + std::to_string(lab_->columnGrain) + "%";
    if (mode == M::Repair && lab_->burstHeadroom)
      value += "; headroom " + std::to_string(lab_->burstHeadroom);
    if (lab_->protectedFallback()) value += " (protected edges)";
    if (lab_->stripCount()) value += "; " + std::to_string(lab_->stripCount()) + " strips";
    if (lab_->zonePlan().count > 1) value += "; " + std::to_string(lab_->zonePlan().count) + " zones";
    return value;
  }
  const char* names[] = {"calibration", "white", "black", "known reference", "random edges",
                         "landscape dither", "landscape gray", "neighbor sweep", "linear ramp",
                         "ramp base only", "ramp inverse only", "load stripes",
                         "load checkerboard", "load strip", "split checkerboard", "B/W photo",
                         "B/W checker", "B/W boiler", "white candidates"};
  char value[112];
  std::snprintf(value, sizeof(value), "%s; seed %08lX", names[uint8_t(lab_->image())],
                (unsigned long)lab_->imageSeed());
  std::string result = value;
  if (::x4render::grayPreparationImage(lab_->image())) {
    if (lab_->grayDither == ::x4render::GrayDither::Columns) result += "; column dither";
    if (lab_->grayDither == ::x4render::GrayDither::Repair)
      result += "; row repair " + std::to_string(lab_->repairStrength) + "%";
    if (lab_->zonePlan().count > 1) result += "; " + std::to_string(lab_->zonePlan().count) + " base zones";
  }
  return result;
}
std::string XteinkDisplay::calibration_bw_load() const {
  if (!lab_ || !::x4render::measuresBwImage(lab_->image())) return "No B/W preparation measured";
  const auto before = lab_->requestedBwLoad(), after = lab_->renderedBwLoad();
  char value[120];
  std::snprintf(value, sizeof(value), "Peak/rail %u to %u; 128 cols %u to %u; recent %u to %u",
                unsigned(before.peak), unsigned(after.peak), unsigned(before.localPeak),
                unsigned(after.localPeak), unsigned(before.recentPeak), unsigned(after.recentPeak));
  return value;
}
std::string XteinkDisplay::calibration_bw_charge() const {
  if (!lab_) return "No modeled B/W pass";
  const auto q = lab_->bwCharge();
  if (!q.frames) return "No modeled B/W pass";
  char value[180];
  std::snprintf(value, sizeof(value), "Half-steps/rail | both %u, one %u, boundary %u; local 8/32 %u/%u; dark %llu, light %llu total",
                unsigned(q.both), unsigned(q.one), unsigned(q.boundary), unsigned(q.recent),
                unsigned(q.sustained), static_cast<unsigned long long>(q.total[0]),
                static_cast<unsigned long long>(q.total[1]));
  return value;
}
std::string XteinkDisplay::calibration_bw_zones() const {
  if (!calibration_active_ && gray_mode_()) {
    std::string plan = std::to_string(gray_refresh_.preparationPasses()) + " B/W | " +
                       std::to_string(gray_refresh_.settlePasses()) + " gray passes";
    if (gray_refresh_.finishesPreparationWithHold()) plan += " | panel hold";
    return plan;
  }
  if (!lab_ || !lab_->zoneRequested()) return "No zoned plan requested";
  const auto& plan = lab_->zonePlan();
  if (!plan.count) return "Over budget with three zones";
  char value[120];
  std::snprintf(value, sizeof(value), "%u pass%s, %u px lanes | rail %u, local 8/32 %u/%u",
                unsigned(plan.count), plan.count == 1 ? "" : "es", unsigned(plan.laneWidth ? plan.laneWidth : 800),
                unsigned(plan.wholeRecent), unsigned(plan.localRecent), unsigned(plan.localSustained));
  std::string result(value);
  if (lab_->appliedZoneFinish() == ::x4render::ZoneFinish::Hold) result += " | panel hold";
  if (lab_->appliedZoneFinish() == ::x4render::ZoneFinish::Wait) result += " | idle wait";
  if (const auto passes = lab_->graySettlePasses())
    result += " | gray " + std::to_string(passes) + (passes == 1 ? " pass" : " passes");
  return result;
}
void XteinkDisplay::end_calibration() {
  if (!calibration_active_) return;
  if (!exit_requested_) {
    exit_requested_ = true;
    exit_shutdown_attempted_ = exit_failed_ = false;
  }
  if (lab_->busy()) return;
  using S = freeink::Uc8279X4Transport::Status;
  if (transport_->status() != S::Off && transport_->status() != S::Sleeping) {
    if (!exit_shutdown_attempted_) {
      exit_shutdown_attempted_ = true;
      if (lab_->powerOff(millis())) return;
    }
    // Keep the display lease and paused UI on an unconfirmed shutdown.
    // Returning it would reset a controller whose power state is unknown.
    exit_failed_ = true;
    exit_requested_ = false;
    ESP_LOGE(TAG, "Lab release failed: controller power-off unconfirmed");
    return;
  }
  parent_->display().endControllerSession();
  lab_->invalidate();
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
    if (finish_normal_(false)) {
      schedule_touch_settle_();
      if (redraw_requested_) {
        redraw_requested_ = false;
        update();
      }
    }
    return;
  }
  if (lab_->poll(millis())) {
    ESP_LOGI(TAG, "Lab %s: %u ms, %u bytes, %u driven, cycle %u",
             lab_->result(), unsigned(lab_->duration),
             unsigned(lab_->uploadedBytes()), unsigned(lab_->driveCount),
             unsigned(lab_->cycles));
    if (detailed_) {
      ESP_LOGI(TAG, "%s", calibration_region().c_str());
      log_calibration_waveforms();
      log_calibration_timing();
    }
    if (exit_requested_) end_calibration();
  }
}
void XteinkDisplay::log_calibration_timing() {
  if (!transport_ || pending_ || calibration_busy()) return;
  static const char* names[] = {"reset", "write", "upload", "issue", "busy-on", "busy-off",
                         "assert-timeout", "complete-timeout"};
  timing_index_ = 0;
  timing_size_ = transport_->traceSize();
  for (size_t i = 0; i < timing_size_; ++i)
    timing_snapshot_[i] = transport_->traceEvent(i);
  ESP_LOGI(TAG, "LABTIME BEGIN count=%u", unsigned(timing_size_));
  // Pace a frozen snapshot after the operation; a burst can overflow API log
  // queues. Pause export while any new waveform is running.
  set_interval("lab_timing_export", 50, [this]() {
    if (pending_ || calibration_busy()) return;
    if (timing_index_ == timing_size_) {
      ESP_LOGI(TAG, "LABTIME END");
      cancel_interval("lab_timing_export");
      return;
    }
    const auto& e = timing_snapshot_[timing_index_++];
    ESP_LOGI(TAG, "LABTIME t=%lu %s cmd=%02X detail=%lu poll-gap=%lu",
             (unsigned long)e.at, names[uint8_t(e.event)], e.command,
             (unsigned long)e.detail, (unsigned long)e.pollGap);
  });
}
void XteinkDisplay::set_panel_power_setup(uint8_t value) {
  panel_power_setup_ = value <= 2 ? value : 0;
}
int XteinkDisplay::factory_temperature_() const {
  const auto snapshot = freeink::getXteinkPanelMtpSnapshot();
  if (temp_override_) return manual_temp_c_;
  return snapshot.temperatureC != -128 ? snapshot.temperatureC : 30;
}
std::string XteinkDisplay::calibration_temperature() const {
  const auto snapshot = freeink::getXteinkPanelMtpSnapshot();
  const int temp = factory_temperature_();
  const auto profile = freeink::observedX4FactoryProfile(
      snapshot.data, snapshot.size, snapshot.verified,
      freeink::getXteinkDisplayProbeDiag().ver[2], temp);
  const char* source = temp_override_ ? "Manual" :
      snapshot.temperatureC != -128 ? "Boot TSC" : "Fallback";
  char status[100];
  if (profile.power.valid)
    std::snprintf(status, sizeof(status), "%s %d C, table %u (<=%d C), TSC %02X %02X",
                  source, temp, unsigned(profile.range + 1), profile.upperC,
                  snapshot.temperatureRaw[0], snapshot.temperatureRaw[1]);
  else
    std::snprintf(status, sizeof(status), "%s %d C, no supported table, TSC %02X %02X",
                  source, temp, snapshot.temperatureRaw[0], snapshot.temperatureRaw[1]);
  return status;
}
std::string XteinkDisplay::panel_power_setup_status() const {
  if (!transport_) return "Not powered in calibration; selection applies at next PON";
  const auto mode = transport_->activePowerMode();
  if (mode != panel_power_setup_)
    return "Selection pending or profile rejected; turn pumps off or exit/re-enter calibration";
  if (!mode) return "Legacy setup; no explicit PWR/VDCS/BTST";
  return std::string("PWR=4300787814 VDCS=24 BTST=25253C PLL=") +
         (mode == 2 ? "09" : "0E");
}
void XteinkDisplay::log_panel_mtp() {
  if (calibration_busy()) return;
  const auto snapshot = freeink::getXteinkPanelMtpSnapshot();
  if (!snapshot.data || !snapshot.size) {
    ESP_LOGW(TAG, "PANELMTP unavailable; capture_panel_mtp requires a reboot");
    return;
  }
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < snapshot.size; ++i)
    hash = (hash ^ snapshot.data[i]) * 16777619u;
  const auto& probe = freeink::getXteinkDisplayProbeDiag();
  ESP_LOGI(TAG, "PANELMTP BEGIN bytes=%u verified=%u fnv1a=%08lX VER=%02X%02X%02X%02X%02X",
           unsigned(snapshot.size), snapshot.verified, (unsigned long)hash,
           probe.ver[0], probe.ver[1], probe.ver[2], probe.ver[3], probe.ver[4]);
  set_interval("panel_mtp_export", 50, [this, snapshot, offset = size_t(0)]() mutable {
    if (calibration_busy()) return;
    if (offset == snapshot.size) {
      ESP_LOGI(TAG, "PANELMTP END");
      cancel_interval("panel_mtp_export");
      return;
    }
    char hex[65] = {};
    const size_t count = std::min(size_t(32), snapshot.size - offset);
    for (size_t i = 0; i < count; ++i)
      snprintf(hex + 2 * i, 3, "%02X", snapshot.data[offset + i]);
    ESP_LOGI(TAG, "PANELMTP %04X %s", unsigned(offset), hex);
    offset += count;
  });
}
const char* XteinkDisplay::calibration_pipeline() const {
  if (!calibration_active_ || !lab_) {
    if (tuned_state_ == TunedState::Held) return "boiler_tuned_power_off_unconfirmed";
    if (tuned_state_ == TunedState::Driving) {
      if (!gray_driving_) return "boiler_tuned_bw_driving";
      if (driving_touch_preview_) return "light_touch_bw_driving";
      using Stage = ::x4render::GrayRefresh::Stage;
      switch (gray_refresh_.stage) {
        case Stage::PowerOn: return "gray_power_on";
        case Stage::Prepare: return "gray_bw_preparation";
        case Stage::Hold: return "gray_panel_hold";
        case Stage::Settle: return "gray_settle_driving";
        default: return "gray_finishing";
      }
    }
    if (tuned_state_ == TunedState::PoweringOff) return "boiler_tuned_power_off";
    if (frames_.hasPreview()) return "light_touch_bw_preview";
    if (gray_mode_()) return requested_mode_ == 1 ? "boiler_single_settle" : "boiler_dither_then_settle";
    return requested_mode_ == TunedBwMode ? "boiler_tuned_bw" : "boiler_bw_preview";
  }
  if (exit_failed_) return "release_failed_power_off_unconfirmed";
  using J = ::x4render::Calibration<freeink::Uc8279X4Transport>::Job;
  switch (lab_->job()) {
    case J::PowerOn:
      return "calibration_power_on";
    case J::Prime:
      return "factory_otp_cold_start_prime";
    case J::Base:
      return lab_->factoryPreparation() ? "otp_full_bw_starts"
                                        : "programmable_bw_starts";
    case J::ZoneWait:
      return "after_zones_idle_wait";
    case J::ZoneHold:
      return "after_zones_panel_hold";
    case J::Pause:
      return "bw_to_gray_host_pause";
    case J::Settle:
      return lab_->stableChart() ? "stable_candidate_settle" : "raw_candidate_settle";
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
std::string XteinkDisplay::calibration_waveform(uint8_t row, bool hex,
                                                bool preparation) const {
  if (!lab_) return "not programmed";
  const auto* lut =
      preparation ? lab_->preparationLut(row) : lab_->settleLut(row);
  if (!lut)
    return preparation && lab_->factoryPreparation()
               ? "Factory OTP: payload unavailable"
               : "not programmed";
  return hex ? ::x4render::lutHex(*lut) : ::x4render::lutTrace(*lut);
}
std::string XteinkDisplay::calibration_assignments() const {
  if (!lab_ || !lab_->settleLut(0)) return "not programmed";
  char text[100];
  const uint8_t r = lab_->programmedRotation();
  if (lab_->stableChart()) {
    std::snprintf(text, sizeof(text), "factory black then lighten; id %u; left=G%u middle=G%u right=G%u",
                  unsigned(lab_->waveformId()), 1u + r, 1u + (r + 1) % 3, 1u + (r + 2) % 3);
    return text;
  }
  std::snprintf(
      text, sizeof(text),
      "v5 absolute-BW; opposite-first; id %u; left=%c middle=%c right=%c",
      unsigned(lab_->waveformId()), 'A' + r, 'A' + (r + 1) % 3,
      'A' + (r + 2) % 3);
  return text;
}
void XteinkDisplay::log_calibration_waveforms() const {
  if (!lab_) return;
  ESP_LOGI(TAG, "%s", calibration_assignments().c_str());
  const uint8_t rows[5] = {0, ::x4render::Row[1], ::x4render::Row[2],
                           ::x4render::Row[3], ::x4render::Row[0]};
  const char* names[5] = {"VCOM", "A", "B", "C", "HOLD"};
  const char* prepNames[5] = {"VCOM", "WHITE01", "BLACK10", "WHITE11", "BLACK00"};
  for (bool preparation : {false, true})
    for (uint8_t i = 0; i < 5; ++i) {
      const auto* lut = preparation ? lab_->preparationLut(rows[i])
                                    : lab_->settleLut(rows[i]);
      if (!lut) continue;
      ESP_LOGI(TAG, "%s %s R%02X hex=%s", preparation ? "PREP" : "SETTLE",
               preparation ? prepNames[i] : names[i], 0x20 + rows[i],
               ::x4render::lutHex(*lut).c_str());
      const auto binary = ::x4render::lutBinary(*lut);
      for (uint8_t group = 0; group < 7; ++group)
        ESP_LOGI(TAG, "%s %s g%u bits=%s", preparation ? "PREP" : "SETTLE",
                 preparation ? prepNames[i] : names[i], group,
                 binary.substr(group * 63, 62).c_str());
    }
}
std::string XteinkDisplay::calibration_region() const {
  if (!calibration_active_ && gray_mode_()) {
    const auto r = frames_.requested, e = frames_.expanded;
    char text[160];
    std::snprintf(text, sizeof(text),
                  "requested %u,%u %ux%u; mask %u,%u %ux%u; activation 0,0 800x480; hold code 0",
                  r.x, r.y, r.width, r.height, e.x, e.y, e.width, e.height);
    return text;
  }
  if (!lab_) return "idle";
  char text[160];
  auto r = lab_->requested, e = lab_->activationWindow();
  std::snprintf(text, sizeof(text),
                "requested %u,%u %ux%u; activation %u,%u %ux%u; hold code %u",
                r.x, r.y, r.width, r.height, e.x, e.y, e.width, e.height,
                lab_->programmedHoldRail());
  return text;
}
bool XteinkDisplay::set_shade_sequence(const std::string& value) {
  if (value.size() != shade_sequence_.size()) return false;
  std::array<uint8_t, 6> sequence{};
  uint8_t used = 0;
  for (size_t i = 0; i < sequence.size(); ++i) {
    if (value[i] < '1' || value[i] > '6') return false;
    const uint8_t id = value[i] - '0';
    if (used & (1u << id)) return false;
    used |= 1u << id;
    sequence[i] = id;
  }
  shade_sequence_ = sequence;
  return true;
}
::x4render::Palette XteinkDisplay::diagnostic_palette_() const {
  if (measured_mask_ == 63) {
    const auto measured = ::x4render::RelativeGrayProfile{measured_, coverage_}.palette();
    if (measured.calibrated) return measured;
  }
  auto palette = ::x4render::provisionalRelativePalette();
  for (uint8_t i = 1; i <= 6; ++i)
    palette.shades[i] = ::x4render::relativeShade(shade_sequence_[i - 1], palette.shades[i].luma);
  return palette;
}
std::string XteinkDisplay::calibration_order() const {
  const auto palette = diagnostic_palette_();
  std::string result = palette.calibrated ? "Measured: " : "Provisional: ";
  for (uint8_t i = 0; i < palette.count; ++i) {
    if (i) result += " | ";
    result += ::x4render::shadeLabel(::x4render::relativeShadeId(palette.shades[i]));
    if (palette.calibrated) result += "=" + std::to_string(palette.shades[i].luma);
  }
  return result;
}
bool XteinkDisplay::flush_for_deep_sleep() {
  if (calibration_active_ || virtual_) return false;
  if (!finish_normal_(true)) return false;
  reset_touch_preview_();
  dirty_ |= frames_.hasPreview();
  update();
  if (!finish_normal_(true)) return false;
  return freeze_for_deep_sleep() && has_frame_;
}
bool XteinkDisplay::freeze_for_deep_sleep() {
  if (calibration_active_) return false;
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

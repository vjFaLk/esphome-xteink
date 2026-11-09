#include "display.h"

#include "esphome/core/log.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <esp_attr.h>
#include <esp_heap_caps.h>

namespace esphome {
namespace xteink {

static const char *const TAG = "xteink.display";

struct RetainedFrameTag {
  uint32_t magic;
  uint32_t value;
  uint32_t partial_refreshes;
  uint32_t check;
};

static constexpr uint32_t RETAINED_FRAME_TAG_MAGIC = 0x58544652;  // "XTFR"
// RTC_NOINIT_ATTR survives deep sleep without causing a flash write. Integrity
// fields make power-on garbage or a power loss fail closed into a normal full
// refresh.
static RTC_NOINIT_ATTR RetainedFrameTag retained_frame_tag;

bool XteinkDisplay::has_retained_frame_tag() const {
  return this->parent_ != nullptr && this->parent_->controller_state_retained() &&
         retained_frame_tag.magic == RETAINED_FRAME_TAG_MAGIC &&
         retained_frame_tag.check ==
             (retained_frame_tag.value ^ retained_frame_tag.partial_refreshes ^ RETAINED_FRAME_TAG_MAGIC);
}

uint32_t XteinkDisplay::get_retained_frame_tag() const {
  return this->has_retained_frame_tag() ? retained_frame_tag.value : 0;
}

void XteinkDisplay::set_retained_frame_tag(uint32_t tag) {
  retained_frame_tag.value = tag;
  retained_frame_tag.partial_refreshes = this->partial_refresh_count_;
  retained_frame_tag.check = tag ^ retained_frame_tag.partial_refreshes ^ RETAINED_FRAME_TAG_MAGIC;
  retained_frame_tag.magic = RETAINED_FRAME_TAG_MAGIC;
}

void XteinkDisplay::clear_retained_frame_tag() {
  retained_frame_tag.magic = 0;
  retained_frame_tag.value = 0;
  retained_frame_tag.partial_refreshes = 0;
  retained_frame_tag.check = 0;
}

void XteinkDisplay::setup() {
  // update_when_display_idle uses this component's loop state as the display
  // busy signal. Stay idle until an asynchronous panel waveform is running;
  // loop() is enabled only for that window and disables itself on completion.
  this->disable_loop();

  if (!this->grayscale_enabled_)
    return;

  auto &epd = this->parent_->display();
  if (this->uses_native_grayscale_planes_()) {
    const auto mode = this->grayscale_strategy_ == GRAYSCALE_ADAPTIVE ? freeink::GrayscaleMode::Overlay
                                                                      : freeink::GrayscaleMode::Absolute;
    const auto capabilities = epd.grayscaleCapabilities(mode);
    if (!capabilities.supported()) {
      ESP_LOGW(TAG, "The detected panel does not support the requested four-level grayscale mode; using monochrome");
      this->grayscale_warning_logged_ = true;
      return;
    }
  }

  this->gray_buffer_size_ = static_cast<size_t>(epd.getDisplayWidthBytes()) * epd.getDisplayHeight();
  this->gray_luma_size_ = static_cast<size_t>(epd.getDisplayWidth()) * epd.getDisplayHeight();
#if defined(BOARD_HAS_PSRAM)
  this->gray_luma_ =
      static_cast<uint8_t *>(heap_caps_malloc(this->gray_luma_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (this->uses_native_grayscale_planes_()) {
    this->gray_lsb_ =
        static_cast<uint8_t *>(heap_caps_malloc(this->gray_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    this->gray_msb_ =
        static_cast<uint8_t *>(heap_caps_malloc(this->gray_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
#endif
  if (this->gray_luma_ == nullptr)
    this->gray_luma_ = static_cast<uint8_t *>(malloc(this->gray_luma_size_));
  if (this->uses_native_grayscale_planes_()) {
    if (this->gray_lsb_ == nullptr)
      this->gray_lsb_ = static_cast<uint8_t *>(malloc(this->gray_buffer_size_));
    if (this->gray_msb_ == nullptr)
      this->gray_msb_ = static_cast<uint8_t *>(malloc(this->gray_buffer_size_));
  }
  const bool allocation_failed = this->gray_luma_ == nullptr ||
                                 (this->uses_native_grayscale_planes_() &&
                                  (this->gray_lsb_ == nullptr || this->gray_msb_ == nullptr));
  if (allocation_failed) {
    free(this->gray_luma_);
    free(this->gray_lsb_);
    free(this->gray_msb_);
    this->gray_luma_ = nullptr;
    this->gray_lsb_ = nullptr;
    this->gray_msb_ = nullptr;
    const size_t requested = this->gray_luma_size_ +
                             (this->uses_native_grayscale_planes_() ? this->gray_buffer_size_ * 2 : 0);
    ESP_LOGW(TAG, "Could not allocate %u bytes for grayscale; using monochrome", static_cast<unsigned>(requested));
    this->grayscale_warning_logged_ = true;
    return;
  }
  if (this->grayscale_strategy_ == GRAYSCALE_ADAPTIVE) {
    // Connected-component expansion needs one visited bit per pixel and a
    // bounded work queue. Keeping these buffers for the component lifetime
    // avoids repeated PSRAM allocations on every small telemetry update.
    this->gray_flood_queue_capacity_ = std::min<size_t>(this->gray_luma_size_, 32768);
#if defined(BOARD_HAS_PSRAM)
    this->gray_flood_visited_ =
        static_cast<uint8_t *>(heap_caps_malloc(this->gray_buffer_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    this->gray_flood_queue_ = static_cast<uint32_t *>(heap_caps_malloc(
        this->gray_flood_queue_capacity_ * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
    if (this->gray_flood_visited_ == nullptr)
      this->gray_flood_visited_ = static_cast<uint8_t *>(malloc(this->gray_buffer_size_));
    if (this->gray_flood_queue_ == nullptr)
      this->gray_flood_queue_ =
          static_cast<uint32_t *>(malloc(this->gray_flood_queue_capacity_ * sizeof(uint32_t)));
    if (this->gray_flood_visited_ == nullptr || this->gray_flood_queue_ == nullptr) {
      free(this->gray_flood_visited_);
      free(this->gray_flood_queue_);
      this->gray_flood_visited_ = nullptr;
      this->gray_flood_queue_ = nullptr;
      this->gray_flood_queue_capacity_ = 0;
      ESP_LOGW(TAG, "Could not allocate grayscale region workspace; changed gray regions will settle full-screen");
    }
  }
  memset(this->gray_luma_, 0xFF, this->gray_luma_size_);
  if (this->uses_native_grayscale_planes_()) {
    memset(this->gray_lsb_, 0xFF, this->gray_buffer_size_);
    memset(this->gray_msb_, 0xFF, this->gray_buffer_size_);
  }
  this->grayscale_available_ = true;
  const size_t allocated = this->gray_luma_size_ +
                           (this->uses_native_grayscale_planes_() ? this->gray_buffer_size_ * 2 : 0);
  ESP_LOGI(TAG, "Grayscale enabled using %s rendering (%u bytes)",
           this->grayscale_strategy_ == GRAYSCALE_NATIVE ? "native" :
           this->grayscale_strategy_ == GRAYSCALE_ADAPTIVE ? "adaptive" : "ordered-dither",
           static_cast<unsigned>(allocated));
}

void XteinkDisplay::draw_absolute_pixel_internal(int x, int y, Color color) {
  auto &epd = this->parent_->display();
  if (x < 0 || y < 0 || x >= epd.getDisplayWidth() || y >= epd.getDisplayHeight())
    return;
  uint8_t *buf = epd.getFrameBuffer();
  const uint32_t idx = y * epd.getDisplayWidthBytes() + (x >> 3);
  const uint8_t bit = 0x80 >> (x & 7);

  // ESPHome's native binary drawing API uses COLOR_ON for marked (black) pixels,
  // while draw_pixels_at() receives literal RGB from LVGL where black is 0 and
  // white is 255. Keep both contracts instead of applying Color::is_on() to RGB.
  const uint16_t luma = (77U * color.r + 150U * color.g + 29U * color.b + 128U) >> 8;
  const bool black = this->rgb_input_ ? luma < 128 : color.is_on();
  const uint8_t old = buf[idx];
  if (black) {
    buf[idx] &= static_cast<uint8_t>(~bit);  // 0 = black
  } else {
    buf[idx] |= bit;  // 1 = white
  }
  this->buffer_dirty_ |= old != buf[idx];

  if (this->grayscale_available_) {
    // Preserve 8-bit luminance until refresh. Quantizing here would destroy the
    // coverage produced by anti-aliased fonts and would make a later ordered or
    // error-diffusion dither impossible to implement correctly.
    const size_t luma_idx = static_cast<size_t>(y) * epd.getDisplayWidth() + x;
    // Native display drawing uses COLOR_ON (255) as ink and COLOR_OFF (0) as
    // paper, the inverse of literal RGB luminance. Preserve intermediate native
    // values as coverage so multi-bpp ESPHome fonts remain anti-aliased too.
    const uint8_t output_luma =
        this->rgb_input_ ? static_cast<uint8_t>(luma) : static_cast<uint8_t>(255U - luma);
    if (this->gray_luma_[luma_idx] != output_luma) {
      this->buffer_dirty_ = true;
      if (this->grayscale_strategy_ == GRAYSCALE_ADAPTIVE)
        this->mark_grayscale_dirty_(x, y);
    }
    this->gray_luma_[luma_idx] = output_luma;
  }
}

void XteinkDisplay::mark_grayscale_dirty_(int x, int y) {
  this->grayscale_dirty_generation_++;
  if (this->grayscale_dirty_full_)
    return;
  if (!this->grayscale_dirty_valid_) {
    this->grayscale_dirty_x0_ = this->grayscale_dirty_x1_ = x;
    this->grayscale_dirty_y0_ = this->grayscale_dirty_y1_ = y;
    this->grayscale_dirty_valid_ = true;
    return;
  }
  if (x < this->grayscale_dirty_x0_)
    this->grayscale_dirty_x0_ = x;
  if (x > this->grayscale_dirty_x1_)
    this->grayscale_dirty_x1_ = x;
  if (y < this->grayscale_dirty_y0_)
    this->grayscale_dirty_y0_ = y;
  if (y > this->grayscale_dirty_y1_)
    this->grayscale_dirty_y1_ = y;
}

void XteinkDisplay::mark_grayscale_dirty_rect_(int x, int y, int w, int h) {
  if (this->grayscale_dirty_full_ || w <= 0 || h <= 0)
    return;

  auto &epd = this->parent_->display();
  const int x0 = std::max(0, x);
  const int y0 = std::max(0, y);
  const int x1 = std::min(epd.getDisplayWidth() - 1, x + w - 1);
  const int y1 = std::min(epd.getDisplayHeight() - 1, y + h - 1);
  if (x0 > x1 || y0 > y1)
    return;

  if (!this->grayscale_dirty_valid_) {
    this->grayscale_dirty_x0_ = x0;
    this->grayscale_dirty_y0_ = y0;
    this->grayscale_dirty_x1_ = x1;
    this->grayscale_dirty_y1_ = y1;
    this->grayscale_dirty_valid_ = true;
    return;
  }
  this->grayscale_dirty_x0_ = std::min(this->grayscale_dirty_x0_, x0);
  this->grayscale_dirty_y0_ = std::min(this->grayscale_dirty_y0_, y0);
  this->grayscale_dirty_x1_ = std::max(this->grayscale_dirty_x1_, x1);
  this->grayscale_dirty_y1_ = std::max(this->grayscale_dirty_y1_, y1);
}

void XteinkDisplay::mark_grayscale_full_() {
  this->grayscale_dirty_valid_ = true;
  this->grayscale_dirty_full_ = true;
}

void XteinkDisplay::clear_grayscale_dirty_() {
  this->grayscale_dirty_valid_ = false;
  this->grayscale_dirty_full_ = false;
}

bool XteinkDisplay::is_grayscale_neighbor_(int x, int y) const {
  auto &epd = this->parent_->display();
  const int width = epd.getDisplayWidth();
  const size_t luma_idx = static_cast<size_t>(y) * width + x;
  const uint8_t level = static_cast<uint8_t>((this->gray_luma_[luma_idx] * 3U + 127U) / 255U);
  if (level == 1 || level == 2)
    return true;

  // The changed pixel may have erased part of a gray object. The native-gray
  // planes still describe the last settled frame until render_grayscale_overlay_()
  // runs, so include intermediate pixels from either the old or new frame.
  const size_t plane_idx = static_cast<size_t>(y) * epd.getDisplayWidthBytes() + (x >> 3);
  const uint8_t bit = static_cast<uint8_t>(0x80 >> (x & 7));
  return ((this->gray_lsb_[plane_idx] | this->gray_msb_[plane_idx]) & bit) != 0;
}

void XteinkDisplay::expand_grayscale_dirty_to_neighbors_() {
  if (!this->grayscale_dirty_valid_ || this->grayscale_dirty_full_)
    return;

  auto &epd = this->parent_->display();
  const int width = epd.getDisplayWidth();
  const int height = epd.getDisplayHeight();
  const int max_x = epd.getDisplayWidth() - 1;
  const int max_y = epd.getDisplayHeight() - 1;
  int x0 = this->grayscale_dirty_x0_;
  int y0 = this->grayscale_dirty_y0_;
  int x1 = this->grayscale_dirty_x1_;
  int y1 = this->grayscale_dirty_y1_;

  if (this->gray_flood_visited_ == nullptr || this->gray_flood_queue_ == nullptr) {
    this->mark_grayscale_full_();
    return;
  }

  // A rectangle-edge flood is subtly wrong here: one gray pixel anywhere on
  // an edge grows the whole edge, which can capture an unrelated glyph on the
  // next row and repeat all the way down the display. Instead, seed every gray
  // pixel touched by the dirty rectangle and follow only genuinely connected
  // (8-neighbour) old/new gray pixels. Static gray objects already intersected
  // by the update rectangle are included in full, but aligned objects separated
  // by white space cannot pull the rectangle toward them.
  memset(this->gray_flood_visited_, 0, this->gray_buffer_size_);
  size_t head = 0;
  size_t tail = 0;
  bool overflow = false;
  auto enqueue = [&](int x, int y) {
    if (overflow || x < 0 || y < 0 || x >= width || y >= height ||
        !this->is_grayscale_neighbor_(x, y))
      return;
    const uint32_t idx = static_cast<uint32_t>(y) * width + x;
    const size_t byte = idx >> 3;
    const uint8_t bit = static_cast<uint8_t>(0x80 >> (idx & 7));
    if ((this->gray_flood_visited_[byte] & bit) != 0)
      return;
    if (tail >= this->gray_flood_queue_capacity_) {
      overflow = true;
      return;
    }
    this->gray_flood_visited_[byte] |= bit;
    this->gray_flood_queue_[tail++] = idx;
  };

  for (int y = std::max(0, y0 - 1); y <= std::min(max_y, y1 + 1); y++)
    for (int x = std::max(0, x0 - 1); x <= std::min(max_x, x1 + 1); x++)
      enqueue(x, y);

  while (head < tail && !overflow) {
    const uint32_t idx = this->gray_flood_queue_[head++];
    const int x = idx % width;
    const int y = idx / width;
    x0 = std::min(x0, x);
    y0 = std::min(y0, y);
    x1 = std::max(x1, x);
    y1 = std::max(y1, y);
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++)
        if (dx != 0 || dy != 0)
          enqueue(x + dx, y + dy);
  }

  if (overflow) {
    // An unusually large connected gray image is safer and cheaper to settle
    // as one full update than to truncate the component at an arbitrary edge.
    this->mark_grayscale_full_();
    return;
  }

  this->grayscale_dirty_x0_ = x0;
  this->grayscale_dirty_y0_ = y0;
  this->grayscale_dirty_x1_ = x1;
  this->grayscale_dirty_y1_ = y1;
}

void XteinkDisplay::render_grayscale_planes_() {
  auto &epd = this->parent_->display();
  memset(this->gray_lsb_, 0, this->gray_buffer_size_);
  memset(this->gray_msb_, 0, this->gray_buffer_size_);
  for (int y = 0; y < epd.getDisplayHeight(); y++) {
    for (int x = 0; x < epd.getDisplayWidth(); x++) {
      const size_t luma_idx = static_cast<size_t>(y) * epd.getDisplayWidth() + x;
      const size_t plane_idx = static_cast<size_t>(y) * epd.getDisplayWidthBytes() + (x >> 3);
      const uint8_t bit = 0x80 >> (x & 7);
      // Current output is the panel's native four levels. Keeping quantization
      // in this one full-frame stage lets a future dither select among those
      // levels using the original 8-bit luminance and neighboring pixels.
      const uint8_t level = static_cast<uint8_t>((this->gray_luma_[luma_idx] * 3U + 127U) / 255U);
      if (level & 1)
        this->gray_lsb_[plane_idx] |= bit;
      if (level & 2)
        this->gray_msb_[plane_idx] |= bit;
    }
  }
}

void XteinkDisplay::render_grayscale_overlay_() {
  // CrossPoint uses overlay masks for anti-aliased text and UI: first paint a
  // B/W base where every non-white pixel is black, then the AA waveform nudges
  // only the two intermediate levels toward dark/light gray. Unlike Absolute,
  // this does not classify the whole dashboard as a grayscale image or select
  // the full-panel quality waveform.
  auto &epd = this->parent_->display();
  uint8_t *base = epd.getFrameBuffer();
  memset(base, 0, this->gray_buffer_size_);
  memset(this->gray_lsb_, 0, this->gray_buffer_size_);
  memset(this->gray_msb_, 0, this->gray_buffer_size_);
  const int width = epd.getDisplayWidth();
  const int width_bytes = epd.getDisplayWidthBytes();

  for (int y = 0; y < epd.getDisplayHeight(); y++) {
    for (int x = 0; x < width; x++) {
      const size_t luma_idx = static_cast<size_t>(y) * width + x;
      const size_t plane_idx = static_cast<size_t>(y) * width_bytes + (x >> 3);
      const uint8_t bit = static_cast<uint8_t>(0x80 >> (x & 7));
      const uint8_t level = static_cast<uint8_t>((this->gray_luma_[luma_idx] * 3U + 127U) / 255U);
      if (level == 3) {
        base[plane_idx] |= bit;  // white remains white in the B/W base
      } else if (level == 1) {
        this->gray_lsb_[plane_idx] |= bit;  // dark gray = (LSB, MSB) (1, 1)
        this->gray_msb_[plane_idx] |= bit;
      } else if (level == 2) {
        this->gray_msb_[plane_idx] |= bit;  // light gray = (0, 1)
      }
    }
  }
}

bool XteinkDisplay::render_ordered_dither_() {
  // A fixed, screen-space Bayer matrix turns continuous luminance into a stable
  // binary texture. The ordinary differential waveform then only drives pixels
  // whose dithered state changed, unlike the controller's 50-frame native-gray
  // waveform which activates the entire panel for every update.
  static constexpr uint8_t BAYER_4X4[16] = {
      0, 8, 2, 10,
      12, 4, 14, 6,
      3, 11, 1, 9,
      15, 7, 13, 5,
  };

  auto &epd = this->parent_->display();
  uint8_t *buf = epd.getFrameBuffer();
  const uint8_t *displayed = epd.getActiveFrameBuffer();
  const int width = epd.getDisplayWidth();
  const int width_bytes = epd.getDisplayWidthBytes();
  bool changed = false;

  int x0 = 0;
  int y0 = 0;
  int x1 = width - 1;
  int y1 = epd.getDisplayHeight() - 1;
  const bool zoned = this->grayscale_has_settled_ && this->grayscale_dirty_valid_ &&
                     !this->grayscale_dirty_full_;
  if (zoned) {
    x0 = this->grayscale_dirty_x0_;
    y0 = this->grayscale_dirty_y0_;
    x1 = this->grayscale_dirty_x1_;
    y1 = this->grayscale_dirty_y1_;
  }

  for (int y = y0; y <= y1; y++) {
    for (int byte_x = x0 >> 3; byte_x <= (x1 >> 3); byte_x++) {
      const size_t idx = static_cast<size_t>(y) * width_bytes + byte_x;
      // A zoned preview starts from the settled B/W base and replaces only the
      // changed pixels. Bits sharing either edge byte stay untouched.
      uint8_t output = zoned ? buf[idx] : 0;
      for (int bit_x = 0; bit_x < 8; bit_x++) {
        const int x = byte_x * 8 + bit_x;
        if (x >= width)
          break;
        if (zoned && (x < x0 || x > x1))
          continue;
        const uint8_t bit = static_cast<uint8_t>(0x80 >> bit_x);
        const uint8_t luma = this->gray_luma_[static_cast<size_t>(y) * width + x];
        const uint8_t threshold = static_cast<uint8_t>(BAYER_4X4[(y & 3) * 4 + (x & 3)] * 16 + 8);
        if (luma > threshold)
          output |= bit;  // 1 = paper/white
        else
          output &= static_cast<uint8_t>(~bit);
      }
      // LVGL first paints RGB into the binary write buffer, which temporarily
      // replaces dither pixels with a simple 50% threshold. Comparing against
      // that transient buffer falsely reports unchanged gray regions as dirty.
      // The active buffer is the last frame actually submitted to the panel.
      changed |= displayed != nullptr ? displayed[idx] != output : buf[idx] != output;
      buf[idx] = output;
    }
  }
  return changed;
}

void XteinkDisplay::schedule_grayscale_settle_() {
  if (this->virtual_update_active_ || this->sleep_preparing_ ||
      this->grayscale_strategy_ != GRAYSCALE_ADAPTIVE ||
      !this->grayscale_available_)
    return;
  this->grayscale_settle_pending_ = true;
  this->set_timeout("grayscale_settle", this->grayscale_settle_time_, [this]() { this->settle_grayscale(); });
}

void XteinkDisplay::postpone_grayscale_settle() {
  if (this->virtual_update_active_) {
    // Touch updates can continue while a slider or retained-frame bootstrap is
    // rendering virtually. They must not re-arm an interactive panel preview.
    this->interactive_preview_requested_ = false;
    this->cancel_timeout("grayscale_interactive_preview");
    this->cancel_timeout("grayscale_settle");
    this->grayscale_settle_pending_ = false;
    return;
  }
  if (this->grayscale_strategy_ == GRAYSCALE_ADAPTIVE && this->grayscale_available_) {
    // activity() runs immediately before LVGL dispatches the button/page event.
    // Mark that next draw as latency-sensitive so it receives an immediate
    // dither preview; expire the marker if the touch changed no widget.
    this->interactive_preview_requested_ = true;
    this->set_timeout("grayscale_interactive_preview", 1000, [this]() {
      this->interactive_preview_requested_ = false;
    });
  }
  if (!this->grayscale_settle_pending_)
    return;
  this->set_timeout("grayscale_settle", this->grayscale_settle_time_, [this]() { this->settle_grayscale(); });
}

void XteinkDisplay::prepare_page_change() {
  if (this->grayscale_strategy_ != GRAYSCALE_ADAPTIVE || !this->grayscale_available_)
    return;

  // This is intentionally one line per explicit page change, rather than per
  // LVGL flush. It lets hardware testing distinguish a page that really had
  // native gray on-panel from a binary fast refresh that merely left pixels at
  // intermediate optical charge.
  ESP_LOGD(TAG, "Page change: native_gray=%s settled=%s pending=%s partial_count=%lu",
           YESNO(this->native_grayscale_on_panel_), YESNO(this->grayscale_has_settled_),
           YESNO(this->grayscale_settle_pending_), static_cast<unsigned long>(this->partial_refresh_count_));

  this->mark_grayscale_full_();
  if (!this->grayscale_has_settled_)
    return;

  // A native-gray page leaves analog gray charge that a normal DU page change
  // does not fully erase. Promote the next actual framebuffer update to the
  // panel's strong full waveform, but only when leaving a settled-gray state.
  this->force_full_refresh_ = true;
  this->interactive_preview_requested_ = true;
}

void XteinkDisplay::begin_virtual_update() {
  this->virtual_update_active_ = true;
  this->interactive_preview_requested_ = false;
  this->cancel_timeout("grayscale_interactive_preview");
  this->cancel_timeout("grayscale_settle");
  this->grayscale_settle_pending_ = false;
}

void XteinkDisplay::end_virtual_update(bool refresh) {
  if (!this->virtual_update_active_)
    return;
  this->virtual_update_active_ = false;
  if (!refresh)
    return;

  // Virtual draws have already populated the framebuffer and accumulated the
  // grayscale dirty bounds. Submit that final state once, without asking LVGL
  // to redraw the gesture's intermediate frames.
  this->buffer_dirty_ = this->buffer_dirty_ || this->grayscale_dirty_valid_;
  this->update();
}

void XteinkDisplay::begin_retained_frame_bootstrap() {
  // Let LVGL rebuild the exact retained frame into fresh host buffers while
  // panel writes are suppressed. The following clock-only mutation can then be
  // reduced to its normal dirty rectangle without redrawing unrelated pixels.
  this->retained_frame_adopted_ = false;
  this->retained_frame_bootstrap_pending_ = true;
  this->begin_virtual_update();
  if (this->has_retained_frame_tag())
    this->partial_refresh_count_ = retained_frame_tag.partial_refreshes;
}

void XteinkDisplay::end_retained_frame_bootstrap() {
  this->end_virtual_update(false);
  if (this->retained_frame_adopted_) {
    auto &epd = this->parent_->display();
    // LVGL may deliver one forced redraw as several flushes. Keep composing
    // all of them into the write buffer while panel I/O is suppressed, then
    // build the clean B/W base from the completed luma frame exactly once.
    // Making that completed buffer active here is essential: adopting the
    // first flush left frameBufferActive holding only the top strip while the
    // write buffer contained the rest of the page. The next differential or
    // gray pass then used fragments of an older page as its OLD plane.
    if (this->grayscale_available_ && this->grayscale_strategy_ == GRAYSCALE_ADAPTIVE) {
      this->render_grayscale_overlay_();
      this->native_grayscale_on_panel_ = true;
      this->grayscale_has_settled_ = true;
      epd.preserveGrayscaleOnFastRefresh();
    }
    epd.skipInitialResync();
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
    epd.swapBuffers();
#endif
    epd.syncWriteBufferFromActive();
  }
  // A full LVGL reconstruction can be delivered as several display flushes.
  // Every one of them describes the already-visible retained frame, so none of
  // their dirty bounds may leak into the following clock-only mutation.
  this->buffer_dirty_ = false;
  this->clear_grayscale_dirty_();
}

bool XteinkDisplay::settle_grayscale() {
  if (this->virtual_update_active_) {
    // A virtual frame is deliberately not a controller transaction. Leave its
    // luma and dirty bounds intact for the first real mutation after resume.
    this->cancel_timeout("grayscale_settle");
    this->grayscale_settle_pending_ = false;
    return false;
  }
  // Timer-wake shutdown can request the final native-gray frame immediately
  // after LVGL submits its dithered preview. Drain that pending waveform first
  // so the settled frame is guaranteed to reach the panel before deep sleep.
  this->finish_pending_refresh_(true);
  this->cancel_timeout("grayscale_settle");
  this->grayscale_settle_pending_ = false;
  if (this->grayscale_strategy_ != GRAYSCALE_ADAPTIVE || !this->grayscale_available_ || !this->has_drawn_)
    return false;
  if (!this->grayscale_dirty_valid_)
    return false;

  auto &epd = this->parent_->display();
  const bool periodic_full = this->full_refresh_every_ != 0 &&
                             this->partial_refresh_count_ + 1 >= this->full_refresh_every_;
  const bool zoned = this->grayscale_has_settled_ && !this->grayscale_dirty_full_ && !periodic_full;
  if (zoned) {
    this->expand_grayscale_dirty_to_neighbors_();
    // Keep the physical PTL boundary outside the anti-aliased object itself.
    // Four pixels was enough for the luma mask but left the UC8279 window edge
    // close enough to small changing labels to accumulate a visible underline
    // and damaged digit strokes. Connected-component expansion still prevents
    // this padding from pulling unrelated widgets into the update.
    constexpr int MARGIN = 12;
    const int x0 = std::max(0, this->grayscale_dirty_x0_ - MARGIN);
    const int y0 = std::max(0, this->grayscale_dirty_y0_ - MARGIN);
    const int x1 = std::min(epd.getDisplayWidth() - 1, this->grayscale_dirty_x1_ + MARGIN);
    const int y1 = std::min(epd.getDisplayHeight() - 1, this->grayscale_dirty_y1_ + MARGIN);
    epd.setGrayscaleWindow(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
  } else {
    epd.setGrayscaleWindow(0, 0, 0, 0);
  }
  // Adaptive UI settling follows CrossPoint's text/UI path. Its overlay mode
  // drives only intermediate-gray pixels after a differential B/W base. Keep
  // the explicit native strategy on Absolute for full grayscale images.
  this->render_grayscale_overlay_();
  // Fast lets the controller move from the dither preview to the overlay's
  // solid B/W base with its differential preconditioning waveform; the gray
  // pass then drives only the intermediate-level masks.
  const auto base_mode = periodic_full ? EInkDisplay::FULL_REFRESH : EInkDisplay::FAST_REFRESH;
  if (!epd.displayGrayscaleBase(freeink::GrayscaleMode::Overlay, base_mode)) {
    ESP_LOGW(TAG, "Native grayscale settle became unavailable; keeping ordered-dither output");
    epd.setGrayscaleWindow(0, 0, 0, 0);
    return false;
  }
  epd.copyGrayscaleBuffers(this->gray_lsb_, this->gray_msb_);
  epd.displayGrayBuffer();
  epd.cleanupGrayscaleBuffers(epd.getFrameBuffer());
  // Adaptive UI updates should retain unchanged native-gray pixels. Without
  // this opt-in the UC8279 reader policy re-drives the whole panel back to its
  // dither pattern as soon as one telemetry digit changes.
  epd.preserveGrayscaleOnFastRefresh();
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  // A background value update may have prepared a newer binary base without
  // displaying that dither preview. Make that write buffer the active baseline
  // now that its matching native-gray frame is physically on the panel.
  epd.swapBuffers();
#endif
  epd.syncWriteBufferFromActive();
  this->native_grayscale_on_panel_ = true;
  this->grayscale_has_settled_ = true;
  this->clear_grayscale_dirty_();
  this->update_count++;
  if (periodic_full)
    this->partial_refresh_count_ = 0;
  else
    this->partial_refresh_count_++;
  return true;
}

bool XteinkDisplay::flush_for_deep_sleep() {
  if (this->virtual_update_active_)
    return false;
  // LVGL can finish drawing into the host framebuffer shortly before a sleep
  // script runs. settle_grayscale() only refines a frame which has already
  // reached the panel, so first submit any dirty B/W preview and synchronously
  // finish its waveform. This makes the sleep boundary a real display flush.
  this->finish_pending_refresh_(true);
  if (this->buffer_dirty_) {
    this->update();
    this->finish_pending_refresh_(true);
  }

  bool flushed = this->has_drawn_;
  if (this->grayscale_strategy_ == GRAYSCALE_ADAPTIVE && this->grayscale_available_ &&
      this->grayscale_dirty_valid_)
    flushed = this->settle_grayscale();

  // No panel transaction may begin after this point. ESPHome tears down API
  // and Wi-Fi before on_powerdown(), and the retained-controller path must not
  // discover a newly queued async refresh there and wait with services already
  // gone. A fresh component instance clears this automatically on wake.
  this->sleep_preparing_ = true;
  this->cancel_timeout("grayscale_interactive_preview");
  this->cancel_timeout("grayscale_settle");
  this->grayscale_settle_pending_ = false;
  return flushed;
}

void XteinkDisplay::freeze_for_deep_sleep() {
  // A low-power caller may prefer the last completed frame over risking a new
  // grayscale transaction at the sleep boundary. In particular, a deferred
  // native-gray settle must never be allowed to keep the MCU awake after Wi-Fi
  // has already been disabled. The component is recreated on the next wake, so
  // discarding unsent dirty state here is intentional.
  this->sleep_preparing_ = true;
  this->cancel_timeout("grayscale_interactive_preview");
  this->cancel_timeout("grayscale_settle");
  this->grayscale_settle_pending_ = false;
  this->interactive_preview_requested_ = false;
  this->buffer_dirty_ = false;
  this->clear_grayscale_dirty_();
}

bool XteinkDisplay::begin_panel_conditioning() {
  if (this->virtual_update_active_ || this->sleep_preparing_ || !this->grayscale_available_ ||
      !this->uses_native_grayscale_planes_() || !this->parent_->display().supportsConditioningRedrive())
    return false;

  // Drain the last ordinary refresh first, then cancel deferred adaptive-gray
  // work so it cannot interleave with the manual sequence.
  this->finish_pending_refresh_(true);
  this->cancel_timeout("grayscale_interactive_preview");
  this->cancel_timeout("grayscale_settle");
  this->grayscale_settle_pending_ = false;
  this->interactive_preview_requested_ = false;
  this->conditioning_active_ = true;
  ESP_LOGI(TAG, "Panel conditioning started");
  return true;
}

bool XteinkDisplay::condition_panel_step(uint8_t step) {
  if (!this->conditioning_active_ || step >= CONDITIONING_TRANSACTION_COUNT)
    return false;

  auto &epd = this->parent_->display();
  auto show_bw = [&](uint8_t value, EInkDisplay::RefreshMode mode) {
    epd.clearScreen(value);
    epd.displayBuffer(mode);
    this->update_count++;
  };
  auto show_gray = [&](uint8_t level) {
    // A conditioning sweep needs a single combined waveform per solid level.
    // Overlay mode is deliberately unsuitable: it first paints a B/W base and
    // then applies gray masks, producing a black/white flash before every tone.
    memset(this->gray_lsb_, (level & 1U) != 0 ? 0xFF : 0x00, this->gray_buffer_size_);
    memset(this->gray_msb_, (level & 2U) != 0 ? 0xFF : 0x00, this->gray_buffer_size_);
    epd.setGrayscaleWindow(0, 0, 0, 0);
    if (!epd.displayGrayscaleBase(freeink::GrayscaleMode::Direct, EInkDisplay::FAST_REFRESH))
      return false;
    epd.copyGrayscaleBuffers(this->gray_lsb_, this->gray_msb_);
    epd.displayGrayBuffer();
    this->update_count++;
    return true;
  };

  // Each endpoint first uses the self-contained scrub pipeline, then five DU
  // redrives with OLD=complement(target). The four-tone sweep uses the Direct
  // pipeline: an overlay would repaint a B/W base between tones, and cannot
  // encode solid white over a black base under its documented mask contract.
  // The black-to-gray and gray-to-black transitions are deliberately full
  // panel; this tests the X4 quality waveform without a partial-window edge.
  enum class Kind : uint8_t { Scrub, Redrive, GraySweep };
  struct Phase { Kind kind; uint8_t value; uint8_t count; };
  static constexpr Phase PHASES[] = {
      {Kind::Scrub, 0x00, 1}, {Kind::Redrive, 0x00, 5},
      {Kind::Scrub, 0xFF, 1}, {Kind::Redrive, 0xFF, 5},
      {Kind::Scrub, 0x00, 1}, {Kind::GraySweep, 0, 4},
      {Kind::Scrub, 0x00, 1}, {Kind::Redrive, 0x00, 5},
      {Kind::Scrub, 0xFF, 1}, {Kind::Redrive, 0xFF, 5},
      {Kind::Scrub, 0xFF, 1},
  };
  uint8_t first_transaction = 0;
  for (const auto& phase : PHASES) {
    if (step >= first_transaction + phase.count) {
      first_transaction += phase.count;
      continue;
    }
    const uint8_t pass = step - first_transaction;
    if (phase.kind == Kind::GraySweep) {
      ESP_LOGD(TAG, "Panel conditioning transaction %u/%u (gray %u)", step + 1,
               CONDITIONING_TRANSACTION_COUNT, pass);
      return show_gray(pass);
    }
    ESP_LOGD(TAG, "Panel conditioning transaction %u/%u (%s %s)", step + 1,
             CONDITIONING_TRANSACTION_COUNT, phase.value == 0 ? "black" : "white",
             phase.kind == Kind::Scrub ? "scrub" : "redrive");
    show_bw(phase.value, phase.kind == Kind::Scrub ? EInkDisplay::HALF_REFRESH
                                                  : EInkDisplay::CONDITIONING_REDRIVE);
    return true;
  }
  return false;
}

void XteinkDisplay::end_panel_conditioning() {
  if (!this->conditioning_active_)
    return;
  auto &epd = this->parent_->display();
  epd.setGrayscaleWindow(0, 0, 0, 0);
  epd.syncWriteBufferFromActive();

  // Both host buffers now describe the white panel. LVGL will reconstruct the
  // requested page virtually and submit it once as a new full baseline.
  if (this->grayscale_available_) {
    memset(this->gray_luma_, 0xFF, this->gray_luma_size_);
    if (this->uses_native_grayscale_planes_()) {
      memset(this->gray_lsb_, 0xFF, this->gray_buffer_size_);
      memset(this->gray_msb_, 0xFF, this->gray_buffer_size_);
    }
  }
  this->has_drawn_ = false;
  this->native_grayscale_on_panel_ = false;
  this->grayscale_has_settled_ = false;
  this->force_full_refresh_ = true;
  this->buffer_dirty_ = false;
  this->partial_refresh_count_ = 0;
  this->clear_grayscale_dirty_();
  this->conditioning_active_ = false;
  ESP_LOGI(TAG, "Panel conditioning finished");
}

void XteinkDisplay::finalize_refresh_(EInkDisplay::RefreshMode mode) {
  auto &epd = this->parent_->display();
  // In dual-buffer mode displayBufferAsync() swaps current and previous
  // frames. LVGL patches only dirty regions, so seed its new write surface
  // from the completed displayed frame.
  epd.syncWriteBufferFromActive();
  this->has_drawn_ = true;
  this->update_count++;
  if (mode == EInkDisplay::FAST_REFRESH)
    this->partial_refresh_count_++;
  else
    this->partial_refresh_count_ = 0;
  this->buffer_dirty_ = false;
  if (this->grayscale_strategy_ == GRAYSCALE_ADAPTIVE)
    this->native_grayscale_on_panel_ = false;
  this->schedule_grayscale_settle_();
}

bool XteinkDisplay::finish_pending_refresh_(bool wait) {
  if (!this->refresh_pending_)
    return true;

  auto &epd = this->parent_->display();
  if (!wait && epd.refreshBusy())
    return false;

  epd.waitRefreshComplete();
  const auto mode = this->pending_refresh_mode_;
  this->refresh_pending_ = false;
  this->finalize_refresh_(mode);
  this->disable_loop();
  return true;
}

void XteinkDisplay::loop() { this->finish_pending_refresh_(false); }

void XteinkDisplay::draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr,
                                   display::ColorOrder order, display::ColorBitness bitness, bool big_endian,
                                   int x_offset, int y_offset, int x_pad) {
  const uint32_t dirty_generation = this->grayscale_dirty_generation_;
  this->rgb_input_ = true;
  display::Display::draw_pixels_at(x_start, y_start, w, h, ptr, order, bitness, big_endian, x_offset, y_offset, x_pad);
  this->rgb_input_ = false;
  if (this->grayscale_strategy_ == GRAYSCALE_ADAPTIVE &&
      this->grayscale_dirty_generation_ != dirty_generation) {
    // LVGL already computed the semantic invalidation rectangle for this draw.
    // Keep all of it: antialiased glyph edges are split into gray islands by
    // solid black strokes, so pixel-only connected-component growth can clip
    // parts of the same label. The generation guard prevents unchanged LVGL
    // flushes from broadening a genuinely small gray update.
    this->mark_grayscale_dirty_rect_(x_start, y_start, w, h);
  }
}

void XteinkDisplay::update() {
  if (this->sleep_preparing_ || this->conditioning_active_)
    return;
  // LVGL's update_when_display_idle gate should prevent this, but keep the
  // framebuffer ownership invariant explicit if another caller invokes update.
  if (this->refresh_pending_)
    return;

  auto &epd = this->parent_->display();
  if (this->writer_local_.has_value()) {
    // Lambda/pages rendering is a complete-frame API by default. Clear before
    // drawing, never after displaying: LVGL has no writer and patches only its
    // invalidated regions into the persistent framebuffer.
    if (this->auto_clear_enabled_) {
      epd.clearScreen(0xFF);
      if (this->grayscale_available_) {
        memset(this->gray_luma_, 0xFF, this->gray_luma_size_);
        if (this->grayscale_strategy_ == GRAYSCALE_ADAPTIVE)
          this->mark_grayscale_full_();
      }
      this->buffer_dirty_ = true;
    }
    (*this->writer_local_)(*this);
  }
  if (!this->buffer_dirty_) {
    ESP_LOGV(TAG, "Skipping panel refresh: framebuffer is unchanged");
    return;
  }

  if (this->virtual_update_active_ && !this->retained_frame_bootstrap_pending_) {
    // LVGL may flush many areas during a drag. Keep composing them into the
    // host buffers, but never schedule a waveform or alter controller state.
    this->buffer_dirty_ = false;
    ESP_LOGV(TAG, "Skipping panel refresh: virtual update is active");
    return;
  }

  if (this->retained_frame_bootstrap_pending_) {
    this->retained_frame_bootstrap_pending_ = false;

    // This is only the first flush of the reconstructed LVGL frame. Mark that
    // rendering began, but leave the write/active buffer relationship alone:
    // later flushes in the same redraw must remain virtual and land in the same
    // write buffer. end_retained_frame_bootstrap() adopts the complete frame.
    this->has_drawn_ = true;
    this->buffer_dirty_ = false;
    this->force_full_refresh_ = false;
    this->interactive_preview_requested_ = false;
    this->cancel_timeout("grayscale_interactive_preview");
    this->cancel_timeout("grayscale_settle");
    this->grayscale_settle_pending_ = false;
    this->clear_grayscale_dirty_();
    this->retained_frame_adopted_ = true;
    ESP_LOGD(TAG, "Retained-frame reconstruction started with panel writes suppressed");
    return;
  }

  if (this->grayscale_available_ && this->grayscale_strategy_ == GRAYSCALE_ADAPTIVE &&
      this->native_grayscale_on_panel_ && !this->interactive_preview_requested_) {
    // A background telemetry update must not turn a settled gray screen back
    // into full-screen dither. Prepare the newest binary base in RAM, but defer
    // the physical panel write to the native-gray settle. Keep the first timer
    // deadline so a steady stream of HA samples cannot postpone the display
    // indefinitely; the settle always renders the latest accumulated luma.
    this->render_ordered_dither_();
    this->buffer_dirty_ = false;
    if (!this->grayscale_settle_pending_)
      this->schedule_grayscale_settle_();
    return;
  }

  this->interactive_preview_requested_ = false;
  this->cancel_timeout("grayscale_interactive_preview");

  EInkDisplay::RefreshMode mode = EInkDisplay::FULL_REFRESH;
  if (this->refresh_mode_ == HALF_REFRESH) {
    mode = EInkDisplay::HALF_REFRESH;
  } else if (this->refresh_mode_ == FAST_REFRESH && !this->has_drawn_) {
    // After MCU sleep neither the retained panel contents nor the controller's
    // old-image RAM can be assumed to match our newly allocated framebuffer.
    // Adaptive gray makes that mismatch especially visible as uneven charge,
    // so establish one strong known base before the first native-gray settle.
    mode = this->grayscale_strategy_ == GRAYSCALE_ADAPTIVE ? EInkDisplay::FULL_REFRESH
                                                           : EInkDisplay::HALF_REFRESH;
  } else if (this->refresh_mode_ == FAST_REFRESH) {
    mode = EInkDisplay::FAST_REFRESH;
  }

  const bool periodic_full = this->full_refresh_every_ != 0 &&
                             this->partial_refresh_count_ + 1 >= this->full_refresh_every_;
  if (periodic_full || this->force_full_refresh_)
    mode = EInkDisplay::FULL_REFRESH;
  this->force_full_refresh_ = false;
  if (this->grayscale_strategy_ == GRAYSCALE_ADAPTIVE && mode != EInkDisplay::FAST_REFRESH)
    this->mark_grayscale_full_();

  bool grayscale_rendered = false;
  bool ordinary_buffer_refresh = false;
  if (this->grayscale_available_ && this->grayscale_strategy_ != GRAYSCALE_NATIVE) {
    if (!this->render_ordered_dither_()) {
      this->buffer_dirty_ = false;
      // Native gray has four levels while the preview has only two. A luma
      // change can land on the same Bayer bit yet still require a gray update.
      if (this->grayscale_strategy_ == GRAYSCALE_ADAPTIVE && this->grayscale_dirty_valid_ &&
          !this->grayscale_settle_pending_)
        this->schedule_grayscale_settle_();
      return;
    }
    ordinary_buffer_refresh = true;
  } else if (this->grayscale_available_) {
    this->render_grayscale_planes_();
    if (epd.displayGrayscaleBase(freeink::GrayscaleMode::Absolute, mode)) {
      epd.copyGrayscaleBuffers(this->gray_lsb_, this->gray_msb_);
      epd.displayGrayBuffer();
      epd.cleanupGrayscaleBuffers(epd.getFrameBuffer());
      grayscale_rendered = true;
    } else {
      if (!this->grayscale_warning_logged_) {
        ESP_LOGW(TAG, "Grayscale became unavailable; using monochrome");
        this->grayscale_warning_logged_ = true;
      }
      epd.displayBuffer(mode);
    }
  } else {
    ordinary_buffer_refresh = true;
  }

  if (ordinary_buffer_refresh) {
    if (epd.supportsAsyncRefresh()) {
      // FreeInk copies the frame into controller RAM, starts the waveform and
      // swaps its dual buffers before returning. Mark the display busy so LVGL
      // does not mutate the next write surface until loop() finalizes it, while
      // the main ESPHome loop remains free for touch, buttons and API traffic.
      this->buffer_dirty_ = false;
      this->pending_refresh_mode_ = mode;
      epd.displayBufferAsync(mode);
      this->refresh_pending_ = true;
      this->enable_loop();
      return;
    }
    epd.displayBuffer(mode);
  }
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  // Grayscale does not pass through displayBuffer(), so it does not swap the
  // two FreeInk buffers. Make the rendered frame active explicitly; the copy
  // below then gives LVGL an identical persistent write surface.
  if (grayscale_rendered)
    epd.swapBuffers();
#else
  (void) grayscale_rendered;
#endif
  this->finalize_refresh_(mode);
}

void XteinkDisplay::dump_config() {
  LOG_DISPLAY("", "Xteink e-paper", this);
  ESP_LOGCONFIG(TAG, "  Refresh mode: %s", this->refresh_mode_ == FULL_REFRESH ? "full" :
                                               this->refresh_mode_ == FAST_REFRESH ? "fast" : "half");
  ESP_LOGCONFIG(TAG, "  Full refresh every: %u", static_cast<unsigned>(this->full_refresh_every_));
  ESP_LOGCONFIG(TAG, "  Grayscale requested: %s", YESNO(this->grayscale_enabled_));
  ESP_LOGCONFIG(TAG, "  Grayscale available: %s", YESNO(this->grayscale_available_));
  ESP_LOGCONFIG(TAG, "  Grayscale strategy: %s",
                this->grayscale_strategy_ == GRAYSCALE_NATIVE ? "native" :
                this->grayscale_strategy_ == GRAYSCALE_ADAPTIVE ? "adaptive" : "ordered-dither");
  if (this->grayscale_strategy_ == GRAYSCALE_ADAPTIVE)
    ESP_LOGCONFIG(TAG, "  Native grayscale settle delay: %u ms",
                  static_cast<unsigned>(this->grayscale_settle_time_));
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace xteink
}  // namespace esphome

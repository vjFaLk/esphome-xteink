#pragma once

#include <stdint.h>

namespace freeink {

// Pure, hardware-independent state model for the UC8279 X4 refresh engine.
// Controller RAM and visible panel charge are deliberately tracked separately.
// The recording-bus tests exercise this model without Arduino or ESP-IDF.
class Uc8279X4RefreshState {
 public:
  using Generation = uint32_t;

  enum class Lifecycle : uint8_t { Uninitialized, ReadyOff, ReadyOn, DeepSleep };
  enum class PlaneContent : uint8_t {
    Unknown,
    RetainedUnknown,
    White,
    BwFrame,
    BwComplement,
    GrayPlane0,
    GrayPlane1,
  };
  enum class PanelContent : uint8_t { Unknown, RetainedUnknown, Monochrome, NativeGray, NativeGrayWithBwDelta };
  enum class Pipeline : uint8_t {
    None,
    BwFull,
    BwScrub,
    BwFast,
    BwFastOverGray,
    BwRedrive,
    GrayPrecondition,
    GrayOverlay,
    GrayAbsolute,
    GrayDirect,
  };
  enum class GrayUpload : uint8_t { Idle, AwaitLsb, AwaitMsb, Ready };

  struct Plane {
    PlaneContent content{PlaneContent::Unknown};
    Generation generation{0};

    bool is_bw(Generation expected) const {
      return content == PlaneContent::BwFrame && generation == expected;
    }
  };

  struct Panel {
    PanelContent content{PanelContent::Unknown};
    Generation generation{0};
    Pipeline source{Pipeline::None};
  };

  struct Snapshot {
    Lifecycle lifecycle{Lifecycle::Uninitialized};
    Plane dtm1{};
    Plane dtm2{};
    Panel panel{};
    Pipeline pending{Pipeline::None};
    GrayUpload gray_upload{GrayUpload::Idle};
    Generation target_generation{0};
    Generation baseline_generation{0};
    bool baseline_staged{false};
    bool strong_sync_required{true};
    bool gray_reference_established{false};
    bool bw_redrive_required{false};
    bool preserve_gray_for_next_bw{false};
  };

  const Snapshot &snapshot() const { return state_; }

  void cold_boot() {
    state_ = {};
    state_.lifecycle = Lifecycle::ReadyOff;
  }

  // A retained controller is real but its contents are not automatically known
  // to a newly created host object. Adoption is a separate explicit operation.
  void retained_boot() {
    state_ = {};
    state_.lifecycle = Lifecycle::ReadyOff;
    state_.dtm1.content = PlaneContent::RetainedUnknown;
    state_.dtm2.content = PlaneContent::RetainedUnknown;
    state_.panel.content = PanelContent::RetainedUnknown;
  }

  bool adopt_retained_monochrome(Generation generation) {
    if (!ready_() || busy_() || generation == 0)
      return false;
    state_.dtm1 = {PlaneContent::BwFrame, generation};
    state_.dtm2 = {PlaneContent::BwFrame, generation};
    state_.panel = {PanelContent::Monochrome, generation, Pipeline::None};
    state_.baseline_generation = generation;
    state_.baseline_staged = true;
    state_.strong_sync_required = false;
    state_.gray_reference_established = false;
    state_.bw_redrive_required = false;
    state_.preserve_gray_for_next_bw = false;
    return invariant();
  }

  bool stage_baseline(Generation generation) {
    if (!ready_() || busy_() || generation == 0)
      return false;
    state_.baseline_generation = generation;
    state_.baseline_staged = true;
    return invariant();
  }

  bool begin_bw(Pipeline pipeline, Generation target_generation) {
    if (!ready_() || busy_() || target_generation == 0 || !is_bw_pipeline_(pipeline))
      return false;

    if (pipeline == Pipeline::BwFast) {
      if (state_.strong_sync_required || state_.bw_redrive_required || !state_.baseline_staged ||
          state_.panel.content != PanelContent::Monochrome ||
          state_.baseline_generation != state_.panel.generation ||
          !state_.dtm1.is_bw(state_.baseline_generation))
        return false;
    }
    if (pipeline == Pipeline::BwFastOverGray) {
      if (!state_.preserve_gray_for_next_bw || state_.strong_sync_required || !state_.baseline_staged ||
          (state_.panel.content != PanelContent::NativeGray &&
           state_.panel.content != PanelContent::NativeGrayWithBwDelta) ||
          !state_.dtm1.is_bw(state_.baseline_generation))
        return false;
    }

    state_.target_generation = target_generation;
    state_.pending = pipeline;
    return invariant();
  }

  bool mark_new_target_uploaded() {
    if (!(is_bw_pipeline_(state_.pending) || state_.pending == Pipeline::GrayPrecondition) ||
        state_.target_generation == 0)
      return false;
    state_.dtm2 = {PlaneContent::BwFrame, state_.target_generation};
    return invariant();
  }

  bool mark_old_white_uploaded() {
    if (state_.pending != Pipeline::BwFull) return false;
    state_.dtm1 = {PlaneContent::White, 0};
    return invariant();
  }

  bool mark_old_complement_uploaded() {
    if (state_.pending != Pipeline::BwScrub && state_.pending != Pipeline::BwRedrive) return false;
    state_.dtm1 = {PlaneContent::BwComplement, state_.target_generation};
    return invariant();
  }

  bool mark_old_target_restored() {
    if (!(is_bw_pipeline_(state_.pending) || state_.pending == Pipeline::GrayPrecondition) ||
        state_.target_generation == 0)
      return false;
    state_.dtm1 = {PlaneContent::BwFrame, state_.target_generation};
    return invariant();
  }

  bool complete_bw() {
    if (!ready_() || !is_bw_pipeline_(state_.pending) || state_.target_generation == 0 ||
        !state_.dtm1.is_bw(state_.target_generation) || !state_.dtm2.is_bw(state_.target_generation))
      return false;
    const Generation displayed = state_.target_generation;
    const bool kept_gray = state_.pending == Pipeline::BwFastOverGray;
    state_.panel = {kept_gray ? PanelContent::NativeGrayWithBwDelta : PanelContent::Monochrome,
                    displayed, state_.pending};
    state_.dtm1 = {PlaneContent::BwFrame, displayed};
    state_.dtm2 = {PlaneContent::BwFrame, displayed};
    state_.baseline_generation = displayed;
    state_.baseline_staged = true;
    state_.strong_sync_required = false;
    state_.bw_redrive_required = false;
    state_.preserve_gray_for_next_bw = false;
    clear_pending_();
    return invariant();
  }

  bool begin_gray_precondition(Generation new_baseline) {
    if (!ready_() || busy_() || new_baseline == 0 || !state_.baseline_staged ||
        state_.strong_sync_required || !state_.dtm1.is_bw(state_.baseline_generation))
      return false;
    state_.target_generation = new_baseline;
    state_.pending = Pipeline::GrayPrecondition;
    return invariant();
  }

  bool complete_gray_precondition() {
    if (!ready_() || state_.pending != Pipeline::GrayPrecondition || state_.target_generation == 0 ||
        !state_.dtm1.is_bw(state_.target_generation) || !state_.dtm2.is_bw(state_.target_generation))
      return false;
    const Generation displayed = state_.target_generation;
    // A zoned transition changes only its PTL window. Native gray outside it
    // remains visible until the following gray activation covers the union.
    const bool retained_gray = state_.panel.content == PanelContent::NativeGray ||
                               state_.panel.content == PanelContent::NativeGrayWithBwDelta;
    state_.panel = {retained_gray ? PanelContent::NativeGrayWithBwDelta : PanelContent::Monochrome,
                    displayed, Pipeline::GrayPrecondition};
    state_.dtm1 = {PlaneContent::BwFrame, displayed};
    state_.dtm2 = {PlaneContent::BwFrame, displayed};
    state_.baseline_generation = displayed;
    state_.baseline_staged = true;
    state_.strong_sync_required = false;
    state_.bw_redrive_required = false;
    state_.preserve_gray_for_next_bw = false;
    clear_pending_();
    return invariant();
  }

  bool begin_gray_upload(Pipeline pipeline, Generation target_generation) {
    if (!ready_() || busy_() || target_generation == 0 || !is_gray_pipeline_(pipeline))
      return false;
    if (pipeline != Pipeline::GrayDirect &&
        (!state_.baseline_staged || state_.baseline_generation != target_generation))
      return false;
    state_.pending = pipeline;
    state_.target_generation = target_generation;
    state_.gray_upload = GrayUpload::AwaitLsb;
    return invariant();
  }

  bool stage_gray_lsb() {
    if (state_.gray_upload != GrayUpload::AwaitLsb || !is_gray_pipeline_(state_.pending))
      return false;
    state_.gray_upload = GrayUpload::AwaitMsb;
    return invariant();
  }

  bool stage_gray_msb() {
    if (state_.gray_upload != GrayUpload::AwaitMsb || !is_gray_pipeline_(state_.pending))
      return false;
    state_.gray_upload = GrayUpload::Ready;
    return invariant();
  }

  bool mark_gray_planes_in_controller() {
    if (state_.gray_upload != GrayUpload::Ready || !is_gray_pipeline_(state_.pending))
      return false;
    state_.dtm1 = {PlaneContent::GrayPlane0, state_.target_generation};
    state_.dtm2 = {PlaneContent::GrayPlane1, state_.target_generation};
    return invariant();
  }

  bool mark_both_baseline_restored() {
    if ((state_.pending != Pipeline::GrayOverlay && state_.pending != Pipeline::GrayAbsolute) ||
        !state_.baseline_staged)
      return false;
    state_.dtm1 = {PlaneContent::BwFrame, state_.baseline_generation};
    state_.dtm2 = {PlaneContent::BwFrame, state_.baseline_generation};
    return invariant();
  }

  bool complete_gray() {
    if (!ready_() || state_.gray_upload != GrayUpload::Ready || !is_gray_pipeline_(state_.pending))
      return false;
    const Pipeline pipeline = state_.pending;
    if (pipeline == Pipeline::GrayDirect) {
      if (state_.dtm1.content != PlaneContent::GrayPlane0 || state_.dtm2.content != PlaneContent::GrayPlane1)
        return false;
    } else if (!state_.dtm1.is_bw(state_.baseline_generation) || !state_.dtm2.is_bw(state_.baseline_generation)) {
      return false;
    }
    const Generation displayed = state_.target_generation;
    state_.panel = {PanelContent::NativeGray, displayed, pipeline};
    state_.gray_reference_established = true;
    if (pipeline == Pipeline::GrayDirect) {
      // The two controller planes still describe the directly displayed gray
      // selectors. A later differential B/W refresh must not treat them as a
      // clean monochrome baseline.
      state_.baseline_staged = false;
      state_.baseline_generation = 0;
      state_.strong_sync_required = true;
      state_.bw_redrive_required = false;
      state_.preserve_gray_for_next_bw = false;
    } else {
      // Overlay/absolute pipelines restore both controller planes from the
      // exact staged B/W base after the gray DRF.
      state_.strong_sync_required = false;
      state_.bw_redrive_required = true;
      state_.preserve_gray_for_next_bw = false;
    }
    clear_pending_();
    return invariant();
  }

  void mark_power_on() {
    if (state_.lifecycle == Lifecycle::ReadyOff)
      state_.lifecycle = Lifecycle::ReadyOn;
  }

  void mark_power_off() {
    if (state_.lifecycle == Lifecycle::ReadyOn)
      state_.lifecycle = Lifecycle::ReadyOff;
  }

  bool preserve_gray_for_next_bw_delta() {
    if (!ready_() || busy_() || state_.strong_sync_required || !state_.baseline_staged ||
        (state_.panel.content != PanelContent::NativeGray &&
         state_.panel.content != PanelContent::NativeGrayWithBwDelta))
      return false;
    state_.preserve_gray_for_next_bw = true;
    state_.bw_redrive_required = false;
    return invariant();
  }

  void deep_sleep() {
    cancel();
    state_.lifecycle = Lifecycle::DeepSleep;
    state_.dtm1 = {};
    state_.dtm2 = {};
    state_.baseline_generation = 0;
    state_.baseline_staged = false;
    state_.strong_sync_required = true;
    state_.bw_redrive_required = false;
    state_.preserve_gray_for_next_bw = false;
  }

  void cancel() {
    if (busy_()) {
      state_.dtm1 = {};
      state_.dtm2 = {};
      state_.baseline_generation = 0;
      state_.baseline_staged = false;
      state_.strong_sync_required = true;
      state_.bw_redrive_required = false;
      state_.preserve_gray_for_next_bw = false;
    }
    clear_pending_();
  }

  void force_resync() {
    clear_pending_();
    state_.dtm1 = {};
    state_.dtm2 = {};
    state_.baseline_generation = 0;
    state_.baseline_staged = false;
    state_.strong_sync_required = true;
    state_.bw_redrive_required = false;
    state_.preserve_gray_for_next_bw = false;
  }

  bool invariant() const {
    if ((state_.lifecycle == Lifecycle::Uninitialized || state_.lifecycle == Lifecycle::DeepSleep) && busy_())
      return false;
    if (state_.gray_upload != GrayUpload::Idle && !is_gray_pipeline_(state_.pending))
      return false;
    if (is_gray_pipeline_(state_.pending) && state_.gray_upload == GrayUpload::Idle)
      return false;
    if (state_.baseline_staged && state_.baseline_generation == 0)
      return false;
    if (!state_.baseline_staged && state_.baseline_generation != 0)
      return false;
    if (state_.pending == Pipeline::BwFast) {
      if (state_.strong_sync_required || state_.bw_redrive_required ||
          state_.panel.content != PanelContent::Monochrome || !state_.baseline_staged)
        return false;
    }
    if (state_.pending == Pipeline::BwFastOverGray && !state_.preserve_gray_for_next_bw)
      return false;
    if (state_.pending == Pipeline::GrayDirect && state_.baseline_staged &&
        state_.baseline_generation == state_.target_generation) {
      // A Direct job may coexist with an older staged baseline, but it must not
      // masquerade as the target it is about to paint.
      return false;
    }
    return true;
  }

 private:
  static bool is_bw_pipeline_(Pipeline pipeline) {
    return pipeline == Pipeline::BwFull || pipeline == Pipeline::BwScrub || pipeline == Pipeline::BwFast ||
           pipeline == Pipeline::BwFastOverGray || pipeline == Pipeline::BwRedrive;
  }

  static bool is_gray_pipeline_(Pipeline pipeline) {
    return pipeline == Pipeline::GrayOverlay || pipeline == Pipeline::GrayAbsolute ||
           pipeline == Pipeline::GrayDirect;
  }

  bool ready_() const {
    return state_.lifecycle == Lifecycle::ReadyOff || state_.lifecycle == Lifecycle::ReadyOn;
  }

  bool busy_() const { return state_.pending != Pipeline::None; }

  void clear_pending_() {
    state_.pending = Pipeline::None;
    state_.gray_upload = GrayUpload::Idle;
    state_.target_generation = 0;
  }

  Snapshot state_{};
};

}  // namespace freeink

#include <cassert>
#include <cstdio>

#include "src/driver/Uc8279X4RefreshState.h"

using State = freeink::Uc8279X4RefreshState;

static void finish_bw_uploads(State& state, State::Pipeline pipeline) {
  assert(state.mark_new_target_uploaded());
  if (pipeline == State::Pipeline::BwFull)
    assert(state.mark_old_white_uploaded());
  else if (pipeline == State::Pipeline::BwScrub || pipeline == State::Pipeline::BwRedrive)
    assert(state.mark_old_complement_uploaded());
  assert(state.mark_old_target_restored());
  assert(state.complete_bw());
}

static void test_cold_bw_lifecycle() {
  State state;
  state.cold_boot();
  assert(state.invariant());
  assert(!state.begin_bw(State::Pipeline::BwFast, 1));
  assert(state.begin_bw(State::Pipeline::BwFull, 1));
  finish_bw_uploads(state, State::Pipeline::BwFull);
  assert(state.snapshot().panel.content == State::PanelContent::Monochrome);
  assert(state.snapshot().dtm1.is_bw(1));
  assert(state.snapshot().dtm2.is_bw(1));
  assert(state.begin_bw(State::Pipeline::BwFast, 2));
  finish_bw_uploads(state, State::Pipeline::BwFast);
  assert(state.snapshot().baseline_generation == 2);
}

static void test_gray_overlay_requires_and_restores_baseline() {
  State state;
  state.cold_boot();
  assert(!state.begin_gray_upload(State::Pipeline::GrayOverlay, 1));
  assert(state.begin_bw(State::Pipeline::BwFull, 1));
  finish_bw_uploads(state, State::Pipeline::BwFull);
  assert(state.begin_gray_upload(State::Pipeline::GrayOverlay, 1));
  assert(!state.stage_gray_msb());
  assert(state.stage_gray_lsb());
  assert(state.stage_gray_msb());
  assert(state.mark_gray_planes_in_controller());
  assert(state.mark_both_baseline_restored());
  assert(state.complete_gray());
  assert(state.snapshot().panel.content == State::PanelContent::NativeGray);
  assert(state.snapshot().dtm1.is_bw(1));
  assert(state.snapshot().dtm2.is_bw(1));
  assert(state.snapshot().bw_redrive_required);
  assert(!state.begin_bw(State::Pipeline::BwFast, 2));
  assert(state.begin_bw(State::Pipeline::BwRedrive, 2));
  finish_bw_uploads(state, State::Pipeline::BwRedrive);
  assert(!state.snapshot().bw_redrive_required);
}

static void test_direct_gray_invalidates_bw_baseline() {
  State state;
  state.cold_boot();
  assert(state.begin_gray_upload(State::Pipeline::GrayDirect, 7));
  assert(state.stage_gray_lsb());
  assert(state.stage_gray_msb());
  assert(state.mark_gray_planes_in_controller());
  assert(state.complete_gray());
  assert(state.snapshot().panel.content == State::PanelContent::NativeGray);
  assert(state.snapshot().strong_sync_required);
  assert(!state.snapshot().baseline_staged);
  assert(!state.begin_bw(State::Pipeline::BwFast, 8));
  assert(state.begin_bw(State::Pipeline::BwFull, 8));
  finish_bw_uploads(state, State::Pipeline::BwFull);
}

static void test_explicit_bw_delta_can_preserve_native_gray() {
  State state;
  state.cold_boot();
  assert(state.begin_bw(State::Pipeline::BwFull, 1));
  finish_bw_uploads(state, State::Pipeline::BwFull);
  assert(state.begin_gray_upload(State::Pipeline::GrayOverlay, 1));
  assert(state.stage_gray_lsb());
  assert(state.stage_gray_msb());
  assert(state.mark_gray_planes_in_controller());
  assert(state.mark_both_baseline_restored());
  assert(state.complete_gray());
  assert(!state.begin_bw(State::Pipeline::BwFastOverGray, 2));
  assert(state.preserve_gray_for_next_bw_delta());
  assert(state.begin_bw(State::Pipeline::BwFastOverGray, 2));
  finish_bw_uploads(state, State::Pipeline::BwFastOverGray);
  assert(state.snapshot().panel.content == State::PanelContent::NativeGrayWithBwDelta);
  assert(state.snapshot().baseline_generation == 2);
  assert(!state.snapshot().preserve_gray_for_next_bw);
}

static void test_retained_state_is_not_guessed() {
  State state;
  state.retained_boot();
  assert(state.invariant());
  assert(state.snapshot().panel.content == State::PanelContent::RetainedUnknown);
  assert(!state.begin_bw(State::Pipeline::BwFast, 2));
  assert(state.adopt_retained_monochrome(1));
  assert(state.begin_bw(State::Pipeline::BwFast, 2));
  finish_bw_uploads(state, State::Pipeline::BwFast);
}

static void test_precondition_transition() {
  State state;
  state.cold_boot();
  assert(state.begin_bw(State::Pipeline::BwFull, 1));
  finish_bw_uploads(state, State::Pipeline::BwFull);
  assert(state.begin_gray_precondition(2));
  assert(state.mark_new_target_uploaded());
  assert(state.mark_old_target_restored());
  assert(state.complete_gray_precondition());
  assert(state.snapshot().panel.generation == 2);
  assert(state.snapshot().dtm1.is_bw(2));
  assert(state.snapshot().dtm2.is_bw(2));
}

static void test_zoned_precondition_keeps_unaffected_gray() {
  State state;
  state.cold_boot();
  assert(state.begin_bw(State::Pipeline::BwFull, 1));
  finish_bw_uploads(state, State::Pipeline::BwFull);
  assert(state.begin_gray_upload(State::Pipeline::GrayOverlay, 1));
  assert(state.stage_gray_lsb());
  assert(state.stage_gray_msb());
  assert(state.mark_gray_planes_in_controller());
  assert(state.mark_both_baseline_restored());
  assert(state.complete_gray());
  assert(state.begin_gray_precondition(2));
  assert(state.mark_new_target_uploaded());
  assert(state.mark_old_target_restored());
  assert(state.complete_gray_precondition());
  assert(state.snapshot().panel.content == State::PanelContent::NativeGrayWithBwDelta);
  assert(state.snapshot().dtm1.is_bw(2));
  assert(state.snapshot().dtm2.is_bw(2));
}

static void test_cancel_is_conservative() {
  State state;
  state.cold_boot();
  assert(state.begin_bw(State::Pipeline::BwFull, 1));
  state.cancel();
  assert(state.invariant());
  assert(state.snapshot().strong_sync_required);
  assert(!state.snapshot().baseline_staged);
  assert(!state.begin_bw(State::Pipeline::BwFast, 2));
}

static void test_sleep_invalidates_controller_ram_only() {
  State state;
  state.cold_boot();
  assert(state.begin_bw(State::Pipeline::BwFull, 1));
  finish_bw_uploads(state, State::Pipeline::BwFull);
  state.deep_sleep();
  assert(state.invariant());
  assert(state.snapshot().lifecycle == State::Lifecycle::DeepSleep);
  assert(state.snapshot().panel.content == State::PanelContent::Monochrome);
  assert(state.snapshot().dtm1.content == State::PlaneContent::Unknown);
  assert(state.snapshot().strong_sync_required);
}

int main() {
  test_cold_bw_lifecycle();
  test_gray_overlay_requires_and_restores_baseline();
  test_direct_gray_invalidates_bw_baseline();
  test_explicit_bw_delta_can_preserve_native_gray();
  test_retained_state_is_not_guessed();
  test_precondition_transition();
  test_zoned_precondition_keeps_unaffected_gray();
  test_cancel_is_conservative();
  test_sleep_invalidates_controller_ram_only();
  std::puts("PASS: UC8279 X4 refresh-state invariants");
}

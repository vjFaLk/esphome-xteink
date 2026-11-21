#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <BoardConfig.h>

#include "driver/Uc8279X4Driver.h"

using freeink::EpdBus;
using freeink::GrayscaleMode;
using freeink::RefreshMode;
using freeink::Uc8279X4Driver;

static std::vector<uint8_t> visible(const EpdBus::PlaneUpload& upload) {
  constexpr size_t kGateOffset = 120;
  constexpr size_t kVisibleRows = 4;
  assert(upload.data.size() >= kGateOffset + kVisibleRows);
  return {upload.data.begin() + kGateOffset, upload.data.begin() + kGateOffset + kVisibleRows};
}

static size_t command_after(const std::vector<uint8_t>& commands, uint8_t command, size_t after) {
  const auto found = std::find(commands.begin() + static_cast<std::ptrdiff_t>(after), commands.end(), command);
  assert(found != commands.end());
  return static_cast<size_t>(found - commands.begin());
}

static void test_async_target_is_driver_owned() {
  EpdBus bus;
  Uc8279X4Driver driver;
  driver.begin(bus);

  uint8_t target[] = {0xF0, 0x0F, 0xAA, 0x55};
  const std::vector<uint8_t> expected(target, target + 4);
  const size_t commandStart = bus.commands.size();
  assert(driver.displayStart(bus, target, nullptr, RefreshMode::Fast, false));
  std::fill(std::begin(target), std::end(target), 0x00);
  driver.displayFinish(bus, target);

  assert(visible(bus.uploads.back()) == expected);
  const size_t cdi = command_after(bus.commands, 0x50, commandStart);
  const size_t ccset = command_after(bus.commands, 0xE0, cdi + 1);
  const size_t tsset = command_after(bus.commands, 0xE5, ccset + 1);
  const size_t pon = command_after(bus.commands, 0x04, tsset + 1);
  const size_t psr = command_after(bus.commands, 0x00, pon + 1);
  const size_t drf = command_after(bus.commands, 0x12, psr + 1);
  assert(cdi < ccset && ccset < tsset && tsset < pon && pon < psr && psr < drf);
}

static void test_fast_window_and_gray_plane_restore() {
  EpdBus bus;
  Uc8279X4Driver driver;
  driver.begin(bus);

  uint8_t base1[] = {0xF0, 0x0F, 0xAA, 0x55};
  driver.display(bus, base1, nullptr, RefreshMode::Full, false);

  uint8_t base2[] = {0xCC, 0x33, 0xC3, 0x3C};
  const size_t commandStart = bus.commands.size();
  const size_t uploadStart = bus.uploads.size();
  assert(driver.displayStart(bus, base2, nullptr, RefreshMode::Fast, false));
  driver.displayFinish(bus, base2);
  assert(bus.uploads.size() == uploadStart + 2);  // DTM2 before DRF, DTM1 after it.
  const size_t ptin = command_after(bus.commands, 0x91, commandStart);
  const size_t ptl = command_after(bus.commands, 0x90, ptin + 1);
  const size_t psr = command_after(bus.commands, 0x00, ptl + 1);
  const size_t drf = command_after(bus.commands, 0x12, psr + 1);
  const size_t ptout = command_after(bus.commands, 0x92, drf + 1);
  assert(ptin < ptl && ptl < psr && psr < drf && drf < ptout);

  driver.beginGrayscale(bus, base2, GrayscaleMode::Overlay, RefreshMode::Fast, false);
  uint8_t lsb[] = {0x01, 0x02, 0x04, 0x08};
  uint8_t msb[] = {0x10, 0x20, 0x40, 0x80};
  driver.copyGrayscaleLsb(bus, lsb);
  driver.copyGrayscaleMsb(bus, msb);
  const size_t grayUploads = bus.uploads.size();
  driver.displayGray(bus, base2, false, nullptr, false);
  assert(bus.uploads.size() == grayUploads + 4);

  std::vector<uint8_t> plane0(4), plane1(4);
  for (size_t i = 0; i < 4; ++i) {
    const uint8_t absolute0 = static_cast<uint8_t>(base2[i] | lsb[i]);
    plane0[i] = static_cast<uint8_t>(~absolute0);
    plane1[i] = static_cast<uint8_t>(~static_cast<uint8_t>(absolute0 ^ msb[i]));
  }
  assert(visible(bus.uploads[grayUploads]) == plane0);
  assert(visible(bus.uploads[grayUploads + 1]) == plane1);
  assert(visible(bus.uploads[grayUploads + 2]) == std::vector<uint8_t>(base2, base2 + 4));
  assert(visible(bus.uploads[grayUploads + 3]) == std::vector<uint8_t>(base2, base2 + 4));

  driver.powerOffRetainingState(bus);
  const size_t preconditionStart = bus.commands.size();
  driver.beginGrayscale(bus, base2, GrayscaleMode::Overlay, RefreshMode::Fast, false);
  const size_t preconditionPsr = command_after(bus.commands, 0x00, preconditionStart);
  const size_t preconditionLut = command_after(bus.commands, 0x20, preconditionPsr + 1);
  const size_t preconditionPon = command_after(bus.commands, 0x04, preconditionLut + 1);
  const size_t preconditionPtl = command_after(bus.commands, 0x90, preconditionPon + 1);
  const size_t finalPsr = command_after(bus.commands, 0x00, preconditionPtl + 1);
  const size_t preconditionDrf = command_after(bus.commands, 0x12, finalPsr + 1);
  assert(preconditionPsr < preconditionLut && preconditionLut < preconditionPon &&
         preconditionPon < preconditionPtl && preconditionPtl < finalPsr &&
         finalPsr < preconditionDrf);
}

static void test_sleep_forces_strong_refresh() {
  EpdBus bus;
  Uc8279X4Driver driver;
  driver.begin(bus);
  uint8_t base[] = {0xFF, 0x00, 0xFF, 0x00};
  driver.display(bus, base, nullptr, RefreshMode::Full, false);
  driver.deepSleep(bus);

  // A fresh begin is required after DSLP, as on the real wake path.
  driver.begin(bus);
  const size_t start = bus.commands.size();
  driver.display(bus, base, nullptr, RefreshMode::Fast, false);
  const size_t pon = command_after(bus.commands, 0x04, start);
  const size_t psr = command_after(bus.commands, 0x00, pon + 1);
  assert(std::find(bus.commands.begin() + static_cast<std::ptrdiff_t>(start),
                   bus.commands.begin() + static_cast<std::ptrdiff_t>(psr), 0x91) ==
         bus.commands.begin() + static_cast<std::ptrdiff_t>(psr));
}

static void test_explicit_conditioning_redrive() {
  EpdBus bus;
  Uc8279X4Driver driver;
  driver.begin(bus);
  uint8_t black[] = {0, 0, 0, 0};
  driver.display(bus, black, nullptr, RefreshMode::Half, false);
  const size_t firstUpload = bus.uploads.size();
  driver.display(bus, black, nullptr, RefreshMode::ConditioningRedrive, false);
  assert(bus.uploads.size() == firstUpload + 3);
  assert(bus.uploads[firstUpload].command == 0x13);
  assert(visible(bus.uploads[firstUpload]) == std::vector<uint8_t>(4, 0x00));
  assert(bus.uploads[firstUpload + 1].command == 0x10);
  assert(visible(bus.uploads[firstUpload + 1]) == std::vector<uint8_t>(4, 0xFF));
  assert(bus.uploads[firstUpload + 2].command == 0x10);
  assert(visible(bus.uploads[firstUpload + 2]) == std::vector<uint8_t>(4, 0x00));
}

static void test_half_keeps_absolute_gc_and_scrub_is_explicit() {
  uint8_t black[] = {0, 0, 0, 0};
  {
    EpdBus bus;
    Uc8279X4Driver driver;
    driver.begin(bus);
    driver.display(bus, black, nullptr, RefreshMode::Half, false);
    assert(bus.uploads.size() >= 2);
    assert(bus.uploads[0].command == 0x13);
    assert(bus.uploads[1].command == 0x10);
    assert(visible(bus.uploads[1]) == std::vector<uint8_t>(4, 0xFF));
  }
  {
    EpdBus bus;
    Uc8279X4Driver driver;
    driver.begin(bus);
    driver.display(bus, black, nullptr, RefreshMode::ConditioningScrub, false);
    assert(bus.uploads.size() >= 2);
    assert(bus.uploads[0].command == 0x13);
    assert(bus.uploads[1].command == 0x10);
    assert(visible(bus.uploads[1]) == std::vector<uint8_t>(4, 0xFF));
  }

  uint8_t white[] = {0xFF, 0xFF, 0xFF, 0xFF};
  {
    EpdBus bus;
    Uc8279X4Driver driver;
    driver.begin(bus);
    driver.display(bus, white, nullptr, RefreshMode::Half, false);
    assert(bus.uploads.size() >= 2);
    assert(bus.uploads[1].command == 0x10);
    assert(visible(bus.uploads[1]) == std::vector<uint8_t>(4, 0xFF));
  }
  EpdBus bus;
  Uc8279X4Driver driver;
  driver.begin(bus);
  driver.display(bus, white, nullptr, RefreshMode::ConditioningScrub, false);
  assert(visible(bus.uploads[1]) == std::vector<uint8_t>(4, 0x00));
}

static void test_selective_precondition_resets_gray_without_driving_static_bw() {
  EpdBus bus;
  Uc8279X4Driver driver(freeink::uc8279X4DefaultConfig(),
                        freeink::uc8279X4DefaultPipelines());
  driver.begin(bus);
  uint8_t black[] = {0, 0, 0, 0};
  uint8_t gray[] = {0x80, 0, 0, 0};
  driver.beginGrayscale(bus, black, GrayscaleMode::Overlay, RefreshMode::Full, false);
  driver.copyGrayscaleLsb(bus, gray);
  driver.copyGrayscaleMsb(bus, gray);
  driver.displayGray(bus, black, false, nullptr, false);

  uint8_t target[] = {0x01, 0, 0, 0};
  driver.powerOffRetainingState(bus);
  const size_t uploadStart = bus.uploads.size();
  const size_t commandStart = bus.commands.size();
  const size_t dataStart = bus.dataWrites.size();
  driver.beginGrayscale(bus, target, GrayscaleMode::Overlay, RefreshMode::Fast, false);
  assert(bus.uploads.size() == uploadStart + 3);
  assert(bus.uploads[uploadStart].command == 0x13);
  assert(visible(bus.uploads[uploadStart]) == std::vector<uint8_t>(target, target + 4));
  assert(bus.uploads[uploadStart + 1].command == 0x10);
  assert(visible(bus.uploads[uploadStart + 1]) == std::vector<uint8_t>({0x80, 0, 0, 0}));
  assert(bus.uploads[uploadStart + 2].command == 0x10);
  assert(visible(bus.uploads[uploadStart + 2]) == std::vector<uint8_t>(target, target + 4));
  const size_t configPsr = command_after(bus.commands, 0x00, commandStart);
  const size_t lut = command_after(bus.commands, 0x20, configPsr + 1);
  const size_t pon = command_after(bus.commands, 0x04, lut + 1);
  const size_t ptl = command_after(bus.commands, 0x90, pon + 1);
  const size_t finalPsr = command_after(bus.commands, 0x00, ptl + 1);
  const size_t drf = command_after(bus.commands, 0x12, finalPsr + 1);
  assert(configPsr < lut && lut < pon && pon < ptl && ptl < finalPsr && finalPsr < drf);

  unsigned noDriveTables = 0;
  unsigned transitionTables = 0;
  for (size_t i = dataStart; i < bus.dataWrites.size(); ++i) {
    const auto& write = bus.dataWrites[i];
    if (write.command == 0x21 || write.command == 0x24) {
      assert(write.data.size() == 42);
      assert(std::all_of(write.data.begin(), write.data.end(), [](uint8_t b) { return b == 0; }));
      ++noDriveTables;
    } else if (write.command == 0x22 || write.command == 0x23) {
      assert(write.data.size() == 42);
      assert(std::any_of(write.data.begin(), write.data.end(), [](uint8_t b) { return b != 0; }));
      ++transitionTables;
    }
  }
  assert(noDriveTables == 2 && transitionTables == 2);

  driver.copyGrayscaleLsb(bus, gray);
  driver.copyGrayscaleMsb(bus, gray);
  driver.displayGray(bus, target, false, nullptr, false);
  driver.preserveGrayscaleOnFastRefresh();
  uint8_t preview[] = {0x81, 0, 0, 0};
  driver.display(bus, preview, nullptr, RefreshMode::Fast, false);
  const size_t previewSettleUploads = bus.uploads.size();
  driver.beginGrayscale(bus, preview, GrayscaleMode::Overlay, RefreshMode::Fast, false);
  assert(bus.uploads[previewSettleUploads + 1].command == 0x10);
  assert(visible(bus.uploads[previewSettleUploads + 1]) ==
         std::vector<uint8_t>(preview, preview + 4));
}

static void test_new_gray_pixels_are_preconditioned_before_overlay() {
  EpdBus bus;
  Uc8279X4Driver driver(freeink::uc8279X4DefaultConfig(),
                        freeink::uc8279X4DefaultPipelines());
  driver.begin(bus);
  uint8_t black[] = {0, 0, 0, 0};
  uint8_t empty[] = {0, 0, 0, 0};
  driver.beginGrayscale(bus, black, GrayscaleMode::Overlay, RefreshMode::Full, false);
  driver.copyGrayscaleLsb(bus, empty);
  driver.copyGrayscaleMsb(bus, empty);
  driver.displayGray(bus, black, false, nullptr, false);

  // The requested window covers row 0; the new gray pixel is in row 3.
  // Both precondition and overlay must cover row 3, and the first pass must
  // drive white-to-black there even though it was previously plain black.
  uint8_t targetLsb[] = {0, 0, 0, 0};
  uint8_t targetMsb[] = {0, 0, 0, 0x40};
  driver.setGrayscaleWindow(0, 0, 8, 1);
  driver.prepareGrayscaleOverlayTarget(targetLsb, targetMsb);
  const size_t uploadStart = bus.uploads.size();
  const size_t dataStart = bus.dataWrites.size();
  driver.beginGrayscale(bus, black, GrayscaleMode::Overlay, RefreshMode::Fast, false);
  assert(bus.uploads[uploadStart + 1].command == 0x10);
  assert(visible(bus.uploads[uploadStart + 1])[3] == 0x40);
  std::vector<uint8_t> ptl;
  for (size_t i = dataStart; i < bus.dataWrites.size(); ++i)
    if (bus.dataWrites[i].command == 0x90)
      ptl.insert(ptl.end(), bus.dataWrites[i].data.begin(), bus.dataWrites[i].data.end());
  assert(ptl.size() == 9);
  assert(ptl[6] == 0 && ptl[7] == 123);  // row 3 plus 120-gate offset

  driver.copyGrayscaleLsb(bus, targetLsb);
  driver.copyGrayscaleMsb(bus, targetMsb);
  driver.displayGray(bus, black, false, nullptr, false);
}

static void test_gray_window_preserves_requested_geometry() {
  const uint16_t originalWidth = BoardConfig::ACTIVE.displayWidth;
  BoardConfig::ACTIVE.displayWidth = 32;
  EpdBus bus;
  Uc8279X4Driver driver(freeink::uc8279X4DefaultConfig(),
                        freeink::uc8279X4DefaultPipelines());
  driver.begin(bus);
  uint8_t black[16] = {};
  driver.beginGrayscale(bus, black, GrayscaleMode::Overlay, RefreshMode::Full, false);
  driver.copyGrayscaleLsb(bus, black);
  driver.copyGrayscaleMsb(bus, black);
  driver.displayGray(bus, black, false, nullptr, false);

  driver.setGrayscaleWindow(8, 1, 8, 2);
  const size_t dataStart = bus.dataWrites.size();
  const size_t uploadStart = bus.uploads.size();
  driver.beginGrayscale(bus, black, GrayscaleMode::Overlay, RefreshMode::Fast, false);
  std::vector<uint8_t> ptl;
  for (size_t i = dataStart; i < bus.dataWrites.size(); ++i)
    if (bus.dataWrites[i].command == 0x90)
      ptl.insert(ptl.end(), bus.dataWrites[i].data.begin(), bus.dataWrites[i].data.end());
  assert(ptl.size() == 9);
  assert(ptl[0] == 0 && ptl[1] == 8 && ptl[2] == 0 && ptl[3] == 15);
  assert(ptl[4] == 0 && ptl[5] == 121 && ptl[6] == 0 && ptl[7] == 122);
  // Window policy does not change the image-plane byte order.
  assert(bus.uploads[uploadStart].command == 0x13);
  BoardConfig::ACTIVE.displayWidth = originalWidth;
}

static void test_windowed_gray_upload_matches_full_plane_slice() {
  const uint16_t originalWidth = BoardConfig::ACTIVE.displayWidth;
  BoardConfig::ACTIVE.displayWidth = 32;

  auto upload = [](bool packed) {
    EpdBus bus;
    Uc8279X4Driver driver;
    driver.begin(bus);
    uint8_t base[16] = {};
    uint8_t empty[16] = {};
    driver.beginGrayscale(bus, base, GrayscaleMode::Overlay, RefreshMode::Full, false);
    driver.copyGrayscaleLsb(bus, empty);
    driver.copyGrayscaleMsb(bus, empty);
    driver.displayGray(bus, base, false, nullptr, false);

    uint8_t gray[16] = {};
    gray[5] = 0xA5;  // x=8..15, visible row 1
    driver.setGrayscaleWindow(8, 1, 8, 2);
    driver.setGrayscaleWindowedUpload(packed);
    driver.beginGrayscale(bus, base, GrayscaleMode::Overlay, RefreshMode::Fast, false);
    driver.copyGrayscaleLsb(bus, gray);
    driver.copyGrayscaleMsb(bus, gray);
    const size_t firstUpload = bus.uploads.size();
    const size_t firstCommand = bus.commands.size();
    driver.displayGray(bus, base, false, nullptr, false);
    assert(bus.uploads.size() == firstUpload + 4);
    assert(bus.uploads[firstUpload].command == 0x10);
    assert(bus.uploads[firstUpload + 1].command == 0x13);
    if (packed) {
      const auto& commands = bus.commands;
      const size_t ptin = command_after(commands, 0x91, firstCommand);
      const size_t ptl = command_after(commands, 0x90, ptin + 1);
      const size_t dtm1 = command_after(commands, 0x10, ptl + 1);
      const size_t dtm2 = command_after(commands, 0x13, dtm1 + 1);
      const size_t ptout = command_after(commands, 0x92, dtm2 + 1);
      assert(ptin < ptl && ptl < dtm1 && dtm1 < dtm2 && dtm2 < ptout);
    }
    return std::vector<EpdBus::PlaneUpload>{bus.uploads[firstUpload], bus.uploads[firstUpload + 1]};
  };

  const auto full = upload(false);
  const auto packed = upload(true);
  for (size_t plane = 0; plane < 2; ++plane) {
    assert(full[plane].data.size() == 600 * 4);
    assert(packed[plane].data.size() == 2);
    assert(packed[plane].data[0] == full[plane].data[121 * 4 + 1]);
    assert(packed[plane].data[1] == full[plane].data[122 * 4 + 1]);
  }
  BoardConfig::ACTIVE.displayWidth = originalWidth;
}

int main() {
  test_async_target_is_driver_owned();
  test_fast_window_and_gray_plane_restore();
  test_sleep_forces_strong_refresh();
  test_explicit_conditioning_redrive();
  test_half_keeps_absolute_gc_and_scrub_is_explicit();
  test_selective_precondition_resets_gray_without_driving_static_bw();
  test_new_gray_pixels_are_preconditioned_before_overlay();
  test_gray_window_preserves_requested_geometry();
  test_windowed_gray_upload_matches_full_plane_slice();
  std::puts("PASS: UC8279 X4 recording-bus refresh pipelines");
}

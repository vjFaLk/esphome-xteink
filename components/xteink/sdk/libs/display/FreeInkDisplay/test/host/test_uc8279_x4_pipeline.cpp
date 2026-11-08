#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

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

int main() {
  test_async_target_is_driver_owned();
  test_fast_window_and_gray_plane_restore();
  test_sleep_forces_strong_refresh();
  std::puts("PASS: UC8279 X4 recording-bus refresh pipelines");
}

#include "Uc8279X4Driver.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "../lut/Uc8279X4VendorLuts.h"

// Orientation of the visible-row stream, switchable per build for field A/B.
// Defaults (0/0) = the stock convention, HARDWARE-CONFIRMED upright on a field
// UC8279 unit: rows forward, no byte/bit mirroring — orientation comes from the
// PSR SHL bit (0x37), which must be written AFTER PON to latch (PON reloads the
// MTP defaults).
#ifndef FREEINK_UC8279X4_ROWREV
#define FREEINK_UC8279X4_ROWREV 0
#endif
#ifndef FREEINK_UC8279X4_XMIRROR
#define FREEINK_UC8279X4_XMIRROR 0
#endif

namespace freeink {
namespace {
// UC8279 (800x480 X4 Pro variant) command set — UC81xx KW family, per the vendor
// X4 Pro display hardware reference.
constexpr uint8_t CMD_PANEL_SETTING = 0x00;       // PSR
constexpr uint8_t CMD_POWER_OFF = 0x02;           // POF
constexpr uint8_t CMD_PFS = 0x03;                 // PFS (power-off sequence)
constexpr uint8_t CMD_POWER_ON = 0x04;            // PON
constexpr uint8_t CMD_DEEP_SLEEP = 0x07;          // DSLP (check code 0xA5)
constexpr uint8_t CMD_DTM1 = 0x10;                // OLD plane in KW mode / AA plane0
constexpr uint8_t CMD_DISPLAY_REFRESH = 0x12;     // DRF
constexpr uint8_t CMD_DTM2 = 0x13;                // NEW plane in KW mode / AA plane1
constexpr uint8_t CMD_PLL = 0x30;                 // PLL frame rate
constexpr uint8_t CMD_VCOM_DATA_INTERVAL = 0x50;  // CDI — 1 data byte on this part
constexpr uint8_t CMD_RESOLUTION = 0x61;          // TRES
constexpr uint8_t CMD_GATE_SOURCE_START = 0x65;   // GSST (4 data bytes)
constexpr uint8_t CMD_PARTIAL_WINDOW = 0x90;      // PTL (window; stock re-issues per partial)
constexpr uint8_t CMD_PARTIAL_IN = 0x91;          // PTIN
constexpr uint8_t CMD_PARTIAL_OUT = 0x92;         // PTOUT
constexpr uint8_t CMD_CCSET = 0xE0;               // CCSET (cascade/output enable)
constexpr uint8_t CMD_GATE_SCAN = 0xE1;           // gate-scan selection
constexpr uint8_t CMD_TSSET = 0xE5;               // TSSET (forced temperature)

// Shutdown must never strand the host after ESPHome has already torn down its
// API and OTA services. POF normally returns BUSY_N high quickly, but a panel
// whose pin remains asserted must not turn a controller wait into an infinite
// MCU-awake state. Two seconds is far longer than the configured PFS sequence;
// after it, proceed with shutdown. A non-retained wake resets and fully redraws
// the controller, while a retained wake has a full minute before its next PON.
constexpr unsigned long POWER_OFF_BUSY_TIMEOUT_MS = 2000;

void waitPowerOffBounded(EpdBus& bus) {
  delay(1);  // allow the low-active BUSY_N assertion to become visible
  const unsigned long started = millis();
  while (bus.isBusy() && millis() - started < POWER_OFF_BUSY_TIMEOUT_MS)
    delay(1);
}

constexpr uint8_t GRAY_LUT_LEN = 49;
struct GrayLut {
  uint8_t cmd;
  uint8_t data[GRAY_LUT_LEN];
};
constexpr GrayLut kXtfAa02[5] = {
    {0x20, {0x01, 0x02, 0x02, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01}},  // VCOM
    {0x21, {0x01, 0x02, 0x02, 0x41, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01}},  // WW
    {0x22, {0x01, 0x02, 0x82, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01}},  // BW (dark gray)
    {0x23, {0x01, 0x02, 0x82, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01}},  // WB (dark gray)
    {0x24, {0x01, 0x02, 0x02, 0x81, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01}},  // BB
};
constexpr GrayLut kXtfAa68[5] = {
    {0x20, {0x01, 0x02, 0x03, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01}},  // VCOM
    {0x21, {0x01, 0x02, 0x03, 0x41, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01}},  // WW
    {0x22, {0x01, 0x02, 0x83, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01}},  // BW (dark gray)
    {0x23, {0x01, 0x02, 0x83, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01}},  // WB (dark gray)
    {0x24, {0x01, 0x02, 0x03, 0x81, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01}},  // BB
};

// UC8279_aa_prebw_mid — the stock non-flashing previous->current transition
// waveform (Factory.bin DROM 0x3c5d826f, uploaded by FUN_4214d3a0). 5 tables,
// command-prefixed, 42 data bytes each (distinct from the 49-byte display bank).
// Same layout as the UC8179 sibling's kGrayPreBwMid.
constexpr uint8_t PREBW_LUT_LEN = 42;
const uint8_t kXtfPreBwMid[5][PREBW_LUT_LEN + 1] = {
    {0x20, 0x01, 0x06, 0x01, 0x06, 0x06, 0x01, 0x01, 0x01, 0x02, 0x04, 0x00, 0x00, 0x01, 0x01},
    {0x21, 0x01, 0x06, 0x81, 0x06, 0x06, 0x01, 0x01, 0x01, 0x02, 0x04, 0x00, 0x00, 0x01, 0x01},
    {0x22, 0x01, 0x86, 0x81, 0x86, 0x86, 0x01, 0x01, 0x01, 0x82, 0x84, 0x00, 0x00, 0x01, 0x01},
    {0x23, 0x01, 0x46, 0x41, 0x46, 0x46, 0x01, 0x01, 0x01, 0x42, 0x44, 0x00, 0x00, 0x01, 0x01},
    {0x24, 0x01, 0x06, 0x01, 0x06, 0x06, 0x01, 0x01, 0x01, 0x02, 0x44, 0x00, 0x00, 0x01, 0x01},
    // remaining bytes of each 42-byte table are zero (aggregate init).
};

const GrayLut* selectAaLuts() {
  // LUT_VER stored by the boot probe. Stock's panel LUT registry (X4 Pro
  // 260917 build, table @0x3c1adbd4 + hardcoded bank selects in its UC8279
  // refresh) keys 0x02/0x03 to the QY bank and 0x68/0x69 to the ZHX bank;
  // anything else unknown keeps the existing fallback to the ZHX bytes. 0x67
  // never reaches this: grayscaleCapabilities() reports unsupported for it
  // (stock ships no external-LUT tables for that id).
  const uint8_t v = BoardConfig::ACTIVE.displayControllerVariant;
  return (v == 0x02 || v == 0x03) ? kXtfAa02 : kXtfAa68;
}

// The QY factory blob appears to contain a nine-byte prefix, five 49-byte
// waveform records, and a 36-byte 0xFF trailer. The prefix's command mapping
// has not been recovered; this payload split needs physical-panel validation.
// ZHX exposes five 42-byte records directly. Keep the layouts explicit rather
// than silently interpreting an X3 waveform as X4 glass.
constexpr uint16_t QY_XTH4_PREFIX_LEN = 9;
constexpr uint8_t QY_XTH4_LUT_LEN = 49;
constexpr uint8_t ZHX_XTH4_LUT_LEN = 42;
static_assert(QY_XTH4_PREFIX_LEN + 5 * QY_XTH4_LUT_LEN + 36 == sizeof(kQyUc8279Xth4),
              "unexpected QY UC8279 XTH4 blob layout");

using Step = Uc8279X4RefreshStep;
constexpr Step kBwFullSteps[] = {
    Step::UploadNewTarget, Step::UploadOldWhite, Step::ConfigureBwFull, Step::PowerOn,
    Step::SelectOtp, Step::Activate, Step::AsyncBoundary, Step::WaitRefresh,
    Step::RestoreOldTarget, Step::PowerOffIfRequested,
};
constexpr Step kBwScrubSteps[] = {
    Step::UploadNewTarget, Step::UploadOldComplementTarget, Step::ConfigureBwFull, Step::PowerOn,
    Step::SelectOtp, Step::Activate, Step::AsyncBoundary, Step::WaitRefresh,
    Step::RestoreOldTarget, Step::PowerOffIfRequested,
};
constexpr Step kBwFastSteps[] = {
    Step::UploadNewTarget, Step::ConfigureBwFast, Step::PowerOn, Step::EnterFullWindow,
    Step::SelectOtp, Step::Activate, Step::AsyncBoundary, Step::WaitRefresh,
    Step::ExitWindow, Step::RestoreOldTarget, Step::PowerOffIfRequested,
};
constexpr Step kBwRedriveSteps[] = {
    Step::UploadNewTarget, Step::UploadOldComplementTarget, Step::ConfigureBwFast, Step::PowerOn,
    Step::EnterFullWindow, Step::SelectOtp, Step::Activate, Step::AsyncBoundary,
    Step::WaitRefresh, Step::ExitWindow, Step::RestoreOldTarget, Step::PowerOffIfRequested,
};
constexpr Step kGrayPreconditionSteps[] = {
    Step::UploadNewTarget, Step::EnterGrayWindow, Step::ConfigureGrayPrecondition, Step::PowerOn,
    Step::SelectExternal, Step::Activate, Step::WaitRefresh, Step::ExitWindow, Step::RestoreOldTarget,
    Step::PowerOffIfRequested,
};
constexpr Step kGrayOverlaySteps[] = {
    Step::UploadGrayPlanes, Step::ConfigureGrayActivation, Step::PowerOn, Step::EnterGrayWindow,
    Step::SelectExternal, Step::Activate, Step::WaitRefresh, Step::ExitWindow,
    Step::RestoreBothBaseline, Step::PowerOffIfRequested,
};
constexpr Step kGrayAbsoluteSteps[] = {
    Step::UploadGrayPlanes, Step::ConfigureGrayActivation, Step::PowerOn, Step::EnterGrayWindow,
    Step::SelectExternal, Step::Activate, Step::WaitRefresh, Step::ExitWindow,
    Step::RestoreBothBaseline, Step::PowerOffIfRequested,
};
constexpr Step kGrayDirectSteps[] = {
    Step::UploadGrayPlanes, Step::ConfigureGrayActivation, Step::PowerOn, Step::EnterGrayWindow,
    Step::SelectExternal, Step::Activate, Step::WaitRefresh, Step::ExitWindow,
    Step::PowerOffIfRequested,
};

template<size_t N>
constexpr Uc8279X4RefreshPipeline pipeline(const char* name, const Step (&steps)[N]) {
  return {name, steps, static_cast<uint8_t>(N)};
}

}  // namespace

const Uc8279X4RefreshPipelines& uc8279X4DefaultPipelines() {
  static constexpr Uc8279X4RefreshPipelines pipelines = {
      pipeline("bw_full", kBwFullSteps),
      pipeline("bw_scrub", kBwScrubSteps),
      pipeline("bw_fast", kBwFastSteps),
      pipeline("bw_fast_over_gray", kBwFastSteps),
      pipeline("bw_redrive", kBwRedriveSteps),
      pipeline("gray_precondition", kGrayPreconditionSteps),
      pipeline("gray_overlay", kGrayOverlaySteps),
      pipeline("gray_absolute", kGrayAbsoluteSteps),
      pipeline("gray_direct", kGrayDirectSteps),
  };
  return pipelines;
}

const Uc8279X4Config& uc8279X4DefaultConfig() {
  static const Uc8279X4Config cfg = {
      0x37,  // psr0: stock X4C UC8279 vtable value, SHL set (rows forward, no RAM
             // mirroring and writes PSR between PON and DRF — PON reloads MTP
             // settings, so only post-PON PSR writes latch). REG=1 (external LUT)
             // at init and for AA; built-in refreshes re-assert psr0 & 0xDF = 0x17
      0x4D,  // psr1
      0x20,  // pfs (0x03)
      0x0E,  // pll (0x30) — X4 Pro only; stock X4C omits this command
      0x02,  // gateScan (0xE1)
      0x02,  // ccset (0xE0)
      0x1E,  // tsset (0xE5) full refresh
      0x5A,  // tssetFast (0xE5) fast/partial refresh
      0x97,  // cdiAa (0x50, 1 byte): constant on EVERY AA refresh, per stock
      0x97,  // cdiBwFull (0x50): stock writes it before every GC/full refresh
      0xD7,  // cdiBwFast (0x50): stock value for the windowed partial
      600,   // tresHeight — addressed 800x600 (480 visible)
      120,   // gateOffset — visible gates start at 120 on this variant
  };
  return cfg;
}

// Visible geometry comes from the active BoardProfile (X4 Pro, 800x480).
Uc8279X4Driver::Uc8279X4Driver(const Uc8279X4Config& cfg, const Uc8279X4RefreshPipelines& pipelines)
    : _cfg(cfg),
      _pipelines(pipelines),
      _w(BoardConfig::ACTIVE.displayWidth),
      _h(BoardConfig::ACTIVE.displayHeight),
      _wb(BoardConfig::ACTIVE.displayWidth / 8),
      _tresH(cfg.tresHeight),
      _bufferSize(static_cast<uint32_t>(BoardConfig::ACTIVE.displayWidth / 8) * BoardConfig::ACTIVE.displayHeight) {}

Uc8279X4Driver::~Uc8279X4Driver() {
  free(_bwBaseline);
  free(_bwTarget);
  free(_grayLsb);
  free(_grayMsb);
  free(_panelGrayLsb);
  free(_panelGrayMsb);
}

uint32_t Uc8279X4Driver::spiHz() const {
  // UC8279 serial write timing is rated to 20 MHz, same as the rest of the family.
  return BoardConfig::ACTIVE.displaySpiHz != 0 ? BoardConfig::ACTIVE.displaySpiHz : 16000000;
}

PanelGeometry Uc8279X4Driver::geometry() const { return {_w, _h, _wb, _bufferSize}; }

// Vendor runtime init: PSR, TRES (800x600), GSST (4x0), PFS, PLL, gate scan.
// PWR/VDCS/BTST stay panel-programmed (OTP/MTP) and are not written here.
void Uc8279X4Driver::initController(EpdBus& bus) {
  bus.cmd(CMD_PANEL_SETTING);
  bus.data(_cfg.psr0);
  bus.data(_cfg.psr1);

  bus.cmd(CMD_RESOLUTION);
  bus.data(static_cast<uint8_t>((_w >> 8) & 0xFF));
  bus.data(static_cast<uint8_t>(_w & 0xFF));
  bus.data(static_cast<uint8_t>((_tresH >> 8) & 0xFF));
  bus.data(static_cast<uint8_t>(_tresH & 0xFF));

  bus.cmd(CMD_GATE_SOURCE_START);
  bus.data(0x00);
  bus.data(0x00);
  bus.data(0x00);
  bus.data(0x00);

  bus.cmd(CMD_PFS);
  bus.data(_cfg.pfs);

  // The stock X4C UC8279 vtable's PLL hook is a no-op. X4 Pro programs PLL.
  if (!BoardConfig::isX4Classic()) {
    bus.cmd(CMD_PLL);
    bus.data(_cfg.pll);
  }

  bus.cmd(CMD_GATE_SCAN);
  bus.data(_cfg.gateScan);
}

void Uc8279X4Driver::resetRuntimeState(bool retained) {
  _cursor = {};
  _grayJob = {};
  _window = {};
  _panelGrayValid = false;
  if (retained)
    _state.retained_boot();
  else
    _state.cold_boot();
}

bool Uc8279X4Driver::ensureHostBuffers() {
  const auto allocate = [this](uint8_t*& buffer) {
    if (buffer != nullptr) return true;
    buffer = static_cast<uint8_t*>(heap_caps_malloc(_bufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) buffer = static_cast<uint8_t*>(malloc(_bufferSize));
    return buffer != nullptr;
  };
  const bool ok = allocate(_bwBaseline) && allocate(_bwTarget) && allocate(_grayLsb) && allocate(_grayMsb) &&
                  allocate(_panelGrayLsb) && allocate(_panelGrayMsb);
  if (!ok) {
    free(_bwBaseline);
    free(_bwTarget);
    free(_grayLsb);
    free(_grayMsb);
    free(_panelGrayLsb);
    free(_panelGrayMsb);
    _bwBaseline = _bwTarget = _grayLsb = _grayMsb = _panelGrayLsb = _panelGrayMsb = nullptr;
  }
  return ok;
}

void Uc8279X4Driver::begin(EpdBus& bus) {
  resetRuntimeState(false);
  bus.reset(50);
  initController(bus);
  ensureHostBuffers();
}

void Uc8279X4Driver::beginRetained(EpdBus& bus) {
  (void)bus;
  // The controller may still retain RAM, but a fresh host object cannot know
  // which logical generations those planes contain. Keep that uncertainty
  // explicit until seedPreviousFrame() establishes an authoritative baseline.
  resetRuntimeState(true);
  ensureHostBuffers();
}

void Uc8279X4Driver::display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) {
  displayStart(bus, fb, prev, mode, turnOff);
  displayFinish(bus, fb);
}

void Uc8279X4Driver::streamPlane(EpdBus& bus, uint8_t ramCmd, const uint8_t* fb, bool invert) {
  uint8_t row[128];
  const uint16_t wb = _wb <= sizeof(row) ? _wb : sizeof(row);
  bus.cmd(ramCmd);
  // The OEM driver keeps CS asserted across a plane upload. Preserve our validated
  // gate offset/orientation, but avoid a new SPI transaction for every row.
  bus.beginTxn();
  // Gates before the visible window (the 120-gate offset): white.
  memset(row, 0xFF, wb);
  for (uint16_t y = 0; y < _cfg.gateOffset; y++) bus.rawWriteBytes(row, wb);
  // Visible rows, stock convention (hardware-confirmed upright): forward order,
  // bytes as-is. The ROWREV/XMIRROR switches (row reversal / reversed byte order
  // + reversed bits) exist for future panel sub-variants whose scan differs.
  // AA planes are sent bitwise-inverted per the vendor reference.
  static const uint8_t kBitRev[16] = {0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE, 0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF};
  for (uint16_t n = 0; n < _h; n++) {
    const uint16_t y = FREEINK_UC8279X4_ROWREV ? static_cast<uint16_t>(_h - 1 - n) : n;
    const uint8_t* src = fb + static_cast<uint32_t>(y) * _wb;
    for (uint16_t i = 0; i < wb; i++) {
      uint8_t b;
      if (FREEINK_UC8279X4_XMIRROR) {
        const uint8_t m = src[wb - 1 - i];
        b = static_cast<uint8_t>((kBitRev[m & 0x0F] << 4) | kBitRev[m >> 4]);
      } else {
        b = src[i];
      }
      row[i] = invert ? static_cast<uint8_t>(~b) : b;
    }
    bus.rawWriteBytes(row, wb);
  }
  // Gates after the visible window: white, up to the addressed gate count.
  memset(row, 0xFF, wb);
  for (uint16_t y = _cfg.gateOffset + _h; y < _tresH; y++) bus.rawWriteBytes(row, wb);
  bus.endTxn();
}

void Uc8279X4Driver::powerOnIfNeeded(EpdBus& bus, const char* tag) {
  if (_state.snapshot().lifecycle == Uc8279X4RefreshState::Lifecycle::ReadyOn) return;
  bus.cmd(CMD_POWER_ON);
  bus.waitBusy(tag);
  _state.mark_power_on();
}

void Uc8279X4Driver::configureBwRefresh(EpdBus& bus, bool fast) {
  bus.cmd(CMD_VCOM_DATA_INTERVAL);
  bus.data(fast ? _cfg.cdiBwFast : _cfg.cdiBwFull);
  bus.cmd(CMD_CCSET);
  bus.data(_cfg.ccset);
  bus.cmd(CMD_TSSET);
  bus.data(fast ? _cfg.tssetFast : _cfg.tsset);
  if (fast) {
    bus.cmd(CMD_PFS);
    bus.data(_cfg.pfs);
    bus.cmd(CMD_GATE_SCAN);
    bus.data(_cfg.gateScan);
  }
}

void Uc8279X4Driver::configureGrayPrecondition(EpdBus& bus) {
  bus.cmd(CMD_PANEL_SETTING);
  bus.data(_cfg.psr0);
  bus.data(_cfg.psr1);
  bus.cmd(CMD_PFS);
  bus.data(_cfg.pfs);
  bus.cmd(CMD_GATE_SCAN);
  bus.data(_cfg.gateScan);
  bus.cmd(CMD_VCOM_DATA_INTERVAL);
  bus.data(_cfg.cdiBwFast);
  bus.cmd(CMD_CCSET);
  bus.data(_cfg.ccset);
  bus.cmd(CMD_TSSET);
  bus.data(_cfg.tssetFast);
  for (const auto& lut : kXtfPreBwMid) {
    bus.cmd(lut[0]);
    bus.data(&lut[1], PREBW_LUT_LEN);
  }
}

void Uc8279X4Driver::configureGrayActivation(EpdBus& bus) {
  bus.cmd(CMD_PANEL_SETTING);
  bus.data(_cfg.psr0);
  bus.data(_cfg.psr1);
  if (_grayJob.imageQuality) {
    const uint8_t variant = BoardConfig::ACTIVE.displayControllerVariant;
    const bool qyPanel = variant == 0x02 || variant == 0x03;
    for (uint8_t i = 0; i < 5; i++) {
      bus.cmd(static_cast<uint8_t>(0x20 + i));
      if (qyPanel)
        bus.data(&kQyUc8279Xth4[QY_XTH4_PREFIX_LEN + static_cast<uint16_t>(i) * QY_XTH4_LUT_LEN],
                 QY_XTH4_LUT_LEN);
      else
        bus.data(kZhxUc8279Xth4Regs[i], ZHX_XTH4_LUT_LEN);
    }
  } else {
    for (uint8_t i = 0; i < 5; i++) {
      const auto& lut = selectAaLuts()[i];
      bus.cmd(lut.cmd);
      bus.data(lut.data, GRAY_LUT_LEN);
    }
  }
  bus.cmd(CMD_VCOM_DATA_INTERVAL);
  bus.data(_cfg.cdiAa);
}

uint32_t Uc8279X4Driver::nextGeneration() {
  ++_generation;
  if (_generation == 0) ++_generation;
  return _generation;
}

const Uc8279X4RefreshPipeline& Uc8279X4Driver::pipelineFor(Uc8279X4RefreshState::Pipeline pipeline) const {
  using Pipeline = Uc8279X4RefreshState::Pipeline;
  switch (pipeline) {
    case Pipeline::BwFull: return _pipelines.bwFull;
    case Pipeline::BwScrub: return _pipelines.bwScrub;
    case Pipeline::BwFast: return _pipelines.bwFast;
    case Pipeline::BwFastOverGray: return _pipelines.bwFastOverGray;
    case Pipeline::BwRedrive: return _pipelines.bwRedrive;
    case Pipeline::GrayPrecondition: return _pipelines.grayPrecondition;
    case Pipeline::GrayOverlay: return _pipelines.grayOverlay;
    case Pipeline::GrayAbsolute: return _pipelines.grayAbsolute;
    case Pipeline::GrayDirect: return _pipelines.grayDirect;
    case Pipeline::None: break;
  }
  return _pipelines.bwFull;
}

bool Uc8279X4Driver::executeStep(EpdBus& bus, Uc8279X4RefreshStep step) {
  using Pipeline = Uc8279X4RefreshState::Pipeline;
  switch (step) {
    case Step::UploadNewTarget:
      if (_bwTarget == nullptr) return false;
      if (_state.snapshot().pending == Pipeline::GrayPrecondition) expandWindowForBwDelta();
      streamPlane(bus, CMD_DTM2, _bwTarget);
      return _state.mark_new_target_uploaded();
    case Step::UploadOldWhite:
      bus.fillPlane(CMD_DTM1, 0xFF, _tresH, _wb);
      return _state.mark_old_white_uploaded();
    case Step::UploadOldComplementTarget:
      if (_bwTarget == nullptr) return false;
      streamPlane(bus, CMD_DTM1, _bwTarget, true);
      return _state.mark_old_complement_uploaded();
    case Step::UploadGrayPlanes: {
      if (_grayLsb == nullptr || _grayMsb == nullptr) return false;
      const Pipeline active = _state.snapshot().pending;
      if (active == Pipeline::GrayOverlay) {
        if (_bwBaseline == nullptr || !_state.snapshot().baseline_staged) return false;
        for (uint32_t i = 0; i < _bufferSize; i++) {
          const uint8_t plane0 = static_cast<uint8_t>(_bwBaseline[i] | _grayLsb[i]);
          _grayLsb[i] = plane0;
          _grayMsb[i] = static_cast<uint8_t>(plane0 ^ _grayMsb[i]);
        }
      }
      expandWindowForGrayDelta();
      streamPlane(bus, CMD_DTM1, _grayLsb, true);
      streamPlane(bus, CMD_DTM2, _grayMsb, true);
      return _state.mark_gray_planes_in_controller();
    }
    case Step::ConfigureBwFull:
      configureBwRefresh(bus, false);
      return true;
    case Step::ConfigureBwFast:
      configureBwRefresh(bus, true);
      return true;
    case Step::ConfigureGrayPrecondition:
      configureGrayPrecondition(bus);
      return true;
    case Step::ConfigureGrayActivation:
      configureGrayActivation(bus);
      return true;
    case Step::PowerOn:
      powerOnIfNeeded(bus, " 8279x4_PON");
      return true;
    case Step::EnterFullWindow:
      enterFullWindow(bus);
      return true;
    case Step::EnterGrayWindow:
      enterGrayscaleWindow(bus);
      return true;
    case Step::SelectOtp:
      bus.cmd(CMD_PANEL_SETTING);
      bus.data(static_cast<uint8_t>(_cfg.psr0 & 0xDF));
      bus.data(_cfg.psr1);
      return true;
    case Step::SelectExternal:
      bus.cmd(CMD_PANEL_SETTING);
      bus.data(_cfg.psr0);
      bus.data(_cfg.psr1);
      return true;
    case Step::Activate: {
      bus.cmd(CMD_DISPLAY_REFRESH);
      const int8_t busyPin = bus.pins().busy;
      const unsigned long started = millis();
      while (digitalRead(busyPin) == HIGH && millis() - started < 50) delay(1);
      return true;
    }
    case Step::AsyncBoundary:
      return true;
    case Step::WaitRefresh:
      bus.waitRefreshComplete(" 8279x4_DRF");
      return true;
    case Step::ExitWindow:
      if (_window.entered) {
        bus.cmd(CMD_PARTIAL_OUT);
        _window.entered = false;
      }
      return true;
    case Step::RestoreOldTarget:
      if (_bwTarget == nullptr) return false;
      streamPlane(bus, CMD_DTM1, _bwTarget);
      return _state.mark_old_target_restored();
    case Step::RestoreBothBaseline:
      if (_bwBaseline == nullptr || !_state.snapshot().baseline_staged) return false;
      streamPlane(bus, CMD_DTM1, _bwBaseline);
      streamPlane(bus, CMD_DTM2, _bwBaseline);
      return _state.mark_both_baseline_restored();
    case Step::PowerOffIfRequested:
      if (_cursor.turnOff && _state.snapshot().lifecycle == Uc8279X4RefreshState::Lifecycle::ReadyOn) {
        bus.cmd(CMD_POWER_OFF);
        waitPowerOffBounded(bus);
        _state.mark_power_off();
      }
      return true;
  }
  return false;
}

bool Uc8279X4Driver::runPipeline(EpdBus& bus, const Uc8279X4RefreshPipeline& pipeline, bool resume) {
  if (!resume) {
    if (_cursor.pipeline != nullptr) return false;
    _cursor.pipeline = &pipeline;
    _cursor.nextStep = 0;
  } else if (_cursor.pipeline != &pipeline) {
    return false;
  }

  while (_cursor.nextStep < pipeline.stepCount) {
    const Step step = pipeline.steps[_cursor.nextStep++];
    if (step == Step::AsyncBoundary && !resume) return true;
    if (!executeStep(bus, step)) {
      _state.cancel();
      _cursor = {};
      return false;
    }
  }
  _cursor = {};
  return false;
}

bool Uc8279X4Driver::displayStart(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode,
                                  bool turnOff) {
  (void)prev;
  if (fb == nullptr || !ensureHostBuffers() || _cursor.pipeline != nullptr) return false;
  memcpy(_bwTarget, fb, _bufferSize);

  using Pipeline = Uc8279X4RefreshState::Pipeline;
  Pipeline selected = mode == RefreshMode::Half ? Pipeline::BwScrub : Pipeline::BwFull;
  if (mode == RefreshMode::ConditioningRedrive) {
    selected = Pipeline::BwRedrive;
  } else if (mode == RefreshMode::Fast) {
    const auto& state = _state.snapshot();
    if (state.preserve_gray_for_next_bw)
      selected = Pipeline::BwFastOverGray;
    else if (state.bw_redrive_required)
      selected = Pipeline::BwRedrive;
    else
      selected = Pipeline::BwFast;
  }

  const uint32_t generation = nextGeneration();
  if (!_state.begin_bw(selected, generation)) {
    selected = mode == RefreshMode::Half ? Pipeline::BwScrub : Pipeline::BwFull;
    if (!_state.begin_bw(selected, generation)) return false;
  }

  _cursor.turnOff = turnOff;
  const bool deferred = runPipeline(bus, pipelineFor(selected), false);
  if (!deferred && _state.snapshot().pending == selected && _state.complete_bw()) {
    memcpy(_bwBaseline, _bwTarget, _bufferSize);
    if (selected != Pipeline::BwFastOverGray) _panelGrayValid = false;
  }
  return deferred;
}

void Uc8279X4Driver::displayFinish(EpdBus& bus, const uint8_t* fb) {
  (void)fb;
  if (_cursor.pipeline == nullptr) return;
  const auto pending = _state.snapshot().pending;
  const auto& pipeline = pipelineFor(pending);
  runPipeline(bus, pipeline, true);
  if (_state.snapshot().pending == pending && _state.complete_bw() && _bwBaseline != nullptr && _bwTarget != nullptr) {
    memcpy(_bwBaseline, _bwTarget, _bufferSize);
    if (pending != Uc8279X4RefreshState::Pipeline::BwFastOverGray) _panelGrayValid = false;
  }
}

void Uc8279X4Driver::seedPreviousFrame(EpdBus& bus, const uint8_t* buf) {
  if (buf == nullptr || !ensureHostBuffers() || _state.snapshot().pending != Uc8279X4RefreshState::Pipeline::None)
    return;
  memcpy(_bwBaseline, buf, _bufferSize);
  streamPlane(bus, CMD_DTM1, _bwBaseline);
  streamPlane(bus, CMD_DTM2, _bwBaseline);
  _state.adopt_retained_monochrome(nextGeneration());
  _panelGrayValid = false;
}

void Uc8279X4Driver::requestResync(uint8_t settlePasses) {
  (void)settlePasses;
  _cursor = {};
  _grayJob = {};
  _window = {};
  _panelGrayValid = false;
  _state.force_resync();
}

void Uc8279X4Driver::skipInitialResync() {
  // Retained RAM is not self-describing. Without an explicit frame passed to
  // seedPreviousFrame(), do not guess that DTM1/DTM2 match the visible panel.
}

void Uc8279X4Driver::deepSleep(EpdBus& bus) {
  if (_state.snapshot().lifecycle == Uc8279X4RefreshState::Lifecycle::ReadyOn) {
    bus.cmd(CMD_POWER_OFF);
    waitPowerOffBounded(bus);
    _state.mark_power_off();
  }
  bus.cmd(CMD_DEEP_SLEEP);
  bus.data(0xA5);
  _cursor = {};
  _window = {};
  _panelGrayValid = false;
  _state.deep_sleep();
}

void Uc8279X4Driver::powerOffRetainingState(EpdBus& bus) {
  if (_state.snapshot().lifecycle == Uc8279X4RefreshState::Lifecycle::ReadyOn) {
    bus.cmd(CMD_POWER_OFF);
    waitPowerOffBounded(bus);
    _state.mark_power_off();
  }
}

// --- 4-level grayscale (anti-aliasing) --------------------------------------
// Absolute-plane scheme ported from the UC8179 sibling (see the header). The
// delta masks are folded into stock's absolute selectors and sent INVERTED, so
// black=(0,0) and white=(1,1) are distinct buckets (the earlier raw-delta path
// conflated them → white-text ghosting). plane0/LSB -> DTM1 (0x10),
// plane1/MSB -> DTM2 (0x13).
GrayscaleCapabilities Uc8279X4Driver::grayscaleCapabilities(GrayscaleMode mode) const {
  // LUT_VER 0x67: stock's panel LUT registry (X4 Pro 260917 build) recognizes
  // the id but carries no external-LUT tables for it and excludes it from the
  // ZHX fallback — that panel refreshes from OTP only, with no grayscale path.
  // Report unsupported so callers render B/W instead of driving it with
  // another vendor's waveform.
  if (BoardConfig::ACTIVE.displayControllerVariant == 0x67) return {};
  if (mode == GrayscaleMode::Absolute || mode == GrayscaleMode::Direct)
    return {GrayscaleEncoding::AbsolutePlanes,
            mode == GrayscaleMode::Direct ? GrayscaleBase::Combined : GrayscaleBase::Separate, false, false, false};
  if (mode != GrayscaleMode::Overlay) return {};
  return {GrayscaleEncoding::OverlayMasks, GrayscaleBase::Separate, false, false, false};
}

void Uc8279X4Driver::setGrayscaleWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  if (w == 0 || h == 0 || x >= _w || y >= _h) {
    _window = {};
    return;
  }
  _window.x = x;
  _window.y = y;
  _window.w = w > _w - x ? static_cast<uint16_t>(_w - x) : w;
  _window.h = h > _h - y ? static_cast<uint16_t>(_h - y) : h;
  _window.enabled = true;
}

void Uc8279X4Driver::includeChangedByte(uint16_t byteX, uint16_t y) {
  const uint16_t changedX0 = static_cast<uint16_t>(byteX * 8U);
  const uint16_t changedX1 = static_cast<uint16_t>(changedX0 + 7U < _w ? changedX0 + 7U : _w - 1U);
  const uint16_t currentX1 = static_cast<uint16_t>(_window.x + _window.w - 1U);
  const uint16_t currentY1 = static_cast<uint16_t>(_window.y + _window.h - 1U);
  const uint16_t x0 = changedX0 < _window.x ? changedX0 : _window.x;
  const uint16_t y0 = y < _window.y ? y : _window.y;
  const uint16_t x1 = changedX1 > currentX1 ? changedX1 : currentX1;
  const uint16_t y1 = y > currentY1 ? y : currentY1;
  _window.x = x0;
  _window.y = y0;
  _window.w = static_cast<uint16_t>(x1 - x0 + 1U);
  _window.h = static_cast<uint16_t>(y1 - y0 + 1U);
}

void Uc8279X4Driver::expandWindowForBwDelta() {
  if (!_window.enabled) return;
  if (_bwBaseline == nullptr || _bwTarget == nullptr || !_state.snapshot().baseline_staged) {
    _window.enabled = false;
    return;
  }
  for (uint16_t y = 0; y < _h; ++y) {
    const uint32_t row = static_cast<uint32_t>(y) * _wb;
    for (uint16_t x = 0; x < _wb; ++x)
      if (_bwBaseline[row + x] != _bwTarget[row + x]) includeChangedByte(x, y);
  }
}

void Uc8279X4Driver::expandWindowForGrayDelta() {
  if (!_window.enabled) return;
  const auto& state = _state.snapshot();
  const bool compareToMonochrome = !_panelGrayValid && state.panel.content == Uc8279X4RefreshState::PanelContent::Monochrome &&
                                   state.baseline_staged && _bwBaseline != nullptr;
  if (!_panelGrayValid && !compareToMonochrome) {
    _window.enabled = false;
    return;
  }
  for (uint16_t y = 0; y < _h; ++y) {
    const uint32_t row = static_cast<uint32_t>(y) * _wb;
    for (uint16_t x = 0; x < _wb; ++x) {
      const uint32_t index = row + x;
      const uint8_t oldLsb = compareToMonochrome ? _bwBaseline[index] : _panelGrayLsb[index];
      const uint8_t oldMsb = compareToMonochrome ? _bwBaseline[index] : _panelGrayMsb[index];
      if (oldLsb != _grayLsb[index] || oldMsb != _grayMsb[index]) includeChangedByte(x, y);
    }
  }
}

void Uc8279X4Driver::enterFullWindow(EpdBus& bus) {
  const bool saved = _window.enabled;
  _window.enabled = false;
  enterGrayscaleWindow(bus);
  _window.enabled = saved;
}

void Uc8279X4Driver::enterGrayscaleWindow(EpdBus& bus) {
  uint16_t x0 = 0;
  uint16_t y0 = 0;
  uint16_t x1 = static_cast<uint16_t>(_w - 1);
  uint16_t y1 = static_cast<uint16_t>(_h - 1);
  if (_window.enabled) {
    x0 = _window.x;
    y0 = _window.y;
    x1 = static_cast<uint16_t>(_window.x + _window.w - 1);
    y1 = static_cast<uint16_t>(_window.y + _window.h - 1);
  }
  if (FREEINK_UC8279X4_XMIRROR) {
    const uint16_t mirrored0 = static_cast<uint16_t>(_w - 1 - x1);
    x1 = static_cast<uint16_t>(_w - 1 - x0);
    x0 = mirrored0;
  }
  if (FREEINK_UC8279X4_ROWREV) {
    const uint16_t reversed0 = static_cast<uint16_t>(_h - 1 - y1);
    y1 = static_cast<uint16_t>(_h - 1 - y0);
    y0 = reversed0;
  }
  x0 = static_cast<uint16_t>(x0 & ~0x07U);
  x1 = static_cast<uint16_t>(x1 | 0x07U);
  if (x1 >= _w)
    x1 = static_cast<uint16_t>(_w - 1);
  y0 = static_cast<uint16_t>(y0 + _cfg.gateOffset);
  y1 = static_cast<uint16_t>(y1 + _cfg.gateOffset);

  bus.cmd(CMD_PARTIAL_IN);
  bus.cmd(CMD_PARTIAL_WINDOW);
  bus.data(static_cast<uint8_t>(x0 >> 8));
  bus.data(static_cast<uint8_t>(x0 & 0xFF));
  bus.data(static_cast<uint8_t>(x1 >> 8));
  bus.data(static_cast<uint8_t>(x1 & 0xFF));
  bus.data(static_cast<uint8_t>(y0 >> 8));
  bus.data(static_cast<uint8_t>(y0 & 0xFF));
  bus.data(static_cast<uint8_t>(y1 >> 8));
  bus.data(static_cast<uint8_t>(y1 & 0xFF));
  bus.data(0x01);
  _window.entered = true;
}

void Uc8279X4Driver::beginGrayscale(EpdBus& bus, const uint8_t* fb, GrayscaleMode mode, RefreshMode fallback,
                                    bool turnOff) {
  if (!ensureHostBuffers() || _state.snapshot().pending != Uc8279X4RefreshState::Pipeline::None) return;
  _grayJob = {};
  _grayJob.mode = mode;
  using Pipeline = Uc8279X4RefreshState::Pipeline;
  if (mode == GrayscaleMode::Direct) {
    _state.begin_gray_upload(Pipeline::GrayDirect, nextGeneration());
    return;
  }
  if (fb == nullptr) return;
  displayGrayscaleBase(bus, fb, fallback, turnOff);
  const uint32_t generation = _state.snapshot().baseline_generation;
  _state.begin_gray_upload(mode == GrayscaleMode::Absolute ? Pipeline::GrayAbsolute : Pipeline::GrayOverlay,
                           generation);
}

void Uc8279X4Driver::copyGrayscaleLsb(EpdBus& bus, const uint8_t* lsb) {
  (void)bus;
  if (lsb == nullptr || _grayLsb == nullptr || !_state.stage_gray_lsb()) return;
  memcpy(_grayLsb, lsb, _bufferSize);
}

// Grey-mask coverage (percent of panel pixels) above which a gray pass is
// treated as an image and refreshed with the vendor's four-tone XTH4 bank.
// Deliberately high: only image-DOMINANT surfaces (sleep screens, standalone
// image pages, full covers — typically 30%+) should cross it. Glyph AA marks
// 1-2%; book pages with modest inline images stay under it and keep stock AA.
#ifndef FREEINK_UC8279X4_QUALITY_COVERAGE_PCT
#define FREEINK_UC8279X4_QUALITY_COVERAGE_PCT 25
#endif

void Uc8279X4Driver::copyGrayscaleMsb(EpdBus& bus, const uint8_t* msb) {
  (void)bus;
  if (msb == nullptr || _grayMsb == nullptr || !_state.stage_gray_msb()) return;
  memcpy(_grayMsb, msb, _bufferSize);
  uint32_t grayPixels = 0;
  for (uint32_t i = 0; i < _bufferSize; i++) grayPixels += __builtin_popcount(msb[i]);
  _grayJob.imageQuality = grayPixels * 100u > _bufferSize * 8u * FREEINK_UC8279X4_QUALITY_COVERAGE_PCT;
}

void Uc8279X4Driver::displayGray(EpdBus& bus, const uint8_t* fb, bool turnOff, const unsigned char* lut,
                                 bool factoryMode) {
  (void)fb;
  (void)lut;
  const auto pending = _state.snapshot().pending;
  if (_state.snapshot().gray_upload != Uc8279X4RefreshState::GrayUpload::Ready) return;
  _grayJob.imageQuality = _grayJob.imageQuality || factoryMode || _grayJob.mode != GrayscaleMode::Overlay;
  _cursor.turnOff = turnOff;
  runPipeline(bus, pipelineFor(pending), false);
  if (_state.snapshot().pending == pending && _state.complete_gray()) {
    memcpy(_panelGrayLsb, _grayLsb, _bufferSize);
    memcpy(_panelGrayMsb, _grayMsb, _bufferSize);
    _panelGrayValid = true;
  }
  _grayJob = {};
  _window = {};
}

void Uc8279X4Driver::displayGrayscaleBase(EpdBus& bus, const uint8_t* fb, RefreshMode fallback, bool turnOff) {
  if (!fb) return;
  const auto& state = _state.snapshot();
  if (fallback != RefreshMode::Fast || !state.gray_reference_established || state.strong_sync_required ||
      !state.baseline_staged) {
    display(bus, fb, nullptr, fallback, turnOff);
    return;
  }
  transitionGrayscaleBase(bus, fb, turnOff);
}

void Uc8279X4Driver::transitionGrayscaleBase(EpdBus& bus, const uint8_t* fb, bool turnOff) {
  if (fb == nullptr || !ensureHostBuffers()) return;
  memcpy(_bwTarget, fb, _bufferSize);
  const uint32_t generation = nextGeneration();
  if (!_state.begin_gray_precondition(generation)) {
    display(bus, fb, nullptr, RefreshMode::Full, turnOff);
    return;
  }
  _cursor.turnOff = turnOff;
  if (!runGrayscalePrecondition(bus)) return;
  if (_state.complete_gray_precondition()) memcpy(_bwBaseline, _bwTarget, _bufferSize);
}

// Stock's UC8279_aa_prebw_mid pre-conditioning pass (byte-exact order from
// Factory.bin FUN_4214d3a0): PTIN -> PTL(full window, +120 gate offset) ->
// PSR(REG=1) -> PFS -> gate scan -> CDI 0xD7 -> CCSET -> TSSET(fast) -> upload
// the 5 settle LUTs -> PON -> DRF -> PTOUT. Requires a valid previous page in
// DTM1 and the new base in DTM2.
bool Uc8279X4Driver::runGrayscalePrecondition(EpdBus& bus) {
  const auto pending = _state.snapshot().pending;
  runPipeline(bus, pipelineFor(pending), false);
  return _state.snapshot().pending == pending;
}

void Uc8279X4Driver::cleanupGrayscaleBuffers(EpdBus& bus, const uint8_t* bw) {
  _grayJob = {};
  _window = {};
  if (bw == nullptr || !ensureHostBuffers()) {
    _state.force_resync();
    _panelGrayValid = false;
    return;
  }
  const auto& state = _state.snapshot();
  const bool sameBaseline = state.baseline_staged && memcmp(_bwBaseline, bw, _bufferSize) == 0;
  if (sameBaseline && state.pending == Uc8279X4RefreshState::Pipeline::None &&
      state.dtm1.is_bw(state.baseline_generation) && state.dtm2.is_bw(state.baseline_generation))
    return;

  // An aborted upload or a changed caller baseline cannot be assigned the
  // previous generation. Seed both RAM planes, then demand a strong refresh.
  memcpy(_bwBaseline, bw, _bufferSize);
  streamPlane(bus, CMD_DTM1, _bwBaseline);
  streamPlane(bus, CMD_DTM2, _bwBaseline);
  _state.force_resync();
  _panelGrayValid = false;
}

void Uc8279X4Driver::preserveGrayscaleOnFastRefresh() {
  _state.preserve_gray_for_next_bw_delta();
}

// Per-board config injection, same idiom as the other drivers: define
// `const Uc8279X4Config& yourConfig();` in namespace freeink and build with
// -DFREEINK_UC8279_X4_CONFIG=yourConfig.
#ifdef FREEINK_UC8279_X4_CONFIG
const Uc8279X4Config& FREEINK_UC8279_X4_CONFIG();
static const Uc8279X4Config& uc8279X4ActiveConfig() { return FREEINK_UC8279_X4_CONFIG(); }
#else
static const Uc8279X4Config& uc8279X4ActiveConfig() {
  // Stock X4C screenType=2 uses the same 800x600/120-gate UC8279 configuration.
  return uc8279X4DefaultConfig();
}
#endif

PanelDriver& uc8279X4Driver() {
  static Uc8279X4Driver instance(uc8279X4ActiveConfig());
  return instance;
}

}  // namespace freeink

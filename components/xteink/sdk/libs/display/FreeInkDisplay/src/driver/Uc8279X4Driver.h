#pragma once

// UC8279 panel driver — Xteink X4 Pro production runs that ship an UltraChip
// UC8279 (800x480) in place of the SSD1677. NOT the X3's UC8279d (792x528,
// Uc8279Driver) — this variant has its own init (PSR 0x37/0x4D, stock-exact;
// PSR must be rewritten AFTER PON to latch), PLL 0x0E, PFS, a 1-byte CDI,
// a 120-gate offset on the 600-gate scan, and an external-LUT AA grayscale
// path with bitwise-INVERTED planes.
//
// Register sequences, waveform tables, and power ordering come from the Xteink
// X4 Pro 480x800 display hardware reference (vendor R&D doc). Identification:
// VER (0x70) byte2 LUT_VER = 0x02 or 0x68 (0x69 reserved — routed here too, but
// with no AA waveform of its own it uses the 0x68 table; built-in refreshes are
// identical). The boot probe stores that byte in
// BoardConfig::ACTIVE.displayControllerVariant. VALIDATED IN THE FIELD
// (2026-08-19, LUT_VER=0x02 unit): detection, GC full, DU partial (PTL window
// required — see displayStart), and orientation all confirmed on hardware.
//
// Same KW differential paradigm as the UC8179 sibling: DTM1 (0x10) = OLD plane,
// DTM2 (0x13) = NEW plane, OTP waveforms for B/W (PSR REG=0 at refresh),
// external 5x49 LUTs for AA grayscale (REG=1). BUSY_N is low while busy;
// production waits one tick and then polls until it returns HIGH.

#include "PanelDriver.h"
#include "Uc8279X4RefreshState.h"

namespace freeink {

struct Uc8279X4Config {
  // PSR (0x00) byte 0 as written at init and for the AA path (REG bit5=1,
  // external LUT). Built-in refreshes re-assert (psr0 & 0xDF) = REG=0 -> OTP.
  uint8_t psr0;
  // PSR (0x00) byte 1.
  uint8_t psr1;
  // PFS power-off sequence (cmd 0x03).
  uint8_t pfs;
  // PLL frame-rate (cmd 0x30) — unlike the UC8179, this variant's init programs it.
  uint8_t pll;
  // Gate-scan selection (cmd 0xE1).
  uint8_t gateScan;
  // CCSET cascade/output enable (cmd 0xE0), built-in refreshes only.
  uint8_t ccset;
  // TSSET forced temperature (cmd 0xE5) for a full refresh.
  uint8_t tsset;
  // TSSET (cmd 0xE5) for a fast/partial refresh.
  uint8_t tssetFast;
  // CDI (0x50) — SINGLE byte on this controller. Stock sends the SAME value on
  // every AA refresh (Factory.bin RE: both vtable CDI getters hard-return 0x97;
  // there is NO first/later split — an earlier split to 0xD7 on later refreshes
  // grayed the background, same class of bug as the UC8179 CDI regression).
  uint8_t cdiAa;
  // CDI for the built-in B/W paths — stock writes it on EVERY refresh (RE of
  // the factory FW trigger fns): full/GC and windowed-partial values.
  uint8_t cdiBwFull;
  uint8_t cdiBwFast;
  // TRES (0x61) gate count: the panel is addressed 800x600 with 480 visible.
  uint16_t tresHeight;
  // First visible gate: the UC8279 scans 600 gates with the bonded 480 starting
  // at this offset; rows outside it are padded 0xFF (white) in every DTM write.
  uint16_t gateOffset;
};

const Uc8279X4Config& uc8279X4DefaultConfig();

// Declarative refresh programs. A pipeline may contain an AsyncBoundary; the
// driver executes through that step in displayStart() and resumes at the next
// step in displayFinish(). Alternate hardware experiments inject a different
// set instead of adding mode-specific branches to the driver.
enum class Uc8279X4RefreshStep : uint8_t {
  UploadNewTarget,
  UploadOldWhite,
  UploadOldComplementTarget,
  UploadGrayPlanes,
  ConfigureBwFull,
  ConfigureBwFast,
  ConfigureGrayPrecondition,
  ConfigureGrayActivation,
  PowerOn,
  EnterFullWindow,
  EnterGrayWindow,
  SelectOtp,
  SelectExternal,
  Activate,
  AsyncBoundary,
  WaitRefresh,
  ExitWindow,
  RestoreOldTarget,
  RestoreBothBaseline,
  PowerOffIfRequested,
};

struct Uc8279X4RefreshPipeline {
  const char* name;
  const Uc8279X4RefreshStep* steps;
  uint8_t stepCount;
};

struct Uc8279X4RefreshPipelines {
  Uc8279X4RefreshPipeline bwFull;
  Uc8279X4RefreshPipeline bwScrub;
  Uc8279X4RefreshPipeline bwFast;
  Uc8279X4RefreshPipeline bwFastOverGray;
  Uc8279X4RefreshPipeline bwRedrive;
  Uc8279X4RefreshPipeline grayPrecondition;
  Uc8279X4RefreshPipeline grayOverlay;
  Uc8279X4RefreshPipeline grayAbsolute;
  Uc8279X4RefreshPipeline grayDirect;
};

const Uc8279X4RefreshPipelines& uc8279X4DefaultPipelines();

class Uc8279X4Driver : public PanelDriver {
 public:
  explicit Uc8279X4Driver(const Uc8279X4Config& cfg = uc8279X4DefaultConfig(),
                          const Uc8279X4RefreshPipelines& pipelines = uc8279X4DefaultPipelines());
  ~Uc8279X4Driver() override;

  uint32_t spiHz() const override;
  BusyPolarity busyPolarity() const override { return BusyPolarity::UcIdleHigh; }
  PanelGeometry geometry() const override;

  void begin(EpdBus& bus) override;
  void deepSleep(EpdBus& bus) override;
  bool supportsRetainedState() const override { return true; }
  void beginRetained(EpdBus& bus) override;
  void powerOffRetainingState(EpdBus& bus) override;

  void display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) override;
  bool displayStart(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) override;
  void displayFinish(EpdBus& bus, const uint8_t* fb) override;
  bool supportsAsyncDisplay() const override { return true; }
  void seedPreviousFrame(EpdBus& bus, const uint8_t* buf) override;

  void requestResync(uint8_t settlePasses) override;
  void skipInitialResync() override;

  // --- 4-level grayscale (anti-aliasing) ---
  // External-LUT path (ported from the UC8179 sibling). CrossPoint supplies DELTA
  // masks (maskLsb, maskMsb): black/white=(0,0), dark=(1,1), light=(0,1), with a
  // B/W base that is 1 for white and 0 for every non-white pixel. Those are folded
  // into stock's ABSOLUTE selectors so white and black are DISTINCT buckets:
  //   plane0 = base | maskLsb,  plane1 = plane0 ^ maskMsb
  //   -> black=(0,0), dark=(1,0), light=(0,1), white=(1,1)
  // sent INVERTED (as stock does; the 5x49 LUTs were extracted for this
  // encoding). Feeding raw delta planes conflated black & white into one bucket
  // and left the B/W diff baseline unaware of AA edge charge -> white ghosting;
  // the absolute fold + post-DRF base restore (base = plane0 & plane1) fixes
  // both. Single-byte CDI (constant 0x97), PSR rewritten before DRF, panel LEFT
  // POWERED (vendor).
  // Defined in the .cpp: reports unsupported for LUT_VER 0x67, a variant
  // stock's panel LUT registry (X4 Pro 260917 build) recognizes but ships no
  // external-LUT tables for — that panel runs OTP waveforms only.
  GrayscaleCapabilities grayscaleCapabilities(GrayscaleMode mode = GrayscaleMode::Overlay) const override;
  void beginGrayscale(EpdBus& bus, const uint8_t* fb, GrayscaleMode mode, RefreshMode fallback, bool turnOff) override;
  void copyGrayscaleLsb(EpdBus& bus, const uint8_t* lsb) override;
  void copyGrayscaleMsb(EpdBus& bus, const uint8_t* msb) override;
  void displayGray(EpdBus& bus, const uint8_t* fb, bool turnOff, const unsigned char* lut, bool factoryMode) override;
  void cleanupGrayscaleBuffers(EpdBus& bus, const uint8_t* bw) override;
  void preserveGrayscaleOnFastRefresh() override;
  void setGrayscaleWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) override;
  // Base frame for a grayscale overlay. Explicit Full/Half requests use their
  // corresponding B/W pipeline; Fast uses the factory pre-B/W transition when
  // a prior gray reference and exact B/W controller baseline are available.
  void displayGrayscaleBase(EpdBus& bus, const uint8_t* fb, RefreshMode fallback, bool turnOff) override;

 private:
  void initController(EpdBus& bus);
  void resetRuntimeState(bool retained);
  bool ensureHostBuffers();
  bool runPipeline(EpdBus& bus, const Uc8279X4RefreshPipeline& pipeline, bool resume);
  bool executeStep(EpdBus& bus, Uc8279X4RefreshStep step);
  void configureBwRefresh(EpdBus& bus, bool fast);
  void configureGrayActivation(EpdBus& bus);
  void configureGrayPrecondition(EpdBus& bus);
  const Uc8279X4RefreshPipeline& pipelineFor(Uc8279X4RefreshState::Pipeline pipeline) const;
  uint32_t nextGeneration();
  // Stream a framebuffer into a RAM plane: 0xFF padding for gates before the
  // visible offset, visible rows in the stock convention (forward order, bytes
  // as-is; FREEINK_UC8279X4_ROWREV/XMIRROR can flip either axis for future
  // sub-variants), then 0xFF padding to the addressed gate count. `invert`
  // bitwise-inverts the image rows (AA planes only, per the vendor reference).
  void streamPlane(EpdBus& bus, uint8_t ramCmd, const uint8_t* fb, bool invert = false);
  void powerOnIfNeeded(EpdBus& bus, const char* tag);
  // Stock's non-flashing previous->current AA base transition (FUN_4214d4ac /
  // FUN_4214d3a0). DTM1 holds the previous page's B/W base, DTM2 the new one;
  // the UC8279_aa_prebw_mid settle waveform drives that transition without an OTP
  // GC flash. Ported from the UC8179 sibling; the base is restored to DTM1 after.
  void transitionGrayscaleBase(EpdBus& bus, const uint8_t* fb, bool turnOff);
  bool runGrayscalePrecondition(EpdBus& bus);
  void enterGrayscaleWindow(EpdBus& bus);
  void enterFullWindow(EpdBus& bus);
  void expandWindowForBwDelta();
  void expandWindowForGrayDelta();
  void includeChangedByte(uint16_t byteX, uint16_t y);

  const Uc8279X4Config& _cfg;
  const Uc8279X4RefreshPipelines& _pipelines;

  uint16_t _w;      // visible width (800)
  uint16_t _h;      // visible height (480)
  uint16_t _wb;     // width in bytes (100)
  uint16_t _tresH;  // addressed gate count (600)
  uint32_t _bufferSize;

  // Stable host-owned buffers. Caller draw buffers may be swapped or modified
  // immediately after displayStart(), so no finish path may depend on them.
  uint8_t* _bwBaseline = nullptr;
  uint8_t* _bwTarget = nullptr;
  uint8_t* _grayLsb = nullptr;
  uint8_t* _grayMsb = nullptr;
  // Absolute logical selector planes corresponding to the native-gray image
  // currently on glass. Zoned activation is only legal while these are valid;
  // otherwise the window is promoted to the full visible panel.
  uint8_t* _panelGrayLsb = nullptr;
  uint8_t* _panelGrayMsb = nullptr;
  bool _panelGrayValid{false};

  struct GrayJob {
    GrayscaleMode mode{GrayscaleMode::Overlay};
    bool imageQuality{false};
  } _grayJob;

  struct Window {
    bool enabled{false};
    bool entered{false};
    uint16_t x{0};
    uint16_t y{0};
    uint16_t w{0};
    uint16_t h{0};
  } _window;

  struct PipelineCursor {
    const Uc8279X4RefreshPipeline* pipeline{nullptr};
    uint8_t nextStep{0};
    bool turnOff{false};
  } _cursor;

  Uc8279X4RefreshState _state;
  uint32_t _generation{0};
};

PanelDriver& uc8279X4Driver();

}  // namespace freeink

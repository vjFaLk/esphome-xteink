# UC8279 X4 refresh pipelines

This document is the specification for the Xteink X4-family UC8279 driver. It
separates facts recovered from the controller/vendor implementation from policy
chosen by FreeInk and from behavior that still needs physical-panel validation.

The refactor must not infer visible panel charge from the contents of DTM1 or
DTM2. Controller RAM, the host buffers, and the electrophoretic panel are three
different state domains.

## Sources and confidence

Primary implementation evidence:

- Xteink X4-family factory firmware sequences recovered in
  `Uc8279X4Driver.cpp`. The known entry points are the UC8279 full trigger
  (`FUN_4214cfe8`), partial trigger (`FUN_4214d050`), AA pre-B/W transition
  (`FUN_4214d3a0`), and its caller (`FUN_4214d4ac`).
- Vendor-exact X4C QY/ZHX XTH4 waveform data recovered from factory firmware in
  upstream FreeInk's `Uc8279X4VendorLuts.h`.
- UC8279d controller command semantics and data-plane definitions summarized in
  the X3 driver reference. Those controller-family semantics are applicable;
  its geometry, analog setup, and waveform tables are not.
- The UC8279 X4 Pro field unit with `LUT_VER=0x02` validated controller
  detection, orientation, OTP GC, and windowed OTP DU. Native-gray quality and
  every proposed custom waveform remain separate hardware tests.

Useful public references:

- [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk)
- [PapyriX UC8279d driver reference](https://github.com/bigbag/papyrix-reader/blob/main/docs/x3-uc8279-driver-reference.md)
- [ESPHome UC8279 implementation](https://api-docs-dev.esphome.io/epaper__spi__uc8279_8cpp_source)

The generic UC8279d documentation is not permission to copy panel-specific
rails, gate geometry, temperature values, or LUTs. For this 800x480 glass the
factory firmware or X4-family vendor reference wins.

## Documented controller facts

- KW mode uses DTM1 (`0x10`) as the OLD selector plane and DTM2 (`0x13`) as the
  NEW selector plane for differential monochrome waveforms.
- DRF (`0x12`) activates the currently selected waveform; BUSY_N is low while
  the controller is busy and high while idle.
- PTIN (`0x91`) must be paired with a PTL (`0x90`) window and terminated with
  PTOUT (`0x92`). The field X4 unit did not develop a DU update when PTIN had no
  PTL.
- The controller addresses 800x600 gates. The visible 800x480 image begins at
  gate 120; non-visible gates are written white.
- OTP B/W refreshes use PSR with REG=0. External grayscale/precondition LUTs use
  REG=1. On this X4 variant PSR is rewritten after PON and before DRF because
  PON reloads the programmed defaults.
- Factory B/W trigger order is CDI, CCSET, TSSET, optional PFS/gate-scan for DU,
  PON, optional PTIN/PTL, PSR, DRF.
- Factory AA activation order is PSR(REG=1), plane upload, LUT upload, CDI, PON,
  optional window, PSR(REG=1), DRF.
- A hardware reset or DSLP exit invalidates assumptions about DTM1/DTM2. POF
  alone can retain RAM and registers, but the host may only adopt them when it
  has an explicit retained-state descriptor—not by assuming a framebuffer is
  equal to whatever the controller retained.

## State domains

### Host buffers

The driver owns stable staging buffers for any data needed after an API call
returns:

- `bw_baseline`: exact clean B/W frame corresponding to the controller's
  differential baseline.
- `bw_target`: immutable copy of the submitted target retained across the
  asynchronous DRF boundary.
- `gray_lsb` and `gray_msb`: complete staged selector planes for the current
  grayscale job.
- `panel_gray_lsb` and `panel_gray_msb`: the last absolute native-gray
  selectors known to be visible on glass. These are not controller-RAM
  mirrors; overlay/absolute pipelines restore controller RAM to the B/W base.

The LVGL/FreeInk draw buffers remain caller-owned. No asynchronous finish step
may reread a caller buffer whose ownership was returned to the caller.

### Controller

The state record names:

- lifecycle: uninitialized, ready/off, ready/on, or deep sleep;
- DTM1 and DTM2 content kind and generation;
- active partial window, if any;
- active/pending pipeline;
- grayscale upload phase.

“Valid” is not a standalone Boolean. A plane is valid only as a specific
content kind for a specific generation.

### Panel

The visible panel state is tracked independently as unknown, monochrome, or
native four-tone, with the generation that produced it. A B/W RAM baseline does
not prove that a native-gray panel has been discharged to the B/W endpoints.

## Pipeline model

Every refresh is a named sequence of declarative steps. The default definitions
must be injectable so a hardware experiment changes a pipeline definition
instead of adding another branch to the driver.

Initial pipelines:

- `bw_full`: DTM2=target, DTM1=white, OTP GC activation, then DTM1=target.
- `bw_scrub`: DTM2=target, DTM1=complement(target), OTP GC activation, then
  DTM1=target. This is a FreeInk policy, not a controller-defined “half” mode.
- `bw_fast`: DTM2=target, DTM1=known prior B/W baseline, windowed OTP DU, then
  DTM1=target.
- `bw_fast_over_gray`: the same controller operation, but only after an
  explicit caller request to preserve the visible native-gray charge. The
  panel state remains native gray with a B/W delta; it is not relabeled as a
  monochrome panel merely because both RAM planes are B/W baselines.
- `bw_redrive`: DTM2=target, DTM1=complement(target), windowed OTP DU, then
  DTM1=target. This deliberately drives every binary pixel.
- `gray_precondition`: DTM1=previous B/W base, DTM2=new B/W base, external
  pre-B/W waveform, then both planes restored to the new B/W base.
- `gray_overlay`: stage/fold overlay masks into absolute selectors, activate the
  factory AA bank, then restore both RAM planes to `bw_baseline`.
- `gray_absolute`: stage absolute selectors, activate the selected four-tone
  bank, then restore both RAM planes to `bw_baseline`.
- `gray_direct`: stage absolute selectors and activate them without claiming a
  B/W differential baseline afterward.

Conditioning must be composed from explicit pipelines. “Black” and “white” are
image targets, not waveform names. Repeating an OTP GC refresh can contain
inverse phases and therefore is not an endpoint overcharge operation.

The manual X4 conditioning experiment runs 30 transactions, yielding to the
ESPHome scheduler between them:

`black scrub → black redrive ×5 → white scrub → white redrive ×5 → black scrub →
gray 0/1/2/3 direct → black scrub → black redrive ×5 → white scrub → white
redrive ×5 → white scrub`.

The scrub is the self-contained `bw_scrub` pipeline. Each redrive runs the
windowed OTP DU waveform with OLD set to the complement of the requested solid
target and NEW set to that target. The gray steps use full-panel absolute
selectors through `gray_direct`, so no intervening B/W base activation obscures
the four-tone waveform. `gray_overlay` cannot encode the final solid white tone
over the black base under its documented mask contract. The repeated DU passes
are a physical experiment, not a proven DC or charge-conditioning sequence;
the observed tone and ghosting on the actual panel decide whether to keep it.

## Required invariants

1. At most one pipeline is pending.
2. A fast B/W pipeline requires a known B/W baseline for the same visible panel
   generation, or must be promoted to a strong synchronization pipeline.
3. A grayscale activation requires two complete staged planes in the declared
   encoding.
4. A grayscale overlay/absolute activation requires an exact `bw_baseline`.
5. DTM1/DTM2 content is updated after every RAM write, not only after DRF.
6. A pipeline updates visible panel state only after DRF completion.
7. DSLP invalidates controller RAM and panel-to-RAM correspondence.
8. POF changes power state only; it does not silently validate or invalidate
   RAM.
9. Failure/cancellation leaves the state conservative: unknown RAM and a
   required strong synchronization, never a guessed fast baseline.
10. Window coordinates are visible coordinates in the public API and are
    transformed/aligned exactly once by the driver.
11. A window is a caller hint, not permission to omit a changed pixel. Before
    PTL is sent, the driver unions every changed B/W or gray-selector byte into
    the requested rectangle. If the prior visible gray selectors are unknown,
    a zoned gray activation is promoted to the full visible panel.

## Waveform banks

Sparse glyph anti-aliasing uses the factory XTF-AA bank selected by `LUT_VER`.
Image-dominant absolute/direct work uses the X4C factory XTH4 data. The QY blob
appears to contain a 9-byte prefix, five 49-byte register payloads, and a
36-byte `0xFF` trailer. The exact purpose and command mapping of its prefix
remain unverified. The ZHX table contains five explicit 42-byte register
payloads. The driver does not scale an X3 waveform or synthesize phases for X4
glass. Neither XTH4 mapping has been validated on this physical X4 Pro panel.

## Validation layers

- Pure state-model tests enumerate legal and illegal transitions and assert all
  invariants after each operation.
- Recording-bus tests verify exact command order, data-plane content, window
  geometry, and postconditions for every default pipeline.
- Facade tests validate framebuffer ownership across synchronous, asynchronous,
  grayscale, cancellation, inversion, reset, and sleep paths.
- Physical tests validate appearance, timing, ghosting, endpoint charge, and
  temperature behavior. Host tests cannot prove those properties.

# esphome-xteink

ESPHome components for the **Xteink X4, X3 and X4 Pro** e-paper readers, built on the
[FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) (the actively maintained successor of
`open-x4-epaper/community-sdk`, and what [CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader) runs on).

This repository is a fork of [vjFaLk/esphome-xteink](https://github.com/vjFaLk/esphome-xteink)
and keeps its component and configuration surface compatible with that upstream project.

The SDK is not copied into the ESPHome code: it is vendored under
`components/xteink/sdk/`, pinned by commit, then supplemented by the small tracked patches in
`components/xteink/sdk-patches/`, and built as ordinary PlatformIO libraries. Updating to a
newer SDK is one script run plus a patch rebase (see [Bumping the SDK](#bumping-the-sdk)).

| Model  | MCU      | Panel                        | Display | Buttons | Battery | Frontlight | Touch |
|--------|----------|------------------------------|---------|---------|---------|------------|-------|
| x4     | ESP32-C3 | 800×480 SSD1677 / UC8179     | ✓       | 7       | ADC     | –          | –     |
| x3     | ESP32-C3 | 792×528 UC8253 / UC8279      | ✓       | 7       | BQ27220 | –          | –     |
| x4_pro | ESP32-S3 | 800×480 SSD1677/UC8179/UC8279| ✓       | 3 + home| CW2017  | warm/cool  | GT911 |

All three compile in CI. **Hardware status:** tested on an **X4** (display, buttons, battery,
deep sleep/wake) and an **X4 Pro** (display, GT911 touch + home key, edge/power buttons,
CW2017 battery, frontlight, OTA, deep sleep/wake), both driving a Home Assistant remote.
The X3 compiles but has not been run on hardware yet; reports welcome via issues.

The grayscale implementation in this version is currently hardware-tested only on an
**X4 Pro with a UC8279 panel**. It supports four-level gray using native, ordered-dither,
and adaptive rendering; no grayscale hardware validation is claimed for the X4 or X3.

## Usage

```yaml
packages:
  board: github://morpheby/esphome-xteink/packages/x4.yaml   # or x3.yaml / x4_pro.yaml

external_components:
  - source: github://morpheby/esphome-xteink
    components: [xteink]

# the package already sets `xteink: {model: x4}`; override update_interval here if you want
# xteink:
#   model: x4
#   update_interval: 50ms   # button/touch poll rate
#   hold_battery_latch: true  # X4 only: stay powered through deep sleep so a timer can wake it

font:
  - file: "gfonts://Roboto"
    id: roboto_32
    size: 32

display:
  - platform: xteink
    update_interval: never
    refresh_mode: fast       # full | half (default) | fast
    full_refresh_every: 20   # 0 disables periodic cleanup
    # grayscale: true        # X4 Pro only; accepts continuous luminance
    # grayscale_strategy: adaptive  # native (default) | dither | adaptive
    # grayscale_settle_time: 5s     # adaptive: quiet time before native gray
    lambda: |-
      it.print(20, 20, id(roboto_32), "Hello");

binary_sensor:
  - platform: xteink
    button_1: { name: Button 1 }   # front row, left to right
    button_2: { name: Button 2 }
    button_3: { name: Button 3 }
    button_4: { name: Button 4 }
    up:       { name: Up }         # side keys (rocker on X4/X3, edge keys on X4 Pro)
    down:     { name: Down }
    power:    { name: Power }
    # home:   { name: Home }       # x4_pro only (capacitive key, pulses ON for one poll)

sensor:
  - platform: xteink
    battery_level:   { name: Battery }
    battery_voltage: { name: Battery voltage }

# x4_pro only
light:
  - platform: xteink
    name: Frontlight              # cold/warm white; color temperature = warm/cool mix

touchscreen:
  - platform: xteink
    # The SDK reports panel-native coords (0..799 x 0..479); ESPHome's touchscreen
    # does not follow `display: rotation:`. For a portrait UI (rotation: 270) use:
    transform: { swap_xy: true, mirror_x: true }
    calibration: { x_min: 0, x_max: 479, y_min: 0, y_max: 799 }
    on_touch:
      - logger.log:
          format: "touch %d,%d"
          args: [touch.x, touch.y]
```

The package YAMLs only carry the board facts (variant, 16 MB flash, USB-JTAG console on the
X4 Pro) and the `xteink: model:`. ESPHome's default partition table already puts the app at
`0x10000`, so no custom `partitions.csv` is needed. Note that a USB flash from ESPHome replaces
the stock bootloader and partition table, as with any ESP32 board.

### Display notes

- Lambda/pages rendering clears before drawing by default. LVGL has no writer and keeps a
  persistent framebuffer, so its dirty rectangles cannot erase untouched content.
- Literal RGB blocks (including LVGL) are converted by luminance with the correct black/white
  polarity. Native display lambdas keep ESPHome's `COLOR_ON` = black-pixel convention.
- `it.set_refresh_mode(n)` picks the waveform for *this* refresh. The first refresh after boot is
  always at least a half refresh; later ones can be fast (partial/diff) updates.
- `refresh_mode` avoids a boot lambda just to select the waveform, while `full_refresh_every`
  provides periodic cleanup. `grayscale: true` accepts continuous luminance on X4 Pro.
  `grayscale_strategy: native` (the default) uses the accurate but slow panel waveform;
  `grayscale_strategy: dither` converts luminance through a stable ordered pattern and keeps
  the normal fast differential waveform. `adaptive` uses dither while interacting, then runs
  one native-gray settle after `grayscale_settle_time` of quiet. Unsupported native-gray
  variants log and render monochrome.
- `it.update_count` counts refreshes, handy for "full refresh every N updates".
- `deep_sleep` needs nothing extra in your YAML: on power-down the hub parks the panel (DSLP),
  drives the X4's GPIO13 battery latch LOW (a real power-off on battery, like CrossPoint;
  on USB the chip deep-sleeps and GPIO3 wakes it; `hold_battery_latch: true` keeps it HIGH
  instead so `sleep_duration` timer wakes work — at ~3-4 mA asleep, a limit of the X4 PCB,
  and less with the microSD card removed), cuts the X3's SD rail (its GPIO13 —
  the X3 has no battery latch, so it deep-sleeps and can timer-wake), holds the X4 Pro's GPIO1 rail latch,
  switches its GT911 off and parks the frontlight pads LOW, and isolates every other pad.
- Buttons are named by position, not function; what they *do* is your YAML's business.
- Migrating from `ngxson/esphome-component-xteink`: rename the platforms (`xteink_edp`,
  `xteink_input`, `xteink_battery` → `xteink`), rename `button_up` / `button_down` /
  `button_pwr` → `up` / `down` / `power` (`button_1..4` and the refresh-mode numbers are the
  same), and drop `libraries: [SPI]`, `partitions.csv` and `package_xteink_x4.yml`.

## How it is put together

```
components/xteink/
  __init__.py                 xteink: { model: x4 | x3 | x4_pro }  — build flags, SDK lib_deps
  display.py / display.*      display platform (DisplayBuffer over the SDK framebuffer)
  binary_sensor.py / buttons.* buttons (SDK InputManager, polled by the hub)
  sensor.py / battery.*       battery level / voltage (SDK BatteryMonitor: ADC or I²C gauge)
  light.py / frontlight.*     X4 Pro frontlight (SDK FrontlightManager)
  touchscreen.py / touch.*    X4 Pro GT911 touch (SDK InputManager touch snapshot)
  sdk/                        sparse copy of freeink-sdk (6 libs) + a 10-line Logging.h shim
```

At codegen time `__init__.py` merges the six libraries' `include/` and `src/` trees into one
PlatformIO library at `<build>/lib/xteink-sdk` and declares it with
`cg.add_library("xteink-sdk", None, "file:///…")`. That single-library shape is deliberate: the
SDK libs `#include` each other, and neither PlatformIO with ESPHome's `lib_ldf_mode = off`
(ESPHome ≤ 2026.7) nor ESPHome ≥ 2026.8's library-to-ESP-IDF-component converter links local
libraries to each other. The sync script replays `sdk-patches/` after copying the pinned SDK;
CI runs the same operation and fails if the checked-in result is not reproducible.

Two ESPHome facts explain the rest of the shape: external components never clone git
submodules, and only files directly inside the component folder are compiled — so a vendored
copy plus `add_library` is the only route that works for `github://` users without extra setup.

## Bumping the SDK

```sh
scripts/sync-sdk.sh <freeink-sdk commit sha>
```

re-copies the six libraries, refreshes `sdk/LICENSE` + `sdk/NOTICE`, writes `sdk/SDK_COMMIT`,
and reapplies every patch in `sdk-patches/`. Rebase those patches if the pinned SDK changed,
then compile the examples (`cd examples && esphome compile x4.yaml` …) and open a PR. The
`sdk-in-sync` CI job re-runs the complete sync-and-patch operation and fails on any drift.

## Credits

- [vjFaLk/esphome-xteink](https://github.com/vjFaLk/esphome-xteink) — the upstream ESPHome
  component this fork extends.
- [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) — drivers, board profiles, waveforms.
- [OpenX4 community-sdk](https://github.com/open-x4-epaper/community-sdk) and CidVonHighwind —
  the original SSD1677 driver and panel tuning FreeInk derives from.
- [ngxson/esphome-component-xteink](https://github.com/ngxson/esphome-component-xteink) — the first
  ESPHome integration, whose YAML surface this repo stays compatible with.

MIT licensed; see `LICENSE` and `NOTICE`.

## X4 Pro waveform calibration branch

This branch stops at the physical calibration checkpoint. Normal boiler output
uses ordered B/W preview; the two grayscale mode requests remain explicitly gated
until measured palettes, hold behavior, and preparation waveforms are accepted.
Selective minute wakes and retained-controller rendering are a later stage.

Dependencies are pinned Git submodules in `components/xteink/dependencies`:
FreeInk owns board communication and the raw UC8279 transport; `xteink-rendering`
owns deterministic quantization, two 8-bit frame histories, masks, waveforms,
pipeline validation, and calibration layouts. ESPHome owns LVGL/HA integration,
display scheduling, and sleep arbitration. `dependencies.json` checks the exact
revisions even when ESPHome's external-component clone omits submodule init.

Clone with `git clone --recurse-submodules`. For an existing checkout, run
`git submodule update --init --recursive`. No patch/sync step is used.

The included `packages/x4_waveform_lab.yaml` binds to the boiler configuration's
`x4_display`, `ds`, `ota_hold`, LVGL, and named sleep/UI scripts. It is an optional
boiler-specific diagnostic package, not a generic board package. Entering the lab
pauses LVGL and prevents sleep. Every waveform is manually requested. Exiting
waits for the active waveform and pump shutdown, resets legacy controller state,
and rebuilds the boiler page before releasing normal sleep scheduling.

Use **Waveform Rebuild B-W Starts** first. A/B/C show pure black and pure white
starts in the upper 75 rows, with ordered ramps in the bottom 15 rows. Candidate C
crosses x752. **Repeat With Held References** drives the candidates while keeping
white/black/gray references on selector 00. **Rotate Candidates** tests transitions
without resetting starts. Rebuild after changing pulse parameters so labels stay
accurate. Raw frame counts are not milliseconds; rail 1/2 polarity and rail 3's
meaning are not established for the matching silicon. Source VSS is not assumed
to produce zero field relative to VCOM.

Default activation omits PTL and spans complete visible lines/full height.
Vertical-window and cropped full-line upload experiments are off by default.
Uploads and activation windows are separate. Cropping is used only when the
vertical-window experiment is also enabled, so a full-height activation cannot
expose stale selectors outside the uploaded rows. A cropped upload always
restores a full-width activation window over the uploaded rows. No horizontal
activation restriction is offered.

Enter six observed luminances in the **Measured A/B/C Black/White Start** controls
only after measuring/comparing the screen. **Waveform Measured Shade Order** sorts
those inputs and reports gaps. No calibrated levels or ordering are preselected.
Duration, RAM-plane bytes, mask counts, regions, pipeline, completion, and cycles
are separate diagnostics. Uploaded bytes count DTM payloads, including hidden gate
padding; register/LUT command traffic is excluded.

The custom LUT interpretation is experimental. The exact UC8279 datasheet was not
available; recovered 7-byte records and a controller emulator support the proposed
packing, but do not validate repeat, source/VCOM voltages, floating/hold options,
or waveform-end behavior on this unit. The unresolved QY 290-byte split is not
used. Host tests prove software behavior; panel photographs/measurements remain
necessary before using these waveforms in normal rendering.

Host validation:

```sh
python3 components/xteink/dependencies/rendering/test/run.py
python3 components/xteink/dependencies/freeink/libs/display/FreeInkDisplay/test/host/run_x4_transport.py
```

### Updated waveform inspection and B/W timing

The second calibration firmware uses **opposite prefix → A/B/C candidate → hold**.
The observed panel behavior is code 1 darkens, 2 brightens, 0 holds references in
current tests; no stable palette or noise cure is established. Rebuild resets
rotation; Repeat preserves it; Assignments reports the actual last mapping.

HA exposes A/B/C/VCOM/Hold **Binary** traces and **LUT Hex** records, plus equivalent
B/W black/white/VCOM diagnostics. These describe the saved, actually programmed
register payloads. The compact trace contains two-bit codes, duration counts,
and group/state repeats; the hex record is the exact 49 bytes. **Waveform Export
Binary LUTs** prints all seven raw binary groups to logs without refreshing.
Factory OTP preparation correctly reports its unrecovered payload unavailable.

For the B/W noise experiment, select **Waveform B-W Preparation → Programmable
B-W Drive**, change **Waveform B-W Frames** (1–63) and **Waveform B-W Repeats**
(1–16), then press Rebuild. The same endpoint duration applies to black and white;
code 1 drives black and code 2 drives white, followed by one hold count. Factory
OTP remains the baseline comparison. Frames/Repetitions affect preparation only,
not A/B/C settle durations. Inspect the upper 75 solid rows of each test patch;
the bottom 15 contain intentional ordered ramps.

### Inverted-image preparation phase

Programmable B/W preparation now performs **inverted image → requested B/W image
→ hold**. **Waveform B-W Inversion Frames** controls the first phase (0–63;
zero disables it for comparison). **Waveform B-W Frames** continues to control
the final target phase. **Waveform B-W Repeats** repeats the complete cycle.
The new field defaults to 16. During this user's update, the saved 20-count B/W
target and two repeats were retained, with inversion initially set to 20.

Before deployment, the user's best current settings were exported: A/B/C=2/3/4,
rail=2, opposite prefix=3, repeats=1, neutral=1, hold=0, B/W=20 counts ×2,
Factory OTP preparation, with vertical-window and cropped-upload experiments on.
Their restoration is checked through the authenticated device API. Preference
storage previously reported insufficient space, so exported settings remain
necessary backup evidence; no device NVS erase is performed.

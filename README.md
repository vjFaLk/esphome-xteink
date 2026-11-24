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
  Set `rendering_mode` to select the X4 Pro renderer.
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

## X4 Pro rendering and calibration

Set `rendering_mode: dither_then_settle` and `grayscale: true` for the
calibrated X4 Pro renderer. The [rendering guide](docs/x4-waveform-lab.md)
covers the supported modes and the C++ calibration entry point. The
[minute wake guide](docs/x4-minute-wake.md) describes the boiler integration.

The driver reads the temperature-selected factory table at boot. The
pinned [C++ rendering library](components/xteink/dependencies/rendering/README.md)
owns the default calibration and the interface for measured replacement profiles.

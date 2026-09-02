# esphome-xteink

ESPHome components for the **Xteink X4, X3 and X4 Pro** e-paper readers, built on the
[FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) (the actively maintained successor of
`open-x4-epaper/community-sdk`, and what [CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader) runs on).

The SDK is not copied into the ESPHome code: it is vendored *unmodified* under
`components/xteink/sdk/`, pinned by commit, and built as ordinary PlatformIO libraries.
Updating to a newer SDK is one script run (see [Bumping the SDK](#bumping-the-sdk)).

| Model  | MCU      | Panel                        | Display | Buttons | Battery | Frontlight | Touch |
|--------|----------|------------------------------|---------|---------|---------|------------|-------|
| x4     | ESP32-C3 | 800×480 SSD1677 / UC8179     | ✓       | 7       | ADC     | –          | –     |
| x3     | ESP32-C3 | 792×528 UC8253 / UC8279      | ✓       | 7       | BQ27220 | –          | –     |
| x4_pro | ESP32-S3 | 800×480 SSD1677/UC8179/UC8279| ✓       | 3 + home| CW2017  | warm/cool  | GT911 |

All three compile in CI. **Hardware status:** tested on an **X4** (display, buttons, battery,
deep sleep/wake, driving a Home Assistant remote). X3 and X4 Pro compile but have not been
run on hardware yet — testing is planned once the devices arrive; reports welcome via issues.

## Usage

```yaml
packages:
  board: github://vjFaLk/esphome-xteink/packages/x4.yaml   # or x3.yaml / x4_pro.yaml

external_components:
  - source: github://vjFaLk/esphome-xteink
    components: [xteink]

# the package already sets `xteink: {model: x4}`; override update_interval here if you want
# xteink:
#   model: x4
#   update_interval: 50ms   # button/touch poll rate

font:
  - file: "gfonts://Roboto"
    id: roboto_32
    size: 32

display:
  - platform: xteink
    update_interval: never
    lambda: |-
      it.print(20, 20, id(roboto_32), "Hello");
      it.set_refresh_mode(2);   # 0 = full, 1 = half (default), 2 = fast (diff)

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

- The lambda repaints the whole frame every `update()`; the buffer is cleared afterwards.
- `it.set_refresh_mode(n)` picks the waveform for *this* refresh. The first refresh after boot is
  always at least a half refresh; later ones can be fast (partial/diff) updates.
- `it.update_count` counts refreshes, handy for "full refresh every N updates".
- `deep_sleep` needs nothing extra in your YAML: on power-down the hub parks the panel (DSLP),
  drives the X4/X3 GPIO13 battery latch LOW (a real power-off on battery, like CrossPoint;
  on USB the chip deep-sleeps and GPIO3 wakes it) and isolates every other pad.
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
libraries to each other. Nothing in `sdk/libs` is edited (CI fails if it is); the merge is
redone whenever `sdk/` changes.

Two ESPHome facts explain the rest of the shape: external components never clone git
submodules, and only files directly inside the component folder are compiled — so a vendored
copy plus `add_library` is the only route that works for `github://` users without extra setup.

## Bumping the SDK

```sh
scripts/sync-sdk.sh <freeink-sdk commit sha>
```

re-copies the six libraries, refreshes `sdk/LICENSE` + `sdk/NOTICE` and writes `sdk/SDK_COMMIT`.
Then compile the three examples (`cd examples && esphome compile x4.yaml` …) and open a PR.
The `sdk-in-sync` CI job re-runs the script against `SDK_COMMIT` and fails on any drift.

## Credits

- [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) — drivers, board profiles, waveforms.
- [OpenX4 community-sdk](https://github.com/open-x4-epaper/community-sdk) and CidVonHighwind —
  the original SSD1677 driver and panel tuning FreeInk derives from.
- [ngxson/esphome-component-xteink](https://github.com/ngxson/esphome-component-xteink) — the first
  ESPHome integration, whose YAML surface this repo stays compatible with.

MIT licensed; see `LICENSE` and `NOTICE`.

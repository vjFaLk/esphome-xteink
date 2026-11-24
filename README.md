# esphome-xteink

Use a Xteink X4, X3, or X4 Pro as an ESPHome display. The component provides e-paper output, physical buttons, and battery readings. The X4 Pro also exposes its frontlight, touch screen, and capacitive Home key.

This branch adds calibrated grayscale and small regional updates for the **800 × 480 UC8279 X4 Pro**. The integration was developed with OpenAI Codex and builds on [FreeInk](https://github.com/Free-Ink/freeink-sdk) and the [C++ rendering overlay](https://github.com/morpheby/xteink-rendering/tree/codex/calibration).

| Board package | MCU | Available ESPHome platforms | Validation |
| --- | --- | --- | --- |
| `x4.yaml` | ESP32-C3 | B/W display, buttons, battery | Build and hardware |
| `x3.yaml` | ESP32-C3 | B/W display, buttons, battery | Build |
| `x4_pro.yaml` | ESP32-S3 | Display, buttons, battery, frontlight, touch | Build and hardware. Calibrated rendering was tested with a UC8279 panel. |

## Quick start

Install [ESPHome](https://esphome.io/guides/installing_esphome/) and add the board package and external component to the same YAML. The example uses the X4 Pro with a standalone LVGL label. You can install it over USB and run it without Wi-Fi or Home Assistant. Replace both `ref` values together if pinning to a specific commit.

```yaml
esphome:
  name: xteink-panel

packages:
  board:
    url: https://github.com/morpheby/esphome-xteink
    ref: codex/x4-rendering-calibration
    files: [packages/x4_pro.yaml]

external_components:
  - source:
      type: git
      url: https://github.com/morpheby/esphome-xteink
      ref: codex/x4-rendering-calibration
    components: [xteink]

display:
  - platform: xteink
    id: epd
    update_interval: never
    grayscale: true
    rendering_mode: dither_then_settle

lvgl:
  displays: epd
  update_when_display_idle: true
  bg_color: 0xFFFFFF
  text_color: 0x000000
  widgets:
    - label:
        align: CENTER
        text: "Hello, Xteink"
        text_font: montserrat_24
```

Use `esphome compile panel.yaml` to check and build the configuration. Choose the reader's USB port to install it the first time. USB flashing replaces the stock firmware, bootloader, and partition table. The board packages supply the model, flash settings, and X4 Pro PSRAM configuration. For ESPHome and LVGL, add only the peripherals and networking your application needs. [ESPHome packages](https://esphome.io/components/packages/) · [External components](https://esphome.io/components/external_components/)

The [examples](examples/) include X4, X3, and X4 Pro display lambdas and an [X4 Pro LVGL configuration](examples/x4_pro_lvgl.yaml). Use `packages/x4.yaml` or `packages/x3.yaml` for those boards and start with the corresponding B/W example. X4 Pro frontlight and touchscreen settings are shown in [examples/x4_pro.yaml](examples/x4_pro.yaml).

## Display configuration

| Option | Behavior |
| --- | --- |
| `rendering_mode` | `bw` uses FreeInk's B/W driver. On a UC8279 X4 Pro, `bw_tuned` uses factory-derived B/W, `single_settle` uses three gray tones plus black and white, and `dither_then_settle` blends adjacent calibrated shades. The renderer defaults to `bw`. |
| `grayscale` | Required for the two calibrated gray modes. X4 Pro only. |
| `refresh_mode` | `full`, `half` or `fast`. Applies to the native B/W path. Defaults to `half`. |
| `full_refresh_every` | Requests a periodic full refresh after the configured number of partial refreshes. `0` disables the periodic refresh. |
| `grayscale_windowed_upload` | Uploads affected full-width rows instead of the whole frame during supported partial updates. Defaults to the value of `grayscale`. |
| `grayscale_update_margin` | Optional 0–2 px margin around changed pixels in gray modes. Omit it to use the overlay profile's margin. |

With LVGL, use `update_interval: never` and `update_when_display_idle: true`. LVGL draws dirty areas into the target buffer and submits the update once the draw is ready. A display lambda can instead use a periodic interval and draw the contents it owns. For crisp text in monochrome, use ESPHome fonts with `bpp: 1`. Touch coordinates follow the panel's native axes. If you rotate LVGL, configure or test the touchscreen transform separately using the appropriate panel bounds.

The [X4 Pro rendering guide](docs/x4-waveform-lab.md) describes mode selection, fast interaction previews, and custom calibration. ESPHome selects a mode. The [C++ overlay](components/xteink/dependencies/rendering/README.md) supplies its profile and calibration interface.

## Partial refresh and sleep

Use `select_region(x, y, width, height)` from a lambda to stage an absolute rectangle in physical coordinates. Draw the whole rectangle, including its background. With LVGL, make the selection before invalidating the widget and let LVGL submit the draw. A direct drawing client can call `update()` itself. Check `region_update_finished()` and `region_update_succeeded()` before sleeping or selecting the next region. `clear_region()` restores ordinary rendering.

A regional update needs no saved image planes. The panel retains its visible image through deep sleep. The next normal refresh reconstructs its software baseline. The [minute-wake guide](docs/x4-minute-wake.md) describes how to adapt the application-specific scheduling example for a small clock and less frequent Home Assistant updates.

On the base X4, set `xteink.hold_battery_latch: true` if you need battery-powered timer wakes. It keeps the board powered through sleep and increases standby drain. The X4 Pro uses a separate board power path.

## Develop locally

```sh
git clone --branch codex/x4-rendering-calibration --recurse-submodules https://github.com/morpheby/esphome-xteink.git
cd esphome-xteink
esphome compile examples/x4_pro_lvgl.yaml
```

The ESPHome adapter imports pinned FreeInk and rendering Git submodules. Code generation checks their revisions against `components/xteink/dependencies.json` and stages the required SDK code for ESPHome's build. Git and access to both dependencies are needed for a fresh external-component build. For an existing checkout, run `git submodule update --init --recursive`.

See [AGENTS.md](AGENTS.md) for repository boundaries, dependency updates, and the host-test commands. Earlier controller experiments are documented in the [historical refresh notes](docs/uc8279-x4-refresh-pipelines.md).

## Credits and license

The ESPHome component is a fork of [vjFaLk/esphome-xteink](https://github.com/vjFaLk/esphome-xteink) and builds on the work of [ngxson](https://github.com/ngxson/esphome-component-xteink), the [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk), the [OpenX4 community SDK](https://github.com/open-x4-epaper/community-sdk), and CidVonHighwind. See [LICENSE](LICENSE) and [NOTICE](NOTICE) for the MIT license and upstream attribution.

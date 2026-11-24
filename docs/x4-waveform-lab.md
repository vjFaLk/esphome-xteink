# X4 Pro rendering and calibration

The calibrated renderer targets the 800 × 480 UC8279 X4 Pro. It reads a temperature-selected factory B/W table at boot. The [C++ overlay](../components/xteink/dependencies/rendering/README.md) supplies the measured profiles and the waveform construction.

## Choose a mode

| `rendering_mode` | Use it for |
| --- | --- |
| `bw` | Native FreeInk B/W. Start here on other supported Xteink models and panel variants. |
| `bw_tuned` | Ordered B/W dither with a factory-derived balance, on the UC8279 X4 Pro. |
| `single_settle` | Black, white, and three gray tones using separately tested lightening doses. |
| `dither_then_settle` | Continuous input luminance rendered by dithering between adjacent calibrated physical shades. |

Set `grayscale: true` for either gray mode. The component enables the factory-table read when you configure a mode on the X4 Pro. Use 1-bit fonts for crisp monochrome text. Gray mode can render antialiased glyphs.

## Keep partial updates reliable

The driver uses full-width scan lines to avoid an observed untouched column near x≈752. `grayscale_windowed_upload` limits the uploaded rows without narrowing the active scan lines. It defaults on with grayscale. The renderer can split demanding updates into up to three interleaved zones and applies a panel-wide hold after split B/W preparation. See the [scan-load notes](../components/xteink/dependencies/rendering/docs/drive-load.md) for the measured observations and the limits of the model.

For a slider, call `begin_touch_preview()` when dragging starts and `end_touch_preview()` on release. During the drag, the gray renderer can update changed pixels with a short direct B/W pulse. The next full-quality update restores the selected gray mode. Use normal LVGL invalidation for the slider and its value label.

An [absolute region update](x4-minute-wake.md) is useful for small areas such as clocks after sleep. It can run even when the previous frame buffers are unavailable.

## Calibrate a different panel

Use the [C++ calibration guide](../components/xteink/dependencies/rendering/docs/calibration.md) with the FreeInk transport. Its `Calibration` class draws the reference patterns. Load measured values through `GrayRefresh::loadCalibration()` in C++ firmware. The ESPHome adapter also exposes `load_panel_calibration()` for a C++ caller that needs to install a replacement profile.

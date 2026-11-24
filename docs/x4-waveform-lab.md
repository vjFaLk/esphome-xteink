# X4 Pro rendering and calibration

The X4 Pro renderer uses the temperature-selected factory B/W table, the
verified panel power and clock settings, and calibrated gray profiles.
## Render modes

| Mode | Rendering path |
| --- | --- |
| B-W Tuned | Ordered dither with a factory-derived waveform. Incremental refreshes drive changed dither pixels. |
| B-W Dither | FreeInk's native B/W path. |
| Single Settle | Five physical tones. Black/white pixels use the B/W pass. Each gray selector combines the factory black reset with its lightening dose. |
| Dither Then Settle | Dithers between eight measured shades. The B/W pass constructs each gray's starting tone, then the relative gray pass settles it. The mask includes connected gray from both frames. |

Set `rendering_mode` on the display in YAML to `bw`, `bw_tuned`,
`single_settle`, or `dither_then_settle`. The gray modes require `grayscale: true`.
Setting the X4 Pro mode enables the boot-time factory table read. The C++
overlay supplies the calibrated profile. Use 1-bit fonts in B/W modes to
avoid dithering glyph edges. The boiler derives font bit depth from the mode.

Mode and page changes rebuild the requested image. Full-width scan lines
avoid the observed x≈752 column artifact. Windowed uploads are enabled by
default with grayscale, so ordinary partial passes send only the affected
full-width rows. Set `grayscale_windowed_upload: false` for a full-plane
comparison. Split B/W preparation uses up to three zones and ends with a
panel hold. See the [drive-load findings](../components/xteink/dependencies/rendering/docs/drive-load.md)
for the observed load limits and the model.

## Calibrate a different panel

Use the [C++ rendering library](../components/xteink/dependencies/rendering/README.md)
with the FreeInk controller transport. Its `Calibration` class draws the
reference patterns. Load the resulting measurements through
`GrayRefresh::loadCalibration` in firmware.

## Check partial refreshes and interaction

- On the Light pane, changed pixels use ordered B/W during touch. A
  direct 16-frame pulse drives the preview. Release restores the selected
  gray mode. The boiler package binds the display's touch-preview calls
  to `x4_touch` while `light_page` is active. Remove legacy virtual-update
  wrappers from the two slider callbacks when using this wiring.
See [Minute wakes](x4-minute-wake.md) for scheduled refreshes. Earlier
experimental recipes and control names remain in Git history
(`git show d85f6ba:docs/x4-waveform-lab.md`).

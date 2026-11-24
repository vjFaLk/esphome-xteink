# X4 Pro rendering and calibration

The X4 Pro renderer uses the temperature-selected factory B/W table, the
verified panel power and clock settings, and calibrated gray profiles.
Calibration configurations can add Home Assistant controls through
[x4_waveform_lab.yaml](../packages/x4_waveform_lab.yaml).

## Render modes

| Mode | Rendering path |
| --- | --- |
| B-W Tuned | Ordered dither with a factory-derived waveform. Incremental refreshes drive changed dither pixels. |
| B-W Dither | FreeInk's native B/W path. |
| Single Settle | Five physical tones. Black/white pixels use the B/W pass. Each gray selector combines the factory black reset with its lightening dose. |
| Dither Then Settle | Dithers between eight measured shades. The B/W pass constructs each gray's starting tone, then the relative gray pass settles it. The mask includes connected gray from both frames. |

Set `rendering_mode` on the display in YAML to `bw`, `bw_tuned`,
`single_settle`, or `dither_then_settle`. The gray modes require
`grayscale: true` and the corresponding calibrated profile. Use 1-bit
fonts for B/W text to avoid dithering glyph edges. The boiler derives font
bit depth from its configured mode and uses 1-bit text for the clock.

Mode and page changes rebuild the requested image. Full-width scan lines
avoid the observed x≈752 column artifact. Windowed uploads are enabled by
default with grayscale, so ordinary partial passes send only the affected
full-width rows. Set `grayscale_windowed_upload: false` for a full-plane
comparison. Split B/W preparation uses up to three zones and ends with a
panel hold. See the [drive-load findings](../components/xteink/dependencies/rendering/docs/drive-load.md)
for the observed load limits and the model.

## Calibration in Home Assistant

Enable **Calibration** to take exclusive display control. Disable it to
return to the boiler UI.

- **Stable Gray** exposes G1–G3 frame counts. **Build Chart** compares black
  and white starting patches. **Repeat** reapplies the same selectors and
  **Rotate** moves the targets through previous gray states. Check the held
  references, consistency across starts, and separation of the tones.
  The boiler's tested doses are **3/5/7**. The example input luminances
  **64/128/192** are provisional and can be replaced with measured values.
- **Gray Tuning** exposes candidate frame counts, chart rebuild/repeat/rotate,
  and the measured shade order. Use **White Candidates**, **Neighbor Mixes**,
  and **Linear Ramp** for the relative profile. Compare the six raw
  candidates and measure the 17 steps between neighboring shades. The
  [rendering library guide](../components/xteink/dependencies/rendering/README.md)
  covers measuring and fitting those curves.
- **Waveform Data** exposes the programmed A/B/C and VCOM tracks. **Export
  LUTs**, **Export Timing**, and **Export Factory** write the detailed
  captures to the device log.

The [stable](../packages/x4_pro_stable_profile_example.yaml) and
[relative](../packages/x4_pro_relative_profile_example.yaml) examples describe
one panel. Verify its physical shades and drive behavior before using those
values on another panel.

## Check partial refreshes and interaction

- **Update Margin** adds 0–2 px around changed pixels in either gray mode.
  **Single Settle Area** compares the sparse pixel mask, a filled rectangle,
  and a rectangle with an extra margin around gray-target pixels.
- **Landscape Demo** uses the selected renderer. **Demo Marker** toggles a
  gray patch across x752 so the surrounding gray and the column can be
  inspected across several refreshes.
- On the Light pane, changed pixels use ordered B/W during touch. A
  direct 16-frame pulse drives the preview. Release restores the selected
  gray mode. The boiler package binds the display's touch-preview calls
  to `x4_touch` while `light_page` is active. Remove legacy virtual-update
  wrappers from the two slider callbacks when using this wiring.
- **Regions**, **Driven Pixels**, **Bytes Sent**, **Duration**, **Pipeline**,
  and **Zone Plan** distinguish the requested rectangle, active mask,
  last scan window, and actual transfer.

See [Minute wakes](x4-minute-wake.md) for scheduled refreshes. Earlier
experimental recipes and control names remain in Git history
(`git show d85f6ba:docs/x4-waveform-lab.md`).

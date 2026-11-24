# Historical UC8279 X4 refresh experiments

These notes describe the former `2162a84` implementation, including its option defaults. For current modes and windowed uploads, see [X4 Pro rendering and calibration](x4-waveform-lab.md). The field observations below characterize the tested panel. Proposed electrical explanations remain hypotheses.

The Xteink X4 Pro driver tracks three separate states: host framebuffers,
controller DTM1/DTM2 RAM, and charge visible on the panel. A RAM write does not
change the visible panel until DRF completes. Reset and deep sleep invalidate
controller RAM assumptions; a retained wake needs an explicit saved state before
it can use a differential refresh.

## Sources

- Xteink X4-family factory firmware sequences recovered in
  `Uc8279X4Driver.cpp` (full, partial, and AA pre-B/W refreshes).
- Vendor X4C QY/ZHX waveform banks in `Uc8279X4VendorLuts.h`.
- [Upstream FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) and
  [inkmod's X4 driver](https://github.com/alpzoloto-sudo/inkmod/blob/main/freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8279X4Driver.cpp).

The controller addresses 800×600 gates. The X4 Pro's visible 800×480 image
starts at gate 120. PTL coordinates are visible coordinates in the driver API;
`grayWindowBounds()` applies orientation and eight-pixel X alignment once, then
the command adds the gate offset. PON can reload panel settings, so external-LUT
PSR is reasserted before DRF.

## Refresh behavior

- Full B/W and fast B/W use the factory GC and DU paths respectively.
- Gray preconditioning stages the previous gray charge and the next B/W base in
  separate host buffers. It drives pixels transitioning into or out of gray
  while leaving unchanged B/W pixels without waveform frames.
- Gray overlay uploads two selector planes, activates the factory AA LUT, and
  restores both controller RAM planes to the B/W baseline. Absolute/direct
  gray modes use their corresponding selector pipeline.
- The next gray window includes every changed selector byte and every B/W
  change. If the previous gray state is unknown, the driver uses the full panel.
- `grayscale_windowed_upload: true` enables packed AA plane uploads for zoned
  overlay refreshes. The driver enters PTL before the two plane writes, sends
  only the aligned window's row bytes, exits PTL, then runs the usual AA
  activation. Full-panel refreshes and other gray modes retain full-plane
  uploads. This reduces SPI data volume; it does not change the AA activation
  window or correct the defect below. The option defaults to `false`.

## Known X4 Pro defect

A horizontally bounded AA PTL refresh can leave an approximately eight-pixel
column near x=752 untouched during gray activation. The same bytes in the gray
selector planes drive that column when the AA window starts farther left or
covers the full width. On the test panel, X=372/width=390 with an AA PTL start
of x=360 showed the column; shifting the start to x=352 cleared it. An
X=500/width=262 rectangle also showed it, while X=300/width=462 did not.
Packed AA uploads, plane order, data-stop commands, PSR/PTL order, chip-select
framing, and several other isolated sequence changes did not remove it. The
cause remains unknown. No column-specific window widening, AA PTL suppression,
or other column workaround is applied in this branch. Battery digits in the
same area may also retain strokes during zoned gray updates.

## Validation

Host tests cover state transitions, command order, buffer ownership, window
geometry, and exact packed-plane slices. Host tests cannot establish optical
quality or prove that a controller command sequence drives every source. The
packed upload was physically tested on an X4 Pro: gray remained confined to
the requested rectangle and the known column defect persisted.

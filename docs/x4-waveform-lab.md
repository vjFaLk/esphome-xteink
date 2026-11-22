# X4 waveform lab: conditioned B/W and starting-image tests

This round isolates controller ownership/setup from B/W waveform experiments.
Optical improvement and the reported re-entry failure still need physical
confirmation. Candidate gray parameters and the production boiler layout are
preserved. The single-settle modes remain gated pending measured calibration.

## Deterministic ownership

Acquisition drains the normal driver's refresh and asks that driver to power down
active pumps, checking BUSY before lending the bus. An already-off or sleeping
controller can be reset without interpreting its deep-sleep BUSY level.
Calibration then resets
the controller, sets geometry, invalidates all reference/snapshot assumptions,
and reconstructs both planes for its first image. Exiting waits for the active
job and confirmed power-off before resetting the ordinary driver. A shutdown
failure retains calibration ownership and the paused normal UI. Fault recovery
does not automatically reset a possibly powered controller: use Power Off Pumps
and retry; unresolved power-state failures require hardware recovery.

Full B/W external-LUT preparation and OTP now share CDI=97, PLL=0E, E1=02,
E0=02, E5=1E, geometry and full-screen activation. Their LUT source selection
necessarily differs. Candidate AA retains CDI=D7 and now explicitly uses E0=02,
E5=1E rather than inheriting mode history. PWR/VDCS/BTST are not guessed from
another panel; the path continues to use this module's programmed defaults.

## Modes and recipe

`Waveform B-W Preparation` has three choices:

* Factory OTP: runs the panel's full B/W waveform, whose payload is unavailable.
* Programmable B-W Drive: original inversion -> target -> one hold count;
  uses existing inversion/target/repeat controls and the gray Hold Rail Code.
* Conditioned B-W Drive: activation -> optional white reference -> rest-code interval ->
  inversion/target -> tail-code interval, inside one LUT activation.

Conditioned default, R=1, I=F=20:

| Stage | Black-target pixels | White-target pixels | Modeled counts |
|---|---|---|---:|
| Activation | code 1, then code 2 | code 1, then code 2 | (2+2) x2 |
| Optional common reference | disabled | disabled | 0 |
| Rest | rest code | rest code | 3 |
| First inversion interval | code 2 | code 1 | 20 |
| Target | code 1 | code 2 | 20 |
| Tail | rest code | rest code | 3 |

The default has 54 modeled counts in all five rows. VCOM remains code 0. The
reference defaults off to avoid adding an asymmetric endpoint dose to the
existing 20/20 experiment. Equal source-code counts are not measured DC balance.
When the white reference is enabled, black-target pixels already have their
inverted endpoint, so another white drive is unnecessary on the first cycle.
Later repeats invert both targets.
Shorter black/white target rows receive rest-code padding to match the longest.

These are raw action codes and modeled counts, not measured voltage or time.
Rest source 0 plus VCOM 0 is not certified zero field or active discharge.

## Controls and expected effects

Changes apply on the next explicit image/rebuild operation, never automatically.
The nine new controls are RAM-only to avoid adding preference writes to a device
already reporting NVS exhaustion. Their defaults return after reboot. Existing
controls retain their prior persistence behavior. Export/save settings before
reboot; actual persistence of existing settings remains subject to the NVS issue.

| Control (Waveform B-W prefix) | Default/range | Applies to | Expected effect |
|---|---|---|---|
| Frames | 20; 1–63 | both programmable | Target drive length. More can strengthen incomplete endpoints; excess can worsen texture/drift. The observed useful region is about 15–30 on this unit, not a newly enforced limit. |
| Inversion Frames | 20; 0–63 | both programmable | Longer complementary endpoint drive. In conditioned mode the first black-target interval holds when a white reference is enabled. Zero skips inversion; it is an experiment, not an equivalent reset. |
| Repeats | 1; 1–16 | both programmable | Repeats inversion/target/ending cycle. Activation/reference run once. More increases duration and dose; try 1 before increasing. |
| Activation Frames | 2; 0–8 | conditioned | Width of each dark/light activation pulse. Zero disables activation. Longer may reduce retained-state dependence but adds excursions and may worsen noise. |
| Activation Cycles | 2; 0–4 | conditioned | Number of dark/light pairs. Zero disables activation; higher increases conditioning and flicker. |
| Reference Frames | 0; 0–63 | conditioned | Optional common white drive before the image. Higher may make the reference more reproducible, but adds an asymmetric dose and may worsen texture/drift. Disabled by default; try separately from activation. |
| Rest Frames | 3; 0–16 | conditioned | Source rest-code scans after reference. More gives a longer interval under that electrical code, not a proven discharge. |
| Tail Frames | 3; 0–16 | conditioned | Rest-code scans after each target cycle. Tests whether ending treatment affects drift/noise. Zero removes the tail. |
| White Frames | 0; 0–63 | conditioned | 0 follows Frames; positive values independently set white-target length. Useful for black/white response asymmetry. Shorter row is padded, so total duration follows the longer target. |
| Rest Rail Code | 0; 0–3 | conditioned | Source action during rest, tail and alignment padding. Raw electrical experiment: other codes can drive pixels rather than rest them. |
| VCOM Rail Code | 0; 0–3 | conditioned | Constant VCOM action throughout preparation. Can alter every pixel's effective field; keep 0 as the comparison baseline. Does not change candidate gray VCOM. |
| Pause ms | 0; 0–1000 | all modes, rebuild only | Software wait AFTER B/W BUSY completion and BEFORE gray upload/refresh. This is outside the LUT and does not establish zero field. No effect on standalone image buttons or repeat-only gray pulses. |

The existing A/B/C, opposite-prefix, pulse-repeat, neutral and gray hold controls
still affect the candidate stage. They do not replace the new B/W rest/VCOM
controls. The simple programmable baseline intentionally ignores the new
conditioning controls (apart from the shared optional host pause).

## Image buttons

* Clear To White: all 384,000 visible pixels white, with no labels.
* Clear To Black: all pixels black, with no labels.
* Show Known Image: fixed chart of solid blocks, outlined rectangle, fine/coarse
  stripes, checkerboard, diagonal edge and house silhouette.
* Show Random Edges: new seeded chunky pattern and house silhouette. No random
  per-pixel speckle. The seed appears in the image and Test Image diagnostic.
* Repeat Last B-W Image: exact previous image/seed with the currently selected
  drive mode and tuning. Use this for identical-target A/B comparisons.

Buttons acquire calibration ownership, drive the selected full-screen B/W
recipe, stop before gray settling, and invalidate held calibration references.
The image remains until another explicit operation or exit to the normal UI.
The normal Power Off Pumps button remains available. Pure white/black commands
are full B/W preparation operations, not a single unconditioned rail pulse.

## Timing experiment: what recommendation 2 measures

The transport keeps its most recent 96 diagnostic events in RAM. Press Export
Timing after the operation completes, or enable Detailed Logging to export after
each job. Collection performs no serial logging during a waveform. Binary/hex
LUT export still records the exact accepted source/VCOM payloads separately.

Example (illustrative, not a measured device result):

```
LABTIME t=10000 issue cmd=12 detail=0 poll-gap=0
LABTIME t=10002 busy-on cmd=12 detail=2 poll-gap=2
LABTIME t=10900 busy-off cmd=12 detail=900 poll-gap=1
LABTIME t=10980 issue cmd=12 detail=0 poll-gap=0
```

`t` is milliseconds since boot. `cmd=12` is display refresh, `04` power-on,
`02` power-off. `detail` is elapsed milliseconds since issue for BUSY/timeout
events, transferred byte count for upload events, and packed command data for
write events (the last up to four data bytes). Command times use a fresh device
clock after data upload. BUSY is
observed by polling, not captured by an interrupt; `poll-gap` shows how long
since the preceding BUSY sample. A large gap can hide an edge or entire short
operation, so it weakens timing conclusions. The ring can overwrite older entry
events after several jobs; export promptly after the test of interest.

In the example, the second refresh command is 80 ms after the first observed
BUSY release. That interval contains host upload/scheduling work and any explicit
Pause setting. It is not automatically a LUT rest. The first operation lasted
about 900 ms from command issue to observed completion; an optical B/W plateau
inside that interval may be saturated drive or electrically quiet waveform
phases. BUSY alone cannot distinguish those two.

For video comparison, use the visible initial inversion to align approximate
optical events, record the actual clip playback conversion, and compare the
position of the B/W plateau relative to these two refresh operations. Command
timestamps alone do not share a clock with the camera. Precise alignment requires
one capture recording SPI/DRF and BUSY together, optionally with a visible timing
marker. To establish genuine neutralization, measure source and VCOM together;
neither identical action codes nor a stationary image proves zero field.

Compare first entry, exit/re-entry and cold boot with unchanged settings and the
same target. Then compare Factory OTP, simple programmable and conditioned
programmable. Start with R=1, I/F=20, rest/VCOM=0; vary one conditioning setting
at a time and inspect immediately, after a delay, and after repeated updates.
There is no automatic stress loop.

# X4 waveform lab

## Testing randomized switch timing (v17)

The user reported better inversion with **1 On / 1 Off** than **8 On /
8 Off**, a small improvement with **Distributed** over **Centered**, and
a high-pitched crackle repeating at roughly 10–15 Hz during weak drive.
Good drive produced steady high-pitched noise. The crackle rate is an
estimated repetition rate and the supply cause is still open.

**Strip Pattern** adds two repeatable randomized stimuli for **Load Strip**.
Keep Strip Width, B/W Drive, and the starting image fixed.

- **Jittered** shuffles whether the full strip switches on the first or
  second row transition of each pair. It matches **1 On / 1 Off** in peak
  count and estimated total charge and allows runs of up to two active
  rows. This tests the effect of breaking the regular row cadence.
- **Staggered** randomly schedules two of the four B/W column pairs in each
  byte early, with the other two switching on the next row. It spreads the
  same total charge over every row transition. A few evenly spaced rows
  get one additional pair to match the exact total over 479 transitions.
  At width 320 the peak is 81 switches per rail. Both probes keep the
  original 400/400 black and white counts and the full-height references.

Compare **1 On / 1 Off → Jittered → Staggered**. Clear white and wait before
each image. Compare inversion and whether the crackle occurs. Jittered and
Staggered use a fixed seed and the same bitmap repeats on each waveform
frame. Their source-charge estimates can be compared with the existing
probes before using the result to change the renderer.


## Adjusting texture strength and probing load (v16)

**Bayer** is the unchanged 4×4 ordered-dither control. Around middle gray
it flips the dot pattern between consecutive scan rows. The user observed
failed drive in this control and successful drive with Column Stable and
Row Repair with the split guard off.

- **Column Strength** replaces that fraction of dispersed 4×4 cells with
  column-stable thresholds. 100% is the original Column Stable result.
  Lower values retain more Bayer texture and can increase switching load.
- **Repair Strength** removes that fraction of the excess transitions
  above the local and whole-row repair limits at each step. At 100% it
  enforces the limits from **Rail Budget**. At lower strength the limits
  become soft and the rendered frame can exceed them. Both strengths are
  0–100% and 0% renders the Bayer control bits. The optional split guard
  still applies to these two modes when on.
- **Source Charge** weights source arrivals by the phase in the programmed
  B/W waveform. It counts one unit from modeled midpoint to an active rail
  and two from the opposite active rail. **Both** and **one** show the
  highest interior load per rail in dual-rail and single-rail phases.
  **Boundary** scores entry to or from the padded gates, including phase
  changes. **Local 8/32** is peak weighted load in 128 columns with an
  assumed recovery half-life of 8 or 32 scan rows. **Dark/light total** sums
  the estimated dose from source-line switching across all frames. Split
  jobs report peaks across the individual passes and totals for all passes.
  Factory OTP uses the verified table read from the controller and the
  programmable paths use the selected LUT. These are relative model units.
  VCOM, pixel charging, converter behavior, and physical bank geometry
  are outside this source-line estimate.

**Strip Pattern** changes the stimulus used by **Load Strip**. **Centered**
and **Distributed** keep the same counts per rail on every active row, but
distribute the latter across the space outside the reference bands. **1 On
/ 1 Off** and **8 On / 8 Off** both drive the centered columns on half the
visible row pairs. Their peak and average switching are equal, with
different burst lengths. Keep Strip Width and the waveform fixed. Compare
the early inversion and audio at the same position. The synthetic
recovery scores do not identify a converter or acoustic resonance.


## Comparing B/W textures on the panel (v15)

In **Test Patterns**, select **B/W Texture**, then press **B/W Photo**,
**B/W Checker**, or **B/W Boiler**. These are full B/W tests. The boiler
test uses the most recently committed dashboard frame. Let the normal
dashboard refresh once after boot so that snapshot is available.

- **Bayer** preserves the original bitmap for a single-pass control.
- **Column Stable** uses one ordered luminance threshold per physical
  column. It keeps the checkerboard reference patches and uses the known
  50% tone for their background. It keeps hard endpoints and high-contrast
  edges from the boiler snapshot. Expect a finer or coarser vertical
  texture depending on gray level.
- **Row Repair** spreads omitted transitions along each scan row using an
  integer accumulator. Paired changes are preferentially removed together
  inside eight-column groups to limit tone error. It budgets both rails
  independently, with 32-column caps and an eight-row recovery estimate.
  The boiler preview returns to its exact Bayer bits if a protected edge
  would be altered. The synthetic checker references can show minor edits
  in this mode.

**Rail Budget** defaults to 80. In Row Repair it sets the whole-row cap;
per 32-column tile the instantaneous and recent limits are 12 and 8 at
that setting. For either new texture **Split if Needed** also checks the
candidate's largest 128-column load and recent load across the scan.
The default provisional limits are 80, 60, and 40 respectively. If exceeded,
the existing full-address, masked split path uses a budget of 40 per rail
so that it also fits the recent-load limit. Lower budgets can require more
passes. Split requires Factory Tuned or Factory Replay and prime off.
Turn the guard off for deliberate single-pass A/B comparison.

**Source Load** reports original and rendered peak per rail, peak in a
byte-aligned 128-column span, and peak recent load in that span. These
are visible-row bitmap estimates before any split. **Test Image** reports
the actual texture, a protected-edge fallback, and the number of strips.
Keep the B/W drive settings fixed and **Clear White** before each trial.
Compare inversion, texture, ghosting, and detail. Check any guard fallback
for seams or drift between passes. The normal dashboard rendering and the
gray calibration charts run through their original paths.


## Checking a split update (v14)

The centered v13 sweep rendered cleanly at 160 columns. At 240 the user
reported weaker sound than at 320, better rendering, and less ghosting.
At 320 the checkerboard looked pale during the update and the switching
was clearly audible. At 400 the pattern only appeared in the final
stages and no switching sound was audible. The side references were
unaffected at 320. Use optical inversion and ghosting to assess the
split. Sound alone is an unreliable indicator.

With the saved **Factory Tuned** setup and **Factory Prime** off, run
**Clear White → Checkerboard** as the one-pass control. Then run
**Clear White → Split Checkerboard**, leaving **Rail Budget** at **80**.
The second button draws the same full-width image in five 160-column
passes. **Test Image** reports the budget and number of passes. Wait for
**Result → complete**, which applies to the entire sequence.

Watch for clean inversion in each active strip and for earlier strips
and untouched white areas to stay steady as the next strip refreshes.
Compare the final image for seams and ghosting. The test passes if it
improves the inversion and the other strips remain stable. A lower
Rail Budget makes the strips narrower. Factory Replay can run the same
experiment with its own optical threshold. Factory OTP supports the
single-pass controls and cannot use the split mask.

For each adjacent visible row pair, the planner adds the black-to-white
and white-to-black popcounts independently across byte-aligned columns.
It starts a new strip before either count exceeds the budget. Width is
also limited to twice the budget to account for the modeled half-swing
from idle to a common active rail at scan entry. At budget 80 this means
up to 160 balanced columns or 80 columns for all-one-direction changes.
This is an estimate under the source-voltage model.

Each pass uses full panel addressing. Both DTM bits are zero for hidden
gates and pixels outside the active strip. Their external LUT row has
the same timing as VCOM and requests code 0 throughout. This code has
held references in the grayscale tests. Holding through multiple full B/W
passes still needs optical verification. Normal rendering bypasses the
split diagnostic.


## Measuring the switching limit (v13)

Under **Test Patterns**, set **Strip Width** and then press **Load Strip**.
The default is 320 columns. The range is 0–576 in steps of 16. This varies
the width of a centered one-pixel checkerboard surrounded by vertical
stripes. The two full-height black, B/W-dithered middle tone, and white
reference groups stay fixed. Each visible row has 400 black and 400 white
pixels. Between adjacent visible rows, half the selected columns switch
in each direction. At 320, that is 160 changes into each rail. The hidden
padding transition is common to all widths.

1. Keep **B/W Drive**, **Cold Start**, and **Temp Source** fixed for one
   sweep. Use **Factory Tuned** and the saved power and clock setup first.
2. Start at **0** as a control, then try **240**, **320**, and **400**
   columns (30%, 40%, and 50% of the width). Run **Clear White** and wait
   for completion before each **Load Strip**. After the press, **Calibration
   → Test Image** reports the width that was actually sent.
3. Watch the solid white-target bands turn dark during inversion and the
   black-target bands return dark at the end. Record the largest width that
   consistently gives a strong, even inversion. If all three pass, try
   480 and 576. If 240 fails, try 160. Repeat the two widths that bracket a
   change and narrow the gap in 16-column steps.
4. If desired, repeat the bracket with **Factory OTP** and **Legacy Setup**.
   Exit calibration and re-enter before the first factory trial to start
   from a controller reset. Treat each drive mode's threshold separately.

The experiment uses a full B/W refresh and varies the bitmap only. The
balanced strip shares its switching evenly between the rails. For real
images, count the changes into each rail separately and weight them by the
waveform's voltage swing. An unbalanced line can put all its changes on one
rail, so a passing balanced width is a per-rail count of half that width.
The separately driven update regions still need their own validation.


## Comparing source switching loads (v12)

**Test Patterns → V Stripes** and **Checkerboard** both use one complete
B/W refresh with the selected B/W Drive. Both have 400 black and 400 white
pixels on every visible line. Both have six identical groups of black,
B/W-dithered middle tone, and white patches. The groups are staggered
across three bands so they sample different parts of the screen.

V Stripes repeats the background row. Checkerboard complements every other
background row. Inside each patch band, 608 columns still switch on every
checkerboard row and zero switch on the stripe rows. Between bands the
counts are 800 and zero. Entry and exit rows also contain the transitions
into or out of the patches. The hidden-gate fill is identical in both tests.

Run both under the tuned preset first. If a common optical baseline is
needed, press Clear White before each pattern. Compare the same solid
patches during inversion and after the refresh settles. Look for a broad
band, uneven fill, or visible edges from the earlier image. The middle
patch uses two-pixel stripes, so it renders in the same single B/W pass.

To compare the controller's factory waveform, set **B/W Drive → Factory
OTP** and **Cold Start → Legacy Setup**. Exit calibration, wait for
release, then re-enter or press one of the test buttons to reacquire it.
This resets the controller and avoids running the OTP comparison on pumps
already configured by the programmable power option. Run the same two
buttons. Restore **Factory Tuned** and **Panel Power And Clock** and again
exit/re-enter to return to the saved baseline.

A scan-line count can be derived from XOR and popcount once this comparison
shows whether switching activity affects the defect. Reducing the number of
rows per update shortens the burst but does not bound the maximum number of
sources switching in any line. A per-line bound would require limiting the
participating columns, with full-line addressing and the existing window
constraints still under consideration.


## Checking Linear Ramp inversion (v10)

**Ramp Base** shows the exact B/W preparation target for Linear Ramp, including
both bars and labels, then stops. **Ramp Inverse** sends the bitwise complement
of that same target and stops. Both use a complete B/W waveform, under the
selected B/W Drive, with no gray settle. The inversion view shows what the
fully driven inverse target should look like. It does not freeze or truncate
the normal drive midway.

In IMG_5324, the photographed inverse phase has a vertical band crossing the
two bars and the header. The expected complement has a sharp transition near
mid-screen in the top bar and a gradual B/W transition across the lower bar.
If both new views finish cleanly, compare a normal Linear Ramp using Factory
Tuned and Factory Replay at the same temperature. That leaves the image data
fixed and separates the tuned waveform additions from the factory waveform.
If the band remains in a settled B/W view, compare that view in Factory OTP.
The user observed the ramp artifact in Factory OTP as well as Factory Tuned,
across the cold-start comparison. Source load remains a hypothesis.


## Photographing neighboring shades (v9)

The lab defaults reproduce the user's optically tuned preparation: Factory
Tuned with panel power and clock, shake 1 frame per polarity × 2 pairs, White
Ref. 5, Rest Trim +3, all other B/W trims zero and Factory Prime off. The
relative gray pulses use A/B/C 2/3/4, rail 2, opposite 3, repeats 1, neutral 1,
hold 0. Temperature still uses the normal Boot Sample setting.

- **Gray Tuning → Neighbor Mixes** shows the eight raw references at the top.
  Each of the seven rows names the two adjacent raw shades it uses. Column
  00 is pure darker, 16 pure lighter; the intervening columns put exactly
  that many lighter pixels in a 4×4 Bayer tile. The columns span 800 pixels.
  This chart always shows all duty counts, independent of the current curves.
- **Gray Tuning → Linear Ramp** shows a full-width ramp from A BLACK through
  C WHITE. The upper bar uses the neighboring-gray palette. The lower bar
  uses only black and white at the same nominal input luma for each column.
- **Shade Sequence** sets a provisional order from dark to light. Enter
  the digits 1–6 exactly once (1/2/3 = A/B/C from black, 4/5/6 = A/B/C
  from white). 123456 is provisional. The fixed-identity reference strip
  makes a wrong order visible. Six distinct measured shade values take
  precedence over the provisional sequence.
- **Gray Photo**, **Known Image**, and **Random Edges** use the same
  neighboring-gray renderer. The photo preserves its B/W dither comparison
  via **Dither Photo**. Pure clear buttons still run only the B/W pass.

Photograph the full display on **Neighbor Mixes** first. If its raw references
show a different order, set Shade Sequence and capture the chart again so we
measure the right pairs. Capture **Linear Ramp** with the same locked exposure,
WB, focus, and lighting. Keep the camera and display in the same positions.
Include all screen edges and avoid clipping the white endpoint.

**Driver calibration** is set on the ESPHome Xteink display with two optional
YAML fields. `dither_luminance` takes six distinct 1–254 values in
A/B/C-from-black, then A/B/C-from-white order. The driver sorts these values and
supplies the black and white endpoints. `dither_curves` takes seven entries,
one per adjacent pair in sorted dark-to-light order. Each entry is either an
empty list (linear) or 17 nondecreasing values from 0 to 255 for measured
optical progress at coverage counts 0/16 through 16/16. Equal samples let
visually redundant steps collapse. The renderer picks the closest measured
step, then uses only those two neighboring shades. These fields populate the
rendering library's `Palette::shades` and `Palette::coverage`.

The provisional levels are evenly spaced for diagnostics. Photo measurements
are needed before calibrating the optical mapping or enabling this palette
for the boiler UI.


## Factory replay and mottling experiments (v8)

**B/W Setup → B/W Drive** defaults to **Factory Replay**. It loads five 49-byte
rows from the selected factory table in the panel's verified boot-time MTP
capture. **Factory OTP** invokes the controller's built-in waveform for an
optical comparison. **Factory Tuned** starts with those same bytes and applies
the controls below. The prior simple and conditioned recipes are no longer
selectable in the lab. The default uses the tested Panel Power And Clock setup
and leaves Factory Prime off.

The five supported full B/W tables end at 5, 10, 15, 20 and 80 °C. They share
analogue and clock codes but have different pulse lengths. Their two source
groups use opposite endpoint drive, a four-frame code-0 pause, then the target
with staggered completion. At 21–80 °C the inverse is 25 frames, then pause 4,
then white drives 25 and rests 11 while black drives 14, rests 11 and drives
the final 11. Both endpoints have equal counts in the two opposing source
codes. The firmware checks the expected shape and rejects unsupported tables.

**Temp Source → Boot Sample** uses the controller's experimental TSC reading
captured at boot. **Factory Table** displays the selected temperature and bin
alongside the raw TSC bytes. If the reading fails the plausibility check, it
shows **Fallback 30 C**. **Manual** uses **Manual Temp** for a measured
temperature or controlled A/B and works without reboot. The boot sample is
not a live temperature feed. The custom gray pulses still require their own
temperature validation.

**Factory Tuned** controls:

- **Shake Frames** sets each dark and light pulse length. **Shake Cycles**
  sets the number of pairs. Either at zero skips agitation.
- **White Ref.** adds a common dark pulse and equally long common white pulse
  immediately before the factory sequence. It ends at white and adds equal
  positive/negative source-code counts, avoiding the prior white-only dose.
  Equal code counts do not certify physical charge balance. Zero skips it.
- **Drive Trim** adds frames to both inversion and the first target phase.
  It tests whether a longer drive improves endpoint definition without
  changing the short final phase or the balance in opposing code counts.
- **Rest Trim** changes the four-frame post-inversion pause. Zero leaves it
  at four. It tests whether resting longer reduces texture or ghosting.
- **Pause (ms)** remains the host wait between B/W completion and gray. It
  does not alter the B/W waveform.

Start with **Known Image**, **Clear White**, and **Clear Black** under Factory
Replay. Compare Factory OTP at the same temperature. For mottling, switch to
Factory Tuned, leave both trims at zero, and compare shake alone (2 frames ×
2 cycles), White Ref. alone (start with 6 frames), and both together. Keep
temperature selection and images fixed. Once the white background is reliable,
rebuild the gray chart and record fresh readings rather than importing old
shade measurements.

## Earlier calibration rounds (v4–v7)

The controls below document the earlier comparisons. Use the v8 names and defaults above for current tests.


This round isolates controller ownership/setup from B/W waveform experiments.
Optical improvement and the reported re-entry failure still need physical
confirmation. Candidate gray parameters and the production boiler layout are
preserved. The single-settle modes remain gated pending measured calibration.

In Home Assistant, the controls are grouped into five ESPHome sub-devices.
**Calibration** owns the session and shows its status. **B/W Setup** contains
power and B/W preparation. **Gray Tuning** contains the chart, candidate
controls and measurements. **Test Patterns** holds the clears and images.
**Waveform Data** contains waveform readouts and exports.

This migration changes the IDs of the lab-only entities. Update any dashboard
or automation that references an earlier lab entity. The original boiler
entities keep their names and device assignment.

## Deterministic ownership

Acquisition drains the normal driver's refresh and asks that driver to power down
active pumps, checking BUSY before lending the bus. An already-off or sleeping
controller can be reset without interpreting its deep-sleep BUSY level.
Calibration then resets
the controller, sets geometry, invalidates all reference/snapshot assumptions,
and reconstructs both planes for its first image. Exiting waits for the active
job and confirmed power-off before resetting the ordinary driver. A shutdown
failure retains calibration ownership and the paused normal UI. Fault recovery
does not automatically reset a possibly powered controller: use Pumps Off
and retry; unresolved power-state failures require hardware recovery.

Full B/W external-LUT preparation and OTP now share CDI=97, PLL=0E, E1=02,
E0=02, E5=1E, geometry and full-screen activation. Their LUT source selection
necessarily differs. Candidate AA retains CDI=D7 and now explicitly uses E0=02,
E5=1E rather than inheriting mode history. With Legacy Setup, PWR/VDCS/BTST use the controller defaults. The optional
explicit power setup is described below.

## Modes and recipe

`B/W Drive` has three choices:

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
All waveform tuning controls and the preparation selection are RAM-only while
the device reports NVS exhaustion. The old saved values restored hold=3 and
A/B/C=2/4/6 after reboot, invalidating comparisons. Boot defaults now use the
user's recorded A/B/C=2/3/4, opposite=3, rail=2, repeat=1, neutral=1, hold=0;
B/W inversion/target=20/20, repeat=1. Conditioned preparation is selected, with
factory priming OFF. Export/save experimental changes before reboot; they are
kept across exit/re-entry without reboot. No device NVS erase is performed.

| Control in B/W Setup | Default/range | Applies to | Expected effect |
|---|---|---|---|
| Target Frames | 20; 1–63 | both programmable | Target drive length. More can strengthen incomplete endpoints; excess can worsen texture/drift. The observed useful region is about 15–30 on this unit, not a newly enforced limit. |
| Inverse Frames | 20; 0–63 | both programmable | Longer complementary endpoint drive. In conditioned mode the first black-target interval holds when a white reference is enabled. Zero skips inversion; it is an experiment, not an equivalent reset. |
| Drive Repeats | 1; 1–16 | both programmable | Repeats inversion/target/ending cycle. Activation/reference run once. More increases duration and dose; try 1 before increasing. |
| Shake Frames | 2; 0–8 | conditioned | Width of each dark/light activation pulse. Zero disables activation. Longer may reduce retained-state dependence but adds excursions and may worsen noise. |
| Shake Cycles | 2; 0–4 | conditioned | Number of dark/light pairs. Zero disables activation; higher increases conditioning and flicker. |
| White Ref. | 0; 0–63 | conditioned | Optional common white drive before the image. Higher may make the reference more reproducible, but adds an asymmetric dose and may worsen texture/drift. Disabled by default; try separately from activation. |
| Rest Frames | 3; 0–16 | conditioned | Source rest-code scans after reference. More gives a longer interval under that electrical code, not a proven discharge. |
| Tail Frames | 3; 0–16 | conditioned | Rest-code scans after each target cycle. Tests whether ending treatment affects drift/noise. Zero removes the tail. |
| White Target | 0; 0–63 | conditioned | 0 follows Frames; positive values independently set white-target length. Useful for black/white response asymmetry. Shorter row is padded, so total duration follows the longer target. |
| Rest Code | 0; 0–3 | conditioned | Source action during rest, tail and alignment padding. Raw electrical experiment: other codes can drive pixels rather than rest them. |
| VCOM Code | 0; 0–3 | conditioned | Constant VCOM action throughout preparation. Can alter every pixel's effective field; keep 0 as the comparison baseline. Does not change candidate gray VCOM. |
| Pause (ms) | 0; 0–1000 | all modes, rebuild only | Software wait AFTER B/W BUSY completion and BEFORE gray upload/refresh. This is outside the LUT and does not establish zero field. No effect on standalone image buttons or repeat-only gray pulses. |

The existing A/B/C, opposite-prefix, pulse-repeat, neutral and gray hold controls
still affect the candidate stage. They do not replace the new B/W rest/VCOM
controls. The simple programmable baseline intentionally ignores the new
conditioning controls (apart from the shared optional host pause).

## Image buttons

* Clear White: all 384,000 visible pixels white, with no labels.
* Clear Black: all pixels black, with no labels.
* Known Image: fixed chart of solid blocks, outlined rectangle, fine/coarse
  stripes, checkerboard, diagonal edge and house silhouette.
* Random Edges: new seeded chunky pattern and house silhouette. No random
  per-pixel speckle. The seed appears in the image and Test Image diagnostic.
* Repeat Image: exact previous image/seed with the currently selected
  drive mode and tuning. Use this for identical-target A/B comparisons.

Buttons acquire calibration ownership, drive the selected full-screen B/W
recipe, stop before gray settling, and invalidate held calibration references.
The image remains until another explicit operation or exit to the normal UI.
The normal Pumps Off button remains available. Pure white/black commands
are full B/W preparation operations, not a single unconditioned rail pulse.

## Timing experiment: what recommendation 2 measures

The transport keeps its most recent 96 diagnostic events in RAM. Press Export
Timing after the operation completes, or enable Detailed Logs to export after
each job. A frozen snapshot is exported one record per 50 ms, with BEGIN/END
markers (up to about five seconds); export pauses during a new waveform. This
avoids the dropped records seen with a burst export. Collection performs no
serial logging during a waveform. Binary/hex
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

## Cold-start correction and landscape targets

The user's IMG_5323 video shows a clear known image within a warmed calibration
session, a clean return to the boiler UI, then weak overlays after re-entry.
The same conditioned waveform working immediately after factory OTP strongly
suggests an initialization dependency; it does not establish a particular missing
voltage register. Earlier API completion checks did not prove optical clearing.

This revision removes an independent DTM assumption: both programmable B/W
profiles now drive all four selector rows according to NEW, even if OLD is not
what the host expected. The B/W OLD plane is the complement of NEW to force a
transition, matching the ordinary driver's scrub. All operations upload NEW
first and OLD last, and hidden B/W gates are white like the ordinary driver.
Candidate selectors still explicitly upload both planes.

Power-on selects OTP mode before PON. After PON completes, the transport
reasserts KW mode, geometry and data setup before either plane upload. It only
selects the external LUT immediately before that refresh. No drive-voltage
values are invented or copied from a different panel.

**Factory Prime** is a separate switch, OFF by default. When ON,
the first programmable B/W preparation after reset or power-off runs an actual
factory OTP refresh of that target, rewrites both planes, then runs the selected
programmable recipe without reset/power-cycle. It reproduces the user's proven
warm sequence. A factory B/W refresh within the session also marks it primed.
Turning the switch OFF does not undo settings/physical conditioning already
established by OTP; exit/re-enter for a cold comparison. The primer is an extra
refresh, distinct from the inversion phases inside the programmable LUT. It can
change physical conditioning as well as controller configuration, so success
with priming alone does not prove an analog-register cause.

**Dither Photo** renders the preserved grayscale photograph through
the B/W ordered quantizer. **Gray Photo** adds the current A/B/C
candidate doses using eight provisional input buckets, three from black and
three from white plus the endpoints. Its title labels the tones unmeasured.
This is an experimental photo target, not a calibrated palette or enabled
production grayscale mode. Both use full-screen preparation; the gray trial
uses full-screen selector upload/activation and preserves its B/W title/credit.
The photo is Ansel Adams's public-domain *The Tetons - Snake River* (NARA 519904);
the source and asset-generation details are preserved in the rendering library.


### Factory-memory diagnostic (v6)

The report that only an actual factory OTP refresh enables strong programmable
B/W is still unresolved. Selecting OTP during PON alone did not resolve it.
Factory Prime remains a workaround, not a verified analogue initialization fix.

`capture_panel_mtp: true` (X4 Pro only) captures the first 4096 bytes using the
controller's existing read-only RMTP command before the SPI driver starts at boot.
It reads twice and marks the capture verified only if both streams match, the
48-byte probe header agrees, and the refresh-enable key is A5. No programming
command, voltage change or display refresh is part of this capture.

**Export Factory** exports that saved boot snapshot in
32-byte chunks, with byte offsets, a verification flag, FNV-1a checksum and an END
marker. Pressing the button does not access the panel bus or change the screen.
Exports pause during a calibration operation. An unverified capture is diagnostic
raw data and must not be used to choose drive settings. A verified read still
requires a matching controller memory map before interpreting voltage fields.


### Cold-start power setup A/B (v7)

The previous setup's cold programmable B/W remains optically unsuccessful; only
an actual factory OTP refresh has been confirmed to make it work. The v5
OTP-selection-during-PON change and DTM corrections did not remove that
requirement. No successful software command count is evidence of optical clearing.

The v6 diagnostic obtained two identical full 4096-byte RMTP reads from this unit
(VER `00 0F 68 00 00`), matching the probe header and export checksum. Eight
populated temperature tables begin at 0x33 with stride 0x121. Their boundaries are
5, 10, 15, 20, 80, 95, 100, 127. The existing forced temperature of 30 selects
range 4, at 0x4B7, whose nine-byte prefix is `09 00 78 78 14 24 00 00 00`.

The interpretation of that prefix is experimental. Its shape agrees with the
X4 vendor external blob; the PWR/VDCS payload layout and full-drive enable byte
0x43 follow the SDK's recovered UC8279 initializer. These are opaque register
codes, not measured rail voltages. The implementation accepts only this observed
module signature, memory shape and values; unfamiliar/unverified data disables the
experiment. No voltage sweep or user-entered raw power values are provided.

**Cold Start** has three choices:

| Choice | Behavior | Desired / expected diagnostic effect |
|---|---|---|
| Legacy Setup | Existing startup, no explicit PWR/VDCS/BTST writes; custom PLL 0E | Reproduces the cold-start weakness as the control. An actual OTP prime can still warm it. |
| Panel Power - Experimental | Before PON, write PWR `43 00 78 78 14`, VDCS `24`, BTST `25 25 3C`; keep custom PLL `0E`; use external-LUT mode for PON | Tests whether supplying the missing analogue setup restores black/white saturation without an OTP refresh. Keeping the clock and pulse counts unchanged reduces one source of ambiguity. If successful, transitions should become stronger, pure clears should approach full endpoints, and the prior image should stop blending through. Noise can change too. |
| Panel Power And Clock - Experimental | Same explicit power setup, plus PLL `09` from the apparent factory table header, reasserted after PON and before refresh | Adds a clock comparison. The duration/energy of a counted frame can change, so the same frame count can give different contrast, noise and total duration. Exact Hz and the direction of the duration change have not been measured on this controller. |

The selector changes only the next OFF-to-PON startup. It never changes voltage
or clock underneath a powered waveform. **Applied Power** reports
the latched setup and its raw payload, or that a selection is pending/rejected.
The default is Legacy Setup on each reboot. Power setup and factory priming are
independent controls; priming can hide whether the explicit setup fixed cold start.

For a useful comparison:

1. Keep the current B/W timing and conditioning controls fixed. Set **Factory Prime** off.
2. Select **Panel Power - Experimental**. Exit calibration, wait for exit to
   finish, and enter it again. This resets the controller and removes the warm
   factory state. Verify Applied Power after the next operation.
3. Show the known image, then clear white and clear black. Report whether each
   reaches its endpoint, whether old edges remain, and whether noise changes.
4. Repeat the exit/re-enter sequence with **Panel Power And Clock - Experimental**.
   Keep the frame counts unchanged for this first clock comparison; record
   refresh duration alongside appearance.
5. For the control, repeat with Legacy Setup, still with priming OFF. If needed,
   re-enable Factory Prime to return to the already-working test path.

Do not treat changing the selector in a warm session as a cold-start test. Do not
select Factory OTP between entering calibration and running the programmable
comparison. Physical confirmation is still required for both new setup choices.

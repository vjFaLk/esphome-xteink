# Minute wakes on the X4 Pro

The [boiler wake package](../packages/x4_pro_minute_wake.yaml) schedules the
next wake on the minute boundary. A timer wake with a known Overview or
OFFLINE screen and valid time refreshes just the clock area on minutes 1–4
of each five-minute period. On minute 0 it enables Wi-Fi and updates the Overview from
Home Assistant. A user-button wake starts the interactive path.

## Region updates

Use `select_region(x, y, width, height)` in physical panel coordinates,
provide the contents of that rectangle, then let LVGL call `update()` at
the end of its draw or call `update()` after painting directly. A selection
requests one absolute update. All pixels in the rectangle are driven,
including its background, so an old glyph is erased even when the glyph
gets narrower. Use `region_update_finished()` and
`region_update_succeeded()` to check completion. Call `clear_region()`
to resume normal rendering.

The controller can sleep and lose its RAM. Each regional pass writes both
planes for the selected rows and supplies a hold selector for the rest of
those scan lines. The call leaves whole-frame history unknown. The next
ordinary update reconstructs it. No display planes or image buffers are
retained between boots. Pass `true` as the fifth argument to select a
single B/W pass. The boiler uses that path with a 1-bit clock font.

## Boiler wiring

The package assumes the boiler's `x4_display`, `x4_battery`, `wifi_client`,
`ha_time`, `overview_page`, `clock_label`, `offline_clock_label`, and the existing input, sleep,
and Overview synchronization scripts. It replaces the old timer,
retained-frame bootstrap, and polling scripts. The clock rectangle
(636, 10, 76, 34) covers the boiler Overview label and its white
background. Adjust it and bump the RTC layout tag if the page layout
changes.

Set the local `boiler_ha_host` substitution to the Home Assistant IP and
keep API encryption enabled. The ESP initiates the native API connection
as soon as Wi-Fi is available. Home Assistant 2026.10 or later needs an
existing encrypted ESPHome entry for this device and must accept TCP
connections from it on port 6054. A boot-time panel gate keeps the previous
image visible while Home Assistant responds. Fresh time, battery, both
boiler temperatures, and the mixer state release the Overview. Auxiliary
controls appear when their own states are available. A 14-second deadline
allows the first reconnect retry and shows OFFLINE with a B/W pass
if the required snapshot is incomplete.
Keep `wifi.enable_on_boot: false`,
`retain_display_controller: false`, and the board's configured battery
latch for timer wakes. The ESP32-S3 RTC keeps system time during deep sleep.
See [ESP-IDF system time](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/system_time.html)
and [ESPHome outgoing API connections](https://esphome.io/components/api/#outgoing-connections).

The RTC layout marker is written only before an orderly sleep with
Overview or OFFLINE visible. The RTC also keeps the intended wake minute and the
last clock minute drawn. On the panel, the RTC wall clock jumped after
deep sleep. A timer wake uses its programmed deadline when the RTC differs
from it and skips duplicate clock writes. Each network wake gets fresh
time together with the boiler states. A cold boot or a failed update
recovers through a full snapshot. The `boiler` log reports the most recent
clock write and the snapshot timing.

On the boiler display, the 18:35–18:40 verification cycle recorded four
clock-only wakes with Wi-Fi disabled. The last clock update sent 6,800
bytes and completed 3.43 seconds after boot. The 18:35 Overview received
its Home Assistant snapshot 6.61 seconds after boot and finished before
the device went back to sleep.

# Clock-only wakes on the X4 Pro

A UC8279 panel keeps its visible image during deep sleep. An absolute update can repaint a clock or status rectangle using the new pixels for that rectangle. This is useful for dashboards that show time more often than they need network data.

## Use the regional display API

| Method | Contract |
| --- | --- |
| `select_region(x, y, width, height, bw_only = false)` | Selects one absolute update in physical 800 × 480 coordinates. Returns `false` if the rectangle or current driver state cannot accept it. Pass `true` for a B/W pass. |
| `update()` | Submits the selected region after its content is drawn. For LVGL, use its normal draw completion instead of calling this manually. |
| `region_update_finished()`, `region_update_succeeded()` | Distinguish an in-progress transaction, completion, and failure. |
| `last_region_bytes()` | Reports the plane payload for the most recent regional drive. |
| `clear_region()` | Returns to ordinary rendering and requests a full baseline on the next draw. |

Select the rectangle before invalidating its LVGL widget or drawing with a lambda. Paint the entire rectangle, including its background, so a shortened label erases its old trailing pixels. Wait for successful completion before entering deep sleep. The driver uses hold outside the rectangle and writes the controller's full-width rows for that band. It discards whole-frame history after a regional update, so the next ordinary refresh reconstructs the visible baseline.

## Adapt the scheduling example

[`packages/x4_pro_minute_wake.yaml`](../packages/x4_pro_minute_wake.yaml) demonstrates this pattern in a dashboard. It is application wiring. Use the [standalone LVGL example](../examples/x4_pro_lvgl.yaml) to bring up a new board first, then copy or adapt the scheduling scripts for your own entities and layout.

| Wake | Example behavior |
| --- | --- |
| Minute 1–4 of each five-minute block | Refresh the clock with Wi-Fi off when time is valid. |
| Minute 0 | Enable Wi-Fi and get a fresh Home Assistant snapshot. Keep the previous page visible until the snapshot arrives. |
| Timed network poll expires | Draw `OFFLINE` and the current clock in the header. Leave the previous readings visible. The next successful snapshot clears the message. |
| Physical button | Start the interactive path. |

1. Give the display a configured `rendering_mode`, keep LVGL paused at boot, and set `resume_on_input: false`. Start `boot_sequence` in a late boot automation after the widgets and inputs exist. Keep the panel-write gate closed until the selected view has real data or the network deadline has expired.
2. Adapt external IDs in the package to your application. The example uses `x4_display`, `x4_battery`, `battery_level`, `wifi_client`, `ha_time`, `x4_frontlight`, and the `ota_hold` maintenance switch. It references `overview_page`, `clock_label`, `offline_overlay`, two temperature sensors, a mixer state, interactive wake flags, and the application's `sync_overview_page`, `interactive_sleep`, and `frontlight_off_timer` scripts. You can replace the required input predicate with your own snapshot contract. Recheck it as each required value arrives.
3. Set the clock and timeout-banner rectangles to your physical layout. The example uses `(636, 10, 76, 34)` for the clock and `(280, 8, 432, 38)` for the header. Use antialiased fonts in a gray mode and 1-bit fonts for B/W text. Update the RTC layout tag if the visible layout changes.
4. Keep `wifi.enable_on_boot: false` for the clock-only wakes and `xteink.retain_display_controller: false`. Keep the board's battery power path configured for timer wake. Retain RTC time and a small amount of scheduling state. The example stores a scheduled deadline and skips duplicate clock transactions if the clock shifts across sleep.

For the outgoing ESPHome API connection, set `boiler_ha_host` to the Home Assistant host and enable API encryption. Register the device in Home Assistant while it is awake. Home Assistant 2026.10 or later accepts the outgoing connection for an existing encrypted ESPHome entry when TCP port 6054 is reachable. The example dials with zero delay, renders as soon as the required states arrive, and uses a 14-second failure deadline. [ESPHome outgoing connections](https://esphome.io/components/api/#outgoing-connections)

Use the `boiler` log to see data readiness, the last clock transfer, and whether Wi-Fi remained disabled. Check the retained image on the panel as well as the log, especially when the layout, panel, or waveform changes.

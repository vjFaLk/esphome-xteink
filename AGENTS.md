# Working on esphome-xteink

This repository adapts Xteink readers to ESPHome. Start with [README.md](README.md), the [X4 Pro rendering guide](docs/x4-waveform-lab.md), and, for sleep changes, the [minute-wake guide](docs/x4-minute-wake.md). Use the checked-out source and dependency lock to resolve discrepancies with historical notes.

## Ownership

- `components/xteink/` owns ESPHome schemas and code generation, LVGL input and display integration, and arbitration between rendering, user interaction, and sleep.
- `packages/` owns the board settings and an application-specific minute-wake example. `examples/` contains standalone configurations used for validation. Keep site-specific scripts and Home Assistant entity mappings in the application.
- [The rendering overlay](components/xteink/dependencies/rendering/README.md) owns frame history, profiles, dithering, waveform planning, drive budgets, and C++ calibration tools. Put new calibrated values and algorithms there. The ESPHome component selects a mode and forwards optional profiles.
- FreeInk owns board detection, panel factory data, SPI, power sequencing, BUSY, and the UC8279 transport. Read the appropriate submodule's instructions before changing its source. Make dependency fixes in its repository, then update the integration pin.

## Display invariants

- Keep LVGL's persistent target image across dirty-region draws. Clear only when the writer owns the areas being cleared. Check RGB luminance and polarity through the actual `draw_pixels_at` path. ESPHome's monochrome `COLOR_ON` convention differs from RGB luminance.
- Let LVGL's built-in draw completion submit updates. Keep `update_interval: never` for that integration. A separate timer or forced update can send a partially composed frame. Test the normal path with a full draw and a changed widget.
- When a boot snapshot is required, keep the physical output gated in addition to pausing LVGL. Gate on the required entity values and fresh time. API connection alone does not provide a complete page. Leave `resume_on_input: false` for explicitly controlled wake flows.
- Set a fixed `rendering_mode` in YAML for normal use. Preserve the factory-table read for factory-derived modes and absolute updates on the UC8279 X4 Pro. It is a transport prerequisite independent of Home Assistant diagnostics. Use C++ firmware for new calibration work.
- Acquire one exclusive controller session for each custom drive. Treat the host frame buffers, controller RAM, and visible panel as separate states. Commit history after all passes and confirmed power-off. On interrupted writes, require a fresh baseline.
- Regional updates use physical coordinates, include the full content of the requested rectangle, and run with full-width scan lines. Keep cropped uploads and activation in the same band. Preserve the select/draw/submit sequence and the completion query before sleep. Regional commits leave whole-frame history unknown.
- Touch preview is transient. Keep its dirty area until the selected gray mode settles it. Preserve page changes and user-wake arbitration when altering that path.

## Sleep and Home Assistant

- Distinguish radio-off minute wakes, network polls, and physical wakes. Keep offline status confined to the header on a timed failure. Waiting for required states must leave the previous image visible. At wake completion, check transaction state before cutting controller power.
- Keep RTC data small. Store time or schedule bookkeeping as needed, and rebuild frame planes after sleep. When changing the page layout, check the physical clock and header coordinates and the layout marker.
- Distinguish an initiated ESPHome connection, authentication, receipt of required states, and a completed panel write when reporting results. A timing measurement should state which event it measures. Native API logs and optical observations provide different evidence.
- Review board-specific power and wake pins before generalizing a change. The base X4's battery latch differs from the X4 Pro's. Keep first-time USB installation guidance separate from OTA instructions.

## Update dependencies

1. Check the integration's current branch and local modifications. Change FreeInk or the rendering overlay in an isolated checkout if the pinned version needs a fix.
2. Commit and make the dependency revision reachable before referencing it. Update the submodule gitlink and matching revision in `components/xteink/dependencies.json` together. Keep the other pinned dependency fixed unless the task changes it.
3. Run `git submodule update --init --recursive`, then `git submodule status --recursive`. Check that the two pinned revisions match what code generation sees. If testing a remote install, fetch the same revision for the board package and the external component.
4. Validate from both the local checkout and the remote package path if changing distribution, dependency resolution, or YAML schemas. The staged SDK directory is build output. Fix its sources or code generation.

## Validate changes

Compile the affected board examples. For a broad integration or dependency change, use the same matrix as CI.

```sh
esphome compile examples/x4.yaml
esphome compile examples/x3.yaml
esphome compile examples/x4_pro.yaml
esphome compile examples/x4_pro_lvgl.yaml
```

Run the host suites relevant to rendering, transport, and panel detection.

```sh
python3 components/xteink/dependencies/rendering/test/run.py
python3 components/xteink/dependencies/freeink/libs/display/FreeInkDisplay/test/host/run_x4_transport.py
python3 components/xteink/dependencies/freeink/libs/display/FreeInkDisplay/test/host/run_pro.py
python3 components/xteink/dependencies/freeink/libs/hardware/XteinkDetect/test/host/run_probe.py
```

Check `git diff --check` and the submodule revisions before committing. Configuration validation, compilation, host tests, OTA, and physical panel checks each establish different things. Report the exact scope that passed. For hardware regressions, include the board/controller, firmware build, refresh mode, temperature if relevant, and what was observed on the panel.

## Public changes and handoffs

- Use reproducible examples with neutral names and placeholders. Keep tokens, local filesystem paths, private addresses, entity mappings, and unpublished hardware captures out of the repository.
- Confirm the intended physical device before OTA or USB flashing, and obtain authorization for live Home Assistant or hardware changes. A documentation or host-validation task can finish using source and local builds.
- Keep protocol experiments in historical notes with their revision and evidence. Update user-facing docs when the supported workflow changes. In the handoff include commits, dependency pins, test results, and any physical checks still open.

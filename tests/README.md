# Performance regression checks

Build with MSYS2 UCRT64 g++ and windres on PATH:

    powershell -File .\build.ps1 -OutputPath SysMon.optimized.exe -RunTests

The tests run without opening the app or modifying settings:

- background_sampler_test checks that blocked collection cannot block reads, pending requests coalesce, published snapshots stay immutable, collection errors can recover, and shutdown joins the worker.
- monitoring_smoke exercises live Windows statistics, process lists, executable metadata, forced hardware refresh, and shutdown over 40 refreshes. It fails if live samples or system metadata never arrive.
- render_smoke draws all 11 pages/views plus scrolled Settings offscreen at 100%, 175%, 1920-pixel maximized, and 3440-pixel ultrawide layouts. It prints timings and saves layout PNGs for visual inspection. Timings are diagnostic, not fixed pass/fail thresholds.

Measured on the local machine during this change:

| Check | Before | After |
| --- | ---: | ---: |
| Slowest UI-side monitoring refresh (40 samples) | 219.17 ms | 0.33 ms |
| Median normal-size page rendering, range across 11 views | 282–455 ms | 5–18 ms |

Rendering comparisons isolate the image-cache optimization; both rendering runs use the new background collectors. Cold image-cache frames can still cost more. These are local offscreen measurements, not an end-to-end input-latency guarantee.

## General Settings

The General Settings test uses tests/SysMon.ini and restores it afterward; it does not change the app's real preferences or register a startup task. Run it in a normal desktop session to verify Task Scheduler and notification-area integration. Restricted runners may print UNVERIFIED for those integrations.

Checks cover saved General Settings, defaults and invalid values, dark/light palette selection, release version comparisons and HTTP error states, Windows validation of the per-user elevated logon task XML, tray minimize/close/restore, and the selected monitoring interval after restore. It also makes a read-only request to the public GitHub latest-release endpoint.

Theme supports Dark, Light, and Follow Windows. English is the sole supported language and is displayed as a fixed value. Automatic updates check on enable/startup and daily afterward (hourly retries after errors), notify for newer published releases, and never install automatically. Startup uses an interactive per-user logon task because the app requires administrator privileges; it does not store a password. An actual sign-out/logon test remains manual.

Pass light to render_smoke.exe to exercise the light theme. The render test saves Dashboard, Processes, and General Settings images at enlarged scale for visual inspection.

- responsive_layout_test checks full client-width allocation, bounded vertical scale, and mouse-coordinate round trips across all page families at 1190, 1920, 2560, and 3440 pixels wide. It creates hidden test windows without running app startup.

## Full Settings audit

settings_runtime_test checks Celsius/Fahrenheit, Mbps/MB/s/Kbps, missing sensors, threshold hysteresis, background log flushing, and retention that preserves unrelated files. General Settings tests also cover every persisted category, valid backup restore, and rejection of malformed backups. responsive_layout_test drives the actual Settings mouse handlers for compact-mode toggling and slider click/drag at four window sizes.

The app now logs once per second when enabled, continues monitoring when minimized, and evaluates alerts with hysteresis. Anonymous telemetry is explicitly unavailable. Support actions are local (folder, report template, bundled guide). Quick Tab Previews names the existing navigation-preview behavior accurately.

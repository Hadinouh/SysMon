# Changelog
## [1.0.0] - 2026-09-23

### Reliability
- Improved shutdown and sensor-helper cleanup.
- Added duplicate-instance prevention and Pause/Resume Monitoring.
- Clarified close-to-tray behavior and expanded tray controls.

### Features
- Added four customizable overlay styles: Precision Rings, Telemetry Stack, Side Rail and Floating Tiles.
- Added independent overlay metrics, optional backgrounds, opacity, scaling, always-on-top and remembered monitor positions.
- Added reversible management of supported current-user startup entries.
- Added settings validation, section resets, tooltips and sensor status.
- Improved local diagnostics, support reports and update checking.

### Interface
- Improved Settings dropdowns and responsive layouts.
- Fixed GPU and Network View All buttons.
- Added higher-resolution process icons and active-column sorting arrows.
- Corrected sidebar icons and added the taskbar logo.
- Simplified System Healthy to a green dot and label.

### Distribution
- Organized all 28 PNG assets into the logos folder.
- Added licensing, dependency notices, privacy, security and asset documentation.
- Included corresponding third-party source material.
- Removed debug symbols, local build paths and saved user settings from the release package.

This Windows x64 release is unsigned.
All notable SysMon changes are documented here.

## [1.0.0-rc7] - 2026-09-23

- Highlight the active Running Processes sort column and show its ascending/descending arrow, including during scrolling.

## [1.0.0-rc6] - 2026-09-23

- Display System Healthy as a green dot and text without a background or border.
- Move all 28 application PNG assets into logos/ and update loading, icon generation and packaging paths.

## [1.0.0-rc5] - 2026-09-23

- Load higher-resolution executable icons for process details and scaled process rows.
- Share icon resolutions across instances of the same application; retain background extraction and cached-only scrolling.

## [1.0.0-rc4] - 2026-09-23

- Correct GPU and Network View All hitboxes by accounting for the content viewport's vertical offset.
- Add regression checks for the visible buttons and reject clicks in the former invisible hitboxes.
- Replace sidebar PNG symbols with original vector icons representing their labels, including CPU, RAM, GPU and temperature.

## [1.0.0-rc3] - 2026-09-23

- Add MIT licensing for original code, privacy/security documentation and an asset provenance checklist.
- Expand dependency notices with exact versions, upstream licenses and corresponding source archives.
- Disable Release debug symbols in the helper and vendored sensor library; reject PDBs and local build paths during packaging.
- Stop tracking the compiled executable; retain portable binaries in release packages.
- Update v1.0 documentation and overlay previews. The candidate remains unsigned.

## [1.0.0-rc1] - 2026-09-22

- Tie the sensor helper to the parent process and explicitly clean up tray, overlay, timers and workers.
- Add a bounded shutdown fallback and prevent duplicate application instances.
- Separate overlay appearance from metric selection; remember monitor positions and add scale, opacity and topmost settings.
- Add pause/resume, useful tray actions, keyboard shortcuts and first-run guidance.
- Manage current-user Run entries reversibly and preserve original registry value data.
- Validate settings, reset individual sections, explain settings with tooltips and expose sensor status.
- Add local diagnostics, privacy-conscious support reports, manual update details and dependency notices.
- Add per-monitor DPI awareness, EXE version metadata and an unsigned portable release candidate.
- Verify lifecycle, settings, startup restoration and dark/light rendering; see V1.0-VERIFICATION.md for remaining acceptance limits.

## [0.9.0] - 2026-09-20

### Monitoring
- Added multi-GPU performance monitoring with GPU selection.
- Added live network monitoring with adapter selection and transfer history.
- Added hardware temperature monitoring through the SysMonSensors helper and LibreHardwareMonitor.
- Added CPU, GPU, motherboard/firmware temperature views and sensor history where supported.
- Expanded dashboard hardware statistics.

### Processes
- Expanded process filtering and grouping.
- Added richer process metadata and executable-path details.
- Added Open File Location, End Process, and priority controls.
- Added background process sampling and metadata caching to reduce UI stalls.

### Settings and integration
- Added a complete Settings page.
- Added Start with Windows, minimize-to-tray, update-checking, and tray preferences.
- Added configurable monitoring intervals, temperature units, and network units.
- Added Dark, Light, and Follow Windows themes.
- Added accent-color selection, transparency, Compact Mode, font sizing, and quick tab previews.
- Added CPU/GPU temperature, disk-usage, and memory-usage alert thresholds.
- Added desktop notifications, alert sounds, alert logging, monitoring-data logging, and retention controls.
- Added settings export/import/reset.
- Added the desktop CPU/RAM overlay preference and Ctrl+Alt+O hotkey.
- Added Task Scheduler based startup integration and .sysmonlog association support.
- Added GitHub Releases update checking.

### Tools and support
- Added the Tools page with Windows maintenance and diagnostic shortcuts.
- Added quick actions for temporary files, DNS, networking, Recycle Bin, and system snapshots.
- Added Data Folder, Support Report, and Documentation actions.
- Expanded System Info and connected-device details.

### UI and performance
- Added responsive resizing and improved maximized/full-screen layouts.
- Added responsive hitboxes and scrolling behavior.
- Added fast navigation previews.
- Improved minimize/restore and tray-icon recovery behavior.
- Improved desktop-widget movement, persistence, and overlay behavior.
- Added background-sampling and rendering/cache optimizations.

### Build and release
- Centralized the application version in `Version.h`.
- Added Windows file/product version metadata.
- Added `build.ps1` for repeatable native builds.
- Added `package-release.ps1` for Windows x64 release packaging.
- Added a clean release layout for the SysMonSensors helper.
- Added/updated smoke, settings, background-sampler, and responsive-layout tests.

## [0.8.0]
- Added the System Info / About PC page.
- Added detailed Windows, CPU, memory, graphics, storage, network, motherboard, BIOS, and connected-device information.
- Added connected-device selection and detailed Plug and Play information.
- Added System Info scrolling and automatic hardware refresh.

## [0.7.0]
- Redesigned the Performance page.
- Added detailed CPU, memory, and physical-disk monitoring.
- Added multiple-disk selection and storage-type identification.

## [0.6]
- Added advanced process monitoring, sorting, search/filtering, selection, and process termination safeguards.

## [0.5]
- Redesigned the dashboard with sidebar navigation and added the Processes page.

## [0.4]
- Added live CPU/RAM history graphs and double-buffered rendering.

## [0.3]
- Added persistent settings and split the original application into modules.

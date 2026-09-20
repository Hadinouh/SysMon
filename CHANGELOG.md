# Changelog

All notable SysMon changes are documented here.

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

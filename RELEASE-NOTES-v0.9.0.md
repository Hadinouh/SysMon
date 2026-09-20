# SysMon v0.9.0

SysMon v0.9.0 is a major monitoring, settings, and UI update built on top of v0.8.0.

## Highlights

- Multi-GPU monitoring and GPU selection
- Live network monitoring and adapter selection
- Hardware temperature monitoring through SysMonSensors / LibreHardwareMonitor
- Redesigned and expanded Processes page
- Complete Settings page with themes, units, refresh rate, alerts, logging, startup and tray controls
- New Tools page for Windows maintenance and diagnostics
- Responsive resizing and improved maximized/full-screen layouts
- Desktop CPU/RAM overlay improvements and Ctrl+Alt+O hotkey
- GitHub Releases update checker
- Settings backup/restore, support report, data folder and documentation actions
- Background sampling, metadata caching and rendering optimizations
- Automated build and release packaging

## Monitoring

GPU monitoring now includes multi-GPU selection, utilization history, memory usage, temperature/fan telemetry when supported, clocks, power, encode/decode activity, driver details and process-related GPU information.

Network monitoring now includes adapter selection, download/upload history, current throughput, link speed, packet/data totals, IPv4/IPv6, MAC, gateway, DNS, subnet and Wi-Fi information where available.

Temperature monitoring uses the SysMonSensors helper and LibreHardwareMonitor to expose CPU, GPU and motherboard/firmware sensor information where supported by the hardware.

## Settings and alerts

v0.9.0 adds configurable startup/tray behavior, update checks, monitoring intervals, Celsius/Fahrenheit, network units, dark/light/Follow Windows themes, accent colors, transparency, compact mode, quick tab previews and UI font sizing.

Alert controls include CPU temperature, GPU temperature, disk usage and memory usage thresholds, plus desktop notifications, sounds and logging. Monitoring data can also be logged with configurable retention.

## UI and performance

The interface now scales with the window, behaves correctly when maximized, keeps hitboxes/scrolling aligned with the rendered UI, and uses quick navigation previews plus background sampling/caching to reduce visible stalls.

## Release packaging

The release ZIP produced by `package-release.ps1` contains `SysMon.exe`, all required UI assets, documentation and a self-contained Windows x64 SysMonSensors helper.

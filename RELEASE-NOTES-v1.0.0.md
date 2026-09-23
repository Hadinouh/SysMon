# SysMon v1.0.0

A portable Windows x64 system monitor with live hardware statistics, process
management, customizable overlays and local diagnostics.

## What's new

- Improved shutdown and sensor-helper cleanup, including when the parent process is force-closed.
- Duplicate-instance prevention and clearer tray, minimize and Exit behavior.
- Four overlay styles: Precision Rings, Telemetry Stack, Side Rail and Floating Tiles.
- Independent overlay metrics, optional backgrounds, opacity, scale, always-on-top and remembered monitor positions.
- Pause/resume monitoring and expanded tray actions.
- Reversible management of current-user startup entries.
- Improved Settings dropdowns, value validation, section resets and tooltips.
- Working GPU and Network View All process lists; network figures represent TCP connection counts, not per-process bandwidth.
- Higher-resolution program icons and sorting arrows in Running Processes.
- Matching sidebar symbols and a simplified green System Healthy indicator.
- Responsive layouts, Windows scaling support and executable version metadata.
- Sensor status, local diagnostic logs, support reports and optional GitHub update checking.
- MIT licensing for original project material, dependency notices and corresponding sensor-library sources.

## Getting started

Extract the entire archive into a writable folder and open SysMon.exe. Keep
logos, SysMonSensors, licenses and sources with the application.

The download does not contain SysMon.ini. SysMon creates settings on first use.
Logs and backups are stored in the per-user SysMon data folder.

Use Exit in the tray menu or Ctrl+Q to close SysMon fully. The X button follows
your minimize-to-tray setting. Ctrl+P pauses monitoring and Ctrl+Alt+O toggles
the overlay.

## Notes

This build is unsigned. Windows may display a publisher warning.
Sensor availability depends on your hardware, drivers and permissions. Startup
Apps manages current-user Run entries; startup impact is not measured. Physical
multi-monitor changes and hardware-specific behavior have not been verified on
every configuration. No telemetry or automatic report uploads are included.

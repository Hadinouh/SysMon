# SysMon


It displays live CPU, memory, disk, and system uptime information in a dark dashboard and includes a movable desktop widget for quick CPU and RAM monitoring.


## Current Version

### v0.3

SysMon v0.3 introduces persistent settings and a cleaner modular codebase.

The application can now remember the desktop widget position and whether the widget was enabled or disabled between launches.

The original single-file project has also been separated into dedicated modules for system statistics, settings, tray functionality, the desktop widget, and UI rendering.

## Features

- Live CPU usage monitoring
- Live RAM usage monitoring
- Disk C: usage monitoring
- System uptime tracking
- Dark native Windows dashboard
- Desktop CPU/RAM widget
- Draggable desktop widget
- Persistent widget position
- Persistent widget ON/OFF setting
- System tray support
- Double-click tray icon to restore SysMon
- Right-click tray menu with Open and Exit options
- Native Win32 API and GDI interface
- No external GUI framework required

## Desktop Widget

When SysMon is minimized, it moves to the Windows system tray and displays a small desktop widget.

The widget shows:

- CPU usage
- RAM usage
- Live progress bars

The widget can be dragged anywhere on the desktop.

Its position is automatically saved in `SysMon.ini` and restored the next time SysMon starts.

The desktop widget can also be enabled or disabled from the main dashboard.

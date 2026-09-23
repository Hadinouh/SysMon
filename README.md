# SysMon

SysMon is a native Windows system monitoring application built in C++ using the Win32 API and GDI.

It provides real-time CPU, memory, disk, GPU, network, process, hardware, device, temperature, and system monitoring through a native Windows desktop interface.

SysMon also includes customizable desktop overlays, Windows system tray integration, startup-app management, alerts, diagnostics, logging, system information, and Windows maintenance tools.

## Current Version

**v1.0.0**

Download the latest compiled Windows release from:

**https://github.com/Hadinouh/SysMon/releases/latest**

> **Note:** The v1.0.0 Windows release is not digitally signed. Windows SmartScreen may display an **Unknown publisher** warning when launching SysMon for the first time.

SysMon is an independent open-source project and is not affiliated with, endorsed by, or associated with Microsoft or Microsoft Sysinternals.

---

# What's New in v1.0.0

From v0.9 to v1.0, SysMon focused on reliability, customization, usability, and release readiness.

### Shutdown & lifecycle

- Improved full Exit cleanup for SysMon, SysMonSensors, overlays, tray icons, and monitoring workers
- Added bounded shutdown behavior
- Added parent-owned sensor cleanup
- Added duplicate-instance prevention

### Tray controls

- Added quicker access to:
  - Overlay
  - Settings
  - Pause Monitoring
  - Resume Monitoring
  - Exit

### Desktop overlays

Added four customizable overlay styles:

- Precision Rings
- Telemetry Stack
- Side Rail
- Floating Tiles

Each overlay supports independent selection of:

- CPU
- RAM
- GPU
- Disk
- Network

Overlay customization includes:

- Optional backgrounds
- Adjustable opacity
- Adjustable scale
- Always-on-top mode
- Remembered monitor position
- Persistent configuration

### Startup Manager

- Added reversible enable/disable controls for supported current-user startup entries

### Settings

- Improved dropdown styling and behavior
- Added settings validation
- Added section reset controls
- Added tooltips
- Added sensor-status information
- Improved DPI and resizing behavior

### Process monitoring

- Fixed GPU **View All**
- Fixed Network **View All**
- Improved process icon resolution
- Added sorting arrows
- Added highlighted active column headings
- Improved process navigation

### Visual polish

- Corrected sidebar icons
- Improved resizing and scaling behavior
- Added the SysMon taskbar logo
- Simplified **System Healthy** to a green status dot and label

### Diagnostics

- Added local error logging
- Added support-report generation
- Improved update-checking information

### Release cleanup

- Added an MIT license for original SysMon code
- Added third-party dependency notices
- Added privacy and security documentation
- Added asset documentation
- Bundled required corresponding source material for applicable third-party components
- Removed debug symbols from release binaries
- Removed local development/build paths from release binaries
- Removed saved user settings from the distributed package
- Moved all application PNG assets into the `logos/` directory

---

# v1.0 Overlay Previews

These previews are rendered by SysMon's overlay test using sample readings.

Each style supports independent CPU/RAM/GPU/Disk/Network selection, background on/off, opacity, scale, always-on-top, and remembered monitor position.

| Precision Rings | Telemetry Stack |
| --- | --- |
| ![Precision Rings](docs/images/overlay-0-background.png) | ![Telemetry Stack](docs/images/overlay-1-background.png) |

| Side Rail | Floating Tiles |
| --- | --- |
| ![Side Rail](docs/images/overlay-2-background.png) | ![Floating Tiles](docs/images/overlay-3-background.png) |

---

# Features

## Dashboard

- Live CPU usage monitoring
- Live RAM usage monitoring
- Real-time physical disk monitoring
- Multiple disk detection
- Live CPU, memory, and disk graphs
- Performance history
- CPU hardware and cache information
- Memory hardware information
- Memory commit, cache, and kernel pool statistics
- Disk active time
- Disk transfer rate
- Disk read and write speeds
- Disk response time
- Disk model and capacity information
- SSD, NVMe, SATA, and HDD identification
- System disk and page-file detection
- System uptime tracking
- System-health status

---

# Performance

The Performance page provides detailed monitoring for CPU, memory, disk, GPU, network, and temperatures.

## CPU

- Live CPU utilization
- 60-second usage history
- Current speed
- Base speed
- Process count
- Thread count
- Handle count
- System uptime
- Socket count
- Core count
- Logical processor count
- Virtualization status
- L1 cache information
- L2 cache information
- L3 cache information

## Memory

- Live memory utilization
- 60-second memory history
- Installed and usable memory
- In-use memory
- Available memory
- Committed memory
- Cached memory
- Paged pool
- Non-paged pool
- Memory speed
- Memory slots used
- Memory form factor
- Hardware-reserved memory

## Disk

- Automatic physical disk detection
- Support for multiple installed disks
- Selectable disk cards
- Live active-time history
- Live disk-transfer-rate history
- Average response time
- Read speed
- Write speed
- Disk model
- Capacity
- Formatted capacity
- System disk detection
- Page-file detection
- Storage type detection

## GPU

- Multi-GPU detection and selection
- Live GPU utilization history
- Dedicated GPU memory usage
- Shared GPU memory usage
- GPU temperature telemetry when supported
- GPU fan telemetry when supported
- GPU clocks
- GPU power telemetry when supported
- Encode/decode activity
- Driver information
- Bus information
- GPU-related process statistics
- GPU process detail window

## Network

- Multiple network-adapter detection and selection
- Live download history
- Live upload history
- Current transfer rates
- Link speed
- Total sent and received data
- Packet statistics
- IPv4 information
- IPv6 information
- MAC address
- Gateway
- DNS
- Subnet information
- Connection information
- Wi-Fi SSID when available
- Wi-Fi standard when available
- Wi-Fi signal quality when available
- Network process statistics
- Network process detail window

## Temperatures

- CPU package temperatures when supported
- CPU core temperatures when supported
- GPU temperature monitoring
- Motherboard and firmware sensor monitoring
- Temperature history
- CPU package power telemetry when available
- CPU clock telemetry when available
- Hardware readings provided through the bundled `SysMonSensors` helper

The temperature engine uses LibreHardwareMonitor through the bundled SysMonSensors helper.

The default Windows x64 release includes the helper as a self-contained application, so users do not need to separately install the .NET runtime.

---

# Processes

SysMon includes a real-time Processes page for inspecting running Windows processes.

Process information includes:

- Process name
- CPU usage
- Memory usage
- Thread count
- PID
- Parent PID
- Handle count
- Application icons

Processes can be sorted by supported columns in ascending or descending order.

Additional process features include:

- Search and filtering
- Process selection
- Row hover states
- Live filtered process counts
- Process termination with confirmation
- Process-tree termination
- Process metadata caching
- Background process sampling
- Protection for critical Windows processes
- Protection for SysMon itself

Protected processes such as **System Idle Process**, **System**, and **SysMon** cannot be terminated through the application.

---

# System Info

SysMon includes a detailed **System Info / About PC** page.

## Operating System

- Windows edition
- Windows version
- Installation date
- OS build
- Windows Feature Experience Pack information
- System architecture
- Computer name

## Processor

- CPU model
- Physical core count
- Logical processor count
- Base speed
- Current reported speed
- CPU socket
- Virtualization status
- L1 cache
- L2 cache
- L3 cache

## Memory

- Installed memory
- Memory type
- Memory speed
- Slots used
- Memory form factor

## Graphics

- GPU model
- Video memory
- Driver version
- Driver date
- DirectX version

## Storage

SysMon lists detected physical storage devices and displays:

- Disk number
- Drive letters
- Disk model
- Capacity
- SSD / HDD identification
- NVMe / SATA / USB storage type

## Network

- Active network adapter
- Connection type
- IPv4 address
- IPv6 address

## System and Motherboard

- System manufacturer
- System model
- Motherboard manufacturer
- Motherboard model

## BIOS / Firmware

- BIOS vendor
- BIOS version
- BIOS release date

---

# Connected Devices

SysMon can enumerate currently present Windows Plug and Play devices.

Supported device categories include:

- USB
- Bluetooth
- HID
- Audio devices
- Monitors
- Cameras
- Printers
- Keyboards
- Mice
- Other user-facing Plug and Play devices

Devices can be selected to display additional information including:

- Device name
- Connection type
- Device class
- Manufacturer
- Device status
- Problem code when available
- Physical device location
- Vendor ID
- Product ID
- Device instance ID
- Hardware ID

The connected-device list automatically refreshes when Windows reports hardware changes.

---

# Desktop Overlay

When SysMon is minimized, it can move to the Windows system tray and display a customizable desktop overlay.

Available overlay styles:

1. Precision Rings
2. Telemetry Stack
3. Side Rail
4. Floating Tiles

Users can independently enable or disable:

- CPU
- RAM
- GPU
- Disk
- Network

Overlay options include:

- Background on/off
- Opacity
- Scale
- Always on top
- Remembered position
- Monitor-position memory

---

# System Tray

SysMon supports Windows system tray integration.

Tray features include:

- Minimize to tray
- Double-click the tray icon to restore SysMon
- Right-click tray menu
- Show/hide overlay
- Open Settings
- Pause Monitoring
- Resume Monitoring
- Exit

Selecting **Exit** performs full application cleanup, including the sensor helper and monitoring workers.

---

# Settings

SysMon includes a complete Settings page.

Available settings include:

### General

- Start with Windows
- Minimize to system tray
- Update checks

### Monitoring

- Monitoring interval
- Temperature units
- Network units
- Sensor status

### Appearance

- Dark theme
- Light theme
- Follow Windows theme
- Accent colors
- Transparency
- Compact mode
- Quick tab previews
- Font sizing

### Alerts

- CPU temperature threshold
- GPU temperature threshold
- Disk usage threshold
- Memory usage threshold
- Desktop notifications
- Alert sounds
- Alert logging

### Logging

- Monitoring-data logging
- Log retention
- SysMon data folder

### Overlay

- Overlay style
- Selectable metrics
- Background
- Opacity
- Scale
- Always-on-top behavior
- Position persistence

### Configuration

- Settings export
- Settings import
- Reset controls
- Section resets

Settings values are validated before being applied.

---

# Startup Manager

SysMon includes a Startup Manager for supported current-user startup entries.

Users can:

- View supported startup applications
- See whether an entry is enabled or disabled
- Disable supported startup entries
- Re-enable previously disabled entries

Startup changes are designed to be reversible.

---

# Tools

The Tools page provides shortcuts and actions for common Windows maintenance and diagnostic utilities, including:

- Disk Cleanup
- System File Checker
- Startup Manager
- Event Viewer
- System Configuration
- Services
- Elevated Command Prompt
- Drive optimization
- Windows Update
- Power Options
- System Restore
- Network settings
- Temporary-file cleanup
- DNS flushing
- Recycle Bin cleanup
- System snapshots

---

# Diagnostics and Support

SysMon v1.0 includes additional diagnostics and support functionality:

- Local error logging
- Support-report generation
- Sensor-status information
- Improved update-checking information

SysMon has **no telemetry or automatic report uploads**.

Optional/manual update checks contact GitHub.

See:

- [PRIVACY.md](PRIVACY.md)
- [SECURITY.md](SECURITY.md)

for additional information.

---

# Architecture

SysMon uses:

- Native C++
- Win32 API
- GDI / GDI+
- Windows system APIs
- A separate `SysMonSensors` helper for supported hardware sensor telemetry

The application uses native Windows controls and rendering rather than an external GUI framework.

---

# License, Privacy, and Security

Original SysMon code and documentation are distributed under the **MIT License**.

See:

- [LICENSE](LICENSE)
- [PRIVACY.md](PRIVACY.md)
- [SECURITY.md](SECURITY.md)
- [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt)
- [ASSET-NOTICES.md](ASSET-NOTICES.md)

Third-party components retain their respective licenses.

The release includes license notices and corresponding source material where required.

Major bundled components include:

- LibreHardwareMonitor — MPL-2.0
- Blacktempel libraries — MPL-2.0
- HidSharp — Apache-2.0
- Mono.Posix — MIT
- Microsoft/.NET support components — applicable MIT and upstream notices
- PawnIO modules — LGPL-2.1

The self-contained sensor helper includes the .NET runtime and its applicable notices.

Application PNG assets are documented in `ASSET-NOTICES.md` as ChatGPT-generated specifically for the SysMon project.

Third-party program icons detected and displayed by SysMon retain their respective owners' rights.

---

# Downloading SysMon

Compiled releases are available from:

**https://github.com/Hadinouh/SysMon/releases/latest**

For the portable Windows version:

1. Download `SysMon-v1.0.0.zip`
2. Extract the entire archive
3. Keep all included folders and files together
4. Run `SysMon.exe`

Do not remove the bundled `SysMonSensors`, `logos`, license, or supporting runtime files from the extracted package.

> SysMon v1.0.0 is currently distributed unsigned. Windows may display a SmartScreen or **Unknown publisher** warning.

The source repository does not track the compiled `SysMon.exe`.

---

# Building

## Requirements

To build the native application:

- Windows
- MSYS2 UCRT64 / MinGW-w64
- `g++`
- `windres`
- C++17 support

To build the sensor helper:

- .NET 10 SDK
- Internet access for NuGet restore and corresponding-source downloads

## Build SysMon

From PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

## Build a Complete Release Package

To create a complete Windows x64 release ZIP with the self-contained SysMonSensors helper:

```powershell
powershell -ExecutionPolicy Bypass -File .\package-release.ps1
```

The finished release archive is written to:

```text
dist/
```

To run the native verification suite during packaging, use the appropriate `-RunTests` option supported by the packaging script.

Release packaging performs checks including:

- Managed debug information disabled
- PDB rejection
- Local user/build-path checks
- Dependency-version checks
- Dependency manifest generation
- SHA-256 checksum generation

`DEPENDENCIES.json` records packaged dependency information.

`SHA256SUMS.txt` records packaged file hashes.

The default Windows x64 package contains a self-contained SysMonSensors runtime, so end users do not need to install .NET separately.

Framework-dependent sensor builds require a compatible installed .NET 10 runtime.

---

# Version History

## v1.0.0

- Improved reliable shutdown and sensor-process cleanup
- Added duplicate-instance prevention
- Added four redesigned desktop overlays
- Added optional overlay backgrounds
- Added selectable CPU/RAM/GPU/Disk/Network overlay metrics
- Added overlay opacity and scale controls
- Added always-on-top support
- Added remembered monitor positions
- Added Pause/Resume Monitoring
- Expanded tray actions
- Added reversible current-user Startup Manager
- Added validated settings
- Added section reset controls
- Improved Settings dropdowns
- Added settings tooltips
- Added sensor status
- Improved DPI/resizing behavior
- Fixed Network process **View All**
- Fixed GPU process **View All**
- Improved process icon resolution
- Added process sorting arrows and active-column highlighting
- Added diagnostic logging
- Added support reports
- Improved update details
- Added embedded application logo
- Added Windows executable version metadata
- Added MIT licensing
- Expanded dependency notices
- Added privacy and security documentation
- Added source bundles for applicable dependencies
- Added release checks for debug symbols and local build paths
- Moved application PNG assets into `logos/`
- Removed saved settings from the distributed package
- The Windows v1.0.0 release is distributed unsigned

## v0.9.0

- Added multi-GPU performance monitoring and GPU selection
- Added live network performance monitoring and adapter selection
- Added hardware temperature monitoring through SysMonSensors / LibreHardwareMonitor
- Added and polished the dedicated Temperatures page
- Expanded dashboard hardware statistics
- Overhauled the Processes page with filtering, grouping, metadata, actions, and priority controls
- Added background process sampling and process metadata caching
- Added the complete Settings page
- Added dark, light, and Follow Windows themes
- Added accent colors
- Added transparency
- Added compact mode
- Added quick tab previews
- Added font sizing
- Added configurable monitoring interval
- Added temperature units
- Added network units
- Added CPU/GPU temperature alerts
- Added disk-usage alerts
- Added memory-usage alerts
- Added desktop notifications
- Added alert sounds
- Added alert logging
- Added monitoring-data logging
- Added configurable log retention
- Added the SysMon data folder
- Added settings export/import/reset
- Added startup integration through Windows Task Scheduler
- Added optional GitHub release update checking
- Added desktop overlay settings
- Added the `Ctrl+Alt+O` overlay hotkey
- Added the Tools page
- Expanded System Info and connected-device details
- Added responsive resizing
- Improved maximized/full-screen layouts
- Added responsive hitboxes and scrolling
- Improved quick navigation previews
- Improved minimize-to-tray and restore behavior
- Improved tray-icon recovery
- Improved desktop-widget behavior and persisted positioning
- Added background-sampling and rendering-performance improvements
- Added build automation
- Added release packaging
- Added application manifest and Windows version metadata

## v0.8.0

- Added the System Info / About PC page
- Added detailed Windows operating system information
- Added computer name and system architecture information
- Added detailed CPU information
- Added CPU core, thread, socket, virtualization, and cache information
- Added installed memory information
- Added memory type, speed, slot, and form-factor information
- Added GPU model and VRAM information
- Added GPU driver version and driver date
- Added DirectX version detection
- Added physical storage information
- Added network adapter information
- Added IPv4 and IPv6 information
- Added system manufacturer and model information
- Added motherboard manufacturer and model information
- Added BIOS vendor, version, and release date
- Added connected Plug and Play device detection
- Added USB, Bluetooth, HID, display, audio, camera, printer, keyboard, and mouse detection
- Added selectable connected devices
- Added detailed device information
- Added automatic device refresh on Windows hardware changes
- Added scrolling support to System Info

## v0.7.0

- Redesigned the Performance page
- Added detailed CPU monitoring
- Added CPU topology, virtualization, and cache information
- Added detailed memory monitoring
- Added memory commit, cache, paged-pool, and non-paged-pool information
- Added memory speed, slots, form factor, and hardware-reserved information
- Added real physical disk activity monitoring
- Added disk active-time graphs
- Added disk transfer-rate graphs
- Added read speed, write speed, and response time
- Added physical disk model and capacity information
- Added SSD, HDD, SATA, and NVMe identification
- Added automatic multiple-disk detection
- Added selectable disk cards
- Added system-disk and page-file detection

## v0.6

- Added advanced process monitoring
- Added sortable Process, CPU, Memory, Threads, and PID columns
- Added ascending and descending sorting
- Added live process search and filtering
- Added clear-search button
- Added process row hover and selection
- Added selected-process details
- Added Parent PID and handle count
- Added live filtered process counts
- Added End Task with confirmation
- Added process-tree termination
- Added safeguards for critical processes and SysMon

## v0.5

- Redesigned the main dashboard with a modern sidebar layout
- Added navigation for Dashboard, Processes, Performance, System Info, and Settings
- Added the real-time Processes page
- Added process CPU usage
- Added process memory usage
- Added thread count and PID
- Added System Idle Process support
- Added process-list scrolling
- Added live process count
- Improved dashboard layout
- Combined uptime and desktop-widget controls into a cleaner summary card

## v0.4

- Added CPU usage history
- Added RAM usage history
- Added 120-sample history buffers

---

# Contributing

Issues, bug reports, and contributions are welcome through the GitHub repository.

For security vulnerabilities, please follow the process described in:

[SECURITY.md](SECURITY.md)

---

# Disclaimer

SysMon is an independent open-source project.

It is **not affiliated with, endorsed by, sponsored by, or associated with Microsoft or Microsoft Sysinternals**.

Windows, Microsoft, and related product names belong to their respective owners.
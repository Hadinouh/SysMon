#pragma once

#include <windows.h>
#include <string>

struct SysMonAppSettings
{
    bool startWithWindows = false;
    bool minimizeToTray = true;
    bool checkForUpdates = false;
    int theme = 0; // 0=Dark, 1=Light, 2=Follow Windows

    int updateIntervalMs = 500;
    int temperatureUnit = 0; // 0=Celsius, 1=Fahrenheit
    int networkUnit = 0;     // 0=Mbps, 1=MB/s, 2=Kbps
    bool showInTray = true;
    bool playAlerts = true;

    int accentColorIndex = 0;
    int transparencyPercent = 100;
    bool compactMode = false;
    bool showAnimations = true;
    int fontSizeIndex = 1;   // 0=Small, 1=Medium, 2=Large

    int cpuTemperatureAlert = 85;
    int gpuTemperatureAlert = 85;
    int diskUsageAlert = 90;
    int memoryUsageAlert = 90;
    bool showDesktopNotifications = true;
    bool logAlertsToFile = true;

    bool enableDataLogging = false;
    int logRetentionDays = 30;
    bool allowAnonymousUsageData = false;

    bool showOverlayWidget = true;
};

extern SysMonAppSettings appSettings;

std::string getSettingsPath();
std::string getSysMonDataFolderPath();

void loadAppSettings();
bool saveAppSettings();
void resetAppSettings();

bool setStartWithWindowsEnabled(bool enabled);
void applyMainWindowTransparency(HWND hwnd);

bool exportAppSettingsBackup(const std::string& backupPath = {});
bool importAppSettingsBackup(const std::string& backupPath = {});

void loadWidgetSettings();
void saveWidgetPosition();
void saveWidgetEnabled();
void positionDesktopWidget(HWND hwnd);

void refreshThemePreference();
bool isLightTheme();
bool isStartWithWindowsEnabled();

extern bool sysMonOverlayHotkeyAvailable;

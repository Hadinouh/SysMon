#include "Settings.h"
#include "Startup.h"

#include <shlobj.h>
#include <string>

// These variables still live in Main.cpp for now.
extern HWND desktopWidget;
extern bool widgetEnabled;
extern int savedWidgetX;
extern int savedWidgetY;

SysMonAppSettings appSettings;

static int clampInt(int value, int minimum, int maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static std::string resolveSysMonDataFolderPath();

std::string getSettingsPath()
{
    char path[MAX_PATH] = {};

    GetModuleFileNameA(
        nullptr,
        path,
        MAX_PATH
    );

    std::string fullPath = path;
    const size_t slash = fullPath.find_last_of("\\/");

    if (slash != std::string::npos)
    {
        fullPath = fullPath.substr(0, slash + 1);
    }

    return fullPath + "SysMon.ini";
}

std::string getSysMonDataFolderPath()
{
    // The Settings page asks for this path on every repaint. Resolving the
    // shell folder and calling CreateDirectoryA each time meant a filesystem
    // round trip per frame, which is what made settings scrolling stutter.
    // The location cannot change while SysMon is running, so resolve once.
    static std::string cachedFolder;
    static bool cachedFolderResolved = false;

    if (cachedFolderResolved)
    {
        return cachedFolder;
    }

    cachedFolder = resolveSysMonDataFolderPath();
    cachedFolderResolved = true;

    return cachedFolder;
}

static std::string resolveSysMonDataFolderPath()
{
    char localAppData[MAX_PATH] = {};

    if (SUCCEEDED(SHGetFolderPathA(
            nullptr,
            CSIDL_LOCAL_APPDATA,
            nullptr,
            SHGFP_TYPE_CURRENT,
            localAppData)))
    {
        std::string folder = localAppData;
        folder += "\\SysMon";
        CreateDirectoryA(folder.c_str(), nullptr);
        return folder;
    }

    std::string settingsPath = getSettingsPath();
    const size_t slash = settingsPath.find_last_of("\\/");
    if (slash != std::string::npos)
    {
        return settingsPath.substr(0, slash);
    }

    return ".";
}

static bool writeBool(
    const char* section,
    const char* key,
    bool value,
    const std::string& path)
{
    return WritePrivateProfileStringA(
        section,
        key,
        value ? "1" : "0",
        path.c_str()
    ) != FALSE;
}

static bool writeInt(
    const char* section,
    const char* key,
    int value,
    const std::string& path)
{
    const std::string text = std::to_string(value);
    return WritePrivateProfileStringA(
        section,
        key,
        text.c_str(),
        path.c_str()
    ) != FALSE;
}

bool saveAppSettings()
{
    const std::string path = getSettingsPath();
    bool saved = true;

    saved = writeBool("General", "StartWithWindows", appSettings.startWithWindows, path) && saved;
    saved = writeBool("General", "MinimizeToTray", appSettings.minimizeToTray, path) && saved;
    saved = writeBool("General", "CheckForUpdates", appSettings.checkForUpdates, path) && saved;
    saved = writeInt("General", "Theme", appSettings.theme, path) && saved;

    saved = writeInt("Monitoring", "UpdateIntervalMs", appSettings.updateIntervalMs, path) && saved;
    saved = writeInt("Monitoring", "TemperatureUnit", appSettings.temperatureUnit, path) && saved;
    saved = writeInt("Monitoring", "NetworkUnit", appSettings.networkUnit, path) && saved;
    saved = writeBool("Monitoring", "ShowInTray", appSettings.showInTray, path) && saved;
    saved = writeBool("Monitoring", "PlayAlerts", appSettings.playAlerts, path) && saved;

    saved = writeInt("Appearance", "AccentColorIndex", appSettings.accentColorIndex, path) && saved;
    saved = writeInt("Appearance", "TransparencyPercent", appSettings.transparencyPercent, path) && saved;
    saved = writeBool("Appearance", "CompactMode", appSettings.compactMode, path) && saved;
    saved = writeBool("Appearance", "ShowAnimations", appSettings.showAnimations, path) && saved;
    saved = writeInt("Appearance", "FontSizeIndex", appSettings.fontSizeIndex, path) && saved;

    saved = writeInt("Alerts", "CpuTemperature", appSettings.cpuTemperatureAlert, path) && saved;
    saved = writeInt("Alerts", "GpuTemperature", appSettings.gpuTemperatureAlert, path) && saved;
    saved = writeInt("Alerts", "DiskUsage", appSettings.diskUsageAlert, path) && saved;
    saved = writeInt("Alerts", "MemoryUsage", appSettings.memoryUsageAlert, path) && saved;
    saved = writeBool("Alerts", "DesktopNotifications", appSettings.showDesktopNotifications, path) && saved;
    saved = writeBool("Alerts", "LogAlerts", appSettings.logAlertsToFile, path) && saved;

    saved = writeBool("Data", "EnableLogging", appSettings.enableDataLogging, path) && saved;
    saved = writeInt("Data", "RetentionDays", appSettings.logRetentionDays, path) && saved;
    saved = writeBool("Data", "AnonymousUsage", appSettings.allowAnonymousUsageData, path) && saved;

    saved = writeBool("Integration", "ShowOverlayWidget", appSettings.showOverlayWidget, path) && saved;
    return saved;
}

void loadAppSettings()
{
    const std::string path = getSettingsPath();

    appSettings.startWithWindows =
        GetPrivateProfileIntA("General", "StartWithWindows", 0, path.c_str()) != 0;
    appSettings.minimizeToTray =
        GetPrivateProfileIntA("General", "MinimizeToTray", 1, path.c_str()) != 0;
    appSettings.checkForUpdates =
        GetPrivateProfileIntA("General", "CheckForUpdates", 0, path.c_str()) != 0;

    appSettings.theme = clampInt(GetPrivateProfileIntA("General", "Theme", 0, path.c_str()), 0, 2);
    refreshThemePreference();

    appSettings.updateIntervalMs = clampInt(
        GetPrivateProfileIntA("Monitoring", "UpdateIntervalMs", 500, path.c_str()),
        250,
        10000
    );
    appSettings.temperatureUnit = clampInt(
        GetPrivateProfileIntA("Monitoring", "TemperatureUnit", 0, path.c_str()),
        0,
        1
    );
    appSettings.networkUnit = clampInt(
        GetPrivateProfileIntA("Monitoring", "NetworkUnit", 0, path.c_str()),
        0,
        2
    );
    appSettings.showInTray =
        GetPrivateProfileIntA("Monitoring", "ShowInTray", 1, path.c_str()) != 0;
    if (!appSettings.showInTray) appSettings.minimizeToTray=false;
    appSettings.playAlerts =
        GetPrivateProfileIntA("Monitoring", "PlayAlerts", 1, path.c_str()) != 0;

    appSettings.accentColorIndex = clampInt(
        GetPrivateProfileIntA("Appearance", "AccentColorIndex", 0, path.c_str()),
        0,
        5
    );
    appSettings.transparencyPercent = clampInt(
        GetPrivateProfileIntA("Appearance", "TransparencyPercent", 100, path.c_str()),
        55,
        100
    );
    appSettings.compactMode =
        GetPrivateProfileIntA("Appearance", "CompactMode", 0, path.c_str()) != 0;
    appSettings.showAnimations =
        GetPrivateProfileIntA("Appearance", "ShowAnimations", 1, path.c_str()) != 0;
    appSettings.fontSizeIndex = clampInt(
        GetPrivateProfileIntA("Appearance", "FontSizeIndex", 1, path.c_str()),
        0,
        2
    );

    appSettings.cpuTemperatureAlert = clampInt(
        GetPrivateProfileIntA("Alerts", "CpuTemperature", 85, path.c_str()),
        50,
        110
    );
    appSettings.gpuTemperatureAlert = clampInt(
        GetPrivateProfileIntA("Alerts", "GpuTemperature", 85, path.c_str()),
        50,
        110
    );
    appSettings.diskUsageAlert = clampInt(
        GetPrivateProfileIntA("Alerts", "DiskUsage", 90, path.c_str()),
        50,
        100
    );
    appSettings.memoryUsageAlert = clampInt(
        GetPrivateProfileIntA("Alerts", "MemoryUsage", 90, path.c_str()),
        50,
        100
    );
    appSettings.showDesktopNotifications =
        GetPrivateProfileIntA("Alerts", "DesktopNotifications", 1, path.c_str()) != 0;
    appSettings.logAlertsToFile =
        GetPrivateProfileIntA("Alerts", "LogAlerts", 1, path.c_str()) != 0;

    appSettings.enableDataLogging =
        GetPrivateProfileIntA("Data", "EnableLogging", 0, path.c_str()) != 0;
    appSettings.logRetentionDays = clampInt(
        GetPrivateProfileIntA("Data", "RetentionDays", 30, path.c_str()),
        1,
        365
    );
    appSettings.allowAnonymousUsageData = false; // No telemetry service in this version.

    appSettings.showOverlayWidget =
        GetPrivateProfileIntA("Integration", "ShowOverlayWidget", 1, path.c_str()) != 0;
}

void resetAppSettings()
{
    appSettings = SysMonAppSettings();
    saveAppSettings();
}

bool isStartWithWindowsEnabled() { return Startup::enabled(); }

bool setStartWithWindowsEnabled(bool enabled)
{
    // Elevated apps cannot reliably launch from the Run key. A per-user logon
    // task preserves the sensor permissions and the asset working directory.
    if (!Startup::configure(enabled)) return false;
    HKEY key=nullptr;
    LONG result=RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_SET_VALUE,&key);
    if(result==ERROR_SUCCESS) {
        result=RegDeleteValueW(key,L"SysMon");
        RegCloseKey(key);
        if(result!=ERROR_SUCCESS && result!=ERROR_FILE_NOT_FOUND) return false;
    } else if(result!=ERROR_FILE_NOT_FOUND) return false;
    return true;
}

void applyMainWindowTransparency(HWND hwnd)
{
    if (hwnd == nullptr)
    {
        return;
    }

    LONG_PTR exStyle = GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_LAYERED;
    SetWindowLongPtrA(hwnd, GWL_EXSTYLE, exStyle);

    const BYTE alpha = static_cast<BYTE>(
        (255 * clampInt(appSettings.transparencyPercent, 55, 100)) / 100
    );

    SetLayeredWindowAttributes(
        hwnd,
        0,
        alpha,
        LWA_ALPHA
    );
}

bool exportAppSettingsBackup(const std::string& backupPath)
{
    const std::string source = getSettingsPath();
    const std::string destination = backupPath.empty() ? getSysMonDataFolderPath() + "\\SysMon-settings-backup.ini" : backupPath;

    if (!saveAppSettings()) return false;
    return CopyFileA(source.c_str(), destination.c_str(), FALSE) != FALSE;
}

bool importAppSettingsBackup(const std::string& backupPath)
{
    const std::string source = backupPath.empty() ? getSysMonDataFolderPath() + "\\SysMon-settings-backup.ini" : backupPath;
    const std::string destination = getSettingsPath();

    char sections[2048] = {};
    if (GetPrivateProfileSectionNamesA(sections, sizeof(sections), source.c_str()) == 0) return false;
    char marker[32] = {};
    GetPrivateProfileStringA("General","MinimizeToTray","missing",marker,sizeof(marker),source.c_str());
    if(std::string(marker)=="missing") return false;
    if (CopyFileA(source.c_str(), destination.c_str(), FALSE) == FALSE)
    {
        return false;
    }

    loadAppSettings();
    return true;
}

void saveWidgetPosition()
{
    if (desktopWidget == nullptr)
        return;

    RECT rect;

    if (!GetWindowRect(desktopWidget, &rect))
    {
        return;
    }

    const std::string path = getSettingsPath();
    const std::string x = std::to_string(rect.left);
    const std::string y = std::to_string(rect.top);

    WritePrivateProfileStringA("Widget", "X", x.c_str(), path.c_str());
    WritePrivateProfileStringA("Widget", "Y", y.c_str(), path.c_str());
}

void saveWidgetEnabled()
{
    const std::string path = getSettingsPath();

    WritePrivateProfileStringA(
        "Widget",
        "Enabled",
        widgetEnabled ? "1" : "0",
        path.c_str()
    );
}

void loadWidgetSettings()
{
    const std::string path = getSettingsPath();

    savedWidgetX = GetPrivateProfileIntA("Widget", "X", -1, path.c_str());
    savedWidgetY = GetPrivateProfileIntA("Widget", "Y", -1, path.c_str());
    widgetEnabled = GetPrivateProfileIntA("Widget", "Enabled", 1, path.c_str()) != 0;
}

void positionDesktopWidget(HWND hwnd)
{
    const int widgetWidth = 240;
    const int widgetHeight = 100;

    if (savedWidgetX != -1 && savedWidgetY != -1)
    {
        SetWindowPos(
            hwnd,
            nullptr,
            savedWidgetX,
            savedWidgetY,
            widgetWidth,
            widgetHeight,
            SWP_NOZORDER | SWP_NOACTIVATE
        );
        return;
    }

    RECT workArea = {};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &workArea, 0);

    const int x = workArea.right - widgetWidth - 25;
    const int y = workArea.bottom - widgetHeight - 25;

    SetWindowPos(
        hwnd,
        nullptr,
        x,
        y,
        widgetWidth,
        widgetHeight,
        SWP_NOZORDER | SWP_NOACTIVATE
    );
}
namespace { bool lightTheme = false; }
bool isLightTheme() { return lightTheme; }
void refreshThemePreference()
{
    DWORD appsUseLightTheme=1, size=sizeof(appsUseLightTheme);
    if (appSettings.theme == 2)
        RegGetValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &appsUseLightTheme, &size);
    lightTheme=appSettings.theme == 1 || (appSettings.theme == 2 && appsUseLightTheme != 0);
}

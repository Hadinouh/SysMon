#include "Settings.h"
#include "Startup.h"

#include <shlobj.h>
#include <string>
#include <charconv>
#include <algorithm>
#include "Widget.h"

// These variables still live in Main.cpp for now.
extern HWND desktopWidget;
extern bool widgetEnabled;
extern int savedWidgetX;
extern int savedWidgetY;

SysMonAppSettings appSettings;

static int readSettingInt(const char* section, const char* key, int fallback, const char* path)
{
    char buffer[64]{}; GetPrivateProfileStringA(section,key,"",buffer,sizeof(buffer),path);
    std::string text=buffer;
    const auto first=text.find_first_not_of(" \t");
    if(first==std::string::npos) return fallback;
    text=text.substr(first,text.find_last_not_of(" \t")-first+1);
    int value=0; const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    return parsed.ec==std::errc() && parsed.ptr==text.data()+text.size() ? value : fallback;
}

static bool readSettingBool(const char* section,const char* key,int fallback,const char* path) {
    const int value=readSettingInt(section,key,fallback,path);
    return (value==0 || value==1 ? value : fallback)!=0;
}
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
    saved = writeInt("Overlay", "Style", appSettings.overlayStyle, path) && saved;
    saved = writeInt("Overlay", "StyleVersion", 2, path) && saved;
    saved = writeBool("Overlay", "ShowBackground", appSettings.overlayBackground, path) && saved;
    saved = writeInt("Overlay", "Metrics", appSettings.overlayMetrics, path) && saved;
    saved = writeInt("Overlay", "Opacity", appSettings.overlayOpacity, path) && saved;
    saved = writeInt("Overlay", "Scale", appSettings.overlayScale, path) && saved;
    saved = writeInt("Overlay", "AlwaysOnTop", appSettings.overlayTopmost, path) && saved;
    saved = writeInt("Overlay", "RememberPosition", appSettings.overlayRememberPosition, path) && saved;
    return saved;
}

void loadAppSettings()
{
    const std::string path = getSettingsPath();

    appSettings.startWithWindows =
        readSettingBool("General", "StartWithWindows", 0, path.c_str());
    appSettings.minimizeToTray =
        readSettingBool("General", "MinimizeToTray", 1, path.c_str());
    appSettings.checkForUpdates =
        readSettingBool("General", "CheckForUpdates", 0, path.c_str());

    appSettings.theme = clampInt(readSettingInt("General", "Theme", 0, path.c_str()), 0, 2);
    refreshThemePreference();

    appSettings.updateIntervalMs = clampInt(
        readSettingInt("Monitoring", "UpdateIntervalMs", 500, path.c_str()),
        250,
        10000
    );
    appSettings.temperatureUnit = clampInt(
        readSettingInt("Monitoring", "TemperatureUnit", 0, path.c_str()),
        0,
        1
    );
    appSettings.networkUnit = clampInt(
        readSettingInt("Monitoring", "NetworkUnit", 0, path.c_str()),
        0,
        2
    );
    appSettings.showInTray =
        readSettingBool("Monitoring", "ShowInTray", 1, path.c_str());
    if (!appSettings.showInTray) appSettings.minimizeToTray=false;
    appSettings.playAlerts =
        readSettingBool("Monitoring", "PlayAlerts", 1, path.c_str());

    appSettings.accentColorIndex = clampInt(
        readSettingInt("Appearance", "AccentColorIndex", 0, path.c_str()),
        0,
        5
    );
    appSettings.transparencyPercent = clampInt(
        readSettingInt("Appearance", "TransparencyPercent", 100, path.c_str()),
        55,
        100
    );
    appSettings.compactMode =
        readSettingBool("Appearance", "CompactMode", 0, path.c_str());
    appSettings.showAnimations =
        readSettingBool("Appearance", "ShowAnimations", 1, path.c_str());
    appSettings.fontSizeIndex = clampInt(
        readSettingInt("Appearance", "FontSizeIndex", 1, path.c_str()),
        0,
        2
    );

    appSettings.cpuTemperatureAlert = clampInt(
        readSettingInt("Alerts", "CpuTemperature", 85, path.c_str()),
        50,
        110
    );
    appSettings.gpuTemperatureAlert = clampInt(
        readSettingInt("Alerts", "GpuTemperature", 85, path.c_str()),
        50,
        110
    );
    appSettings.diskUsageAlert = clampInt(
        readSettingInt("Alerts", "DiskUsage", 90, path.c_str()),
        50,
        100
    );
    appSettings.memoryUsageAlert = clampInt(
        readSettingInt("Alerts", "MemoryUsage", 90, path.c_str()),
        50,
        100
    );
    appSettings.showDesktopNotifications =
        readSettingBool("Alerts", "DesktopNotifications", 1, path.c_str());
    appSettings.logAlertsToFile =
        readSettingBool("Alerts", "LogAlerts", 1, path.c_str());

    appSettings.enableDataLogging =
        readSettingBool("Data", "EnableLogging", 0, path.c_str());
    appSettings.logRetentionDays = clampInt(
        readSettingInt("Data", "RetentionDays", 30, path.c_str()),
        1,
        365
    );
    appSettings.allowAnonymousUsageData = false; // No telemetry service in this version.

    appSettings.showOverlayWidget =
        readSettingBool("Integration", "ShowOverlayWidget", 1, path.c_str());
    appSettings.overlayStyle = clampInt(readSettingInt("Overlay","Style",0,path.c_str()),0,3);
    if(readSettingInt("Overlay","StyleVersion",0,path.c_str())<2) {
        const int replacement[]={2,0,1,3};
        appSettings.overlayStyle=replacement[clampInt(readSettingInt("Overlay","Style",1,path.c_str()),0,3)];
    }
    appSettings.overlayBackground=readSettingBool("Overlay","ShowBackground",1,path.c_str());
    appSettings.overlayMetrics = clampInt(readSettingInt("Overlay","Metrics",3,path.c_str()),1,31);
    appSettings.overlayOpacity = clampInt(readSettingInt("Overlay","Opacity",90,path.c_str()),20,100);
    appSettings.overlayScale = clampInt(readSettingInt("Overlay","Scale",100,path.c_str()),50,200);
    appSettings.overlayTopmost = clampInt(readSettingInt("Overlay","AlwaysOnTop",1,path.c_str()),0,1);
    appSettings.overlayRememberPosition = clampInt(readSettingInt("Overlay","RememberPosition",1,path.c_str()),0,1);
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
    if (desktopWidget == nullptr || !appSettings.overlayRememberPosition)
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
    savedWidgetX=rect.left; savedWidgetY=rect.top;
    MONITORINFOEXA monitor{};monitor.cbSize=sizeof(monitor);
    if(GetMonitorInfoA(MonitorFromWindow(desktopWidget,MONITOR_DEFAULTTONEAREST),&monitor)) {
        WritePrivateProfileStringA("Widget","Monitor",monitor.szDevice,path.c_str());
        writeInt(monitor.szDevice,"OffsetX",rect.left-monitor.rcWork.left,path);
        writeInt(monitor.szDevice,"OffsetY",rect.top-monitor.rcWork.top,path);
    }
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

    savedWidgetX = readSettingInt("Widget", "X", -1, path.c_str());
    savedWidgetY = readSettingInt("Widget", "Y", -1, path.c_str());
    widgetEnabled = readSettingBool("Widget", "Enabled", 1, path.c_str());
}

void positionDesktopWidget(HWND hwnd)
{
    if(!hwnd)return;
    applyOverlayAppearance();
    RECT rect{};GetWindowRect(hwnd,&rect);
    struct Search { std::string name; HMONITOR found=nullptr; } search;
    const auto path=getSettingsPath(); char device[128]{};
    GetPrivateProfileStringA("Widget","Monitor","",device,sizeof(device),path.c_str());search.name=device;
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM data)->BOOL {
        auto* search=reinterpret_cast<Search*>(data);MONITORINFOEXA info{};info.cbSize=sizeof(info);
        if(GetMonitorInfoA(monitor,&info)&&search->name==info.szDevice)search->found=monitor;
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    HMONITOR target=appSettings.overlayRememberPosition?search.found:nullptr;
    if(!target)target=MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXA info{};info.cbSize=sizeof(info);GetMonitorInfoA(target,&info);
    int width=rect.right-rect.left,height=rect.bottom-rect.top;
    int x=info.rcWork.right-width-24,y=info.rcWork.bottom-height-24;
    if(appSettings.overlayRememberPosition && search.found) {
        x=info.rcWork.left+readSettingInt(info.szDevice,"OffsetX",24,path.c_str());
        y=info.rcWork.top+readSettingInt(info.szDevice,"OffsetY",24,path.c_str());
    } else if(appSettings.overlayRememberPosition && search.name.empty() && savedWidgetX!=-1 && savedWidgetY!=-1) { x=savedWidgetX;y=savedWidgetY; }
    x=(std::clamp)(x,static_cast<int>(info.rcWork.left),(std::max)(static_cast<int>(info.rcWork.left),static_cast<int>(info.rcWork.right)-width));
    y=(std::clamp)(y,static_cast<int>(info.rcWork.top),(std::max)(static_cast<int>(info.rcWork.top),static_cast<int>(info.rcWork.bottom)-height));
    SetWindowPos(hwnd,appSettings.overlayTopmost?HWND_TOPMOST:HWND_NOTOPMOST,x,y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
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

void resetSettingsSection(int section) {
    SysMonAppSettings defaults;
    if(section==0)appSettings=defaults;
    if(section==1) {appSettings.startWithWindows=defaults.startWithWindows;appSettings.minimizeToTray=true;appSettings.checkForUpdates=false;appSettings.theme=0;appSettings.showInTray=true;}
    if(section==2) {appSettings.updateIntervalMs=500;appSettings.temperatureUnit=0;appSettings.networkUnit=0;appSettings.showInTray=true;appSettings.playAlerts=true;}
    if(section==3) {appSettings.accentColorIndex=0;appSettings.transparencyPercent=100;appSettings.compactMode=false;appSettings.showAnimations=true;appSettings.fontSizeIndex=1;}
    if(section==4) {appSettings.cpuTemperatureAlert=85;appSettings.gpuTemperatureAlert=85;appSettings.diskUsageAlert=90;appSettings.memoryUsageAlert=90;appSettings.showDesktopNotifications=true;appSettings.logAlertsToFile=true;}
    if(section==5) {appSettings.enableDataLogging=false;appSettings.logRetentionDays=30;appSettings.allowAnonymousUsageData=false;}
    if(section==6) {appSettings.showOverlayWidget=true;appSettings.overlayStyle=0;appSettings.overlayBackground=true;appSettings.overlayMetrics=3;appSettings.overlayOpacity=90;appSettings.overlayScale=100;appSettings.overlayTopmost=true;appSettings.overlayRememberPosition=true;}
}

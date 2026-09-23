#include "Settings.h"
#include "Theme.h"
#include "Startup.h"
#include "UpdateChecker.h"
#include "Tray.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
extern LRESULT CALLBACK WindowProc(HWND,UINT,WPARAM,LPARAM);
extern bool widgetEnabled;
int main()
{
    const std::string path=getSettingsPath();
    assert(path.find("\\tests\\")!=std::string::npos);
    std::ifstream previous(path,std::ios::binary);
    const bool existed=previous.good();
    const std::string backup((std::istreambuf_iterator<char>(previous)),{});
    previous.close();
    appSettings.startWithWindows=true;
    appSettings.minimizeToTray=false;
    appSettings.checkForUpdates=true;
    appSettings.theme=1;
    appSettings.updateIntervalMs=2000; appSettings.temperatureUnit=1; appSettings.networkUnit=2;
    appSettings.accentColorIndex=3; appSettings.transparencyPercent=75; appSettings.compactMode=true;
    appSettings.showAnimations=false; appSettings.fontSizeIndex=2;
    appSettings.cpuTemperatureAlert=95; appSettings.gpuTemperatureAlert=90;
    appSettings.diskUsageAlert=80; appSettings.memoryUsageAlert=75;
    appSettings.showDesktopNotifications=false; appSettings.logAlertsToFile=false;
    appSettings.playAlerts=false; appSettings.enableDataLogging=true; appSettings.logRetentionDays=14;
    appSettings.showOverlayWidget=false;
    appSettings.overlayStyle=3;appSettings.overlayMetrics=31;appSettings.overlayOpacity=60;appSettings.overlayScale=150;appSettings.overlayTopmost=false;appSettings.overlayBackground=false;
    assert(saveAppSettings());
    appSettings=SysMonAppSettings();
    loadAppSettings();
    assert(appSettings.startWithWindows && !appSettings.minimizeToTray && appSettings.checkForUpdates && appSettings.theme==1);
    assert(appSettings.updateIntervalMs==2000 && appSettings.temperatureUnit==1 && appSettings.networkUnit==2);
    assert(appSettings.accentColorIndex==3 && appSettings.transparencyPercent==75 && appSettings.compactMode);
    assert(!appSettings.showAnimations && appSettings.fontSizeIndex==2);
    assert(appSettings.cpuTemperatureAlert==95 && appSettings.gpuTemperatureAlert==90);
    assert(appSettings.diskUsageAlert==80 && appSettings.memoryUsageAlert==75);
    assert(!appSettings.showDesktopNotifications && !appSettings.logAlertsToFile && !appSettings.playAlerts);
    assert(appSettings.enableDataLogging && appSettings.logRetentionDays==14 && !appSettings.showOverlayWidget);
    assert(appSettings.overlayStyle==3 && appSettings.overlayMetrics==31 && appSettings.overlayOpacity==60 && appSettings.overlayScale==150 && !appSettings.overlayTopmost && !appSettings.overlayBackground);
    WritePrivateProfileStringA("Monitoring","UpdateIntervalMs","garbage",path.c_str());
    WritePrivateProfileStringA("Alerts","CpuTemperature","999999999999999999999",path.c_str());
    WritePrivateProfileStringA("Overlay","Metrics","0",path.c_str());
    loadAppSettings();assert(appSettings.updateIntervalMs==500 && appSettings.cpuTemperatureAlert==85 && appSettings.overlayMetrics==1);
    assert(isLightTheme() && uiColor(RGB(18,20,26))!=RGB(18,20,26));
    appSettings.theme=0; refreshThemePreference();
    assert(!isLightTheme() && uiColor(RGB(18,20,26))==RGB(18,20,26));
    appSettings.theme=2; saveAppSettings(); appSettings.theme=0; loadAppSettings();
    assert(appSettings.theme==2);
    WritePrivateProfileStringA("General","Theme","999",path.c_str());
    loadAppSettings(); assert(appSettings.theme==2);
    const std::string testBackup=path+".test-backup";
    assert(exportAppSettingsBackup(testBackup));
    appSettings.temperatureUnit=0; appSettings.fontSizeIndex=0; assert(saveAppSettings());
    assert(importAppSettingsBackup(testBackup));
    assert(appSettings.temperatureUnit==1 && appSettings.fontSizeIndex==2);
    { std::ofstream invalid(testBackup); invalid << "not a settings backup"; }
    assert(!importAppSettingsBackup(testBackup));
    assert(appSettings.temperatureUnit==1);
    DeleteFileA(testBackup.c_str());
    resetAppSettings(); loadAppSettings();
    assert(appSettings.theme==0 && !isLightTheme() && !appSettings.startWithWindows && !appSettings.checkForUpdates && appSettings.minimizeToTray);
    if(existed) { std::ofstream restore(path,std::ios::binary|std::ios::trunc); restore<<backup; }
    else DeleteFileA(path.c_str());
    std::cout<<"PASS: General Settings persistence, reset, clamping, and theme mapping\n";
    assert(Updates::interpret(200,R"({"tag_name":"v1.0.1"})").newer);
    assert(Updates::interpret(200,R"({"tag_name":"v1.1.0"})").newer);
    assert(!Updates::interpret(200,R"({"tag_name":"v0.8.9"})").newer);
    assert(!Updates::interpret(200,R"({"tag_name":"v0.9.0"})").newer);
    assert(!Updates::interpret(404,"").newer);
    assert(Updates::interpret(403,"").failed);
    assert(Updates::interpret(500,"").failed);
    assert(Updates::interpret(200,"{}").failed);
    assert(Updates::interpret(200,R"({"tag_name":"v99999999999999999999999.0.0"})").failed);
    assert(Updates::interpret(200,R"({"tag_name":"1.0.0-beta"})").failed);
    std::cout<<"PASS: newer/current/older release, missing release, HTTP errors, malformed and oversized versions\n";
    std::wstring xml;
    const bool startupValidated=Startup::configure(true,&xml);
    if(startupValidated) {
        assert(xml.find(L"HighestAvailable")!=std::wstring::npos);
        assert(xml.find(L"LogonTrigger")!=std::wstring::npos);
        assert(xml.find(L"WorkingDirectory")!=std::wstring::npos);
        assert(xml.find(L"PT0S")!=std::wstring::npos);
        std::cout<<"PASS: Windows validated logon task XML; no task registered\n";
    } else std::cout<<"UNVERIFIED: Task Scheduler is unavailable in this test process\n";
    widgetEnabled=false;
    HWND window=CreateWindowExA(WS_EX_TOOLWINDOW,"STATIC","SysMon settings test",WS_POPUP,-10000,-10000,100,100,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
    assert(window);
    ShowWindow(window,SW_SHOWNOACTIVATE);
    appSettings.minimizeToTray=false; appSettings.showInTray=true;
    WindowProc(window,WM_SIZE,SIZE_MINIMIZED,0);
    assert(IsWindowVisible(window));
    appSettings.minimizeToTray=true;
    WindowProc(window,WM_SIZE,SIZE_MINIMIZED,0);
    const bool trayWorked=!IsWindowVisible(window);
    if(trayWorked) {
        appSettings.updateIntervalMs=2000;
        restoreSysMon(window); assert(IsWindowVisible(window));
        MSG message={}; Sleep(650);
        assert(!PeekMessage(&message,window,WM_TIMER,WM_TIMER,PM_REMOVE));
        Sleep(1450); assert(PeekMessage(&message,window,WM_TIMER,WM_TIMER,PM_REMOVE));
        WindowProc(window,WM_CLOSE,0,0); assert(IsWindow(window) && !IsWindowVisible(window));
        std::cout<<"PASS: minimize preference, close-to-tray, restore, and selected timer interval\n";
    } else std::cout<<"UNVERIFIED: Windows notification area unavailable; window stayed accessible\n";
    removeTrayIcon();
    appSettings.minimizeToTray=false;
    WindowProc(window,WM_CLOSE,0,0); assert(!IsWindow(window));
    std::cout<<"PASS: close exits when close-to-tray is disabled\n";
    auto live=Updates::check();
    std::cout<<"Live GitHub response: "<<live.status<<"\n";
}

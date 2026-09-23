#include "Tray.h"
#include "Stats.h"
#include "Settings.h"
#include <algorithm>

#include <shellapi.h>


extern HWND desktopWidget;


NOTIFYICONDATAA trayIcon = {};


bool addTrayIcon(HWND hwnd)
{
    trayIcon = {};

    trayIcon.cbSize =
        sizeof(NOTIFYICONDATAA);

    trayIcon.hWnd =
        hwnd;

    trayIcon.uID =
        1;

    trayIcon.uFlags =
        NIF_ICON |
        NIF_MESSAGE |
        NIF_TIP;

    trayIcon.uCallbackMessage =
        WM_TRAYICON;

    trayIcon.hIcon =
        LoadIcon(
            GetModuleHandle(nullptr),
            MAKEINTRESOURCE(1)
        );

    if (!trayIcon.hIcon) trayIcon.hIcon = LoadIcon(nullptr, IDI_APPLICATION);

    strcpy_s(
        trayIcon.szTip,
        "SysMon"
    );

    if (Shell_NotifyIconA(NIM_MODIFY, &trayIcon)) return true;
    return Shell_NotifyIconA(NIM_ADD, &trayIcon) != FALSE;
}


void removeTrayIcon()
{
    Shell_NotifyIconA(
        NIM_DELETE,
        &trayIcon
    );
}


void restoreSysMon(HWND hwnd)
{
    if (desktopWidget != nullptr)
    {
        KillTimer(
            desktopWidget,
            2
        );

        ShowWindow(
            desktopWidget,
            SW_HIDE
        );
    }

    // Show the cached frame immediately, then let the normal timer collect
    // fresh data. A synchronous updateStats() here made restore-from-tray feel
    // sticky because hardware I/O ran before the window became visible.
    SetTimer(
        hwnd,
        1,
        static_cast<UINT>((std::max)(250, appSettings.updateIntervalMs)),
        nullptr
    );

    ShowWindow(
        hwnd,
        SW_SHOW
    );

    ShowWindow(
        hwnd,
        SW_RESTORE
    );

    InvalidateRect(
        hwnd,
        nullptr,
        FALSE
    );

    UpdateWindow(
        hwnd
    );

    SetForegroundWindow(
        hwnd
    );

    if (!appSettings.showInTray) removeTrayIcon();
}

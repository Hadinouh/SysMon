#include "Tray.h"
#include "Stats.h"

#include <shellapi.h>


extern HWND desktopWidget;


NOTIFYICONDATAA trayIcon = {};


void addTrayIcon(HWND hwnd)
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
            nullptr,
            IDI_APPLICATION
        );

    strcpy_s(
        trayIcon.szTip,
        "SysMon"
    );

    Shell_NotifyIconA(
        NIM_ADD,
        &trayIcon
    );
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

    // Restart normal SysMon updates
    SetTimer(
        hwnd,
        1,
        1000,
        nullptr
    );

    updateStats();

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

    removeTrayIcon();
}
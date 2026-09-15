#include <windows.h>
#include <shellapi.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include "Settings.h"
#include "Stats.h"
#include "Tray.h"
#include "Widget.h"
#include "UI.h"
void drawDashboard(
    HWND hwnd,
    HDC hdc
);
RECT widgetToggleRect =
{
    600,
    575,
    755,
    605
};
// Desktop widget
int savedWidgetX = -1;
int savedWidgetY = -1;
// Fonts
HFONT titleFont;
HFONT subtitleFont;
HFONT labelFont;
HFONT bigFont;
HFONT smallFont;
LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        titleFont =
            CreateFontA(
                36,
                0,
                0,
                0,
                FW_BOLD,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                "Segoe UI"
            );

        subtitleFont =
            CreateFontA(
                17,
                0,
                0,
                0,
                FW_NORMAL,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                "Segoe UI"
            );

        labelFont =
            CreateFontA(
                18,
                0,
                0,
                0,
                FW_SEMIBOLD,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                "Segoe UI"
            );

        bigFont =
            CreateFontA(
                40,
                0,
                0,
                0,
                FW_BOLD,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                "Segoe UI"
            );

        smallFont =
            CreateFontA(
                15,
                0,
                0,
                0,
                FW_NORMAL,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                "Segoe UI"
            );

widgetLabelFont =
    CreateFontA(
        17,
        0,
        0,
        0,
        FW_SEMIBOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH,
        "Segoe UI"
    );

widgetValueFont =
    CreateFontA(
        22,
        0,
        0,
        0,
        FW_BOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH,
        "Segoe UI"
    );
        // Initialize CPU counters
        getCpuUsage();

        updateStats();

        SetTimer(
            hwnd,
            1,
            1000,
            nullptr
        );

        return 0;
    }
    
    case WM_LBUTTONDOWN:
    
{
    int mouseX =
        LOWORD(lParam);

    int mouseY =
        HIWORD(lParam);

    if (
        mouseX >= 610 &&
        mouseX <= 755 &&
        mouseY >= 573 &&
        mouseY <= 605
    )
    {
        widgetEnabled =
            !widgetEnabled;
saveWidgetEnabled();
        if (
            !widgetEnabled &&
            desktopWidget != nullptr
        )
        {
            ShowWindow(
                desktopWidget,
                SW_HIDE
            );
        }

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );
    }

    return 0;
}
case WM_SIZE:
{
    if (wParam == SIZE_MINIMIZED)
    {
        // Hide SysMon so it disappears
        // completely from the taskbar.
        ShowWindow(
            hwnd,
            SW_HIDE
        );

        addTrayIcon(
            hwnd
        );

        // Only show desktop widget
        // if the user has enabled it.
       if (
    widgetEnabled &&
    desktopWidget != nullptr
)
{
    // Stop the main-window timer
    KillTimer(
        hwnd,
        1
    );

    updateStats();

    ShowWindow(
        desktopWidget,
        SW_SHOWNOACTIVATE
    );

    // Widget gets its own live timer
    SetTimer(
        desktopWidget,
        2,
        1000,
        nullptr
    );

    InvalidateRect(
        desktopWidget,
        nullptr,
        TRUE
    );

    UpdateWindow(
        desktopWidget
    );
}
    }

    return 0;
}
case WM_TIMER:
{
    if (wParam == 1)
    {
        // Read fresh CPU, RAM, disk and uptime values
        updateStats();

        // Refresh the main dashboard if it is open
        if (IsWindowVisible(hwnd))
        {
            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );
        }

        // Refresh the desktop widget if it is visible
        if (
            widgetEnabled &&
            desktopWidget != nullptr &&
            IsWindowVisible(desktopWidget)
        )
        {
            InvalidateRect(
                desktopWidget,
                nullptr,
                TRUE
            );

            UpdateWindow(
                desktopWidget
            );
        }
    }

    return 0;
}
case WM_TRAYICON:
{
    if (
        lParam == WM_LBUTTONDBLCLK
    )
    {
        restoreSysMon(
            hwnd
        );
    }

    if (
        lParam == WM_RBUTTONUP
    )
    {
        POINT cursor;

        GetCursorPos(
            &cursor
        );

        HMENU menu =
            CreatePopupMenu();

        AppendMenuA(
            menu,
            MF_STRING,
            ID_TRAY_OPEN,
            "Open SysMon"
        );

        AppendMenuA(
            menu,
            MF_SEPARATOR,
            0,
            nullptr
        );

        AppendMenuA(
            menu,
            MF_STRING,
            ID_TRAY_EXIT,
            "Exit"
        );

        SetForegroundWindow(
            hwnd
        );

        int command =
            TrackPopupMenu(
                menu,
                TPM_RETURNCMD |
                TPM_RIGHTBUTTON,

                cursor.x,
                cursor.y,

                0,

                hwnd,
                nullptr
            );

        DestroyMenu(
            menu
        );

        if (
            command ==
            ID_TRAY_OPEN
        )
        {
            restoreSysMon(
                hwnd
            );
        }

        if (
            command ==
            ID_TRAY_EXIT
        )
        {
            removeTrayIcon();

            DestroyWindow(
                hwnd
            );
        }
    }

    return 0;
}
    case WM_PAINT:
    {
        PAINTSTRUCT ps;

        HDC hdc =
            BeginPaint(
                hwnd,
                &ps
            );

        drawDashboard(
            hwnd,
            hdc
        );

        EndPaint(
            hwnd,
            &ps
        );

        return 0;
    }


    case WM_ERASEBKGND:
        return 1;


   case WM_DESTROY:
{
    KillTimer(
        hwnd,
        1
    );

    DeleteObject(titleFont);
    DeleteObject(subtitleFont);
    DeleteObject(labelFont);
    DeleteObject(bigFont);
    DeleteObject(smallFont);

    DeleteObject(widgetLabelFont);
    DeleteObject(widgetValueFont);

removeTrayIcon();
    PostQuitMessage(0);

    return 0;
}
    }

    return DefWindowProc(
        hwnd,
        message,
        wParam,
        lParam
    );
}


// ============================================================
// Program entry
// ============================================================

int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE,
    LPSTR,
    int nCmdShow)
{
    SetProcessDPIAware();

    const char CLASS_NAME[] =
        "SysMonWindowClass";

    WNDCLASSA wc = {};

    wc.lpfnWndProc =
        WindowProc;

    wc.hInstance =
        hInstance;

    wc.lpszClassName =
        CLASS_NAME;

    wc.hCursor =
        LoadCursor(
            nullptr,
            IDC_ARROW
        );

    wc.hbrBackground =
        nullptr;

    if (!RegisterClassA(&wc))
        return 0;
const char WIDGET_CLASS_NAME[] =
    "SysMonDesktopWidget";

WNDCLASSA widgetClass = {};

widgetClass.lpfnWndProc =
    WidgetProc;

widgetClass.hInstance =
    hInstance;

widgetClass.lpszClassName =
    WIDGET_CLASS_NAME;

widgetClass.hCursor =
    LoadCursor(
        nullptr,
        IDC_ARROW
    );

widgetClass.hbrBackground =
    nullptr;

if (!RegisterClassA(&widgetClass))
    return 0;

    HWND hwnd =
        CreateWindowExA(
            0,
            CLASS_NAME,
            "SysMon - Windows System Monitor",

            WS_OVERLAPPED |
            WS_CAPTION |
            WS_SYSMENU |
            WS_MINIMIZEBOX,

            CW_USEDEFAULT,
            CW_USEDEFAULT,

            810,
            625,

            nullptr,
            nullptr,
            hInstance,
            nullptr
        );


    if (hwnd == nullptr)
        return 0;
desktopWidget =
    CreateWindowExA(
        WS_EX_LAYERED |
        WS_EX_TOOLWINDOW |
        WS_EX_NOACTIVATE,

        WIDGET_CLASS_NAME,

        "",

        WS_POPUP,

        0,
        0,
        240,
        100,

        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

if (desktopWidget != nullptr)
{
    SetLayeredWindowAttributes(
        desktopWidget,
        WIDGET_TRANSPARENT,
        0,
        LWA_COLORKEY
    );

    // Load saved widget position
    loadWidgetSettings();

    positionDesktopWidget(
        desktopWidget
    );
}

    ShowWindow(
        hwnd,
        nCmdShow
    );

    UpdateWindow(
        hwnd
    );


    MSG msg = {};

    while (GetMessage(
        &msg,
        nullptr,
        0,
        0))
    {
        TranslateMessage(
            &msg
        );

        DispatchMessage(
            &msg
        );
    }

    return 0;
}
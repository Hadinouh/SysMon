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
#include "Processes.h"
int processMaxScrollOffset = 0;

int processScrollbarThumbTop = 221;
int processScrollbarThumbBottom = 261;

bool processScrollbarDragging = false;
int processScrollbarDragOffsetY = 0;

AppPage currentPage =
    AppPage::Dashboard;
    int processScrollOffset = 0;
    ProcessSort processSort =
    ProcessSort::Memory;
bool processSortDescending =
    true;
    PerformanceView performanceView =
    PerformanceView::CPU;
    int selectedDiskIndex = 0;
    int systemInfoScrollOffset = 0;
    int systemInfoMaxScrollOffset = 0;
    std::string processSearch = "";
    bool processSearchFocused = false;
    DWORD selectedProcessPid = MAXDWORD;
    DWORD hoveredProcessPid = MAXDWORD;
    std::vector<DWORD> visibleProcessPids;
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
            500,
            nullptr
        );

        return 0;
    }



case WM_LBUTTONUP:
{
    processScrollbarDragging = false;
 break;
}
case WM_MOUSEMOVE:
{
    // Stop dragging when the real mouse button is released
    if (
        processScrollbarDragging &&
        (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0
    )
    {
        processScrollbarDragging = false;
    }


    // --------------------------------------------------------
    // PROCESS SCROLLBAR DRAGGING
    // --------------------------------------------------------

    if (
        currentPage == AppPage::Processes &&
        processScrollbarDragging
    )
    {
        int mouseY =
            HIWORD(lParam);

        const int trackTop = 221;
        const int trackBottom = 513;

        int thumbHeight =
            processScrollbarThumbBottom -
            processScrollbarThumbTop;

        int thumbTravel =
            (trackBottom - trackTop) -
            thumbHeight;

        int newThumbTop =
            mouseY -
            processScrollbarDragOffsetY;

        if (newThumbTop < trackTop)
        {
            newThumbTop = trackTop;
        }

        if (
            newThumbTop >
            trackTop + thumbTravel
        )
        {
            newThumbTop =
                trackTop + thumbTravel;
        }

        if (
            thumbTravel > 0 &&
            processMaxScrollOffset > 0
        )
        {
            double ratio =
                static_cast<double>(
                    newThumbTop - trackTop
                ) /
                static_cast<double>(
                    thumbTravel
                );

            processScrollOffset =
                static_cast<int>(
                    ratio *
                    processMaxScrollOffset
                );
        }

        hoveredProcessPid = MAXDWORD;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }


    // --------------------------------------------------------
    // PROCESS ROW HOVER
    // --------------------------------------------------------

    if (currentPage == AppPage::Processes)
    {
        int mouseX =
            LOWORD(lParam);

        int mouseY =
            HIWORD(lParam);

        DWORD newHoveredPid =
            MAXDWORD;

        if (
            mouseX >= 285 &&
            mouseX <= 1115 &&
            mouseY >= 205 &&
            mouseY < 529
        )
        {
            int rowIndex =
                (mouseY - 205) / 27;

            if (
                rowIndex >= 0 &&
                rowIndex <
                    static_cast<int>(
                        visibleProcessPids.size()
                    )
            )
            {
                newHoveredPid =
                    visibleProcessPids[
                        rowIndex
                    ];
            }
        }

        if (
            newHoveredPid !=
            hoveredProcessPid
        )
        {
            hoveredProcessPid =
                newHoveredPid;

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );
        }
    }

    break;
}

   case WM_MOUSEWHEEL:
{
    if (currentPage == AppPage::SystemInfo)
    {
        short wheelDelta =
            GET_WHEEL_DELTA_WPARAM(
                wParam
            );

        systemInfoScrollOffset -=
            (wheelDelta / WHEEL_DELTA) * 70;

        systemInfoScrollOffset =
            std::clamp(
                systemInfoScrollOffset,
                0,
                systemInfoMaxScrollOffset
            );

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }

    if (currentPage == AppPage::Processes)
    {
        short wheelDelta =
            GET_WHEEL_DELTA_WPARAM(
                wParam
            );

        if (wheelDelta < 0)
        {
            processScrollOffset += 3;
        }
        else if (wheelDelta > 0)
        {
            processScrollOffset -= 3;
        }

        if (processScrollOffset < 0)
        {
            processScrollOffset = 0;
        }

        if (
            processScrollOffset >
            processMaxScrollOffset
        )
        {
            processScrollOffset =
                processMaxScrollOffset;
        }

        hoveredProcessPid =
            MAXDWORD;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }

    break;
}
case WM_CHAR:
{
    if (
        currentPage == AppPage::Processes &&
        processSearchFocused
    )
    {
        // Backspace
        if (wParam == 8)
        {
            if (!processSearch.empty())
            {
                processSearch.pop_back();
            }
        }

        // Printable characters
        else if (
            wParam >= 32 &&
            wParam <= 126
        )
        {
            processSearch +=
                static_cast<char>(
                    wParam
                );
        }

        processScrollOffset = 0;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }

    break;
}


case WM_LBUTTONDOWN:
{
    int mouseX =
        LOWORD(lParam);

    int mouseY =
        HIWORD(lParam);
        // --------------------------------------------------------
// PERFORMANCE RESOURCE CARD CLICKS
// --------------------------------------------------------

if (
    currentPage == AppPage::Performance &&
    mouseX >= 280 &&
    mouseX <= 460
)
{
    // CPU
    if (
        mouseY >= 135 &&
        mouseY <= 205
    )
    {
        performanceView =
            PerformanceView::CPU;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }

    // Memory
    if (
        mouseY >= 225 &&
        mouseY <= 295
    )
    {
        performanceView =
            PerformanceView::Memory;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }

    // Physical disk cards
    int visibleDiskCards =
        performanceVisibleDiskCardCount(
            diskStats.size()
        );

    for (
        int index = 0;
        index < visibleDiskCards;
        index++
    )
    {
        int top =
            performanceDiskCardTop(
                index
            );

        int bottom =
            top +
            performanceDiskCardHeight;

        if (
            mouseY >= top &&
            mouseY <= bottom
        )
        {
            performanceView =
                PerformanceView::Disk;

            if (
                index <
                static_cast<int>(
                    diskStats.size()
                )
            )
            {
                selectedDiskIndex =
                    index;
            }

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );

            return 0;
        }
    }

    int gpuTop =
        performanceGpuCardTop(
            diskStats.size()
        );

    if (
        mouseY >= gpuTop &&
        mouseY <=
            gpuTop +
            performanceDiskCardHeight
    )
    {
        performanceView =
            PerformanceView::GPU;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }

    int networkTop =
        performanceNetworkCardTop(
            diskStats.size()
        );

    if (
        mouseY >= networkTop &&
        mouseY <=
            networkTop +
            performanceDiskCardHeight
    )
    {
        performanceView =
            PerformanceView::Network;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }
}
// --------------------------------------------------------
// PROCESS SCROLLBAR
// --------------------------------------------------------

if (
    currentPage == AppPage::Processes &&
    mouseX >= 1114 &&
    mouseX <= 1136
)
{
    // Top arrow
    if (
        mouseY >= 205 &&
        mouseY <= 221
    )
    {
        processScrollOffset -= 3;

        if (processScrollOffset < 0)
        {
            processScrollOffset = 0;
        }

        hoveredProcessPid = MAXDWORD;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }


    // Bottom arrow
    if (
        mouseY >= 513 &&
        mouseY <= 529
    )
    {
        processScrollOffset += 3;

        if (
            processScrollOffset >
            processMaxScrollOffset
        )
        {
            processScrollOffset =
                processMaxScrollOffset;
        }

        hoveredProcessPid = MAXDWORD;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }


    // Drag scrollbar thumb
    if (
        mouseY >= processScrollbarThumbTop &&
        mouseY <= processScrollbarThumbBottom
    )
    {
        processScrollbarDragging =
            true;

        processScrollbarDragOffsetY =
            mouseY -
            processScrollbarThumbTop;

        return 0;
    }


    // Page up
    if (
        mouseY > 221 &&
        mouseY < processScrollbarThumbTop
    )
    {
        processScrollOffset -= 12;

        if (processScrollOffset < 0)
        {
            processScrollOffset = 0;
        }

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }


    // Page down
    if (
        mouseY > processScrollbarThumbBottom &&
        mouseY < 513
    )
    {
        processScrollOffset += 12;

        if (
            processScrollOffset >
            processMaxScrollOffset
        )
        {
            processScrollOffset =
                processMaxScrollOffset;
        }

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }
}

    // --------------------------------------------------------
    // CLEAR PROCESS SEARCH FOCUS ON ANY CLICK
    // --------------------------------------------------------

    if (currentPage == AppPage::Processes)
    {
        processSearchFocused = false;
    }


    // --------------------------------------------------------
    // SIDEBAR NAVIGATION
    // --------------------------------------------------------

    // Dashboard
    if (
        mouseX >= 35 &&
        mouseX <= 185 &&
        mouseY >= 125 &&
        mouseY <= 170
    )
    {
        currentPage =
            AppPage::Dashboard;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }


    // Processes
    if (
        mouseX >= 35 &&
        mouseX <= 185 &&
        mouseY >= 175 &&
        mouseY <= 220
    )
    {
        currentPage =
            AppPage::Processes;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }


    // Performance
    if (
        mouseX >= 35 &&
        mouseX <= 185 &&
        mouseY >= 225 &&
        mouseY <= 270
    )
    {
        currentPage =
            AppPage::Performance;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }


    // System Info
    if (
        mouseX >= 35 &&
        mouseX <= 185 &&
        mouseY >= 275 &&
        mouseY <= 320
    )
    {
        currentPage =
            AppPage::SystemInfo;

        systemInfoScrollOffset = 0;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }


    // Settings
    if (
        mouseX >= 35 &&
        mouseX <= 185 &&
        mouseY >= 325 &&
        mouseY <= 370
    )
    {
        currentPage =
            AppPage::Settings;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }


    // --------------------------------------------------------
    // PROCESS SEARCH BOX
    // --------------------------------------------------------
// Clear search button
if (
    currentPage == AppPage::Processes &&
    !processSearch.empty() &&
    mouseX >= 545 &&
    mouseX <= 575 &&
    mouseY >= 118 &&
    mouseY <= 147
)
{
    processSearch.clear();

    processScrollOffset = 0;

    processSearchFocused = true;

    InvalidateRect(
        hwnd,
        nullptr,
        FALSE
    );

    return 0;
}
    if (
        currentPage == AppPage::Processes &&
        mouseX >= 285 &&
        mouseX <= 575 &&
        mouseY >= 120 &&
        mouseY <= 145
    )
    {
        processSearchFocused = true;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }


    // --------------------------------------------------------
    // PROCESS TABLE HEADER CLICKS
    // --------------------------------------------------------

    if (
        currentPage == AppPage::Processes &&
        mouseY >= 155 &&
        mouseY <= 190
    )
    {
        ProcessSort newSort =
            processSort;

        bool headerClicked =
            true;


        // PROCESS
        if (
            mouseX >= 285 &&
            mouseX < 680
        )
        {
            newSort =
                ProcessSort::Name;
        }

        // CPU
        else if (
            mouseX >= 680 &&
            mouseX < 790
        )
        {
            newSort =
                ProcessSort::CPU;
        }

        // MEMORY
        else if (
            mouseX >= 790 &&
            mouseX < 920
        )
        {
            newSort =
                ProcessSort::Memory;
        }

        // THREADS
        else if (
            mouseX >= 920 &&
            mouseX < 1040
        )
        {
            newSort =
                ProcessSort::Threads;
        }

        // PID
        else if (
            mouseX >= 1040 &&
            mouseX <= 1115
        )
        {
            newSort =
                ProcessSort::PID;
        }

        else
        {
            headerClicked =
                false;
        }


        if (headerClicked)
        {
            if (newSort == processSort)
            {
                processSortDescending =
                    !processSortDescending;
            }
            else
            {
                processSort =
                    newSort;

                processSortDescending =
                    processSort !=
                    ProcessSort::Name;
            }

            processScrollOffset = 0;

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );

            return 0;
        }
    }

    // --------------------------------------------------------
// PROCESS ROW SELECTION
// --------------------------------------------------------

if (
    currentPage == AppPage::Processes &&
    mouseX >= 285 &&
    mouseX <= 1115 &&
    mouseY >= 205 &&
    mouseY < 529
)
{
    int rowIndex =
        (mouseY - 205) / 27;

    if (
        rowIndex >= 0 &&
        rowIndex <
            static_cast<int>(
                visibleProcessPids.size()
            )
    )
    {
        selectedProcessPid =
            visibleProcessPids[
                rowIndex
            ];

        processSearchFocused =
            false;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }
}
// --------------------------------------------------------
// END PROCESS BUTTON
// --------------------------------------------------------

if (
    currentPage == AppPage::Processes &&
    selectedProcessPid != MAXDWORD &&
    selectedProcessPid != 0 &&
    selectedProcessPid != 4 &&
    selectedProcessPid != GetCurrentProcessId() &&
    mouseX >= 960 &&
    mouseX <= 1100 &&
    mouseY >= 575 &&
    mouseY <= 600
)
{
    int result =
        MessageBoxA(
            hwnd,
            "Are you sure you want to end this task and its related processes?",
            "Confirm End Process",
            MB_YESNO |
            MB_ICONWARNING |
            MB_DEFBUTTON2
        );

    if (result == IDYES)
    {
        if (
          terminateTaskByPid(
    selectedProcessPid
)
        )
        {
            MessageBoxA(
                hwnd,
                "The task was ended successfully.",
                "SysMon",
                MB_OK |
                MB_ICONINFORMATION
            );

            selectedProcessPid =
                MAXDWORD;

            processScrollOffset =
                0;

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );
        }
        else
        {
            MessageBoxA(
                hwnd,
                "SysMon could not terminate this process.\n\nIt may be protected by Windows or require administrator privileges.",
                "Unable to End Process",
                MB_OK |
                MB_ICONERROR
            );
        }
    }

    return 0;
}

    // --------------------------------------------------------
    // DESKTOP WIDGET TOGGLE
    // --------------------------------------------------------

    if (
        currentPage == AppPage::Dashboard &&
        mouseX >= 875 &&
        mouseX <= 1090 &&
        mouseY >= 525 &&
        mouseY <= 557
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
        return 0;
    }


    break;

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
        500,
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
case WM_DEVICECHANGE:
{
    // Refresh the About PC connected-device / network data
    // immediately when Windows reports a hardware change.
    refreshSystemInfo(true);

    if (currentPage == AppPage::SystemInfo)
    {
        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );
    }

    break;
}

case WM_TIMER:
{
    if (wParam == 1)
    {
        // Read fresh CPU, RAM, disk and uptime values
        updateStats();

        if (
            selectedDiskIndex >=
                static_cast<int>(
                    diskStats.size()
                )
        )
        {
            selectedDiskIndex = 0;
        }

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

    RECT client;

    GetClientRect(
        hwnd,
        &client
    );


    // Create an off-screen drawing surface
    HDC memoryDC =
        CreateCompatibleDC(
            hdc
        );

    HBITMAP memoryBitmap =
        CreateCompatibleBitmap(
            hdc,
            client.right,
            client.bottom
        );

    HGDIOBJ oldBitmap =
        SelectObject(
            memoryDC,
            memoryBitmap
        );


    // Draw the entire dashboard off-screen
    drawDashboard(
        hwnd,
        memoryDC
    );


    // Copy the finished frame to the window
    BitBlt(
        hdc,
        0,
        0,
        client.right,
        client.bottom,
        memoryDC,
        0,
        0,
        SRCCOPY
    );


    // Cleanup
    SelectObject(
        memoryDC,
        oldBitmap
    );

    DeleteObject(
        memoryBitmap
    );

    DeleteDC(
        memoryDC
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

            1200,
            760,

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
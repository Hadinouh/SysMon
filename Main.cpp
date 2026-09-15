#include <windows.h>
#include <shellapi.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>

// ============================================================
// Live system data
// ============================================================
// ============================================================
// Live system data
// ============================================================
RECT widgetToggleRect =
{
    600,
    575,
    755,
    605
};
double cpuUsage = 0.0;

double usedRamGB = 0.0;
double totalRamGB = 0.0;
int ramPercent = 0;

double usedDiskGB = 0.0;
double totalDiskGB = 0.0;
int diskPercent = 0;

ULONGLONG uptimeSeconds = 0;


// Desktop widget
HWND desktopWidget = nullptr;

HFONT widgetLabelFont = nullptr;
HFONT widgetValueFont = nullptr;
bool widgetEnabled = true;

NOTIFYICONDATAA trayIcon = {};

#define WM_TRAYICON (WM_APP + 1)

#define ID_TRAY_OPEN 1001
#define ID_TRAY_EXIT 1002
const COLORREF WIDGET_TRANSPARENT =
    RGB(1, 2, 3);
// ============================================================
// CPU tracking
// ============================================================

ULONGLONG previousIdle = 0;
ULONGLONG previousKernel = 0;
ULONGLONG previousUser = 0;


// ============================================================
// Fonts
// ============================================================

HFONT titleFont;
HFONT subtitleFont;
HFONT labelFont;
HFONT bigFont;
HFONT smallFont;


// ============================================================
// Convert FILETIME
// ============================================================

ULONGLONG fileTimeToULL(const FILETIME& ft)
{
    ULARGE_INTEGER value;

    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;

    return value.QuadPart;
}


// ============================================================
// CPU Usage
// ============================================================

double getCpuUsage()
{
    FILETIME idleTime;
    FILETIME kernelTime;
    FILETIME userTime;

    if (!GetSystemTimes(
        &idleTime,
        &kernelTime,
        &userTime))
    {
        return 0.0;
    }

    ULONGLONG idle =
        fileTimeToULL(idleTime);

    ULONGLONG kernel =
        fileTimeToULL(kernelTime);

    ULONGLONG user =
        fileTimeToULL(userTime);

    if (previousKernel == 0)
    {
        previousIdle = idle;
        previousKernel = kernel;
        previousUser = user;

        return 0.0;
    }

    ULONGLONG idleDiff =
        idle - previousIdle;

    ULONGLONG kernelDiff =
        kernel - previousKernel;

    ULONGLONG userDiff =
        user - previousUser;

    ULONGLONG total =
        kernelDiff + userDiff;

    previousIdle = idle;
    previousKernel = kernel;
    previousUser = user;

    if (total == 0)
        return 0.0;

    double usage =
        100.0 *
        (1.0 -
            static_cast<double>(idleDiff)
            / total);

    return std::clamp(
        usage,
        0.0,
        100.0
    );
}


// ============================================================
// Update system data
// ============================================================

void updateStats()
{
    // CPU
    cpuUsage = getCpuUsage();


    // RAM
    MEMORYSTATUSEX memory = {};

    memory.dwLength =
        sizeof(memory);

    if (GlobalMemoryStatusEx(&memory))
    {
        totalRamGB =
            memory.ullTotalPhys /
            (1024.0 * 1024.0 * 1024.0);

        double available =
            memory.ullAvailPhys /
            (1024.0 * 1024.0 * 1024.0);

        usedRamGB =
            totalRamGB - available;

        ramPercent =
            static_cast<int>(
                (usedRamGB / totalRamGB)
                * 100.0
            );
    }


    // Disk
    ULARGE_INTEGER freeAvailable;
    ULARGE_INTEGER totalBytes;
    ULARGE_INTEGER freeBytes;

    if (GetDiskFreeSpaceExA(
        "C:\\",
        &freeAvailable,
        &totalBytes,
        &freeBytes))
    {
        totalDiskGB =
            totalBytes.QuadPart /
            (1024.0 * 1024.0 * 1024.0);

        double freeDiskGB =
            freeBytes.QuadPart /
            (1024.0 * 1024.0 * 1024.0);

        usedDiskGB =
            totalDiskGB - freeDiskGB;

        diskPercent =
            static_cast<int>(
                (usedDiskGB / totalDiskGB)
                * 100.0
            );
    }


    // Uptime
    uptimeSeconds =
        GetTickCount64() / 1000;
}
void positionDesktopWidget(HWND hwnd)
{
    RECT workArea;

    SystemParametersInfoA(
        SPI_GETWORKAREA,
        0,
        &workArea,
        0
    );

    const int widgetWidth = 240;
    const int widgetHeight = 100;

    int x =
        workArea.right -
        widgetWidth -
        25;

    int y =
        workArea.bottom -
        widgetHeight -
        25;

    SetWindowPos(
        hwnd,
        nullptr,
        x,
        y,
        widgetWidth,
        widgetHeight,
        SWP_NOZORDER |
        SWP_NOACTIVATE
    );
}
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
// ============================================================
// Drawing helpers
// ============================================================

void setFont(
    HDC hdc,
    HFONT font)
{
    SelectObject(
        hdc,
        font
    );
}


void drawText(
    HDC hdc,
    const std::string& text,
    int x,
    int y,
    COLORREF color,
    HFONT font)
{
    SetBkMode(
        hdc,
        TRANSPARENT
    );

    SetTextColor(
        hdc,
        color
    );

    setFont(
        hdc,
        font
    );

    TextOutA(
        hdc,
        x,
        y,
        text.c_str(),
        static_cast<int>(
            text.length()
        )
    );
}


void drawRoundedBox(
    HDC hdc,
    int left,
    int top,
    int right,
    int bottom,
    COLORREF color)
{
    HBRUSH brush =
        CreateSolidBrush(color);

    HPEN pen =
        CreatePen(
            PS_SOLID,
            1,
            color
        );

    HGDIOBJ oldBrush =
        SelectObject(
            hdc,
            brush
        );

    HGDIOBJ oldPen =
        SelectObject(
            hdc,
            pen
        );

    RoundRect(
        hdc,
        left,
        top,
        right,
        bottom,
        18,
        18
    );

    SelectObject(
        hdc,
        oldBrush
    );

    SelectObject(
        hdc,
        oldPen
    );

    DeleteObject(brush);
    DeleteObject(pen);
}


void drawProgressBar(
    HDC hdc,
    int x,
    int y,
    int width,
    int height,
    double percent)
{
    COLORREF trackColor =
        RGB(45, 48, 58);

    COLORREF fillColor =
        RGB(66, 135, 245);

    // Background
    drawRoundedBox(
        hdc,
        x,
        y,
        x + width,
        y + height,
        trackColor
    );

    int fillWidth =
        static_cast<int>(
            width *
            (percent / 100.0)
        );

    if (fillWidth > 0)
    {
        drawRoundedBox(
            hdc,
            x,
            y,
            x + fillWidth,
            y + height,
            fillColor
        );
    }
}


// ============================================================
// Dashboard drawing
// ============================================================

void drawDashboard(
    HWND hwnd,
    HDC hdc)
{
    RECT client;

    GetClientRect(
        hwnd,
        &client
    );

    COLORREF background =
        RGB(18, 20, 26);

    COLORREF card =
        RGB(29, 32, 40);

    COLORREF textPrimary =
        RGB(240, 242, 245);

    COLORREF textSecondary =
        RGB(150, 157, 170);

    COLORREF green =
        RGB(70, 220, 140);


    // --------------------------------------------------------
    // Background
    // --------------------------------------------------------

    HBRUSH backgroundBrush =
        CreateSolidBrush(
            background
        );

    FillRect(
        hdc,
        &client,
        backgroundBrush
    );

    DeleteObject(
        backgroundBrush
    );


    // --------------------------------------------------------
    // Header
    // --------------------------------------------------------

    drawText(
        hdc,
        "SysMon",
        35,
        25,
        textPrimary,
        titleFont
    );

    drawText(
        hdc,
        "Windows System Monitor",
        37,
        68,
        textSecondary,
        subtitleFont
    );

    drawText(
        hdc,
        "LIVE",
        690,
        40,
        green,
        labelFont
    );

    HBRUSH liveBrush =
        CreateSolidBrush(
            green
        );

    HGDIOBJ oldBrush =
        SelectObject(
            hdc,
            liveBrush
        );

    Ellipse(
        hdc,
        665,
        43,
        677,
        55
    );

    SelectObject(
        hdc,
        oldBrush
    );

    DeleteObject(
        liveBrush
    );


    // --------------------------------------------------------
    // CPU CARD
    // --------------------------------------------------------

    drawRoundedBox(
        hdc,
        35,
        115,
        385,
        300,
        card
    );

    drawText(
        hdc,
        "CPU",
        60,
        140,
        textSecondary,
        labelFont
    );

    std::ostringstream cpuStream;

    cpuStream
        << std::fixed
        << std::setprecision(1)
        << cpuUsage
        << "%";

    drawText(
        hdc,
        cpuStream.str(),
        60,
        180,
        textPrimary,
        bigFont
    );

    drawProgressBar(
        hdc,
        60,
        250,
        300,
        12,
        cpuUsage
    );


    // --------------------------------------------------------
    // MEMORY CARD
    // --------------------------------------------------------

    drawRoundedBox(
        hdc,
        405,
        115,
        755,
        300,
        card
    );

    drawText(
        hdc,
        "MEMORY",
        430,
        140,
        textSecondary,
        labelFont
    );

    std::ostringstream ramPercentage;

    ramPercentage
        << ramPercent
        << "%";

    drawText(
        hdc,
        ramPercentage.str(),
        430,
        180,
        textPrimary,
        bigFont
    );

    std::ostringstream ramInfo;

    ramInfo
        << std::fixed
        << std::setprecision(1)
        << usedRamGB
        << " GB / "
        << totalRamGB
        << " GB";

    drawText(
        hdc,
        ramInfo.str(),
        430,
        225,
        textSecondary,
        smallFont
    );

    drawProgressBar(
        hdc,
        430,
        250,
        300,
        12,
        ramPercent
    );


    // --------------------------------------------------------
    // DISK CARD
    // --------------------------------------------------------

    drawRoundedBox(
        hdc,
        35,
        320,
        755,
        455,
        card
    );

    drawText(
        hdc,
        "DISK C:\\",
        60,
        345,
        textSecondary,
        labelFont
    );

    std::ostringstream diskPercentage;

    diskPercentage
        << diskPercent
        << "%";

    drawText(
        hdc,
        diskPercentage.str(),
        655,
        345,
        textPrimary,
        labelFont
    );

    std::ostringstream diskInfo;

    diskInfo
        << std::fixed
        << std::setprecision(1)
        << usedDiskGB
        << " GB / "
        << totalDiskGB
        << " GB";

    drawText(
        hdc,
        diskInfo.str(),
        60,
        380,
        textPrimary,
        labelFont
    );

    drawProgressBar(
        hdc,
        60,
        420,
        670,
        12,
        diskPercent
    );


    // --------------------------------------------------------
    // UPTIME CARD
    // --------------------------------------------------------

    drawRoundedBox(
        hdc,
        35,
        475,
        755,
        555,
        card
    );

    drawText(
        hdc,
        "SYSTEM UPTIME",
        60,
        495,
        textSecondary,
        smallFont
    );

    ULONGLONG days =
        uptimeSeconds / 86400;

    ULONGLONG hours =
        (uptimeSeconds % 86400)
        / 3600;

    ULONGLONG minutes =
        (uptimeSeconds % 3600)
        / 60;

    ULONGLONG seconds =
        uptimeSeconds % 60;

    std::ostringstream uptime;

    uptime
        << days
        << "d  "
        << hours
        << "h  "
        << minutes
        << "m  "
        << seconds
        << "s";

    drawText(
        hdc,
        uptime.str(),
        240,
        495,
        textPrimary,
        labelFont
    );
// --------------------------------------------------------
// DESKTOP WIDGET TOGGLE
// --------------------------------------------------------

drawText(
    hdc,
    "DESKTOP WIDGET",
    430,
    580,
    textSecondary,
    smallFont
);


COLORREF toggleColor;

if (widgetEnabled)
{
    toggleColor =
        RGB(70, 220, 140);
}
else
{
    toggleColor =
        RGB(90, 95, 105);
}

drawRoundedBox(
    hdc,
    610,
    573,
    755,
    605,
    toggleColor
);

drawText(
    hdc,
    widgetEnabled
        ? "ON"
        : "OFF",
    665,
    579,
    RGB(245, 245, 245),
    smallFont
);
}
// ============================================================
// Window procedure
// ============================================================
LRESULT CALLBACK WidgetProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message)
    {
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

        // This exact color becomes invisible.
        HBRUSH transparentBrush =
            CreateSolidBrush(
                WIDGET_TRANSPARENT
            );

        FillRect(
            hdc,
            &client,
            transparentBrush
        );

        DeleteObject(
            transparentBrush
        );

        SetBkMode(
            hdc,
            TRANSPARENT
        );


        // ---------------- CPU ----------------

        SelectObject(
            hdc,
            widgetLabelFont
        );

        SetTextColor(
            hdc,
            RGB(170, 180, 195)
        );

        TextOutA(
            hdc,
            10,
            8,
            "CPU",
            3
        );


        std::ostringstream cpuStream;

        cpuStream
            << std::fixed
            << std::setprecision(1)
            << cpuUsage
            << "%";

        std::string cpuString =
            cpuStream.str();

        SelectObject(
            hdc,
            widgetValueFont
        );

        SetTextColor(
            hdc,
            RGB(245, 245, 245)
        );

        TextOutA(
            hdc,
            60,
            4,
            cpuString.c_str(),
            static_cast<int>(
                cpuString.length()
            )
        );


        HBRUSH blueBrush =
            CreateSolidBrush(
                RGB(66, 135, 245)
            );

        int cpuWidth =
            static_cast<int>(
                200 *
                (cpuUsage / 100.0)
            );

        RECT cpuBar =
        {
            10,
            35,
            10 + cpuWidth,
            39
        };

        FillRect(
            hdc,
            &cpuBar,
            blueBrush
        );


        // ---------------- RAM ----------------

        SelectObject(
            hdc,
            widgetLabelFont
        );

        SetTextColor(
            hdc,
            RGB(170, 180, 195)
        );

        TextOutA(
            hdc,
            10,
            55,
            "RAM",
            3
        );


        std::ostringstream ramStream;

        ramStream
            << ramPercent
            << "%";

        std::string ramString =
            ramStream.str();

        SelectObject(
            hdc,
            widgetValueFont
        );

        SetTextColor(
            hdc,
            RGB(245, 245, 245)
        );

        TextOutA(
            hdc,
            60,
            51,
            ramString.c_str(),
            static_cast<int>(
                ramString.length()
            )
        );


        int ramWidth =
            static_cast<int>(
                200 *
                (ramPercent / 100.0)
            );

        RECT ramBar =
        {
            10,
            82,
            10 + ramWidth,
            86
        };

        FillRect(
            hdc,
            &ramBar,
            blueBrush
        );

        DeleteObject(
            blueBrush
        );


        EndPaint(
            hwnd,
            &ps
        );

        return 0;
    }


    // Treat the visible widget as if it were a title bar.
    // This lets you grab it and drag it freely.
    case WM_TIMER:
{
    if (wParam == 2)
    {
        updateStats();

        InvalidateRect(
            hwnd,
            nullptr,
            TRUE
        );

        UpdateWindow(
            hwnd
        );
    }

    return 0;
}
    case WM_NCHITTEST:
        return HTCAPTION;


    case WM_ERASEBKGND:
        return 1;
    }

    return DefWindowProc(
        hwnd,
        message,
        wParam,
        lParam
    );
}
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
#include "UI.h"
#include "Stats.h"
#include "Widget.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include "Processes.h"
#include <cctype>

extern HFONT titleFont;
extern HFONT subtitleFont;
extern HFONT labelFont;
extern HFONT bigFont;
extern HFONT smallFont;


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
        CreateSolidBrush(
            color
        );

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

    DeleteObject(
        brush
    );

    DeleteObject(
        pen
    );
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

void drawGraphGrid(
    HDC hdc,
    int x,
    int y,
    int width,
    int height)
{
    HPEN gridPen =
        CreatePen(
            PS_SOLID,
            1,
            RGB(42, 46, 56)
        );

    HGDIOBJ oldPen =
        SelectObject(
            hdc,
            gridPen
        );


    // Horizontal grid lines
    for (int i = 1; i < 4; i++)
    {
        int lineY =
            y +
            (height * i / 4);

        MoveToEx(
            hdc,
            x,
            lineY,
            nullptr
        );

        LineTo(
            hdc,
            x + width,
            lineY
        );
    }


    // Vertical grid lines
    for (int i = 1; i < 4; i++)
    {
        int lineX =
            x +
            (width * i / 4);

        MoveToEx(
            hdc,
            lineX,
            y,
            nullptr
        );

        LineTo(
            hdc,
            lineX,
            y + height
        );
    }


    SelectObject(
        hdc,
        oldPen
    );

    DeleteObject(
        gridPen
    );
}

void drawCpuGraph(
    HDC hdc,
    int x,
    int y,
    int width,
    int height)
{
    if (cpuHistory.size() < 2)
        return;


    // Graph background
    drawRoundedBox(
        hdc,
        x,
        y,
        x + width,
        y + height,
        RGB(24, 27, 34)
    );
drawGraphGrid(
    hdc,
    x,
    y,
    width,
    height
);

    // Convert CPU samples into screen points
    std::vector<POINT> points(
        cpuHistory.size()
    );

    for (size_t i = 0;
         i < cpuHistory.size();
         i++)
    {
        double percent =
            cpuHistory[i];

        int pointX =
            x +
            static_cast<int>(
                i *
                static_cast<double>(width) /
                (cpuHistory.size() - 1)
            );

        int pointY =
            y +
            height -
            static_cast<int>(
                (percent / 100.0) *
                height
            );

        points[i].x = pointX;
        points[i].y = pointY;
    }
std::vector<POINT> fillPoints =
    points;

fillPoints.push_back(
    {
        static_cast<LONG>(x + width),
        static_cast<LONG>(y + height)
    }
);

fillPoints.push_back(
    {
        static_cast<LONG>(x),
        static_cast<LONG>(y + height)
    }
);

HBRUSH fillBrush =
    CreateSolidBrush(
        RGB(30, 48, 75)
    );

HGDIOBJ oldBrush =
    SelectObject(
        hdc,
        fillBrush
    );

HGDIOBJ oldFillPen =
    SelectObject(
        hdc,
        GetStockObject(NULL_PEN)
    );

Polygon(
    hdc,
    fillPoints.data(),
    static_cast<int>(
        fillPoints.size()
    )
);

SelectObject(
    hdc,
    oldFillPen
);

SelectObject(
    hdc,
    oldBrush
);

DeleteObject(
    fillBrush
);

    // Blue graph pen
    HPEN graphPen =
        CreatePen(
            PS_SOLID,
            2,
            RGB(66, 135, 245)
        );

    HGDIOBJ oldPen =
        SelectObject(
            hdc,
            graphPen
        );


    // If there are only two points,
    // just draw a normal line.
    if (points.size() == 2)
    {
        Polyline(
            hdc,
            points.data(),
            2
        );
    }
    else
    {
        // Convert the real data points into
        // smooth Bezier curve segments.
        std::vector<POINT> bezierPoints;

        bezierPoints.push_back(
            points[0]
        );

        for (size_t i = 0;
             i < points.size() - 1;
             i++)
        {
            POINT p0 =
                (i == 0)
                ? points[i]
                : points[i - 1];

            POINT p1 =
                points[i];

            POINT p2 =
                points[i + 1];

            POINT p3 =
                (i + 2 < points.size())
                ? points[i + 2]
                : points[i + 1];


            POINT control1;

            control1.x =
                p1.x +
                (p2.x - p0.x) / 6;

            control1.y =
                p1.y +
                (p2.y - p0.y) / 6;


            POINT control2;

            control2.x =
                p2.x -
                (p3.x - p1.x) / 6;

            control2.y =
                p2.y -
                (p3.y - p1.y) / 6;


        control1.y =
    std::clamp<LONG>(
        control1.y,
        static_cast<LONG>(y),
        static_cast<LONG>(y + height)
    );

control2.y =
    std::clamp<LONG>(
        control2.y,
        static_cast<LONG>(y),
        static_cast<LONG>(y + height)
    );

            bezierPoints.push_back(
                control1
            );

            bezierPoints.push_back(
                control2
            );

            bezierPoints.push_back(
                p2
            );
        }


        PolyBezier(
            hdc,
            bezierPoints.data(),
            static_cast<DWORD>(
                bezierPoints.size()
            )
        );
    }


    SelectObject(
        hdc,
        oldPen
    );

    DeleteObject(
        graphPen
    );
}
void drawRamGraph(
    HDC hdc,
    int x,
    int y,
    int width,
    int height)
{
    if (ramHistory.size() < 2)
        return;


    drawRoundedBox(
        hdc,
        x,
        y,
        x + width,
        y + height,
        RGB(24, 27, 34)
    );


    std::vector<POINT> points(
        ramHistory.size()
    );

    for (size_t i = 0;
         i < ramHistory.size();
         i++)
    {
        double percent =
            ramHistory[i];

        int pointX =
            x +
            static_cast<int>(
                i *
                static_cast<double>(width) /
                (ramHistory.size() - 1)
            );

        int pointY =
            y +
            height -
            static_cast<int>(
                (percent / 100.0) *
                height
            );

        points[i].x = pointX;
        points[i].y = pointY;
    }


    HPEN graphPen =
        CreatePen(
            PS_SOLID,
            2,
            RGB(66, 135, 245)
        );

    HGDIOBJ oldPen =
        SelectObject(
            hdc,
            graphPen
        );


    if (points.size() == 2)
    {
        Polyline(
            hdc,
            points.data(),
            2
        );
    }
    else
    {
        std::vector<POINT> bezierPoints;

        bezierPoints.push_back(
            points[0]
        );

        for (size_t i = 0;
             i < points.size() - 1;
             i++)
        {
            POINT p0 =
                (i == 0)
                ? points[i]
                : points[i - 1];

            POINT p1 =
                points[i];

            POINT p2 =
                points[i + 1];

            POINT p3 =
                (i + 2 < points.size())
                ? points[i + 2]
                : points[i + 1];


            POINT control1;

            control1.x =
                p1.x +
                (p2.x - p0.x) / 6;

            control1.y =
                p1.y +
                (p2.y - p0.y) / 6;


            POINT control2;

            control2.x =
                p2.x -
                (p3.x - p1.x) / 6;

            control2.y =
                p2.y -
                (p3.y - p1.y) / 6;


            control1.y =
                std::clamp<LONG>(
                    control1.y,
                    static_cast<LONG>(y),
                    static_cast<LONG>(y + height)
                );

            control2.y =
                std::clamp<LONG>(
                    control2.y,
                    static_cast<LONG>(y),
                    static_cast<LONG>(y + height)
                );


            bezierPoints.push_back(
                control1
            );

            bezierPoints.push_back(
                control2
            );

            bezierPoints.push_back(
                p2
            );
        }


        PolyBezier(
            hdc,
            bezierPoints.data(),
            static_cast<DWORD>(
                bezierPoints.size()
            )
        );
    }


    SelectObject(
        hdc,
        oldPen
    );

    DeleteObject(
        graphPen
    );
}
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
// SIDEBAR
// --------------------------------------------------------

drawRoundedBox(
    hdc,
    20,
    20,
    220,
    700,
    RGB(24, 27, 34)
);

drawText(
    hdc,
    "SysMon",
    45,
    40,
    textPrimary,
    titleFont
);

drawText(
    hdc,
    "SYSTEM MONITOR",
    47,
    82,
    textSecondary,
    smallFont
);


// Navigation items
int activeTop = 125;

switch (currentPage)
{
case AppPage::Dashboard:
    activeTop = 125;
    break;

case AppPage::Processes:
    activeTop = 175;
    break;

case AppPage::Performance:
    activeTop = 225;
    break;

case AppPage::SystemInfo:
    activeTop = 275;
    break;

case AppPage::Settings:
    activeTop = 325;
    break;
}

drawRoundedBox(
    hdc,
    35,
    activeTop,
    185,
    activeTop + 45,
    RGB(36, 42, 54)
);

drawRoundedBox(
    hdc,
    35,
    activeTop,
    41,
    activeTop + 45,
    RGB(66, 135, 245)
);
drawText(
    hdc,
    "Dashboard",
    55,
    145,
    currentPage == AppPage::Dashboard
    ? textPrimary
    : textSecondary,
    labelFont
);

drawText(
    hdc,
    "Processes",
    55,
    195,
    currentPage == AppPage::Processes
    ? textPrimary
    : textSecondary,
    labelFont
);

drawText(
    hdc,
    "Performance",
    55,
    245,
    currentPage == AppPage::Performance
    ? textPrimary
    : textSecondary,
    labelFont
);

drawText(
    hdc,
    "System Info",
    55,
    295,
   currentPage == AppPage::SystemInfo
    ? textPrimary
    : textSecondary,
    labelFont
);

drawText(
    hdc,
    "Settings",
    55,
    345,
    currentPage == AppPage::Settings
    ? textPrimary
    : textSecondary,
    labelFont
);
POINT oldOrigin;
drawText(
    hdc,
    "v0.6",
    55,
    660,
    RGB(100, 108, 122),
    smallFont
);
SetViewportOrgEx(
    hdc,
    225,
    0,
    &oldOrigin
);
// --------------------------------------------------------
// NON-DASHBOARD PAGES
// --------------------------------------------------------

if (currentPage != AppPage::Dashboard)
{
    std::string pageTitle;
    std::string pageSubtitle;

    switch (currentPage)
    {
    case AppPage::Processes:
        pageTitle = "Processes";
        pageSubtitle = "Running applications and background processes";
        break;

    case AppPage::Performance:
        pageTitle = "Performance";
        pageSubtitle = "Detailed system performance monitoring";
        break;

    case AppPage::SystemInfo:
        pageTitle = "System Info";
        pageSubtitle = "Hardware and operating system information";
        break;

    case AppPage::Settings:
        pageTitle = "Settings";
        pageSubtitle = "Configure SysMon preferences";
        break;

    default:
        break;
    }

    drawText(
        hdc,
        pageTitle,
        35,
        25,
        textPrimary,
        titleFont
    );

    drawText(
        hdc,
        pageSubtitle,
        37,
        68,
        textSecondary,
        subtitleFont
    );

    drawRoundedBox(
        hdc,
        35,
        115,
        915,
        610,
        card
    );

  if (currentPage == AppPage::Processes)
{
    std::vector<ProcessInfo> processes =
        getRunningProcesses();

        int totalProcessCount =
    static_cast<int>(
        processes.size()
    );

      // --------------------------------------------------------
// PROCESS SEARCH FILTER
// --------------------------------------------------------

if (!processSearch.empty())
{
    std::string searchLower =
        processSearch;

    std::transform(
        searchLower.begin(),
        searchLower.end(),
        searchLower.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(
                std::tolower(c)
            );
        }
    );

    processes.erase(
        std::remove_if(
            processes.begin(),
            processes.end(),
            [&](const ProcessInfo& process)
            {
                std::string nameLower =
                    process.name;

                std::transform(
                    nameLower.begin(),
                    nameLower.end(),
                    nameLower.begin(),
                    [](unsigned char c)
                    {
                        return static_cast<char>(
                            std::tolower(c)
                        );
                    }
                );

                return nameLower.find(
                    searchLower
                ) == std::string::npos;
            }
        ),
        processes.end()
    );
}  
std::sort(
    processes.begin(),
    processes.end(),
    [](const ProcessInfo& a,
       const ProcessInfo& b)
    {
        // Keep System Idle Process visible at the top
        if (a.pid == 0 && b.pid != 0)
            return true;

        if (b.pid == 0 && a.pid != 0)
            return false;


        switch (processSort)
        {
        case ProcessSort::Name:
            if (processSortDescending)
            {
                return a.name > b.name;
            }

            return a.name < b.name;


        case ProcessSort::CPU:
            if (processSortDescending)
            {
                return a.cpuPercent >
                       b.cpuPercent;
            }

            return a.cpuPercent <
                   b.cpuPercent;


        case ProcessSort::Memory:
            if (processSortDescending)
            {
                return a.memoryMB >
                       b.memoryMB;
            }

            return a.memoryMB <
                   b.memoryMB;


        case ProcessSort::Threads:
            if (processSortDescending)
            {
                return a.threadCount >
                       b.threadCount;
            }

            return a.threadCount <
                   b.threadCount;


        case ProcessSort::PID:
            if (processSortDescending)
            {
                return a.pid > b.pid;
            }

            return a.pid < b.pid;
        }


        return false;
    }
);
// --------------------------------------------------------
// SEARCH BOX
// --------------------------------------------------------

if (processSearchFocused)
{
    // Blue focus border
    drawRoundedBox(
        hdc,
        58,
        118,
        352,
        147,
        RGB(66, 135, 245)
    );
}

// Search box background
drawRoundedBox(
    hdc,
    60,
    120,
    350,
    145,
    RGB(36, 42, 54)
);

std::string searchDisplay;

if (processSearch.empty())
{
    searchDisplay =
        processSearchFocused
        ? "|"
        : "Search processes...";
}
else
{
    searchDisplay =
        processSearch;

    if (processSearchFocused)
    {
        searchDisplay += "|";
    }
}

drawText(
    hdc,
    searchDisplay,
    75,
    126,
    processSearch.empty()
        ? RGB(100, 108, 122)
        : textPrimary,
    smallFont
);
if (!processSearch.empty())
{
    drawText(
        hdc,
        "X",
        330,
        126,
        RGB(150, 157, 170),
        smallFont
    );
}
std::string nameHeader = "PROCESS";
std::string cpuHeader = "CPU";
std::string memoryHeader = "MEMORY";
std::string threadsHeader = "THREADS";
std::string pidHeader = "PID";

std::string sortArrow =
    processSortDescending
    ? " v"
    : " ^";
switch (processSort)
{
case ProcessSort::Name:
    nameHeader += sortArrow;
    break;

case ProcessSort::CPU:
    cpuHeader += sortArrow;
    break;

case ProcessSort::Memory:
    memoryHeader += sortArrow;
    break;

case ProcessSort::Threads:
    threadsHeader += sortArrow;
    break;

case ProcessSort::PID:
    pidHeader += sortArrow;
    break;
}


    // Table header
  drawText(
    hdc,
   nameHeader,
    80,
    165,
    textSecondary,
    smallFont
);

drawText(
    hdc,
    cpuHeader,
    470,
    165,
    textSecondary,
    smallFont
);

drawText(
    hdc,
    memoryHeader,
    570,
    165,
    textSecondary,
    smallFont
);

drawText(
    hdc,
    threadsHeader,
    700,
    165,
    textSecondary,
    smallFont
);

drawText(
    hdc,
    pidHeader,
    820,
    165,
    textSecondary,
    smallFont
);
    int rowY = 205;

   const int visibleRows = 12;

int maxOffset =
    (std::max)(
        0,
        static_cast<int>(processes.size()) -
        visibleRows
    );
    processMaxScrollOffset =
    maxOffset;
processScrollOffset =
    std::clamp(
        processScrollOffset,
        0,
        maxOffset
    );

int endIndex =
    (std::min)(
        processScrollOffset + visibleRows,
        static_cast<int>(processes.size())
    );
    visibleProcessPids.clear();
for (
    int i = processScrollOffset;
    i < endIndex;
    i++
)

{
    visibleProcessPids.push_back(
        processes[i].pid
    );

// Hover highlight
if (
    processes[i].pid ==
        hoveredProcessPid &&
    processes[i].pid !=
        selectedProcessPid
)
{
    drawRoundedBox(
        hdc,
        65,
        rowY - 5,
        885,
        rowY + 21,
        RGB(31, 35, 44)
    );
}

// Highlight selected process
if (
    processes[i].pid ==
    selectedProcessPid
)
{
    drawRoundedBox(
        hdc,
        65,
        rowY - 5,
        885,
        rowY + 21,
        RGB(36, 42, 54)
    );

    // Blue selection accent
    drawRoundedBox(
        hdc,
        65,
        rowY - 5,
        70,
        rowY + 21,
        RGB(66, 135, 245)
    );
}

    drawText(
        hdc,
        processes[i].name,
        80,
        rowY,
        textPrimary,
        smallFont
    );
// CPU usage
std::string cpuText;

if (processes[i].cpuPercent >= 0.0)
{
    std::ostringstream cpuStream;

    cpuStream
        << std::fixed
        << std::setprecision(1)
        << processes[i].cpuPercent;

    if (processes[i].pid == 0)
    {
        cpuStream << "% Idle";
    }
    else
    {
        cpuStream << "%";
    }

    cpuText =
        cpuStream.str();
}
else
{
    cpuText = "--";
}

drawText(
    hdc,
    cpuText,
    470,
    rowY,
    textSecondary,
    smallFont
);

// Memory
std::string memoryText;

if (processes[i].pid == 0)
{
    memoryText = "0.0 MB";
}
else if (processes[i].memoryMB >= 0.0)
{
    std::ostringstream memoryStream;

    memoryStream
        << std::fixed
        << std::setprecision(1)
        << processes[i].memoryMB
        << " MB";

    memoryText =
        memoryStream.str();
}
else
{
    memoryText = "N/A";
}
drawText(
    hdc,
    memoryText,
    570,
    rowY,
    textSecondary,
    smallFont
);
// Threads
drawText(
    hdc,
    std::to_string(
        processes[i].threadCount
    ),
    700,
    rowY,
    textSecondary,
    smallFont
);

// PID
drawText(
    hdc,
    std::to_string(
        processes[i].pid
    ),
    820,
    rowY,
    textSecondary,
    smallFont
);
        rowY += 27;
    }

// --------------------------------------------------------
// CUSTOM SCROLLBAR
// --------------------------------------------------------

const int scrollBarLeft = 895;
const int scrollBarTop = 205;
const int scrollBarRight = 905;
const int scrollBarBottom = 529;

const int arrowArea = 16;

drawRoundedBox(
    hdc,
    scrollBarLeft,
    scrollBarTop,
    scrollBarRight,
    scrollBarBottom,
    RGB(24, 28, 36)
);

drawText(
    hdc,
    "^",
    897,
    206,
    RGB(120, 128, 140),
    smallFont
);

drawText(
    hdc,
    "v",
    897,
    512,
    RGB(120, 128, 140),
    smallFont
);

if (
    static_cast<int>(
        processes.size()
    ) > visibleRows
)
{
    int trackTop =
        scrollBarTop + arrowArea;

    int trackBottom =
        scrollBarBottom - arrowArea;

    int trackHeight =
        trackBottom - trackTop;

    double visibleRatio =
        static_cast<double>(
            visibleRows
        ) /
        static_cast<double>(
            processes.size()
        );

    int thumbHeight =
        static_cast<int>(
            trackHeight *
            visibleRatio
        );

    if (thumbHeight < 40)
    {
        thumbHeight = 40;
    }

    if (thumbHeight > trackHeight)
    {
        thumbHeight = trackHeight;
    }

    int thumbTravel =
        trackHeight -
        thumbHeight;

    int thumbTop =
        trackTop;

    if (maxOffset > 0)
    {
        double scrollRatio =
            static_cast<double>(
                processScrollOffset
            ) /
            static_cast<double>(
                maxOffset
            );

        thumbTop =
            trackTop +
            static_cast<int>(
                scrollRatio *
                thumbTravel
            );
    }
int thumbBottom =
    thumbTop + thumbHeight;

processScrollbarThumbTop =
    thumbTop;

processScrollbarThumbBottom =
    thumbBottom;
    drawRoundedBox(
        hdc,
        scrollBarLeft + 1,
        thumbTop,
        scrollBarRight - 1,
        thumbTop + thumbHeight,
        RGB(110, 116, 128)
    );
}

// --------------------------------------------------------
// SELECTED PROCESS DETAILS
// --------------------------------------------------------

for (const ProcessInfo& process : processes)
{
    if (process.pid == selectedProcessPid)
    {
        drawRoundedBox(
            hdc,
            60,
            530,
            890,
            590,
            RGB(36, 42, 54)
        );

        // Process name
        drawText(
            hdc,
            process.name,
            75,
            540,
            textPrimary,
            labelFont
        );


        // First details line
        std::ostringstream detailsLine1;

        detailsLine1
            << "PID: "
            << process.pid
            << "     Parent PID: "
            << process.parentPid
            << "     Threads: "
            << process.threadCount
            << "     Handles: "
            << process.handleCount;

        drawText(
            hdc,
            detailsLine1.str(),
            280,
            540,
            textSecondary,
            smallFont
        );


        // Second details line
        std::ostringstream detailsLine2;

        if (process.pid == 0)
        {
            detailsLine2
                << std::fixed
                << std::setprecision(1)
                << process.cpuPercent
                << "% Idle";
        }
        else
        {
            detailsLine2
                << std::fixed
                << std::setprecision(1)
                << process.cpuPercent
                << "% CPU";
        }

        detailsLine2
            << "     Memory: "
            << std::fixed
            << std::setprecision(1)
            << process.memoryMB
            << " MB";

        drawText(
            hdc,
            detailsLine2.str(),
            280,
            563,
            textSecondary,
            smallFont
        );


        // End Task button
        if (
            process.pid != 0 &&
            process.pid != 4 &&
            process.pid != GetCurrentProcessId()
        )
        {
            drawRoundedBox(
                hdc,
                735,
                575,
                875,
                600,
                RGB(150, 55, 55)
            );

            drawText(
                hdc,
                "END TASK",
                770,
                581,
                RGB(245, 245, 245),
                smallFont
            );
        }

        break;
    }
}
    std::string processCount;

if (!processSearch.empty())
{
    processCount =
        std::to_string(
            processes.size()
        ) +
        " of " +
        std::to_string(
            totalProcessCount
        ) +
        " processes";
}
else
{
    processCount =
        std::to_string(
            totalProcessCount
        ) +
        " processes detected";
}

    drawText(
        hdc,
        processCount,
        680,
        125,
        textSecondary,
        smallFont
    );
}
else
{
    drawText(
        hdc,
        "This page is under development.",
        70,
        160,
        textPrimary,
        labelFont
    );

    drawText(
        hdc,
        "More features will be added in upcoming SysMon versions.",
        70,
        200,
        textSecondary,
        smallFont
    );
}
    SetViewportOrgEx(
        hdc,
        oldOrigin.x,
        oldOrigin.y,
        nullptr
    );

    return;
}
    // --------------------------------------------------------
    // Header
    // --------------------------------------------------------

drawText(
    hdc,
    "Dashboard",
    35,
    25,
    textPrimary,
    titleFont
);

drawText(
    hdc,
    "Real-time system overview",
    37,
    68,
    textSecondary,
    subtitleFont
);
    drawRoundedBox(
    hdc,
    735,
    28,
    915,
    68,
    RGB(24, 45, 38)
);

HBRUSH statusBrush =
    CreateSolidBrush(
        green
    );

HGDIOBJ oldStatusBrush =
    SelectObject(
        hdc,
        statusBrush
    );

HPEN statusPen =
    CreatePen(
        PS_SOLID,
        1,
        green
    );

HGDIOBJ oldStatusPen =
    SelectObject(
        hdc,
        statusPen
    );

Ellipse(
    hdc,
    755,
    43,
    765,
    53
);

SelectObject(
    hdc,
    oldStatusPen
);

SelectObject(
    hdc,
    oldStatusBrush
);

DeleteObject(
    statusPen
);

DeleteObject(
    statusBrush
);

drawText(
    hdc,
   "MONITORING ACTIVE",
    780,
    39,
    green,
    smallFont
);

    // --------------------------------------------------------
    // CPU CARD
    // --------------------------------------------------------

drawRoundedBox(
    hdc,
    35,
    115,
    315,
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
drawCpuGraph(
    hdc,
    145,
    170,
    145,
    65
);
drawText(
    hdc,
    "60s",
    145,
    236,
    RGB(100, 108, 122),
    smallFont
);

drawText(
    hdc,
    "NOW",
    258,
    236,
    RGB(100, 108, 122),
    smallFont
);

    drawProgressBar(
        hdc,
        60,
        250,
        230,
        12,
        cpuUsage
    );


    // --------------------------------------------------------
    // MEMORY CARD
    // --------------------------------------------------------

    drawRoundedBox(
        hdc,
    335,
    115,
    615,
    300,
        card
    );

    drawText(
        hdc,
        "MEMORY",
        360,
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
        360,
        180,
        textPrimary,
        bigFont
    );
drawRamGraph(
    hdc,
    445,
    170,
    145,
    65
);
drawText(
    hdc,
    "60s",
    445,
    236,
    RGB(100, 108, 122),
    smallFont
);

drawText(
    hdc,
    "NOW",
    558,
    236,
    RGB(100, 108, 122),
    smallFont
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
        360,
        225,
        textSecondary,
        smallFont
    );

drawProgressBar(
    hdc,
    360,
    250,
    230,
    12,
    ramPercent
);

    // --------------------------------------------------------
    // DISK CARD
    // --------------------------------------------------------

    drawRoundedBox(
    hdc,
    635,
    115,
    915,
    300,
    card
);
    drawText(
        hdc,
        "DISK C:\\",
        660,
        140,
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
        845,
        140,
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
        660,
        185,
        textPrimary,
        labelFont
    );

drawProgressBar(
    hdc,
    660,
    250,
    230,
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
    915,
    585,
    card
);

    drawText(
        hdc,
        "SYSTEM UPTIME",
        60,
        500,
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
    60,
    530,
    textPrimary,
    labelFont
);
// --------------------------------------------------------
// DESKTOP WIDGET TOGGLE
// --------------------------------------------------------

drawText(
    hdc,
    "DESKTOP WIDGET",
    650,
    500,
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
    650,
    525,
    865,
    557,
    toggleColor
);

drawText(
    hdc,
    widgetEnabled
        ? "ON"
        : "OFF",
    745,
    531,
    RGB(245, 245, 245),
    smallFont
);
// --------------------------------------------------------
// GPU CARD
// --------------------------------------------------------

drawRoundedBox(
    hdc,
    35,
    325,
    315,
    450,
    card
);

drawText(
    hdc,
    "GPU",
    60,
    350,
    textSecondary,
    labelFont
);

drawRoundedBox(
    hdc,
    60,
    395,
    145,
    425,
    RGB(36, 42, 54)
);

drawText(
    hdc,
    "PLANNED",
    75,
    402,
    textSecondary,
    smallFont
);


// --------------------------------------------------------
// NETWORK CARD
// --------------------------------------------------------

drawRoundedBox(
    hdc,
    335,
    325,
    615,
    450,
    card
);

drawText(
    hdc,
    "NETWORK",
    360,
    350,
    textSecondary,
    labelFont
);

drawRoundedBox(
    hdc,
    360,
    395,
    445,
    425,
    RGB(36, 42, 54)
);

drawText(
    hdc,
    "PLANNED",
    375,
    402,
    textSecondary,
    smallFont
);


// --------------------------------------------------------
// TEMPERATURE CARD
// --------------------------------------------------------

drawRoundedBox(
    hdc,
    635,
    325,
    915,
    450,
    card
);

drawText(
    hdc,
    "TEMPERATURE",
    660,
    350,
    textSecondary,
    labelFont
);

drawRoundedBox(
    hdc,
    660,
    395,
    745,
    425,
    RGB(36, 42, 54)
);

drawText(
    hdc,
    "PLANNED",
    675,
    402,
    textSecondary,
    smallFont
);


// Restore normal drawing coordinates
SetViewportOrgEx(
    hdc,
    oldOrigin.x,
    oldOrigin.y,
    nullptr
);

}
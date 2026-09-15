#include "UI.h"
#include "Stats.h"
#include "Widget.h"

#include <sstream>
#include <iomanip>


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
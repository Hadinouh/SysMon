#include "UI.h"
#include "Stats.h"
#include "Widget.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include "Processes.h"
#include <cctype>
#include <psapi.h>
#include <vector>
#include <string>

extern HFONT titleFont;
extern HFONT subtitleFont;
extern HFONT labelFont;
extern HFONT bigFont;
extern HFONT smallFont;


struct MemoryPerformanceDetails
{
    bool valid = false;
    unsigned long long commitTotalBytes = 0;
    unsigned long long commitLimitBytes = 0;
    unsigned long long cachedBytes = 0;
    unsigned long long pagedPoolBytes = 0;
    unsigned long long nonPagedPoolBytes = 0;
};


struct MemoryHardwareDetails
{
    DWORD speedMTs = 0;
    int usedSlots = 0;
    int totalSlots = 0;
    std::string formFactor = "--";
    bool hardwareReservedValid = false;
    unsigned long long hardwareReservedBytes = 0;
};


static WORD readSmbiosWord(
    const BYTE* data)
{
    return static_cast<WORD>(
        static_cast<WORD>(data[0]) |
        static_cast<WORD>(data[1] << 8)
    );
}


static std::string memoryFormFactorName(
    BYTE formFactor)
{
    switch (formFactor)
    {
    case 0x03: return "SIMM";
    case 0x04: return "SIP";
    case 0x05: return "Chip";
    case 0x06: return "DIP";
    case 0x07: return "ZIP";
    case 0x08: return "Card";
    case 0x09: return "DIMM";
    case 0x0A: return "TSOP";
    case 0x0B: return "Row of chips";
    case 0x0C: return "RIMM";
    case 0x0D: return "SODIMM";
    case 0x0E: return "SRIMM";
    case 0x0F: return "FB-DIMM";
    case 0x10: return "Die";
    default:   return "--";
    }
}


static std::string formatMemoryBytes(
    unsigned long long bytes)
{
    std::ostringstream stream;

    const double megabyte =
        1024.0 * 1024.0;

    const double gigabyte =
        1024.0 * 1024.0 * 1024.0;

    if (bytes >=
        1024ULL * 1024ULL * 1024ULL)
    {
        stream
            << std::fixed
            << std::setprecision(1)
            << (bytes / gigabyte)
            << " GB";
    }
    else
    {
        stream
            << std::fixed
            << std::setprecision(0)
            << (bytes / megabyte)
            << " MB";
    }

    return stream.str();
}


static std::string formatMemoryGigabyteNumber(
    unsigned long long bytes)
{
    std::ostringstream stream;

    stream
        << std::fixed
        << std::setprecision(1)
        << (
            bytes /
            (1024.0 * 1024.0 * 1024.0)
        );

    return stream.str();
}


static MemoryPerformanceDetails
getMemoryPerformanceDetails()
{
    MemoryPerformanceDetails details;

    using GetPerformanceInfoFn =
        BOOL (WINAPI *)(
            PPERFORMANCE_INFORMATION,
            DWORD
        );

    static HMODULE psapiModule =
        LoadLibraryA(
            "psapi.dll"
        );

    static GetPerformanceInfoFn
        getPerformanceInfoFn =
            psapiModule
            ? reinterpret_cast<
                GetPerformanceInfoFn
              >(
                GetProcAddress(
                    psapiModule,
                    "GetPerformanceInfo"
                )
              )
            : nullptr;

    if (getPerformanceInfoFn == nullptr)
    {
        return details;
    }

    PERFORMANCE_INFORMATION info = {};

    if (!getPerformanceInfoFn(
            &info,
            sizeof(info)
        ))
    {
        return details;
    }

    const unsigned long long pageSize =
        static_cast<unsigned long long>(
            info.PageSize
        );

    details.commitTotalBytes =
        static_cast<unsigned long long>(
            info.CommitTotal
        ) * pageSize;

    details.commitLimitBytes =
        static_cast<unsigned long long>(
            info.CommitLimit
        ) * pageSize;

    details.cachedBytes =
        static_cast<unsigned long long>(
            info.SystemCache
        ) * pageSize;

    details.pagedPoolBytes =
        static_cast<unsigned long long>(
            info.KernelPaged
        ) * pageSize;

    details.nonPagedPoolBytes =
        static_cast<unsigned long long>(
            info.KernelNonpaged
        ) * pageSize;

    details.valid = true;

    return details;
}


static MemoryHardwareDetails
queryMemoryHardwareDetails()
{
    MemoryHardwareDetails details;

    struct RawSmbiosHeader
    {
        BYTE used20CallingMethod;
        BYTE majorVersion;
        BYTE minorVersion;
        BYTE dmiRevision;
        DWORD length;
    };

    const DWORD rawSmbiosProvider =
        0x52534D42UL; // 'RSMB' without a multi-character literal warning

    UINT firmwareSize =
        GetSystemFirmwareTable(
            rawSmbiosProvider,
            0,
            nullptr,
            0
        );

    if (firmwareSize >=
        sizeof(RawSmbiosHeader))
    {
        std::vector<BYTE> firmware(
            firmwareSize
        );

        if (GetSystemFirmwareTable(
                rawSmbiosProvider,
                0,
                firmware.data(),
                firmwareSize
            ) == firmwareSize)
        {
            const RawSmbiosHeader* raw =
                reinterpret_cast<
                    const RawSmbiosHeader*
                >(
                    firmware.data()
                );

            const BYTE* table =
                firmware.data() +
                sizeof(RawSmbiosHeader);

            size_t availableTableBytes =
                firmware.size() -
                sizeof(RawSmbiosHeader);

            size_t tableLength =
                static_cast<size_t>(
                    raw->length
                );

            if (tableLength >
                availableTableBytes)
            {
                tableLength =
                    availableTableBytes;
            }

            size_t offset = 0;
            int type17Count = 0;

            while (offset + 4 <=
                   tableLength)
            {
                const BYTE* structure =
                    table + offset;

                BYTE type = structure[0];
                BYTE length = structure[1];

                if (length < 4 ||
                    offset + length >
                    tableLength)
                {
                    break;
                }

                if (type == 16 &&
                    length >= 0x0F)
                {
                    WORD slotCount =
                        readSmbiosWord(
                            structure + 0x0D
                        );

                    if (slotCount > 0 &&
                        slotCount != 0xFFFF &&
                        slotCount >
                        details.totalSlots)
                    {
                        details.totalSlots =
                            slotCount;
                    }
                }
                else if (type == 17 &&
                         length >= 0x0F)
                {
                    type17Count++;

                    WORD sizeField =
                        readSmbiosWord(
                            structure + 0x0C
                        );

                    bool populated =
                        sizeField != 0 &&
                        sizeField != 0xFFFF;

                    if (populated)
                    {
                        details.usedSlots++;

                        if (details.formFactor ==
                            "--")
                        {
                            details.formFactor =
                                memoryFormFactorName(
                                    structure[0x0E]
                                );
                        }

                        DWORD speed = 0;

                        if (length >= 0x22)
                        {
                            WORD configuredSpeed =
                                readSmbiosWord(
                                    structure + 0x20
                                );

                            if (configuredSpeed != 0 &&
                                configuredSpeed !=
                                0xFFFF)
                            {
                                speed =
                                    configuredSpeed;
                            }
                        }

                        if (speed == 0 &&
                            length >= 0x17)
                        {
                            WORD reportedSpeed =
                                readSmbiosWord(
                                    structure + 0x15
                                );

                            if (reportedSpeed != 0 &&
                                reportedSpeed !=
                                0xFFFF)
                            {
                                speed =
                                    reportedSpeed;
                            }
                        }

                        if (speed >
                            details.speedMTs)
                        {
                            details.speedMTs = speed;
                        }
                    }
                }

                if (type == 127)
                {
                    break;
                }

                size_t next =
                    offset + length;

                while (
                    next + 1 < tableLength &&
                    !(
                        table[next] == 0 &&
                        table[next + 1] == 0
                    )
                )
                {
                    next++;
                }

                if (next + 1 >=
                    tableLength)
                {
                    break;
                }

                offset = next + 2;
            }

            if (details.totalSlots == 0)
            {
                details.totalSlots =
                    type17Count;
            }
        }
    }

    ULONGLONG installedMemoryKB = 0;

    MEMORYSTATUSEX memoryStatus = {};
    memoryStatus.dwLength =
        sizeof(memoryStatus);

    if (GetPhysicallyInstalledSystemMemory(
            &installedMemoryKB
        ) &&
        GlobalMemoryStatusEx(
            &memoryStatus
        ))
    {
        unsigned long long installedBytes =
            static_cast<unsigned long long>(
                installedMemoryKB
            ) * 1024ULL;

        unsigned long long usableBytes =
            static_cast<unsigned long long>(
                memoryStatus.ullTotalPhys
            );

        details.hardwareReservedValid = true;

        if (installedBytes > usableBytes)
        {
            details.hardwareReservedBytes =
                installedBytes -
                usableBytes;
        }
    }

    return details;
}


static const MemoryHardwareDetails&
getMemoryHardwareDetails()
{
    static const MemoryHardwareDetails
        details =
            queryMemoryHardwareDetails();

    return details;
}


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

    drawGraphGrid(
        hdc,
        x,
        y,
        width,
        height
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
// --------------------------------------------------------
// PURPLE FILL UNDER MEMORY GRAPH
// --------------------------------------------------------

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

HBRUSH memoryFillBrush =
    CreateSolidBrush(
        RGB(60, 38, 95)
    );

HGDIOBJ oldMemoryBrush =
    SelectObject(
        hdc,
        memoryFillBrush
    );

HGDIOBJ oldMemoryFillPen =
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
    oldMemoryFillPen
);

SelectObject(
    hdc,
    oldMemoryBrush
);

DeleteObject(
    memoryFillBrush
);


// Draw the grid again so it stays visible
// above the purple filled area.
drawGraphGrid(
    hdc,
    x,
    y,
    width,
    height
);

    HPEN graphPen =
        CreatePen(
            PS_SOLID,
            2,
            RGB(140, 80, 220)
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

void drawCurrentPercentGraph(
    HDC hdc,
    int x,
    int y,
    int width,
    int height,
    double percent,
    COLORREF lineColor,
    COLORREF fillColor)
{
    double clampedPercent =
        std::clamp(
            percent,
            0.0,
            100.0
        );

    drawRoundedBox(
        hdc,
        x,
        y,
        x + width,
        y + height,
        RGB(20, 24, 30)
    );

    drawGraphGrid(
        hdc,
        x,
        y,
        width,
        height
    );

    int lineY =
        y +
        height -
        static_cast<int>(
            (
                clampedPercent /
                100.0
            ) *
            height
        );

    POINT fillPoints[4] =
    {
        {
            static_cast<LONG>(x),
            static_cast<LONG>(lineY)
        },
        {
            static_cast<LONG>(x + width),
            static_cast<LONG>(lineY)
        },
        {
            static_cast<LONG>(x + width),
            static_cast<LONG>(y + height)
        },
        {
            static_cast<LONG>(x),
            static_cast<LONG>(y + height)
        }
    };

    HBRUSH fillBrush =
        CreateSolidBrush(
            fillColor
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
        fillPoints,
        4
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

    HPEN graphPen =
        CreatePen(
            PS_SOLID,
            2,
            lineColor
        );

    HGDIOBJ oldPen =
        SelectObject(
            hdc,
            graphPen
        );

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

    SelectObject(
        hdc,
        oldPen
    );

    DeleteObject(
        graphPen
    );
}


static void drawDiskHistoryGraph(
    HDC hdc,
    int x,
    int y,
    int width,
    int height,
    const std::vector<double>& history,
    double scaleMaximum,
    COLORREF lineColor,
    COLORREF fillColor)
{
    drawRoundedBox(
        hdc,
        x,
        y,
        x + width,
        y + height,
        RGB(20, 24, 30)
    );

    drawGraphGrid(
        hdc,
        x,
        y,
        width,
        height
    );

    if (
        history.size() < 2 ||
        scaleMaximum <= 0.0
    )
    {
        return;
    }

    std::vector<POINT> points(
        history.size()
    );

    for (
        size_t i = 0;
        i < history.size();
        i++
    )
    {
        double value =
            std::clamp(
                history[i],
                0.0,
                scaleMaximum
            );

        points[i].x =
            x +
            static_cast<int>(
                i *
                static_cast<double>(width) /
                (history.size() - 1)
            );

        points[i].y =
            y +
            height -
            static_cast<int>(
                (value /
                 scaleMaximum) *
                height
            );
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
            fillColor
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

    drawGraphGrid(
        hdc,
        x,
        y,
        width,
        height
    );

    HPEN graphPen =
        CreatePen(
            PS_SOLID,
            2,
            lineColor
        );

    HGDIOBJ oldPen =
        SelectObject(
            hdc,
            graphPen
        );

    Polyline(
        hdc,
        points.data(),
        static_cast<int>(
            points.size()
        )
    );

    SelectObject(
        hdc,
        oldPen
    );

    DeleteObject(
        graphPen
    );
}


static double getDiskTransferGraphScale(
    const DiskStats& disk)
{
    double maximum =
        disk.readMBps +
        disk.writeMBps;

    for (double value :
         disk.transferHistory)
    {
        if (value > maximum)
        {
            maximum = value;
        }
    }

    const double scales[] =
    {
        1.0,
        5.0,
        10.0,
        25.0,
        50.0,
        100.0,
        250.0,
        500.0,
        1000.0,
        2000.0,
        5000.0,
        10000.0
    };

    double target =
        maximum * 1.15;

    for (double scale : scales)
    {
        if (target <= scale)
        {
            return scale;
        }
    }

    return
        (std::max)(
            10000.0,
            target
        );
}


static double getNetworkGraphScale(
    const NetworkStats& adapter)
{
    double maximum =
        (std::max)(
            adapter.downloadMbps,
            adapter.uploadMbps
        );

    for (double value :
         adapter.downloadHistory)
    {
        maximum =
            (std::max)(maximum, value);
    }

    for (double value :
         adapter.uploadHistory)
    {
        maximum =
            (std::max)(maximum, value);
    }

    const double scales[] =
    {
        1.0,
        5.0,
        10.0,
        25.0,
        50.0,
        100.0,
        250.0,
        500.0,
        1000.0,
        2500.0,
        5000.0,
        10000.0
    };

    double target =
        (std::max)(10.0, maximum * 1.15);

    for (double scale : scales)
    {
        if (target <= scale)
        {
            return scale;
        }
    }

    return target;
}


static std::string formatNetworkSpeed(
    double megabitsPerSecond)
{
    std::ostringstream stream;

    if (megabitsPerSecond >= 1000.0)
    {
        stream
            << std::fixed
            << std::setprecision(2)
            << (megabitsPerSecond / 1000.0)
            << " Gbps";
    }
    else
    {
        stream
            << std::fixed
            << std::setprecision(
                megabitsPerSecond >= 10.0
                ? 1
                : 2
            )
            << megabitsPerSecond
            << " Mbps";
    }

    return stream.str();
}


static std::string formatNetworkBytes(
    unsigned long long bytes)
{
    std::ostringstream stream;

    const double kb = 1024.0;
    const double mb = kb * 1024.0;
    const double gb = mb * 1024.0;
    const double tb = gb * 1024.0;

    if (bytes >=
        static_cast<unsigned long long>(tb))
    {
        stream
            << std::fixed
            << std::setprecision(2)
            << (bytes / tb)
            << " TB";
    }
    else if (bytes >=
             static_cast<unsigned long long>(gb))
    {
        stream
            << std::fixed
            << std::setprecision(2)
            << (bytes / gb)
            << " GB";
    }
    else if (bytes >=
             static_cast<unsigned long long>(mb))
    {
        stream
            << std::fixed
            << std::setprecision(1)
            << (bytes / mb)
            << " MB";
    }
    else if (bytes >=
             static_cast<unsigned long long>(kb))
    {
        stream
            << std::fixed
            << std::setprecision(1)
            << (bytes / kb)
            << " KB";
    }
    else
    {
        stream << bytes << " B";
    }

    return stream.str();
}


static std::string formatNetworkDuration(
    ULONGLONG trackedSinceTick)
{
    if (trackedSinceTick == 0)
    {
        return "--";
    }

    ULONGLONG elapsed =
        (
            GetTickCount64() -
            trackedSinceTick
        ) / 1000;

    ULONGLONG hours =
        elapsed / 3600;
    ULONGLONG minutes =
        (elapsed % 3600) / 60;
    ULONGLONG seconds =
        elapsed % 60;

    std::ostringstream stream;
    stream
        << std::setfill('0')
        << std::setw(2)
        << hours
        << ":"
        << std::setw(2)
        << minutes
        << ":"
        << std::setw(2)
        << seconds;

    return stream.str();
}


static void drawNetworkHistoryGraph(
    HDC hdc,
    int x,
    int y,
    int width,
    int height,
    const NetworkStats& adapter,
    double scaleMaximum,
    COLORREF downloadColor,
    COLORREF uploadColor)
{
    drawRoundedBox(
        hdc,
        x,
        y,
        x + width,
        y + height,
        RGB(20, 24, 30)
    );

    drawGraphGrid(
        hdc,
        x,
        y,
        width,
        height
    );

    auto makePoints =
        [&](const std::vector<double>& history)
        -> std::vector<POINT>
    {
        std::vector<POINT> points;

        if (history.size() < 2 ||
            scaleMaximum <= 0.0)
        {
            return points;
        }

        points.resize(history.size());

        for (size_t i = 0;
             i < history.size();
             i++)
        {
            double value =
                std::clamp(
                    history[i],
                    0.0,
                    scaleMaximum
                );

            points[i].x =
                x +
                static_cast<int>(
                    i *
                    static_cast<double>(width) /
                    (history.size() - 1)
                );

            points[i].y =
                y +
                height -
                static_cast<int>(
                    (value / scaleMaximum) *
                    height
                );
        }

        return points;
    };

    std::vector<POINT> downloadPoints =
        makePoints(
            adapter.downloadHistory
        );

    std::vector<POINT> uploadPoints =
        makePoints(
            adapter.uploadHistory
        );

    if (!downloadPoints.empty())
    {
        std::vector<POINT> fillPoints =
            downloadPoints;

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
                RGB(74, 48, 20)
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
        DeleteObject(fillBrush);

        drawGraphGrid(
            hdc,
            x,
            y,
            width,
            height
        );

        HPEN pen =
            CreatePen(
                PS_SOLID,
                2,
                downloadColor
            );
        HGDIOBJ oldPen =
            SelectObject(
                hdc,
                pen
            );
        Polyline(
            hdc,
            downloadPoints.data(),
            static_cast<int>(
                downloadPoints.size()
            )
        );
        SelectObject(
            hdc,
            oldPen
        );
        DeleteObject(pen);
    }

    if (!uploadPoints.empty())
    {
        HPEN pen =
            CreatePen(
                PS_SOLID,
                2,
                uploadColor
            );
        HGDIOBJ oldPen =
            SelectObject(
                hdc,
                pen
            );
        Polyline(
            hdc,
            uploadPoints.data(),
            static_cast<int>(
                uploadPoints.size()
            )
        );
        SelectObject(
            hdc,
            oldPen
        );
        DeleteObject(pen);
    }
}


static std::string formatDiskSpeed(
    double megabytesPerSecond)
{
    std::ostringstream stream;

    if (megabytesPerSecond >= 1024.0)
    {
        stream
            << std::fixed
            << std::setprecision(1)
            << (megabytesPerSecond / 1024.0)
            << " GB/s";
    }
    else
    {
        stream
            << std::fixed
            << std::setprecision(1)
            << megabytesPerSecond
            << " MB/s";
    }

    return stream.str();
}


static std::string formatDiskCapacity(
    double gigabytes)
{
    if (gigabytes <= 0.0)
    {
        return "--";
    }

    std::ostringstream stream;

    stream
        << std::fixed
        << std::setprecision(
            gigabytes >= 100.0
            ? 0
            : 1
        )
        << gigabytes
        << " GB";

    return stream.str();
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

case AppPage::Temperatures:
    activeTop = 275;
    break;

case AppPage::SystemInfo:
    activeTop = 325;
    break;

case AppPage::Settings:
    activeTop = 375;
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
    "Temperatures",
    55,
    295,
    currentPage == AppPage::Temperatures
    ? textPrimary
    : textSecondary,
    labelFont
);

drawText(
    hdc,
    "About PC",
    55,
    345,
   currentPage == AppPage::SystemInfo
    ? textPrimary
    : textSecondary,
    labelFont
);

drawText(
    hdc,
    "Settings",
    55,
    395,
    currentPage == AppPage::Settings
    ? textPrimary
    : textSecondary,
    labelFont
);
POINT oldOrigin;
drawText(
    hdc,
    "v0.9-dev",
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
        pageSubtitle = "Real-time performance and resource usage.";
        break;

    case AppPage::Temperatures:
        pageTitle = "Temperatures";
        pageSubtitle = "Live CPU, GPU, and firmware thermal monitoring.";
        break;

    case AppPage::SystemInfo:
        pageTitle = "About";
        pageSubtitle = "Detailed information about your computer hardware and software.";
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

 if (currentPage != AppPage::Performance &&
    currentPage != AppPage::Temperatures &&
    currentPage != AppPage::SystemInfo)
{
    drawRoundedBox(
        hdc,
        35,
        115,
        915,
        680,
        card
    );
}
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
else if (currentPage == AppPage::SystemInfo)
{
    // --------------------------------------------------------
    // ABOUT PC / SYSTEM INFORMATION PAGE
    // --------------------------------------------------------

    const COLORREF infoCard =
        RGB(24, 27, 34);

    const COLORREF infoAccent =
        RGB(66, 135, 245);

    const COLORREF infoPurple =
        RGB(140, 80, 220);

    const COLORREF infoGreen =
        RGB(70, 200, 110);

    const COLORREF infoCyan =
        RGB(35, 190, 210);

    const COLORREF infoOrange =
        RGB(220, 140, 55);

    const int leftX = 35;
    const int leftRight = 465;
    const int rightX = 480;
    const int rightRight = 915;

    auto fitInfoText =
        [&](const std::string& source,
            int maxWidth,
            HFONT font)
        -> std::string
    {
        if (source.empty())
        {
            return "--";
        }

        std::string text = source;

        setFont(
            hdc,
            font
        );

        SIZE extent = {};

        GetTextExtentPoint32A(
            hdc,
            text.c_str(),
            static_cast<int>(
                text.size()
            ),
            &extent
        );

        if (extent.cx <= maxWidth)
        {
            return text;
        }

        while (text.size() > 3)
        {
            text.pop_back();

            std::string candidate =
                text + "...";

            GetTextExtentPoint32A(
                hdc,
                candidate.c_str(),
                static_cast<int>(
                    candidate.size()
                ),
                &extent
            );

            if (extent.cx <= maxWidth)
            {
                return candidate;
            }
        }

        return "...";
    };

    auto drawInfoCard =
        [&](int left,
            int top,
            int right,
            int bottom,
            const std::string& tag,
            const std::string& title,
            COLORREF accent)
    {
        drawRoundedBox(
            hdc,
            left,
            top,
            right,
            bottom,
            infoCard
        );

        drawRoundedBox(
            hdc,
            left + 18,
            top + 15,
            left + 57,
            top + 48,
            accent
        );

        drawText(
            hdc,
            tag,
            left + 24,
            top + 23,
            RGB(245, 247, 250),
            smallFont
        );

        drawText(
            hdc,
            title,
            left + 72,
            top + 20,
            textPrimary,
            labelFont
        );
    };

    auto drawInfoRow =
        [&](int left,
            int right,
            int y,
            const std::string& label,
            const std::string& value,
            int valueOffset = 170)
    {
        drawText(
            hdc,
            label,
            left + 20,
            y,
            textSecondary,
            smallFont
        );

        int valueX =
            left + valueOffset;

        drawText(
            hdc,
            fitInfoText(
                value,
                right - valueX - 15,
                smallFont
            ),
            valueX,
            y,
            textPrimary,
            smallFont
        );
    };

    int deviceCount =
        static_cast<int>(
            systemInfo.connectedDevices.size()
        );

    int shownDeviceCount =
        deviceCount;

    const ConnectedDeviceInfo*
        selectedConnectedDevice =
            nullptr;

    if (!selectedConnectedDeviceKey.empty())
    {
        for (const ConnectedDeviceInfo& device :
             systemInfo.connectedDevices)
        {
            if (
                device.selectionKey ==
                selectedConnectedDeviceKey
            )
            {
                selectedConnectedDevice =
                    &device;
                break;
            }
        }

        if (selectedConnectedDevice == nullptr)
        {
            selectedConnectedDeviceKey.clear();
        }
    }

    int deviceRows =
        (shownDeviceCount + 1) / 2;

    if (deviceRows < 2)
    {
        deviceRows = 2;
    }

    const int devicesTop = 945;
    const int deviceCardHeight =
        90 +
        deviceRows * 38;

    const int devicesBottom =
        devicesTop +
        deviceCardHeight;

    const int deviceDetailsTop =
        devicesBottom + 15;

    const int deviceDetailsBottom =
        deviceDetailsTop +
        (
            selectedConnectedDevice != nullptr
            ? 315
            : 110
        );

    const int aboutTop =
        deviceDetailsBottom + 15;

    const int aboutBottom =
        aboutTop + 95;

    const int visibleTop = 105;
    const int visibleBottom = 680;

    systemInfoMaxScrollOffset =
        (std::max)(
            0,
            aboutBottom -
            visibleBottom
        );

    systemInfoScrollOffset =
        std::clamp(
            systemInfoScrollOffset,
            0,
            systemInfoMaxScrollOffset
        );

    int savedDc =
        SaveDC(hdc);

    IntersectClipRect(
        hdc,
        25,
        visibleTop,
        925,
        visibleBottom
    );

    POINT currentOrigin = {};

    GetViewportOrgEx(
        hdc,
        &currentOrigin
    );

    SetViewportOrgEx(
        hdc,
        currentOrigin.x,
        currentOrigin.y -
            systemInfoScrollOffset,
        nullptr
    );

    // ----------------------------------------------------
    // OPERATING SYSTEM
    // ----------------------------------------------------
    drawInfoCard(
        leftX,
        115,
        leftRight,
        365,
        "OS",
        "Operating System",
        infoAccent
    );

    drawInfoRow(
        leftX,
        leftRight,
        170,
        "Name:",
        systemInfo.osName
    );

    drawInfoRow(
        leftX,
        leftRight,
        195,
        "Version:",
        systemInfo.osVersion
    );

    drawInfoRow(
        leftX,
        leftRight,
        220,
        "Installed on:",
        systemInfo.installedOn
    );

    drawInfoRow(
        leftX,
        leftRight,
        245,
        "OS build:",
        systemInfo.osBuild
    );

    drawInfoRow(
        leftX,
        leftRight,
        270,
        "Experience:",
        systemInfo.experience
    );

    drawInfoRow(
        leftX,
        leftRight,
        310,
        "System type:",
        systemInfo.systemType
    );

    drawInfoRow(
        leftX,
        leftRight,
        335,
        "PC name:",
        systemInfo.computerName
    );


    // ----------------------------------------------------
    // PROCESSOR
    // ----------------------------------------------------
    drawInfoCard(
        rightX,
        115,
        rightRight,
        365,
        "CPU",
        "Processor",
        infoAccent
    );

    drawInfoRow(
        rightX,
        rightRight,
        165,
        "Name:",
        systemInfo.cpuName,
        145
    );

    drawInfoRow(
        rightX,
        rightRight,
        187,
        "Cores:",
        systemInfo.cpuCores > 0
            ? std::to_string(
                systemInfo.cpuCores
              )
            : "--",
        145
    );

    drawInfoRow(
        rightX,
        rightRight,
        209,
        "Threads:",
        systemInfo.cpuThreads > 0
            ? std::to_string(
                systemInfo.cpuThreads
              )
            : "--",
        145
    );

    drawInfoRow(
        rightX,
        rightRight,
        231,
        "Base speed:",
        systemInfo.cpuBaseSpeed,
        145
    );

    drawInfoRow(
        rightX,
        rightRight,
        253,
        "Current speed:",
        systemInfo.cpuCurrentSpeed,
        145
    );

    drawInfoRow(
        rightX,
        rightRight,
        275,
        "Socket:",
        systemInfo.cpuSocket,
        145
    );

    drawInfoRow(
        rightX,
        rightRight,
        297,
        "Virtualization:",
        systemInfo.virtualization,
        145
    );

    drawInfoRow(
        rightX,
        rightRight,
        319,
        "L1 / L2 cache:",
        systemInfo.l1Cache +
            " / " +
            systemInfo.l2Cache,
        145
    );

    drawInfoRow(
        rightX,
        rightRight,
        341,
        "L3 cache:",
        systemInfo.l3Cache,
        145
    );


    // ----------------------------------------------------
    // MEMORY
    // ----------------------------------------------------
    drawInfoCard(
        leftX,
        380,
        leftRight,
        555,
        "RAM",
        "Memory",
        infoPurple
    );

    drawInfoRow(
        leftX,
        leftRight,
        435,
        "Installed memory:",
        systemInfo.installedMemory,
        185
    );

    drawInfoRow(
        leftX,
        leftRight,
        458,
        "Type:",
        systemInfo.memoryType,
        185
    );

    drawInfoRow(
        leftX,
        leftRight,
        481,
        "Speed:",
        systemInfo.memorySpeed,
        185
    );

    drawInfoRow(
        leftX,
        leftRight,
        504,
        "Slots used:",
        systemInfo.memorySlots,
        185
    );

    drawInfoRow(
        leftX,
        leftRight,
        527,
        "Form factor:",
        systemInfo.memoryFormFactor,
        185
    );


    // ----------------------------------------------------
    // GRAPHICS
    // ----------------------------------------------------
    drawInfoCard(
        rightX,
        380,
        rightRight,
        555,
        "GPU",
        "Graphics",
        infoPurple
    );

    drawInfoRow(
        rightX,
        rightRight,
        435,
        "Name:",
        systemInfo.gpuName,
        145
    );

    drawInfoRow(
        rightX,
        rightRight,
        458,
        "Memory:",
        systemInfo.gpuMemory,
        145
    );

    drawInfoRow(
        rightX,
        rightRight,
        481,
        "Driver version:",
        systemInfo.gpuDriverVersion,
        145
    );

    drawInfoRow(
        rightX,
        rightRight,
        504,
        "Driver date:",
        systemInfo.gpuDriverDate,
        145
    );

    drawInfoRow(
        rightX,
        rightRight,
        527,
        "DirectX version:",
        systemInfo.directXVersion,
        145
    );


    // ----------------------------------------------------
    // STORAGE
    // ----------------------------------------------------
    drawInfoCard(
        leftX,
        570,
        leftRight,
        760,
        "DSK",
        "Storage",
        infoGreen
    );

    if (diskStats.empty())
    {
        drawText(
            hdc,
            "No physical disks detected.",
            leftX + 20,
            625,
            textSecondary,
            smallFont
        );
    }
    else
    {
        int diskY = 625;
        int visibleDisks =
            (std::min)(
                static_cast<int>(
                    diskStats.size()
                ),
                3
            );

        for (int index = 0;
             index < visibleDisks;
             index++)
        {
            const DiskStats& disk =
                diskStats[index];

            drawText(
                hdc,
                fitInfoText(
                    disk.displayName,
                    115,
                    smallFont
                ),
                leftX + 20,
                diskY,
                textPrimary,
                smallFont
            );

            drawText(
                hdc,
                fitInfoText(
                    disk.model,
                    185,
                    smallFont
                ),
                leftX + 125,
                diskY,
                textPrimary,
                smallFont
            );

            std::ostringstream capacity;

            if (disk.capacityGB >= 1024.0)
            {
                capacity
                    << std::fixed
                    << std::setprecision(1)
                    << (disk.capacityGB / 1024.0)
                    << " TB";
            }
            else
            {
                capacity
                    << std::fixed
                    << std::setprecision(0)
                    << disk.capacityGB
                    << " GB";
            }

            drawText(
                hdc,
                fitInfoText(
                    capacity.str() +
                        " - " +
                        disk.type,
                    115,
                    smallFont
                ),
                leftX + 315,
                diskY,
                textSecondary,
                smallFont
            );

            diskY += 38;
        }

        if (static_cast<int>(diskStats.size()) >
            visibleDisks)
        {
            drawText(
                hdc,
                "+" +
                    std::to_string(
                        static_cast<int>(
                            diskStats.size()
                        ) -
                        visibleDisks
                    ) +
                    " more disk(s)",
                leftX + 20,
                735,
                textSecondary,
                smallFont
            );
        }
    }


    // ----------------------------------------------------
    // NETWORK
    // ----------------------------------------------------
    drawInfoCard(
        rightX,
        570,
        rightRight,
        760,
        "NET",
        "Network",
        infoCyan
    );

    drawInfoRow(
        rightX,
        rightRight,
        625,
        "Adapter:",
        systemInfo.networkAdapter,
        155
    );

    drawInfoRow(
        rightX,
        rightRight,
        650,
        "Connection type:",
        systemInfo.networkConnectionType,
        155
    );

    drawInfoRow(
        rightX,
        rightRight,
        675,
        "IPv4 address:",
        systemInfo.ipv4Address,
        155
    );

    drawInfoRow(
        rightX,
        rightRight,
        700,
        "IPv6 address:",
        systemInfo.ipv6Address,
        155
    );


    // ----------------------------------------------------
    // SYSTEM / MOTHERBOARD
    // ----------------------------------------------------
    drawInfoCard(
        leftX,
        775,
        leftRight,
        930,
        "MB",
        "System / Motherboard",
        infoOrange
    );

    drawInfoRow(
        leftX,
        leftRight,
        830,
        "System maker:",
        systemInfo.systemManufacturer,
        180
    );

    drawInfoRow(
        leftX,
        leftRight,
        855,
        "System model:",
        systemInfo.systemModel,
        180
    );

    drawInfoRow(
        leftX,
        leftRight,
        880,
        "Board maker:",
        systemInfo.motherboardManufacturer,
        180
    );

    drawInfoRow(
        leftX,
        leftRight,
        905,
        "Board model:",
        systemInfo.motherboardModel,
        180
    );


    // ----------------------------------------------------
    // BIOS
    // ----------------------------------------------------
    drawInfoCard(
        rightX,
        775,
        rightRight,
        930,
        "BIO",
        "BIOS / Firmware",
        infoOrange
    );

    drawInfoRow(
        rightX,
        rightRight,
        830,
        "Vendor:",
        systemInfo.biosVendor,
        145
    );

    drawInfoRow(
        rightX,
        rightRight,
        855,
        "Version:",
        systemInfo.biosVersion,
        145
    );

    drawInfoRow(
        rightX,
        rightRight,
        880,
        "Release date:",
        systemInfo.biosDate,
        145
    );


    // ----------------------------------------------------
    // CONNECTED DEVICES
    // ----------------------------------------------------
    drawInfoCard(
        leftX,
        devicesTop,
        rightRight,
        devicesBottom,
        "DEV",
        "Connected Devices",
        infoCyan
    );

    drawText(
        hdc,
        "Currently present USB, Bluetooth, HID, display and user-facing devices. Click a device for details.",
        leftX + 72,
        devicesTop + 45,
        textSecondary,
        smallFont
    );

    if (shownDeviceCount == 0)
    {
        drawText(
            hdc,
            "No matching connected devices were detected.",
            leftX + 20,
            devicesTop + 82,
            textSecondary,
            smallFont
        );
    }
    else
    {
        for (int index = 0;
             index < shownDeviceCount;
             index++)
        {
            int column =
                index % 2;

            int row =
                index / 2;

            int itemLeft =
                column == 0
                ? leftX + 20
                : leftX + 455;

            int itemWidth =
                column == 0
                ? 390
                : 410;

            int itemY =
                devicesTop +
                80 +
                row * 38;

            const ConnectedDeviceInfo& device =
                systemInfo.connectedDevices[
                    index
                ];

            bool selected =
                !selectedConnectedDeviceKey.empty() &&
                device.selectionKey ==
                    selectedConnectedDeviceKey;

            drawRoundedBox(
                hdc,
                itemLeft - 6,
                itemY - 7,
                itemLeft + itemWidth,
                itemY + 24,
                selected
                    ? RGB(37, 54, 73)
                    : RGB(28, 31, 39)
            );

            drawText(
                hdc,
                "[" +
                    device.connectionType +
                    "]",
                itemLeft,
                itemY,
                selected
                    ? RGB(105, 170, 250)
                    : infoAccent,
                smallFont
            );

            drawText(
                hdc,
                fitInfoText(
                    device.name,
                    itemWidth - 125,
                    smallFont
                ),
                itemLeft + 115,
                itemY,
                textPrimary,
                smallFont
            );
        }
    }


    // ----------------------------------------------------
    // SELECTED DEVICE DETAILS
    // ----------------------------------------------------
    drawInfoCard(
        leftX,
        deviceDetailsTop,
        rightRight,
        deviceDetailsBottom,
        "DET",
        "Device Details",
        infoCyan
    );

    if (selectedConnectedDevice == nullptr)
    {
        drawText(
            hdc,
            "Select any connected device above to view its hardware and connection details.",
            leftX + 20,
            deviceDetailsTop + 68,
            textSecondary,
            smallFont
        );
    }
    else
    {
        const ConnectedDeviceInfo& device =
            *selectedConnectedDevice;

        drawInfoRow(
            leftX,
            rightRight,
            deviceDetailsTop + 55,
            "Name:",
            device.name,
            170
        );

        drawInfoRow(
            leftX,
            rightRight,
            deviceDetailsTop + 82,
            "Type:",
            device.type,
            170
        );

        drawInfoRow(
            leftX,
            rightRight,
            deviceDetailsTop + 109,
            "Connection:",
            device.connectionType,
            170
        );

        drawInfoRow(
            leftX,
            rightRight,
            deviceDetailsTop + 136,
            "Device class:",
            device.deviceClass,
            170
        );

        drawInfoRow(
            leftX,
            rightRight,
            deviceDetailsTop + 163,
            "Manufacturer:",
            device.manufacturer,
            170
        );

        drawInfoRow(
            leftX,
            rightRight,
            deviceDetailsTop + 190,
            "Status:",
            device.status,
            170
        );

        drawInfoRow(
            leftX,
            rightRight,
            deviceDetailsTop + 217,
            "Location:",
            device.location,
            170
        );

        std::string idText;

        if (
            device.vendorId == "--" &&
            device.productId == "--"
        )
        {
            idText = "--";
        }
        else
        {
            idText =
                "Vendor " +
                device.vendorId +
                " / Product " +
                device.productId;
        }

        drawInfoRow(
            leftX,
            rightRight,
            deviceDetailsTop + 244,
            "Vendor / Product:",
            idText,
            170
        );

        drawInfoRow(
            leftX,
            rightRight,
            deviceDetailsTop + 271,
            "Instance ID:",
            device.instanceId,
            170
        );

        drawInfoRow(
            leftX,
            rightRight,
            deviceDetailsTop + 298,
            "Hardware ID:",
            device.hardwareId,
            170
        );
    }


    // ----------------------------------------------------
    // SYSMON FOOTER CARD
    // ----------------------------------------------------
    drawRoundedBox(
        hdc,
        leftX,
        aboutTop,
        rightRight,
        aboutBottom,
        infoCard
    );

    drawRoundedBox(
        hdc,
        leftX + 20,
        aboutTop + 18,
        leftX + 70,
        aboutTop + 68,
        infoAccent
    );

    drawText(
        hdc,
        "SM",
        leftX + 31,
        aboutTop + 33,
        RGB(245, 247, 250),
        labelFont
    );

    drawText(
        hdc,
        "SysMon System Monitor",
        leftX + 90,
        aboutTop + 22,
        textPrimary,
        labelFont
    );

    drawText(
        hdc,
        "Version 0.9-dev",
        leftX + 90,
        aboutTop + 49,
        textSecondary,
        smallFont
    );

    drawText(
        hdc,
        "Real-time Windows hardware and software monitoring.",
        560,
        aboutTop + 38,
        textSecondary,
        smallFont
    );

    RestoreDC(
        hdc,
        savedDc
    );


    // ----------------------------------------------------
    // PAGE SCROLLBAR
    // ----------------------------------------------------
    if (systemInfoMaxScrollOffset > 0)
    {
        const int barLeft = 905;
        const int barTop = 115;
        const int barBottom = 680;
        const int barHeight =
            barBottom - barTop;

        drawRoundedBox(
            hdc,
            barLeft,
            barTop,
            barLeft + 7,
            barBottom,
            RGB(24, 28, 36)
        );

        double visibleRatio =
            static_cast<double>(
                visibleBottom -
                visibleTop
            ) /
            static_cast<double>(
                aboutBottom -
                visibleTop
            );

        int thumbHeight =
            static_cast<int>(
                barHeight *
                visibleRatio
            );

        thumbHeight =
            std::clamp(
                thumbHeight,
                55,
                barHeight
            );

        int thumbTravel =
            barHeight -
            thumbHeight;

        int thumbTop =
            barTop;

        if (systemInfoMaxScrollOffset > 0)
        {
            thumbTop +=
                static_cast<int>(
                    static_cast<double>(
                        systemInfoScrollOffset
                    ) /
                    static_cast<double>(
                        systemInfoMaxScrollOffset
                    ) *
                    thumbTravel
                );
        }

        drawRoundedBox(
            hdc,
            barLeft + 1,
            thumbTop,
            barLeft + 6,
            thumbTop +
                thumbHeight,
            RGB(105, 115, 132)
        );
    }
}

else if (currentPage == AppPage::Temperatures)
{
    const COLORREF tempCard =
        RGB(24, 27, 34);

    const COLORREF tempPanel =
        RGB(20, 24, 30);

    const COLORREF cpuBlue =
        RGB(45, 150, 245);

    const COLORREF gpuPurple =
        RGB(165, 55, 235);

    const COLORREF boardOrange =
        RGB(220, 140, 55);

    auto shortText =
        [](const std::string& value,
           size_t maximum)
        -> std::string
    {
        if (value.size() <= maximum)
        {
            return value;
        }

        if (maximum <= 3)
        {
            return value.substr(0, maximum);
        }

        return
            value.substr(0, maximum - 3) +
            "...";
    };

    auto formatTemperature =
        [](double value)
        -> std::string
    {
        if (value < 0.0)
        {
            return "-- C";
        }

        std::ostringstream stream;
        stream
            << std::fixed
            << std::setprecision(1)
            << value
            << " C";

        return stream.str();
    };

    auto drawTemperatureTab =
        [&](int left,
            int right,
            const std::string& text,
            bool selected,
            COLORREF accent)
    {
        drawRoundedBox(
            hdc,
            left,
            115,
            right,
            170,
            selected
                ? RGB(31, 49, 67)
                : tempCard
        );

        if (selected)
        {
            drawRoundedBox(
                hdc,
                left,
                115,
                left + 5,
                170,
                accent
            );
        }

        drawText(
            hdc,
            text,
            left + 18,
            133,
            selected
                ? textPrimary
                : textSecondary,
            labelFont
        );
    };

    drawTemperatureTab(
        35,
        185,
        "CPU",
        temperatureView ==
            TemperatureView::CPU,
        cpuBlue
    );

    drawTemperatureTab(
        200,
        350,
        "GPU",
        temperatureView ==
            TemperatureView::GPU,
        gpuPurple
    );

    drawTemperatureTab(
        365,
        540,
        "Motherboard",
        temperatureView ==
            TemperatureView::Motherboard,
        boardOrange
    );


    // --------------------------------------------------------
    // CPU TEMPERATURE VIEW
    // --------------------------------------------------------
    if (temperatureView == TemperatureView::CPU)
    {
        drawText(
            hdc,
            "CPU",
            35,
            185,
            textPrimary,
            titleFont
        );

        drawText(
            hdc,
            "Real-time CPU performance, temperature, and utilization.",
            37,
            216,
            textSecondary,
            smallFont
        );

        const int summaryTop = 235;
        const int summaryBottom = 300;

        auto drawCpuSummary =
            [&](int left,
                int right,
                const std::string& label,
                const std::string& value,
                COLORREF accent)
        {
            drawRoundedBox(
                hdc,
                left,
                summaryTop,
                right,
                summaryBottom,
                tempCard
            );

            drawRoundedBox(
                hdc,
                left + 12,
                summaryTop + 14,
                left + 17,
                summaryBottom - 14,
                accent
            );

            drawText(
                hdc,
                label,
                left + 28,
                summaryTop + 10,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                value,
                left + 28,
                summaryTop + 32,
                textPrimary,
                labelFont
            );
        };

        std::ostringstream utilization;
        utilization
            << std::fixed
            << std::setprecision(0)
            << cpuUsage
            << "%";

        drawCpuSummary(
            35,
            240,
            "Utilization",
            utilization.str(),
            cpuBlue
        );

        drawCpuSummary(
            250,
            455,
            "Temperature",
            formatTemperature(
                temperatureStats.cpuTemperatureC
            ),
            cpuBlue
        );

        std::string currentSpeed =
            systemInfo.cpuCurrentSpeed;

        if (temperatureStats.cpuAverageClockMHz > 0.0)
        {
            std::ostringstream clockStream;

            if (temperatureStats.cpuAverageClockMHz >= 1000.0)
            {
                clockStream
                    << std::fixed
                    << std::setprecision(2)
                    << (temperatureStats.cpuAverageClockMHz / 1000.0)
                    << " GHz";
            }
            else
            {
                clockStream
                    << std::fixed
                    << std::setprecision(0)
                    << temperatureStats.cpuAverageClockMHz
                    << " MHz";
            }

            currentSpeed = clockStream.str();
        }
        else if (currentSpeed.empty() ||
                 currentSpeed == "--")
        {
            currentSpeed =
                systemInfo.cpuBaseSpeed;
        }

        drawCpuSummary(
            465,
            670,
            "Clock Speed",
            currentSpeed,
            cpuBlue
        );

        std::string powerUsage = "-- W";

        if (temperatureStats.cpuPackagePowerW >= 0.0)
        {
            std::ostringstream powerStream;
            powerStream
                << std::fixed
                << std::setprecision(1)
                << temperatureStats.cpuPackagePowerW
                << " W";
            powerUsage = powerStream.str();
        }

        drawCpuSummary(
            680,
            915,
            "Power Usage",
            powerUsage,
            cpuBlue
        );

        // Main temperature graph.
        drawRoundedBox(
            hdc,
            35,
            315,
            650,
            505,
            tempCard
        );

        drawText(
            hdc,
            "CPU Temperature",
            55,
            330,
            textPrimary,
            labelFont
        );

        std::string currentText =
            formatTemperature(
                temperatureStats.cpuTemperatureC
            );

        drawText(
            hdc,
            "Current: " + currentText,
            425,
            332,
            cpuBlue,
            smallFont
        );

        if (!temperatureStats.
                cpuTemperatureHistory.empty())
        {
            auto minimum =
                std::min_element(
                    temperatureStats.
                        cpuTemperatureHistory.begin(),
                    temperatureStats.
                        cpuTemperatureHistory.end()
                );

            auto maximum =
                std::max_element(
                    temperatureStats.
                        cpuTemperatureHistory.begin(),
                    temperatureStats.
                        cpuTemperatureHistory.end()
                );

            drawText(
                hdc,
                "Min: " +
                    formatTemperature(*minimum),
                515,
                332,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "Max: " +
                    formatTemperature(*maximum),
                585,
                332,
                textSecondary,
                smallFont
            );
        }

        drawText(
            hdc,
            "100 C",
            48,
            355,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "50 C",
            52,
            411,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "0 C",
            58,
            467,
            textSecondary,
            smallFont
        );

        drawDiskHistoryGraph(
            hdc,
            88,
            355,
            540,
            115,
            temperatureStats.cpuTemperatureHistory,
            100.0,
            cpuBlue,
            RGB(24, 48, 74)
        );

        drawText(
            hdc,
            "60 seconds ago",
            88,
            474,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "Now",
            600,
            474,
            textSecondary,
            smallFont
        );

        if (temperatureStats.cpuTemperatureC < 0.0)
        {
            drawText(
                hdc,
                temperatureStats.hardwareSensorAvailable
                    ? "The sensor engine is connected, but this CPU did not expose a package temperature."
                    : "Hardware sensor engine unavailable - CPU temperature cannot be read yet.",
                115,
                407,
                textSecondary,
                smallFont
            );
        }

        // Temperature details.  Core rows are generated from the
        // detected physical-core count. A value is filled only when
        // the CPU exposes a genuine per-core temperature sensor.
        drawRoundedBox(
            hdc,
            665,
            315,
            915,
            505,
            tempCard
        );

        drawText(
            hdc,
            "Temperature Details",
            685,
            330,
            textPrimary,
            labelFont
        );

        int physicalCoreCount =
            systemInfo.cpuCores;

        if (
            static_cast<int>(
                temperatureStats.cpuCoreTemperatures.size()
            ) > physicalCoreCount
        )
        {
            physicalCoreCount =
                static_cast<int>(
                    temperatureStats.cpuCoreTemperatures.size()
                );
        }

        if (physicalCoreCount < 0)
        {
            physicalCoreCount = 0;
        }

        int visibleCoreCount =
            physicalCoreCount;

        if (visibleCoreCount > 16)
        {
            visibleCoreCount = 16;
        }

        int rowsPerColumn =
            visibleCoreCount > 0
            ? (visibleCoreCount + 1) / 2
            : 0;

        if (rowsPerColumn > 8)
        {
            rowsPerColumn = 8;
        }

        for (int index = 0;
             index < visibleCoreCount;
             index++)
        {
            int column =
                index / rowsPerColumn;
            int row =
                index % rowsPerColumn;

            int labelX =
                column == 0 ? 685 : 805;
            int valueX =
                column == 0 ? 750 : 870;
            int rowY =
                358 + row * 18;

            drawText(
                hdc,
                "Core " + std::to_string(index),
                labelX,
                rowY,
                textSecondary,
                smallFont
            );

            double coreTemperature = -1.0;

            if (
                index <
                static_cast<int>(
                    temperatureStats.
                        cpuCoreTemperatures.size()
                )
            )
            {
                coreTemperature =
                    temperatureStats.
                        cpuCoreTemperatures[
                            static_cast<size_t>(index)
                        ];
            }

            drawText(
                hdc,
                formatTemperature(coreTemperature),
                valueX,
                rowY,
                coreTemperature >= 0.0
                    ? cpuBlue
                    : textSecondary,
                smallFont
            );
        }

        int detailY =
            358 + rowsPerColumn * 18 + 6;

        // On CPUs with fewer cores, use the remaining space for
        // package/die/CCD readings supplied by the hardware backend.
        for (
            size_t sensorIndex = 0;
            sensorIndex < temperatureStats.cpuSensors.size() &&
            detailY <= 476;
            sensorIndex++
        )
        {
            const ThermalSensorInfo& sensor =
                temperatureStats.cpuSensors[sensorIndex];

            std::string upperName = sensor.name;
            std::transform(
                upperName.begin(),
                upperName.end(),
                upperName.begin(),
                [](unsigned char ch)
                {
                    return static_cast<char>(
                        std::toupper(ch)
                    );
                }
            );

            // Per-core sensors are already represented by the Core
            // rows above. Keep the lower rows for package/die/CCD.
            if (upperName.find("CORE #") !=
                std::string::npos)
            {
                continue;
            }

            drawText(
                hdc,
                shortText(sensor.name, 17),
                685,
                detailY,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                formatTemperature(sensor.temperatureC),
                835,
                detailY,
                cpuBlue,
                smallFont
            );

            detailY += 18;
        }

        // Keep Windows/firmware ACPI zones underneath the real CPU
        // sensors exactly as additional readings, not as fake cores.
        if (
            !temperatureStats.sensors.empty() &&
            detailY <= 476
        )
        {
            const ThermalSensorInfo& acpiSensor =
                temperatureStats.sensors.front();

            drawText(
                hdc,
                shortText(acpiSensor.name, 17),
                685,
                detailY,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                formatTemperature(acpiSensor.temperatureC),
                835,
                detailY,
                boardOrange,
                smallFont
            );
        }

        if (physicalCoreCount > visibleCoreCount)
        {
            drawText(
                hdc,
                "+" +
                    std::to_string(
                        physicalCoreCount - visibleCoreCount
                    ) +
                    " more cores",
                805,
                488,
                textSecondary,
                smallFont
            );
        }

        // CPU information.
        drawRoundedBox(
            hdc,
            35,
            520,
            300,
            680,
            tempCard
        );

        drawText(
            hdc,
            "CPU Information",
            55,
            535,
            textPrimary,
            labelFont
        );

        int cpuInfoY = 565;
        const int cpuInfoGap = 20;

        auto drawCpuInfo =
            [&](const std::string& label,
                const std::string& value)
        {
            drawText(
                hdc,
                label,
                55,
                cpuInfoY,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                shortText(value, 21),
                140,
                cpuInfoY,
                textPrimary,
                smallFont
            );

            cpuInfoY += cpuInfoGap;
        };

        drawCpuInfo(
            "Name:",
            systemInfo.cpuName
        );
        drawCpuInfo(
            "Cores:",
            std::to_string(
                systemInfo.cpuCores
            )
        );
        drawCpuInfo(
            "Threads:",
            std::to_string(
                systemInfo.cpuThreads
            )
        );
        drawCpuInfo(
            "Base:",
            systemInfo.cpuBaseSpeed
        );
        drawCpuInfo(
            "Socket:",
            systemInfo.cpuSocket
        );

        // Per logical processor utilization.
        drawRoundedBox(
            hdc,
            315,
            520,
            610,
            680,
            tempCard
        );

        drawText(
            hdc,
            "Utilization per Logical CPU",
            335,
            535,
            textPrimary,
            labelFont
        );

        int logicalCount =
            (std::min)(
                6,
                static_cast<int>(
                    cpuCoreUsage.size()
                )
            );

        for (int index = 0;
             index < logicalCount;
             index++)
        {
            int rowY =
                565 +
                index * 18;

            drawText(
                hdc,
                "CPU " +
                    std::to_string(index),
                335,
                rowY,
                textSecondary,
                smallFont
            );

            drawRoundedBox(
                hdc,
                395,
                rowY + 3,
                535,
                rowY + 12,
                RGB(37, 46, 56)
            );

            int fillWidth =
                static_cast<int>(
                    140.0 *
                    cpuCoreUsage[index] /
                    100.0
                );

            if (fillWidth > 0)
            {
                drawRoundedBox(
                    hdc,
                    395,
                    rowY + 3,
                    395 + fillWidth,
                    rowY + 12,
                    cpuBlue
                );
            }

            std::ostringstream corePercent;
            corePercent
                << std::fixed
                << std::setprecision(0)
                << cpuCoreUsage[index]
                << "%";

            drawText(
                hdc,
                corePercent.str(),
                550,
                rowY,
                textPrimary,
                smallFont
            );
        }

        if (
            static_cast<int>(
                cpuCoreUsage.size()
            ) > logicalCount
        )
        {
            drawText(
                hdc,
                "+" +
                    std::to_string(
                        static_cast<int>(
                            cpuCoreUsage.size()
                        ) - logicalCount
                    ) +
                    " more logical CPUs",
                335,
                665,
                textSecondary,
                smallFont
            );
        }

        // Thermal status.
        drawRoundedBox(
            hdc,
            625,
            520,
            915,
            680,
            tempCard
        );

        drawText(
            hdc,
            "Thermal Status",
            645,
            535,
            textPrimary,
            labelFont
        );

        if (temperatureStats.cpuTemperatureC >= 0.0)
        {
            drawText(
                hdc,
                temperatureStats.cpuSensorFromHardware
                    ? "LIVE HARDWARE SENSOR"
                    : "LIVE ACPI SENSOR",
                645,
                575,
                cpuBlue,
                labelFont
            );

            drawText(
                hdc,
                "Current:",
                645,
                610,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                currentText,
                735,
                610,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                "Source:",
                645,
                640,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                shortText(
                    temperatureStats.cpuSensorName,
                    24
                ),
                700,
                640,
                textPrimary,
                smallFont
            );

            drawText(
                hdc,
                temperatureStats.cpuSensorFromHardware
                    ? "Read by SysMon's hardware sensor engine."
                    : "Read from a CPU-identified Windows ACPI thermal zone.",
                645,
                662,
                textSecondary,
                smallFont
            );
        }
        else
        {
            drawText(
                hdc,
                temperatureStats.hardwareSensorAvailable
                    ? "NO CPU TEMP SENSOR"
                    : "SENSOR ENGINE OFFLINE",
                645,
                575,
                textSecondary,
                labelFont
            );

            drawText(
                hdc,
                temperatureStats.hardwareSensorAvailable
                    ? "The CPU did not expose a package/core temperature sensor."
                    : "SysMonSensors is not providing hardware readings.",
                645,
                615,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "Unsupported readings stay as -- instead of being estimated.",
                645,
                642,
                textSecondary,
                smallFont
            );
        }
    }


    // --------------------------------------------------------
    // GPU TEMPERATURE VIEW
    // --------------------------------------------------------
    else if (temperatureView == TemperatureView::GPU)
    {
        if (gpuStats.empty())
        {
            drawRoundedBox(
                hdc,
                35,
                185,
                915,
                680,
                tempCard
            );

            drawText(
                hdc,
                "GPU",
                55,
                205,
                textPrimary,
                titleFont
            );

            drawText(
                hdc,
                "No compatible GPU adapter was detected.",
                55,
                265,
                textSecondary,
                labelFont
            );
        }
        else
        {
            if (
                selectedTemperatureGpuIndex < 0 ||
                selectedTemperatureGpuIndex >=
                    static_cast<int>(
                        gpuStats.size()
                    )
            )
            {
                selectedTemperatureGpuIndex = 0;
            }

            const GpuStats& gpu =
                gpuStats[
                    selectedTemperatureGpuIndex
                ];

            const double bytesPerGpuGB =
                1024.0 * 1024.0 * 1024.0;

            auto percentText =
                [](double value)
                -> std::string
            {
                std::ostringstream stream;
                stream
                    << std::fixed
                    << std::setprecision(0)
                    << value
                    << "%";
                return stream.str();
            };

            auto gpuMemoryText =
                [&](unsigned long long used,
                    unsigned long long total)
                -> std::string
            {
                if (total == 0)
                {
                    return "--";
                }

                std::ostringstream stream;
                stream
                    << std::fixed
                    << std::setprecision(1)
                    << used / bytesPerGpuGB
                    << " / "
                    << total / bytesPerGpuGB
                    << " GB";
                return stream.str();
            };

            drawText(
                hdc,
                "GPU " +
                    std::to_string(
                        selectedTemperatureGpuIndex
                    ),
                35,
                185,
                textPrimary,
                titleFont
            );

            drawText(
                hdc,
                "Real-time GPU performance, temperature, and memory usage.",
                37,
                216,
                textSecondary,
                smallFont
            );

            std::string gpuName =
                shortText(
                    gpu.name,
                    42
                );

            SIZE gpuExtent = {};
            setFont(hdc, labelFont);
            GetTextExtentPoint32A(
                hdc,
                gpuName.c_str(),
                static_cast<int>(
                    gpuName.size()
                ),
                &gpuExtent
            );

            int gpuNameX =
                890 - gpuExtent.cx;

            if (gpuNameX < 575)
            {
                gpuNameX = 575;
            }

            drawText(
                hdc,
                gpuName,
                gpuNameX,
                192,
                textSecondary,
                labelFont
            );

            if (gpuStats.size() > 1)
            {
                drawRoundedBox(
                    hdc,
                    830,
                    190,
                    853,
                    220,
                    RGB(36, 42, 54)
                );
                drawText(
                    hdc,
                    "<",
                    838,
                    195,
                    textPrimary,
                    labelFont
                );

                drawRoundedBox(
                    hdc,
                    857,
                    190,
                    880,
                    220,
                    RGB(36, 42, 54)
                );
                drawText(
                    hdc,
                    ">",
                    865,
                    195,
                    textPrimary,
                    labelFont
                );
            }

            const int gpuSummaryTop = 235;
            const int gpuSummaryBottom = 300;

            auto drawGpuSummary =
                [&](int left,
                    int right,
                    const std::string& label,
                    const std::string& value)
            {
                drawRoundedBox(
                    hdc,
                    left,
                    gpuSummaryTop,
                    right,
                    gpuSummaryBottom,
                    tempCard
                );

                drawRoundedBox(
                    hdc,
                    left + 12,
                    gpuSummaryTop + 14,
                    left + 17,
                    gpuSummaryBottom - 14,
                    gpuPurple
                );

                drawText(
                    hdc,
                    label,
                    left + 28,
                    gpuSummaryTop + 10,
                    textSecondary,
                    smallFont
                );

                drawText(
                    hdc,
                    value,
                    left + 28,
                    gpuSummaryTop + 32,
                    textPrimary,
                    labelFont
                );
            };

            drawGpuSummary(
                35,
                240,
                "Utilization",
                percentText(
                    gpu.utilizationPercent
                )
            );

            drawGpuSummary(
                250,
                455,
                "Temperature",
                formatTemperature(
                    gpu.temperatureC
                )
            );

            drawGpuSummary(
                465,
                670,
                "Memory Usage",
                gpuMemoryText(
                    gpu.dedicatedMemoryUsedBytes,
                    gpu.dedicatedMemoryTotalBytes
                )
            );

            std::string fanText = "--";
            if (gpu.fanPercent >= 0)
            {
                fanText =
                    std::to_string(
                        gpu.fanPercent
                    ) +
                    "%";
            }
            else if (gpu.fanRpm >= 0)
            {
                fanText =
                    std::to_string(
                        gpu.fanRpm
                    ) +
                    " RPM";
            }

            drawGpuSummary(
                680,
                915,
                "Fan Speed",
                fanText
            );

            // Utilization graph.
            drawRoundedBox(
                hdc,
                35,
                315,
                430,
                465,
                tempCard
            );
            drawText(
                hdc,
                "GPU Utilization",
                55,
                330,
                textPrimary,
                labelFont
            );
            drawDiskHistoryGraph(
                hdc,
                55,
                360,
                355,
                85,
                gpu.utilizationHistory,
                100.0,
                gpuPurple,
                RGB(49, 26, 69)
            );
            drawText(
                hdc,
                "60 seconds ago",
                55,
                447,
                textSecondary,
                smallFont
            );
            drawText(
                hdc,
                "Now",
                382,
                447,
                textSecondary,
                smallFont
            );

            // Temperature graph.
            drawRoundedBox(
                hdc,
                445,
                315,
                690,
                465,
                tempCard
            );
            drawText(
                hdc,
                "GPU Temperature",
                465,
                330,
                textPrimary,
                labelFont
            );
            drawDiskHistoryGraph(
                hdc,
                465,
                360,
                205,
                85,
                gpu.temperatureHistory,
                100.0,
                gpuPurple,
                RGB(49, 26, 69)
            );
            drawText(
                hdc,
                "100 C",
                620,
                343,
                textSecondary,
                smallFont
            );

            if (gpu.temperatureC < 0.0)
            {
                drawText(
                    hdc,
                    "Sensor unavailable",
                    500,
                    397,
                    textSecondary,
                    smallFont
                );
            }

            // GPU information.
            drawRoundedBox(
                hdc,
                705,
                315,
                915,
                610,
                tempCard
            );
            drawText(
                hdc,
                "GPU Information",
                725,
                330,
                textPrimary,
                labelFont
            );

            int gpuInfoY = 365;
            const int gpuInfoGap = 24;

            auto drawGpuInfo =
                [&](const std::string& label,
                    const std::string& value)
            {
                drawText(
                    hdc,
                    label,
                    725,
                    gpuInfoY,
                    textSecondary,
                    smallFont
                );

                drawText(
                    hdc,
                    shortText(value, 17),
                    815,
                    gpuInfoY,
                    textPrimary,
                    smallFont
                );

                gpuInfoY += gpuInfoGap;
            };

            drawGpuInfo(
                "Name:",
                gpu.name
            );
            drawGpuInfo(
                "Vendor:",
                gpu.vendor
            );
            drawGpuInfo(
                gpu.vendor == "NVIDIA"
                    ? "CUDA cores:"
                    : "Cores:",
                gpu.computeCores
            );
            drawGpuInfo(
                "Driver:",
                gpu.driverVersion
            );
            drawGpuInfo(
                "DirectX:",
                gpu.directXVersion
            );
            drawGpuInfo(
                "PCIe:",
                gpu.busInterface
            );

            std::string powerText = "--";
            if (gpu.powerW >= 0.0)
            {
                std::ostringstream power;
                power
                    << std::fixed
                    << std::setprecision(0)
                    << gpu.powerW
                    << " W";

                if (gpu.powerLimitW > 0.0)
                {
                    power
                        << " / "
                        << std::setprecision(0)
                        << gpu.powerLimitW
                        << " W";
                }

                powerText = power.str();
            }

            drawGpuInfo(
                "Power:",
                powerText
            );

            // Dedicated memory graph.
            double dedicatedTotalGB =
                gpu.dedicatedMemoryTotalBytes /
                bytesPerGpuGB;

            drawRoundedBox(
                hdc,
                35,
                480,
                430,
                610,
                tempCard
            );
            drawText(
                hdc,
                "Dedicated GPU Memory Usage",
                55,
                495,
                textPrimary,
                labelFont
            );
            drawDiskHistoryGraph(
                hdc,
                55,
                525,
                355,
                65,
                gpu.dedicatedMemoryHistory,
                dedicatedTotalGB > 0.0
                    ? dedicatedTotalGB
                    : 1.0,
                gpuPurple,
                RGB(49, 26, 69)
            );

            // Shared memory graph.
            double sharedTotalGB =
                gpu.sharedMemoryTotalBytes /
                bytesPerGpuGB;

            drawRoundedBox(
                hdc,
                445,
                480,
                690,
                610,
                tempCard
            );
            drawText(
                hdc,
                "Shared GPU Memory Usage",
                465,
                495,
                textPrimary,
                labelFont
            );
            drawDiskHistoryGraph(
                hdc,
                465,
                525,
                205,
                65,
                gpu.sharedMemoryHistory,
                sharedTotalGB > 0.0
                    ? sharedTotalGB
                    : 1.0,
                gpuPurple,
                RGB(49, 26, 69)
            );

            // GPU load footer.
            drawRoundedBox(
                hdc,
                35,
                625,
                915,
                680,
                tempCard
            );

            drawText(
                hdc,
                "GPU Load",
                55,
                638,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                "3D " +
                    percentText(
                        gpu.utilizationPercent
                    ),
                185,
                642,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "Encode " +
                    percentText(
                        gpu.encodePercent
                    ),
                300,
                642,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "Decode " +
                    percentText(
                        gpu.decodePercent
                    ),
                435,
                642,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "Dedicated " +
                    gpuMemoryText(
                        gpu.dedicatedMemoryUsedBytes,
                        gpu.dedicatedMemoryTotalBytes
                    ),
                570,
                642,
                textSecondary,
                smallFont
            );
        }
    }


    // --------------------------------------------------------
    // MOTHERBOARD / SYSTEM TEMPERATURE VIEW
    // --------------------------------------------------------
    else
    {
        drawText(
            hdc,
            "Motherboard / System",
            35,
            185,
            textPrimary,
            titleFont
        );

        drawText(
            hdc,
            "Firmware thermal zones and motherboard-level information exposed by Windows.",
            37,
            216,
            textSecondary,
            smallFont
        );

        auto drawBoardSummary =
            [&](int left,
                int right,
                const std::string& label,
                const std::string& value)
        {
            drawRoundedBox(
                hdc,
                left,
                235,
                right,
                300,
                tempCard
            );

            drawText(
                hdc,
                label,
                left + 18,
                247,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                shortText(value, 25),
                left + 18,
                270,
                textPrimary,
                labelFont
            );
        };

        drawBoardSummary(
            35,
            240,
            "Hottest board sensor",
            formatTemperature(
                temperatureStats.
                    motherboardTemperatureC
            )
        );

        drawBoardSummary(
            250,
            455,
            "Detected sensors",
            std::to_string(
                temperatureStats.motherboardSensors.size() +
                temperatureStats.sensors.size()
            )
        );

        drawBoardSummary(
            465,
            670,
            "Motherboard",
            systemInfo.motherboardModel
        );

        drawBoardSummary(
            680,
            915,
            "BIOS",
            systemInfo.biosVersion
        );

        drawRoundedBox(
            hdc,
            35,
            315,
            650,
            525,
            tempCard
        );

        drawText(
            hdc,
            "System Thermal History",
            55,
            330,
            textPrimary,
            labelFont
        );

        drawDiskHistoryGraph(
            hdc,
            75,
            365,
            550,
            125,
            temperatureStats.
                motherboardTemperatureHistory,
            100.0,
            boardOrange,
            RGB(65, 45, 24)
        );

        drawText(
            hdc,
            "100 C",
            575,
            345,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "60 seconds ago",
            75,
            495,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "Now",
            600,
            495,
            textSecondary,
            smallFont
        );

        if (
            temperatureStats.motherboardSensors.empty() &&
            !temperatureStats.acpiAvailable
        )
        {
            drawText(
                hdc,
                "No motherboard or ACPI temperature sensors were exposed on this PC.",
                135,
                420,
                textSecondary,
                smallFont
            );
        }

        drawRoundedBox(
            hdc,
            665,
            315,
            915,
            525,
            tempCard
        );

        drawText(
            hdc,
            "Board Sensors",
            685,
            330,
            textPrimary,
            labelFont
        );

        int drawnSensorCount = 0;

        for (
            size_t index = 0;
            index < temperatureStats.motherboardSensors.size() &&
            drawnSensorCount < 6;
            index++
        )
        {
            const ThermalSensorInfo& sensor =
                temperatureStats.motherboardSensors[index];

            int sensorY =
                365 + drawnSensorCount * 25;

            drawText(
                hdc,
                shortText(sensor.name, 18),
                685,
                sensorY,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                formatTemperature(sensor.temperatureC),
                835,
                sensorY,
                boardOrange,
                smallFont
            );

            drawnSensorCount++;
        }

        for (
            size_t index = 0;
            index < temperatureStats.sensors.size() &&
            drawnSensorCount < 6;
            index++
        )
        {
            const ThermalSensorInfo& sensor =
                temperatureStats.sensors[index];

            int sensorY =
                365 + drawnSensorCount * 25;

            drawText(
                hdc,
                shortText(sensor.name, 18),
                685,
                sensorY,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                formatTemperature(sensor.temperatureC),
                835,
                sensorY,
                RGB(185, 150, 85),
                smallFont
            );

            drawnSensorCount++;
        }

        if (drawnSensorCount == 0)
        {
            drawText(
                hdc,
                "No board temperature sensors",
                685,
                375,
                textSecondary,
                smallFont
            );
        }

        drawRoundedBox(
            hdc,
            35,
            540,
            915,
            680,
            tempCard
        );

        drawText(
            hdc,
            "System / Motherboard Information",
            55,
            555,
            textPrimary,
            labelFont
        );

        drawText(
            hdc,
            "Manufacturer:",
            55,
            590,
            textSecondary,
            smallFont
        );
        drawText(
            hdc,
            shortText(
                systemInfo.motherboardManufacturer,
                28
            ),
            160,
            590,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            "Model:",
            55,
            615,
            textSecondary,
            smallFont
        );
        drawText(
            hdc,
            shortText(
                systemInfo.motherboardModel,
                28
            ),
            160,
            615,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            "BIOS:",
            55,
            640,
            textSecondary,
            smallFont
        );
        drawText(
            hdc,
            shortText(
                systemInfo.biosVendor +
                    " " +
                    systemInfo.biosVersion,
                30
            ),
            160,
            640,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            "Windows exposes firmware thermal zones, not every board sensor.",
            480,
            590,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "Unavailable sensors remain -- instead of using estimated values.",
            480,
            620,
            textSecondary,
            smallFont
        );

        if (temperatureStats.motherboardSensorGeneric)
        {
            drawText(
                hdc,
                "The displayed reading is a generic ACPI zone, not a named board sensor.",
                480,
                650,
                textSecondary,
                smallFont
            );
        }
    }
}


else if (currentPage == AppPage::Performance)
{
    // --------------------------------------------------------
    // PERFORMANCE PAGE
    // Fixed layout: no page scrolling.
    // The left resource cards stay visible, while the selected
    // resource is shown in the large panel on the right.
    // --------------------------------------------------------

    const COLORREF performanceCard =
        RGB(24, 27, 34);

    const COLORREF performanceTrack =
        RGB(45, 48, 58);

    const COLORREF memoryPurple =
        RGB(140, 80, 220);

    const COLORREF diskGreen =
        RGB(70, 200, 90);

    const COLORREF gpuPurple =
        RGB(155, 85, 220);

    const COLORREF networkOrange =
        RGB(220, 140, 55);


    auto shortenPerformanceText =
        [](const std::string& value,
           size_t maximum)
        -> std::string
    {
        if (value.size() <= maximum)
        {
            return value;
        }

        if (maximum <= 3)
        {
            return value.substr(0, maximum);
        }

        return
            value.substr(0, maximum - 3) +
            "...";
    };


    // --------------------------------------------------------
    // COMMON CPU DATA
    // --------------------------------------------------------

    char cpuModel[256] = "Processor";

    DWORD cpuModelSize =
        sizeof(cpuModel);

    RegGetValueA(
        HKEY_LOCAL_MACHINE,
        "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        "ProcessorNameString",
        RRF_RT_REG_SZ,
        nullptr,
        cpuModel,
        &cpuModelSize
    );

    std::string cpuModelText =
        cpuModel;

    size_t cpuModelFirstCharacter =
        cpuModelText.find_first_not_of(
            " \t"
        );

    if (
        cpuModelFirstCharacter !=
        std::string::npos
    )
    {
        cpuModelText.erase(
            0,
            cpuModelFirstCharacter
        );
    }


    DWORD cpuMHz = 0;
    DWORD cpuMHzSize =
        sizeof(cpuMHz);

    RegGetValueA(
        HKEY_LOCAL_MACHINE,
        "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        "~MHz",
        RRF_RT_REG_DWORD,
        nullptr,
        &cpuMHz,
        &cpuMHzSize
    );

    std::ostringstream baseSpeedText;

    if (cpuMHz > 0)
    {
        baseSpeedText
            << std::fixed
            << std::setprecision(2)
            << (
                cpuMHz /
                1000.0
            )
            << " GHz";
    }
    else
    {
        baseSpeedText << "--";
    }


    std::vector<ProcessInfo> performanceProcesses =
        getRunningProcesses();

    unsigned long long performanceThreadCount = 0;
    unsigned long long performanceHandleCount = 0;

    for (
        const ProcessInfo& process :
        performanceProcesses
    )
    {
        performanceThreadCount +=
            process.threadCount;

        performanceHandleCount +=
            process.handleCount;
    }


    auto formatCount =
        [](unsigned long long value)
        -> std::string
    {
        std::string result =
            std::to_string(value);

        int insertPosition =
            static_cast<int>(
                result.length()
            ) - 3;

        while (insertPosition > 0)
        {
            result.insert(
                static_cast<size_t>(
                    insertPosition
                ),
                ","
            );

            insertPosition -= 3;
        }

        return result;
    };


    ULONGLONG cpuUpDays =
        uptimeSeconds / 86400;

    ULONGLONG cpuUpHours =
        (
            uptimeSeconds %
            86400
        ) / 3600;

    ULONGLONG cpuUpMinutes =
        (
            uptimeSeconds %
            3600
        ) / 60;

    ULONGLONG cpuUpSeconds =
        uptimeSeconds % 60;

    std::ostringstream cpuUpTimeText;

    cpuUpTimeText
        << cpuUpDays
        << ":"
        << std::setfill('0')
        << std::setw(2)
        << cpuUpHours
        << ":"
        << std::setw(2)
        << cpuUpMinutes
        << ":"
        << std::setw(2)
        << cpuUpSeconds;


    auto countCpuRelationship =
        [](LOGICAL_PROCESSOR_RELATIONSHIP relationship)
        -> DWORD
    {
        DWORD bufferSize = 0;

        GetLogicalProcessorInformationEx(
            relationship,
            nullptr,
            &bufferSize
        );

        if (bufferSize == 0)
        {
            return 0;
        }

        std::vector<BYTE> buffer(
            bufferSize
        );

        auto* info =
            reinterpret_cast<
                PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX
            >(
                buffer.data()
            );

        if (
            !GetLogicalProcessorInformationEx(
                relationship,
                info,
                &bufferSize
            )
        )
        {
            return 0;
        }

        DWORD count = 0;

        BYTE* current =
            buffer.data();

        BYTE* end =
            buffer.data() +
            bufferSize;

        while (current < end)
        {
            auto* currentInfo =
                reinterpret_cast<
                    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX
                >(
                    current
                );

            count++;

            current +=
                currentInfo->Size;
        }

        return count;
    };


    DWORD socketCount =
        countCpuRelationship(
            RelationProcessorPackage
        );

    DWORD coreCount =
        countCpuRelationship(
            RelationProcessorCore
        );

    DWORD logicalProcessorCount =
        GetActiveProcessorCount(
            ALL_PROCESSOR_GROUPS
        );

    bool virtualizationEnabled =
        IsProcessorFeaturePresent(
            PF_VIRT_FIRMWARE_ENABLED
        ) != FALSE;


    unsigned long long l1CacheBytes = 0;
    unsigned long long l2CacheBytes = 0;
    unsigned long long l3CacheBytes = 0;

    DWORD cacheBufferSize = 0;

    GetLogicalProcessorInformationEx(
        RelationCache,
        nullptr,
        &cacheBufferSize
    );

    if (cacheBufferSize > 0)
    {
        std::vector<BYTE> cacheBuffer(
            cacheBufferSize
        );

        auto* cacheInfo =
            reinterpret_cast<
                PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX
            >(
                cacheBuffer.data()
            );

        if (
            GetLogicalProcessorInformationEx(
                RelationCache,
                cacheInfo,
                &cacheBufferSize
            )
        )
        {
            BYTE* current =
                cacheBuffer.data();

            BYTE* end =
                cacheBuffer.data() +
                cacheBufferSize;

            while (current < end)
            {
                auto* currentInfo =
                    reinterpret_cast<
                        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX
                    >(
                        current
                    );

                DWORD cacheSize =
                    currentInfo->Cache.CacheSize;

                BYTE cacheLevel =
                    currentInfo->Cache.Level;

                if (cacheLevel == 1)
                {
                    l1CacheBytes += cacheSize;
                }
                else if (cacheLevel == 2)
                {
                    l2CacheBytes += cacheSize;
                }
                else if (cacheLevel == 3)
                {
                    l3CacheBytes += cacheSize;
                }

                current +=
                    currentInfo->Size;
            }
        }
    }


    auto formatCacheSize =
        [](unsigned long long bytes)
        -> std::string
    {
        if (bytes == 0)
        {
            return "--";
        }

        std::ostringstream stream;

        if (
            bytes >=
            1024ULL * 1024ULL
        )
        {
            stream
                << std::fixed
                << std::setprecision(1)
                << (
                    bytes /
                    (
                        1024.0 *
                        1024.0
                    )
                )
                << " MB";
        }
        else
        {
            stream
                << (
                    bytes /
                    1024ULL
                )
                << " KB";
        }

        return stream.str();
    };


    std::string l1CacheText =
        formatCacheSize(
            l1CacheBytes
        );

    std::string l2CacheText =
        formatCacheSize(
            l2CacheBytes
        );

    std::string l3CacheText =
        formatCacheSize(
            l3CacheBytes
        );


    // --------------------------------------------------------
    // LEFT RESOURCE CARDS
    // Keep these coordinates compatible with Main.cpp clicks.
    // --------------------------------------------------------

    auto drawResourceCard =
        [&](int top,
            int bottom,
            COLORREF accent,
            bool selected)
    {
        if (selected)
        {
            drawRoundedBox(
                hdc,
                55,
                top,
                235,
                bottom,
                accent
            );

            drawRoundedBox(
                hdc,
                57,
                top + 2,
                233,
                bottom - 2,
                performanceCard
            );
        }
        else
        {
            drawRoundedBox(
                hdc,
                55,
                top,
                235,
                bottom,
                performanceCard
            );
        }
    };


    // CPU
    drawResourceCard(
        135,
        205,
        RGB(66, 135, 245),
        performanceView ==
            PerformanceView::CPU
    );

    drawCpuGraph(
        hdc,
        65,
        145,
        58,
        48
    );

    drawText(
        hdc,
        "CPU",
        135,
        145,
        textPrimary,
        labelFont
    );

    std::ostringstream cpuSideText;

    cpuSideText
        << std::fixed
        << std::setprecision(0)
        << cpuUsage
        << "%";

    drawText(
        hdc,
        cpuSideText.str(),
        135,
        172,
        textSecondary,
        smallFont
    );


    // Memory
    drawResourceCard(
        225,
        295,
        memoryPurple,
        performanceView ==
            PerformanceView::Memory
    );

    drawRamGraph(
        hdc,
        65,
        235,
        58,
        48
    );

    drawText(
        hdc,
        "Memory",
        135,
        235,
        textPrimary,
        labelFont
    );

    std::ostringstream memorySideText;

    memorySideText
        << std::fixed
        << std::setprecision(1)
        << usedRamGB
        << " / "
        << totalRamGB
        << " GB";

    drawText(
        hdc,
        memorySideText.str(),
        135,
        262,
        textSecondary,
        smallFont
    );


    // Physical disks
    int visibleDiskCards =
        performanceVisibleDiskCardCount(
            diskStats.size()
        );

    if (diskStats.empty())
    {
        int top =
            performanceDiskCardTop(0);

        drawResourceCard(
            top,
            top + performanceDiskCardHeight,
            diskGreen,
            performanceView ==
                PerformanceView::Disk
        );

        drawRoundedBox(
            hdc,
            65,
            top + 10,
            123,
            top + 58,
            RGB(20, 24, 30)
        );

        drawGraphGrid(
            hdc,
            65,
            top + 10,
            58,
            48
        );

        drawText(
            hdc,
            "Disk",
            135,
            top + 10,
            textPrimary,
            labelFont
        );

        drawText(
            hdc,
            "No disk data",
            135,
            top + 38,
            textSecondary,
            smallFont
        );
    }
    else
    {
        int actualDiskCards =
            (std::min)(
                visibleDiskCards,
                static_cast<int>(
                    diskStats.size()
                )
            );

        for (
            int index = 0;
            index < actualDiskCards;
            index++
        )
        {
            const DiskStats& disk =
                diskStats[index];

            int top =
                performanceDiskCardTop(
                    index
                );

            int bottom =
                top +
                performanceDiskCardHeight;

            bool selected =
                performanceView ==
                    PerformanceView::Disk &&
                selectedDiskIndex ==
                    index;

            drawResourceCard(
                top,
                bottom,
                diskGreen,
                selected
            );

            drawDiskHistoryGraph(
                hdc,
                65,
                top + 10,
                58,
                48,
                disk.activeHistory,
                100.0,
                diskGreen,
                RGB(25, 55, 34)
            );

            drawText(
                hdc,
                disk.displayName,
                135,
                top + 8,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                disk.type,
                135,
                top + 31,
                textSecondary,
                smallFont
            );

            std::ostringstream diskSideText;

            if (disk.performanceValid)
            {
                diskSideText
                    << std::fixed
                    << std::setprecision(0)
                    << disk.activeTimePercent
                    << "%";
            }
            else
            {
                diskSideText
                    << "--";
            }

            drawText(
                hdc,
                diskSideText.str(),
                135,
                top + 49,
                textSecondary,
                smallFont
            );
        }
    }


    // GPU adapters - one resource card per detected GPU.
    int gpuCardCount =
        performanceVisibleGpuCardCount(
            gpuStats.size()
        );

    for (int index = 0;
         index < gpuCardCount;
         index++)
    {
        int gpuTop =
            performanceGpuCardTop(
                diskStats.size(),
                index
            );

        bool hasGpu =
            index <
            static_cast<int>(
                gpuStats.size()
            );

        bool selected =
            performanceView ==
                PerformanceView::GPU &&
            selectedGpuIndex == index;

        drawResourceCard(
            gpuTop,
            gpuTop +
                performanceDiskCardHeight,
            gpuPurple,
            selected
        );

        if (hasGpu)
        {
            const GpuStats& gpu =
                gpuStats[index];

            drawDiskHistoryGraph(
                hdc,
                65,
                gpuTop + 10,
                58,
                48,
                gpu.utilizationHistory,
                100.0,
                gpuPurple,
                RGB(48, 28, 70)
            );

            std::string gpuLabel =
                gpuStats.size() > 1
                ? "GPU " +
                    std::to_string(index)
                : "GPU";

            drawText(
                hdc,
                gpuLabel,
                135,
                gpuTop + 7,
                textPrimary,
                labelFont
            );

            std::ostringstream gpuUsageText;
            gpuUsageText
                << std::fixed
                << std::setprecision(0)
                << gpu.utilizationPercent
                << "%";

            drawText(
                hdc,
                gpuUsageText.str(),
                135,
                gpuTop + 31,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                shortenPerformanceText(
                    gpu.name,
                    19
                ),
                135,
                gpuTop + 49,
                textSecondary,
                smallFont
            );
        }
        else
        {
            drawRoundedBox(
                hdc,
                65,
                gpuTop + 10,
                123,
                gpuTop + 58,
                RGB(20, 24, 30)
            );

            drawGraphGrid(
                hdc,
                65,
                gpuTop + 10,
                58,
                48
            );

            drawText(
                hdc,
                "--",
                86,
                gpuTop + 25,
                gpuPurple,
                labelFont
            );

            drawText(
                hdc,
                "GPU",
                135,
                gpuTop + 10,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                "No GPU detected",
                135,
                gpuTop + 37,
                textSecondary,
                smallFont
            );
        }
    }


    // Network
    int networkTop =
        performanceNetworkCardTop(
            diskStats.size(),
            gpuStats.size()
        );

    drawResourceCard(
        networkTop,
        networkTop +
            performanceDiskCardHeight,
        networkOrange,
        performanceView ==
            PerformanceView::Network
    );

    const NetworkStats* sidebarNetwork =
        nullptr;

    if (!networkStats.empty())
    {
        int sidebarIndex =
            std::clamp(
                selectedNetworkIndex,
                0,
                static_cast<int>(
                    networkStats.size()
                ) - 1
            );

        sidebarNetwork =
            &networkStats[sidebarIndex];
    }

    if (sidebarNetwork != nullptr)
    {
        double miniScale =
            getNetworkGraphScale(
                *sidebarNetwork
            );

        drawNetworkHistoryGraph(
            hdc,
            65,
            networkTop + 10,
            58,
            48,
            *sidebarNetwork,
            miniScale,
            networkOrange,
            RGB(45, 145, 245)
        );

        drawText(
            hdc,
            "Network",
            135,
            networkTop + 7,
            textPrimary,
            labelFont
        );

        drawText(
            hdc,
            "R: " +
                formatNetworkSpeed(
                    sidebarNetwork->downloadMbps
                ),
            135,
            networkTop + 30,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "S: " +
                formatNetworkSpeed(
                    sidebarNetwork->uploadMbps
                ),
            135,
            networkTop + 48,
            textSecondary,
            smallFont
        );
    }
    else
    {
        drawRoundedBox(
            hdc,
            65,
            networkTop + 10,
            123,
            networkTop + 58,
            RGB(20, 24, 30)
        );

        drawGraphGrid(
            hdc,
            65,
            networkTop + 10,
            58,
            48
        );

        drawText(
            hdc,
            "--",
            86,
            networkTop + 25,
            networkOrange,
            labelFont
        );

        drawText(
            hdc,
            "Network",
            135,
            networkTop + 10,
            textPrimary,
            labelFont
        );

        drawText(
            hdc,
            "No active adapter",
            135,
            networkTop + 37,
            textSecondary,
            smallFont
        );
    }

    // --------------------------------------------------------
    // CPU VIEW
    // --------------------------------------------------------

    if (
        performanceView ==
        PerformanceView::CPU
    )
    {
        // ----------------------------------------------------
        // MAIN CPU CARD
        // ----------------------------------------------------

        drawRoundedBox(
            hdc,
            255,
            115,
            915,
            430,
            performanceCard
        );


        drawText(
            hdc,
            "CPU",
            280,
            130,
            textPrimary,
            titleFont
        );


        // CPU model, right aligned.
        SIZE cpuModelExtent = {};

        setFont(
            hdc,
            smallFont
        );

        GetTextExtentPoint32A(
            hdc,
            cpuModelText.c_str(),
            static_cast<int>(
                cpuModelText.length()
            ),
            &cpuModelExtent
        );

        int cpuModelX =
            895 -
            cpuModelExtent.cx;

        if (cpuModelX < 545)
        {
            cpuModelX = 545;
        }

        drawText(
            hdc,
            cpuModelText,
            cpuModelX,
            136,
            textSecondary,
            smallFont
        );


        drawText(
            hdc,
            "% Utilization",
            280,
            158,
            textSecondary,
            smallFont
        );


        // Y-axis labels beside the graph.
        drawText(
            hdc,
            "100%",
            260,
            176,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "50%",
            266,
            228,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "0%",
            272,
            280,
            textSecondary,
            smallFont
        );


        drawCpuGraph(
            hdc,
            300,
            175,
            590,
            110
        );

        drawText(
            hdc,
            "60 seconds",
            830,
            289,
            textSecondary,
            smallFont
        );


        // ----------------------------------------------------
        // CPU LEFT DETAILS
        // ----------------------------------------------------

        std::ostringstream utilizationText;

        utilizationText
            << std::fixed
            << std::setprecision(0)
            << cpuUsage
            << "%";


        drawText(
            hdc,
            "Utilization",
            280,
            310,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            utilizationText.str(),
            280,
            328,
            textPrimary,
            labelFont
        );


        drawText(
            hdc,
            "Speed",
            390,
            310,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            baseSpeedText.str(),
            390,
            328,
            textPrimary,
            labelFont
        );


        drawText(
            hdc,
            "Processes",
            280,
            353,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            formatCount(
                performanceProcesses.size()
            ),
            280,
            371,
            textPrimary,
            labelFont
        );


        drawText(
            hdc,
            "Threads",
            390,
            353,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            formatCount(
                performanceThreadCount
            ),
            390,
            371,
            textPrimary,
            labelFont
        );


        drawText(
            hdc,
            "Handles",
            500,
            353,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            formatCount(
                performanceHandleCount
            ),
            500,
            371,
            textPrimary,
            labelFont
        );


        drawText(
            hdc,
            "Up time",
            280,
            397,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            cpuUpTimeText.str(),
            280,
            413,
            textPrimary,
            smallFont
        );


        // ----------------------------------------------------
        // CPU RIGHT DETAILS
        // ----------------------------------------------------

        const int cpuDetailLabelX = 600;
        const int cpuDetailValueX = 765;
        const int cpuDetailStartY = 310;
        const int cpuDetailSpacing = 15;


        drawText(
            hdc,
            "Base speed:",
            cpuDetailLabelX,
            cpuDetailStartY,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            baseSpeedText.str(),
            cpuDetailValueX,
            cpuDetailStartY,
            textPrimary,
            smallFont
        );


        drawText(
            hdc,
            "Sockets:",
            cpuDetailLabelX,
            cpuDetailStartY +
                cpuDetailSpacing,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            std::to_string(
                socketCount
            ),
            cpuDetailValueX,
            cpuDetailStartY +
                cpuDetailSpacing,
            textPrimary,
            smallFont
        );


        drawText(
            hdc,
            "Cores:",
            cpuDetailLabelX,
            cpuDetailStartY +
                cpuDetailSpacing * 2,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            std::to_string(
                coreCount
            ),
            cpuDetailValueX,
            cpuDetailStartY +
                cpuDetailSpacing * 2,
            textPrimary,
            smallFont
        );


        drawText(
            hdc,
            "Logical processors:",
            cpuDetailLabelX,
            cpuDetailStartY +
                cpuDetailSpacing * 3,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            std::to_string(
                logicalProcessorCount
            ),
            cpuDetailValueX,
            cpuDetailStartY +
                cpuDetailSpacing * 3,
            textPrimary,
            smallFont
        );


        drawText(
            hdc,
            "Virtualization:",
            cpuDetailLabelX,
            cpuDetailStartY +
                cpuDetailSpacing * 4,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            virtualizationEnabled
                ? "Enabled"
                : "Disabled",
            cpuDetailValueX,
            cpuDetailStartY +
                cpuDetailSpacing * 4,
            textPrimary,
            smallFont
        );


        drawText(
            hdc,
            "L1 cache:",
            cpuDetailLabelX,
            cpuDetailStartY +
                cpuDetailSpacing * 5,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            l1CacheText,
            cpuDetailValueX,
            cpuDetailStartY +
                cpuDetailSpacing * 5,
            textPrimary,
            smallFont
        );


        drawText(
            hdc,
            "L2 cache:",
            cpuDetailLabelX,
            cpuDetailStartY +
                cpuDetailSpacing * 6,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            l2CacheText,
            cpuDetailValueX,
            cpuDetailStartY +
                cpuDetailSpacing * 6,
            textPrimary,
            smallFont
        );


        drawText(
            hdc,
            "L3 cache:",
            cpuDetailLabelX,
            cpuDetailStartY +
                cpuDetailSpacing * 7,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            l3CacheText,
            cpuDetailValueX,
            cpuDetailStartY +
                cpuDetailSpacing * 7,
            textPrimary,
            smallFont
        );


        // ----------------------------------------------------
        // MEMORY SUMMARY CARD
        // ----------------------------------------------------

        const int summaryTop = 440;
        const int summaryBottom = 680;

        const int memoryLeft = 255;
        const int memoryRight = 465;

        drawRoundedBox(
            hdc,
            memoryLeft,
            summaryTop,
            memoryRight,
            summaryBottom,
            performanceCard
        );

        drawText(
            hdc,
            "Memory",
            270,
            455,
            textPrimary,
            labelFont
        );

        std::ostringstream memoryTotalTopText;

        memoryTotalTopText
            << std::fixed
            << std::setprecision(1)
            << totalRamGB
            << " GB";

        SIZE memoryTotalExtent = {};

        setFont(
            hdc,
            smallFont
        );

        GetTextExtentPoint32A(
            hdc,
            memoryTotalTopText.str().c_str(),
            static_cast<int>(
                memoryTotalTopText.str().length()
            ),
            &memoryTotalExtent
        );

        drawText(
            hdc,
            memoryTotalTopText.str(),
            memoryRight -
                15 -
                memoryTotalExtent.cx,
            457,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "Memory usage",
            270,
            480,
            textSecondary,
            smallFont
        );

        drawRamGraph(
            hdc,
            270,
            497,
            180,
            55
        );

        drawText(
            hdc,
            "60 seconds",
            390,
            555,
            textSecondary,
            smallFont
        );

        drawRoundedBox(
            hdc,
            270,
            570,
            450,
            580,
            performanceTrack
        );

        int memoryFillWidth =
            static_cast<int>(
                180.0 *
                (
                    ramPercent /
                    100.0
                )
            );

        memoryFillWidth =
            std::clamp(
                memoryFillWidth,
                0,
                180
            );

        if (memoryFillWidth > 0)
        {
            drawRoundedBox(
                hdc,
                270,
                570,
                270 + memoryFillWidth,
                580,
                memoryPurple
            );
        }

        std::ostringstream memoryMainText;

        memoryMainText
            << std::fixed
            << std::setprecision(1)
            << usedRamGB
            << " GB ("
            << std::setprecision(0)
            << ramPercent
            << "%)";

        drawText(
            hdc,
            memoryMainText.str(),
            270,
            590,
            textPrimary,
            labelFont
        );

        double availableRamGB =
            totalRamGB -
            usedRamGB;

        drawText(
            hdc,
            "In use",
            270,
            620,
            textSecondary,
            smallFont
        );

        std::ostringstream memoryUsedText;

        memoryUsedText
            << std::fixed
            << std::setprecision(1)
            << usedRamGB
            << " GB";

        drawText(
            hdc,
            memoryUsedText.str(),
            270,
            637,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            "Available",
            365,
            620,
            textSecondary,
            smallFont
        );

        std::ostringstream memoryAvailableText;

        memoryAvailableText
            << std::fixed
            << std::setprecision(1)
            << availableRamGB
            << " GB";

        drawText(
            hdc,
            memoryAvailableText.str(),
            365,
            637,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            "Total",
            270,
            648,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            memoryTotalTopText.str(),
            270,
            665,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            "Usage",
            365,
            648,
            textSecondary,
            smallFont
        );

        std::ostringstream memoryUsageBottomText;

        memoryUsageBottomText
            << std::fixed
            << std::setprecision(0)
            << ramPercent
            << "%";

        drawText(
            hdc,
            memoryUsageBottomText.str(),
            365,
            665,
            textPrimary,
            smallFont
        );


        // ----------------------------------------------------
        // DISK SUMMARY CARD
        // Graph is styled like the CPU graph, but it shows the
        // current STORAGE USED percentage because active-time
        // history is not collected by the current backend.
        // ----------------------------------------------------

        const int diskLeft = 475;
        const int diskRight = 685;

        drawRoundedBox(
            hdc,
            diskLeft,
            summaryTop,
            diskRight,
            summaryBottom,
            performanceCard
        );

        drawText(
            hdc,
            "Disk 0 (C:)",
            490,
            455,
            textPrimary,
            labelFont
        );

        std::ostringstream diskCapacityTopText;

        diskCapacityTopText
            << std::fixed
            << std::setprecision(0)
            << totalDiskGB
            << " GB";

        SIZE diskCapacityExtent = {};

        setFont(
            hdc,
            smallFont
        );

        GetTextExtentPoint32A(
            hdc,
            diskCapacityTopText.str().c_str(),
            static_cast<int>(
                diskCapacityTopText.str().length()
            ),
            &diskCapacityExtent
        );

        drawText(
            hdc,
            diskCapacityTopText.str(),
            diskRight -
                15 -
                diskCapacityExtent.cx,
            457,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "Storage used",
            490,
            480,
            textSecondary,
            smallFont
        );

        drawCurrentPercentGraph(
            hdc,
            490,
            497,
            180,
            55,
            diskPercent,
            diskGreen,
            RGB(25, 55, 34)
        );

        drawText(
            hdc,
            "Current",
            625,
            555,
            textSecondary,
            smallFont
        );

        std::ostringstream diskPercentText;

        diskPercentText
            << diskPercent
            << "%";

        drawText(
            hdc,
            diskPercentText.str(),
            490,
            575,
            textPrimary,
            labelFont
        );

        double freeDiskGB =
            totalDiskGB -
            usedDiskGB;

        drawText(
            hdc,
            "Used",
            490,
            610,
            textSecondary,
            smallFont
        );

        std::ostringstream diskUsedText;

        diskUsedText
            << std::fixed
            << std::setprecision(1)
            << usedDiskGB
            << " GB";

        drawText(
            hdc,
            diskUsedText.str(),
            490,
            627,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            "Free",
            590,
            610,
            textSecondary,
            smallFont
        );

        std::ostringstream diskFreeText;

        diskFreeText
            << std::fixed
            << std::setprecision(1)
            << freeDiskGB
            << " GB";

        drawText(
            hdc,
            diskFreeText.str(),
            590,
            627,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            "Capacity",
            490,
            648,
            textSecondary,
            smallFont
        );

        std::ostringstream diskTotalText;

        diskTotalText
            << std::fixed
            << std::setprecision(1)
            << totalDiskGB
            << " GB";

        drawText(
            hdc,
            diskTotalText.str(),
            490,
            665,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            "Drive",
            590,
            648,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "C:\\",
            590,
            665,
            textPrimary,
            smallFont
        );


        // ----------------------------------------------------
        // GPU SUMMARY CARD
        // ----------------------------------------------------

        const int gpuLeft = 695;
        const int gpuRight = 915;

        drawRoundedBox(
            hdc,
            gpuLeft,
            summaryTop,
            gpuRight,
            summaryBottom,
            performanceCard
        );

        drawText(
            hdc,
            "GPU",
            710,
            455,
            textPrimary,
            labelFont
        );

        drawText(
            hdc,
            "PLANNED",
            845,
            457,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "3D Utilization",
            710,
            480,
            textSecondary,
            smallFont
        );

        drawRoundedBox(
            hdc,
            710,
            497,
            900,
            552,
            RGB(20, 24, 30)
        );

        drawGraphGrid(
            hdc,
            710,
            497,
            190,
            55
        );

        drawText(
            hdc,
            "--",
            795,
            515,
            gpuPurple,
            labelFont
        );

        drawText(
            hdc,
            "--",
            710,
            575,
            textPrimary,
            labelFont
        );

        drawText(
            hdc,
            "Dedicated GPU memory",
            710,
            610,
            textSecondary,
            smallFont
        );

        drawRoundedBox(
            hdc,
            710,
            628,
            805,
            638,
            performanceTrack
        );

        drawText(
            hdc,
            "--",
            815,
            626,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            "Shared GPU memory",
            710,
            648,
            textSecondary,
            smallFont
        );

        drawRoundedBox(
            hdc,
            710,
            664,
            805,
            674,
            performanceTrack
        );

        drawText(
            hdc,
            "--",
            815,
            662,
            textPrimary,
            smallFont
        );
    }


    // --------------------------------------------------------
    // MEMORY VIEW
    // --------------------------------------------------------
    else if (
        performanceView ==
        PerformanceView::Memory
    )
    {
        drawRoundedBox(
            hdc,
            255,
            135,
            890,
            680,
            performanceCard
        );

        const MemoryPerformanceDetails
            memoryPerformance =
                getMemoryPerformanceDetails();

        const MemoryHardwareDetails&
            memoryHardware =
                getMemoryHardwareDetails();

        double availableRamGB =
            totalRamGB - usedRamGB;

        double usedPercent =
            totalRamGB > 0.0
                ? (
                    usedRamGB /
                    totalRamGB
                  ) * 100.0
                : 0.0;

        // ----------------------------------------------------
        // TOP
        // ----------------------------------------------------

        drawText(
            hdc,
            "Memory",
            280,
            155,
            textPrimary,
            titleFont
        );

        std::ostringstream memoryTotalTop;

        memoryTotalTop
            << std::fixed
            << std::setprecision(1)
            << totalRamGB
            << " GB";

        drawText(
            hdc,
            memoryTotalTop.str(),
            820,
            155,
            textPrimary,
            labelFont
        );

        drawText(
            hdc,
            "Memory usage",
            280,
            195,
            textSecondary,
            smallFont
        );

        // ----------------------------------------------------
        // LARGE MEMORY GRAPH
        // ----------------------------------------------------

        drawRamGraph(
            hdc,
            300,
            225,
            565,
            205
        );

        std::ostringstream memoryGraphTop;
        memoryGraphTop
            << std::fixed
            << std::setprecision(1)
            << totalRamGB
            << " GB";

        drawText(
            hdc,
            memoryGraphTop.str(),
            255,
            220,
            textSecondary,
            smallFont
        );

        std::ostringstream memoryGraph75;
        memoryGraph75
            << std::fixed
            << std::setprecision(1)
            << (totalRamGB * 0.75)
            << " GB";

        drawText(
            hdc,
            memoryGraph75.str(),
            255,
            270,
            textSecondary,
            smallFont
        );

        std::ostringstream memoryGraph50;
        memoryGraph50
            << std::fixed
            << std::setprecision(1)
            << (totalRamGB * 0.50)
            << " GB";

        drawText(
            hdc,
            memoryGraph50.str(),
            255,
            320,
            textSecondary,
            smallFont
        );

        std::ostringstream memoryGraph25;
        memoryGraph25
            << std::fixed
            << std::setprecision(1)
            << (totalRamGB * 0.25)
            << " GB";

        drawText(
            hdc,
            memoryGraph25.str(),
            255,
            370,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "0 GB",
            265,
            416,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "60 seconds",
            300,
            437,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "0",
            858,
            437,
            textSecondary,
            smallFont
        );

        // ----------------------------------------------------
        // MEMORY COMPOSITION
        // ----------------------------------------------------

        drawText(
            hdc,
            "Memory composition",
            280,
            470,
            textSecondary,
            smallFont
        );

        drawRoundedBox(
            hdc,
            280,
            495,
            865,
            530,
            RGB(32, 36, 44)
        );

        int compositionWidth =
            static_cast<int>(
                585.0 *
                (usedPercent / 100.0)
            );

        if (compositionWidth < 0)
            compositionWidth = 0;

        if (compositionWidth > 585)
            compositionWidth = 585;

        HBRUSH compositionBrush =
            CreateSolidBrush(
                RGB(140, 80, 220)
            );

        RECT usedRect =
        {
            282,
            497,
            282 + compositionWidth,
            528
        };

        FillRect(
            hdc,
            &usedRect,
            compositionBrush
        );

        DeleteObject(
            compositionBrush
        );

        HPEN dividerPen =
            CreatePen(
                PS_SOLID,
                1,
                RGB(80, 84, 96)
            );

        HGDIOBJ oldDividerPen =
            SelectObject(
                hdc,
                dividerPen
            );

        MoveToEx(
            hdc,
            282 + compositionWidth,
            497,
            nullptr
        );

        LineTo(
            hdc,
            282 + compositionWidth,
            528
        );

        SelectObject(
            hdc,
            oldDividerPen
        );

        DeleteObject(
            dividerPen
        );

        // ----------------------------------------------------
        // TEXT VALUES
        // ----------------------------------------------------

        std::ostringstream memoryInUseText;
        memoryInUseText
            << std::fixed
            << std::setprecision(1)
            << usedRamGB
            << " GB";

        std::ostringstream memoryAvailableText;
        memoryAvailableText
            << std::fixed
            << std::setprecision(1)
            << availableRamGB
            << " GB";

        std::string memoryCommittedText =
            "--";

        std::string memoryCachedText =
            "--";

        std::string pagedPoolText =
            "--";

        std::string nonPagedPoolText =
            "--";

        if (memoryPerformance.valid)
        {
            memoryCommittedText =
                formatMemoryGigabyteNumber(
                    memoryPerformance.
                        commitTotalBytes
                ) +
                " / " +
                formatMemoryGigabyteNumber(
                    memoryPerformance.
                        commitLimitBytes
                ) +
                " GB";

            memoryCachedText =
                formatMemoryBytes(
                    memoryPerformance.
                        cachedBytes
                );

            pagedPoolText =
                formatMemoryBytes(
                    memoryPerformance.
                        pagedPoolBytes
                );

            nonPagedPoolText =
                formatMemoryBytes(
                    memoryPerformance.
                        nonPagedPoolBytes
                );
        }

        std::string memorySpeedText =
            "--";

        if (memoryHardware.speedMTs > 0)
        {
            memorySpeedText =
                std::to_string(
                    memoryHardware.speedMTs
                ) +
                " MT/s";
        }

        std::string memorySlotsText =
            "--";

        if (memoryHardware.totalSlots > 0)
        {
            memorySlotsText =
                std::to_string(
                    memoryHardware.usedSlots
                ) +
                " of " +
                std::to_string(
                    memoryHardware.totalSlots
                );
        }

        std::string hardwareReservedText =
            "--";

        if (memoryHardware.
                hardwareReservedValid)
        {
            hardwareReservedText =
                formatMemoryBytes(
                    memoryHardware.
                        hardwareReservedBytes
                );
        }

        // ----------------------------------------------------
        // MEMORY DETAILS - LEFT
        // ----------------------------------------------------

        drawText(
            hdc,
            "In use (Compressed)",
            280,
            550,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            memoryInUseText.str(),
            280,
            570,
            textPrimary,
            labelFont
        );

        drawText(
            hdc,
            "Available",
            440,
            550,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            memoryAvailableText.str(),
            440,
            570,
            textPrimary,
            labelFont
        );

        drawText(
            hdc,
            "Committed",
            280,
            595,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            memoryCommittedText,
            280,
            613,
            textPrimary,
            labelFont
        );

        drawText(
            hdc,
            "Cached",
            440,
            595,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            memoryCachedText,
            440,
            613,
            textPrimary,
            labelFont
        );

        drawText(
            hdc,
            "Paged pool",
            280,
            642,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            pagedPoolText,
            280,
            660,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            "Non-paged pool",
            440,
            642,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            nonPagedPoolText,
            440,
            660,
            textPrimary,
            smallFont
        );

        // ----------------------------------------------------
        // MEMORY HARDWARE DETAILS - RIGHT
        // ----------------------------------------------------

        const int memoryDetailLabelX = 620;
        const int memoryDetailValueX = 790;

        drawText(
            hdc,
            "Speed:",
            memoryDetailLabelX,
            550,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            memorySpeedText,
            memoryDetailValueX,
            550,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            "Slots used:",
            memoryDetailLabelX,
            575,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            memorySlotsText,
            memoryDetailValueX,
            575,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            "Form factor:",
            memoryDetailLabelX,
            600,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            memoryHardware.formFactor,
            memoryDetailValueX,
            600,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            "Hardware reserved:",
            memoryDetailLabelX,
            625,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            hardwareReservedText,
            memoryDetailValueX,
            625,
            textPrimary,
            smallFont
        );
    }

    // --------------------------------------------------------
    // DISK VIEW
    // --------------------------------------------------------
    else if (
        performanceView ==
        PerformanceView::Disk
    )
    {
        drawRoundedBox(
            hdc,
            255,
            115,
            915,
            680,
            performanceCard
        );

        if (diskStats.empty())
        {
            drawText(
                hdc,
                "Disk",
                280,
                135,
                textPrimary,
                titleFont
            );

            drawText(
                hdc,
                "No physical disk information is currently available.",
                280,
                195,
                textSecondary,
                labelFont
            );
        }
        else
        {
            int diskIndex =
                selectedDiskIndex;

            if (
                diskIndex < 0 ||
                diskIndex >=
                    static_cast<int>(
                        diskStats.size()
                    )
            )
            {
                diskIndex = 0;
            }

            const DiskStats& disk =
                diskStats[diskIndex];

            drawText(
                hdc,
                disk.displayName,
                280,
                130,
                textPrimary,
                titleFont
            );

            SIZE modelExtent = {};

            setFont(
                hdc,
                smallFont
            );

            GetTextExtentPoint32A(
                hdc,
                disk.model.c_str(),
                static_cast<int>(
                    disk.model.length()
                ),
                &modelExtent
            );

            int modelX =
                895 -
                modelExtent.cx;

            if (modelX < 565)
            {
                modelX = 565;
            }

            drawText(
                hdc,
                disk.model,
                modelX,
                138,
                textSecondary,
                smallFont
            );


            // ------------------------------------------------
            // ACTIVE TIME GRAPH
            // ------------------------------------------------

            drawText(
                hdc,
                "Active time",
                280,
                168,
                textSecondary,
                smallFont
            );

            drawDiskHistoryGraph(
                hdc,
                300,
                188,
                565,
                125,
                disk.activeHistory,
                100.0,
                diskGreen,
                RGB(25, 55, 34)
            );

            drawText(
                hdc,
                "100%",
                865,
                181,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "0%",
                875,
                302,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "60 seconds",
                300,
                316,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "0",
                858,
                316,
                textSecondary,
                smallFont
            );


            // ------------------------------------------------
            // DISK TRANSFER RATE GRAPH
            // ------------------------------------------------

            const COLORREF transferColor =
                RGB(35, 210, 180);

            const COLORREF transferFill =
                RGB(18, 62, 58);

            double transferScale =
                getDiskTransferGraphScale(
                    disk
                );

            drawText(
                hdc,
                "Disk transfer rate",
                280,
                340,
                textSecondary,
                smallFont
            );

            drawDiskHistoryGraph(
                hdc,
                300,
                360,
                565,
                92,
                disk.transferHistory,
                transferScale,
                transferColor,
                transferFill
            );

            std::ostringstream transferTopText;
            transferTopText
                << std::fixed
                << std::setprecision(
                    transferScale < 10.0
                    ? 1
                    : 0
                )
                << transferScale
                << " MB/s";

            std::ostringstream transferHalfText;
            transferHalfText
                << std::fixed
                << std::setprecision(
                    transferScale < 10.0
                    ? 1
                    : 0
                )
                << (transferScale / 2.0)
                << " MB/s";

            drawText(
                hdc,
                transferTopText.str(),
                820,
                347,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                transferHalfText.str(),
                820,
                392,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "60 seconds",
                300,
                455,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "0",
                858,
                455,
                textSecondary,
                smallFont
            );


            // ------------------------------------------------
            // LIVE VALUES - LEFT
            // ------------------------------------------------

            std::string activeText = "--";
            std::string responseText = "--";
            std::string readText = "--";
            std::string writeText = "--";

            if (disk.performanceValid)
            {
                std::ostringstream activeStream;
                activeStream
                    << std::fixed
                    << std::setprecision(0)
                    << disk.activeTimePercent
                    << "%";
                activeText =
                    activeStream.str();

                std::ostringstream responseStream;
                responseStream
                    << std::fixed
                    << std::setprecision(1)
                    << disk.averageResponseMs
                    << " ms";
                responseText =
                    responseStream.str();

                readText =
                    formatDiskSpeed(
                        disk.readMBps
                    );

                writeText =
                    formatDiskSpeed(
                        disk.writeMBps
                    );
            }

            drawText(
                hdc,
                "Active time",
                280,
                480,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                activeText,
                280,
                500,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                "Average response time",
                410,
                480,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                responseText,
                410,
                500,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                "Read speed",
                280,
                540,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                readText,
                280,
                560,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                "Write speed",
                410,
                540,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                writeText,
                410,
                560,
                textPrimary,
                labelFont
            );


            // ------------------------------------------------
            // DISK DETAILS - RIGHT
            // ------------------------------------------------

            const int diskDetailLabelX = 610;
            const int diskDetailValueX = 760;
            const int diskDetailStartY = 480;
            const int diskDetailSpacing = 28;

            std::string capacityText =
                formatDiskCapacity(
                    disk.capacityGB
                );

            std::string formattedText =
                formatDiskCapacity(
                    disk.formattedGB
                );

            drawText(
                hdc,
                "Capacity:",
                diskDetailLabelX,
                diskDetailStartY,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                capacityText,
                diskDetailValueX,
                diskDetailStartY,
                textPrimary,
                smallFont
            );

            drawText(
                hdc,
                "Formatted:",
                diskDetailLabelX,
                diskDetailStartY +
                    diskDetailSpacing,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                formattedText,
                diskDetailValueX,
                diskDetailStartY +
                    diskDetailSpacing,
                textPrimary,
                smallFont
            );

            drawText(
                hdc,
                "System disk:",
                diskDetailLabelX,
                diskDetailStartY +
                    diskDetailSpacing * 2,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                disk.systemDisk
                    ? "Yes"
                    : "No",
                diskDetailValueX,
                diskDetailStartY +
                    diskDetailSpacing * 2,
                textPrimary,
                smallFont
            );

            drawText(
                hdc,
                "Page file:",
                diskDetailLabelX,
                diskDetailStartY +
                    diskDetailSpacing * 3,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                disk.pageFile
                    ? "Yes"
                    : "No",
                diskDetailValueX,
                diskDetailStartY +
                    diskDetailSpacing * 3,
                textPrimary,
                smallFont
            );

            drawText(
                hdc,
                "Type:",
                diskDetailLabelX,
                diskDetailStartY +
                    diskDetailSpacing * 4,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                disk.type,
                diskDetailValueX,
                diskDetailStartY +
                    diskDetailSpacing * 4,
                textPrimary,
                smallFont
            );
        }
    }


    // --------------------------------------------------------
    // GPU VIEW
    // --------------------------------------------------------
    else if (
        performanceView ==
        PerformanceView::GPU
    )
    {
        drawRoundedBox(
            hdc,
            255,
            115,
            915,
            680,
            performanceCard
        );

        if (gpuStats.empty())
        {
            drawText(
                hdc,
                "GPU",
                280,
                135,
                textPrimary,
                titleFont
            );

            drawText(
                hdc,
                "No compatible Windows GPU adapter was detected.",
                280,
                200,
                textSecondary,
                labelFont
            );
        }
        else
        {
            if (
                selectedGpuIndex < 0 ||
                selectedGpuIndex >=
                    static_cast<int>(
                        gpuStats.size()
                    )
            )
            {
                selectedGpuIndex = 0;
            }

            const GpuStats& gpu =
                gpuStats[selectedGpuIndex];

            const double bytesPerGpuGB =
                1024.0 * 1024.0 * 1024.0;

            auto formatGpuMemory =
                [&](unsigned long long used,
                    unsigned long long total)
                -> std::string
            {
                std::ostringstream stream;

                if (total == 0)
                {
                    return "--";
                }

                stream
                    << std::fixed
                    << std::setprecision(1)
                    << (used / bytesPerGpuGB)
                    << " / "
                    << (total / bytesPerGpuGB)
                    << " GB";

                return stream.str();
            };

            auto percentText =
                [](double value)
                -> std::string
            {
                std::ostringstream stream;
                stream
                    << std::fixed
                    << std::setprecision(0)
                    << value
                    << "%";
                return stream.str();
            };

            auto drawSmallGpuGraph =
                [&](int x,
                    int y,
                    int width,
                    int height,
                    const std::vector<double>& history,
                    double maximum)
            {
                if (maximum <= 0.0)
                {
                    maximum = 1.0;
                }

                drawDiskHistoryGraph(
                    hdc,
                    x,
                    y,
                    width,
                    height,
                    history,
                    maximum,
                    gpuPurple,
                    RGB(48, 28, 70)
                );
            };

            drawText(
                hdc,
                gpuStats.size() > 1
                    ? "GPU " +
                        std::to_string(
                            selectedGpuIndex
                        )
                    : "GPU",
                280,
                128,
                textPrimary,
                titleFont
            );

            std::string displayGpuName =
                shortenPerformanceText(
                    gpu.name,
                    44
                );

            SIZE gpuNameExtent = {};
            setFont(hdc, labelFont);

            GetTextExtentPoint32A(
                hdc,
                displayGpuName.c_str(),
                static_cast<int>(
                    displayGpuName.size()
                ),
                &gpuNameExtent
            );

            int gpuNameX =
                890 - gpuNameExtent.cx;

            if (gpuNameX < 560)
            {
                gpuNameX = 560;
            }

            drawText(
                hdc,
                displayGpuName,
                gpuNameX,
                140,
                textSecondary,
                labelFont
            );

            drawText(
                hdc,
                "3D",
                280,
                168,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                percentText(
                    gpu.utilizationPercent
                ),
                850,
                168,
                textSecondary,
                smallFont
            );

            drawSmallGpuGraph(
                280,
                187,
                610,
                118,
                gpu.utilizationHistory,
                100.0
            );

            drawText(
                hdc,
                "60 seconds",
                280,
                308,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "0",
                882,
                308,
                textSecondary,
                smallFont
            );

            double dedicatedTotalGB =
                gpu.dedicatedMemoryTotalBytes /
                bytesPerGpuGB;

            double sharedTotalGB =
                gpu.sharedMemoryTotalBytes /
                bytesPerGpuGB;

            drawText(
                hdc,
                "Dedicated GPU memory usage",
                280,
                335,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                formatGpuMemory(
                    gpu.dedicatedMemoryUsedBytes,
                    gpu.dedicatedMemoryTotalBytes
                ),
                438,
                335,
                textSecondary,
                smallFont
            );

            drawSmallGpuGraph(
                280,
                354,
                290,
                60,
                gpu.dedicatedMemoryHistory,
                dedicatedTotalGB
            );

            drawText(
                hdc,
                "Shared GPU memory usage",
                600,
                335,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                formatGpuMemory(
                    gpu.sharedMemoryUsedBytes,
                    gpu.sharedMemoryTotalBytes
                ),
                748,
                335,
                textSecondary,
                smallFont
            );

            drawSmallGpuGraph(
                600,
                354,
                290,
                60,
                gpu.sharedMemoryHistory,
                sharedTotalGB
            );

            drawText(
                hdc,
                "Video Encode",
                280,
                430,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                percentText(
                    gpu.encodePercent
                ),
                535,
                430,
                textSecondary,
                smallFont
            );

            drawSmallGpuGraph(
                280,
                449,
                290,
                50,
                gpu.encodeHistory,
                100.0
            );

            drawText(
                hdc,
                "Video Decode",
                600,
                430,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                percentText(
                    gpu.decodePercent
                ),
                855,
                430,
                textSecondary,
                smallFont
            );

            drawSmallGpuGraph(
                600,
                449,
                290,
                50,
                gpu.decodeHistory,
                100.0
            );

            // Bottom live values.
            drawText(
                hdc,
                "Utilization",
                280,
                520,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                percentText(
                    gpu.utilizationPercent
                ),
                280,
                540,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                "Dedicated GPU memory",
                390,
                520,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                formatGpuMemory(
                    gpu.dedicatedMemoryUsedBytes,
                    gpu.dedicatedMemoryTotalBytes
                ),
                390,
                540,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                "Shared GPU memory",
                535,
                520,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                formatGpuMemory(
                    gpu.sharedMemoryUsedBytes,
                    gpu.sharedMemoryTotalBytes
                ),
                535,
                540,
                textPrimary,
                smallFont
            );

            std::string temperatureText = "--";
            if (gpu.temperatureC >= 0.0)
            {
                std::ostringstream stream;
                stream
                    << std::fixed
                    << std::setprecision(0)
                    << gpu.temperatureC
                    << " C";
                temperatureText = stream.str();
            }

            std::string fanText = "--";
            if (gpu.fanPercent >= 0)
            {
                fanText =
                    std::to_string(
                        gpu.fanPercent
                    ) +
                    "%";

                if (gpu.fanRpm >= 0)
                {
                    fanText +=
                        " (" +
                        std::to_string(
                            gpu.fanRpm
                        ) +
                        " RPM)";
                }
            }

            std::string powerText = "--";
            if (gpu.powerW >= 0.0)
            {
                std::ostringstream stream;
                stream
                    << std::fixed
                    << std::setprecision(0)
                    << gpu.powerW
                    << " W";

                if (gpu.powerLimitW > 0.0)
                {
                    stream
                        << " / "
                        << std::setprecision(0)
                        << gpu.powerLimitW
                        << " W";
                }

                powerText = stream.str();
            }

            drawText(
                hdc,
                "GPU temperature",
                280,
                585,
                textSecondary,
                smallFont
            );
            drawText(
                hdc,
                temperatureText,
                280,
                605,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                "Fan speed",
                390,
                585,
                textSecondary,
                smallFont
            );
            drawText(
                hdc,
                fanText,
                390,
                605,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                "GPU power",
                535,
                585,
                textSecondary,
                smallFont
            );
            drawText(
                hdc,
                powerText,
                535,
                605,
                textPrimary,
                labelFont
            );

            // Right-side technical details, matching the reference
            // layout. Values that Windows/the vendor driver does not
            // expose are deliberately shown as -- instead of guessed.
            HPEN detailPen =
                CreatePen(
                    PS_SOLID,
                    1,
                    RGB(45, 48, 58)
                );
            HGDIOBJ oldDetailPen =
                SelectObject(
                    hdc,
                    detailPen
                );
            MoveToEx(
                hdc,
                665,
                515,
                nullptr
            );
            LineTo(
                hdc,
                665,
                660
            );
            SelectObject(
                hdc,
                oldDetailPen
            );
            DeleteObject(detailPen);

            const int detailLabelX = 680;
            const int detailValueX = 790;
            int detailY = 520;
            const int detailGap = 20;

            auto drawGpuDetail =
                [&](const std::string& label,
                    const std::string& value)
            {
                drawText(
                    hdc,
                    label,
                    detailLabelX,
                    detailY,
                    textSecondary,
                    smallFont
                );

                drawText(
                    hdc,
                    shortenPerformanceText(
                        value.empty()
                            ? "--"
                            : value,
                        18
                    ),
                    detailValueX,
                    detailY,
                    textPrimary,
                    smallFont
                );

                detailY += detailGap;
            };

            drawGpuDetail(
                "Driver version:",
                gpu.driverVersion
            );
            drawGpuDetail(
                "Driver date:",
                gpu.driverDate
            );
            drawGpuDetail(
                "DirectX version:",
                gpu.directXVersion
            );
            drawGpuDetail(
                gpu.vendor == "NVIDIA"
                    ? "CUDA cores:"
                    : "Compute cores:",
                gpu.computeCores
            );

            std::ostringstream graphicsMemory;
            if (gpu.dedicatedMemoryTotalBytes > 0)
            {
                graphicsMemory
                    << std::fixed
                    << std::setprecision(1)
                    << dedicatedTotalGB
                    << " GB";
            }
            else
            {
                graphicsMemory << "--";
            }

            drawGpuDetail(
                "Graphics memory:",
                graphicsMemory.str()
            );
            drawGpuDetail(
                "Bus interface:",
                gpu.busInterface
            );
            drawGpuDetail(
                "HW reserved:",
                gpu.hardwareReservedMemory
            );
        }
    }


    // --------------------------------------------------------
    // NETWORK VIEW
    // --------------------------------------------------------
    else
    {
        drawRoundedBox(
            hdc,
            255,
            115,
            915,
            665,
            performanceCard
        );

        drawText(
            hdc,
            "NETWORK",
            280,
            125,
            textPrimary,
            titleFont
        );

        drawText(
            hdc,
            "Live adapter throughput, addressing and connection details.",
            280,
            157,
            textSecondary,
            smallFont
        );

        if (networkStats.empty())
        {
            drawText(
                hdc,
                "No active Ethernet, Wi-Fi, PPP, or tunnel adapter was detected.",
                280,
                215,
                textSecondary,
                labelFont
            );
        }
        else
        {
            int networkIndex =
                std::clamp(
                    selectedNetworkIndex,
                    0,
                    static_cast<int>(
                        networkStats.size()
                    ) - 1
                );

            const NetworkStats& adapter =
                networkStats[networkIndex];

            const COLORREF uploadBlue =
                RGB(45, 145, 245);

            // ------------------------------------------------
            // ADAPTER SELECTOR / SUMMARY
            // ------------------------------------------------
            drawRoundedBox(
                hdc,
                280,
                180,
                890,
                240,
                RGB(20, 24, 30)
            );

            drawText(
                hdc,
                adapter.type,
                300,
                190,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                shortenPerformanceText(
                    adapter.description,
                    34
                ),
                300,
                214,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "Status",
                565,
                188,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                adapter.status,
                565,
                211,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                "IPv4",
                670,
                188,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                shortenPerformanceText(
                    adapter.ipv4Address,
                    15
                ),
                670,
                211,
                textPrimary,
                smallFont
            );

            std::string linkText =
                adapter.linkSpeedMbps > 0.0
                ? formatNetworkSpeed(
                    adapter.linkSpeedMbps
                  )
                : "--";

            drawText(
                hdc,
                "Link",
                750,
                188,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                linkText,
                750,
                211,
                textPrimary,
                smallFont
            );

            if (networkStats.size() > 1)
            {
                drawRoundedBox(
                    hdc,
                    835,
                    186,
                    858,
                    215,
                    RGB(36, 42, 54)
                );
                drawText(
                    hdc,
                    "<",
                    843,
                    191,
                    textPrimary,
                    labelFont
                );

                drawRoundedBox(
                    hdc,
                    862,
                    186,
                    885,
                    215,
                    RGB(36, 42, 54)
                );
                drawText(
                    hdc,
                    ">",
                    870,
                    191,
                    textPrimary,
                    labelFont
                );

                drawText(
                    hdc,
                    std::to_string(
                        networkIndex + 1
                    ) +
                    " / " +
                    std::to_string(
                        networkStats.size()
                    ),
                    837,
                    219,
                    textSecondary,
                    smallFont
                );
            }

            // ------------------------------------------------
            // NETWORK USAGE GRAPH
            // ------------------------------------------------
            drawRoundedBox(
                hdc,
                280,
                255,
                675,
                485,
                RGB(20, 24, 30)
            );

            drawText(
                hdc,
                "Network Usage",
                300,
                270,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                "Download",
                500,
                271,
                networkOrange,
                smallFont
            );

            drawText(
                hdc,
                "Upload",
                590,
                271,
                uploadBlue,
                smallFont
            );

            double networkScale =
                getNetworkGraphScale(
                    adapter
                );

            drawNetworkHistoryGraph(
                hdc,
                315,
                305,
                330,
                135,
                adapter,
                networkScale,
                networkOrange,
                uploadBlue
            );

            std::ostringstream scaleTop;
            scaleTop
                << std::fixed
                << std::setprecision(
                    networkScale >= 10.0
                    ? 0
                    : 1
                )
                << networkScale
                << " Mbps";

            std::ostringstream scaleHalf;
            scaleHalf
                << std::fixed
                << std::setprecision(
                    networkScale >= 10.0
                    ? 0
                    : 1
                )
                << (networkScale / 2.0)
                << " Mbps";

            drawText(
                hdc,
                scaleTop.str(),
                290,
                299,
                textSecondary,
                smallFont
            );
            drawText(
                hdc,
                scaleHalf.str(),
                290,
                365,
                textSecondary,
                smallFont
            );
            drawText(
                hdc,
                "0",
                300,
                431,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "60 seconds",
                315,
                447,
                textSecondary,
                smallFont
            );
            drawText(
                hdc,
                "Now",
                620,
                447,
                textSecondary,
                smallFont
            );

            // ------------------------------------------------
            // ADAPTER DETAILS
            // ------------------------------------------------
            drawRoundedBox(
                hdc,
                690,
                255,
                890,
                450,
                RGB(20, 24, 30)
            );

            drawText(
                hdc,
                "Adapter Details",
                705,
                270,
                textPrimary,
                labelFont
            );

            int detailY = 300;
            const int detailGap = 20;

            auto drawNetworkDetail =
                [&](const std::string& label,
                    const std::string& value)
            {
                drawText(
                    hdc,
                    label,
                    705,
                    detailY,
                    textSecondary,
                    smallFont
                );

                drawText(
                    hdc,
                    shortenPerformanceText(
                        value.empty()
                            ? "--"
                            : value,
                        19
                    ),
                    785,
                    detailY,
                    textPrimary,
                    smallFont
                );

                detailY += detailGap;
            };

            drawNetworkDetail(
                "Name:",
                adapter.name
            );
            drawNetworkDetail(
                "Type:",
                adapter.type
            );
            drawNetworkDetail(
                "MAC:",
                adapter.macAddress
            );
            drawNetworkDetail(
                "IPv4:",
                adapter.ipv4Address
            );
            drawNetworkDetail(
                "IPv6:",
                adapter.ipv6Address
            );
            drawNetworkDetail(
                "Gateway:",
                adapter.defaultGateway
            );
            drawNetworkDetail(
                "DNS:",
                adapter.dnsServers
            );

            // ------------------------------------------------
            // DOWNLOAD / UPLOAD SUMMARY CARDS
            // ------------------------------------------------
            drawRoundedBox(
                hdc,
                280,
                500,
                470,
                650,
                RGB(20, 24, 30)
            );

            drawText(
                hdc,
                "DOWNLOAD",
                300,
                515,
                networkOrange,
                labelFont
            );
            drawText(
                hdc,
                formatNetworkSpeed(
                    adapter.downloadMbps
                ),
                300,
                548,
                textPrimary,
                bigFont
            );
            drawText(
                hdc,
                "Total received",
                300,
                605,
                textSecondary,
                smallFont
            );
            drawText(
                hdc,
                formatNetworkBytes(
                    adapter.totalDownloadedBytes
                ),
                300,
                625,
                textPrimary,
                labelFont
            );

            drawRoundedBox(
                hdc,
                485,
                500,
                675,
                650,
                RGB(20, 24, 30)
            );

            drawText(
                hdc,
                "UPLOAD",
                505,
                515,
                uploadBlue,
                labelFont
            );
            drawText(
                hdc,
                formatNetworkSpeed(
                    adapter.uploadMbps
                ),
                505,
                548,
                textPrimary,
                bigFont
            );
            drawText(
                hdc,
                "Total sent",
                505,
                605,
                textSecondary,
                smallFont
            );
            drawText(
                hdc,
                formatNetworkBytes(
                    adapter.totalUploadedBytes
                ),
                505,
                625,
                textPrimary,
                labelFont
            );

            // ------------------------------------------------
            // NETWORK ACTIVITY / CONNECTIONS
            // ------------------------------------------------
            drawRoundedBox(
                hdc,
                690,
                465,
                890,
                650,
                RGB(20, 24, 30)
            );

            drawText(
                hdc,
                "Network Activity",
                705,
                480,
                textPrimary,
                labelFont
            );

            int activityY = 510;
            const int activityGap = 20;

            auto drawActivity =
                [&](const std::string& label,
                    const std::string& value)
            {
                drawText(
                    hdc,
                    label,
                    705,
                    activityY,
                    textSecondary,
                    smallFont
                );
                drawText(
                    hdc,
                    shortenPerformanceText(
                        value,
                        15
                    ),
                    800,
                    activityY,
                    textPrimary,
                    smallFont
                );
                activityY += activityGap;
            };

            drawActivity(
                "Packets in:",
                formatCount(
                    adapter.packetsReceived
                )
            );
            drawActivity(
                "Packets out:",
                formatCount(
                    adapter.packetsSent
                )
            );
            drawActivity(
                "Tracked:",
                formatNetworkDuration(
                    adapter.trackedSinceTick
                )
            );
            drawActivity(
                "Connections:",
                std::to_string(
                    activeNetworkConnections.size()
                )
            );

            if (!activeNetworkConnections.empty())
            {
                const NetworkConnectionInfo& connection =
                    activeNetworkConnections.front();

                drawActivity(
                    "Top process:",
                    connection.processName
                );
                drawActivity(
                    "Remote:",
                    connection.remoteAddress
                );
            }
        }
    }
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
    345,
    textSecondary,
    labelFont
);

if (!gpuStats.empty())
{
    int dashboardGpuIndex = 0;

    for (int i = 1;
         i < static_cast<int>(
             gpuStats.size()
         );
         i++)
    {
        if (
            gpuStats[i].utilizationPercent >
            gpuStats[dashboardGpuIndex].
                utilizationPercent
        )
        {
            dashboardGpuIndex = i;
        }
    }

    const GpuStats& dashboardGpu =
        gpuStats[dashboardGpuIndex];

    std::ostringstream gpuPercent;
    gpuPercent
        << std::fixed
        << std::setprecision(0)
        << dashboardGpu.utilizationPercent
        << "%";

    drawText(
        hdc,
        gpuPercent.str(),
        60,
        378,
        textPrimary,
        bigFont
    );

    drawDiskHistoryGraph(
        hdc,
        150,
        370,
        135,
        48,
        dashboardGpu.utilizationHistory,
        100.0,
        RGB(155, 85, 220),
        RGB(55, 31, 76)
    );

    std::string gpuName =
        dashboardGpu.name;

    if (gpuName.size() > 27)
    {
        gpuName =
            gpuName.substr(0, 24) +
            "...";
    }

    drawText(
        hdc,
        "GPU " +
            std::to_string(
                dashboardGpuIndex
            ) +
            " - " +
            gpuName,
        60,
        425,
        textSecondary,
        smallFont
    );
}
else
{
    drawText(
        hdc,
        "--",
        60,
        382,
        textPrimary,
        bigFont
    );

    drawText(
        hdc,
        "No GPU detected",
        60,
        425,
        textSecondary,
        smallFont
    );
}


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
    345,
    textSecondary,
    labelFont
);

if (!networkStats.empty())
{
    const NetworkStats& dashboardNetwork =
        networkStats.front();

    drawText(
        hdc,
        "R " +
            formatNetworkSpeed(
                dashboardNetwork.downloadMbps
            ),
        360,
        378,
        textPrimary,
        labelFont
    );

    drawText(
        hdc,
        "S " +
            formatNetworkSpeed(
                dashboardNetwork.uploadMbps
            ),
        360,
        402,
        textPrimary,
        labelFont
    );

    drawNetworkHistoryGraph(
        hdc,
        475,
        370,
        115,
        48,
        dashboardNetwork,
        getNetworkGraphScale(
            dashboardNetwork
        ),
        RGB(220, 140, 55),
        RGB(45, 145, 245)
    );

    std::string adapterText =
        dashboardNetwork.type +
        " - " +
        dashboardNetwork.name;

    if (adapterText.size() > 30)
    {
        adapterText =
            adapterText.substr(0, 27) +
            "...";
    }

    drawText(
        hdc,
        adapterText,
        360,
        425,
        textSecondary,
        smallFont
    );
}
else
{
    drawText(
        hdc,
        "--",
        360,
        382,
        textPrimary,
        bigFont
    );

    drawText(
        hdc,
        "No active adapter",
        360,
        425,
        textSecondary,
        smallFont
    );
}


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
    345,
    textSecondary,
    labelFont
);

double dashboardTemperature = -1.0;
std::string dashboardTemperatureSource =
    "--";

for (int i = 0;
     i < static_cast<int>(
         gpuStats.size()
     );
     i++)
{
    if (
        gpuStats[i].temperatureC >= 0.0 &&
        gpuStats[i].temperatureC >
            dashboardTemperature
    )
    {
        dashboardTemperature =
            gpuStats[i].temperatureC;
        dashboardTemperatureSource =
            "GPU " +
            std::to_string(i) +
            " - " +
            gpuStats[i].name;
    }
}

if (
    temperatureStats.systemTemperatureC >= 0.0 &&
    temperatureStats.systemTemperatureC >
        dashboardTemperature
)
{
    dashboardTemperature =
        temperatureStats.systemTemperatureC;
    dashboardTemperatureSource =
        temperatureStats.hardwareSensorAvailable
        ? "CPU / motherboard sensor"
        : "Windows ACPI thermal zone";
}

if (dashboardTemperature >= 0.0)
{
    std::ostringstream temperature;
    temperature
        << std::fixed
        << std::setprecision(0)
        << dashboardTemperature
        << " C";

    drawText(
        hdc,
        temperature.str(),
        660,
        378,
        textPrimary,
        bigFont
    );

    if (dashboardTemperatureSource.size() > 30)
    {
        dashboardTemperatureSource =
            dashboardTemperatureSource.substr(
                0,
                27
            ) +
            "...";
    }

    drawText(
        hdc,
        dashboardTemperatureSource,
        660,
        425,
        textSecondary,
        smallFont
    );
}
else
{
    drawText(
        hdc,
        "--",
        660,
        382,
        textPrimary,
        bigFont
    );

    drawText(
        hdc,
        "No supported temperature sensor",
        660,
        425,
        textSecondary,
        smallFont
    );
}


// Restore normal drawing coordinates
SetViewportOrgEx(
    hdc,
    oldOrigin.x,
    oldOrigin.y,
    nullptr
);

}

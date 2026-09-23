#include "SettingsRuntime.h"
#include "UpdateChecker.h"
#include "Version.h"
#include "Theme.h"
#include "ProcessMetadata.h"
#include "UI.h"
#include "SidebarIcons.h"
#include "Settings.h"
#include "Stats.h"
#include "Widget.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include "Processes.h"
#include <cctype>
#include <cwctype>
#include <psapi.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <memory>
#include <map>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <initializer_list>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;
extern HFONT titleFont;
extern HFONT subtitleFont;
extern HFONT labelFont;
extern HFONT mediumFont;
extern HFONT bigFont;
extern HFONT smallFont;
extern bool processPriorityDropdownOpen;


// --------------------------------------------------------
// RESPONSIVE HIGH-RESOLUTION RENDER STATE
// --------------------------------------------------------
// Main.cpp keeps the Win32/GDI logical coordinate system at 1190x720.
// GDI+ owns a separate transform, so it receives the same scale/offset
// explicitly through ScopedUiGraphics. This prevents GDI+ from drawing at
// 1x while the rest of the interface is scaled.
static float gSysMonUiRenderScale = 1.0f;
static float gSysMonUiRenderScaleX = 1.0f;
static int gSysMonUiRenderOffsetX = 0;
static int gSysMonUiRenderOffsetY = 0;
static bool gProcessFastScrollRender = false;
static bool gFastNavigationRender = false;

void setProcessFastScrollRender(bool enabled)
{
    gProcessFastScrollRender = enabled;
}

void setUiFastNavigationRender(bool enabled)
{
    gFastNavigationRender = enabled;
}

void setUiRenderTransform(
    float scale,
    int offsetX,
    int offsetY,
    float scaleX)
{
    gSysMonUiRenderScale =
        scale > 0.0f
        ? scale
        : 1.0f;

    gSysMonUiRenderScaleX = scaleX > 0.0f ? scaleX : gSysMonUiRenderScale;
    gSysMonUiRenderOffsetX = offsetX;
    gSysMonUiRenderOffsetY = offsetY;
}

static int scaleUiCoordinate(
    int logicalValue)
{
    return static_cast<int>(
        logicalValue *
        gSysMonUiRenderScale +
        (logicalValue >= 0 ? 0.5f : -0.5f)
    );
}

class ScopedUiGraphics
{
public:
    explicit ScopedUiGraphics(
        HDC hdc, float fixedCenterX = 0.0f, bool preserveShape = false)
        : hdc_(hdc)
    {
        POINT viewportOrigin = {};
        GetViewportOrgEx(
            hdc_,
            &viewportOrigin
        );

        savedDc_ =
            SaveDC(hdc_);

        SetGraphicsMode(
            hdc_,
            GM_ADVANCED
        );

        ModifyWorldTransform(
            hdc_,
            nullptr,
            MWT_IDENTITY
        );

        SetMapMode(
            hdc_,
            MM_TEXT
        );

        SetViewportOrgEx(
            hdc_,
            0,
            0,
            nullptr
        );

        graphics_ =
            std::make_unique<Gdiplus::Graphics>(
                hdc_
            );

        Gdiplus::Matrix transform(
            preserveShape ? gSysMonUiRenderScale : gSysMonUiRenderScaleX,
            0.0f,
            0.0f,
            gSysMonUiRenderScale,
            static_cast<Gdiplus::REAL>(
                gSysMonUiRenderOffsetX +
                viewportOrigin.x + (preserveShape ?
                    (gSysMonUiRenderScaleX - gSysMonUiRenderScale) * fixedCenterX : 0.0f)
            ),
            static_cast<Gdiplus::REAL>(
                gSysMonUiRenderOffsetY +
                viewportOrigin.y
            )
        );

        graphics_->SetTransform(
            &transform
        );
    }

    ~ScopedUiGraphics()
    {
        graphics_.reset();

        if (savedDc_ != 0)
        {
            RestoreDC(
                hdc_,
                savedDc_
            );
        }
    }

    Gdiplus::Graphics& get()
    {
        return *graphics_;
    }

private:
    HDC hdc_ = nullptr;
    int savedDc_ = 0;
    std::unique_ptr<Gdiplus::Graphics>
        graphics_;
};


// Small GDI symbols and executable icons retain their native aspect ratio.
class ScopedGdiShape
{
public:
    ScopedGdiShape(HDC dc, float centerX) : dc_(dc)
    {
        active_ = GetWorldTransform(dc_, &old_) && std::abs(old_.eM11 - old_.eM22) > 0.001f;
        if (active_)
        {
            XFORM shape = old_;
            shape.eDx += (old_.eM11 - old_.eM22) * centerX;
            shape.eM11 = old_.eM22;
            SetWorldTransform(dc_, &shape);
        }
    }
    ~ScopedGdiShape() { if (active_) SetWorldTransform(dc_, &old_); }
private:
    HDC dc_;
    XFORM old_ = {};
    bool active_ = false;
};

static BOOL drawUiEllipse(HDC dc, int left, int top, int right, int bottom)
{
    ScopedGdiShape shape(dc, (left + right) * 0.5f);
    return Ellipse(dc, left, top, right, bottom);
}

static BOOL drawUiIcon(HDC dc, int x, int y, HICON icon, int width, int height,
    UINT step, HBRUSH brush, UINT flags)
{
    ScopedGdiShape shape(dc, x + width * 0.5f);
    return DrawIconEx(dc, x, y, icon, width, height, step, brush, flags);
}

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
    const int sizeIndex = (std::max)(0, (std::min)(2, appSettings.fontSizeIndex));
    const int variant = sizeIndex + (appSettings.compactMode ? 3 : 0);
    static std::map<std::pair<HFONT,int>, HFONT> fonts;
    if (variant != 1 && font) {
        auto key=std::make_pair(font,variant);
        auto found=fonts.find(key);
        if(found==fonts.end()) {
            LOGFONTA description{};
            if(GetObjectA(font,sizeof(description),&description)) {
                const double factor=(sizeIndex==0 ? 0.9 : sizeIndex==2 ? 1.08 : 1.0)*(appSettings.compactMode ? 0.96 : 1.0);
                description.lfHeight=static_cast<LONG>(std::round(description.lfHeight*factor));
                HFONT resized=CreateFontIndirectA(&description);
                if(resized) found=fonts.emplace(key,resized).first;
            }
        }
        if(found!=fonts.end()) font=found->second;
    }
    SelectObject(hdc,font);
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

    XFORM original = {};
    const bool restoreTransform = GetWorldTransform(hdc, &original) &&
        std::abs(original.eM11 - original.eM22) > 0.001f;
    if (restoreTransform)
    {
        XFORM textTransform = original;
        textTransform.eDx += (original.eM11 - original.eM22) * x;
        textTransform.eM11 = original.eM22;
        SetWorldTransform(hdc, &textTransform);
    }
    TextOutA(
        hdc,
        x,
        y,
        text.c_str(),
        static_cast<int>(
            text.length()
        )
    );
    if (restoreTransform) SetWorldTransform(hdc, &original);
}


namespace
{
    std::map<COLORREF, HBRUSH> cachedSolidBrushes;

    // Pens were being created and destroyed for every card border, divider
    // and chevron on every frame. Cache them the same way brushes already are.
    std::map<std::pair<COLORREF, int>, HPEN> cachedPens;

    HPEN getCachedPen(COLORREF color, int width)
    {
        const std::pair<COLORREF, int> key(color, width);
        auto found = cachedPens.find(key);

        if (found != cachedPens.end())
        {
            return found->second;
        }

        HPEN pen = CreatePen(PS_SOLID, width, color);

        if (pen != nullptr)
        {
            cachedPens[key] = pen;
        }

        return pen;
    }

    HBRUSH getCachedSolidBrush(COLORREF color)
    {
        auto found = cachedSolidBrushes.find(color);

        if (found != cachedSolidBrushes.end())
        {
            return found->second;
        }

        HBRUSH brush = CreateSolidBrush(color);

        if (brush != nullptr)
        {
            cachedSolidBrushes[color] = brush;
        }

        return brush;
    }
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
        getCachedSolidBrush(color);

    if (brush == nullptr)
    {
        return;
    }

    HGDIOBJ oldBrush =
        SelectObject(
            hdc,
            brush
        );

    // The old implementation created a new pen and brush for every card on
    // every frame. The border uses the same color as the fill, so a stock
    // NULL_PEN gives the same appearance without allocating a GDI object.
    HGDIOBJ oldPen =
        SelectObject(
            hdc,
            GetStockObject(NULL_PEN)
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
}



static void addRoundedRectPath(
    Gdiplus::GraphicsPath& path,
    float x,
    float y,
    float width,
    float height,
    float radius)
{
    if (width <= 0.0f || height <= 0.0f)
    {
        return;
    }

    radius =
        (std::min)(
            radius,
            (std::min)(width, height) * 0.5f
        );

    if (radius <= 0.5f)
    {
        Gdiplus::RectF rect(x, y, width, height);
        path.AddRectangle(rect);
        return;
    }

    const float diameter = radius * 2.0f;

    path.AddArc(x, y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(x + width - diameter, y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(x + width - diameter, y + height - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(x, y + height - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}


// Small interactive controls use antialiased paths at the current UI scale.
static void drawSmoothControlRect(
    HDC hdc, float left, float top, float right, float bottom,
    float radius, COLORREF color)
{
    if (right <= left || bottom <= top) return;
    ScopedUiGraphics scoped(hdc, (left + right) * 0.5f, right - left <= 22.0f);
    auto& graphics = scoped.get();
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::GraphicsPath path;
    addRoundedRectPath(path, left, top, right - left, bottom - top, radius);
    Gdiplus::SolidBrush brush(Gdiplus::Color(
        255, GetRValue(color), GetGValue(color), GetBValue(color)));
    graphics.FillPath(&brush, &path);
}


static BYTE brightenChannel(
    BYTE value,
    int amount)
{
    return static_cast<BYTE>(
        (std::min)(
            255,
            static_cast<int>(value) + amount
        )
    );
}


static void drawRecommendedUsageBar(
    HDC hdc,
    int x,
    int y,
    int width,
    int height,
    double percent,
    COLORREF accentColor)
{
    if (hdc == nullptr || width <= 0 || height <= 0)
    {
        return;
    }

    percent = std::clamp(percent, 0.0, 100.0);

    ScopedUiGraphics scopedGraphics(hdc);
    Gdiplus::Graphics& graphics = scopedGraphics.get();

    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    const float barX = static_cast<float>(x);
    const float barY = static_cast<float>(y);
    const float barWidth = static_cast<float>(width);
    const float barHeight = static_cast<float>(height);
    const float radius = barHeight * 0.5f;

    Gdiplus::GraphicsPath trackPath;
    addRoundedRectPath(
        trackPath,
        barX,
        barY,
        barWidth,
        barHeight,
        radius
    );

    Gdiplus::SolidBrush trackBrush(
        themeGdiColor(255, 24, 38, 49)
    );
    graphics.FillPath(&trackBrush, &trackPath);

    Gdiplus::Pen trackBorder(
        themeGdiColor(255, 50, 70, 86),
        1.0f
    );
    graphics.DrawPath(&trackBorder, &trackPath);

    if (percent <= 0.0)
    {
        return;
    }

    float fillWidth =
        barWidth * static_cast<float>(percent / 100.0);

    fillWidth =
        (std::max)(
            fillWidth,
            (std::min)(barHeight, barWidth)
        );

    fillWidth = (std::min)(fillWidth, barWidth);

    const BYTE red = GetRValue(accentColor);
    const BYTE green = GetGValue(accentColor);
    const BYTE blue = GetBValue(accentColor);

    const float fillRadius =
        (std::min)(radius, fillWidth * 0.5f);

    if (height >= 7)
    {
        Gdiplus::GraphicsPath glowPath;
        addRoundedRectPath(
            glowPath,
            barX - 1.0f,
            barY - 1.0f,
            fillWidth + 2.0f,
            barHeight + 2.0f,
            fillRadius + 1.0f
        );

        Gdiplus::SolidBrush glowBrush(
            Gdiplus::Color(34, red, green, blue)
        );
        graphics.FillPath(&glowBrush, &glowPath);
    }

    Gdiplus::GraphicsPath fillPath;
    addRoundedRectPath(
        fillPath,
        barX,
        barY,
        fillWidth,
        barHeight,
        fillRadius
    );

    const Gdiplus::Color brightColor(
        255,
        brightenChannel(red, 30),
        brightenChannel(green, 30),
        brightenChannel(blue, 30)
    );

    const Gdiplus::Color baseColor(
        255,
        red,
        green,
        blue
    );

    Gdiplus::LinearGradientBrush fillBrush(
        Gdiplus::PointF(barX, barY),
        Gdiplus::PointF(
            barX + (std::max)(fillWidth, 1.0f),
            barY
        ),
        brightColor,
        baseColor
    );

    graphics.FillPath(&fillBrush, &fillPath);

    if (height >= 8 && fillWidth > barHeight + 4.0f)
    {
        const float inset = (std::min)(radius, 3.0f);
        Gdiplus::Pen highlightPen(
            themeGdiColor(72, 255, 255, 255),
            1.0f
        );

        graphics.DrawLine(
            &highlightPen,
            barX + inset,
            barY + 1.5f,
            barX + fillWidth - inset,
            barY + 1.5f
        );
    }
}


void drawProgressBar(
    HDC hdc,
    int x,
    int y,
    int width,
    int height,
    double percent)
{
    drawRecommendedUsageBar(
        hdc,
        x,
        y,
        width,
        height,
        percent,
        uiColor(RGB(36, 188, 246))
    );
}


void drawGraphGrid(
    HDC hdc,
    int x,
    int y,
    int width,
    int height)
{
    // One shared pen is enough for every graph grid. Avoid creating and
    // deleting a GDI pen each time a mini graph or full graph is repainted.
    static HPEN gridPen =
        CreatePen(
            PS_SOLID,
            1,
            uiColor(RGB(42, 46, 56))
        );

    if (gridPen == nullptr)
    {
        return;
    }

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
}

void drawCpuGraph(
    HDC hdc,
    int x,
    int y,
    int width,
    int height,
    size_t maxSamples = 0)
{
    size_t sampleCount =
        cpuHistory.size();

    if (
        maxSamples > 0 &&
        sampleCount > maxSamples
    )
    {
        sampleCount = maxSamples;
    }

    if (sampleCount < 2)
        return;

    const size_t startIndex =
        cpuHistory.size() - sampleCount;


    // Draw GDI elements while the caller's responsive world transform
    // and viewport origin are still active.  ScopedUiGraphics temporarily
    // resets the HDC for GDI+ rendering, so creating it before these calls
    // would make the background/grid render at unscaled screen coordinates.
    drawRoundedBox(
        hdc,
        x,
        y,
        x + width,
        y + height,
        uiColor(RGB(24, 27, 34))
    );

    drawGraphGrid(
        hdc,
        x,
        y,
        width,
        height
    );

    ScopedUiGraphics scopedGraphics(hdc);
    Gdiplus::Graphics& graphics =
        scopedGraphics.get();

    graphics.SetSmoothingMode(
        Gdiplus::SmoothingModeAntiAlias
    );

    graphics.SetInterpolationMode(
        Gdiplus::InterpolationModeHighQualityBicubic
    );

    graphics.SetPixelOffsetMode(
        Gdiplus::PixelOffsetModeHighQuality
    );

    // Convert only the requested real CPU-history window into points.
    std::vector<POINT> points(
        sampleCount
    );

    for (size_t i = 0;
         i < sampleCount;
         i++)
    {
        double percent =
            cpuHistory[startIndex + i];

        int pointX =
            x +
            static_cast<int>(
                i *
                static_cast<double>(width) /
                (sampleCount - 1)
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
std::vector<POINT> bezierPoints;

if (points.size() > 2)
{
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
}


graphics.SetSmoothingMode(
    Gdiplus::SmoothingModeAntiAlias
);

graphics.SetInterpolationMode(
    Gdiplus::InterpolationModeHighQualityBicubic
);

graphics.SetPixelOffsetMode(
    Gdiplus::PixelOffsetModeHighQuality
);

POINT firstPoint = points.front();
POINT lastPoint = points.back();

Gdiplus::GraphicsPath fillPath;
Gdiplus::GraphicsPath linePath;

if (points.size() == 2)
{
    fillPath.AddLine(
        static_cast<Gdiplus::REAL>(points[0].x),
        static_cast<Gdiplus::REAL>(points[0].y),
        static_cast<Gdiplus::REAL>(points[1].x),
        static_cast<Gdiplus::REAL>(points[1].y)
    );

    linePath.AddLine(
        static_cast<Gdiplus::REAL>(points[0].x),
        static_cast<Gdiplus::REAL>(points[0].y),
        static_cast<Gdiplus::REAL>(points[1].x),
        static_cast<Gdiplus::REAL>(points[1].y)
    );
}
else
{
    std::vector<Gdiplus::PointF> curvePoints;
    curvePoints.reserve(
        bezierPoints.size()
    );

    for (const POINT& pt : bezierPoints)
    {
        curvePoints.push_back(
            Gdiplus::PointF(
                static_cast<Gdiplus::REAL>(pt.x),
                static_cast<Gdiplus::REAL>(pt.y)
            )
        );
    }

    if (!curvePoints.empty())
    {
        fillPath.AddBeziers(
            curvePoints.data(),
            static_cast<INT>(
                curvePoints.size()
            )
        );

        linePath.AddBeziers(
            curvePoints.data(),
            static_cast<INT>(
                curvePoints.size()
            )
        );
    }
}

fillPath.AddLine(
    static_cast<Gdiplus::REAL>(lastPoint.x),
    static_cast<Gdiplus::REAL>(lastPoint.y),
    static_cast<Gdiplus::REAL>(x + width),
    static_cast<Gdiplus::REAL>(y + height)
);

fillPath.AddLine(
    static_cast<Gdiplus::REAL>(x + width),
    static_cast<Gdiplus::REAL>(y + height),
    static_cast<Gdiplus::REAL>(x),
    static_cast<Gdiplus::REAL>(y + height)
);

fillPath.AddLine(
    static_cast<Gdiplus::REAL>(x),
    static_cast<Gdiplus::REAL>(y + height),
    static_cast<Gdiplus::REAL>(firstPoint.x),
    static_cast<Gdiplus::REAL>(firstPoint.y)
);

fillPath.CloseFigure();

Gdiplus::SolidBrush fillBrush(
    themeGdiColor(55, 50, 130, 245)
);

graphics.FillPath(
    &fillBrush,
    &fillPath
);

Gdiplus::Pen glowOuter(
    themeGdiColor(55, 66, 135, 245),
    6.0f
);

glowOuter.SetLineJoin(
    Gdiplus::LineJoinRound
);
glowOuter.SetStartCap(
    Gdiplus::LineCapRound
);
glowOuter.SetEndCap(
    Gdiplus::LineCapRound
);

graphics.DrawPath(
    &glowOuter,
    &linePath
);

Gdiplus::Pen glowInner(
    themeGdiColor(110, 66, 135, 245),
    3.5f
);

glowInner.SetLineJoin(
    Gdiplus::LineJoinRound
);
glowInner.SetStartCap(
    Gdiplus::LineCapRound
);
glowInner.SetEndCap(
    Gdiplus::LineCapRound
);

graphics.DrawPath(
    &glowInner,
    &linePath
);

Gdiplus::Pen mainLine(
    themeGdiColor(255, 82, 160, 255),
    2.0f
);

mainLine.SetLineJoin(
    Gdiplus::LineJoinRound
);
mainLine.SetStartCap(
    Gdiplus::LineCapRound
);
mainLine.SetEndCap(
    Gdiplus::LineCapRound
);

graphics.DrawPath(
    &mainLine,
    &linePath
);
}
void drawRamGraph(
    HDC hdc,
    int x,
    int y,
    int width,
    int height,
    size_t maxSamples = 0)
{
    const size_t historySize =
        ramHistory.size();

    size_t startIndex = 0;

    if (
        maxSamples > 0 &&
        historySize > maxSamples
    )
    {
        startIndex =
            historySize - maxSamples;
    }

    const size_t visibleSampleCount =
        historySize - startIndex;

    if (visibleSampleCount < 2)
        return;


    drawRoundedBox(
        hdc,
        x,
        y,
        x + width,
        y + height,
        uiColor(RGB(24, 27, 34))
    );

    drawGraphGrid(
        hdc,
        x,
        y,
        width,
        height
    );


    std::vector<POINT> points(
        visibleSampleCount
    );

    for (size_t visibleIndex = 0;
         visibleIndex < visibleSampleCount;
         visibleIndex++)
    {
        const size_t historyIndex =
            startIndex + visibleIndex;

        double percent =
            ramHistory[historyIndex];

        int pointX =
            x +
            static_cast<int>(
                visibleIndex *
                static_cast<double>(width) /
                (visibleSampleCount - 1)
            );

        int pointY =
            y +
            height -
            static_cast<int>(
                (percent / 100.0) *
                height
            );

        points[visibleIndex].x = pointX;
        points[visibleIndex].y = pointY;
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
        uiColor(RGB(60, 38, 95))
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
            uiColor(RGB(140, 80, 220))
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
        uiColor(RGB(20, 24, 30))
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
    COLORREF fillColor,
    size_t maximumSamples = 0)
{
    drawRoundedBox(
        hdc,
        x,
        y,
        x + width,
        y + height,
        uiColor(RGB(20, 24, 30))
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

    size_t startIndex = 0;

    if (
        maximumSamples > 0 &&
        history.size() > maximumSamples
    )
    {
        startIndex =
            history.size() - maximumSamples;
    }

    const size_t visibleCount =
        history.size() - startIndex;

    if (visibleCount < 2)
    {
        return;
    }

    std::vector<POINT> points(
        visibleCount
    );

    for (
        size_t visibleIndex = 0;
        visibleIndex < visibleCount;
        visibleIndex++
    )
    {
        double value =
            std::clamp(
                history[
                    startIndex +
                    visibleIndex
                ],
                0.0,
                scaleMaximum
            );

        points[visibleIndex].x =
            x +
            static_cast<int>(
                visibleIndex *
                static_cast<double>(width) /
                (visibleCount - 1)
            );

        points[visibleIndex].y =
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
    const NetworkStats& adapter,
    size_t maximumSamples = 0)
{
    double maximum =
        (std::max)(
            adapter.downloadMbps,
            adapter.uploadMbps
        );

    auto includeHistory =
        [&](const std::vector<double>& history)
    {
        if (history.empty())
        {
            return;
        }

        size_t startIndex = 0;

        if (
            maximumSamples > 0 &&
            history.size() > maximumSamples
        )
        {
            startIndex =
                history.size() - maximumSamples;
        }

        for (size_t index = startIndex;
             index < history.size();
             index++)
        {
            maximum =
                (std::max)(
                    maximum,
                    history[index]
                );
        }
    };

    includeHistory(
        adapter.downloadHistory
    );
    includeHistory(
        adapter.uploadHistory
    );

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

static std::string formatNetworkSpeed(double mbps) { return SettingsRuntime::networkRate(mbps); }


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
    COLORREF uploadColor,
    size_t maximumSamples = 0)
{
    drawRoundedBox(
        hdc,
        x,
        y,
        x + width,
        y + height,
        uiColor(RGB(20, 24, 30))
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

        if (
            history.size() < 2 ||
            scaleMaximum <= 0.0
        )
        {
            return points;
        }

        size_t startIndex = 0;

        if (
            maximumSamples > 0 &&
            history.size() > maximumSamples
        )
        {
            startIndex =
                history.size() - maximumSamples;
        }

        const size_t visibleCount =
            history.size() - startIndex;

        if (visibleCount < 2)
        {
            return points;
        }

        points.resize(visibleCount);

        for (size_t visibleIndex = 0;
             visibleIndex < visibleCount;
             visibleIndex++)
        {
            double value =
                std::clamp(
                    history[
                        startIndex +
                        visibleIndex
                    ],
                    0.0,
                    scaleMaximum
                );

            points[visibleIndex].x =
                x +
                static_cast<int>(
                    visibleIndex *
                    static_cast<double>(width) /
                    (visibleCount - 1)
                );

            points[visibleIndex].y =
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
                uiColor(RGB(22, 57, 78))
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


static std::string formatDiskByteCounter(
    unsigned long long bytes)
{
    if (bytes == 0)
    {
        return "0 MB";
    }

    const double kilobyte = 1024.0;
    const double megabyte = kilobyte * 1024.0;
    const double gigabyte = megabyte * 1024.0;
    const double terabyte = gigabyte * 1024.0;

    std::ostringstream stream;

    if (bytes >= static_cast<unsigned long long>(terabyte))
    {
        stream
            << std::fixed
            << std::setprecision(1)
            << (bytes / terabyte)
            << " TB";
    }
    else if (bytes >= static_cast<unsigned long long>(gigabyte))
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


struct DiskVolumeUsage
{
    bool valid = false;
    double totalGB = 0.0;
    double usedGB = 0.0;
    double freeGB = 0.0;
};


static DiskVolumeUsage queryDiskVolumeUsage(
    const DiskStats& disk)
{
    struct CachedDiskVolumeUsage
    {
        ULONGLONG refreshedAt = 0;
        DiskVolumeUsage value;
    };

    static std::map<int, CachedDiskVolumeUsage> cache;

    const ULONGLONG now = GetTickCount64();
    auto cached = cache.find(disk.diskNumber);

    if (
        cached != cache.end() &&
        now - cached->second.refreshedAt < 2000
    )
    {
        return cached->second.value;
    }

    DiskVolumeUsage result;

    unsigned long long totalBytes = 0;
    unsigned long long freeBytes = 0;

    for (char driveLetter : disk.driveLetters)
    {
        char rootPath[] = "C:\\";
        rootPath[0] = driveLetter;

        ULARGE_INTEGER freeAvailable = {};
        ULARGE_INTEGER volumeTotal = {};
        ULARGE_INTEGER volumeFree = {};

        if (GetDiskFreeSpaceExA(
                rootPath,
                &freeAvailable,
                &volumeTotal,
                &volumeFree
            ))
        {
            totalBytes += volumeTotal.QuadPart;
            freeBytes += volumeFree.QuadPart;
            result.valid = true;
        }
    }

    if (!result.valid)
    {
        cache[disk.diskNumber] =
            { now, result };
        return result;
    }

    const double bytesPerGB =
        1024.0 * 1024.0 * 1024.0;

    result.totalGB =
        static_cast<double>(totalBytes) /
        bytesPerGB;

    result.freeGB =
        static_cast<double>(freeBytes) /
        bytesPerGB;

    result.usedGB =
        (std::max)(
            0.0,
            result.totalGB - result.freeGB
        );

    cache[disk.diskNumber] =
        { now, result };

    return result;
}


static std::vector<double> diskHistoryTail(
    const std::vector<double>& history,
    size_t sampleCount)
{
    if (history.size() <= sampleCount)
    {
        return history;
    }

    return std::vector<double>(
        history.begin() +
            (history.size() - sampleCount),
        history.end()
    );
}


static double getDiskTransferGraphScaleForRange(
    const DiskStats& disk,
    size_t sampleCount)
{
    double maximum =
        (std::max)(
            disk.readMBps,
            disk.writeMBps
        );

    auto considerTail =
        [&](const std::vector<double>& history)
        {
            size_t start =
                history.size() > sampleCount
                ? history.size() - sampleCount
                : 0;

            for (size_t i = start;
                 i < history.size();
                 i++)
            {
                maximum =
                    (std::max)(
                        maximum,
                        history[i]
                    );
            }
        };

    considerTail(disk.readHistory);
    considerTail(disk.writeHistory);

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
        (std::max)(
            1.0,
            maximum * 1.15
        );

    for (double scale : scales)
    {
        if (scale >= target)
        {
            return scale;
        }
    }

    return target;
}


static void drawDiskReadWriteGraph(
    HDC hdc,
    int x,
    int y,
    int width,
    int height,
    const DiskStats& disk,
    size_t sampleCount,
    double scaleMaximum,
    COLORREF readColor,
    COLORREF writeColor)
{
    drawRoundedBox(
        hdc,
        x,
        y,
        x + width,
        y + height,
        uiColor(RGB(20, 24, 30))
    );

    drawGraphGrid(
        hdc,
        x,
        y,
        width,
        height
    );

    if (scaleMaximum <= 0.0)
    {
        return;
    }

    auto drawSeries =
        [&](const std::vector<double>& history,
            COLORREF color,
            bool fill)
        {
            std::vector<double> visible =
                diskHistoryTail(
                    history,
                    sampleCount
                );

            if (visible.size() < 2)
            {
                return;
            }

            std::vector<POINT> points(
                visible.size()
            );

            for (size_t i = 0;
                 i < visible.size();
                 i++)
            {
                double value =
                    std::clamp(
                        visible[i],
                        0.0,
                        scaleMaximum
                    );

                points[i].x =
                    x +
                    static_cast<int>(
                        i *
                        static_cast<double>(width) /
                        (visible.size() - 1)
                    );

                points[i].y =
                    y +
                    height -
                    static_cast<int>(
                        (value / scaleMaximum) *
                        height
                    );
            }

            if (fill)
            {
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
                        uiColor(RGB(
                            GetRValue(color) / 5,
                            GetGValue(color) / 5,
                            GetBValue(color) / 5
                        ))
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
            }

            HPEN graphPen =
                CreatePen(
                    PS_SOLID,
                    2,
                    color
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
            DeleteObject(graphPen);
        };

    drawSeries(
        disk.readHistory,
        readColor,
        true
    );

    drawSeries(
        disk.writeHistory,
        writeColor,
        false
    );
}

namespace
{
    struct CachedPngEntry
    {
        std::wstring path;
        std::unique_ptr<Gdiplus::Bitmap> bitmap;
    };

    struct CachedTintedPngEntry
    {
        std::wstring path;
        int width = 0;
        int height = 0;
        COLORREF tint = uiColor(RGB(255, 255, 255));
        std::unique_ptr<Gdiplus::Bitmap> bitmap;
    };

    std::vector<CachedPngEntry> cachedPngImages;
    std::vector<CachedTintedPngEntry> cachedTintedPngImages;

    struct CachedAssetAvailability
    {
        std::wstring path;
        bool exists = false;
    };

    std::vector<CachedAssetAvailability> cachedAssetAvailability;

    bool uiAssetExists(const wchar_t* filePath)
    {
        if (filePath == nullptr || *filePath == L'\0')
        {
            return false;
        }

        for (const auto& entry : cachedAssetAvailability)
        {
            if (entry.path == filePath)
            {
                return entry.exists;
            }
        }

        const bool exists =
            GetFileAttributesW((std::wstring(L"logos/") + filePath).c_str()) != INVALID_FILE_ATTRIBUTES;

        cachedAssetAvailability.push_back(
            { filePath, exists }
        );

        return exists;
    }

    ProcessMetadataCache processMetadata;

    std::wstring getProcessExecutablePath(DWORD pid)
    {
        return processMetadata.path(pid);
    }

    HICON getCachedProcessIcon(DWORD pid, int size)
    {
        return processMetadata.icon(pid, true, scaleUiCoordinate(size));
    }

    void drawProcessExecutableIconCachedOnly(
        HDC hdc,
        DWORD pid,
        int x,
        int y,
        int size);

    void drawProcessExecutableIcon(
        HDC hdc,
        DWORD pid,
        int x,
        int y,
        int size)
    {
        if (gFastNavigationRender)
        {
            drawProcessExecutableIconCachedOnly(
                hdc,
                pid,
                x,
                y,
                size
            );
            return;
        }

        HICON icon =
            getCachedProcessIcon(pid, size);

        if (icon == nullptr)
        {
            return;
        }

        drawUiIcon(
            hdc,
            x,
            y,
            icon,
            size,
            size,
            0,
            nullptr,
            DI_NORMAL
        );
    }


    // Scroll repaints must never block on OpenProcess/SHGetFileInfo for a row
    // that has just entered view. Draw an icon only if it is already cached;
    // the next normal Processes refresh resolves any new icons.
    void drawProcessExecutableIconCachedOnly(
        HDC hdc,
        DWORD pid,
        int x,
        int y,
        int size)
    {
        HICON icon = processMetadata.icon(pid, false, scaleUiCoordinate(size));
        if (icon) drawUiIcon(hdc, x, y, icon, size, size, 0, nullptr, DI_NORMAL);
    }


    Gdiplus::Bitmap* getCachedPngImage(
        const wchar_t* filePath)
    {
        if (filePath == nullptr)
        {
            return nullptr;
        }

        for (auto& entry : cachedPngImages)
        {
            if (entry.path == filePath)
            {
                return entry.bitmap.get();
            }
        }

        if (
            !uiAssetExists(filePath)
        )
        {
            return nullptr;
        }

        auto bitmap =
            std::make_unique<Gdiplus::Bitmap>(
                (std::wstring(L"logos/") + filePath).c_str()
            );

        if (bitmap->GetLastStatus() != Gdiplus::Ok)
        {
            return nullptr;
        }

        // Decode once into a compact premultiplied bitmap. UI artwork is used
        // as icons, so 256 pixels still exceeds its largest displayed size.
        // Drawing file-backed 1254px PNGs repeatedly forces costly resampling.
        const UINT longest = (std::max)(bitmap->GetWidth(), bitmap->GetHeight());
        const double ratio = longest > 256 ? 256.0 / longest : 1.0;
        const int masterWidth = (std::max)(1, static_cast<int>(bitmap->GetWidth() * ratio + 0.5));
        const int masterHeight = (std::max)(1, static_cast<int>(bitmap->GetHeight() * ratio + 0.5));
        auto decoded = std::make_unique<Gdiplus::Bitmap>(masterWidth, masterHeight, PixelFormat32bppPARGB);
        if (decoded->GetLastStatus() == Gdiplus::Ok)
        {
            Gdiplus::Graphics graphics(decoded.get());
            graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            Gdiplus::ImageAttributes attributes;
            attributes.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
            if (graphics.DrawImage(bitmap.get(), Gdiplus::Rect(0, 0, masterWidth, masterHeight),
                    0, 0, bitmap->GetWidth(), bitmap->GetHeight(), Gdiplus::UnitPixel, &attributes) == Gdiplus::Ok)
                bitmap.swap(decoded);
        }

        cachedPngImages.push_back(
            {
                filePath,
                std::move(bitmap)
            }
        );

        return
            cachedPngImages.back()
                .bitmap.get();
    }


    Gdiplus::Bitmap* getCachedTintedPngImage(
        const wchar_t* filePath,
        int width,
        int height,
        COLORREF tint)
    {
        if (
            filePath == nullptr ||
            width <= 0 ||
            height <= 0
        )
        {
            return nullptr;
        }

        // Cache at the physical output size, preserving sharpness at each scale.
        width = (std::max)(1, scaleUiCoordinate(width));
        height = (std::max)(1, scaleUiCoordinate(height));
        for (auto& entry : cachedTintedPngImages)
        {
            if (
                entry.path == filePath &&
                entry.tint == tint && entry.width == width && entry.height == height
            )
            {
                return entry.bitmap.get();
            }
        }

        Gdiplus::Bitmap* source =
            getCachedPngImage(filePath);

        if (source == nullptr)
        {
            return nullptr;
        }

        const int sourceWidth =
            (std::max)(
                1,
                static_cast<int>(
                    source->GetWidth()
                )
            );

        const int sourceHeight =
            (std::max)(
                1,
                static_cast<int>(
                    source->GetHeight()
                )
            );

        auto tintedBitmap =
            std::make_unique<Gdiplus::Bitmap>(
                width,
                height,
                PixelFormat32bppPARGB
            );

        if (
            tintedBitmap->GetLastStatus() !=
            Gdiplus::Ok
        )
        {
            return nullptr;
        }

        Gdiplus::Graphics graphics(
            tintedBitmap.get()
        );

        graphics.Clear(
            themeGdiColor(0, 0, 0, 0)
        );

        graphics.SetInterpolationMode(
            Gdiplus::InterpolationModeHighQualityBicubic
        );

        graphics.SetSmoothingMode(
            Gdiplus::SmoothingModeAntiAlias
        );

        graphics.SetPixelOffsetMode(
            Gdiplus::PixelOffsetModeHighQuality
        );

        const float red =
            GetRValue(tint) / 255.0f;

        const float green =
            GetGValue(tint) / 255.0f;

        const float blue =
            GetBValue(tint) / 255.0f;

        Gdiplus::ColorMatrix colorMatrix =
        {
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
            red,  green, blue,  0.0f, 1.0f
        };

        Gdiplus::ImageAttributes attributes;

        attributes.SetColorMatrix(
            &colorMatrix,
            Gdiplus::ColorMatrixFlagsDefault,
            Gdiplus::ColorAdjustTypeBitmap
        );

        Gdiplus::Rect destination(
            0,
            0,
            width,
            height
        );

        graphics.DrawImage(
            source,
            destination,
            0,
            0,
            sourceWidth,
            sourceHeight,
            Gdiplus::UnitPixel,
            &attributes
        );

        if (cachedTintedPngImages.size() >= 128) cachedTintedPngImages.clear();
        cachedTintedPngImages.push_back(
            {
                filePath,
                width,
                height,
                tint,
                std::move(tintedBitmap)
            }
        );

        return
            cachedTintedPngImages.back()
                .bitmap.get();
    }

}


void clearUiImageCache()
{
    for (auto& entry : cachedSolidBrushes)
    {
        if (entry.second != nullptr)
        {
            DeleteObject(entry.second);
        }
    }
    cachedSolidBrushes.clear();

    processMetadata.clear();
    cachedTintedPngImages.clear();
    cachedPngImages.clear();
    cachedAssetAvailability.clear();
}


void preloadUiAssets()
{
    static const wchar_t* assets[] =
    {
        L"logo.png",
        L"dashboard.png",
        L"processes.png",
        L"performance.png",
        L"tools.png",
        L"settings.png",
        L"cpu.png",
        L"memory.png",
        L"disk.png",
        L"gpu.png",
        L"temp.png",
        L"network.png",
        L"sidebar_overview.png",
        L"sidebar_cpu.png",
        L"sidebar_memory.png",
        L"sidebar_disk.png",
        L"sidebar_gpu.png",
        L"sidebar_network.png",
        L"sidebar_temperatures.png",
        L"sidebar_processes.png",
        L"sidebar_systeminfo.png",
        L"sysinfo_os.png",
        L"sysinfo_cpu.png",
        L"sysinfo_gpu.png",
        L"sysinfo_memory.png",
        L"sysinfo_storage.png",
        L"sysinfo_uptime.png"
    };

    for (const wchar_t* asset : assets)
    {
        (void)getCachedPngImage(asset);
    }
}


void drawPngImage(
    HDC hdc,
    const wchar_t* filePath,
    int x,
    int y,
    int width,
    int height)
{
    Gdiplus::Bitmap* image =
        getCachedPngImage(filePath);

    if (image == nullptr)
    {
        return;
    }

    ScopedUiGraphics scopedGraphics(hdc, x + width * 0.5f, true);
    Gdiplus::Graphics& graphics =
        scopedGraphics.get();

    graphics.SetInterpolationMode(
        Gdiplus::InterpolationModeHighQualityBicubic
    );

    graphics.SetSmoothingMode(
        Gdiplus::SmoothingModeAntiAlias
    );

    graphics.SetPixelOffsetMode(
        Gdiplus::PixelOffsetModeHighQuality
    );

    graphics.DrawImage(
        image,
        x,
        y,
        width,
        height
    );
}


static bool drawTintedPngImage(
    HDC hdc,
    const wchar_t* filePath,
    int x,
    int y,
    int width,
    int height,
    COLORREF tint)
{
    if (filePath && wcsncmp(filePath, L"sidebar_", 8) == 0) {
        ScopedUiGraphics symbols(hdc, x + width * 0.5f, true);
        if (drawSidebarSymbol(symbols.get(), filePath, x, y, width, height, tint)) return true;
    }
    Gdiplus::Bitmap* image =
        getCachedTintedPngImage(
            filePath,
            width,
            height,
            tint
        );

    if (image == nullptr)
    {
        return false;
    }

    ScopedUiGraphics scopedGraphics(hdc, x + width * 0.5f, true);
    Gdiplus::Graphics& graphics =
        scopedGraphics.get();

    graphics.SetInterpolationMode(
        Gdiplus::InterpolationModeHighQualityBicubic
    );

    graphics.SetPixelOffsetMode(
        Gdiplus::PixelOffsetModeHighQuality
    );

    graphics.DrawImage(
        image,
        x,
        y,
        width,
        height
    );

    return true;
}


void drawCircularGauge(
    HDC hdc,
    int x,
    int y,
    int size,
    double percent,
    COLORREF accent,
    COLORREF track)
{
    percent = std::clamp(percent, 0.0, 100.0);

    ScopedUiGraphics scopedGraphics(hdc, x + size * 0.5f, true);
    Gdiplus::Graphics& graphics =
        scopedGraphics.get();
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    Gdiplus::Color trackColor(
        255,
        GetRValue(track),
        GetGValue(track),
        GetBValue(track)
    );

    Gdiplus::Color glowColor(
        48,
        GetRValue(accent),
        GetGValue(accent),
        GetBValue(accent)
    );

    Gdiplus::Color accentColor(
        255,
        GetRValue(accent),
        GetGValue(accent),
        GetBValue(accent)
    );

    Gdiplus::Pen trackPen(trackColor, 7.0f);
    Gdiplus::Pen glowPen(glowColor, 11.0f);
    Gdiplus::Pen accentPen(accentColor, 7.0f);

    glowPen.SetStartCap(Gdiplus::LineCapRound);
    glowPen.SetEndCap(Gdiplus::LineCapRound);
    accentPen.SetStartCap(Gdiplus::LineCapRound);
    accentPen.SetEndCap(Gdiplus::LineCapRound);

    Gdiplus::RectF gaugeRect(
        static_cast<Gdiplus::REAL>(x + 6),
        static_cast<Gdiplus::REAL>(y + 6),
        static_cast<Gdiplus::REAL>(size - 12),
        static_cast<Gdiplus::REAL>(size - 12)
    );

    graphics.DrawArc(
        &trackPen,
        gaugeRect,
        0.0f,
        360.0f
    );

    const Gdiplus::REAL sweep =
        static_cast<Gdiplus::REAL>(
            percent * 3.6
        );

    graphics.DrawArc(
        &glowPen,
        gaugeRect,
        -90.0f,
        sweep
    );

    graphics.DrawArc(
        &accentPen,
        gaugeRect,
        -90.0f,
        sweep
    );
}


static std::string shortenDashboardText(
    const std::string& text,
    size_t maxLength)
{
    if (text.size() <= maxLength)
        return text;

    if (maxLength <= 3)
        return text.substr(0, maxLength);

    return
        text.substr(0, maxLength - 3) +
        "...";
}


static void drawDashboardCenteredText(
    HDC hdc,
    const std::string& text,
    int x,
    int y,
    int width,
    int height,
    COLORREF color,
    HFONT font)
{
    setFont(hdc, font);

    SIZE textSize = {};

    GetTextExtentPoint32A(
        hdc,
        text.c_str(),
        static_cast<int>(text.size()),
        &textSize
    );

    drawText(
        hdc,
        text,
        x + static_cast<int>((width - textSize.cx * gSysMonUiRenderScale / gSysMonUiRenderScaleX) / 2),
        y + (height - textSize.cy) / 2,
        color,
        font
    );
}


static void drawDashboardCardIcon(
    HDC hdc,
    int x,
    int y,
    const std::string& kind)
{
    drawRoundedBox(
        hdc,
        x,
        y,
        x + 42,
        y + 42,
        uiColor(RGB(30, 40, 50))
    );

    // Prefer the custom PNG dashboard icons when they are present
    // next to SysMon.exe. Transparent PNGs blend directly into the
    // existing rounded icon tile.
    const wchar_t* iconPath = nullptr;

    if (kind == "CPU")
        iconPath = L"cpu.png";
    else if (kind == "MEMORY")
        iconPath = L"memory.png";
    else if (kind == "DISK")
        iconPath = L"disk.png";
    else if (kind == "GPU")
        iconPath = L"gpu.png";
    else if (kind == "TEMP")
        iconPath = L"temp.png";
    else if (kind == "NETWORK")
        iconPath = L"network.png";

    if (
        iconPath != nullptr &&
        uiAssetExists(iconPath)
    )
    {
        drawPngImage(
            hdc,
            iconPath,
            x + 5,
            y + 5,
            32,
            32
        );

        return;
    }

    // Fallback to the built-in vector icon if a PNG is missing.
    HPEN iconPen =
        CreatePen(
            PS_SOLID,
            2,
            uiColor(RGB(190, 208, 222))
        );

    HGDIOBJ oldPen =
        SelectObject(
            hdc,
            iconPen
        );

    HGDIOBJ oldBrush =
        SelectObject(
            hdc,
            GetStockObject(HOLLOW_BRUSH)
        );

    if (kind == "CPU")
    {
        Rectangle(hdc, x + 14, y + 14, x + 28, y + 28);

        for (int offset = 0; offset < 3; offset++)
        {
            int pin = x + 17 + offset * 4;

            MoveToEx(hdc, pin, y + 10, nullptr);
            LineTo(hdc, pin, y + 14);
            MoveToEx(hdc, pin, y + 28, nullptr);
            LineTo(hdc, pin, y + 32);

            int side = y + 17 + offset * 4;

            MoveToEx(hdc, x + 10, side, nullptr);
            LineTo(hdc, x + 14, side);
            MoveToEx(hdc, x + 28, side, nullptr);
            LineTo(hdc, x + 32, side);
        }
    }
    else if (kind == "MEMORY")
    {
        RoundRect(hdc, x + 8, y + 13, x + 34, y + 28, 4, 4);

        for (int i = 0; i < 4; i++)
        {
            int chipX = x + 12 + i * 5;
            Rectangle(hdc, chipX, y + 17, chipX + 3, y + 23);
        }

        for (int i = 0; i < 5; i++)
        {
            int pinX = x + 10 + i * 5;
            MoveToEx(hdc, pinX, y + 28, nullptr);
            LineTo(hdc, pinX, y + 32);
        }
    }
    else if (kind == "DISK")
    {
        RoundRect(hdc, x + 10, y + 7, x + 32, y + 35, 5, 5);
        MoveToEx(hdc, x + 13, y + 27, nullptr);
        LineTo(hdc, x + 29, y + 27);
        drawUiEllipse(hdc, x + 24, y + 29, x + 28, y + 33);
    }
    else if (kind == "GPU")
    {
        Rectangle(hdc, x + 7, y + 12, x + 35, y + 29);
        drawUiEllipse(hdc, x + 11, y + 16, x + 20, y + 25);
        drawUiEllipse(hdc, x + 23, y + 16, x + 32, y + 25);
        MoveToEx(hdc, x + 11, y + 31, nullptr);
        LineTo(hdc, x + 30, y + 31);
    }
    else if (kind == "TEMP")
    {
        drawUiEllipse(hdc, x + 15, y + 25, x + 27, y + 37);
        RoundRect(hdc, x + 18, y + 7, x + 24, y + 30, 5, 5);
        MoveToEx(hdc, x + 21, y + 14, nullptr);
        LineTo(hdc, x + 21, y + 29);
    }
    else if (kind == "NETWORK")
    {
        MoveToEx(hdc, x + 8, y + 17, nullptr);
        LineTo(hdc, x + 21, y + 9);
        LineTo(hdc, x + 34, y + 17);

        MoveToEx(hdc, x + 12, y + 22, nullptr);
        LineTo(hdc, x + 21, y + 16);
        LineTo(hdc, x + 30, y + 22);

        drawUiEllipse(hdc, x + 18, y + 26, x + 24, y + 32);
    }

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(iconPen);
}


static void drawDashboardHistoryGraph(
    HDC hdc,
    int x,
    int y,
    int width,
    int height,
    const std::vector<double>& history,
    double scaleMaximum,
    COLORREF accent)
{
    ScopedUiGraphics scopedGraphics(hdc);
    Gdiplus::Graphics& graphics =
        scopedGraphics.get();

    graphics.SetSmoothingMode(
        Gdiplus::SmoothingModeAntiAlias
    );

    graphics.SetPixelOffsetMode(
        Gdiplus::PixelOffsetModeHighQuality
    );

    graphics.SetCompositingQuality(
        Gdiplus::CompositingQualityHighQuality
    );

    Gdiplus::SolidBrush backgroundBrush(
        themeGdiColor(105, 10, 16, 23)
    );

    graphics.FillRectangle(
        &backgroundBrush,
        x,
        y,
        width,
        height
    );

    Gdiplus::Pen gridPen(
        themeGdiColor(42, 145, 160, 180),
        1.0f
    );

    for (int i = 1; i < 4; i++)
    {
        Gdiplus::REAL lineX =
            static_cast<Gdiplus::REAL>(
                x + width * i / 4
            );

        graphics.DrawLine(
            &gridPen,
            lineX,
            static_cast<Gdiplus::REAL>(y),
            lineX,
            static_cast<Gdiplus::REAL>(y + height)
        );
    }

    for (int i = 1; i < 4; i++)
    {
        Gdiplus::REAL lineY =
            static_cast<Gdiplus::REAL>(
                y + height * i / 4
            );

        graphics.DrawLine(
            &gridPen,
            static_cast<Gdiplus::REAL>(x),
            lineY,
            static_cast<Gdiplus::REAL>(x + width),
            lineY
        );
    }

    if (history.size() < 2 || scaleMaximum <= 0.0)
        return;

    std::vector<Gdiplus::PointF> points(history.size());

    for (size_t i = 0; i < history.size(); i++)
    {
        double value =
            std::clamp(
                history[i],
                0.0,
                scaleMaximum
            );

        points[i].X =
            static_cast<Gdiplus::REAL>(
                x +
                i *
                static_cast<double>(width) /
                (history.size() - 1)
            );

        points[i].Y =
            static_cast<Gdiplus::REAL>(
                y +
                height -
                (value / scaleMaximum) *
                height
            );
    }

    Gdiplus::GraphicsPath linePath;

    if (points.size() == 2)
    {
        linePath.AddLine(points[0], points[1]);
    }
    else
    {
        linePath.AddCurve(
            points.data(),
            static_cast<INT>(points.size()),
            0.28f
        );
    }

    Gdiplus::GraphicsPath fillPath;
    fillPath.AddPath(&linePath, FALSE);

    fillPath.AddLine(
        points.back(),
        Gdiplus::PointF(
            static_cast<Gdiplus::REAL>(x + width),
            static_cast<Gdiplus::REAL>(y + height)
        )
    );

    fillPath.AddLine(
        static_cast<Gdiplus::REAL>(x + width),
        static_cast<Gdiplus::REAL>(y + height),
        static_cast<Gdiplus::REAL>(x),
        static_cast<Gdiplus::REAL>(y + height)
    );

    fillPath.AddLine(
        Gdiplus::PointF(
            static_cast<Gdiplus::REAL>(x),
            static_cast<Gdiplus::REAL>(y + height)
        ),
        points.front()
    );

    fillPath.CloseFigure();

    Gdiplus::LinearGradientBrush fillBrush(
        Gdiplus::PointF(
            static_cast<Gdiplus::REAL>(x),
            static_cast<Gdiplus::REAL>(y)
        ),
        Gdiplus::PointF(
            static_cast<Gdiplus::REAL>(x),
            static_cast<Gdiplus::REAL>(y + height)
        ),
        Gdiplus::Color(
            92,
            GetRValue(accent),
            GetGValue(accent),
            GetBValue(accent)
        ),
        Gdiplus::Color(
            7,
            GetRValue(accent),
            GetGValue(accent),
            GetBValue(accent)
        )
    );

    graphics.SetClip(
        Gdiplus::Rect(
            x,
            y,
            width,
            height
        )
    );

    graphics.FillPath(
        &fillBrush,
        &fillPath
    );

    Gdiplus::Pen glowPen(
        Gdiplus::Color(
            72,
            GetRValue(accent),
            GetGValue(accent),
            GetBValue(accent)
        ),
        5.0f
    );

    glowPen.SetLineJoin(Gdiplus::LineJoinRound);
    glowPen.SetStartCap(Gdiplus::LineCapRound);
    glowPen.SetEndCap(Gdiplus::LineCapRound);

    graphics.DrawPath(
        &glowPen,
        &linePath
    );

    Gdiplus::Pen linePen(
        Gdiplus::Color(
            255,
            GetRValue(accent),
            GetGValue(accent),
            GetBValue(accent)
        ),
        1.9f
    );

    linePen.SetLineJoin(Gdiplus::LineJoinRound);
    linePen.SetStartCap(Gdiplus::LineCapRound);
    linePen.SetEndCap(Gdiplus::LineCapRound);

    graphics.DrawPath(
        &linePen,
        &linePath
    );
}



// --------------------------------------------------------
// PROCESSES PAGE - FAST SCROLL SUPPORT
// --------------------------------------------------------
// Scrolling used to repaint the entire high-resolution application for every
// wheel/drag event. Keep the prepared process list briefly cached and provide
// a table-only renderer so scrolling only redraws the region that changed.
static std::string lowerProcessText(
    const std::string& value)
{
    std::string result = value;

    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        }
    );

    return result;
}


static std::string friendlyProcessName(
    const std::string& executableName)
{
    const std::string lower =
        lowerProcessText(executableName);

    if (lower == "chrome.exe") return "Google Chrome";
    if (lower == "msedge.exe") return "Microsoft Edge";
    if (lower == "code.exe") return "Visual Studio Code";
    if (lower == "discord.exe") return "Discord";
    if (lower == "spotify.exe") return "Spotify";
    if (lower == "explorer.exe") return "Windows Explorer";
    if (lower == "searchhost.exe") return "Windows Search";
    if (lower == "startmenuexperiencehost.exe") return "Start";
    if (lower == "shellexperiencehost.exe") return "Windows Shell Experience Host";
    if (lower == "runtimebroker.exe") return "Runtime Broker";
    if (lower == "applicationframehost.exe") return "Application Frame Host";
    if (lower == "msedgewebview2.exe") return "Microsoft Edge WebView2";
    if (lower == "steam.exe") return "Steam";
    if (lower == "steamwebhelper.exe") return "Steam Client WebHelper";
    if (lower == "msmpeng.exe") return "Antimalware Service Executable";
    if (lower == "taskmgr.exe") return "Task Manager";

    std::string result = executableName;

    if (
        result.size() > 4 &&
        lower.substr(lower.size() - 4) == ".exe"
    )
    {
        result.resize(result.size() - 4);
    }

    return result;
}


static DWORD sameExecutableRootPid(
    const ProcessInfo& process,
    const std::unordered_map<DWORD, const ProcessInfo*>& byPid)
{
    DWORD root = process.pid;
    const ProcessInfo* current = &process;
    const std::string familyName =
        lowerProcessText(process.name);

    // Prevent malformed parent loops from ever blocking the UI.
    for (int depth = 0; depth < 32; depth++)
    {
        auto parent = byPid.find(current->parentPid);

        if (parent == byPid.end() || parent->second == nullptr)
        {
            break;
        }

        if (lowerProcessText(parent->second->name) != familyName)
        {
            break;
        }

        root = parent->second->pid;
        current = parent->second;
    }

    return root;
}


static const std::vector<DWORD>& visibleApplicationWindowPids()
{
    static std::vector<DWORD> cachedPids;
    static ULONGLONG refreshedAt = 0;

    const ULONGLONG now = GetTickCount64();

    if (refreshedAt != 0 && now - refreshedAt < 1000)
    {
        return cachedPids;
    }

    cachedPids.clear();

    EnumWindows(
        [](HWND window, LPARAM context) -> BOOL
        {
            if (!IsWindowVisible(window))
            {
                return TRUE;
            }

            if (GetWindow(window, GW_OWNER) != nullptr)
            {
                return TRUE;
            }

            if (GetWindowTextLengthW(window) <= 0)
            {
                return TRUE;
            }

            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);

            if (pid == 0)
            {
                return TRUE;
            }

            auto* output =
                reinterpret_cast<std::vector<DWORD>*>(context);

            if (
                std::find(output->begin(), output->end(), pid) ==
                output->end()
            )
            {
                output->push_back(pid);
            }

            return TRUE;
        },
        reinterpret_cast<LPARAM>(&cachedPids)
    );

    refreshedAt = now;
    return cachedPids;
}


static bool isWindowsSystemProcess(
    const ProcessInfo& process)
{
    if (process.pid == 0 || process.pid == 4)
    {
        return true;
    }

    const std::string lowerName =
        lowerProcessText(process.name);

    static const char* knownWindowsProcesses[] =
    {
        "system",
        "registry",
        "smss.exe",
        "csrss.exe",
        "wininit.exe",
        "winlogon.exe",
        "services.exe",
        "lsass.exe",
        "svchost.exe",
        "dwm.exe",
        "fontdrvhost.exe",
        "sihost.exe",
        "ctfmon.exe",
        "audiodg.exe",
        "securityhealthservice.exe",
        "memory compression"
    };

    for (const char* known : knownWindowsProcesses)
    {
        if (lowerName == known)
        {
            return true;
        }
    }

    const std::wstring path =
        getProcessExecutablePath(process.pid);

    if (path.empty())
    {
        return false;
    }

    static std::wstring windowsDirectoryLower;

    if (windowsDirectoryLower.empty())
    {
        wchar_t directory[MAX_PATH] = {};

        if (GetWindowsDirectoryW(directory, MAX_PATH) > 0)
        {
            windowsDirectoryLower = directory;

            std::transform(
                windowsDirectoryLower.begin(),
                windowsDirectoryLower.end(),
                windowsDirectoryLower.begin(),
                [](wchar_t c)
                {
                    return static_cast<wchar_t>(std::towlower(c));
                }
            );

            if (
                !windowsDirectoryLower.empty() &&
                windowsDirectoryLower.back() != L'\\'
            )
            {
                windowsDirectoryLower.push_back(L'\\');
            }
        }
    }

    std::wstring pathLower = path;

    std::transform(
        pathLower.begin(),
        pathLower.end(),
        pathLower.begin(),
        [](wchar_t c)
        {
            return static_cast<wchar_t>(std::towlower(c));
        }
    );

    return
        !windowsDirectoryLower.empty() &&
        pathLower.rfind(windowsDirectoryLower, 0) == 0;
}


static void drawProcessSortHeader(HDC hdc, const char* label, int x, int y,
    int arrowOffset, ProcessSort column, COLORREF normalColor)
{
    const bool active = processSort == column;
    const COLORREF color = active ? uiColor(RGB(0, 200, 255)) : normalColor;
    drawText(hdc, label, x, y, color, smallFont);
    if (!active) return;
    ScopedUiGraphics scoped(hdc, x + arrowOffset + 4.0f, true);
    auto& graphics = scoped.get();
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));
    const float left = static_cast<float>(x + arrowOffset), top = static_cast<float>(y + 5);
    Gdiplus::PointF points[] = {
        {left, top + (processSortDescending ? 0.f : 6.f)},
        {left + 8.f, top + (processSortDescending ? 0.f : 6.f)},
        {left + 4.f, top + (processSortDescending ? 6.f : 0.f)}
    };
    graphics.FillPolygon(&brush, points, 3);
}

static bool processSortLess(
    const ProcessInfo& a,
    const ProcessInfo& b,
    const std::string& aDisplay = "",
    const std::string& bDisplay = "")
{
    switch (processSort)
    {
    case ProcessSort::Name:
    {
        const std::string left =
            lowerProcessText(aDisplay.empty() ? a.name : aDisplay);
        const std::string right =
            lowerProcessText(bDisplay.empty() ? b.name : bDisplay);
        return processSortDescending ? left > right : left < right;
    }
    case ProcessSort::CPU:
        return processSortDescending
            ? a.cpuPercent > b.cpuPercent
            : a.cpuPercent < b.cpuPercent;
    case ProcessSort::Memory:
        return processSortDescending
            ? a.memoryMB > b.memoryMB
            : a.memoryMB < b.memoryMB;
    case ProcessSort::Threads:
        return processSortDescending
            ? a.threadCount > b.threadCount
            : a.threadCount < b.threadCount;
    case ProcessSort::PID:
        return processSortDescending
            ? a.pid > b.pid
            : a.pid < b.pid;
    }

    return false;
}


static const std::vector<ProcessInfo>& getPreparedProcessListForUi(
    int& totalProcessCount)
{
    static std::vector<ProcessInfo> cachedProcesses;
    static ULONGLONG cachedAt = 0;
    static std::string cachedSearch;
    static ProcessSort cachedSort = ProcessSort::Memory;
    static bool cachedDescending = true;
    static ProcessFilterMode cachedFilter = ProcessFilterMode::All;
    static int cachedTotalCount = 0;

    const ULONGLONG now = GetTickCount64();

    if (
        cachedAt != 0 &&
        (gFastNavigationRender || now - cachedAt < 450) &&
        cachedSearch == processSearch &&
        cachedSort == processSort &&
        cachedDescending == processSortDescending &&
        cachedFilter == processFilterMode
    )
    {
        totalProcessCount = cachedTotalCount;
        return cachedProcesses;
    }

    cachedProcesses = getCachedRunningProcesses();

    cachedProcesses.erase(
        std::remove_if(
            cachedProcesses.begin(),
            cachedProcesses.end(),
            [](const ProcessInfo& process)
            {
                return process.pid == 0;
            }
        ),
        cachedProcesses.end()
    );

    cachedTotalCount = static_cast<int>(cachedProcesses.size());

    // Build the parent lookup before filtering so Apps can retain all of an
    // application's helper/renderer processes when its root owns a window.
    std::unordered_map<DWORD, const ProcessInfo*> allByPid;

    for (const ProcessInfo& process : cachedProcesses)
    {
        allByPid[process.pid] = &process;
    }

    const std::vector<DWORD>& windowPids =
        visibleApplicationWindowPids();

    std::unordered_set<DWORD> appRoots;

    for (DWORD windowPid : windowPids)
    {
        auto found = allByPid.find(windowPid);

        if (found != allByPid.end() && found->second != nullptr)
        {
            appRoots.insert(
                sameExecutableRootPid(*found->second, allByPid)
            );
        }
    }

    if (processFilterMode != ProcessFilterMode::All)
    {
        std::vector<ProcessInfo> filteredProcesses;
        filteredProcesses.reserve(cachedProcesses.size());

        for (const ProcessInfo& process : cachedProcesses)
        {
            const DWORD root =
                sameExecutableRootPid(process, allByPid);

            const bool app =
                appRoots.find(root) != appRoots.end();

            const bool windows =
                isWindowsSystemProcess(process);

            bool include = true;

            switch (processFilterMode)
            {
            case ProcessFilterMode::Apps:
                include = app;
                break;
            case ProcessFilterMode::Background:
                include = !app && !windows;
                break;
            case ProcessFilterMode::Windows:
                include = windows;
                break;
            case ProcessFilterMode::All:
            default:
                include = true;
                break;
            }

            if (include)
            {
                filteredProcesses.push_back(process);
            }
        }

        cachedProcesses.swap(filteredProcesses);
    }

    if (!processSearch.empty())
    {
        const std::string searchLower =
            lowerProcessText(processSearch);

        cachedProcesses.erase(
            std::remove_if(
                cachedProcesses.begin(),
                cachedProcesses.end(),
                [&](const ProcessInfo& process)
                {
                    const std::string nameLower =
                        lowerProcessText(process.name);

                    const std::string friendlyLower =
                        lowerProcessText(
                            friendlyProcessName(process.name)
                        );

                    return
                        nameLower.find(searchLower) == std::string::npos &&
                        friendlyLower.find(searchLower) == std::string::npos;
                }
            ),
            cachedProcesses.end()
        );
    }

    std::sort(
        cachedProcesses.begin(),
        cachedProcesses.end(),
        [](const ProcessInfo& a, const ProcessInfo& b)
        {
            return processSortLess(a, b);
        }
    );

    cachedAt = now;
    cachedSearch = processSearch;
    cachedSort = processSort;
    cachedDescending = processSortDescending;
    cachedFilter = processFilterMode;
    totalProcessCount = cachedTotalCount;
    return cachedProcesses;
}


struct ProcessDisplayRow
{
    ProcessInfo stats = {};
    std::string displayName;
    bool groupHeader = false;
    bool child = false;
    DWORD groupKey = 0;
    int groupCount = 1;
};


static bool isProcessGroupExpanded(DWORD groupKey)
{
    return
        std::find(
            expandedProcessGroupKeys.begin(),
            expandedProcessGroupKeys.end(),
            groupKey
        ) != expandedProcessGroupKeys.end();
}


static std::vector<ProcessDisplayRow> buildProcessDisplayRows(
    const std::vector<ProcessInfo>& processes)
{
    std::vector<ProcessDisplayRow> rows;

    if (processes.empty())
    {
        return rows;
    }

    std::unordered_map<DWORD, const ProcessInfo*> byPid;

    for (const ProcessInfo& process : processes)
    {
        byPid[process.pid] = &process;
    }

    std::map<DWORD, std::vector<ProcessInfo>> grouped;

    for (const ProcessInfo& process : processes)
    {
        const DWORD root =
            sameExecutableRootPid(process, byPid);

        grouped[root].push_back(process);
    }

    struct TopEntry
    {
        DWORD groupKey = 0;
        std::vector<ProcessInfo> members;
        ProcessInfo aggregate = {};
        std::string displayName;
    };

    std::vector<TopEntry> entries;
    entries.reserve(grouped.size());

    for (auto& pair : grouped)
    {
        TopEntry entry;
        entry.groupKey = pair.first;
        entry.members = std::move(pair.second);

        const ProcessInfo* rootProcess = nullptr;

        for (const ProcessInfo& member : entry.members)
        {
            if (member.pid == entry.groupKey)
            {
                rootProcess = &member;
                break;
            }
        }

        if (rootProcess == nullptr)
        {
            rootProcess = &entry.members.front();
            entry.groupKey = rootProcess->pid;
        }

        entry.aggregate = *rootProcess;
        entry.aggregate.cpuPercent = -1.0;
        entry.aggregate.memoryMB = 0.0;
        entry.aggregate.threadCount = 0;
        entry.aggregate.handleCount = 0;

        bool hasCpu = false;
        double cpuTotal = 0.0;

        for (const ProcessInfo& member : entry.members)
        {
            if (member.cpuPercent >= 0.0)
            {
                cpuTotal += member.cpuPercent;
                hasCpu = true;
            }

            if (member.memoryMB >= 0.0)
            {
                entry.aggregate.memoryMB += member.memoryMB;
            }

            entry.aggregate.threadCount += member.threadCount;
            entry.aggregate.handleCount += member.handleCount;
        }

        if (hasCpu)
        {
            entry.aggregate.cpuPercent =
                std::clamp(cpuTotal, 0.0, 100.0);
        }

        entry.displayName =
            friendlyProcessName(rootProcess->name);

        entries.push_back(std::move(entry));
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const TopEntry& a, const TopEntry& b)
        {
            return processSortLess(
                a.aggregate,
                b.aggregate,
                a.displayName,
                b.displayName
            );
        }
    );

    for (TopEntry& entry : entries)
    {
        const bool groupedEntry =
            entry.members.size() > 1;

        if (!groupedEntry)
        {
            ProcessDisplayRow row;
            row.stats = entry.members.front();
            row.displayName =
                friendlyProcessName(row.stats.name);
            rows.push_back(std::move(row));
            continue;
        }

        ProcessDisplayRow header;
        header.stats = entry.aggregate;
        header.displayName =
            entry.displayName +
            " (" +
            std::to_string(entry.members.size()) +
            ")";
        header.groupHeader = true;
        header.groupKey = entry.groupKey;
        header.groupCount =
            static_cast<int>(entry.members.size());
        rows.push_back(std::move(header));

        if (!isProcessGroupExpanded(entry.groupKey))
        {
            continue;
        }

        std::sort(
            entry.members.begin(),
            entry.members.end(),
            [](const ProcessInfo& a, const ProcessInfo& b)
            {
                return processSortLess(a, b);
            }
        );

        for (const ProcessInfo& childProcess : entry.members)
        {
            ProcessDisplayRow child;
            child.stats = childProcess;
            child.displayName =
                friendlyProcessName(childProcess.name);
            child.child = true;
            child.groupKey = entry.groupKey;
            rows.push_back(std::move(child));
        }
    }

    return rows;
}


// Shared cyan-accent style for every scrollable page. Geometry stays in
// logical coordinates so the existing render transform handles display scale.
static void drawAccentScrollbar(HDC hdc, int left, int right, int top,
    int bottom, int thumbTop, int thumbBottom, bool dragging = false)
{
    const int width = right - left;
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    HGDIOBJ oldBrush = SelectObject(hdc, getCachedSolidBrush(uiColor(RGB(34, 45, 56))));
    RoundRect(hdc, left, top, right, bottom, width, width);
    SelectObject(hdc, getCachedSolidBrush(dragging ? uiColor(RGB(113, 221, 250)) : uiColor(RGB(39, 188, 233))));
    RoundRect(hdc, left, thumbTop, right, thumbBottom, width, width);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
}

static void drawProcessScrollbarUi(
    HDC hdc,
    int tableRight,
    int rowsTop,
    int visibleRows,
    int rowHeight,
    int processCount)
{
    // Keep the cyan thumb visible whenever the process list can scroll.
    const int contentBottom =
        rowsTop + visibleRows * rowHeight - 2;

    const int trackTop = rowsTop;
    const int trackBottom = contentBottom;

    const bool canScroll =
        processCount > visibleRows;

    const int trackHeight =
        (std::max)(1, trackBottom - trackTop);

    if (!canScroll)
    {
        processScrollbarThumbTop = trackTop;
        processScrollbarThumbBottom = trackBottom;
        return;
    }

    double visibleRatio =
        static_cast<double>(visibleRows) /
        static_cast<double>(processCount);

    int thumbHeight =
        static_cast<int>(
            trackHeight * visibleRatio
        );

    // Similar to the narrow Windows scrollbar in the reference screenshot.
    thumbHeight =
        std::clamp(
            thumbHeight,
            34,
            trackHeight
        );

    const int thumbTravel =
        trackHeight - thumbHeight;

    int thumbTop = trackTop;

    if (processMaxScrollOffset > 0)
    {
        const double ratio =
            static_cast<double>(processScrollOffset) /
            static_cast<double>(processMaxScrollOffset);

        thumbTop +=
            static_cast<int>(
                ratio * thumbTravel + 0.5
            );
    }

    processScrollbarThumbTop = thumbTop;
    processScrollbarThumbBottom =
        thumbTop + thumbHeight;

    drawAccentScrollbar(hdc, tableRight - 10, tableRight - 2,
        trackTop, trackBottom, processScrollbarThumbTop,
        processScrollbarThumbBottom, processScrollbarDragging);
}

static void drawFastProcessTable(HDC hdc)
{
    const int tableLeft = 20;
    const int tableTop = 275;
    const int tableRight = 700;
    const int tableBottom = 690;
    const int visibleRows = 12;
    const int rowHeight = 25;
    const int rowsTop = tableTop + 88;

    const COLORREF processPanel = uiColor(RGB(16, 26, 35));
    const COLORREF textPrimary = uiColor(RGB(240, 242, 245));
    const COLORREF textSecondary = uiColor(RGB(150, 157, 170));
    const COLORREF cyanAccent = uiColor(RGB(36, 200, 250));
    const COLORREF runningGreen = uiColor(RGB(68, 225, 126));

    int totalProcessCount = 0;
    const std::vector<ProcessInfo>& processes =
        getPreparedProcessListForUi(totalProcessCount);

    const std::vector<ProcessDisplayRow> displayRows =
        buildProcessDisplayRows(processes);

    const int maxOffset =
        (std::max)(0, static_cast<int>(displayRows.size()) - visibleRows);

    processMaxScrollOffset = maxOffset;
    processScrollOffset = std::clamp(processScrollOffset, 0, maxOffset);

    drawRoundedBox(hdc, tableLeft, tableTop, tableRight, tableBottom, processPanel);

    drawTintedPngImage(
        hdc,
        L"sidebar_processes.png",
        tableLeft + 16,
        tableTop + 16,
        18,
        18,
        uiColor(RGB(225, 232, 239))
    );

    drawText(hdc, "Running Processes", tableLeft + 43, tableTop + 14, textPrimary, labelFont);

    const bool filtered =
        !processSearch.empty() ||
        processFilterMode != ProcessFilterMode::All;

    std::string countText = filtered
        ? std::to_string(processes.size()) + " of " + std::to_string(totalProcessCount)
        : std::to_string(totalProcessCount) + " processes";

    drawText(hdc, countText, tableRight - 105, tableTop + 16, textSecondary, smallFont);

    const int headerY = tableTop + 54;
    drawProcessSortHeader(hdc, "Name", tableLeft + 16, headerY, 48, ProcessSort::Name, textSecondary);
    drawProcessSortHeader(hdc, "PID", tableLeft + 280, headerY, 32, ProcessSort::PID, textSecondary);
    drawProcessSortHeader(hdc, "CPU", tableLeft + 355, headerY, 36, ProcessSort::CPU, textSecondary);
    drawProcessSortHeader(hdc, "Memory", tableLeft + 420, headerY, 58, ProcessSort::Memory, textSecondary);
    drawProcessSortHeader(hdc, "Threads", tableLeft + 520, headerY, 60, ProcessSort::Threads, textSecondary);
    drawText(hdc, "Status", tableLeft + 600, headerY, textSecondary, smallFont);

    HPEN linePen = CreatePen(PS_SOLID, 1, uiColor(RGB(33, 44, 58)));
    HGDIOBJ oldLinePen = SelectObject(hdc, linePen);
    MoveToEx(hdc, tableLeft + 14, tableTop + 78, nullptr);
    LineTo(hdc, tableRight - 15, tableTop + 78);
    SelectObject(hdc, oldLinePen);
    DeleteObject(linePen);

    const int endIndex =
        (std::min)(processScrollOffset + visibleRows, static_cast<int>(displayRows.size()));

    visibleProcessPids.clear();
    visibleProcessGroupKeys.clear();

    for (int i = processScrollOffset; i < endIndex; ++i)
    {
        const ProcessDisplayRow& row = displayRows[i];
        const ProcessInfo& process = row.stats;
        const int rowIndex = i - processScrollOffset;
        const int rowY = rowsTop + rowIndex * rowHeight;

        visibleProcessPids.push_back(process.pid);
        visibleProcessGroupKeys.push_back(
            row.groupHeader ? row.groupKey : 0
        );

        if (process.pid == hoveredProcessPid && process.pid != selectedProcessPid)
        {
            drawRoundedBox(hdc, tableLeft + 10, rowY - 4, tableRight - 16, rowY + 20, uiColor(RGB(23, 33, 44)));
        }

        if (process.pid == selectedProcessPid)
        {
            drawRoundedBox(hdc, tableLeft + 10, rowY - 4, tableRight - 16, rowY + 20, uiColor(RGB(20, 55, 79)));
            drawRoundedBox(hdc, tableLeft + 10, rowY - 4, tableLeft + 14, rowY + 20, cyanAccent);
        }

        int iconX = tableLeft + 18;
        int nameX = tableLeft + 42;

        if (row.groupHeader)
        {
            const bool expanded = isProcessGroupExpanded(row.groupKey);

            drawText(
                hdc,
                expanded ? "v" : ">",
                tableLeft + 17,
                rowY,
                uiColor(RGB(185, 197, 208)),
                smallFont
            );

            iconX = tableLeft + 35;
            nameX = tableLeft + 59;
        }
        else if (row.child)
        {
            iconX = tableLeft + 38;
            nameX = tableLeft + 62;
        }

        drawProcessExecutableIconCachedOnly(hdc, process.pid, iconX, rowY, 16);

        std::string name = row.displayName;
        if (name.size() > 24) name = name.substr(0, 21) + "...";
        drawText(hdc, name, nameX, rowY, textPrimary, smallFont);

        drawText(
            hdc,
            row.groupHeader ? "--" : std::to_string(process.pid),
            tableLeft + 280,
            rowY,
            textSecondary,
            smallFont
        );

        std::ostringstream cpuStream;
        if (process.cpuPercent >= 0.0)
            cpuStream << std::fixed << std::setprecision(1) << process.cpuPercent << "%";
        else
            cpuStream << "--";
        drawText(hdc, cpuStream.str(), tableLeft + 355, rowY, textSecondary, smallFont);

        std::ostringstream memoryStream;
        if (process.memoryMB >= 0.0)
            memoryStream << std::fixed << std::setprecision(0) << process.memoryMB << " MB";
        else
            memoryStream << "--";
        drawText(hdc, memoryStream.str(), tableLeft + 420, rowY, textSecondary, smallFont);
        drawText(hdc, std::to_string(process.threadCount), tableLeft + 520, rowY, textSecondary, smallFont);

        HBRUSH statusBrush = getCachedSolidBrush(runningGreen);
        HGDIOBJ oldBrush = SelectObject(hdc, statusBrush);
        HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
        drawUiEllipse(hdc, tableLeft + 600, rowY + 5, tableLeft + 607, rowY + 12);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        drawText(hdc, "Running", tableLeft + 613, rowY, runningGreen, smallFont);
    }

    if (displayRows.empty())
    {
        drawText(
            hdc,
            "No running processes matched the current filter.",
            tableLeft + 18,
            rowsTop + 10,
            textSecondary,
            smallFont
        );
    }

    drawProcessScrollbarUi(
        hdc,
        tableRight,
        rowsTop,
        visibleRows,
        rowHeight,
        static_cast<int>(displayRows.size())
    );
}

// Dashboard's older three-column grid has a shorter content extent than
// the newer pages. Expand that grid to the same right-hand margin.
class ScopedDashboardWidth
{
public:
    explicit ScopedDashboardWidth(HDC dc) : dc_(dc), oldScaleX_(gSysMonUiRenderScaleX)
    {
        active_ = currentPage == AppPage::Dashboard &&
            gSysMonUiRenderScaleX > gSysMonUiRenderScale + 0.001f;
        if (active_)
        {
            GetWorldTransform(dc_, &oldTransform_);
            XFORM expanded = oldTransform_;
            expanded.eM11 *= sysMonDashboardWideFactor;
            SetWorldTransform(dc_, &expanded);
            gSysMonUiRenderScaleX *= sysMonDashboardWideFactor;
        }
    }
    ~ScopedDashboardWidth()
    {
        if (active_) SetWorldTransform(dc_, &oldTransform_);
        gSysMonUiRenderScaleX = oldScaleX_;
    }
private:
    HDC dc_;
    float oldScaleX_;
    bool active_ = false;
    XFORM oldTransform_ = {};
};

void drawDashboard(
    HWND hwnd,
    HDC hdc)
{
    if (gProcessFastScrollRender && currentPage == AppPage::Processes)
    {
        POINT oldFastOrigin = {};
        SetViewportOrgEx(
            hdc,
            static_cast<int>(165 * gSysMonUiRenderScaleX + 0.5f),
            0,
            &oldFastOrigin
        );

        drawFastProcessTable(hdc);

        SetViewportOrgEx(
            hdc,
            oldFastOrigin.x,
            oldFastOrigin.y,
            nullptr
        );
        return;
    }

    // Draw every page in SysMon's fixed logical coordinate space.
    // Main.cpp scales this completed frame to the current window size.
    RECT client =
    {
        0,
        0,
        sysMonDesignWidth,
        sysMonDesignHeight
    };

    COLORREF background =
        uiColor(RGB(18, 20, 26));

    COLORREF card =
        uiColor(RGB(29, 32, 40));

    COLORREF textPrimary =
        uiColor(RGB(240, 242, 245));

    COLORREF textSecondary =
        uiColor(RGB(150, 157, 170));

    COLORREF green =
        uiColor(RGB(70, 220, 140));


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
// TOP HEADER
// --------------------------------------------------------

RECT headerRect =
{
    0,
    0,
    client.right,
    58
};

HBRUSH headerBrush =
    CreateSolidBrush(
        uiColor(RGB(14, 18, 24))
    );

FillRect(
    hdc,
    &headerRect,
    headerBrush
);

DeleteObject(
    headerBrush
);


// Logo box
drawPngImage(
    hdc,
    L"logo.png",
    8,
    8,
    40,
    40
);

// App name
drawText(
    hdc,
    "SysMon",
    60,
    8,
    textPrimary,
    labelFont
);

drawText(
    hdc,
    "REAL-TIME SYSTEM MONITOR",
    60,
    32,
    textSecondary,
    smallFont
);


// --------------------------------------------------------
// TOP NAVIGATION
// --------------------------------------------------------

const COLORREF topNavActive =
    uiColor(RGB(65, 205, 245));

const COLORREF topNavInactive =
    uiColor(RGB(174, 185, 196));

const COLORREF topNavSelectedBackground =
    uiColor(RGB(25, 43, 58));


auto drawTopNavigation =
    [&](int left,
        int right,
        const std::string& text,
        const wchar_t* iconPath,
        bool selected)
{
    const int itemTop = 10;
    const int itemBottom = 48;
    const int iconSize = 18;
    const int iconGap = 7;

    COLORREF itemColor =
        selected
        ? topNavActive
        : topNavInactive;

    if (selected)
    {
        drawRoundedBox(
            hdc,
            left,
            itemTop,
            right,
            itemBottom,
            topNavSelectedBackground
        );
    }

    setFont(
        hdc,
        smallFont
    );

    SIZE textSize = {};

    GetTextExtentPoint32A(
        hdc,
        text.c_str(),
        static_cast<int>(text.size()),
        &textSize
    );

    bool hasIcon =
        getCachedPngImage(iconPath) !=
            nullptr;

    int contentWidth =
        textSize.cx;

    if (hasIcon)
    {
        contentWidth +=
            iconSize +
            iconGap;
    }

    int contentX =
        left +
        (
            (right - left) -
            contentWidth
        ) / 2;

    int textY =
        itemTop +
        (
            (itemBottom - itemTop) -
            textSize.cy
        ) / 2;

    if (hasIcon)
    {
        int iconY =
            itemTop +
            (
                (itemBottom - itemTop) -
                iconSize
            ) / 2;

        drawTintedPngImage(
            hdc,
            iconPath,
            contentX,
            iconY,
            iconSize,
            iconSize,
            itemColor
        );

        contentX +=
            iconSize +
            iconGap;
    }

    drawText(
        hdc,
        text,
        contentX,
        textY,
        itemColor,
        smallFont
    );
};


drawTopNavigation(
    325,
    435,
    "Dashboard",
    L"dashboard.png",
    currentPage == AppPage::Dashboard
);

drawTopNavigation(
    443,
    553,
    "Processes",
    L"processes.png",
    currentPage == AppPage::Processes
);

drawTopNavigation(
    561,
    691,
    "Performance",
    L"performance.png",
    currentPage == AppPage::Performance
);

drawTopNavigation(
    699,
    785,
    "Tools",
    L"tools.png",
    currentPage == AppPage::Tools
);

drawTopNavigation(
    793,
    895,
    "Settings",
    L"settings.png",
    currentPage == AppPage::Settings
);
// --------------------------------------------------------
// SIDEBAR
// --------------------------------------------------------

const int sidebarLeft = 8;
const int sidebarTop = 60;
const int sidebarRight = 165;
const int sidebarBottom = 700;

// Dark reference-style sidebar panel with a subtle border.
{
    ScopedUiGraphics scopedGraphics(hdc);
    Gdiplus::Graphics& graphics =
        scopedGraphics.get();
    graphics.SetSmoothingMode(
        Gdiplus::SmoothingModeAntiAlias
    );

    Gdiplus::Rect panelRect(
        sidebarLeft,
        sidebarTop,
        sidebarRight - sidebarLeft,
        sidebarBottom - sidebarTop
    );

    Gdiplus::GraphicsPath panelPath;
    const int radius = 16;

    panelPath.AddArc(
        panelRect.X,
        panelRect.Y,
        radius,
        radius,
        180,
        90
    );

    panelPath.AddArc(
        panelRect.GetRight() - radius,
        panelRect.Y,
        radius,
        radius,
        270,
        90
    );

    panelPath.AddArc(
        panelRect.GetRight() - radius,
        panelRect.GetBottom() - radius,
        radius,
        radius,
        0,
        90
    );

    panelPath.AddArc(
        panelRect.X,
        panelRect.GetBottom() - radius,
        radius,
        radius,
        90,
        90
    );

    panelPath.CloseFigure();

    Gdiplus::LinearGradientBrush panelBrush(
        Gdiplus::Point(
            panelRect.X,
            panelRect.Y
        ),
        Gdiplus::Point(
            panelRect.GetRight(),
            panelRect.GetBottom()
        ),
        themeGdiColor(255, 17, 25, 34),
        themeGdiColor(255, 12, 18, 26)
    );

    graphics.FillPath(
        &panelBrush,
        &panelPath
    );

    Gdiplus::Pen borderPen(
        themeGdiColor(120, 38, 51, 65),
        1.0f
    );

    graphics.DrawPath(
        &borderPen,
        &panelPath
    );
}


// --------------------------------------------------------
// SIDEBAR NAVIGATION
// --------------------------------------------------------

const COLORREF sidebarActiveColor =
    uiColor(RGB(69, 197, 241));

const COLORREF sidebarInactiveColor =
    uiColor(RGB(175, 187, 199));

const COLORREF sidebarSelectedBackground =
    uiColor(RGB(34, 48, 62));


auto drawSidebarItem =
    [&](int top,
        const std::string& text,
        const wchar_t* iconPath,
        bool selected)
{
    const int itemLeft = 14;
    const int itemRight = 155;
    const int itemHeight = 36;
    const int iconX = 25;
    const int iconSize = 16;
    const int textX = 52;

    COLORREF itemColor =
        selected
        ? sidebarActiveColor
        : sidebarInactiveColor;

    if (selected)
    {
        drawRoundedBox(
            hdc,
            itemLeft,
            top,
            itemRight,
            top + itemHeight,
            sidebarSelectedBackground
        );

        drawRoundedBox(
            hdc,
            8,
            top + 1,
            12,
            top + itemHeight - 1,
            sidebarActiveColor
        );
    }

    bool iconAvailable =
        iconPath != nullptr &&
        uiAssetExists(iconPath);

    if (iconAvailable)
    {
        drawTintedPngImage(
            hdc,
            iconPath,
            iconX,
            top + 10,
            iconSize,
            iconSize,
            itemColor
        );
    }
    else
    {
        // Small fallback marker until the matching PNG is added.
        HPEN fallbackPen =
            CreatePen(
                PS_SOLID,
                1,
                itemColor
            );

        HGDIOBJ oldFallbackPen =
            SelectObject(
                hdc,
                fallbackPen
            );

        HGDIOBJ oldFallbackBrush =
            SelectObject(
                hdc,
                GetStockObject(HOLLOW_BRUSH)
            );

        drawUiEllipse(
            hdc,
            iconX + 3,
            top + 13,
            iconX + 13,
            top + 23
        );

        SelectObject(
            hdc,
            oldFallbackBrush
        );

        SelectObject(
            hdc,
            oldFallbackPen
        );

        DeleteObject(
            fallbackPen
        );
    }

    drawText(
        hdc,
        text,
        textX,
        top + 10,
        itemColor,
        smallFont
    );
};


drawSidebarItem(
    72,
    "Overview",
    L"sidebar_overview.png",
    currentPage == AppPage::Dashboard
);

drawSidebarItem(
    112,
    "CPU",
    L"sidebar_cpu.png",
    currentPage == AppPage::Performance &&
    performanceView == PerformanceView::CPU
);

drawSidebarItem(
    152,
    "Memory",
    L"sidebar_memory.png",
    currentPage == AppPage::Performance &&
    performanceView == PerformanceView::Memory
);

drawSidebarItem(
    192,
    "Disk",
    L"sidebar_disk.png",
    currentPage == AppPage::Performance &&
    performanceView == PerformanceView::Disk
);

drawSidebarItem(
    232,
    "GPU",
    L"sidebar_gpu.png",
    currentPage == AppPage::Performance &&
    performanceView == PerformanceView::GPU
);

drawSidebarItem(
    272,
    "Network",
    L"sidebar_network.png",
    currentPage == AppPage::Performance &&
    performanceView == PerformanceView::Network
);

drawSidebarItem(
    312,
    "Temperatures",
    L"sidebar_temperatures.png",
    currentPage == AppPage::Temperatures
);

// Processes is available from the top navigation, so the left sidebar
// stays focused on resource pages.
drawSidebarItem(
    352,
    "System Info",
    L"sidebar_systeminfo.png",
    currentPage == AppPage::SystemInfo
);


drawSidebarItem(
    392,
    "Settings",
    L"sidebar_settings.png",
    currentPage == AppPage::Settings
);


// --------------------------------------------------------
// SIDEBAR FOOTER
// Reference-style compact branding block.
// --------------------------------------------------------

const COLORREF sidebarFooterPrimary =
    uiColor(RGB(118, 130, 145));

const COLORREF sidebarFooterSecondary =
    uiColor(RGB(96, 109, 125));

const int sidebarFooterX = 30;

drawText(
    hdc,
    "Prof System Monitor",
    sidebarFooterX,
    602,
    sidebarFooterPrimary,
    smallFont
);

drawText(
    hdc,
    SYSMON_VERSION_DISPLAY,
    sidebarFooterX,
    620,
    sidebarFooterSecondary,
    smallFont
);

drawText(
    hdc,
    "Monitor today",
    sidebarFooterX,
    650,
    sidebarFooterSecondary,
    smallFont
);

drawText(
    hdc,
    "for a better tomorrow.",
    sidebarFooterX,
    668,
    sidebarFooterSecondary,
    smallFont
);

POINT oldOrigin;
int contentOriginX =
    currentPage == AppPage::Dashboard
        ? 165
        : (
            currentPage == AppPage::Processes ||
            currentPage == AppPage::SystemInfo ||
            currentPage == AppPage::Temperatures ||
            currentPage == AppPage::Tools ||
            currentPage == AppPage::Performance ||
            currentPage == AppPage::Settings
                ? 165
                : 225
          );

SetViewportOrgEx(
    hdc,
    static_cast<int>(contentOriginX * gSysMonUiRenderScaleX + 0.5f),
    0,
    &oldOrigin
);
ScopedDashboardWidth dashboardWidth(hdc);
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

    case AppPage::Tools:
        pageTitle = "Tools";
        pageSubtitle = "System utilities and maintenance tools.";
        break;

    case AppPage::SystemInfo:
        pageTitle = "About";
        pageSubtitle = "Detailed information about your computer hardware and software.";
        break;

    case AppPage::Settings:
        pageTitle = "Settings";
        pageSubtitle = "Customize your experience and system monitoring preferences.";
        break;

    default:
        break;
    }

    const bool drawStandardPageHeader =
        !(
            currentPage == AppPage::Processes ||
            currentPage == AppPage::SystemInfo ||
            currentPage == AppPage::Temperatures ||
            currentPage == AppPage::Tools ||
            currentPage == AppPage::Performance ||
            currentPage == AppPage::Settings
        );

    if (drawStandardPageHeader)
    {
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
    }

 if (currentPage != AppPage::Processes &&
    currentPage != AppPage::Performance &&
    currentPage != AppPage::Temperatures &&
    currentPage != AppPage::Tools &&
    currentPage != AppPage::SystemInfo &&
    currentPage != AppPage::Settings)
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

  // --------------------------------------------------------
  // SETTINGS PAGE
  // --------------------------------------------------------
  // Geometry lives in SettingsLayout (UI.h) so Main.cpp hit-tests exactly
  // what is painted here. The page header is laid out below the top
  // navigation strip instead of across it.
  if (currentPage == AppPage::Settings)
{
    namespace SL = SettingsLayout;
    SL::refreshDensity();
    settingsScrollOffset = (std::min)(settingsScrollOffset, SL::maxScrollOffset());
    using SettingsRect = SettingsLayout::Rect;

    const COLORREF panelColor = uiColor(RGB(14, 28, 43));
    const COLORREF panelBorder = uiColor(RGB(30, 58, 82));
    const COLORREF dividerColor = uiColor(RGB(26, 47, 66));
    const COLORREF fieldColor = uiColor(RGB(17, 35, 52));
    const COLORREF fieldBorder = uiColor(RGB(35, 64, 88));
    const COLORREF switchOff = uiColor(RGB(58, 76, 95));
    const COLORREF muted = uiColor(RGB(138, 154, 172));

    const COLORREF accentPalette[] = {
        themeSurfaceColor(RGB(45, 199, 255)), themeSurfaceColor(RGB(27, 207, 139)), themeSurfaceColor(RGB(165, 83, 255)),
        themeSurfaceColor(RGB(255, 132, 56)), themeSurfaceColor(RGB(255, 76, 91)), themeSurfaceColor(RGB(235, 57, 155))
    };

    const COLORREF accent =
        accentPalette[(std::max)(0, (std::min)(5, appSettings.accentColorIndex))];

    static HFONT settingsSectionFont = CreateFontA(18,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,"Segoe UI");
    static HFONT settingsRowFont = CreateFontA(15,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,"Segoe UI");
    static HFONT settingsDescriptionFont = CreateFontA(12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,"Segoe UI");
    static HFONT settingsControlFont = CreateFontA(13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,"Segoe UI");

    auto fitSettingsText =
        [&](const std::string& source,
            int maxWidth) -> std::string
    {
        if (source.empty() || maxWidth <= 0)
        {
            return std::string();
        }

        SIZE extent = {};
        setFont(hdc, settingsDescriptionFont);
        GetTextExtentPoint32A(
            hdc,
            source.c_str(),
            static_cast<int>(source.size()),
            &extent
        );

        if (extent.cx <= maxWidth)
        {
            return source;
        }

        std::string result = source;

        while (!result.empty())
        {
            result.pop_back();
            const std::string candidate = result + "...";

            GetTextExtentPoint32A(
                hdc,
                candidate.c_str(),
                static_cast<int>(candidate.size()),
                &extent
            );

            if (extent.cx <= maxWidth)
            {
                return candidate;
            }
        }

        return std::string("...");
    };

    auto strokeRoundRect =
        [&](int left,
            int top,
            int right,
            int bottom,
            int radius,
            COLORREF color)
    {
        HGDIOBJ oldPen = SelectObject(hdc, getCachedPen(color, 1));
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));

        RoundRect(hdc, left, top, right, bottom, radius, radius);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
    };

    auto drawPanel =
        [&](int column,
            int panelTop,
            int height,
            const std::string& title,
            const wchar_t* iconPath)
    {
        const int left = SL::columnLeft(column);
        const int right = SL::columnRight(column);

        drawRoundedBox(hdc, left, panelTop, right, panelTop + height, panelColor);
        strokeRoundRect(left, panelTop, right, panelTop + height, 16, panelBorder);

        const int headerX = left + SL::panelPadding;
        const int headerY = panelTop + SL::panelHeaderY;

        if (iconPath != nullptr && uiAssetExists(iconPath))
        {
            drawTintedPngImage(
                hdc, iconPath, headerX, headerY + 3, 18, 18, uiColor(RGB(226, 236, 246))
            );
        }
        else
        {
            drawRoundedBox(
                hdc, headerX + 4, headerY + 7, headerX + 13, headerY + 16,
                uiColor(RGB(191, 205, 219))
            );
        }

        drawText(hdc, title, headerX + 28, headerY, textPrimary, settingsSectionFont);

        HGDIOBJ oldDivider = SelectObject(hdc, getCachedPen(dividerColor, 1));
        MoveToEx(hdc, headerX, panelTop + SL::panelDividerY, nullptr);
        LineTo(hdc, right - SL::panelPadding, panelTop + SL::panelDividerY);
        SelectObject(hdc, oldDivider);
    };

    auto drawRowLabel =
        [&](int column,
            int panelTop,
            int index,
            const std::string& title,
            const std::string& subtitle)
    {
        const int x = SL::rowLeft(column);
        const int y = SL::rowTop(panelTop, index);

        drawText(hdc, title, x, y, textPrimary, settingsRowFont);

        if (!subtitle.empty())
        {
            drawText(hdc, subtitle, x, y + 20, muted, settingsDescriptionFont);
        }
    };

    auto drawField =
        [&](const SettingsRect& box)
    {
        drawRoundedBox(hdc, box.left, box.top, box.right, box.bottom, fieldColor);
        strokeRoundRect(box.left, box.top, box.right, box.bottom, 9, fieldBorder);
    };

    auto drawChevron =
        [&](int centerX,
            int centerY,
            COLORREF color)
    {
        HGDIOBJ oldPen = SelectObject(hdc, getCachedPen(color, 2));

        MoveToEx(hdc, centerX - 4, centerY - 2, nullptr);
        LineTo(hdc, centerX, centerY + 2);
        LineTo(hdc, centerX + 5, centerY - 3);

        SelectObject(hdc, oldPen);
    };

    auto drawCenteredControlText =
        [&](const SettingsRect& box,
            const std::string& text,
            COLORREF color)
    {
        SIZE size = {};
        setFont(hdc, settingsControlFont);
        GetTextExtentPoint32A(
            hdc, text.c_str(), static_cast<int>(text.size()), &size
        );

        drawText(
            hdc,
            text,
            box.left + ((box.right - box.left) - static_cast<int>(size.cx)) / 2,
            box.top + ((box.bottom - box.top) - static_cast<int>(size.cy)) / 2,
            color,
            settingsControlFont
        );
    };

    auto drawSwitchRow =
        [&](int column,
            int panelTop,
            int index,
            const std::string& title,
            const std::string& subtitle,
            bool enabled)
    {
        drawRowLabel(column, panelTop, index, title, subtitle);

        const SettingsRect box = SL::switchRect(column, panelTop, index);

        drawSmoothControlRect(
            hdc, box.left, box.top, box.right, box.bottom, 6.0f,
            enabled ? accent : switchOff
        );

        const int knobLeft = enabled ? box.right - 18 : box.left + 2;
        const float knobTop = (box.top + box.bottom) * 0.5f - 8.0f;
        drawSmoothControlRect(hdc, knobLeft, knobTop, knobLeft + 16,
            knobTop + 16, 3.5f, RGB(246, 250, 253));
    };

    auto drawComboRow =
        [&](int column,
            int panelTop,
            int index,
            const std::string& title,
            const std::string& subtitle,
            const std::string& value)
    {
        drawRowLabel(column, panelTop, index, title, subtitle);

        const SettingsRect box = SL::comboRect(column, panelTop, index);
        drawField(box);

        drawText(
            hdc, value, box.left + 12, box.top + 7, textPrimary, settingsControlFont
        );

        drawChevron(box.right - 14, box.top + 16, muted);
    };

    auto drawValueRow =
        [&](int column,
            int panelTop,
            int index,
            const std::string& title,
            const std::string& subtitle,
            int value,
            const std::string& suffix)
    {
        drawRowLabel(column, panelTop, index, title, subtitle);

        const SettingsRect box = SL::valueRect(column, panelTop, index);
        drawField(box);
        drawCenteredControlText(box, std::to_string(value), textPrimary);

        drawText(
            hdc, suffix, box.right + 10, box.top + 7, muted, settingsControlFont
        );
    };

    auto drawButton =
        [&](const SettingsRect& box,
            const std::string& text)
    {
        drawField(box);
        drawCenteredControlText(box, text, textPrimary);
    };

    auto drawButtonRow =
        [&](int column,
            int panelTop,
            int index,
            const std::string& title,
            const std::string& subtitle,
            const std::string& buttonText,
            int buttonWidth)
    {
        drawRowLabel(column, panelTop, index, title, subtitle);
        drawButton(SL::buttonRect(column, panelTop, index, buttonWidth), buttonText);
    };

    // ----------------------------------------------------
    // PAGE HEADER (fixed, below the top navigation strip)
    // ----------------------------------------------------
    drawRoundedBox(
        hdc,
        SL::contentLeft,
        SL::headerTop,
        SL::contentLeft + SL::headerIconSize,
        SL::headerTop + SL::headerIconSize,
        uiColor(RGB(24, 42, 60))
    );

    if (uiAssetExists(L"settings.png"))
    {
        drawTintedPngImage(
            hdc, L"settings.png",
            SL::contentLeft + 13, SL::headerTop + 13,
            26, 26, uiColor(RGB(235, 243, 249))
        );
    }

    drawText(
        hdc, "Settings", SL::headerTitleX, SL::headerTop + 4,
        textPrimary, mediumFont
    );

    drawText(
        hdc,
        "Customize your experience and system monitoring preferences.",
        SL::headerTitleX + 1,
        SL::headerTop + 36,
        textSecondary,
        subtitleFont
    );

    drawButton(SL::resetButton(), "Reset to Default");

    // ----------------------------------------------------
    // SCROLLING CARD AREA
    // ----------------------------------------------------
    settingsMaxScrollOffset = SL::maxScrollOffset();
    settingsScrollOffset =
        std::clamp(settingsScrollOffset, 0, settingsMaxScrollOffset);

    const int savedSettingsDc = SaveDC(hdc);

    IntersectClipRect(
        hdc, 0, SL::clipTop, SL::contentRight + 40, SL::clipBottom
    );

    // The viewport origin lives in device space, so the logical scroll
    // offset has to be scaled the same way the render transform scales
    // everything else. Without this the page drifts on resized windows.
    OffsetViewportOrgEx(
        hdc, 0, -scaleUiCoordinate(settingsScrollOffset), nullptr
    );

    // ---- General Settings -------------------------------
    drawPanel(0, SL::topRowTop, SL::panelHeight(5), "General Settings", L"settings.png");
    drawSwitchRow(0, SL::topRowTop, 0, "Start with Windows", "Launch SysMon on system startup.", appSettings.startWithWindows);
    drawSwitchRow(0, SL::topRowTop, 1, "Minimize to System Tray", "Minimize or close to the system tray.", appSettings.minimizeToTray);
    drawSwitchRow(0, SL::topRowTop, 2, "Check for Updates", Updates::status, appSettings.checkForUpdates);
    drawComboRow(0, SL::topRowTop, 3, "Theme", "Choose your preferred theme.", appSettings.theme==0 ? "Dark" : appSettings.theme==1 ? "Light" : "Windows");
    drawRowLabel(0, SL::topRowTop, 4, "Language", "English is currently supported.");
    drawCenteredControlText(SL::comboRect(0, SL::topRowTop, 4), "English", textPrimary);

    // ---- Monitoring Settings ----------------------------
    std::string intervalText = std::to_string(appSettings.updateIntervalMs) + " ms";

    if (appSettings.updateIntervalMs == 1000)
        intervalText = "1 second";
    else if (appSettings.updateIntervalMs == 2000)
        intervalText = "2 seconds";
    else if (appSettings.updateIntervalMs == 5000)
        intervalText = "5 seconds";
    else if (appSettings.updateIntervalMs == 10000)
        intervalText = "10 seconds";

    drawPanel(1, SL::topRowTop, SL::panelHeight(5), "Monitoring Settings", L"performance.png");
    drawComboRow(1, SL::topRowTop, 0, "Update Interval", "How often to refresh data.", intervalText);
    drawComboRow(1, SL::topRowTop, 1, "Temperature Unit", "Display temperatures in:", appSettings.temperatureUnit == 0 ? "Celsius" : "Fahrenheit");
    drawComboRow(1, SL::topRowTop, 2, "Network Unit", "Display network speed in:", appSettings.networkUnit == 0 ? "Mbps" : (appSettings.networkUnit == 1 ? "MB/s" : "Kbps"));
    drawSwitchRow(1, SL::topRowTop, 3, "Show in Tray", "Keep SysMon available in the tray.", appSettings.showInTray);
    drawSwitchRow(1, SL::topRowTop, 4, "Play Alerts", "Play a sound for system alerts.", appSettings.playAlerts);

    // ---- Appearance -------------------------------------
    drawPanel(2, SL::topRowTop, SL::panelHeight(5), "Appearance", L"sidebar_settings.png");
    drawRowLabel(2, SL::topRowTop, 0, "Accent Color", "Choose the accent color.");

    for (int index = 0; index < SL::swatchCount; index++)
    {
        const int cx = SL::swatchCenterX(2, index);
        const int cy = SL::swatchCenterY(SL::topRowTop);

        if (index == appSettings.accentColorIndex)
        {
            drawSmoothControlRect(hdc, cx - 11, cy - 11, cx + 11, cy + 11,
                5.0f, uiColor(RGB(235, 243, 249)));
            drawSmoothControlRect(hdc, cx - 9.5f, cy - 9.5f, cx + 9.5f, cy + 9.5f,
                4.0f, panelColor);
        }

        drawSmoothControlRect(hdc, cx - SL::swatchRadius, cy - SL::swatchRadius,
            cx + SL::swatchRadius, cy + SL::swatchRadius, 3.0f, accentPalette[index]);
    }

    drawRowLabel(2, SL::topRowTop, 1, "Transparency", "Adjust window transparency.");

    {
        const int sliderLeft = SL::sliderLeft(2);
        const int sliderTop = SL::sliderTop(SL::topRowTop);
        const int sliderFill =
            (SL::sliderWidth * (appSettings.transparencyPercent - 55)) / 45;

        drawSmoothControlRect(
            hdc, sliderLeft, sliderTop + 1,
            sliderLeft + SL::sliderWidth, sliderTop + 5, 2.0f,
            uiColor(RGB(48, 66, 84))
        );

        drawSmoothControlRect(
            hdc, sliderLeft, sliderTop + 1,
            sliderLeft + sliderFill, sliderTop + 5, 2.0f, accent
        );

        const int thumbCenter = sliderLeft + sliderFill;
        drawSmoothControlRect(hdc, thumbCenter - 6, sliderTop - 5,
            thumbCenter + 6, sliderTop + 11, 3.0f, uiColor(RGB(240, 246, 252)));

        drawText(
            hdc,
            std::to_string(appSettings.transparencyPercent) + "%",
            sliderLeft + SL::sliderWidth + 12,
            sliderTop - 8,
            textPrimary,
            settingsControlFont
        );
    }

    drawSwitchRow(2, SL::topRowTop, 2, "Compact Mode", "Tighter settings rows and smaller text.", appSettings.compactMode);
    drawSwitchRow(2, SL::topRowTop, 3, "Quick Tab Previews", "Show a quick frame when switching tabs.", appSettings.showAnimations);
    drawComboRow(2, SL::topRowTop, 4, "Font Size", "Adjust the UI font size.", appSettings.fontSizeIndex == 0 ? "Small" : (appSettings.fontSizeIndex == 2 ? "Large" : "Medium"));

    // ---- Alerts & Thresholds ----------------------------
    drawPanel(0, SL::middleRowTop, SL::panelHeight(6), "Alerts & Thresholds", L"temp.png");
    drawValueRow(0, SL::middleRowTop, 0, "CPU Temperature Alert", "Notify above this temperature.", static_cast<int>(SettingsRuntime::temperatureValue(appSettings.cpuTemperatureAlert)), SettingsRuntime::temperatureSuffix());
    drawValueRow(0, SL::middleRowTop, 1, "GPU Temperature Alert", "Notify above this temperature.", static_cast<int>(SettingsRuntime::temperatureValue(appSettings.gpuTemperatureAlert)), SettingsRuntime::temperatureSuffix());
    drawValueRow(0, SL::middleRowTop, 2, "Disk Usage Alert", "Alert on system disk space used.", appSettings.diskUsageAlert, "%");
    drawValueRow(0, SL::middleRowTop, 3, "Memory Usage Alert", "Notify above this usage.", appSettings.memoryUsageAlert, "%");
    drawSwitchRow(0, SL::middleRowTop, 4, "Desktop Notifications", "Show Windows alert notifications.", appSettings.showDesktopNotifications);
    drawSwitchRow(0, SL::middleRowTop, 5, "Log Alerts to File", "Save alerts to the data folder.", appSettings.logAlertsToFile);

    // ---- Data & Privacy ---------------------------------
    drawPanel(1, SL::middleRowTop, SL::panelHeight(4), "Data & Privacy", L"sysinfo_storage.png");
    drawSwitchRow(1, SL::middleRowTop, 0, "Enable Data Logging", "Save historical monitoring data.", appSettings.enableDataLogging);
    drawComboRow(1, SL::middleRowTop, 1, "Log Retention", "How long to keep log data.", std::to_string(appSettings.logRetentionDays) + " days");

    // The folder path never changes while SysMon runs, so the ellipsis fitting
    // (which measures the string one character at a time) runs once, not on
    // every repaint of the page.
    static std::string cachedLogLocationSource;
    static std::string cachedLogLocationText;

    {
        const std::string logLocationSource = getSysMonDataFolderPath();

        if (logLocationSource != cachedLogLocationSource ||
            cachedLogLocationText.empty())
        {
            cachedLogLocationSource = logLocationSource;
            cachedLogLocationText =
                fitSettingsText(logLocationSource, SL::rowWidth() - 124);
        }
    }

    drawButtonRow(1, SL::middleRowTop, 2, "Log Location", cachedLogLocationText, "Open Folder", 112);
    drawRowLabel(1, SL::middleRowTop, 3, "Anonymous Usage Data", "Not collected by this version.");

    // ---- System Integration -----------------------------
    drawPanel(2, SL::middleRowTop, SL::panelHeight(4), "System Integration", L"tools.png");
    drawSwitchRow(2, SL::middleRowTop, 0, "Show Overlay Widget", "Choose metrics and overlay appearance.", appSettings.showOverlayWidget);
    drawButtonRow(2, SL::middleRowTop, 1, "Overlay Options", "Drag to move; Ctrl + Alt + O to toggle.", "Configure", 124);
    drawButtonRow(2, SL::middleRowTop, 2, "Sensor Status", temperatureStats.hardwareSensorAvailable ? "Sensors: Active" : "Sensors: Unavailable", "Details", 112);
    drawButtonRow(2, SL::middleRowTop, 3, "File Associations", "Open .sysmonlog files in Notepad.", "Associate", 96);

    // ---- About ------------------------------------------
    drawPanel(0, SL::aboutPanelTop, SL::aboutPanelHeight, "About", L"logo.png");

    {
        const int aboutX = SL::rowLeft(0);
        const int aboutY = SL::aboutPanelTop + SL::panelFirstRowY;

        drawRoundedBox(hdc, aboutX, aboutY, aboutX + 44, aboutY + 44, uiColor(RGB(24, 42, 60)));

        if (uiAssetExists(L"logo.png"))
        {
            drawTintedPngImage(
                hdc, L"logo.png", aboutX + 10, aboutY + 10, 24, 24, uiColor(RGB(78, 205, 250))
            );
        }

        drawText(hdc, "Prof System Monitor", aboutX + 58, aboutY + 2, textPrimary, settingsRowFont);
        drawText(hdc, "Version " SYSMON_VERSION_STRING, aboutX + 58, aboutY + 22, muted, settingsDescriptionFont);
        drawText(hdc, "Dependencies & licenses (click to open)", aboutX, aboutY + 58, muted, settingsDescriptionFont);
    }

    // ---- Backup & Restore -------------------------------
    drawPanel(1, SL::backupPanelTop, SL::panelHeight(2), "Backup & Restore", L"sysinfo_storage.png");
    drawButtonRow(1, SL::backupPanelTop, 0, "Export Settings", "Save your current settings.", "Export", 106);
    drawButtonRow(1, SL::backupPanelTop, 1, "Import Settings", "Load settings from a backup.", "Import", 106);

    // ---- Support ----------------------------------------
    drawPanel(2, SL::supportPanelTop, SL::panelHeight(3), "Support", L"sidebar_systeminfo.png");
    drawButtonRow(2, SL::supportPanelTop, 0, "Data Folder", "Open local logs and backups.", "Open Folder", 112);
    drawButtonRow(2, SL::supportPanelTop, 1, "Support Report", "Create a local report to share.", "Create Report", 112);
    drawButtonRow(2, SL::supportPanelTop, 2, "Documentation", "Browse the user guide.", "Open Docs", 112);

    RestoreDC(hdc, savedSettingsDc);

    // ----------------------------------------------------
    // SCROLLBAR
    // ----------------------------------------------------
    if (settingsMaxScrollOffset > 0)
    {
        const int trackTop = SL::clipTop + 6;
        const int trackBottom = SL::clipBottom - 6;
        const int trackHeight = trackBottom - trackTop;
        const int visibleHeight = SL::clipBottom - SL::clipTop;
        const int totalHeight = SL::contentBottom - SL::clipTop;

        int thumbHeight = (visibleHeight * trackHeight) / (std::max)(1, totalHeight);
        thumbHeight = std::clamp(thumbHeight, SL::scrollbarMinThumb, trackHeight);

        const int travel = trackHeight - thumbHeight;
        const int thumbTop =
            trackTop + (settingsScrollOffset * travel) / settingsMaxScrollOffset;

        drawAccentScrollbar(hdc, SL::scrollbarLeft, SL::scrollbarRight,
            trackTop, trackBottom, thumbTop, thumbTop + thumbHeight);
    }
}

  if (currentPage == AppPage::Processes)
{
    int totalProcessCount = 0;
    const std::vector<ProcessInfo>& processes =
        getPreparedProcessListForUi(totalProcessCount);

    const std::vector<ProcessDisplayRow> processDisplayRows =
        buildProcessDisplayRows(processes);

    bool selectedProcessStillVisible = false;

    for (const ProcessInfo& process : processes)
    {
        if (process.pid == selectedProcessPid)
        {
            selectedProcessStillVisible = true;
            break;
        }
    }

    if (!selectedProcessStillVisible && !processes.empty())
    {
        selectedProcessPid = processes.front().pid;
    }

    const COLORREF processPanel = uiColor(RGB(16, 26, 35));
    const COLORREF processPanelAlt = uiColor(RGB(20, 31, 41));
    const COLORREF processTrack = uiColor(RGB(49, 64, 77));
    const COLORREF cyanAccent = uiColor(RGB(36, 200, 250));
    const COLORREF cpuAccent = uiColor(RGB(35, 170, 255));
    const COLORREF memoryAccent = uiColor(RGB(193, 88, 250));
    const COLORREF diskAccent = uiColor(RGB(72, 230, 95));
    const COLORREF networkAccent = uiColor(RGB(45, 195, 235));
    const COLORREF runningGreen = uiColor(RGB(68, 225, 126));

    auto shortenProcessText =
        [](const std::string& value,
           int maxChars) -> std::string
    {
        if (static_cast<int>(value.size()) <= maxChars)
        {
            return value;
        }

        if (maxChars <= 3)
        {
            return value.substr(0, maxChars);
        }

        return value.substr(
            0,
            static_cast<size_t>(maxChars - 3)
        ) + "...";
    };

    auto percentText =
        [](double value) -> std::string
    {
        std::ostringstream stream;
        stream << std::fixed
               << std::setprecision(0)
               << std::clamp(value, 0.0, 100.0)
               << "%";
        return stream.str();
    };

    const DiskStats* busiestDisk = nullptr;

    for (const DiskStats& disk : diskStats)
    {
        if (
            busiestDisk == nullptr ||
            disk.activeTimePercent >
                busiestDisk->activeTimePercent
        )
        {
            busiestDisk = &disk;
        }
    }

    const NetworkStats* busiestNetwork = nullptr;

    for (const NetworkStats& adapter : networkStats)
    {
        if (
            busiestNetwork == nullptr ||
            adapter.downloadMbps + adapter.uploadMbps >
                busiestNetwork->downloadMbps +
                busiestNetwork->uploadMbps
        )
        {
            busiestNetwork = &adapter;
        }
    }

    double networkPercent = 0.0;

    if (
        busiestNetwork != nullptr &&
        busiestNetwork->linkSpeedMbps > 0.0
    )
    {
        networkPercent = std::clamp(
            (
                (busiestNetwork->downloadMbps +
                 busiestNetwork->uploadMbps) /
                busiestNetwork->linkSpeedMbps
            ) * 100.0,
            0.0,
            100.0
        );
    }

    // ----------------------------------------------------
    // PROCESSES PAGE HEADER
    // ----------------------------------------------------

    drawRoundedBox(
        hdc,
        20,
        70,
        86,
        136,
        uiColor(RGB(25, 38, 50))
    );

    drawTintedPngImage(
        hdc,
        L"sidebar_processes.png",
        37,
        87,
        32,
        32,
        uiColor(RGB(235, 238, 243))
    );

    drawText(
        hdc,
        "Processes",
        105,
        75,
        textPrimary,
        titleFont
    );

    drawText(
        hdc,
        "View and manage running processes in real time.",
        105,
        110,
        textSecondary,
        smallFont
    );

    if (processSearchFocused)
    {
        drawRoundedBox(
            hdc,
            618,
            80,
            818,
            126,
            uiColor(RGB(39, 126, 178))
        );
    }

    drawRoundedBox(
        hdc,
        620,
        82,
        816,
        124,
        processPanel
    );

    // Search magnifier.
    HPEN processSearchPen =
        CreatePen(
            PS_SOLID,
            1,
            uiColor(RGB(176, 190, 204))
        );

    HGDIOBJ oldProcessSearchPen =
        SelectObject(hdc, processSearchPen);

    HGDIOBJ oldProcessSearchBrush =
        SelectObject(
            hdc,
            GetStockObject(HOLLOW_BRUSH)
        );

    drawUiEllipse(hdc, 635, 95, 647, 107);
    MoveToEx(hdc, 645, 105, nullptr);
    LineTo(hdc, 651, 111);

    SelectObject(hdc, oldProcessSearchBrush);
    SelectObject(hdc, oldProcessSearchPen);
    DeleteObject(processSearchPen);

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
        searchDisplay = processSearch;

        if (processSearchFocused)
        {
            searchDisplay += "|";
        }
    }

    drawText(
        hdc,
        shortenProcessText(searchDisplay, 22),
        660,
        96,
        processSearch.empty()
            ? uiColor(RGB(115, 128, 143))
            : textPrimary,
        smallFont
    );

    if (!processSearch.empty())
    {
        drawText(
            hdc,
            "X",
            793,
            96,
            uiColor(RGB(160, 172, 184)),
            smallFont
        );
    }

    drawRoundedBox(
        hdc,
        826,
        82,
        995,
        124,
        processPanel
    );

    std::string processFilterLabel = "All Processes";

    switch (processFilterMode)
    {
    case ProcessFilterMode::Apps:
        processFilterLabel = "Apps";
        break;
    case ProcessFilterMode::Background:
        processFilterLabel = "Background";
        break;
    case ProcessFilterMode::Windows:
        processFilterLabel = "Windows";
        break;
    case ProcessFilterMode::All:
    default:
        processFilterLabel = "All Processes";
        break;
    }

    drawText(
        hdc,
        processFilterLabel,
        844,
        96,
        textPrimary,
        smallFont
    );

    drawText(
        hdc,
        processFilterDropdownOpen ? "^" : "v",
        972,
        96,
        textSecondary,
        smallFont
    );

    // ----------------------------------------------------
    // TOP SUMMARY CARDS
    // ----------------------------------------------------

    auto drawProcessSummaryCard =
        [&](int left,
            int right,
            const std::string& title,
            const wchar_t* iconPath,
            double gaugeValue,
            const std::vector<double>& history,
            double scale,
            COLORREF accent)
    {
        drawRoundedBox(
            hdc,
            left,
            150,
            right,
            260,
            processPanel
        );

        drawRoundedBox(
            hdc,
            left + 12,
            164,
            left + 46,
            198,
            processPanelAlt
        );

        drawTintedPngImage(
            hdc,
            iconPath,
            left + 20,
            172,
            18,
            18,
            uiColor(RGB(220, 228, 237))
        );

        drawText(
            hdc,
            title,
            left + 56,
            169,
            textPrimary,
            smallFont
        );

        drawCircularGauge(
            hdc,
            left + 16,
            200,
            54,
            gaugeValue,
            accent,
            processTrack
        );

        drawDashboardCenteredText(
            hdc,
            percentText(gaugeValue),
            left + 16,
            200,
            54,
            54,
            textPrimary,
            smallFont
        );

        drawDashboardHistoryGraph(
            hdc,
            left + 82,
            203,
            right - left - 94,
            45,
            history,
            scale,
            accent
        );
    };

    drawProcessSummaryCard(
        20,
        252,
        "CPU Usage",
        L"sidebar_cpu.png",
        cpuUsage,
        cpuHistory,
        100.0,
        cpuAccent
    );

    drawProcessSummaryCard(
        262,
        494,
        "Memory Usage",
        L"sidebar_memory.png",
        ramPercent,
        ramHistory,
        100.0,
        memoryAccent
    );

    drawProcessSummaryCard(
        504,
        736,
        "Disk Activity",
        L"sidebar_disk.png",
        busiestDisk != nullptr
            ? busiestDisk->activeTimePercent
            : 0.0,
        busiestDisk != nullptr
            ? busiestDisk->activeHistory
            : cpuHistory,
        100.0,
        diskAccent
    );

    drawProcessSummaryCard(
        746,
        995,
        "Network Activity",
        L"sidebar_network.png",
        networkPercent,
        busiestNetwork != nullptr
            ? busiestNetwork->downloadHistory
            : cpuHistory,
        busiestNetwork != nullptr
            ? getNetworkGraphScale(*busiestNetwork)
            : 100.0,
        networkAccent
    );

    // ----------------------------------------------------
    // RUNNING PROCESS TABLE
    // ----------------------------------------------------

    const int tableLeft = 20;
    const int tableTop = 275;
    const int tableRight = 700;
    const int tableBottom = 690;

    const int detailsLeft = 710;
    const int detailsTop = 275;
    const int detailsRight = 995;
    const int detailsBottom = 690;

    drawRoundedBox(
        hdc,
        tableLeft,
        tableTop,
        tableRight,
        tableBottom,
        processPanel
    );

    drawRoundedBox(
        hdc,
        detailsLeft,
        detailsTop,
        detailsRight,
        detailsBottom,
        processPanel
    );

    drawTintedPngImage(
        hdc,
        L"sidebar_processes.png",
        tableLeft + 16,
        tableTop + 16,
        18,
        18,
        uiColor(RGB(225, 232, 239))
    );

    drawText(
        hdc,
        "Running Processes",
        tableLeft + 43,
        tableTop + 14,
        textPrimary,
        labelFont
    );

    std::string processCountText;

    if (
        !processSearch.empty() ||
        processFilterMode != ProcessFilterMode::All
    )
    {
        processCountText =
            std::to_string(processes.size()) +
            " of " +
            std::to_string(totalProcessCount);
    }
    else
    {
        processCountText =
            std::to_string(totalProcessCount) +
            " processes";
    }

    drawText(
        hdc,
        processCountText,
        tableRight - 105,
        tableTop + 16,
        textSecondary,
        smallFont
    );

    const int headerY = tableTop + 54;

    drawProcessSortHeader(hdc, "Name", tableLeft + 16, headerY, 48, ProcessSort::Name, textSecondary);
    drawProcessSortHeader(hdc, "PID", tableLeft + 280, headerY, 32, ProcessSort::PID, textSecondary);
    drawProcessSortHeader(hdc, "CPU", tableLeft + 355, headerY, 36, ProcessSort::CPU, textSecondary);
    drawProcessSortHeader(hdc, "Memory", tableLeft + 420, headerY, 58, ProcessSort::Memory, textSecondary);
    drawProcessSortHeader(hdc, "Threads", tableLeft + 520, headerY, 60, ProcessSort::Threads, textSecondary);
    drawText(hdc, "Status", tableLeft + 600, headerY, textSecondary, smallFont);

    HPEN processHeaderPen =
        CreatePen(
            PS_SOLID,
            1,
            uiColor(RGB(33, 44, 58))
        );

    HGDIOBJ oldProcessHeaderPen =
        SelectObject(hdc, processHeaderPen);

    MoveToEx(hdc, tableLeft + 14, tableTop + 78, nullptr);
    LineTo(hdc, tableRight - 15, tableTop + 78);

    SelectObject(hdc, oldProcessHeaderPen);
    DeleteObject(processHeaderPen);

    const int visibleRows = 12;
    const int rowHeight = 25;
    const int rowsTop = tableTop + 88;

    int maxOffset =
        (std::max)(
            0,
            static_cast<int>(processDisplayRows.size()) -
                visibleRows
        );

    processMaxScrollOffset = maxOffset;

    processScrollOffset =
        std::clamp(
            processScrollOffset,
            0,
            maxOffset
        );

    int endIndex =
        (std::min)(
            processScrollOffset + visibleRows,
            static_cast<int>(processDisplayRows.size())
        );

    visibleProcessPids.clear();
    visibleProcessGroupKeys.clear();

    for (
        int i = processScrollOffset;
        i < endIndex;
        i++
    )
    {
        const ProcessDisplayRow& displayRow =
            processDisplayRows[i];

        const ProcessInfo& process =
            displayRow.stats;

        const int rowIndex =
            i - processScrollOffset;

        const int rowY =
            rowsTop + rowIndex * rowHeight;

        visibleProcessPids.push_back(process.pid);
        visibleProcessGroupKeys.push_back(
            displayRow.groupHeader
                ? displayRow.groupKey
                : 0
        );

        if (
            process.pid == hoveredProcessPid &&
            process.pid != selectedProcessPid
        )
        {
            drawRoundedBox(
                hdc,
                tableLeft + 10,
                rowY - 4,
                tableRight - 16,
                rowY + 20,
                uiColor(RGB(23, 33, 44))
            );
        }

        if (process.pid == selectedProcessPid)
        {
            drawRoundedBox(
                hdc,
                tableLeft + 10,
                rowY - 4,
                tableRight - 16,
                rowY + 20,
                uiColor(RGB(20, 55, 79))
            );

            drawRoundedBox(
                hdc,
                tableLeft + 10,
                rowY - 4,
                tableLeft + 14,
                rowY + 20,
                cyanAccent
            );
        }

        int iconX = tableLeft + 18;
        int nameX = tableLeft + 42;

        if (displayRow.groupHeader)
        {
            drawText(
                hdc,
                isProcessGroupExpanded(displayRow.groupKey)
                    ? "v"
                    : ">",
                tableLeft + 17,
                rowY,
                uiColor(RGB(185, 197, 208)),
                smallFont
            );

            iconX = tableLeft + 35;
            nameX = tableLeft + 59;
        }
        else if (displayRow.child)
        {
            iconX = tableLeft + 38;
            nameX = tableLeft + 62;
        }

        drawProcessExecutableIcon(
            hdc,
            process.pid,
            iconX,
            rowY,
            16
        );

        drawText(
            hdc,
            shortenProcessText(displayRow.displayName, 24),
            nameX,
            rowY,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            displayRow.groupHeader
                ? "--"
                : std::to_string(process.pid),
            tableLeft + 280,
            rowY,
            textSecondary,
            smallFont
        );

        std::string cpuText = "--";

        if (process.cpuPercent >= 0.0)
        {
            std::ostringstream stream;
            stream << std::fixed
                   << std::setprecision(1)
                   << process.cpuPercent
                   << "%";
            cpuText = stream.str();
        }

        drawText(
            hdc,
            cpuText,
            tableLeft + 355,
            rowY,
            textSecondary,
            smallFont
        );

        std::string memoryText = "--";

        if (process.memoryMB >= 0.0)
        {
            std::ostringstream stream;
            stream << std::fixed
                   << std::setprecision(0)
                   << process.memoryMB
                   << " MB";
            memoryText = stream.str();
        }

        drawText(
            hdc,
            memoryText,
            tableLeft + 420,
            rowY,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            std::to_string(process.threadCount),
            tableLeft + 520,
            rowY,
            textSecondary,
            smallFont
        );

        HBRUSH statusBrush =
            getCachedSolidBrush(runningGreen);

        HGDIOBJ oldStatusBrush =
            SelectObject(hdc, statusBrush);

        HGDIOBJ oldStatusPen =
            SelectObject(
                hdc,
                GetStockObject(NULL_PEN)
            );

        drawUiEllipse(
            hdc,
            tableLeft + 600,
            rowY + 5,
            tableLeft + 607,
            rowY + 12
        );

        SelectObject(hdc, oldStatusPen);
        SelectObject(hdc, oldStatusBrush);

        drawText(
            hdc,
            "Running",
            tableLeft + 613,
            rowY,
            runningGreen,
            smallFont
        );
    }

    if (processDisplayRows.empty())
    {
        drawText(
            hdc,
            "No running processes matched the current filter.",
            tableLeft + 18,
            rowsTop + 10,
            textSecondary,
            smallFont
        );
    }

    drawProcessScrollbarUi(
        hdc,
        tableRight,
        rowsTop,
        visibleRows,
        rowHeight,
        static_cast<int>(processDisplayRows.size())
    );

    // ----------------------------------------------------
    // PROCESS DETAILS
    // ----------------------------------------------------

    drawTintedPngImage(
        hdc,
        L"sidebar_processes.png",
        detailsLeft + 16,
        detailsTop + 16,
        18,
        18,
        uiColor(RGB(225, 232, 239))
    );

    drawText(
        hdc,
        "Process Details",
        detailsLeft + 43,
        detailsTop + 14,
        textPrimary,
        labelFont
    );

    const ProcessInfo* selectedProcess = nullptr;

    for (const ProcessInfo& process : processes)
    {
        if (process.pid == selectedProcessPid)
        {
            selectedProcess = &process;
            break;
        }
    }

    if (selectedProcess != nullptr)
    {
        drawProcessExecutableIcon(
            hdc,
            selectedProcess->pid,
            detailsLeft + 18,
            detailsTop + 53,
            38
        );

        drawText(
            hdc,
            shortenProcessText(selectedProcess->name, 20),
            detailsLeft + 68,
            detailsTop + 56,
            textPrimary,
            labelFont
        );

        HBRUSH detailsStatusBrush =
            getCachedSolidBrush(runningGreen);

        HGDIOBJ oldDetailsStatusBrush =
            SelectObject(hdc, detailsStatusBrush);

        HGDIOBJ oldDetailsStatusPen =
            SelectObject(
                hdc,
                GetStockObject(NULL_PEN)
            );

        drawUiEllipse(
            hdc,
            detailsRight - 82,
            detailsTop + 61,
            detailsRight - 74,
            detailsTop + 69
        );

        SelectObject(hdc, oldDetailsStatusPen);
        SelectObject(hdc, oldDetailsStatusBrush);

        drawText(
            hdc,
            "Running",
            detailsRight - 67,
            detailsTop + 55,
            runningGreen,
            smallFont
        );

        std::wstring pathWide =
            getProcessExecutablePath(
                selectedProcess->pid
            );

        std::string processPath =
            pathWide.empty()
                ? "--"
                : std::string(
                      pathWide.begin(),
                      pathWide.end()
                  );

        drawText(
            hdc,
            shortenProcessText(processPath, 34),
            detailsLeft + 18,
            detailsTop + 98,
            textSecondary,
            smallFont
        );

        HPEN detailsLinePen =
            CreatePen(
                PS_SOLID,
                1,
                uiColor(RGB(33, 44, 58))
            );

        HGDIOBJ oldDetailsLinePen =
            SelectObject(hdc, detailsLinePen);

        MoveToEx(
            hdc,
            detailsLeft + 15,
            detailsTop + 125,
            nullptr
        );

        LineTo(
            hdc,
            detailsRight - 15,
            detailsTop + 125
        );

        SelectObject(hdc, oldDetailsLinePen);
        DeleteObject(detailsLinePen);

        auto drawProcessDetailRow =
            [&](int row,
                const std::string& label,
                const std::string& value)
        {
            const int y =
                detailsTop + 143 + row * 28;

            drawText(
                hdc,
                label,
                detailsLeft + 18,
                y,
                textSecondary,
                smallFont
            );

            std::string shownValue =
                shortenProcessText(value, 22);

            setFont(hdc, smallFont);

            SIZE valueSize = {};

            GetTextExtentPoint32A(
                hdc,
                shownValue.c_str(),
                static_cast<int>(shownValue.size()),
                &valueSize
            );

            int valueX =
                detailsRight - 18 - valueSize.cx;

            if (valueX < detailsLeft + 120)
            {
                valueX = detailsLeft + 120;
            }

            drawText(
                hdc,
                shownValue,
                valueX,
                y,
                textPrimary,
                smallFont
            );
        };

        std::ostringstream cpuStream;
        cpuStream << std::fixed
                  << std::setprecision(1)
                  << (std::max)(0.0, selectedProcess->cpuPercent)
                  << "%";

        std::ostringstream memoryStream;
        memoryStream << std::fixed
                     << std::setprecision(0)
                     << (std::max)(0.0, selectedProcess->memoryMB)
                     << " MB";

        drawProcessDetailRow(0, "PID", std::to_string(selectedProcess->pid));
        drawProcessDetailRow(1, "CPU Usage", cpuStream.str());
        drawProcessDetailRow(2, "Memory", memoryStream.str());
        drawProcessDetailRow(3, "Threads", std::to_string(selectedProcess->threadCount));
        drawProcessDetailRow(4, "Handles", std::to_string(selectedProcess->handleCount));
        drawProcessDetailRow(5, "Parent PID", std::to_string(selectedProcess->parentPid));
        drawProcessDetailRow(6, "Status", "Running");

        const bool canManageProcess =
            selectedProcess->pid != 0 &&
            selectedProcess->pid != 4;

        const bool canTerminate =
            canManageProcess &&
            selectedProcess->pid != GetCurrentProcessId();

        // ----------------------------------------------------
        // PROCESS ACTION BUTTONS
        // ----------------------------------------------------

        const int actionTop =
            detailsBottom - 49;

        const int actionBottom =
            detailsBottom - 16;

        // Compact reference-style utility buttons.  The available width
        // is shared deliberately so all three controls feel like one
        // polished action strip instead of three unrelated buttons.
        const int openLeft =
            detailsLeft + 10;

        const int openRight =
            openLeft + 110;

        const int endLeft =
            openRight + 5;

        const int endRight =
            endLeft + 76;

        const int priorityLeft =
            endRight + 5;

        const int priorityRight =
            detailsRight - 10;

        const COLORREF actionBorder =
            uiColor(RGB(34, 52, 67));

        const COLORREF actionFill =
            uiColor(RGB(20, 34, 46));

        const COLORREF actionFillDisabled =
            uiColor(RGB(31, 39, 48));

        const COLORREF actionText =
            uiColor(RGB(224, 232, 239));

        const COLORREF actionTextDisabled =
            uiColor(RGB(115, 126, 137));

        const COLORREF actionIcon =
            uiColor(RGB(188, 204, 216));

        auto drawActionButtonBase =
            [&](int left,
                int right,
                bool enabled)
        {
            drawRoundedBox(
                hdc,
                left,
                actionTop,
                right,
                actionBottom,
                actionBorder
            );

            drawRoundedBox(
                hdc,
                left + 1,
                actionTop + 1,
                right - 1,
                actionBottom - 1,
                enabled
                    ? actionFill
                    : actionFillDisabled
            );
        };

        drawActionButtonBase(
            openLeft,
            openRight,
            canManageProcess
        );

        drawActionButtonBase(
            endLeft,
            endRight,
            canTerminate
        );

        drawActionButtonBase(
            priorityLeft,
            priorityRight,
            canManageProcess
        );

        // Smaller reference-style font so the labels have comfortable
        // padding instead of running into the neighbouring controls.
        static HFONT actionButtonFont =
            CreateFontA(
                12,
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

        // Shared thin icon pen for the three action buttons.
        HPEN actionIconPen =
            CreatePen(
                PS_SOLID,
                1,
                actionIcon
            );

        HGDIOBJ oldActionPen =
            SelectObject(
                hdc,
                actionIconPen
            );

        HGDIOBJ oldActionBrush =
            SelectObject(
                hdc,
                GetStockObject(HOLLOW_BRUSH)
            );

        // Folder icon - Open File Location.
        const int folderX = openLeft + 7;
        const int folderY = actionTop + 11;

        MoveToEx(hdc, folderX, folderY + 1, nullptr);
        LineTo(hdc, folderX + 4, folderY + 1);
        LineTo(hdc, folderX + 6, folderY + 3);
        LineTo(hdc, folderX + 12, folderY + 3);
        LineTo(hdc, folderX + 12, folderY + 11);
        LineTo(hdc, folderX, folderY + 11);
        LineTo(hdc, folderX, folderY + 1);

        // Stop/process icon - End Process.
        Rectangle(
            hdc,
            endLeft + 7,
            actionTop + 12,
            endLeft + 15,
            actionTop + 20
        );

        // Priority icon - two small opposing chevrons.
        const int priorityIconX = priorityLeft + 6;
        const int priorityIconY = actionTop + 10;

        MoveToEx(hdc, priorityIconX, priorityIconY + 4, nullptr);
        LineTo(hdc, priorityIconX + 2, priorityIconY + 2);
        LineTo(hdc, priorityIconX + 4, priorityIconY + 4);

        MoveToEx(hdc, priorityIconX, priorityIconY + 9, nullptr);
        LineTo(hdc, priorityIconX + 2, priorityIconY + 11);
        LineTo(hdc, priorityIconX + 4, priorityIconY + 9);

        SelectObject(hdc, oldActionBrush);
        SelectObject(hdc, oldActionPen);
        DeleteObject(actionIconPen);

        drawText(
            hdc,
            "Open File Location",
            openLeft + 22,
            actionTop + 10,
            canManageProcess
                ? actionText
                : actionTextDisabled,
            actionButtonFont
        );

        drawText(
            hdc,
            canTerminate
                ? "End Process"
                : "Protected",
            endLeft + 19,
            actionTop + 10,
            canTerminate
                ? actionText
                : actionTextDisabled,
            actionButtonFont
        );

        drawText(
            hdc,
            "Set Priority",
            priorityLeft + 13,
            actionTop + 10,
            canManageProcess
                ? actionText
                : actionTextDisabled,
            actionButtonFont
        );

        // Small reference-style chevron on the right.
        HPEN chevronPen =
            CreatePen(
                PS_SOLID,
                1,
                canManageProcess
                    ? uiColor(RGB(155, 171, 184))
                    : uiColor(RGB(93, 104, 114))
            );

        HGDIOBJ oldChevronPen =
            SelectObject(
                hdc,
                chevronPen
            );

        const int chevronX = priorityRight - 7;
        const int chevronY = actionTop + 15;

        if (processPriorityDropdownOpen)
        {
            MoveToEx(hdc, chevronX - 3, chevronY + 2, nullptr);
            LineTo(hdc, chevronX, chevronY - 1);
            LineTo(hdc, chevronX + 3, chevronY + 2);
        }
        else
        {
            MoveToEx(hdc, chevronX - 3, chevronY - 1, nullptr);
            LineTo(hdc, chevronX, chevronY + 2);
            LineTo(hdc, chevronX + 3, chevronY - 1);
        }

        SelectObject(hdc, oldChevronPen);
        DeleteObject(chevronPen);

        // Priority menu opens upward so it remains inside the page.
        if (
            processPriorityDropdownOpen &&
            canManageProcess
        )
        {
            const char* priorityNames[] =
            {
                "Realtime",
                "High",
                "Above Normal",
                "Normal",
                "Below Normal",
                "Low"
            };

            const int optionHeight = 27;
            const int optionCount = 6;
            const int menuBottom = actionTop - 6;
            const int menuTop =
                menuBottom -
                optionHeight * optionCount;

            drawRoundedBox(
                hdc,
                priorityLeft - 12,
                menuTop,
                priorityRight,
                menuBottom,
                uiColor(RGB(20, 29, 40))
            );

            for (int option = 0;
                 option < optionCount;
                 option++)
            {
                int optionTop =
                    menuTop +
                    option * optionHeight;

                if (option > 0)
                {
                    HPEN optionLinePen =
                        CreatePen(
                            PS_SOLID,
                            1,
                            uiColor(RGB(34, 45, 58))
                        );

                    HGDIOBJ oldOptionLinePen =
                        SelectObject(
                            hdc,
                            optionLinePen
                        );

                    MoveToEx(
                        hdc,
                        priorityLeft - 6,
                        optionTop,
                        nullptr
                    );

                    LineTo(
                        hdc,
                        priorityRight - 6,
                        optionTop
                    );

                    SelectObject(
                        hdc,
                        oldOptionLinePen
                    );

                    DeleteObject(optionLinePen);
                }

                drawText(
                    hdc,
                    priorityNames[option],
                    priorityLeft - 4,
                    optionTop + 7,
                    option == 0
                        ? uiColor(RGB(255, 180, 105))
                        : textPrimary,
                    smallFont
                );
            }
        }
    }
    else
    {
        drawText(
            hdc,
            "Select a process from the list",
            detailsLeft + 18,
            detailsTop + 70,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "to view details and task actions.",
            detailsLeft + 18,
            detailsTop + 94,
            textSecondary,
            smallFont
        );
    }

    // Draw the filter menu last so it stays above the summary cards.
    if (processFilterDropdownOpen)
    {
        const int menuLeft = 826;
        const int menuRight = 995;
        const int menuTop = 128;
        const int optionHeight = 32;
        const char* labels[] =
        {
            "All Processes",
            "Apps",
            "Background",
            "Windows"
        };

        drawRoundedBox(
            hdc,
            menuLeft,
            menuTop,
            menuRight,
            menuTop + optionHeight * 4,
            uiColor(RGB(20, 30, 40))
        );

        for (int option = 0; option < 4; option++)
        {
            const int optionTop =
                menuTop + option * optionHeight;

            const bool selected =
                static_cast<int>(processFilterMode) == option;

            if (selected)
            {
                drawRoundedBox(
                    hdc,
                    menuLeft + 5,
                    optionTop + 3,
                    menuRight - 5,
                    optionTop + optionHeight - 3,
                    uiColor(RGB(27, 48, 64))
                );
            }

            drawText(
                hdc,
                labels[option],
                menuLeft + 16,
                optionTop + 8,
                selected
                    ? uiColor(RGB(54, 205, 250))
                    : textPrimary,
                smallFont
            );
        }
    }
}
else if (currentPage == AppPage::Tools)
{
    const COLORREF toolsCard = uiColor(RGB(18, 26, 36));
    const COLORREF toolsInner = uiColor(RGB(29, 41, 54));
    const COLORREF toolsLine = uiColor(RGB(35, 48, 62));
    const COLORREF toolsCyan = uiColor(RGB(38, 198, 246));
    const COLORREF toolsGreen = uiColor(RGB(59, 229, 118));
    const COLORREF toolsPurple = uiColor(RGB(195, 92, 240));

    auto drawToolsIconBox =
        [&](int x,
            int y,
            const wchar_t* iconPath,
            COLORREF color)
    {
        drawRoundedBox(
            hdc,
            x,
            y,
            x + 42,
            y + 42,
            toolsInner
        );

        if (
            iconPath != nullptr &&
            getCachedPngImage(iconPath) != nullptr
        )
        {
            drawTintedPngImage(
                hdc,
                iconPath,
                x + 10,
                y + 10,
                22,
                22,
                color
            );
        }
    };

    auto drawToolButton =
        [&](int left,
            int top,
            int right,
            int bottom,
            const std::string& text,
            COLORREF background,
            COLORREF foreground)
    {
        drawRoundedBox(
            hdc,
            left,
            top,
            right,
            bottom,
            background
        );

        setFont(hdc, smallFont);
        SIZE extent = {};
        GetTextExtentPoint32A(
            hdc,
            text.c_str(),
            static_cast<int>(text.size()),
            &extent
        );

        drawText(
            hdc,
            text,
            left + ((right - left) - extent.cx) / 2,
            top + ((bottom - top) - extent.cy) / 2,
            foreground,
            smallFont
        );
    };

    auto drawToolCard =
        [&](int left,
            int top,
            int right,
            int bottom,
            const std::string& title,
            const std::string& line1,
            const std::string& line2,
            const wchar_t* iconPath,
            COLORREF iconColor,
            const std::string& buttonText,
            COLORREF buttonBackground,
            COLORREF buttonTextColor)
    {
        drawRoundedBox(
            hdc,
            left,
            top,
            right,
            bottom,
            toolsCard
        );

        drawToolsIconBox(
            left + 12,
            top + 12,
            iconPath,
            iconColor
        );

        drawText(
            hdc,
            title,
            left + 66,
            top + 13,
            textPrimary,
            labelFont
        );

        drawText(
            hdc,
            line1,
            left + 66,
            top + 38,
            textSecondary,
            smallFont
        );

        if (!line2.empty())
        {
            drawText(
                hdc,
                line2,
                left + 66,
                top + 55,
                textSecondary,
                smallFont
            );
        }

        drawToolButton(
            left + 66,
            bottom - 31,
            right - 12,
            bottom - 8,
            buttonText,
            buttonBackground,
            buttonTextColor
        );
    };

    auto drawUtilityRow =
        [&](int left,
            int right,
            int y,
            const std::string& title,
            const std::string& subtitle,
            const wchar_t* iconPath)
    {
        drawTintedPngImage(
            hdc,
            iconPath,
            left + 16,
            y + 7,
            18,
            18,
            uiColor(RGB(225, 232, 239))
        );

        drawText(
            hdc,
            title,
            left + 48,
            y + 2,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            subtitle,
            left + 48,
            y + 20,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            ">",
            right - 24,
            y + 11,
            textSecondary,
            smallFont
        );
    };

    auto drawQuickAction =
        [&](int left,
            int top,
            int right,
            const std::string& title,
            const wchar_t* iconPath)
    {
        drawRoundedBox(
            hdc,
            left,
            top,
            right,
            top + 34,
            uiColor(RGB(27, 40, 53))
        );

        drawTintedPngImage(
            hdc,
            iconPath,
            left + 14,
            top + 8,
            18,
            18,
            uiColor(RGB(225, 232, 239))
        );

        drawText(
            hdc,
            title,
            left + 46,
            top + 9,
            textPrimary,
            smallFont
        );
    };

    // ----------------------------------------------------
    // PAGE HEADER
    // ----------------------------------------------------
    drawRoundedBox(
        hdc,
        35,
        62,
        81,
        108,
        uiColor(RGB(29, 41, 55))
    );

    drawTintedPngImage(
        hdc,
        L"tools.png",
        44,
        71,
        28,
        28,
        uiColor(RGB(235, 239, 244))
    );

    drawText(
        hdc,
        "Tools",
        95,
        64,
        textPrimary,
        mediumFont
    );

    drawText(
        hdc,
        "System utilities and maintenance tools.",
        95,
        94,
        textSecondary,
        subtitleFont
    );

    // ----------------------------------------------------
    // PRIMARY TOOL CARDS - ROW 1
    // ----------------------------------------------------
    drawToolCard(
        30, 122, 267, 230,
        "Disk Cleanup",
        "Remove temporary files",
        "and free up space.",
        L"disk.png",
        toolsCyan,
        "Run Cleanup",
        uiColor(RGB(0, 91, 139)),
        uiColor(RGB(78, 213, 255))
    );

    drawToolCard(
        277, 122, 514, 230,
        "System File Check",
        "Scan and repair corrupted",
        "Windows system files.",
        L"sysinfo_os.png",
        toolsGreen,
        "Run Scan",
        uiColor(RGB(0, 91, 62)),
        toolsGreen
    );

    drawToolCard(
        524, 122, 761, 230,
        "Performance Boost",
        "Open Windows performance",
        "optimization options.",
        L"performance.png",
        toolsPurple,
        "Optimize",
        uiColor(RGB(70, 38, 112)),
        uiColor(RGB(224, 131, 255))
    );

    drawToolCard(
        771, 122, 1015, 230,
        "Startup Manager",
        "Manage programs that",
        "start with Windows.",
        L"settings.png",
        toolsCyan,
        "Open Manager",
        uiColor(RGB(0, 91, 139)),
        uiColor(RGB(78, 213, 255))
    );

    // ----------------------------------------------------
    // PRIMARY TOOL CARDS - ROW 2
    // ----------------------------------------------------
    drawToolCard(
        30, 240, 267, 348,
        "View Logs",
        "Open Windows Event",
        "Viewer logs.",
        L"sidebar_systeminfo.png",
        uiColor(RGB(220, 230, 240)),
        "Open Logs",
        uiColor(RGB(31, 49, 64)),
        textPrimary
    );

    drawToolCard(
        277, 240, 514, 348,
        "System Configuration",
        "Adjust advanced",
        "system settings.",
        L"settings.png",
        toolsCyan,
        "Open Config",
        uiColor(RGB(0, 91, 139)),
        uiColor(RGB(78, 213, 255))
    );

    drawToolCard(
        524, 240, 761, 348,
        "Services Manager",
        "View and manage Windows",
        "background services.",
        L"settings.png",
        uiColor(RGB(225, 232, 239)),
        "Open Services",
        uiColor(RGB(31, 49, 64)),
        textPrimary
    );

    drawToolCard(
        771, 240, 1015, 348,
        "Command Prompt",
        "Open an elevated",
        "command prompt.",
        L"tools.png",
        uiColor(RGB(225, 232, 239)),
        "Open as Admin",
        uiColor(RGB(31, 49, 64)),
        textPrimary
    );

    // ----------------------------------------------------
    // SYSTEM UTILITIES
    // ----------------------------------------------------
    drawRoundedBox(
        hdc,
        30,
        360,
        514,
        690,
        toolsCard
    );

    drawTintedPngImage(
        hdc,
        L"settings.png",
        45,
        374,
        19,
        19,
        uiColor(RGB(225, 232, 239))
    );

    drawText(
        hdc,
        "System Utilities",
        75,
        374,
        textPrimary,
        labelFont
    );

    HPEN utilitiesLine =
        CreatePen(
            PS_SOLID,
            1,
            toolsLine
        );

    HGDIOBJ oldUtilitiesLine =
        SelectObject(
            hdc,
            utilitiesLine
        );

    MoveToEx(hdc, 44, 405, nullptr);
    LineTo(hdc, 500, 405);

    SelectObject(hdc, oldUtilitiesLine);
    DeleteObject(utilitiesLine);

    drawUtilityRow(
        42, 500, 416,
        "Defragment Drive",
        "Open Optimize Drives",
        L"disk.png"
    );

    drawUtilityRow(
        42, 500, 464,
        "Check for Updates",
        "Open Windows Update",
        L"sysinfo_os.png"
    );

    drawUtilityRow(
        42, 500, 512,
        "Power Options",
        "Manage Windows power plans",
        L"settings.png"
    );

    drawUtilityRow(
        42, 500, 560,
        "System Restore",
        "Open System Restore",
        L"sidebar_systeminfo.png"
    );

    drawUtilityRow(
        42, 500, 608,
        "Network Troubleshooter",
        "Open network settings",
        L"network.png"
    );

    // ----------------------------------------------------
    // QUICK ACTIONS
    // ----------------------------------------------------
    drawRoundedBox(
        hdc,
        524,
        360,
        1015,
        690,
        toolsCard
    );

    drawTintedPngImage(
        hdc,
        L"tools.png",
        540,
        374,
        19,
        19,
        uiColor(RGB(225, 232, 239))
    );

    drawText(
        hdc,
        "Quick Actions",
        570,
        374,
        textPrimary,
        labelFont
    );

    drawQuickAction(
        540, 410, 999,
        "Clear Temporary Files",
        L"disk.png"
    );

    drawQuickAction(
        540, 452, 999,
        "Flush DNS Cache",
        L"network.png"
    );

    drawQuickAction(
        540, 494, 999,
        "Reset Network Settings",
        L"network.png"
    );

    drawQuickAction(
        540, 536, 999,
        "Empty Recycle Bin",
        L"sysinfo_storage.png"
    );

    drawQuickAction(
        540, 578, 999,
        "Take System Snapshot",
        L"sidebar_systeminfo.png"
    );
}
else if (currentPage == AppPage::SystemInfo)
{
    // --------------------------------------------------------
    // REFERENCE-STYLE SYSTEM INFORMATION PAGE
    // --------------------------------------------------------

    systemInfoMaxScrollOffset = 0;
    systemInfoScrollOffset = 0;

    const COLORREF panelColor =
        uiColor(RGB(17, 25, 35));

    const COLORREF panelBorder =
        uiColor(RGB(30, 43, 57));

    const COLORREF accent =
        uiColor(RGB(45, 197, 245));

    const COLORREF healthyGreen =
        uiColor(RGB(68, 225, 126));

    const COLORREF warningOrange =
        uiColor(RGB(235, 170, 70));

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

        setFont(hdc, font);

        SIZE extent = {};

        GetTextExtentPoint32A(
            hdc,
            source.c_str(),
            static_cast<int>(source.size()),
            &extent
        );

        if (extent.cx <= maxWidth)
        {
            return source;
        }

        std::string shortened = source;

        while (shortened.size() > 3)
        {
            shortened.pop_back();

            std::string candidate =
                shortened + "...";

            GetTextExtentPoint32A(
                hdc,
                candidate.c_str(),
                static_cast<int>(candidate.size()),
                &extent
            );

            if (extent.cx <= maxWidth)
            {
                return candidate;
            }
        }

        return "...";
    };

    auto drawPanel =
        [&](int left,
            int top,
            int right,
            int bottom)
    {
        drawRoundedBox(
            hdc,
            left,
            top,
            right,
            bottom,
            panelColor
        );

        HPEN borderPen =
            CreatePen(
                PS_SOLID,
                1,
                panelBorder
            );

        HGDIOBJ oldPen =
            SelectObject(
                hdc,
                borderPen
            );

        HGDIOBJ oldBrush =
            SelectObject(
                hdc,
                GetStockObject(HOLLOW_BRUSH)
            );

        RoundRect(
            hdc,
            left,
            top,
            right,
            bottom,
            16,
            16
        );

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);
    };

    auto drawPanelTitle =
        [&](int left,
            int top,
            const wchar_t* icon,
            const std::string& title)
    {
        drawRoundedBox(
            hdc,
            left + 12,
            top + 10,
            left + 40,
            top + 38,
            uiColor(RGB(28, 39, 52))
        );

        drawTintedPngImage(
            hdc,
            icon,
            left + 18,
            top + 16,
            16,
            16,
            uiColor(RGB(225, 232, 239))
        );

        drawText(
            hdc,
            title,
            left + 49,
            top + 14,
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
            int valueX)
    {
        drawText(
            hdc,
            label,
            left + 14,
            y,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            fitInfoText(
                value,
                right - valueX - 14,
                smallFont
            ),
            valueX,
            y,
            textPrimary,
            smallFont
        );

        HPEN linePen =
            CreatePen(
                PS_SOLID,
                1,
                uiColor(RGB(28, 39, 51))
            );

        HGDIOBJ oldPen =
            SelectObject(hdc, linePen);

        MoveToEx(
            hdc,
            left + 14,
            y + 19,
            nullptr
        );

        LineTo(
            hdc,
            right - 14,
            y + 19
        );

        SelectObject(hdc, oldPen);
        DeleteObject(linePen);
    };

    auto drawTopCard =
        [&](int left,
            int top,
            int right,
            int bottom,
            const wchar_t* iconPath,
            const std::string& title,
            const std::string& primary,
            const std::string& secondary,
            const std::string& counter)
    {
        drawPanel(
            left,
            top,
            right,
            bottom
        );

        drawRoundedBox(
            hdc,
            left + 9,
            top + 11,
            left + 48,
            top + 51,
            uiColor(RGB(29, 41, 55))
        );

        if (!drawTintedPngImage(
                hdc,
                iconPath,
                left + 18,
                top + 20,
                21,
                21,
                uiColor(RGB(225, 232, 239))
            ))
        {
            drawText(
                hdc,
                "*",
                left + 24,
                top + 21,
                textSecondary,
                labelFont
            );
        }

        drawText(
            hdc,
            title,
            left + 57,
            top + 10,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            fitInfoText(
                primary,
                right - left - 67,
                smallFont
            ),
            left + 57,
            top + 31,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            fitInfoText(
                secondary,
                right - left - 67,
                smallFont
            ),
            left + 57,
            top + 51,
            textSecondary,
            smallFont
        );

        if (!counter.empty())
        {
            drawText(
                hdc,
                counter,
                right - 42,
                top + 10,
                accent,
                smallFont
            );
        }
    };

    auto formatGb =
        [](double value)
        -> std::string
    {
        if (value < 0.0)
        {
            return "--";
        }

        std::ostringstream stream;
        stream
            << std::fixed
            << std::setprecision(
                value >= 100.0
                ? 0
                : 1
            )
            << value
            << " GB";

        return stream.str();
    };

    auto drawUsageBar =
        [&](int left,
            int right,
            int y,
            double percent,
            COLORREF barColor)
    {
        drawRecommendedUsageBar(
            hdc,
            left,
            y,
            right - left,
            11,
            percent,
            barColor
        );
    };

    // Keep selections valid when hardware is added or removed.
    if (gpuStats.empty())
    {
        systemInfoSelectedGpuIndex = 0;
    }
    else
    {
        systemInfoSelectedGpuIndex =
            std::clamp(
                systemInfoSelectedGpuIndex,
                0,
                static_cast<int>(gpuStats.size()) - 1
            );
    }

    if (diskStats.empty())
    {
        systemInfoSelectedDiskIndex = 0;
    }
    else
    {
        systemInfoSelectedDiskIndex =
            std::clamp(
                systemInfoSelectedDiskIndex,
                0,
                static_cast<int>(diskStats.size()) - 1
            );
    }

    const GpuStats* selectedInfoGpu =
        gpuStats.empty()
        ? nullptr
        : &gpuStats[
            static_cast<size_t>(
                systemInfoSelectedGpuIndex
            )
          ];

    const DiskStats* selectedInfoDisk =
        diskStats.empty()
        ? nullptr
        : &diskStats[
            static_cast<size_t>(
                systemInfoSelectedDiskIndex
            )
          ];

    // ----------------------------------------------------
    // Header
    // ----------------------------------------------------

    drawText(
        hdc,
        "System Information",
        25,
        65,
        textPrimary,
        mediumFont
    );

    drawText(
        hdc,
        "Hardware, software and system details at a glance.",
        25,
        96,
        textSecondary,
        smallFont
    );

    HBRUSH statusDotBrush =
        CreateSolidBrush(healthyGreen);

    HGDIOBJ oldStatusDotBrush =
        SelectObject(hdc, statusDotBrush);

    HGDIOBJ oldStatusDotPen =
        SelectObject(
            hdc,
            GetStockObject(NULL_PEN)
        );

    drawUiEllipse(
        hdc,
        712,
        78,
        719,
        85
    );

    SelectObject(hdc, oldStatusDotPen);
    SelectObject(hdc, oldStatusDotBrush);
    DeleteObject(statusDotBrush);

    drawText(
        hdc,
        "System Healthy",
        726,
        74,
        healthyGreen,
        smallFont
    );

    SYSTEMTIME infoLocalTime = {};
    GetLocalTime(&infoLocalTime);

    static const char* infoWeekDays[] =
    {
        "Sun", "Mon", "Tue", "Wed",
        "Thu", "Fri", "Sat"
    };

    static const char* infoMonths[] =
    {
        "",
        "Jan", "Feb", "Mar", "Apr",
        "May", "Jun", "Jul", "Aug",
        "Sep", "Oct", "Nov", "Dec"
    };

    std::ostringstream infoDateText;
    infoDateText
        << infoWeekDays[
            infoLocalTime.wDayOfWeek
        ]
        << ", "
        << infoMonths[
            infoLocalTime.wMonth
        ]
        << " "
        << infoLocalTime.wDay
        << ", "
        << infoLocalTime.wYear;

    int infoHour12 =
        infoLocalTime.wHour % 12;

    if (infoHour12 == 0)
    {
        infoHour12 = 12;
    }

    std::ostringstream infoClockText;
    infoClockText
        << infoHour12
        << ":"
        << std::setw(2)
        << std::setfill('0')
        << infoLocalTime.wMinute
        << " "
        << (
            infoLocalTime.wHour >= 12
            ? "PM"
            : "AM"
        );

    drawText(
        hdc,
        infoDateText.str(),
        832,
        74,
        textSecondary,
        smallFont
    );

    drawText(
        hdc,
        infoClockText.str(),
        948,
        74,
        textPrimary,
        smallFont
    );

    // ----------------------------------------------------
    // Top summary cards
    // ----------------------------------------------------

    std::string osCardSecondary =
        systemInfo.osVersion;

    if (systemInfo.osBuild != "--")
    {
        osCardSecondary +=
            " (Build " +
            systemInfo.osBuild +
            ")";
    }

    std::ostringstream cpuCardSecondary;
    if (systemInfo.cpuCores > 0)
    {
        cpuCardSecondary
            << systemInfo.cpuCores
            << " Cores";
    }
    else
    {
        cpuCardSecondary << "--";
    }

    if (systemInfo.cpuThreads > 0)
    {
        cpuCardSecondary
            << " / "
            << systemInfo.cpuThreads
            << " Threads";
    }

    std::string gpuCardPrimary =
        selectedInfoGpu != nullptr
        ? selectedInfoGpu->name
        : systemInfo.gpuName;

    std::string gpuCardSecondary = "--";

    if (
        selectedInfoGpu != nullptr &&
        selectedInfoGpu->dedicatedMemoryTotalBytes > 0
    )
    {
        gpuCardSecondary =
            formatMemoryBytes(
                selectedInfoGpu->
                    dedicatedMemoryTotalBytes
            );
    }
    else if (systemInfo.gpuMemory != "--")
    {
        gpuCardSecondary =
            systemInfo.gpuMemory;
    }

    std::string gpuCounter;
    if (gpuStats.size() > 1)
    {
        gpuCounter =
            std::to_string(
                systemInfoSelectedGpuIndex + 1
            ) +
            "/" +
            std::to_string(gpuStats.size());
    }

    std::string memoryCardPrimary =
        systemInfo.installedMemory;

    if (systemInfo.memoryType != "--")
    {
        memoryCardPrimary +=
            " " +
            systemInfo.memoryType;
    }

    std::string storageCardPrimary =
        selectedInfoDisk != nullptr
        ? selectedInfoDisk->model
        : "--";

    std::string storageCardSecondary = "--";

    if (selectedInfoDisk != nullptr)
    {
        if (selectedInfoDisk->capacityGB > 0.0)
        {
            storageCardSecondary =
                formatDiskCapacity(
                    selectedInfoDisk->capacityGB
                );
        }

        if (selectedInfoDisk->type != "--")
        {
            storageCardSecondary +=
                " (" +
                selectedInfoDisk->type +
                ")";
        }
    }

    std::string storageCounter;
    if (diskStats.size() > 1)
    {
        storageCounter =
            std::to_string(
                systemInfoSelectedDiskIndex + 1
            ) +
            "/" +
            std::to_string(diskStats.size());
    }

    ULONGLONG uptimeDays =
        uptimeSeconds / 86400ULL;

    ULONGLONG uptimeHours =
        (uptimeSeconds % 86400ULL) /
        3600ULL;

    ULONGLONG uptimeMinutes =
        (uptimeSeconds % 3600ULL) /
        60ULL;

    std::ostringstream uptimePrimary;
    uptimePrimary
        << uptimeDays
        << " days "
        << uptimeHours
        << " hours";

    std::ostringstream uptimeSecondary;
    uptimeSecondary
        << uptimeMinutes
        << " minutes";

    const int cardsTop = 116;
    const int cardsBottom = 206;
    const int cardWidth = 155;
    const int cardGap = 8;
    const int card1 = 25;
    const int card2 = card1 + cardWidth + cardGap;
    const int card3 = card2 + cardWidth + cardGap;
    const int card4 = card3 + cardWidth + cardGap;
    const int card5 = card4 + cardWidth + cardGap;
    const int card6 = card5 + cardWidth + cardGap;

    drawTopCard(
        card1,
        cardsTop,
        card1 + cardWidth,
        cardsBottom,
        L"sysinfo_os.png",
        "OS",
        systemInfo.osName,
        osCardSecondary,
        ""
    );

    drawTopCard(
        card2,
        cardsTop,
        card2 + cardWidth,
        cardsBottom,
        L"sysinfo_cpu.png",
        "CPU",
        systemInfo.cpuName,
        cpuCardSecondary.str(),
        ""
    );

    drawTopCard(
        card3,
        cardsTop,
        card3 + cardWidth,
        cardsBottom,
        L"sysinfo_gpu.png",
        "GPU",
        gpuCardPrimary,
        gpuCardSecondary,
        gpuCounter
    );

    drawTopCard(
        card4,
        cardsTop,
        card4 + cardWidth,
        cardsBottom,
        L"sysinfo_memory.png",
        "Memory",
        memoryCardPrimary,
        systemInfo.memorySpeed,
        ""
    );

    drawTopCard(
        card5,
        cardsTop,
        card5 + cardWidth,
        cardsBottom,
        L"sysinfo_storage.png",
        "Storage",
        storageCardPrimary,
        storageCardSecondary,
        storageCounter
    );

    drawTopCard(
        card6,
        cardsTop,
        card6 + cardWidth,
        cardsBottom,
        L"sysinfo_uptime.png",
        "Uptime",
        uptimePrimary.str(),
        uptimeSecondary.str(),
        ""
    );

    // ----------------------------------------------------
    // System Summary
    // ----------------------------------------------------

    const int upperTop = 216;
    const int upperBottom = 458;
    const int summaryLeft = 25;
    const int summaryRight = 497;
    const int hardwareLeft = 507;
    const int hardwareRight = 995;

    drawPanel(
        summaryLeft,
        upperTop,
        summaryRight,
        upperBottom
    );

    drawPanelTitle(
        summaryLeft,
        upperTop,
        L"sidebar_systeminfo.png",
        "System Summary"
    );

    std::string biosVersionText =
        systemInfo.biosVersion;

    if (
        systemInfo.biosVendor != "--" &&
        !systemInfo.biosVendor.empty()
    )
    {
        biosVersionText =
            systemInfo.biosVendor +
            " " +
            systemInfo.biosVersion;
    }

    int summaryY = upperTop + 43;
    const int summaryGap = 19;
    const int summaryValueX = summaryLeft + 145;

    drawInfoRow(summaryLeft, summaryRight, summaryY + summaryGap * 0, "Computer Name", systemInfo.computerName, summaryValueX);
    drawInfoRow(summaryLeft, summaryRight, summaryY + summaryGap * 1, "Operating System", systemInfo.osName, summaryValueX);
    drawInfoRow(summaryLeft, summaryRight, summaryY + summaryGap * 2, "Version", systemInfo.osVersion, summaryValueX);
    drawInfoRow(summaryLeft, summaryRight, summaryY + summaryGap * 3, "Build", systemInfo.osBuild, summaryValueX);
    drawInfoRow(summaryLeft, summaryRight, summaryY + summaryGap * 4, "System Type", systemInfo.systemType, summaryValueX);
    drawInfoRow(summaryLeft, summaryRight, summaryY + summaryGap * 5, "Manufacturer", systemInfo.systemManufacturer, summaryValueX);
    drawInfoRow(summaryLeft, summaryRight, summaryY + summaryGap * 6, "Model", systemInfo.systemModel, summaryValueX);
    drawInfoRow(summaryLeft, summaryRight, summaryY + summaryGap * 7, "BIOS Version", biosVersionText, summaryValueX);
    drawInfoRow(summaryLeft, summaryRight, summaryY + summaryGap * 8, "Secure Boot", systemInfo.secureBoot, summaryValueX);
    drawInfoRow(summaryLeft, summaryRight, summaryY + summaryGap * 9, "Windows Install Date", systemInfo.installedOn, summaryValueX);

    // ----------------------------------------------------
    // Hardware Overview
    // ----------------------------------------------------

    drawPanel(
        hardwareLeft,
        upperTop,
        hardwareRight,
        upperBottom
    );

    drawPanelTitle(
        hardwareLeft,
        upperTop,
        L"sysinfo_cpu.png",
        "Hardware Overview"
    );

    std::string motherboardText =
        systemInfo.motherboardManufacturer;

    if (systemInfo.motherboardModel != "--")
    {
        if (
            !motherboardText.empty() &&
            motherboardText != "--"
        )
        {
            motherboardText += " ";
        }

        motherboardText +=
            systemInfo.motherboardModel;
    }

    std::ostringstream processorText;
    processorText << systemInfo.cpuName;

    if (systemInfo.cpuCores > 0 ||
        systemInfo.cpuThreads > 0)
    {
        processorText
            << " ("
            << systemInfo.cpuCores
            << " Cores, "
            << systemInfo.cpuThreads
            << " Threads)";
    }

    std::string graphicsText =
        gpuCardPrimary;

    if (gpuCardSecondary != "--")
    {
        graphicsText +=
            " (" +
            gpuCardSecondary +
            ")";
    }

    std::string storageText =
        storageCardPrimary;

    if (storageCardSecondary != "--")
    {
        storageText +=
            " (" +
            storageCardSecondary +
            ")";
    }

    int hardwareY = upperTop + 43;
    const int hardwareGap = 21;
    const int hardwareValueX = hardwareLeft + 155;

    drawInfoRow(hardwareLeft, hardwareRight, hardwareY + hardwareGap * 0, "Motherboard", motherboardText, hardwareValueX);
    drawInfoRow(hardwareLeft, hardwareRight, hardwareY + hardwareGap * 1, "Processor", processorText.str(), hardwareValueX);
    drawInfoRow(hardwareLeft, hardwareRight, hardwareY + hardwareGap * 2, "Graphics Card", graphicsText, hardwareValueX);
    drawInfoRow(hardwareLeft, hardwareRight, hardwareY + hardwareGap * 3, "Installed RAM", memoryCardPrimary, hardwareValueX);
    drawInfoRow(hardwareLeft, hardwareRight, hardwareY + hardwareGap * 4, "Memory Speed", systemInfo.memorySpeed, hardwareValueX);
    drawInfoRow(hardwareLeft, hardwareRight, hardwareY + hardwareGap * 5, "Storage Drive", storageText, hardwareValueX);
    drawInfoRow(hardwareLeft, hardwareRight, hardwareY + hardwareGap * 6, "Network Adapter", systemInfo.networkAdapter, hardwareValueX);
    drawInfoRow(hardwareLeft, hardwareRight, hardwareY + hardwareGap * 7, "Audio Device", systemInfo.audioDevice, hardwareValueX);
    drawInfoRow(hardwareLeft, hardwareRight, hardwareY + hardwareGap * 8, "Display Resolution", systemInfo.displayResolution, hardwareValueX);

    // ----------------------------------------------------
    // Software & Drivers
    // ----------------------------------------------------

    const int lowerTop = 468;
    const int lowerBottom = 700;

    const int softwareLeft = 25;
    const int softwareRight = 340;
    const int storageLeft = 350;
    const int storageRight = 670;
    const int statusLeft = 680;
    const int statusRight = 995;

    drawPanel(
        softwareLeft,
        lowerTop,
        softwareRight,
        lowerBottom
    );

    drawPanelTitle(
        softwareLeft,
        lowerTop,
        L"settings.png",
        "Software & Drivers"
    );

    std::string gpuDriverText =
        selectedInfoGpu != nullptr
        ? selectedInfoGpu->driverVersion
        : systemInfo.gpuDriverVersion;

    if (
        selectedInfoGpu != nullptr &&
        selectedInfoGpu->driverDate != "--"
    )
    {
        gpuDriverText +=
            " (" +
            selectedInfoGpu->driverDate +
            ")";
    }

    std::string directXText =
        selectedInfoGpu != nullptr &&
        selectedInfoGpu->directXVersion != "--"
        ? selectedInfoGpu->directXVersion
        : systemInfo.directXVersion;

    int softwareY = lowerTop + 50;
    const int softwareGap = 25;
    const int softwareValueX = softwareLeft + 115;

    drawInfoRow(softwareLeft, softwareRight, softwareY + softwareGap * 0, "DirectX Version", directXText, softwareValueX);
    drawInfoRow(softwareLeft, softwareRight, softwareY + softwareGap * 1, ".NET Runtime", systemInfo.dotNetRuntime, softwareValueX);
    drawInfoRow(softwareLeft, softwareRight, softwareY + softwareGap * 2, "GPU Driver", gpuDriverText, softwareValueX);
    drawInfoRow(softwareLeft, softwareRight, softwareY + softwareGap * 3, "Chipset Driver", systemInfo.chipsetDriver, softwareValueX);
    drawInfoRow(softwareLeft, softwareRight, softwareY + softwareGap * 4, "Audio Driver", systemInfo.audioDriver, softwareValueX);
    drawInfoRow(softwareLeft, softwareRight, softwareY + softwareGap * 5, "Network Driver", systemInfo.networkDriver, softwareValueX);
    drawInfoRow(softwareLeft, softwareRight, softwareY + softwareGap * 6, "Last Windows Update", systemInfo.lastWindowsUpdate, softwareValueX);

    // ----------------------------------------------------
    // Storage & Memory
    // ----------------------------------------------------

    drawPanel(
        storageLeft,
        lowerTop,
        storageRight,
        lowerBottom
    );

    drawPanelTitle(
        storageLeft,
        lowerTop,
        L"sysinfo_storage.png",
        "Storage & Memory"
    );

    double physicalMemoryPercent =
        totalRamGB > 0.0
        ? (usedRamGB / totalRamGB) * 100.0
        : 0.0;

    std::ostringstream memoryUsageText;
    memoryUsageText
        << std::fixed
        << std::setprecision(1)
        << usedRamGB
        << " GB / "
        << totalRamGB
        << " GB";

    drawText(
        hdc,
        "Memory",
        storageLeft + 16,
        lowerTop + 52,
        textPrimary,
        smallFont
    );

    drawText(
        hdc,
        memoryUsageText.str(),
        storageRight - 128,
        lowerTop + 52,
        textSecondary,
        smallFont
    );

    drawUsageBar(
        storageLeft + 16,
        storageRight - 16,
        lowerTop + 73,
        physicalMemoryPercent,
        accent
    );

    drawText(
        hdc,
        "Used  " +
            formatGb(usedRamGB) +
            "   Available  " +
            formatGb(
                (std::max)(
                    0.0,
                    totalRamGB - usedRamGB
                )
            ),
        storageLeft + 16,
        lowerTop + 89,
        textSecondary,
        smallFont
    );

    double selectedStorageTotalGB = 0.0;
    double selectedStorageFreeGB = 0.0;

    if (selectedInfoDisk != nullptr)
    {
        const DiskVolumeUsage volumeUsage =
            queryDiskVolumeUsage(*selectedInfoDisk);

        if (volumeUsage.valid)
        {
            selectedStorageTotalGB = volumeUsage.totalGB;
            selectedStorageFreeGB = volumeUsage.freeGB;
        }
    }

    double selectedStorageUsedGB =
        (std::max)(
            0.0,
            selectedStorageTotalGB -
            selectedStorageFreeGB
        );

    double selectedStoragePercent =
        selectedStorageTotalGB > 0.0
        ? selectedStorageUsedGB /
            selectedStorageTotalGB *
            100.0
        : 0.0;

    drawText(
        hdc,
        "Storage",
        storageLeft + 16,
        lowerTop + 118,
        textPrimary,
        smallFont
    );

    std::string storageUsageText = "--";
    if (selectedStorageTotalGB > 0.0)
    {
        storageUsageText =
            formatGb(selectedStorageUsedGB) +
            " / " +
            formatGb(selectedStorageTotalGB);
    }

    drawText(
        hdc,
        storageUsageText,
        storageRight - 128,
        lowerTop + 118,
        textSecondary,
        smallFont
    );

    drawUsageBar(
        storageLeft + 16,
        storageRight - 16,
        lowerTop + 139,
        selectedStoragePercent,
        uiColor(RGB(50, 205, 220))
    );

    drawText(
        hdc,
        selectedStorageTotalGB > 0.0
            ? "Used  " +
                formatGb(selectedStorageUsedGB) +
                "   Free  " +
                formatGb(selectedStorageFreeGB)
            : "Volume usage unavailable",
        storageLeft + 16,
        lowerTop + 155,
        textSecondary,
        smallFont
    );

    MemoryPerformanceDetails memoryDetails =
        getMemoryPerformanceDetails();

    double virtualUsedGB = 0.0;
    double virtualLimitGB = 0.0;
    double virtualPercent = 0.0;

    if (
        memoryDetails.valid &&
        memoryDetails.commitLimitBytes > 0
    )
    {
        virtualUsedGB =
            memoryDetails.commitTotalBytes /
            (1024.0 * 1024.0 * 1024.0);

        virtualLimitGB =
            memoryDetails.commitLimitBytes /
            (1024.0 * 1024.0 * 1024.0);

        virtualPercent =
            virtualUsedGB /
            virtualLimitGB *
            100.0;
    }

    drawText(
        hdc,
        "Virtual Memory",
        storageLeft + 16,
        lowerTop + 184,
        textPrimary,
        smallFont
    );

    drawText(
        hdc,
        memoryDetails.valid
            ? formatGb(virtualUsedGB) +
                " / " +
                formatGb(virtualLimitGB)
            : "--",
        storageRight - 128,
        lowerTop + 184,
        textSecondary,
        smallFont
    );

    drawUsageBar(
        storageLeft + 16,
        storageRight - 16,
        lowerTop + 205,
        virtualPercent,
        uiColor(RGB(60, 190, 230))
    );

    // ----------------------------------------------------
    // System Status
    // ----------------------------------------------------

    drawPanel(
        statusLeft,
        lowerTop,
        statusRight,
        lowerBottom
    );

    drawPanelTitle(
        statusLeft,
        lowerTop,
        L"sysinfo_os.png",
        "System Status"
    );

    auto drawStatusRow =
        [&](int y,
            const std::string& label,
            const std::string& value,
            const std::string& detail,
            COLORREF statusColor)
    {
        HBRUSH dotBrush =
            CreateSolidBrush(statusColor);

        HGDIOBJ oldDotBrush =
            SelectObject(hdc, dotBrush);

        HGDIOBJ oldDotPen =
            SelectObject(
                hdc,
                GetStockObject(NULL_PEN)
            );

        drawUiEllipse(
            hdc,
            statusLeft + 16,
            y + 4,
            statusLeft + 24,
            y + 12
        );

        SelectObject(hdc, oldDotPen);
        SelectObject(hdc, oldDotBrush);
        DeleteObject(dotBrush);

        drawText(
            hdc,
            label,
            statusLeft + 34,
            y,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            value,
            statusLeft + 170,
            y,
            statusColor,
            smallFont
        );

        drawText(
            hdc,
            fitInfoText(
                detail,
                statusRight - statusLeft - 188,
                smallFont
            ),
            statusLeft + 170,
            y + 17,
            textSecondary,
            smallFont
        );
    };

    bool driverInfoAvailable =
        systemInfo.gpuDriverVersion != "--" ||
        systemInfo.networkDriver != "--" ||
        systemInfo.audioDriver != "--";

    COLORREF secureBootColor =
        systemInfo.secureBoot == "Enabled"
        ? healthyGreen
        : (
            systemInfo.secureBoot == "Disabled"
            ? warningOrange
            : textSecondary
          );

    std::string sensorStatus = "Unavailable";
    std::string sensorDetail =
        "No hardware temperature source is available.";
    COLORREF sensorColor = warningOrange;

    if (temperatureStats.hardwareSensorAvailable)
    {
        sensorStatus = "Active";
        sensorDetail =
            "Hardware sensors are reporting live data.";
        sensorColor = healthyGreen;
    }
    else if (temperatureStats.acpiAvailable)
    {
        sensorStatus = "ACPI Only";
        sensorDetail =
            "Windows ACPI thermal data is available.";
        sensorColor = warningOrange;
    }

    drawStatusRow(
        lowerTop + 48,
        "System Health",
        "Monitoring Active",
        "SysMon is receiving live system statistics.",
        healthyGreen
    );

    drawStatusRow(
        lowerTop + 86,
        "Driver Status",
        driverInfoAvailable
            ? "Available"
            : "Partial",
        driverInfoAvailable
            ? "Driver information was detected from Windows."
            : "Some driver metadata is unavailable.",
        driverInfoAvailable
            ? healthyGreen
            : warningOrange
    );

    drawStatusRow(
        lowerTop + 124,
        "Security Status",
        systemInfo.secureBoot == "Enabled"
            ? "Secure Boot On"
            : (
                systemInfo.secureBoot == "Disabled"
                ? "Secure Boot Off"
                : "Unavailable"
              ),
        systemInfo.secureBoot == "Enabled"
            ? "UEFI Secure Boot is enabled."
            : "Secure Boot state reported by Windows.",
        secureBootColor
    );

    drawStatusRow(
        lowerTop + 162,
        "Sensor Status",
        sensorStatus,
        sensorDetail,
        sensorColor
    );

    drawText(
        hdc,
        "\" A healthier system powers a brighter you. \"",
        statusLeft + 58,
        lowerBottom - 24,
        uiColor(RGB(106, 119, 135)),
        smallFont
    );
}

else if (currentPage == AppPage::Temperatures)
{
    const COLORREF tempCard =
        uiColor(RGB(18, 27, 37));

    const COLORREF tempCardInner =
        uiColor(RGB(27, 38, 51));

    const COLORREF tempLine =
        uiColor(RGB(38, 51, 65));

    const COLORREF cpuAccent =
        uiColor(RGB(255, 137, 42));

    const COLORREF gpuAccent =
        uiColor(RGB(72, 230, 105));

    const COLORREF systemAccent =
        uiColor(RGB(44, 181, 246));

    const COLORREF boardAccent =
        uiColor(RGB(173, 75, 235));

    const COLORREF liveGreen =
        uiColor(RGB(65, 228, 120));

    const COLORREF unavailableColor =
        uiColor(RGB(116, 128, 142));

    auto formatTemperature =
        [](double value)
        -> std::string
    {
        if (value < 0.0)
        {
            return std::string("--") + SettingsRuntime::temperatureSuffix();
        }

        std::ostringstream stream;
        stream
            << std::fixed
            << std::setprecision(0)
            << SettingsRuntime::temperature(value);

        return stream.str();
    };

    auto formatPower =
        [](double value)
        -> std::string
    {
        if (value < 0.0)
        {
            return "--";
        }

        std::ostringstream stream;
        stream
            << std::fixed
            << std::setprecision(1)
            << value
            << " W";
        return stream.str();
    };

    auto shortenTemperatureText =
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

    auto historySampleCount =
        []()
        -> size_t
    {
        if (cpuHistoryRangeSeconds <= 15)
            return 30;
        if (cpuHistoryRangeSeconds <= 30)
            return 60;
        return 120;
    };

    const size_t maxHistorySamples =
        historySampleCount();

    int selectedGpu =
        selectedTemperatureGpuIndex;

    if (gpuStats.empty())
    {
        selectedGpu = -1;
    }
    else
    {
        selectedGpu =
            std::clamp(
                selectedGpu,
                0,
                static_cast<int>(gpuStats.size()) - 1
            );

        selectedTemperatureGpuIndex =
            selectedGpu;
    }

    const GpuStats* temperatureGpu =
        selectedGpu >= 0
        ? &gpuStats[static_cast<size_t>(selectedGpu)]
        : nullptr;

    // --------------------------------------------------------
    // PAGE HEADER
    // --------------------------------------------------------

    drawRoundedBox(
        hdc,
        35,
        62,
        81,
        108,
        uiColor(RGB(29, 41, 55))
    );

    drawTintedPngImage(
        hdc,
        L"temp.png",
        44,
        71,
        28,
        28,
        uiColor(RGB(235, 239, 244))
    );

    drawText(
        hdc,
        "Temperatures",
        95,
        66,
        textPrimary,
        mediumFont
    );

    drawText(
        hdc,
        "Monitor component temperatures in real time.",
        95,
        96,
        textSecondary,
        subtitleFont
    );

    std::string temperatureRangeLabel =
        "Last " +
        std::to_string(cpuHistoryRangeSeconds) +
        " Seconds";

    drawRoundedBox(
        hdc,
        865,
        64,
        1015,
        104,
        uiColor(RGB(15, 27, 38))
    );

    // Clock glyph.
    HPEN clockPen =
        CreatePen(
            PS_SOLID,
            1,
            uiColor(RGB(205, 221, 234))
        );

    HGDIOBJ oldClockPen =
        SelectObject(hdc, clockPen);

    HGDIOBJ oldClockBrush =
        SelectObject(
            hdc,
            GetStockObject(HOLLOW_BRUSH)
        );

    drawUiEllipse(
        hdc,
        878,
        76,
        894,
        92
    );

    MoveToEx(hdc, 886, 79, nullptr);
    LineTo(hdc, 886, 84);
    LineTo(hdc, 890, 86);

    SelectObject(hdc, oldClockBrush);
    SelectObject(hdc, oldClockPen);
    DeleteObject(clockPen);

    drawText(
        hdc,
        temperatureRangeLabel,
        902,
        77,
        textPrimary,
        smallFont
    );

    drawText(
        hdc,
        cpuHistoryRangeDropdownOpen
            ? "^"
            : "v",
        992,
        77,
        textSecondary,
        smallFont
    );

    // --------------------------------------------------------
    // TOP TEMPERATURE CARDS
    // --------------------------------------------------------

    auto drawTemperatureCard =
        [&](int left,
            int right,
            const std::string& title,
            const wchar_t* iconPath,
            double temperature,
            const std::string& sourceText,
            const std::vector<double>& history,
            COLORREF accent)
    {
        const int top = 118;
        const int bottom = 222;

        drawRoundedBox(
            hdc,
            left,
            top,
            right,
            bottom,
            tempCard
        );

        drawRoundedBox(
            hdc,
            left + 14,
            top + 14,
            left + 50,
            top + 50,
            tempCardInner
        );

        drawTintedPngImage(
            hdc,
            iconPath,
            left + 22,
            top + 22,
            18,
            18,
            uiColor(RGB(226, 233, 240))
        );

        drawText(
            hdc,
            title,
            left + 60,
            top + 15,
            textPrimary,
            labelFont
        );

        drawText(
            hdc,
            formatTemperature(temperature),
            left + 60,
            top + 43,
            textPrimary,
            mediumFont
        );

        // Keep the compact top cards clean.  Full sensor names are shown in
        // Sensor Sources / Temperature Details below; the summary card only
        // needs an accurate live/unavailable state.
        (void)sourceText;

        drawText(
            hdc,
            temperature >= 0.0
                ? "Live"
                : "Unavailable",
            left + 60,
            top + 75,
            temperature >= 0.0
                ? liveGreen
                : unavailableColor,
            smallFont
        );

        if (history.size() >= 2)
        {
            size_t sampleCount =
                history.size();

            if (
                maxHistorySamples > 0 &&
                sampleCount > maxHistorySamples
            )
            {
                sampleCount =
                    maxHistorySamples;
            }

            const size_t startIndex =
                history.size() - sampleCount;

            ScopedUiGraphics scopedGraphics(hdc);
    Gdiplus::Graphics& graphics =
        scopedGraphics.get();
            graphics.SetSmoothingMode(
                Gdiplus::SmoothingModeAntiAlias
            );

            Gdiplus::GraphicsPath fillPath;
            Gdiplus::GraphicsPath linePath;

            const int graphLeft = left + 132;
            const int graphTop = top + 48;
            const int graphWidth = right - graphLeft - 12;
            const int graphHeight = 36;

            float previousX = 0.0f;
            float previousY = 0.0f;

            for (size_t i = 0;
                 i < sampleCount;
                 i++)
            {
                double value =
                    std::clamp(
                        history[startIndex + i],
                        0.0,
                        100.0
                    );

                float pointX =
                    static_cast<float>(graphLeft) +
                    static_cast<float>(
                        i *
                        static_cast<double>(graphWidth) /
                        (sampleCount - 1)
                    );

                float pointY =
                    static_cast<float>(graphTop + graphHeight) -
                    static_cast<float>(
                        (value / 100.0) * graphHeight
                    );

                if (i == 0)
                {
                    linePath.StartFigure();
                    linePath.AddLine(
                        pointX,
                        pointY,
                        pointX,
                        pointY
                    );

                    fillPath.StartFigure();
                    fillPath.AddLine(
                        pointX,
                        static_cast<float>(graphTop + graphHeight),
                        pointX,
                        pointY
                    );
                }
                else
                {
                    linePath.AddLine(
                        previousX,
                        previousY,
                        pointX,
                        pointY
                    );

                    fillPath.AddLine(
                        previousX,
                        previousY,
                        pointX,
                        pointY
                    );
                }

                previousX = pointX;
                previousY = pointY;
            }

            fillPath.AddLine(
                static_cast<float>(graphLeft + graphWidth),
                static_cast<float>(graphTop + graphHeight),
                static_cast<float>(graphLeft),
                static_cast<float>(graphTop + graphHeight)
            );
            fillPath.CloseFigure();

            Gdiplus::Color accentColor(
                255,
                GetRValue(accent),
                GetGValue(accent),
                GetBValue(accent)
            );

            Gdiplus::Color transparentAccent(
                18,
                GetRValue(accent),
                GetGValue(accent),
                GetBValue(accent)
            );

            Gdiplus::LinearGradientBrush fillBrush(
                Gdiplus::Point(graphLeft, graphTop),
                Gdiplus::Point(graphLeft, graphTop + graphHeight),
                Gdiplus::Color(
                    88,
                    GetRValue(accent),
                    GetGValue(accent),
                    GetBValue(accent)
                ),
                transparentAccent
            );

            Gdiplus::Pen glowPen(
                Gdiplus::Color(
                    45,
                    GetRValue(accent),
                    GetGValue(accent),
                    GetBValue(accent)
                ),
                5.0f
            );

            Gdiplus::Pen linePen(
                accentColor,
                1.6f
            );

            graphics.FillPath(
                &fillBrush,
                &fillPath
            );
            graphics.DrawPath(
                &glowPen,
                &linePath
            );
            graphics.DrawPath(
                &linePen,
                &linePath
            );
        }
    };

    std::string cpuSource =
        temperatureStats.cpuSensorName != "--"
        ? temperatureStats.cpuSensorName
        : "Live sensor";

    std::string gpuSource =
        temperatureGpu != nullptr
        ? (
            gpuStats.size() > 1
            ? "GPU " +
              std::to_string(selectedGpu + 1) +
              " / " +
              std::to_string(gpuStats.size())
            : "Live GPU sensor"
          )
        : "No GPU sensor";

    std::string systemSource =
        temperatureStats.acpiAvailable
        ? "System / ACPI"
        : "Hottest live sensor";

    std::string boardSource =
        temperatureStats.motherboardSensorName != "--"
        ? temperatureStats.motherboardSensorName
        : "Board sensor";

    const std::vector<double> emptyTemperatureHistory;

    drawTemperatureCard(
        35,
        270,
        "CPU",
        L"sidebar_cpu.png",
        temperatureStats.cpuTemperatureC,
        cpuSource,
        temperatureStats.cpuTemperatureHistory,
        cpuAccent
    );

    drawTemperatureCard(
        282,
        517,
        "GPU",
        L"sidebar_gpu.png",
        temperatureGpu != nullptr
            ? temperatureGpu->temperatureC
            : -1.0,
        gpuSource,
        temperatureGpu != nullptr
            ? temperatureGpu->temperatureHistory
            : emptyTemperatureHistory,
        gpuAccent
    );

    drawTemperatureCard(
        529,
        764,
        "System",
        L"sidebar_temperatures.png",
        temperatureStats.systemTemperatureC,
        systemSource,
        temperatureStats.systemTemperatureHistory,
        systemAccent
    );

    drawTemperatureCard(
        776,
        1015,
        "Motherboard",
        L"sidebar_temperatures.png",
        temperatureStats.motherboardTemperatureC,
        boardSource,
        temperatureStats.motherboardTemperatureHistory,
        boardAccent
    );

    // --------------------------------------------------------
    // MAIN TEMPERATURE HISTORY
    // --------------------------------------------------------

    const int graphPanelLeft = 35;
    const int graphPanelTop = 236;
    const int graphPanelRight = 720;
    const int graphPanelBottom = 510;

    drawRoundedBox(
        hdc,
        graphPanelLeft,
        graphPanelTop,
        graphPanelRight,
        graphPanelBottom,
        tempCard
    );

    drawTintedPngImage(
        hdc,
        L"performance.png",
        graphPanelLeft + 18,
        graphPanelTop + 16,
        18,
        18,
        uiColor(RGB(220, 228, 236))
    );

    drawText(
        hdc,
        "Temperature History",
        graphPanelLeft + 46,
        graphPanelTop + 15,
        textPrimary,
        labelFont
    );

    struct TemperatureHistoryLine
    {
        const std::vector<double>* history;
        COLORREF color;
        std::string label;
    };

    std::vector<TemperatureHistoryLine> temperatureLines;

    if (!temperatureStats.cpuTemperatureHistory.empty())
    {
        temperatureLines.push_back(
            {
                &temperatureStats.cpuTemperatureHistory,
                cpuAccent,
                "CPU"
            }
        );
    }

    if (
        temperatureGpu != nullptr &&
        !temperatureGpu->temperatureHistory.empty()
    )
    {
        temperatureLines.push_back(
            {
                &temperatureGpu->temperatureHistory,
                gpuAccent,
                "GPU"
            }
        );
    }

    if (!temperatureStats.systemTemperatureHistory.empty())
    {
        temperatureLines.push_back(
            {
                &temperatureStats.systemTemperatureHistory,
                systemAccent,
                "System"
            }
        );
    }

    if (!temperatureStats.motherboardTemperatureHistory.empty())
    {
        temperatureLines.push_back(
            {
                &temperatureStats.motherboardTemperatureHistory,
                boardAccent,
                "Board"
            }
        );
    }

    int legendX = graphPanelRight - 205;
    for (const auto& line : temperatureLines)
    {
        HBRUSH legendBrush =
            CreateSolidBrush(line.color);

        HGDIOBJ oldLegendBrush =
            SelectObject(hdc, legendBrush);

        HGDIOBJ oldLegendPen =
            SelectObject(
                hdc,
                GetStockObject(NULL_PEN)
            );

        drawUiEllipse(
            hdc,
            legendX,
            graphPanelTop + 20,
            legendX + 10,
            graphPanelTop + 30
        );

        SelectObject(hdc, oldLegendPen);
        SelectObject(hdc, oldLegendBrush);
        DeleteObject(legendBrush);

        drawText(
            hdc,
            line.label,
            legendX + 16,
            graphPanelTop + 16,
            textPrimary,
            smallFont
        );

        legendX +=
            22 +
            static_cast<int>(line.label.size()) * 7;
    }

    const int graphLeft = graphPanelLeft + 54;
    const int graphTop = graphPanelTop + 64;
    const int graphWidth = graphPanelRight - graphLeft - 16;
    const int graphHeight = 168;

    drawGraphGrid(
        hdc,
        graphLeft,
        graphTop,
        graphWidth,
        graphHeight
    );

    for (int index = 0;
         index <= 5;
         index++)
    {
        int value = 100 - index * 20;
        int labelY =
            graphTop +
            index * graphHeight / 5 -
            7;

        drawText(
            hdc,
            SettingsRuntime::temperature(value),
            graphPanelLeft + 14,
            labelY,
            textSecondary,
            smallFont
        );
    }

    // Keep the temporary GDI+ transform strictly scoped to the history
    // graph. ScopedUiGraphics temporarily resets the underlying HDC world
    // transform, so letting it live beyond this block would make the
    // Temperature Details and lower status cards render at 1x while the
    // rest of the page is scaled. That is what caused the maximized-page
    // overlap/misalignment.
    {
        ScopedUiGraphics scopedHistoryGraphics(hdc);
        Gdiplus::Graphics& historyGraphics =
            scopedHistoryGraphics.get();
        historyGraphics.SetSmoothingMode(
            Gdiplus::SmoothingModeAntiAlias
        );

        for (const auto& line : temperatureLines)
        {
            const std::vector<double>& history =
                *line.history;

            size_t sampleCount =
                history.size();

            if (
                maxHistorySamples > 0 &&
                sampleCount > maxHistorySamples
            )
            {
                sampleCount = maxHistorySamples;
            }

            if (sampleCount < 2)
            {
                continue;
            }

            const size_t startIndex =
                history.size() - sampleCount;

            std::vector<Gdiplus::PointF> points;
            points.reserve(sampleCount);

            for (size_t i = 0;
                 i < sampleCount;
                 i++)
            {
                double temperature =
                    std::clamp(
                        history[startIndex + i],
                        0.0,
                        100.0
                    );

                float x =
                    static_cast<float>(graphLeft) +
                    static_cast<float>(
                        i *
                        static_cast<double>(graphWidth) /
                        (sampleCount - 1)
                    );

                float y =
                    static_cast<float>(graphTop + graphHeight) -
                    static_cast<float>(
                        (temperature / 100.0) *
                        graphHeight
                    );

                points.emplace_back(x, y);
            }

            Gdiplus::Pen glowPen(
                Gdiplus::Color(
                    45,
                    GetRValue(line.color),
                    GetGValue(line.color),
                    GetBValue(line.color)
                ),
                5.0f
            );

            Gdiplus::Pen linePen(
                Gdiplus::Color(
                    255,
                    GetRValue(line.color),
                    GetGValue(line.color),
                    GetBValue(line.color)
                ),
                1.8f
            );

            historyGraphics.DrawLines(
                &glowPen,
                points.data(),
                static_cast<INT>(points.size())
            );

            historyGraphics.DrawLines(
                &linePen,
                points.data(),
                static_cast<INT>(points.size())
            );
        }
    }

    // --------------------------------------------------------
    // TEMPERATURE DETAILS
    // --------------------------------------------------------

    const int detailLeft = 732;
    const int detailTop = 236;
    const int detailRight = 1015;
    const int detailBottom = 690;

    drawRoundedBox(
        hdc,
        detailLeft,
        detailTop,
        detailRight,
        detailBottom,
        tempCard
    );

    drawTintedPngImage(
        hdc,
        L"temp.png",
        detailLeft + 18,
        detailTop + 16,
        18,
        18,
        uiColor(RGB(225, 232, 239))
    );

    drawText(
        hdc,
        "Temperature Details",
        detailLeft + 46,
        detailTop + 15,
        textPrimary,
        labelFont
    );

    drawText(
        hdc,
        "Sensor",
        detailLeft + 16,
        detailTop + 54,
        textSecondary,
        smallFont
    );

    drawText(
        hdc,
        "Temp",
        detailLeft + 160,
        detailTop + 54,
        textSecondary,
        smallFont
    );

    drawText(
        hdc,
        "Status",
        detailLeft + 220,
        detailTop + 54,
        textSecondary,
        smallFont
    );

    std::vector<std::pair<std::string, double>> detailSensors;

    if (temperatureStats.cpuTemperatureC >= 0.0)
    {
        detailSensors.push_back(
            {
                "CPU (Package)",
                temperatureStats.cpuTemperatureC
            }
        );
    }

    for (size_t index = 0;
         index < temperatureStats.cpuCoreTemperatures.size();
         index++)
    {
        double value =
            temperatureStats.cpuCoreTemperatures[index];

        if (value >= 0.0)
        {
            detailSensors.push_back(
                {
                    "CPU (Core " +
                        std::to_string(index + 1) +
                        ")",
                    value
                }
            );
        }
    }

    if (
        temperatureGpu != nullptr &&
        temperatureGpu->temperatureC >= 0.0
    )
    {
        detailSensors.push_back(
            {
                gpuStats.size() > 1
                    ? "GPU " +
                      std::to_string(selectedGpu + 1)
                    : "GPU (Core)",
                temperatureGpu->temperatureC
            }
        );
    }

    for (const auto& sensor : temperatureStats.motherboardSensors)
    {
        if (sensor.temperatureC >= 0.0)
        {
            detailSensors.push_back(
                {
                    sensor.name,
                    sensor.temperatureC
                }
            );
        }
    }

    for (const auto& sensor : temperatureStats.sensors)
    {
        if (sensor.temperatureC >= 0.0)
        {
            detailSensors.push_back(
                {
                    sensor.name,
                    sensor.temperatureC
                }
            );
        }
    }

    const int maximumDetailRows = 13;
    int detailRows =
        (std::min)(
            maximumDetailRows,
            static_cast<int>(detailSensors.size())
        );

    for (int index = 0;
         index < detailRows;
         index++)
    {
        const int rowY =
            detailTop + 80 +
            index * 25;

        HPEN rowPen =
            CreatePen(
                PS_SOLID,
                1,
                tempLine
            );

        HGDIOBJ oldRowPen =
            SelectObject(hdc, rowPen);

        MoveToEx(
            hdc,
            detailLeft + 14,
            rowY + 20,
            nullptr
        );

        LineTo(
            hdc,
            detailRight - 14,
            rowY + 20
        );

        SelectObject(hdc, oldRowPen);
        DeleteObject(rowPen);

        drawText(
            hdc,
            shortenTemperatureText(
                detailSensors[index].first,
                17
            ),
            detailLeft + 16,
            rowY,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            formatTemperature(
                detailSensors[index].second
            ),
            detailLeft + 158,
            rowY,
            textPrimary,
            smallFont
        );

        HBRUSH statusBrush =
            CreateSolidBrush(liveGreen);

        HGDIOBJ oldStatusBrush =
            SelectObject(hdc, statusBrush);

        HGDIOBJ oldStatusPen =
            SelectObject(
                hdc,
                GetStockObject(NULL_PEN)
            );

        drawUiEllipse(
            hdc,
            detailLeft + 220,
            rowY + 5,
            detailLeft + 228,
            rowY + 13
        );

        SelectObject(hdc, oldStatusPen);
        SelectObject(hdc, oldStatusBrush);
        DeleteObject(statusBrush);

        drawText(
            hdc,
            "Live",
            detailLeft + 234,
            rowY,
            liveGreen,
            smallFont
        );
    }

    if (detailSensors.empty())
    {
        drawText(
            hdc,
            "No live temperature sensors are currently available.",
            detailLeft + 16,
            detailTop + 90,
            textSecondary,
            smallFont
        );
    }

    // --------------------------------------------------------
    // LOWER STATUS PANELS
    // --------------------------------------------------------

    auto drawLowerPanelTitle =
        [&](int left,
            int top,
            const wchar_t* icon,
            const std::string& title)
    {
        drawTintedPngImage(
            hdc,
            icon,
            left + 16,
            top + 16,
            18,
            18,
            uiColor(RGB(225, 232, 239))
        );

        drawText(
            hdc,
            title,
            left + 44,
            top + 15,
            textPrimary,
            labelFont
        );
    };

    const int lowerTop = 525;
    const int lowerBottom = 688;

    drawRoundedBox(
        hdc,
        35,
        lowerTop,
        258,
        lowerBottom,
        tempCard
    );

    drawLowerPanelTitle(
        35,
        lowerTop,
        L"sidebar_temperatures.png",
        "Sensor Status"
    );

    bool anyTemperatureAvailable =
        temperatureStats.cpuTemperatureC >= 0.0 ||
        temperatureStats.motherboardTemperatureC >= 0.0 ||
        (
            temperatureGpu != nullptr &&
            temperatureGpu->temperatureC >= 0.0
        );

    std::string sensorHeadline =
        temperatureStats.hardwareSensorAvailable
        ? "Sensors active"
        : (
            temperatureStats.acpiAvailable
            ? "ACPI active"
            : "Unavailable"
          );

    // Large live-sensor check, matching the reference status card.
    {
        ScopedUiGraphics scopedStatusGraphics(hdc);
        Gdiplus::Graphics& statusGraphics =
            scopedStatusGraphics.get();
        statusGraphics.SetSmoothingMode(
            Gdiplus::SmoothingModeAntiAlias
        );

        Gdiplus::Color statusColor =
            anyTemperatureAvailable
            ? themeGdiColor(255, 72, 236, 124)
            : themeGdiColor(255, 116, 128, 142);

        Gdiplus::SolidBrush statusBrush(statusColor);
        statusGraphics.FillEllipse(
            &statusBrush,
            52,
            lowerTop + 58,
            46,
            46
        );

        if (anyTemperatureAvailable)
        {
            Gdiplus::Pen checkPen(
                themeGdiColor(255, 10, 45, 35),
                5.0f
            );

            statusGraphics.DrawLine(
                &checkPen,
                64.0f,
                static_cast<float>(lowerTop + 82),
                72.0f,
                static_cast<float>(lowerTop + 90)
            );

            statusGraphics.DrawLine(
                &checkPen,
                72.0f,
                static_cast<float>(lowerTop + 90),
                88.0f,
                static_cast<float>(lowerTop + 70)
            );
        }
    }

    drawText(
        hdc,
        sensorHeadline,
        106,
        lowerTop + 64,
        anyTemperatureAvailable
            ? liveGreen
            : unavailableColor,
        smallFont
    );

    drawText(
        hdc,
        std::to_string(detailSensors.size()) +
            " live temperature readings",
        108,
        lowerTop + 90,
        textSecondary,
        smallFont
    );

    drawRoundedBox(
        hdc,
        270,
        lowerTop,
        490,
        lowerBottom,
        tempCard
    );

    drawLowerPanelTitle(
        270,
        lowerTop,
        L"sysinfo_os.png",
        "Sensor Sources"
    );

    auto drawSourceLine =
        [&](int y,
            const std::string& label,
            const std::string& value)
    {
        drawText(
            hdc,
            label,
            286,
            y,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            shortenTemperatureText(value, 18),
            354,
            y,
            textPrimary,
            smallFont
        );
    };

    drawSourceLine(
        lowerTop + 58,
        "CPU:",
        temperatureStats.cpuSensorName
    );

    drawSourceLine(
        lowerTop + 84,
        "Board:",
        temperatureStats.motherboardSensorName
    );

    drawSourceLine(
        lowerTop + 110,
        "ACPI:",
        std::to_string(
            temperatureStats.sensors.size()
        ) + " zones"
    );

    drawRoundedBox(
        hdc,
        502,
        lowerTop,
        720,
        lowerBottom,
        tempCard
    );

    drawLowerPanelTitle(
        502,
        lowerTop,
        L"gpu.png",
        "Cooling / Power"
    );

    auto drawCoolingLine =
        [&](int y,
            const std::string& label,
            const std::string& value)
    {
        drawText(
            hdc,
            label,
            518,
            y,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            value,
            620,
            y,
            textPrimary,
            smallFont
        );
    };

    std::string gpuFanRpm = "--";
    std::string gpuFanPercent = "--";
    std::string gpuPower = "--";

    if (temperatureGpu != nullptr)
    {
        if (temperatureGpu->fanRpm >= 0)
        {
            gpuFanRpm =
                std::to_string(
                    temperatureGpu->fanRpm
                ) +
                " RPM";
        }

        if (temperatureGpu->fanPercent >= 0)
        {
            gpuFanPercent =
                std::to_string(
                    temperatureGpu->fanPercent
                ) +
                "%";
        }

        gpuPower =
            formatPower(
                temperatureGpu->powerW
            );
    }

    drawCoolingLine(
        lowerTop + 52,
        "GPU Fan:",
        gpuFanRpm
    );

    drawCoolingLine(
        lowerTop + 76,
        "GPU Fan %:",
        gpuFanPercent
    );

    drawCoolingLine(
        lowerTop + 100,
        "GPU Power:",
        gpuPower
    );

    drawCoolingLine(
        lowerTop + 124,
        "CPU Power:",
        formatPower(
            temperatureStats.cpuPackagePowerW
        )
    );

    // --------------------------------------------------------
    // HISTORY RANGE DROPDOWN OVERLAY
    // --------------------------------------------------------

    if (cpuHistoryRangeDropdownOpen)
    {
        const int menuLeft = 865;
        const int menuTop = 108;
        const int menuRight = 1015;
        const int rowHeight = 34;
        const int menuBottom =
            menuTop + rowHeight * 3;

        drawRoundedBox(
            hdc,
            menuLeft,
            menuTop,
            menuRight,
            menuBottom,
            uiColor(RGB(13, 23, 32))
        );

        const int rangeOptions[3] =
        {
            15,
            30,
            60
        };

        for (int index = 0;
             index < 3;
             index++)
        {
            const int option =
                rangeOptions[index];

            const int rowTop =
                menuTop +
                index * rowHeight;

            const bool selected =
                cpuHistoryRangeSeconds ==
                option;

            if (selected)
            {
                drawRoundedBox(
                    hdc,
                    menuLeft + 4,
                    rowTop + 3,
                    menuRight - 4,
                    rowTop + rowHeight - 3,
                    uiColor(RGB(24, 48, 64))
                );
            }

            drawText(
                hdc,
                "Last " +
                    std::to_string(option) +
                    " Seconds",
                menuLeft + 18,
                rowTop + 10,
                selected
                    ? uiColor(RGB(78, 205, 245))
                    : textPrimary,
                smallFont
            );
        }
    }
}
else if (
    currentPage == AppPage::Performance &&
    performanceView == PerformanceView::Overview
)
{
    // --------------------------------------------------------
    // PERFORMANCE OVERVIEW
    // Dedicated top-navigation dashboard.  The detailed resource pages
    // remain available from the left sidebar.
    // --------------------------------------------------------
    POINT performanceOverviewOrigin = {};
    GetViewportOrgEx(hdc, &performanceOverviewOrigin);

    SetViewportOrgEx(
        hdc,
        performanceOverviewOrigin.x,
        performanceOverviewOrigin.y + scaleUiCoordinate(88),
        nullptr
    );

    const COLORREF overviewPanel = uiColor(RGB(16, 26, 35));
    const COLORREF overviewPanelAlt = uiColor(RGB(19, 31, 41));
    const COLORREF overviewTrack = uiColor(RGB(43, 59, 73));
    const COLORREF overviewBlue = uiColor(RGB(30, 174, 245));
    const COLORREF overviewPurple = uiColor(RGB(197, 65, 232));
    const COLORREF overviewGreen = uiColor(RGB(55, 226, 112));
    const COLORREF overviewOrange = uiColor(RGB(255, 137, 36));

    const size_t overviewSamples =
        static_cast<size_t>(cpuHistoryRangeSeconds * 2);

    auto shortText =
        [](const std::string& value, size_t maximum) -> std::string
    {
        if (value.size() <= maximum)
            return value;
        if (maximum <= 3)
            return value.substr(0, maximum);
        return value.substr(0, maximum - 3) + "...";
    };

    auto fixed1 =
        [](double value) -> std::string
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(1) << value;
        return stream.str();
    };

    auto drawMetricBar =
        [&](int x, int y, int width, double percent, COLORREF color)
    {
        drawRecommendedUsageBar(
            hdc,
            x,
            y,
            width,
            9,
            percent,
            color
        );
    };

    struct OverviewSeries
    {
        const std::vector<double>* history = nullptr;
        COLORREF color = uiColor(RGB(255, 255, 255));
    };

    auto drawOverviewGraph =
        [&](int x,
            int y,
            int width,
            int height,
            double scaleMaximum,
            std::initializer_list<OverviewSeries> series)
    {
        drawRoundedBox(
            hdc,
            x,
            y,
            x + width,
            y + height,
            uiColor(RGB(12, 22, 30))
        );

        drawGraphGrid(hdc, x, y, width, height);

        if (scaleMaximum <= 0.0)
            scaleMaximum = 1.0;

        ScopedUiGraphics scopedGraphics(hdc);
        Gdiplus::Graphics& graphics = scopedGraphics.get();
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

        for (const OverviewSeries& entry : series)
        {
            if (
                entry.history == nullptr ||
                entry.history->size() < 2
            )
            {
                continue;
            }

            const std::vector<double>& history = *entry.history;
            size_t startIndex = 0;

            if (
                overviewSamples > 0 &&
                history.size() > overviewSamples
            )
            {
                startIndex = history.size() - overviewSamples;
            }

            const size_t count = history.size() - startIndex;
            if (count < 2)
                continue;

            std::vector<Gdiplus::PointF> points;
            points.reserve(count);

            for (size_t index = 0; index < count; index++)
            {
                const double value =
                    std::clamp(
                        history[startIndex + index],
                        0.0,
                        scaleMaximum
                    );

                const Gdiplus::REAL px =
                    static_cast<Gdiplus::REAL>(
                        x +
                        (count > 1
                            ? (width - 1) *
                                static_cast<double>(index) /
                                static_cast<double>(count - 1)
                            : 0.0)
                    );

                const Gdiplus::REAL py =
                    static_cast<Gdiplus::REAL>(
                        y + height - 1 -
                        (height - 2) *
                            (value / scaleMaximum)
                    );

                points.emplace_back(px, py);
            }

            Gdiplus::Pen linePen(
                Gdiplus::Color(
                    255,
                    GetRValue(entry.color),
                    GetGValue(entry.color),
                    GetBValue(entry.color)
                ),
                1.8f
            );

            graphics.DrawLines(
                &linePen,
                points.data(),
                static_cast<INT>(points.size())
            );
        }
    };

    const DiskStats* overviewDisk = nullptr;
    for (const DiskStats& disk : diskStats)
    {
        if (disk.systemDisk)
        {
            overviewDisk = &disk;
            break;
        }
    }
    if (overviewDisk == nullptr && !diskStats.empty())
        overviewDisk = &diskStats.front();

    const GpuStats* overviewGpu = nullptr;
    if (!gpuStats.empty())
    {
        overviewGpu = &gpuStats.front();
        for (const GpuStats& gpu : gpuStats)
        {
            if (
                gpu.performanceValid &&
                (!overviewGpu->performanceValid ||
                 gpu.utilizationPercent > overviewGpu->utilizationPercent)
            )
            {
                overviewGpu = &gpu;
            }
        }
    }

    const NetworkStats* overviewNetwork = nullptr;
    for (const NetworkStats& network : networkStats)
    {
        if (network.status == "Connected")
        {
            overviewNetwork = &network;
            break;
        }
    }
    if (overviewNetwork == nullptr && !networkStats.empty())
        overviewNetwork = &networkStats.front();

    const double diskActive =
        overviewDisk != nullptr
        ? overviewDisk->activeTimePercent
        : 0.0;

    const double gpuUsage =
        overviewGpu != nullptr
        ? overviewGpu->utilizationPercent
        : 0.0;

    const double gpuVramUsedGB =
        overviewGpu != nullptr
        ? overviewGpu->dedicatedMemoryUsedBytes /
            (1024.0 * 1024.0 * 1024.0)
        : 0.0;

    const double gpuVramTotalGB =
        overviewGpu != nullptr
        ? overviewGpu->dedicatedMemoryTotalBytes /
            (1024.0 * 1024.0 * 1024.0)
        : 0.0;

    // Header
    drawRoundedBox(hdc, 20, 4, 72, 56, uiColor(RGB(25, 38, 50)));
    if (uiAssetExists(L"performance.png"))
    {
        drawTintedPngImage(
            hdc,
            L"performance.png",
            34,
            18,
            24,
            24,
            uiColor(RGB(225, 234, 242))
        );
    }

    drawText(hdc, "Performance", 84, 8, textPrimary, titleFont);
    drawText(
        hdc,
        "Detailed performance metrics for your system.",
        86,
        45,
        textSecondary,
        smallFont
    );

    drawRoundedBox(hdc, 820, 12, 995, 54, overviewPanelAlt);
    drawText(
        hdc,
        "Last " + std::to_string(cpuHistoryRangeSeconds) + " Seconds",
        850,
        25,
        textPrimary,
        smallFont
    );
    drawText(hdc, "v", 975, 24, textSecondary, smallFont);

    // Top summary cards
    const int topCardY = 72;
    const int topCardBottom = 326;
    const int cardWidth = 232;
    const int cardXs[4] = { 20, 260, 500, 740 };

    for (int cardIndex = 0; cardIndex < 4; cardIndex++)
    {
        drawRoundedBox(
            hdc,
            cardXs[cardIndex],
            topCardY,
            cardXs[cardIndex] + cardWidth,
            topCardBottom,
            overviewPanel
        );
    }

    // CPU card
    drawTintedPngImage(hdc, L"sidebar_cpu.png", 34, 88, 20, 20, textPrimary);
    drawText(hdc, "CPU", 64, 89, textPrimary, labelFont);
    drawText(hdc, shortText(systemInfo.cpuName, 20), 126, 91, textSecondary, smallFont);
    drawText(
        hdc,
        std::to_string(static_cast<int>(cpuUsage + 0.5)) + "%",
        40,
        120,
        textPrimary,
        titleFont
    );
    drawText(
        hdc,
        (!systemInfo.cpuCurrentSpeed.empty() ? systemInfo.cpuCurrentSpeed : "--") +
            " / " +
            (!systemInfo.cpuBaseSpeed.empty() ? systemInfo.cpuBaseSpeed : "--"),
        40,
        155,
        textSecondary,
        smallFont
    );
    drawOverviewGraph(32, 178, 208, 72, 100.0, {{ &cpuHistory, overviewBlue }});
    drawText(hdc, "Cores", 32, 261, textSecondary, smallFont);
    drawText(hdc, std::to_string(systemInfo.cpuCores), 205, 261, textPrimary, smallFont);
    drawText(hdc, "Threads", 32, 281, textSecondary, smallFont);
    drawText(hdc, std::to_string(systemInfo.cpuThreads), 205, 281, textPrimary, smallFont);
    drawText(hdc, "Temperature", 32, 301, textSecondary, smallFont);
    drawText(
        hdc,
        temperatureStats.cpuTemperatureC >= 0.0
            ? SettingsRuntime::temperature(temperatureStats.cpuTemperatureC)
            : "--",
        190,
        301,
        textPrimary,
        smallFont
    );

    // Memory card
    drawTintedPngImage(hdc, L"sidebar_memory.png", 274, 88, 20, 20, textPrimary);
    drawText(hdc, "Memory", 304, 89, textPrimary, labelFont);
    drawText(hdc, shortText(systemInfo.installedMemory, 12), 413, 91, textSecondary, smallFont);
    drawText(
        hdc,
        std::to_string(ramPercent) + "%",
        280,
        120,
        textPrimary,
        titleFont
    );
    drawText(
        hdc,
        fixed1(usedRamGB) + " GB / " + fixed1(totalRamGB) + " GB",
        280,
        155,
        textSecondary,
        smallFont
    );
    drawOverviewGraph(272, 178, 208, 72, 100.0, {{ &ramHistory, overviewPurple }});
    drawText(hdc, "In Use", 272, 261, textSecondary, smallFont);
    drawText(hdc, fixed1(usedRamGB) + " GB", 416, 261, textPrimary, smallFont);
    drawText(hdc, "Available", 272, 281, textSecondary, smallFont);
    drawText(hdc, fixed1((std::max)(0.0, totalRamGB - usedRamGB)) + " GB", 416, 281, textPrimary, smallFont);
    drawText(hdc, "Speed", 272, 301, textSecondary, smallFont);
    drawText(hdc, shortText(systemInfo.memorySpeed, 12), 403, 301, textPrimary, smallFont);

    // Disk card
    drawTintedPngImage(hdc, L"sidebar_disk.png", 514, 88, 20, 20, textPrimary);
    drawText(hdc, "Disk", 544, 89, textPrimary, labelFont);
    drawText(
        hdc,
        overviewDisk != nullptr ? shortText(overviewDisk->model, 18) : "--",
        596,
        91,
        textSecondary,
        smallFont
    );
    drawText(
        hdc,
        std::to_string(static_cast<int>(diskActive + 0.5)) + "%",
        520,
        120,
        textPrimary,
        titleFont
    );
    drawText(
        hdc,
        overviewDisk != nullptr
            ? "Read " + fixed1(overviewDisk->readMBps) + " MB/s  |  Write " +
                fixed1(overviewDisk->writeMBps) + " MB/s"
            : "--",
        520,
        155,
        textSecondary,
        smallFont
    );
    if (overviewDisk != nullptr)
    {
        drawOverviewGraph(
            512,
            178,
            208,
            72,
            100.0,
            {{ &overviewDisk->activeHistory, overviewGreen }}
        );
    }
    else
    {
        drawOverviewGraph(512, 178, 208, 72, 100.0, {});
    }
    drawText(hdc, "Total Space", 512, 261, textSecondary, smallFont);
    drawText(hdc, fixed1(totalDiskGB) + " GB", 645, 261, textPrimary, smallFont);
    drawText(hdc, "Used Space", 512, 281, textSecondary, smallFont);
    drawText(hdc, fixed1(usedDiskGB) + " GB", 645, 281, textPrimary, smallFont);
    drawText(hdc, "Active Time", 512, 301, textSecondary, smallFont);
    drawText(hdc, std::to_string(static_cast<int>(diskActive + 0.5)) + "%", 681, 301, textPrimary, smallFont);

    // GPU card
    drawTintedPngImage(hdc, L"sidebar_gpu.png", 754, 88, 20, 20, textPrimary);
    drawText(hdc, "GPU", 784, 89, textPrimary, labelFont);
    drawText(
        hdc,
        overviewGpu != nullptr ? shortText(overviewGpu->name, 19) : "--",
        838,
        91,
        textSecondary,
        smallFont
    );
    drawText(
        hdc,
        std::to_string(static_cast<int>(gpuUsage + 0.5)) + "%",
        760,
        120,
        textPrimary,
        titleFont
    );
    drawText(
        hdc,
        gpuVramTotalGB > 0.0
            ? fixed1(gpuVramUsedGB) + " / " + fixed1(gpuVramTotalGB) + " GB"
            : "--",
        760,
        155,
        textSecondary,
        smallFont
    );
    if (overviewGpu != nullptr)
    {
        drawOverviewGraph(
            752,
            178,
            208,
            72,
            100.0,
            {{ &overviewGpu->utilizationHistory, overviewOrange }}
        );
    }
    else
    {
        drawOverviewGraph(752, 178, 208, 72, 100.0, {});
    }
    drawText(hdc, "Temperature", 752, 261, textSecondary, smallFont);
    drawText(
        hdc,
        overviewGpu != nullptr && overviewGpu->temperatureC >= 0.0
            ? SettingsRuntime::temperature(overviewGpu->temperatureC)
            : "--",
        904,
        261,
        textPrimary,
        smallFont
    );
    drawText(hdc, "Core Clock", 752, 281, textSecondary, smallFont);
    drawText(
        hdc,
        overviewGpu != nullptr && overviewGpu->coreClockMHz >= 0.0
            ? std::to_string(static_cast<int>(overviewGpu->coreClockMHz + 0.5)) + " MHz"
            : "--",
        886,
        281,
        textPrimary,
        smallFont
    );
    drawText(hdc, "Fan Speed", 752, 301, textSecondary, smallFont);
    drawText(
        hdc,
        overviewGpu != nullptr && overviewGpu->fanPercent >= 0
            ? std::to_string(overviewGpu->fanPercent) + "%"
            : "--",
        914,
        301,
        textPrimary,
        smallFont
    );

    // Bottom panels
    drawRoundedBox(hdc, 20, 340, 300, 612, overviewPanel);
    drawRoundedBox(hdc, 310, 340, 540, 612, overviewPanel);
    drawRoundedBox(hdc, 550, 340, 995, 612, overviewPanel);

    // Network panel
    drawTintedPngImage(hdc, L"sidebar_network.png", 34, 354, 20, 20, overviewBlue);
    drawText(hdc, "Network", 64, 355, textPrimary, labelFont);
    drawText(
        hdc,
        overviewNetwork != nullptr ? shortText(overviewNetwork->name, 20) : "--",
        142,
        357,
        textSecondary,
        smallFont
    );

    const double downloadMbps =
        overviewNetwork != nullptr ? overviewNetwork->downloadMbps : 0.0;
    const double uploadMbps =
        overviewNetwork != nullptr ? overviewNetwork->uploadMbps : 0.0;

    drawText(hdc, "v", 38, 389, overviewBlue, labelFont);
    drawText(hdc, formatNetworkSpeed(downloadMbps), 62, 389, textPrimary, labelFont);
    drawText(hdc, "Download", 62, 414, textSecondary, smallFont);
    drawText(hdc, "^", 178, 389, overviewGreen, labelFont);
    drawText(hdc, formatNetworkSpeed(uploadMbps), 202, 389, textPrimary, labelFont);
    drawText(hdc, "Upload", 202, 414, textSecondary, smallFont);

    double networkScale = 10.0;
    if (overviewNetwork != nullptr)
    {
        for (double value : overviewNetwork->downloadHistory)
            networkScale = (std::max)(networkScale, value * 1.15);
        for (double value : overviewNetwork->uploadHistory)
            networkScale = (std::max)(networkScale, value * 1.15);

        drawOverviewGraph(
            32,
            444,
            256,
            82,
            networkScale,
            {
                { &overviewNetwork->downloadHistory, overviewBlue },
                { &overviewNetwork->uploadHistory, overviewGreen }
            }
        );
    }
    else
    {
        drawOverviewGraph(32, 444, 256, 82, networkScale, {});
    }

    drawText(hdc, "Type", 32, 540, textSecondary, smallFont);
    drawText(
        hdc,
        overviewNetwork != nullptr ? shortText(overviewNetwork->type, 17) : "--",
        152,
        540,
        textPrimary,
        smallFont
    );
    drawText(hdc, "Link", 32, 562, textSecondary, smallFont);
    drawText(
        hdc,
        overviewNetwork != nullptr && overviewNetwork->linkSpeedMbps > 0.0
            ? formatNetworkSpeed(overviewNetwork->linkSpeedMbps)
            : "--",
        152,
        562,
        textPrimary,
        smallFont
    );

    // System Performance panel
    drawText(hdc, "System Performance", 330, 355, textPrimary, labelFont);
    drawText(hdc, "Overall system resource usage", 330, 378, textSecondary, smallFont);

    const struct
    {
        const char* name;
        double value;
        COLORREF color;
    } performanceRows[] = {
        { "CPU", cpuUsage, overviewBlue },
        { "Memory", static_cast<double>(ramPercent), overviewPurple },
        { "Disk", diskActive, overviewGreen },
        { "GPU", gpuUsage, overviewOrange }
    };

    int performanceRowY = 420;
    for (const auto& row : performanceRows)
    {
        drawText(hdc, row.name, 330, performanceRowY, textSecondary, smallFont);
        drawMetricBar(382, performanceRowY + 2, 112, row.value, row.color);
        drawText(
            hdc,
            std::to_string(static_cast<int>(row.value + 0.5)) + "%",
            506,
            performanceRowY,
            textPrimary,
            smallFont
        );
        performanceRowY += 42;
    }

    // Combined Performance History panel
    drawText(hdc, "Performance History", 570, 355, textPrimary, labelFont);
    drawText(hdc, "Combined resource usage over time", 570, 378, textSecondary, smallFont);

    drawText(hdc, "CPU", 740, 357, overviewBlue, smallFont);
    drawText(hdc, "Memory", 790, 357, overviewPurple, smallFont);
    drawText(hdc, "Disk", 864, 357, overviewGreen, smallFont);
    drawText(hdc, "GPU", 915, 357, overviewOrange, smallFont);

    if (overviewDisk != nullptr && overviewGpu != nullptr)
    {
        drawOverviewGraph(
            570,
            408,
            405,
            166,
            100.0,
            {
                { &cpuHistory, overviewBlue },
                { &ramHistory, overviewPurple },
                { &overviewDisk->activeHistory, overviewGreen },
                { &overviewGpu->utilizationHistory, overviewOrange }
            }
        );
    }
    else
    {
        std::initializer_list<OverviewSeries> basicSeries = {
            { &cpuHistory, overviewBlue },
            { &ramHistory, overviewPurple }
        };
        drawOverviewGraph(570, 408, 405, 166, 100.0, basicSeries);
    }

    drawText(hdc, std::to_string(cpuHistoryRangeSeconds) + "s", 570, 582, textSecondary, smallFont);
    drawText(hdc, "0s", 955, 582, textSecondary, smallFont);

    // Dropdown menu stays on top of the cards.
    if (cpuHistoryRangeDropdownOpen)
    {
        const int menuLeft = 820;
        const int menuTop = 60;
        const int menuRight = 995;
        const int rowHeight = 34;
        const int menuBottom = menuTop + rowHeight * 3;

        drawRoundedBox(hdc, menuLeft, menuTop, menuRight, menuBottom, uiColor(RGB(19, 31, 41)));

        const int options[] = { 15, 30, 60 };
        for (int optionIndex = 0; optionIndex < 3; optionIndex++)
        {
            const int option = options[optionIndex];
            const int rowTop = menuTop + optionIndex * rowHeight;
            const bool selected = option == cpuHistoryRangeSeconds;

            if (selected)
            {
                drawRoundedBox(
                    hdc,
                    menuLeft + 4,
                    rowTop + 3,
                    menuRight - 4,
                    rowTop + rowHeight - 3,
                    uiColor(RGB(24, 48, 64))
                );
            }

            drawText(
                hdc,
                "Last " + std::to_string(option) + " Seconds",
                menuLeft + 18,
                rowTop + 10,
                selected ? uiColor(RGB(78, 205, 245)) : textPrimary,
                smallFont
            );
        }
    }

    SetViewportOrgEx(
        hdc,
        performanceOverviewOrigin.x,
        performanceOverviewOrigin.y,
        nullptr
    );
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
        uiColor(RGB(24, 27, 34));

    const COLORREF performanceTrack =
        uiColor(RGB(45, 48, 58));

    const COLORREF memoryPurple =
        uiColor(RGB(140, 80, 220));

    const COLORREF diskGreen =
        uiColor(RGB(70, 200, 90));

    const COLORREF gpuPurple =
        uiColor(RGB(155, 85, 220));

    const COLORREF networkOrange =
        uiColor(RGB(220, 140, 55));


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
    // Static CPU topology/cache information is already collected and cached
    // by Stats.cpp. Reading the registry and walking
    // GetLogicalProcessorInformationEx on every paint caused unnecessary
    // work whenever the Performance page refreshed.

    std::string cpuModelText =
        (!systemInfo.cpuName.empty() &&
         systemInfo.cpuName != "--")
        ? systemInfo.cpuName
        : "Processor";

    std::ostringstream baseSpeedText;
    baseSpeedText << (
        !systemInfo.cpuBaseSpeed.empty()
        ? systemInfo.cpuBaseSpeed
        : "--"
    );

    // Shared one-second process snapshot used only by the performance views
    // that actually show per-process data. Disk/Network avoid even the copy.
    std::vector<ProcessInfo> performanceProcesses;

    if (
        performanceView == PerformanceView::CPU ||
        performanceView == PerformanceView::Memory ||
        performanceView == PerformanceView::GPU
    )
    {
        performanceProcesses =
            getCachedRunningProcesses();
    }

    // cpuSocket is already exposed as text in SystemInfoData. Keep the old
    // numeric fallback disabled rather than querying topology every frame.
    const DWORD socketCount = 0;

    const DWORD coreCount =
        systemInfo.cpuCores > 0
        ? static_cast<DWORD>(systemInfo.cpuCores)
        : 0;

    const DWORD logicalProcessorCount =
        systemInfo.cpuThreads > 0
        ? static_cast<DWORD>(systemInfo.cpuThreads)
        : 0;

    const bool virtualizationEnabled =
        systemInfo.virtualization == "Enabled";

    const std::string l1CacheText =
        !systemInfo.l1Cache.empty()
        ? systemInfo.l1Cache
        : "--";

    const std::string l2CacheText =
        !systemInfo.l2Cache.empty()
        ? systemInfo.l2Cache
        : "--";

    const std::string l3CacheText =
        !systemInfo.l3Cache.empty()
        ? systemInfo.l3Cache
        : "--";


    // --------------------------------------------------------
    // LEGACY PERFORMANCE RESOURCE RAIL
    // Disabled now that each performance page has its own
    // dedicated full-width layout (CPU / Memory / Disk / GPU / Network).
    // --------------------------------------------------------

    const bool showPerformanceResourceRail = false;

    if (showPerformanceResourceRail)
    {

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
        uiColor(RGB(66, 135, 245)),
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
            uiColor(RGB(20, 24, 30))
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
                uiColor(RGB(25, 55, 34))
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
                uiColor(RGB(48, 28, 70))
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
                uiColor(RGB(20, 24, 30))
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
            uiColor(RGB(45, 145, 245))
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
            uiColor(RGB(20, 24, 30))
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

    }


    // --------------------------------------------------------
    // CPU VIEW
    // --------------------------------------------------------

    if (
        performanceView ==
        PerformanceView::CPU
    )
    {
        // Keep the CPU page clearly below the global navigation header,
        // matching the reference layout while preserving the existing
        // Performance-page viewport X origin.
        POINT cpuPageOriginalOrigin = {};
        GetViewportOrgEx(
            hdc,
            &cpuPageOriginalOrigin
        );

        SetViewportOrgEx(
            hdc,
            cpuPageOriginalOrigin.x,
            cpuPageOriginalOrigin.y +
            scaleUiCoordinate(88),
            nullptr
        );

        const COLORREF cpuBlue =
            uiColor(RGB(45, 169, 245));

        const COLORREF cpuPanel =
            uiColor(RGB(16, 26, 35));

        const COLORREF cpuPanelAlt =
            uiColor(RGB(20, 31, 41));

        const COLORREF cpuGaugeTrack =
            uiColor(RGB(49, 64, 77));

        // updateStats() records one CPU sample every 500 ms.
        // 15/30/60 seconds therefore map to 30/60/120 real samples.
        const size_t cpuHistorySamplesForRange =
            static_cast<size_t>(
                cpuHistoryRangeSeconds * 2
            );

        const std::string cpuHistoryRangeLabel =
            "Last " +
            std::to_string(
                cpuHistoryRangeSeconds
            ) +
            " Seconds";

        // ----------------------------------------------------
        // CPU PAGE HEADER
        // Uses the same logo.png as the SysMon app branding.
        // ----------------------------------------------------

        drawRoundedBox(
            hdc,
            20,
            4,
            96,
            80,
            uiColor(RGB(25, 38, 50))
        );

        if (
            uiAssetExists(L"logo.png")
        )
        {
            drawPngImage(
                hdc,
                L"logo.png",
                32,
                16,
                52,
                52
            );
        }

        drawText(
            hdc,
            "CPU",
            118,
            12,
            textPrimary,
            bigFont
        );

        drawText(
            hdc,
            "Real-time CPU performance and usage statistics.",
            118,
            54,
            textSecondary,
            smallFont
        );

        // Reference-style time-range control.
        drawRoundedBox(
            hdc,
            820,
            12,
            995,
            54,
            cpuPanel
        );

        // Small clock icon drawn with GDI so no additional asset is needed.
        HPEN cpuClockPen =
            CreatePen(
                PS_SOLID,
                1,
                uiColor(RGB(205, 221, 234))
            );

        HGDIOBJ oldCpuClockPen =
            SelectObject(
                hdc,
                cpuClockPen
            );

        HGDIOBJ oldCpuClockBrush =
            SelectObject(
                hdc,
                GetStockObject(HOLLOW_BRUSH)
            );

        drawUiEllipse(
            hdc,
            836,
            24,
            850,
            38
        );

        MoveToEx(
            hdc,
            843,
            27,
            nullptr
        );

        LineTo(
            hdc,
            843,
            31
        );

        LineTo(
            hdc,
            847,
            33
        );

        SelectObject(
            hdc,
            oldCpuClockBrush
        );

        SelectObject(
            hdc,
            oldCpuClockPen
        );

        DeleteObject(
            cpuClockPen
        );

        drawText(
            hdc,
            cpuHistoryRangeLabel,
            858,
            25,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            cpuHistoryRangeDropdownOpen
                ? "^"
                : "v",
            974,
            25,
            textSecondary,
            smallFont
        );

        // ----------------------------------------------------
        // REAL CPU VALUES USED BY THE SUMMARY CARDS
        // ----------------------------------------------------

        std::ostringstream cpuUsageText;
        cpuUsageText
            << std::fixed
            << std::setprecision(0)
            << cpuUsage
            << "%";

        std::string cpuBaseClockText =
            systemInfo.cpuBaseSpeed;

        if (
            cpuBaseClockText.empty() ||
            cpuBaseClockText == "--"
        )
        {
            cpuBaseClockText =
                baseSpeedText.str();
        }

        std::string cpuCurrentClockText =
            systemInfo.cpuCurrentSpeed;

        if (cpuCurrentClockText.empty())
        {
            cpuCurrentClockText = "--";
        }

        std::string cpuTemperatureText = "--";

        if (
            temperatureStats.cpuTemperatureC >=
            0.0
        )
        {
            std::ostringstream stream;
            stream
                << std::fixed
                << std::setprecision(0)
                << SettingsRuntime::temperature(temperatureStats.cpuTemperatureC);

            cpuTemperatureText =
                stream.str();
        }

        std::string cpuSocketText =
            systemInfo.cpuSocket;

        if (
            cpuSocketText.empty() ||
            cpuSocketText == "--"
        )
        {
            cpuSocketText =
                socketCount > 0
                ? std::to_string(socketCount)
                : "--";
        }

        // The reference leaves a little more breathing room between
        // the CPU heading and the summary cards.  Everything from the
        // cards downward shares this additional offset.
        SetViewportOrgEx(
            hdc,
            cpuPageOriginalOrigin.x,
            cpuPageOriginalOrigin.y +
            scaleUiCoordinate(103),
            nullptr
        );

        // ----------------------------------------------------
        // TOP CPU SUMMARY CARDS
        // ----------------------------------------------------

        // Total Usage card.
        drawRoundedBox(
            hdc,
            20,
            86,
            245,
            196,
            cpuPanel
        );

        drawText(
            hdc,
            "Total Usage",
            35,
            99,
            textPrimary,
            labelFont
        );

        drawCircularGauge(
            hdc,
            34,
            119,
            70,
            cpuUsage,
            cpuBlue,
            cpuGaugeTrack
        );

        drawDashboardCenteredText(
            hdc,
            cpuUsageText.str(),
            34,
            119,
            70,
            70,
            textPrimary,
            labelFont
        );

        drawCpuGraph(
            hdc,
            115,
            126,
            112,
            56,
            cpuHistorySamplesForRange
        );

        auto drawCpuSummaryCard =
            [&](int left,
                int right,
                const std::string& title,
                const std::string& value,
                const wchar_t* iconPath)
        {
            drawRoundedBox(
                hdc,
                left,
                86,
                right,
                196,
                cpuPanel
            );

            drawRoundedBox(
                hdc,
                left + 11,
                103,
                left + 45,
                137,
                cpuPanelAlt
            );

            if (
                iconPath != nullptr &&
                uiAssetExists(iconPath)
            )
            {
                drawTintedPngImage(
                    hdc,
                    iconPath,
                    left + 19,
                    111,
                    18,
                    18,
                    uiColor(RGB(205, 221, 234))
                );
            }

            drawText(
                hdc,
                title,
                left + 52,
                104,
                textPrimary,
                smallFont
            );

            drawText(
                hdc,
                value,
                left + 52,
                135,
                textPrimary,
                labelFont
            );
        };

        drawCpuSummaryCard(
            255,
            395,
            "Base Clock",
            cpuBaseClockText,
            L"performance.png"
        );

        drawCpuSummaryCard(
            405,
            545,
            "Current Clock",
            cpuCurrentClockText,
            L"performance.png"
        );

        drawCpuSummaryCard(
            555,
            685,
            "Cores",
            coreCount > 0
                ? std::to_string(coreCount)
                : "--",
            L"sidebar_cpu.png"
        );

        drawCpuSummaryCard(
            695,
            825,
            "Threads",
            logicalProcessorCount > 0
                ? std::to_string(logicalProcessorCount)
                : "--",
            L"sidebar_cpu.png"
        );

        drawCpuSummaryCard(
            835,
            995,
            "Temperature",
            cpuTemperatureText,
            L"sidebar_temperatures.png"
        );

        // ----------------------------------------------------
        // CPU USAGE GRAPH
        // ----------------------------------------------------

        drawRoundedBox(
            hdc,
            20,
            210,
            650,
            410,
            cpuPanel
        );

        if (
            uiAssetExists(L"performance.png")
        )
        {
            drawTintedPngImage(
                hdc,
                L"performance.png",
                34,
                224,
                18,
                18,
                cpuBlue
            );
        }

        drawText(
            hdc,
            "CPU Usage",
            60,
            223,
            textPrimary,
            labelFont
        );

        drawText(
            hdc,
            "100%",
            34,
            252,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "0%",
            42,
            374,
            textSecondary,
            smallFont
        );

        drawCpuGraph(
            hdc,
            67,
            255,
            565,
            130,
            cpuHistorySamplesForRange
        );

        drawText(
            hdc,
            std::to_string(
                cpuHistoryRangeSeconds
            ) + " seconds",
            565,
            387,
            textSecondary,
            smallFont
        );

        // ----------------------------------------------------
        // CPU DETAILS
        // ----------------------------------------------------

        drawRoundedBox(
            hdc,
            660,
            210,
            995,
            598,
            cpuPanel
        );

        if (
            uiAssetExists(L"sidebar_cpu.png")
        )
        {
            drawTintedPngImage(
                hdc,
                L"sidebar_cpu.png",
                678,
                224,
                18,
                18,
                uiColor(RGB(205, 221, 234))
            );
        }

        drawText(
            hdc,
            "CPU Details",
            706,
            222,
            textPrimary,
            labelFont
        );

        const int detailLabelX = 678;
        const int detailValueX = 825;
        const int detailStartY = 256;
        const int detailGap = 27;

        auto drawCpuDetailRow =
            [&](int row,
                const std::string& label,
                const std::string& value)
        {
            int y =
                detailStartY +
                row * detailGap;

            drawText(
                hdc,
                label,
                detailLabelX,
                y,
                textSecondary,
                smallFont
            );

            setFont(
                hdc,
                smallFont
            );

            std::string shownValue =
                shortenPerformanceText(
                    value,
                    24
                );

            SIZE valueExtent = {};

            GetTextExtentPoint32A(
                hdc,
                shownValue.c_str(),
                static_cast<int>(
                    shownValue.size()
                ),
                &valueExtent
            );

            int rightAlignedX =
                978 - valueExtent.cx;

            if (rightAlignedX < detailValueX)
            {
                rightAlignedX =
                    detailValueX;
            }

            drawText(
                hdc,
                shownValue,
                rightAlignedX,
                y,
                textPrimary,
                smallFont
            );
        };

        drawCpuDetailRow(
            0,
            "Name",
            cpuModelText
        );

        drawCpuDetailRow(
            1,
            "Socket",
            cpuSocketText
        );

        drawCpuDetailRow(
            2,
            "Base Clock",
            cpuBaseClockText
        );

        drawCpuDetailRow(
            3,
            "Current Clock",
            cpuCurrentClockText
        );

        drawCpuDetailRow(
            4,
            "Cores",
            coreCount > 0
                ? std::to_string(coreCount)
                : "--"
        );

        drawCpuDetailRow(
            5,
            "Threads",
            logicalProcessorCount > 0
                ? std::to_string(logicalProcessorCount)
                : "--"
        );

        drawCpuDetailRow(
            6,
            "Virtualization",
            virtualizationEnabled
                ? "Enabled"
                : "Disabled"
        );

        drawCpuDetailRow(
            7,
            "L1 Cache",
            l1CacheText
        );

        drawCpuDetailRow(
            8,
            "L2 Cache",
            l2CacheText
        );

        drawCpuDetailRow(
            9,
            "L3 Cache",
            l3CacheText
        );

        drawCpuDetailRow(
            10,
            "Temperature",
            cpuTemperatureText
        );

        // ----------------------------------------------------
        // PER-CORE CURRENT USAGE
        // No artificial history is generated here; these cards
        // show the real current values already collected by SysMon.
        // ----------------------------------------------------

        drawRoundedBox(
            hdc,
            20,
            420,
            390,
            598,
            cpuPanel
        );

        drawText(
            hdc,
            "Per-Core Usage",
            35,
            434,
            textPrimary,
            labelFont
        );

        const int displayedCoreCount =
            (std::min)(
                12,
                static_cast<int>(
                    cpuCoreUsage.size()
                )
            );

        if (
            static_cast<int>(
                cpuCoreUsage.size()
            ) > displayedCoreCount
        )
        {
            std::ostringstream shownText;
            shownText
                << displayedCoreCount
                << " of "
                << cpuCoreUsage.size()
                << " shown";

            drawText(
                hdc,
                shownText.str(),
                294,
                436,
                textSecondary,
                smallFont
            );
        }

        if (displayedCoreCount == 0)
        {
            drawText(
                hdc,
                "Per-core utilization is unavailable.",
                35,
                482,
                textSecondary,
                smallFont
            );
        }
        else
        {
            const int coreCardWidth = 53;
            const int coreCardHeight = 55;
            const int coreGapX = 5;
            const int coreGapY = 9;
            const int coreStartX = 32;
            const int coreStartY = 459;

            for (
                int index = 0;
                index < displayedCoreCount;
                index++
            )
            {
                int column = index % 6;
                int row = index / 6;

                int left =
                    coreStartX +
                    column *
                    (coreCardWidth + coreGapX);

                int top =
                    coreStartY +
                    row *
                    (coreCardHeight + coreGapY);

                drawRoundedBox(
                    hdc,
                    left,
                    top,
                    left + coreCardWidth,
                    top + coreCardHeight,
                    cpuPanelAlt
                );

                std::string coreLabel =
                    "Core " +
                    std::to_string(index + 1);

                drawText(
                    hdc,
                    coreLabel,
                    left + 5,
                    top + 6,
                    textSecondary,
                    smallFont
                );

                double corePercent =
                    std::clamp(
                        cpuCoreUsage[index],
                        0.0,
                        100.0
                    );

                std::ostringstream coreUsageText;
                coreUsageText
                    << std::fixed
                    << std::setprecision(0)
                    << corePercent
                    << "%";

                drawText(
                    hdc,
                    coreUsageText.str(),
                    left + 6,
                    top + 23,
                    textPrimary,
                    smallFont
                );

                drawRecommendedUsageBar(
                    hdc,
                    left + 5,
                    top + 43,
                    coreCardWidth - 10,
                    6,
                    corePercent,
                    cpuBlue
                );
            }
        }

        // ----------------------------------------------------
        // TOP CPU PROCESSES
        // ----------------------------------------------------

        drawRoundedBox(
            hdc,
            400,
            420,
            650,
            598,
            cpuPanel
        );

        drawText(
            hdc,
            "Top CPU Processes",
            415,
            434,
            textPrimary,
            labelFont
        );

        drawText(
            hdc,
            "View All >",
            580,
            434,
            cpuBlue,
            smallFont
        );

        drawText(
            hdc,
            "Name",
            415,
            459,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "CPU",
            535,
            459,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "Threads",
            568,
            459,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "PID",
            620,
            459,
            textSecondary,
            smallFont
        );

        std::vector<ProcessInfo> cpuTopProcesses =
            performanceProcesses;

        cpuTopProcesses.erase(
            std::remove_if(
                cpuTopProcesses.begin(),
                cpuTopProcesses.end(),
                [](const ProcessInfo& process)
                {
                    return process.pid == 0;
                }
            ),
            cpuTopProcesses.end()
        );

        std::stable_sort(
            cpuTopProcesses.begin(),
            cpuTopProcesses.end(),
            [](const ProcessInfo& a,
               const ProcessInfo& b)
            {
                const double aCpu =
                    a.cpuPercent >= 0.0
                    ? a.cpuPercent
                    : -1.0;

                const double bCpu =
                    b.cpuPercent >= 0.0
                    ? b.cpuPercent
                    : -1.0;

                if (aCpu != bCpu)
                {
                    return aCpu > bCpu;
                }

                return a.memoryMB > b.memoryMB;
            }
        );

        const int topCpuProcessCount =
            (std::min)(
                5,
                static_cast<int>(
                    cpuTopProcesses.size()
                )
            );

        for (
            int index = 0;
            index < topCpuProcessCount;
            index++
        )
        {
            const ProcessInfo& process =
                cpuTopProcesses[index];

            int rowY =
                481 +
                index * 21;

            drawProcessExecutableIcon(
                hdc,
                process.pid,
                414,
                rowY - 2,
                15
            );

            drawText(
                hdc,
                shortenPerformanceText(
                    process.name,
                    13
                ),
                434,
                rowY,
                textPrimary,
                smallFont
            );

            std::string processCpuText = "--";

            if (process.cpuPercent >= 0.0)
            {
                std::ostringstream stream;
                stream
                    << std::fixed
                    << std::setprecision(1)
                    << process.cpuPercent
                    << "%";

                processCpuText =
                    stream.str();
            }

            drawText(
                hdc,
                processCpuText,
                535,
                rowY,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                std::to_string(
                    process.threadCount
                ),
                578,
                rowY,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                std::to_string(
                    process.pid
                ),
                617,
                rowY,
                textSecondary,
                smallFont
            );
        }

        // ----------------------------------------------------
        // CPU HISTORY RANGE DROPDOWN OVERLAY
        // ----------------------------------------------------

        if (cpuHistoryRangeDropdownOpen)
        {
            // Draw after the rest of the CPU page so the dropdown floats
            // above the summary cards instead of being painted underneath.
            SetViewportOrgEx(
                hdc,
                cpuPageOriginalOrigin.x,
                cpuPageOriginalOrigin.y +
            scaleUiCoordinate(88),
                nullptr
            );

            const int menuLeft = 820;
            const int menuTop = 60;
            const int menuRight = 995;
            const int rowHeight = 34;
            const int menuBottom =
                menuTop + rowHeight * 3;

            drawRoundedBox(
                hdc,
                menuLeft,
                menuTop,
                menuRight,
                menuBottom,
                uiColor(RGB(13, 23, 32))
            );

            const int rangeOptions[3] =
            {
                15,
                30,
                60
            };

            for (int index = 0;
                 index < 3;
                 index++)
            {
                const int option =
                    rangeOptions[index];

                const int rowTop =
                    menuTop +
                    index * rowHeight;

                const bool selected =
                    cpuHistoryRangeSeconds ==
                    option;

                if (selected)
                {
                    drawRoundedBox(
                        hdc,
                        menuLeft + 4,
                        rowTop + 3,
                        menuRight - 4,
                        rowTop + rowHeight - 3,
                        uiColor(RGB(24, 48, 64))
                    );

                    HBRUSH selectedDotBrush =
                        CreateSolidBrush(
                            cpuBlue
                        );

                    HGDIOBJ oldSelectedDotBrush =
                        SelectObject(
                            hdc,
                            selectedDotBrush
                        );

                    HPEN selectedDotPen =
                        CreatePen(
                            PS_SOLID,
                            1,
                            cpuBlue
                        );

                    HGDIOBJ oldSelectedDotPen =
                        SelectObject(
                            hdc,
                            selectedDotPen
                        );

                    drawUiEllipse(
                        hdc,
                        835,
                        rowTop + 14,
                        841,
                        rowTop + 20
                    );

                    SelectObject(
                        hdc,
                        oldSelectedDotPen
                    );

                    SelectObject(
                        hdc,
                        oldSelectedDotBrush
                    );

                    DeleteObject(
                        selectedDotPen
                    );

                    DeleteObject(
                        selectedDotBrush
                    );
                }

                drawText(
                    hdc,
                    "Last " +
                        std::to_string(option) +
                        " Seconds",
                    850,
                    rowTop + 9,
                    selected
                        ? cpuBlue
                        : textPrimary,
                    smallFont
                );
            }
        }

        SetViewportOrgEx(
            hdc,
            cpuPageOriginalOrigin.x,
            cpuPageOriginalOrigin.y,
            nullptr
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
        POINT memoryPageOriginalOrigin = {};
        GetViewportOrgEx(
            hdc,
            &memoryPageOriginalOrigin
        );

        // Match the dedicated CPU-page spacing and keep the page
        // clearly separated from the global navigation header.
        SetViewportOrgEx(
            hdc,
            memoryPageOriginalOrigin.x,
            memoryPageOriginalOrigin.y +
            scaleUiCoordinate(88),
            nullptr
        );

        const COLORREF memoryPurple =
            uiColor(RGB(190, 72, 245));

        const COLORREF memoryPurpleBright =
            uiColor(RGB(205, 84, 255));

        const COLORREF memoryPanel =
            uiColor(RGB(16, 26, 35));

        const COLORREF memoryPanelAlt =
            uiColor(RGB(20, 31, 41));

        const COLORREF memoryGaugeTrack =
            uiColor(RGB(49, 64, 77));

        const COLORREF memoryAvailableColor =
            uiColor(RGB(65, 100, 175));

        const size_t memoryHistorySamplesForRange =
            static_cast<size_t>(
                cpuHistoryRangeSeconds * 2
            );

        const std::string memoryHistoryRangeLabel =
            "Last " +
            std::to_string(
                cpuHistoryRangeSeconds
            ) +
            " Seconds";

        const MemoryPerformanceDetails
            memoryPerformance =
                getMemoryPerformanceDetails();

        const MemoryHardwareDetails&
            memoryHardware =
                getMemoryHardwareDetails();

        const double availableRamGB =
            (std::max)(
                0.0,
                totalRamGB - usedRamGB
            );

        const double usedPercent =
            totalRamGB > 0.0
                ? std::clamp(
                    (usedRamGB / totalRamGB) * 100.0,
                    0.0,
                    100.0
                  )
                : 0.0;

        const double availablePercent =
            100.0 - usedPercent;

        auto formatMemoryGB =
            [](double value)
            -> std::string
        {
            std::ostringstream stream;
            stream
                << std::fixed
                << std::setprecision(1)
                << value
                << " GB";
            return stream.str();
        };

        const std::string totalMemoryText =
            formatMemoryGB(totalRamGB);

        const std::string usedMemoryText =
            formatMemoryGB(usedRamGB);

        const std::string availableMemoryText =
            formatMemoryGB(availableRamGB);

        std::string committedText = "--";
        std::string cachedText = "--";
        std::string pagedPoolText = "--";
        std::string nonPagedPoolText = "--";

        if (memoryPerformance.valid)
        {
            committedText =
                formatMemoryGigabyteNumber(
                    memoryPerformance.commitTotalBytes
                ) +
                " / " +
                formatMemoryGigabyteNumber(
                    memoryPerformance.commitLimitBytes
                ) +
                " GB";

            cachedText =
                formatMemoryBytes(
                    memoryPerformance.cachedBytes
                );

            pagedPoolText =
                formatMemoryBytes(
                    memoryPerformance.pagedPoolBytes
                );

            nonPagedPoolText =
                formatMemoryBytes(
                    memoryPerformance.nonPagedPoolBytes
                );
        }

        std::string memorySpeedText = "--";

        if (memoryHardware.speedMTs > 0)
        {
            memorySpeedText =
                std::to_string(
                    memoryHardware.speedMTs
                ) +
                " MT/s";
        }

        std::string memorySlotsText = "--";

        if (memoryHardware.totalSlots > 0)
        {
            memorySlotsText =
                std::to_string(
                    memoryHardware.usedSlots
                ) +
                " / " +
                std::to_string(
                    memoryHardware.totalSlots
                );
        }

        std::string hardwareReservedText = "--";

        if (memoryHardware.hardwareReservedValid)
        {
            hardwareReservedText =
                formatMemoryBytes(
                    memoryHardware.hardwareReservedBytes
                );
        }

        // ----------------------------------------------------
        // MEMORY PAGE HEADER
        // ----------------------------------------------------

        drawRoundedBox(
            hdc,
            20,
            4,
            96,
            80,
            uiColor(RGB(25, 38, 50))
        );

        if (
            uiAssetExists(L"memory.png")
        )
        {
            drawPngImage(
                hdc,
                L"memory.png",
                32,
                16,
                52,
                52
            );
        }
        else if (
            uiAssetExists(L"sidebar_memory.png")
        )
        {
            drawTintedPngImage(
                hdc,
                L"sidebar_memory.png",
                32,
                16,
                52,
                52,
                uiColor(RGB(220, 225, 235))
            );
        }

        drawText(
            hdc,
            "Memory",
            118,
            12,
            textPrimary,
            bigFont
        );

        drawText(
            hdc,
            "Live memory usage, performance and details.",
            118,
            54,
            textSecondary,
            smallFont
        );

        // Shared real 15/30/60-second history selector.
        drawRoundedBox(
            hdc,
            820,
            12,
            995,
            54,
            memoryPanel
        );

        HPEN memoryClockPen =
            CreatePen(
                PS_SOLID,
                1,
                uiColor(RGB(205, 221, 234))
            );

        HGDIOBJ oldMemoryClockPen =
            SelectObject(
                hdc,
                memoryClockPen
            );

        HGDIOBJ oldMemoryClockBrush =
            SelectObject(
                hdc,
                GetStockObject(HOLLOW_BRUSH)
            );

        drawUiEllipse(
            hdc,
            836,
            24,
            850,
            38
        );

        MoveToEx(
            hdc,
            843,
            27,
            nullptr
        );

        LineTo(
            hdc,
            843,
            31
        );

        LineTo(
            hdc,
            847,
            33
        );

        SelectObject(
            hdc,
            oldMemoryClockBrush
        );

        SelectObject(
            hdc,
            oldMemoryClockPen
        );

        DeleteObject(
            memoryClockPen
        );

        drawText(
            hdc,
            memoryHistoryRangeLabel,
            858,
            25,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            cpuHistoryRangeDropdownOpen
                ? "^"
                : "v",
            974,
            25,
            textSecondary,
            smallFont
        );

        // Give the header and summary cards the same breathing room
        // as the redesigned CPU page.
        SetViewportOrgEx(
            hdc,
            memoryPageOriginalOrigin.x,
            memoryPageOriginalOrigin.y +
            scaleUiCoordinate(103),
            nullptr
        );

        // ----------------------------------------------------
        // TOP MEMORY SUMMARY CARDS
        // ----------------------------------------------------

        drawRoundedBox(
            hdc,
            20,
            86,
            210,
            196,
            memoryPanel
        );

        drawText(
            hdc,
            "Total Usage",
            34,
            99,
            textPrimary,
            labelFont
        );

        drawCircularGauge(
            hdc,
            32,
            119,
            70,
            usedPercent,
            memoryPurpleBright,
            memoryGaugeTrack
        );

        std::ostringstream memoryUsagePercentText;
        memoryUsagePercentText
            << std::fixed
            << std::setprecision(0)
            << usedPercent
            << "%";

        drawDashboardCenteredText(
            hdc,
            memoryUsagePercentText.str(),
            32,
            119,
            70,
            70,
            textPrimary,
            labelFont
        );

        drawRamGraph(
            hdc,
            112,
            126,
            86,
            56,
            memoryHistorySamplesForRange
        );

        auto drawMemorySummaryCard =
            [&](int left,
                int right,
                const std::string& title,
                const std::string& value,
                const std::string& subText)
        {
            drawRoundedBox(
                hdc,
                left,
                86,
                right,
                196,
                memoryPanel
            );

            drawRoundedBox(
                hdc,
                left + 10,
                102,
                left + 40,
                132,
                memoryPanelAlt
            );

            if (
                uiAssetExists(L"sidebar_memory.png")
            )
            {
                drawTintedPngImage(
                    hdc,
                    L"sidebar_memory.png",
                    left + 17,
                    109,
                    16,
                    16,
                    uiColor(RGB(220, 225, 235))
                );
            }

            drawText(
                hdc,
                title,
                left + 46,
                103,
                textPrimary,
                smallFont
            );

            drawText(
                hdc,
                value,
                left + 46,
                132,
                textPrimary,
                labelFont
            );

            if (!subText.empty())
            {
                drawText(
                    hdc,
                    subText,
                    left + 46,
                    158,
                    textSecondary,
                    smallFont
                );
            }
        };

        std::string memoryTypeText =
            systemInfo.memoryType;

        if (memoryTypeText.empty())
        {
            memoryTypeText = "--";
        }

        drawMemorySummaryCard(
            220,
            344,
            "Total Memory",
            totalMemoryText,
            memoryTypeText
        );

        drawMemorySummaryCard(
            354,
            478,
            "Used Memory",
            usedMemoryText,
            ""
        );

        drawMemorySummaryCard(
            488,
            612,
            "Available",
            availableMemoryText,
            ""
        );

        drawMemorySummaryCard(
            622,
            746,
            "Committed",
            memoryPerformance.valid
                ? formatMemoryGigabyteNumber(
                    memoryPerformance.commitTotalBytes
                  ) + " GB"
                : "--",
            memoryPerformance.valid
                ? "/ " +
                    formatMemoryGigabyteNumber(
                        memoryPerformance.commitLimitBytes
                    ) + " GB"
                : ""
        );

        drawMemorySummaryCard(
            756,
            870,
            "Paged Pool",
            pagedPoolText,
            ""
        );

        drawMemorySummaryCard(
            880,
            995,
            "Non-Paged",
            nonPagedPoolText,
            ""
        );

        // ----------------------------------------------------
        // MEMORY USAGE HISTORY
        // ----------------------------------------------------

        drawRoundedBox(
            hdc,
            20,
            210,
            650,
            410,
            memoryPanel
        );

        if (
            uiAssetExists(L"performance.png")
        )
        {
            drawTintedPngImage(
                hdc,
                L"performance.png",
                34,
                224,
                18,
                18,
                memoryPurpleBright
            );
        }

        drawText(
            hdc,
            "Memory Usage History",
            60,
            223,
            textPrimary,
            labelFont
        );

        drawText(
            hdc,
            "100%",
            34,
            252,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "75%",
            41,
            283,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "50%",
            41,
            315,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "25%",
            41,
            346,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "0%",
            48,
            374,
            textSecondary,
            smallFont
        );

        drawRamGraph(
            hdc,
            67,
            255,
            565,
            130,
            memoryHistorySamplesForRange
        );

        drawText(
            hdc,
            std::to_string(
                cpuHistoryRangeSeconds
            ) + " seconds",
            565,
            387,
            textSecondary,
            smallFont
        );

        // ----------------------------------------------------
        // MEMORY DETAILS
        // ----------------------------------------------------

        drawRoundedBox(
            hdc,
            660,
            210,
            995,
            598,
            memoryPanel
        );

        if (
            uiAssetExists(L"sidebar_memory.png")
        )
        {
            drawTintedPngImage(
                hdc,
                L"sidebar_memory.png",
                678,
                224,
                18,
                18,
                uiColor(RGB(220, 225, 235))
            );
        }

        drawText(
            hdc,
            "Memory Details",
            706,
            222,
            textPrimary,
            labelFont
        );

        const int memoryDetailLabelX = 678;
        const int memoryDetailValueX = 815;
        const int memoryDetailStartY = 256;
        const int memoryDetailGap = 27;

        auto drawMemoryDetailRow =
            [&](int row,
                const std::string& label,
                const std::string& value)
        {
            const int y =
                memoryDetailStartY +
                row * memoryDetailGap;

            drawText(
                hdc,
                label,
                memoryDetailLabelX,
                y,
                textSecondary,
                smallFont
            );

            std::string shownValue =
                shortenPerformanceText(
                    value,
                    24
                );

            setFont(
                hdc,
                smallFont
            );

            SIZE valueExtent = {};

            GetTextExtentPoint32A(
                hdc,
                shownValue.c_str(),
                static_cast<int>(
                    shownValue.size()
                ),
                &valueExtent
            );

            int valueX =
                978 - valueExtent.cx;

            if (valueX < memoryDetailValueX)
            {
                valueX =
                    memoryDetailValueX;
            }

            drawText(
                hdc,
                shownValue,
                valueX,
                y,
                textPrimary,
                smallFont
            );
        };

        std::ostringstream usedDetailText;
        usedDetailText
            << std::fixed
            << std::setprecision(1)
            << usedRamGB
            << " GB ("
            << std::setprecision(0)
            << usedPercent
            << "%)";

        std::ostringstream availableDetailText;
        availableDetailText
            << std::fixed
            << std::setprecision(1)
            << availableRamGB
            << " GB ("
            << std::setprecision(0)
            << availablePercent
            << "%)";

        drawMemoryDetailRow(
            0,
            "Total Memory",
            totalMemoryText
        );

        drawMemoryDetailRow(
            1,
            "Used Memory",
            usedDetailText.str()
        );

        drawMemoryDetailRow(
            2,
            "Available Memory",
            availableDetailText.str()
        );

        drawMemoryDetailRow(
            3,
            "Committed",
            committedText
        );

        drawMemoryDetailRow(
            4,
            "Cached",
            cachedText
        );

        drawMemoryDetailRow(
            5,
            "Paged Pool",
            pagedPoolText
        );

        drawMemoryDetailRow(
            6,
            "Non-Paged Pool",
            nonPagedPoolText
        );

        drawMemoryDetailRow(
            7,
            "Memory Speed",
            memorySpeedText
        );

        drawMemoryDetailRow(
            8,
            "Form Factor",
            memoryHardware.formFactor
        );

        drawMemoryDetailRow(
            9,
            "Hardware Reserved",
            hardwareReservedText
        );

        drawMemoryDetailRow(
            10,
            "Slots Used",
            memorySlotsText
        );

        // ----------------------------------------------------
        // MEMORY COMPOSITION
        // Only categories SysMon can measure directly are shown.
        // ----------------------------------------------------

        drawRoundedBox(
            hdc,
            20,
            420,
            385,
            598,
            memoryPanel
        );

        if (
            uiAssetExists(L"sidebar_memory.png")
        )
        {
            drawTintedPngImage(
                hdc,
                L"sidebar_memory.png",
                34,
                434,
                18,
                18,
                uiColor(RGB(220, 225, 235))
            );
        }

        drawText(
            hdc,
            "Memory Composition",
            60,
            433,
            textPrimary,
            labelFont
        );

        const int compositionLeft = 35;
        const int compositionTop = 480;
        const int compositionRight = 370;
        const int compositionBottom = 498;
        const int compositionWidth =
            compositionRight - compositionLeft;

        drawRecommendedUsageBar(
            hdc,
            compositionLeft,
            compositionTop,
            compositionWidth,
            compositionBottom - compositionTop,
            usedPercent,
            memoryPurpleBright
        );

        // Small legend dots.
        HBRUSH usedLegendBrush =
            CreateSolidBrush(
                memoryPurpleBright
            );
        HGDIOBJ oldUsedLegendBrush =
            SelectObject(
                hdc,
                usedLegendBrush
            );
        HPEN usedLegendPen =
            CreatePen(
                PS_SOLID,
                1,
                memoryPurpleBright
            );
        HGDIOBJ oldUsedLegendPen =
            SelectObject(
                hdc,
                usedLegendPen
            );

        drawUiEllipse(
            hdc,
            35,
            528,
            45,
            538
        );

        SelectObject(
            hdc,
            oldUsedLegendPen
        );
        SelectObject(
            hdc,
            oldUsedLegendBrush
        );
        DeleteObject(
            usedLegendPen
        );
        DeleteObject(
            usedLegendBrush
        );

        HBRUSH availableLegendBrush =
            CreateSolidBrush(
                memoryAvailableColor
            );
        HGDIOBJ oldAvailableLegendBrush =
            SelectObject(
                hdc,
                availableLegendBrush
            );
        HPEN availableLegendPen =
            CreatePen(
                PS_SOLID,
                1,
                memoryAvailableColor
            );
        HGDIOBJ oldAvailableLegendPen =
            SelectObject(
                hdc,
                availableLegendPen
            );

        drawUiEllipse(
            hdc,
            195,
            528,
            205,
            538
        );

        SelectObject(
            hdc,
            oldAvailableLegendPen
        );
        SelectObject(
            hdc,
            oldAvailableLegendBrush
        );
        DeleteObject(
            availableLegendPen
        );
        DeleteObject(
            availableLegendBrush
        );

        std::ostringstream inUseLegend;
        inUseLegend
            << "In Use ("
            << std::fixed
            << std::setprecision(0)
            << usedPercent
            << "%)";

        std::ostringstream availableLegend;
        availableLegend
            << "Available ("
            << std::fixed
            << std::setprecision(0)
            << availablePercent
            << "%)";

        drawText(
            hdc,
            inUseLegend.str(),
            52,
            524,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            availableLegend.str(),
            212,
            524,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            usedMemoryText,
            52,
            550,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            availableMemoryText,
            212,
            550,
            textPrimary,
            smallFont
        );

        // ----------------------------------------------------
        // TOP MEMORY PROCESSES
        // ----------------------------------------------------

        drawRoundedBox(
            hdc,
            395,
            420,
            650,
            598,
            memoryPanel
        );

        drawText(
            hdc,
            "Top Memory Processes",
            410,
            434,
            textPrimary,
            labelFont
        );

        drawText(
            hdc,
            "View All >",
            580,
            434,
            memoryPurpleBright,
            smallFont
        );

        drawText(
            hdc,
            "Name",
            410,
            459,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "Memory",
            525,
            459,
            textSecondary,
            smallFont
        );

        drawText(
            hdc,
            "%",
            620,
            459,
            textSecondary,
            smallFont
        );

        std::vector<ProcessInfo> memoryTopProcesses =
            performanceProcesses;

        memoryTopProcesses.erase(
            std::remove_if(
                memoryTopProcesses.begin(),
                memoryTopProcesses.end(),
                [](const ProcessInfo& process)
                {
                    return process.pid == 0;
                }
            ),
            memoryTopProcesses.end()
        );

        std::stable_sort(
            memoryTopProcesses.begin(),
            memoryTopProcesses.end(),
            [](const ProcessInfo& a,
               const ProcessInfo& b)
            {
                if (a.memoryMB != b.memoryMB)
                {
                    return a.memoryMB > b.memoryMB;
                }

                return a.cpuPercent > b.cpuPercent;
            }
        );

        const int topMemoryProcessCount =
            (std::min)(
                5,
                static_cast<int>(
                    memoryTopProcesses.size()
                )
            );

        for (
            int index = 0;
            index < topMemoryProcessCount;
            index++
        )
        {
            const ProcessInfo& process =
                memoryTopProcesses[index];

            const int rowY =
                481 +
                index * 21;

            drawProcessExecutableIcon(
                hdc,
                process.pid,
                409,
                rowY - 2,
                15
            );

            drawText(
                hdc,
                shortenPerformanceText(
                    process.name,
                    13
                ),
                429,
                rowY,
                textPrimary,
                smallFont
            );

            std::ostringstream processMemoryText;
            processMemoryText
                << std::fixed
                << std::setprecision(
                    process.memoryMB >= 100.0
                        ? 0
                        : 1
                )
                << process.memoryMB
                << " MB";

            drawText(
                hdc,
                processMemoryText.str(),
                525,
                rowY,
                textSecondary,
                smallFont
            );

            std::string processMemoryPercentText = "--";

            if (totalRamGB > 0.0)
            {
                const double processMemoryPercent =
                    (process.memoryMB /
                        (totalRamGB * 1024.0)) *
                    100.0;

                std::ostringstream percentStream;
                percentStream
                    << std::fixed
                    << std::setprecision(1)
                    << processMemoryPercent
                    << "%";

                processMemoryPercentText =
                    percentStream.str();
            }

            drawText(
                hdc,
                processMemoryPercentText,
                610,
                rowY,
                textSecondary,
                smallFont
            );
        }

        // ----------------------------------------------------
        // MEMORY HISTORY RANGE DROPDOWN OVERLAY
        // ----------------------------------------------------

        if (cpuHistoryRangeDropdownOpen)
        {
            SetViewportOrgEx(
                hdc,
                memoryPageOriginalOrigin.x,
                memoryPageOriginalOrigin.y +
            scaleUiCoordinate(88),
                nullptr
            );

            const int menuLeft = 820;
            const int menuTop = 60;
            const int menuRight = 995;
            const int rowHeight = 34;
            const int menuBottom =
                menuTop + rowHeight * 3;

            drawRoundedBox(
                hdc,
                menuLeft,
                menuTop,
                menuRight,
                menuBottom,
                memoryPanel
            );

            const int rangeOptions[] =
            {
                15,
                30,
                60
            };

            for (
                int index = 0;
                index < 3;
                index++
            )
            {
                const int option =
                    rangeOptions[index];

                const int rowTop =
                    menuTop +
                    index * rowHeight;

                const bool selected =
                    cpuHistoryRangeSeconds ==
                    option;

                if (selected)
                {
                    drawRoundedBox(
                        hdc,
                        menuLeft + 4,
                        rowTop + 3,
                        menuRight - 4,
                        rowTop + rowHeight - 3,
                        uiColor(RGB(47, 28, 63))
                    );

                    HBRUSH selectedDotBrush =
                        CreateSolidBrush(
                            memoryPurpleBright
                        );

                    HGDIOBJ oldSelectedDotBrush =
                        SelectObject(
                            hdc,
                            selectedDotBrush
                        );

                    HPEN selectedDotPen =
                        CreatePen(
                            PS_SOLID,
                            1,
                            memoryPurpleBright
                        );

                    HGDIOBJ oldSelectedDotPen =
                        SelectObject(
                            hdc,
                            selectedDotPen
                        );

                    drawUiEllipse(
                        hdc,
                        835,
                        rowTop + 14,
                        841,
                        rowTop + 20
                    );

                    SelectObject(
                        hdc,
                        oldSelectedDotPen
                    );

                    SelectObject(
                        hdc,
                        oldSelectedDotBrush
                    );

                    DeleteObject(
                        selectedDotPen
                    );

                    DeleteObject(
                        selectedDotBrush
                    );
                }

                drawText(
                    hdc,
                    "Last " +
                        std::to_string(option) +
                        " Seconds",
                    850,
                    rowTop + 9,
                    selected
                        ? memoryPurpleBright
                        : textPrimary,
                    smallFont
                );
            }
        }

        SetViewportOrgEx(
            hdc,
            memoryPageOriginalOrigin.x,
            memoryPageOriginalOrigin.y,
            nullptr
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
        POINT diskPageOriginalOrigin = {};
        GetViewportOrgEx(
            hdc,
            &diskPageOriginalOrigin
        );

        SetViewportOrgEx(
            hdc,
            diskPageOriginalOrigin.x,
            diskPageOriginalOrigin.y +
            scaleUiCoordinate(88),
            nullptr
        );

        const COLORREF diskGreenBright =
            uiColor(RGB(70, 235, 105));

        const COLORREF diskReadGreen =
            uiColor(RGB(65, 235, 105));

        const COLORREF diskWriteBlue =
            uiColor(RGB(32, 185, 235));

        const COLORREF diskPanel =
            uiColor(RGB(16, 26, 35));

        const COLORREF diskPanelAlt =
            uiColor(RGB(20, 31, 41));

        const COLORREF diskGaugeTrack =
            uiColor(RGB(49, 64, 77));

        const size_t diskHistorySamplesForRange =
            static_cast<size_t>(
                cpuHistoryRangeSeconds * 2
            );

        const std::string diskHistoryRangeLabel =
            "Last " +
            std::to_string(
                cpuHistoryRangeSeconds
            ) +
            " Seconds";

        // ----------------------------------------------------
        // DISK PAGE HEADER
        // ----------------------------------------------------

        drawRoundedBox(
            hdc,
            20,
            4,
            96,
            80,
            uiColor(RGB(25, 38, 50))
        );

        if (
            uiAssetExists(L"disk.png")
        )
        {
            drawPngImage(
                hdc,
                L"disk.png",
                32,
                16,
                52,
                52
            );
        }
        else if (
            uiAssetExists(L"sidebar_disk.png")
        )
        {
            drawTintedPngImage(
                hdc,
                L"sidebar_disk.png",
                36,
                20,
                44,
                44,
                uiColor(RGB(220, 232, 240))
            );
        }

        drawText(
            hdc,
            "Disk",
            118,
            12,
            textPrimary,
            bigFont
        );

        drawText(
            hdc,
            "Disk performance, active time and transfer rates.",
            118,
            54,
            textSecondary,
            smallFont
        );

        // Shared real-history range dropdown.
        drawRoundedBox(
            hdc,
            820,
            12,
            995,
            54,
            diskPanel
        );

        HPEN diskClockPen =
            CreatePen(
                PS_SOLID,
                1,
                uiColor(RGB(205, 221, 234))
            );

        HGDIOBJ oldDiskClockPen =
            SelectObject(
                hdc,
                diskClockPen
            );

        HGDIOBJ oldDiskClockBrush =
            SelectObject(
                hdc,
                GetStockObject(HOLLOW_BRUSH)
            );

        drawUiEllipse(
            hdc,
            835,
            25,
            849,
            39
        );

        MoveToEx(
            hdc,
            842,
            28,
            nullptr
        );

        LineTo(
            hdc,
            842,
            33
        );

        LineTo(
            hdc,
            846,
            35
        );

        SelectObject(
            hdc,
            oldDiskClockBrush
        );

        SelectObject(
            hdc,
            oldDiskClockPen
        );

        DeleteObject(
            diskClockPen
        );

        drawText(
            hdc,
            diskHistoryRangeLabel,
            860,
            24,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            cpuHistoryRangeDropdownOpen
                ? "^"
                : "v",
            976,
            24,
            textSecondary,
            smallFont
        );

        if (diskStats.empty())
        {
            drawRoundedBox(
                hdc,
                20,
                100,
                995,
                235,
                diskPanel
            );

            drawText(
                hdc,
                "No physical disk information is currently available.",
                44,
                135,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                "SysMon will automatically show a disk here when Windows detects it.",
                44,
                170,
                textSecondary,
                smallFont
            );
        }
        else
        {
            if (
                selectedDiskIndex < 0 ||
                selectedDiskIndex >=
                    static_cast<int>(
                        diskStats.size()
                    )
            )
            {
                selectedDiskIndex = 0;
            }

            const DiskStats& disk =
                diskStats[
                    selectedDiskIndex
                ];

            // Compact physical-disk selector. It automatically reflects
            // the live diskStats list, which is re-enumerated in Stats.cpp.
            drawRoundedBox(
                hdc,
                520,
                54,
                800,
                82,
                diskPanel
            );

            drawText(
                hdc,
                "<",
                533,
                59,
                diskStats.size() > 1
                    ? diskGreenBright
                    : uiColor(RGB(82, 96, 108)),
                labelFont
            );

            std::string diskSelectorText =
                std::to_string(
                    selectedDiskIndex + 1
                ) +
                "/" +
                std::to_string(
                    diskStats.size()
                ) +
                "  " +
                disk.displayName;

            if (
                !disk.model.empty() &&
                disk.model != "--"
            )
            {
                diskSelectorText +=
                    "  " + disk.model;
            }

            diskSelectorText =
                shortenPerformanceText(
                    diskSelectorText,
                    30
                );

            drawText(
                hdc,
                diskSelectorText,
                558,
                61,
                textPrimary,
                smallFont
            );

            drawText(
                hdc,
                ">",
                777,
                59,
                diskStats.size() > 1
                    ? diskGreenBright
                    : uiColor(RGB(82, 96, 108)),
                labelFont
            );

            const DiskVolumeUsage volumeUsage =
                queryDiskVolumeUsage(
                    disk
                );

            std::string activeText = "--";
            std::string responseText = "--";
            std::string readText = "--";
            std::string writeText = "--";
            std::string readOperationsText = "--";
            std::string writeOperationsText = "--";
            std::string totalOperationsText = "--";

            if (disk.performanceValid)
            {
                std::ostringstream activeStream;
                activeStream
                    << std::fixed
                    << std::setprecision(0)
                    << disk.activeTimePercent
                    << "%";
                activeText = activeStream.str();

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

                std::ostringstream readOpsStream;
                readOpsStream
                    << std::fixed
                    << std::setprecision(0)
                    << disk.readOperationsPerSecond;
                readOperationsText =
                    readOpsStream.str();

                std::ostringstream writeOpsStream;
                writeOpsStream
                    << std::fixed
                    << std::setprecision(0)
                    << disk.writeOperationsPerSecond;
                writeOperationsText =
                    writeOpsStream.str();

                std::ostringstream totalOpsStream;
                totalOpsStream
                    << std::fixed
                    << std::setprecision(0)
                    << (
                        disk.readOperationsPerSecond +
                        disk.writeOperationsPerSecond
                    );
                totalOperationsText =
                    totalOpsStream.str();
            }

            const std::string totalReadText =
                formatDiskByteCounter(
                    disk.totalBytesRead
                );

            const std::string totalWriteText =
                formatDiskByteCounter(
                    disk.totalBytesWritten
                );

            // Everything below the header follows the same vertical
            // rhythm as the dedicated CPU/Memory pages.
            SetViewportOrgEx(
                hdc,
                diskPageOriginalOrigin.x,
                diskPageOriginalOrigin.y +
            scaleUiCoordinate(103),
                nullptr
            );

            // ------------------------------------------------
            // TOP DISK SUMMARY CARDS
            // ------------------------------------------------

            drawRoundedBox(
                hdc,
                20,
                86,
                245,
                196,
                diskPanel
            );

            drawText(
                hdc,
                "Active Time",
                35,
                99,
                textPrimary,
                labelFont
            );

            drawCircularGauge(
                hdc,
                34,
                119,
                70,
                disk.performanceValid
                    ? disk.activeTimePercent
                    : 0.0,
                diskGreenBright,
                diskGaugeTrack
            );

            drawDashboardCenteredText(
                hdc,
                activeText,
                34,
                119,
                70,
                70,
                textPrimary,
                labelFont
            );

            const std::vector<double> activeVisible =
                diskHistoryTail(
                    disk.activeHistory,
                    diskHistorySamplesForRange
                );

            drawDashboardHistoryGraph(
                hdc,
                115,
                126,
                112,
                56,
                activeVisible,
                100.0,
                diskGreenBright
            );

            auto drawDiskSummaryCard =
                [&](int left,
                    int right,
                    const std::string& title,
                    const std::string& value,
                    const wchar_t* iconPath,
                    COLORREF accent)
            {
                drawRoundedBox(
                    hdc,
                    left,
                    86,
                    right,
                    196,
                    diskPanel
                );

                drawRoundedBox(
                    hdc,
                    left + 11,
                    103,
                    left + 45,
                    137,
                    diskPanelAlt
                );

                if (
                    iconPath != nullptr &&
                    uiAssetExists(iconPath)
                )
                {
                    drawTintedPngImage(
                        hdc,
                        iconPath,
                        left + 19,
                        111,
                        18,
                        18,
                        accent
                    );
                }

                drawText(
                    hdc,
                    title,
                    left + 52,
                    104,
                    textPrimary,
                    smallFont
                );

                drawText(
                    hdc,
                    value,
                    left + 52,
                    135,
                    textPrimary,
                    labelFont
                );
            };

            drawDiskSummaryCard(
                255,
                395,
                "Read Speed",
                readText,
                L"performance.png",
                diskReadGreen
            );

            drawDiskSummaryCard(
                405,
                545,
                "Write Speed",
                writeText,
                L"performance.png",
                diskWriteBlue
            );

            drawDiskSummaryCard(
                555,
                685,
                "Response",
                responseText,
                L"sidebar_disk.png",
                uiColor(RGB(205, 221, 234))
            );

            drawDiskSummaryCard(
                695,
                825,
                "Total Read",
                totalReadText,
                L"sidebar_disk.png",
                uiColor(RGB(205, 221, 234))
            );

            drawDiskSummaryCard(
                835,
                995,
                "Total Write",
                totalWriteText,
                L"sidebar_disk.png",
                uiColor(RGB(205, 221, 234))
            );

            // ------------------------------------------------
            // DISK TRANSFER GRAPH
            // ------------------------------------------------

            drawRoundedBox(
                hdc,
                20,
                210,
                650,
                410,
                diskPanel
            );

            if (
                uiAssetExists(L"performance.png")
            )
            {
                drawTintedPngImage(
                    hdc,
                    L"performance.png",
                    34,
                    224,
                    18,
                    18,
                    diskGreenBright
                );
            }

            drawText(
                hdc,
                "Disk Transfer Rate",
                60,
                223,
                textPrimary,
                labelFont
            );

            HBRUSH readLegendBrush =
                CreateSolidBrush(
                    diskReadGreen
                );

            HGDIOBJ oldReadLegendBrush =
                SelectObject(
                    hdc,
                    readLegendBrush
                );

            HPEN readLegendPen =
                CreatePen(
                    PS_SOLID,
                    1,
                    diskReadGreen
                );

            HGDIOBJ oldReadLegendPen =
                SelectObject(
                    hdc,
                    readLegendPen
                );

            drawUiEllipse(
                hdc,
                506,
                226,
                516,
                236
            );

            SelectObject(
                hdc,
                oldReadLegendPen
            );
            SelectObject(
                hdc,
                oldReadLegendBrush
            );
            DeleteObject(readLegendPen);
            DeleteObject(readLegendBrush);

            drawText(
                hdc,
                "Read",
                522,
                221,
                textSecondary,
                smallFont
            );

            HBRUSH writeLegendBrush =
                CreateSolidBrush(
                    diskWriteBlue
                );

            HGDIOBJ oldWriteLegendBrush =
                SelectObject(
                    hdc,
                    writeLegendBrush
                );

            HPEN writeLegendPen =
                CreatePen(
                    PS_SOLID,
                    1,
                    diskWriteBlue
                );

            HGDIOBJ oldWriteLegendPen =
                SelectObject(
                    hdc,
                    writeLegendPen
                );

            drawUiEllipse(
                hdc,
                566,
                226,
                576,
                236
            );

            SelectObject(
                hdc,
                oldWriteLegendPen
            );
            SelectObject(
                hdc,
                oldWriteLegendBrush
            );
            DeleteObject(writeLegendPen);
            DeleteObject(writeLegendBrush);

            drawText(
                hdc,
                "Write",
                582,
                221,
                textSecondary,
                smallFont
            );

            const double transferScale =
                getDiskTransferGraphScaleForRange(
                    disk,
                    diskHistorySamplesForRange
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
                34,
                252,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                transferHalfText.str(),
                34,
                315,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "0 MB/s",
                34,
                374,
                textSecondary,
                smallFont
            );

            drawDiskReadWriteGraph(
                hdc,
                95,
                255,
                537,
                130,
                disk,
                diskHistorySamplesForRange,
                transferScale,
                diskReadGreen,
                diskWriteBlue
            );

            drawText(
                hdc,
                std::to_string(
                    cpuHistoryRangeSeconds
                ) + " seconds",
                565,
                387,
                textSecondary,
                smallFont
            );

            // ------------------------------------------------
            // DISK DETAILS
            // ------------------------------------------------

            drawRoundedBox(
                hdc,
                660,
                210,
                995,
                598,
                diskPanel
            );

            if (
                uiAssetExists(L"sidebar_disk.png")
            )
            {
                drawTintedPngImage(
                    hdc,
                    L"sidebar_disk.png",
                    678,
                    224,
                    18,
                    18,
                    uiColor(RGB(205, 221, 234))
                );
            }

            drawText(
                hdc,
                "Disk Details",
                706,
                222,
                textPrimary,
                labelFont
            );

            const int diskDetailLabelX = 678;
            const int diskDetailValueX = 810;
            const int diskDetailStartY = 256;
            const int diskDetailGap = 27;

            auto drawDiskDetailRow =
                [&](int row,
                    const std::string& label,
                    const std::string& value)
            {
                const int y =
                    diskDetailStartY +
                    row * diskDetailGap;

                drawText(
                    hdc,
                    label,
                    diskDetailLabelX,
                    y,
                    textSecondary,
                    smallFont
                );

                std::string shownValue =
                    shortenPerformanceText(
                        value,
                        24
                    );

                setFont(
                    hdc,
                    smallFont
                );

                SIZE valueExtent = {};

                GetTextExtentPoint32A(
                    hdc,
                    shownValue.c_str(),
                    static_cast<int>(
                        shownValue.size()
                    ),
                    &valueExtent
                );

                int valueX =
                    978 - valueExtent.cx;

                if (valueX < diskDetailValueX)
                {
                    valueX =
                        diskDetailValueX;
                }

                drawText(
                    hdc,
                    shownValue,
                    valueX,
                    y,
                    textPrimary,
                    smallFont
                );
            };

            const std::string driveText =
                disk.driveLettersText.empty()
                    ? "--"
                    : disk.driveLettersText;

            const std::string usedSpaceText =
                volumeUsage.valid
                    ? formatDiskCapacity(
                        volumeUsage.usedGB
                      )
                    : "--";

            const std::string freeSpaceText =
                volumeUsage.valid
                    ? formatDiskCapacity(
                        volumeUsage.freeGB
                      )
                    : "--";

            drawDiskDetailRow(
                0,
                "Model",
                disk.model
            );

            drawDiskDetailRow(
                1,
                "Physical Disk",
                "Disk " +
                    std::to_string(
                        disk.diskNumber
                    )
            );

            drawDiskDetailRow(
                2,
                "Type",
                disk.type
            );

            drawDiskDetailRow(
                3,
                "Volumes",
                driveText
            );

            drawDiskDetailRow(
                4,
                "Capacity",
                formatDiskCapacity(
                    disk.capacityGB
                )
            );

            drawDiskDetailRow(
                5,
                "Formatted",
                formatDiskCapacity(
                    disk.formattedGB
                )
            );

            drawDiskDetailRow(
                6,
                "Used Space",
                usedSpaceText
            );

            drawDiskDetailRow(
                7,
                "Free Space",
                freeSpaceText
            );

            drawDiskDetailRow(
                8,
                "System Disk",
                disk.systemDisk
                    ? "Yes"
                    : "No"
            );

            drawDiskDetailRow(
                9,
                "Page File",
                disk.pageFile
                    ? "Yes"
                    : "No"
            );

            drawDiskDetailRow(
                10,
                "Performance",
                disk.performanceValid
                    ? "Available"
                    : "Unavailable"
            );

            // ------------------------------------------------
            // DISK USAGE
            // ------------------------------------------------

            drawRoundedBox(
                hdc,
                20,
                420,
                360,
                598,
                diskPanel
            );

            if (
                uiAssetExists(L"sidebar_disk.png")
            )
            {
                drawTintedPngImage(
                    hdc,
                    L"sidebar_disk.png",
                    34,
                    434,
                    18,
                    18,
                    uiColor(RGB(205, 221, 234))
                );
            }

            drawText(
                hdc,
                "Disk Usage",
                60,
                432,
                textPrimary,
                labelFont
            );

            std::string usageName =
                shortenPerformanceText(
                    disk.model +
                    (
                        disk.driveLettersText.empty()
                            ? ""
                            : " (" +
                              disk.driveLettersText +
                              ")"
                    ),
                    31
                );

            drawText(
                hdc,
                usageName,
                35,
                470,
                textSecondary,
                smallFont
            );

            if (volumeUsage.valid)
            {
                const double usagePercent =
                    volumeUsage.totalGB > 0.0
                        ? std::clamp(
                            volumeUsage.usedGB /
                            volumeUsage.totalGB *
                            100.0,
                            0.0,
                            100.0
                          )
                        : 0.0;

                drawRecommendedUsageBar(
                    hdc,
                    35,
                    507,
                    310,
                    14,
                    usagePercent,
                    diskGreenBright
                );

                drawText(
                    hdc,
                    formatDiskCapacity(
                        volumeUsage.usedGB
                    ) + " used",
                    35,
                    544,
                    textPrimary,
                    smallFont
                );

                std::string freeLabel =
                    formatDiskCapacity(
                        volumeUsage.freeGB
                    ) + " free";

                setFont(
                    hdc,
                    smallFont
                );

                SIZE freeExtent = {};
                GetTextExtentPoint32A(
                    hdc,
                    freeLabel.c_str(),
                    static_cast<int>(
                        freeLabel.size()
                    ),
                    &freeExtent
                );

                drawText(
                    hdc,
                    freeLabel,
                    345 - freeExtent.cx,
                    544,
                    textPrimary,
                    smallFont
                );

                drawText(
                    hdc,
                    formatDiskCapacity(
                        volumeUsage.totalGB
                    ) + " formatted",
                    35,
                    570,
                    textSecondary,
                    smallFont
                );
            }
            else
            {
                drawText(
                    hdc,
                    "Volume usage is unavailable for this physical disk.",
                    35,
                    510,
                    textSecondary,
                    smallFont
                );
            }

            // ------------------------------------------------
            // DISK ACTIVITY
            // ------------------------------------------------

            drawRoundedBox(
                hdc,
                370,
                420,
                650,
                598,
                diskPanel
            );

            if (
                uiAssetExists(L"performance.png")
            )
            {
                drawTintedPngImage(
                    hdc,
                    L"performance.png",
                    384,
                    434,
                    18,
                    18,
                    uiColor(RGB(205, 221, 234))
                );
            }

            drawText(
                hdc,
                "Disk Activity (Per Second)",
                410,
                432,
                textPrimary,
                labelFont
            );

            const int activityStartY = 470;
            const int activityGap = 25;

            auto drawActivityRow =
                [&](int row,
                    const std::string& label,
                    const std::string& value)
            {
                const int y =
                    activityStartY +
                    row * activityGap;

                drawText(
                    hdc,
                    label,
                    388,
                    y,
                    textSecondary,
                    smallFont
                );

                setFont(
                    hdc,
                    smallFont
                );

                SIZE extent = {};
                GetTextExtentPoint32A(
                    hdc,
                    value.c_str(),
                    static_cast<int>(
                        value.size()
                    ),
                    &extent
                );

                drawText(
                    hdc,
                    value,
                    634 - extent.cx,
                    y,
                    textPrimary,
                    smallFont
                );
            };

            drawActivityRow(
                0,
                "Read operations/s",
                readOperationsText
            );

            drawActivityRow(
                1,
                "Write operations/s",
                writeOperationsText
            );

            drawActivityRow(
                2,
                "Total operations/s",
                totalOperationsText
            );

            drawActivityRow(
                3,
                "Read data/s",
                readText
            );

            drawActivityRow(
                4,
                "Write data/s",
                writeText
            );

            // ------------------------------------------------
            // DISK HISTORY RANGE DROPDOWN OVERLAY
            // ------------------------------------------------

            if (cpuHistoryRangeDropdownOpen)
            {
                SetViewportOrgEx(
                    hdc,
                    diskPageOriginalOrigin.x,
                    diskPageOriginalOrigin.y +
            scaleUiCoordinate(88),
                    nullptr
                );

                const int menuLeft = 820;
                const int menuTop = 60;
                const int menuRight = 995;
                const int rowHeight = 34;
                const int menuBottom =
                    menuTop + rowHeight * 3;

                drawRoundedBox(
                    hdc,
                    menuLeft,
                    menuTop,
                    menuRight,
                    menuBottom,
                    uiColor(RGB(13, 23, 32))
                );

                const int rangeOptions[3] =
                {
                    15,
                    30,
                    60
                };

                for (int index = 0;
                     index < 3;
                     index++)
                {
                    const int option =
                        rangeOptions[index];

                    const int rowTop =
                        menuTop +
                        index * rowHeight;

                    const bool selected =
                        cpuHistoryRangeSeconds ==
                        option;

                    if (selected)
                    {
                        drawRoundedBox(
                            hdc,
                            menuLeft + 4,
                            rowTop + 3,
                            menuRight - 4,
                            rowTop + rowHeight - 3,
                            uiColor(RGB(24, 48, 64))
                        );

                        HBRUSH selectedDotBrush =
                            CreateSolidBrush(
                                diskGreenBright
                            );

                        HGDIOBJ oldSelectedDotBrush =
                            SelectObject(
                                hdc,
                                selectedDotBrush
                            );

                        HPEN selectedDotPen =
                            CreatePen(
                                PS_SOLID,
                                1,
                                diskGreenBright
                            );

                        HGDIOBJ oldSelectedDotPen =
                            SelectObject(
                                hdc,
                                selectedDotPen
                            );

                        drawUiEllipse(
                            hdc,
                            835,
                            rowTop + 14,
                            841,
                            rowTop + 20
                        );

                        SelectObject(
                            hdc,
                            oldSelectedDotPen
                        );
                        SelectObject(
                            hdc,
                            oldSelectedDotBrush
                        );
                        DeleteObject(selectedDotPen);
                        DeleteObject(selectedDotBrush);
                    }

                    drawText(
                        hdc,
                        "Last " +
                            std::to_string(option) +
                            " Seconds",
                        850,
                        rowTop + 9,
                        selected
                            ? diskGreenBright
                            : textPrimary,
                        smallFont
                    );
                }
            }
        }

        SetViewportOrgEx(
            hdc,
            diskPageOriginalOrigin.x,
            diskPageOriginalOrigin.y,
            nullptr
        );
    }


    // --------------------------------------------------------
    // GPU VIEW
    // --------------------------------------------------------
    else if (
        performanceView ==
        PerformanceView::GPU
    )
    {
        POINT gpuPageOriginalOrigin = {};
        GetViewportOrgEx(
            hdc,
            &gpuPageOriginalOrigin
        );

        SetViewportOrgEx(
            hdc,
            gpuPageOriginalOrigin.x,
            gpuPageOriginalOrigin.y +
            scaleUiCoordinate(88),
            nullptr
        );

        const COLORREF gpuGreen =
            uiColor(RGB(80, 235, 95));

        const COLORREF gpuPanel =
            uiColor(RGB(16, 26, 35));

        const COLORREF gpuPanelAlt =
            uiColor(RGB(20, 31, 41));

        const COLORREF gpuGaugeTrack =
            uiColor(RGB(49, 64, 77));

        const size_t gpuHistorySamplesForRange =
            static_cast<size_t>(
                cpuHistoryRangeSeconds * 2
            );

        const std::string gpuHistoryRangeLabel =
            "Last " +
            std::to_string(
                cpuHistoryRangeSeconds
            ) +
            " Seconds";

        // ----------------------------------------------------
        // GPU PAGE HEADER
        // ----------------------------------------------------

        drawRoundedBox(
            hdc,
            20,
            4,
            96,
            80,
            uiColor(RGB(25, 38, 50))
        );

        if (
            uiAssetExists(L"gpu.png")
        )
        {
            drawPngImage(
                hdc,
                L"gpu.png",
                32,
                16,
                52,
                52
            );
        }
        else if (
            uiAssetExists(L"sidebar_gpu.png")
        )
        {
            drawTintedPngImage(
                hdc,
                L"sidebar_gpu.png",
                36,
                20,
                44,
                44,
                uiColor(RGB(220, 232, 240))
            );
        }

        drawText(
            hdc,
            "GPU",
            118,
            12,
            textPrimary,
            bigFont
        );

        drawText(
            hdc,
            "Real-time GPU performance, usage, and graphics statistics.",
            118,
            54,
            textSecondary,
            smallFont
        );

        // Shared real-history range dropdown.
        drawRoundedBox(
            hdc,
            820,
            12,
            995,
            54,
            gpuPanel
        );

        HPEN gpuClockPen =
            CreatePen(
                PS_SOLID,
                1,
                uiColor(RGB(205, 221, 234))
            );

        HGDIOBJ oldGpuClockPen =
            SelectObject(
                hdc,
                gpuClockPen
            );

        HGDIOBJ oldGpuClockBrush =
            SelectObject(
                hdc,
                GetStockObject(HOLLOW_BRUSH)
            );

        drawUiEllipse(
            hdc,
            835,
            25,
            849,
            39
        );

        MoveToEx(
            hdc,
            842,
            28,
            nullptr
        );

        LineTo(
            hdc,
            842,
            33
        );

        LineTo(
            hdc,
            846,
            35
        );

        SelectObject(
            hdc,
            oldGpuClockBrush
        );

        SelectObject(
            hdc,
            oldGpuClockPen
        );

        DeleteObject(
            gpuClockPen
        );

        drawText(
            hdc,
            gpuHistoryRangeLabel,
            860,
            24,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            cpuHistoryRangeDropdownOpen
                ? "^"
                : "v",
            976,
            24,
            textSecondary,
            smallFont
        );

        if (gpuStats.empty())
        {
            drawRoundedBox(
                hdc,
                20,
                100,
                995,
                235,
                gpuPanel
            );

            drawText(
                hdc,
                "No compatible GPU adapter is currently available.",
                44,
                135,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                "SysMon rechecks Windows GPU adapters automatically and will show newly detected GPUs here.",
                44,
                170,
                textSecondary,
                smallFont
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
                gpuStats[
                    selectedGpuIndex
                ];

            // Only show a selector when more than one real adapter exists.
            // The list comes directly from gpuStats, which Stats.cpp
            // re-enumerates every five seconds.
            if (gpuStats.size() > 1)
            {
                drawRoundedBox(
                    hdc,
                    520,
                    54,
                    800,
                    82,
                    gpuPanel
                );

                drawText(
                    hdc,
                    "<",
                    533,
                    59,
                    gpuGreen,
                    labelFont
                );

                std::string gpuSelectorText =
                    std::to_string(
                        selectedGpuIndex + 1
                    ) +
                    "/" +
                    std::to_string(
                        gpuStats.size()
                    ) +
                    "  " +
                    gpu.name;

                gpuSelectorText =
                    shortenPerformanceText(
                        gpuSelectorText,
                        30
                    );

                drawText(
                    hdc,
                    gpuSelectorText,
                    558,
                    61,
                    textPrimary,
                    smallFont
                );

                drawText(
                    hdc,
                    ">",
                    777,
                    59,
                    gpuGreen,
                    labelFont
                );
            }

            const double bytesPerGpuGB =
                1024.0 * 1024.0 * 1024.0;

            const double bytesPerGpuMB =
                1024.0 * 1024.0;

            auto formatGpuPercent =
                [](double value)
                -> std::string
            {
                std::ostringstream stream;
                stream
                    << std::fixed
                    << std::setprecision(0)
                    << std::clamp(
                        value,
                        0.0,
                        100.0
                    )
                    << "%";
                return stream.str();
            };

            auto formatGpuClock =
                [](double mhz)
                -> std::string
            {
                if (mhz <= 0.0)
                {
                    return "--";
                }

                std::ostringstream stream;

                if (mhz >= 1000.0)
                {
                    stream
                        << std::fixed
                        << std::setprecision(2)
                        << (mhz / 1000.0)
                        << " GHz";
                }
                else
                {
                    stream
                        << std::fixed
                        << std::setprecision(0)
                        << mhz
                        << " MHz";
                }

                return stream.str();
            };

            auto formatGpuBytes =
                [&](unsigned long long bytes)
                -> std::string
            {
                if (bytes == 0)
                {
                    return "0 MB";
                }

                std::ostringstream stream;

                if (bytes >=
                    static_cast<unsigned long long>(
                        bytesPerGpuGB
                    ))
                {
                    stream
                        << std::fixed
                        << std::setprecision(1)
                        << (
                            bytes /
                            bytesPerGpuGB
                        )
                        << " GB";
                }
                else
                {
                    stream
                        << std::fixed
                        << std::setprecision(0)
                        << (
                            bytes /
                            bytesPerGpuMB
                        )
                        << " MB";
                }

                return stream.str();
            };

            auto formatGpuTemperature =
                [&](double temperature)
                -> std::string
            {
                if (temperature < 0.0)
                {
                    return "--";
                }

                std::ostringstream stream;
                stream
                    << std::fixed
                    << std::setprecision(0)
                    << SettingsRuntime::temperature(temperature);
                return stream.str();
            };

            auto formatGpuPower =
                [&](double watts,
                    double limit)
                -> std::string
            {
                if (watts < 0.0)
                {
                    return "--";
                }

                std::ostringstream stream;
                stream
                    << std::fixed
                    << std::setprecision(0)
                    << watts
                    << " W";

                if (limit > 0.0)
                {
                    stream
                        << " / "
                        << std::setprecision(0)
                        << limit
                        << " W";
                }

                return stream.str();
            };

            const std::string usageText =
                gpu.performanceValid
                    ? formatGpuPercent(
                        gpu.utilizationPercent
                      )
                    : "--";

            const std::string gpuClockText =
                formatGpuClock(
                    gpu.coreClockMHz
                );

            const std::string memoryClockText =
                formatGpuClock(
                    gpu.memoryClockMHz
                );

            std::string vramUsageText = "--";
            std::string vramSubText;

            if (gpu.dedicatedMemoryTotalBytes > 0)
            {
                vramUsageText =
                    formatGpuBytes(
                        gpu.dedicatedMemoryUsedBytes
                    );

                vramSubText =
                    "/ " +
                    formatGpuBytes(
                        gpu.dedicatedMemoryTotalBytes
                    );
            }

            const std::string temperatureText =
                formatGpuTemperature(
                    gpu.temperatureC
                );

            std::string fanText = "--";

            if (gpu.fanRpm >= 0)
            {
                fanText =
                    std::to_string(
                        gpu.fanRpm
                    ) +
                    " RPM";
            }
            else if (gpu.fanPercent >= 0)
            {
                fanText =
                    std::to_string(
                        gpu.fanPercent
                    ) +
                    "%";
            }

            SetViewportOrgEx(
                hdc,
                gpuPageOriginalOrigin.x,
                gpuPageOriginalOrigin.y +
            scaleUiCoordinate(103),
                nullptr
            );

            // ------------------------------------------------
            // TOP GPU SUMMARY CARDS
            // ------------------------------------------------

            drawRoundedBox(
                hdc,
                20,
                86,
                245,
                196,
                gpuPanel
            );

            drawText(
                hdc,
                "Total Usage",
                35,
                99,
                textPrimary,
                labelFont
            );

            drawCircularGauge(
                hdc,
                34,
                119,
                70,
                gpu.performanceValid
                    ? gpu.utilizationPercent
                    : 0.0,
                gpuGreen,
                gpuGaugeTrack
            );

            drawDashboardCenteredText(
                hdc,
                usageText,
                34,
                119,
                70,
                70,
                textPrimary,
                labelFont
            );

            const std::vector<double> gpuVisibleHistory =
                diskHistoryTail(
                    gpu.utilizationHistory,
                    gpuHistorySamplesForRange
                );

            drawDashboardHistoryGraph(
                hdc,
                115,
                126,
                112,
                56,
                gpuVisibleHistory,
                100.0,
                gpuGreen
            );

            auto drawGpuSummaryCard =
                [&](int left,
                    int right,
                    const std::string& title,
                    const std::string& value,
                    const std::string& subText,
                    const wchar_t* iconPath)
            {
                drawRoundedBox(
                    hdc,
                    left,
                    86,
                    right,
                    196,
                    gpuPanel
                );

                drawRoundedBox(
                    hdc,
                    left + 11,
                    103,
                    left + 45,
                    137,
                    gpuPanelAlt
                );

                if (
                    iconPath != nullptr &&
                    uiAssetExists(iconPath)
                )
                {
                    drawTintedPngImage(
                        hdc,
                        iconPath,
                        left + 19,
                        111,
                        18,
                        18,
                        uiColor(RGB(205, 221, 234))
                    );
                }

                drawText(
                    hdc,
                    title,
                    left + 52,
                    104,
                    textPrimary,
                    smallFont
                );

                drawText(
                    hdc,
                    value,
                    left + 52,
                    135,
                    textPrimary,
                    labelFont
                );

                if (!subText.empty())
                {
                    drawText(
                        hdc,
                        subText,
                        left + 52,
                        159,
                        textSecondary,
                        smallFont
                    );
                }
            };

            drawGpuSummaryCard(
                255,
                395,
                "GPU Clock",
                gpuClockText,
                "",
                L"performance.png"
            );

            drawGpuSummaryCard(
                405,
                545,
                "Memory Clock",
                memoryClockText,
                "",
                L"sidebar_gpu.png"
            );

            drawGpuSummaryCard(
                555,
                685,
                "VRAM Usage",
                vramUsageText,
                vramSubText,
                L"sidebar_memory.png"
            );

            drawGpuSummaryCard(
                695,
                825,
                "Temperature",
                temperatureText,
                "",
                L"sidebar_temperatures.png"
            );

            drawGpuSummaryCard(
                835,
                995,
                "Fan Speed",
                fanText,
                "",
                L"sidebar_gpu.png"
            );

            // ------------------------------------------------
            // GPU USAGE HISTORY
            // ------------------------------------------------

            drawRoundedBox(
                hdc,
                20,
                210,
                650,
                410,
                gpuPanel
            );

            if (
                uiAssetExists(L"performance.png")
            )
            {
                drawTintedPngImage(
                    hdc,
                    L"performance.png",
                    34,
                    224,
                    18,
                    18,
                    gpuGreen
                );
            }

            drawText(
                hdc,
                "GPU Usage History",
                60,
                223,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                "100%",
                34,
                252,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "75%",
                42,
                283,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "50%",
                42,
                315,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "25%",
                42,
                346,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "0%",
                42,
                374,
                textSecondary,
                smallFont
            );

            drawDashboardHistoryGraph(
                hdc,
                67,
                255,
                565,
                130,
                gpuVisibleHistory,
                100.0,
                gpuGreen
            );

            drawText(
                hdc,
                std::to_string(
                    cpuHistoryRangeSeconds
                ) +
                " seconds",
                565,
                387,
                textSecondary,
                smallFont
            );

            // ------------------------------------------------
            // GPU DETAILS
            // ------------------------------------------------

            drawRoundedBox(
                hdc,
                660,
                210,
                995,
                598,
                gpuPanel
            );

            if (
                uiAssetExists(L"sidebar_gpu.png")
            )
            {
                drawTintedPngImage(
                    hdc,
                    L"sidebar_gpu.png",
                    678,
                    224,
                    18,
                    18,
                    uiColor(RGB(205, 221, 234))
                );
            }

            drawText(
                hdc,
                "GPU Details",
                704,
                223,
                textPrimary,
                labelFont
            );

            const int gpuDetailStartY = 258;
            const int gpuDetailGap = 20;

            auto drawGpuDetailRow =
                [&](int row,
                    const std::string& label,
                    const std::string& value)
            {
                const int y =
                    gpuDetailStartY +
                    row * gpuDetailGap;

                drawText(
                    hdc,
                    label,
                    678,
                    y,
                    textSecondary,
                    smallFont
                );

                std::string displayValue =
                    shortenPerformanceText(
                        value.empty()
                            ? "--"
                            : value,
                        30
                    );

                setFont(
                    hdc,
                    smallFont
                );

                SIZE valueExtent = {};
                GetTextExtentPoint32A(
                    hdc,
                    displayValue.c_str(),
                    static_cast<int>(
                        displayValue.size()
                    ),
                    &valueExtent
                );

                int valueX =
                    980 -
                    valueExtent.cx;

                if (valueX < 805)
                {
                    valueX = 805;
                }

                drawText(
                    hdc,
                    displayValue,
                    valueX,
                    y,
                    textPrimary,
                    smallFont
                );

                HPEN separator =
                    CreatePen(
                        PS_SOLID,
                        1,
                        uiColor(RGB(31, 47, 59))
                    );

                HGDIOBJ oldSeparator =
                    SelectObject(
                        hdc,
                        separator
                    );

                MoveToEx(
                    hdc,
                    678,
                    y + 19,
                    nullptr
                );

                LineTo(
                    hdc,
                    980,
                    y + 19
                );

                SelectObject(
                    hdc,
                    oldSeparator
                );

                DeleteObject(
                    separator
                );
            };

            const unsigned long long freeVramBytes =
                gpu.dedicatedMemoryTotalBytes >
                    gpu.dedicatedMemoryUsedBytes
                    ? gpu.dedicatedMemoryTotalBytes -
                        gpu.dedicatedMemoryUsedBytes
                    : 0;

            drawGpuDetailRow(
                0,
                "Name",
                gpu.name
            );

            drawGpuDetailRow(
                1,
                "Vendor",
                gpu.vendor
            );

            drawGpuDetailRow(
                2,
                "Total VRAM",
                gpu.dedicatedMemoryTotalBytes > 0
                    ? formatGpuBytes(
                        gpu.dedicatedMemoryTotalBytes
                      )
                    : "--"
            );

            drawGpuDetailRow(
                3,
                "Used VRAM",
                gpu.dedicatedMemoryTotalBytes > 0
                    ? formatGpuBytes(
                        gpu.dedicatedMemoryUsedBytes
                      )
                    : "--"
            );

            drawGpuDetailRow(
                4,
                "Free VRAM",
                gpu.dedicatedMemoryTotalBytes > 0
                    ? formatGpuBytes(
                        freeVramBytes
                      )
                    : "--"
            );

            drawGpuDetailRow(
                5,
                "Shared Memory",
                gpu.sharedMemoryTotalBytes > 0
                    ? formatGpuBytes(
                        gpu.sharedMemoryTotalBytes
                      )
                    : "--"
            );

            drawGpuDetailRow(
                6,
                "GPU Clock",
                gpuClockText
            );

            drawGpuDetailRow(
                7,
                "Memory Clock",
                memoryClockText
            );

            drawGpuDetailRow(
                8,
                "Temperature",
                temperatureText
            );

            drawGpuDetailRow(
                9,
                "Fan Speed",
                fanText
            );

            drawGpuDetailRow(
                10,
                "Power Draw",
                formatGpuPower(
                    gpu.powerW,
                    gpu.powerLimitW
                )
            );

            drawGpuDetailRow(
                11,
                "Bus Interface",
                gpu.busInterface
            );

            drawGpuDetailRow(
                12,
                gpu.vendor == "NVIDIA"
                    ? "CUDA Cores"
                    : "Compute Cores",
                gpu.computeCores
            );

            drawGpuDetailRow(
                13,
                "Driver Version",
                gpu.driverVersion
            );

            drawGpuDetailRow(
                14,
                "Driver Date",
                gpu.driverDate
            );

            drawGpuDetailRow(
                15,
                "DirectX",
                gpu.directXVersion
            );

            drawGpuDetailRow(
                16,
                "HW Reserved",
                gpu.hardwareReservedMemory
            );

            // ------------------------------------------------
            // VRAM USAGE
            // ------------------------------------------------

            drawRoundedBox(
                hdc,
                20,
                420,
                360,
                598,
                gpuPanel
            );

            if (
                uiAssetExists(L"sidebar_memory.png")
            )
            {
                drawTintedPngImage(
                    hdc,
                    L"sidebar_memory.png",
                    34,
                    434,
                    18,
                    18,
                    uiColor(RGB(205, 221, 234))
                );
            }

            drawText(
                hdc,
                "VRAM Usage",
                60,
                432,
                textPrimary,
                labelFont
            );

            if (gpu.dedicatedMemoryTotalBytes > 0)
            {
                const double vramPercent =
                    std::clamp(
                        static_cast<double>(
                            gpu.dedicatedMemoryUsedBytes
                        ) /
                        static_cast<double>(
                            gpu.dedicatedMemoryTotalBytes
                        ) *
                        100.0,
                        0.0,
                        100.0
                    );

                const std::string vramTotalText =
                    formatGpuBytes(
                        gpu.dedicatedMemoryUsedBytes
                    ) +
                    " / " +
                    formatGpuBytes(
                        gpu.dedicatedMemoryTotalBytes
                    );

                HFONT vramValueFont = labelFont;
                setFont(hdc, vramValueFont);
                SIZE vramExtent = {};
                GetTextExtentPoint32A(
                    hdc,
                    vramTotalText.c_str(),
                    static_cast<int>(vramTotalText.size()),
                    &vramExtent
                );

                if (vramExtent.cx > 150)
                {
                    vramValueFont = smallFont;
                    setFont(hdc, vramValueFont);
                    GetTextExtentPoint32A(
                        hdc,
                        vramTotalText.c_str(),
                        static_cast<int>(vramTotalText.size()),
                        &vramExtent
                    );
                }

                drawText(
                    hdc,
                    vramTotalText,
                    (std::max)(
                        168,
                        345 - static_cast<int>(vramExtent.cx)
                    ),
                    434,
                    textPrimary,
                    vramValueFont
                );

                drawRecommendedUsageBar(
                    hdc,
                    35,
                    485,
                    310,
                    14,
                    vramPercent,
                    gpuGreen
                );

                std::ostringstream usedLegend;
                usedLegend
                    << "Used ("
                    << std::fixed
                    << std::setprecision(0)
                    << vramPercent
                    << "%)";

                std::ostringstream freeLegend;
                freeLegend
                    << "Free ("
                    << std::fixed
                    << std::setprecision(0)
                    << (100.0 - vramPercent)
                    << "%)";

                drawText(
                    hdc,
                    usedLegend.str(),
                    52,
                    526,
                    textSecondary,
                    smallFont
                );

                drawText(
                    hdc,
                    freeLegend.str(),
                    206,
                    526,
                    textSecondary,
                    smallFont
                );

                drawText(
                    hdc,
                    formatGpuBytes(
                        gpu.dedicatedMemoryUsedBytes
                    ),
                    52,
                    551,
                    textPrimary,
                    smallFont
                );

                drawText(
                    hdc,
                    formatGpuBytes(
                        freeVramBytes
                    ),
                    206,
                    551,
                    textPrimary,
                    smallFont
                );
            }
            else
            {
                drawText(
                    hdc,
                    "Dedicated VRAM total is unavailable for this adapter.",
                    35,
                    485,
                    textSecondary,
                    smallFont
                );
            }

            // ------------------------------------------------
            // TOP GPU PROCESSES
            // ------------------------------------------------

            drawRoundedBox(
                hdc,
                370,
                420,
                650,
                598,
                gpuPanel
            );

            if (
                uiAssetExists(L"performance.png")
            )
            {
                drawTintedPngImage(
                    hdc,
                    L"performance.png",
                    384,
                    434,
                    18,
                    18,
                    uiColor(RGB(205, 221, 234))
                );
            }

            drawText(
                hdc,
                "Top GPU Processes",
                410,
                432,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                "View All >",
                580,
                432,
                gpuGreen,
                smallFont
            );

            drawText(
                hdc,
                "Name",
                385,
                459,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "GPU",
                510,
                459,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "VRAM",
                552,
                459,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "PID",
                620,
                459,
                textSecondary,
                smallFont
            );

            std::vector<GpuProcessStats> gpuProcesses =
                gpu.processes;

            std::stable_sort(
                gpuProcesses.begin(),
                gpuProcesses.end(),
                [](const GpuProcessStats& a,
                   const GpuProcessStats& b)
                {
                    if (
                        a.utilizationPercent !=
                        b.utilizationPercent
                    )
                    {
                        return
                            a.utilizationPercent >
                            b.utilizationPercent;
                    }

                    return
                        a.dedicatedMemoryBytes >
                        b.dedicatedMemoryBytes;
                }
            );

            const int topGpuProcessCount =
                (std::min)(
                    5,
                    static_cast<int>(
                        gpuProcesses.size()
                    )
                );

            auto gpuProcessName =
                [&](DWORD pid)
                -> std::string
            {
                for (
                    const ProcessInfo& process :
                    performanceProcesses
                )
                {
                    if (process.pid == pid)
                    {
                        return process.name;
                    }
                }

                return
                    "PID " +
                    std::to_string(pid);
            };

            for (
                int index = 0;
                index < topGpuProcessCount;
                index++
            )
            {
                const GpuProcessStats& process =
                    gpuProcesses[index];

                const int rowY =
                    481 +
                    index * 21;

                drawProcessExecutableIcon(
                    hdc,
                    process.pid,
                    384,
                    rowY - 2,
                    15
                );

                drawText(
                    hdc,
                    shortenPerformanceText(
                        gpuProcessName(
                            process.pid
                        ),
                        13
                    ),
                    404,
                    rowY,
                    textPrimary,
                    smallFont
                );

                std::ostringstream processGpuText;
                processGpuText
                    << std::fixed
                    << std::setprecision(1)
                    << process.utilizationPercent
                    << "%";

                drawText(
                    hdc,
                    processGpuText.str(),
                    510,
                    rowY,
                    textSecondary,
                    smallFont
                );

                drawText(
                    hdc,
                    process.dedicatedMemoryBytes > 0
                        ? formatGpuBytes(
                            process.dedicatedMemoryBytes
                          )
                        : "--",
                    552,
                    rowY,
                    textSecondary,
                    smallFont
                );

                drawText(
                    hdc,
                    std::to_string(
                        process.pid
                    ),
                    615,
                    rowY,
                    textSecondary,
                    smallFont
                );
            }

            if (topGpuProcessCount == 0)
            {
                drawText(
                    hdc,
                    "No active per-process GPU usage reported.",
                    385,
                    492,
                    textSecondary,
                    smallFont
                );
            }

            // ------------------------------------------------
            // GPU HISTORY RANGE DROPDOWN OVERLAY
            // ------------------------------------------------

            if (cpuHistoryRangeDropdownOpen)
            {
                SetViewportOrgEx(
                    hdc,
                    gpuPageOriginalOrigin.x,
                    gpuPageOriginalOrigin.y +
            scaleUiCoordinate(88),
                    nullptr
                );

                const int menuLeft = 820;
                const int menuTop = 60;
                const int menuRight = 995;
                const int rowHeight = 34;
                const int menuBottom =
                    menuTop +
                    rowHeight * 3;

                drawRoundedBox(
                    hdc,
                    menuLeft,
                    menuTop,
                    menuRight,
                    menuBottom,
                    uiColor(RGB(13, 23, 32))
                );

                const int rangeOptions[3] =
                {
                    15,
                    30,
                    60
                };

                for (
                    int index = 0;
                    index < 3;
                    index++
                )
                {
                    const int option =
                        rangeOptions[index];

                    const int rowTop =
                        menuTop +
                        index * rowHeight;

                    const bool selected =
                        cpuHistoryRangeSeconds ==
                        option;

                    if (selected)
                    {
                        drawRoundedBox(
                            hdc,
                            menuLeft + 4,
                            rowTop + 3,
                            menuRight - 4,
                            rowTop + rowHeight - 3,
                            uiColor(RGB(24, 48, 64))
                        );

                        HBRUSH selectedDotBrush =
                            CreateSolidBrush(
                                gpuGreen
                            );

                        HGDIOBJ oldSelectedDotBrush =
                            SelectObject(
                                hdc,
                                selectedDotBrush
                            );

                        HPEN selectedDotPen =
                            CreatePen(
                                PS_SOLID,
                                1,
                                gpuGreen
                            );

                        HGDIOBJ oldSelectedDotPen =
                            SelectObject(
                                hdc,
                                selectedDotPen
                            );

                        drawUiEllipse(
                            hdc,
                            835,
                            rowTop + 14,
                            841,
                            rowTop + 20
                        );

                        SelectObject(
                            hdc,
                            oldSelectedDotPen
                        );
                        SelectObject(
                            hdc,
                            oldSelectedDotBrush
                        );
                        DeleteObject(selectedDotPen);
                        DeleteObject(selectedDotBrush);
                    }

                    drawText(
                        hdc,
                        "Last " +
                            std::to_string(option) +
                            " Seconds",
                        850,
                        rowTop + 9,
                        selected
                            ? gpuGreen
                            : textPrimary,
                        smallFont
                    );
                }
            }
        }

        SetViewportOrgEx(
            hdc,
            gpuPageOriginalOrigin.x,
            gpuPageOriginalOrigin.y,
            nullptr
        );
    }


    // --------------------------------------------------------
    // NETWORK VIEW
    // --------------------------------------------------------
    else
    {
        POINT networkPageOriginalOrigin = {};
        GetViewportOrgEx(
            hdc,
            &networkPageOriginalOrigin
        );

        SetViewportOrgEx(
            hdc,
            networkPageOriginalOrigin.x,
            networkPageOriginalOrigin.y +
            scaleUiCoordinate(88),
            nullptr
        );

        const COLORREF networkBlue =
            uiColor(RGB(25, 174, 245));

        const COLORREF networkGreen =
            uiColor(RGB(65, 235, 115));

        const COLORREF networkPanel =
            uiColor(RGB(16, 26, 35));

        const COLORREF networkPanelAlt =
            uiColor(RGB(20, 31, 41));

        const COLORREF networkGaugeTrack =
            uiColor(RGB(49, 64, 77));

        const size_t networkHistorySamplesForRange =
            static_cast<size_t>(
                cpuHistoryRangeSeconds * 2
            );

        const std::string networkHistoryRangeLabel =
            "Last " +
            std::to_string(
                cpuHistoryRangeSeconds
            ) +
            " Seconds";

        // ----------------------------------------------------
        // NETWORK PAGE HEADER
        // ----------------------------------------------------

        drawRoundedBox(
            hdc,
            20,
            4,
            96,
            80,
            uiColor(RGB(25, 38, 50))
        );

        if (
            uiAssetExists(L"network.png")
        )
        {
            drawPngImage(
                hdc,
                L"network.png",
                32,
                16,
                52,
                52
            );
        }

        drawText(
            hdc,
            "Network",
            118,
            12,
            textPrimary,
            bigFont
        );

        drawText(
            hdc,
            "Network usage, connection details and adapter information.",
            118,
            54,
            textSecondary,
            smallFont
        );

        // Compact adapter selector.  It appears only when multiple
        // real adapters are present in the live networkStats list.
        if (networkStats.size() > 1)
        {
            int safeNetworkIndex =
                std::clamp(
                    selectedNetworkIndex,
                    0,
                    static_cast<int>(
                        networkStats.size()
                    ) - 1
                );

            drawRoundedBox(
                hdc,
                650,
                12,
                805,
                54,
                networkPanel
            );

            drawText(
                hdc,
                "<",
                663,
                24,
                textSecondary,
                labelFont
            );

            drawText(
                hdc,
                "Adapter " +
                    std::to_string(
                        safeNetworkIndex + 1
                    ) +
                    " / " +
                    std::to_string(
                        networkStats.size()
                    ),
                688,
                25,
                textPrimary,
                smallFont
            );

            drawText(
                hdc,
                ">",
                785,
                24,
                textSecondary,
                labelFont
            );
        }

        // Shared real history-range dropdown.
        drawRoundedBox(
            hdc,
            820,
            12,
            995,
            54,
            networkPanel
        );

        HPEN networkClockPen =
            CreatePen(
                PS_SOLID,
                1,
                uiColor(RGB(205, 221, 234))
            );

        HGDIOBJ oldNetworkClockPen =
            SelectObject(
                hdc,
                networkClockPen
            );

        HGDIOBJ oldNetworkClockBrush =
            SelectObject(
                hdc,
                GetStockObject(HOLLOW_BRUSH)
            );

        drawUiEllipse(
            hdc,
            836,
            24,
            850,
            38
        );

        MoveToEx(
            hdc,
            843,
            27,
            nullptr
        );

        LineTo(
            hdc,
            843,
            31
        );

        LineTo(
            hdc,
            847,
            33
        );

        SelectObject(
            hdc,
            oldNetworkClockBrush
        );

        SelectObject(
            hdc,
            oldNetworkClockPen
        );

        DeleteObject(
            networkClockPen
        );

        drawText(
            hdc,
            networkHistoryRangeLabel,
            858,
            25,
            textPrimary,
            smallFont
        );

        drawText(
            hdc,
            cpuHistoryRangeDropdownOpen
                ? "^"
                : "v",
            974,
            25,
            textSecondary,
            smallFont
        );

        // Everything below the header shares the same vertical offset
        // used by the dedicated CPU/Memory/Disk/GPU pages.
        SetViewportOrgEx(
            hdc,
            networkPageOriginalOrigin.x,
            networkPageOriginalOrigin.y +
            scaleUiCoordinate(103),
            nullptr
        );

        if (networkStats.empty())
        {
            drawRoundedBox(
                hdc,
                20,
                86,
                995,
                250,
                networkPanel
            );

            drawText(
                hdc,
                "No Ethernet, Wi-Fi, PPP, or tunnel adapter was detected.",
                45,
                125,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                "SysMon will update automatically when Windows exposes an adapter.",
                45,
                158,
                textSecondary,
                smallFont
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

            const double networkScale =
                getNetworkGraphScale(
                    adapter,
                    networkHistorySamplesForRange
                );

            std::string connectionTypeText =
                adapter.type.empty()
                ? "--"
                : adapter.type;

            std::string networkNameText =
                adapter.ssid != "--"
                ? adapter.ssid
                : (
                    adapter.name != "--"
                    ? adapter.name
                    : adapter.description
                  );

            std::string networkNameSubtext =
                adapter.type == "Wi-Fi" &&
                adapter.wifiStandard != "--"
                ? adapter.wifiStandard
                : (
                    adapter.linkSpeedMbps > 0.0
                    ? formatNetworkSpeed(
                        adapter.linkSpeedMbps
                      )
                    : adapter.description
                  );

            // ------------------------------------------------
            // TOP SUMMARY CARDS
            // ------------------------------------------------

            // Connection Type
            drawRoundedBox(
                hdc,
                20,
                86,
                205,
                196,
                networkPanel
            );

            if (
                uiAssetExists(L"network.png")
            )
            {
                drawTintedPngImage(
                    hdc,
                    L"network.png",
                    34,
                    101,
                    25,
                    25,
                    uiColor(RGB(205, 221, 234))
                );
            }

            drawText(
                hdc,
                "Connection Type",
                68,
                100,
                textPrimary,
                smallFont
            );

            drawText(
                hdc,
                connectionTypeText,
                68,
                128,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                adapter.status,
                68,
                157,
                adapter.status == "Connected"
                    ? networkGreen
                    : textSecondary,
                smallFont
            );

            // Network Name / SSID
            drawRoundedBox(
                hdc,
                215,
                86,
                405,
                196,
                networkPanel
            );

            drawText(
                hdc,
                "Network Name (SSID)",
                230,
                100,
                textPrimary,
                smallFont
            );

            drawText(
                hdc,
                shortenPerformanceText(
                    networkNameText,
                    18
                ),
                230,
                129,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                shortenPerformanceText(
                    networkNameSubtext,
                    20
                ),
                230,
                158,
                textSecondary,
                smallFont
            );

            // Download Speed
            drawRoundedBox(
                hdc,
                415,
                86,
                600,
                196,
                networkPanel
            );

            drawText(
                hdc,
                "Download Speed",
                430,
                100,
                textPrimary,
                smallFont
            );

            drawText(
                hdc,
                formatNetworkSpeed(
                    adapter.downloadMbps
                ),
                430,
                127,
                networkBlue,
                labelFont
            );

            drawDiskHistoryGraph(
                hdc,
                430,
                155,
                154,
                28,
                adapter.downloadHistory,
                networkScale,
                networkBlue,
                uiColor(RGB(14, 48, 67)),
                networkHistorySamplesForRange
            );

            // Upload Speed
            drawRoundedBox(
                hdc,
                610,
                86,
                795,
                196,
                networkPanel
            );

            drawText(
                hdc,
                "Upload Speed",
                625,
                100,
                textPrimary,
                smallFont
            );

            drawText(
                hdc,
                formatNetworkSpeed(
                    adapter.uploadMbps
                ),
                625,
                127,
                networkGreen,
                labelFont
            );

            drawDiskHistoryGraph(
                hdc,
                625,
                155,
                154,
                28,
                adapter.uploadHistory,
                networkScale,
                networkGreen,
                uiColor(RGB(14, 55, 38)),
                networkHistorySamplesForRange
            );

            // Signal Strength for Wi-Fi, otherwise real link speed.
            drawRoundedBox(
                hdc,
                805,
                86,
                995,
                196,
                networkPanel
            );

            if (
                adapter.type == "Wi-Fi" &&
                adapter.signalQuality >= 0
            )
            {
                drawText(
                    hdc,
                    "Signal Strength",
                    820,
                    100,
                    textPrimary,
                    smallFont
                );

                drawText(
                    hdc,
                    std::to_string(
                        adapter.signalQuality
                    ) + "%",
                    820,
                    130,
                    textPrimary,
                    labelFont
                );

                std::string signalLabel =
                    adapter.signalQuality >= 80
                    ? "Excellent"
                    : (
                        adapter.signalQuality >= 60
                        ? "Good"
                        : (
                            adapter.signalQuality >= 40
                            ? "Fair"
                            : "Weak"
                          )
                      );

                COLORREF signalColor =
                    adapter.signalQuality >= 60
                    ? networkGreen
                    : (
                        adapter.signalQuality >= 40
                        ? uiColor(RGB(235, 190, 70))
                        : uiColor(RGB(235, 100, 85))
                      );

                drawText(
                    hdc,
                    signalLabel,
                    820,
                    160,
                    signalColor,
                    smallFont
                );
            }
            else
            {
                drawText(
                    hdc,
                    "Link Speed",
                    820,
                    100,
                    textPrimary,
                    smallFont
                );

                drawText(
                    hdc,
                    adapter.linkSpeedMbps > 0.0
                        ? formatNetworkSpeed(
                            adapter.linkSpeedMbps
                          )
                        : "--",
                    820,
                    132,
                    textPrimary,
                    labelFont
                );

                drawText(
                    hdc,
                    adapter.status,
                    820,
                    160,
                    adapter.status == "Connected"
                        ? networkGreen
                        : textSecondary,
                    smallFont
                );
            }

            // ------------------------------------------------
            // NETWORK USAGE GRAPH
            // ------------------------------------------------

            drawRoundedBox(
                hdc,
                20,
                210,
                680,
                430,
                networkPanel
            );

            if (
                uiAssetExists(L"performance.png")
            )
            {
                drawTintedPngImage(
                    hdc,
                    L"performance.png",
                    34,
                    222,
                    18,
                    18,
                    networkBlue
                );
            }

            drawText(
                hdc,
                "Network Usage",
                60,
                221,
                textPrimary,
                labelFont
            );

            // Legend
            HBRUSH downloadLegendBrush =
                CreateSolidBrush(networkBlue);
            HGDIOBJ oldDownloadLegendBrush =
                SelectObject(
                    hdc,
                    downloadLegendBrush
                );
            HPEN downloadLegendPen =
                CreatePen(
                    PS_SOLID,
                    1,
                    networkBlue
                );
            HGDIOBJ oldDownloadLegendPen =
                SelectObject(
                    hdc,
                    downloadLegendPen
                );
            drawUiEllipse(
                hdc,
                520,
                225,
                530,
                235
            );
            SelectObject(
                hdc,
                oldDownloadLegendPen
            );
            SelectObject(
                hdc,
                oldDownloadLegendBrush
            );
            DeleteObject(downloadLegendPen);
            DeleteObject(downloadLegendBrush);

            drawText(
                hdc,
                "Download",
                536,
                221,
                textSecondary,
                smallFont
            );

            HBRUSH uploadLegendBrush =
                CreateSolidBrush(networkGreen);
            HGDIOBJ oldUploadLegendBrush =
                SelectObject(
                    hdc,
                    uploadLegendBrush
                );
            HPEN uploadLegendPen =
                CreatePen(
                    PS_SOLID,
                    1,
                    networkGreen
                );
            HGDIOBJ oldUploadLegendPen =
                SelectObject(
                    hdc,
                    uploadLegendPen
                );
            drawUiEllipse(
                hdc,
                600,
                225,
                610,
                235
            );
            SelectObject(
                hdc,
                oldUploadLegendPen
            );
            SelectObject(
                hdc,
                oldUploadLegendBrush
            );
            DeleteObject(uploadLegendPen);
            DeleteObject(uploadLegendBrush);

            drawText(
                hdc,
                "Upload",
                616,
                221,
                textSecondary,
                smallFont
            );

            drawNetworkHistoryGraph(
                hdc,
                90,
                258,
                570,
                135,
                adapter,
                networkScale,
                networkBlue,
                networkGreen,
                networkHistorySamplesForRange
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
                formatNetworkSpeed(networkScale),
                30,
                255,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                formatNetworkSpeed(networkScale / 2.0),
                30,
                318,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                formatNetworkSpeed(0),
                39,
                383,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                std::to_string(
                    cpuHistoryRangeSeconds
                ) +
                " seconds",
                590,
                400,
                textSecondary,
                smallFont
            );

            // ------------------------------------------------
            // NETWORK DETAILS
            // ------------------------------------------------

            drawRoundedBox(
                hdc,
                695,
                210,
                995,
                430,
                networkPanel
            );

            if (
                uiAssetExists(L"sidebar_network.png")
            )
            {
                drawTintedPngImage(
                    hdc,
                    L"sidebar_network.png",
                    710,
                    222,
                    18,
                    18,
                    uiColor(RGB(205, 221, 234))
                );
            }

            drawText(
                hdc,
                "Network Details",
                738,
                221,
                textPrimary,
                labelFont
            );

            int networkDetailY = 250;
            const int networkDetailGap = 19;

            auto drawNetworkReferenceDetail =
                [&](const std::string& label,
                    const std::string& value)
            {
                drawText(
                    hdc,
                    label,
                    712,
                    networkDetailY,
                    textSecondary,
                    smallFont
                );

                std::string safeValue =
                    value.empty()
                    ? "--"
                    : value;

                drawText(
                    hdc,
                    shortenPerformanceText(
                        safeValue,
                        20
                    ),
                    825,
                    networkDetailY,
                    textPrimary,
                    smallFont
                );

                networkDetailY +=
                    networkDetailGap;
            };

            drawNetworkReferenceDetail(
                "Adapter Name",
                adapter.description
            );
            drawNetworkReferenceDetail(
                "Connection Type",
                adapter.type == "Wi-Fi" &&
                    adapter.wifiStandard != "--"
                    ? adapter.type +
                        " (" +
                        adapter.wifiStandard +
                        ")"
                    : adapter.type
            );
            drawNetworkReferenceDetail(
                "Status",
                adapter.status
            );
            drawNetworkReferenceDetail(
                "IPv4 Address",
                adapter.ipv4Address
            );
            drawNetworkReferenceDetail(
                "IPv6 Address",
                adapter.ipv6Address
            );
            drawNetworkReferenceDetail(
                "Subnet Mask",
                adapter.subnetMask
            );
            drawNetworkReferenceDetail(
                "Default Gateway",
                adapter.defaultGateway
            );
            drawNetworkReferenceDetail(
                "DNS Server",
                adapter.dnsServers
            );
            drawNetworkReferenceDetail(
                "MAC Address",
                adapter.macAddress
            );

            // ------------------------------------------------
            // DATA USAGE
            // ------------------------------------------------

            drawRoundedBox(
                hdc,
                20,
                445,
                360,
                600,
                networkPanel
            );

            drawText(
                hdc,
                "Data Usage",
                58,
                459,
                textPrimary,
                labelFont
            );

            unsigned long long totalTrafficBytes =
                adapter.totalDownloadedBytes +
                adapter.totalUploadedBytes;

            double downloadShare =
                totalTrafficBytes > 0
                ? 100.0 *
                    static_cast<double>(
                        adapter.totalDownloadedBytes
                    ) /
                    static_cast<double>(
                        totalTrafficBytes
                    )
                : 0.0;

            double uploadShare =
                totalTrafficBytes > 0
                ? 100.0 - downloadShare
                : 0.0;

            drawCircularGauge(
                hdc,
                42,
                484,
                92,
                downloadShare,
                networkBlue,
                networkGaugeTrack
            );

            drawDashboardCenteredText(
                hdc,
                formatNetworkBytes(
                    adapter.totalDownloadedBytes
                ),
                42,
                484,
                92,
                92,
                textPrimary,
                smallFont
            );

            drawText(
                hdc,
                "Downloaded",
                59,
                576,
                networkBlue,
                smallFont
            );

            drawCircularGauge(
                hdc,
                205,
                484,
                92,
                uploadShare,
                networkGreen,
                networkGaugeTrack
            );

            drawDashboardCenteredText(
                hdc,
                formatNetworkBytes(
                    adapter.totalUploadedBytes
                ),
                205,
                484,
                92,
                92,
                textPrimary,
                smallFont
            );

            drawText(
                hdc,
                "Uploaded",
                229,
                576,
                networkGreen,
                smallFont
            );

            // ------------------------------------------------
            // TOP NETWORK PROCESSES
            // Real Windows TCP activity.  SysMon does not invent
            // per-process Mbps values that Windows has not exposed.
            // ------------------------------------------------

            drawRoundedBox(
                hdc,
                375,
                445,
                680,
                600,
                networkPanel
            );

            drawText(
                hdc,
                "Top Network Processes",
                392,
                459,
                textPrimary,
                labelFont
            );

            drawText(
                hdc,
                "View All >",
                610,
                459,
                networkBlue,
                smallFont
            );

            drawText(
                hdc,
                "Name",
                392,
                482,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "Conn.",
                535,
                482,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "State",
                580,
                482,
                textSecondary,
                smallFont
            );

            drawText(
                hdc,
                "PID",
                645,
                482,
                textSecondary,
                smallFont
            );

            struct NetworkProcessSummary
            {
                DWORD pid = 0;
                std::string name = "--";
                int connectionCount = 0;
                std::string state = "ACTIVE";
            };

            std::map<DWORD, NetworkProcessSummary>
                networkProcessMap;

            for (
                const NetworkConnectionInfo& connection :
                activeNetworkConnections
            )
            {
                NetworkProcessSummary& summary =
                    networkProcessMap[
                        connection.pid
                    ];

                summary.pid = connection.pid;
                summary.name = connection.processName;
                summary.connectionCount++;

                if (
                    connection.state ==
                    "ESTABLISHED"
                )
                {
                    summary.state =
                        "ESTAB.";
                }
            }

            std::vector<NetworkProcessSummary>
                networkProcesses;

            for (const auto& entry :
                 networkProcessMap)
            {
                networkProcesses.push_back(
                    entry.second
                );
            }

            std::stable_sort(
                networkProcesses.begin(),
                networkProcesses.end(),
                [](const NetworkProcessSummary& a,
                   const NetworkProcessSummary& b)
                {
                    if (
                        a.connectionCount !=
                        b.connectionCount
                    )
                    {
                        return
                            a.connectionCount >
                            b.connectionCount;
                    }

                    return a.name < b.name;
                }
            );

            const int visibleNetworkProcesses =
                (std::min)(
                    5,
                    static_cast<int>(
                        networkProcesses.size()
                    )
                );

            for (
                int processIndex = 0;
                processIndex < visibleNetworkProcesses;
                processIndex++
            )
            {
                const NetworkProcessSummary& process =
                    networkProcesses[
                        processIndex
                    ];

                int rowY =
                    503 +
                    processIndex * 19;

                drawProcessExecutableIcon(
                    hdc,
                    process.pid,
                    392,
                    rowY - 2,
                    15
                );

                drawText(
                    hdc,
                    shortenPerformanceText(
                        process.name,
                        16
                    ),
                    412,
                    rowY,
                    textPrimary,
                    smallFont
                );

                drawText(
                    hdc,
                    std::to_string(
                        process.connectionCount
                    ),
                    548,
                    rowY,
                    textSecondary,
                    smallFont
                );

                drawText(
                    hdc,
                    process.state,
                    580,
                    rowY,
                    textSecondary,
                    smallFont
                );

                drawText(
                    hdc,
                    std::to_string(
                        process.pid
                    ),
                    642,
                    rowY,
                    textSecondary,
                    smallFont
                );
            }

            if (visibleNetworkProcesses == 0)
            {
                drawText(
                    hdc,
                    "No active TCP connections reported.",
                    392,
                    510,
                    textSecondary,
                    smallFont
                );
            }

            // ------------------------------------------------
            // NETWORK ADAPTERS
            // Shows all detected physical Ethernet/Wi-Fi adapters and
            // active PPP/tunnel adapters. Newly detected adapters enter
            // networkStats during its existing 5-second re-enumeration.
            // ------------------------------------------------

            drawRoundedBox(
                hdc,
                695,
                445,
                995,
                600,
                networkPanel
            );

            drawText(
                hdc,
                "Network Adapters",
                738,
                459,
                textPrimary,
                labelFont
            );

            const int maxAdapterRows = 3;
            int adapterStartIndex = 0;

            if (
                static_cast<int>(
                    networkStats.size()
                ) > maxAdapterRows
            )
            {
                adapterStartIndex =
                    std::clamp(
                        networkIndex - 1,
                        0,
                        static_cast<int>(
                            networkStats.size()
                        ) -
                        maxAdapterRows
                    );
            }

            int adapterRows =
                (std::min)(
                    maxAdapterRows,
                    static_cast<int>(
                        networkStats.size()
                    ) -
                    adapterStartIndex
                );

            for (
                int visibleAdapterIndex = 0;
                visibleAdapterIndex < adapterRows;
                visibleAdapterIndex++
            )
            {
                int adapterIndex =
                    adapterStartIndex +
                    visibleAdapterIndex;

                const NetworkStats& listedAdapter =
                    networkStats[adapterIndex];

                int rowTop =
                    481 +
                    visibleAdapterIndex * 38;

                bool selected =
                    adapterIndex == networkIndex;

                if (selected)
                {
                    drawRoundedBox(
                        hdc,
                        706,
                        rowTop - 4,
                        984,
                        rowTop + 31,
                        uiColor(RGB(24, 48, 64))
                    );

                    HBRUSH accentBrush =
                        CreateSolidBrush(
                            networkBlue
                        );
                    RECT accentRect =
                    {
                        706,
                        rowTop - 1,
                        709,
                        rowTop + 28
                    };
                    FillRect(
                        hdc,
                        &accentRect,
                        accentBrush
                    );
                    DeleteObject(accentBrush);
                }

                if (
                    uiAssetExists(L"sidebar_network.png")
                )
                {
                    drawTintedPngImage(
                        hdc,
                        L"sidebar_network.png",
                        718,
                        rowTop + 3,
                        18,
                        18,
                        selected
                            ? networkBlue
                            : uiColor(RGB(190, 205, 218))
                    );
                }

                drawText(
                    hdc,
                    shortenPerformanceText(
                        listedAdapter.type,
                        10
                    ),
                    746,
                    rowTop,
                    selected
                        ? networkBlue
                        : textPrimary,
                    smallFont
                );

                drawText(
                    hdc,
                    shortenPerformanceText(
                        listedAdapter.description,
                        24
                    ),
                    746,
                    rowTop + 17,
                    textSecondary,
                    smallFont
                );

                HBRUSH statusBrush =
                    CreateSolidBrush(
                        listedAdapter.status ==
                            "Connected"
                        ? networkGreen
                        : uiColor(RGB(95, 115, 132))
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
                        listedAdapter.status ==
                            "Connected"
                        ? networkGreen
                        : uiColor(RGB(95, 115, 132))
                    );
                HGDIOBJ oldStatusPen =
                    SelectObject(
                        hdc,
                        statusPen
                    );
                drawUiEllipse(
                    hdc,
                    965,
                    rowTop + 8,
                    973,
                    rowTop + 16
                );
                SelectObject(
                    hdc,
                    oldStatusPen
                );
                SelectObject(
                    hdc,
                    oldStatusBrush
                );
                DeleteObject(statusPen);
                DeleteObject(statusBrush);
            }

            // ------------------------------------------------
            // HISTORY RANGE DROPDOWN OVERLAY
            // ------------------------------------------------

            if (cpuHistoryRangeDropdownOpen)
            {
                SetViewportOrgEx(
                    hdc,
                    networkPageOriginalOrigin.x,
                    networkPageOriginalOrigin.y +
            scaleUiCoordinate(88),
                    nullptr
                );

                const int menuLeft = 820;
                const int menuTop = 60;
                const int menuRight = 995;
                const int rowHeight = 34;
                const int menuBottom =
                    menuTop +
                    rowHeight * 3;

                drawRoundedBox(
                    hdc,
                    menuLeft,
                    menuTop,
                    menuRight,
                    menuBottom,
                    uiColor(RGB(13, 23, 32))
                );

                const int rangeOptions[3] =
                {
                    15,
                    30,
                    60
                };

                for (
                    int optionIndex = 0;
                    optionIndex < 3;
                    optionIndex++
                )
                {
                    const int option =
                        rangeOptions[
                            optionIndex
                        ];

                    const int rowTop =
                        menuTop +
                        optionIndex *
                        rowHeight;

                    const bool selected =
                        cpuHistoryRangeSeconds ==
                        option;

                    if (selected)
                    {
                        drawRoundedBox(
                            hdc,
                            menuLeft + 4,
                            rowTop + 3,
                            menuRight - 4,
                            rowTop +
                                rowHeight - 3,
                            uiColor(RGB(24, 48, 64))
                        );

                        HBRUSH selectedDotBrush =
                            CreateSolidBrush(
                                networkBlue
                            );
                        HGDIOBJ oldSelectedDotBrush =
                            SelectObject(
                                hdc,
                                selectedDotBrush
                            );
                        HPEN selectedDotPen =
                            CreatePen(
                                PS_SOLID,
                                1,
                                networkBlue
                            );
                        HGDIOBJ oldSelectedDotPen =
                            SelectObject(
                                hdc,
                                selectedDotPen
                            );

                        drawUiEllipse(
                            hdc,
                            835,
                            rowTop + 14,
                            841,
                            rowTop + 20
                        );

                        SelectObject(
                            hdc,
                            oldSelectedDotPen
                        );
                        SelectObject(
                            hdc,
                            oldSelectedDotBrush
                        );
                        DeleteObject(
                            selectedDotPen
                        );
                        DeleteObject(
                            selectedDotBrush
                        );
                    }

                    drawText(
                        hdc,
                        "Last " +
                            std::to_string(option) +
                            " Seconds",
                        850,
                        rowTop + 9,
                        selected
                            ? networkBlue
                            : textPrimary,
                        smallFont
                    );
                }
            }
        }

        SetViewportOrgEx(
            hdc,
            networkPageOriginalOrigin.x,
            networkPageOriginalOrigin.y,
            nullptr
        );
    }
}
else if (currentPage != AppPage::Settings)
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

const int dashboardHeaderLeft = 22;
const int dashboardHeaderTop = 70;
const int dashboardHeaderRight = 865;
const int dashboardHeaderBottom = 110;

// Subtle top strip glow to match the reference look.
{
    ScopedUiGraphics scopedGraphics(hdc);
    Gdiplus::Graphics& graphics =
        scopedGraphics.get();
    graphics.SetSmoothingMode(
        Gdiplus::SmoothingModeAntiAlias
    );

    Gdiplus::Rect headerRect(
        dashboardHeaderLeft - 12,
        dashboardHeaderTop - 18,
        dashboardHeaderRight - dashboardHeaderLeft + 24,
        dashboardHeaderBottom - dashboardHeaderTop + 10
    );

    Gdiplus::GraphicsPath headerPath;
    const int radius = 18;
    headerPath.AddArc(
        headerRect.X,
        headerRect.Y,
        radius,
        radius,
        180,
        90
    );
    headerPath.AddArc(
        headerRect.GetRight() - radius,
        headerRect.Y,
        radius,
        radius,
        270,
        90
    );
    headerPath.AddArc(
        headerRect.GetRight() - radius,
        headerRect.GetBottom() - radius,
        radius,
        radius,
        0,
        90
    );
    headerPath.AddArc(
        headerRect.X,
        headerRect.GetBottom() - radius,
        radius,
        radius,
        90,
        90
    );
    headerPath.CloseFigure();

    Gdiplus::LinearGradientBrush headerBrush(
        Gdiplus::Point(headerRect.X, headerRect.Y),
        Gdiplus::Point(headerRect.GetRight(), headerRect.GetBottom()),
        themeGdiColor(34, 8, 22, 38),
        themeGdiColor(12, 8, 16, 26)
    );

    graphics.FillPath(
        &headerBrush,
        &headerPath
    );
}

drawText(
    hdc,
    "System Overview",
    22,
    66,
    textPrimary,
    titleFont
);

drawText(
    hdc,
    "Live performance. Total control.",
    22,
    98,
    textSecondary,
    smallFont
);

// --------------------------------------------------------
// DASHBOARD STATUS / DATE / TIME
// --------------------------------------------------------

HBRUSH healthBrush =
    CreateSolidBrush(
        uiColor(RGB(70, 220, 140))
    );

HGDIOBJ oldHealthBrush =
    SelectObject(
        hdc,
        healthBrush
    );

HPEN healthPen =
    CreatePen(
        PS_SOLID,
        1,
        uiColor(RGB(70, 220, 140))
    );

HGDIOBJ oldHealthPen =
    SelectObject(
        hdc,
        healthPen
    );

drawUiEllipse(
    hdc,
    588,
    78,
    594,
    84
);

SelectObject(
    hdc,
    oldHealthPen
);

SelectObject(
    hdc,
    oldHealthBrush
);

DeleteObject(
    healthPen
);

DeleteObject(
    healthBrush
);

drawText(
    hdc,
    "System Healthy",
    601,
    75,
    uiColor(RGB(70, 220, 140)),
    smallFont
);

// Live local date and time
SYSTEMTIME localTime = {};
GetLocalTime(
    &localTime
);

static const char* weekDays[] =
{
    "Sun",
    "Mon",
    "Tue",
    "Wed",
    "Thu",
    "Fri",
    "Sat"
};

static const char* months[] =
{
    "",
    "Jan",
    "Feb",
    "Mar",
    "Apr",
    "May",
    "Jun",
    "Jul",
    "Aug",
    "Sep",
    "Oct",
    "Nov",
    "Dec"
};

std::ostringstream dashboardDate;

dashboardDate
    << weekDays[localTime.wDayOfWeek]
    << ", "
    << months[localTime.wMonth]
    << " "
    << localTime.wDay
    << ", "
    << localTime.wYear;

int hour12 =
    localTime.wHour % 12;

if (hour12 == 0)
{
    hour12 = 12;
}

std::ostringstream dashboardClock;

dashboardClock
    << hour12
    << ":"
    << std::setw(2)
    << std::setfill('0')
    << localTime.wMinute
    << " "
    << (
        localTime.wHour >= 12
        ? "PM"
        : "AM"
    );

drawText(
    hdc,
    dashboardDate.str(),
    720,
    75,
    textSecondary,
    smallFont
);

drawText(
    hdc,
    dashboardClock.str(),
    840,
    75,
    textPrimary,
    smallFont
);
// --------------------------------------------------------
// REFERENCE-STYLE DASHBOARD CARDS
// --------------------------------------------------------

const COLORREF dashboardCardColor =
    uiColor(RGB(21, 28, 36));

const COLORREF cpuAccent =
    uiColor(RGB(70, 170, 245));

const COLORREF memoryAccent =
    uiColor(RGB(190, 90, 240));

const COLORREF diskAccent =
    uiColor(RGB(80, 220, 120));

const COLORREF gpuAccent =
    uiColor(RGB(115, 225, 85));

const COLORREF temperatureAccent =
    uiColor(RGB(255, 135, 60));

const COLORREF networkAccent =
    uiColor(RGB(55, 165, 245));

const COLORREF gaugeTrack =
    uiColor(RGB(55, 68, 80));

const int cardWidth = 290;
const int cardHeight = 160;

const int firstColumn = 20;
const int secondColumn = 325;
const int thirdColumn = 630;

const int firstRow = 118;
const int secondRow = 292;

const int gaugeSize = 72;

static const std::vector<double>
    emptyDashboardHistory;


// --------------------------------------------------------
// CPU CARD
// --------------------------------------------------------

drawRoundedBox(
    hdc,
    firstColumn,
    firstRow,
    firstColumn + cardWidth,
    firstRow + cardHeight,
    dashboardCardColor
);

drawDashboardCardIcon(
    hdc,
    firstColumn + 12,
    firstRow + 12,
    "CPU"
);

drawText(
    hdc,
    "CPU",
    firstColumn + 62,
    firstRow + 13,
    textPrimary,
    labelFont
);

drawText(
    hdc,
    shortenDashboardText(
        systemInfo.cpuName,
        29
    ),
    firstColumn + 62,
    firstRow + 35,
    textSecondary,
    smallFont
);

drawCircularGauge(
    hdc,
    firstColumn + 13,
    firstRow + 68,
    gaugeSize,
    cpuUsage,
    cpuAccent,
    gaugeTrack
);

std::ostringstream cpuGaugeText;
cpuGaugeText
    << std::fixed
    << std::setprecision(0)
    << cpuUsage
    << "%";

drawDashboardCenteredText(
    hdc,
    cpuGaugeText.str(),
    firstColumn + 13,
    firstRow + 68,
    gaugeSize,
    gaugeSize,
    textPrimary,
    labelFont
);

drawDashboardHistoryGraph(
    hdc,
    firstColumn + 105,
    firstRow + 72,
    170,
    54,
    cpuHistory,
    100.0,
    cpuAccent
);

drawText(
    hdc,
    shortenDashboardText(
        "Base " +
            systemInfo.cpuBaseSpeed +
            "  |  Now " +
            systemInfo.cpuCurrentSpeed,
        28
    ),
    firstColumn + 105,
    firstRow + 134,
    textSecondary,
    smallFont
);


// --------------------------------------------------------
// MEMORY CARD
// --------------------------------------------------------

drawRoundedBox(
    hdc,
    secondColumn,
    firstRow,
    secondColumn + cardWidth,
    firstRow + cardHeight,
    dashboardCardColor
);

drawDashboardCardIcon(
    hdc,
    secondColumn + 12,
    firstRow + 12,
    "MEMORY"
);

drawText(
    hdc,
    "Memory",
    secondColumn + 62,
    firstRow + 13,
    textPrimary,
    labelFont
);

std::string memorySubtitle =
    systemInfo.installedMemory;

if (
    systemInfo.memoryType != "--" &&
    !systemInfo.memoryType.empty()
)
{
    memorySubtitle +=
        " " +
        systemInfo.memoryType;
}

drawText(
    hdc,
    shortenDashboardText(
        memorySubtitle,
        29
    ),
    secondColumn + 62,
    firstRow + 35,
    textSecondary,
    smallFont
);

drawCircularGauge(
    hdc,
    secondColumn + 13,
    firstRow + 68,
    gaugeSize,
    ramPercent,
    memoryAccent,
    gaugeTrack
);

std::ostringstream ramGaugeText;
ramGaugeText
    << ramPercent
    << "%";

drawDashboardCenteredText(
    hdc,
    ramGaugeText.str(),
    secondColumn + 13,
    firstRow + 68,
    gaugeSize,
    gaugeSize,
    textPrimary,
    labelFont
);

drawDashboardHistoryGraph(
    hdc,
    secondColumn + 105,
    firstRow + 72,
    170,
    54,
    ramHistory,
    100.0,
    memoryAccent
);

std::ostringstream dashboardRamInfo;
dashboardRamInfo
    << std::fixed
    << std::setprecision(1)
    << usedRamGB
    << " GB / "
    << totalRamGB
    << " GB";

drawText(
    hdc,
    shortenDashboardText(
        dashboardRamInfo.str(),
        28
    ),
    secondColumn + 105,
    firstRow + 134,
    textSecondary,
    smallFont
);


// --------------------------------------------------------
// DISK CARD
// --------------------------------------------------------

int dashboardDiskIndex = -1;

for (int i = 0;
     i < static_cast<int>(
         diskStats.size()
     );
     i++)
{
    if (diskStats[i].systemDisk)
    {
        dashboardDiskIndex = i;
        break;
    }
}

if (
    dashboardDiskIndex < 0 &&
    !diskStats.empty()
)
{
    dashboardDiskIndex = 0;
}

const DiskStats* dashboardDisk =
    dashboardDiskIndex >= 0
    ? &diskStats[
        dashboardDiskIndex
      ]
    : nullptr;

double dashboardDiskPercent =
    dashboardDisk != nullptr &&
    dashboardDisk->performanceValid
    ? dashboardDisk->activeTimePercent
    : static_cast<double>(
        diskPercent
      );

const std::vector<double>&
    dashboardDiskHistory =
        dashboardDisk != nullptr
        ? dashboardDisk->activeHistory
        : emptyDashboardHistory;

drawRoundedBox(
    hdc,
    thirdColumn,
    firstRow,
    thirdColumn + cardWidth,
    firstRow + cardHeight,
    dashboardCardColor
);

drawDashboardCardIcon(
    hdc,
    thirdColumn + 12,
    firstRow + 12,
    "DISK"
);

drawText(
    hdc,
    "Disk",
    thirdColumn + 62,
    firstRow + 13,
    textPrimary,
    labelFont
);

std::string diskSubtitle =
    "System disk";

if (dashboardDisk != nullptr)
{
    diskSubtitle =
        dashboardDisk->model;

    if (
        dashboardDisk->capacityGB >
        0.0
    )
    {
        diskSubtitle +=
            " " +
            formatDiskCapacity(
                dashboardDisk->capacityGB
            );
    }
}

drawText(
    hdc,
    shortenDashboardText(
        diskSubtitle,
        29
    ),
    thirdColumn + 62,
    firstRow + 35,
    textSecondary,
    smallFont
);

drawCircularGauge(
    hdc,
    thirdColumn + 13,
    firstRow + 68,
    gaugeSize,
    dashboardDiskPercent,
    diskAccent,
    gaugeTrack
);

std::ostringstream diskGaugeText;
diskGaugeText
    << std::fixed
    << std::setprecision(0)
    << dashboardDiskPercent
    << "%";

drawDashboardCenteredText(
    hdc,
    diskGaugeText.str(),
    thirdColumn + 13,
    firstRow + 68,
    gaugeSize,
    gaugeSize,
    textPrimary,
    labelFont
);

drawDashboardHistoryGraph(
    hdc,
    thirdColumn + 105,
    firstRow + 72,
    170,
    54,
    dashboardDiskHistory,
    100.0,
    diskAccent
);

std::string diskFooter =
    "Usage data unavailable";

if (dashboardDisk != nullptr)
{
    diskFooter =
        "R " +
        formatDiskSpeed(
            dashboardDisk->readMBps
        ) +
        "  |  W " +
        formatDiskSpeed(
            dashboardDisk->writeMBps
        );
}

drawText(
    hdc,
    shortenDashboardText(
        diskFooter,
        28
    ),
    thirdColumn + 105,
    firstRow + 134,
    textSecondary,
    smallFont
);


// --------------------------------------------------------
// GPU CARD
// --------------------------------------------------------

int dashboardGpuIndex = -1;

if (!gpuStats.empty())
{
    dashboardGpuIndex = 0;

    for (int i = 1;
         i < static_cast<int>(
             gpuStats.size()
         );
         i++)
    {
        if (
            gpuStats[i].
                utilizationPercent >
            gpuStats[
                dashboardGpuIndex
            ].
                utilizationPercent
        )
        {
            dashboardGpuIndex = i;
        }
    }
}

const GpuStats* dashboardGpu =
    dashboardGpuIndex >= 0
    ? &gpuStats[
        dashboardGpuIndex
      ]
    : nullptr;

const double dashboardGpuPercent =
    dashboardGpu != nullptr
    ? dashboardGpu->
        utilizationPercent
    : 0.0;

drawRoundedBox(
    hdc,
    firstColumn,
    secondRow,
    firstColumn + cardWidth,
    secondRow + cardHeight,
    dashboardCardColor
);

drawDashboardCardIcon(
    hdc,
    firstColumn + 12,
    secondRow + 12,
    "GPU"
);

drawText(
    hdc,
    "GPU",
    firstColumn + 62,
    secondRow + 13,
    textPrimary,
    labelFont
);

drawText(
    hdc,
    shortenDashboardText(
        dashboardGpu != nullptr
        ? dashboardGpu->name
        : "No GPU detected",
        29
    ),
    firstColumn + 62,
    secondRow + 35,
    textSecondary,
    smallFont
);

drawCircularGauge(
    hdc,
    firstColumn + 13,
    secondRow + 68,
    gaugeSize,
    dashboardGpuPercent,
    gpuAccent,
    gaugeTrack
);

std::ostringstream gpuGaugeText;
gpuGaugeText
    << std::fixed
    << std::setprecision(0)
    << dashboardGpuPercent
    << "%";

drawDashboardCenteredText(
    hdc,
    gpuGaugeText.str(),
    firstColumn + 13,
    secondRow + 68,
    gaugeSize,
    gaugeSize,
    textPrimary,
    labelFont
);

drawDashboardHistoryGraph(
    hdc,
    firstColumn + 105,
    secondRow + 72,
    170,
    54,
    dashboardGpu != nullptr
        ? dashboardGpu->
            utilizationHistory
        : emptyDashboardHistory,
    100.0,
    gpuAccent
);

std::string gpuFooter =
    "GPU data unavailable";

if (dashboardGpu != nullptr)
{
    std::ostringstream gpuFooterStream;

    if (
        dashboardGpu->
            dedicatedMemoryTotalBytes >
        0
    )
    {
        gpuFooterStream
            << formatMemoryBytes(
                dashboardGpu->
                    dedicatedMemoryUsedBytes
            )
            << " / "
            << formatMemoryBytes(
                dashboardGpu->
                    dedicatedMemoryTotalBytes
            );
    }

    if (
        dashboardGpu->
            temperatureC >= 0.0
    )
    {
        if (
            dashboardGpu->
                dedicatedMemoryTotalBytes >
            0
        )
        {
            gpuFooterStream
                << "  |  ";
        }

        gpuFooterStream
            << std::fixed
            << std::setprecision(0)
            << SettingsRuntime::temperature(dashboardGpu->
                temperatureC);
    }

    gpuFooter =
        gpuFooterStream.str();

    if (gpuFooter.empty())
        gpuFooter = "GPU data active";
}

drawText(
    hdc,
    shortenDashboardText(
        gpuFooter,
        28
    ),
    firstColumn + 105,
    secondRow + 134,
    textSecondary,
    smallFont
);


// --------------------------------------------------------
// TEMPERATURE CARD
// --------------------------------------------------------

double dashboardTemperature =
    -1.0;

std::string dashboardTemperatureSource =
    "No supported sensor";

const std::vector<double>*
    dashboardTemperatureHistory =
        &emptyDashboardHistory;

if (
    temperatureStats.cpuTemperatureC >=
    0.0
)
{
    dashboardTemperature =
        temperatureStats.cpuTemperatureC;

    dashboardTemperatureSource =
        temperatureStats.cpuSensorName;

    dashboardTemperatureHistory =
        &temperatureStats.
            cpuTemperatureHistory;
}

if (
    temperatureStats.
        motherboardTemperatureC >
    dashboardTemperature
)
{
    dashboardTemperature =
        temperatureStats.
            motherboardTemperatureC;

    dashboardTemperatureSource =
        temperatureStats.
            motherboardSensorName;

    dashboardTemperatureHistory =
        &temperatureStats.
            motherboardTemperatureHistory;
}

if (
    temperatureStats.systemTemperatureC >
    dashboardTemperature
)
{
    dashboardTemperature =
        temperatureStats.
            systemTemperatureC;

    dashboardTemperatureSource =
        "System sensor";

    dashboardTemperatureHistory =
        &emptyDashboardHistory;
}

double dashboardHottestGpuTemperature =
    -1.0;

for (int i = 0;
     i < static_cast<int>(
         gpuStats.size()
     );
     i++)
{
    if (
        gpuStats[i].temperatureC >
        dashboardHottestGpuTemperature
    )
    {
        dashboardHottestGpuTemperature =
            gpuStats[i].temperatureC;
    }

    if (
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

        dashboardTemperatureHistory =
            &gpuStats[i].
                temperatureHistory;
    }
}

drawRoundedBox(
    hdc,
    secondColumn,
    secondRow,
    secondColumn + cardWidth,
    secondRow + cardHeight,
    dashboardCardColor
);

drawDashboardCardIcon(
    hdc,
    secondColumn + 12,
    secondRow + 12,
    "TEMP"
);

drawText(
    hdc,
    "Temperature",
    secondColumn + 62,
    secondRow + 13,
    textPrimary,
    labelFont
);

drawText(
    hdc,
    shortenDashboardText(
        dashboardTemperatureSource,
        29
    ),
    secondColumn + 62,
    secondRow + 35,
    textSecondary,
    smallFont
);

drawCircularGauge(
    hdc,
    secondColumn + 13,
    secondRow + 68,
    gaugeSize,
    dashboardTemperature >= 0.0
        ? dashboardTemperature
        : 0.0,
    temperatureAccent,
    gaugeTrack
);

std::ostringstream temperatureGaugeText;

if (dashboardTemperature >= 0.0)
{
    temperatureGaugeText
        << std::fixed
        << std::setprecision(0)
        << SettingsRuntime::temperature(dashboardTemperature);
}
else
{
    temperatureGaugeText
        << "--";
}

drawDashboardCenteredText(
    hdc,
    temperatureGaugeText.str(),
    secondColumn + 13,
    secondRow + 68,
    gaugeSize,
    gaugeSize,
    textPrimary,
    labelFont
);

drawDashboardHistoryGraph(
    hdc,
    secondColumn + 105,
    secondRow + 72,
    170,
    54,
    *dashboardTemperatureHistory,
    100.0,
    temperatureAccent
);

std::ostringstream temperatureFooter;

if (
    temperatureStats.cpuTemperatureC >=
    0.0
)
{
    temperatureFooter
        << "CPU "
        << std::fixed
        << std::setprecision(0)
        << SettingsRuntime::temperature(temperatureStats.
            cpuTemperatureC);
}

if (
    dashboardHottestGpuTemperature >=
    0.0
)
{
    if (!temperatureFooter.str().empty())
        temperatureFooter << " | ";

    temperatureFooter
        << "GPU "
        << std::fixed
        << std::setprecision(0)
        << SettingsRuntime::temperature(dashboardHottestGpuTemperature);
}

if (
    temperatureStats.
        motherboardTemperatureC >=
    0.0
)
{
    if (!temperatureFooter.str().empty())
        temperatureFooter << " | ";

    temperatureFooter
        << "MB "
        << std::fixed
        << std::setprecision(0)
        << SettingsRuntime::temperature(temperatureStats.
            motherboardTemperatureC);
}

drawText(
    hdc,
    shortenDashboardText(
        temperatureFooter.str().empty()
        ? "Sensor data unavailable"
        : temperatureFooter.str(),
        28
    ),
    secondColumn + 105,
    secondRow + 134,
    textSecondary,
    smallFont
);


// --------------------------------------------------------
// NETWORK CARD
// --------------------------------------------------------

const NetworkStats* dashboardNetwork =
    !networkStats.empty()
    ? &networkStats.front()
    : nullptr;

drawRoundedBox(
    hdc,
    thirdColumn,
    secondRow,
    thirdColumn + cardWidth,
    secondRow + cardHeight,
    dashboardCardColor
);

drawDashboardCardIcon(
    hdc,
    thirdColumn + 12,
    secondRow + 12,
    "NETWORK"
);

drawText(
    hdc,
    "Network",
    thirdColumn + 62,
    secondRow + 13,
    textPrimary,
    labelFont
);

std::string networkSubtitle =
    "No active adapter";

if (dashboardNetwork != nullptr)
{
    networkSubtitle =
        dashboardNetwork->description != "--"
        ? dashboardNetwork->description
        : dashboardNetwork->name;
}

drawText(
    hdc,
    shortenDashboardText(
        networkSubtitle,
        29
    ),
    thirdColumn + 62,
    secondRow + 35,
    textSecondary,
    smallFont
);

if (dashboardNetwork != nullptr)
{
    drawText(
        hdc,
        "Down " +
            formatNetworkSpeed(
                dashboardNetwork->
                    downloadMbps
            ),
        thirdColumn + 14,
        secondRow + 78,
        textPrimary,
        smallFont
    );

    drawText(
        hdc,
        "Up   " +
            formatNetworkSpeed(
                dashboardNetwork->
                    uploadMbps
            ),
        thirdColumn + 14,
        secondRow + 102,
        textPrimary,
        smallFont
    );

    drawDashboardHistoryGraph(
        hdc,
        thirdColumn + 118,
        secondRow + 72,
        157,
        54,
        dashboardNetwork->
            downloadHistory,
        getNetworkGraphScale(
            *dashboardNetwork
        ),
        networkAccent
    );

    std::ostringstream networkFooter;

    if (
        dashboardNetwork->
            linkSpeedMbps >
        0.0
    )
    {
        networkFooter
            << "Link "
            << formatNetworkSpeed(
                dashboardNetwork->
                    linkSpeedMbps
            );
    }
    else
    {
        networkFooter
            << dashboardNetwork->
                status;
    }

    drawText(
        hdc,
        shortenDashboardText(
            networkFooter.str(),
            24
        ),
        thirdColumn + 118,
        secondRow + 134,
        textSecondary,
        smallFont
    );
}
else
{
    drawDashboardHistoryGraph(
        hdc,
        thirdColumn + 118,
        secondRow + 72,
        157,
        54,
        emptyDashboardHistory,
        100.0,
        networkAccent
    );

    drawText(
        hdc,
        "--",
        thirdColumn + 14,
        secondRow + 88,
        textPrimary,
        labelFont
    );
}


// --------------------------------------------------------
// RUNNING PROCESSES / SYSTEM INFORMATION
// --------------------------------------------------------

const int dashboardLowerTop = 470;
const int dashboardLowerBottom = 665;

const int processPanelLeft = 20;
const int processPanelRight = 465;

const int infoPanelLeft = 480;
const int infoPanelRight = 920;


// --------------------------------------------------------
// RUNNING PROCESSES
// --------------------------------------------------------

drawRoundedBox(
    hdc,
    processPanelLeft,
    dashboardLowerTop,
    processPanelRight,
    dashboardLowerBottom,
    dashboardCardColor
);

drawText(
    hdc,
    "Running Processes",
    processPanelLeft + 34,
    dashboardLowerTop + 14,
    textPrimary,
    labelFont
);

// Small header icon. Re-use the navigation process PNG when present.
drawTintedPngImage(
    hdc,
    L"processes.png",
    processPanelLeft + 12,
    dashboardLowerTop + 14,
    16,
    16,
    uiColor(RGB(205, 215, 225))
);

// View All navigation control.
drawText(
    hdc,
    "View All  >",
    processPanelRight - 78,
    dashboardLowerTop + 16,
    uiColor(RGB(82, 190, 240)),
    smallFont
);

// Header separator.
HPEN lowerPanelLinePen =
    CreatePen(
        PS_SOLID,
        1,
        uiColor(RGB(36, 46, 56))
    );

HGDIOBJ oldLowerPanelLinePen =
    SelectObject(
        hdc,
        lowerPanelLinePen
    );

MoveToEx(
    hdc,
    processPanelLeft + 12,
    dashboardLowerTop + 42,
    nullptr
);

LineTo(
    hdc,
    processPanelRight - 12,
    dashboardLowerTop + 42
);

SelectObject(
    hdc,
    oldLowerPanelLinePen
);

DeleteObject(lowerPanelLinePen);

// Column labels.
drawText(
    hdc,
    "Name",
    processPanelLeft + 14,
    dashboardLowerTop + 49,
    textSecondary,
    smallFont
);

drawText(
    hdc,
    "CPU",
    processPanelLeft + 240,
    dashboardLowerTop + 49,
    textSecondary,
    smallFont
);

drawText(
    hdc,
    "Memory",
    processPanelLeft + 298,
    dashboardLowerTop + 49,
    textSecondary,
    smallFont
);

drawText(
    hdc,
    "Status",
    processPanelLeft + 374,
    dashboardLowerTop + 49,
    textSecondary,
    smallFont
);

std::vector<ProcessInfo> dashboardProcesses =
    getCachedRunningProcesses();

// Do not treat the synthetic System Idle Process as an app row.
dashboardProcesses.erase(
    std::remove_if(
        dashboardProcesses.begin(),
        dashboardProcesses.end(),
        [](const ProcessInfo& process)
        {
            return process.pid == 0;
        }
    ),
    dashboardProcesses.end()
);

// "Top 5" = the five processes with the highest current CPU use.
// Processes with unavailable CPU values stay below measured values.
std::stable_sort(
    dashboardProcesses.begin(),
    dashboardProcesses.end(),
    [](const ProcessInfo& a,
       const ProcessInfo& b)
    {
        const double aCpu =
            a.cpuPercent >= 0.0
            ? a.cpuPercent
            : -1.0;

        const double bCpu =
            b.cpuPercent >= 0.0
            ? b.cpuPercent
            : -1.0;

        if (aCpu != bCpu)
        {
            return aCpu > bCpu;
        }

        return a.memoryMB > b.memoryMB;
    }
);

const int dashboardProcessCount =
    (std::min)(
        5,
        static_cast<int>(
            dashboardProcesses.size()
        )
    );

const int dashboardProcessRowStart =
    dashboardLowerTop + 72;

const int dashboardProcessRowGap = 23;

for (int index = 0;
     index < dashboardProcessCount;
     index++)
{
    const ProcessInfo& process =
        dashboardProcesses[index];

    const int rowY =
        dashboardProcessRowStart +
        index * dashboardProcessRowGap;

    // Actual icon from the running executable.
    drawProcessExecutableIcon(
        hdc,
        process.pid,
        processPanelLeft + 14,
        rowY - 2,
        17
    );

    drawText(
        hdc,
        shortenDashboardText(
            process.name,
            24
        ),
        processPanelLeft + 38,
        rowY,
        textPrimary,
        smallFont
    );

    std::string processCpuText = "--";

    if (process.cpuPercent >= 0.0)
    {
        std::ostringstream cpuTextStream;
        cpuTextStream
            << std::fixed
            << std::setprecision(1)
            << process.cpuPercent
            << "%";

        processCpuText =
            cpuTextStream.str();
    }

    drawText(
        hdc,
        processCpuText,
        processPanelLeft + 240,
        rowY,
        textSecondary,
        smallFont
    );

    std::string processMemoryText = "--";

    if (process.pid == 0)
    {
        processMemoryText = "0 MB";
    }
    else if (process.memoryMB >= 0.0)
    {
        std::ostringstream memoryTextStream;
        memoryTextStream
            << std::fixed
            << std::setprecision(0)
            << process.memoryMB
            << " MB";

        processMemoryText =
            memoryTextStream.str();
    }

    drawText(
        hdc,
        processMemoryText,
        processPanelLeft + 298,
        rowY,
        textSecondary,
        smallFont
    );

    // Green running indicator.
    HBRUSH processStatusBrush =
        CreateSolidBrush(
            uiColor(RGB(68, 225, 126))
        );

    HGDIOBJ oldProcessStatusBrush =
        SelectObject(
            hdc,
            processStatusBrush
        );

    HGDIOBJ oldProcessStatusPen =
        SelectObject(
            hdc,
            GetStockObject(NULL_PEN)
        );

    drawUiEllipse(
        hdc,
        processPanelLeft + 374,
        rowY + 4,
        processPanelLeft + 381,
        rowY + 11
    );

    SelectObject(
        hdc,
        oldProcessStatusPen
    );

    SelectObject(
        hdc,
        oldProcessStatusBrush
    );

    DeleteObject(processStatusBrush);

    drawText(
        hdc,
        "Running",
        processPanelLeft + 387,
        rowY,
        uiColor(RGB(68, 225, 126)),
        smallFont
    );
}

if (dashboardProcessCount == 0)
{
    drawText(
        hdc,
        "No running processes are available.",
        processPanelLeft + 14,
        dashboardLowerTop + 90,
        textSecondary,
        smallFont
    );
}


// --------------------------------------------------------
// SYSTEM INFORMATION
// --------------------------------------------------------

drawRoundedBox(
    hdc,
    infoPanelLeft,
    dashboardLowerTop,
    infoPanelRight,
    dashboardLowerBottom,
    dashboardCardColor
);

drawTintedPngImage(
    hdc,
    L"sysinfo_os.png",
    infoPanelLeft + 14,
    dashboardLowerTop + 14,
    17,
    17,
    uiColor(RGB(205, 215, 225))
);

drawText(
    hdc,
    "System Information",
    infoPanelLeft + 39,
    dashboardLowerTop + 14,
    textPrimary,
    labelFont
);

HPEN infoHeaderPen =
    CreatePen(
        PS_SOLID,
        1,
        uiColor(RGB(36, 46, 56))
    );

HGDIOBJ oldInfoHeaderPen =
    SelectObject(
        hdc,
        infoHeaderPen
    );

MoveToEx(
    hdc,
    infoPanelLeft + 12,
    dashboardLowerTop + 42,
    nullptr
);

LineTo(
    hdc,
    infoPanelRight - 12,
    dashboardLowerTop + 42
);

SelectObject(
    hdc,
    oldInfoHeaderPen
);

DeleteObject(infoHeaderPen);

std::string dashboardStorageText = "--";

if (dashboardDisk != nullptr)
{
    dashboardStorageText =
        dashboardDisk->model;

    if (dashboardDisk->capacityGB > 0.0)
    {
        if (!dashboardStorageText.empty() &&
            dashboardStorageText != "--")
        {
            dashboardStorageText += "  ";
        }

        dashboardStorageText +=
            formatDiskCapacity(
                dashboardDisk->capacityGB
            );
    }
}

std::string dashboardGpuInfoText =
    dashboardGpu != nullptr
    ? dashboardGpu->name
    : systemInfo.gpuName;

std::string dashboardMemoryInfoText =
    systemInfo.installedMemory;

if (
    systemInfo.memoryType != "--" &&
    !systemInfo.memoryType.empty()
)
{
    dashboardMemoryInfoText +=
        " " +
        systemInfo.memoryType;
}

ULONGLONG dashboardUptimeDays =
    uptimeSeconds / 86400;

ULONGLONG dashboardUptimeHours =
    (uptimeSeconds % 86400) / 3600;

ULONGLONG dashboardUptimeMinutes =
    (uptimeSeconds % 3600) / 60;

std::ostringstream compactUptime;
compactUptime
    << dashboardUptimeDays
    << "d "
    << dashboardUptimeHours
    << "h "
    << dashboardUptimeMinutes
    << "m";

struct DashboardSystemInfoRow
{
    const wchar_t* iconPath;
    const char* label;
    std::string value;
};

const DashboardSystemInfoRow dashboardInfoRows[] =
{
    {
        L"sysinfo_os.png",
        "OS",
        systemInfo.osName
    },
    {
        L"sysinfo_cpu.png",
        "CPU",
        systemInfo.cpuName
    },
    {
        L"sysinfo_gpu.png",
        "GPU",
        dashboardGpuInfoText
    },
    {
        L"sysinfo_memory.png",
        "Memory",
        dashboardMemoryInfoText
    },
    {
        L"sysinfo_storage.png",
        "Storage",
        dashboardStorageText
    },
    {
        L"sysinfo_uptime.png",
        "Uptime",
        compactUptime.str()
    }
};

const int systemInfoRowStart =
    dashboardLowerTop + 53;

const int systemInfoRowGap = 22;

for (int index = 0;
     index < 6;
     index++)
{
    const int rowY =
        systemInfoRowStart +
        index * systemInfoRowGap;

    bool drewInfoIcon =
        drawTintedPngImage(
            hdc,
            dashboardInfoRows[index].iconPath,
            infoPanelLeft + 16,
            rowY,
            14,
            14,
            uiColor(RGB(170, 183, 196))
        );

    // Simple neutral fallback when a mini PNG has not been added yet.
    if (!drewInfoIcon)
    {
        HPEN fallbackPen =
            CreatePen(
                PS_SOLID,
                1,
                uiColor(RGB(130, 145, 160))
            );

        HGDIOBJ oldFallbackPen =
            SelectObject(
                hdc,
                fallbackPen
            );

        HGDIOBJ oldFallbackBrush =
            SelectObject(
                hdc,
                GetStockObject(NULL_BRUSH)
            );

        Rectangle(
            hdc,
            infoPanelLeft + 17,
            rowY + 1,
            infoPanelLeft + 29,
            rowY + 13
        );

        SelectObject(
            hdc,
            oldFallbackBrush
        );

        SelectObject(
            hdc,
            oldFallbackPen
        );

        DeleteObject(fallbackPen);
    }

    drawText(
        hdc,
        dashboardInfoRows[index].label,
        infoPanelLeft + 42,
        rowY,
        textSecondary,
        smallFont
    );

    drawText(
        hdc,
        shortenDashboardText(
            dashboardInfoRows[index].value.empty()
                ? "--"
                : dashboardInfoRows[index].value,
            35
        ),
        infoPanelLeft + 116,
        rowY,
        textPrimary,
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

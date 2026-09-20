#pragma once
#include "Settings.h"
#include <vector>
#include <windows.h>
#include <string>

// SysMon's UI is authored against this logical client size.
// Main.cpp scales this logical canvas to the real window size so all
// existing pages, graphs, icons and hitboxes resize together.
constexpr int sysMonDesignWidth = 1190;
constexpr int sysMonDesignHeight = 720;



enum class AppPage
{
    Dashboard,
    Processes,
    Performance,
    Temperatures,
    Tools,
    SystemInfo,
    Settings
};

extern std::vector<DWORD> visibleProcessPids;
extern AppPage currentPage;
extern int processScrollOffset;
enum class ProcessFilterMode
{
    All,
    Apps,
    Background,
    Windows
};

enum class ProcessSort
{
    Name,
    CPU,
    Memory,
    Threads,
    PID
};
enum class PerformanceView
{
    Overview,
    CPU,
    Memory,
    Disk,
    GPU,
    Network
};

enum class TemperatureView
{
    CPU,
    GPU,
    Motherboard
};

extern PerformanceView performanceView;

// Shared CPU/Memory history range dropdown.
// CPU and RAM statistics are sampled every 500 ms and the history buffers
// store up to 120 samples, so the supported real windows are 15/30/60 seconds.
extern int cpuHistoryRangeSeconds;
extern bool cpuHistoryRangeDropdownOpen;
extern int selectedDiskIndex;
extern int selectedGpuIndex;
extern int selectedNetworkIndex;
extern TemperatureView temperatureView;
extern int selectedTemperatureGpuIndex;
extern int systemInfoScrollOffset;
extern int systemInfoMaxScrollOffset;
extern int settingsScrollOffset;
extern int settingsMaxScrollOffset;
extern int systemInfoSelectedDiskIndex;
extern int systemInfoSelectedGpuIndex;
extern std::string selectedConnectedDeviceKey;

// Shared Performance-page resource-card layout.
// Main.cpp uses the same helpers for mouse hit-testing.
constexpr int performanceDiskCardStartY = 320;
constexpr int performanceDiskCardHeight = 70;
constexpr int performanceDiskCardGap = 15;
constexpr int performanceMaxVisibleDiskCards = 3;

inline int performanceVisibleDiskCardCount(
    size_t diskCount)
{
    int count =
        static_cast<int>(diskCount);

    if (count < 1)
        count = 1;

    if (count > performanceMaxVisibleDiskCards)
        count = performanceMaxVisibleDiskCards;

    return count;
}

inline int performanceDiskCardTop(
    int index)
{
    return
        performanceDiskCardStartY +
        index *
        (performanceDiskCardHeight +
         performanceDiskCardGap);
}

inline int performanceVisibleGpuCardCount(
    size_t gpuCount)
{
    int count =
        static_cast<int>(gpuCount);

    if (count < 1)
        count = 1;

    return count;
}

inline int performanceGpuCardTop(
    size_t diskCount,
    int gpuIndex = 0)
{
    return
        performanceDiskCardStartY +
        (
            performanceVisibleDiskCardCount(
                diskCount
            ) +
            gpuIndex
        ) *
        (performanceDiskCardHeight +
         performanceDiskCardGap);
}

inline int performanceNetworkCardTop(
    size_t diskCount,
    size_t gpuCount)
{
    return
        performanceGpuCardTop(
            diskCount,
            performanceVisibleGpuCardCount(
                gpuCount
            )
        );
}
extern ProcessSort processSort;
extern bool processSortDescending;

extern std::string processSearch;
extern bool processSearchFocused;
extern ProcessFilterMode processFilterMode;
extern bool processFilterDropdownOpen;
extern DWORD selectedProcessPid;
extern DWORD hoveredProcessPid;
extern std::vector<DWORD> visibleProcessGroupKeys;
extern std::vector<DWORD> expandedProcessGroupKeys;
extern int processMaxScrollOffset;
extern int processScrollbarThumbTop;
extern int processScrollbarThumbBottom;

extern bool processScrollbarDragging;
extern int processScrollbarDragOffsetY;
extern bool processScrollbarVisible;

void setFont(
    HDC hdc,
    HFONT font
);

void drawText(
    HDC hdc,
    const std::string& text,
    int x,
    int y,
    COLORREF color,
    HFONT font
);

void drawRoundedBox(
    HDC hdc,
    int left,
    int top,
    int right,
    int bottom,
    COLORREF color
);

void drawProgressBar(
    HDC hdc,
    int x,
    int y,
    int width,
    int height,
    double percent
);

void clearUiImageCache();
void preloadUiAssets();

// Supplies the physical render transform used by the responsive high-DPI
// renderer. Existing UI code continues to use the 1190x720 logical canvas.
void setUiRenderTransform(
    float scale,
    int offsetX,
    int offsetY,
    float scaleX = 0.0f
);

// Fast Processes-page repaint used while scrolling/dragging the process list.
// The retained frame already contains the rest of the page, so this mode only
// redraws the table and its scrollbar.
void setProcessFastScrollRender(bool enabled);

// Lightweight first frame used during tab/page navigation. Expensive optional
// UI lookups (such as uncached process icons) are deferred to the sharp frame.
void setUiFastNavigationRender(bool enabled);

// --------------------------------------------------------
// SETTINGS PAGE LAYOUT
// --------------------------------------------------------
// UI.cpp paints the Settings page from these values and Main.cpp hit-tests
// against the exact same geometry, so the drawing and the mouse handling can
// no longer drift apart. Everything is expressed in the 1190x720 logical
// canvas, relative to the content origin (x = 165) that both files already
// apply, and in unscrolled page coordinates.
namespace SettingsLayout
{
    struct Rect
    {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
    };

    // The top navigation strip owns y = 10..48. The page header starts below
    // it so the title and the Reset button stop colliding with the tabs and
    // with the window caption buttons.
    constexpr int headerTop = 58;
    constexpr int headerIconSize = 52;
    constexpr int headerTitleX = 84;

    constexpr int contentLeft = 18;
    constexpr int contentRight = 1006;
    constexpr int columnWidth = 320;
    constexpr int columnGap = 14;

    // Scrolling viewport for the settings cards.
    constexpr int clipTop = 112;
    constexpr int clipBottom = 712;

    constexpr int panelPadding = 16;
    constexpr int panelHeaderY = 16;
    constexpr int panelDividerY = 48;
    constexpr int panelFirstRowY = 66;
    constexpr int panelBottomPadding = 8;
    inline int rowPitch = 50;

    constexpr int controlHeight = 32;
    constexpr int controlOffsetY = 1;
    constexpr int comboWidth = 118;
    constexpr int valueWidth = 56;
    constexpr int valueSuffixGap = 32;
    constexpr int switchWidth = 38;
    constexpr int switchHeight = 20;
    constexpr int switchOffsetY = 6;

    // Card tops. Column 0 carries the tallest middle card, so the bottom row
    // is staggered per column the way the reference layout is.
    constexpr int topRowTop = 126;
    inline int middleRowTop = 468;
    inline int aboutPanelTop = 860;
    inline int backupPanelTop = 760;
    inline int supportPanelTop = 760;

    constexpr int aboutPanelHeight = 160;
    inline int contentBottom = 1036;

    // Fixed header button (does not scroll with the cards).
    constexpr int resetButtonWidth = 158;
    constexpr int resetButtonTop = 64;
    constexpr int resetButtonHeight = 34;

    constexpr int scrollbarLeft = 1011;
    constexpr int scrollbarRight = 1019;
    constexpr int scrollbarMinThumb = 70;

    inline void refreshDensity() {
        rowPitch = appSettings.compactMode ? 44 : 50;
        middleRowTop = topRowTop + panelFirstRowY + 5*rowPitch + panelBottomPadding + 18;
        aboutPanelTop = middleRowTop + panelFirstRowY + 6*rowPitch + panelBottomPadding + 18;
        backupPanelTop = supportPanelTop = middleRowTop + panelFirstRowY + 4*rowPitch + panelBottomPadding + 18;
        contentBottom = aboutPanelTop + aboutPanelHeight + 16;
    }

    inline int columnLeft(int column)
    {
        return contentLeft + column * (columnWidth + columnGap);
    }

    inline int columnRight(int column)
    {
        return columnLeft(column) + columnWidth;
    }

    inline int rowLeft(int column)
    {
        return columnLeft(column) + panelPadding;
    }

    inline int rowRight(int column)
    {
        return columnRight(column) - panelPadding;
    }

    inline int rowWidth()
    {
        return columnWidth - 2 * panelPadding;
    }

    inline int panelHeight(int rowCount)
    {
        return panelFirstRowY + rowCount * rowPitch + panelBottomPadding;
    }

    inline int rowTop(int panelTop, int index)
    {
        return panelTop + panelFirstRowY + index * rowPitch;
    }

    inline int maxScrollOffset()
    {
        const int range = contentBottom - clipBottom;
        return range > 0 ? range : 0;
    }

    // Whole-row hit band, used by rows that toggle when clicked anywhere.
    inline Rect rowBand(int column, int panelTop, int index)
    {
        const int top = rowTop(panelTop, index);
        return { rowLeft(column), top - 8, rowRight(column), top + rowPitch - 14 };
    }

    inline Rect switchRect(int column, int panelTop, int index)
    {
        const int top = rowTop(panelTop, index) + switchOffsetY;
        const int right = rowRight(column);
        return { right - switchWidth, top, right, top + switchHeight };
    }

    inline Rect comboRect(int column, int panelTop, int index)
    {
        const int top = rowTop(panelTop, index) + controlOffsetY;
        const int right = rowRight(column);
        return { right - comboWidth, top, right, top + controlHeight };
    }

    inline Rect valueRect(int column, int panelTop, int index)
    {
        const int top = rowTop(panelTop, index) + controlOffsetY;
        const int right = rowRight(column) - valueSuffixGap;
        return { right - valueWidth, top, right, top + controlHeight };
    }

    inline Rect buttonRect(int column, int panelTop, int index, int width)
    {
        const int top = rowTop(panelTop, index) + controlOffsetY;
        const int right = rowRight(column);
        return { right - width, top, right, top + controlHeight };
    }

    inline Rect resetButton()
    {
        return {
            contentRight - resetButtonWidth,
            resetButtonTop,
            contentRight,
            resetButtonTop + resetButtonHeight
        };
    }

    // Appearance card: accent swatches (row 0) and transparency slider (row 1).
    constexpr int swatchCount = 6;
    constexpr int swatchSpacing = 26;
    constexpr int swatchRadius = 8;
    constexpr int swatchHitRadius = 12;

    inline int swatchCenterX(int column, int index)
    {
        return rowRight(column) - 11 -
               (swatchCount - 1 - index) * swatchSpacing;
    }

    inline int swatchCenterY(int panelTop)
    {
        return rowTop(panelTop, 0) + 15;
    }

    constexpr int sliderWidth = 116;
    constexpr int sliderPercentGap = 50;
    constexpr int sliderRowIndex = 1;

    inline int sliderLeft(int column)
    {
        return rowRight(column) - sliderPercentGap - sliderWidth;
    }

    inline int sliderTop(int panelTop)
    {
        return rowTop(panelTop, sliderRowIndex) + 14;
    }

    inline Rect sliderBand(int column, int panelTop)
    {
        const int top = rowTop(panelTop, sliderRowIndex);
        const int left = sliderLeft(column);
        return { left - 9, top, left + sliderWidth + 9, top + 34 };
    }
}

void drawDashboard(
    HWND hwnd,
    HDC hdc
);
// Dashboard cards end at x=920 relative to their x=165 content origin.
constexpr float sysMonDashboardWideFactor = (1190.0f - 165.0f - 20.0f) / 920.0f;

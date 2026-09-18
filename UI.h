#pragma once
#include <vector>
#include <windows.h>
#include <string>



enum class AppPage
{
    Dashboard,
    Processes,
    Performance,
    SystemInfo,
    Settings
};

extern std::vector<DWORD> visibleProcessPids;
extern AppPage currentPage;
extern int processScrollOffset;
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
    CPU,
    Memory,
    Disk,
    GPU,
    Network
};

extern PerformanceView performanceView;
extern int selectedDiskIndex;
extern int selectedGpuIndex;
extern int systemInfoScrollOffset;
extern int systemInfoMaxScrollOffset;
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
extern DWORD selectedProcessPid;
extern DWORD hoveredProcessPid;
extern int processMaxScrollOffset;
extern int processScrollbarThumbTop;
extern int processScrollbarThumbBottom;

extern bool processScrollbarDragging;
extern int processScrollbarDragOffsetY;

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

void drawDashboard(
    HWND hwnd,
    HDC hdc
);
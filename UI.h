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
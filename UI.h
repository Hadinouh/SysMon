#pragma once

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

extern AppPage currentPage;
extern int processScrollOffset;
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
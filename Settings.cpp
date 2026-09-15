#include "Settings.h"

#include <string>


// These variables still live in Main.cpp for now.
extern HWND desktopWidget;
extern bool widgetEnabled;
extern int savedWidgetX;
extern int savedWidgetY;


std::string getSettingsPath()
{
    char path[MAX_PATH];

    GetModuleFileNameA(
        nullptr,
        path,
        MAX_PATH
    );

    std::string fullPath =
        path;

    size_t slash =
        fullPath.find_last_of(
            "\\/"
        );

    if (slash != std::string::npos)
    {
        fullPath =
            fullPath.substr(
                0,
                slash + 1
            );
    }

    return fullPath +
        "SysMon.ini";
}


void saveWidgetPosition()
{
    if (desktopWidget == nullptr)
        return;

    RECT rect;

    if (!GetWindowRect(
        desktopWidget,
        &rect))
    {
        return;
    }

    std::string path =
        getSettingsPath();

    std::string x =
        std::to_string(
            rect.left
        );

    std::string y =
        std::to_string(
            rect.top
        );

    WritePrivateProfileStringA(
        "Widget",
        "X",
        x.c_str(),
        path.c_str()
    );

    WritePrivateProfileStringA(
        "Widget",
        "Y",
        y.c_str(),
        path.c_str()
    );
}


void saveWidgetEnabled()
{
    std::string path =
        getSettingsPath();

    WritePrivateProfileStringA(
        "Widget",
        "Enabled",
        widgetEnabled ? "1" : "0",
        path.c_str()
    );
}


void loadWidgetSettings()
{
    std::string path =
        getSettingsPath();

    savedWidgetX =
        GetPrivateProfileIntA(
            "Widget",
            "X",
            -1,
            path.c_str()
        );

    savedWidgetY =
        GetPrivateProfileIntA(
            "Widget",
            "Y",
            -1,
            path.c_str()
        );

    widgetEnabled =
        GetPrivateProfileIntA(
            "Widget",
            "Enabled",
            1,
            path.c_str()
        ) != 0;
}


void positionDesktopWidget(HWND hwnd)
{
    const int widgetWidth = 240;
    const int widgetHeight = 100;

    if (
        savedWidgetX != -1 &&
        savedWidgetY != -1
    )
    {
        SetWindowPos(
            hwnd,
            nullptr,
            savedWidgetX,
            savedWidgetY,
            widgetWidth,
            widgetHeight,
            SWP_NOZORDER |
            SWP_NOACTIVATE
        );

        return;
    }

    RECT workArea;

    SystemParametersInfoA(
        SPI_GETWORKAREA,
        0,
        &workArea,
        0
    );

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
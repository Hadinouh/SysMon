#include "Widget.h"
#include "Stats.h"
#include "Settings.h"

#include <sstream>
#include <iomanip>


HWND desktopWidget = nullptr;

HFONT widgetLabelFont = nullptr;
HFONT widgetValueFont = nullptr;

bool widgetEnabled = true;

const COLORREF WIDGET_TRANSPARENT =
    RGB(1, 2, 3);


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


        // CPU
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


        // RAM
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


    case WM_EXITSIZEMOVE:
    {
        saveWidgetPosition();
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
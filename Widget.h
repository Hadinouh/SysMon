#pragma once

#include <windows.h>


extern HWND desktopWidget;

extern HFONT widgetLabelFont;
extern HFONT widgetValueFont;

extern bool widgetEnabled;

extern const COLORREF WIDGET_TRANSPARENT;


LRESULT CALLBACK WidgetProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
);
void configureOverlay(HWND owner);
void applyOverlayAppearance();

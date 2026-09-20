#pragma once
#include "Settings.h"
#include <algorithm>
#include <gdiplus.h>

// The source palette is dark. Preserve accents while mapping surfaces and
// text to a contrasting light palette; never query Windows during painting.
inline COLORREF themeSurfaceColor(COLORREF color)
{
    if (!isLightTheme()) return color;
    const int r=GetRValue(color), g=GetGValue(color), b=GetBValue(color);
    const int high=(std::max)({r,g,b}), low=(std::min)({r,g,b});
    const double luminance=0.2126*r+0.7152*g+0.0722*b;
    if (high-low > 65 && luminance >= 75)
        return RGB(static_cast<int>(r*0.60),static_cast<int>(g*0.60),static_cast<int>(b*0.60));
    if (color==RGB(18,20,26)) return RGB(237,242,247);
    if (color==RGB(14,18,24)) return RGB(250,252,254);
    if (luminance < 38) return RGB(255,255,255);
    if (luminance < 60) return RGB(211,220,229);
    int value=luminance < 110 ? static_cast<int>(210-(luminance-60)*1.4)
                             : (std::max)(24,static_cast<int>(145-(luminance-110)*0.85));
    return RGB(value,value+3,value+7);
}
inline COLORREF uiColor(COLORREF color)
{
    // Recolor UI cyan only; keep CPU, memory, disk and temperature series colors.
    if (appSettings.accentColorIndex != 0 &&
        (color==RGB(0,200,255) || color==RGB(0,190,255) || color==RGB(45,199,255) ||
         color==RGB(39,188,233) || color==RGB(0,180,255) || color==RGB(0,210,255) || color==RGB(65,205,245) || color==RGB(65,200,240) || color==RGB(70,205,245) || color==RGB(69,197,241))) {
        const COLORREF palette[]={RGB(45,199,255),RGB(27,207,139),RGB(165,83,255),RGB(255,132,56),RGB(255,76,91),RGB(235,57,155)};
        color=palette[(std::max)(0,(std::min)(5,appSettings.accentColorIndex))];
    }
    return themeSurfaceColor(color);
}
inline Gdiplus::Color themeGdiColor(BYTE alpha, BYTE r, BYTE g, BYTE b)
{
    const COLORREF color=uiColor(RGB(r,g,b));
    return Gdiplus::Color(alpha,GetRValue(color),GetGValue(color),GetBValue(color));
}

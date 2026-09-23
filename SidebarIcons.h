#pragma once
#include <gdiplus.h>
#include <string>

// Original vector artwork. Coordinates use a 16-unit square and inherit the
// theme tint and DPI transform, rather than depending on mislabeled PNG files.
inline bool drawSidebarSymbol(Gdiplus::Graphics& g, const wchar_t* path,
    int x, int y, int width, int height, COLORREF tint)
{
    if (!path) return false;
    const std::wstring name(path);
    if (name.rfind(L"sidebar_",0)!=0) return false;
    using namespace Gdiplus;
    const auto state=g.Save();
    g.TranslateTransform(static_cast<REAL>(x),static_cast<REAL>(y));
    g.ScaleTransform(width/16.0f,height/16.0f);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    Pen pen(Color(255,GetRValue(tint),GetGValue(tint),GetBValue(tint)),1.35f);
    pen.SetStartCap(LineCapRound);pen.SetEndCap(LineCapRound);pen.SetLineJoin(LineJoinRound);
    auto line=[&](float a,float b,float c,float d){g.DrawLine(&pen,a,b,c,d);};
    auto box=[&](float a,float b,float c,float d){g.DrawRectangle(&pen,a,b,c,d);};
    if(name==L"sidebar_cpu.png") {
        box(4,4,8,8);box(6,6,4,4);
        for(float n:{5.f,8.f,11.f}){line(n,1,n,3);line(n,13,n,15);line(1,n,3,n);line(13,n,15,n);}
    } else if(name==L"sidebar_memory.png") {
        box(1,4,14,7);for(float n:{3.f,7.f,11.f})box(n,6,2,3);
        for(float n:{3.f,5.f,7.f,9.f,11.f,13.f})line(n,12,n,14);
    } else if(name==L"sidebar_gpu.png") {
        box(2,3,12,9);line(1,2,1,14);line(4,13,10,13);line(14,6,15,6);
        g.DrawEllipse(&pen,4.f,5.f,5.f,5.f);line(6.5f,5,6.5f,10);line(4,7.5f,9,7.5f);line(11,5,11,10);
    } else if(name==L"sidebar_temperatures.png") {
        g.DrawArc(&pen,5.f,1.f,4.f,4.f,180.f,180.f);
        line(5,3,5,9);line(9,3,9,9);g.DrawArc(&pen,4.f,8.f,6.f,7.f,-42.f,264.f);
        line(7,5,7,12);line(11,4,13,4);line(11,7,13,7);
    } else if(name==L"sidebar_disk.png") {
        box(2,2,12,12);g.DrawEllipse(&pen,5.f,4.f,6.f,6.f);line(8,7,12,11);line(4,12,5,12);
    } else if(name==L"sidebar_network.png") {
        g.DrawArc(&pen,1.f,3.f,14.f,12.f,218.f,104.f);
        g.DrawArc(&pen,4.f,6.f,8.f,7.f,218.f,104.f);
        g.DrawEllipse(&pen,7.f,11.f,2.f,2.f);
    } else if(name==L"sidebar_overview.png") {
        box(1,1,6,6);box(10,1,5,4);box(1,10,6,5);box(10,8,5,7);
    } else if(name==L"sidebar_systeminfo.png") {
        box(1,2,14,10);line(8,12,8,15);line(5,15,11,15);line(8,7,8,10);
        g.DrawEllipse(&pen,7.6f,4.5f,.8f,.8f);
    } else if(name==L"sidebar_settings.png") {
        for(float n:{3.f,8.f,13.f})line(n,1,n,15);
        line(1,5,5,5);line(6,11,10,11);line(11,6,15,6);
    } else if(name==L"sidebar_processes.png") {
        for(float n:{3.f,8.f,13.f}){box(1,n-1,2,2);line(6,n,15,n);}
    } else {g.Restore(state);return false;}
    g.Restore(state);return true;
}

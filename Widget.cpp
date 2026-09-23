#include "Theme.h"
#include "Widget.h"
#include "Stats.h"
#include "Settings.h"
#include "BackgroundSampler.h"
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

HWND desktopWidget=nullptr;
HFONT widgetLabelFont=nullptr, widgetValueFont=nullptr;
bool widgetEnabled=true;
const COLORREF WIDGET_TRANSPARENT=RGB(1,2,3);

namespace {
using namespace Gdiplus;
struct OverlayMetric {
    const wchar_t* name;
    Color color;
    double value=0,capacity=100;
    bool valid=false,network=false;
    std::vector<double> history;
};
int metricCount() {
    int count=0;for(int i=0;i<5;++i)if(appSettings.overlayMetrics&(1<<i))++count;
    return (std::max)(1,count);
}
SizeF overlaySize() {
    const int count=metricCount();
    switch(appSettings.overlayStyle) {
    case 0:return SizeF(24+132.f*count,146);
    case 1:return SizeF(354,24+54.f*count);
    case 2:return SizeF(224,24+44.f*count);
    default:return SizeF(12+138.f*count,120);
    }
}
std::vector<OverlayMetric> overlayMetrics() {
    const wchar_t* names[]={L"CPU",L"RAM",L"GPU",L"Disk",L"Network"};
    const Color colors[]={Color(255,73,172,255),Color(255,187,112,241),Color(255,112,220,118),Color(255,52,210,123),Color(255,48,200,245)};
    std::vector<OverlayMetric> result;
    for(int i=0;i<5;++i) {
        if(!(appSettings.overlayMetrics&(1<<i)))continue;
        OverlayMetric metric{names[i],colors[i]};
        if(i==0){metric.value=cpuUsage;metric.valid=!cpuHistory.empty();metric.history=cpuHistory;}
        if(i==1){metric.value=ramPercent;metric.valid=totalRamGB>0;metric.history=ramHistory;}
        if(i==2 && !gpuStats.empty()){metric.value=gpuStats[0].utilizationPercent;metric.valid=gpuStats[0].performanceValid;metric.history=gpuStats[0].utilizationHistory;}
        if(i==3 && !diskStats.empty()){metric.value=diskStats[0].activeTimePercent;metric.valid=diskStats[0].performanceValid;metric.history=diskStats[0].activeHistory;}
        if(i==4) {
            metric.network=true;metric.capacity=0;size_t length=0;
            for(const auto& adapter:networkStats)if(adapter.performanceValid)length=(std::max)(length,adapter.downloadHistory.size());
            metric.history.assign(length,0);
            for(const auto& adapter:networkStats)if(adapter.performanceValid) {
                metric.valid=true;metric.value+=adapter.downloadMbps+adapter.uploadMbps;metric.capacity+=adapter.linkSpeedMbps;
                for(size_t j=0;j<adapter.downloadHistory.size();++j)
                    metric.history[length-adapter.downloadHistory.size()+j]+=adapter.downloadHistory[j]+(j<adapter.uploadHistory.size()?adapter.uploadHistory[j]:0);
            }
        }
        metric.valid=metric.valid && std::isfinite(metric.value);
        result.push_back(std::move(metric));
    }
    return result;
}
std::wstring metricValue(const OverlayMetric& metric) {
    if(!metric.valid)return L"Unavailable";
    std::wostringstream text;
    text<<std::fixed<<std::setprecision(metric.network?1:0)<<metric.value<<(metric.network?L" Mbps":L"%");
    return text.str();
}
void roundedPanel(Graphics& graphics,RectF rect) {
    GraphicsPath path;const float d=20;
    path.AddArc(rect.X,rect.Y,d,d,180,90);path.AddArc(rect.GetRight()-d,rect.Y,d,d,270,90);
    path.AddArc(rect.GetRight()-d,rect.GetBottom()-d,d,d,0,90);path.AddArc(rect.X,rect.GetBottom()-d,d,d,90,90);path.CloseFigure();
    SolidBrush fill(Color(245,13,22,31));graphics.FillPath(&fill,&path);
    Pen edge(Color(255,44,61,76),1);graphics.DrawPath(&edge,&path);
}
void overlayText(Graphics& graphics,const std::wstring& text,RectF rect,float size,Color color,StringAlignment align=StringAlignmentNear) {
    FontFamily family(L"Segoe UI");Font font(&family,size,FontStyleRegular,UnitPixel);
    StringFormat format;format.SetAlignment(align);format.SetLineAlignment(StringAlignmentCenter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);format.SetTrimming(StringTrimmingEllipsisCharacter);
    if(!appSettings.overlayBackground) {
        SolidBrush shadow(Color(190,0,0,0));RectF shifted=rect;shifted.X+=1;shifted.Y+=1;
        graphics.DrawString(text.c_str(),-1,&font,shifted,&format,&shadow);
    }
    SolidBrush brush(color);graphics.DrawString(text.c_str(),-1,&font,rect,&format,&brush);
}
void sparkline(Graphics& graphics,const OverlayMetric& metric,RectF rect) {
    if(!metric.valid || metric.history.size()<2)return;
    double peak=100;
    if(metric.network){peak=1;for(double value:metric.history)if(std::isfinite(value))peak=(std::max)(peak,value);}
    Pen pen(metric.color,1.8f);pen.SetLineJoin(LineJoinRound);pen.SetStartCap(LineCapRound);pen.SetEndCap(LineCapRound);
    for(size_t i=1;i<metric.history.size();++i) {
        if(!std::isfinite(metric.history[i-1]) || !std::isfinite(metric.history[i]))continue;
        const float x1=rect.X+rect.Width*static_cast<float>(i-1)/(metric.history.size()-1);
        const float x2=rect.X+rect.Width*static_cast<float>(i)/(metric.history.size()-1);
        const float y1=rect.GetBottom()-rect.Height*static_cast<float>((std::clamp)(metric.history[i-1]/peak,0.0,1.0));
        const float y2=rect.GetBottom()-rect.Height*static_cast<float>((std::clamp)(metric.history[i]/peak,0.0,1.0));
        graphics.DrawLine(&pen,x1,y1,x2,y2);
    }
}
// Shared by layered-window rendering and offscreen visual regression tests.
void drawOverlay(Graphics& graphics,float scale) {
    graphics.Clear(Color(0,0,0,0));graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);graphics.ScaleTransform(scale,scale);
    const SizeF size=overlaySize();
    const Color text(255,239,246,252),muted(255,166,184,199);
    const auto metrics=overlayMetrics();
    if(appSettings.overlayBackground && appSettings.overlayStyle!=3)roundedPanel(graphics,RectF(1,1,size.Width-2,size.Height-2));
    if(appSettings.overlayStyle==2){Pen accent(Color(255,48,200,245),2);graphics.DrawLine(&accent,10.f,14.f,10.f,size.Height-14);}
    for(size_t i=0;i<metrics.size();++i) {
        const auto& metric=metrics[i];const auto number=metricValue(metric);
        const float fraction=static_cast<float>((std::clamp)(metric.value/(std::max)(1.0,metric.capacity),0.0,1.0));
        if(appSettings.overlayStyle==0) {
            const float x=12+132.f*i;
            Pen track(Color(140,68,87,102),5),arc(metric.color,5);arc.SetStartCap(LineCapRound);arc.SetEndCap(LineCapRound);
            graphics.DrawEllipse(&track,x+23,18.f,86.f,86.f);
            if(metric.valid && fraction>0)graphics.DrawArc(&arc,x+23,18.f,86.f,86.f,-90.f,360*fraction);
            overlayText(graphics,number,RectF(x+24,40,84,40),!metric.valid?11.f:metric.network?13.f:22.f,text,StringAlignmentCenter);
            overlayText(graphics,metric.name,RectF(x,110,132,22),12,muted,StringAlignmentCenter);
        } else if(appSettings.overlayStyle==1) {
            const float y=12+54.f*i;
            overlayText(graphics,metric.name,RectF(16,y,70,30),12,muted);
            sparkline(graphics,metric,RectF(93,y+4,119,26));
            overlayText(graphics,number,RectF(224,y,113,32),metric.valid?20.f:13.f,text,StringAlignmentFar);
            if(i+1<metrics.size() && appSettings.overlayBackground){Pen divider(Color(110,44,61,76),1);graphics.DrawLine(&divider,16.f,y+43,338.f,y+43);}
        } else if(appSettings.overlayStyle==2) {
            const float y=12+44.f*i;
            overlayText(graphics,metric.name,RectF(24,y,72,28),12,muted);
            overlayText(graphics,number,RectF(99,y,109,28),metric.valid?19.f:13.f,text,StringAlignmentFar);
        } else {
            const float x=8+138.f*i;
            if(appSettings.overlayBackground)roundedPanel(graphics,RectF(x,4,130,112));
            overlayText(graphics,metric.name,RectF(x+13,17,104,22),12,muted);
            overlayText(graphics,number,RectF(x+13,41,104,34),metric.valid?(metric.network?18.f:25.f):13.f,text);
            SolidBrush track(Color(140,68,87,102)),fill(metric.color);
            graphics.FillRectangle(&track,x+13,91.f,104.f,4.f);
            if(metric.valid)graphics.FillRectangle(&fill,x+13,91.f,104*fraction,4.f);
        }
    }
    if(monitoringPaused)overlayText(graphics,L"Paused",RectF(size.Width-68,size.Height-15,60,14),10,muted,StringAlignmentFar);
}
void updateOverlaySurface(HWND hwnd) {
    RECT bounds{};if(!GetWindowRect(hwnd,&bounds))return;
    const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    if(width<=0 || height<=0)return;
    Bitmap bitmap(width,height,PixelFormat32bppPARGB);
    if(bitmap.GetLastStatus()!=Ok)return;
    {Graphics graphics(&bitmap);drawOverlay(graphics,width/overlaySize().Width);}
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);void* pixels=nullptr;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    HBITMAP dib=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(dc && dib && pixels) {
        BitmapData data{};Rect rect(0,0,width,height);
        if(bitmap.LockBits(&rect,ImageLockModeRead,PixelFormat32bppPARGB,&data)==Ok) {
            for(int y=0;y<height;++y)memcpy(static_cast<BYTE*>(pixels)+static_cast<size_t>(y)*width*4,static_cast<BYTE*>(data.Scan0)+static_cast<ptrdiff_t>(y)*data.Stride,static_cast<size_t>(width)*4);
            bitmap.UnlockBits(&data);
            auto previous=SelectObject(dc,dib);POINT destination{bounds.left,bounds.top},origin{};SIZE size{width,height};
            BLENDFUNCTION blend{AC_SRC_OVER,0,static_cast<BYTE>(255*appSettings.overlayOpacity/100),AC_SRC_ALPHA};
            UpdateLayeredWindow(hwnd,screen,&destination,&size,dc,&origin,0,&blend,ULW_ALPHA);
            SelectObject(dc,previous);
        }
    }
    if(dib)DeleteObject(dib);if(dc)DeleteDC(dc);if(screen)ReleaseDC(nullptr,screen);
}
}
void applyOverlayAppearance() {
    if(!desktopWidget)return;
    const auto dimensions=overlaySize();
    double scale=appSettings.overlayScale/100.0*GetDpiForWindow(desktopWidget)/96.0;
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfo(MonitorFromWindow(desktopWidget,MONITOR_DEFAULTTONEAREST),&monitor);
    scale=(std::min)(scale,(monitor.rcWork.bottom-monitor.rcWork.top-16.0)/dimensions.Height);
    scale=(std::min)(scale,(monitor.rcWork.right-monitor.rcWork.left-16.0)/dimensions.Width);
    SetWindowPos(desktopWidget,appSettings.overlayTopmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,
        (std::max)(1,static_cast<int>(dimensions.Width*scale)),(std::max)(1,static_cast<int>(dimensions.Height*scale)),SWP_NOMOVE|SWP_NOACTIVATE);
    updateOverlaySurface(desktopWidget);
}
void configureOverlay(HWND owner) {
    HMENU menu=CreatePopupMenu(),styles=CreatePopupMenu(),metrics=CreatePopupMenu(),opacity=CreatePopupMenu(),scale=CreatePopupMenu();
    const char* names[]={"Precision Rings","Telemetry Stack","Side Rail","Floating Tiles"};
    for(int i=0;i<4;++i)AppendMenuA(styles,MF_STRING|(appSettings.overlayStyle==i?MF_CHECKED:0),100+i,names[i]);
    const char* labels[]={"CPU","RAM","GPU","Disk","Network"};
    for(int i=0;i<5;++i)AppendMenuA(metrics,MF_STRING|((appSettings.overlayMetrics&(1<<i))?MF_CHECKED:0),110+i,labels[i]);
    for(int i=20;i<=100;i+=10)AppendMenuA(opacity,MF_STRING|(appSettings.overlayOpacity==i?MF_CHECKED:0),200+i,(std::to_string(i)+"%").c_str());
    for(int i=50;i<=200;i+=25)AppendMenuA(scale,MF_STRING|(appSettings.overlayScale==i?MF_CHECKED:0),400+i,(std::to_string(i)+"%").c_str());
    AppendMenuA(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(styles),"Appearance");
    AppendMenuA(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(metrics),"Metrics (select independently)");
    AppendMenuA(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(opacity),"Opacity");
    AppendMenuA(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(scale),"Scale");
    AppendMenuA(menu,MF_STRING|(appSettings.overlayBackground?MF_CHECKED:0),13,"Show background");
    AppendMenuA(menu,MF_STRING|(appSettings.overlayTopmost?MF_CHECKED:0),10,"Always on top");
    AppendMenuA(menu,MF_STRING|(appSettings.overlayRememberPosition?MF_CHECKED:0),11,"Remember monitor and position");
    AppendMenuA(menu,MF_STRING,12,"Reset overlay to defaults");
    POINT cursor;GetCursorPos(&cursor);SetForegroundWindow(owner);
    int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,cursor.x,cursor.y,0,owner,nullptr);DestroyMenu(menu);
    if(!command)return;
    saveWidgetPosition();
    if(command>=100 && command<=103)appSettings.overlayStyle=command-100;
    if(command>=110 && command<=114){int mask=appSettings.overlayMetrics^(1<<(command-110));if(mask)appSettings.overlayMetrics=mask;}
    if(command>=220 && command<=300)appSettings.overlayOpacity=command-200;
    if(command>=450 && command<=600)appSettings.overlayScale=command-400;
    if(command==10)appSettings.overlayTopmost=!appSettings.overlayTopmost;
    if(command==11)appSettings.overlayRememberPosition=!appSettings.overlayRememberPosition;
    if(command==13)appSettings.overlayBackground=!appSettings.overlayBackground;
    if(command==12)resetSettingsSection(6);
    positionDesktopWidget(desktopWidget);
    if(!saveAppSettings())MessageBoxA(owner,"Could not save overlay preferences.","SysMon",MB_OK|MB_ICONWARNING);
}
LRESULT CALLBACK WidgetProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) {
    switch(message) {
    case WM_PAINT:{PAINTSTRUCT paint;BeginPaint(hwnd,&paint);EndPaint(hwnd,&paint);updateOverlaySurface(hwnd);return 0;}
    case WM_TIMER:if(wParam==2)updateOverlaySurface(hwnd);return 0;
    case WM_EXITSIZEMOVE:saveWidgetPosition();return 0;
    case WM_DISPLAYCHANGE:positionDesktopWidget(hwnd);return 0;
    case WM_DPICHANGED:{const RECT* next=reinterpret_cast<RECT*>(lParam);SetWindowPos(hwnd,nullptr,next->left,next->top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);applyOverlayAppearance();return 0;}
    case WM_NCRBUTTONUP:case WM_CONTEXTMENU:configureOverlay(hwnd);return 0;
    case WM_NCHITTEST:return HTCAPTION;
    case WM_ERASEBKGND:return 1;
    }
    return DefWindowProc(hwnd,message,wParam,lParam);
}

#include <windows.h>
#include <gdiplus.h>
#include "UI.h"
#include "Settings.h"
#include "Stats.h"
#include "Processes.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>
#include <memory>
extern HFONT titleFont, subtitleFont, labelFont, mediumFont, bigFont, smallFont;
int main(int argc, char** argv)
{
    std::cout << std::unitbuf;
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token;
    Gdiplus::GdiplusStartup(&token, &input, nullptr);
    HFONT* fonts[] = {&titleFont, &subtitleFont, &labelFont, &mediumFont, &bigFont, &smallFont};
    int sizes[] = {32, 18, 18, 27, 40, 15};
    for (int i=0; i<6; ++i) *fonts[i] = CreateFontA(sizes[i],0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,"Segoe UI");
    appSettings.theme=argc>1 ? 1 : 0;
    if(argc>2) { appSettings.compactMode=true; appSettings.fontSizeIndex=2; appSettings.accentColorIndex=2; appSettings.temperatureUnit=1; appSettings.networkUnit=1; }
    refreshThemePreference();
    preloadUiAssets();
    for (int i=0; i<24; ++i) { updateStats(true); getCachedRunningProcesses(); Sleep(250); }
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 3440, 1440);
    HGDIOBJ old = SelectObject(dc, bitmap);
    ReleaseDC(nullptr, screen);
    struct Page { const char* name; AppPage page; PerformanceView view; };
    Page pages[] = {
        {"Dashboard",AppPage::Dashboard,PerformanceView::Overview},
        {"Processes",AppPage::Processes,PerformanceView::Overview},
        {"CPU",AppPage::Performance,PerformanceView::CPU},
        {"Memory",AppPage::Performance,PerformanceView::Memory},
        {"Disk",AppPage::Performance,PerformanceView::Disk},
        {"GPU",AppPage::Performance,PerformanceView::GPU},
        {"Network",AppPage::Performance,PerformanceView::Network},
        {"Temperatures",AppPage::Temperatures,PerformanceView::Overview},
        {"Tools",AppPage::Tools,PerformanceView::Overview},
        {"SystemInfo",AppPage::SystemInfo,PerformanceView::Overview},
        {"Settings",AppPage::Settings,PerformanceView::Overview},
        {"SettingsScrolled",AppPage::Settings,PerformanceView::Overview}
    };
    struct Viewport { float scale, scaleX; };
    for(auto viewport : {Viewport{1.0f,1.0f}, Viewport{1.25f,1.25f}, Viewport{1.5f,1.5f}, Viewport{2.0f,2.0f}, Viewport{1.75f,1.75f}, Viewport{1.4f,1920.0f/1190.0f}, Viewport{1.75f,3440.0f/1190.0f}}) for(const auto& page : pages)
    {
        const float scale=viewport.scale;
        currentPage=page.page; performanceView=page.view;
        if (page.page == AppPage::Settings) settingsScrollOffset = std::string(page.name)=="SettingsScrolled" ? 350 : 0;
        std::vector<double> times;
        for(int i=0; i<4; ++i) {
            int saved=SaveDC(dc); SetGraphicsMode(dc,GM_ADVANCED);
            XFORM transform={viewport.scaleX,0,0,scale,0,0}; SetWorldTransform(dc,&transform);
            setUiRenderTransform(scale,0,0,viewport.scaleX);
            auto start=std::chrono::steady_clock::now();
            drawDashboard(nullptr,dc); GdiFlush();
            times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
            RestoreDC(dc,saved);
        }
        if (viewport.scaleX > scale)
        {
            CLSID png = {0x557cf406, 0x1a04, 0x11d3, {0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
            Gdiplus::Bitmap fullImage(bitmap, nullptr);
            const int width = viewport.scaleX > 2.0f ? 3440 : 1920;
            auto image = std::unique_ptr<Gdiplus::Bitmap>(fullImage.Clone(0,0,width,
                static_cast<int>(720*scale),PixelFormat32bppARGB));
            std::string path = std::string("tests/layout-") + (argc>1 ? "light-" : "dark-") +
                std::to_string(width) + "-" + page.name + ".png";
            std::wstring widePath(path.begin(),path.end());
            if (image->Save(widePath.c_str(), &png, nullptr) != Gdiplus::Ok) return 1;
        }
        std::sort(times.begin(),times.end());
        std::cout << page.name << " scale=" << scale << " scaleX=" << viewport.scaleX << " median_ms=" << times[2] << " max_ms=" << times.back() << "\n";
    }
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc);
    clearUiImageCache(); stopProcessSampler(); stopHardwareSensorBridge();
    for(auto font : fonts) DeleteObject(*font);
    Gdiplus::GdiplusShutdown(token);
}

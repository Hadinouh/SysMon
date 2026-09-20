#include "../Main.cpp"
#include <cassert>
#include <iostream>
#include <fstream>

int main()
{
    const auto settingsPath=getSettingsPath();
    assert(settingsPath.find("\\tests\\")!=std::string::npos);
    std::ifstream original(settingsPath,std::ios::binary); bool existed=original.good();
    std::string backup((std::istreambuf_iterator<char>(original)),{}); original.close();
    for (const auto dimensions : {std::pair<int,int>{1190,720}, {1920,1008}, {2560,1400}, {3440,1400}})
    {
        HWND window = CreateWindowExW(0, L"STATIC", L"Layout test", WS_POPUP,
            0, 0, dimensions.first, dimensions.second, nullptr, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        assert(window);
        const auto transform = getSysMonUiTransform(window);
        assert(transform.drawWidth == dimensions.first);
        assert(transform.scale <= 1.75);
        assert(transform.scaleX >= transform.scale);
        for (auto page : {AppPage::Dashboard, AppPage::Processes, AppPage::Settings, AppPage::Performance, AppPage::Tools, AppPage::SystemInfo, AppPage::Temperatures})
        {
            currentPage = page;
            for (POINT logical : {POINT{80,150}, POINT{500,30}, POINT{810,200}, POINT{850,600}})
            {
                double x = logical.x;
                if (page == AppPage::Dashboard && logical.y >= 58 && logical.x >= 165 &&
                    transform.scaleX > transform.scale + 0.001)
                    x = 165 + (x - 165) * sysMonDashboardWideFactor;
                const int physicalX = static_cast<int>(std::ceil(x * transform.scaleX));
                const int physicalY = static_cast<int>(std::ceil(logical.y * transform.scale));
                const auto mapped = sysMonClientToLogicalPoint(window, MAKELPARAM(physicalX, physicalY));
                assert(std::abs(mapped.x - logical.x) <= 1);
                assert(std::abs(mapped.y - logical.y) <= 1);
            }
        }
        currentPage=AppPage::Settings; appSettings=SysMonAppSettings(); settingsScrollOffset=0;
        SettingsLayout::refreshDensity();
        auto click=[&](int x,int y,UINT message=WM_LBUTTONDOWN,WPARAM flags=MK_LBUTTON) {
            WindowProc(window,message,flags,MAKELPARAM(static_cast<int>(std::ceil(x*transform.scaleX)),static_cast<int>(std::ceil(y*transform.scale))));
        };
        const auto compact=SettingsLayout::rowBand(2,SettingsLayout::topRowTop,2);
        click(165+compact.left+5,compact.top+12);
        assert(appSettings.compactMode);
        SettingsLayout::refreshDensity(); assert(SettingsLayout::rowPitch==44);
        const int sliderX=165+SettingsLayout::sliderLeft(2);
        const int sliderY=SettingsLayout::sliderTop(SettingsLayout::topRowTop);
        click(sliderX,sliderY); assert(appSettings.transparencyPercent==55);
        click(sliderX+SettingsLayout::sliderWidth,sliderY,WM_MOUSEMOVE);
        assert(appSettings.transparencyPercent==100);
        click(sliderX+SettingsLayout::sliderWidth,sliderY,WM_LBUTTONUP,0);
        assert(!settingsTransparencyDragging);
        flushPendingSettingsSave(window);
        DestroyWindow(window);
    }
    if(existed) { std::ofstream restore(settingsPath,std::ios::binary|std::ios::trunc); restore<<backup; }
    else DeleteFileA(settingsPath.c_str());
    std::cout << "PASS: settings click/drag interactions; expanded widths, bounded scale, and click mapping for all pages\n";
}

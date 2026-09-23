#include "../Widget.cpp"
#include <cassert>
#include <iostream>
int main() {
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token;assert(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok);
    cpuUsage=34;cpuHistory={15,20,18,33,28,34};totalRamGB=32;ramPercent=48;ramHistory={44,46,46,47,48,48};
    GpuStats gpu;gpu.performanceValid=true;gpu.utilizationPercent=62;gpu.utilizationHistory={20,30,45,35,50,62};gpuStats={gpu};
    appSettings.overlayMetrics=7;
    CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    for(int style=0;style<4;++style)for(bool background:{false,true})for(int mask:{1,7,31}) {
        appSettings.overlayStyle=style;appSettings.overlayBackground=background;appSettings.overlayMetrics=mask;
        const auto size=overlaySize();
        for(float scale:{0.5f,1.f,1.25f,1.5f,2.f}) {
            Gdiplus::Bitmap bitmap(static_cast<int>(size.Width*scale),static_cast<int>(size.Height*scale),PixelFormat32bppPARGB);
            {Gdiplus::Graphics graphics(&bitmap);drawOverlay(graphics,scale);}
            Gdiplus::Color pixel;assert(bitmap.GetPixel(0,0,&pixel)==Gdiplus::Ok);assert(pixel.GetA()==0);
            size_t visible=0,partial=0,transparent=0;
            for(UINT y=0;y<bitmap.GetHeight();++y)for(UINT x=0;x<bitmap.GetWidth();++x){bitmap.GetPixel(x,y,&pixel);if(pixel.GetA())++visible;else ++transparent;if(pixel.GetA()>0&&pixel.GetA()<245)++partial;}
            assert(visible>0 && transparent>0 && partial>0);
            if(!background)assert(transparent>visible);
            if(mask==7 && scale==1.f){std::wstring path=L"tests/overlay-"+std::to_wstring(style)+(background?L"-background.png":L"-transparent.png");assert(bitmap.Save(path.c_str(),&png,nullptr)==Gdiplus::Ok);}
        }
    }
    Gdiplus::GdiplusShutdown(token);
    std::cout<<"PASS: four styles, both backgrounds, 1/3/5 metrics, 50/100/125/150/200% scale and antialiased transparency\n";
}

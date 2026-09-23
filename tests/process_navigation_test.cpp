#include "../Main.cpp"
#include <cassert>
#include <iostream>
int main() {
    GdiplusStartupInput input;ULONG_PTR token;GdiplusStartup(&token,&input,nullptr);
    GpuStats gpu;gpu.stableId="fixture-gpu";
    for(DWORD pid=101;pid<113;++pid)gpu.processes.push_back({pid,static_cast<double>(pid-100),1024*pid});
    gpuStats={gpu};activeNetworkConnections.clear();
    for(DWORD pid=201;pid<213;++pid){activeNetworkConnections.push_back({pid,"fixture.exe","","ESTABLISHED"});activeNetworkConnections.push_back({pid,"fixture.exe","","LISTEN"});}
    for(auto dimensions:{std::pair<int,int>{1190,720},{1920,1008},{3440,1400}}) {
        HWND owner=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"SysMon navigation fixture",WS_POPUP,-10000,-10000,dimensions.first,dimensions.second,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);assert(owner);
        const auto transform=getSysMonUiTransform(owner);
        auto click=[&](int x,int y){WindowProc(owner,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(static_cast<int>(std::ceil(x*transform.scaleX)),static_cast<int>(std::ceil(y*transform.scale))));};
        currentPage=AppPage::Performance;performanceView=PerformanceView::GPU;selectedGpuIndex=0;
        click(770,440);assert(!ProcessDetails::window); // Old invisible hitbox must not respond.
        // Text is rendered at (580,432), with viewport origin (165,103).
        click(770,543);assert(ProcessDetails::window);assert(IsWindowVisible(ProcessDetails::window));assert(ListView_GetItemCount(ProcessDetails::list)==12);
        auto rows=ProcessDetails::rows();assert(rows.front().pid==112);DestroyWindow(ProcessDetails::window);
        performanceView=PerformanceView::Network;click(800,465);assert(!ProcessDetails::window);
        // Text is rendered at (610,459), with viewport origin (165,103).
        click(800,568);assert(ProcessDetails::window);assert(IsWindowVisible(ProcessDetails::window));assert(ListView_GetItemCount(ProcessDetails::list)==12);
        rows=ProcessDetails::rows();assert(rows.front().connections==2&&rows.front().established);
        activeNetworkConnections.clear();ProcessDetails::refresh();assert(ListView_GetItemCount(ProcessDetails::list)==0);
        for(DWORD pid=201;pid<213;++pid){activeNetworkConnections.push_back({pid,"fixture.exe","","ESTABLISHED"});activeNetworkConnections.push_back({pid,"fixture.exe","","LISTEN"});}
        DestroyWindow(ProcessDetails::window);DestroyWindow(owner);
    }
    ProcessDetails::kind=ProcessDetails::Kind::Gpu;ProcessDetails::gpuId="disconnected";assert(ProcessDetails::rows().empty());
    stopProcessSampler();GdiplusShutdown(token);
    std::cout<<"PASS: GPU/network View All hitboxes at three sizes, full lists beyond five rows, ranking, aggregation and empty/disconnected states\n";
}

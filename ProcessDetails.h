#pragma once
#include <windows.h>
#include <commctrl.h>
#include <map>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include "Stats.h"
#include "Processes.h"

namespace ProcessDetails {
enum class Kind { Gpu, Network };
struct Row { DWORD pid;std::string name;double usage=0;unsigned long long memory=0;int connections=0;bool established=false; };
inline HWND window=nullptr,list=nullptr;
inline Kind kind=Kind::Gpu;
inline std::string gpuId;
inline int gpuIndex=0;
inline std::vector<Row> rows() {
    std::vector<Row> result;
    if(kind==Kind::Network) {
        std::map<DWORD,Row> byPid;
        for(const auto& connection:activeNetworkConnections){auto& row=byPid[connection.pid];row.pid=connection.pid;row.name=connection.processName;++row.connections;row.established=row.established||connection.state=="ESTABLISHED";}
        for(auto& entry:byPid)result.push_back(entry.second);
        std::stable_sort(result.begin(),result.end(),[](const Row& a,const Row& b){return a.connections!=b.connections?a.connections>b.connections:a.pid<b.pid;});
    } else {
        const GpuStats* selected=nullptr;
        for(const auto& gpu:gpuStats)if(!gpuId.empty()&&gpu.stableId==gpuId){selected=&gpu;break;}
        if(gpuId.empty() && gpuIndex>=0 && gpuIndex<static_cast<int>(gpuStats.size()))selected=&gpuStats[gpuIndex];
        if(selected){const auto& processes=getCachedRunningProcesses();for(const auto& gpuProcess:selected->processes){Row row{};row.pid=gpuProcess.pid;row.name="PID "+std::to_string(row.pid);row.usage=gpuProcess.utilizationPercent;row.memory=gpuProcess.dedicatedMemoryBytes;for(const auto& process:processes)if(process.pid==row.pid){row.name=process.name;break;}result.push_back(row);}}
        std::stable_sort(result.begin(),result.end(),[](const Row& a,const Row& b){return a.usage!=b.usage?a.usage>b.usage:a.memory!=b.memory?a.memory>b.memory:a.pid<b.pid;});
    }
    return result;
}
inline void refresh() {
    DWORD selectedPid=MAXDWORD;int selected=ListView_GetNextItem(list,-1,LVNI_SELECTED),top=ListView_GetTopIndex(list);
    if(selected>=0){LVITEMA item{};item.mask=LVIF_PARAM;item.iItem=selected;SendMessageA(list,LVM_GETITEMA,0,reinterpret_cast<LPARAM>(&item));selectedPid=static_cast<DWORD>(item.lParam);}
    SendMessage(list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(list);
    auto data=rows();int selection=-1;
    for(size_t i=0;i<data.size();++i){auto& row=data[i];std::ostringstream usage,memory;usage<<std::fixed<<std::setprecision(1)<<row.usage<<"%";memory<<std::fixed<<std::setprecision(1)<<row.memory/1048576.0<<" MB";
        std::string cells[]={row.name,std::to_string(row.pid),kind==Kind::Gpu?usage.str():std::to_string(row.connections),kind==Kind::Gpu?memory.str():row.established?"Established":"Other TCP states"};
        LVITEMA item{};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=static_cast<int>(i);item.pszText=cells[0].data();item.lParam=row.pid;SendMessageA(list,LVM_INSERTITEMA,0,reinterpret_cast<LPARAM>(&item));
        for(int column=1;column<4;++column){item.iSubItem=column;item.pszText=cells[column].data();SendMessageA(list,LVM_SETITEMTEXTA,i,reinterpret_cast<LPARAM>(&item));}if(row.pid==selectedPid)selection=static_cast<int>(i);
    }
    if(selection>=0)ListView_SetItemState(list,selection,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    if(top>0 && !data.empty())ListView_EnsureVisible(list,(std::min)(top,static_cast<int>(data.size())-1),FALSE);
    SendMessage(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,FALSE);
    std::string status=data.empty()?"No process data available.":std::to_string(data.size())+" processes";
    if(kind==Kind::Network)status+=" | TCP connection counts, not per-process bandwidth";
    SetWindowTextA(GetDlgItem(window,2),status.c_str());
}
inline LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    switch(message){
    case WM_GETMINMAXINFO:{auto* limits=reinterpret_cast<MINMAXINFO*>(lp);limits->ptMinTrackSize={620,300};return 0;}
    case WM_SIZE:{int width=LOWORD(lp),height=HIWORD(lp);MoveWindow(list,12,12,(std::max)(1,width-24),(std::max)(1,height-58),TRUE);MoveWindow(GetDlgItem(hwnd,2),12,height-34,width-24,24,TRUE);return 0;}
    case WM_TIMER:refresh();return 0;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:KillTimer(hwnd,1);window=nullptr;list=nullptr;return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
inline void show(HWND owner,Kind selectedKind,int selectedGpu) {
    if(window)DestroyWindow(window);kind=selectedKind;gpuIndex=selectedGpu;gpuId=selectedGpu>=0&&selectedGpu<static_cast<int>(gpuStats.size())?gpuStats[selectedGpu].stableId:"";
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandle(nullptr);cls.lpszClassName=L"SysMonProcessDetails";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
    window=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,kind==Kind::Gpu?L"SysMon - All GPU Processes":L"SysMon - All Network Processes",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,800,480,owner,nullptr,cls.hInstance,nullptr);
    if(!window)return;
    list=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,12,12,750,370,window,nullptr,cls.hInstance,nullptr);
    ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
    const char* titles[]={"Name","PID",kind==Kind::Gpu?"GPU usage":"TCP connections",kind==Kind::Gpu?"Dedicated VRAM":"Connection state"};
    for(int i=0;i<4;++i){LVCOLUMNA column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<char*>(titles[i]);column.cx=i==0?280:i==1?90:170;SendMessageA(list,LVM_INSERTCOLUMNA,i,reinterpret_cast<LPARAM>(&column));}
    HWND status=CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE,12,400,750,24,window,reinterpret_cast<HMENU>(2),cls.hInstance,nullptr);
    for(HWND control:{list,status})SendMessage(control,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
    refresh();SetTimer(window,1,1000,nullptr);ShowWindow(window,SW_SHOW);SetForegroundWindow(window);SetFocus(list);
}
}

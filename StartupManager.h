#pragma once
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <vector>
#include <string>
#include "Settings.h"
namespace StartupManager {
inline constexpr const wchar_t* runKey=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr const wchar_t* disabledKey=L"Software\\SysMon\\DisabledStartupRun";
struct Entry { std::wstring name,command;DWORD type=0;std::vector<BYTE> data;bool enabled=true; };
inline std::vector<Entry> entries;
inline HWND window=nullptr,list=nullptr;
inline void enumerate(const wchar_t* path,bool enabled) {
    HKEY key=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,path,0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return;
    DWORD maxName=0,maxData=0;RegQueryInfoKeyW(key,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&maxName,&maxData,nullptr,nullptr);
    std::vector<wchar_t> name(maxName+2);std::vector<BYTE> data(maxData+sizeof(wchar_t)*2);
    for(DWORD index=0;;++index) {
        DWORD length=static_cast<DWORD>(name.size()),bytes=static_cast<DWORD>(data.size()),type=0;
        LONG result=RegEnumValueW(key,index,name.data(),&length,nullptr,&type,data.data(),&bytes);
        if(result==ERROR_NO_MORE_ITEMS)break;if(result!=ERROR_SUCCESS)continue;
        if(type!=REG_SZ && type!=REG_EXPAND_SZ)continue;
        Entry entry;entry.name.assign(name.data(),length);entry.type=type;entry.enabled=enabled;entry.data.assign(data.begin(),data.begin()+bytes);
        data[bytes]=0;data[bytes+1]=0;entry.command=reinterpret_cast<const wchar_t*>(data.data());entries.push_back(std::move(entry));
    }
    RegCloseKey(key);
}
inline std::wstring publisher(const std::wstring& command) {
    wchar_t expanded[32768]{};if(!ExpandEnvironmentStringsW(command.c_str(),expanded,32768))return L"Unavailable";
    std::wstring path=expanded;
    if(path.empty())return L"Unavailable";
    if(path[0]==L'"') {auto end=path.find(L'"',1);if(end==std::wstring::npos)return L"Unavailable";path=path.substr(1,end-1);}
    else {auto end=path.find(L".exe");if(end!=std::wstring::npos)path.resize(end+4);}
    DWORD ignored=0,size=GetFileVersionInfoSizeW(path.c_str(),&ignored);if(!size || size>1024*1024)return L"Unavailable";
    std::vector<BYTE> data(size);if(!GetFileVersionInfoW(path.c_str(),0,size,data.data()))return L"Unavailable";
    struct Translation {WORD language,codepage;};Translation* translations=nullptr;UINT bytes=0;
    if(!VerQueryValueW(data.data(),L"\\VarFileInfo\\Translation",reinterpret_cast<void**>(&translations),&bytes)||bytes<sizeof(Translation))return L"Unavailable";
    wchar_t query[128]{};swprintf_s(query,L"\\StringFileInfo\\%04x%04x\\CompanyName",translations[0].language,translations[0].codepage);
    wchar_t* company=nullptr;UINT count=0;if(VerQueryValueW(data.data(),query,reinterpret_cast<void**>(&company),&count)&&count>1)return company;
    return L"Unavailable";
}
inline void refresh() {
    entries.clear();enumerate(runKey,true);enumerate(disabledKey,false);ListView_DeleteAllItems(list);
    for(size_t i=0;i<entries.size();++i) {
        auto& e=entries[i];LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=static_cast<int>(i);item.pszText=e.name.data();
        SendMessageW(list,LVM_INSERTITEMW,0,reinterpret_cast<LPARAM>(&item));
        std::wstring texts[]={publisher(e.command),e.enabled?L"Registered with Windows":L"Disabled by SysMon",L"Not measured"};
        for(int j=0;j<3;++j) {item.iSubItem=j+1;item.pszText=texts[j].data();SendMessageW(list,LVM_SETITEMTEXTW,i,reinterpret_cast<LPARAM>(&item));}
    }
}
// Copy and flush first; preserve the complete value type and bytes. Refuse
// conflicts and stale rows instead of overwriting another program's entry.
inline bool moveEntry(const Entry& entry,const wchar_t* active=runKey,const wchar_t* backup=disabledKey) {
    HKEY source=nullptr,destination=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,entry.enabled?active:backup,0,KEY_QUERY_VALUE|KEY_SET_VALUE,&source)!=ERROR_SUCCESS)return false;
    DWORD type=0,size=0;bool ok=false;
    if(RegQueryValueExW(source,entry.name.c_str(),nullptr,&type,nullptr,&size)==ERROR_SUCCESS && type==entry.type && size==entry.data.size()) {
        std::vector<BYTE> actual(size);
        if(RegQueryValueExW(source,entry.name.c_str(),nullptr,&type,actual.data(),&size)==ERROR_SUCCESS && actual==entry.data &&
           RegCreateKeyExW(HKEY_CURRENT_USER,entry.enabled?backup:active,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&destination,nullptr)==ERROR_SUCCESS) {
            DWORD existing=0;
            if(RegQueryValueExW(destination,entry.name.c_str(),nullptr,nullptr,nullptr,&existing)==ERROR_FILE_NOT_FOUND &&
               RegSetValueExW(destination,entry.name.c_str(),0,entry.type,entry.data.data(),static_cast<DWORD>(entry.data.size()))==ERROR_SUCCESS &&
               RegFlushKey(destination)==ERROR_SUCCESS) {
                ok=RegDeleteValueW(source,entry.name.c_str())==ERROR_SUCCESS;
                if(!ok)RegDeleteValueW(destination,entry.name.c_str());
            }
        }
    }
    if(destination)RegCloseKey(destination);RegCloseKey(source);return ok;
}
inline LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_CREATE: {
        auto control=[&](const wchar_t* cls,const wchar_t* text,DWORD style,int id){HWND h=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,10,10,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandle(nullptr),nullptr);SendMessage(h,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return h;};
        control(L"STATIC",L"Current-user Run entries. Windows may separately block registered entries. Use Windows Settings for all sources.",0,10);
        list=control(WC_LISTVIEWW,L"",WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,11);
        ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
        const wchar_t* titles[]={L"Name",L"Publisher",L"Status",L"Startup impact"};
        for(int i=0;i<4;++i){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<wchar_t*>(titles[i]);column.cx=i==0?240:170;SendMessageW(list,LVM_INSERTCOLUMNW,i,reinterpret_cast<LPARAM>(&column));}
        control(L"BUTTON",L"Enable / Disable",WS_TABSTOP,12);control(L"BUTTON",L"Refresh",WS_TABSTOP,13);control(L"BUTTON",L"All Windows startup apps",WS_TABSTOP,14);
        refresh();return 0;
    }
    case WM_SIZE:{int width=LOWORD(lp),height=HIWORD(lp);MoveWindow(GetDlgItem(hwnd,10),16,12,width-32,32,TRUE);MoveWindow(list,16,48,width-32,(std::max)(40,height-112),TRUE);MoveWindow(GetDlgItem(hwnd,12),16,height-48,150,30,TRUE);MoveWindow(GetDlgItem(hwnd,13),180,height-48,90,30,TRUE);MoveWindow(GetDlgItem(hwnd,14),284,height-48,200,30,TRUE);return 0;}
    case WM_COMMAND:
        if(LOWORD(wp)==12){int i=ListView_GetNextItem(list,-1,LVNI_SELECTED);if(i>=0 && i<static_cast<int>(entries.size())) {if(!moveEntry(entries[i]))MessageBoxW(hwnd,L"The entry changed, a conflicting backup exists, or Windows denied access. Nothing has been overwritten. Refresh and try again.",L"Startup Apps",MB_OK|MB_ICONWARNING);refresh();}}
        if(LOWORD(wp)==13)refresh();
        if(LOWORD(wp)==14)ShellExecuteW(hwnd,L"open",L"ms-settings:startupapps",nullptr,nullptr,SW_SHOWNORMAL);
        return 0;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:window=nullptr;list=nullptr;return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
inline void show(HWND owner) {
    if(window){ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);return;}
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandle(nullptr);cls.lpszClassName=L"SysMonStartupManager";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
    window=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"SysMon - Startup Apps",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,820,480,owner,nullptr,cls.hInstance,nullptr);
}
}

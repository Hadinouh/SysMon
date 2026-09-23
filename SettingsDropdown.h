#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <algorithm>
#include "Theme.h"

namespace SettingsDropdown {
struct Item { std::string label; bool checked; };
inline HFONT font=nullptr;
inline int width=180,rowHeight=34;
inline bool active=false;
inline bool measure(MEASUREITEMSTRUCT* item) {
    if(!active || !item || item->CtlType!=ODT_MENU)return false;
    item->itemWidth=width;item->itemHeight=rowHeight;return true;
}
inline bool draw(DRAWITEMSTRUCT* item) {
    if(!active || !item || item->CtlType!=ODT_MENU || !item->itemData)return false;
    const auto& choice=*reinterpret_cast<Item*>(item->itemData);
    const bool highlighted=(item->itemState&ODS_SELECTED)!=0;
    COLORREF background=uiColor(highlighted?RGB(35,64,88):RGB(17,35,52));
    HBRUSH brush=CreateSolidBrush(background);FillRect(item->hDC,&item->rcItem,brush);DeleteObject(brush);
    auto previous=SelectObject(item->hDC,font);SetBkMode(item->hDC,TRANSPARENT);
    SetTextColor(item->hDC,uiColor(RGB(245,245,245)));
    RECT text=item->rcItem;text.left+=rowHeight;text.right-=12;
    DrawTextA(item->hDC,choice.label.c_str(),-1,&text,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
    if(choice.checked) {
        RECT mark=item->rcItem;mark.right=mark.left+rowHeight;
        SetTextColor(item->hDC,uiColor(RGB(45,199,255)));
        DrawTextW(item->hDC,L"\x2713",1,&mark,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    SelectObject(item->hDC,previous);return true;
}
inline int show(HWND owner,RECT anchor,int current,const std::vector<std::pair<int,std::string>>& choices) {
    if(choices.empty())return current;
    const UINT dpi=GetDpiForWindow(owner);
    rowHeight=(std::max)(MulDiv(34,dpi?dpi:96,96),static_cast<int>(anchor.bottom-anchor.top)+4);
    font=CreateFontW(-(std::max)(14,rowHeight*14/34),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    HMENU menu=CreatePopupMenu();if(!menu){DeleteObject(font);font=nullptr;return current;}
    std::vector<Item> items;items.reserve(choices.size());
    HDC dc=GetDC(owner);auto old=SelectObject(dc,font);width=(std::max)(140L,anchor.right-anchor.left);
    for(const auto& choice:choices){SIZE extent{};GetTextExtentPoint32A(dc,choice.second.c_str(),static_cast<int>(choice.second.size()),&extent);width=(std::max)(width,static_cast<int>(extent.cx)+rowHeight+24);items.push_back({choice.second,choice.first==current});}
    SelectObject(dc,old);ReleaseDC(owner,dc);
    for(size_t i=0;i<items.size();++i){MENUITEMINFOA info{};info.cbSize=sizeof(info);info.fMask=MIIM_ID|MIIM_FTYPE|MIIM_DATA|MIIM_STRING|MIIM_STATE;info.wID=12000+static_cast<UINT>(i);info.fType=MFT_OWNERDRAW;info.fState=items[i].checked?MFS_CHECKED:MFS_UNCHECKED;info.dwItemData=reinterpret_cast<ULONG_PTR>(&items[i]);info.dwTypeData=items[i].label.data();InsertMenuItemA(menu,static_cast<UINT>(i),TRUE,&info);}
    HBRUSH background=CreateSolidBrush(uiColor(RGB(17,35,52)));MENUINFO info{};info.cbSize=sizeof(info);info.fMask=MIM_BACKGROUND;info.hbrBack=background;SetMenuInfo(menu,&info);
    TPMPARAMS params{};params.cbSize=sizeof(params);params.rcExclude=anchor;active=true;
    int selected=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN|TPM_TOPALIGN,anchor.left,anchor.bottom,owner,&params);
    active=false;DestroyMenu(menu);DeleteObject(background);DeleteObject(font);font=nullptr;
    return selected>=12000 && selected<12000+static_cast<int>(choices.size())?choices[selected-12000].first:current;
}
}

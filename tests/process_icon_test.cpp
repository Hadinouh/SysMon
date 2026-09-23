#include "ProcessMetadata.h"
#include <cassert>
#include <iostream>
int main() {
    ProcessMetadataCache cache;
    HICON large=nullptr;
    for(int i=0;i<100 && !large;++i) {large=cache.icon(GetCurrentProcessId(),true,96);Sleep(50);}
    assert(large);
    for(int requested : {16,24,32,48,64,96}) {
        HICON icon=cache.icon(GetCurrentProcessId(),false,requested);
        assert(icon);ICONINFO info{};assert(GetIconInfo(icon,&info));
        BITMAP bitmap{};assert(GetObject(info.hbmColor,sizeof(bitmap),&bitmap));
        assert(bitmap.bmWidth==requested && bitmap.bmHeight==requested);
        DeleteObject(info.hbmColor);DeleteObject(info.hbmMask);
    }
    assert(!cache.icon(0,false,96));cache.clear();
    std::cout<<"PASS: background cache supplies native 16/24/32/48/64/96 pixel icons and handles missing processes\n";
}

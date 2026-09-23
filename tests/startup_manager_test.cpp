#include "StartupManager.h"
#include <cassert>
#include <iostream>
int main() {
    std::wstring base=L"Software\\SysMonTests\\Startup-"+std::to_wstring(GetCurrentProcessId());
    std::wstring active=base+L"\\Active",backup=base+L"\\Backup";
    HKEY key=nullptr;assert(RegCreateKeyExW(HKEY_CURRENT_USER,active.c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&key,nullptr)==ERROR_SUCCESS);
    StartupManager::Entry e;e.name=L"Round trip";e.type=REG_EXPAND_SZ;
    std::wstring command=L"\"%LOCALAPPDATA%\\Example\\example.exe\" --test";
    auto bytes=reinterpret_cast<const BYTE*>(command.c_str());e.data.assign(bytes,bytes+(command.size()+1)*sizeof(wchar_t));
    assert(RegSetValueExW(key,e.name.c_str(),0,e.type,e.data.data(),static_cast<DWORD>(e.data.size()))==ERROR_SUCCESS);
    assert(StartupManager::moveEntry(e,active.c_str(),backup.c_str()));
    DWORD size=0;assert(RegQueryValueExW(key,e.name.c_str(),nullptr,nullptr,nullptr,&size)==ERROR_FILE_NOT_FOUND);
    e.enabled=false;assert(StartupManager::moveEntry(e,active.c_str(),backup.c_str()));
    DWORD type=0;size=static_cast<DWORD>(e.data.size());std::vector<BYTE> restored(size);
    assert(RegQueryValueExW(key,e.name.c_str(),nullptr,&type,restored.data(),&size)==ERROR_SUCCESS);assert(type==e.type && restored==e.data);
    e.enabled=true;e.data.push_back(0);assert(!StartupManager::moveEntry(e,active.c_str(),backup.c_str()));
    RegCloseKey(key);assert(RegDeleteTreeW(HKEY_CURRENT_USER,base.c_str())==ERROR_SUCCESS);
    std::cout<<"PASS: startup disable/enable preserves original bytes and type; stale row rejected; test-only registry keys removed\n";
}

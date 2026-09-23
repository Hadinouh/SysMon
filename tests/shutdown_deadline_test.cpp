#include "AppLifecycle.h"
#include <cassert>
#include <iostream>
int main(int argc,char**) {
    if(argc>1) { AppLifecycle::beginShutdown(); Sleep(INFINITE); return 3; }
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
    std::wstring command=L"\""+std::wstring(path)+L"\" --blocked-cleanup";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    assert(CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process));
    assert(WaitForSingleObject(process.hProcess,20000)==WAIT_OBJECT_0);
    DWORD result=0;assert(GetExitCodeProcess(process.hProcess,&result));assert(result==2);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    std::cout<<"PASS: blocked cleanup terminates at the shutdown deadline\n";
}

#include "Stats.h"
#include "BackgroundSampler.h"
#include <tlhelp32.h>
#include <cassert>
#include <iostream>
#include <string>
static DWORD childOf(DWORD parent) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);DWORD result=0;
    if(Process32FirstW(snapshot,&entry))do {if(entry.th32ParentProcessID==parent && _wcsicmp(entry.szExeFile,L"SysMonSensors.exe")==0){result=entry.th32ProcessID;break;}}while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);return result;
}
int main(int argc,char**) {
    if(argc>1) {startHardwareSensorBridge();for(;;){updateStats();Sleep(100);}}
    startHardwareSensorBridge();DWORD child=0;
    for(int i=0;i<200 && !(child=childOf(GetCurrentProcessId()));++i){updateStats();Sleep(100);}
    if(!child){std::cerr<<"FAIL: helper did not launch\n";stopHardwareSensorBridge();return 1;}
    HANDLE helper=OpenProcess(SYNCHRONIZE,FALSE,child);assert(helper);
    setMonitoringPaused(true);
    assert(WaitForSingleObject(helper,15000)==WAIT_OBJECT_0);CloseHandle(helper);
    setMonitoringPaused(false);
    for(int i=0;i<200 && !(child=childOf(GetCurrentProcessId()));++i){updateStats();Sleep(100);}
    assert(child);helper=OpenProcess(SYNCHRONIZE,FALSE,child);assert(helper);
    stopHardwareSensorBridge();assert(WaitForSingleObject(helper,5000)==WAIT_OBJECT_0);CloseHandle(helper);
    wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);std::wstring command=L"\""+std::wstring(path)+L"\" --child";
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};assert(CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi));
    child=0;for(int i=0;i<200 && !(child=childOf(pi.dwProcessId));++i)Sleep(100);
    assert(child);helper=OpenProcess(SYNCHRONIZE,FALSE,child);assert(helper);
    TerminateProcess(pi.hProcess,99);assert(WaitForSingleObject(pi.hProcess,5000)==WAIT_OBJECT_0);
    assert(WaitForSingleObject(helper,5000)==WAIT_OBJECT_0);CloseHandle(helper);CloseHandle(pi.hProcess);CloseHandle(pi.hThread);
    std::cout<<"PASS: pause terminates sensor helper; resume restarts it; normal shutdown and forced parent death leave no sensor child\n";
}

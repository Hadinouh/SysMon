#pragma once
#include <windows.h>
#include <string>
#include <exception>

namespace AppLifecycle {
inline HANDLE instance = nullptr;
inline HANDLE shutdownDone = nullptr;
inline HANDLE logFile = INVALID_HANDLE_VALUE;
inline SRWLOCK logLock = SRWLOCK_INIT;
inline void log(const char* event) noexcept {
    if (logFile == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME time{}; GetLocalTime(&time);
    char line[1024]{};
    int count = _snprintf_s(line, sizeof(line), _TRUNCATE,
        "%04u-%02u-%02u %02u:%02u:%02u %s\r\n", time.wYear,time.wMonth,time.wDay,
        time.wHour,time.wMinute,time.wSecond,event);
    if (count < 0) count = static_cast<int>(strlen(line));
    AcquireSRWLockExclusive(&logLock);
    DWORD written; WriteFile(logFile,line,count,&written,nullptr);
    ReleaseSRWLockExclusive(&logLock);
}
inline void emergencyLog(const char* message) noexcept {
    if(logFile!=INVALID_HANDLE_VALUE) {DWORD written;WriteFile(logFile,message,static_cast<DWORD>(strlen(message)),&written,nullptr);}
}
inline LONG WINAPI crash(EXCEPTION_POINTERS* info) {
    char event[100]{};
    _snprintf_s(event,sizeof(event),_TRUNCATE,"Unhandled exception 0x%08lX",info->ExceptionRecord->ExceptionCode);
    emergencyLog(event);
    return EXCEPTION_EXECUTE_HANDLER;
}
inline void initializeLog(const std::string& directory) {
    const std::string path=directory+"\\SysMon.log";
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExA(path.c_str(),GetFileExInfoStandard,&data) &&
        (data.nFileSizeHigh || data.nFileSizeLow>1024*1024))
        MoveFileExA(path.c_str(),(path+".previous").c_str(),MOVEFILE_REPLACE_EXISTING);
    logFile=CreateFileA(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(crash);
    std::set_terminate([] { emergencyLog("Unhandled C++ exception\r\n"); TerminateProcess(GetCurrentProcess(),1); });
    log("SysMon starting");
}
inline void beginShutdown() {
    shutdownDone=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!shutdownDone)return;
    HANDLE thread=CreateThread(nullptr,0,[](LPVOID)->DWORD {
        if(WaitForSingleObject(shutdownDone,15000)==WAIT_TIMEOUT) {
            emergencyLog("Shutdown deadline exceeded; terminating process and its sensor job\r\n");
            TerminateProcess(GetCurrentProcess(),2);
        }
        return 0;
    },nullptr,0,nullptr);
    if(thread)CloseHandle(thread);
}
inline bool acquireInstance() {
    instance=CreateMutexW(nullptr,FALSE,L"Local\\SysMon.MainWindow.v1");
    if (!instance) return false;
    if (GetLastError()!=ERROR_ALREADY_EXISTS) return true;
    // The first process may still be constructing its window.
    for (int attempt=0;attempt<40;++attempt) {
        HWND window=FindWindowA("SysMonWindowClass",nullptr);
        if (window) { ShowWindowAsync(window,SW_RESTORE); SetForegroundWindow(window); break; }
        Sleep(50);
    }
    CloseHandle(instance); instance=nullptr;
    return false;
}
}

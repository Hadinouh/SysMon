#pragma once

#include <windows.h>
#include <string>
#include <vector>

struct ProcessInfo
{
    DWORD pid;
    DWORD parentPid;

    std::string name;

    double memoryMB;
    double cpuPercent;

    DWORD threadCount;
    DWORD handleCount;
};
std::vector<ProcessInfo> getRunningProcesses();
bool terminateTaskByPid(
    DWORD pid
);
#pragma once

#include <windows.h>
#include <string>
#include <vector>

struct ProcessInfo
{
    DWORD pid;
    std::string name;
    double memoryMB;
    double cpuPercent;
    DWORD threadCount;
};
std::vector<ProcessInfo> getRunningProcesses();
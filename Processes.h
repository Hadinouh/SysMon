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

// UI-owned process snapshot shared by every page. Reads request a background
// refresh at most once per second and never enumerate processes while painting.
// The first read can be empty until the initial collection completes.
const std::vector<ProcessInfo>& getCachedRunningProcesses(
    bool forceRefresh = false
);


bool terminateTaskByPid(
    DWORD pid
);
// Join the background collector before application teardown.
void stopProcessSampler();

#include "Stats.h"

#include <algorithm>
#include <vector>



double cpuUsage = 0.0;
std::vector<double> cpuHistory;
std::vector<double> ramHistory;
double usedRamGB = 0.0;
double totalRamGB = 0.0;
int ramPercent = 0;

double usedDiskGB = 0.0;
double totalDiskGB = 0.0;
int diskPercent = 0;

ULONGLONG uptimeSeconds = 0;


ULONGLONG previousIdle = 0;
ULONGLONG previousKernel = 0;
ULONGLONG previousUser = 0;


ULONGLONG fileTimeToULL(const FILETIME& ft)
{
    ULARGE_INTEGER value;

    value.LowPart =
        ft.dwLowDateTime;

    value.HighPart =
        ft.dwHighDateTime;

    return value.QuadPart;
}


double getCpuUsage()
{
    FILETIME idleTime;
    FILETIME kernelTime;
    FILETIME userTime;

    if (!GetSystemTimes(
        &idleTime,
        &kernelTime,
        &userTime))
    {
        return 0.0;
    }

    ULONGLONG idle =
        fileTimeToULL(idleTime);

    ULONGLONG kernel =
        fileTimeToULL(kernelTime);

    ULONGLONG user =
        fileTimeToULL(userTime);

    if (previousKernel == 0)
    {
        previousIdle = idle;
        previousKernel = kernel;
        previousUser = user;

        return 0.0;
    }

    ULONGLONG idleDiff =
        idle - previousIdle;

    ULONGLONG kernelDiff =
        kernel - previousKernel;

    ULONGLONG userDiff =
        user - previousUser;

    ULONGLONG total =
        kernelDiff + userDiff;

    previousIdle = idle;
    previousKernel = kernel;
    previousUser = user;

    if (total == 0)
        return 0.0;

    double usage =
        100.0 *
        (
            1.0 -
            static_cast<double>(
                idleDiff
            ) / total
        );

    return std::clamp(
        usage,
        0.0,
        100.0
    );
}

void addCpuHistorySample()
{
    cpuHistory.push_back(cpuUsage);

    if (cpuHistory.size() > 120 )
    {
        cpuHistory.erase(
            cpuHistory.begin()
        );
    }
}

void addRamHistorySample()
{
    ramHistory.push_back(
        static_cast<double>(ramPercent)
    );

    if (ramHistory.size() > 120)
    {
        ramHistory.erase(
            ramHistory.begin()
        );
    }
}

void updateStats()
{
    // CPU
    cpuUsage =
        getCpuUsage();
addCpuHistorySample();

    // RAM
    MEMORYSTATUSEX memory = {};

    memory.dwLength =
        sizeof(memory);

    if (GlobalMemoryStatusEx(
        &memory))
    {
        totalRamGB =
            memory.ullTotalPhys /
            (1024.0 *
             1024.0 *
             1024.0);

        double available =
            memory.ullAvailPhys /
            (1024.0 *
             1024.0 *
             1024.0);

        usedRamGB =
            totalRamGB -
            available;

        ramPercent =
            static_cast<int>(
                (usedRamGB /
                 totalRamGB)
                * 100.0
            );

    }
       addRamHistorySample();

    // Disk
    ULARGE_INTEGER freeAvailable;
    ULARGE_INTEGER totalBytes;
    ULARGE_INTEGER freeBytes;

    if (GetDiskFreeSpaceExA(
        "C:\\",
        &freeAvailable,
        &totalBytes,
        &freeBytes))
    {
        totalDiskGB =
            totalBytes.QuadPart /
            (1024.0 *
             1024.0 *
             1024.0);

        double freeDiskGB =
            freeBytes.QuadPart /
            (1024.0 *
             1024.0 *
             1024.0);

        usedDiskGB =
            totalDiskGB -
            freeDiskGB;

        diskPercent =
            static_cast<int>(
                (usedDiskGB /
                 totalDiskGB)
                * 100.0
            );
    }


    // Uptime
    uptimeSeconds =
        GetTickCount64() / 1000;
}
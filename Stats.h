#pragma once

#include <windows.h>
#include <string>
#include <vector>


struct DiskStats
{
    int diskNumber = 0;
    std::string displayName = "Disk";
    std::string driveLettersText;
    std::vector<char> driveLetters;
    std::string model = "--";
    std::string type = "--";

    double capacityGB = 0.0;
    double formattedGB = 0.0;

    bool systemDisk = false;
    bool pageFile = false;
    bool performanceValid = false;

    double activeTimePercent = 0.0;
    double averageResponseMs = 0.0;
    double readMBps = 0.0;
    double writeMBps = 0.0;

    std::vector<double> activeHistory;
    std::vector<double> transferHistory;
};


extern double cpuUsage;

extern double usedRamGB;
extern double totalRamGB;
extern int ramPercent;

extern double usedDiskGB;
extern double totalDiskGB;
extern int diskPercent;

extern std::vector<DiskStats> diskStats;

extern ULONGLONG uptimeSeconds;

extern std::vector<double> cpuHistory;
extern std::vector<double> ramHistory;


double getCpuUsage();
void updateStats();
void addCpuHistorySample();
void addRamHistorySample();

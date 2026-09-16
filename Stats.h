#pragma once

#include <windows.h>

extern double cpuUsage;

extern double usedRamGB;
extern double totalRamGB;
extern int ramPercent;

extern double usedDiskGB;
extern double totalDiskGB;
extern int diskPercent;

extern ULONGLONG uptimeSeconds;

double getCpuUsage();
void updateStats();
#include <vector>

extern std::vector<double> cpuHistory;

void addCpuHistorySample();
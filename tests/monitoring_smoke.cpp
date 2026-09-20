#include "Stats.h"
#include "Processes.h"
#ifndef BASELINE
#include "ProcessMetadata.h"
#endif
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>
int main()
{
    using Clock = std::chrono::steady_clock;
    std::vector<double> times;
#ifndef BASELINE
    ProcessMetadataCache metadata;
#endif
    size_t processCount = 0;
    for (int i = 0; i < 40; ++i)
    {
        auto start = Clock::now();
        updateStats(true);
        const auto& processes = getCachedRunningProcesses();
        processCount = processes.size();
#ifndef BASELINE
        for (const auto& p : processes) metadata.path(p.pid);
        metadata.icon(GetCurrentProcessId());
#endif
        if (i == 20) {
            refreshSystemInfo(true); refreshGpuStats(true);
            refreshNetworkStats(true); refreshTemperatureStats(true);
        }
        times.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
        Sleep(250);
    }
    std::sort(times.begin(), times.end());
    std::cout << "UI refresh ms: median=" << times[times.size()/2]
              << " p95=" << times[times.size()*95/100] << " max=" << times.back()
              << " processes=" << processCount << " history=" << cpuHistory.size()
              << " RAM_GB=" << totalRamGB << " system_initialized=" << systemInfo.initialized;
#ifndef BASELINE
    const bool metadataReady = !metadata.path(GetCurrentProcessId()).empty();
    std::cout << " metadata_ready=" << metadataReady;
    stopProcessSampler();
    metadata.clear();
#endif
    stopHardwareSensorBridge();
    std::cout << " shutdown=ok\n";
    return (processCount > 0 && totalRamGB > 0 && cpuHistory.size() > 1 && systemInfo.initialized) ? 0 : 1;
}

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


struct ConnectedDeviceInfo
{
    std::string name = "--";
    std::string type = "Device";
    std::string connectionType = "--";
    std::string deviceClass = "--";
    std::string manufacturer = "--";
    std::string status = "--";
    std::string location = "--";
    std::string vendorId = "--";
    std::string productId = "--";
    std::string instanceId = "--";
    std::string hardwareId = "--";
    std::string selectionKey;
};


struct SystemInfoData
{
    bool initialized = false;

    // Operating system
    std::string osName = "--";
    std::string osVersion = "--";
    std::string installedOn = "--";
    std::string osBuild = "--";
    std::string experience = "--";
    std::string systemType = "--";
    std::string computerName = "--";

    // Processor
    std::string cpuName = "--";
    int cpuCores = 0;
    int cpuThreads = 0;
    std::string cpuBaseSpeed = "--";
    std::string cpuCurrentSpeed = "--";
    std::string cpuSocket = "--";
    std::string virtualization = "--";
    std::string l1Cache = "--";
    std::string l2Cache = "--";
    std::string l3Cache = "--";

    // Memory
    std::string installedMemory = "--";
    std::string memoryType = "--";
    std::string memorySpeed = "--";
    std::string memorySlots = "--";
    std::string memoryFormFactor = "--";

    // Graphics
    std::string gpuName = "--";
    std::string gpuMemory = "--";
    std::string gpuDriverVersion = "--";
    std::string gpuDriverDate = "--";
    std::string directXVersion = "--";

    // System / motherboard
    std::string systemManufacturer = "--";
    std::string systemModel = "--";
    std::string motherboardManufacturer = "--";
    std::string motherboardModel = "--";

    // BIOS
    std::string biosVendor = "--";
    std::string biosVersion = "--";
    std::string biosDate = "--";

    // Network
    std::string networkAdapter = "--";
    std::string networkConnectionType = "--";
    std::string ipv4Address = "--";
    std::string ipv6Address = "--";

    // Currently connected external / PnP devices
    std::vector<ConnectedDeviceInfo> connectedDevices;
};


extern double cpuUsage;

extern double usedRamGB;
extern double totalRamGB;
extern int ramPercent;

extern double usedDiskGB;
extern double totalDiskGB;
extern int diskPercent;

extern std::vector<DiskStats> diskStats;
extern SystemInfoData systemInfo;

extern ULONGLONG uptimeSeconds;

extern std::vector<double> cpuHistory;
extern std::vector<double> ramHistory;


double getCpuUsage();
void updateStats();
void addCpuHistorySample();
void addRamHistorySample();
void refreshSystemInfo(bool force = false);

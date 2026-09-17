#include "Stats.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <winioctl.h>


double cpuUsage = 0.0;
std::vector<double> cpuHistory;
std::vector<double> ramHistory;
double usedRamGB = 0.0;
double totalRamGB = 0.0;
int ramPercent = 0;

double usedDiskGB = 0.0;
double totalDiskGB = 0.0;
int diskPercent = 0;

std::vector<DiskStats> diskStats;

ULONGLONG uptimeSeconds = 0;


ULONGLONG previousIdle = 0;
ULONGLONG previousKernel = 0;
ULONGLONG previousUser = 0;


namespace
{
    struct DiskCounterState
    {
        bool initialized = false;
        ULONGLONG tickMs = 0;
        LONGLONG bytesRead = 0;
        LONGLONG bytesWritten = 0;
        LONGLONG readTime = 0;
        LONGLONG writeTime = 0;
        LONGLONG idleTime = 0;
        DWORD readCount = 0;
        DWORD writeCount = 0;
    };

    std::map<int, DiskCounterState> diskCounterStates;
    ULONGLONG lastDiskEnumerationTick = 0;

    constexpr double bytesPerGB =
        1024.0 * 1024.0 * 1024.0;

    constexpr double bytesPerMB =
        1024.0 * 1024.0;


    std::string trimText(
        const std::string& value)
    {
        size_t first = 0;

        while (
            first < value.size() &&
            std::isspace(
                static_cast<unsigned char>(
                    value[first]
                )
            )
        )
        {
            first++;
        }

        size_t last = value.size();

        while (
            last > first &&
            std::isspace(
                static_cast<unsigned char>(
                    value[last - 1]
                )
            )
        )
        {
            last--;
        }

        return value.substr(
            first,
            last - first
        );
    }


    HANDLE openPhysicalDrive(
        int diskNumber,
        DWORD access)
    {
        std::ostringstream path;

        path
            << "\\\\.\\PhysicalDrive"
            << diskNumber;

        return CreateFileA(
            path.str().c_str(),
            access,
            FILE_SHARE_READ |
                FILE_SHARE_WRITE |
                FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );
    }


    bool queryVolumeDiskNumbers(
        char driveLetter,
        std::vector<int>& diskNumbers)
    {
        char volumePath[] =
            "\\\\.\\C:";

        volumePath[4] = driveLetter;

        HANDLE volume =
            CreateFileA(
                volumePath,
                0,
                FILE_SHARE_READ |
                    FILE_SHARE_WRITE |
                    FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr
            );

        if (volume == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        BYTE buffer[
            sizeof(VOLUME_DISK_EXTENTS) +
            sizeof(DISK_EXTENT) * 15
        ] = {};

        DWORD bytesReturned = 0;

        BOOL ok =
            DeviceIoControl(
                volume,
                IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                nullptr,
                0,
                buffer,
                sizeof(buffer),
                &bytesReturned,
                nullptr
            );

        CloseHandle(volume);

        if (!ok ||
            bytesReturned <
                sizeof(VOLUME_DISK_EXTENTS))
        {
            return false;
        }

        auto* extents =
            reinterpret_cast<
                VOLUME_DISK_EXTENTS*
            >(
                buffer
            );

        for (
            DWORD i = 0;
            i < extents->NumberOfDiskExtents;
            i++
        )
        {
            int diskNumber =
                static_cast<int>(
                    extents->Extents[i].DiskNumber
                );

            if (
                std::find(
                    diskNumbers.begin(),
                    diskNumbers.end(),
                    diskNumber
                ) == diskNumbers.end()
            )
            {
                diskNumbers.push_back(
                    diskNumber
                );
            }
        }

        return !diskNumbers.empty();
    }


    std::map<int, std::vector<char>>
    getDiskVolumeMap()
    {
        std::map<int, std::vector<char>> result;

        DWORD driveMask =
            GetLogicalDrives();

        for (
            int index = 0;
            index < 26;
            index++
        )
        {
            if (
                (driveMask &
                    (1UL << index)) == 0
            )
            {
                continue;
            }

            char driveLetter =
                static_cast<char>(
                    'A' + index
                );

            char rootPath[] =
                "C:\\";

            rootPath[0] = driveLetter;

            if (
                GetDriveTypeA(
                    rootPath
                ) != DRIVE_FIXED
            )
            {
                continue;
            }

            std::vector<int> diskNumbers;

            if (!queryVolumeDiskNumbers(
                    driveLetter,
                    diskNumbers
                ))
            {
                continue;
            }

            for (int diskNumber :
                 diskNumbers)
            {
                result[diskNumber]
                    .push_back(
                        driveLetter
                    );
            }
        }

        return result;
    }


    std::set<char> getPageFileDrives()
    {
        std::set<char> result;

        const char* keyPath =
            "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management";

        DWORD bytes = 0;

        LSTATUS status =
            RegGetValueA(
                HKEY_LOCAL_MACHINE,
                keyPath,
                "PagingFiles",
                RRF_RT_REG_MULTI_SZ,
                nullptr,
                nullptr,
                &bytes
            );

        if (
            status != ERROR_SUCCESS ||
            bytes == 0
        )
        {
            return result;
        }

        std::vector<char> buffer(
            bytes + 2,
            '\0'
        );

        status =
            RegGetValueA(
                HKEY_LOCAL_MACHINE,
                keyPath,
                "PagingFiles",
                RRF_RT_REG_MULTI_SZ,
                nullptr,
                buffer.data(),
                &bytes
            );

        if (status != ERROR_SUCCESS)
        {
            return result;
        }

        const char* entry =
            buffer.data();

        while (*entry != '\0')
        {
            if (
                std::isalpha(
                    static_cast<unsigned char>(
                        entry[0]
                    )
                ) &&
                entry[1] == ':'
            )
            {
                result.insert(
                    static_cast<char>(
                        std::toupper(
                            static_cast<unsigned char>(
                                entry[0]
                            )
                        )
                    )
                );
            }

            entry +=
                std::strlen(entry) + 1;
        }

        return result;
    }


    char getWindowsDriveLetter()
    {
        char windowsPath[MAX_PATH] = {};

        if (
            GetWindowsDirectoryA(
                windowsPath,
                MAX_PATH
            ) > 1 &&
            windowsPath[1] == ':'
        )
        {
            return static_cast<char>(
                std::toupper(
                    static_cast<unsigned char>(
                        windowsPath[0]
                    )
                )
            );
        }

        return 0;
    }


    std::string makeDriveLetterText(
        const std::vector<char>& letters)
    {
        std::ostringstream text;

        for (
            size_t i = 0;
            i < letters.size();
            i++
        )
        {
            if (i > 0)
            {
                text << " ";
            }

            text
                << letters[i]
                << ":";
        }

        return text.str();
    }


    double getFormattedCapacityGB(
        const std::vector<char>& letters)
    {
        unsigned long long totalBytes = 0;

        for (char letter : letters)
        {
            char rootPath[] =
                "C:\\";

            rootPath[0] = letter;

            ULARGE_INTEGER freeAvailable = {};
            ULARGE_INTEGER volumeTotal = {};
            ULARGE_INTEGER freeBytes = {};

            if (GetDiskFreeSpaceExA(
                    rootPath,
                    &freeAvailable,
                    &volumeTotal,
                    &freeBytes
                ))
            {
                totalBytes +=
                    volumeTotal.QuadPart;
            }
        }

        return
            static_cast<double>(
                totalBytes
            ) / bytesPerGB;
    }


    bool containsDriveLetter(
        const std::vector<char>& letters,
        char letter)
    {
        if (letter == 0)
        {
            return false;
        }

        for (char current : letters)
        {
            if (
                std::toupper(
                    static_cast<unsigned char>(
                        current
                    )
                ) ==
                std::toupper(
                    static_cast<unsigned char>(
                        letter
                    )
                )
            )
            {
                return true;
            }
        }

        return false;
    }


    bool containsAnyDriveLetter(
        const std::vector<char>& letters,
        const std::set<char>& targets)
    {
        for (char letter : letters)
        {
            char upper =
                static_cast<char>(
                    std::toupper(
                        static_cast<unsigned char>(
                            letter
                        )
                    )
                );

            if (
                targets.find(upper) !=
                targets.end()
            )
            {
                return true;
            }
        }

        return false;
    }


    std::string storageTypeText(
        STORAGE_BUS_TYPE busType,
        bool seekPenaltyKnown,
        bool incursSeekPenalty)
    {
        if (busType == BusTypeNvme)
        {
            return "SSD (NVMe)";
        }

        if (seekPenaltyKnown)
        {
            if (!incursSeekPenalty)
            {
                if (busType == BusTypeSata ||
                    busType == BusTypeAta)
                {
                    return "SSD (SATA)";
                }

                if (busType == BusTypeUsb)
                {
                    return "SSD (USB)";
                }

                return "SSD";
            }

            if (busType == BusTypeSata ||
                busType == BusTypeAta)
            {
                return "HDD (SATA)";
            }

            if (busType == BusTypeUsb)
            {
                return "HDD (USB)";
            }

            return "HDD";
        }

        switch (busType)
        {
        case BusTypeSata:
        case BusTypeAta:
            return "SATA drive";

        case BusTypeUsb:
            return "USB drive";

        case BusTypeSas:
            return "SAS drive";

        case BusTypeVirtual:
        case BusTypeFileBackedVirtual:
            return "Virtual disk";

        default:
            return "Drive";
        }
    }


    void queryPhysicalDiskMetadata(
        DiskStats& disk)
    {
        HANDLE handle =
            openPhysicalDrive(
                disk.diskNumber,
                0
            );

        if (handle == INVALID_HANDLE_VALUE)
        {
            return;
        }

        GET_LENGTH_INFORMATION lengthInfo = {};
        DWORD bytesReturned = 0;

        if (DeviceIoControl(
                handle,
                IOCTL_DISK_GET_LENGTH_INFO,
                nullptr,
                0,
                &lengthInfo,
                sizeof(lengthInfo),
                &bytesReturned,
                nullptr
            ))
        {
            disk.capacityGB =
                static_cast<double>(
                    lengthInfo.Length.QuadPart
                ) / bytesPerGB;
        }

        STORAGE_PROPERTY_QUERY deviceQuery = {};
        deviceQuery.PropertyId =
            StorageDeviceProperty;
        deviceQuery.QueryType =
            PropertyStandardQuery;

        BYTE descriptorBuffer[4096] = {};
        bytesReturned = 0;

        STORAGE_BUS_TYPE busType =
            BusTypeUnknown;

        if (DeviceIoControl(
                handle,
                IOCTL_STORAGE_QUERY_PROPERTY,
                &deviceQuery,
                sizeof(deviceQuery),
                descriptorBuffer,
                sizeof(descriptorBuffer),
                &bytesReturned,
                nullptr
            ) &&
            bytesReturned >=
                sizeof(STORAGE_DEVICE_DESCRIPTOR))
        {
            auto* descriptor =
                reinterpret_cast<
                    STORAGE_DEVICE_DESCRIPTOR*
                >(
                    descriptorBuffer
                );

            busType =
                descriptor->BusType;

            std::string vendor;
            std::string product;

            if (
                descriptor->VendorIdOffset > 0 &&
                descriptor->VendorIdOffset <
                    bytesReturned
            )
            {
                vendor = trimText(
                    reinterpret_cast<char*>(
                        descriptorBuffer +
                        descriptor->VendorIdOffset
                    )
                );
            }

            if (
                descriptor->ProductIdOffset > 0 &&
                descriptor->ProductIdOffset <
                    bytesReturned
            )
            {
                product = trimText(
                    reinterpret_cast<char*>(
                        descriptorBuffer +
                        descriptor->ProductIdOffset
                    )
                );
            }

            if (!product.empty())
            {
                disk.model = product;
            }
            else if (!vendor.empty())
            {
                disk.model = vendor;
            }

            if (
                !vendor.empty() &&
                !product.empty() &&
                product.find(vendor) ==
                    std::string::npos
            )
            {
                disk.model =
                    vendor + " " + product;
            }
        }

        STORAGE_PROPERTY_QUERY seekQuery = {};
        seekQuery.PropertyId =
            StorageDeviceSeekPenaltyProperty;
        seekQuery.QueryType =
            PropertyStandardQuery;

        DEVICE_SEEK_PENALTY_DESCRIPTOR
            seekPenalty = {};

        bool seekPenaltyKnown =
            DeviceIoControl(
                handle,
                IOCTL_STORAGE_QUERY_PROPERTY,
                &seekQuery,
                sizeof(seekQuery),
                &seekPenalty,
                sizeof(seekPenalty),
                &bytesReturned,
                nullptr
            ) != FALSE;

        disk.type =
            storageTypeText(
                busType,
                seekPenaltyKnown,
                seekPenaltyKnown &&
                    seekPenalty.IncursSeekPenalty
            );

        CloseHandle(handle);
    }


    void copyDynamicDiskData(
        const DiskStats& oldDisk,
        DiskStats& newDisk)
    {
        newDisk.performanceValid =
            oldDisk.performanceValid;

        newDisk.activeTimePercent =
            oldDisk.activeTimePercent;

        newDisk.readMBps =
            oldDisk.readMBps;

        newDisk.writeMBps =
            oldDisk.writeMBps;

        newDisk.averageResponseMs =
            oldDisk.averageResponseMs;

        newDisk.activeHistory =
            oldDisk.activeHistory;

        newDisk.transferHistory =
            oldDisk.transferHistory;
    }


    void refreshDiskList()
    {
        std::map<int, std::vector<char>>
            volumeMap =
                getDiskVolumeMap();

        std::set<int> diskNumbers;

        for (const auto& item : volumeMap)
        {
            diskNumbers.insert(
                item.first
            );
        }

        for (
            int diskNumber = 0;
            diskNumber < 32;
            diskNumber++
        )
        {
            HANDLE handle =
                openPhysicalDrive(
                    diskNumber,
                    0
                );

            if (handle != INVALID_HANDLE_VALUE)
            {
                diskNumbers.insert(
                    diskNumber
                );

                CloseHandle(handle);
            }
        }

        char windowsDrive =
            getWindowsDriveLetter();

        std::set<char> pageFileDrives =
            getPageFileDrives();

        std::vector<DiskStats> refreshed;

        for (int diskNumber :
             diskNumbers)
        {
            DiskStats disk;
            disk.diskNumber =
                diskNumber;

            auto volumeIt =
                volumeMap.find(
                    diskNumber
                );

            if (volumeIt !=
                volumeMap.end())
            {
                disk.driveLetters =
                    volumeIt->second;

                std::sort(
                    disk.driveLetters.begin(),
                    disk.driveLetters.end()
                );
            }

            disk.driveLettersText =
                makeDriveLetterText(
                    disk.driveLetters
                );

            std::ostringstream displayName;

            displayName
                << "Disk "
                << diskNumber;

            if (!disk.driveLettersText.empty())
            {
                displayName
                    << " ("
                    << disk.driveLettersText
                    << ")";
            }

            disk.displayName =
                displayName.str();

            std::ostringstream fallbackModel;
            fallbackModel
                << "Physical Drive "
                << diskNumber;

            disk.model =
                fallbackModel.str();

            disk.type = "Drive";

            queryPhysicalDiskMetadata(
                disk
            );

            disk.formattedGB =
                getFormattedCapacityGB(
                    disk.driveLetters
                );

            if (
                disk.capacityGB <= 0.0 &&
                disk.formattedGB > 0.0
            )
            {
                disk.capacityGB =
                    disk.formattedGB;
            }

            // Ignore empty card readers / phantom physical-drive entries.
            if (
                disk.capacityGB <= 0.0 &&
                disk.driveLetters.empty()
            )
            {
                continue;
            }

            disk.systemDisk =
                containsDriveLetter(
                    disk.driveLetters,
                    windowsDrive
                );

            disk.pageFile =
                containsAnyDriveLetter(
                    disk.driveLetters,
                    pageFileDrives
                );

            auto oldIt =
                std::find_if(
                    diskStats.begin(),
                    diskStats.end(),
                    [&](const DiskStats& oldDisk)
                    {
                        return
                            oldDisk.diskNumber ==
                            disk.diskNumber;
                    }
                );

            if (oldIt !=
                diskStats.end())
            {
                copyDynamicDiskData(
                    *oldIt,
                    disk
                );
            }

            refreshed.push_back(
                disk
            );
        }

        std::sort(
            refreshed.begin(),
            refreshed.end(),
            [](const DiskStats& a,
               const DiskStats& b)
            {
                return
                    a.diskNumber <
                    b.diskNumber;
            }
        );

        diskStats =
            std::move(refreshed);
    }


    void pushDiskHistory(
        std::vector<double>& history,
        double value)
    {
        history.push_back(value);

        if (history.size() > 120)
        {
            history.erase(
                history.begin()
            );
        }
    }


    bool queryDiskPerformance(
        int diskNumber,
        DISK_PERFORMANCE& performance)
    {
        DWORD bytesReturned = 0;

        HANDLE handle =
            openPhysicalDrive(
                diskNumber,
                0
            );

        if (handle != INVALID_HANDLE_VALUE)
        {
            BOOL ok =
                DeviceIoControl(
                    handle,
                    IOCTL_DISK_PERFORMANCE,
                    nullptr,
                    0,
                    &performance,
                    sizeof(performance),
                    &bytesReturned,
                    nullptr
                );

            CloseHandle(handle);

            if (ok)
            {
                return true;
            }
        }

        handle =
            openPhysicalDrive(
                diskNumber,
                GENERIC_READ
            );

        if (handle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        BOOL ok =
            DeviceIoControl(
                handle,
                IOCTL_DISK_PERFORMANCE,
                nullptr,
                0,
                &performance,
                sizeof(performance),
                &bytesReturned,
                nullptr
            );

        CloseHandle(handle);

        return ok != FALSE;
    }


    void updateDiskPerformance()
    {
        ULONGLONG now =
            GetTickCount64();

        if (
            diskStats.empty() ||
            lastDiskEnumerationTick == 0 ||
            now - lastDiskEnumerationTick >=
                10000
        )
        {
            refreshDiskList();
            lastDiskEnumerationTick = now;
        }

        for (DiskStats& disk :
             diskStats)
        {
            DISK_PERFORMANCE performance = {};

            if (!queryDiskPerformance(
                    disk.diskNumber,
                    performance
                ))
            {
                disk.performanceValid =
                    false;
                continue;
            }

            DiskCounterState& previous =
                diskCounterStates[
                    disk.diskNumber
                ];

            if (!previous.initialized)
            {
                previous.initialized = true;
                previous.tickMs = now;
                previous.bytesRead =
                    performance.BytesRead.QuadPart;
                previous.bytesWritten =
                    performance.BytesWritten.QuadPart;
                previous.readTime =
                    performance.ReadTime.QuadPart;
                previous.writeTime =
                    performance.WriteTime.QuadPart;
                previous.idleTime =
                    performance.IdleTime.QuadPart;
                previous.readCount =
                    performance.ReadCount;
                previous.writeCount =
                    performance.WriteCount;

                disk.performanceValid =
                    false;

                pushDiskHistory(
                    disk.activeHistory,
                    0.0
                );

                pushDiskHistory(
                    disk.transferHistory,
                    0.0
                );

                continue;
            }

            ULONGLONG elapsedMs =
                now - previous.tickMs;

            LONGLONG bytesReadDiff =
                performance.BytesRead.QuadPart -
                previous.bytesRead;

            LONGLONG bytesWrittenDiff =
                performance.BytesWritten.QuadPart -
                previous.bytesWritten;

            LONGLONG readTimeDiff =
                performance.ReadTime.QuadPart -
                previous.readTime;

            LONGLONG writeTimeDiff =
                performance.WriteTime.QuadPart -
                previous.writeTime;

            LONGLONG idleTimeDiff =
                performance.IdleTime.QuadPart -
                previous.idleTime;

            DWORD readCountDiff =
                performance.ReadCount -
                previous.readCount;

            DWORD writeCountDiff =
                performance.WriteCount -
                previous.writeCount;

            previous.tickMs = now;
            previous.bytesRead =
                performance.BytesRead.QuadPart;
            previous.bytesWritten =
                performance.BytesWritten.QuadPart;
            previous.readTime =
                performance.ReadTime.QuadPart;
            previous.writeTime =
                performance.WriteTime.QuadPart;
            previous.idleTime =
                performance.IdleTime.QuadPart;
            previous.readCount =
                performance.ReadCount;
            previous.writeCount =
                performance.WriteCount;

            if (
                elapsedMs == 0 ||
                bytesReadDiff < 0 ||
                bytesWrittenDiff < 0 ||
                readTimeDiff < 0 ||
                writeTimeDiff < 0 ||
                idleTimeDiff < 0
            )
            {
                disk.performanceValid =
                    false;
                continue;
            }

            double elapsedSeconds =
                elapsedMs / 1000.0;

            disk.readMBps =
                static_cast<double>(
                    bytesReadDiff
                ) /
                bytesPerMB /
                elapsedSeconds;

            disk.writeMBps =
                static_cast<double>(
                    bytesWrittenDiff
                ) /
                bytesPerMB /
                elapsedSeconds;

            unsigned long long operationCount =
                static_cast<unsigned long long>(
                    readCountDiff
                ) +
                static_cast<unsigned long long>(
                    writeCountDiff
                );

            if (operationCount > 0)
            {
                double totalIoMilliseconds =
                    static_cast<double>(
                        readTimeDiff +
                        writeTimeDiff
                    ) / 10000.0;

                disk.averageResponseMs =
                    totalIoMilliseconds /
                    static_cast<double>(
                        operationCount
                    );
            }
            else
            {
                disk.averageResponseMs =
                    0.0;
            }

            bool noIo =
                bytesReadDiff == 0 &&
                bytesWrittenDiff == 0 &&
                operationCount == 0;

            if (noIo)
            {
                disk.activeTimePercent =
                    0.0;
            }
            else if (
                performance.IdleTime.QuadPart != 0 ||
                previous.idleTime != 0
            )
            {
                double idleMilliseconds =
                    static_cast<double>(
                        idleTimeDiff
                    ) / 10000.0;

                disk.activeTimePercent =
                    100.0 *
                    (
                        1.0 -
                        idleMilliseconds /
                        static_cast<double>(
                            elapsedMs
                        )
                    );
            }
            else
            {
                double busyMilliseconds =
                    static_cast<double>(
                        readTimeDiff +
                        writeTimeDiff
                    ) / 10000.0;

                disk.activeTimePercent =
                    100.0 *
                    busyMilliseconds /
                    static_cast<double>(
                        elapsedMs
                    );
            }

            disk.activeTimePercent =
                std::clamp(
                    disk.activeTimePercent,
                    0.0,
                    100.0
                );

            if (disk.readMBps < 0.0)
            {
                disk.readMBps = 0.0;
            }

            if (disk.writeMBps < 0.0)
            {
                disk.writeMBps = 0.0;
            }

            disk.performanceValid = true;

            pushDiskHistory(
                disk.activeHistory,
                disk.activeTimePercent
            );

            pushDiskHistory(
                disk.transferHistory,
                disk.readMBps +
                disk.writeMBps
            );
        }
    }
}


ULONGLONG fileTimeToULL(
    const FILETIME& ft)
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
            &userTime
        ))
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
    {
        return 0.0;
    }

    double usage =
        100.0 *
        (
            1.0 -
            static_cast<double>(
                idleDiff
            ) /
            static_cast<double>(
                total
            )
        );

    return std::clamp(
        usage,
        0.0,
        100.0
    );
}


void addCpuHistorySample()
{
    cpuHistory.push_back(
        cpuUsage
    );

    if (cpuHistory.size() > 120)
    {
        cpuHistory.erase(
            cpuHistory.begin()
        );
    }
}


void addRamHistorySample()
{
    ramHistory.push_back(
        static_cast<double>(
            ramPercent
        )
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
            &memory
        ))
    {
        totalRamGB =
            memory.ullTotalPhys /
            bytesPerGB;

        double available =
            memory.ullAvailPhys /
            bytesPerGB;

        usedRamGB =
            totalRamGB -
            available;

        if (totalRamGB > 0.0)
        {
            ramPercent =
                static_cast<int>(
                    (usedRamGB /
                     totalRamGB) *
                    100.0
                );
        }
    }

    addRamHistorySample();


    // Keep the original C: capacity values for the dashboard/widget.
    ULARGE_INTEGER freeAvailable = {};
    ULARGE_INTEGER totalBytes = {};
    ULARGE_INTEGER freeBytes = {};

    if (GetDiskFreeSpaceExA(
            "C:\\",
            &freeAvailable,
            &totalBytes,
            &freeBytes
        ))
    {
        totalDiskGB =
            totalBytes.QuadPart /
            bytesPerGB;

        double freeDiskGB =
            freeBytes.QuadPart /
            bytesPerGB;

        usedDiskGB =
            totalDiskGB -
            freeDiskGB;

        if (totalDiskGB > 0.0)
        {
            diskPercent =
                static_cast<int>(
                    (usedDiskGB /
                     totalDiskGB) *
                    100.0
                );
        }
    }


    // Physical-disk activity, transfer rate and metadata.
    updateDiskPerformance();


    // Uptime
    uptimeSeconds =
        GetTickCount64() / 1000;
}

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <winsock2.h>
#include <iphlpapi.h>
#include <setupapi.h>
#include <netioapi.h>
#include <tlhelp32.h>
#include "Stats.h"

#include <ws2tcpip.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <string>
#include <utility>
#include <vector>
#include <cstdlib>
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
std::vector<GpuStats> gpuStats;
std::vector<NetworkStats> networkStats;
std::vector<NetworkConnectionInfo> activeNetworkConnections;
SystemInfoData systemInfo;

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

    ULONGLONG lastSystemInfoDynamicTick = 0;


    std::string readRegistryString(
        HKEY root,
        const std::string& path,
        const char* valueName)
    {
        DWORD bytes = 0;

        LSTATUS status =
            RegGetValueA(
                root,
                path.c_str(),
                valueName,
                RRF_RT_REG_SZ |
                    RRF_RT_REG_EXPAND_SZ,
                nullptr,
                nullptr,
                &bytes
            );

        if (status != ERROR_SUCCESS ||
            bytes == 0)
        {
            return "";
        }

        std::vector<char> buffer(
            static_cast<size_t>(bytes) + 2,
            '\0'
        );

        status =
            RegGetValueA(
                root,
                path.c_str(),
                valueName,
                RRF_RT_REG_SZ |
                    RRF_RT_REG_EXPAND_SZ,
                nullptr,
                buffer.data(),
                &bytes
            );

        if (status != ERROR_SUCCESS)
        {
            return "";
        }

        return trimText(
            std::string(buffer.data())
        );
    }


    bool readRegistryDword(
        HKEY root,
        const std::string& path,
        const char* valueName,
        DWORD& value)
    {
        DWORD bytes = sizeof(value);

        return
            RegGetValueA(
                root,
                path.c_str(),
                valueName,
                RRF_RT_REG_DWORD,
                nullptr,
                &value,
                &bytes
            ) == ERROR_SUCCESS;
    }


    bool readRegistryQword(
        HKEY root,
        const std::string& path,
        const char* valueName,
        ULONGLONG& value)
    {
        DWORD bytes = sizeof(value);

        return
            RegGetValueA(
                root,
                path.c_str(),
                valueName,
                RRF_RT_REG_QWORD,
                nullptr,
                &value,
                &bytes
            ) == ERROR_SUCCESS;
    }


    std::string formatGigabytes(
        unsigned long long bytes)
    {
        if (bytes == 0)
        {
            return "--";
        }

        std::ostringstream stream;
        stream
            << std::fixed
            << std::setprecision(1)
            << (
                static_cast<double>(bytes) /
                bytesPerGB
            )
            << " GB";

        return stream.str();
    }


    std::string formatCacheBytes(
        unsigned long long bytes)
    {
        if (bytes == 0)
        {
            return "--";
        }

        std::ostringstream stream;

        if (bytes >= 1024ULL * 1024ULL)
        {
            stream
                << std::fixed
                << std::setprecision(1)
                << (
                    static_cast<double>(bytes) /
                    (1024.0 * 1024.0)
                )
                << " MB";
        }
        else
        {
            stream
                << (bytes / 1024ULL)
                << " KB";
        }

        return stream.str();
    }


    std::string formatMHzAsGHz(
        DWORD mhz)
    {
        if (mhz == 0)
        {
            return "--";
        }

        std::ostringstream stream;
        stream
            << std::fixed
            << std::setprecision(2)
            << (mhz / 1000.0)
            << " GHz";

        return stream.str();
    }


    std::string formatInstallDate(
        DWORD unixSeconds)
    {
        if (unixSeconds == 0)
        {
            return "--";
        }

        ULARGE_INTEGER value = {};
        value.QuadPart =
            (
                static_cast<ULONGLONG>(unixSeconds) +
                11644473600ULL
            ) *
            10000000ULL;

        FILETIME utc = {};
        utc.dwLowDateTime = value.LowPart;
        utc.dwHighDateTime = value.HighPart;

        FILETIME local = {};
        SYSTEMTIME systemTime = {};

        if (!FileTimeToLocalFileTime(
                &utc,
                &local
            ) ||
            !FileTimeToSystemTime(
                &local,
                &systemTime
            ))
        {
            return "--";
        }

        static const char* months[] =
        {
            "Jan", "Feb", "Mar", "Apr",
            "May", "Jun", "Jul", "Aug",
            "Sep", "Oct", "Nov", "Dec"
        };

        if (systemTime.wMonth < 1 ||
            systemTime.wMonth > 12)
        {
            return "--";
        }

        std::ostringstream stream;
        stream
            << months[systemTime.wMonth - 1]
            << " "
            << systemTime.wDay
            << ", "
            << systemTime.wYear;

        return stream.str();
    }


    std::string getSystemTypeText()
    {
        SYSTEM_INFO info = {};
        GetNativeSystemInfo(&info);

        switch (info.wProcessorArchitecture)
        {
        case PROCESSOR_ARCHITECTURE_AMD64:
            return
                "64-bit operating system, x64-based processor";

#ifdef PROCESSOR_ARCHITECTURE_ARM64
        case PROCESSOR_ARCHITECTURE_ARM64:
            return
                "64-bit operating system, ARM64-based processor";
#endif

        case PROCESSOR_ARCHITECTURE_INTEL:
            return
                "32-bit operating system, x86-based processor";

        default:
            return "Windows system";
        }
    }


    DWORD countCpuRelationship(
        LOGICAL_PROCESSOR_RELATIONSHIP relationship)
    {
        DWORD bufferSize = 0;

        GetLogicalProcessorInformationEx(
            relationship,
            nullptr,
            &bufferSize
        );

        if (bufferSize == 0)
        {
            return 0;
        }

        std::vector<BYTE> buffer(bufferSize);

        if (!GetLogicalProcessorInformationEx(
                relationship,
                reinterpret_cast<
                    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX
                >(buffer.data()),
                &bufferSize
            ))
        {
            return 0;
        }

        DWORD count = 0;
        BYTE* current = buffer.data();
        BYTE* end = buffer.data() + bufferSize;

        while (current < end)
        {
            auto* currentInfo =
                reinterpret_cast<
                    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX
                >(current);

            if (currentInfo->Size == 0)
            {
                break;
            }

            count++;
            current += currentInfo->Size;
        }

        return count;
    }


    void queryCpuCacheInfo()
    {
        unsigned long long l1Bytes = 0;
        unsigned long long l2Bytes = 0;
        unsigned long long l3Bytes = 0;

        DWORD bufferSize = 0;

        GetLogicalProcessorInformationEx(
            RelationCache,
            nullptr,
            &bufferSize
        );

        if (bufferSize == 0)
        {
            return;
        }

        std::vector<BYTE> buffer(bufferSize);

        if (!GetLogicalProcessorInformationEx(
                RelationCache,
                reinterpret_cast<
                    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX
                >(buffer.data()),
                &bufferSize
            ))
        {
            return;
        }

        BYTE* current = buffer.data();
        BYTE* end = buffer.data() + bufferSize;

        while (current < end)
        {
            auto* currentInfo =
                reinterpret_cast<
                    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX
                >(current);

            if (currentInfo->Size == 0)
            {
                break;
            }

            DWORD cacheSize =
                currentInfo->Cache.CacheSize;

            switch (currentInfo->Cache.Level)
            {
            case 1:
                l1Bytes += cacheSize;
                break;

            case 2:
                l2Bytes += cacheSize;
                break;

            case 3:
                l3Bytes += cacheSize;
                break;

            default:
                break;
            }

            current += currentInfo->Size;
        }

        systemInfo.l1Cache =
            formatCacheBytes(l1Bytes);
        systemInfo.l2Cache =
            formatCacheBytes(l2Bytes);
        systemInfo.l3Cache =
            formatCacheBytes(l3Bytes);
    }


    WORD readSmbiosWordValue(
        const BYTE* data)
    {
        return static_cast<WORD>(
            static_cast<WORD>(data[0]) |
            (
                static_cast<WORD>(data[1])
                << 8
            )
        );
    }


    std::string smbiosFormFactorName(
        BYTE formFactor)
    {
        switch (formFactor)
        {
        case 0x03: return "SIMM";
        case 0x04: return "SIP";
        case 0x05: return "Chip";
        case 0x06: return "DIP";
        case 0x07: return "ZIP";
        case 0x08: return "Card";
        case 0x09: return "DIMM";
        case 0x0A: return "TSOP";
        case 0x0B: return "Row of chips";
        case 0x0C: return "RIMM";
        case 0x0D: return "SODIMM";
        case 0x0E: return "SRIMM";
        case 0x0F: return "FB-DIMM";
        case 0x10: return "Die";
        default:   return "--";
        }
    }


    std::string smbiosMemoryTypeName(
        BYTE memoryType)
    {
        switch (memoryType)
        {
        case 0x12: return "DDR";
        case 0x13: return "DDR2";
        case 0x18: return "DDR3";
        case 0x1A: return "DDR4";
        case 0x1B: return "LPDDR";
        case 0x1C: return "LPDDR2";
        case 0x1D: return "LPDDR3";
        case 0x1E: return "LPDDR4";
        case 0x22: return "DDR5";
        case 0x23: return "LPDDR5";
        default:   return "--";
        }
    }


    std::string getSmbiosString(
        const BYTE* structure,
        BYTE formattedLength,
        const BYTE* recordEnd,
        BYTE stringIndex)
    {
        if (stringIndex == 0)
        {
            return "";
        }

        const char* current =
            reinterpret_cast<const char*>(
                structure + formattedLength
            );

        const char* end =
            reinterpret_cast<const char*>(
                recordEnd
            );

        BYTE currentIndex = 1;

        while (current < end &&
               *current != '\0')
        {
            size_t remaining =
                static_cast<size_t>(
                    end - current
                );

            size_t length =
                strnlen(
                    current,
                    remaining
                );

            if (length >= remaining)
            {
                break;
            }

            if (currentIndex == stringIndex)
            {
                return trimText(
                    std::string(
                        current,
                        length
                    )
                );
            }

            current += length + 1;
            currentIndex++;
        }

        return "";
    }


    void querySmbiosSystemInfo()
    {
        struct RawSmbiosHeader
        {
            BYTE used20CallingMethod;
            BYTE majorVersion;
            BYTE minorVersion;
            BYTE dmiRevision;
            DWORD length;
        };

        const DWORD provider =
            0x52534D42UL;

        UINT size =
            GetSystemFirmwareTable(
                provider,
                0,
                nullptr,
                0
            );

        if (size < sizeof(RawSmbiosHeader))
        {
            return;
        }

        std::vector<BYTE> buffer(size);

        if (GetSystemFirmwareTable(
                provider,
                0,
                buffer.data(),
                size
            ) != size)
        {
            return;
        }

        const auto* raw =
            reinterpret_cast<
                const RawSmbiosHeader*
            >(buffer.data());

        const BYTE* table =
            buffer.data() +
            sizeof(RawSmbiosHeader);

        size_t available =
            buffer.size() -
            sizeof(RawSmbiosHeader);

        size_t tableLength =
            (std::min)(
                static_cast<size_t>(
                    raw->length
                ),
                available
            );

        size_t offset = 0;
        int type17Count = 0;
        int usedSlots = 0;
        int totalSlots = 0;
        DWORD memorySpeed = 0;
        std::string memoryType = "--";
        std::string formFactor = "--";

        while (offset + 4 <= tableLength)
        {
            const BYTE* structure =
                table + offset;

            BYTE type = structure[0];
            BYTE length = structure[1];

            if (length < 4 ||
                offset + length > tableLength)
            {
                break;
            }

            size_t next =
                offset + length;

            while (
                next + 1 < tableLength &&
                !(
                    table[next] == 0 &&
                    table[next + 1] == 0
                )
            )
            {
                next++;
            }

            if (next + 1 >= tableLength)
            {
                break;
            }

            const BYTE* recordEnd =
                table + next + 2;

            if (type == 4 &&
                length >= 0x18)
            {
                std::string socket =
                    getSmbiosString(
                        structure,
                        length,
                        recordEnd,
                        structure[0x04]
                    );

                if (!socket.empty())
                {
                    systemInfo.cpuSocket =
                        socket;
                }

                WORD maxSpeed =
                    readSmbiosWordValue(
                        structure + 0x14
                    );

                WORD currentSpeed =
                    readSmbiosWordValue(
                        structure + 0x16
                    );

                if (maxSpeed > 0 &&
                    maxSpeed != 0xFFFF)
                {
                    systemInfo.cpuBaseSpeed =
                        formatMHzAsGHz(maxSpeed);
                }

                if (currentSpeed > 0 &&
                    currentSpeed != 0xFFFF)
                {
                    systemInfo.cpuCurrentSpeed =
                        formatMHzAsGHz(
                            currentSpeed
                        );
                }
            }
            else if (type == 16 &&
                     length >= 0x0F)
            {
                WORD count =
                    readSmbiosWordValue(
                        structure + 0x0D
                    );

                if (count > 0 &&
                    count != 0xFFFF)
                {
                    totalSlots =
                        (std::max)(
                            totalSlots,
                            static_cast<int>(count)
                        );
                }
            }
            else if (type == 17 &&
                     length >= 0x15)
            {
                type17Count++;

                WORD sizeField =
                    readSmbiosWordValue(
                        structure + 0x0C
                    );

                bool populated =
                    sizeField != 0 &&
                    sizeField != 0xFFFF;

                if (populated)
                {
                    usedSlots++;

                    if (formFactor == "--")
                    {
                        formFactor =
                            smbiosFormFactorName(
                                structure[0x0E]
                            );
                    }

                    if (memoryType == "--" &&
                        length > 0x12)
                    {
                        memoryType =
                            smbiosMemoryTypeName(
                                structure[0x12]
                            );
                    }

                    DWORD speed = 0;

                    if (length >= 0x22)
                    {
                        WORD configuredSpeed =
                            readSmbiosWordValue(
                                structure + 0x20
                            );

                        if (configuredSpeed > 0 &&
                            configuredSpeed != 0xFFFF)
                        {
                            speed = configuredSpeed;
                        }
                    }

                    if (speed == 0 &&
                        length >= 0x17)
                    {
                        WORD reportedSpeed =
                            readSmbiosWordValue(
                                structure + 0x15
                            );

                        if (reportedSpeed > 0 &&
                            reportedSpeed != 0xFFFF)
                        {
                            speed = reportedSpeed;
                        }
                    }

                    memorySpeed =
                        (std::max)(
                            memorySpeed,
                            speed
                        );
                }
            }

            if (type == 127)
            {
                break;
            }

            offset = next + 2;
        }

        if (totalSlots == 0)
        {
            totalSlots = type17Count;
        }

        systemInfo.memoryType =
            memoryType;
        systemInfo.memoryFormFactor =
            formFactor;

        if (memorySpeed > 0)
        {
            systemInfo.memorySpeed =
                std::to_string(memorySpeed) +
                " MT/s";
        }

        if (totalSlots > 0)
        {
            systemInfo.memorySlots =
                std::to_string(usedSlots) +
                " of " +
                std::to_string(totalSlots);
        }
    }


    struct SetupApiFunctions
    {
        using GetClassDevsFn =
            HDEVINFO (WINAPI *)(
                const GUID*,
                PCSTR,
                HWND,
                DWORD
            );

        using EnumDeviceInfoFn =
            BOOL (WINAPI *)(
                HDEVINFO,
                DWORD,
                PSP_DEVINFO_DATA
            );

        using GetPropertyFn =
            BOOL (WINAPI *)(
                HDEVINFO,
                PSP_DEVINFO_DATA,
                DWORD,
                PDWORD,
                PBYTE,
                DWORD,
                PDWORD
            );

        using GetDeviceInstanceIdFn =
            BOOL (WINAPI *)(
                HDEVINFO,
                PSP_DEVINFO_DATA,
                PSTR,
                DWORD,
                PDWORD
            );

        using DestroyFn =
            BOOL (WINAPI *)(HDEVINFO);

        HMODULE module = nullptr;
        GetClassDevsFn getClassDevs = nullptr;
        EnumDeviceInfoFn enumDeviceInfo = nullptr;
        GetPropertyFn getProperty = nullptr;
        GetDeviceInstanceIdFn getDeviceInstanceId = nullptr;
        DestroyFn destroy = nullptr;

        bool valid() const
        {
            return
                module != nullptr &&
                getClassDevs != nullptr &&
                enumDeviceInfo != nullptr &&
                getProperty != nullptr &&
                getDeviceInstanceId != nullptr &&
                destroy != nullptr;
        }
    };


    SetupApiFunctions& getSetupApi()
    {
        static SetupApiFunctions functions;
        static bool initialized = false;

        if (!initialized)
        {
            initialized = true;

            functions.module =
                LoadLibraryA("setupapi.dll");

            if (functions.module != nullptr)
            {
                functions.getClassDevs =
                    reinterpret_cast<
                        SetupApiFunctions::GetClassDevsFn
                    >(
                        GetProcAddress(
                            functions.module,
                            "SetupDiGetClassDevsA"
                        )
                    );

                functions.enumDeviceInfo =
                    reinterpret_cast<
                        SetupApiFunctions::EnumDeviceInfoFn
                    >(
                        GetProcAddress(
                            functions.module,
                            "SetupDiEnumDeviceInfo"
                        )
                    );

                functions.getProperty =
                    reinterpret_cast<
                        SetupApiFunctions::GetPropertyFn
                    >(
                        GetProcAddress(
                            functions.module,
                            "SetupDiGetDeviceRegistryPropertyA"
                        )
                    );

                functions.getDeviceInstanceId =
                    reinterpret_cast<
                        SetupApiFunctions::GetDeviceInstanceIdFn
                    >(
                        GetProcAddress(
                            functions.module,
                            "SetupDiGetDeviceInstanceIdA"
                        )
                    );

                functions.destroy =
                    reinterpret_cast<
                        SetupApiFunctions::DestroyFn
                    >(
                        GetProcAddress(
                            functions.module,
                            "SetupDiDestroyDeviceInfoList"
                        )
                    );
            }
        }

        return functions;
    }


    std::string getSetupDeviceProperty(
        SetupApiFunctions& setup,
        HDEVINFO deviceInfoSet,
        SP_DEVINFO_DATA& deviceInfo,
        DWORD property)
    {
        DWORD required = 0;
        DWORD type = 0;

        setup.getProperty(
            deviceInfoSet,
            &deviceInfo,
            property,
            &type,
            nullptr,
            0,
            &required
        );

        if (required == 0)
        {
            return "";
        }

        std::vector<BYTE> buffer(
            static_cast<size_t>(required) + 2,
            0
        );

        if (!setup.getProperty(
                deviceInfoSet,
                &deviceInfo,
                property,
                &type,
                buffer.data(),
                required,
                &required
            ))
        {
            return "";
        }

        return trimText(
            reinterpret_cast<char*>(
                buffer.data()
            )
        );
    }


    std::string toUpperCopy(
        std::string value);


    std::string getSetupDeviceInstanceId(
        SetupApiFunctions& setup,
        HDEVINFO deviceInfoSet,
        SP_DEVINFO_DATA& deviceInfo)
    {
        DWORD required = 0;

        setup.getDeviceInstanceId(
            deviceInfoSet,
            &deviceInfo,
            nullptr,
            0,
            &required
        );

        if (required == 0)
        {
            return "";
        }

        std::vector<char> buffer(
            static_cast<size_t>(required) + 1,
            '\0'
        );

        if (!setup.getDeviceInstanceId(
                deviceInfoSet,
                &deviceInfo,
                buffer.data(),
                static_cast<DWORD>(
                    buffer.size()
                ),
                &required
            ))
        {
            return "";
        }

        return trimText(
            buffer.data()
        );
    }


    std::string queryDeviceStatus(
        const SP_DEVINFO_DATA& deviceInfo)
    {
        using GetDevNodeStatusFn =
            ULONG (WINAPI *)(
                PULONG,
                PULONG,
                ULONG,
                ULONG
            );

        static HMODULE cfgMgrModule =
            LoadLibraryA(
                "cfgmgr32.dll"
            );

        static GetDevNodeStatusFn
            getDevNodeStatus =
                cfgMgrModule
                ? reinterpret_cast<
                    GetDevNodeStatusFn
                  >(
                    GetProcAddress(
                        cfgMgrModule,
                        "CM_Get_DevNode_Status"
                    )
                  )
                : nullptr;

        if (getDevNodeStatus == nullptr)
        {
            return "--";
        }

        ULONG status = 0;
        ULONG problem = 0;

        ULONG result =
            getDevNodeStatus(
                &status,
                &problem,
                static_cast<ULONG>(
                    deviceInfo.DevInst
                ),
                0
            );

        if (result != 0)
        {
            return "--";
        }

        if (problem == 0)
        {
            return "OK";
        }

        return
            "Problem code " +
            std::to_string(problem);
    }


    std::string extractPnPIdentifier(
        const std::string& hardwareId,
        const std::string& firstPrefix,
        const std::string& secondPrefix = "")
    {
        std::string upper =
            toUpperCopy(hardwareId);

        size_t position =
            upper.find(firstPrefix);

        size_t prefixLength =
            firstPrefix.size();

        if (
            position == std::string::npos &&
            !secondPrefix.empty()
        )
        {
            position =
                upper.find(secondPrefix);

            prefixLength =
                secondPrefix.size();
        }

        if (
            position == std::string::npos ||
            position + prefixLength >=
                upper.size()
        )
        {
            return "--";
        }

        size_t start =
            position + prefixLength;

        size_t end = start;

        while (
            end < upper.size() &&
            end - start < 8
        )
        {
            char c = upper[end];

            if (!std::isalnum(
                    static_cast<unsigned char>(c)
                ))
            {
                break;
            }

            end++;
        }

        if (end == start)
        {
            return "--";
        }

        return upper.substr(
            start,
            end - start
        );
    }


    std::string deviceConnectionType(
        const std::string& enumerator,
        const std::string& className)
    {
        std::string enumUpper =
            toUpperCopy(enumerator);

        std::string classUpper =
            toUpperCopy(className);

        if (
            enumUpper.find("USB") !=
            std::string::npos
        )
        {
            return "USB";
        }

        if (
            enumUpper.find("BTH") !=
                std::string::npos ||
            enumUpper.find("BLUETOOTH") !=
                std::string::npos
        )
        {
            return "Bluetooth";
        }

        if (
            enumUpper.find("HID") !=
            std::string::npos
        )
        {
            return "HID";
        }

        if (classUpper == "MONITOR")
        {
            return "Display";
        }

        if (
            classUpper == "MEDIA" ||
            classUpper == "AUDIOENDPOINT"
        )
        {
            return "Audio";
        }

        if (classUpper == "CAMERA")
        {
            return "Camera";
        }

        if (classUpper == "IMAGE")
        {
            return "Imaging";
        }

        if (classUpper == "PRINTER")
        {
            return "Printer";
        }

        if (
            classUpper == "KEYBOARD" ||
            classUpper == "MOUSE"
        )
        {
            return "HID";
        }

        if (!enumerator.empty())
        {
            return enumerator;
        }

        return "PnP";
    }


    std::string toUpperCopy(
        std::string value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(
                    std::toupper(c)
                );
            }
        );

        return value;
    }


    std::string findDisplayDriverKey()
    {
        SetupApiFunctions& setup =
            getSetupApi();

        if (!setup.valid())
        {
            return "";
        }

        HDEVINFO set =
            setup.getClassDevs(
                nullptr,
                nullptr,
                nullptr,
                DIGCF_PRESENT |
                    DIGCF_ALLCLASSES
            );

        if (set == INVALID_HANDLE_VALUE)
        {
            return "";
        }

        std::string result;

        for (DWORD index = 0;; index++)
        {
            SP_DEVINFO_DATA info = {};
            info.cbSize = sizeof(info);

            if (!setup.enumDeviceInfo(
                    set,
                    index,
                    &info
                ))
            {
                break;
            }

            std::string className =
                getSetupDeviceProperty(
                    setup,
                    set,
                    info,
                    SPDRP_CLASS
                );

            if (toUpperCopy(className) !=
                "DISPLAY")
            {
                continue;
            }

            result =
                getSetupDeviceProperty(
                    setup,
                    set,
                    info,
                    SPDRP_DRIVER
                );

            if (!result.empty())
            {
                break;
            }
        }

        setup.destroy(set);
        return result;
    }


    void queryGraphicsInfo()
    {
        DISPLAY_DEVICEA device = {};
        device.cb = sizeof(device);

        std::string registryPath;

        for (DWORD index = 0;
             EnumDisplayDevicesA(
                 nullptr,
                 index,
                 &device,
                 0
             );
             index++)
        {
            if (
                (device.StateFlags &
                 DISPLAY_DEVICE_MIRRORING_DRIVER) != 0
            )
            {
                device = {};
                device.cb = sizeof(device);
                continue;
            }

            if (device.DeviceString[0] != '\0')
            {
                systemInfo.gpuName =
                    trimText(
                        device.DeviceString
                    );

                registryPath =
                    device.DeviceKey;
                break;
            }

            device = {};
            device.cb = sizeof(device);
        }

        const std::string prefix =
            "\\Registry\\Machine\\";

        if (registryPath.size() >=
                prefix.size() &&
            _strnicmp(
                registryPath.c_str(),
                prefix.c_str(),
                prefix.size()
            ) == 0)
        {
            registryPath.erase(
                0,
                prefix.size()
            );
        }

        ULONGLONG videoMemory = 0;

        if (!registryPath.empty())
        {
            readRegistryQword(
                HKEY_LOCAL_MACHINE,
                registryPath,
                "HardwareInformation.qwMemorySize",
                videoMemory
            );

            if (videoMemory == 0)
            {
                DWORD memory32 = 0;

                if (readRegistryDword(
                        HKEY_LOCAL_MACHINE,
                        registryPath,
                        "HardwareInformation.MemorySize",
                        memory32
                    ))
                {
                    videoMemory = memory32;
                }
            }

            systemInfo.gpuDriverVersion =
                readRegistryString(
                    HKEY_LOCAL_MACHINE,
                    registryPath,
                    "DriverVersion"
                );

            systemInfo.gpuDriverDate =
                readRegistryString(
                    HKEY_LOCAL_MACHINE,
                    registryPath,
                    "DriverDate"
                );
        }

        if (videoMemory > 0)
        {
            systemInfo.gpuMemory =
                formatGigabytes(videoMemory);
        }

        if (systemInfo.gpuDriverVersion.empty() ||
            systemInfo.gpuDriverVersion == "--" ||
            systemInfo.gpuDriverDate.empty() ||
            systemInfo.gpuDriverDate == "--")
        {
            std::string driverKey =
                findDisplayDriverKey();

            if (!driverKey.empty())
            {
                std::string path =
                    "SYSTEM\\CurrentControlSet\\Control\\Class\\" +
                    driverKey;

                std::string version =
                    readRegistryString(
                        HKEY_LOCAL_MACHINE,
                        path,
                        "DriverVersion"
                    );

                std::string date =
                    readRegistryString(
                        HKEY_LOCAL_MACHINE,
                        path,
                        "DriverDate"
                    );

                if (!version.empty())
                {
                    systemInfo.gpuDriverVersion =
                        version;
                }

                if (!date.empty())
                {
                    systemInfo.gpuDriverDate =
                        date;
                }
            }
        }

        HMODULE directX =
            LoadLibraryA("d3d12.dll");

        if (directX != nullptr)
        {
            systemInfo.directXVersion = "12";
            FreeLibrary(directX);
        }
        else
        {
            directX =
                LoadLibraryA("d3d11.dll");

            if (directX != nullptr)
            {
                systemInfo.directXVersion = "11";
                FreeLibrary(directX);
            }
        }
    }


    // ============================================================
    // GPU PERFORMANCE / MULTI-GPU BACKEND
    // ============================================================

    ULONGLONG lastGpuEnumerationTick = 0;


    void pushGpuHistory(
        std::vector<double>& history,
        double value)
    {
        history.push_back(value);

        if (history.size() > 120)
        {
            history.erase(history.begin());
        }
    }


    std::string normalizeDisplayRegistryPath(
        std::string path)
    {
        const std::string prefix =
            "\\Registry\\Machine\\";

        if (
            path.size() >= prefix.size() &&
            _strnicmp(
                path.c_str(),
                prefix.c_str(),
                prefix.size()
            ) == 0
        )
        {
            path.erase(0, prefix.size());
        }

        return path;
    }


    std::string gpuVendorFromDeviceId(
        const std::string& deviceId,
        const std::string& name)
    {
        std::string upper =
            toUpperCopy(deviceId + " " + name);

        if (upper.find("VEN_10DE") !=
                std::string::npos ||
            upper.find("NVIDIA") !=
                std::string::npos)
        {
            return "NVIDIA";
        }

        if (upper.find("VEN_1002") !=
                std::string::npos ||
            upper.find("AMD") !=
                std::string::npos ||
            upper.find("RADEON") !=
                std::string::npos)
        {
            return "AMD";
        }

        if (upper.find("VEN_8086") !=
                std::string::npos ||
            upper.find("INTEL") !=
                std::string::npos)
        {
            return "Intel";
        }

        return "--";
    }


    std::string detectDirectXRuntime()
    {
        HMODULE module =
            LoadLibraryA("d3d12.dll");

        if (module != nullptr)
        {
            FreeLibrary(module);
            return "12";
        }

        module =
            LoadLibraryA("d3d11.dll");

        if (module != nullptr)
        {
            FreeLibrary(module);
            return "11";
        }

        return "--";
    }


    void enumerateGpuAdapters()
    {
        std::vector<GpuStats> oldStats =
            gpuStats;

        std::vector<GpuStats> refreshed;
        std::set<std::string> seen;

        MEMORYSTATUSEX memory = {};
        memory.dwLength = sizeof(memory);
        GlobalMemoryStatusEx(&memory);

        const std::string directX =
            detectDirectXRuntime();

        for (DWORD displayIndex = 0;;
             displayIndex++)
        {
            DISPLAY_DEVICEA device = {};
            device.cb = sizeof(device);

            if (!EnumDisplayDevicesA(
                    nullptr,
                    displayIndex,
                    &device,
                    0
                ))
            {
                break;
            }

            if (
                (device.StateFlags &
                 DISPLAY_DEVICE_MIRRORING_DRIVER) != 0
            )
            {
                continue;
            }

            std::string name =
                trimText(device.DeviceString);

            if (name.empty())
            {
                continue;
            }

            std::string upperName =
                toUpperCopy(name);

            if (
                upperName.find(
                    "MICROSOFT BASIC DISPLAY"
                ) != std::string::npos ||
                upperName.find(
                    "REMOTE DISPLAY"
                ) != std::string::npos
            )
            {
                continue;
            }

            // EnumDisplayDevices can expose more than one logical
            // display entry for the same physical GPU. DeviceName is
            // different for those entries, so using DeviceName in the
            // identity makes one card appear multiple times.
            //
            // The Control\Video GUID in DeviceKey identifies the actual
            // adapter much more reliably. Strip the final \0000/\0001
            // driver sub-key so multiple logical heads of one GPU collapse
            // into a single physical adapter, while two separate physical
            // GPUs still keep separate GUIDs.
            std::string registryPath =
                normalizeDisplayRegistryPath(
                    device.DeviceKey
                );

            std::string adapterIdentity =
                registryPath;

            if (!adapterIdentity.empty())
            {
                size_t lastSlash =
                    adapterIdentity.find_last_of(
                        "\\/"
                    );

                if (lastSlash !=
                    std::string::npos)
                {
                    adapterIdentity.erase(
                        lastSlash
                    );
                }
            }

            if (adapterIdentity.empty())
            {
                adapterIdentity =
                    std::string(device.DeviceID);
            }

            if (adapterIdentity.empty())
            {
                adapterIdentity =
                    std::string(device.DeviceName) +
                    "|" +
                    name;
            }

            std::string stableId =
                toUpperCopy(adapterIdentity);

            if (!seen.insert(stableId).second)
            {
                continue;
            }

            GpuStats gpu;
            gpu.index =
                static_cast<int>(refreshed.size());
            gpu.physicalIndex = gpu.index;
            gpu.stableId = stableId;
            gpu.name = name;
            gpu.vendor =
                gpuVendorFromDeviceId(
                    device.DeviceID,
                    name
                );
            gpu.directXVersion = directX;

            if (!registryPath.empty())
            {
                ULONGLONG videoMemory = 0;

                readRegistryQword(
                    HKEY_LOCAL_MACHINE,
                    registryPath,
                    "HardwareInformation.qwMemorySize",
                    videoMemory
                );

                if (videoMemory == 0)
                {
                    DWORD memory32 = 0;

                    if (readRegistryDword(
                            HKEY_LOCAL_MACHINE,
                            registryPath,
                            "HardwareInformation.MemorySize",
                            memory32
                        ))
                    {
                        videoMemory = memory32;
                    }
                }

                gpu.dedicatedMemoryTotalBytes =
                    videoMemory;

                std::string version =
                    readRegistryString(
                        HKEY_LOCAL_MACHINE,
                        registryPath,
                        "DriverVersion"
                    );

                std::string date =
                    readRegistryString(
                        HKEY_LOCAL_MACHINE,
                        registryPath,
                        "DriverDate"
                    );

                if (!version.empty())
                {
                    gpu.driverVersion = version;
                }

                if (!date.empty())
                {
                    gpu.driverDate = date;
                }
            }

            if (memory.ullTotalPhys > 0)
            {
                gpu.sharedMemoryTotalBytes =
                    static_cast<unsigned long long>(
                        memory.ullTotalPhys / 2ULL
                    );
            }

            for (const GpuStats& oldGpu :
                 oldStats)
            {
                if (oldGpu.stableId ==
                    gpu.stableId)
                {
                    gpu.utilizationHistory =
                        oldGpu.utilizationHistory;
                    gpu.dedicatedMemoryHistory =
                        oldGpu.dedicatedMemoryHistory;
                    gpu.sharedMemoryHistory =
                        oldGpu.sharedMemoryHistory;
                    gpu.encodeHistory =
                        oldGpu.encodeHistory;
                    gpu.decodeHistory =
                        oldGpu.decodeHistory;

                    gpu.busInterface =
                        oldGpu.busInterface;
                    gpu.computeCores =
                        oldGpu.computeCores;
                    gpu.hardwareReservedMemory =
                        oldGpu.hardwareReservedMemory;
                    break;
                }
            }

            refreshed.push_back(gpu);
        }

        gpuStats = std::move(refreshed);
    }


    int parseGpuPhysicalIndex(
        const std::wstring& instance)
    {
        size_t position =
            instance.find(L"phys_");

        if (position == std::wstring::npos)
        {
            return -1;
        }

        position += 5;

        int value = 0;
        bool found = false;

        while (
            position < instance.size() &&
            instance[position] >= L'0' &&
            instance[position] <= L'9'
        )
        {
            found = true;
            value =
                value * 10 +
                static_cast<int>(
                    instance[position] - L'0'
                );
            position++;
        }

        return found ? value : -1;
    }


    struct PdhFunctions
    {
        using OpenQueryFn =
            PDH_STATUS (WINAPI *)(
                LPCWSTR,
                DWORD_PTR,
                PDH_HQUERY*
            );

        using AddEnglishCounterFn =
            PDH_STATUS (WINAPI *)(
                PDH_HQUERY,
                LPCWSTR,
                DWORD_PTR,
                PDH_HCOUNTER*
            );

        using CollectQueryDataFn =
            PDH_STATUS (WINAPI *)(
                PDH_HQUERY
            );

        using GetFormattedCounterArrayFn =
            PDH_STATUS (WINAPI *)(
                PDH_HCOUNTER,
                DWORD,
                LPDWORD,
                LPDWORD,
                PPDH_FMT_COUNTERVALUE_ITEM_W
            );

        using CloseQueryFn =
            PDH_STATUS (WINAPI *)(
                PDH_HQUERY
            );

        HMODULE module = nullptr;
        OpenQueryFn openQuery = nullptr;
        AddEnglishCounterFn addEnglishCounter = nullptr;
        CollectQueryDataFn collectQueryData = nullptr;
        GetFormattedCounterArrayFn getFormattedCounterArray = nullptr;
        CloseQueryFn closeQuery = nullptr;

        bool valid() const
        {
            return
                module != nullptr &&
                openQuery != nullptr &&
                addEnglishCounter != nullptr &&
                collectQueryData != nullptr &&
                getFormattedCounterArray != nullptr &&
                closeQuery != nullptr;
        }
    };


    PdhFunctions& getPdhFunctions()
    {
        static PdhFunctions functions;
        static bool initialized = false;

        if (!initialized)
        {
            initialized = true;
            functions.module =
                LoadLibraryA("pdh.dll");

            if (functions.module != nullptr)
            {
                functions.openQuery =
                    reinterpret_cast<
                        PdhFunctions::OpenQueryFn
                    >(
                        GetProcAddress(
                            functions.module,
                            "PdhOpenQueryW"
                        )
                    );

                functions.addEnglishCounter =
                    reinterpret_cast<
                        PdhFunctions::AddEnglishCounterFn
                    >(
                        GetProcAddress(
                            functions.module,
                            "PdhAddEnglishCounterW"
                        )
                    );

                functions.collectQueryData =
                    reinterpret_cast<
                        PdhFunctions::CollectQueryDataFn
                    >(
                        GetProcAddress(
                            functions.module,
                            "PdhCollectQueryData"
                        )
                    );

                functions.getFormattedCounterArray =
                    reinterpret_cast<
                        PdhFunctions::GetFormattedCounterArrayFn
                    >(
                        GetProcAddress(
                            functions.module,
                            "PdhGetFormattedCounterArrayW"
                        )
                    );

                functions.closeQuery =
                    reinterpret_cast<
                        PdhFunctions::CloseQueryFn
                    >(
                        GetProcAddress(
                            functions.module,
                            "PdhCloseQuery"
                        )
                    );
            }
        }

        return functions;
    }


    struct GpuPdhState
    {
        bool initialized = false;
        PDH_HQUERY query = nullptr;
        PDH_HCOUNTER engine = nullptr;
        PDH_HCOUNTER dedicated = nullptr;
        PDH_HCOUNTER shared = nullptr;
    };


    GpuPdhState& getGpuPdhState()
    {
        static GpuPdhState state;

        if (state.initialized)
        {
            return state;
        }

        state.initialized = true;

        PdhFunctions& pdh =
            getPdhFunctions();

        if (!pdh.valid())
        {
            return state;
        }

        if (pdh.openQuery(
                nullptr,
                0,
                &state.query
            ) != ERROR_SUCCESS)
        {
            state.query = nullptr;
            return state;
        }

        if (pdh.addEnglishCounter(
                state.query,
                L"\\GPU Engine(*)\\Utilization Percentage",
                0,
                &state.engine
            ) != ERROR_SUCCESS)
        {
            state.engine = nullptr;
        }

        if (pdh.addEnglishCounter(
                state.query,
                L"\\GPU Adapter Memory(*)\\Dedicated Usage",
                0,
                &state.dedicated
            ) != ERROR_SUCCESS)
        {
            state.dedicated = nullptr;
        }

        if (pdh.addEnglishCounter(
                state.query,
                L"\\GPU Adapter Memory(*)\\Shared Usage",
                0,
                &state.shared
            ) != ERROR_SUCCESS)
        {
            state.shared = nullptr;
        }

        return state;
    }


    std::vector<std::pair<std::wstring, double>>
    readPdhCounterArray(
        PDH_HCOUNTER counter)
    {
        std::vector<std::pair<std::wstring, double>>
            result;

        if (counter == nullptr)
        {
            return result;
        }

        PdhFunctions& pdh =
            getPdhFunctions();

        if (!pdh.valid())
        {
            return result;
        }

        DWORD bytes = 0;
        DWORD itemCount = 0;

        PDH_STATUS status =
            pdh.getFormattedCounterArray(
                counter,
                PDH_FMT_DOUBLE,
                &bytes,
                &itemCount,
                nullptr
            );

        if (
            status != PDH_MORE_DATA ||
            bytes == 0 ||
            itemCount == 0
        )
        {
            return result;
        }

        std::vector<BYTE> buffer(bytes);

        auto* items =
            reinterpret_cast<
                PPDH_FMT_COUNTERVALUE_ITEM_W
            >(
                buffer.data()
            );

        status =
            pdh.getFormattedCounterArray(
                counter,
                PDH_FMT_DOUBLE,
                &bytes,
                &itemCount,
                items
            );

        if (status != ERROR_SUCCESS)
        {
            return result;
        }

        for (DWORD index = 0;
             index < itemCount;
             index++)
        {
            if (
                items[index].FmtValue.CStatus !=
                    PDH_CSTATUS_VALID_DATA &&
                items[index].FmtValue.CStatus !=
                    PDH_CSTATUS_NEW_DATA
            )
            {
                continue;
            }

            if (items[index].szName == nullptr)
            {
                continue;
            }

            double value =
                items[index].FmtValue.doubleValue;

            if (value < 0.0)
            {
                value = 0.0;
            }

            result.push_back(
                {
                    items[index].szName,
                    value
                }
            );
        }

        return result;
    }


    void updateGpuPdhMetrics()
    {
        if (gpuStats.empty())
        {
            return;
        }

        GpuPdhState& state =
            getGpuPdhState();

        PdhFunctions& pdh =
            getPdhFunctions();

        if (
            state.query == nullptr ||
            !pdh.valid()
        )
        {
            return;
        }

        if (pdh.collectQueryData(
                state.query
            ) != ERROR_SUCCESS)
        {
            return;
        }

        std::map<int, double> threeD;
        std::map<int, double> encode;
        std::map<int, double> decode;
        std::map<int, double> allEngines;
        std::map<int, double> dedicated;
        std::map<int, double> shared;

        for (const auto& item :
             readPdhCounterArray(
                 state.engine
             ))
        {
            int physicalIndex =
                parseGpuPhysicalIndex(
                    item.first
                );

            if (physicalIndex < 0)
            {
                continue;
            }

            const std::wstring& name =
                item.first;

            allEngines[physicalIndex] +=
                item.second;

            if (name.find(
                    L"engtype_3D"
                ) != std::wstring::npos)
            {
                threeD[physicalIndex] +=
                    item.second;
            }
            else if (
                name.find(
                    L"engtype_VideoEncode"
                ) != std::wstring::npos
            )
            {
                encode[physicalIndex] +=
                    item.second;
            }
            else if (
                name.find(
                    L"engtype_VideoDecode"
                ) != std::wstring::npos
            )
            {
                decode[physicalIndex] +=
                    item.second;
            }
        }

        for (const auto& item :
             readPdhCounterArray(
                 state.dedicated
             ))
        {
            int physicalIndex =
                parseGpuPhysicalIndex(
                    item.first
                );

            if (physicalIndex >= 0)
            {
                dedicated[physicalIndex] +=
                    item.second;
            }
        }

        for (const auto& item :
             readPdhCounterArray(
                 state.shared
             ))
        {
            int physicalIndex =
                parseGpuPhysicalIndex(
                    item.first
                );

            if (physicalIndex >= 0)
            {
                shared[physicalIndex] +=
                    item.second;
            }
        }

        for (GpuStats& gpu : gpuStats)
        {
            int physicalIndex =
                gpu.physicalIndex;

            bool hasPerformanceData = false;

            auto threeDIt =
                threeD.find(physicalIndex);

            if (threeDIt != threeD.end())
            {
                gpu.utilizationPercent =
                    std::clamp(
                        threeDIt->second,
                        0.0,
                        100.0
                    );
                hasPerformanceData = true;
            }
            else
            {
                auto allIt =
                    allEngines.find(
                        physicalIndex
                    );

                if (allIt != allEngines.end())
                {
                    gpu.utilizationPercent =
                        std::clamp(
                            allIt->second,
                            0.0,
                            100.0
                        );
                    hasPerformanceData = true;
                }
            }

            auto encodeIt =
                encode.find(physicalIndex);

            if (encodeIt != encode.end())
            {
                gpu.encodePercent =
                    std::clamp(
                        encodeIt->second,
                        0.0,
                        100.0
                    );
                hasPerformanceData = true;
            }
            else
            {
                gpu.encodePercent = 0.0;
            }

            auto decodeIt =
                decode.find(physicalIndex);

            if (decodeIt != decode.end())
            {
                gpu.decodePercent =
                    std::clamp(
                        decodeIt->second,
                        0.0,
                        100.0
                    );
                hasPerformanceData = true;
            }
            else
            {
                gpu.decodePercent = 0.0;
            }

            auto dedicatedIt =
                dedicated.find(physicalIndex);

            if (dedicatedIt != dedicated.end())
            {
                gpu.dedicatedMemoryUsedBytes =
                    static_cast<unsigned long long>(
                        dedicatedIt->second
                    );
                hasPerformanceData = true;
            }

            auto sharedIt =
                shared.find(physicalIndex);

            if (sharedIt != shared.end())
            {
                gpu.sharedMemoryUsedBytes =
                    static_cast<unsigned long long>(
                        sharedIt->second
                    );
                hasPerformanceData = true;
            }

            gpu.performanceValid =
                hasPerformanceData;
        }
    }


    // ------------------------------------------------------------
    // Optional NVIDIA NVML enrichment.
    // Generic GPU discovery/usage still works without NVML.  NVML
    // is only used for vendor sensor data such as temperature/fan/
    // power and extra PCIe information when an NVIDIA driver exposes
    // the library.
    // ------------------------------------------------------------

    struct nvmlDevice_st;
    using nvmlDevice_t = nvmlDevice_st*;
    using nvmlReturn_t = int;

    struct NvmlUtilization
    {
        unsigned int gpu = 0;
        unsigned int memory = 0;
    };

    struct NvmlMemory
    {
        unsigned long long total = 0;
        unsigned long long free = 0;
        unsigned long long used = 0;
    };

    struct NvmlFunctions
    {
        using InitFn = nvmlReturn_t (*)();
        using GetCountFn =
            nvmlReturn_t (*)(unsigned int*);
        using GetHandleFn =
            nvmlReturn_t (*)(
                unsigned int,
                nvmlDevice_t*
            );
        using GetNameFn =
            nvmlReturn_t (*)(
                nvmlDevice_t,
                char*,
                unsigned int
            );
        using GetTemperatureFn =
            nvmlReturn_t (*)(
                nvmlDevice_t,
                int,
                unsigned int*
            );
        using GetFanSpeedFn =
            nvmlReturn_t (*)(
                nvmlDevice_t,
                unsigned int*
            );
        using GetPowerUsageFn =
            nvmlReturn_t (*)(
                nvmlDevice_t,
                unsigned int*
            );
        using GetPowerLimitFn =
            nvmlReturn_t (*)(
                nvmlDevice_t,
                unsigned int*
            );
        using GetUtilizationFn =
            nvmlReturn_t (*)(
                nvmlDevice_t,
                NvmlUtilization*
            );
        using GetMemoryFn =
            nvmlReturn_t (*)(
                nvmlDevice_t,
                NvmlMemory*
            );
        using GetCodecUtilizationFn =
            nvmlReturn_t (*)(
                nvmlDevice_t,
                unsigned int*,
                unsigned int*
            );
        using GetPcieValueFn =
            nvmlReturn_t (*)(
                nvmlDevice_t,
                unsigned int*
            );
        using GetNumCoresFn =
            nvmlReturn_t (*)(
                nvmlDevice_t,
                unsigned int*
            );
        using GetDriverVersionFn =
            nvmlReturn_t (*)(
                char*,
                unsigned int
            );

        HMODULE module = nullptr;
        bool initialized = false;

        InitFn init = nullptr;
        GetCountFn getCount = nullptr;
        GetHandleFn getHandle = nullptr;
        GetNameFn getName = nullptr;
        GetTemperatureFn getTemperature = nullptr;
        GetFanSpeedFn getFanSpeed = nullptr;
        GetPowerUsageFn getPowerUsage = nullptr;
        GetPowerLimitFn getPowerLimit = nullptr;
        GetUtilizationFn getUtilization = nullptr;
        GetMemoryFn getMemory = nullptr;
        GetCodecUtilizationFn getEncoderUtilization = nullptr;
        GetCodecUtilizationFn getDecoderUtilization = nullptr;
        GetPcieValueFn getPcieGeneration = nullptr;
        GetPcieValueFn getPcieWidth = nullptr;
        GetNumCoresFn getNumCores = nullptr;
        GetDriverVersionFn getDriverVersion = nullptr;

        bool valid() const
        {
            return
                initialized &&
                getCount != nullptr &&
                getHandle != nullptr &&
                getName != nullptr;
        }
    };


    FARPROC nvmlProc(
        HMODULE module,
        const char* name)
    {
        return module != nullptr
            ? GetProcAddress(module, name)
            : nullptr;
    }


    NvmlFunctions& getNvmlFunctions()
    {
        static NvmlFunctions nvml;
        static bool attempted = false;

        if (attempted)
        {
            return nvml;
        }

        attempted = true;
        nvml.module =
            LoadLibraryA("nvml.dll");

        if (nvml.module == nullptr)
        {
            const char* programFiles =
                std::getenv("ProgramW6432");

            if (programFiles == nullptr)
            {
                programFiles =
                    std::getenv("ProgramFiles");
            }

            if (programFiles != nullptr)
            {
                std::string path =
                    std::string(programFiles) +
                    "\\NVIDIA Corporation\\NVSMI\\nvml.dll";

                nvml.module =
                    LoadLibraryA(path.c_str());
            }
        }

        if (nvml.module == nullptr)
        {
            return nvml;
        }

        nvml.init =
            reinterpret_cast<NvmlFunctions::InitFn>(
                nvmlProc(
                    nvml.module,
                    "nvmlInit_v2"
                )
            );
        nvml.getCount =
            reinterpret_cast<NvmlFunctions::GetCountFn>(
                nvmlProc(
                    nvml.module,
                    "nvmlDeviceGetCount_v2"
                )
            );
        nvml.getHandle =
            reinterpret_cast<NvmlFunctions::GetHandleFn>(
                nvmlProc(
                    nvml.module,
                    "nvmlDeviceGetHandleByIndex_v2"
                )
            );
        nvml.getName =
            reinterpret_cast<NvmlFunctions::GetNameFn>(
                nvmlProc(
                    nvml.module,
                    "nvmlDeviceGetName"
                )
            );
        nvml.getTemperature =
            reinterpret_cast<NvmlFunctions::GetTemperatureFn>(
                nvmlProc(
                    nvml.module,
                    "nvmlDeviceGetTemperature"
                )
            );
        nvml.getFanSpeed =
            reinterpret_cast<NvmlFunctions::GetFanSpeedFn>(
                nvmlProc(
                    nvml.module,
                    "nvmlDeviceGetFanSpeed"
                )
            );
        nvml.getPowerUsage =
            reinterpret_cast<NvmlFunctions::GetPowerUsageFn>(
                nvmlProc(
                    nvml.module,
                    "nvmlDeviceGetPowerUsage"
                )
            );
        nvml.getPowerLimit =
            reinterpret_cast<NvmlFunctions::GetPowerLimitFn>(
                nvmlProc(
                    nvml.module,
                    "nvmlDeviceGetPowerManagementLimit"
                )
            );
        nvml.getUtilization =
            reinterpret_cast<NvmlFunctions::GetUtilizationFn>(
                nvmlProc(
                    nvml.module,
                    "nvmlDeviceGetUtilizationRates"
                )
            );
        nvml.getMemory =
            reinterpret_cast<NvmlFunctions::GetMemoryFn>(
                nvmlProc(
                    nvml.module,
                    "nvmlDeviceGetMemoryInfo"
                )
            );
        nvml.getEncoderUtilization =
            reinterpret_cast<NvmlFunctions::GetCodecUtilizationFn>(
                nvmlProc(
                    nvml.module,
                    "nvmlDeviceGetEncoderUtilization"
                )
            );
        nvml.getDecoderUtilization =
            reinterpret_cast<NvmlFunctions::GetCodecUtilizationFn>(
                nvmlProc(
                    nvml.module,
                    "nvmlDeviceGetDecoderUtilization"
                )
            );
        nvml.getPcieGeneration =
            reinterpret_cast<NvmlFunctions::GetPcieValueFn>(
                nvmlProc(
                    nvml.module,
                    "nvmlDeviceGetCurrPcieLinkGeneration"
                )
            );
        nvml.getPcieWidth =
            reinterpret_cast<NvmlFunctions::GetPcieValueFn>(
                nvmlProc(
                    nvml.module,
                    "nvmlDeviceGetCurrPcieLinkWidth"
                )
            );
        nvml.getNumCores =
            reinterpret_cast<NvmlFunctions::GetNumCoresFn>(
                nvmlProc(
                    nvml.module,
                    "nvmlDeviceGetNumGpuCores"
                )
            );
        nvml.getDriverVersion =
            reinterpret_cast<NvmlFunctions::GetDriverVersionFn>(
                nvmlProc(
                    nvml.module,
                    "nvmlSystemGetDriverVersion"
                )
            );

        if (
            nvml.init != nullptr &&
            nvml.init() == 0
        )
        {
            nvml.initialized = true;
        }

        return nvml;
    }


    bool gpuNamesMatch(
        const std::string& left,
        const std::string& right)
    {
        std::string a = toUpperCopy(left);
        std::string b = toUpperCopy(right);

        return
            a == b ||
            a.find(b) != std::string::npos ||
            b.find(a) != std::string::npos;
    }


    void enrichGpuWithNvml()
    {
        NvmlFunctions& nvml =
            getNvmlFunctions();

        if (!nvml.valid())
        {
            return;
        }

        unsigned int count = 0;

        if (nvml.getCount(&count) != 0 ||
            count == 0)
        {
            return;
        }

        struct NvmlDeviceEntry
        {
            nvmlDevice_t handle = nullptr;
            std::string name;
            bool used = false;
        };

        std::vector<NvmlDeviceEntry> devices;

        for (unsigned int index = 0;
             index < count;
             index++)
        {
            nvmlDevice_t handle = nullptr;

            if (nvml.getHandle(
                    index,
                    &handle
                ) != 0 ||
                handle == nullptr)
            {
                continue;
            }

            char name[128] = {};
            nvml.getName(
                handle,
                name,
                sizeof(name)
            );

            NvmlDeviceEntry entry;
            entry.handle = handle;
            entry.name = trimText(name);
            devices.push_back(entry);
        }

        char globalDriver[96] = {};
        std::string globalDriverVersion;

        if (
            nvml.getDriverVersion != nullptr &&
            nvml.getDriverVersion(
                globalDriver,
                sizeof(globalDriver)
            ) == 0
        )
        {
            globalDriverVersion =
                trimText(globalDriver);
        }

        for (GpuStats& gpu : gpuStats)
        {
            if (gpu.vendor != "NVIDIA")
            {
                continue;
            }

            int match = -1;

            for (size_t index = 0;
                 index < devices.size();
                 index++)
            {
                if (
                    !devices[index].used &&
                    gpuNamesMatch(
                        gpu.name,
                        devices[index].name
                    )
                )
                {
                    match =
                        static_cast<int>(index);
                    break;
                }
            }

            if (match < 0)
            {
                for (size_t index = 0;
                     index < devices.size();
                     index++)
                {
                    if (!devices[index].used)
                    {
                        match =
                            static_cast<int>(index);
                        break;
                    }
                }
            }

            if (match < 0)
            {
                continue;
            }

            devices[match].used = true;
            nvmlDevice_t device =
                devices[match].handle;

            unsigned int value = 0;

            if (
                nvml.getTemperature != nullptr &&
                nvml.getTemperature(
                    device,
                    0,
                    &value
                ) == 0
            )
            {
                gpu.temperatureC =
                    static_cast<double>(value);
            }

            value = 0;
            if (
                nvml.getFanSpeed != nullptr &&
                nvml.getFanSpeed(
                    device,
                    &value
                ) == 0
            )
            {
                gpu.fanPercent =
                    static_cast<int>(value);
            }

            value = 0;
            if (
                nvml.getPowerUsage != nullptr &&
                nvml.getPowerUsage(
                    device,
                    &value
                ) == 0
            )
            {
                gpu.powerW =
                    value / 1000.0;
            }

            value = 0;
            if (
                nvml.getPowerLimit != nullptr &&
                nvml.getPowerLimit(
                    device,
                    &value
                ) == 0
            )
            {
                gpu.powerLimitW =
                    value / 1000.0;
            }

            NvmlUtilization utilization = {};
            if (
                nvml.getUtilization != nullptr &&
                nvml.getUtilization(
                    device,
                    &utilization
                ) == 0 &&
                !gpu.performanceValid
            )
            {
                gpu.utilizationPercent =
                    static_cast<double>(
                        utilization.gpu
                    );
                gpu.performanceValid = true;
            }

            NvmlMemory memoryInfo = {};
            if (
                nvml.getMemory != nullptr &&
                nvml.getMemory(
                    device,
                    &memoryInfo
                ) == 0
            )
            {
                if (
                    gpu.dedicatedMemoryTotalBytes == 0
                )
                {
                    gpu.dedicatedMemoryTotalBytes =
                        memoryInfo.total;
                }

                if (
                    gpu.dedicatedMemoryUsedBytes == 0
                )
                {
                    gpu.dedicatedMemoryUsedBytes =
                        memoryInfo.used;
                }
            }

            unsigned int utilizationValue = 0;
            unsigned int samplingPeriod = 0;

            if (
                nvml.getEncoderUtilization != nullptr &&
                nvml.getEncoderUtilization(
                    device,
                    &utilizationValue,
                    &samplingPeriod
                ) == 0 &&
                gpu.encodePercent <= 0.0
            )
            {
                gpu.encodePercent =
                    static_cast<double>(
                        utilizationValue
                    );
            }

            utilizationValue = 0;
            samplingPeriod = 0;

            if (
                nvml.getDecoderUtilization != nullptr &&
                nvml.getDecoderUtilization(
                    device,
                    &utilizationValue,
                    &samplingPeriod
                ) == 0 &&
                gpu.decodePercent <= 0.0
            )
            {
                gpu.decodePercent =
                    static_cast<double>(
                        utilizationValue
                    );
            }

            unsigned int generation = 0;
            unsigned int width = 0;

            if (
                nvml.getPcieGeneration != nullptr &&
                nvml.getPcieWidth != nullptr &&
                nvml.getPcieGeneration(
                    device,
                    &generation
                ) == 0 &&
                nvml.getPcieWidth(
                    device,
                    &width
                ) == 0 &&
                generation > 0 &&
                width > 0
            )
            {
                gpu.busInterface =
                    "PCIe " +
                    std::to_string(generation) +
                    ".0 x" +
                    std::to_string(width);
            }

            unsigned int cores = 0;

            if (
                nvml.getNumCores != nullptr &&
                nvml.getNumCores(
                    device,
                    &cores
                ) == 0 &&
                cores > 0
            )
            {
                gpu.computeCores =
                    std::to_string(cores);
            }

            if (
                (gpu.driverVersion.empty() ||
                 gpu.driverVersion == "--") &&
                !globalDriverVersion.empty()
            )
            {
                gpu.driverVersion =
                    globalDriverVersion;
            }
        }
    }


    void sampleGpuHistories()
    {
        for (GpuStats& gpu : gpuStats)
        {
            pushGpuHistory(
                gpu.utilizationHistory,
                gpu.utilizationPercent
            );

            pushGpuHistory(
                gpu.dedicatedMemoryHistory,
                gpu.dedicatedMemoryUsedBytes /
                    bytesPerGB
            );

            pushGpuHistory(
                gpu.sharedMemoryHistory,
                gpu.sharedMemoryUsedBytes /
                    bytesPerGB
            );

            pushGpuHistory(
                gpu.encodeHistory,
                gpu.encodePercent
            );

            pushGpuHistory(
                gpu.decodeHistory,
                gpu.decodePercent
            );
        }
    }


    std::string wideToAnsi(
        const wchar_t* text)
    {
        if (text == nullptr ||
            *text == L'\0')
        {
            return "";
        }

        int required =
            WideCharToMultiByte(
                CP_ACP,
                0,
                text,
                -1,
                nullptr,
                0,
                nullptr,
                nullptr
            );

        if (required <= 1)
        {
            return "";
        }

        std::vector<char> buffer(
            static_cast<size_t>(required),
            '\0'
        );

        WideCharToMultiByte(
            CP_ACP,
            0,
            text,
            -1,
            buffer.data(),
            required,
            nullptr,
            nullptr
        );

        return buffer.data();
    }


    std::string ipv4ToText(
        const sockaddr_in* address)
    {
        if (address == nullptr)
        {
            return "--";
        }

        const unsigned char* bytes =
            reinterpret_cast<
                const unsigned char*
            >(&address->sin_addr);

        std::ostringstream stream;
        stream
            << static_cast<int>(bytes[0])
            << "."
            << static_cast<int>(bytes[1])
            << "."
            << static_cast<int>(bytes[2])
            << "."
            << static_cast<int>(bytes[3]);

        return stream.str();
    }


    std::string ipv6ToText(
        const sockaddr_in6* address)
    {
        if (address == nullptr)
        {
            return "--";
        }

        const unsigned char* bytes =
            reinterpret_cast<
                const unsigned char*
            >(&address->sin6_addr);

        std::ostringstream stream;
        stream << std::hex;

        for (int group = 0;
             group < 8;
             group++)
        {
            if (group > 0)
            {
                stream << ":";
            }

            unsigned value =
                static_cast<unsigned>(
                    bytes[group * 2]
                ) << 8;

            value |=
                static_cast<unsigned>(
                    bytes[group * 2 + 1]
                );

            stream << value;
        }

        return stream.str();
    }


    std::string networkTypeText(
        ULONG ifType)
    {
        switch (ifType)
        {
        case IF_TYPE_ETHERNET_CSMACD:
            return "Ethernet";

        case IF_TYPE_IEEE80211:
            return "Wi-Fi";

        case IF_TYPE_PPP:
            return "PPP";

        case IF_TYPE_TUNNEL:
            return "Tunnel";

        default:
            return "Network";
        }
    }


    struct NetworkCounterState
    {
        bool initialized = false;
        ULONGLONG tickMs = 0;
        unsigned long long inOctets = 0;
        unsigned long long outOctets = 0;
    };

    std::map<std::string, NetworkCounterState>
        networkCounterStates;

    ULONGLONG lastNetworkEnumerationTick = 0;
    ULONGLONG lastNetworkConnectionTick = 0;


    struct NetworkApiFunctions
    {
        using GetAdaptersAddressesFn =
            ULONG (WINAPI *)(
                ULONG,
                ULONG,
                PVOID,
                PIP_ADAPTER_ADDRESSES,
                PULONG
            );

        using GetIfEntryFn =
            DWORD (WINAPI *)(
                PMIB_IFROW
            );

        using GetExtendedTcpTableFn =
            DWORD (WINAPI *)(
                PVOID,
                PDWORD,
                BOOL,
                ULONG,
                TCP_TABLE_CLASS,
                ULONG
            );

        HMODULE module = nullptr;
        GetAdaptersAddressesFn getAdaptersAddresses = nullptr;
        GetIfEntryFn getIfEntry = nullptr;
        GetExtendedTcpTableFn getExtendedTcpTable = nullptr;

        bool validAdapters() const
        {
            return
                module != nullptr &&
                getAdaptersAddresses != nullptr;
        }
    };


    NetworkApiFunctions& getNetworkApi()
    {
        static NetworkApiFunctions api;
        static bool initialized = false;

        if (!initialized)
        {
            initialized = true;

            api.module =
                LoadLibraryA("iphlpapi.dll");

            if (api.module != nullptr)
            {
                api.getAdaptersAddresses =
                    reinterpret_cast<
                        NetworkApiFunctions::GetAdaptersAddressesFn
                    >(
                        GetProcAddress(
                            api.module,
                            "GetAdaptersAddresses"
                        )
                    );

                api.getIfEntry =
                    reinterpret_cast<
                        NetworkApiFunctions::GetIfEntryFn
                    >(
                        GetProcAddress(
                            api.module,
                            "GetIfEntry"
                        )
                    );

                api.getExtendedTcpTable =
                    reinterpret_cast<
                        NetworkApiFunctions::GetExtendedTcpTableFn
                    >(
                        GetProcAddress(
                            api.module,
                            "GetExtendedTcpTable"
                        )
                    );
            }
        }

        return api;
    }


    std::string formatMacAddress(
        const BYTE* address,
        ULONG length)
    {
        if (address == nullptr ||
            length == 0)
        {
            return "--";
        }

        std::ostringstream stream;
        stream
            << std::uppercase
            << std::hex
            << std::setfill('0');

        ULONG visibleLength =
            (std::min)(length, 8UL);

        for (ULONG i = 0;
             i < visibleLength;
             i++)
        {
            if (i > 0)
            {
                stream << "-";
            }

            stream
                << std::setw(2)
                << static_cast<unsigned>(
                    address[i]
                );
        }

        return stream.str();
    }


    std::string socketAddressToText(
        const SOCKET_ADDRESS& socketAddress)
    {
        if (socketAddress.lpSockaddr == nullptr)
        {
            return "--";
        }

        if (socketAddress.lpSockaddr->sa_family ==
            AF_INET)
        {
            return ipv4ToText(
                reinterpret_cast<
                    const sockaddr_in*
                >(
                    socketAddress.lpSockaddr
                )
            );
        }

        if (socketAddress.lpSockaddr->sa_family ==
            AF_INET6)
        {
            return ipv6ToText(
                reinterpret_cast<
                    const sockaddr_in6*
                >(
                    socketAddress.lpSockaddr
                )
            );
        }

        return "--";
    }


    void pushNetworkHistory(
        std::vector<double>& history,
        double value)
    {
        history.push_back(
            (std::max)(0.0, value)
        );

        if (history.size() > 120)
        {
            history.erase(
                history.begin()
            );
        }
    }


    void copyNetworkDynamicData(
        const NetworkStats& oldAdapter,
        NetworkStats& newAdapter)
    {
        newAdapter.performanceValid =
            oldAdapter.performanceValid;
        newAdapter.downloadMbps =
            oldAdapter.downloadMbps;
        newAdapter.uploadMbps =
            oldAdapter.uploadMbps;
        newAdapter.totalDownloadedBytes =
            oldAdapter.totalDownloadedBytes;
        newAdapter.totalUploadedBytes =
            oldAdapter.totalUploadedBytes;
        newAdapter.packetsSent =
            oldAdapter.packetsSent;
        newAdapter.packetsReceived =
            oldAdapter.packetsReceived;
        newAdapter.trackedSinceTick =
            oldAdapter.trackedSinceTick;
        newAdapter.downloadHistory =
            oldAdapter.downloadHistory;
        newAdapter.uploadHistory =
            oldAdapter.uploadHistory;
    }


    void enumerateNetworkAdapters()
    {
        NetworkApiFunctions& api =
            getNetworkApi();

        if (!api.validAdapters())
        {
            networkStats.clear();
            return;
        }

        ULONG bufferSize = 16 * 1024;
        std::vector<BYTE> buffer(bufferSize);

        ULONG flags =
            GAA_FLAG_INCLUDE_GATEWAYS;

        ULONG result =
            api.getAdaptersAddresses(
                AF_UNSPEC,
                flags,
                nullptr,
                reinterpret_cast<
                    PIP_ADAPTER_ADDRESSES
                >(buffer.data()),
                &bufferSize
            );

        if (result == ERROR_BUFFER_OVERFLOW)
        {
            buffer.resize(bufferSize);

            result =
                api.getAdaptersAddresses(
                    AF_UNSPEC,
                    flags,
                    nullptr,
                    reinterpret_cast<
                        PIP_ADAPTER_ADDRESSES
                    >(buffer.data()),
                    &bufferSize
                );
        }

        if (result != NO_ERROR)
        {
            return;
        }

        std::vector<NetworkStats> oldStats =
            networkStats;
        std::vector<NetworkStats> refreshed;
        ULONGLONG now = GetTickCount64();

        for (
            PIP_ADAPTER_ADDRESSES adapter =
                reinterpret_cast<
                    PIP_ADAPTER_ADDRESSES
                >(buffer.data());
            adapter != nullptr;
            adapter = adapter->Next
        )
        {
            if (
                adapter->OperStatus !=
                    IfOperStatusUp ||
                adapter->IfType ==
                    IF_TYPE_SOFTWARE_LOOPBACK
            )
            {
                continue;
            }

            bool hasAddress =
                adapter->FirstUnicastAddress !=
                nullptr;

            bool usefulType =
                adapter->IfType ==
                    IF_TYPE_ETHERNET_CSMACD ||
                adapter->IfType ==
                    IF_TYPE_IEEE80211 ||
                adapter->IfType ==
                    IF_TYPE_PPP ||
                adapter->IfType ==
                    IF_TYPE_TUNNEL;

            if (!hasAddress || !usefulType)
            {
                continue;
            }

            NetworkStats item;
            item.index =
                static_cast<int>(
                    refreshed.size()
                );
            item.interfaceIndex =
                adapter->IfIndex;

            item.stableId =
                adapter->AdapterName != nullptr
                ? adapter->AdapterName
                : std::to_string(
                    adapter->IfIndex
                  );

            std::string friendly =
                wideToAnsi(
                    adapter->FriendlyName
                );
            std::string description =
                wideToAnsi(
                    adapter->Description
                );

            item.name =
                !friendly.empty()
                ? friendly
                : (
                    !description.empty()
                    ? description
                    : "Network adapter"
                  );

            item.description =
                !description.empty()
                ? description
                : item.name;

            item.type =
                networkTypeText(
                    adapter->IfType
                );
            item.status = "Connected";

            item.macAddress =
                formatMacAddress(
                    adapter->PhysicalAddress,
                    adapter->PhysicalAddressLength
                );

            unsigned long long linkBits =
                (std::max)(
                    static_cast<unsigned long long>(
                        adapter->ReceiveLinkSpeed
                    ),
                    static_cast<unsigned long long>(
                        adapter->TransmitLinkSpeed
                    )
                );

            if (linkBits > 0)
            {
                item.linkSpeedMbps =
                    static_cast<double>(
                        linkBits
                    ) /
                    1000000.0;
            }

            for (
                PIP_ADAPTER_UNICAST_ADDRESS address =
                    adapter->FirstUnicastAddress;
                address != nullptr;
                address = address->Next
            )
            {
                if (address->Address.lpSockaddr ==
                    nullptr)
                {
                    continue;
                }

                int family =
                    address->Address.
                        lpSockaddr->sa_family;

                if (
                    family == AF_INET &&
                    item.ipv4Address == "--"
                )
                {
                    item.ipv4Address =
                        socketAddressToText(
                            address->Address
                        );
                }
                else if (
                    family == AF_INET6 &&
                    item.ipv6Address == "--"
                )
                {
                    item.ipv6Address =
                        socketAddressToText(
                            address->Address
                        );
                }
            }

            if (adapter->FirstGatewayAddress !=
                nullptr)
            {
                item.defaultGateway =
                    socketAddressToText(
                        adapter->FirstGatewayAddress->
                            Address
                    );
            }

            std::ostringstream dns;
            int dnsCount = 0;

            for (
                PIP_ADAPTER_DNS_SERVER_ADDRESS server =
                    adapter->FirstDnsServerAddress;
                server != nullptr &&
                    dnsCount < 2;
                server = server->Next
            )
            {
                std::string address =
                    socketAddressToText(
                        server->Address
                    );

                if (address == "--")
                {
                    continue;
                }

                if (dnsCount > 0)
                {
                    dns << ", ";
                }

                dns << address;
                dnsCount++;
            }

            if (dnsCount > 0)
            {
                item.dnsServers = dns.str();
            }

            for (const NetworkStats& oldItem :
                 oldStats)
            {
                if (oldItem.stableId ==
                    item.stableId)
                {
                    copyNetworkDynamicData(
                        oldItem,
                        item
                    );
                    break;
                }
            }

            if (item.trackedSinceTick == 0)
            {
                item.trackedSinceTick = now;
            }

            refreshed.push_back(item);
        }

        std::stable_sort(
            refreshed.begin(),
            refreshed.end(),
            [](const NetworkStats& a,
               const NetworkStats& b)
            {
                auto score =
                    [](const NetworkStats& item)
                    -> int
                {
                    int value = 0;

                    if (item.type == "Ethernet")
                    {
                        value += 30;
                    }
                    else if (item.type == "Wi-Fi")
                    {
                        value += 25;
                    }

                    if (item.defaultGateway != "--")
                    {
                        value += 20;
                    }

                    if (item.ipv4Address != "--")
                    {
                        value += 10;
                    }

                    return value;
                };

                return score(a) > score(b);
            }
        );

        for (size_t i = 0;
             i < refreshed.size();
             i++)
        {
            refreshed[i].index =
                static_cast<int>(i);
        }

        networkStats =
            std::move(refreshed);
    }


    void updateNetworkCounters()
    {
        NetworkApiFunctions& api =
            getNetworkApi();

        ULONGLONG now =
            GetTickCount64();

        for (NetworkStats& adapter :
             networkStats)
        {
            adapter.performanceValid = false;
            adapter.downloadMbps = 0.0;
            adapter.uploadMbps = 0.0;

            if (api.getIfEntry == nullptr)
            {
                pushNetworkHistory(
                    adapter.downloadHistory,
                    0.0
                );
                pushNetworkHistory(
                    adapter.uploadHistory,
                    0.0
                );
                continue;
            }

            MIB_IFROW row = {};
            row.dwIndex =
                adapter.interfaceIndex;

            if (api.getIfEntry(&row) !=
                NO_ERROR)
            {
                pushNetworkHistory(
                    adapter.downloadHistory,
                    0.0
                );
                pushNetworkHistory(
                    adapter.uploadHistory,
                    0.0
                );
                continue;
            }

            adapter.totalDownloadedBytes =
                static_cast<unsigned long long>(
                    row.dwInOctets
                );
            adapter.totalUploadedBytes =
                static_cast<unsigned long long>(
                    row.dwOutOctets
                );

            adapter.packetsReceived =
                static_cast<unsigned long long>(
                    row.dwInUcastPkts +
                    row.dwInNUcastPkts
                );
            adapter.packetsSent =
                static_cast<unsigned long long>(
                    row.dwOutUcastPkts +
                    row.dwOutNUcastPkts
                );

            NetworkCounterState& state =
                networkCounterStates[
                    adapter.stableId
                ];

            if (!state.initialized)
            {
                state.initialized = true;
                state.tickMs = now;
                state.inOctets =
                    adapter.totalDownloadedBytes;
                state.outOctets =
                    adapter.totalUploadedBytes;

                pushNetworkHistory(
                    adapter.downloadHistory,
                    0.0
                );
                pushNetworkHistory(
                    adapter.uploadHistory,
                    0.0
                );
                continue;
            }

            ULONGLONG elapsedMs =
                now - state.tickMs;

            const unsigned long long counterWrap =
                0x100000000ULL;

            unsigned long long inDelta =
                adapter.totalDownloadedBytes >=
                    state.inOctets
                ? adapter.totalDownloadedBytes -
                    state.inOctets
                : counterWrap -
                    state.inOctets +
                    adapter.totalDownloadedBytes;

            unsigned long long outDelta =
                adapter.totalUploadedBytes >=
                    state.outOctets
                ? adapter.totalUploadedBytes -
                    state.outOctets
                : counterWrap -
                    state.outOctets +
                    adapter.totalUploadedBytes;

            state.tickMs = now;
            state.inOctets =
                adapter.totalDownloadedBytes;
            state.outOctets =
                adapter.totalUploadedBytes;

            if (elapsedMs > 0)
            {
                double elapsedSeconds =
                    elapsedMs / 1000.0;

                adapter.downloadMbps =
                    static_cast<double>(
                        inDelta
                    ) * 8.0 /
                    elapsedSeconds /
                    1000000.0;

                adapter.uploadMbps =
                    static_cast<double>(
                        outDelta
                    ) * 8.0 /
                    elapsedSeconds /
                    1000000.0;

                adapter.performanceValid = true;
            }

            pushNetworkHistory(
                adapter.downloadHistory,
                adapter.downloadMbps
            );
            pushNetworkHistory(
                adapter.uploadHistory,
                adapter.uploadMbps
            );
        }
    }


    std::map<DWORD, std::string>
    snapshotProcessNames()
    {
        std::map<DWORD, std::string> result;

        HANDLE snapshot =
            CreateToolhelp32Snapshot(
                TH32CS_SNAPPROCESS,
                0
            );

        if (snapshot ==
            INVALID_HANDLE_VALUE)
        {
            return result;
        }

        PROCESSENTRY32 entry = {};
        entry.dwSize = sizeof(entry);

        if (Process32First(
                snapshot,
                &entry
            ))
        {
            do
            {
                result[
                    entry.th32ProcessID
                ] = entry.szExeFile;
            }
            while (Process32Next(
                snapshot,
                &entry
            ));
        }

        CloseHandle(snapshot);
        return result;
    }


    std::string tcpStateText(
        DWORD state)
    {
        switch (state)
        {
        case MIB_TCP_STATE_ESTAB:
            return "ESTABLISHED";
        case MIB_TCP_STATE_SYN_SENT:
            return "SYN_SENT";
        case MIB_TCP_STATE_SYN_RCVD:
            return "SYN_RECEIVED";
        case MIB_TCP_STATE_FIN_WAIT1:
            return "FIN_WAIT_1";
        case MIB_TCP_STATE_FIN_WAIT2:
            return "FIN_WAIT_2";
        case MIB_TCP_STATE_CLOSE_WAIT:
            return "CLOSE_WAIT";
        default:
            return "ACTIVE";
        }
    }


    unsigned short networkPortToHost(
        DWORD value)
    {
        unsigned short port =
            static_cast<unsigned short>(
                value & 0xFFFF
            );

        return static_cast<unsigned short>(
            (port >> 8) |
            (port << 8)
        );
    }


    std::string ipv4DwordToText(
        DWORD address)
    {
        const unsigned char* bytes =
            reinterpret_cast<
                const unsigned char*
            >(&address);

        std::ostringstream stream;
        stream
            << static_cast<unsigned>(bytes[0])
            << "."
            << static_cast<unsigned>(bytes[1])
            << "."
            << static_cast<unsigned>(bytes[2])
            << "."
            << static_cast<unsigned>(bytes[3]);

        return stream.str();
    }


    void refreshActiveConnections()
    {
        activeNetworkConnections.clear();

        NetworkApiFunctions& api =
            getNetworkApi();

        if (api.getExtendedTcpTable ==
            nullptr)
        {
            return;
        }

        DWORD bytes = 0;

        DWORD result =
            api.getExtendedTcpTable(
                nullptr,
                &bytes,
                FALSE,
                AF_INET,
                TCP_TABLE_OWNER_PID_CONNECTIONS,
                0
            );

        if (result != ERROR_INSUFFICIENT_BUFFER ||
            bytes == 0)
        {
            return;
        }

        std::vector<BYTE> buffer(bytes);

        result =
            api.getExtendedTcpTable(
                buffer.data(),
                &bytes,
                FALSE,
                AF_INET,
                TCP_TABLE_OWNER_PID_CONNECTIONS,
                0
            );

        if (result != NO_ERROR)
        {
            return;
        }

        auto* table =
            reinterpret_cast<
                PMIB_TCPTABLE_OWNER_PID
            >(buffer.data());

        std::map<DWORD, std::string>
            processNames =
                snapshotProcessNames();

        for (DWORD i = 0;
             i < table->dwNumEntries;
             i++)
        {
            const MIB_TCPROW_OWNER_PID& row =
                table->table[i];

            if (
                row.dwState ==
                    MIB_TCP_STATE_LISTEN ||
                row.dwRemoteAddr == 0
            )
            {
                continue;
            }

            NetworkConnectionInfo item;
            item.pid = row.dwOwningPid;

            auto process =
                processNames.find(
                    item.pid
                );

            if (process !=
                processNames.end())
            {
                item.processName =
                    process->second;
            }
            else
            {
                item.processName =
                    "PID " +
                    std::to_string(
                        item.pid
                    );
            }

            std::ostringstream remote;
            remote
                << ipv4DwordToText(
                    row.dwRemoteAddr
                )
                << ":"
                << networkPortToHost(
                    row.dwRemotePort
                );

            item.remoteAddress =
                remote.str();
            item.state =
                tcpStateText(
                    row.dwState
                );

            activeNetworkConnections.push_back(
                item
            );
        }

        std::stable_sort(
            activeNetworkConnections.begin(),
            activeNetworkConnections.end(),
            [](const NetworkConnectionInfo& a,
               const NetworkConnectionInfo& b)
            {
                bool aEstablished =
                    a.state == "ESTABLISHED";
                bool bEstablished =
                    b.state == "ESTABLISHED";

                if (aEstablished != bEstablished)
                {
                    return aEstablished;
                }

                if (a.processName != b.processName)
                {
                    return
                        a.processName <
                        b.processName;
                }

                return
                    a.remoteAddress <
                    b.remoteAddress;
            }
        );

        if (activeNetworkConnections.size() >
            12)
        {
            activeNetworkConnections.resize(12);
        }
    }


    void queryNetworkInfo()
    {
        systemInfo.networkAdapter = "--";
        systemInfo.networkConnectionType = "--";
        systemInfo.ipv4Address = "--";
        systemInfo.ipv6Address = "--";

        if (!networkStats.empty())
        {
            const NetworkStats& best =
                networkStats.front();

            systemInfo.networkAdapter =
                best.description;
            systemInfo.networkConnectionType =
                best.type;
            systemInfo.ipv4Address =
                best.ipv4Address;
            systemInfo.ipv6Address =
                best.ipv6Address;
            return;
        }

        using GetAdaptersAddressesFn =
            ULONG (WINAPI *)(
                ULONG,
                ULONG,
                PVOID,
                PIP_ADAPTER_ADDRESSES,
                PULONG
            );

        static HMODULE module =
            LoadLibraryA("iphlpapi.dll");

        static GetAdaptersAddressesFn getAdapters =
            module
            ? reinterpret_cast<
                GetAdaptersAddressesFn
              >(
                GetProcAddress(
                    module,
                    "GetAdaptersAddresses"
                )
              )
            : nullptr;

        if (getAdapters == nullptr)
        {
            return;
        }

        ULONG size = 16 * 1024;
        std::vector<BYTE> buffer(size);

        ULONG result =
            getAdapters(
                AF_UNSPEC,
                0,
                nullptr,
                reinterpret_cast<
                    PIP_ADAPTER_ADDRESSES
                >(buffer.data()),
                &size
            );

        if (result == ERROR_BUFFER_OVERFLOW)
        {
            buffer.resize(size);

            result =
                getAdapters(
                    AF_UNSPEC,
                    0,
                    nullptr,
                    reinterpret_cast<
                        PIP_ADAPTER_ADDRESSES
                    >(buffer.data()),
                    &size
                );
        }

        if (result != NO_ERROR)
        {
            return;
        }

        PIP_ADAPTER_ADDRESSES best = nullptr;
        int bestScore = -1;

        for (
            PIP_ADAPTER_ADDRESSES adapter =
                reinterpret_cast<
                    PIP_ADAPTER_ADDRESSES
                >(buffer.data());
            adapter != nullptr;
            adapter = adapter->Next
        )
        {
            if (adapter->OperStatus !=
                    IfOperStatusUp ||
                adapter->IfType ==
                    IF_TYPE_SOFTWARE_LOOPBACK)
            {
                continue;
            }

            int score = 0;

            if (adapter->IfType ==
                IF_TYPE_ETHERNET_CSMACD)
            {
                score += 20;
            }
            else if (adapter->IfType ==
                     IF_TYPE_IEEE80211)
            {
                score += 18;
            }

            if (adapter->FirstGatewayAddress !=
                nullptr)
            {
                score += 10;
            }

            if (adapter->FirstUnicastAddress !=
                nullptr)
            {
                score += 5;
            }

            if (score > bestScore)
            {
                best = adapter;
                bestScore = score;
            }
        }

        if (best == nullptr)
        {
            return;
        }

        std::string friendly =
            wideToAnsi(best->FriendlyName);

        std::string description =
            wideToAnsi(best->Description);

        systemInfo.networkAdapter =
            !description.empty()
            ? description
            : (
                !friendly.empty()
                ? friendly
                : "--"
              );

        systemInfo.networkConnectionType =
            networkTypeText(best->IfType);

        for (
            PIP_ADAPTER_UNICAST_ADDRESS address =
                best->FirstUnicastAddress;
            address != nullptr;
            address = address->Next
        )
        {
            if (address->Address.lpSockaddr ==
                nullptr)
            {
                continue;
            }

            int family =
                address->Address.lpSockaddr->sa_family;

            if (family == AF_INET &&
                systemInfo.ipv4Address == "--")
            {
                systemInfo.ipv4Address =
                    ipv4ToText(
                        reinterpret_cast<
                            const sockaddr_in*
                        >(
                            address->Address.lpSockaddr
                        )
                    );
            }
            else if (family == AF_INET6 &&
                     systemInfo.ipv6Address == "--")
            {
                systemInfo.ipv6Address =
                    ipv6ToText(
                        reinterpret_cast<
                            const sockaddr_in6*
                        >(
                            address->Address.lpSockaddr
                        )
                    );
            }
        }
    }


    void queryConnectedDevices()
    {
        systemInfo.connectedDevices.clear();

        SetupApiFunctions& setup =
            getSetupApi();

        if (!setup.valid())
        {
            return;
        }

        HDEVINFO set =
            setup.getClassDevs(
                nullptr,
                nullptr,
                nullptr,
                DIGCF_PRESENT |
                    DIGCF_ALLCLASSES
            );

        if (set == INVALID_HANDLE_VALUE)
        {
            return;
        }

        std::set<std::string> seen;

        for (DWORD index = 0;; index++)
        {
            SP_DEVINFO_DATA info = {};
            info.cbSize = sizeof(info);

            if (!setup.enumDeviceInfo(
                    set,
                    index,
                    &info
                ))
            {
                break;
            }

            std::string enumerator =
                getSetupDeviceProperty(
                    setup,
                    set,
                    info,
                    SPDRP_ENUMERATOR_NAME
                );

            std::string enumUpper =
                toUpperCopy(enumerator);

            std::string className =
                getSetupDeviceProperty(
                    setup,
                    set,
                    info,
                    SPDRP_CLASS
                );

            std::string classUpper =
                toUpperCopy(className);

            bool userFacingClass =
                classUpper == "MONITOR" ||
                classUpper == "MEDIA" ||
                classUpper == "AUDIOENDPOINT" ||
                classUpper == "CAMERA" ||
                classUpper == "IMAGE" ||
                classUpper == "PRINTER" ||
                classUpper == "KEYBOARD" ||
                classUpper == "MOUSE";

            bool external =
                enumUpper.find("USB") !=
                    std::string::npos ||
                enumUpper.find("BTH") !=
                    std::string::npos ||
                enumUpper.find("BLUETOOTH") !=
                    std::string::npos ||
                enumUpper.find("HID") !=
                    std::string::npos ||
                userFacingClass;

            if (!external)
            {
                continue;
            }

            std::string name =
                getSetupDeviceProperty(
                    setup,
                    set,
                    info,
                    SPDRP_FRIENDLYNAME
                );

            if (name.empty())
            {
                name =
                    getSetupDeviceProperty(
                        setup,
                        set,
                        info,
                        SPDRP_DEVICEDESC
                    );
            }

            if (name.empty())
            {
                continue;
            }

            std::string upperName =
                toUpperCopy(name);

            if (
                upperName.find(
                    "HOST CONTROLLER"
                ) != std::string::npos ||
                upperName.find(
                    "ROOT HUB"
                ) != std::string::npos
            )
            {
                continue;
            }

            std::string type = "Device";

            if (enumUpper.find("USB") !=
                std::string::npos)
            {
                type = "USB";
            }
            else if (
                enumUpper.find("BTH") !=
                    std::string::npos ||
                enumUpper.find("BLUETOOTH") !=
                    std::string::npos
            )
            {
                type = "Bluetooth";
            }
            else if (enumUpper.find("HID") !=
                     std::string::npos)
            {
                type = "HID";
            }
            else if (!className.empty())
            {
                type = className;
            }

            std::string key =
                type + "|" + name;

            if (!seen.insert(key).second)
            {
                continue;
            }

            ConnectedDeviceInfo device;
            device.name = name;
            device.type = type;

            device.connectionType =
                deviceConnectionType(
                    enumerator,
                    className
                );

            device.deviceClass =
                !className.empty()
                ? className
                : "--";

            std::string manufacturer =
                getSetupDeviceProperty(
                    setup,
                    set,
                    info,
                    SPDRP_MFG
                );

            if (!manufacturer.empty())
            {
                device.manufacturer =
                    manufacturer;
            }

            std::string location =
                getSetupDeviceProperty(
                    setup,
                    set,
                    info,
                    SPDRP_LOCATION_INFORMATION
                );

            if (!location.empty())
            {
                device.location =
                    location;
            }

            std::string instanceId =
                getSetupDeviceInstanceId(
                    setup,
                    set,
                    info
                );

            if (!instanceId.empty())
            {
                device.instanceId =
                    instanceId;

                device.selectionKey =
                    instanceId;
            }

            std::string hardwareId =
                getSetupDeviceProperty(
                    setup,
                    set,
                    info,
                    SPDRP_HARDWAREID
                );

            if (!hardwareId.empty())
            {
                device.hardwareId =
                    hardwareId;

                device.vendorId =
                    extractPnPIdentifier(
                        hardwareId,
                        "VID_",
                        "VEN_"
                    );

                device.productId =
                    extractPnPIdentifier(
                        hardwareId,
                        "PID_",
                        "DEV_"
                    );
            }

            if (device.selectionKey.empty())
            {
                device.selectionKey =
                    key;
            }

            device.status =
                queryDeviceStatus(
                    info
                );

            systemInfo.connectedDevices.push_back(
                device
            );

            if (systemInfo.connectedDevices.size() >=
                80)
            {
                break;
            }
        }

        setup.destroy(set);

        std::sort(
            systemInfo.connectedDevices.begin(),
            systemInfo.connectedDevices.end(),
            [](const ConnectedDeviceInfo& a,
               const ConnectedDeviceInfo& b)
            {
                if (a.type != b.type)
                {
                    return a.type < b.type;
                }

                return a.name < b.name;
            }
        );
    }


    void queryStaticSystemInfo()
    {
        const std::string currentVersionKey =
            "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

        std::string productName =
            readRegistryString(
                HKEY_LOCAL_MACHINE,
                currentVersionKey,
                "ProductName"
            );

        std::string displayVersion =
            readRegistryString(
                HKEY_LOCAL_MACHINE,
                currentVersionKey,
                "DisplayVersion"
            );

        std::string buildNumber =
            readRegistryString(
                HKEY_LOCAL_MACHINE,
                currentVersionKey,
                "CurrentBuildNumber"
            );

        DWORD ubr = 0;
        readRegistryDword(
            HKEY_LOCAL_MACHINE,
            currentVersionKey,
            "UBR",
            ubr
        );

        if (!productName.empty())
        {
            systemInfo.osName = productName;
        }

        if (!displayVersion.empty())
        {
            systemInfo.osVersion =
                displayVersion;

            if (!buildNumber.empty())
            {
                systemInfo.osVersion +=
                    " (" +
                    buildNumber;

                if (ubr > 0)
                {
                    systemInfo.osVersion +=
                        "." +
                        std::to_string(ubr);
                }

                systemInfo.osVersion += ")";
            }
        }

        if (!buildNumber.empty())
        {
            systemInfo.osBuild =
                buildNumber;

            if (ubr > 0)
            {
                systemInfo.osBuild +=
                    "." +
                    std::to_string(ubr);
            }
        }

        DWORD installDate = 0;

        if (readRegistryDword(
                HKEY_LOCAL_MACHINE,
                currentVersionKey,
                "InstallDate",
                installDate
            ))
        {
            systemInfo.installedOn =
                formatInstallDate(
                    installDate
                );
        }

        std::string experience =
            readRegistryString(
                HKEY_LOCAL_MACHINE,
                currentVersionKey,
                "FeatureExperiencePackVersion"
            );

        if (!experience.empty())
        {
            systemInfo.experience =
                "Windows Feature Experience Pack " +
                experience;
        }

        systemInfo.systemType =
            getSystemTypeText();

        char computerName[MAX_COMPUTERNAME_LENGTH + 1] = {};
        DWORD computerNameLength =
            MAX_COMPUTERNAME_LENGTH + 1;

        if (GetComputerNameA(
                computerName,
                &computerNameLength
            ))
        {
            systemInfo.computerName =
                computerName;
        }

        const std::string cpuKey =
            "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";

        std::string cpuName =
            readRegistryString(
                HKEY_LOCAL_MACHINE,
                cpuKey,
                "ProcessorNameString"
            );

        if (!cpuName.empty())
        {
            systemInfo.cpuName = cpuName;
        }

        DWORD cpuMHz = 0;

        if (readRegistryDword(
                HKEY_LOCAL_MACHINE,
                cpuKey,
                "~MHz",
                cpuMHz
            ))
        {
            systemInfo.cpuBaseSpeed =
                formatMHzAsGHz(cpuMHz);
            systemInfo.cpuCurrentSpeed =
                formatMHzAsGHz(cpuMHz);
        }

        systemInfo.cpuCores =
            static_cast<int>(
                countCpuRelationship(
                    RelationProcessorCore
                )
            );

        systemInfo.cpuThreads =
            static_cast<int>(
                GetActiveProcessorCount(
                    ALL_PROCESSOR_GROUPS
                )
            );

        systemInfo.virtualization =
            IsProcessorFeaturePresent(
                PF_VIRT_FIRMWARE_ENABLED
            )
            ? "Enabled"
            : "Disabled";

        queryCpuCacheInfo();

        ULONGLONG installedKb = 0;

        if (GetPhysicallyInstalledSystemMemory(
                &installedKb
            ))
        {
            systemInfo.installedMemory =
                formatGigabytes(
                    installedKb * 1024ULL
                );
        }

        querySmbiosSystemInfo();

        const std::string biosKey =
            "HARDWARE\\DESCRIPTION\\System\\BIOS";

        std::string value;

        value = readRegistryString(
            HKEY_LOCAL_MACHINE,
            biosKey,
            "SystemManufacturer"
        );
        if (!value.empty())
            systemInfo.systemManufacturer = value;

        value = readRegistryString(
            HKEY_LOCAL_MACHINE,
            biosKey,
            "SystemProductName"
        );
        if (!value.empty())
            systemInfo.systemModel = value;

        value = readRegistryString(
            HKEY_LOCAL_MACHINE,
            biosKey,
            "BaseBoardManufacturer"
        );
        if (!value.empty())
            systemInfo.motherboardManufacturer = value;

        value = readRegistryString(
            HKEY_LOCAL_MACHINE,
            biosKey,
            "BaseBoardProduct"
        );
        if (!value.empty())
            systemInfo.motherboardModel = value;

        value = readRegistryString(
            HKEY_LOCAL_MACHINE,
            biosKey,
            "BIOSVendor"
        );
        if (!value.empty())
            systemInfo.biosVendor = value;

        value = readRegistryString(
            HKEY_LOCAL_MACHINE,
            biosKey,
            "BIOSVersion"
        );
        if (!value.empty())
            systemInfo.biosVersion = value;

        value = readRegistryString(
            HKEY_LOCAL_MACHINE,
            biosKey,
            "BIOSReleaseDate"
        );
        if (!value.empty())
            systemInfo.biosDate = value;

        queryGraphicsInfo();

        systemInfo.initialized = true;
    }
}


void refreshGpuStats(bool force)
{
    ULONGLONG now =
        GetTickCount64();

    if (
        force ||
        lastGpuEnumerationTick == 0 ||
        now - lastGpuEnumerationTick >= 5000
    )
    {
        enumerateGpuAdapters();
        lastGpuEnumerationTick = now;
    }

    for (GpuStats& gpu : gpuStats)
    {
        gpu.performanceValid = false;
        gpu.utilizationPercent = 0.0;
        gpu.encodePercent = 0.0;
        gpu.decodePercent = 0.0;
        gpu.dedicatedMemoryUsedBytes = 0;
        gpu.sharedMemoryUsedBytes = 0;
        gpu.temperatureC = -1.0;
        gpu.fanPercent = -1;
        gpu.fanRpm = -1;
        gpu.powerW = -1.0;
        gpu.powerLimitW = -1.0;
    }

    updateGpuPdhMetrics();
    enrichGpuWithNvml();
    sampleGpuHistories();
}


void refreshNetworkStats(bool force)
{
    ULONGLONG now =
        GetTickCount64();

    if (
        force ||
        lastNetworkEnumerationTick == 0 ||
        now - lastNetworkEnumerationTick >= 5000
    )
    {
        enumerateNetworkAdapters();
        lastNetworkEnumerationTick = now;
    }

    updateNetworkCounters();

    if (
        force ||
        lastNetworkConnectionTick == 0 ||
        now - lastNetworkConnectionTick >= 2000
    )
    {
        refreshActiveConnections();
        lastNetworkConnectionTick = now;
    }
}


void refreshSystemInfo(bool force)
{
    ULONGLONG now =
        GetTickCount64();

    if (!systemInfo.initialized)
    {
        queryStaticSystemInfo();
        force = true;
    }

    if (
        force ||
        lastSystemInfoDynamicTick == 0 ||
        now - lastSystemInfoDynamicTick >= 5000
    )
    {
        queryNetworkInfo();
        queryConnectedDevices();
        lastSystemInfoDynamicTick = now;
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


    // GPU performance and hot-plug detection.
    refreshGpuStats(false);


    // Network performance and adapter hot-plug detection.
    refreshNetworkStats(false);


    // System / hardware overview and connected-device refresh.
    // Static information is cached; network and device data refresh
    // every five seconds.
    refreshSystemInfo(false);


    // Uptime
    uptimeSeconds =
        GetTickCount64() / 1000;
}

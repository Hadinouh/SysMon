#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <winsock2.h>
#include <iphlpapi.h>
#include <setupapi.h>
#include "Stats.h"

#include <ws2tcpip.h>
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

        using DestroyFn =
            BOOL (WINAPI *)(HDEVINFO);

        HMODULE module = nullptr;
        GetClassDevsFn getClassDevs = nullptr;
        EnumDeviceInfoFn enumDeviceInfo = nullptr;
        GetPropertyFn getProperty = nullptr;
        DestroyFn destroy = nullptr;

        bool valid() const
        {
            return
                module != nullptr &&
                getClassDevs != nullptr &&
                enumDeviceInfo != nullptr &&
                getProperty != nullptr &&
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


    void queryNetworkInfo()
    {
        systemInfo.networkAdapter = "--";
        systemInfo.networkConnectionType = "--";
        systemInfo.ipv4Address = "--";
        systemInfo.ipv6Address = "--";

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

            if (upperName.find(
                    "HOST CONTROLLER"
                ) != std::string::npos ||
                upperName.find(
                    "ROOT HUB"
                ) != std::string::npos)
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

            systemInfo.connectedDevices.push_back(
                device
            );

            if (systemInfo.connectedDevices.size() >=
                40)
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


    // System / hardware overview and connected-device refresh.
    // Static information is cached; network and device data refresh
    // every five seconds.
    refreshSystemInfo(false);


    // Uptime
    uptimeSeconds =
        GetTickCount64() / 1000;
}

#include "Processes.h"

#include <windows.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>


typedef LONG NTSTATUS;


// --------------------------------------------------------
// Native Windows structures
// --------------------------------------------------------

struct NativeUnicodeString
{
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
};


struct NativeSystemProcessInformation
{
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;

    LARGE_INTEGER WorkingSetPrivateSize;

    ULONG HardFaultCount;
    ULONG NumberOfThreadsHighWatermark;

    ULONGLONG CycleTime;

    LARGE_INTEGER CreateTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER KernelTime;

    NativeUnicodeString ImageName;

    LONG BasePriority;

    HANDLE UniqueProcessId;
    HANDLE InheritedFromUniqueProcessId;

    ULONG HandleCount;
    ULONG SessionId;

    ULONG_PTR UniqueProcessKey;

    SIZE_T PeakVirtualSize;
    SIZE_T VirtualSize;

    ULONG PageFaultCount;

    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
};


typedef NTSTATUS (NTAPI* NtQuerySystemInformationFn)(
    ULONG,
    PVOID,
    ULONG,
    PULONG
);


// --------------------------------------------------------
// Previous CPU samples
// --------------------------------------------------------

static std::unordered_map<DWORD, ULONGLONG>
    previousProcessTimes;

static ULONGLONG previousSystemTime = 0;
static ULONGLONG previousIdleTime = 0;


// --------------------------------------------------------
// FILETIME -> number
// --------------------------------------------------------

static ULONGLONG fileTimeToULL(
    const FILETIME& time)
{
    ULARGE_INTEGER value;

    value.LowPart =
        time.dwLowDateTime;

    value.HighPart =
        time.dwHighDateTime;

    return value.QuadPart;
}


// --------------------------------------------------------
// Wide string -> std::string
// --------------------------------------------------------

std::string wideToString(
    const wchar_t* text,
    int length)
{
    if (!text || length <= 0)
    {
        return "";
    }

    int requiredSize =
        WideCharToMultiByte(
            CP_UTF8,
            0,
            text,
            length,
            nullptr,
            0,
            nullptr,
            nullptr
        );

    if (requiredSize <= 0)
    {
        return "";
    }

    std::string result(
        requiredSize,
        '\0'
    );

    WideCharToMultiByte(
        CP_UTF8,
        0,
        text,
        length,
        result.data(),
        requiredSize,
        nullptr,
        nullptr
    );

    return result;
}


// --------------------------------------------------------
// Get running processes
// --------------------------------------------------------

std::vector<ProcessInfo> getRunningProcesses()
{
    std::vector<ProcessInfo> processes;


    // ----------------------------------------------------
    // Get current total CPU time
    // ----------------------------------------------------

    FILETIME idleTime;
    FILETIME kernelTime;
    FILETIME userTime;

    ULONGLONG currentSystemTime = 0;
    ULONGLONG currentIdleTime = 0;


    if (GetSystemTimes(
            &idleTime,
            &kernelTime,
            &userTime))
    {
        currentIdleTime =
            fileTimeToULL(
                idleTime
            );

        currentSystemTime =
            fileTimeToULL(
                kernelTime
            ) +
            fileTimeToULL(
                userTime
            );
    }


    // ----------------------------------------------------
    // Load NtQuerySystemInformation
    // ----------------------------------------------------

    HMODULE ntdll =
        GetModuleHandleW(
            L"ntdll.dll"
        );

    if (!ntdll)
    {
        return processes;
    }


    auto NtQuerySystemInformation =
        reinterpret_cast<
            NtQuerySystemInformationFn
        >(
            GetProcAddress(
                ntdll,
                "NtQuerySystemInformation"
            )
        );

    if (!NtQuerySystemInformation)
    {
        return processes;
    }


    constexpr ULONG
        SystemProcessInformationClass = 5;

    constexpr NTSTATUS
        StatusInfoLengthMismatch =
            static_cast<NTSTATUS>(
                0xC0000004u
            );


    ULONG bufferSize =
        1024 * 1024;

    std::vector<unsigned char> buffer(
        bufferSize
    );


    NTSTATUS status;


    while (true)
    {
        ULONG requiredSize = 0;

        status =
            NtQuerySystemInformation(
                SystemProcessInformationClass,
                buffer.data(),
                bufferSize,
                &requiredSize
            );


        if (status ==
            StatusInfoLengthMismatch)
        {
            if (requiredSize >
                bufferSize)
            {
                bufferSize =
                    requiredSize +
                    65536;
            }
            else
            {
                bufferSize *= 2;
            }

            buffer.resize(
                bufferSize
            );

            continue;
        }

        break;
    }


    if (status < 0)
    {
        return processes;
    }


    auto* info =
        reinterpret_cast<
            NativeSystemProcessInformation*
        >(
            buffer.data()
        );


    std::unordered_map<
        DWORD,
        ULONGLONG
    > currentProcessTimes;


    ULONGLONG systemDelta = 0;

    if (
        previousSystemTime > 0 &&
        currentSystemTime >
            previousSystemTime
    )
    {
        systemDelta =
            currentSystemTime -
            previousSystemTime;
    }


    // ----------------------------------------------------
    // Read each process
    // ----------------------------------------------------

    while (true)
    {
        ProcessInfo process = {};


        process.pid =
            static_cast<DWORD>(
                reinterpret_cast<
                    ULONG_PTR
                >(
                    info->UniqueProcessId
                )
            );


        // ------------------------------------------------
        // Name
        // ------------------------------------------------

        if (
            info->ImageName.Buffer &&
            info->ImageName.Length > 0
        )
        {
            process.name =
                wideToString(
                    info->ImageName.Buffer,
                    info->ImageName.Length /
                        sizeof(wchar_t)
                );
        }
        else
        {
            if (process.pid == 0)
            {
                process.name =
                    "System Idle Process";
            }
            else
            {
                process.name =
                    "System";
            }
        }


        // ------------------------------------------------
        // Memory
        // ------------------------------------------------

        process.memoryMB =
            static_cast<double>(
                info->WorkingSetSize
            ) /
            (1024.0 * 1024.0);
process.threadCount =
    static_cast<DWORD>(
        info->NumberOfThreads
    );

        // ------------------------------------------------
        // CPU
        // ------------------------------------------------

        process.cpuPercent = -1.0;


        ULONGLONG processTime =
            static_cast<ULONGLONG>(
                info->UserTime.QuadPart +
                info->KernelTime.QuadPart
            );


        currentProcessTimes[
            process.pid
        ] = processTime;


        if (systemDelta > 0)
        {
            // PID 0 = actual system idle percentage
            if (
                process.pid == 0 &&
                previousIdleTime > 0 &&
                currentIdleTime >=
                    previousIdleTime
            )
            {
                ULONGLONG idleDelta =
                    currentIdleTime -
                    previousIdleTime;

                process.cpuPercent =
                    (
                        static_cast<double>(
                            idleDelta
                        ) /
                        static_cast<double>(
                            systemDelta
                        )
                    ) *
                    100.0;
            }
            else
            {
                auto previous =
                    previousProcessTimes.find(
                        process.pid
                    );

                if (
                    previous !=
                        previousProcessTimes.end() &&
                    processTime >=
                        previous->second
                )
                {
                    ULONGLONG processDelta =
                        processTime -
                        previous->second;

                    process.cpuPercent =
                        (
                            static_cast<double>(
                                processDelta
                            ) /
                            static_cast<double>(
                                systemDelta
                            )
                        ) *
                        100.0;
                }
            }
        }


        if (process.cpuPercent > 100.0)
        {
            process.cpuPercent = 100.0;
        }


        processes.push_back(
            process
        );


        if (info->NextEntryOffset == 0)
        {
            break;
        }


        info =
            reinterpret_cast<
                NativeSystemProcessInformation*
            >(
                reinterpret_cast<
                    unsigned char*
                >(
                    info
                ) +
                info->NextEntryOffset
            );
    }


    // ----------------------------------------------------
    // Save this sample for next refresh
    // ----------------------------------------------------

    previousProcessTimes =
        currentProcessTimes;

    previousSystemTime =
        currentSystemTime;

    previousIdleTime =
        currentIdleTime;


    return processes;
}
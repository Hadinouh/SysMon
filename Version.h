#pragma once

#define SYSMON_VERSION_MAJOR 1
#define SYSMON_VERSION_MINOR 0
#define SYSMON_VERSION_PATCH 0
#define SYSMON_VERSION_BUILD 0

#define SYSMON_VERSION_COMMA 1,0,0,0
#define SYSMON_VERSION_STRING "1.0.0"
#define SYSMON_VERSION_DISPLAY "v1.0.0"
#define SYSMON_VERSION_WSTRING L"1.0.0"
#define SYSMON_USER_AGENT_WSTRING L"SysMon/1.0.0"

#ifdef __cplusplus
namespace SysMonVersion
{
inline constexpr const char* number = SYSMON_VERSION_STRING;
inline constexpr const char* display = SYSMON_VERSION_DISPLAY;
inline constexpr const wchar_t* wide = SYSMON_VERSION_WSTRING;
inline constexpr const wchar_t* userAgent = SYSMON_USER_AGENT_WSTRING;
}
#endif

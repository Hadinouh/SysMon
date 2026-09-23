#include <shlobj.h>
#include "SettingsRuntime.h"
#include "UpdateChecker.h"
#include "Version.h"
#include "AppLifecycle.h"
#include "StartupManager.h"
#include "SettingsDropdown.h"
#include "ProcessDetails.h"
#include "Theme.h"
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include "Settings.h"
#include "Stats.h"
#include "Tray.h"
#include "Widget.h"
#include "UI.h"
#include "Processes.h"
#include <gdiplus.h>
using namespace Gdiplus;

// Instant-navigation preview renderer implemented in UI.cpp.
// Keep the declaration here as well so Main.cpp remains compatible with
// projects that still have the previous UI.h during a staged file update.
void setUiFastNavigationRender(bool enabled);


// --------------------------------------------------------
// RESPONSIVE UI SCALING
// --------------------------------------------------------
// All existing SysMon pages use the logical 1190x720 coordinate space
// declared in UI.h. Panels and plots expand horizontally into the client
// area while text and gauges retain their proportions. Mouse coordinates
// are mapped back into the same
// logical space before the existing hitboxes run.
struct SysMonUiTransform
{
    double scale = 1.0;
    double scaleX = 1.0;
    int offsetX = 0;
    int offsetY = 0;
    int drawWidth = sysMonDesignWidth;
    int drawHeight = sysMonDesignHeight;
};

static SysMonUiTransform getSysMonUiTransform(
    HWND hwnd)
{
    RECT client = {};
    GetClientRect(hwnd, &client);

    const int clientWidth =
        (std::max)(
            1,
            static_cast<int>(
                client.right - client.left
            )
        );

    const int clientHeight =
        (std::max)(
            1,
            static_cast<int>(
                client.bottom - client.top
            )
        );

    const double scaleX =
        static_cast<double>(clientWidth) /
        static_cast<double>(sysMonDesignWidth);

    const double scaleY =
        static_cast<double>(clientHeight) /
        static_cast<double>(sysMonDesignHeight);

    SysMonUiTransform transform;

    transform.scale =
        (std::min)(scaleX, scaleY);

    if (transform.scale <= 0.0)
    {
        transform.scale = 1.0;
    }

    // Keep vertical sizing and typography bounded; use spare horizontal
    // space for wider panels and plots. UI primitives preserve glyphs,
    // gauges and icons independently from this layout transform.
    transform.scale = (std::min)(transform.scale, (std::max)(1.75, GetDpiForWindow(hwnd)/96.0));
    transform.scaleX = (std::max)(transform.scale, scaleX);
    transform.drawWidth = clientWidth;

    transform.drawHeight =
        (std::max)(
            1,
            static_cast<int>(
                sysMonDesignHeight * transform.scale + 0.5
            )
        );

    // The expanded layout is anchored at the client origin.
    transform.offsetX = 0;
    transform.offsetY = 0;

    return transform;
}

static void enforceSysMonResizeAspectRatio(
    HWND hwnd,
    WPARAM sizingEdge,
    RECT* proposedWindowRect)
{
    if (
        proposedWindowRect == nullptr ||
        IsZoomed(hwnd)
    )
    {
        return;
    }

    RECT currentWindow = {};
    RECT currentClient = {};

    if (
        !GetWindowRect(hwnd, &currentWindow) ||
        !GetClientRect(hwnd, &currentClient)
    )
    {
        return;
    }

    const int currentWindowWidth =
        static_cast<int>(
            currentWindow.right -
            currentWindow.left
        );

    const int currentWindowHeight =
        static_cast<int>(
            currentWindow.bottom -
            currentWindow.top
        );

    const int currentClientWidth =
        static_cast<int>(
            currentClient.right -
            currentClient.left
        );

    const int currentClientHeight =
        static_cast<int>(
            currentClient.bottom -
            currentClient.top
        );

    // Keep the real Win32 frame/caption thickness in the calculation so
    // the CLIENT area, not the outer window rectangle, matches SysMon's
    // logical 1190x720 aspect ratio.
    const int nonClientWidth =
        (std::max)(
            0,
            currentWindowWidth -
            currentClientWidth
        );

    const int nonClientHeight =
        (std::max)(
            0,
            currentWindowHeight -
            currentClientHeight
        );

    int proposedWindowWidth =
        static_cast<int>(
            proposedWindowRect->right -
            proposedWindowRect->left
        );

    int proposedWindowHeight =
        static_cast<int>(
            proposedWindowRect->bottom -
            proposedWindowRect->top
        );

    int proposedClientWidth =
        (std::max)(
            1,
            proposedWindowWidth -
            nonClientWidth
        );

    int proposedClientHeight =
        (std::max)(
            1,
            proposedWindowHeight -
            nonClientHeight
        );

    const double targetAspect =
        static_cast<double>(
            sysMonDesignWidth
        ) /
        static_cast<double>(
            sysMonDesignHeight
        );

    auto applyWidth =
        [&](int newWindowWidth)
    {
        if (
            sizingEdge == WMSZ_LEFT ||
            sizingEdge == WMSZ_TOPLEFT ||
            sizingEdge == WMSZ_BOTTOMLEFT
        )
        {
            proposedWindowRect->left =
                proposedWindowRect->right -
                newWindowWidth;
        }
        else
        {
            proposedWindowRect->right =
                proposedWindowRect->left +
                newWindowWidth;
        }
    };

    auto applyHeight =
        [&](int newWindowHeight)
    {
        if (
            sizingEdge == WMSZ_TOP ||
            sizingEdge == WMSZ_TOPLEFT ||
            sizingEdge == WMSZ_TOPRIGHT
        )
        {
            proposedWindowRect->top =
                proposedWindowRect->bottom -
                newWindowHeight;
        }
        else
        {
            proposedWindowRect->bottom =
                proposedWindowRect->top +
                newWindowHeight;
        }
    };

    const bool horizontalOnly =
        sizingEdge == WMSZ_LEFT ||
        sizingEdge == WMSZ_RIGHT;

    const bool verticalOnly =
        sizingEdge == WMSZ_TOP ||
        sizingEdge == WMSZ_BOTTOM;

    if (horizontalOnly)
    {
        const int targetClientHeight =
            (std::max)(
                1,
                static_cast<int>(
                    proposedClientWidth /
                    targetAspect +
                    0.5
                )
            );

        applyHeight(
            targetClientHeight +
            nonClientHeight
        );

        return;
    }

    if (verticalOnly)
    {
        const int targetClientWidth =
            (std::max)(
                1,
                static_cast<int>(
                    proposedClientHeight *
                    targetAspect +
                    0.5
                )
            );

        applyWidth(
            targetClientWidth +
            nonClientWidth
        );

        return;
    }

    // Corner drag: alter whichever dimension requires the smaller visual
    // correction. This makes diagonal resizing feel natural instead of
    // making the window jump aggressively.
    const int clientHeightFromWidth =
        (std::max)(
            1,
            static_cast<int>(
                proposedClientWidth /
                targetAspect +
                0.5
            )
        );

    const int clientWidthFromHeight =
        (std::max)(
            1,
            static_cast<int>(
                proposedClientHeight *
                targetAspect +
                0.5
            )
        );

    const int heightCorrection =
        std::abs(
            clientHeightFromWidth -
            proposedClientHeight
        );

    const int widthCorrection =
        std::abs(
            clientWidthFromHeight -
            proposedClientWidth
        );

    if (heightCorrection <= widthCorrection)
    {
        applyHeight(
            clientHeightFromWidth +
            nonClientHeight
        );
    }
    else
    {
        applyWidth(
            clientWidthFromHeight +
            nonClientWidth
        );
    }
}


static POINT sysMonClientToLogicalPoint(
    HWND hwnd,
    LPARAM lParam)
{
    const int physicalX =
        static_cast<short>(LOWORD(lParam));

    const int physicalY =
        static_cast<short>(HIWORD(lParam));

    const SysMonUiTransform transform =
        getSysMonUiTransform(hwnd);

    POINT point = {};

    point.x =
        static_cast<LONG>(
            (physicalX - transform.offsetX) /
            transform.scaleX
        );

    point.y =
        static_cast<LONG>(
            (physicalY - transform.offsetY) /
            transform.scale
        );

    if (currentPage == AppPage::Dashboard && point.y >= 58 && point.x >= 165 &&
        transform.scaleX > transform.scale + 0.001)
    {
        point.x = 165 + static_cast<LONG>((point.x - 165) / sysMonDashboardWideFactor);
    }
    return point;
}


// --------------------------------------------------------
// REUSABLE FULL-RESOLUTION FRAME BUFFER
// --------------------------------------------------------
// Creating/deleting a full-screen compatible bitmap on every 500 ms paint
// is expensive (especially at 1440p/4K) and was a major source of UI lag.
// Keep one back buffer and only grow it when the window needs more space.
static HDC sysMonFrameDC = nullptr;
static HBITMAP sysMonFrameBitmap = nullptr;
static HGDIOBJ sysMonFrameOldBitmap = nullptr;
static int sysMonFrameCapacityWidth = 0;
static int sysMonFrameCapacityHeight = 0;
static bool sysMonLiveResizing = false;
static HBRUSH sysMonShellBrush = nullptr;
static HBRUSH sysMonHeaderExtensionBrush = nullptr;
static int sysMonLastRenderedWidth = 0;
static int sysMonLastRenderedHeight = 0;
static ULONGLONG sysMonLastUserInteractionTick = 0;
static ULONGLONG sysMonNavigationBoostUntilTick = 0;
static bool sysMonProcessScrollPaintPending = false;

// Fast tab-switch preview surface. The target page is first rendered at
// SysMon's logical 1190x720 size and shown immediately. A short one-shot
// timer then replaces it with the normal full-resolution frame.
static HDC sysMonNavigationPreviewDC = nullptr;
static HBITMAP sysMonNavigationPreviewBitmap = nullptr;
static HGDIOBJ sysMonNavigationPreviewOldBitmap = nullptr;
static bool sysMonNavigationPreviewPending = false;
static bool settingsTransparencyDragging = false;
bool sysMonOverlayHotkeyAvailable = false;

static RECT getProcessTablePhysicalRect(HWND hwnd)
{
    const SysMonUiTransform transform = getSysMonUiTransform(hwnd);

    // Processes content uses a logical X origin of 165. The table itself is
    // x=20..700 and y=275..690. Add a few pixels of breathing room so rounded
    // corners and the new scrollbar are always included in the update region.
    const int logicalLeft = 180;
    const int logicalTop = 270;
    const int logicalRight = 870;
    const int logicalBottom = 696;

    RECT result = {};
    result.left = transform.offsetX + static_cast<LONG>(logicalLeft * transform.scaleX) - 2;
    result.top = transform.offsetY + static_cast<LONG>(logicalTop * transform.scale) - 2;
    result.right = transform.offsetX + static_cast<LONG>(logicalRight * transform.scaleX + 0.5) + 2;
    result.bottom = transform.offsetY + static_cast<LONG>(logicalBottom * transform.scale + 0.5) + 2;
    return result;
}

static void invalidateProcessTableFast(HWND hwnd)
{
    sysMonProcessScrollPaintPending = true;
    const RECT rect = getProcessTablePhysicalRect(hwnd);
    InvalidateRect(hwnd, &rect, FALSE);
}

static bool ensureSysMonNavigationPreviewBuffer(HDC referenceDC)
{
    if (
        sysMonNavigationPreviewDC != nullptr &&
        sysMonNavigationPreviewBitmap != nullptr
    )
    {
        return true;
    }

    sysMonNavigationPreviewDC = CreateCompatibleDC(referenceDC);

    if (sysMonNavigationPreviewDC == nullptr)
    {
        return false;
    }

    sysMonNavigationPreviewBitmap =
        CreateCompatibleBitmap(
            referenceDC,
            sysMonDesignWidth,
            sysMonDesignHeight
        );

    if (sysMonNavigationPreviewBitmap == nullptr)
    {
        DeleteDC(sysMonNavigationPreviewDC);
        sysMonNavigationPreviewDC = nullptr;
        return false;
    }

    sysMonNavigationPreviewOldBitmap =
        SelectObject(
            sysMonNavigationPreviewDC,
            sysMonNavigationPreviewBitmap
        );

    return true;
}

static void releaseSysMonNavigationPreviewBuffer()
{
    if (sysMonNavigationPreviewDC != nullptr)
    {
        if (
            sysMonNavigationPreviewBitmap != nullptr &&
            sysMonNavigationPreviewOldBitmap != nullptr
        )
        {
            SelectObject(
                sysMonNavigationPreviewDC,
                sysMonNavigationPreviewOldBitmap
            );
        }

        if (sysMonNavigationPreviewBitmap != nullptr)
        {
            DeleteObject(sysMonNavigationPreviewBitmap);
            sysMonNavigationPreviewBitmap = nullptr;
        }

        DeleteDC(sysMonNavigationPreviewDC);
        sysMonNavigationPreviewDC = nullptr;
    }

    sysMonNavigationPreviewOldBitmap = nullptr;
}

// SysMon.ini is written with ~25 WritePrivateProfileString calls, each of
// which touches the file. Doing that inline on every toggle click made the
// Settings page feel sticky, so writes are coalesced onto a short one-shot
// timer and flushed on exit. appSettings is always the source of truth, and
// saveAppSettings() writes the whole struct, so a coalesced write is complete.
static bool sysMonSettingsSaveDue = false;

static void requestSettingsSave(HWND hwnd)
{
    sysMonSettingsSaveDue = true;
    SetTimer(hwnd, 5, 400, nullptr);
}

static void flushPendingSettingsSave(HWND hwnd)
{
    if (!sysMonSettingsSaveDue)
    {
        return;
    }

    KillTimer(hwnd, 5);
    sysMonSettingsSaveDue = false;
    if (!saveAppSettings())
        MessageBoxA(hwnd,"Your changes are active, but SysMon could not save them. Check that the app folder is writable.","SysMon settings",MB_OK|MB_ICONWARNING);
}

// Navigation should feel immediate. Show a cheap logical-resolution frame
// synchronously, then let a one-shot timer request the sharp final render.
static void requestImmediateSysMonPaint(HWND hwnd)
{
    // Keep expensive hardware collectors out of the way long enough for both
    // the instant preview and the sharp follow-up frame to complete.
    sysMonNavigationBoostUntilTick = GetTickCount64() + 900;

    sysMonProcessScrollPaintPending = false;
    sysMonNavigationPreviewPending = appSettings.showAnimations;

    InvalidateRect(hwnd, nullptr, FALSE);

    // WM_PAINT has low message-queue priority. Force only the cheap preview
    // now so the selected tab/page changes visually as part of this click.
    if (!sysMonLiveResizing)
    {
        UpdateWindow(hwnd);
    }
}

static int roundFrameCapacity(int value)
{
    const int quantum = 256;
    return ((std::max)(1, value) + quantum - 1) / quantum * quantum;
}

static bool ensureSysMonFrameBuffer(
    HDC referenceDC,
    int requiredWidth,
    int requiredHeight)
{
    requiredWidth = (std::max)(1, requiredWidth);
    requiredHeight = (std::max)(1, requiredHeight);

    if (sysMonFrameDC == nullptr)
    {
        sysMonFrameDC = CreateCompatibleDC(referenceDC);
        if (sysMonFrameDC == nullptr)
        {
            return false;
        }
    }

    if (
        sysMonFrameBitmap != nullptr &&
        requiredWidth <= sysMonFrameCapacityWidth &&
        requiredHeight <= sysMonFrameCapacityHeight
    )
    {
        return true;
    }

    const int newWidth =
        (std::max)(
            roundFrameCapacity(requiredWidth),
            sysMonFrameCapacityWidth
        );

    const int newHeight =
        (std::max)(
            roundFrameCapacity(requiredHeight),
            sysMonFrameCapacityHeight
        );

    HBITMAP newBitmap =
        CreateCompatibleBitmap(
            referenceDC,
            newWidth,
            newHeight
        );

    if (newBitmap == nullptr)
    {
        return false;
    }

    if (sysMonFrameBitmap != nullptr)
    {
        SelectObject(
            sysMonFrameDC,
            sysMonFrameOldBitmap
        );

        DeleteObject(sysMonFrameBitmap);
        sysMonFrameBitmap = nullptr;
    }

    sysMonFrameOldBitmap =
        SelectObject(
            sysMonFrameDC,
            newBitmap
        );

    sysMonFrameBitmap = newBitmap;
    sysMonFrameCapacityWidth = newWidth;
    sysMonFrameCapacityHeight = newHeight;

    return true;
}

static void releaseSysMonFrameBuffer()
{
    if (sysMonFrameDC != nullptr)
    {
        if (
            sysMonFrameBitmap != nullptr &&
            sysMonFrameOldBitmap != nullptr
        )
        {
            SelectObject(
                sysMonFrameDC,
                sysMonFrameOldBitmap
            );
        }

        if (sysMonFrameBitmap != nullptr)
        {
            DeleteObject(sysMonFrameBitmap);
            sysMonFrameBitmap = nullptr;
        }

        DeleteDC(sysMonFrameDC);
        sysMonFrameDC = nullptr;
    }

    sysMonFrameOldBitmap = nullptr;
    sysMonFrameCapacityWidth = 0;
    sysMonFrameCapacityHeight = 0;
    sysMonLastRenderedWidth = 0;
    sysMonLastRenderedHeight = 0;

    if (sysMonShellBrush != nullptr)
    {
        DeleteObject(sysMonShellBrush);
        sysMonShellBrush = nullptr;
    }

    if (sysMonHeaderExtensionBrush != nullptr)
    {
        DeleteObject(sysMonHeaderExtensionBrush);
        sysMonHeaderExtensionBrush = nullptr;
    }
}


// Real SysMon icon used by the taskbar / Alt-Tab.
// The native caption icon is hidden separately with WS_EX_DLGMODALFRAME,
// so the title bar stays clean while Windows can still show logo.png
// everywhere an application icon is expected.
static HICON sysMonTaskbarIcon = nullptr;

static HICON loadSysMonTaskbarIcon()
{
    HICON embedded = static_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    if (embedded) return embedded;
    Gdiplus::Bitmap logo(
        L"logos/logo.png"
    );

    if (logo.GetLastStatus() != Gdiplus::Ok)
    {
        return nullptr;
    }

    HICON icon = nullptr;

    if (logo.GetHICON(&icon) != Gdiplus::Ok)
    {
        return nullptr;
    }

    return icon;
}

static void applySysMonWindowIcon(
    HWND hwnd)
{
    if (sysMonTaskbarIcon == nullptr)
    {
        sysMonTaskbarIcon =
            loadSysMonTaskbarIcon();
    }

    if (sysMonTaskbarIcon != nullptr)
    {
        // Windows can use these for the taskbar, Alt-Tab and window switching.
        SendMessageW(
            hwnd,
            WM_SETICON,
            ICON_BIG,
            reinterpret_cast<LPARAM>(
                sysMonTaskbarIcon
            )
        );

        SendMessageW(
            hwnd,
            WM_SETICON,
            ICON_SMALL,
            reinterpret_cast<LPARAM>(
                sysMonTaskbarIcon
            )
        );

        SetClassLongPtrW(
            hwnd,
            GCLP_HICON,
            reinterpret_cast<LONG_PTR>(
                sysMonTaskbarIcon
            )
        );

        SetClassLongPtrW(
            hwnd,
            GCLP_HICONSM,
            reinterpret_cast<LONG_PTR>(
                sysMonTaskbarIcon
            )
        );
    }
}

static void hideSysMonCaptionIcon(
    HWND hwnd)
{
    // This removes the native caption icon without removing the window's
    // real application icon. The taskbar / Alt-Tab can therefore keep
    // using logo.png while the title bar remains visually empty on the left.
    LONG_PTR extendedStyle =
        GetWindowLongPtrW(
            hwnd,
            GWL_EXSTYLE
        );

    extendedStyle |=
        WS_EX_DLGMODALFRAME;

    SetWindowLongPtrW(
        hwnd,
        GWL_EXSTYLE,
        extendedStyle
    );

    SetWindowPos(
        hwnd,
        nullptr,
        0,
        0,
        0,
        0,
        SWP_NOMOVE |
        SWP_NOSIZE |
        SWP_NOZORDER |
        SWP_NOACTIVATE |
        SWP_FRAMECHANGED
    );
}


// --------------------------------------------------------
// Native Windows title-bar styling
// --------------------------------------------------------
// DwmSetWindowAttribute is loaded dynamically so SysMon keeps the same
// MinGW link command and still runs on systems where a newer DWM attribute
// is unavailable.
static void applySysMonTitleBarTheme(
    HWND hwnd)
{
    HMODULE dwmModule =
        LoadLibraryW(
            L"dwmapi.dll"
        );

    if (dwmModule == nullptr)
    {
        return;
    }

    using DwmSetWindowAttributeFn =
        HRESULT (WINAPI *)(
            HWND,
            DWORD,
            LPCVOID,
            DWORD
        );

    auto setWindowAttribute =
        reinterpret_cast<
            DwmSetWindowAttributeFn
        >(
            GetProcAddress(
                dwmModule,
                "DwmSetWindowAttribute"
            )
        );

    if (setWindowAttribute != nullptr)
    {
        // Windows 10 20H1+ / Windows 11 dark caption buttons and glyphs.
        BOOL darkMode = isLightTheme() ? FALSE : TRUE;

        HRESULT darkResult =
            setWindowAttribute(
                hwnd,
                20, // DWMWA_USE_IMMERSIVE_DARK_MODE
                &darkMode,
                sizeof(darkMode)
            );

        // Older Windows 10 builds used attribute 19.
        if (FAILED(darkResult))
        {
            setWindowAttribute(
                hwnd,
                19,
                &darkMode,
                sizeof(darkMode)
            );
        }

        // Match the native caption to the SysMon header.
        COLORREF captionColor =
            uiColor(RGB(14, 18, 24));

        COLORREF captionTextColor =
            uiColor(RGB(210, 219, 228));

        COLORREF borderColor =
            uiColor(RGB(27, 44, 58));

        setWindowAttribute(
            hwnd,
            35, // DWMWA_CAPTION_COLOR
            &captionColor,
            sizeof(captionColor)
        );

        setWindowAttribute(
            hwnd,
            36, // DWMWA_TEXT_COLOR
            &captionTextColor,
            sizeof(captionTextColor)
        );

        setWindowAttribute(
            hwnd,
            34, // DWMWA_BORDER_COLOR
            &borderColor,
            sizeof(borderColor)
        );

        // Prefer the normal rounded Windows 11 corner treatment.
        DWORD cornerPreference = 2; // DWMWCP_ROUND

        setWindowAttribute(
            hwnd,
            33, // DWMWA_WINDOW_CORNER_PREFERENCE
            &cornerPreference,
            sizeof(cornerPreference)
        );
    }

    FreeLibrary(
        dwmModule
    );
}

int processMaxScrollOffset = 0;
ULONG_PTR gdiplusToken;
int processScrollbarThumbTop = 221;
int processScrollbarThumbBottom = 261;

bool processScrollbarDragging = false;
int processScrollbarDragOffsetY = 0;
bool processScrollbarVisible = false;

AppPage currentPage =
    AppPage::Dashboard;
    int processScrollOffset = 0;
    ProcessSort processSort =
    ProcessSort::Memory;
bool processSortDescending =
    true;
    PerformanceView performanceView =
    PerformanceView::Overview;
    int cpuHistoryRangeSeconds = 60;
    bool cpuHistoryRangeDropdownOpen = false;
    int selectedDiskIndex = 0;
    int selectedGpuIndex = 0;
    int selectedNetworkIndex = 0;
    TemperatureView temperatureView =
        TemperatureView::CPU;
    int selectedTemperatureGpuIndex = 0;
    int systemInfoScrollOffset = 0;
    int systemInfoMaxScrollOffset = 0;
    int settingsScrollOffset = 0;
    int settingsMaxScrollOffset = SettingsLayout::maxScrollOffset();
    int systemInfoSelectedDiskIndex = 0;
    int systemInfoSelectedGpuIndex = 0;
    std::string selectedConnectedDeviceKey = "";
    std::string processSearch = "";
    bool processSearchFocused = false;
    ProcessFilterMode processFilterMode =
        ProcessFilterMode::All;
    bool processFilterDropdownOpen = false;
    DWORD selectedProcessPid = MAXDWORD;
    bool processPriorityDropdownOpen = false;
    DWORD hoveredProcessPid = MAXDWORD;
    std::vector<DWORD> visibleProcessPids;
    std::vector<DWORD> visibleProcessGroupKeys;
    std::vector<DWORD> expandedProcessGroupKeys;
void drawDashboard(
    HWND hwnd,
    HDC hdc
);
RECT widgetToggleRect =
{
    600,
    575,
    755,
    605
};
// Desktop widget
int savedWidgetX = -1;
int savedWidgetY = -1;
// Fonts
HFONT titleFont;
HFONT subtitleFont;
HFONT labelFont;
HFONT mediumFont;
HFONT bigFont;
HFONT smallFont;
static bool getProcessExecutablePathForAction(
    DWORD pid,
    std::wstring& path)
{
    path.clear();

    HANDLE processHandle =
        OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            pid
        );

    if (processHandle == nullptr)
    {
        return false;
    }

    std::vector<wchar_t> buffer(32768);
    DWORD length =
        static_cast<DWORD>(
            buffer.size()
        );

    bool result =
        QueryFullProcessImageNameW(
            processHandle,
            0,
            buffer.data(),
            &length
        ) != FALSE;

    if (result)
    {
        path.assign(
            buffer.data(),
            length
        );
    }

    CloseHandle(processHandle);
    return result && !path.empty();
}

static bool openProcessFileLocation(
    HWND hwnd,
    DWORD pid)
{
    std::wstring executablePath;

    if (!getProcessExecutablePathForAction(
            pid,
            executablePath
        ))
    {
        return false;
    }

    std::wstring arguments =
        L"/select,\"" +
        executablePath +
        L"\"";

    HINSTANCE result =
        ShellExecuteW(
            hwnd,
            L"open",
            L"explorer.exe",
            arguments.c_str(),
            nullptr,
            SW_SHOWNORMAL
        );

    return
        reinterpret_cast<INT_PTR>(
            result
        ) > 32;
}

static bool setProcessPriorityClassByPid(
    DWORD pid,
    DWORD priorityClass)
{
    HANDLE processHandle =
        OpenProcess(
            PROCESS_SET_INFORMATION |
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            pid
        );

    if (processHandle == nullptr)
    {
        return false;
    }

    BOOL result =
        SetPriorityClass(
            processHandle,
            priorityClass
        );

    CloseHandle(processHandle);
    return result != FALSE;
}


static bool launchWindowsTarget(
    HWND hwnd,
    const wchar_t* target,
    const wchar_t* parameters = nullptr,
    bool runAsAdministrator = false)
{
    HINSTANCE result =
        ShellExecuteW(
            hwnd,
            runAsAdministrator
                ? L"runas"
                : L"open",
            target,
            parameters,
            nullptr,
            SW_SHOWNORMAL
        );

    return
        reinterpret_cast<INT_PTR>(
            result
        ) > 32;
}

static bool runHiddenCommandAndWait(
    const std::wstring& command,
    DWORD& exitCode)
{
    std::wstring commandLine =
        L"cmd.exe /c " + command;

    std::vector<wchar_t> buffer(
        commandLine.begin(),
        commandLine.end()
    );
    buffer.push_back(L'\0');

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION processInfo = {};

    BOOL created =
        CreateProcessW(
            nullptr,
            buffer.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo
        );

    if (!created)
    {
        return false;
    }

    WaitForSingleObject(
        processInfo.hProcess,
        15000
    );

    exitCode = MAXDWORD;
    GetExitCodeProcess(
        processInfo.hProcess,
        &exitCode
    );

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    return exitCode == 0;
}

static bool createSysMonSnapshot(
    std::string& outputPath)
{
    SYSTEMTIME now = {};
    GetLocalTime(&now);

    char fileName[MAX_PATH] = {};
    std::snprintf(
        fileName,
        sizeof(fileName),
        "SysMon_Snapshot_%04u%02u%02u_%02u%02u%02u.txt",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond
    );

    char currentDirectory[MAX_PATH] = {};
    DWORD directoryLength =
        GetCurrentDirectoryA(
            MAX_PATH,
            currentDirectory
        );

    if (
        directoryLength == 0 ||
        directoryLength >= MAX_PATH
    )
    {
        return false;
    }

    outputPath =
        std::string(currentDirectory) +
        "\\" +
        fileName;

    std::ostringstream snapshot;
    snapshot
        << "SysMon System Snapshot\r\n"
        << "======================\r\n"
        << "Computer: " << systemInfo.computerName << "\r\n"
        << "OS: " << systemInfo.osName << " " << systemInfo.osVersion << "\r\n"
        << "CPU: " << systemInfo.cpuName << "\r\n"
        << "CPU Usage: " << std::fixed << std::setprecision(1) << cpuUsage << "%\r\n"
        << "Memory: " << usedRamGB << " GB / " << totalRamGB << " GB (" << ramPercent << "%)\r\n"
        << "Uptime: " << uptimeSeconds << " seconds\r\n";

    if (temperatureStats.cpuTemperatureC >= 0.0)
    {
        snapshot
            << "CPU Temperature: "
            << temperatureStats.cpuTemperatureC
            << " C\r\n";
    }

    snapshot << "\r\nPhysical Disks\r\n";
    for (const auto& disk : diskStats)
    {
        snapshot
            << "- Disk " << disk.diskNumber
            << ": " << disk.model
            << ", Active " << disk.activeTimePercent << "%"
            << ", Read " << disk.readMBps << " MB/s"
            << ", Write " << disk.writeMBps << " MB/s\r\n";
    }

    snapshot << "\r\nGPUs\r\n";
    for (const auto& gpu : gpuStats)
    {
        snapshot
            << "- GPU " << gpu.index
            << ": " << gpu.name
            << ", Utilization " << gpu.utilizationPercent << "%";

        if (gpu.temperatureC >= 0.0)
        {
            snapshot
                << ", Temperature "
                << gpu.temperatureC
                << " C";
        }

        snapshot << "\r\n";
    }

    snapshot << "\r\nNetwork Adapters\r\n";
    for (const auto& adapter : networkStats)
    {
        snapshot
            << "- " << adapter.name
            << ": " << adapter.status
            << ", Download " << adapter.downloadMbps << " Mbps"
            << ", Upload " << adapter.uploadMbps << " Mbps\r\n";
    }

    std::string contents = snapshot.str();

    HANDLE fileHandle =
        CreateFileA(
            outputPath.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

    if (fileHandle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    DWORD bytesWritten = 0;
    BOOL written =
        WriteFile(
            fileHandle,
            contents.data(),
            static_cast<DWORD>(
                contents.size()
            ),
            &bytesWritten,
            nullptr
        );

    CloseHandle(fileHandle);

    return
        written != FALSE &&
        bytesWritten == contents.size();
}


static bool registerSysMonLogFileAssociation()
{
    char executablePath[MAX_PATH] = {};
    GetModuleFileNameA(
        nullptr,
        executablePath,
        MAX_PATH
    );

    HKEY extensionKey = nullptr;
    if (RegCreateKeyExA(
            HKEY_CURRENT_USER,
            "Software\\Classes\\.sysmonlog",
            0,
            nullptr,
            0,
            KEY_SET_VALUE,
            nullptr,
            &extensionKey,
            nullptr) != ERROR_SUCCESS)
    {
        return false;
    }

    const char className[] = "SysMon.LogFile";
    const LONG extensionResult = RegSetValueExA(
        extensionKey,
        nullptr,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(className),
        sizeof(className)
    );
    RegCloseKey(extensionKey);
    if(extensionResult!=ERROR_SUCCESS) return false;

    HKEY commandKey = nullptr;
    if (RegCreateKeyExA(
            HKEY_CURRENT_USER,
            "Software\\Classes\\SysMon.LogFile\\shell\\open\\command",
            0,
            nullptr,
            0,
            KEY_SET_VALUE,
            nullptr,
            &commandKey,
            nullptr) != ERROR_SUCCESS)
    {
        return false;
    }

    const std::string command =
        std::string("notepad.exe \"%1\"");

    const LONG result =
        RegSetValueExA(
            commandKey,
            nullptr,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>(command.size() + 1)
        );

    RegCloseKey(commandKey);
    if(result==ERROR_SUCCESS) SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr);
    return result == ERROR_SUCCESS;
}

static void applyOverlayWidgetSetting()
{
    widgetEnabled =
        appSettings.showOverlayWidget;

    saveWidgetEnabled();

    if (desktopWidget == nullptr)
    {
        return;
    }

    if (widgetEnabled)
    {
        positionDesktopWidget(desktopWidget);
        ShowWindow(
            desktopWidget,
            SW_SHOWNOACTIVATE
        );
        SetTimer(
            desktopWidget,
            2,
            static_cast<UINT>(
                (std::max)(250, appSettings.updateIntervalMs)
            ),
            nullptr
        );
    }
    else
    {
        KillTimer(
            desktopWidget,
            2
        );
        ShowWindow(
            desktopWidget,
            SW_HIDE
        );
    }
}

static void restartMonitoringTimer(HWND hwnd)
{
    if (desktopWidget && widgetEnabled) SetTimer(desktopWidget,2,static_cast<UINT>((std::max)(250,appSettings.updateIntervalMs)),nullptr);
    KillTimer(hwnd, 1);
    SetTimer(
        hwnd,
        1,
        static_cast<UINT>(
            (std::max)(250, appSettings.updateIntervalMs)
        ),
        nullptr
    );
}

static void applyThemeToWindow(HWND hwnd)
{
    refreshThemePreference();
    applySysMonTitleBarTheme(hwnd);
    if (sysMonShellBrush) { DeleteObject(sysMonShellBrush); sysMonShellBrush=nullptr; }
    if (sysMonHeaderExtensionBrush) { DeleteObject(sysMonHeaderExtensionBrush); sysMonHeaderExtensionBrush=nullptr; }
    KillTimer(hwnd,4);
    sysMonNavigationPreviewPending=false;
    sysMonLastRenderedWidth=0;
    sysMonLastRenderedHeight=0;
    InvalidateRect(hwnd,nullptr,FALSE);
    if (desktopWidget) InvalidateRect(desktopWidget,nullptr,FALSE);
}

static bool minimizeSysMonToTray(HWND hwnd)
{
    // Never hide the only usable window unless the tray icon was created.
    if (!addTrayIcon(hwnd)) return false;
    ShowWindow(hwnd,SW_HIDE);
    if (widgetEnabled && appSettings.showOverlayWidget && desktopWidget) {
        updateStats();
        ShowWindow(desktopWidget,SW_SHOWNOACTIVATE);
        SetTimer(desktopWidget,2,static_cast<UINT>((std::max)(250,appSettings.updateIntervalMs)),nullptr);
        InvalidateRect(desktopWidget,nullptr,FALSE);
    }
    return true;
}

static void applySavedStartupPreference(HWND hwnd)
{
    if (!setStartWithWindowsEnabled(appSettings.startWithWindows)) {
        appSettings.startWithWindows=isStartWithWindowsEnabled();
        saveAppSettings();
        MessageBoxA(hwnd,"Windows could not apply the startup setting. The switch now reflects the registered startup task.",
            "SysMon",MB_OK|MB_ICONWARNING);
    }
}

static void openSettingsTarget(HWND hwnd, const char* target)
{
    const auto result=reinterpret_cast<INT_PTR>(ShellExecuteA(hwnd,"open",target,nullptr,nullptr,SW_SHOWNORMAL));
    if(result<=32) MessageBoxA(hwnd,"Windows could not open this item. Check the file or folder and its default application.","SysMon",MB_OK|MB_ICONWARNING);
}

static int chooseSetting(HWND hwnd, int current, const std::vector<std::pair<int,std::string>>& choices)
{
    namespace SL=SettingsLayout;
    POINT cursor{};GetCursorPos(&cursor);POINT client=cursor;ScreenToClient(hwnd,&client);
    const auto logical=sysMonClientToLogicalPoint(hwnd,MAKELPARAM(client.x,client.y));
    const auto transform=getSysMonUiTransform(hwnd);
    RECT anchor{cursor.x,cursor.y,cursor.x+140,cursor.y+1};
    const SL::Rect fields[]={SL::comboRect(0,SL::topRowTop,3),SL::comboRect(1,SL::topRowTop,0),SL::comboRect(1,SL::topRowTop,1),SL::comboRect(1,SL::topRowTop,2),SL::comboRect(2,SL::topRowTop,4),SL::comboRect(1,SL::middleRowTop,1),SL::valueRect(0,SL::middleRowTop,0),SL::valueRect(0,SL::middleRowTop,1),SL::valueRect(0,SL::middleRowTop,2),SL::valueRect(0,SL::middleRowTop,3)};
    for(const auto& field:fields)if(logical.x-165>=field.left&&logical.x-165<=field.right&&logical.y+settingsScrollOffset>=field.top&&logical.y+settingsScrollOffset<=field.bottom){
        POINT corner{static_cast<LONG>((field.left+165)*transform.scaleX+transform.offsetX),static_cast<LONG>((field.top-settingsScrollOffset)*transform.scale+transform.offsetY)};
        POINT end{static_cast<LONG>((field.right+165)*transform.scaleX+transform.offsetX),static_cast<LONG>((field.bottom-settingsScrollOffset)*transform.scale+transform.offsetY)};
        ClientToScreen(hwnd,&corner);ClientToScreen(hwnd,&end);anchor={corner.x,corner.y,end.x,end.y};break;
    }
    auto available=choices;
    if(std::none_of(available.begin(),available.end(),[&](const auto& choice){return choice.first==current;}))available.insert(available.begin(),{current,std::to_string(current)+" (current)"});
    return SettingsDropdown::show(hwnd,anchor,current,available);
}

static void pollSettingsServices(HWND hwnd)
{
    using namespace SettingsRuntime;
    Sample sample;
    sample.cpu=cpuUsage; sample.memory=totalRamGB>0 ? ramPercent : -1;
    sample.disk=totalDiskGB>0 ? diskPercent : -1;
    sample.cpuC=temperatureStats.cpuTemperatureC;
    for(const auto& gpu:gpuStats) sample.gpuC=(std::max)(sample.gpuC,gpu.temperatureC);
    for(const auto& adapter:networkStats) { sample.down+=adapter.downloadMbps; sample.up+=adapter.uploadMbps; }
    const std::string alert=alerts.evaluate(sample,appSettings);
    if(!alert.empty()) {
        if(appSettings.logAlertsToFile) logs.enqueue(getSysMonDataFolderPath(),alert,appSettings.logRetentionDays,true);
        if(appSettings.playAlerts) MessageBeep(MB_ICONWARNING);
        if(appSettings.showDesktopNotifications && addTrayIcon(hwnd)) {
            NOTIFYICONDATAA notification={}; notification.cbSize=sizeof(notification); notification.hWnd=hwnd;
            notification.uID=1; notification.uFlags=NIF_INFO; notification.dwInfoFlags=NIIF_WARNING|NIIF_NOSOUND;
            lstrcpynA(notification.szInfoTitle,"SysMon alert",sizeof(notification.szInfoTitle));
            lstrcpynA(notification.szInfo,alert.c_str(),sizeof(notification.szInfo));
            Shell_NotifyIconA(NIM_MODIFY,&notification);
            if(!appSettings.showInTray) SetTimer(hwnd,8,15000,nullptr);
        }
    }
    if(appSettings.enableDataLogging && totalRamGB>0) {
        std::ostringstream row; row << "cpu_percent=" << sample.cpu << ",memory_percent=" << sample.memory
            << ",disk_used_percent=" << sample.disk << ",cpu_c=" << sample.cpuC << ",gpu_c=" << sample.gpuC
            << ",download_mbps=" << sample.down << ",upload_mbps=" << sample.up;
        logs.enqueue(getSysMonDataFolderPath(),row.str(),appSettings.logRetentionDays);
    }
    static std::string reportedError;
    const std::string error=logs.error();
    if(!error.empty() && error!=reportedError) {
        reportedError=error;
        MessageBoxA(hwnd,("SysMon could not save a log: "+error).c_str(),"Logging error",MB_OK|MB_ICONWARNING);
    }
}

LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    static const UINT taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    if (message==taskbarCreated && (!IsWindowVisible(hwnd) || appSettings.showInTray)) {
        if(!addTrayIcon(hwnd) && !IsWindowVisible(hwnd)) restoreSysMon(hwnd);
        return 0;
    }
    switch (message)
    {
    case WM_MEASUREITEM:
        if(SettingsDropdown::measure(reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)))return TRUE;
        break;
    case WM_DRAWITEM:
        if(SettingsDropdown::draw(reinterpret_cast<DRAWITEMSTRUCT*>(lParam)))return TRUE;
        break;
    case WM_CONTEXTMENU:
    if(currentPage==AppPage::Settings) {
        HMENU menu=CreatePopupMenu();AppendMenuA(menu,MF_STRING,1,"Check for updates now");AppendMenuA(menu,MF_STRING,2,"Version and release notes");AppendMenuA(menu,MF_STRING,3,"Open GitHub Release");AppendMenuA(menu,MF_STRING,4,"Dependencies and licenses");
        POINT cursor;GetCursorPos(&cursor);int command=TrackPopupMenu(menu,TPM_RETURNCMD,cursor.x,cursor.y,0,hwnd,nullptr);DestroyMenu(menu);
        if(command==1){Updates::sampler().request();Updates::pending=true;Updates::status="Checking GitHub releases...";}
        if(command==2){std::string details="Current version: " SYSMON_VERSION_STRING "\nLatest version: ";details+=Updates::displayed?Updates::displayed->version:"Not checked";details+="\n\n";details+=Updates::displayed?Updates::displayed->status:"Choose Check for updates now first.";if(Updates::displayed)details+="\n\nRelease notes:\n"+Updates::displayed->notes;MessageBoxA(hwnd,details.c_str(),"SysMon updates",MB_OK|MB_ICONINFORMATION);}
        if(command==3)ShellExecuteA(hwnd,"open",Updates::releasePage,nullptr,nullptr,SW_SHOWNORMAL);
        if(command==4)openSettingsTarget(hwnd,(getSettingsPath().substr(0,getSettingsPath().find_last_of("\\/"))+"\\THIRD-PARTY-NOTICES.txt").c_str());
        return 0;
    }
    break;
case WM_KEYDOWN:
    if(GetKeyState(VK_CONTROL)&0x8000) {
        if(wParam=='Q') {DestroyWindow(hwnd);return 0;}
        if(wParam=='P') {setMonitoringPaused(!monitoringPaused);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        if(wParam==VK_OEM_COMMA) {currentPage=AppPage::Settings;InvalidateRect(hwnd,nullptr,FALSE);return 0;}
    }
    break;
case WM_DPICHANGED: {
    const RECT* bounds=reinterpret_cast<RECT*>(lParam);
    SetWindowPos(hwnd,nullptr,bounds->left,bounds->top,bounds->right-bounds->left,bounds->bottom-bounds->top,SWP_NOZORDER|SWP_NOACTIVATE);
    InvalidateRect(hwnd,nullptr,FALSE); return 0;
}
case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        if (appSettings.theme==2) applyThemeToWindow(hwnd);
        return 0;
    case WM_CREATE:
    {
        ChangeWindowMessageFilterEx(hwnd,taskbarCreated,MSGFLT_ALLOW,nullptr);
        loadAppSettings();
        if (appSettings.startWithWindows) applySavedStartupPreference(hwnd);
        else appSettings.startWithWindows=isStartWithWindowsEnabled();
        SetTimer(hwnd,7,1000,nullptr);
        applyMainWindowTransparency(hwnd);
        if(appSettings.showInTray) addTrayIcon(hwnd);
        sysMonOverlayHotkeyAvailable = RegisterHotKey(
            hwnd,
            1,
            MOD_CONTROL | MOD_ALT,
            'O'
        );

        titleFont =
            CreateFontA(
                36,
                0,
                0,
                0,
                FW_BOLD,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                "Segoe UI"
            );

        subtitleFont =
            CreateFontA(
                17,
                0,
                0,
                0,
                FW_NORMAL,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                "Segoe UI"
            );

        labelFont =
            CreateFontA(
                18,
                0,
                0,
                0,
                FW_SEMIBOLD,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                "Segoe UI"
            );

        mediumFont =
            CreateFontA(
                27,
                0,
                0,
                0,
                FW_BOLD,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                "Segoe UI"
            );

        bigFont =
            CreateFontA(
                40,
                0,
                0,
                0,
                FW_BOLD,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                "Segoe UI"
            );

        smallFont =
            CreateFontA(
                15,
                0,
                0,
                0,
                FW_NORMAL,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH,
                "Segoe UI"
            );

widgetLabelFont =
    CreateFontA(
        17,
        0,
        0,
        0,
        FW_SEMIBOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH,
        "Segoe UI"
    );

widgetValueFont =
    CreateFontA(
        22,
        0,
        0,
        0,
        FW_BOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH,
        "Segoe UI"
    );
        // Show/paint the window before doing hardware discovery.  The old
        // startup path decoded assets, launched the sensor helper and queried
        // every hardware class inside WM_CREATE, which left a white/unpainted
        // window visible while Windows waited for creation to finish.
        // Timer 3 performs a few short warm-up stages after the first frame is
        // already on screen, then starts the normal 500 ms monitoring timer.
        SetTimer(
            hwnd,
            3,
            40,
            nullptr
        );

        return 0;
    }



case WM_LBUTTONUP:
{
    if (settingsTransparencyDragging) { settingsTransparencyDragging=false; ReleaseCapture(); flushPendingSettingsSave(hwnd); return 0; }
    if (processScrollbarDragging)
    {
        processScrollbarDragging = false;
        ReleaseCapture();

        POINT cursor = {};
        GetCursorPos(&cursor);
        ScreenToClient(hwnd, &cursor);

        const LPARAM cursorParam =
            MAKELPARAM(
                static_cast<short>(cursor.x),
                static_cast<short>(cursor.y)
            );

        POINT logicalCursor =
            sysMonClientToLogicalPoint(
                hwnd,
                cursorParam
            );

        processScrollbarVisible =
            currentPage == AppPage::Processes &&
            logicalCursor.x >= 185 &&
            logicalCursor.x <= 865 &&
            logicalCursor.y >= 275 &&
            logicalCursor.y <= 690;

        invalidateProcessTableFast(hwnd);
    }
    break;
}
case WM_MOUSEMOVE:
{
    static HWND tips=nullptr;static std::string tipText;
    if(!tips) {
        tips=CreateWindowExA(WS_EX_TOPMOST,TOOLTIPS_CLASSA,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,hwnd,nullptr,GetModuleHandle(nullptr),nullptr);
        TOOLINFOA info{};info.cbSize=sizeof(info);info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=hwnd;info.uId=reinterpret_cast<UINT_PTR>(hwnd);info.lpszText=const_cast<char*>("");SendMessageA(tips,TTM_ADDTOOLA,0,reinterpret_cast<LPARAM>(&info));
        SendMessage(tips,TTM_SETMAXTIPWIDTH,0,360);SendMessage(tips,TTM_SETDELAYTIME,TTDT_INITIAL,650);
    }
    std::string next;
    if(currentPage==AppPage::Settings) {
        POINT logical=sysMonClientToLogicalPoint(hwnd,lParam);int x=logical.x-165,y=logical.y+settingsScrollOffset;
        if(logical.y>=SettingsLayout::clipTop && logical.y<=SettingsLayout::clipBottom) {
            auto over=[&](int column,int top,int row){auto r=SettingsLayout::rowBand(column,top,row);return x>=r.left&&x<=r.right&&y>=r.top&&y<=r.bottom;};
            if(over(0,SettingsLayout::topRowTop,1))next="X hides SysMon in the tray when enabled. Tray Exit and Ctrl+Q always close the application.";
            if(over(0,SettingsLayout::topRowTop,2))next="Checks GitHub without installing anything. Right-click Settings for version details, release notes, and manual checks.";
            if(over(1,SettingsLayout::topRowTop,0))next="One refresh interval controls monitoring. Slower intervals reduce collection work.";
            if(over(1,SettingsLayout::middleRowTop,1))next="Old SysMon monitoring and alert logs are removed after this many days. Other files are left alone.";
            if(over(2,SettingsLayout::middleRowTop,1))next="Choose appearance and metrics independently. Always on top keeps the overlay above other windows. Position memory restores its saved monitor.";
            if(over(2,SettingsLayout::middleRowTop,2))next="Unavailable can mean unsupported hardware or a missing sensor helper. Open Details or export a Support Report.";
        }
    }
    if(next!=tipText) {tipText=next;TOOLINFOA info{};info.cbSize=sizeof(info);info.hwnd=hwnd;info.uId=reinterpret_cast<UINT_PTR>(hwnd);info.lpszText=tipText.data();SendMessageA(tips,TTM_UPDATETIPTEXTA,0,reinterpret_cast<LPARAM>(&info));if(tipText.empty())SendMessage(tips,TTM_POP,0,0);}

    if (settingsTransparencyDragging) {
        if (!(wParam & MK_LBUTTON)) { settingsTransparencyDragging=false; ReleaseCapture(); return 0; }
        const POINT point=sysMonClientToLogicalPoint(hwnd,lParam);
        const int localX=point.x-165;
        appSettings.transparencyPercent=std::clamp(55+((localX-SettingsLayout::sliderLeft(2))*45)/SettingsLayout::sliderWidth,55,100);
        applyMainWindowTransparency(hwnd); requestSettingsSave(hwnd); InvalidateRect(hwnd,nullptr,FALSE); return 0;
    }
    // Stop dragging when the real mouse button is released
    if (
        processScrollbarDragging &&
        (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0
    )
    {
        processScrollbarDragging = false;
    }


    // --------------------------------------------------------
    // PROCESS SCROLLBAR DRAGGING
    // --------------------------------------------------------

    if (
        currentPage == AppPage::Processes &&
        processScrollbarDragging
    )
    {
        POINT logicalMouse =
            sysMonClientToLogicalPoint(
                hwnd,
                lParam
            );

        int mouseY =
            logicalMouse.y;

        const int trackTop = 363;
        const int trackBottom = 661;

        const int thumbHeight =
            processScrollbarThumbBottom -
            processScrollbarThumbTop;

        const int thumbTravel =
            (trackBottom - trackTop) -
            thumbHeight;

        int newThumbTop =
            mouseY -
            processScrollbarDragOffsetY;

        newThumbTop = std::clamp(
            newThumbTop,
            trackTop,
            trackTop + (std::max)(0, thumbTravel)
        );

        int newScrollOffset = processScrollOffset;

        if (thumbTravel > 0 && processMaxScrollOffset > 0)
        {
            const double ratio =
                static_cast<double>(newThumbTop - trackTop) /
                static_cast<double>(thumbTravel);

            newScrollOffset = static_cast<int>(
                ratio * processMaxScrollOffset + 0.5
            );
        }

        if (newScrollOffset != processScrollOffset)
        {
            processScrollOffset = newScrollOffset;
            hoveredProcessPid = MAXDWORD;
            sysMonLastUserInteractionTick = GetTickCount64();
            invalidateProcessTableFast(hwnd);
        }

        return 0;
    }


    // --------------------------------------------------------
    // PROCESS ROW HOVER
    // --------------------------------------------------------

    if (currentPage == AppPage::Processes)
    {
        POINT logicalMouse =
            sysMonClientToLogicalPoint(
                hwnd,
                lParam
            );

        int mouseX =
            logicalMouse.x;

        int mouseY =
            logicalMouse.y;

        const bool mouseInsideProcessPanel =
            mouseX >= 185 &&
            mouseX <= 865 &&
            mouseY >= 275 &&
            mouseY <= 690;

        if (
            mouseInsideProcessPanel !=
            processScrollbarVisible
        )
        {
            processScrollbarVisible =
                mouseInsideProcessPanel;

            invalidateProcessTableFast(hwnd);
        }

        // Ask Windows for WM_MOUSELEAVE so the panel hover state resets
        // when the pointer leaves the SysMon window.
        TRACKMOUSEEVENT trackMouse = {};
        trackMouse.cbSize = sizeof(trackMouse);
        trackMouse.dwFlags = TME_LEAVE;
        trackMouse.hwndTrack = hwnd;
        TrackMouseEvent(&trackMouse);

        DWORD newHoveredPid =
            MAXDWORD;

        if (
            mouseX >= 185 &&
            mouseX <= 865 &&
            mouseY >= 359 &&
            mouseY < 659
        )
        {
            int rowIndex =
                (mouseY - 359) / 25;

            if (
                rowIndex >= 0 &&
                rowIndex <
                    static_cast<int>(
                        visibleProcessPids.size()
                    )
            )
            {
                newHoveredPid =
                    visibleProcessPids[
                        rowIndex
                    ];
            }
        }

        if (
            newHoveredPid !=
            hoveredProcessPid
        )
        {
            hoveredProcessPid =
                newHoveredPid;

            invalidateProcessTableFast(hwnd);
        }
    }

    break;
}

case WM_MOUSELEAVE:
{
    if (
        currentPage == AppPage::Processes &&
        processScrollbarVisible &&
        !processScrollbarDragging
    )
    {
        processScrollbarVisible = false;
        hoveredProcessPid = MAXDWORD;
        invalidateProcessTableFast(hwnd);
    }

    return 0;
}

   case WM_MOUSEWHEEL:
{
    sysMonLastUserInteractionTick = GetTickCount64();
    if (currentPage == AppPage::SystemInfo)
    {
        short wheelDelta =
            GET_WHEEL_DELTA_WPARAM(
                wParam
            );

        const int previousInfoOffset = systemInfoScrollOffset;

        systemInfoScrollOffset -=
            (wheelDelta / WHEEL_DELTA) * 70;

        systemInfoScrollOffset =
            std::clamp(
                systemInfoScrollOffset,
                0,
                systemInfoMaxScrollOffset
            );

        if (systemInfoScrollOffset == previousInfoOffset)
        {
            return 0;
        }

        requestImmediateSysMonPaint(hwnd);

        return 0;
    }

    if (currentPage == AppPage::Settings)
    {
        const short wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        const int previousOffset = settingsScrollOffset;

        settingsScrollOffset -= (wheelDelta / WHEEL_DELTA) * 72;
        settingsScrollOffset =
            std::clamp(settingsScrollOffset, 0, settingsMaxScrollOffset);

        // Already at an end of the page: nothing moved, so do not repaint.
        if (settingsScrollOffset == previousOffset)
        {
            return 0;
        }

        // Scroll directly into the full-quality back buffer. The navigation
        // preview clears the visible window and changes resolution twice per
        // wheel event, producing flashes and text shimmer on scaled windows.
        KillTimer(hwnd, 4);
        sysMonNavigationPreviewPending = false;
        sysMonProcessScrollPaintPending = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    if (currentPage == AppPage::Processes)
    {
        short wheelDelta =
            GET_WHEEL_DELTA_WPARAM(
                wParam
            );

        const int oldOffset = processScrollOffset;
        const int wheelSteps =
            (std::max)(1, std::abs(wheelDelta) / WHEEL_DELTA);
        const int rowStep = 2 * wheelSteps;

        if (wheelDelta < 0)
            processScrollOffset += rowStep;
        else if (wheelDelta > 0)
            processScrollOffset -= rowStep;

        processScrollOffset = std::clamp(
            processScrollOffset,
            0,
            processMaxScrollOffset
        );

        if (processScrollOffset != oldOffset)
        {
            hoveredProcessPid = MAXDWORD;
            invalidateProcessTableFast(hwnd);
        }

        return 0;
    }

    break;
}
case WM_CHAR:
{
    sysMonLastUserInteractionTick = GetTickCount64();
    if (
        currentPage == AppPage::Processes &&
        processSearchFocused
    )
    {
        // Backspace
        if (wParam == 8)
        {
            if (!processSearch.empty())
            {
                processSearch.pop_back();
            }
        }

        // Printable characters
        else if (
            wParam >= 32 &&
            wParam <= 126
        )
        {
            processSearch +=
                static_cast<char>(
                    wParam
                );
        }

        processScrollOffset = 0;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }

    break;
}


case WM_LBUTTONDOWN:
{
    sysMonLastUserInteractionTick = GetTickCount64();
    POINT logicalMouse =
        sysMonClientToLogicalPoint(
            hwnd,
            lParam
        );

    int mouseX =
        logicalMouse.x;

    int mouseY =
        logicalMouse.y;

    // --------------------------------------------------------
    // CPU / MEMORY / DISK / GPU / NETWORK - HISTORY RANGE DROPDOWN
    // --------------------------------------------------------

    if (
        currentPage == AppPage::Performance &&
        (
            performanceView == PerformanceView::Overview ||
            performanceView == PerformanceView::CPU ||
            performanceView == PerformanceView::Memory ||
            performanceView == PerformanceView::Disk ||
            performanceView == PerformanceView::GPU ||
            performanceView == PerformanceView::Network
        )
    )
    {
        // The dedicated Performance pages use the same +165 X
        // origin and the same history-range control coordinates.
        // additionally shifted +88 Y while the time-range control
        // is drawn, so these are the matching global hitboxes.
        const int rangeLeft = 985;
        const int rangeRight = 1160;
        const int rangeTop = 100;
        const int rangeBottom = 142;

        if (
            mouseX >= rangeLeft &&
            mouseX <= rangeRight &&
            mouseY >= rangeTop &&
            mouseY <= rangeBottom
        )
        {
            cpuHistoryRangeDropdownOpen =
                !cpuHistoryRangeDropdownOpen;

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );

            return 0;
        }

        if (cpuHistoryRangeDropdownOpen)
        {
            // Menu is drawn directly below the pill.
            const int menuTop = 148;
            const int rowHeight = 34;

            if (
                mouseX >= rangeLeft &&
                mouseX <= rangeRight &&
                mouseY >= menuTop &&
                mouseY < menuTop + rowHeight * 3
            )
            {
                int selectedRow =
                    (mouseY - menuTop) / rowHeight;

                if (selectedRow == 0)
                {
                    cpuHistoryRangeSeconds = 15;
                }
                else if (selectedRow == 1)
                {
                    cpuHistoryRangeSeconds = 30;
                }
                else
                {
                    cpuHistoryRangeSeconds = 60;
                }

                cpuHistoryRangeDropdownOpen = false;

                InvalidateRect(
                    hwnd,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            // Clicking anywhere else closes the dropdown while still
            // allowing the underlying navigation/click handling to run.
            cpuHistoryRangeDropdownOpen = false;

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );
        }
    }

    // --------------------------------------------------------
    // DISK PAGE - PHYSICAL DISK SELECTOR
    // --------------------------------------------------------

    if (
        currentPage == AppPage::Performance &&
        performanceView == PerformanceView::Disk &&
        diskStats.size() > 1 &&
        mouseY >= 138 &&
        mouseY <= 174
    )
    {
        if (
            selectedDiskIndex < 0 ||
            selectedDiskIndex >=
                static_cast<int>(diskStats.size())
        )
        {
            selectedDiskIndex = 0;
        }

        if (mouseX >= 685 && mouseX <= 730)
        {
            selectedDiskIndex--;

            if (selectedDiskIndex < 0)
            {
                selectedDiskIndex =
                    static_cast<int>(diskStats.size()) - 1;
            }

            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (mouseX >= 925 && mouseX <= 970)
        {
            selectedDiskIndex++;

            if (
                selectedDiskIndex >=
                static_cast<int>(diskStats.size())
            )
            {
                selectedDiskIndex = 0;
            }

            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
    }

    // --------------------------------------------------------
    // GPU PAGE - ADAPTER SELECTOR
    // --------------------------------------------------------

    if (
        currentPage == AppPage::Performance &&
        performanceView == PerformanceView::GPU &&
        gpuStats.size() > 1 &&
        mouseY >= 138 &&
        mouseY <= 174
    )
    {
        if (
            selectedGpuIndex < 0 ||
            selectedGpuIndex >=
                static_cast<int>(gpuStats.size())
        )
        {
            selectedGpuIndex = 0;
        }

        if (mouseX >= 685 && mouseX <= 730)
        {
            selectedGpuIndex--;

            if (selectedGpuIndex < 0)
            {
                selectedGpuIndex =
                    static_cast<int>(gpuStats.size()) - 1;
            }

            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (mouseX >= 925 && mouseX <= 970)
        {
            selectedGpuIndex++;

            if (
                selectedGpuIndex >=
                static_cast<int>(gpuStats.size())
            )
            {
                selectedGpuIndex = 0;
            }

            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
    }

    // --------------------------------------------------------
    // DASHBOARD - VIEW ALL PROCESSES
    // --------------------------------------------------------

    // Dashboard content is drawn with a +165 X viewport origin.
    // The visible View All text is therefore at approximately
    // global X 552..613 and Y 480..508.
    if (
        currentPage == AppPage::Dashboard &&
        mouseX >= 545 &&
        mouseX <= 625 &&
        mouseY >= 472 &&
        mouseY <= 512
    )
    {
        currentPage =
            AppPage::Processes;

        processScrollOffset = 0;
        processSearchFocused = false;
        hoveredProcessPid = MAXDWORD;

        requestImmediateSysMonPaint(hwnd);

        return 0;
    }


    // --------------------------------------------------------
    // TOP NAVIGATION
    // --------------------------------------------------------

    if (
        mouseY >= 10 &&
        mouseY <= 48
    )
    {
        AppPage targetPage =
            currentPage;

        bool topNavigationClicked =
            true;

        if (
            mouseX >= 325 &&
            mouseX <= 435
        )
        {
            targetPage =
                AppPage::Dashboard;
        }
        else if (
            mouseX >= 443 &&
            mouseX <= 553
        )
        {
            targetPage =
                AppPage::Processes;
        }
        else if (
            mouseX >= 561 &&
            mouseX <= 691
        )
        {
            targetPage =
                AppPage::Performance;
        }
        else if (
            mouseX >= 699 &&
            mouseX <= 785
        )
        {
            targetPage =
                AppPage::Tools;
        }
        else if (
            mouseX >= 793 &&
            mouseX <= 895
        )
        {
            targetPage =
                AppPage::Settings;
        }
        else
        {
            topNavigationClicked =
                false;
        }

        if (topNavigationClicked)
        {
            currentPage =
                targetPage;

            // The top Performance tab is a dedicated overall dashboard.
            // Detailed CPU/Memory/Disk/GPU/Network pages are reached only
            // from the left sidebar.
            if (targetPage == AppPage::Performance)
            {
                performanceView = PerformanceView::Overview;
            }

            if (targetPage == AppPage::Settings)
            {
                settingsScrollOffset = 0;
            }

            processSearchFocused =
                false;

            hoveredProcessPid =
                MAXDWORD;

            requestImmediateSysMonPaint(hwnd);

            return 0;
        }
    }
    // --------------------------------------------------------
    // GPU / NETWORK - complete metric-specific process lists.
    if(currentPage==AppPage::Performance) {
        // Detail content is drawn below a 103-unit vertical viewport offset.
        if(performanceView==PerformanceView::GPU && mouseX>=735 && mouseX<=823 && mouseY>=527 && mouseY<=561) {
            ProcessDetails::show(hwnd,ProcessDetails::Kind::Gpu,selectedGpuIndex);return 0;
        }
        if(performanceView==PerformanceView::Network && mouseX>=765 && mouseX<=850 && mouseY>=554 && mouseY<=583) {
            ProcessDetails::show(hwnd,ProcessDetails::Kind::Network,selectedGpuIndex);return 0;
        }
    }
    // CPU / MEMORY - VIEW ALL PROCESSES
    // --------------------------------------------------------

    // The dedicated CPU and Memory pages use a +165 X viewport origin.
    // Their View All text is drawn at local X 580..650.
    if (
        currentPage == AppPage::Performance &&
        (
            performanceView == PerformanceView::CPU ||
            performanceView == PerformanceView::Memory
        ) &&
        mouseX >= 735 &&
        mouseX <= 820 &&
        mouseY >= 523 &&
        mouseY <= 565
    )
    {
        currentPage =
            AppPage::Processes;

        processScrollOffset = 0;
        processSearchFocused = false;
        hoveredProcessPid = MAXDWORD;

        requestImmediateSysMonPaint(hwnd);

        return 0;
    }


    // --------------------------------------------------------
    // PERFORMANCE RESOURCE CARD CLICKS
    // --------------------------------------------------------

if (
    false && // legacy resource rail is no longer drawn
    currentPage == AppPage::Performance &&
    mouseX >= 280 &&
    mouseX <= 460
)
{
    // CPU
    if (
        mouseY >= 135 &&
        mouseY <= 205
    )
    {
        performanceView =
            PerformanceView::CPU;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }

    // Memory
    if (
        mouseY >= 225 &&
        mouseY <= 295
    )
    {
        performanceView =
            PerformanceView::Memory;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }

    // Physical disk cards
    int visibleDiskCards =
        performanceVisibleDiskCardCount(
            diskStats.size()
        );

    for (
        int index = 0;
        index < visibleDiskCards;
        index++
    )
    {
        int top =
            performanceDiskCardTop(
                index
            );

        int bottom =
            top +
            performanceDiskCardHeight;

        if (
            mouseY >= top &&
            mouseY <= bottom
        )
        {
            performanceView =
                PerformanceView::Disk;

            if (
                index <
                static_cast<int>(
                    diskStats.size()
                )
            )
            {
                selectedDiskIndex =
                    index;
            }

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );

            return 0;
        }
    }

    int gpuCardCount =
        performanceVisibleGpuCardCount(
            gpuStats.size()
        );

    for (int index = 0;
         index < gpuCardCount;
         index++)
    {
        int gpuTop =
            performanceGpuCardTop(
                diskStats.size(),
                index
            );

        if (
            mouseY >= gpuTop &&
            mouseY <=
                gpuTop +
                performanceDiskCardHeight
        )
        {
            performanceView =
                PerformanceView::GPU;

            if (
                index <
                static_cast<int>(
                    gpuStats.size()
                )
            )
            {
                selectedGpuIndex = index;
            }
            else
            {
                selectedGpuIndex = 0;
            }

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );

            return 0;
        }
    }

    int networkTop =
        performanceNetworkCardTop(
            diskStats.size(),
            gpuStats.size()
        );

    if (
        mouseY >= networkTop &&
        mouseY <=
            networkTop +
            performanceDiskCardHeight
    )
    {
        performanceView =
            PerformanceView::Network;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }
}

// --------------------------------------------------------
// TEMPERATURE PAGE CONTROLS
// --------------------------------------------------------
if (currentPage == AppPage::Temperatures)
{
    // The redesigned temperature page uses the +165 X viewport.
    // History range pill: local X 855..1015 => window X 1020..1180.
    const int rangeLeft = 1030;
    const int rangeRight = 1180;
    const int rangeTop = 64;
    const int rangeBottom = 104;

    if (
        mouseX >= rangeLeft &&
        mouseX <= rangeRight &&
        mouseY >= rangeTop &&
        mouseY <= rangeBottom
    )
    {
        cpuHistoryRangeDropdownOpen =
            !cpuHistoryRangeDropdownOpen;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }

    if (cpuHistoryRangeDropdownOpen)
    {
        const int menuTop = 108;
        const int rowHeight = 34;

        if (
            mouseX >= rangeLeft &&
            mouseX <= rangeRight &&
            mouseY >= menuTop &&
            mouseY < menuTop + rowHeight * 3
        )
        {
            int selectedRow =
                (mouseY - menuTop) / rowHeight;

            if (selectedRow == 0)
            {
                cpuHistoryRangeSeconds = 15;
            }
            else if (selectedRow == 1)
            {
                cpuHistoryRangeSeconds = 30;
            }
            else
            {
                cpuHistoryRangeSeconds = 60;
            }

            cpuHistoryRangeDropdownOpen = false;

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );

            return 0;
        }

        cpuHistoryRangeDropdownOpen = false;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );
    }

    // GPU summary card cycles through all detected GPUs and loops.
    // Local X 282..517 => window X 447..682.
    if (
        gpuStats.size() > 1 &&
        mouseX >= 447 &&
        mouseX <= 682 &&
        mouseY >= 110 &&
        mouseY <= 224
    )
    {
        selectedTemperatureGpuIndex++;

        if (
            selectedTemperatureGpuIndex >=
            static_cast<int>(gpuStats.size())
        )
        {
            selectedTemperatureGpuIndex = 0;
        }

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }
}


// --------------------------------------------------------
// NETWORK PAGE - ADAPTER SELECTOR / ADAPTER LIST
// --------------------------------------------------------
if (
    currentPage == AppPage::Performance &&
    performanceView == PerformanceView::Network &&
    !networkStats.empty()
)
{
    if (
        selectedNetworkIndex < 0 ||
        selectedNetworkIndex >=
            static_cast<int>(networkStats.size())
    )
    {
        selectedNetworkIndex = 0;
    }

    if (
        networkStats.size() > 1 &&
        mouseY >= 100 &&
        mouseY <= 142
    )
    {
        if (mouseX >= 815 && mouseX <= 855)
        {
            selectedNetworkIndex--;

            if (selectedNetworkIndex < 0)
            {
                selectedNetworkIndex =
                    static_cast<int>(networkStats.size()) - 1;
            }

            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (mouseX >= 935 && mouseX <= 970)
        {
            selectedNetworkIndex++;

            if (
                selectedNetworkIndex >=
                static_cast<int>(networkStats.size())
            )
            {
                selectedNetworkIndex = 0;
            }

            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
    }

    const int maxAdapterRows = 3;
    int adapterStartIndex = 0;

    if (
        static_cast<int>(networkStats.size()) >
        maxAdapterRows
    )
    {
        adapterStartIndex =
            std::clamp(
                selectedNetworkIndex - 1,
                0,
                static_cast<int>(networkStats.size()) -
                    maxAdapterRows
            );
    }

    int adapterRows =
        (std::min)(
            maxAdapterRows,
            static_cast<int>(networkStats.size()) -
                adapterStartIndex
        );

    for (
        int visibleAdapterIndex = 0;
        visibleAdapterIndex < adapterRows;
        visibleAdapterIndex++
    )
    {
        int rowTop =
            580 +
            visibleAdapterIndex * 38;

        if (
            mouseX >= 871 &&
            mouseX <= 1149 &&
            mouseY >= rowTop &&
            mouseY <= rowTop + 35
        )
        {
            selectedNetworkIndex =
                adapterStartIndex +
                visibleAdapterIndex;

            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
    }
}

// --------------------------------------------------------
// SYSTEM INFO GPU / STORAGE CARD CYCLING
// --------------------------------------------------------

if (
    currentPage == AppPage::SystemInfo &&
    mouseY >= 116 &&
    mouseY <= 206
)
{
    // System Info uses a 165 px content origin.
    // GPU card: local x 351..506 -> window x 516..671.
    if (
        mouseX >= 516 &&
        mouseX <= 671 &&
        gpuStats.size() > 1
    )
    {
        systemInfoSelectedGpuIndex++;

        if (
            systemInfoSelectedGpuIndex >=
            static_cast<int>(gpuStats.size())
        )
        {
            systemInfoSelectedGpuIndex = 0;
        }

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }

    // Storage card: local x 677..832 -> window x 842..997.
    if (
        mouseX >= 842 &&
        mouseX <= 997 &&
        diskStats.size() > 1
    )
    {
        systemInfoSelectedDiskIndex++;

        if (
            systemInfoSelectedDiskIndex >=
            static_cast<int>(diskStats.size())
        )
        {
            systemInfoSelectedDiskIndex = 0;
        }

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }
}

// --------------------------------------------------------
// PROCESS SCROLLBAR
// --------------------------------------------------------

if (
    currentPage == AppPage::Processes &&
    processMaxScrollOffset > 0 &&
    mouseX >= 853 &&
    mouseX <= 865
)
{
    const int trackTop = 363;
    const int trackBottom = 661;

    if (
        mouseY >= processScrollbarThumbTop &&
        mouseY <= processScrollbarThumbBottom
    )
    {
        processScrollbarDragging = true;
        processScrollbarVisible = true;
        processScrollbarDragOffsetY =
            mouseY - processScrollbarThumbTop;
        SetCapture(hwnd);
        invalidateProcessTableFast(hwnd);
        return 0;
    }

    // Clicking the rail above/below the thumb pages through the list.
    if (
        mouseY >= trackTop &&
        mouseY < processScrollbarThumbTop
    )
    {
        const int oldOffset = processScrollOffset;
        processScrollOffset =
            (std::max)(0, processScrollOffset - 10);

        if (processScrollOffset != oldOffset)
        {
            hoveredProcessPid = MAXDWORD;
            invalidateProcessTableFast(hwnd);
        }
        return 0;
    }

    if (
        mouseY > processScrollbarThumbBottom &&
        mouseY <= trackBottom
    )
    {
        const int oldOffset = processScrollOffset;
        processScrollOffset =
            (std::min)(
                processMaxScrollOffset,
                processScrollOffset + 10
            );

        if (processScrollOffset != oldOffset)
        {
            hoveredProcessPid = MAXDWORD;
            invalidateProcessTableFast(hwnd);
        }
        return 0;
    }
}

    // --------------------------------------------------------
    // CLEAR PROCESS SEARCH FOCUS ON ANY CLICK
    // --------------------------------------------------------

    if (currentPage == AppPage::Processes)
    {
        processSearchFocused = false;
    }



    // --------------------------------------------------------
    // SETTINGS PAGE ACTIONS
    // --------------------------------------------------------
    if (currentPage == AppPage::Settings)
    {
        namespace SL = SettingsLayout;
        SL::refreshDensity();

        const int localX = mouseX - 165;
        const int localY = mouseY;
        const int contentY = localY + settingsScrollOffset;

        // Fixed chrome (page header) uses screen-space Y.
        auto inFixedRect =
            [&](const SettingsLayout::Rect& rect)
        {
            return localX >= rect.left &&
                   localX <= rect.right &&
                   localY >= rect.top &&
                   localY <= rect.bottom;
        };

        // Card contents scroll, so they are tested in page space and only
        // inside the visible scrolling viewport.
        auto inCardRect =
            [&](const SettingsLayout::Rect& rect)
        {
            if (localY < SL::clipTop || localY > SL::clipBottom)
            {
                return false;
            }

            return localX >= rect.left &&
                   localX <= rect.right &&
                   contentY >= rect.top &&
                   contentY <= rect.bottom;
        };

        auto saveAndPaint = [&]()
        {
            requestSettingsSave(hwnd);
            requestImmediateSysMonPaint(hwnd);
        };

        // ---- Reset to Default ----------------------------
        if (inFixedRect(SL::resetButton()))
        {
            // resetAppSettings() writes the defaults itself.
            sysMonSettingsSaveDue = false;
            KillTimer(hwnd, 5);

            HMENU resetMenu=CreatePopupMenu();
            const char* sections[]={"All settings","General","Monitoring","Appearance","Alerts","Data","Overlay"};
            for(int section=0;section<7;++section)AppendMenuA(resetMenu,MF_STRING,section+1,sections[section]);
            POINT cursor;GetCursorPos(&cursor);
            int selection=TrackPopupMenu(resetMenu,TPM_RETURNCMD,cursor.x,cursor.y,0,hwnd,nullptr);DestroyMenu(resetMenu);
            if(!selection)return 0;
            resetSettingsSection(selection-1);
            if(selection<=2)applySavedStartupPreference(hwnd);
            if(!saveAppSettings())MessageBoxA(hwnd,"Could not save reset preferences.","SysMon",MB_OK|MB_ICONWARNING);
            applyThemeToWindow(hwnd);
            Updates::poll(appSettings.checkForUpdates);
            applyMainWindowTransparency(hwnd);
            restartMonitoringTimer(hwnd);
            applyOverlayWidgetSetting();
            if(appSettings.showInTray) addTrayIcon(hwnd); else removeTrayIcon();
            requestImmediateSysMonPaint(hwnd);
            return 0;
        }

        // ---- General Settings ----------------------------
        if (inCardRect(SL::rowBand(0, SL::topRowTop, 0)))
        {
            const bool newValue =
                !appSettings.startWithWindows;

            if (setStartWithWindowsEnabled(newValue))
            {
                appSettings.startWithWindows = newValue;
                saveAndPaint();
            }
            else
            {
                appSettings.startWithWindows=isStartWithWindowsEnabled();
                requestSettingsSave(hwnd);
                InvalidateRect(hwnd,nullptr,FALSE);
                MessageBoxA(
                    hwnd,
                    "Windows could not update the startup task.",
                    "SysMon",
                    MB_OK | MB_ICONWARNING
                );
            }
            return 0;
        }

        if (inCardRect(SL::rowBand(0, SL::topRowTop, 1)))
        {
            appSettings.minimizeToTray =
                !appSettings.minimizeToTray;
            if (appSettings.minimizeToTray) appSettings.showInTray=true;
            saveAndPaint();
            return 0;
        }

        if (inCardRect(SL::rowBand(0, SL::topRowTop, 2)))
        {
            appSettings.checkForUpdates =
                !appSettings.checkForUpdates;
            Updates::poll(appSettings.checkForUpdates);
            saveAndPaint();
            return 0;
        }

        if (inCardRect(SL::comboRect(0, SL::topRowTop, 3)))
        {
            const int choice=chooseSetting(hwnd,appSettings.theme,{{0,"Dark"},{1,"Light"},{2,"Follow Windows"}});
            if (choice!=appSettings.theme) {
                appSettings.theme=choice;
                requestSettingsSave(hwnd);
                applyThemeToWindow(hwnd);
            }
            return 0;
        }

        // ---- Monitoring Settings -------------------------
        if (inCardRect(SL::comboRect(1, SL::topRowTop, 0)))
        {
            appSettings.updateIntervalMs=chooseSetting(hwnd,appSettings.updateIntervalMs,{{250,"250 ms"},{500,"500 ms"},{1000,"1 second"},{2000,"2 seconds"},{5000,"5 seconds"},{10000,"10 seconds"}});

            requestSettingsSave(hwnd);
            restartMonitoringTimer(hwnd);
            requestImmediateSysMonPaint(hwnd);
            return 0;
        }

        if (inCardRect(SL::comboRect(1, SL::topRowTop, 1)))
        {
            appSettings.temperatureUnit=chooseSetting(hwnd,appSettings.temperatureUnit,{{0,"Celsius"},{1,"Fahrenheit"}});
            saveAndPaint();
            return 0;
        }

        if (inCardRect(SL::comboRect(1, SL::topRowTop, 2)))
        {
            appSettings.networkUnit=chooseSetting(hwnd,appSettings.networkUnit,{{0,"Mbps"},{1,"MB/s"},{2,"Kbps"}});
            saveAndPaint();
            return 0;
        }

        if (inCardRect(SL::rowBand(1, SL::topRowTop, 3)))
        {
            appSettings.showInTray =
                !appSettings.showInTray;
            if (!appSettings.showInTray) { appSettings.minimizeToTray=false; removeTrayIcon(); }
            else addTrayIcon(hwnd);
            saveAndPaint();
            return 0;
        }

        if (inCardRect(SL::rowBand(1, SL::topRowTop, 4)))
        {
            appSettings.playAlerts =
                !appSettings.playAlerts;
            saveAndPaint();
            return 0;
        }

        // ---- Appearance: accent swatches -----------------
        if (localY >= SL::clipTop && localY <= SL::clipBottom)
        {
            bool swatchClicked = false;

            for (int index = 0; index < SL::swatchCount; index++)
            {
                const int dx =
                    localX - SL::swatchCenterX(2, index);

                const int dy =
                    contentY - SL::swatchCenterY(SL::topRowTop);

                if (std::abs(dx) <= SL::swatchHitRadius &&
                    std::abs(dy) <= SL::swatchHitRadius)
                {
                    appSettings.accentColorIndex = index;
                    swatchClicked = true;
                    break;
                }
            }

            if (swatchClicked)
            {
                saveAndPaint();
                return 0;
            }
        }

        // ---- Appearance: transparency slider -------------
        if (inCardRect(SL::sliderBand(2, SL::topRowTop)))
        {
            int percentage =
                55 + ((localX - SL::sliderLeft(2)) * 45) / SL::sliderWidth;

            percentage =
                (std::max)(
                    55,
                    (std::min)(100, percentage)
                );

            appSettings.transparencyPercent =
                percentage;
            settingsTransparencyDragging=true; SetCapture(hwnd);

            requestSettingsSave(hwnd);
            applyMainWindowTransparency(hwnd);
            requestImmediateSysMonPaint(hwnd);
            return 0;
        }

        if (inCardRect(SL::rowBand(2, SL::topRowTop, 2)))
        {
            appSettings.compactMode =
                !appSettings.compactMode;
            saveAndPaint();
            return 0;
        }

        if (inCardRect(SL::rowBand(2, SL::topRowTop, 3)))
        {
            appSettings.showAnimations =
                !appSettings.showAnimations;
            saveAndPaint();
            return 0;
        }

        if (inCardRect(SL::comboRect(2, SL::topRowTop, 4)))
        {
            appSettings.fontSizeIndex=chooseSetting(hwnd,appSettings.fontSizeIndex,{{0,"Small"},{1,"Medium"},{2,"Large"}});
            saveAndPaint();
            return 0;
        }

        // ---- Alerts & Thresholds -------------------------
        auto advanceThreshold = [&](int& value,int minimum,int maximum) {
            std::vector<std::pair<int,std::string>> choices;
            for(int candidate=minimum;candidate<=maximum;candidate+=5)
                choices.push_back({candidate,maximum>100 ? SettingsRuntime::temperature(candidate) : std::to_string(candidate)+"%"});
            value=chooseSetting(hwnd,value,choices);
        };

        if (inCardRect(SL::valueRect(0, SL::middleRowTop, 0)))
        {
            advanceThreshold(appSettings.cpuTemperatureAlert, 50, 110);
            saveAndPaint();
            return 0;
        }

        if (inCardRect(SL::valueRect(0, SL::middleRowTop, 1)))
        {
            advanceThreshold(appSettings.gpuTemperatureAlert, 50, 110);
            saveAndPaint();
            return 0;
        }

        if (inCardRect(SL::valueRect(0, SL::middleRowTop, 2)))
        {
            advanceThreshold(appSettings.diskUsageAlert, 50, 100);
            saveAndPaint();
            return 0;
        }

        if (inCardRect(SL::valueRect(0, SL::middleRowTop, 3)))
        {
            advanceThreshold(appSettings.memoryUsageAlert, 50, 100);
            saveAndPaint();
            return 0;
        }

        if (inCardRect(SL::rowBand(0, SL::middleRowTop, 4)))
        {
            appSettings.showDesktopNotifications =
                !appSettings.showDesktopNotifications;
            saveAndPaint();
            return 0;
        }

        if (inCardRect(SL::rowBand(0, SL::middleRowTop, 5)))
        {
            appSettings.logAlertsToFile =
                !appSettings.logAlertsToFile;
            saveAndPaint();
            return 0;
        }

        // ---- Data & Privacy ------------------------------
        if (inCardRect(SL::rowBand(1, SL::middleRowTop, 0)))
        {
            appSettings.enableDataLogging =
                !appSettings.enableDataLogging;
            saveAndPaint();
            return 0;
        }

        if (inCardRect(SL::comboRect(1, SL::middleRowTop, 1)))
        {
            appSettings.logRetentionDays=chooseSetting(hwnd,appSettings.logRetentionDays,{{7,"7 days"},{14,"14 days"},{30,"30 days"},{90,"90 days"}});

            saveAndPaint();
            return 0;
        }

        if (inCardRect(SL::buttonRect(1, SL::middleRowTop, 2, 112)))
        {
            const std::string folder =
                getSysMonDataFolderPath();

            openSettingsTarget(hwnd, folder.c_str());
            return 0;
        }

        if (inCardRect(SL::rowBand(1, SL::middleRowTop, 3)))
        {
            appSettings.allowAnonymousUsageData = false;
            saveAndPaint();
            return 0;
        }

        if (inCardRect(SL::buttonRect(2, SL::middleRowTop, 1, 124))) {
            configureOverlay(hwnd); saveAndPaint(); return 0;
        }

        // ---- System Integration --------------------------
        if (inCardRect(SL::rowBand(2, SL::middleRowTop, 0)))
        {
            appSettings.showOverlayWidget =
                !appSettings.showOverlayWidget;
            requestSettingsSave(hwnd);
            applyOverlayWidgetSetting();
            requestImmediateSysMonPaint(hwnd);
            return 0;
        }

        if (inCardRect(SL::buttonRect(2, SL::middleRowTop, 2, 112)))
        {
            MessageBoxA(hwnd,temperatureStats.hardwareSensorAvailable?"Sensors: Active\nHardware sensors are responding.":"Sensors: Unavailable\nSome hardware may not expose sensors. Check that the SysMonSensors folder is beside SysMon.exe. Diagnostic events are in the data folder.","Sensor status",MB_OK|MB_ICONINFORMATION);
            return 0;


        }

        if (inCardRect(SL::buttonRect(2, SL::middleRowTop, 3, 96)))
        {
            const bool ok =
                registerSysMonLogFileAssociation();

            MessageBoxA(
                hwnd,
                ok
                    ? ".sysmonlog files will now open in Notepad."
                    : "Windows could not create the file association.",
                "SysMon",
                MB_OK |
                    (ok ? MB_ICONINFORMATION : MB_ICONWARNING)
            );
            return 0;
        }

        // ---- Backup & Restore ----------------------------
        if (inCardRect(SL::buttonRect(1, SL::backupPanelTop, 0, 106)))
        {
            flushPendingSettingsSave(hwnd);

            const bool ok =
                exportAppSettingsBackup();

            MessageBoxA(
                hwnd,
                ok
                    ? "Settings exported to the SysMon data folder."
                    : "Settings could not be exported.",
                "SysMon",
                MB_OK |
                    (ok ? MB_ICONINFORMATION : MB_ICONWARNING)
            );
            return 0;
        }

        if (inCardRect(SL::buttonRect(1, SL::backupPanelTop, 1, 106)))
        {
            // Drop any coalesced write so it cannot overwrite the import.
            sysMonSettingsSaveDue = false;
            KillTimer(hwnd, 5);

            const bool ok =
                importAppSettingsBackup();

            if (!ok) requestSettingsSave(hwnd);
            if (ok)
            {
                applySavedStartupPreference(hwnd);
                applyThemeToWindow(hwnd);
                Updates::poll(appSettings.checkForUpdates);
                applyMainWindowTransparency(hwnd);
                restartMonitoringTimer(hwnd);
                applyOverlayWidgetSetting();
                if(appSettings.showInTray) addTrayIcon(hwnd); else removeTrayIcon();
                requestImmediateSysMonPaint(hwnd);
            }

            MessageBoxA(
                hwnd,
                ok
                    ? "Settings imported successfully."
                    : "The backup was missing, invalid, or could not be read.",
                "SysMon",
                MB_OK |
                    (ok ? MB_ICONINFORMATION : MB_ICONWARNING)
            );
            return 0;
        }

        if(inCardRect(SL::rowBand(0,SL::aboutPanelTop,1))) {
            openSettingsTarget(hwnd,(getSettingsPath().substr(0,getSettingsPath().find_last_of("\\/"))+"\\THIRD-PARTY-NOTICES.txt").c_str());return 0;
        }
        // ---- Support -------------------------------------
        if (inCardRect(SL::buttonRect(2, SL::supportPanelTop, 0, 112)))
        {
            openSettingsTarget(hwnd, getSysMonDataFolderPath().c_str());
            return 0;
        }

        if (inCardRect(SL::buttonRect(2, SL::supportPanelTop, 1, 112)))
        {
            std::ofstream report(getSysMonDataFolderPath()+"\\SysMon-support.txt");
            report << "SysMon support report\nVersion: " << SYSMON_VERSION_STRING << "\nDescribe the issue and steps to reproduce:\n\n"
                << "Theme: " << appSettings.theme << "\nUpdate interval: " << appSettings.updateIntervalMs
                << " ms\nLogging: " << appSettings.enableDataLogging << "\n"
                << "Windows: " << systemInfo.osName << " " << systemInfo.osVersion << " build " << systemInfo.osBuild << "\n"
                << "CPU: " << systemInfo.cpuName << "\nGPU: " << systemInfo.gpuName << "\n"
                << "Sensors: " << (temperatureStats.hardwareSensorAvailable?"Active":"Unavailable") << "\n"
                << "Monitoring: " << (monitoringPaused?"Paused":"Active") << "\n\nRecent diagnostic events (no paths, addresses, or serial numbers):\n";
            std::ifstream diagnostics(getSysMonDataFolderPath()+"\\SysMon.log");
            std::deque<std::string> recent;std::string event;
            while(std::getline(diagnostics,event)) {recent.push_back(event);if(recent.size()>40)recent.pop_front();}
            for(const auto& line:recent) report<<line<<"\n";
            report.close();
            if(!report) { MessageBoxA(hwnd,"Could not save the report.","SysMon",MB_OK|MB_ICONWARNING); return 0; }

            openSettingsTarget(hwnd, (getSysMonDataFolderPath()+"\\SysMon-support.txt").c_str());
            return 0;
        }

        if (inCardRect(SL::buttonRect(2, SL::supportPanelTop, 2, 112)))
        {
            openSettingsTarget(hwnd, (getSettingsPath().substr(0,getSettingsPath().find_last_of("\\/"))+"\\SysMon-help.txt").c_str());
            return 0;
        }
    }

// --------------------------------------------------------
// SIDEBAR NAVIGATION
// --------------------------------------------------------

// Overview
if (
    mouseX >= 8 &&
    mouseX <= 155 &&
    mouseY >= 72 &&
    mouseY <= 108
)
{
    currentPage = AppPage::Dashboard;

    requestImmediateSysMonPaint(hwnd);
    return 0;
}

// CPU
if (
    mouseX >= 8 &&
    mouseX <= 155 &&
    mouseY >= 112 &&
    mouseY <= 148
)
{
    currentPage = AppPage::Performance;
    performanceView = PerformanceView::CPU;

    requestImmediateSysMonPaint(hwnd);
    return 0;
}

// Memory
if (
    mouseX >= 8 &&
    mouseX <= 155 &&
    mouseY >= 152 &&
    mouseY <= 188
)
{
    currentPage = AppPage::Performance;
    performanceView = PerformanceView::Memory;

    requestImmediateSysMonPaint(hwnd);
    return 0;
}

// Disk
if (
    mouseX >= 8 &&
    mouseX <= 155 &&
    mouseY >= 192 &&
    mouseY <= 228
)
{
    currentPage = AppPage::Performance;
    performanceView = PerformanceView::Disk;

    requestImmediateSysMonPaint(hwnd);
    return 0;
}

// GPU
if (
    mouseX >= 8 &&
    mouseX <= 155 &&
    mouseY >= 232 &&
    mouseY <= 268
)
{
    currentPage = AppPage::Performance;
    performanceView = PerformanceView::GPU;

    requestImmediateSysMonPaint(hwnd);
    return 0;
}

// Network
if (
    mouseX >= 8 &&
    mouseX <= 155 &&
    mouseY >= 272 &&
    mouseY <= 308
)
{
    currentPage = AppPage::Performance;
    performanceView = PerformanceView::Network;

    requestImmediateSysMonPaint(hwnd);
    return 0;
}

// Temperatures
if (
    mouseX >= 8 &&
    mouseX <= 155 &&
    mouseY >= 312 &&
    mouseY <= 348
)
{
    currentPage = AppPage::Temperatures;

    requestImmediateSysMonPaint(hwnd);
    return 0;
}

// Processes is intentionally top-navigation only.

// System Info
if (
    mouseX >= 8 &&
    mouseX <= 155 &&
    mouseY >= 352 &&
    mouseY <= 388
)
{
    currentPage = AppPage::SystemInfo;
    systemInfoScrollOffset = 0;

    requestImmediateSysMonPaint(hwnd);
    return 0;
}

// Settings
if (
    mouseX >= 8 &&
    mouseX <= 155 &&
    mouseY >= 392 &&
    mouseY <= 428
)
{
    currentPage = AppPage::Settings;
    settingsScrollOffset = 0;

    requestImmediateSysMonPaint(hwnd);
    return 0;
}

    // --------------------------------------------------------
    // TOOLS PAGE ACTIONS
    // --------------------------------------------------------
    if (currentPage == AppPage::Tools)
    {
        const int localX = mouseX - 165;
        const int localY = mouseY;

        auto inRect =
            [&](int left,
                int top,
                int right,
                int bottom)
        {
            return
                localX >= left &&
                localX <= right &&
                localY >= top &&
                localY <= bottom;
        };

        // Primary cards - first row.
        if (inRect(96, 199, 255, 222))
        {
            if (!launchWindowsTarget(hwnd, L"cleanmgr.exe"))
            {
                MessageBoxA(hwnd, "Unable to open Windows Disk Cleanup.", "SysMon", MB_OK | MB_ICONERROR);
            }
            return 0;
        }

        if (inRect(343, 199, 502, 222))
        {
            int result =
                MessageBoxA(
                    hwnd,
                    "System File Checker will scan protected Windows files and attempt to repair corrupted files.\n\nThis can take several minutes. Start the scan now?",
                    "System File Check",
                    MB_YESNO |
                    MB_ICONINFORMATION |
                    MB_DEFBUTTON2
                );

            if (result == IDYES)
            {
                if (!launchWindowsTarget(
                        hwnd,
                        L"cmd.exe",
                        L"/k sfc /scannow",
                        true
                    ))
                {
                    MessageBoxA(
                        hwnd,
                        "Unable to start System File Checker.",
                        "SysMon",
                        MB_OK |
                        MB_ICONERROR
                    );
                }
            }

            return 0;
        }

        if (inRect(590, 199, 749, 222))
        {
            if (!launchWindowsTarget(hwnd, L"SystemPropertiesPerformance.exe"))
            {
                MessageBoxA(hwnd, "Unable to open Windows Performance Options.", "SysMon", MB_OK | MB_ICONERROR);
            }
            return 0;
        }

        if (inRect(837, 199, 1003, 222))
        {
            StartupManager::show(hwnd);
            return 0;
        }

        // Primary cards - second row.
        if (inRect(96, 317, 255, 340))
        {
            if (!launchWindowsTarget(hwnd, L"eventvwr.msc"))
            {
                MessageBoxA(hwnd, "Unable to open Event Viewer.", "SysMon", MB_OK | MB_ICONERROR);
            }
            return 0;
        }

        if (inRect(343, 317, 502, 340))
        {
            if (!launchWindowsTarget(hwnd, L"msconfig.exe"))
            {
                MessageBoxA(hwnd, "Unable to open System Configuration.", "SysMon", MB_OK | MB_ICONERROR);
            }
            return 0;
        }

        if (inRect(590, 317, 749, 340))
        {
            if (!launchWindowsTarget(hwnd, L"services.msc"))
            {
                MessageBoxA(
                    hwnd,
                    "Unable to open Windows Services Manager.",
                    "SysMon",
                    MB_OK |
                    MB_ICONERROR
                );
            }
            return 0;
        }

        if (inRect(837, 317, 1003, 340))
        {
            if (!launchWindowsTarget(hwnd, L"cmd.exe", nullptr, true))
            {
                MessageBoxA(hwnd, "Unable to open an elevated Command Prompt.", "SysMon", MB_OK | MB_ICONERROR);
            }
            return 0;
        }

        // System Utilities rows.
        if (inRect(42, 416, 500, 454))
        {
            launchWindowsTarget(hwnd, L"dfrgui.exe");
            return 0;
        }

        if (inRect(42, 464, 500, 502))
        {
            launchWindowsTarget(hwnd, L"ms-settings:windowsupdate");
            return 0;
        }

        if (inRect(42, 512, 500, 550))
        {
            launchWindowsTarget(hwnd, L"powercfg.cpl");
            return 0;
        }

        if (inRect(42, 560, 500, 598))
        {
            launchWindowsTarget(hwnd, L"rstrui.exe");
            return 0;
        }

        if (inRect(42, 608, 500, 646))
        {
            launchWindowsTarget(hwnd, L"ms-settings:network-status");
            return 0;
        }

        // Quick Actions.
        if (inRect(540, 410, 999, 444))
        {
            launchWindowsTarget(hwnd, L"ms-settings:storagesense");
            return 0;
        }

        if (inRect(540, 452, 999, 486))
        {
            DWORD exitCode = MAXDWORD;
            if (runHiddenCommandAndWait(L"ipconfig /flushdns", exitCode))
            {
                MessageBoxA(hwnd, "The DNS resolver cache was flushed successfully.", "SysMon", MB_OK | MB_ICONINFORMATION);
            }
            else
            {
                MessageBoxA(hwnd, "SysMon could not flush the DNS cache.", "SysMon", MB_OK | MB_ICONERROR);
            }
            return 0;
        }

        if (inRect(540, 494, 999, 528))
        {
            launchWindowsTarget(hwnd, L"ms-settings:network-status");
            return 0;
        }

        if (inRect(540, 536, 999, 570))
        {
            int result =
                MessageBoxA(
                    hwnd,
                    "Empty the Recycle Bin?",
                    "SysMon",
                    MB_YESNO |
                    MB_ICONWARNING |
                    MB_DEFBUTTON2
                );

            if (result == IDYES)
            {
                HRESULT emptyResult =
                    SHEmptyRecycleBinW(
                        hwnd,
                        nullptr,
                        SHERB_NOCONFIRMATION |
                        SHERB_NOPROGRESSUI |
                        SHERB_NOSOUND
                    );

                if (SUCCEEDED(emptyResult))
                {
                    MessageBoxA(hwnd, "Recycle Bin emptied successfully.", "SysMon", MB_OK | MB_ICONINFORMATION);
                }
                else
                {
                    MessageBoxA(hwnd, "SysMon could not empty the Recycle Bin.", "SysMon", MB_OK | MB_ICONERROR);
                }
            }
            return 0;
        }

        if (inRect(540, 578, 999, 612))
        {
            std::string snapshotPath;
            if (createSysMonSnapshot(snapshotPath))
            {
                std::string message =
                    "System snapshot saved to:\n\n" +
                    snapshotPath;

                MessageBoxA(hwnd, message.c_str(), "SysMon", MB_OK | MB_ICONINFORMATION);
            }
            else
            {
                MessageBoxA(hwnd, "SysMon could not create the system snapshot file.", "SysMon", MB_OK | MB_ICONERROR);
            }
            return 0;
        }
    }

    // --------------------------------------------------------
    // PROCESS FILTER DROPDOWN
    // --------------------------------------------------------
    if (currentPage == AppPage::Processes)
    {
        // The Processes page content is drawn with a +165 logical X origin.
        // The filter button itself is local X 826..995.
        const int filterLeft = 991;
        const int filterRight = 1160;
        const int filterTop = 82;
        const int filterBottom = 124;
        const int menuTop = 128;
        const int optionHeight = 32;
        const int optionCount = 4;
        const int menuBottom = menuTop + optionHeight * optionCount;

        if (
            mouseX >= filterLeft &&
            mouseX <= filterRight &&
            mouseY >= filterTop &&
            mouseY <= filterBottom
        )
        {
            processFilterDropdownOpen =
                !processFilterDropdownOpen;
            processSearchFocused = false;
            processPriorityDropdownOpen = false;

            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (processFilterDropdownOpen)
        {
            if (
                mouseX >= filterLeft &&
                mouseX <= filterRight &&
                mouseY >= menuTop &&
                mouseY < menuBottom
            )
            {
                const int option =
                    (mouseY - menuTop) / optionHeight;

                switch (option)
                {
                case 0:
                    processFilterMode = ProcessFilterMode::All;
                    break;
                case 1:
                    processFilterMode = ProcessFilterMode::Apps;
                    break;
                case 2:
                    processFilterMode = ProcessFilterMode::Background;
                    break;
                case 3:
                    processFilterMode = ProcessFilterMode::Windows;
                    break;
                default:
                    break;
                }

                processFilterDropdownOpen = false;
                processScrollOffset = 0;
                hoveredProcessPid = MAXDWORD;
                selectedProcessPid = MAXDWORD;

                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            // Any click outside the open menu closes it, then the click can
            // continue to whichever control was actually pressed.
            processFilterDropdownOpen = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    // --------------------------------------------------------
    // PROCESS SEARCH BOX
    // --------------------------------------------------------
// Clear search button
if (
    currentPage == AppPage::Processes &&
    !processSearch.empty() &&
    mouseX >= 952 &&
    mouseX <= 979 &&
    mouseY >= 82 &&
    mouseY <= 124
)
{
    processSearch.clear();

    processScrollOffset = 0;

    processSearchFocused = true;

    InvalidateRect(
        hwnd,
        nullptr,
        FALSE
    );

    return 0;
}
    if (
        currentPage == AppPage::Processes &&
        mouseX >= 785 &&
        mouseX <= 981 &&
        mouseY >= 82 &&
        mouseY <= 124
    )
    {
        processSearchFocused = true;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }


    // --------------------------------------------------------
    // PROCESS TABLE HEADER CLICKS
    // --------------------------------------------------------

    if (
        currentPage == AppPage::Processes &&
        mouseY >= 320 &&
        mouseY <= 355
    )
    {
        ProcessSort newSort =
            processSort;

        bool headerClicked =
            true;


        // PROCESS
        if (
            mouseX >= 185 &&
            mouseX < 465
        )
        {
            newSort =
                ProcessSort::Name;
        }

        // CPU
        else if (
            mouseX >= 540 &&
            mouseX < 605
        )
        {
            newSort =
                ProcessSort::CPU;
        }

        // MEMORY
        else if (
            mouseX >= 605 &&
            mouseX < 705
        )
        {
            newSort =
                ProcessSort::Memory;
        }

        // THREADS
        else if (
            mouseX >= 705 &&
            mouseX < 785
        )
        {
            newSort =
                ProcessSort::Threads;
        }

        // PID
        else if (
            mouseX >= 465 &&
            mouseX < 540
        )
        {
            newSort =
                ProcessSort::PID;
        }

        else
        {
            headerClicked =
                false;
        }


        if (headerClicked)
        {
            if (newSort == processSort)
            {
                processSortDescending =
                    !processSortDescending;
            }
            else
            {
                processSort =
                    newSort;

                processSortDescending =
                    processSort !=
                    ProcessSort::Name;
            }

            processScrollOffset = 0;

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );

            return 0;
        }
    }

    // --------------------------------------------------------
// PROCESS ROW SELECTION
// --------------------------------------------------------

if (
    currentPage == AppPage::Processes &&
    mouseX >= 185 &&
    mouseX <= 865 &&
    mouseY >= 359 &&
    mouseY < 659
)
{
    int rowIndex =
        (mouseY - 359) / 25;

    if (
        rowIndex >= 0 &&
        rowIndex <
            static_cast<int>(
                visibleProcessPids.size()
            )
    )
    {
        DWORD groupKey = 0;

        if (
            rowIndex <
            static_cast<int>(
                visibleProcessGroupKeys.size()
            )
        )
        {
            groupKey =
                visibleProcessGroupKeys[rowIndex];
        }

        // Task-Manager-style grouping: ONLY the small chevron toggles
        // expand/collapse. Clicking the rest of the group row simply selects
        // that row instead of unexpectedly opening/closing the group.
        const bool clickedGroupChevron =
            groupKey != 0 &&
            mouseX >= 195 &&
            mouseX <= 222;

        if (clickedGroupChevron)
        {
            auto expanded =
                std::find(
                    expandedProcessGroupKeys.begin(),
                    expandedProcessGroupKeys.end(),
                    groupKey
                );

            if (expanded == expandedProcessGroupKeys.end())
            {
                expandedProcessGroupKeys.push_back(groupKey);
            }
            else
            {
                expandedProcessGroupKeys.erase(expanded);
            }

            hoveredProcessPid = MAXDWORD;
            processPriorityDropdownOpen = false;
            processSearchFocused = false;
            invalidateProcessTableFast(hwnd);
            return 0;
        }

        selectedProcessPid =
            visibleProcessPids[rowIndex];

        processPriorityDropdownOpen =
            false;

        processSearchFocused =
            false;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return 0;
    }
}
// --------------------------------------------------------
// PROCESS DETAIL ACTIONS
// --------------------------------------------------------

// Set Priority dropdown options. The menu opens upward from the button.
if (
    currentPage == AppPage::Processes &&
    processPriorityDropdownOpen &&
    selectedProcessPid != MAXDWORD &&
    selectedProcessPid != 0 &&
    selectedProcessPid != 4 &&
    mouseX >= 1059 &&
    mouseX <= 1150 &&
    mouseY >= 473 &&
    mouseY < 635
)
{
    const int optionHeight = 27;

    int option =
        (mouseY - 473) /
        optionHeight;

    DWORD priorityClass =
        NORMAL_PRIORITY_CLASS;

    const char* priorityName =
        "Normal";

    switch (option)
    {
    case 0:
        priorityClass = REALTIME_PRIORITY_CLASS;
        priorityName = "Realtime";
        break;

    case 1:
        priorityClass = HIGH_PRIORITY_CLASS;
        priorityName = "High";
        break;

    case 2:
        priorityClass = ABOVE_NORMAL_PRIORITY_CLASS;
        priorityName = "Above Normal";
        break;

    case 3:
        priorityClass = NORMAL_PRIORITY_CLASS;
        priorityName = "Normal";
        break;

    case 4:
        priorityClass = BELOW_NORMAL_PRIORITY_CLASS;
        priorityName = "Below Normal";
        break;

    case 5:
        priorityClass = IDLE_PRIORITY_CLASS;
        priorityName = "Low";
        break;

    default:
        processPriorityDropdownOpen = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    if (priorityClass == REALTIME_PRIORITY_CLASS)
    {
        int warningResult =
            MessageBoxA(
                hwnd,
                "Realtime priority can make Windows less responsive if the process uses too much CPU.\n\nContinue?",
                "Set Realtime Priority",
                MB_YESNO |
                MB_ICONWARNING |
                MB_DEFBUTTON2
            );

        if (warningResult != IDYES)
        {
            processPriorityDropdownOpen = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
    }

    if (setProcessPriorityClassByPid(
            selectedProcessPid,
            priorityClass
        ))
    {
        std::string message =
            "Process priority changed to ";

        message += priorityName;
        message += ".";

        MessageBoxA(
            hwnd,
            message.c_str(),
            "SysMon",
            MB_OK |
            MB_ICONINFORMATION
        );
    }
    else
    {
        MessageBoxA(
            hwnd,
            "SysMon could not change this process priority.\n\nThe process may be protected by Windows or require additional privileges.",
            "Unable to Set Priority",
            MB_OK |
            MB_ICONERROR
        );
    }

    processPriorityDropdownOpen = false;

    InvalidateRect(
        hwnd,
        nullptr,
        FALSE
    );

    return 0;
}

// Open File Location
if (
    currentPage == AppPage::Processes &&
    selectedProcessPid != MAXDWORD &&
    selectedProcessPid != 0 &&
    selectedProcessPid != 4 &&
    mouseX >= 885 &&
    mouseX <= 995 &&
    mouseY >= 641 &&
    mouseY <= 674
)
{
    processPriorityDropdownOpen = false;

    if (!openProcessFileLocation(
            hwnd,
            selectedProcessPid
        ))
    {
        MessageBoxA(
            hwnd,
            "SysMon could not open this process file location.\n\nThe executable path may be unavailable or protected by Windows.",
            "Unable to Open File Location",
            MB_OK |
            MB_ICONERROR
        );
    }

    InvalidateRect(
        hwnd,
        nullptr,
        FALSE
    );

    return 0;
}

// End Task / Kill Task
if (
    currentPage == AppPage::Processes &&
    selectedProcessPid != MAXDWORD &&
    selectedProcessPid != 0 &&
    selectedProcessPid != 4 &&
    selectedProcessPid != GetCurrentProcessId() &&
    mouseX >= 1000 &&
    mouseX <= 1076 &&
    mouseY >= 641 &&
    mouseY <= 674
)
{
    processPriorityDropdownOpen = false;

    int result =
        MessageBoxA(
            hwnd,
            "Are you sure you want to end this task?",
            "Confirm End Process",
            MB_YESNO |
            MB_ICONWARNING |
            MB_DEFBUTTON2
        );

    if (result == IDYES)
    {
        if (terminateTaskByPid(
                selectedProcessPid
            ))
        {
            MessageBoxA(
                hwnd,
                "The task was ended successfully.",
                "SysMon",
                MB_OK |
                MB_ICONINFORMATION
            );

            selectedProcessPid =
                MAXDWORD;

            processScrollOffset =
                0;

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );
        }
        else
        {
            MessageBoxA(
                hwnd,
                "SysMon could not terminate this process.\n\nIt may be protected by Windows or require administrator privileges.",
                "Unable to End Process",
                MB_OK |
                MB_ICONERROR
            );
        }
    }

    return 0;
}

// Set Priority button
if (
    currentPage == AppPage::Processes &&
    selectedProcessPid != MAXDWORD &&
    selectedProcessPid != 0 &&
    selectedProcessPid != 4 &&
    mouseX >= 1081 &&
    mouseX <= 1150 &&
    mouseY >= 641 &&
    mouseY <= 674
)
{
    processPriorityDropdownOpen =
        !processPriorityDropdownOpen;

    processSearchFocused = false;

    InvalidateRect(
        hwnd,
        nullptr,
        FALSE
    );

    return 0;
}

// Close an open priority menu when another Processes-page area is clicked.
if (
    currentPage == AppPage::Processes &&
    processPriorityDropdownOpen
)
{
    processPriorityDropdownOpen = false;

    InvalidateRect(
        hwnd,
        nullptr,
        FALSE
    );
}

    // --------------------------------------------------------
    // DASHBOARD PERFORMANCE / TEMPERATURE CARDS
    // --------------------------------------------------------

    if (
        currentPage == AppPage::Dashboard &&
        mouseY >= 118 &&
        mouseY <= 278
    )
    {
        // CPU
        if (
            mouseX >= 145 &&
            mouseX <= 435
        )
        {
            currentPage =
                AppPage::Performance;

            performanceView =
                PerformanceView::CPU;

            requestImmediateSysMonPaint(hwnd);

            return 0;
        }

        // Memory
        if (
            mouseX >= 450 &&
            mouseX <= 740
        )
        {
            currentPage =
                AppPage::Performance;

            performanceView =
                PerformanceView::Memory;

            requestImmediateSysMonPaint(hwnd);

            return 0;
        }

        // Disk
        if (
            mouseX >= 755 &&
            mouseX <= 1045
        )
        {
            currentPage =
                AppPage::Performance;

            performanceView =
                PerformanceView::Disk;

            selectedDiskIndex = 0;

            for (int i = 0;
                 i < static_cast<int>(
                     diskStats.size()
                 );
                 i++)
            {
                if (diskStats[i].systemDisk)
                {
                    selectedDiskIndex = i;
                    break;
                }
            }

            requestImmediateSysMonPaint(hwnd);

            return 0;
        }
    }

    if (
        currentPage == AppPage::Dashboard &&
        mouseY >= 292 &&
        mouseY <= 452
    )
    {
        // GPU
        if (
            mouseX >= 145 &&
            mouseX <= 435
        )
        {
            currentPage =
                AppPage::Performance;

            performanceView =
                PerformanceView::GPU;

            selectedGpuIndex = 0;

            if (!gpuStats.empty())
            {
                for (int i = 1;
                     i < static_cast<int>(
                         gpuStats.size()
                     );
                     i++)
                {
                    if (
                        gpuStats[i].
                            utilizationPercent >
                        gpuStats[
                            selectedGpuIndex
                        ].
                            utilizationPercent
                    )
                    {
                        selectedGpuIndex = i;
                    }
                }
            }

            requestImmediateSysMonPaint(hwnd);

            return 0;
        }

        // Temperature
        if (
            mouseX >= 450 &&
            mouseX <= 740
        )
        {
            currentPage =
                AppPage::Temperatures;

            requestImmediateSysMonPaint(hwnd);

            return 0;
        }

        // Network
        if (
            mouseX >= 755 &&
            mouseX <= 1045
        )
        {
            currentPage =
                AppPage::Performance;

            performanceView =
                PerformanceView::Network;

            selectedNetworkIndex = 0;

            requestImmediateSysMonPaint(hwnd);

            return 0;
        }
    }



    break;

}

case WM_HOTKEY:
{
    if (wParam == 1)
    {
        appSettings.showOverlayWidget =
            !appSettings.showOverlayWidget;
        saveAppSettings();
        applyOverlayWidgetSetting();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    break;
}

case WM_CLOSE:
{
    flushPendingSettingsSave(hwnd);

    if (
        appSettings.minimizeToTray &&
        appSettings.showInTray
    )
    {
        if (!minimizeSysMonToTray(hwnd))
            MessageBoxA(hwnd,"The notification area is unavailable. SysMon will stay open.","SysMon",MB_OK|MB_ICONWARNING);
        return 0;
    }

    DestroyWindow(hwnd);
    return 0;
}

case WM_ENTERSIZEMOVE:
{
    sysMonLiveResizing = true;

    // A pending "settle" timer from a non-drag size change (see WM_SIZE)
    // must not fire mid-drag and prematurely switch back to full-quality
    // rendering; WM_EXITSIZEMOVE below is what ends this drag.
    KillTimer(hwnd, 6);

    // Stop the monitoring timer completely while Windows is in its modal
    // move/resize loop.  This prevents even lightweight timer messages from
    // competing with DWM while the user is dragging the window.
    KillTimer(hwnd, 1);

    return 0;
}

case WM_EXITSIZEMOVE:
{
    sysMonLiveResizing = false;
    KillTimer(hwnd, 6);

    // Resume normal sampling after the drag.  Do not run updateStats() here:
    // doing synchronous hardware I/O on mouse release makes the window feel
    // as if it sticks for a moment.  The timer will refresh on its next tick.
    SetTimer(hwnd, 1, static_cast<UINT>((std::max)(250, appSettings.updateIntervalMs)), nullptr);

    InvalidateRect(
        hwnd,
        nullptr,
        FALSE
    );

    return 0;
}

case WM_SIZE:
{
    if (wParam == SIZE_MINIMIZED)
    {
        KillTimer(hwnd, 6);

        if (appSettings.minimizeToTray && appSettings.showInTray)
            minimizeSysMonToTray(hwnd);
        return 0;
    }

    // Maximizing, restoring from the taskbar, and Aero Snap all deliver a
    // single WM_SIZE with no WM_ENTERSIZEMOVE/WM_EXITSIZEMOVE around them, so
    // without this they fell straight through to the full high-resolution
    // re-render below. That re-render can take a noticeable moment on a big
    // or maximized window, and while it runs DWM keeps showing a stretched
    // copy of the previous frame at the old size - the "resolution goes bad,
    // then it lags, then it gets better" a resize or un-minimize produces.
    // Route this through the exact same cheap-scaled-preview path the live
    // drag case already uses (WM_PAINT takes it whenever sysMonLiveResizing
    // is set), then settle on one sharp re-render shortly after instead of
    // blocking the resize itself on the expensive redraw.
    const bool wasAlreadyTransitioning = sysMonLiveResizing;

    sysMonLiveResizing = true;

    InvalidateRect(
        hwnd,
        nullptr,
        FALSE
    );

    if (!wasAlreadyTransitioning)
    {
        // One-shot: repeated WM_SIZE messages (e.g. a snap animation) just
        // keep resetting this timer instead of queueing several redraws.
        SetTimer(hwnd, 6, 60, nullptr);
    }

    return 0;
}
case WM_DEVICECHANGE:
{
    // Refresh the About PC connected-device / network data
    // asynchronously when Windows reports a hardware change.
    refreshNetworkStats(true);
    refreshSystemInfo(true);
    refreshGpuStats(true);
    refreshTemperatureStats(true);

    if (
        currentPage == AppPage::SystemInfo ||
        currentPage == AppPage::Performance ||
        currentPage == AppPage::Temperatures
    )
    {
        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );
    }

    break;
}

case WM_TIMER:
{
    if (wParam==8) { KillTimer(hwnd,8); if(!appSettings.showInTray && IsWindowVisible(hwnd)) removeTrayIcon(); return 0; }
    if (wParam==7) {
        if (!monitoringPaused) pollSettingsServices(hwnd);
        const std::string previous=Updates::status;
        Updates::poll(appSettings.checkForUpdates);
        if(previous!=Updates::status && currentPage==AppPage::Settings)
            InvalidateRect(hwnd,nullptr,FALSE);
        if(appSettings.checkForUpdates && !Updates::pending && Updates::displayed &&
           Updates::displayed->newer && Updates::notifiedVersion!=Updates::displayed->version) {
            Updates::notifiedVersion=Updates::displayed->version;
            const std::string prompt=Updates::displayed->status+" Open the download page?";
            if(MessageBoxA(hwnd,prompt.c_str(),"SysMon update",MB_YESNO|MB_ICONINFORMATION)==IDYES)
                ShellExecuteA(hwnd,"open",Updates::releasePage,nullptr,nullptr,SW_SHOWNORMAL);
        }
        return 0;
    }

    // Deferred startup warm-up.  WM_CREATE now returns immediately so the
    // first SysMon frame is visible before any hardware/sensor discovery.
    if (wParam == 3)
    {
        static int startupStage = 0;

        if (startupStage == 0)
        {
            // Decode PNG assets after the window already has a painted frame.
            preloadUiAssets();
            startupStage++;

            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (startupStage == 1)
        {
            // Start the sampler; it primes CPU timing and launches the sensor helper.
            startHardwareSensorBridge();
            startupStage++;
            return 0;
        }

        // First statistics pass. Stats.cpp deliberately warms only one heavy
        // hardware class on this pass; the rest arrive on later 500 ms ticks.
        updateStats(true);

        KillTimer(hwnd, 3);
        SetTimer(hwnd, 1, static_cast<UINT>((std::max)(250, appSettings.updateIntervalMs)), nullptr);

        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    if (wParam == 5)
    {
        flushPendingSettingsSave(hwnd);
        return 0;
    }

    // Replace the quick navigation preview with the normal crisp frame.
    // Repeated clicks reuse this timer ID, so only the final destination page
    // receives the expensive high-resolution render.
    if (wParam == 4)
    {
        KillTimer(hwnd, 4);
        sysMonNavigationPreviewPending = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    // A maximize/restore/snap transition has settled: switch back from the
    // cheap scaled preview to a full-quality render at the final size.
    if (wParam == 6)
    {
        KillTimer(hwnd, 6);
        sysMonLiveResizing = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    if (wParam == 1)
    {
        if (monitoringPaused) return 0;
        // Keep live edge/corner dragging responsive.
        if (sysMonLiveResizing)
        {
            return 0;
        }

        const ULONGLONG timerNow = GetTickCount64();

        // Sampling runs in the background. Keep the existing hardware cadence
        // without deferring monitoring just because the user is interacting.
        static ULONGLONG lastHeavyStatsTick = 0;
        const ULONGLONG heavyIntervalMs =
            (currentPage == AppPage::Settings || currentPage == AppPage::Tools)
                ? 4000 : 900;
        const bool allowHeavyStats = lastHeavyStatsTick == 0 ||
            timerNow - lastHeavyStatsTick >= heavyIntervalMs;

        updateStats(allowHeavyStats);

        if (allowHeavyStats)
        {
            lastHeavyStatsTick = timerNow;
        }

        if (
            selectedDiskIndex >=
                static_cast<int>(
                    diskStats.size()
                )
        )
        {
            selectedDiskIndex = 0;
        }

        if (
            selectedGpuIndex >=
                static_cast<int>(
                    gpuStats.size()
                )
        )
        {
            selectedGpuIndex = 0;
        }

        if (
            selectedNetworkIndex >=
                static_cast<int>(
                    networkStats.size()
                )
        )
        {
            selectedNetworkIndex = 0;
        }

        if (
            selectedTemperatureGpuIndex >=
                static_cast<int>(
                    gpuStats.size()
                )
        )
        {
            selectedTemperatureGpuIndex = 0;
        }

        // Fast-changing monitoring pages stay at the 500 ms sampling rate.
        // Slower pages redraw once per second, while static Tools/Settings
        // pages do not waste CPU repainting when nothing changed.
        static ULONGLONG lastSlowUiRedrawTick = 0;
        const ULONGLONG now = GetTickCount64();

        bool repaintMainWindow =
            currentPage == AppPage::Dashboard ||
            currentPage == AppPage::Performance ||
            currentPage == AppPage::Temperatures;

        if (
            currentPage == AppPage::Processes ||
            currentPage == AppPage::SystemInfo
        )
        {
            if (
                lastSlowUiRedrawTick == 0 ||
                now - lastSlowUiRedrawTick >= 1000
            )
            {
                repaintMainWindow = true;
                lastSlowUiRedrawTick = now;
            }
        }

        if (
            repaintMainWindow &&
            IsWindowVisible(hwnd)
        )
        {
            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );
        }

        // Refresh the desktop widget if it is visible
        if (
            widgetEnabled &&
            desktopWidget != nullptr &&
            IsWindowVisible(desktopWidget)
        )
        {
            InvalidateRect(
                desktopWidget,
                nullptr,
                TRUE
            );

            UpdateWindow(
                desktopWidget
            );
        }
    }

    return 0;
}
case WM_TRAYICON:
{
    if (
        lParam == WM_LBUTTONDBLCLK
    )
    {
        restoreSysMon(
            hwnd
        );
    }

    if (
        lParam == WM_RBUTTONUP
    )
    {
        POINT cursor;

        GetCursorPos(
            &cursor
        );

        HMENU menu =
            CreatePopupMenu();

        AppendMenuA(
            menu,
            MF_STRING,
            ID_TRAY_OPEN,
            "Open SysMon"
        );

        AppendMenuA(menu,MF_STRING|(appSettings.showOverlayWidget?MF_CHECKED:0),6001,"Show Overlay");
        AppendMenuA(menu,MF_STRING,6002,monitoringPaused?"Resume Monitoring":"Pause Monitoring");
        AppendMenuA(menu,MF_STRING,6003,"Settings");
        AppendMenuA(menu,MF_STRING,6004,"Configure Overlay");
        AppendMenuA(
            menu,
            MF_SEPARATOR,
            0,
            nullptr
        );

        AppendMenuA(
            menu,
            MF_STRING,
            ID_TRAY_EXIT,
            "Exit"
        );

        SetForegroundWindow(
            hwnd
        );

        int command =
            TrackPopupMenu(
                menu,
                TPM_RETURNCMD |
                TPM_RIGHTBUTTON,

                cursor.x,
                cursor.y,

                0,

                hwnd,
                nullptr
            );

        DestroyMenu(
            menu
        );

        if(command==6001) { appSettings.showOverlayWidget=!appSettings.showOverlayWidget;applyOverlayWidgetSetting();saveAppSettings(); }
        if(command==6002) { setMonitoringPaused(!monitoringPaused);InvalidateRect(hwnd,nullptr,FALSE); }
        if(command==6003) {currentPage=AppPage::Settings;restoreSysMon(hwnd);}
        if(command==6004) configureOverlay(hwnd);
        if (
            command ==
            ID_TRAY_OPEN
        )
        {
            restoreSysMon(
                hwnd
            );
        }

        if (
            command ==
            ID_TRAY_EXIT
        )
        {
            removeTrayIcon();

            DestroyWindow(
                hwnd
            );
        }
    }

    return 0;
}
case WM_SIZING:
{
    enforceSysMonResizeAspectRatio(
        hwnd,
        wParam,
        reinterpret_cast<RECT*>(
            lParam
        )
    );

    return TRUE;
}

   case WM_GETMINMAXINFO:
{
    MINMAXINFO* minMaxInfo =
        reinterpret_cast<MINMAXINFO*>(
            lParam
        );

    if (minMaxInfo != nullptr)
    {
        // Keep controls and text usable when the user drags the window
        // smaller. Everything above this minimum remains fully resizable.
        minMaxInfo->ptMinTrackSize.x = 840;
        minMaxInfo->ptMinTrackSize.y = 540;
    }

    return 0;
}


   case WM_PAINT:
{
    PAINTSTRUCT ps;

    HDC hdc =
        BeginPaint(
            hwnd,
            &ps
        );

    RECT client = {};

    GetClientRect(
        hwnd,
        &client
    );

    const int clientWidth =
        (std::max)(
            1,
            static_cast<int>(
                client.right - client.left
            )
        );

    const int clientHeight =
        (std::max)(
            1,
            static_cast<int>(
                client.bottom - client.top
            )
        );


    // --------------------------------------------------------
    // FAST TAB-SWITCH PREVIEW
    // --------------------------------------------------------
    // The expensive final renderer still runs at the real window resolution,
    // but navigation first draws the same logical UI at 1190x720 and stretches
    // it into place. This first frame is intentionally cheap and very brief.
    if (
        sysMonNavigationPreviewPending &&
        std::abs(getSysMonUiTransform(hwnd).scaleX - getSysMonUiTransform(hwnd).scale) < 0.01 &&
        !sysMonLiveResizing &&
        ensureSysMonNavigationPreviewBuffer(hdc)
    )
    {
        RECT previewRect =
        {
            0,
            0,
            sysMonDesignWidth,
            sysMonDesignHeight
        };

        HBRUSH previewBackground =
            CreateSolidBrush(uiColor(RGB(18, 20, 26)));

        if (previewBackground != nullptr)
        {
            FillRect(
                sysMonNavigationPreviewDC,
                &previewRect,
                previewBackground
            );
            DeleteObject(previewBackground);
        }

        const int savedPreviewState =
            SaveDC(sysMonNavigationPreviewDC);

        SetGraphicsMode(
            sysMonNavigationPreviewDC,
            GM_ADVANCED
        );

        XFORM identityTransform = {};
        identityTransform.eM11 = 1.0f;
        identityTransform.eM22 = 1.0f;

        SetWorldTransform(
            sysMonNavigationPreviewDC,
            &identityTransform
        );

        setUiRenderTransform(1.0f, 0, 0);
        setProcessFastScrollRender(false);
        setUiFastNavigationRender(true);

        drawDashboard(
            hwnd,
            sysMonNavigationPreviewDC
        );

        setUiFastNavigationRender(false);
        setUiRenderTransform(1.0f, 0, 0);

        RestoreDC(
            sysMonNavigationPreviewDC,
            savedPreviewState
        );

        if (sysMonShellBrush == nullptr)
        {
            sysMonShellBrush =
                CreateSolidBrush(uiColor(RGB(18, 20, 26)));
        }

        if (sysMonShellBrush != nullptr)
        {
            FillRect(hdc, &client, sysMonShellBrush);
        }

        const SysMonUiTransform previewUi =
            getSysMonUiTransform(hwnd);

        const int oldStretchMode =
            SetStretchBltMode(hdc, COLORONCOLOR);

        StretchBlt(
            hdc,
            previewUi.offsetX,
            previewUi.offsetY,
            previewUi.drawWidth,
            previewUi.drawHeight,
            sysMonNavigationPreviewDC,
            0,
            0,
            sysMonDesignWidth,
            sysMonDesignHeight,
            SRCCOPY
        );

        SetStretchBltMode(hdc, oldStretchMode);

        const int previewUiRight =
            previewUi.offsetX + previewUi.drawWidth;

        if (previewUiRight < clientWidth)
        {
            if (sysMonHeaderExtensionBrush == nullptr)
            {
                sysMonHeaderExtensionBrush =
                    CreateSolidBrush(uiColor(RGB(14, 18, 24)));
            }

            if (sysMonHeaderExtensionBrush != nullptr)
            {
                const int previewHeaderHeight =
                    (std::max)(
                        1,
                        static_cast<int>(
                            58.0 * previewUi.scale + 0.5
                        )
                    );

                RECT headerExtension =
                {
                    previewUiRight,
                    0,
                    clientWidth,
                    (std::min)(
                        previewHeaderHeight,
                        clientHeight
                    )
                };

                FillRect(
                    hdc,
                    &headerExtension,
                    sysMonHeaderExtensionBrush
                );
            }
        }

        // One-shot sharp replacement. Calling SetTimer again on rapid clicks
        // resets the same timer instead of queueing many expensive renders.
        SetTimer(hwnd, 4, 24, nullptr);

        EndPaint(hwnd, &ps);
        return 0;
    }


    // --------------------------------------------------------
    // FAST LIVE-RESIZE PREVIEW
    // --------------------------------------------------------
    // Re-rendering the entire high-DPI dashboard for every single WM_SIZE
    // generated by an edge drag is expensive. While the user is dragging,
    // simply scale the most recently completed frame. As soon as the drag
    // ends WM_EXITSIZEMOVE invalidates the window and a fresh full-quality
    // frame is rendered at the final size.
    if (
        sysMonLiveResizing &&
        sysMonFrameDC != nullptr &&
        sysMonFrameBitmap != nullptr &&
        sysMonLastRenderedWidth > 0 &&
        sysMonLastRenderedHeight > 0
    )
    {
        HBRUSH previewBrush = sysMonShellBrush;

        if (previewBrush == nullptr)
        {
            sysMonShellBrush =
                CreateSolidBrush(uiColor(RGB(18, 20, 26)));
            previewBrush = sysMonShellBrush;
        }

        if (previewBrush != nullptr)
        {
            FillRect(hdc, &client, previewBrush);
        }

        const int oldStretchMode =
            SetStretchBltMode(hdc, COLORONCOLOR);

        StretchBlt(
            hdc,
            0,
            0,
            clientWidth,
            clientHeight,
            sysMonFrameDC,
            0,
            0,
            sysMonLastRenderedWidth,
            sysMonLastRenderedHeight,
            SRCCOPY
        );

        SetStretchBltMode(hdc, oldStretchMode);
        EndPaint(hwnd, &ps);
        return 0;
    }


    // --------------------------------------------------------
    // FULL-RESOLUTION REUSABLE BACK BUFFER
    // --------------------------------------------------------
    // The bitmap/DC are retained between frames. This avoids allocating and
    // freeing a multi-megabyte surface every 500 ms and avoids resize-time
    // allocation storms. The buffer grows in 256px chunks and is reused when
    // the window is restored or made smaller.
    if (!ensureSysMonFrameBuffer(
            hdc,
            clientWidth,
            clientHeight
        ))
    {
        EndPaint(hwnd, &ps);
        return 0;
    }

    HDC frameDC = sysMonFrameDC;

    const bool fastProcessPaint =
        sysMonProcessScrollPaintPending &&
        currentPage == AppPage::Processes &&
        sysMonLastRenderedWidth == clientWidth &&
        sysMonLastRenderedHeight == clientHeight;

    RECT frameRect =
    {
        0,
        0,
        clientWidth,
        clientHeight
    };

    if (sysMonShellBrush == nullptr)
    {
        sysMonShellBrush =
            CreateSolidBrush(
                uiColor(RGB(18, 20, 26))
            );
    }

    const SysMonUiTransform transform =
        getSysMonUiTransform(hwnd);

    if (!fastProcessPaint && sysMonShellBrush != nullptr)
    {
        // Fill any area outside the 1190x720 logical layout with the same
        // main-page background used by SysMon. On wide/maximized monitors this
        // makes the canvas continue cleanly to the right edge instead of
        // leaving a darker letterbox strip.
        FillRect(
            frameDC,
            &frameRect,
            sysMonShellBrush
        );

        // The logical UI draws its top navigation/header only inside the
        // scaled design width. Extend that header across any spare right-side
        // width so maximized mode still looks like one continuous window.
        const int headerHeight =
            (std::max)(
                1,
                static_cast<int>(
                    58.0 * transform.scale + 0.5
                )
            );

        const int uiRight =
            transform.offsetX +
            transform.drawWidth;

        if (uiRight < clientWidth)
        {
            if (sysMonHeaderExtensionBrush == nullptr)
            {
                sysMonHeaderExtensionBrush =
                    CreateSolidBrush(
                        uiColor(RGB(14, 18, 24))
                    );
            }

            if (sysMonHeaderExtensionBrush != nullptr)
            {
                RECT headerExtension =
                {
                    uiRight,
                    0,
                    clientWidth,
                    (std::min)(headerHeight, clientHeight)
                };

                FillRect(
                    frameDC,
                    &headerExtension,
                    sysMonHeaderExtensionBrush
                );
            }
        }
    }

    const int savedFrameState =
        SaveDC(frameDC);

    SetGraphicsMode(
        frameDC,
        GM_ADVANCED
    );

    XFORM uiTransform = {};
    uiTransform.eM11 =
        static_cast<FLOAT>(transform.scaleX);
    uiTransform.eM12 = 0.0f;
    uiTransform.eM21 = 0.0f;
    uiTransform.eM22 =
        static_cast<FLOAT>(transform.scale);
    uiTransform.eDx =
        static_cast<FLOAT>(transform.offsetX);
    uiTransform.eDy =
        static_cast<FLOAT>(transform.offsetY);

    SetWorldTransform(
        frameDC,
        &uiTransform
    );

    setUiRenderTransform(
        static_cast<float>(transform.scale),
        transform.offsetX,
        transform.offsetY,
        static_cast<float>(transform.scaleX)
    );

    // During process-list scrolling the retained frame already contains the
    // rest of the page. Redraw only the process table; normal paints still
    // render the complete high-resolution interface.
    setProcessFastScrollRender(fastProcessPaint);

    drawDashboard(
        hwnd,
        frameDC
    );

    setProcessFastScrollRender(false);

    setUiRenderTransform(
        1.0f,
        0,
        0
    );

    RestoreDC(
        frameDC,
        savedFrameState
    );


    // Present only the process-table region during fast scrolling. Normal
    // paints continue to blit the completed full-resolution frame.
    if (fastProcessPaint)
    {
        const RECT processRect = getProcessTablePhysicalRect(hwnd);
        const int blitWidth =
            (std::max)(
                0,
                static_cast<int>(
                    processRect.right -
                    processRect.left
                )
            );

        const int blitHeight =
            (std::max)(
                0,
                static_cast<int>(
                    processRect.bottom -
                    processRect.top
                )
            );

        BitBlt(
            hdc,
            processRect.left,
            processRect.top,
            blitWidth,
            blitHeight,
            frameDC,
            processRect.left,
            processRect.top,
            SRCCOPY
        );
    }
    else
    {
        BitBlt(
            hdc,
            0,
            0,
            clientWidth,
            clientHeight,
            frameDC,
            0,
            0,
            SRCCOPY
        );
    }

    sysMonProcessScrollPaintPending = false;
    sysMonLastRenderedWidth = clientWidth;
    sysMonLastRenderedHeight = clientHeight;


    EndPaint(
        hwnd,
        &ps
    );

    return 0;
}



    case WM_ERASEBKGND:
        return 1;


   case WM_DESTROY:
    AppLifecycle::log("Shutdown requested");
    AppLifecycle::beginShutdown();
    for (UINT_PTR timer=1;timer<=8;++timer) KillTimer(hwnd,timer);
    UnregisterHotKey(hwnd,1);
    if (desktopWidget) { KillTimer(desktopWidget,2); DestroyWindow(desktopWidget); desktopWidget=nullptr; }
    removeTrayIcon();
    SettingsRuntime::logs.stop();
{
    flushPendingSettingsSave(hwnd);

    KillTimer(
        hwnd,
        1
    );

    KillTimer(
        hwnd,
        5
    );

    KillTimer(
        hwnd,
        6
    );

    KillTimer(
        hwnd,
        3
    );

    KillTimer(
        hwnd,
        4
    );

    DeleteObject(titleFont);
    DeleteObject(subtitleFont);
    DeleteObject(labelFont);
    DeleteObject(mediumFont);
    DeleteObject(bigFont);
    DeleteObject(smallFont);

    DeleteObject(widgetLabelFont);
    DeleteObject(widgetValueFont);

    KillTimer(hwnd,7);
    Updates::sampler().stop();
    stopProcessSampler();
    stopHardwareSensorBridge();

    clearUiImageCache();
    releaseSysMonNavigationPreviewBuffer();
    releaseSysMonFrameBuffer();

    if (sysMonTaskbarIcon != nullptr)
    {
        DestroyIcon(
            sysMonTaskbarIcon
        );

        sysMonTaskbarIcon = nullptr;
    }

removeTrayIcon();
    AppLifecycle::log("Shutdown completed");
    if(AppLifecycle::shutdownDone)SetEvent(AppLifecycle::shutdownDone);
    PostQuitMessage(0);

    return 0;
}
    }

    return DefWindowProc(
        hwnd,
        message,
        wParam,
        lParam
    );
}


// ============================================================
// Program entry
// ============================================================

int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE,
    LPSTR,
    int nCmdShow)
{
    if (!AppLifecycle::acquireInstance()) return 0;
    // Assets and helper files belong to this executable, regardless of shortcut working directory.
    wchar_t executablePath[32768]{};
    if (GetModuleFileNameW(nullptr,executablePath,32768)) {
        std::wstring directory=executablePath;auto separator=directory.find_last_of(L"\\/");
        if(separator!=std::wstring::npos)SetCurrentDirectoryW(directory.substr(0,separator).c_str());
    }
    AppLifecycle::initializeLog(getSysMonDataFolderPath());
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(
        &gdiplusToken,
        &gdiplusStartupInput,
        nullptr
    );
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const char CLASS_NAME[] =
        "SysMonWindowClass";

    WNDCLASSA wc = {};

    wc.lpfnWndProc =
        WindowProc;

    wc.hInstance =
        hInstance;

    wc.lpszClassName =
        CLASS_NAME;

    wc.hCursor =
        LoadCursor(
            nullptr,
            IDC_ARROW
        );

    // Give Windows a dark native fallback surface immediately. This avoids
    // a white client flash before SysMon's first buffered WM_PAINT completes.
    HBRUSH mainClassBackground =
        CreateSolidBrush(uiColor(RGB(18, 20, 26)));

    wc.hbrBackground =
        mainClassBackground;

    if (!RegisterClassA(&wc))
    {
        if (mainClassBackground != nullptr)
        {
            DeleteObject(mainClassBackground);
        }
        return 0;
    }
const char WIDGET_CLASS_NAME[] =
    "SysMonDesktopWidget";

WNDCLASSA widgetClass = {};

widgetClass.lpfnWndProc =
    WidgetProc;

widgetClass.hInstance =
    hInstance;

widgetClass.lpszClassName =
    WIDGET_CLASS_NAME;

widgetClass.hCursor =
    LoadCursor(
        nullptr,
        IDC_ARROW
    );

widgetClass.hbrBackground =
    nullptr;

if (!RegisterClassA(&widgetClass))
{
    if (mainClassBackground != nullptr)
    {
        DeleteObject(mainClassBackground);
    }
    return 0;
}

    HWND hwnd =
        CreateWindowExA(
            0,
            CLASS_NAME,
            "",

            WS_OVERLAPPED |
            WS_CAPTION |
            WS_SYSMENU |
            WS_THICKFRAME |
            WS_MINIMIZEBOX |
            WS_MAXIMIZEBOX,

            CW_USEDEFAULT,
            CW_USEDEFAULT,

            1200,
            760,

            nullptr,
            nullptr,
            hInstance,
            nullptr
        );


    if (hwnd == nullptr)
    {
        if (mainClassBackground != nullptr)
        {
            DeleteObject(mainClassBackground);
        }
        return 0;
    }

    applySysMonTitleBarTheme(
        hwnd
    );

    applySysMonWindowIcon(
        hwnd
    );

    hideSysMonCaptionIcon(
        hwnd
    );
desktopWidget =
    CreateWindowExA(
        WS_EX_LAYERED |
        WS_EX_TOOLWINDOW |
        WS_EX_NOACTIVATE,

        WIDGET_CLASS_NAME,

        "",

        WS_POPUP,

        0,
        0,
        240,
        100,

        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

if (desktopWidget != nullptr)
{
    // Load saved widget position
    loadWidgetSettings();

    // The Settings page owns the user-facing overlay preference. Preserve the
    // existing widget position but use the app setting for visibility.
    widgetEnabled = appSettings.showOverlayWidget;
    saveWidgetEnabled();

    positionDesktopWidget(
        desktopWidget
    );
}

    ShowWindow(
        hwnd,
        nCmdShow
    );

    UpdateWindow(hwnd);
    const std::string preferences=getSettingsPath();
    if(!GetPrivateProfileIntA("General","WelcomeShown",0,preferences.c_str())) {
        MessageBoxA(hwnd,"Welcome to SysMon.\n\nBy default, X minimizes SysMon to the tray. Use tray > Exit or Ctrl+Q to fully close it.\n\nCtrl+P pauses monitoring. Ctrl+Alt+O toggles the overlay. Drag the overlay to move it; right-click it to customize.\n\nRight-click Settings for updates and licenses. Reset to Defaults can reset one section or everything.","Welcome to SysMon",MB_OK|MB_ICONINFORMATION);
        WritePrivateProfileStringA("General","WelcomeShown","1",preferences.c_str());
    }

    MSG msg = {};

   while (GetMessage(
    &msg,
    nullptr,
    0,
    0) > 0)
{
    if (StartupManager::window && IsDialogMessage(StartupManager::window,&msg)) continue;
    if (ProcessDetails::window && IsDialogMessage(ProcessDetails::window,&msg)) continue;
    TranslateMessage(
        &msg
    );

    DispatchMessage(
        &msg
    );
}

clearUiImageCache();

GdiplusShutdown(
    gdiplusToken
);

if (mainClassBackground != nullptr)
{
    DeleteObject(mainClassBackground);
}

return 0;
}

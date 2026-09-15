#pragma once

#include <windows.h>

#define WM_TRAYICON (WM_APP + 1)

#define ID_TRAY_OPEN 1001
#define ID_TRAY_EXIT 1002

void addTrayIcon(HWND hwnd);
void removeTrayIcon();
void restoreSysMon(HWND hwnd);
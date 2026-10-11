/*
 * Tux-Tools - native Win32 edition
 *
 * Runtime DPI helper. The application is declared Per-Monitor-V2 DPI aware in
 * its manifest, so Windows no longer bitmap-stretches the window. Every metric
 * that used to be a fixed pixel value is expressed in 96-DPI design units and
 * passed through dpi::scale() so the UI stays crisp and correctly sized on
 * high-DPI displays.
 */

#pragma once

#include <windows.h>

namespace dpi {

inline int g_dpi = 96;

inline void setDpi(int dpiValue) {
    if (dpiValue > 0) g_dpi = dpiValue;
}

inline int getDpi() {
    return g_dpi;
}

// Converts a 96-DPI (logical) value to physical pixels.
inline int scale(int value) {
    return MulDiv(value, g_dpi, 96);
}

// Queries the DPI of a window, with a fallback for older systems.
inline int queryWindowDpi(HWND hwnd) {
    typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn getDpiForWindow = nullptr;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) {
            getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
                reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")));
        }
    }
    if (getDpiForWindow && hwnd) {
        UINT value = getDpiForWindow(hwnd);
        if (value > 0) return static_cast<int>(value);
    }
    HDC dc = hwnd ? GetDC(hwnd) : nullptr;
    int fallback = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc && hwnd) ReleaseDC(hwnd, dc);
    return fallback > 0 ? fallback : 96;
}

// Queries the primary/system DPI (used before any window exists).
inline int querySystemDpi() {
    HDC dc = GetDC(nullptr);
    int value = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return value > 0 ? value : 96;
}

}  // namespace dpi

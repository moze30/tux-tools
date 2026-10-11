/*
 * Tux-Tools - native Win32 edition
 *
 * Design tokens. Layout follows the Docker-Desktop-style arrangement: a
 * coloured top bar, a light navigation sidebar, a light content area with a
 * data table and a dark status bar. Typography, spacing and radii follow the
 * Fluent 2 / Windows 11 guidance.
 *
 * Colours are the project's own palette: the three accents
 * (#448AFF core, #03A9F4 hover, #0288D1 border) plus a light neutral scale.
 *
 * Colours and font sizes are constants; spacing, radii and metrics are
 * DPI-scaled at startup by init() so the UI stays crisp and correctly sized
 * on high-DPI displays (the app is Per-Monitor-V2 DPI aware).
 */

#pragma once

#include <windows.h>

#include "dpi.h"

namespace theme {

// Top bar (accent).
const COLORREF kTopBar = RGB(0x44, 0x8A, 0xFF);        // #448AFF
const COLORREF kTopBarText = RGB(255, 255, 255);
const COLORREF kTopBarMuted = RGB(0xD5, 0xE3, 0xFF);
const COLORREF kSearchFill = RGB(0xF2, 0xF2, 0xF2);
const COLORREF kSearchFillTop = RGB(0x5A, 0x9A, 0xFF);

// Surfaces (light).
const COLORREF kSidebarBg = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kContentBg = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kStatusBg = RGB(0x1A, 0x1A, 0x1A);
const COLORREF kStatusText = RGB(0xE0, 0xE0, 0xE0);

// Sidebar.
const COLORREF kSidebarText = RGB(0x21, 0x21, 0x21);
const COLORREF kSidebarMuted = RGB(0x6B, 0x6B, 0x6B);
const COLORREF kNavHover = RGB(0xF2, 0xF2, 0xF2);
const COLORREF kNavSelected = RGB(0xE8, 0xF0, 0xFE);
const COLORREF kNavSelectedBar = RGB(0x44, 0x8A, 0xFF);

// Content / table.
const COLORREF kPageTitle = RGB(0x1A, 0x1A, 0x1A);
const COLORREF kPageSubtitle = RGB(0x6B, 0x6B, 0x6B);
const COLORREF kTableHeaderText = RGB(0x6B, 0x6B, 0x6B);
const COLORREF kTableText = RGB(0x21, 0x21, 0x21);
const COLORREF kTableRowHover = RGB(0xF5, 0xF9, 0xFF);
const COLORREF kTableSelected = RGB(0xE8, 0xF0, 0xFE);
const COLORREF kDivider = RGB(0xE6, 0xE6, 0xE6);
const COLORREF kBorder = RGB(0xE0, 0xE0, 0xE0);

// Accent palette (user specified): #448AFF core, #03A9F4 hover, #0288D1 border.
const COLORREF kAccent = RGB(0x44, 0x8A, 0xFF);
const COLORREF kAccentHover = RGB(0x03, 0xA9, 0xF4);
const COLORREF kAccentBorder = RGB(0x02, 0x88, 0xD1);

// Type ramp (Windows), in 96-DPI design pixels.
const int kFontCaption = 12;
const int kFontBody = 14;
const int kFontSubtitle = 20;

// Spacing scale (Windows), scaled by init().
inline int kSpaceXS = 4;
inline int kSpaceS = 8;
inline int kSpaceM = 12;
inline int kSpaceL = 16;
inline int kSpaceXL = 24;

// Corner radius.
inline int kRadiusControl = 4;
inline int kRadiusCard = 8;
inline int kRadiusPill = 6;

// Metrics.
inline int kTopBarHeight = 54;
inline int kStatusHeight = 32;
inline int kSidebarWidth = 256;
inline int kNavRowHeight = 40;
inline int kRowHeight = 40;
inline int kTableHeaderHeight = 40;

// Rescales every metric from its 96-DPI design value to the given DPI.
inline void init(int dpiValue) {
    dpi::setDpi(dpiValue);
    kSpaceXS = dpi::scale(4);
    kSpaceS = dpi::scale(8);
    kSpaceM = dpi::scale(12);
    kSpaceL = dpi::scale(16);
    kSpaceXL = dpi::scale(24);
    kRadiusControl = dpi::scale(4);
    kRadiusCard = dpi::scale(8);
    kRadiusPill = dpi::scale(6);
    kTopBarHeight = dpi::scale(54);
    kStatusHeight = dpi::scale(32);
    kSidebarWidth = dpi::scale(256);
    kNavRowHeight = dpi::scale(40);
    kRowHeight = dpi::scale(40);
    kTableHeaderHeight = dpi::scale(40);
}

}  // namespace theme

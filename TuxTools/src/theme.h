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
 * The type ramp is the design size; the user may scale it from the settings
 * card and it is applied through font(). Spacing, radii and metrics are
 * DPI-scaled at startup by init() so the UI stays crisp and correctly sized
 * on high-DPI displays (the app is Per-Monitor-V2 DPI aware), and the few
 * metrics that exist only to hold text follow the user's text size as well.
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
// The value table is read at a distance and at length, so its rows are set a
// step larger than the surrounding chrome. The list view otherwise falls back to
// the default system font, which is smaller than anything else on screen.
const int kFontTable = 16;
const int kFontSubtitle = 20;

// The user's text size, as a percentage of the ramp above. 100 is the design
// size and the default; the settings card moves it.
//
// It is deliberately a *type* scale and not a layout scale. Growing every
// metric with the font turns the setting into a zoom: the sidebar alone would
// reach 400 pixels and push the navigation off the window. So the geometry
// stays where it is and only the type and the boxes whose single job is to
// hold type follow along.
inline int g_textScale = 100;

// Bumped by every change. Anything caching a font watches this and rebuilds,
// which is what makes the setting take effect without a restart.
inline int g_fontGeneration = 0;

inline int textScale() { return g_textScale; }
inline int fontGeneration() { return g_fontGeneration; }

// The range the settings card offers. Below 80 the caption face stops being
// legible; above 160 the table no longer fits the content area.
const int kTextScaleMin = 80;
const int kTextScaleMax = 160;
const int kTextScaleStep = 5;

// A design size at the monitor's DPI, then scaled in full by the user's text
// size. This is the one to reach for inside a dialog: its content is fixed, so
// the whole card is a zoom of its design and its width, padding and line steps
// have to grow together or the text it holds stops fitting the box.
inline int zoom(int designPx) {
    return MulDiv(dpi::scale(designPx), g_textScale, 100);
}

// A type size in pixels. Every CreateFont in the program goes through this.
inline int font(int designPx) { return zoom(designPx); }

// A box whose job is to hold one line of type: the design box, grown by exactly
// the amount the type inside it grew.
//
// Scaling the whole box by the text ratio instead would grow the padding along
// with the text, so a 100-pixel button would gain 60 pixels of empty space at
// 160% and push its neighbour off the bar. Growing only by the type delta keeps
// the padding the same at every size, which is what makes the setting a text
// size rather than a zoom.
inline int textBox(int designPx, int typePx) {
    return dpi::scale(designPx) + (font(typePx) - dpi::scale(typePx));
}

// The common case: a box around the body face.
inline int metric(int designPx) { return textBox(designPx, kFontBody); }

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

// Bars and columns that hold a line of text. They follow the text size, but
// only by the type delta - see textBox - so they stay the same thickness as the
// text inside them rather than inflating.
inline int kTopBarHeight = 54;
inline int kStatusHeight = 32;
inline int kSidebarWidth = 256;

// Metrics whose only job is to fit a line of text: they follow the text size
// too, or a larger font would clip inside its own row.
inline int kNavRowHeight = 40;
inline int kRowHeight = 40;
inline int kTableHeaderHeight = 40;

// The design values the text-sized metrics grow from. A row is designed to
// carry a single line with this much breathing room around it, so the extra
// height a larger font needs is the difference between the two type sizes and
// nothing more.
const int kNavRowDesign = 40;
const int kTableHeaderDesign = 40;
const int kRowDesign = 40;

// Recomputes every metric. Called from init() and whenever the text size
// changes, so the two never drift apart.
inline void refreshMetrics() {
    kSpaceXS = dpi::scale(4);
    kSpaceS = dpi::scale(8);
    kSpaceM = dpi::scale(12);
    kSpaceL = dpi::scale(16);
    kSpaceXL = dpi::scale(24);
    kRadiusControl = dpi::scale(4);
    kRadiusCard = dpi::scale(8);
    kRadiusPill = dpi::scale(6);
    // The top bar carries the product name (18px) and the status bar and
    // sidebar carry the body face.
    kTopBarHeight = textBox(54, 18);
    kStatusHeight = textBox(32, kFontBody);
    kSidebarWidth = textBox(256, kFontBody);

    // A row is the type height plus a constant pad, so it tracks the type.
    kNavRowHeight = textBox(kNavRowDesign, kFontBody);
    kRowHeight = textBox(kRowDesign, kFontTable);
    kTableHeaderHeight = textBox(kTableHeaderDesign, kFontTable);
}

inline void setTextScale(int percent) {
    if (percent < kTextScaleMin) percent = kTextScaleMin;
    if (percent > kTextScaleMax) percent = kTextScaleMax;
    if (percent == g_textScale) return;
    g_textScale = percent;
    ++g_fontGeneration;
    refreshMetrics();
}

// Rescales every metric from its 96-DPI design value to the given DPI.
//
// A move to a different monitor changes every pixel size, so anything holding a
// font built for the previous DPI has to let go of it: the generation is bumped
// as well, which is what tells the font caches their entries are stale.
inline void init(int dpiValue) {
    if (dpiValue > 0 && dpiValue != dpi::getDpi()) ++g_fontGeneration;
    dpi::setDpi(dpiValue);
    refreshMetrics();
}

}  // namespace theme

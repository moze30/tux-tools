/*
 * Tux-Tools - persisted user preferences.
 *
 * Right now there is one: the text size. It lives here rather than in app_log
 * because it is a display preference, not a debugging one, even though the two
 * share the same INI file beside the executable.
 *
 * The type scale itself is owned by theme; this module is the part that
 * survives a restart.
 */

#pragma once

#include <windows.h>

namespace settings {

// Reads the saved preferences and applies them. Call once, before the window is
// created, so the first paint is already drawn at the size the user chose.
void init();

// The current text size, as a percentage of the design ramp.
int textScale();

// Applies a new text size and saves it. Clamped to the range theme offers.
void setTextScale(int percent);

// Back to the design size.
void resetTextScale();

}  // namespace settings

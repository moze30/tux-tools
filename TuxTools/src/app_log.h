/*
 * Tux-Tools - optional debug log.
 *
 * When the debug option is on, everything the program does worth recording
 * goes to Tux-Tools.log next to the executable: startup, every tool switch,
 * every attempt to create a device or a context, every failure, and the
 * messages the decoder cores already emit. The file is flushed as each line is
 * written, because the thing this exists to catch is a crash - if the log were
 * buffered, the last and most interesting lines would be exactly the ones lost.
 *
 * The setting is kept in Tux-Tools.ini beside the executable, so it survives a
 * restart. Nothing is written when the option is off, including no INI file
 * unless the user actually changes something.
 */

#pragma once

#include <windows.h>

#include <string>

namespace applog {

// Reads the saved setting and, if it is on, opens the log. Call once, early.
// Safe to call when logging ends up disabled: nothing is opened.
void init();
void shutdown();

bool enabled();

// Turns logging on or off, opens or closes the file, and saves the choice.
void setEnabled(bool value);

// Path of the log file, whether or not it is currently open.
const std::wstring& path();

// One timestamped line. Adds the newline itself.
void write(const wchar_t* format, ...);

// A line describing an HRESULT: its name where one is known, its number, and
// the system message when there is one. Used on every failure path, so what
// lands in the log is the same shape throughout.
void writeHresult(const wchar_t* what, long hr);

// The same for a Vulkan VkResult. These are small negative numbers, so the
// name carries essentially all of the meaning: "-9" says nothing, while
// VK_ERROR_INCOMPATIBLE_DRIVER says the environment cannot do it at all.
void writeVkResult(const wchar_t* what, int result);

// Text that already carries its own line breaks, as the decoder cores send it.
// Used as the sink for their log calls so their messages land in this file too.
void writeRaw(const wchar_t* text);

// Records the fact that the process is ending normally, so a log that stops
// mid-sentence is recognisable as a crash rather than a normal exit.
// Writes a block of machine facts - Vulkan loader, ICDs, graphics DLL versions,
// whether this is Wine and with which settings - so a single log from a
// failing machine answers questions that would otherwise need several shell
// commands there.
void writeEnvironmentSnapshot();

// Installs a filter that records an unhandled exception before the process dies.
//
// The reason this matters is the log itself: a crash takes the process with it,
// and without the filter the file simply stops, leaving the reader to guess
// whether the last line was reached or the next one killed it. With it, the
// exception code is written first, so "access violation while doing X" replaces
// "the log ends after X".
void installCrashHandler();

void writeShutdown();

}  // namespace applog

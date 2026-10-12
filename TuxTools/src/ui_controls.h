/*
 * Tux-Tools - native Win32 edition
 *
 * Lightweight custom-drawn controls for the light Docker-style theme:
 *  - a navigation-style section list (rounded hover / selected states);
 *  - a report-mode value list with a light header and row separators;
 *  - a search box (magnifier + placeholder) fully drawn by us.
 */

#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace ui {

extern const wchar_t* const kSectionListClass;
extern const wchar_t* const kSearchBoxClass;

void registerControlClasses(HINSTANCE instance);

HWND createSearchBox(HWND parent, HINSTANCE instance, int id);
void searchBoxGetText(HWND hwnd, std::wstring& out);
void searchBoxSetText(HWND hwnd, const std::wstring& text);

HWND createValueList(HWND parent, HINSTANCE instance, int id);

// The app's interface face at a design size, scaled for the monitor and for the
// user's text-size setting. Shared with the shell so the custom-drawn chrome and
// the list view can never disagree about which font is in use.
HFONT font(int designPx, bool semibold = false);

// Re-applies the interface face to the value list after the text size changed.
// The list view derives its row height from its own font, so it has to be told
// again or the rows keep the size they were created with.
void valueListApplyFont(HWND valueList);

// Throws away every cached face belonging to an older text size, keeping the
// ones the current size is using. Called after the text size changes.
void resetFontCache();

void sectionListSetItems(HWND hwnd, const std::vector<std::wstring>& titles,
                         const std::vector<int>& counts);
int sectionListGetSelection(HWND hwnd);
void sectionListSetSelection(HWND hwnd, int index);

struct ValueRow {
    std::wstring name;
    std::wstring value;
};

void valueListSetRows(HWND hwnd, const std::vector<ValueRow>& rows);
void valueListResizeColumns(HWND hwnd);

// Paints one row of the value table.
//
// A control sends its custom draw notification to its parent, not to itself, so
// the list view's rows cannot be painted from its own subclass: that subclass
// only ever sees the header, which is a child of the list. The parent window -
// the main window - is where the notification actually arrives, and it hands it
// back here. The return value is the one the parent must return from WM_NOTIFY.
LRESULT valueListCustomDraw(HWND valueList, NMHDR* hdr);

}  // namespace ui

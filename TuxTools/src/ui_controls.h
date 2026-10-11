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

}  // namespace ui

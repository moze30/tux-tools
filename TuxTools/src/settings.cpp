/*
 * Tux-Tools - persisted user preferences, implementation.
 */

#include "settings.h"

#include "theme.h"

#include <cstdio>
#include <string>

namespace settings {

namespace {

std::wstring g_iniPath;

// The INI lives beside the executable, with the log, so a portable copy is
// still portable: nothing is written outside the folder the user unzipped.
std::wstring moduleDirectory() {
    wchar_t path[MAX_PATH] = {0};
    const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L".";
    std::wstring full(path, len);
    const size_t slash = full.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring(L".") : full.substr(0, slash);
}

void save() {
    wchar_t value[16] = {0};
    std::swprintf(value, 16, L"%d", theme::textScale());
    // The section and key are created on first write, so a user who never
    // touches the setting gets no INI file at all.
    WritePrivateProfileStringW(L"ui", L"textScale", value, g_iniPath.c_str());
}

int clampToRange(int percent) {
    if (percent < theme::kTextScaleMin) return theme::kTextScaleMin;
    if (percent > theme::kTextScaleMax) return theme::kTextScaleMax;
    return percent;
}

}  // namespace

int textScale() {
    return theme::textScale();
}

void setTextScale(int percent) {
    const int wanted = clampToRange(percent);
    theme::setTextScale(wanted);
    save();
}

void resetTextScale() {
    setTextScale(100);
}

void init() {
    g_iniPath = moduleDirectory() + L"\\Tux-Tools.ini";

    // A stored value outside the range is clamped rather than rejected, so a
    // hand-edited INI can never produce an unusable window.
    const int saved = static_cast<int>(
        GetPrivateProfileIntW(L"ui", L"textScale", 100, g_iniPath.c_str()));
    theme::setTextScale(clampToRange(saved));
}

}  // namespace settings

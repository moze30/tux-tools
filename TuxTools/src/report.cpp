/*
 * Tux-Tools - native Win32 edition
 *
 * Shared report model helpers.
 */

#include "report.h"

#include <windows.h>

#include <sstream>

namespace viewer {

std::wstring toWide(const char* utf8) {
    if (!utf8) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out.data(), len);
    return out;
}

std::wstring toWide(const std::string& utf8) {
    return toWide(utf8.c_str());
}

std::wstring formatInt(long long value) {
    return std::to_wstring(value);
}

std::wstring formatUInt(unsigned long long value) {
    return std::to_wstring(value);
}

std::wstring formatHex(unsigned long long value) {
    std::wstringstream ss;
    ss << L"0x" << std::hex << value;
    return ss.str();
}

std::wstring formatVersion(unsigned int variant, unsigned int major,
                           unsigned int minor, unsigned int patch) {
    std::wstringstream ss;
    ss << variant << L"." << major << L"." << minor << L"." << patch;
    return ss.str();
}

}  // namespace viewer

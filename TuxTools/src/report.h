/*
 * Tux-Tools - native Win32 edition
 *
 * Shared report model used by the OpenGL and Vulkan collectors.
 * A report is a description plus a list of sections holding name/value fields.
 */

#pragma once

#include <string>
#include <vector>

namespace viewer {

struct Field {
    std::wstring name;
    std::wstring value;
};

struct Section {
    std::wstring title;
    std::vector<Field> fields;
};

struct Report {
    std::wstring description;
    std::vector<Section> sections;

    Section& addSection(const std::wstring& title) {
        sections.push_back(Section());
        sections.back().title = title;
        return sections.back();
    }

    void add(const std::wstring& name, const std::wstring& value) {
        sections.back().fields.push_back(Field{name, value});
    }
};

std::wstring toWide(const char* utf8);
std::wstring toWide(const std::string& utf8);
std::wstring formatInt(long long value);
std::wstring formatUInt(unsigned long long value);
std::wstring formatHex(unsigned long long value);
std::wstring formatVersion(unsigned int variant, unsigned int major,
                           unsigned int minor, unsigned int patch);

}  // namespace viewer

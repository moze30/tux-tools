/*
 * Tux-Tools - interface language.
 *
 * The program is written in Chinese, so the Chinese text is the source and the
 * key: tr(L"...") looks up the literal it is given and returns the English text
 * when the active language is English. A language that has no entry for a
 * string falls back to the Chinese original, which means a partial translation
 * is always usable and never leaves a blank label on screen.
 *
 * The active language is picked from the environment at startup - the user's
 * Windows UI language - and can be overridden from the settings card, where the
 * choice is saved beside the other preferences.
 *
 * tr() returns a pointer that stays valid until the next call. That is what lets
 * it be dropped into the Win32 APIs that want an LPWSTR without a copy: the
 * table's own strings live for the life of the process, and the fallback is the
 * caller's own literal.
 */

#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace i18n {

// The languages the settings card offers, in the order it shows them.
enum class Language {
    Chinese = 0,
    English,
};

struct LanguageInfo {
    Language language;
    const wchar_t* name;  // shown in the settings card, in its own language
};

// How many languages there are. A constant rather than languages().size() so a
// caller can check its own tables against it at compile time.
const int kLanguageCount = 2;

// Every switchable language, in the order the settings card shows them.
const std::vector<LanguageInfo>& languages();

// The language in use. Always concrete: on first run it is whatever the
// environment is, and after that it is the saved choice.
Language language();

// Sets the language and persists it.
void setLanguage(Language value);

// What the settings card marks as selected. The same as language() - the card
// highlights the language in effect - and named separately so a reader does not
// have to work out that the two cannot differ.
Language selection();

// The environment's language, resolved without regard to the saved setting.
Language detect();

std::string code();  // "zh-CN" / "en-US", for the log

// Applies the value read back from the INI. Called by settings::init, before any
// window exists, so the first paint is already in the right language.
void loadSaved(const std::wstring& saved);

// Looks up a Chinese string. Returns the translation when one exists, otherwise
// the argument itself. Valid until the next call.
const wchar_t* tr(const wchar_t* zh);

}  // namespace i18n

/*
 * Tux-Tools - optional debug log, implementation.
 */

#include "app_log.h"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include <winver.h>

namespace applog {

namespace {

FILE* g_file = nullptr;
bool g_enabled = false;
std::wstring g_path;
std::wstring g_iniPath;
CRITICAL_SECTION g_lock;
bool g_lockReady = false;

// The log lives beside the executable, which is where a user running a
// portable tool expects to find both it and the INI that controls it.
std::wstring moduleDirectory() {
    wchar_t path[MAX_PATH] = {0};
    const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L".";
    std::wstring full(path, len);
    const size_t slash = full.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring(L".") : full.substr(0, slash);
}

std::wstring readIniValue(const wchar_t* key) {
    wchar_t buffer[64] = {0};
    GetPrivateProfileStringW(L"debug", key, L"", buffer, 64, g_iniPath.c_str());
    return buffer;
}

void openFile() {
    if (g_file) return;
    // "a" rather than "w": consecutive runs append, so a crash and the run that
    // follows it are both in the same file and can be compared.
    g_file = _wfopen(g_path.c_str(), L"a, ccs=UTF-8");
    if (!g_file) {
        // Fall back to a plain byte stream if the wide mode is unavailable.
        g_file = _wfopen(g_path.c_str(), L"a");
    }
}

void closeFile() {
    if (!g_file) return;
    fflush(g_file);
    fclose(g_file);
    g_file = nullptr;
}

std::wstring timestamp() {
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t buf[48];
    std::swprintf(buf, 48, L"%04d-%02d-%02d %02d:%02d:%02d.%03d", st.wYear, st.wMonth,
                  st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}

// The decoder cores send CRLF-terminated chunks; the log wants them on their own
// lines with the same prefix as everything else, so each is broken out and
// written separately.
void writeChunkLocked(const std::wstring& text) {
    if (!g_file) return;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        std::wstring line = (end == std::wstring::npos) ? text.substr(start)
                                                        : text.substr(start, end - start);
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) {
            line.pop_back();
        }
        if (!line.empty()) {
            const std::wstring stamp = timestamp();
            std::fwprintf(g_file, L"[%ls] %ls\n", stamp.c_str(), line.c_str());
            // Every line is flushed. This is the whole point of the file.
            fflush(g_file);
        }
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
}

}  // namespace

const std::wstring& path() {
    return g_path;
}

bool enabled() {
    return g_enabled;
}

void init() {
    InitializeCriticalSection(&g_lock);
    g_lockReady = true;

    const std::wstring dir = moduleDirectory();
    g_path = dir + L"\\Tux-Tools.log";
    g_iniPath = dir + L"\\Tux-Tools.ini";

    const std::wstring value = readIniValue(L"log");
    if (value == L"1") {
        g_enabled = true;
        openFile();
    }
}

void shutdown() {
    if (!g_lockReady) return;
    // No EnterCriticalSection here: shutdown runs on the UI thread after the
    // loop has ended and nothing else writes.
    writeChunkLocked(L"===== 程序正常退出 =====");
    closeFile();
    DeleteCriticalSection(&g_lock);
    g_lockReady = false;
}

void setEnabled(bool value) {
    if (!g_lockReady) return;
    EnterCriticalSection(&g_lock);
    if (value == g_enabled) {
        LeaveCriticalSection(&g_lock);
        return;
    }
    g_enabled = value;
    if (value) {
        openFile();
        writeChunkLocked(L"===== 调试日志已启用 =====");
    } else {
        writeChunkLocked(L"===== 调试日志已关闭 =====");
        closeFile();
    }
    LeaveCriticalSection(&g_lock);

    // Remember the choice. The section and key are created on first write.
    WritePrivateProfileStringW(L"debug", L"log", value ? L"1" : L"0",
                               g_iniPath.c_str());
}

void write(const wchar_t* format, ...) {
    if (!g_enabled || !g_lockReady) return;
    EnterCriticalSection(&g_lock);
    if (g_file) {
        wchar_t buffer[2048];
        va_list args;
        va_start(args, format);
        _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
        va_end(args);
        writeChunkLocked(buffer);
    }
    LeaveCriticalSection(&g_lock);
}

void writeRaw(const wchar_t* text) {
    if (!g_enabled || !g_lockReady || !text) return;
    EnterCriticalSection(&g_lock);
    writeChunkLocked(text);
    LeaveCriticalSection(&g_lock);
}

void writeHresult(const wchar_t* what, long hr) {
    if (!g_enabled) return;

    // The name matters more than the number when reading back: 0x887A0004 is
    // not memorable, DXGI_ERROR_UNSUPPORTED is.
    const wchar_t* name = nullptr;
    switch (static_cast<unsigned long>(hr)) {
        case 0x887A0001u: name = L"DXGI_ERROR_INVALID_CALL"; break;
        case 0x887A0002u: name = L"DXGI_ERROR_NOT_FOUND"; break;
        case 0x887A0004u: name = L"DXGI_ERROR_UNSUPPORTED"; break;
        case 0x887A0005u: name = L"DXGI_ERROR_DEVICE_REMOVED"; break;
        case 0x887A0006u: name = L"DXGI_ERROR_DEVICE_HUNG"; break;
        case 0x887A0007u: name = L"DXGI_ERROR_DEVICE_RESET"; break;
        case 0x887A0020u: name = L"DXGI_ERROR_DRIVER_INTERNAL_ERROR"; break;
        // Direct3D 9's own codes. They are the ones that show up on the Wine
        // side, where wined3d reports the reason as a D3DERR_ value.
        case 0x8876086Au: name = L"D3DERR_NOTAVAILABLE"; break;
        case 0x8876086Cu: name = L"D3DERR_INVALIDCALL"; break;
        case 0x88760868u: name = L"D3DERR_DEVICELOST"; break;
        case 0x88760869u: name = L"D3DERR_DEVICENOTRESET"; break;
        case 0x88760870u: name = L"D3DERR_DRIVERINTERNALERROR"; break;
        case 0x88760874u: name = L"D3DERR_OUTOFVIDEOMEMORY"; break;
        case 0x8876017Cu: name = L"D3DERR_CONFLICTINGRENDERSTATE"; break;
        case 0x88760066u: name = L"D3DERR_NOTFOUND"; break;
        case 0x80070005u: name = L"E_ACCESSDENIED"; break;
        case 0x80070057u: name = L"E_INVALIDARG"; break;
        case 0x80004005u: name = L"E_FAIL"; break;
        case 0x80004001u: name = L"E_NOTIMPL"; break;
        case 0x8007000Eu: name = L"E_OUTOFMEMORY"; break;
        case 0x800401F0u: name = L"CO_E_NOTINITIALIZED"; break;
        case 0x8007007Eu: name = L"ERROR_MOD_NOT_FOUND"; break;
        default: break;
    }

    // MessageText from FormatMessage, which resolves the system's own wording
    // and is the only part that works for codes not listed above.
    wchar_t* sys = nullptr;
    const DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(hr),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<wchar_t*>(&sys), 0,
        nullptr);
    std::wstring message;
    if (len && sys) {
        message.assign(sys, len);
        while (!message.empty() &&
               (message.back() == L'\r' || message.back() == L'\n' ||
                message.back() == L' ')) {
            message.pop_back();
        }
    }
    if (sys) LocalFree(sys);

    if (name) {
        write(L"%ls 失败：%ls (0x%08lX)", what, name,
              static_cast<unsigned long>(hr));
    } else {
        write(L"%ls 失败：0x%08lX", what, static_cast<unsigned long>(hr));
    }
    if (!message.empty()) write(L"          系统消息：%ls", message.c_str());
}

void writeVkResult(const wchar_t* what, int result) {
    if (!g_enabled) return;
    const wchar_t* name = nullptr;
    switch (result) {
        case 0: name = L"VK_SUCCESS"; break;
        case 1: name = L"VK_NOT_READY"; break;
        case 2: name = L"VK_TIMEOUT"; break;
        case 5: name = L"VK_INCOMPLETE"; break;
        case -1: name = L"VK_ERROR_OUT_OF_HOST_MEMORY"; break;
        case -2: name = L"VK_ERROR_OUT_OF_DEVICE_MEMORY"; break;
        case -3: name = L"VK_ERROR_INITIALIZATION_FAILED"; break;
        case -4: name = L"VK_ERROR_DEVICE_LOST"; break;
        case -5: name = L"VK_ERROR_MEMORY_MAP_FAILED"; break;
        case -6: name = L"VK_ERROR_LAYER_NOT_PRESENT"; break;
        case -7: name = L"VK_ERROR_EXTENSION_NOT_PRESENT"; break;
        case -8: name = L"VK_ERROR_FEATURE_NOT_PRESENT"; break;
        case -9: name = L"VK_ERROR_INCOMPATIBLE_DRIVER"; break;
        case -10: name = L"VK_ERROR_TOO_MANY_OBJECTS"; break;
        case -11: name = L"VK_ERROR_FORMAT_NOT_SUPPORTED"; break;
        case -12: name = L"VK_ERROR_FRAGMENTED_POOL"; break;
        case -1000000000: name = L"VK_ERROR_SURFACE_LOST_KHR"; break;
        case -1000000001: name = L"VK_ERROR_NATIVE_WINDOW_IN_USE_KHR"; break;
        case -1000003001: name = L"VK_ERROR_OUT_OF_DATE_KHR"; break;
        case -1000001004: name = L"VK_ERROR_INCOMPATIBLE_DISPLAY_KHR"; break;
        default: break;
    }
    // The wording follows the result: this is used for probes that are expected
    // to succeed as well as for failures, and "VK_SUCCESS failed" reads absurdly.
    const wchar_t* verb = (result == 0) ? L"成功" : L"失败";
    if (name) {
        write(L"%ls %ls：%ls (VkResult %d)", what, verb, name, result);
    } else {
        write(L"%ls %ls：VkResult %d", what, verb, result);
    }
}

// --- environment snapshot ----------------------------------------------------

// A block of facts about the machine that would otherwise take a separate shell
// command each. Everything here is read through documented APIs or the registry,
// so it works the same on Windows and under Wine, and it is written once at
// startup when logging is on.
void writeEnvironmentSnapshot() {
    if (!g_enabled) return;

    write(L"---- 运行环境 ----");

    // Vulkan loader, if there is one, and where it came from.
    {
        HMODULE vk = LoadLibraryW(L"vulkan-1.dll");
        if (!vk) {
            write(L"vulkan-1.dll：未找到");
        } else {
            wchar_t path[MAX_PATH] = {0};
            if (GetModuleFileNameW(vk, path, MAX_PATH)) {
                write(L"vulkan-1.dll：%ls", path);
                DWORD dummy = 0;
                const DWORD size = GetFileVersionInfoSizeW(path, &dummy);
                if (size > 0) {
                    std::vector<BYTE> data(size);
                    if (GetFileVersionInfoW(path, 0, size, data.data())) {
                        VS_FIXEDFILEINFO* info = nullptr;
                        UINT len = 0;
                        if (VerQueryValueW(data.data(), L"\\",
                                           reinterpret_cast<void**>(&info), &len) &&
                            info) {
                            write(L"  loader 文件版本：%u.%u.%u.%u",
                                  HIWORD(info->dwFileVersionMS),
                                  LOWORD(info->dwFileVersionMS),
                                  HIWORD(info->dwFileVersionLS),
                                  LOWORD(info->dwFileVersionLS));
                        }
                    }
                }
            }
            FreeLibrary(vk);
        }
    }

    // Which ICDs the loader has been told about. Under Wine this is the most
    // direct statement of whether a real Vulkan driver is installed.
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\Vulkan\\Drivers",
                          0, KEY_READ, &key) == ERROR_SUCCESS) {
            wchar_t name[512] = {0};
            DWORD nameLen = 512;
            DWORD index = 0;
            int found = 0;
            while (RegEnumValueW(key, index++, name, &nameLen, nullptr, nullptr, nullptr,
                                 nullptr) == ERROR_SUCCESS) {
                write(L"Vulkan ICD：%ls", name);
                ++found;
                nameLen = 512;
            }
            if (found == 0) write(L"Vulkan ICD：注册表项为空");
            RegCloseKey(key);
        } else {
            write(L"Vulkan ICD：注册表中没有 HKLM\\SOFTWARE\\Khronos\\Vulkan\\Drivers");
        }
    }

    // The graphics DLLs the test relies on, with their versions. A missing one
    // or an unexpected version explains a lot of otherwise puzzling results.
    {
        const wchar_t* dlls[] = {L"vulkan-1.dll", L"opengl32.dll", L"d3d9.dll",
                                 L"d3d11.dll", L"d3d12.dll", L"dxgi.dll",
                                 L"d3dcompiler_47.dll", L"mfplat.dll"};
        for (const wchar_t* name : dlls) {
            HMODULE mod = LoadLibraryW(name);
            if (!mod) {
                write(L"  缺少 %ls", name);
                continue;
            }
            wchar_t path[MAX_PATH] = {0};
            GetModuleFileNameW(mod, path, MAX_PATH);
            wchar_t version[64] = L"?";
            DWORD dummy = 0;
            const DWORD size = GetFileVersionInfoSizeW(path, &dummy);
            if (size > 0) {
                std::vector<BYTE> data(size);
                if (GetFileVersionInfoW(path, 0, size, data.data())) {
                    VS_FIXEDFILEINFO* info = nullptr;
                    UINT len = 0;
                    if (VerQueryValueW(data.data(), L"\\",
                                       reinterpret_cast<void**>(&info), &len) && info) {
                        std::swprintf(version, 64, L"%u.%u.%u.%u",
                                      HIWORD(info->dwFileVersionMS),
                                      LOWORD(info->dwFileVersionMS),
                                      HIWORD(info->dwFileVersionLS),
                                      LOWORD(info->dwFileVersionLS));
                    }
                }
            }
            write(L"  %ls = %ls", name, version);
            FreeLibrary(mod);
        }
    }

    // Whether the process is running under Wine, and a few Wine settings that
    // change graphics behaviour.
    {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        const bool wine = ntdll && GetProcAddress(ntdll, "wine_get_version") != nullptr;
        write(L"运行在 Wine 下：%ls", wine ? L"是" : L"否");
        if (wine) {
            typedef const char*(CDECL * WineVersionFn)();
            auto fn = reinterpret_cast<WineVersionFn>(
                reinterpret_cast<void*>(GetProcAddress(ntdll, "wine_get_version")));
            if (fn) {
                const char* v = fn();
                if (v) write(L"Wine 版本：%hs", v);
            }
            const wchar_t* vars[] = {L"WINEDLLOVERRIDES", L"WINEESYNC", L"WINEFSYNC",
                                     L"DXVK_HUD", L"VK_ICD_FILENAMES", L"MESA_LOADER_DRIVER_OVERRIDE"};
            for (const wchar_t* v : vars) {
                wchar_t buf[512] = {0};
                if (GetEnvironmentVariableW(v, buf, 512) > 0) {
                    write(L"  环境变量 %ls=%ls", v, buf);
                }
            }
        }
    }

    write(L"---- 环境结束 ----");
}

// --- crash reporting ---------------------------------------------------------

namespace {

// A short name for the exception codes that actually turn up here.
const wchar_t* exceptionName(DWORD code) {
    switch (code) {
        case 0xC0000005: return L"访问冲突（读写无效地址）";
        case 0xC0000006: return L"页面错误（无法读入页面）";
        case 0xC000001D: return L"非法指令（可能是不支持的 CPU 指令）";
        case 0xC0000025: return L"不可继续的异常";
        case 0xC000008C: return L"数组越界";
        case 0xC0000094: return L"整数除零";
        case 0xC0000095: return L"整数溢出";
        case 0xC0000096: return L"特权指令";
        case 0xC00000FD: return L"栈溢出";
        case 0xC0000374: return L"堆损坏";
        case 0xC0000417: return L"无效参数（CRT 检测到）";
        case 0xC0000602: return L"安全 cookie 校验失败（栈被破坏）";
        case 0xE06D7363: return L"未捕获的 C++ 异常";
        default: return nullptr;
    }
}

LONG WINAPI crashFilter(EXCEPTION_POINTERS* info) {
    if (!g_enabled) return EXCEPTION_CONTINUE_SEARCH;

    const DWORD code = info && info->ExceptionRecord
                           ? info->ExceptionRecord->ExceptionCode
                           : 0;
    void* address = (info && info->ExceptionRecord)
                        ? info->ExceptionRecord->ExceptionAddress
                        : nullptr;

    writeRaw(L"");
    writeRaw(L"=========== 程序崩溃了 ===========");
    const wchar_t* name = exceptionName(code);
    if (name) {
        write(L"异常：%ls (0x%08lX)", name, static_cast<unsigned long>(code));
    } else {
        write(L"异常代码：0x%08lX", static_cast<unsigned long>(code));
    }
    write(L"出错地址：%p", address);

    // The log's own last line is the useful part: it is the last thing the
    // program said it was doing. Pointing at it here saves the reader from
    // having to work out which of several similar lines came last.
    write(L"上面最后一条普通日志就是崩溃前正在做的事。");

    // The faulting module, when the address falls inside one, which is the
    // difference between a bug in this program and one in a driver.
    HMODULE module = nullptr;
    if (address && GetModuleHandleExW(
                       GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(address), &module) && module) {
        wchar_t path[MAX_PATH] = {0};
        if (GetModuleFileNameW(module, path, MAX_PATH)) {
            write(L"出错模块：%ls", path);
        }
    }
    writeRaw(L"==================================");

    if (g_file) std::fflush(g_file);
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void installCrashHandler() {
    SetUnhandledExceptionFilter(crashFilter);
}

void writeShutdown() {
    if (!g_enabled) return;
    writeChunkLocked(L"===== 程序正常退出 =====");
}

}  // namespace applog

/*
 * tuxdrive - headless-ish driver for Tux-Tools UI testing.
 *
 * Lives in _diag/ and is NOT part of the product. It lets the agent drive the
 * real Tux-Tools window from outside the process, so rendering bugs can be
 * reproduced on this Windows machine without touching product source.
 *
 * It relies on the fact that TuxToolsWindow hit-tests WM_LBUTTONDOWN/UP purely
 * from the lParam coordinates, so synthetic PostMessage clicks work and the
 * user's physical mouse is never hijacked.
 *
 * Modes:
 *   launch                         start the app and wait for its window
 *   find                           print hwnd / geometry / hung state
 *   pshot   <file.png>             capture the app client area from the screen
 *   sshot   <file.png>             capture the whole virtual screen
 *   click   <x> <y>                synthetic click at client coords (no mouse)
 *   rclick  <x> <y>                real mouse click at client coords
 *   key     <vk> [vk] [vk] ...     real key presses (for native popup menus)
 *   hash    <n> <interval_ms>      capture n times, print a pixel hash each
 *   quit                           ask the app to close
 */

#include <windows.h>
#include <gdiplus.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <string>

namespace {

const wchar_t kAppClass[] = L"TuxToolsWindow";

// Path to the built Tux-Tools.exe. Resolved at run time, in this order:
//
//   1. TUXTOOLS_EXE environment variable, if set;
//   2. TuxTools/build-win/Tux-Tools.exe next to this source tree.
//
// The path is deliberately not hardcoded: this file is checked in, and a
// machine-specific absolute path would be useless to anyone else.
const wchar_t* findAppExe() {
    static wchar_t buf[MAX_PATH * 2] = {0};
    if (buf[0]) return buf;

    DWORD n = GetEnvironmentVariableW(L"TUXTOOLS_EXE", buf, MAX_PATH * 2);
    if (n > 0 && n < MAX_PATH * 2) return buf;

    // Feed the exe name back to GetModuleFileNameW while it is still stored,
    // because that call would overwrite the buffer we are reading from.
    wchar_t exe[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t dir[MAX_PATH] = {0};
    wcsncpy_s(dir, MAX_PATH, exe, _TRUNCATE);
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (slash) *slash = 0;
    swprintf_s(buf, MAX_PATH * 2, L"%s\\..\\TuxTools\\build-win\\Tux-Tools.exe", dir);
    return buf;
}

HWND findApp() {
    return FindWindowW(kAppClass, nullptr);
}

// The app is PerMonitorV2 DPI aware. If we are not, Windows virtualises every
// coordinate we see, which makes screen grabs and clicks land in the wrong
// place. Match the app's awareness level.
void makeDpiAware() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL(WINAPI * SetCtxFn)(HANDLE);
    SetCtxFn setCtx = reinterpret_cast<SetCtxFn>(
        reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
    if (setCtx && setCtx(reinterpret_cast<HANDLE>(-4))) return;  // PerMonitorV2
    SetProcessDPIAware();
}

int windowDpi(HWND hwnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    typedef UINT(WINAPI * GetDpiFn)(HWND);
    GetDpiFn fn = reinterpret_cast<GetDpiFn>(
        reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")));
    return fn ? static_cast<int>(fn(hwnd)) : 96;
}

// --- PNG encoding (GDI+) ----------------------------------------------------

int getEncoderClsid(const WCHAR* mime, CLSID* clsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    Gdiplus::ImageCodecInfo* info =
        static_cast<Gdiplus::ImageCodecInfo*>(malloc(size));
    if (!info) return -1;
    Gdiplus::GetImageEncoders(num, size, info);
    int found = -1;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(info[i].MimeType, mime) == 0) {
            *clsid = info[i].Clsid;
            found = static_cast<int>(i);
            break;
        }
    }
    free(info);
    return found;
}

bool savePng(const wchar_t* path, HBITMAP hbmp) {
    CLSID clsid;
    if (getEncoderClsid(L"image/png", &clsid) < 0) return false;
    Gdiplus::Bitmap bmp(hbmp, nullptr);
    return bmp.Save(path, &clsid, nullptr) == Gdiplus::Ok;
}

// Grabs a window's contents via WM_PRINT/PrintWindow. Unlike BitBlt from the
// screen this works while the window is behind another one, so verification
// never has to raise the app or take the foreground.
HBITMAP grabWindow(HWND hwnd, int w, int h, bool fullContent) {
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    BOOL ok = PrintWindow(hwnd, mem, fullContent ? PW_RENDERFULLCONTENT : 0);
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    if (!ok) { DeleteObject(bmp); return nullptr; }
    return bmp;
}

// Grabs a screen rectangle into a fresh HBITMAP (caller deletes it).
HBITMAP grabScreen(int x, int y, int w, int h) {
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return bmp;
}

// Client (0,0) in screen coordinates.
POINT clientOrigin(HWND hwnd) {
    POINT pt = {0, 0};
    ClientToScreen(hwnd, &pt);
    return pt;
}

bool clientBox(HWND hwnd, int* x, int* y, int* w, int* h) {
    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return false;
    POINT org = clientOrigin(hwnd);
    *x = org.x;
    *y = org.y;
    *w = rc.right - rc.left;
    *h = rc.bottom - rc.top;
    return *w > 0 && *h > 0;
}

// FNV-1a over the raw pixels, so animation shows up as a changing hash.
uint64_t hashBitmap(HBITMAP hbmp, int w, int h) {
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    size_t bytes = static_cast<size_t>(w) * h * 4;
    unsigned char* buf = static_cast<unsigned char*>(malloc(bytes));
    if (!buf) return 0;

    HDC dc = GetDC(nullptr);
    int lines = GetDIBits(dc, hbmp, 0, h, buf, &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (lines == 0) { free(buf); return 0; }

    uint64_t hval = 1469598103934665603ULL;
    for (size_t i = 0; i < bytes; ++i) {
        hval ^= buf[i];
        hval *= 1099511628211ULL;
    }
    free(buf);
    return hval;
}

void bringToFront(HWND hwnd) {
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    ShowWindow(hwnd, SW_SHOW);
    // A separate process cannot steal the foreground, but it *can* raise the
    // window above everything temporarily.
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    Sleep(350);
}

void dropTopmost(HWND hwnd) {
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

bool isHung(HWND hwnd) {
    DWORD_PTR res = 0;
    LRESULT r = SendMessageTimeoutW(hwnd, WM_NULL, 0, 0,
                                    SMTO_ABORTIFHUNG | SMTO_BLOCK, 1200, &res);
    return r == 0;
}

// --- input ------------------------------------------------------------------

void postClick(HWND hwnd, int x, int y) {
    LPARAM lp = MAKELPARAM(x, y);
    PostMessageW(hwnd, WM_MOUSEMOVE, 0, lp);
    Sleep(30);
    PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
    Sleep(60);
    PostMessageW(hwnd, WM_LBUTTONUP, 0, lp);
    Sleep(60);
}

void realClick(HWND hwnd, int x, int y) {
    POINT org = clientOrigin(hwnd);
    SetCursorPos(org.x + x, org.y + y);
    Sleep(120);
    INPUT in[2] = {};
    in[0].type = INPUT_MOUSE;
    in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].type = INPUT_MOUSE;
    in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, in, sizeof(INPUT));
    Sleep(120);
}

void sendKey(WORD vk) {
    INPUT in[2] = {};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
    Sleep(160);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: tuxdrive <mode> [args]\n");
        return 2;
    }
    std::string mode = argv[1];

    makeDpiAware();

    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartup(&token, &gsi, nullptr);

    int rc = 0;

    if (mode == "launch") {
        const wchar_t* appExe = findAppExe();
        std::wstring cmd = appExe;
        std::wstring dir = cmd.substr(0, cmd.find_last_of(L'\\'));
        STARTUPINFOW si = {sizeof(si)};
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(appExe, nullptr, nullptr, nullptr, FALSE, 0,
                            nullptr, dir.c_str(), &si, &pi)) {
            printf("FAILED to launch %ls, err=%lu\n", appExe, GetLastError());
            rc = 1;
        } else {
            printf("launched pid=%lu\n", pi.dwProcessId);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            HWND hwnd = nullptr;
            for (int i = 0; i < 60; ++i) {
                Sleep(150);
                hwnd = findApp();
                if (hwnd) break;
            }
            if (!hwnd) {
                printf("FAILED to find window\n");
                rc = 1;
            } else {
                Sleep(400);
                int x, y, w, h;
                clientBox(hwnd, &x, &y, &w, &h);
                printf("hwnd=%p client=%dx%d at screen %d,%d\n", hwnd, w, h, x, y);
                bringToFront(hwnd);
                printf("OK\n");
            }
        }
    } else if (mode == "find") {
        printf("screen=%dx%d dpi=%d\n", GetSystemMetrics(SM_CXSCREEN),
               GetSystemMetrics(SM_CYSCREEN), GetDeviceCaps(GetDC(nullptr), LOGPIXELSX));
        HWND hwnd = findApp();
        if (!hwnd) {
            printf("NOT RUNNING\n");
            rc = 1;
        } else {
            int x, y, w, h;
            clientBox(hwnd, &x, &y, &w, &h);
            printf("hwnd=%p client=%dx%d at screen %d,%d dpi=%d hung=%s\n",
                   hwnd, w, h, x, y, windowDpi(hwnd), isHung(hwnd) ? "YES" : "no");
        }
    } else if (mode == "pshot" && argc >= 3) {
        HWND hwnd = findApp();
        if (!hwnd) { printf("NOT RUNNING\n"); rc = 1; }
        else {
            int x, y, w, h;
            clientBox(hwnd, &x, &y, &w, &h);
            bringToFront(hwnd);
            clientBox(hwnd, &x, &y, &w, &h);
            HBITMAP bmp = grabScreen(x, y, w, h);
            std::wstring path;
            for (const char* p = argv[2]; *p; ++p) path.push_back(*p);
            printf(savePng(path.c_str(), bmp) ? "saved %ls (%dx%d)\n"
                                              : "SAVE FAILED %ls\n",
                   path.c_str(), w, h);
            DeleteObject(bmp);
        }
    } else if (mode == "child") {
        // List the app's child windows and their rects, so child-relative
        // coordinates can be computed instead of guessed at.
        HWND hwnd = findApp();
        if (!hwnd) { printf("NOT RUNNING\n"); rc = 1; }
        else {
            for (HWND c = GetWindow(hwnd, GW_CHILD); c;
                 c = GetWindow(c, GW_HWNDNEXT)) {
                wchar_t cls[128] = {};
                GetClassNameW(c, cls, 128);
                RECT wr;
                GetWindowRect(c, &wr);
                POINT org = clientOrigin(hwnd);
                printf("%ls rect_screen=%ld,%ld,%ld,%ld rel_to_client=%ld,%ld %ldx%ld\n",
                       cls, wr.left, wr.top, wr.right, wr.bottom,
                       wr.left - org.x, wr.top - org.y,
                       wr.right - wr.left, wr.bottom - wr.top);
            }
        }
    } else if (mode == "hshot" && argc >= 3) {
        // Capture the render host child window directly, with no raise and no
        // foreground. Used so the four backends can be verified offscreen.
        HWND hwnd = findApp();
        if (!hwnd) { printf("NOT RUNNING\n"); rc = 1; }
        else {
            HWND host = FindWindowExW(hwnd, nullptr, L"TuxToolsRenderHost", nullptr);
            if (!host) { printf("NO RENDER HOST\n"); rc = 1; }
            else {
                RECT rc2;
                GetClientRect(host, &rc2);
                int w = rc2.right - rc2.left, h = rc2.bottom - rc2.top;
                HBITMAP bmp = grabWindow(host, w, h, true);
                std::wstring path;
                for (const char* p = argv[2]; *p; ++p) path.push_back(*p);
                if (!bmp) { printf("PRINTWINDOW FAILED\n"); rc = 1; }
                else {
                    printf(savePng(path.c_str(), bmp) ? "saved %ls (%dx%d)\n"
                                                      : "SAVE FAILED %ls\n",
                           path.c_str(), w, h);
                    DeleteObject(bmp);
                }
            }
        }
    } else if (mode == "sshot" && argc >= 3) {
        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        HBITMAP bmp = grabScreen(vx, vy, vw, vh);
        std::wstring path;
        for (const char* p = argv[2]; *p; ++p) path.push_back(*p);
        printf(savePng(path.c_str(), bmp) ? "saved %ls (%dx%d)\n"
                                          : "SAVE FAILED %ls\n",
               path.c_str(), vw, vh);
        DeleteObject(bmp);
    } else if (mode == "click" && argc >= 4) {
        HWND hwnd = findApp();
        if (!hwnd) { printf("NOT RUNNING\n"); rc = 1; }
        else {
            int x = atoi(argv[2]), y = atoi(argv[3]);
            postClick(hwnd, x, y);
            printf("posted click at %d,%d\n", x, y);
        }
    } else if (mode == "cclick" && argc >= 5) {
        // Post a click directly to a named child window. The sidebar is a child,
        // so clicking the top-level window never reaches it; this does, and it
        // uses PostMessage, so the physical mouse is untouched.
        HWND hwnd = findApp();
        if (!hwnd) { printf("NOT RUNNING\n"); rc = 1; }
        else {
            std::wstring cls;
            for (const char* p = argv[2]; *p; ++p) cls.push_back(*p);
            HWND child = FindWindowExW(hwnd, nullptr, cls.c_str(), nullptr);
            if (!child) { printf("NO CHILD %ls\n", cls.c_str()); rc = 1; }
            else {
                int x = atoi(argv[3]), y = atoi(argv[4]);
                postClick(child, x, y);
                printf("posted click to child at %d,%d\n", x, y);
            }
        }
    } else if (mode == "rclick" && argc >= 4) {
        HWND hwnd = findApp();
        if (!hwnd) { printf("NOT RUNNING\n"); rc = 1; }
        else {
            int x = atoi(argv[2]), y = atoi(argv[3]);
            realClick(hwnd, x, y);
            printf("real click at %d,%d\n", x, y);
        }
    } else if (mode == "clickid" && argc >= 5) {
        HWND hwnd = findApp();
        if (!hwnd) { printf("NOT RUNNING\n"); rc = 1; }
        else {
            int id = atoi(argv[2]);
            HWND child = GetDlgItem(hwnd, id);
            if (!child) { printf("NO CHILD id=%d\n", id); rc = 1; }
            else {
                int x = atoi(argv[3]), y = atoi(argv[4]);
                postClick(child, x, y);
                printf("posted click to id=%d (%p) at %d,%d\n", id, child, x, y);
            }
        }
    } else if (mode == "front") {
        HWND hwnd = findApp();
        if (!hwnd) { printf("NOT RUNNING\n"); rc = 1; }
        else { bringToFront(hwnd); printf("front ok\n"); }
    } else if (mode == "untop") {
        HWND hwnd = findApp();
        if (hwnd) dropTopmost(hwnd);
        printf("untop done\n");
    } else if (mode == "move" && argc >= 4) {
        // Windows cascades new windows, and a 1140px-tall window on a 1067px
        // work area ends up partly (or entirely) off screen. Park it somewhere
        // known before taking screenshots.
        HWND hwnd = findApp();
        if (!hwnd) { printf("NOT RUNNING\n"); rc = 1; }
        else {
            int x = atoi(argv[2]);
            int y = atoi(argv[3]);
            SetWindowPos(hwnd, nullptr, x, y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            Sleep(250);
            int cx, cy, cw, ch;
            clientBox(hwnd, &cx, &cy, &cw, &ch);
            printf("moved to %d,%d client=%dx%d at screen %d,%d\n", x, y, cw, ch, cx, cy);
        }
    } else if (mode == "menupick" && argc >= 3) {
        // The app's tool switcher uses TrackPopupMenu, so the popup is a real
        // system menu window. Ask it for the target item's exact screen rect
        // instead of guessing at coordinates.
        int want = atoi(argv[2]);
        HWND popup = nullptr;
        for (int i = 0; i < 30; ++i) {  // wait up to ~3s for the menu
            popup = FindWindowW(L"#32768", nullptr);
            if (popup) break;
            Sleep(100);
        }
        if (!popup) {
            printf("NO POPUP MENU OPEN\n");
            rc = 1;
        } else {
            RECT mr = {};
            GetWindowRect(popup, &mr);
            printf("popup at %ld,%ld %ldx%ld\n", mr.left, mr.top,
                   mr.right - mr.left, mr.bottom - mr.top);
            HMENU hm = reinterpret_cast<HMENU>(
                SendMessageW(popup, 0x01E1 /*MN_GETHMENU*/, 0, 0));
            if (!hm) {
                printf("MN_GETHMENU failed\n");
                rc = 1;
            } else {
                int count = GetMenuItemCount(hm);
                printf("items=%d\n", count);
                RECT ir = {};
                if (want < 0 || want >= count ||
                    !GetMenuItemRect(popup, hm, static_cast<UINT>(want), &ir)) {
                    printf("GetMenuItemRect failed\n");
                    rc = 1;
                } else {
                    int cx = (ir.left + ir.right) / 2;
                    int cy = (ir.top + ir.bottom) / 2;
                    printf("item %d rect=%ld,%ld,%ld,%ld -> click %d,%d\n",
                           want, ir.left, ir.top, ir.right, ir.bottom, cx, cy);
                    SetCursorPos(cx, cy);
                    Sleep(200);
                    INPUT in[2] = {};
                    in[0].type = INPUT_MOUSE;
                    in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                    in[1].type = INPUT_MOUSE;
                    in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
                    SendInput(2, in, sizeof(INPUT));
                    Sleep(300);
                    printf("clicked\n");
                }
            }
        }
    } else if (mode == "hashrect" && argc >= 8) {
        // Hash only a sub-rectangle of the client area, so an external OSD
        // drawn along the top edge cannot mask a frozen render.
        HWND hwnd = findApp();
        if (!hwnd) { printf("NOT RUNNING\n"); rc = 1; }
        else {
            int rx = atoi(argv[2]), ry = atoi(argv[3]);
            int rw = atoi(argv[4]), rh = atoi(argv[5]);
            int n = atoi(argv[6]);
            int interval = atoi(argv[7]);
            bringToFront(hwnd);
            int x, y, w, h;
            clientBox(hwnd, &x, &y, &w, &h);
            printf("rect %d,%d %dx%d of client %dx%d\n", rx, ry, rw, rh, w, h);
            for (int i = 0; i < n; ++i) {
                HBITMAP bmp = grabScreen(x + rx, y + ry, rw, rh);
                uint64_t hv = hashBitmap(bmp, rw, rh);
                DeleteObject(bmp);
                printf("sample %d: %016llx\n", i, (unsigned long long)hv);
                fflush(stdout);
                if (i + 1 < n) Sleep(interval);
            }
        }
    } else if (mode == "sclick" && argc >= 4) {
        int sx = atoi(argv[2]), sy = atoi(argv[3]);
        SetCursorPos(sx, sy);
        Sleep(150);
        INPUT in[2] = {};
        in[0].type = INPUT_MOUSE;
        in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        in[1].type = INPUT_MOUSE;
        in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(2, in, sizeof(INPUT));
        Sleep(150);
        printf("screen click at %d,%d\n", sx, sy);
    } else if (mode == "key" && argc >= 3) {
        HWND hwnd = findApp();
        if (hwnd) SetForegroundWindow(hwnd);
        Sleep(200);
        for (int i = 2; i < argc; ++i) {
            sendKey(static_cast<WORD>(strtol(argv[i], nullptr, 0)));
            printf("key %s sent\n", argv[i]);
        }
    } else if (mode == "hash" && argc >= 4) {
        HWND hwnd = findApp();
        if (!hwnd) { printf("NOT RUNNING\n"); rc = 1; }
        else {
            int n = atoi(argv[2]);
            int interval = atoi(argv[3]);
            int x, y, w, h;
            bringToFront(hwnd);
            clientBox(hwnd, &x, &y, &w, &h);
            printf("client %dx%d hung=%s\n", w, h, isHung(hwnd) ? "YES" : "no");
            for (int i = 0; i < n; ++i) {
                HBITMAP bmp = grabScreen(x, y, w, h);
                uint64_t hv = hashBitmap(bmp, w, h);
                DeleteObject(bmp);
                printf("sample %d: %016llx\n", i, (unsigned long long)hv);
                fflush(stdout);
                if (i + 1 < n) Sleep(interval);
            }
        }
    } else if (mode == "quit") {
        HWND hwnd = findApp();
        if (hwnd) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        printf("quit sent\n");
    } else {
        printf("unknown mode or missing args\n");
        rc = 2;
    }

    Gdiplus::GdiplusShutdown(token);
    return rc;
}

/*
 * Tux-Tools - native Win32 edition
 *
 * A single self-contained GUI application that hosts the project tools. The
 * first tool is the OpenGL / Vulkan driver capability viewer.
 *
 * Layout: a coloured top bar (logo + product name + tool switcher + caption
 * buttons), a light navigation sidebar whose top holds the API switch
 * (OpenGL / Vulkan) followed by the report section list, a light content area
 * with the property table, and a dark status bar at the bottom.
 *
 * Only Windows system libraries are used (no Qt, no extra DLLs).
 */

#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <shellapi.h>

#include "collectors.h"
#include "theme.h"
#include "settings.h"
#include "ui_controls.h"
#include "render/render_tool.h"
#include "decode/decode_test.h"
#include "app_log.h"
#include "resource.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

namespace {

// The about box is the one place the repository facts live, so they are kept
// together here rather than sprinkled through the drawing code.
const wchar_t kAppVersion[] = L"1.0.0";
const wchar_t kAppRepo[] = L"github.com/moze30/tux-tools";
const wchar_t kAppRepoUrl[] = L"https://github.com/moze30/tux-tools";
const wchar_t kAppAuthor[] = L"moze30";
const wchar_t kAppLicense[] = L"GPL-3.0";
const wchar_t kAppSummary[] = L"通过 Windows 图形 API 查看驱动能力、测试驱动性能，并对比系统各条视频解码路径。";

// Credited because their content is what the tools draw: the driver test ports
// these demos rather than inventing its own scenes, and the capability viewer
// reports the same properties these projects collect.
//
// "shown" is what the card prints and "url" is what the browser is handed. They
// differ only in the scheme: the address reads better without "https://" beside
// the project name, but a browser needs it spelled out.
struct Credit {
    const wchar_t* name;
    const wchar_t* shown;
    const wchar_t* url;
};
const Credit kCredits[] = {
    {L"VulkanCapsViewer", L"github.com/SaschaWillems/VulkanCapsViewer",
     L"https://github.com/SaschaWillems/VulkanCapsViewer"},
    {L"glCapsViewer", L"github.com/SaschaWillems/glCapsViewer",
     L"https://github.com/SaschaWillems/glCapsViewer"},
    {L"Vulkan-Tools (vkcube)", L"github.com/KhronosGroup/Vulkan-Tools",
     L"https://github.com/KhronosGroup/Vulkan-Tools"},
    {L"glxgears", L"github.com/CalvinHartwell/windows-glxgears",
     L"https://github.com/CalvinHartwell/windows-glxgears"},
    {L"DSMF-Decode-Test", L"github.com/Waim908/DSMF-Decode-Test",
     L"https://github.com/Waim908/DSMF-Decode-Test"},
};
const int kCreditCount = static_cast<int>(sizeof(kCredits) / sizeof(kCredits[0]));

const wchar_t kWindowClass[] = L"TuxToolsWindow";
const wchar_t kWindowTitle[] = L"Tux-Tools";
const wchar_t kBrandName[] = L"Tux-Tools";
const std::wstring kToolLabel = L"驱动能力查看器";

#define ID_SEARCH 1002
#define ID_SECTION_LIST 1003
#define ID_VALUE_LIST 1004
#define ID_STATUS_TEXT 1005
#define ID_RENDER_HOST 1006

enum class Api { OpenGL = 0, Vulkan = 1 };

// Which tool is currently active in the shell.
enum class ToolKind { DriverCaps = 0, DriverTest = 1, DecodeTest = 2 };

// Entries of the "driver test" tool, in sidebar order.
enum class DxApi { Dx9 = 0, Dx10 = 1, Dx11 = 2, Dx12 = 3, OpenGl = 4, Vulkan = 5 };

const wchar_t* dxApiLabel(DxApi api) {
    switch (api) {
        case DxApi::Dx9: return L"Direct3D 9";
        case DxApi::Dx10: return L"Direct3D 10";
        case DxApi::Dx11: return L"Direct3D 11";
        case DxApi::Dx12: return L"Direct3D 12";
        case DxApi::OpenGl: return L"OpenGL";
        case DxApi::Vulkan: return L"Vulkan";
    }
    return L"";
}

// Top-bar search box geometry (96-DPI design units; scaled on demand).
const int kSearchWidthDesign = 300;
const int kSearchHeightDesign = 32;
const int kSearchLeftDesign = 320;

// Custom caption buttons rendered inside the blue top bar.
const int kCaptionButtonWidthDesign = 46;
const int kResizeBorderDesign = 8;

// Tool switcher button in the top bar (where the API switch used to be).
const int kToolButtonWidthDesign = 200;
const int kToolButtonHeightDesign = 32;

// API switch at the top of the sidebar.
const int kSwitchMarginDesign = 12;
const int kSwitchHeightDesign = 34;
const int kSwitchGapDesign = 8;

int searchWidth() { return theme::textBox(kSearchWidthDesign, theme::kFontBody); }
// The search box is a box with one line of text in it, so it follows the type.
int searchHeight() { return theme::metric(kSearchHeightDesign); }
// Its width has to fit the placeholder, which is a line of the body face.
int searchWidthScaled() { return theme::textBox(kSearchWidthDesign, theme::kFontBody); }
int searchTop() { return (theme::kTopBarHeight - searchHeight()) / 2; }
int searchLeft() { return dpi::scale(kSearchLeftDesign); }
// The glyphs are drawn at a fixed size, but the bar they sit in grows, so the
// buttons grow with it or three of them stop filling the right-hand end.
int captionButtonWidth() { return theme::textBox(kCaptionButtonWidthDesign, theme::kFontBody); }
int resizeBorder() { return dpi::scale(kResizeBorderDesign); }

enum CaptionButton { kCaptionNone = -1, kCaptionMin = 0, kCaptionMax = 1, kCaptionClose = 2 };

// Every interactive element on the settings card. The card is a list of rows
// with one control each, so one hit rectangle and one hot/pressed pair per item
// covers all of them; adding a row is one entry here and one case at each use.
// Tool-switcher commands. They are the popup menu's ids and also what a
// WM_COMMAND carrying a menu id is understood to mean.
enum AppCommand {
    kCmdToolCaps = 1,
    kCmdToolTest = 2,
    kCmdToolDecode = 3,
    kCmdSettings = 4,
    kCmdAbout = 5,
    kCmdLast = kCmdAbout
};

enum SettingsItem {
    kSettingsNone = -1,
    kSettingsScaleDown = 0,
    kSettingsScaleUp,
    kSettingsScaleReset,
    kSettingsLog,
    kSettingsPath,
    kSettingsItemCount
};

struct AppState {
    Api api = Api::OpenGL;
    ToolKind tool = ToolKind::DriverCaps;
    DxApi dxApi = DxApi::Dx11;
    viewer::Report gl;
    viewer::Report vk;
    bool glLoaded = false;
    bool vkLoaded = false;

    // Driver-test tool state. All test content is generated by the renderer
    // itself; nothing is loaded from disk.
    render::Tool* renderer = nullptr;
    DxApi rendererApi = DxApi::Dx11;
    bool rendererReady = false;
    bool renderRunning = false;
    LARGE_INTEGER perfFrequency = {};
    LARGE_INTEGER renderStart = {};
    // Frame-rate measurement. The render loop is deliberately uncapped so the
    // device's real throughput is what gets measured.
    int framesInWindow = 0;
    LARGE_INTEGER fpsWindowStart = {};
    double currentFps = 0.0;
    std::wstring renderDeviceInfo;

    // Decode test tool state. The decoder owns the video surface and the log;
    // the shell only lays it out and forwards the timer.
    decode::DecodeTest* decoder = nullptr;
    bool decoderReady = false;
    bool openButtonHot = false;
    bool openButtonPressed = false;
    // The decode status line is rebuilt every tick; only push it to the status
    // bar when it actually changed, to avoid a window message per frame.
    std::wstring lastDecodeStatus;
    RECT openButtonRect = {};
    RECT videoRect = {};
    RECT logRect = {};

    // The about box is an in-window overlay rather than a second top-level
    // window: everything else here is drawn by hand, and an overlay keeps it
    // that way without a dialog resource or a nested message loop.
    bool aboutVisible = false;
    // The settings card, same construction as the about box.
    bool settingsVisible = false;
    RECT settingsRects[kSettingsItemCount] = {};
    int settingsHot = kSettingsNone;
    int settingsPressed = kSettingsNone;
    // Where the card's top edge sits while it is open, or -1 when it still has
    // to be placed. The card is tall enough that its controls sit where the
    // pointer already is, so it is placed once, from the client size, and then
    // left there: re-centring it on every change would slide the very button
    // the user is clicking out from under the pointer.
    int settingsCardTop = -1;
    bool aboutCloseHot = false;
    bool aboutLinkHot = false;
    RECT aboutCloseRect = {};
    RECT aboutLinkRect = {};

    // One row per credited project. They are drawn as links and clicked to open
    // the project on GitHub, so each needs its own hit rectangle; the index of
    // the one under the pointer is what the hover styling keys off.
    RECT creditRects[kCreditCount] = {};
    int creditHot = -1;

    int apiHot = -1;
    int sectionHot = -1;
    int captionHot = kCaptionNone;
    int captionPressed = kCaptionNone;
    bool toolHot = false;
    bool toolPressed = false;
    bool runButtonHot = false;
    bool runButtonPressed = false;
    RECT glSwitchRect = {};
    RECT vkSwitchRect = {};
    RECT toolButtonRect = {};
    RECT runButtonRect = {};
    RECT contentRect = {};
    RECT statusRect = {};
    RECT renderHostRect = {};

    HWND search = nullptr;
    HWND sectionList = nullptr;
    HWND valueList = nullptr;
    HWND renderHost = nullptr;
    HWND status = nullptr;

    // A window that has hosted a DXGI *flip model* swap chain keeps presenting
    // that chain's last frame through the compositor, even after the chain has
    // been released. Any bitblt model chain (Direct3D 9 / 10 / 11) created on
    // the same HWND afterwards renders perfectly but is never composited, so
    // the canvas appears frozen on the last Direct3D 12 frame. The only
    // reliable cure is to hand the next renderer a brand new host window, so
    // this flag records that the current one is contaminated.
    bool renderHostStale = false;

    // Whether the renderer is currently drawing into a top level popup instead
    // of a child of this window.
    //
    // It starts false on every platform. The renderer is given a child window
    // first because that is the shape that belongs inside this window's layout;
    // if a backend then fails to start, the same backend is retried once on a
    // popup, and the answer is remembered in detachWorks. Wine on Winlator only
    // implements window presentation for top level windows, so there the retry
    // is what makes the driver test work at all, while on Windows nothing ever
    // fails and the popup is never used.
    bool detachRenderHost = false;
    bool detachWorks = false;
    bool detachTried = false;
    HWND detachedHost = nullptr;

    HICON logo = nullptr;

    HFONT font = nullptr;
    HFONT boldFont = nullptr;
    HFONT titleFont = nullptr;
    HFONT brandFont = nullptr;
    HFONT captionFont = nullptr;
};

AppState* state(HWND hwnd) {
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void fillRect(HDC dc, const RECT& rc, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
}

void fillRoundRect(HDC dc, const RECT& rc, int radius, COLORREF fill) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_NULL, 0, fill);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void drawText(HDC dc, const std::wstring& text, RECT rc, COLORREF color, HFONT font,
              UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &rc, format);
    SelectObject(dc, old);
}

bool containsFold(const std::wstring& text, const std::wstring& query) {
    if (query.empty()) return true;
    std::wstring t, q;
    t.reserve(text.size());
    q.reserve(query.size());
    for (wchar_t c : text) t.push_back(towlower(c));
    for (wchar_t c : query) q.push_back(towlower(c));
    return t.find(q) != std::wstring::npos;
}

std::wstring searchText(HWND hwnd) {
    AppState* s = state(hwnd);
    if (!s || !s->search) return L"";
    std::wstring text;
    ui::searchBoxGetText(s->search, text);
    return text;
}

viewer::Report& currentReport(AppState* s) {
    return s->api == Api::OpenGL ? s->gl : s->vk;
}

void updateValueList(HWND hwnd);

void updateStatus(HWND hwnd);
void updateToolVisibility(HWND hwnd);
void loadCurrentApi(HWND hwnd);
void destroyRenderer(HWND hwnd);
void ensureRenderer(HWND hwnd);
void createRenderHost(HWND hwnd);

// Window procedure for the detached render host.
//
// The popup lies over the content area of the main window, so without this it
// would swallow every click meant for the window underneath. Returning
// HTTRANSPARENT from the hit test passes those clicks straight through, which
// makes the popup behave as if it were not there as far as input is concerned.
// Nothing else is handled: the graphics API owns the window surface, and the
// rest is left to the default procedure.
LRESULT CALLBACK detachedHostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
void startRender(HWND hwnd);
void stopRender(HWND hwnd);

void showToolMenu(HWND hwnd);
void selectTool(HWND hwnd, ToolKind tool);
void setAboutVisible(HWND hwnd, bool visible);
void setSettingsVisible(HWND hwnd, bool visible);
void setOverlayVisible(HWND hwnd, bool visible);


// --- Driver test tool -------------------------------------------------------

void destroyRenderer(HWND hwnd) {
    AppState* s = state(hwnd);
    if (!s || !s->renderer) return;
    s->renderer->shutdown();
    delete s->renderer;
    s->renderer = nullptr;
    s->rendererReady = false;
    s->renderRunning = false;
    s->renderDeviceInfo.clear();
}

// Creates a fresh render host child window and retires the old one.
//
// The window itself is trivial; the point is that it has never hosted a DXGI
// flip model swap chain, which is the state that makes the compositor keep
// showing a stale Direct3D 12 frame. See the comment on AppState::renderHostStale.
void createRenderHost(HWND hwnd) {
    AppState* s = state(hwnd);
    if (!s) return;

    HINSTANCE inst = GetModuleHandleW(nullptr);

    HWND host = nullptr;

    if (s->detachRenderHost) {
        // The experimental path: an owned popup rather than a child. It still
        // belongs to the main window - it minimises and closes with it - but it
        // is a top level window, which is the shape every standalone Vulkan or
        // Direct3D sample uses and the shape a translation layer is most likely
        // to have been tested against.
        if (s->detachedHost && IsWindow(s->detachedHost)) {
            DestroyWindow(s->detachedHost);
        }
        // The menu slot is the child-window control id and must be left null for
        // a popup, where the same parameter means a menu handle instead. Passing
        // the id here is what made CreateWindowEx fail with
        // ERROR_CLASS_DOES_NOT_EXIST - a thoroughly misleading error for it.
        // NOACTIVATE keeps a click on the popup from pulling activation away
        // from the main window, and the class's hit test lets the click through
        // to the window beneath as well.
        // TOPMOST, because the popup lies over the main window and would
        // otherwise be covered by it whenever that window is repainted or
        // activated. The symptom of getting this wrong is a blank canvas with a
        // moving frame rate: the renderer is drawing, nothing is visible, and the
        // white fill of the window underneath is what is on screen.
        host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                               L"TuxToolsRenderHostDetached", L"",
                               WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                               0, 0, 0, 0, hwnd, nullptr, inst, nullptr);
        s->detachedHost = host;
    } else {
        host = CreateWindowExW(0, L"TuxToolsRenderHost", L"",
            WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(ID_RENDER_HOST), inst, nullptr);
    }
    if (!host) {
        applog::write(L"创建渲染宿主窗口失败（错误 %lu）", GetLastError());
        return;
    }

    const RECT r = s->renderHostRect;
    if (s->detachRenderHost) {
        // An owned popup is positioned in screen coordinates, so the client
        // rectangle has to be mapped out to the screen.
        POINT tl = {r.left, r.top};
        ClientToScreen(hwnd, &tl);
        SetWindowPos(host, HWND_TOPMOST, tl.x, tl.y, r.right - r.left,
                     r.bottom - r.top, SWP_NOACTIVATE);
    } else {
        MoveWindow(host, r.left, r.top, r.right - r.left, r.bottom - r.top, FALSE);
    }

    // Swap the new window in before the old one goes away, so the canvas never
    // shows a hole, then retire the old host.
    HWND old = s->renderHost;
    s->renderHost = host;
    s->renderHostStale = false;

    applog::write(L"驱动测试：新渲染宿主窗口 %p（%ls）", reinterpret_cast<void*>(host),
                  host == old ? L"沿用旧窗口" : L"全新窗口");
    ShowWindow(host, s->tool == ToolKind::DriverTest ? SW_SHOW : SW_HIDE);
    if (old) {
        ShowWindow(old, SW_HIDE);
        DestroyWindow(old);
    }
}

// Creates the renderer for the currently selected DX API and initialises it on
// the render host window.
void ensureRenderer(HWND hwnd) {
    AppState* s = state(hwnd);
    if (!s) return;
    if (s->renderer && s->rendererApi == s->dxApi && s->rendererReady) return;

    destroyRenderer(hwnd);

    // Once a popup has been shown to work, keep using one: remembering that is
    // the point of detachWorks, so the failure is not paid again per backend.
    if (s->detachWorks && !s->detachRenderHost) {
        s->detachRenderHost = true;
    }

    // A fresh host window for every start, unconditionally.
    //
    // This used to be done only for the three backends known to leave the window
    // unusable: Direct3D 12 and Vulkan through a flip model chain the compositor
    // keeps showing, and OpenGL because SetPixelFormat may only be called once.
    // That list turned out to be incomplete. Measured on Wine, a window that has
    // hosted one Direct3D present never shows the next one either - Direct3D 10
    // and 11 both rendered at full rate into a window nothing reached the screen
    // from, and the canvas stayed on whatever the window underneath painted.
    //
    // Rather than extend the list every time another backend is found to be
    // affected, the window is now always new. It costs one CreateWindow per
    // start, which is nothing next to creating a graphics device, and it removes
    // the class of fault entirely.
    createRenderHost(hwnd);

    HWND host = s->renderHost;
    RECT rc;
    GetClientRect(host, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) {
        applog::write(L"驱动测试：渲染区尺寸无效（%dx%d），跳过初始化", w, h);
        return;
    }

    render::Tool* tool = nullptr;
    switch (s->dxApi) {
        case DxApi::Dx9: tool = render::createDx9(); break;
        case DxApi::Dx10: tool = render::createDx10(); break;
        case DxApi::Dx11: tool = render::createDx11(); break;
        case DxApi::Dx12: tool = render::createDx12(); break;
        case DxApi::OpenGl: tool = render::createOpenGl(); break;
        case DxApi::Vulkan: tool = render::createVulkan(); break;
    }
    if (!tool) return;

    if (!tool->init(host)) {
        // The backend recorded why as it failed; without this line the log would
        // say only that the test started and stopped, which is exactly what made
        // the first version of this log useless.
        const std::wstring why = tool->failureReason();
        applog::write(L"驱动测试：%ls 初始化失败 - %ls", dxApiLabel(s->dxApi),
                      why.c_str());
        delete tool;
        s->renderer = nullptr;
        s->rendererReady = false;

        // One automatic retry on a top level window. Some translation layers -
        // Wine on Winlator is the one this was written for - implement window
        // presentation for top level windows only, so a backend that cannot use
        // a child window works normally once given a popup. The retry happens
        // once per run and only after a real failure, so on a platform where the
        // child window works this code never runs.
        if (!s->detachRenderHost && !s->detachTried) {
            s->detachTried = true;
            applog::write(L"驱动测试：改用顶层窗口重试 %ls", dxApiLabel(s->dxApi));
            s->detachRenderHost = true;
            // ensureRenderer creates the window itself, at the top of the call.
            ensureRenderer(hwnd);
            if (s->rendererReady) {
                // Remember the answer: later backends go straight to a popup
                // instead of failing once more each.
                s->detachWorks = true;
                applog::write(L"驱动测试：顶层窗口可用，后续后端将直接使用");
            } else {
                s->detachRenderHost = false;
                createRenderHost(hwnd);
            }
        }

        if (s->rendererReady) return;
        s->renderDeviceInfo = std::wstring(L"无法初始化 ") + dxApiLabel(s->dxApi) +
                              L"：" + why;
        return;
    }
    applog::write(L"驱动测试：%ls 初始化成功 - %ls", dxApiLabel(s->dxApi),
                  tool->deviceInfo().c_str());

    s->renderer = tool;
    s->rendererApi = s->dxApi;
    s->rendererReady = true;
    s->renderDeviceInfo = tool->deviceInfo();

    // Every backend is treated as having spoiled its window, so the flag is set
    // for all of them. It is now only a record of what happened; the shell no
    // longer consults it before rebuilding, because rebuilding is unconditional.
    s->renderHostStale = true;

    QueryPerformanceFrequency(&s->perfFrequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    s->renderStart = now;
}

// Starts the driver test: creates the renderer (if needed) and runs the loop.
void startRender(HWND hwnd) {
    {
        AppState* s0 = state(hwnd);
        if (s0) applog::write(L"驱动测试：开始 %ls", dxApiLabel(s0->dxApi));
    }
    AppState* s = state(hwnd);
    if (!s) return;
    ensureRenderer(hwnd);
    if (!s->rendererReady || !s->renderer) {
        updateStatus(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
        return;
    }

    // Reset the animation clock and resize to the host client area.
    RECT rc;
    GetClientRect(s->renderHost, &rc);
    s->renderer->resize(rc.right - rc.left, rc.bottom - rc.top);
    QueryPerformanceFrequency(&s->perfFrequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    s->renderStart = now;
    s->fpsWindowStart = now;
    s->framesInWindow = 0;
    s->currentFps = 0.0;
    // Re-assert the host placement before the first frame. Starting a backend is
    // exactly when the window can have drifted behind the main window - a
    // previous stop, a resize, or a repaint of the window underneath - and doing
    // it here means the canvas is on screen before anything is drawn into it.
    if (s->detachRenderHost && s->renderHost) {
        RECT r2 = s->renderHostRect;
        POINT tl = {r2.left, r2.top};
        ClientToScreen(hwnd, &tl);
        SetWindowPos(s->renderHost, HWND_TOPMOST, tl.x, tl.y, r2.right - r2.left,
                     r2.bottom - r2.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        // Recorded because "the canvas is blank but the frame rate moves" is
        // ambiguous between a window that is hidden and one that is behind the
        // window underneath, and these four facts separate them.
        LONG_PTR ex = GetWindowLongPtrW(s->renderHost, GWL_EXSTYLE);
        applog::write(L"驱动测试：宿主窗口 可见=%ls 置顶=%ls 位置=%ld,%ld 尺寸=%ldx%ld",
                      IsWindowVisible(s->renderHost) ? L"是" : L"否",
                      (ex & WS_EX_TOPMOST) ? L"是" : L"否", tl.x, tl.y,
                      r2.right - r2.left, r2.bottom - r2.top);
    }

    s->renderRunning = true;
    updateStatus(hwnd);
    InvalidateRect(hwnd, nullptr, TRUE);
}

// Stops the driver test and releases the graphics device.
void stopRender(HWND hwnd) {
    AppState* s = state(hwnd);
    if (!s) return;
    // Only worth a line when something was actually running; a stop with nothing
    // to stop happens on every tool switch and would be noise.
    if (s->renderer || s->renderRunning) applog::write(L"驱动测试：停止");
    s->renderRunning = false;
    s->currentFps = 0.0;
    s->framesInWindow = 0;
    destroyRenderer(hwnd);
    updateStatus(hwnd);
    InvalidateRect(hwnd, nullptr, TRUE);
}

// Fills the sidebar section list according to the active tool.
void populateSections(HWND hwnd) {
    AppState* s = state(hwnd);
    if (!s) return;

    std::vector<std::wstring> titles;
    std::vector<int> counts;
    if (s->tool == ToolKind::DriverCaps) {
        viewer::Report& report = currentReport(s);
        for (const viewer::Section& section : report.sections) {
            titles.push_back(section.title);
            counts.push_back(static_cast<int>(section.fields.size()));
        }
    } else if (s->tool == ToolKind::DriverTest) {
        titles.push_back(L"Direct3D 9");
        titles.push_back(L"Direct3D 10");
        titles.push_back(L"Direct3D 11");
        titles.push_back(L"Direct3D 12");
        titles.push_back(L"OpenGL");
        titles.push_back(L"Vulkan");
        counts.assign(6, -1);
    } else if (s->tool == ToolKind::DecodeTest) {
        for (int i = 0; i < 6; ++i) {
            titles.push_back(decode::methodName(static_cast<decode::Method>(i)));
            counts.push_back(-1);
        }
    }
    ui::sectionListSetItems(s->sectionList, titles, counts);
    ui::sectionListSetSelection(s->sectionList, 0);
    updateValueList(hwnd);
}

void updateValueList(HWND hwnd) {
    AppState* s = state(hwnd);
    if (!s) return;

    if (s->tool == ToolKind::DriverTest) {
        // The sidebar selects a Direct3D API; switching stops any running test
        // and waits for the user to press "开始".
        int index = ui::sectionListGetSelection(s->sectionList);
        if (index >= 0) {
            switch (index) {
        case 0: s->dxApi = DxApi::Dx9; break;
        case 1: s->dxApi = DxApi::Dx10; break;
        case 2: s->dxApi = DxApi::Dx11; break;
        case 3: s->dxApi = DxApi::Dx12; break;
        case 4: s->dxApi = DxApi::OpenGl; break;
        case 5: s->dxApi = DxApi::Vulkan; break;
        default: s->dxApi = DxApi::Dx9; break;
    }
            stopRender(hwnd);
        }
        updateStatus(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
        return;
    }

    if (s->tool == ToolKind::DecodeTest) {
        // The sidebar selects one of the six decoding paths. Changing it stops
        // whatever is playing, because only one decoder can own the video
        // surface at a time.
        if (s->decoder) {
            int index = ui::sectionListGetSelection(s->sectionList);
            if (index >= 0 && index < 6) {
                s->decoder->setMethod(static_cast<decode::Method>(index));
            }
        }
        updateStatus(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
        return;
    }

    viewer::Report& report = currentReport(s);
    int index = ui::sectionListGetSelection(s->sectionList);
    if (index < 0 || index >= static_cast<int>(report.sections.size())) {
        ui::valueListSetRows(s->valueList, {});
        return;
    }

    const viewer::Section& section = report.sections[static_cast<size_t>(index)];
    std::wstring query = searchText(hwnd);
    std::vector<ui::ValueRow> rows;
    for (const viewer::Field& field : section.fields) {
        if (!query.empty() &&
            !(containsFold(field.name, query) || containsFold(field.value, query))) {
            continue;
        }
        rows.push_back({field.name, field.value});
    }
    ui::valueListSetRows(s->valueList, rows);
}

void updateStatus(HWND hwnd) {
    AppState* s = state(hwnd);
    if (!s || !s->status) return;
    if (s->tool == ToolKind::DriverTest) {
        std::wstring text = s->renderDeviceInfo;
        if (s->renderRunning && s->currentFps > 0.0) {
            wchar_t fps[64] = {};
            swprintf(fps, 64, L"   |   %.1f FPS", s->currentFps);
            text += fps;
        }
        SetWindowTextW(s->status, text.c_str());
        return;
    }
    if (s->tool == ToolKind::DecodeTest) {
        std::wstring text;
        if (s->decoder) {
            text = s->decoder->statusText();
            if (text.empty()) text = s->decoder->filePath();
        }
        if (text.empty()) text = L"选择一种解码方式，打开媒体文件后点击开始";
        SetWindowTextW(s->status, text.c_str());
        return;
    }
    viewer::Report& report = currentReport(s);
    SetWindowTextW(s->status, report.description.c_str());
}

// Tool switcher menu: pick between the driver capability viewer and the
// Direct3D driver test.
// Carries out one of the tool-switcher commands. Both paths in reach the same
// ids: the popup menu below, and a WM_COMMAND carrying a menu id. Routing both
// through here keeps the two from drifting apart, and gives the switcher the
// same scriptable surface the command line already offers.
void runAppCommand(HWND hwnd, int cmd) {
    switch (cmd) {
        case kCmdToolCaps: selectTool(hwnd, ToolKind::DriverCaps); break;
        case kCmdToolTest: selectTool(hwnd, ToolKind::DriverTest); break;
        case kCmdToolDecode: selectTool(hwnd, ToolKind::DecodeTest); break;
        case kCmdSettings: setSettingsVisible(hwnd, true); break;
        case kCmdAbout: setAboutVisible(hwnd, true); break;
        default: break;
    }
}

void showToolMenu(HWND hwnd) {
    AppState* s = state(hwnd);
    if (!s) return;

    HMENU menu = CreatePopupMenu();
    UINT capsFlags = MF_STRING | (s->tool == ToolKind::DriverCaps ? MF_CHECKED : 0);
    UINT testFlags = MF_STRING | (s->tool == ToolKind::DriverTest ? MF_CHECKED : 0);
    UINT decodeFlags = MF_STRING | (s->tool == ToolKind::DecodeTest ? MF_CHECKED : 0);
    AppendMenuW(menu, capsFlags, kCmdToolCaps, L"驱动能力查看器");
    AppendMenuW(menu, testFlags, kCmdToolTest, L"驱动测试");
    AppendMenuW(menu, decodeFlags, kCmdToolDecode, L"解码测试");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdSettings, L"设置...");
    AppendMenuW(menu, MF_STRING, kCmdAbout, L"关于 Tux-Tools...");

    RECT r = s->toolButtonRect;
    POINT pt = {r.left, r.bottom};
    ClientToScreen(hwnd, &pt);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                             pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);

    runAppCommand(hwnd, cmd);
}

void selectTool(HWND hwnd, ToolKind tool) {
    AppState* s = state(hwnd);
    if (!s || s->tool == tool) return;
    s->tool = tool;
    switch (tool) {
        case ToolKind::DriverCaps: applog::write(L"切换到：驱动能力查看器"); break;
        case ToolKind::DriverTest: applog::write(L"切换到：驱动测试"); break;
        case ToolKind::DecodeTest: applog::write(L"切换到：解码测试"); break;
    }
    ui::searchBoxSetText(s->search, L"");

    if (tool == ToolKind::DriverTest) {
        updateToolVisibility(hwnd);
        ui::sectionListSetSelection(s->sectionList, 0);
        s->dxApi = DxApi::Dx9;
        s->renderRunning = false;  // do not auto-run; wait for "开始"
    } else if (tool == ToolKind::DecodeTest) {
        destroyRenderer(hwnd);
        if (s->decoder) s->decoder->setMethod(decode::Method::DirectShowSoftware);
        updateToolVisibility(hwnd);
        ui::sectionListSetSelection(s->sectionList, 0);
    } else {
        destroyRenderer(hwnd);
        updateToolVisibility(hwnd);
        loadCurrentApi(hwnd);
        return;
    }
    populateSections(hwnd);
    updateStatus(hwnd);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void loadCurrentApi(HWND hwnd) {
    AppState* s = state(hwnd);
    if (!s) return;

    if (s->api == Api::OpenGL && !s->glLoaded) {
        HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        s->gl = viewer::collectOpenGL();
        s->glLoaded = true;
        SetCursor(old);
    } else if (s->api == Api::Vulkan && !s->vkLoaded) {
        HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        s->vk = viewer::collectVulkan();
        s->vkLoaded = true;
        SetCursor(old);
    }

    populateSections(hwnd);
    updateStatus(hwnd);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void selectApi(HWND hwnd, Api api) {
    AppState* s = state(hwnd);
    if (!s || s->tool != ToolKind::DriverCaps || s->api == api) return;
    s->api = api;
    ui::searchBoxSetText(s->search, L"");
    loadCurrentApi(hwnd);
}

RECT captionRect(HWND hwnd, int button) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int right = rc.right;
    // Order left-to-right: minimize, maximize, close.
    int indexFromRight = (button == kCaptionClose) ? 0
                       : (button == kCaptionMax) ? 1 : 2;
    RECT r = {};
    r.right = right - indexFromRight * captionButtonWidth();
    r.left = r.right - captionButtonWidth();
    r.top = 0;
    r.bottom = theme::kTopBarHeight;
    return r;
}

int captionButtonFromPoint(HWND hwnd, POINT pt) {
    for (int b = 0; b < 3; ++b) {
        RECT r = captionRect(hwnd, b);
        if (PtInRect(&r, pt)) return b;
    }
    return kCaptionNone;
}

RECT searchRect() {
    RECT r = {searchLeft(), searchTop(), searchLeft() + searchWidth(),
              searchTop() + searchHeight()};
    return r;
}

void updateTabRects(HWND hwnd) {
    AppState* s = state(hwnd);
    if (!s) return;
    RECT rc;
    GetClientRect(hwnd, &rc);

    // API switch at the top of the sidebar, two equal halves.
    int margin = dpi::scale(kSwitchMarginDesign);
    int gap = dpi::scale(kSwitchGapDesign);
    int total = theme::kSidebarWidth - margin * 2;
    int halfWidth = (total - gap) / 2;
    int top = theme::kTopBarHeight + margin;
    int height = theme::metric(kSwitchHeightDesign);
    s->glSwitchRect = {margin, top, margin + halfWidth, top + height};
    s->vkSwitchRect = {margin + halfWidth + gap, top, margin + total, top + height};

    // Tool switcher button in the top bar, placed to the left of the caption
    // buttons (minimize / maximize / close).
    // The switcher holds the longest label in the program ("驱动能力查看器"), so
    // its width tracks the type or the label runs out of its own button.
    int toolWidth = theme::textBox(kToolButtonWidthDesign, theme::kFontBody);
    int toolHeight = theme::metric(kToolButtonHeightDesign);
    int toolTop = (theme::kTopBarHeight - toolHeight) / 2;
    int captionsLeft = rc.right - captionButtonWidth() * 3;
    s->toolButtonRect = {captionsLeft - theme::kSpaceS - toolWidth, toolTop,
                         captionsLeft - theme::kSpaceS, toolTop + toolHeight};
}

void layoutChildren(HWND hwnd) {
    AppState* s = state(hwnd);
    if (!s) return;

    RECT rc;
    GetClientRect(hwnd, &rc);

    updateTabRects(hwnd);

    int topBar = theme::kTopBarHeight;
    int statusTop = rc.bottom - theme::kStatusHeight;
    s->statusRect = {0, statusTop, rc.right, rc.bottom};
    s->contentRect = {theme::kSidebarWidth, topBar, rc.right, statusTop};

    int contentBottom = statusTop;

    // The section list sits below the API switch at the top of the sidebar.
    int listTop = s->glSwitchRect.bottom + dpi::scale(kSwitchMarginDesign);
    int listHeight = contentBottom - listTop - dpi::scale(kSwitchMarginDesign);
    if (listHeight < 0) listHeight = 0;
    MoveWindow(s->sectionList, 0, listTop, theme::kSidebarWidth, listHeight, TRUE);

    // Search box in the top bar.
    MoveWindow(s->search, searchLeft(), searchTop(), searchWidth(), searchHeight(), TRUE);

    // Value table fills the content area below the page header. The header
    // band is what holds the page title, so it scales with the type.
    int pageHeader = theme::metric(64);
    int tableTop = topBar + pageHeader;
    int tableLeft = theme::kSidebarWidth + theme::kSpaceXL;
    int tableBottom = contentBottom - theme::kSpaceL;
    int tableHeight = tableBottom - tableTop;
    if (tableHeight < 0) tableHeight = 0;
    MoveWindow(s->valueList, tableLeft, tableTop,
               rc.right - tableLeft - theme::kSpaceXL, tableHeight, TRUE);

    // Render host (driver test) fills the content area below the page header,
    // matching the value table's geometry.
    int hostTop = topBar + pageHeader;
    int hostLeft = theme::kSidebarWidth + theme::kSpaceXL;
    int hostBottom = contentBottom - theme::kSpaceL;
    int hostHeight = hostBottom - hostTop;
    if (hostHeight < 0) hostHeight = 0;
    s->renderHostRect = {hostLeft, hostTop, rc.right - theme::kSpaceXL, hostBottom};
    if (s->detachRenderHost && s->renderHost) {
        // A popup is placed in screen coordinates and must not be touched while
        // hidden: moving a hidden window is fine, but showing it here would put
        // it on screen outside the driver test.
        POINT tl = {hostLeft, hostTop};
        ClientToScreen(hwnd, &tl);
        // TOPMOST rather than TOP: see the comment on the window creation. The
        // popup has to stay above the window it covers, not merely above its
        // siblings.
        SetWindowPos(s->renderHost, HWND_TOPMOST, tl.x, tl.y,
                     rc.right - theme::kSpaceXL - hostLeft, hostHeight,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    } else {
        MoveWindow(s->renderHost, hostLeft, hostTop,
                   rc.right - theme::kSpaceXL - hostLeft, hostHeight, TRUE);
    }

    // Run/stop button in the page header, right-aligned.
    int runW = theme::metric(96);
    int runH = theme::metric(30);
    // The header band is centred on the taller of the two: the title above and
    // the button here share it, and the button is what has to fit.
    int runTop = topBar + (pageHeader - runH) / 2;
    s->runButtonRect = {rc.right - theme::kSpaceXL - runW, runTop,
                        rc.right - theme::kSpaceXL, runTop + runH};

    // Decode test: an "open file" button sits to the left of the run button,
    // and the content area below the header splits into the video surface and
    // the log. The video keeps the larger share; the log is a fixed band at the
    // bottom so its height does not jump as lines arrive.
    s->openButtonRect = {s->runButtonRect.left - theme::kSpaceS - runW, runTop,
                         s->runButtonRect.left - theme::kSpaceS, runTop + runH};
    {
        int vTop = topBar + pageHeader;
        int vLeft = theme::kSidebarWidth + theme::kSpaceXL;
        int vBottom = contentBottom - theme::kSpaceL;
        // The log is a fixed number of lines tall, so it grows with the type
        // rather than showing fewer and fewer of them.
        int logH = theme::metric(140);
        int vHeight = (vBottom - vTop) - logH - theme::kSpaceM;
        if (vHeight < dpi::scale(80)) vHeight = dpi::scale(80);
        s->videoRect = {vLeft, vTop, rc.right - theme::kSpaceXL, vTop + vHeight};
        s->logRect = {vLeft, s->videoRect.bottom + theme::kSpaceM,
                      rc.right - theme::kSpaceXL, vBottom};
        if (s->decoder) {
            s->decoder->layout(rc, s->videoRect.left, s->videoRect.top,
                               s->videoRect.right - s->videoRect.left,
                               s->videoRect.bottom - s->videoRect.top);
        }
    }

    MoveWindow(s->status, theme::kSpaceL, statusTop, rc.right - theme::kSpaceL * 2,
               theme::kStatusHeight, TRUE);
}

// Shows/hides the content-area children for the active tool.
void updateToolVisibility(HWND hwnd) {
    AppState* s = state(hwnd);
    if (!s) return;
    // While the about overlay is up its card is drawn into the parent window,
    // so nothing may be shown over it.
    if (s->aboutVisible) {
        ShowWindow(s->valueList, SW_HIDE);
        ShowWindow(s->renderHost, SW_HIDE);
        if (s->decoder) s->decoder->show(false);
        return;
    }

    bool test = s->tool == ToolKind::DriverTest;
    bool decodeTool = s->tool == ToolKind::DecodeTest;
    ShowWindow(s->valueList, (test || decodeTool) ? SW_HIDE : SW_SHOW);
    ShowWindow(s->renderHost, test ? SW_SHOW : SW_HIDE);
    if (s->decoder) s->decoder->show(decodeTool);
    ShowWindow(s->search, SW_SHOW);
}

void paintTopBar(HWND hwnd, HDC dc, const RECT& client) {
    AppState* s = state(hwnd);
    if (!s) return;

    RECT bar = {0, 0, client.right, theme::kTopBarHeight};
    fillRect(dc, bar, theme::kTopBar);

    // Logo (application icon) followed by the product name.
    int logoSize = dpi::scale(26);
    int logoTop = (theme::kTopBarHeight - logoSize) / 2;
    int logoLeft = theme::kSpaceL;
    if (s->logo) {
        DrawIconEx(dc, logoLeft, logoTop, s->logo, logoSize, logoSize, 0, nullptr, DI_NORMAL);
    }

    RECT titleRc = {logoLeft + logoSize + theme::kSpaceS, 0,
                    s->toolButtonRect.left - theme::kSpaceM, theme::kTopBarHeight};
    drawText(dc, kBrandName, titleRc, theme::kTopBarText, s->brandFont);

    // Tool switcher (appears where the API switch used to be).
    {
        RECT r = s->toolButtonRect;
        bool hot = s->toolHot;
        bool down = hot && s->toolPressed;
        COLORREF fill = down ? RGB(0xDC, 0xE6, 0xF5)
                             : (hot ? RGB(0xF2, 0xF2, 0xF2) : RGB(0xFF, 0xFF, 0xFF));
        fillRoundRect(dc, r, theme::kRadiusControl, fill);
        COLORREF text = RGB(0x21, 0x21, 0x21);
        std::wstring label = L"驱动能力查看器";
        if (s->tool == ToolKind::DriverTest) label = L"驱动测试";
        else if (s->tool == ToolKind::DecodeTest) label = L"解码测试";
        RECT textRc = r;
        textRc.left += theme::kSpaceM;
        textRc.right -= theme::kSpaceM;
        RECT chevron = {r.right - dpi::scale(22), r.top, r.right - dpi::scale(8), r.bottom};
        drawText(dc, label, textRc, text, s->boldFont);
        // Down chevron.
        int cx = (chevron.left + chevron.right) / 2;
        int cy = (chevron.top + chevron.bottom) / 2;
        int half = dpi::scale(4);
        HPEN pen = CreatePen(PS_SOLID, dpi::scale(1), text);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, cx - half, cy - dpi::scale(1), nullptr);
        LineTo(dc, cx, cy + half - dpi::scale(1));
        LineTo(dc, cx + half, cy - dpi::scale(1));
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    // Caption buttons (minimize / maximize / close), drawn in the blue bar.
    for (int b = 0; b < 3; ++b) {
        RECT r = captionRect(hwnd, b);
        bool hot = s->captionHot == b;
        bool down = hot && s->captionPressed == b;
        COLORREF fill = theme::kTopBar;
        if (b == kCaptionClose) {
            fill = down ? RGB(0xC4, 0x2B, 0x1C)
                        : (hot ? RGB(0xE8, 0x11, 0x23) : theme::kTopBar);
        } else if (down) {
            fill = RGB(0x2E, 0x6B, 0xE0);
        } else if (hot) {
            fill = RGB(0x5A, 0x9A, 0xFF);
        }
        fillRect(dc, r, fill);

        COLORREF glyph = theme::kTopBarText;
        int cx = (r.left + r.right) / 2;
        int cy = (r.top + r.bottom) / 2;
        int half = dpi::scale(6);
        int yOffset = dpi::scale(1);  // optical centring inside the bar
        int penWidth = dpi::scale(1);
        HPEN pen = CreatePen(PS_SOLID, penWidth < 1 ? 1 : penWidth, glyph);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        if (b == kCaptionMin) {
            MoveToEx(dc, cx - half, cy + half - yOffset, nullptr);
            LineTo(dc, cx + half, cy + half - yOffset);
        } else if (b == kCaptionMax) {
            bool maximized = IsZoomed(hwnd);
            if (maximized) {
                // Restore glyph: two offset squares.
                int offset = dpi::scale(3);
                Rectangle(dc, cx - half - offset, cy - yOffset - offset,
                          cx + half - offset, cy + half - yOffset - offset);
                Rectangle(dc, cx - half + offset, cy - half + yOffset + offset,
                          cx + half + offset, cy + yOffset + offset);
            } else {
                Rectangle(dc, cx - half, cy - half + yOffset,
                          cx + half, cy + half + yOffset);
            }
        } else {
            MoveToEx(dc, cx - half, cy - half + yOffset, nullptr);
            LineTo(dc, cx + half, cy + half + yOffset);
            MoveToEx(dc, cx + half, cy - half + yOffset, nullptr);
            LineTo(dc, cx - half, cy + half + yOffset);
        }
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
}

void paintSidebarHeader(HWND hwnd, HDC dc) {
    AppState* s = state(hwnd);
    if (!s) return;

    RECT sidebar = {0, theme::kTopBarHeight, theme::kSidebarWidth, s->statusRect.top};
    fillRect(dc, sidebar, theme::kSidebarBg);

    // Only the capability viewer has anything to put in a segmented switch: it
    // reports on OpenGL and Vulkan and you pick which. The other two tools put a
    // heading here instead, and the section list below is the only control.
    if (s->tool == ToolKind::DriverTest) {
        RECT heading = {theme::kSpaceL, theme::kTopBarHeight + theme::kSpaceM,
                        theme::kSidebarWidth - theme::kSpaceL,
                        s->glSwitchRect.bottom};
        drawText(dc, L"图形 API 测试", heading, theme::kPageSubtitle, s->boldFont);
        return;
    }
    if (s->tool == ToolKind::DecodeTest) {
        RECT heading = {theme::kSpaceL, theme::kTopBarHeight + theme::kSpaceM,
                        theme::kSidebarWidth - theme::kSpaceL,
                        s->glSwitchRect.bottom};
        drawText(dc, L"解码方式", heading, theme::kPageSubtitle, s->boldFont);
        return;
    }

    // Segmented OpenGL / Vulkan switch.
    RECT rects[2] = {s->glSwitchRect, s->vkSwitchRect};
    const wchar_t* labels[2] = {L"OpenGL", L"Vulkan"};
    for (int i = 0; i < 2; ++i) {
        bool active = static_cast<int>(s->api) == i;
        bool hot = s->apiHot == i;
        if (active) {
            fillRoundRect(dc, rects[i], theme::kRadiusControl, theme::kAccent);
        } else {
            fillRoundRect(dc, rects[i], theme::kRadiusControl,
                          hot ? theme::kNavHover : theme::kNavSelected);
        }
        drawText(dc, labels[i], rects[i],
                 active ? theme::kTopBarText : theme::kSidebarText,
                 active ? s->boldFont : s->font,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void paintContent(HWND hwnd, HDC dc, const RECT& client) {
    AppState* s = state(hwnd);

    RECT content = s->contentRect;
    fillRect(dc, content, theme::kContentBg);

    if (s && s->tool == ToolKind::DriverTest) {
        RECT titleRc = {theme::kSidebarWidth + theme::kSpaceXL,
                        theme::kTopBarHeight + theme::metric(14),
                        s->runButtonRect.left - theme::kSpaceM,
                        theme::kTopBarHeight + theme::metric(42)};
        drawText(dc, std::wstring(L"驱动测试 - ") + dxApiLabel(s->dxApi), titleRc,
                 theme::kPageTitle, s->titleFont);

        // Run / stop button.
        RECT r = s->runButtonRect;
        bool down = s->runButtonPressed && s->runButtonHot;
        COLORREF fill = down ? theme::kAccentBorder
                             : (s->runButtonHot ? theme::kAccentHover : theme::kAccent);
        fillRoundRect(dc, r, theme::kRadiusControl, fill);
        drawText(dc, s->renderRunning ? L"停止" : L"开始", r, theme::kTopBarText,
                 s->boldFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT line = {theme::kSidebarWidth, theme::kTopBarHeight,
                     theme::kSidebarWidth + 1, s->statusRect.top};
        fillRect(dc, line, theme::kBorder);
        return;
    }

    if (s && s->tool == ToolKind::DecodeTest) {
        RECT titleRc = {theme::kSidebarWidth + theme::kSpaceXL,
                        theme::kTopBarHeight + theme::metric(14),
                        s->openButtonRect.left - theme::kSpaceM,
                        theme::kTopBarHeight + theme::metric(42)};
        std::wstring title = L"解码测试";
        if (s->decoder) title += std::wstring(L" - ") + decode::methodName(s->decoder->method());
        drawText(dc, title, titleRc, theme::kPageTitle, s->titleFont);

        // Open file button.
        RECT ob = s->openButtonRect;
        COLORREF obFill = s->openButtonPressed && s->openButtonHot
                              ? theme::kAccentBorder
                              : (s->openButtonHot ? theme::kAccentHover : theme::kAccent);
        fillRoundRect(dc, ob, theme::kRadiusControl, obFill);
        drawText(dc, L"打开文件", ob, theme::kTopBarText, s->font,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Run / stop button.
        RECT r = s->runButtonRect;
        bool down = s->runButtonPressed && s->runButtonHot;
        COLORREF fill = down ? theme::kAccentBorder
                             : (s->runButtonHot ? theme::kAccentHover : theme::kAccent);
        fillRoundRect(dc, r, theme::kRadiusControl, fill);
        const bool running = s->decoder && s->decoder->running();
        drawText(dc, running ? L"停止" : L"开始", r, theme::kTopBarText,
                 s->boldFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // The video surface is a child window and paints itself; only the log
        // panel is drawn here.
        if (s->decoder) s->decoder->paint(dc, s->logRect);

        RECT line = {theme::kSidebarWidth, theme::kTopBarHeight,
                     theme::kSidebarWidth + 1, s->statusRect.top};
        fillRect(dc, line, theme::kBorder);
        return;
    }

    // Page header: current section title.
    RECT titleRc = {theme::kSidebarWidth + theme::kSpaceXL,
                    theme::kTopBarHeight + theme::metric(14),
                    client.right - theme::kSpaceXL,
                    theme::kTopBarHeight + theme::metric(42)};
    std::wstring title = L"属性";
    if (s) {
        viewer::Report& report = currentReport(s);
        int index = ui::sectionListGetSelection(s->sectionList);
        if (index >= 0 && index < static_cast<int>(report.sections.size())) {
            title = report.sections[static_cast<size_t>(index)].title;
        }
    }
    if (s) drawText(dc, title, titleRc, theme::kPageTitle, s->titleFont);

    RECT line = {theme::kSidebarWidth, theme::kTopBarHeight,
                 theme::kSidebarWidth + 1, s->statusRect.top};
    fillRect(dc, line, theme::kBorder);
}

void paintStatusBar(HWND hwnd, HDC dc) {
    AppState* s = state(hwnd);
    if (!s) return;
    fillRect(dc, s->statusRect, theme::kStatusBg);

    int dotY = s->statusRect.top + (theme::kStatusHeight - 8) / 2;
    RECT dot = {theme::kSpaceL, dotY, theme::kSpaceL + 8, dotY + 8};
    fillRoundRect(dc, dot, 4, theme::kAccentHover);
    RECT textRc = {dot.right + theme::kSpaceS, s->statusRect.top,
                   s->statusRect.right - 200, s->statusRect.bottom};
    drawText(dc, L"驱动信息已就绪", textRc, theme::kStatusText, s->font);
}

// --- about box --------------------------------------------------------------

// A dimming wash over the whole client area. AlphaBlend comes from msimg32,
// which ships with Windows, so this costs nothing at install time.
void dimClient(HDC dc, const RECT& client) {
    HDC memDc = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);

    RECT rc = client;
    fillRect(memDc, rc, RGB(0x10, 0x14, 0x1A));

    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 120;
    AlphaBlend(dc, client.left, client.top, client.right, client.bottom,
               memDc, 0, 0, client.right, client.bottom, blend);

    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
}

// The overlay is drawn into the parent window, but the sidebar list, the value
// table and the status bar are separate child windows and are composited on top
// of it. They have to go away while it is up, or the card ends up underneath
// them. Only the ones this shell owns are touched; the decode test and the
// render host manage their own visibility.
// Both modal cards are drawn into the parent window, so the same child-window
// hiding serves them; `visible` says whether either one is now up.
void setOverlayVisible(HWND hwnd, bool visible) {
    AppState* s = state(hwnd);
    if (!s) return;
    s->aboutCloseHot = false;
    s->aboutLinkHot = false;
    s->settingsHot = kSettingsNone;
    s->settingsPressed = kSettingsNone;

    const int show = visible ? SW_HIDE : SW_SHOW;
    if (s->sectionList) ShowWindow(s->sectionList, show);
    // The search box and the status bar are child windows too, and a child is
    // composited by the system rather than drawn into our DC, so the dimming
    // wash cannot reach them. They step aside for the duration.
    if (s->search) ShowWindow(s->search, show);
    if (s->status) ShowWindow(s->status, show);
    // The render host and the decoder video surface are child windows as well,
    // and both sit exactly where the card goes. They have to go away when it
    // opens, not only when it closes.
    if (s->renderHost) ShowWindow(s->renderHost, SW_HIDE);
    if (s->decoder) s->decoder->show(false);
    if (s->valueList) {
        // The decode test keeps the value list hidden regardless, so restoring
        // it blindly would put an empty table on screen.
        const bool wanted = !visible && s->tool != ToolKind::DriverTest &&
                            s->tool != ToolKind::DecodeTest;
        ShowWindow(s->valueList, wanted ? SW_SHOW : SW_HIDE);
    }
    if (!visible && s->renderHost) {
        ShowWindow(s->renderHost, s->tool == ToolKind::DriverTest ? SW_SHOW : SW_HIDE);
    }
    if (!visible && s->decoder) s->decoder->show(s->tool == ToolKind::DecodeTest);
    InvalidateRect(hwnd, nullptr, TRUE);
}

// Both cards are drawn into this window, over the same area, so only one can be
// up at a time: opening either dismisses the other rather than stacking the two.
void setAboutVisible(HWND hwnd, bool visible) {
    AppState* s = state(hwnd);
    if (!s) return;
    s->aboutVisible = visible;
    if (visible) s->settingsVisible = false;
    setOverlayVisible(hwnd, visible || s->settingsVisible);
}

void setSettingsVisible(HWND hwnd, bool visible) {
    AppState* s = state(hwnd);
    if (!s) return;
    s->settingsVisible = visible;
    if (visible) {
        s->aboutVisible = false;
        // Re-place the card on every open, so it comes up centred for the text
        // size in effect and then stays put for as long as it is up.
        s->settingsCardTop = -1;
    }
    setOverlayVisible(hwnd, visible || s->aboutVisible);
}

// Opens a URL in the default browser. The about card uses it for the repository
// row and for every credited project.
void openUrl(const wchar_t* url) {
    if (!url || !*url) return;
    ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

void openRepository() { openUrl(kAppRepoUrl); }

// Draws the card and records the hit rectangles for the close button and the
// repository link, which is what makes the overlay interactive.
void paintAbout(HWND hwnd, HDC dc, const RECT& client) {
    AppState* s = state(hwnd);
    if (!s || !s->aboutVisible) return;

    dimClient(dc, client);

    const int cardW = theme::zoom(560);
    const int cardH = theme::zoom(432);
    RECT card = {(client.right - cardW) / 2, (client.bottom - cardH) / 2,
                 (client.right - cardW) / 2 + cardW,
                 (client.bottom - cardH) / 2 + cardH};
    if (card.top < 0) { card.top = theme::zoom(8); card.bottom = card.top + cardH; }

    // Card with a soft shadow, then the accent stripe down the left edge that
    // ties it to the top bar.
    RECT shadow = card;
    OffsetRect(&shadow, theme::zoom(3), theme::zoom(3));
    fillRoundRect(dc, shadow, theme::kRadiusCard, RGB(0xC8, 0xCC, 0xD2));
    fillRoundRect(dc, card, theme::kRadiusCard, RGB(0xFF, 0xFF, 0xFF));

    const int pad = theme::zoom(24);
    int y = card.top + pad;

    // Header: logo, product name, version.
    const int logoSz = theme::zoom(44);
    if (s->logo) {
        DrawIconEx(dc, card.left + pad, y, s->logo, logoSz, logoSz, 0, nullptr, DI_NORMAL);
    }
    RECT nameRc = {card.left + pad + logoSz + theme::zoom(14), y,
                   card.right - pad, y + logoSz / 2};
    drawText(dc, kBrandName, nameRc, theme::kPageTitle, s->titleFont);
    RECT verRc = {nameRc.left, y + logoSz / 2, nameRc.right, y + logoSz};
    drawText(dc, std::wstring(L"版本 ") + kAppVersion + L"  ·  " + kAppLicense,
             verRc, theme::kPageSubtitle, s->font);

    // Close button, top right of the card.
    const int closeSz = theme::zoom(28);
    s->aboutCloseRect = {card.right - pad - closeSz, card.top + pad,
                         card.right - pad, card.top + pad + closeSz};
    if (s->aboutCloseHot) {
        fillRoundRect(dc, s->aboutCloseRect, theme::kRadiusControl, theme::kNavHover);
    }
    {
        const int cx = (s->aboutCloseRect.left + s->aboutCloseRect.right) / 2;
        const int cy = (s->aboutCloseRect.top + s->aboutCloseRect.bottom) / 2;
        const int half = theme::zoom(5);
        HPEN pen = CreatePen(PS_SOLID, theme::zoom(1), theme::kSidebarMuted);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, cx - half, cy - half, nullptr);
        LineTo(dc, cx + half + 1, cy + half + 1);
        MoveToEx(dc, cx + half, cy - half, nullptr);
        LineTo(dc, cx - half - 1, cy + half + 1);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    y += logoSz + theme::zoom(16);
    {
        RECT line = {card.left + pad, y, card.right - pad, y + 1};
        fillRect(dc, line, theme::kDivider);
    }
    y += theme::zoom(16);

    // Summary.
    {
        RECT r = {card.left + pad, y, card.right - pad, y + theme::zoom(44)};
        drawText(dc, kAppSummary, r, theme::kTableText, s->font,
                 DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        y += theme::zoom(52);
    }

    // Key/value rows: the label column is fixed so the values line up.
    const int labelW = theme::zoom(76);
    const int rowH = theme::zoom(24);
    const auto row = [&](const wchar_t* label, const std::wstring& value,
                         COLORREF valueColour, HFONT valueFont) {
        RECT lr = {card.left + pad, y, card.left + pad + labelW, y + rowH};
        drawText(dc, label, lr, theme::kPageSubtitle, s->font);
        RECT vr = {lr.right, y, card.right - pad, y + rowH};
        drawText(dc, value, vr, valueColour, valueFont);
        y += rowH;
    };

    row(L"仓库", kAppRepo, theme::kAccentBorder, s->font);
    row(L"作者", kAppAuthor,
        theme::kTableText, s->font);
    row(L"许可证", kAppLicense, theme::kTableText, s->font);

    // The link is the repository row; remember where it went so a click opens
    // it. Hovering underlines it, which is the only affordance available
    // without pulling the mouse cursor ourselves.
    s->aboutLinkRect = {card.left + pad + labelW, y - rowH * 3, card.right - pad,
                        y - rowH * 2};
    if (s->aboutLinkHot) {
        HGDIOBJ oldPen = SelectObject(
            dc, CreatePen(PS_SOLID, theme::zoom(1), theme::kAccentBorder));
        int uy = s->aboutLinkRect.bottom - theme::zoom(4);
        drawText(dc, kAppRepo, s->aboutLinkRect, theme::kAccentBorder, s->font);
        MoveToEx(dc, s->aboutLinkRect.left, uy, nullptr);
        LineTo(dc, s->aboutLinkRect.left + theme::zoom(150), uy);
        DeleteObject(SelectObject(dc, oldPen));
    }

    y += theme::zoom(14);
    {
        RECT line = {card.left + pad, y, card.right - pad, y + 1};
        fillRect(dc, line, theme::kDivider);
    }
    y += theme::zoom(14);

    // Credits. Every project name is a link to its repository, so each one is
    // drawn in the accent colour and highlights on hover. The whole row is the
    // click target rather than the text alone, which is easier to hit.
    {
        RECT hr = {card.left + pad, y, card.right - pad, y + theme::zoom(20)};
        drawText(dc, L"鸣谢", hr, theme::kPageTitle, s->boldFont);
        y += theme::zoom(24);
    }
    for (int i = 0; i < kCreditCount; ++i) {
        if (y + theme::zoom(30) > card.bottom - theme::zoom(12)) {
            s->creditRects[i] = {};
            continue;
        }
        // The name column only has to hold a project name; the address needs the
        // rest and is the longest string on the card, so it gets the room.
        const int nameW = theme::zoom(158);
        RECT rowRc = {card.left + pad, y, card.right - pad, y + theme::zoom(20)};
        s->creditRects[i] = rowRc;

        // Hover is shown as a tinted background behind the whole row. Swapping
        // the text colour was tried first and was invisible: the two accent
        // shades differ by too little to notice. This is also the tint the
        // sidebar uses for its selected row, so it reads as "this one".
        const bool hot = (s->creditHot == i);
        if (hot) {
            RECT pill = rowRc;
            InflateRect(&pill, -theme::zoom(6), 0);
            fillRoundRect(dc, pill, theme::kRadiusControl, theme::kNavSelected);
        }

        RECT nr = {rowRc.left, y, rowRc.left + nameW, y + theme::zoom(18)};
        drawText(dc, kCredits[i].name, nr, theme::kAccent, s->boldFont,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT dr = {nr.right + theme::zoom(10), y, card.right - pad, y + theme::zoom(18)};
        drawText(dc, kCredits[i].shown, dr, theme::kPageSubtitle, s->font,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += theme::zoom(22);
    }
}

// A rounded pill button, the shape the settings card uses for its small
// controls. Drawn like every other control in the shell: a tint when the pointer
// is over it and a darker tint while it is held down.
void drawPillButton(HDC dc, const RECT& rc, const wchar_t* label, HFONT labelFont,
                    bool hot, bool down, bool enabled) {
    COLORREF fill = !enabled ? RGB(0xF5, 0xF5, 0xF5)
                             : (down ? theme::kAccentBorder
                                     : (hot ? RGB(0xE3, 0xEC, 0xFD)
                                            : RGB(0xF2, 0xF2, 0xF2)));
    fillRoundRect(dc, rc, theme::kRadiusControl, fill);

    COLORREF text = enabled ? theme::kTableText : RGB(0xB0, 0xB0, 0xB0);
    if (enabled && down) text = theme::kTopBarText;
    else if (enabled && hot) text = theme::kAccentBorder;
    drawText(dc, label, rc, text, labelFont,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// The vertical steps the settings card is built from. They are named rather than
// inlined because the card's height is the sum of them: computing the height
// separately from the drawing is how a card ends up clipping its own last row.
//
// A row is laid out in two lines - the control, then a hint under it.
struct SettingsMetrics {
    int pad;
    int width;
    int title;      // heading, rule and the gap before the first row
    int row;        // a control row, including the gap to its hint
    int hint;       // one line of hint text
    int blockGap;   // the separator between two settings
    int rowGap;     // the gap between the hint and the row after it
    int path;       // the clickable path line
};

// The card and everything inside it scale in full - see theme::zoom. A dialog is
// a fixed piece of content, so the whole of it grows together; growing only the
// type would leave the hints wider than the card that holds them.
SettingsMetrics settingsMetrics() {
    SettingsMetrics m = {};
    m.pad = theme::zoom(24);
    m.width = theme::zoom(560);
    m.title = theme::zoom(38) + theme::zoom(18);
    m.row = theme::zoom(30);
    m.hint = theme::zoom(20);
    m.blockGap = theme::zoom(18);
    m.rowGap = theme::zoom(4);
    m.path = theme::zoom(22);
    return m;
}

// The settings card. Same frame as the about box; the body is a list of rows,
// each one a label, a control and a hint.
void paintSettings(HWND hwnd, HDC dc, const RECT& client) {
    AppState* s = state(hwnd);
    if (!s || !s->settingsVisible) return;

    dimClient(dc, client);

    const SettingsMetrics m = settingsMetrics();

    // Sized from its own content: two control rows, each followed by its hint,
    // and the path line under them. A larger text size grows the card instead of
    // clipping the last row. The +1 is the separator's hairline.
    const int cardH = m.pad * 2 + m.title + (m.row + m.hint) +
                      (m.blockGap * 2 + 1) + (m.row + m.hint) + m.rowGap + m.path;

    // Placed once and then held: see settingsCardTop. Growing the text size makes
    // the card taller, and the height is what changes, not the top.
    if (s->settingsCardTop < 0) {
        int top = (client.bottom - cardH) / 2;
        if (top < dpi::scale(8)) top = dpi::scale(8);
        // The tallest card the setting allows still has to fit the window.
        if (top + cardH > client.bottom - dpi::scale(8)) {
            top = client.bottom - dpi::scale(8) - cardH;
        }
        if (top < dpi::scale(8)) top = dpi::scale(8);
        s->settingsCardTop = top;
    }
    RECT card = {(client.right - m.width) / 2, s->settingsCardTop,
                 (client.right - m.width) / 2 + m.width,
                 s->settingsCardTop + cardH};

    RECT shadow = card;
    OffsetRect(&shadow, dpi::scale(3), dpi::scale(3));
    fillRoundRect(dc, shadow, theme::kRadiusCard, RGB(0xC8, 0xCC, 0xD2));
    fillRoundRect(dc, card, theme::kRadiusCard, RGB(0xFF, 0xFF, 0xFF));

    const int pad = m.pad;
    const int rowH = m.row;
    const int hintH = m.hint;
    int y = card.top + pad;

    RECT titleRc = {card.left + pad, y, card.right - pad - theme::zoom(36),
                    y + theme::zoom(28)};
    drawText(dc, L"设置", titleRc, theme::kPageTitle, s->titleFont);
    y += theme::zoom(38);

    RECT line = {card.left + pad, y, card.right - pad, y + 1};
    fillRect(dc, line, theme::kDivider);
    y += theme::zoom(18);

    // --- text size ---------------------------------------------------------
    {
        RECT labelRc = {card.left + pad, y, card.left + pad + theme::zoom(120),
                        y + rowH};
        drawText(dc, L"字体大小", labelRc, theme::kTableText, s->boldFont);

        // A stepper: minus, the value, plus. The row is right-aligned so the
        // controls sit under the switch on the row below.
        //
        // The buttons keep their distance from the pointer by being anchored to
        // the card's right edge and by the card's top being held while it is
        // open: growing the text grows the card downwards, so the row being
        // clicked does not move out from under the pointer.
        const int btn = theme::zoom(30);
        const int pctW = theme::zoom(64);
        const int gap = theme::zoom(8);
        RECT plusRc = {card.right - pad - btn, y + (rowH - btn) / 2,
                       card.right - pad, y + (rowH - btn) / 2 + btn};
        RECT pctRc = {plusRc.left - gap - pctW, y, plusRc.left - gap, y + rowH};
        RECT minusRc = {pctRc.left - gap - btn, plusRc.top, pctRc.left - gap,
                        plusRc.bottom};
        s->settingsRects[kSettingsScaleUp] = plusRc;
        s->settingsRects[kSettingsScaleDown] = minusRc;

        const bool canDown = theme::textScale() > theme::kTextScaleMin;
        const bool canUp = theme::textScale() < theme::kTextScaleMax;

        drawPillButton(dc, minusRc, L"-", s->boldFont,
                       s->settingsHot == kSettingsScaleDown,
                       s->settingsPressed == kSettingsScaleDown, canDown);
        drawPillButton(dc, plusRc, L"+", s->boldFont,
                       s->settingsHot == kSettingsScaleUp,
                       s->settingsPressed == kSettingsScaleUp, canUp);

        wchar_t value[16] = {0};
        std::swprintf(value, 16, L"%d%%", theme::textScale());
        drawText(dc, value, pctRc, theme::kTableText, s->boldFont,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // The reset button is offered only once the value has moved, so the row
        // stays quiet by default.
        if (theme::textScale() != 100) {
            const int resetW = theme::zoom(52);
            RECT resetRc = {minusRc.left - gap - resetW, minusRc.top,
                            minusRc.left - gap, minusRc.bottom};
            s->settingsRects[kSettingsScaleReset] = resetRc;
            drawPillButton(dc, resetRc, L"重置", s->font,
                           s->settingsHot == kSettingsScaleReset,
                           s->settingsPressed == kSettingsScaleReset, true);
        } else {
            s->settingsRects[kSettingsScaleReset] = {};
        }

        y += rowH;
        RECT hint = {card.left + pad, y, card.right - pad, y + hintH};
        drawText(dc, L"调整整个界面的文字大小，立即生效并记住选择。",
                 hint, theme::kPageSubtitle, s->font);
        y += hintH;
    }

    y += m.blockGap;
    {
        RECT sep = {card.left + pad, y, card.right - pad, y + 1};
        fillRect(dc, sep, theme::kDivider);
    }
    y += 1 + m.blockGap;

    // --- the debug log row -------------------------------------------------
    {
        RECT labelRc = {card.left + pad, y, card.left + pad + theme::zoom(120),
                        y + rowH};
        drawText(dc, L"调试日志", labelRc, theme::kTableText, s->boldFont);

        // A switch, drawn the way the rest of the shell draws its controls.
        // The hit rectangle is the whole row rather than the track alone: a
        // 44-pixel switch is a small target next to a label and a hint.
        const int swW = theme::zoom(44);
        const int swH = theme::zoom(24);
        RECT switchRc = {card.right - pad - swW, y + (rowH - swH) / 2,
                         card.right - pad, y + (rowH - swH) / 2 + swH};
        s->settingsRects[kSettingsLog] = {card.left + pad, y, card.right - pad,
                                          y + rowH};

        const bool on = applog::enabled();
        COLORREF track = on ? theme::kAccent
                            : (s->settingsHot == kSettingsLog
                                   ? RGB(0xD8, 0xD8, 0xD8)
                                   : RGB(0xE0, 0xE0, 0xE0));
        fillRoundRect(dc, switchRc, swH / 2, track);
        const int knob = swH - theme::zoom(6);
        const int knobX = on ? switchRc.right - theme::zoom(3) - knob
                             : switchRc.left + theme::zoom(3);
        RECT k = {knobX, switchRc.top + theme::zoom(3), knobX + knob,
                  switchRc.top + theme::zoom(3) + knob};
        fillRoundRect(dc, k, knob / 2, RGB(0xFF, 0xFF, 0xFF));

        y += rowH;

        // Hint, and the log path so the user knows where to look. The path line
        // is itself clickable and opens the folder.
        RECT hint = {card.left + pad, y, card.right - pad, y + hintH};
        drawText(dc,
                 on ? L"已启用。程序会把运行情况写入下面的文件，便于排查问题。"
                    : L"启用后会在程序所在目录持续写入日志，便于排查问题。",
                 hint, theme::kPageSubtitle, s->font);
        y += hintH + m.rowGap;
    }

    RECT pathRc = {card.left + pad, y, card.right - pad, y + m.path};
    s->settingsRects[kSettingsPath] = pathRc;
    {
        // The log is what the line names, so before it exists the line points at
        // the folder that would hold it rather than at a file that is not there.
        std::wstring shown = applog::path();
        drawText(dc, shown, pathRc,
                 s->settingsHot == kSettingsPath ? theme::kAccentBorder
                                                 : theme::kPageSubtitle,
                 s->font, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
    }

    // Close button, where the about box puts its own.
    const int closeSz = dpi::scale(28);
    s->aboutCloseRect = {card.right - pad - closeSz, card.top + pad,
                         card.right - pad, card.top + pad + closeSz};
    if (s->aboutCloseHot) {
        fillRoundRect(dc, s->aboutCloseRect, theme::kRadiusControl, theme::kNavHover);
    }
    {
        const int cx = (s->aboutCloseRect.left + s->aboutCloseRect.right) / 2;
        const int cy = (s->aboutCloseRect.top + s->aboutCloseRect.bottom) / 2;
        const int half = dpi::scale(5);
        HPEN pen = CreatePen(PS_SOLID, dpi::scale(1), theme::kSidebarMuted);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, cx - half, cy - half, nullptr);
        LineTo(dc, cx + half + 1, cy + half + 1);
        MoveToEx(dc, cx + half, cy - half, nullptr);
        LineTo(dc, cx - half - 1, cy + half + 1);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
}

// Every face here goes through theme::font(), which is the monitor's DPI and
// the user's text size together; that is what makes the setting reach the
// chrome as well as the table.
void createFonts(AppState* s) {
    const wchar_t* uiFont = L"Segoe UI Variable Text";
    const wchar_t* displayFont = L"Segoe UI Variable Display";
    s->font = CreateFontW(-theme::font(theme::kFontBody), 0, 0, 0, FW_NORMAL,
                          FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, uiFont);
    s->boldFont = CreateFontW(-theme::font(theme::kFontBody), 0, 0, 0, FW_SEMIBOLD,
                              FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, uiFont);
    s->captionFont = CreateFontW(-theme::font(theme::kFontCaption), 0, 0, 0, FW_NORMAL,
                                 FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, uiFont);
    s->titleFont = CreateFontW(-theme::font(theme::kFontSubtitle), 0, 0, 0, FW_SEMIBOLD,
                               FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, displayFont);
    s->brandFont = CreateFontW(-theme::font(18), 0, 0, 0, FW_SEMIBOLD,
                               FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, displayFont);
}

void deleteFonts(AppState* s) {
    if (s->font) DeleteObject(s->font);
    if (s->boldFont) DeleteObject(s->boldFont);
    if (s->captionFont) DeleteObject(s->captionFont);
    if (s->titleFont) DeleteObject(s->titleFont);
    if (s->brandFont) DeleteObject(s->brandFont);
}

void loadLogo(AppState* s) {
    if (s->logo) {
        DestroyIcon(s->logo);
        s->logo = nullptr;
    }
    int logoPx = dpi::scale(26);
    s->logo = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                                            MAKEINTRESOURCEW(102), IMAGE_ICON,
                                            logoPx, logoPx, LR_DEFAULTCOLOR));
}

void destroyLogo(AppState* s) {
    if (s->logo) {
        DestroyIcon(s->logo);
        s->logo = nullptr;
    }
}

void applyFonts(AppState* s) {
    HWND controls[] = {s->status};
    for (HWND control : controls) {
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(s->font), TRUE);
    }
}

void rebuildFonts(HWND hwnd) {
    AppState* s = state(hwnd);
    if (!s) return;
    deleteFonts(s);
    createFonts(s);
    loadLogo(s);
    applyFonts(s);
}

// Puts a new text size into effect everywhere at once: our own faces and the
// fonts the child controls own are separate sets, and the layout is recomputed
// because the row heights follow the type. Called after every change, so there
// is no path where the setting is stored but only half applied.
void applyTextScale(HWND hwnd) {
    AppState* s = state(hwnd);
    if (!s) return;

    // Order matters. The cache is pruned first so no face from the previous size
    // is handed out again; the list view is told about its new font before the
    // layout runs, because it derives its row height from it; only then is the
    // geometry recomputed.
    ui::resetFontCache();
    rebuildFonts(hwnd);
    ui::valueListApplyFont(s->valueList);
    layoutChildren(hwnd);
    // layoutChildren uses MoveWindow(..., TRUE), which repaints every child it
    // places, so the sidebar list, the search box and the status bar all pick up
    // their new faces without being named here.
    InvalidateRect(hwnd, nullptr, TRUE);
}

// Which settings row a point falls on, or kSettingsNone.
//
// The rectangles are recorded by the card as it draws, so a row that is not on
// screen - the reset button before the size has moved - has no rectangle and is
// never hit.
int settingsItemFromPoint(AppState* s, POINT pt) {
    if (!s || !s->settingsVisible) return kSettingsNone;
    for (int i = 0; i < kSettingsItemCount; ++i) {
        if (IsRectEmpty(&s->settingsRects[i])) continue;
        if (PtInRect(&s->settingsRects[i], pt)) return i;
    }
    return kSettingsNone;
}

// Applies one click on the settings card. Kept in one place so the press and the
// release path cannot drift apart about what a given row does.
void activateSettingsItem(HWND hwnd, int item) {
    AppState* s = state(hwnd);
    if (!s) return;

    // Only the three text-size rows change the type; the other two are ordinary
    // toggles and a full relayout would be wasted work on them.
    switch (item) {
        case kSettingsScaleDown:
            settings::setTextScale(theme::textScale() - theme::kTextScaleStep);
            applyTextScale(hwnd);
            return;
        case kSettingsScaleUp:
            settings::setTextScale(theme::textScale() + theme::kTextScaleStep);
            applyTextScale(hwnd);
            return;
        case kSettingsScaleReset:
            settings::resetTextScale();
            applyTextScale(hwnd);
            return;
        case kSettingsLog:
            applog::setEnabled(!applog::enabled());
            applog::write(L"设置：调试日志 %ls", applog::enabled() ? L"开" : L"关");
            break;
        case kSettingsPath: {
            // Opening the folder is the useful next step after turning logging
            // on, so the path line does that.
            std::wstring dir = applog::path();
            const size_t slash = dir.find_last_of(L"\\/");
            if (slash != std::wstring::npos) dir.erase(slash);
            ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr,
                          SW_SHOWNORMAL);
            return;
        }
        default:
            return;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppState* s = state(hwnd);
    switch (msg) {
        case WM_NCCALCSIZE: {
            // Remove the standard frame so the client area covers the whole
            // window; the caption is drawn by us inside the top bar.
            if (wParam) {
                NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                WINDOWPLACEMENT wp = {};
                wp.length = sizeof(wp);
                GetWindowPlacement(hwnd, &wp);
                if (wp.showCmd == SW_MAXIMIZE) {
                    // Keep the maximized window inside the monitor work area.
                    int frameX = GetSystemMetrics(SM_CXSIZEFRAME) +
                                 GetSystemMetrics(SM_CXPADDEDBORDER);
                    int frameY = GetSystemMetrics(SM_CYSIZEFRAME) +
                                 GetSystemMetrics(SM_CXPADDEDBORDER);
                    params->rgrc[0].left += frameX;
                    params->rgrc[0].right -= frameX;
                    params->rgrc[0].top += frameY;
                    params->rgrc[0].bottom -= frameY;
                }
                return 0;
            }
            break;
        }
        case WM_NCHITTEST: {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT rc;
            GetWindowRect(hwnd, &rc);

            bool maxed = IsZoomed(hwnd) != 0;
            int borderX = resizeBorder();
            int borderY = resizeBorder();

            bool left = pt.x < rc.left + borderX;
            bool right = pt.x >= rc.right - borderX;
            bool top = pt.y < rc.top + borderY;
            bool bottom = pt.y >= rc.bottom - borderY;

            if (!maxed) {
                if (top && left) return HTTOPLEFT;
                if (top && right) return HTTOPRIGHT;
                if (bottom && left) return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left) return HTLEFT;
                if (right) return HTRIGHT;
                if (top) return HTTOP;
                if (bottom) return HTBOTTOM;
            }

            POINT client = pt;
            ScreenToClient(hwnd, &client);
            if (client.y < theme::kTopBarHeight) {
                if (captionButtonFromPoint(hwnd, client) != kCaptionNone) return HTCLIENT;
                RECT searchBox = searchRect();
                if (PtInRect(&s->glSwitchRect, client) || PtInRect(&s->vkSwitchRect, client) ||
                    PtInRect(&searchBox, client) || PtInRect(&s->toolButtonRect, client)) {
                    return HTCLIENT;
                }
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        case WM_NCLBUTTONDOWN: {
            // Fall through to DefWindowProc for resize/drag handling.
            break;
        }
        case WM_NCACTIVATE: {
            // Keep the custom frame painted while active.
            return TRUE;
        }
        case WM_CREATE: {
            s = new AppState();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));

            createFonts(s);
            loadLogo(s);

            HINSTANCE inst = GetModuleHandleW(nullptr);

            s->search = ui::createSearchBox(hwnd, inst, ID_SEARCH);

            s->sectionList = CreateWindowExW(0, ui::kSectionListClass, L"",
                WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd,
                reinterpret_cast<HMENU>(ID_SECTION_LIST), inst, nullptr);

            s->valueList = ui::createValueList(hwnd, inst, ID_VALUE_LIST);
            ShowWindow(s->valueList, SW_HIDE);

    // A plain child window acts as the swap-chain target for the renderers.
    WNDCLASSW hostClass = {};
    hostClass.lpfnWndProc = DefWindowProcW;
    hostClass.hInstance = inst;
    hostClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    hostClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    hostClass.lpszClassName = L"TuxToolsRenderHost";
    RegisterClassW(&hostClass);

    // The detached host gets a class of its own. Reusing the one above fails
    // with ERROR_CLASS_DOES_NOT_EXIST: a class is registered against the instance
    // and the first registration wins, so the popup creation is rejected before
    // it is even attempted. A second class avoids the question entirely.
    WNDCLASSW popupClass = {};
    popupClass.lpfnWndProc = detachedHostProc;
    popupClass.hInstance = inst;
    popupClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    popupClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    popupClass.lpszClassName = L"TuxToolsRenderHostDetached";
    RegisterClassW(&popupClass);
    // The first host is always a child. A child window is the shape that belongs
    // in this window's layout, and if a backend turns out to need a popup, the
    // retry in ensureRenderer replaces this window; see detachWorks.
    s->renderHost = CreateWindowExW(0, hostClass.lpszClassName, L"",
        WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, hwnd,
        reinterpret_cast<HMENU>(ID_RENDER_HOST), inst, nullptr);
    ShowWindow(s->renderHost, SW_HIDE);

            s->status = CreateWindowExW(0, L"STATIC", L"",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS,
                0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_STATUS_TEXT), inst, nullptr);
            SendMessageW(s->status, WM_SETFONT, reinterpret_cast<WPARAM>(s->font), TRUE);

            // The decode test owns a video surface and a log; it is created up
            // front so switching to it is instant, and hidden until selected.
            s->decoder = new decode::DecodeTest();
            s->decoderReady = s->decoder->init(hwnd);

            layoutChildren(hwnd);
            updateToolVisibility(hwnd);
            loadCurrentApi(hwnd);
            return 0;
        }
        case WM_ACTIVATE: {
            // The detached host is topmost so that it stays above the window it
            // covers. That would also make it hang over other applications, so it
            // is hidden whenever this window is not the active one and brought
            // back when it is. Without this the canvas would follow the user into
            // whatever else they switched to.
            if (s && s->detachRenderHost && s->renderHost) {
                const bool active = LOWORD(wParam) != WA_INACTIVE;
                ShowWindow(s->renderHost, active ? SW_SHOWNOACTIVATE : SW_HIDE);
                if (active && s->renderRunning) layoutChildren(hwnd);
            }
            break;
        }
        case WM_MOVE: {
            // A popup is positioned in screen coordinates, so it has to be moved
            // whenever the window it sits over moves. layoutChildren recalculates
            // the placement from the client rectangle, which the move has just
            // changed.
            if (s && s->detachRenderHost) layoutChildren(hwnd);
            return 0;
        }
        case WM_SIZE: {
            // A resize may leave the anchored settings card off the screen, so
            // the next paint places it again from the new client size.
            if (s && s->settingsVisible) s->settingsCardTop = -1;
            layoutChildren(hwnd);
            if (s && s->tool == ToolKind::DriverTest && s->rendererReady && s->renderer) {
                s->renderer->resize(s->renderHostRect.right - s->renderHostRect.left,
                                    s->renderHostRect.bottom - s->renderHostRect.top);
            }
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case WM_TIMER: {
            // Rendering is driven by the main message loop, not a timer.
            return 0;
        }
        case WM_DPICHANGED: {
            theme::init(HIWORD(wParam));
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            rebuildFonts(hwnd);
            layoutChildren(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = 1000;
            mmi->ptMinTrackSize.y = 620;
            return 0;
        }
        case WM_NOTIFY: {
            // The value table's rows are painted from here.
            //
            // A control sends its custom draw to its parent, and the list view's
            // parent is this window - not the list itself. Its own subclass only
            // ever sees the header, which is a child of the list. Looking for the
            // rows there is why they were left to the system's default painter,
            // which clipped them into a stale twelve-pixel item rectangle and
            // produced the empty-looking table.
            NMHDR* hdr = reinterpret_cast<NMHDR*>(lParam);
            if (s && hdr && hdr->hwndFrom == s->valueList &&
                hdr->code == NM_CUSTOMDRAW) {
                return ui::valueListCustomDraw(s->valueList, hdr);
            }
            break;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);
            // A menu command arrives with no originating window; a control
            // notification carries one. That is what tells the two apart, since
            // the ids come from different ranges but are not disjoint.
            if (lParam == 0) {
                if (id >= kCmdToolCaps && id <= kCmdLast) runAppCommand(hwnd, id);
            } else if (id == ID_SEARCH && code == EN_CHANGE) {
                updateValueList(hwnd);
            } else if (id == ID_SECTION_LIST && code == LBN_SELCHANGE) {
                updateValueList(hwnd);
                updateStatus(hwnd);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!s) return 0;
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            bool changed = false;

            if (s->aboutVisible || s->settingsVisible) {
                const bool closeHot = PtInRect(&s->aboutCloseRect, pt) != 0;
                const bool linkHot = s->aboutVisible &&
                                     PtInRect(&s->aboutLinkRect, pt) != 0;

                // Which settings row the pointer is over, or kSettingsNone.
                const int settingsHot = settingsItemFromPoint(s, pt);

                // Which credited project the pointer is over, or -1. Each name
                // is a link, so each needs its own hover state.
                int creditHot = -1;
                if (s->aboutVisible) {
                    for (int i = 0; i < kCreditCount; ++i) {
                        if (!IsRectEmpty(&s->creditRects[i]) &&
                            PtInRect(&s->creditRects[i], pt)) {
                            creditHot = i;
                            break;
                        }
                    }
                }
                if (closeHot != s->aboutCloseHot || linkHot != s->aboutLinkHot ||
                    settingsHot != s->settingsHot || creditHot != s->creditHot) {
                    s->aboutCloseHot = closeHot;
                    s->aboutLinkHot = linkHot;
                    s->creditHot = creditHot;
                    s->settingsHot = settingsHot;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }

            int caption = captionButtonFromPoint(hwnd, pt);
            if (caption != s->captionHot) {
                s->captionHot = caption;
                changed = true;
            }

            int hot = -1;
            if (caption == kCaptionNone && s->tool == ToolKind::DriverCaps) {
                if (PtInRect(&s->glSwitchRect, pt)) hot = 0;
                else if (PtInRect(&s->vkSwitchRect, pt)) hot = 1;
            }
            if (hot != s->apiHot) {
                s->apiHot = hot;
                changed = true;
            }
            bool toolHot = PtInRect(&s->toolButtonRect, pt);
            if (toolHot != s->toolHot) {
                s->toolHot = toolHot;
                changed = true;
            }
            bool openHot = false;
            if (s->tool == ToolKind::DecodeTest) {
                openHot = PtInRect(&s->openButtonRect, pt) != 0;
                bool runHotDecode = PtInRect(&s->runButtonRect, pt) != 0;
                if (openHot != s->openButtonHot || runHotDecode != s->runButtonHot) {
                    s->openButtonHot = openHot;
                    s->runButtonHot = runHotDecode;
                    RECT header = {0, 0, 600000, theme::kTopBarHeight + theme::metric(64)};
                    InvalidateRect(hwnd, &header, FALSE);
                }
            }
            bool runHot = s->tool == ToolKind::DriverTest && PtInRect(&s->runButtonRect, pt);
            if (runHot != s->runButtonHot) {
                s->runButtonHot = runHot;
                changed = true;
            }
            if (changed) {
                RECT header = {0, 0, 600000, theme::kTopBarHeight};
                InvalidateRect(hwnd, &header, FALSE);
                RECT sidebarHeader = {0, theme::kTopBarHeight, theme::kSidebarWidth,
                                      s->glSwitchRect.bottom + theme::kSpaceM};
                InvalidateRect(hwnd, &sidebarHeader, FALSE);
                if (s->tool == ToolKind::DriverTest) {
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (s && (s->apiHot != -1 || s->captionHot != kCaptionNone || s->toolHot ||
                      s->runButtonHot || s->aboutCloseHot || s->aboutLinkHot ||
                      s->settingsHot != kSettingsNone || s->creditHot >= 0)) {
                s->aboutCloseHot = false;
                s->aboutLinkHot = false;
                s->creditHot = -1;
                s->settingsHot = kSettingsNone;
                s->apiHot = -1;
                s->captionHot = kCaptionNone;
                s->toolHot = false;
                s->runButtonHot = false;
                RECT header = {0, 0, 600000, theme::kTopBarHeight};
                InvalidateRect(hwnd, &header, FALSE);
                RECT sidebarHeader = {0, theme::kTopBarHeight, theme::kSidebarWidth,
                                      s->glSwitchRect.bottom + theme::kSpaceM};
                InvalidateRect(hwnd, &sidebarHeader, FALSE);
            }
            return 0;
        case WM_KEYDOWN: {
            if (!s) return 0;
            // F1 opens the about box and Esc dismisses it; both are also what a
            // keyboard user would reach for without a mouse.
            if (wParam == VK_F1) {
                setAboutVisible(hwnd, true);
                return 0;
            }
            if (wParam == VK_ESCAPE && (s->aboutVisible || s->settingsVisible)) {
                setAboutVisible(hwnd, false);
                setSettingsVisible(hwnd, false);
                return 0;
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            if (!s) return 0;
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            // The about overlay is modal: while it is up it swallows every
            // press, so nothing behind it can be operated by accident.
            if (s->aboutVisible || s->settingsVisible) {
                // The row is resolved from the point, not from the hovered row:
                // this runs on a press that may have been posted without a
                // preceding mouse-move, and a press has to land where it was
                // aimed regardless of what the hover state happens to say.
                const int item = settingsItemFromPoint(s, pt);
                if (item == kSettingsPath) {
                    // Opening the folder rather than changing a setting, so it
                    // acts on press and has no held-down state.
                    activateSettingsItem(hwnd, kSettingsPath);
                } else if (item != kSettingsNone) {
                    // A button or a switch: which one it is is decided on release,
                    // so that dragging off cancels the press the way the rest of
                    // the shell behaves.
                    s->settingsPressed = item;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (s->aboutVisible && PtInRect(&s->aboutLinkRect, pt)) {
                    openRepository();
                } else if (s->aboutVisible && s->creditHot >= 0 &&
                           s->creditHot < kCreditCount) {
                    // A credited project was clicked; open its repository.
                    openUrl(kCredits[s->creditHot].url);
                } else if (!PtInRect(&s->aboutCloseRect, pt)) {
                    setAboutVisible(hwnd, false);
                    setSettingsVisible(hwnd, false);
                }
                return 0;
            }
            int caption = captionButtonFromPoint(hwnd, pt);
            if (caption != kCaptionNone) {
                s->captionPressed = caption;
                SetCapture(hwnd);
                RECT header = {0, 0, 600000, theme::kTopBarHeight};
                InvalidateRect(hwnd, &header, FALSE);
                return 0;
            }
            if (PtInRect(&s->toolButtonRect, pt)) {
                s->toolPressed = true;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (s->tool == ToolKind::DriverTest && PtInRect(&s->runButtonRect, pt)) {
                s->runButtonPressed = true;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (s->tool == ToolKind::DecodeTest) {
                if (PtInRect(&s->openButtonRect, pt)) {
                    s->openButtonPressed = true;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (PtInRect(&s->runButtonRect, pt)) {
                    s->runButtonPressed = true;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            // The OpenGL / Vulkan switch belongs to the capability viewer only.
            // The other tools draw a heading over the same rectangle, so without
            // this guard a click there would silently retarget the viewer.
            if (s->tool == ToolKind::DriverCaps) {
                if (PtInRect(&s->glSwitchRect, pt)) selectApi(hwnd, Api::OpenGL);
                else if (PtInRect(&s->vkSwitchRect, pt)) selectApi(hwnd, Api::Vulkan);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (!s) return 0;
            if (s->aboutVisible || s->settingsVisible) {
                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (s->settingsPressed != kSettingsNone) {
                    const int pressed = s->settingsPressed;
                    s->settingsPressed = kSettingsNone;
                    ReleaseCapture();
                    // Releasing over the control that was pressed is what
                    // commits it; letting go anywhere else is a cancel.
                    if (pressed < kSettingsItemCount &&
                        PtInRect(&s->settingsRects[pressed], pt)) {
                        activateSettingsItem(hwnd, pressed);
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (PtInRect(&s->aboutCloseRect, pt)) {
                    setAboutVisible(hwnd, false);
                    setSettingsVisible(hwnd, false);
                }
                return 0;
            }
            if (s->openButtonPressed) {
                s->openButtonPressed = false;
                ReleaseCapture();
                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (PtInRect(&s->openButtonRect, pt) && s->decoder) {
                    s->decoder->chooseFile(hwnd);
                    updateStatus(hwnd);
                }
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            if (s->runButtonPressed) {
                s->runButtonPressed = false;
                ReleaseCapture();
                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (PtInRect(&s->runButtonRect, pt)) {
                    if (s->tool == ToolKind::DecodeTest) {
                        if (!s->decoder) return 0;
                        if (s->decoder->running()) {
                            s->decoder->stop();
                        } else {
                            s->decoder->start();
                        }
                        updateStatus(hwnd);
                    } else if (s->renderRunning) {
                        stopRender(hwnd);
                    } else {
                        startRender(hwnd);
                    }
                }
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            if (s->toolPressed) {
                s->toolPressed = false;
                ReleaseCapture();
                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (PtInRect(&s->toolButtonRect, pt)) {
                    showToolMenu(hwnd);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (s->captionPressed != kCaptionNone) {
                int pressed = s->captionPressed;
                s->captionPressed = kCaptionNone;
                ReleaseCapture();
                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (captionButtonFromPoint(hwnd, pt) == pressed) {
                    if (pressed == kCaptionMin) {
                        ShowWindow(hwnd, SW_MINIMIZE);
                    } else if (pressed == kCaptionMax) {
                        ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
                    } else {
                        PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    }
                }
                RECT header = {0, 0, 600000, theme::kTopBarHeight};
                InvalidateRect(hwnd, &header, FALSE);
            }
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, theme::kStatusText);
            SetBkColor(dc, theme::kStatusBg);
            static HBRUSH statusBrush = CreateSolidBrush(theme::kStatusBg);
            return reinterpret_cast<LRESULT>(statusBrush);
        }
        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            fillRect(dc, rc, theme::kContentBg);
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            HDC memDc = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);

            fillRect(memDc, rc, theme::kContentBg);
            paintContent(hwnd, memDc, rc);
            paintSidebarHeader(hwnd, memDc);
            paintTopBar(hwnd, memDc, rc);
            paintStatusBar(hwnd, memDc);
            paintAbout(hwnd, memDc, rc);
            paintSettings(hwnd, memDc, rc);

            BitBlt(dc, 0, 0, rc.right, rc.bottom, memDc, 0, 0, SRCCOPY);
            SelectObject(memDc, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(memDc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY: {
            if (s) {
                destroyRenderer(hwnd);
                if (s->decoder) {
                    s->decoder->shutdown();
                    delete s->decoder;
                    s->decoder = nullptr;
                }
                destroyLogo(s);
                deleteFonts(s);
                delete s;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace


int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmdLine, int showCmd) {
    // DirectShow and Media Foundation are both COM based, and neither does its
    // own apartment setup. Without this the very first CoCreateInstance fails
    // with CO_E_NOTINITIALIZED and the decode test reports that it cannot open
    // any file at all. STA is what DirectShow expects; the Media Foundation
    // source reader is happy either way.
    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // The log is opened before anything else so that a failure during startup is
    // itself recorded. Nothing is written when the option is off.
    applog::init();
    // The saved text size is applied before the window exists so the first frame
    // is already drawn at the size the user chose, instead of opening at the
    // design size and jumping.
    settings::init();
    applog::write(L"===== Tux-Tools 启动 =====");
    applog::write(L"界面文字大小：%d%%", theme::textScale());
    // One block of machine facts up front. On a machine that needs diagnosing
    // this saves running several separate commands there.
    applog::writeEnvironmentSnapshot();
    // Installed once logging is on, so a crash is recorded rather than just ending
    // the file.
    applog::installCrashHandler();
    applog::write(L"命令行：%ls", GetCommandLineW());
    applog::write(L"COM 初始化：0x%08lx", static_cast<unsigned long>(comInit));
    {
        OSVERSIONINFOW os = {};
        os.dwOSVersionInfoSize = sizeof(os);
        // GetVersionEx lies without a manifest opt-in, so read the build number
        // from the file version of kernel32 instead - it is what tells Wine apart
        // from Windows and one Wine build from another.
        std::wstring build = L"未知";
        wchar_t sys[MAX_PATH] = {0};
        if (GetSystemDirectoryW(sys, MAX_PATH)) {
            const std::wstring k32 = std::wstring(sys) + L"\\kernel32.dll";
            DWORD dummy = 0;
            const DWORD size = GetFileVersionInfoSizeW(k32.c_str(), &dummy);
            if (size > 0) {
                std::vector<BYTE> data(size);
                if (GetFileVersionInfoW(k32.c_str(), 0, size, data.data())) {
                    VS_FIXEDFILEINFO* info = nullptr;
                    UINT len = 0;
                    if (VerQueryValueW(data.data(), L"\\",
                                       reinterpret_cast<void**>(&info), &len) &&
                        info) {
                        wchar_t buf[64];
                        std::swprintf(buf, 64, L"%u.%u.%u.%u",
                                      HIWORD(info->dwFileVersionMS),
                                      LOWORD(info->dwFileVersionMS),
                                      HIWORD(info->dwFileVersionLS),
                                      LOWORD(info->dwFileVersionLS));
                        build = buf;
                    }
                }
            }
        }
        // kernel32's file version reads like "6.2.26100.x" on Windows 11
        // because the first two fields are frozen for compatibility. RtlGetVersion
        // gives the real build, and the two together identify the system well
        // enough to tell Windows from Wine and one Wine build from another.
        std::wstring real = L"未知";
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll) {
            typedef LONG(WINAPI * RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
            auto fn = reinterpret_cast<RtlGetVersionFn>(
                reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
            RTL_OSVERSIONINFOW vi = {};
            vi.dwOSVersionInfoSize = sizeof(vi);
            if (fn && fn(&vi) == 0) {
                wchar_t buf[96];
                std::swprintf(buf, 96, L"%u.%u build %u", vi.dwMajorVersion,
                              vi.dwMinorVersion, vi.dwBuildNumber);
                real = buf;
            }
        }
        applog::write(L"系统版本：%ls（kernel32 %ls）", real.c_str(), build.c_str());
        applog::write(L"处理器架构：%ls",
#if defined(_WIN64)
                      L"x86_64",
#else
                      L"x86",
#endif
                      L"");
    }
    // An optional media path may be given on the command line. It selects the
    // decode test, loads the file and starts the default method, which is what
    // makes the tool scriptable and what the automated checks drive. With no
    // argument the app opens on the capability viewer as usual.
    (void)cmdLine;
    std::wstring startupFile;
    int startupMethod = -1;
    int startupDriver = -1;   // DxApi index when --driver was given
    int startupDriverRun = 0; // whether to press start as well
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            // Accepted forms:
            //   <file>                     decode test, default method
            //   <method> <file>            decode test, method 0..5
            //   --driver <api> [run]       driver test, api 0..5
            int first = 1;
            if (argc >= 3 && !wcscmp(argv[1], L"--driver")) {
                wchar_t* end = nullptr;
                const long v = wcstol(argv[2], &end, 10);
                if (end && *end == 0 && v >= 0 && v < 6) {
                    startupDriver = static_cast<int>(v);
                    startupDriverRun = (argc >= 4 && !wcscmp(argv[3], L"run")) ? 1 : 0;
                }
            } else if (argc >= 3) {
                wchar_t* end = nullptr;
                const long v = wcstol(argv[1], &end, 10);
                if (end && *end == 0 && v >= 0 && v < 6) {
                    startupMethod = static_cast<int>(v);
                    first = 2;
                }
            }
            if (argc > first && startupDriver < 0) startupFile = argv[first];
            LocalFree(argv);
        }
    }
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    ui::registerControlClasses(instance);

    WNDCLASSW wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClass;
    wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101),
                                             IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
    RegisterClassW(&wc);

    // Resolve the system DPI before creating any window so that fonts and
    // metrics are built at the correct scale from the start.
    theme::init(dpi::querySystemDpi());

    HWND hwnd = CreateWindowExW(0, kWindowClass, kWindowTitle,
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                dpi::scale(1140), dpi::scale(760),
                                nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;

    // --driver switches to the driver test on a named API, which is what makes
    // the six backends scriptable: a log per backend can be produced without
    // driving the sidebar by hand.
    if (startupDriver >= 0) {
        AppState* s = state(hwnd);
        if (s) {
            // --driver only chooses which backend to start. How the window is
            // chosen is the renderer's business and is decided by what works, so
            // there is nothing to set here.
            selectTool(hwnd, ToolKind::DriverTest);
            s->dxApi = static_cast<DxApi>(startupDriver);
            ui::sectionListSetSelection(s->sectionList, startupDriver);
            updateValueList(hwnd);
            if (startupDriverRun) startRender(hwnd);
        }
    }

    // A path on the command line switches to the decode test and starts it.
    // This happens after CreateWindow so the child surfaces exist; loading the
    // file goes through the same call the "打开文件" button uses.
    if (!startupFile.empty()) {
        AppState* s = state(hwnd);
        if (s) {
            selectTool(hwnd, ToolKind::DecodeTest);
            if (s->decoder) {
                if (startupMethod >= 0) {
                    s->decoder->setMethod(static_cast<decode::Method>(startupMethod));
                    ui::sectionListSetSelection(s->sectionList, startupMethod);
                }
                s->decoder->setFile(startupFile);
                std::wstring base = startupFile;
                const size_t slash = base.find_last_of(L"\\/");
                if (slash != std::wstring::npos) base.erase(0, slash + 1);
                SetWindowTextW(hwnd, (std::wstring(kWindowTitle) + L" - " + base).c_str());
                s->decoder->start();
                updateStatus(hwnd);
            }
        }
    }

    // Fine-tune to the monitor the window landed on.
    theme::init(dpi::queryWindowDpi(hwnd));

    // Drop the standard caption so the window has no system title bar; the
    // top bar drawn by the app acts as the caption.
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style &= ~WS_CAPTION;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                 SWP_FRAMECHANGED);

    // Rounded window corners and dark-safe caption (Windows 11).
    DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    BOOL darkCaption = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &darkCaption,
                          sizeof(darkCaption));

    ShowWindow(hwnd, showCmd);
    UpdateWindow(hwnd);

    MSG msg;
    bool running = true;
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;

        // Pump the active renderer as fast as it will go. The frame rate is
        // deliberately not capped: the point of the driver test is to let the
        // device run the API flat out, and the status bar reports what it
        // actually managed.
        AppState* s = state(hwnd);
        if (s && s->tool == ToolKind::DriverTest && s->renderRunning &&
            s->rendererReady && s->renderer) {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            const double freq = static_cast<double>(s->perfFrequency.QuadPart);
            const double elapsed =
                freq > 0.0
                    ? static_cast<double>(now.QuadPart - s->renderStart.QuadPart) / freq
                    : 0.0;

            s->renderer->render(elapsed);

            ++s->framesInWindow;
            const double window =
                freq > 0.0
                    ? static_cast<double>(now.QuadPart - s->fpsWindowStart.QuadPart) / freq
                    : 0.0;
            if (window >= 0.25) {
                s->currentFps = static_cast<double>(s->framesInWindow) / window;
                s->framesInWindow = 0;
                s->fpsWindowStart = now;
                updateStatus(hwnd);
            }
        } else if (s && s->tool == ToolKind::DecodeTest && s->decoder) {
            // The decode test advances on its own schedule: Media Foundation is
            // pull-based and needs a frame asked for, DirectShow only needs its
            // progress read. Either way the loop keeps spinning, so a short sleep
            // keeps this from eating a core the way the driver test deliberately
            // does.
            s->decoder->tick();
            const std::wstring text = s->decoder->statusText();
            if (text != s->lastDecodeStatus) {
                s->lastDecodeStatus = text;
                if (!s->aboutVisible) updateStatus(hwnd);
            }
            // While the about overlay is up it owns the client area, so the log
            // panel must not be repainted underneath it; a partial invalidate
            // would redraw just that rectangle and paint over the card.
            if (!s->aboutVisible) InvalidateRect(hwnd, &s->logRect, FALSE);
            Sleep(8);
        } else {
            // Idle: wait for messages without burning CPU.
            GetMessageW(&msg, nullptr, 0, 0);
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    applog::write(L"主循环结束，准备退出");
    applog::shutdown();
    if (SUCCEEDED(comInit)) CoUninitialize();
    return static_cast<int>(msg.wParam);
}
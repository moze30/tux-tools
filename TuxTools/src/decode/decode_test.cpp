/*
 * Tux-Tools - decode test implementation.
 *
 * Owns the two child surfaces (video and log), the file picker and the
 * dispatch into whichever decoder the sidebar selected.
 */

#include "decode_test.h"

#include "directshow_decoder.h"
#include "mf_decoder.h"
#include "d3d11_video_helper.h"
#include "d3d12_video_helper.h"
#include "log.h"

#include "../theme.h"
#include "../dpi.h"

#include <commdlg.h>

#include <cstdio>
#include <cstring>

namespace decode {

namespace {

// The video surface is a plain child window. DirectShow renders into it
// through a renderer filter, and Media Foundation hands it buffers to present;
// the class needs no behaviour of its own, only a stable window to target and
// a black background while nothing is playing.
const wchar_t* const kVideoClass = L"TuxToolsDecodeVideo";

// Media Foundation is a frame-at-a-time pull API. 33 ms matches the upstream
// project and gives roughly 30 fps, which is what the source reader is asked
// for; the decoder itself is what decides how fast frames actually come back.
constexpr UINT kMfIntervalMs = 33;

// DirectShow plays on its own thread, so this one only refreshes the readouts.
constexpr UINT kDsIntervalMs = 100;

// The renderer name a decoder reports before it has started.
bool g_videoClassReady = false;

void fillRectLocal(HDC dc, const RECT& rc, COLORREF colour) {
    HBRUSH brush = CreateSolidBrush(colour);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
}

void frameRectLocal(HDC dc, const RECT& rc, COLORREF colour) {
    HBRUSH brush = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    HPEN pen = CreatePen(PS_SOLID, 1, colour);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
}

// A fixed-pitch face for the log: decoder messages are columns of hex and
// dimensions, which are far easier to scan when the characters line up.
HFONT monoFont() {
    static HFONT font = nullptr;
    if (!font) {
        font = CreateFontW(-dpi::scale(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    }
    return font;
}

void ensureVideoClass(HINSTANCE inst) {
    if (g_videoClassReady) return;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = inst;
    // A black brush is what makes the letterbox around a video black rather
    // than the parent's white, which would flash on every resize.
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kVideoClass;
    RegisterClassW(&wc);
    g_videoClassReady = true;
}

std::wstring formatTime(double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    const int total = static_cast<int>(seconds);
    wchar_t buf[64];
    std::swprintf(buf, 64, L"%02d:%02d", total / 60, total % 60);
    return buf;
}

std::wstring formatHnsTime(long long hns) {
    return formatTime(static_cast<double>(hns) / 10000000.0);
}

}  // namespace

// --- log -------------------------------------------------------------------

void DecodeTest::appendLog(const wchar_t* text) {
    if (!text) return;

    // The cores send CRLF-terminated chunks, sometimes several lines at once.
    std::wstring chunk(text);
    size_t start = 0;
    while (start <= chunk.size()) {
        size_t end = chunk.find(L'\n', start);
        std::wstring line = (end == std::wstring::npos)
                                ? chunk.substr(start)
                                : chunk.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (!line.empty()) log_.push_back(line);
        if (end == std::wstring::npos) break;
        start = end + 1;
    }

    // The upstream log was a scrolling control; here the panel shows the tail,
    // so old lines are trimmed rather than kept forever.
    constexpr size_t kMaxLines = 500;
    if (log_.size() > kMaxLines) {
        log_.erase(log_.begin(), log_.begin() + (log_.size() - kMaxLines));
    }
    logDirty_ = true;

    // Anything the decoders say about the current attempt belongs in the status
    // bar too, which is the one line the user reads without scrolling.
    if (log_.size() > 1000) log_.erase(log_.begin(), log_.begin() + 500);
}

void DecodeTest::clearLog() {
    log_.clear();
    logDirty_ = true;
}

namespace {

// The cores call this; it forwards into whichever instance is running. There is
// only ever one decode test, so a file-scope pointer is enough and keeps the
// cores' plain C callback signature working.
DecodeTest* g_active = nullptr;

void logThunk(const wchar_t* text) {
    if (g_active) g_active->appendLog(text);
}

}  // namespace
// --- window plumbing --------------------------------------------------------

bool DecodeTest::ensureChildWindows() {
    if (video_ && IsWindow(video_)) return true;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    ensureVideoClass(inst);

    video_ = CreateWindowExW(WS_EX_CLIENTEDGE, kVideoClass, L"",
                             WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                             0, 0, 0, 0, parent_,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(9001)),
                             inst, nullptr);
    return video_ != nullptr;
}

bool DecodeTest::init(HWND parent) {
    parent_ = parent;
    g_active = this;
    DecodeLog_SetSink(logThunk);
    return ensureChildWindows();
}

void DecodeTest::shutdown() {
    stop();
    DecodeLog_SetSink(nullptr);
    if (g_active == this) g_active = nullptr;
    if (video_ && IsWindow(video_)) {
        DestroyWindow(video_);
    }
    video_ = nullptr;
}

void DecodeTest::show(bool visible) {
    if (video_ && IsWindow(video_)) {
        ShowWindow(video_, visible ? SW_SHOW : SW_HIDE);
    }
}

void DecodeTest::layout(const RECT& client, int left, int top, int width,
                        int height) {
    (void)client;
    if (!ensureChildWindows()) return;
    if (width < 0) width = 0;
    if (height < 0) height = 0;
    MoveWindow(video_, left, top, width, height, TRUE);

    // DirectShow keeps its renderer filter on the window and follows a resize by
    // itself. The Media Foundation paths that present through D3D build their
    // swap chain once, at the size they were initialised with, so a resize is
    // only fully honoured after the next start; the video is letterboxed rather
    // than stretched in the meantime.
    if (running_ && isDirectShow(method_)) {
        ds_resize(0, 0, width, height);
    }
}

void DecodeTest::setMethod(Method m) {
    if (method_ == m) return;
    stop();
    method_ = m;
    status_.clear();
    appendLog(L"");
    Log_Printf(L"[选择] %ls", methodName(m));
}

// --- file selection ---------------------------------------------------------

bool DecodeTest::chooseFile(HWND owner) {
    wchar_t path[MAX_PATH] = {0};
    if (!filePath_.empty()) {
        wcsncpy_s(path, MAX_PATH, filePath_.c_str(), _TRUNCATE);
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter =
        L"媒体文件\0*.mp4;*.mkv;*.avi;*.wmv;*.mov;*.mpg;*.mpeg;*.ts;*.m2ts;"
        L"*.webm;*.flv;*.rmvb;*.m4v;*.3gp;*.mp3;*.aac;*.flac;*.wav;*.wma\0"
        L"所有文件\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"选择要测试解码的媒体文件";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) return false;

    stop();
    filePath_ = path;
    Log_Printf(L"[文件] %ls", path);
    status_ = std::wstring(L"已选择: ") + path;
    return true;
}

// --- playback ---------------------------------------------------------------

void DecodeTest::start() {
    if (running_) return;
    if (filePath_.empty()) {
        Log_Printf(L"[错误] 请先选择一个媒体文件");
        status_ = L"未选择文件";
        return;
    }

    if (!ensureChildWindows()) return;

    RECT rc;
    GetClientRect(video_, &rc);
    int w = rc.right > 0 ? rc.right : 1920;
    int h = rc.bottom > 0 ? rc.bottom : 1080;

    const wchar_t* name = methodName(method_);
    Log_Printf(L"");
    Log_Printf(L"========== %ls ==========", name);

    int ret = 0;
    switch (method_) {
        case Method::DirectShowSoftware:
            ret = ds_open(filePath_.c_str(), video_);
            if (ret == 0) ret = ds_play();
            break;

        case Method::DirectShowDxva2:
            ret = ds_open_dxva2(filePath_.c_str(), video_, 1);
            if (ret == 0) ret = ds_play();
            break;

        case Method::MediaFoundationSoftware:
            ret = mf_open(filePath_.c_str(), video_, 0);
            break;

        case Method::MediaFoundationDxva2:
            ret = mf_open(filePath_.c_str(), video_, 1);
            break;

        case Method::MediaFoundationD3D11:
            if (d3d11_video_check_support()) {
                d3d11_video_init(video_, w, h);
            } else {
                Log_Printf(L"[警告] 本机不支持 D3D11 视频处理，回退到软件解码");
            }
            ret = mf_open(filePath_.c_str(), video_, 2);
            break;

        case Method::MediaFoundationD3D12:
            if (d3d12_video_check_support()) {
                d3d12_video_init(video_, w, h);
            } else {
                Log_Printf(L"[警告] 本机不支持 D3D12 视频处理，回退到软件解码");
            }
            ret = mf_open(filePath_.c_str(), video_, 3);
            break;
    }

    if (ret != 0) {
        Log_Printf(L"[失败] %ls 无法打开该文件", name);
        status_ = std::wstring(name) + L" 打开失败";
        stop();
        return;
    }

    running_ = true;
    lastTick_ = GetTickCount64();
    updateStatusFromDecoders();
}

void DecodeTest::stop() {
    if (!running_) {
        // Even when nothing is running the helpers may hold a device from a
        // previous attempt; the cores tolerate a stop they did not start.
        ds_stop();
        mf_stop();
        d3d11_video_cleanup();
        d3d12_video_cleanup();
        return;
    }

    ds_stop();
    mf_stop();
    d3d11_video_cleanup();
    d3d12_video_cleanup();
    running_ = false;
}
// --- per-frame work ---------------------------------------------------------

void DecodeTest::updateStatusFromDecoders() {
    if (isDirectShow(method_)) {
        const int w = ds_get_video_width();
        const int h = ds_get_video_height();
        const std::wstring renderer = ds_get_renderer_name() ? ds_get_renderer_name() : L"-";
        const bool hw = ds_is_using_dxva2() != 0;

        std::wstring s = std::wstring(methodName(method_)) + L" | " + renderer;
        if (ds_has_video()) {
            wchar_t buf[128];
            std::swprintf(buf, 128, L" | %dx%d", w, h);
            s += buf;
        } else {
            s += L" | 仅音频";
        }
        s += L" | ";
        s += hw ? L"硬件加速" : L"软件解码";
        s += L" | " + formatTime(ds_get_position()) + L" / " + formatTime(ds_get_duration());
        status_ = s;
        return;
    }

    const int w = mf_get_width();
    const int h = mf_get_height();
    const wchar_t* renderer = mf_get_renderer_name();
    const wchar_t* codec = mf_get_video_codec();
    const bool hw = mf_is_using_dxva2() != 0;

    std::wstring s = std::wstring(methodName(method_)) + L" | ";
    s += renderer ? renderer : L"-";
    if (mf_has_video()) {
        wchar_t buf[192];
        const double fps = mf_get_video_fps();
        const UINT32 vbr = mf_get_video_bitrate();
        if (fps > 0.0 && vbr > 0) {
            std::swprintf(buf, 192, L" | %ls %dx%d %.1ffps %ukbps",
                          codec ? codec : L"?", w, h, fps, vbr / 1000);
        } else {
            std::swprintf(buf, 192, L" | %ls %dx%d", codec ? codec : L"?", w, h);
        }
        s += buf;
    } else {
        s += L" | 仅音频";
    }
    s += L" | ";
    s += hw ? L"硬件加速" : L"软件解码";
    s += L" | " + formatHnsTime(mf_get_position()) + L" / " + formatHnsTime(mf_get_duration());

    // Dropped frames are the number that actually says whether a decoder is
    // keeping up, so it goes in the one line the user reads.
    const int dropped = mf_get_dropped_frames();
    if (dropped > 0) {
        wchar_t buf[64];
        std::swprintf(buf, 64, L" | 丢帧 %d", dropped);
        s += buf;
    }
    status_ = s;
}

void DecodeTest::tick() {
    if (!running_) return;

    const ULONGLONG now = GetTickCount64();

    if (isDirectShow(method_)) {
        if (now - lastTick_ < kDsIntervalMs) return;
        lastTick_ = now;

        // DirectShow runs its own graph thread; the only thing to notice here
        // is the end of the stream.
        if (!ds_is_playing()) {
            Log_Printf(L"[完成] 播放结束");
            stop();
            return;
        }
        updateStatusFromDecoders();
        return;
    }

    if (now - lastTick_ < kMfIntervalMs) return;
    lastTick_ = now;

    // Media Foundation is pull-based: one call decodes and presents one frame.
    const int ret = mf_render_next_frame();
    if (ret == 1) {
        Log_Printf(L"[完成] 播放结束");
        stop();
        return;
    }
    if (ret == -1) {
        Log_Printf(L"[错误] 解码失败");
        stop();
        return;
    }
    updateStatusFromDecoders();
}

// --- drawing ----------------------------------------------------------------

void DecodeTest::paint(HDC dc, const RECT& area) const {
    // The log panel: a light well with a hairline border, matching the value
    // table the capability viewer draws. Only the tail is shown, anchored to
    // the bottom so the newest line is always the one nearest the eye.
    const int pad = theme::kSpaceM;
    RECT inner = area;
    InflateRect(&inner, -pad, -pad);
    if (inner.right <= inner.left || inner.bottom <= inner.top) return;

    fillRectLocal(dc, inner, RGB(0xFA, 0xFA, 0xFA));
    frameRectLocal(dc, inner, theme::kDivider);

    HFONT font = monoFont();
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, theme::kTableText);

    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    const int lineH = tm.tmHeight + 2;
    const int maxLines = (inner.bottom - inner.top - 8) / lineH;

    int first = static_cast<int>(log_.size()) - maxLines;
    if (first < 0) first = 0;

    int y = inner.top + 4;
    for (size_t i = static_cast<size_t>(first); i < log_.size(); ++i) {
        const std::wstring& line = log_[i];
        // Colour the prefixes the decoders use so failures stand out without
        // needing to read every line.
        COLORREF colour = theme::kTableText;
        if (line.rfind(L"[错误]", 0) == 0 || line.rfind(L"[失败]", 0) == 0) {
            colour = RGB(0xC6, 0x28, 0x28);
        } else if (line.rfind(L"[警告]", 0) == 0) {
            colour = RGB(0xE6, 0x7E, 0x22);
        } else if (line.rfind(L"==========", 0) == 0) {
            colour = theme::kAccentBorder;
        }
        SetTextColor(dc, colour);
        RECT r = {inner.left + 6, y, inner.right - 6, y + lineH};
        DrawTextW(dc, line.c_str(), static_cast<int>(line.size()), &r,
                  DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        y += lineH;
        if (y > inner.bottom) break;
    }

    SelectObject(dc, oldFont);
}

}  // namespace decode

/*
 * Tux-Tools - decode test
 *
 * The third tool: play a media file through each of Windows' video decoding
 * paths and report what each one actually did. The decoding itself is the
 * five sources in this directory, taken from DSMF-Decode-Test (GPL-3.0) and
 * used unmodified; what lives here is the shell around them.
 *
 * The six paths are:
 *
 *   DirectShow            software decoding through a filter graph
 *   DirectShow + DXVA2    the same graph with hardware acceleration
 *   Media Foundation      IMFSourceReader with software decoding
 *   MF + DXVA2            hardware decoding through the D3D9 manager
 *   MF + D3D11            frames presented by a D3D11 video processor
 *   MF + D3D12            frames presented by a D3D12 video processor
 *
 * The distinction is the point of the tool: on one machine these six can
 * differ by an order of magnitude in frame rate, and some can fail outright.
 */

#pragma once

#include <windows.h>

#include "log.h"

#include <string>
#include <vector>
#include "i18n.h"

namespace decode {

// The six decoding paths, in the order they appear in the sidebar.
enum class Method {
    DirectShowSoftware = 0,
    DirectShowDxva2 = 1,
    MediaFoundationSoftware = 2,
    MediaFoundationDxva2 = 3,
    MediaFoundationD3D11 = 4,
    MediaFoundationD3D12 = 5,
};

inline const wchar_t* methodName(Method m) {
    switch (m) {
        case Method::DirectShowSoftware: return L"DirectShow";
        case Method::DirectShowDxva2: return L"DirectShow + DXVA2";
        case Method::MediaFoundationSoftware: return L"Media Foundation";
        case Method::MediaFoundationDxva2: return L"Media Foundation + DXVA2";
        case Method::MediaFoundationD3D11: return L"Media Foundation + D3D11";
        case Method::MediaFoundationD3D12: return L"Media Foundation + D3D12";
    }
    return L"";
}

inline bool isDirectShow(Method m) {
    return m == Method::DirectShowSoftware || m == Method::DirectShowDxva2;
}

// Everything the decode test owns: the video surface, the log, the media file
// and whichever decoder is currently open.
class DecodeTest {
public:
    bool init(HWND parent);
    void shutdown();

    // Placement of the two child surfaces, in the parent's client coordinates.
    void layout(const RECT& client, int left, int top, int width, int height);

    void show(bool visible);

    // The sidebar selection changed: stop whatever is playing.
    void setMethod(Method m);
    Method method() const { return method_; }

    // Opens a file picker and loads the chosen path. Returns false when the
    // user cancels.
    bool chooseFile(HWND owner);
    const std::wstring& filePath() const { return filePath_; }

    // Loads a path without going through the file picker, which is what the
    // command-line entry point uses.
    void setFile(const std::wstring& path) {
        stop();
        filePath_ = path;
        Log_Printf(i18n::tr(L"[文件] %ls"), path.c_str());
        status_ = std::wstring(i18n::tr(L"已选择: ")) + path;
    }

    // Starts the selected method on the selected file.
    void start();
    void stop();
    bool running() const { return running_; }

    // Called from the parent's message loop. Drives Media Foundation, which
    // needs a frame pumped at a time, and refreshes DirectShow's progress.
    void tick();

    // One line for the status bar.
    std::wstring statusText() const { return status_; }

    // The log panel is drawn by us, so the parent paints it.
    void paint(HDC dc, const RECT& area) const;
    void clearLog();

    // A log line arrived from one of the decoder cores.
    void appendLog(const wchar_t* text);

    HWND videoWindow() const { return video_; }

private:
    bool ensureChildWindows();
    void updateStatusFromDecoders();

    HWND parent_ = nullptr;
    HWND video_ = nullptr;   // the surface decoders render into

    Method method_ = Method::DirectShowSoftware;
    std::wstring filePath_;
    bool running_ = false;
    std::wstring status_;

    // The log is kept as lines rather than as a control's text so the panel
    // can be drawn with the same font and colours as the rest of the tool.
    std::vector<std::wstring> log_;
    bool logDirty_ = false;

    // Media Foundation advances a frame at a time, so the parent asks us to
    // tick; DirectShow runs its own thread and only needs polling for the
    // position.
    ULONGLONG lastTick_ = 0;
};

}  // namespace decode

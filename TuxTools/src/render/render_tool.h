/*
 * Tux-Tools - driver test renderers
 *
 * Each supported graphics API implements this simple interface so the shell
 * can host one renderer at a time inside a child window. Every renderer builds
 * the same content procedurally - geometry and textures are generated in code,
 * so the test exercises the graphics API and nothing else. There is no image
 * asset and no image decoder involved.
 */

#pragma once

#include <windows.h>

#include <string>

namespace render {

enum class Api {
    None = 0,
    Dx9,
    Dx10,
    Dx11,
    Dx12,
    OpenGl,
    Vulkan,
};

inline const wchar_t* apiName(Api api) {
    switch (api) {
        case Api::Dx9: return L"Direct3D 9";
        case Api::Dx10: return L"Direct3D 10";
        case Api::Dx11: return L"Direct3D 11";
        case Api::Dx12: return L"Direct3D 12";
        case Api::OpenGl: return L"OpenGL";
        case Api::Vulkan: return L"Vulkan";
        default: return L"None";
    }
}

class Tool {
public:
    virtual ~Tool() = default;

    // Creates the graphics device for the host window and builds the
    // procedural test content.
    virtual bool init(HWND host) = 0;

    // Called when the host window is resized.
    virtual void resize(int width, int height) = 0;

    // Draws one frame; elapsed is seconds since start.
    virtual void render(double elapsed) = 0;

    // Releases all graphics resources.
    virtual void shutdown() = 0;

    // Human readable description of the active device (shown in the status bar).
    virtual std::wstring deviceInfo() const = 0;
};

Tool* createDx9();
Tool* createDx10();
Tool* createDx11();
Tool* createDx12();
Tool* createOpenGl();
Tool* createVulkan();

}  // namespace render
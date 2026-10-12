/*
 * Tux-Tools - native Win32 edition
 *
 * OpenGL driver information collector.
 *
 * Creates a temporary hidden WGL context and queries the implementation
 * strings, driver limits and the extension list. No external dependency
 * (GLEW/GLFW) is used.
 */

#include "collectors.h"

#include <windows.h>
#include <GL/gl.h>

#include <cstdio>
#include <string>
#include <vector>
#include "i18n.h"

namespace {

#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_NUM_EXTENSIONS 0x821D
#define GL_NUM_COMPRESSED_TEXTURE_FORMATS 0x86A2
#define GL_COMPRESSED_TEXTURE_FORMATS 0x86A3

#ifndef GL_MAX_3D_TEXTURE_SIZE
#define GL_MAX_3D_TEXTURE_SIZE 0x8073
#endif
#ifndef GL_MAX_CUBE_MAP_TEXTURE_SIZE
#define GL_MAX_CUBE_MAP_TEXTURE_SIZE 0x851C
#endif
#ifndef GL_MAX_ELEMENTS_VERTICES
#define GL_MAX_ELEMENTS_VERTICES 0x80E8
#endif
#ifndef GL_MAX_ELEMENTS_INDICES
#define GL_MAX_ELEMENTS_INDICES 0x80E9
#endif
#ifndef GL_MAX_TEXTURE_IMAGE_UNITS
#define GL_MAX_TEXTURE_IMAGE_UNITS 0x8872
#endif
#ifndef GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#endif
#ifndef GL_MAX_VERTEX_ATTRIBS
#define GL_MAX_VERTEX_ATTRIBS 0x8869
#endif
#ifndef GL_MAX_VERTEX_UNIFORM_COMPONENTS
#define GL_MAX_VERTEX_UNIFORM_COMPONENTS 0x8B4A
#endif
#ifndef GL_MAX_FRAGMENT_UNIFORM_COMPONENTS
#define GL_MAX_FRAGMENT_UNIFORM_COMPONENTS 0x8B49
#endif
#ifndef GL_MAX_VARYING_COMPONENTS
#define GL_MAX_VARYING_COMPONENTS 0x8B4B
#endif
#ifndef GL_MAX_RENDERBUFFER_SIZE
#define GL_MAX_RENDERBUFFER_SIZE 0x84E8
#endif
#ifndef GL_MAX_DRAW_BUFFERS
#define GL_MAX_DRAW_BUFFERS 0x8824
#endif
#ifndef GL_MAX_COLOR_ATTACHMENTS
#define GL_MAX_COLOR_ATTACHMENTS 0x8CDF
#endif
#ifndef GL_MAX_SAMPLES
#define GL_MAX_SAMPLES 0x8D57
#endif
#ifndef GL_MAX_ARRAY_TEXTURE_LAYERS
#define GL_MAX_ARRAY_TEXTURE_LAYERS 0x88FF
#endif
#ifndef GL_MAX_UNIFORM_BUFFER_BINDINGS
#define GL_MAX_UNIFORM_BUFFER_BINDINGS 0x8A2F
#endif
#ifndef GL_MAX_UNIFORM_BLOCK_SIZE
#define GL_MAX_UNIFORM_BLOCK_SIZE 0x8A30
#endif
#ifndef GL_MAX_VERTEX_UNIFORM_BLOCKS
#define GL_MAX_VERTEX_UNIFORM_BLOCKS 0x8A2B
#endif
#ifndef GL_MAX_FRAGMENT_UNIFORM_BLOCKS
#define GL_MAX_FRAGMENT_UNIFORM_BLOCKS 0x8A2C
#endif

typedef const GLubyte*(APIENTRY* PFN_glGetStringi)(GLenum name, GLuint index);

const char* glString(GLenum name) {
    const GLubyte* s = glGetString(name);
    return s ? reinterpret_cast<const char*>(s) : "";
}

bool glTryGetInt(GLenum pname, GLint* out) {
    while (glGetError() != GL_NO_ERROR) {
    }
    GLint value = 0;
    glGetIntegerv(pname, &value);
    if (glGetError() != GL_NO_ERROR) return false;
    *out = value;
    return true;
}

std::vector<std::string> split(const std::string& str) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < str.size()) {
        while (i < str.size() && str[i] == ' ') ++i;
        size_t start = i;
        while (i < str.size() && str[i] != ' ') ++i;
        if (i > start) out.push_back(str.substr(start, i - start));
    }
    return out;
}

void parseVersion(const std::string& version, int* major, int* minor) {
    *major = 0;
    *minor = 0;
    std::sscanf(version.c_str(), "%d.%d", major, minor);
}

struct RenderContext {
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC hglrc = nullptr;

    bool create() {
        HINSTANCE inst = GetModuleHandleW(nullptr);
        WNDCLASSW wc = {};
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = inst;
        wc.lpszClassName = L"TuxToolsGLTmp";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                               0, 0, 1, 1, nullptr, nullptr, inst, nullptr);
        if (!hwnd) return false;

        hdc = GetDC(hwnd);
        if (!hdc) return false;

        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        pfd.cStencilBits = 8;

        int format = ChoosePixelFormat(hdc, &pfd);
        if (!format) return false;
        if (!SetPixelFormat(hdc, format, &pfd)) return false;

        hglrc = wglCreateContext(hdc);
        if (!hglrc) return false;
        if (!wglMakeCurrent(hdc, hglrc)) return false;
        return true;
    }

    ~RenderContext() {
        if (hglrc) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(hglrc);
        }
        if (hdc && hwnd) ReleaseDC(hwnd, hdc);
        if (hwnd) DestroyWindow(hwnd);
    }
};

void* glProc(const char* name) {
    void* p = reinterpret_cast<void*>(wglGetProcAddress(name));
    if (p) return p;
    static HMODULE opengl = LoadLibraryW(L"opengl32.dll");
    if (!opengl) return nullptr;
    return reinterpret_cast<void*>(GetProcAddress(opengl, name));
}

struct LimitEntry {
    const wchar_t* name;
    GLenum pname;
    int minMajor;
};

const LimitEntry kLimits[] = {
    {L"GL_MAX_TEXTURE_SIZE", GL_MAX_TEXTURE_SIZE, 1},
    {L"GL_MAX_VIEWPORT_DIMS", GL_MAX_VIEWPORT_DIMS, 1},
    {L"GL_MAX_TEXTURE_IMAGE_UNITS", GL_MAX_TEXTURE_IMAGE_UNITS, 2},
    {L"GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS", GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, 2},
    {L"GL_MAX_VERTEX_ATTRIBS", GL_MAX_VERTEX_ATTRIBS, 2},
    {L"GL_MAX_VERTEX_UNIFORM_COMPONENTS", GL_MAX_VERTEX_UNIFORM_COMPONENTS, 2},
    {L"GL_MAX_FRAGMENT_UNIFORM_COMPONENTS", GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, 2},
    {L"GL_MAX_VARYING_COMPONENTS", GL_MAX_VARYING_COMPONENTS, 2},
    {L"GL_MAX_RENDERBUFFER_SIZE", GL_MAX_RENDERBUFFER_SIZE, 3},
    {L"GL_MAX_DRAW_BUFFERS", GL_MAX_DRAW_BUFFERS, 2},
    {L"GL_MAX_COLOR_ATTACHMENTS", GL_MAX_COLOR_ATTACHMENTS, 3},
    {L"GL_MAX_SAMPLES", GL_MAX_SAMPLES, 3},
    {L"GL_MAX_3D_TEXTURE_SIZE", GL_MAX_3D_TEXTURE_SIZE, 1},
    {L"GL_MAX_CUBE_MAP_TEXTURE_SIZE", GL_MAX_CUBE_MAP_TEXTURE_SIZE, 1},
    {L"GL_MAX_ARRAY_TEXTURE_LAYERS", GL_MAX_ARRAY_TEXTURE_LAYERS, 3},
    {L"GL_MAX_UNIFORM_BUFFER_BINDINGS", GL_MAX_UNIFORM_BUFFER_BINDINGS, 3},
    {L"GL_MAX_UNIFORM_BLOCK_SIZE", GL_MAX_UNIFORM_BLOCK_SIZE, 3},
    {L"GL_MAX_VERTEX_UNIFORM_BLOCKS", GL_MAX_VERTEX_UNIFORM_BLOCKS, 3},
    {L"GL_MAX_FRAGMENT_UNIFORM_BLOCKS", GL_MAX_FRAGMENT_UNIFORM_BLOCKS, 3},
    {L"GL_MAX_ELEMENTS_VERTICES", GL_MAX_ELEMENTS_VERTICES, 1},
    {L"GL_MAX_ELEMENTS_INDICES", GL_MAX_ELEMENTS_INDICES, 1},
};

void collectWglExtensions(viewer::Report& report) {
    typedef const char*(WINAPI * PFN_wglGetExtensionsStringARB)(HDC);
    typedef const char*(WINAPI * PFN_wglGetExtensionsStringEXT)();

    viewer::Section& section = report.addSection(i18n::tr(L"WGL 扩展（系统层）"));

    auto* arb = reinterpret_cast<PFN_wglGetExtensionsStringARB>(
        glProc("wglGetExtensionsStringARB"));
    const char* list = nullptr;
    if (arb) list = arb(wglGetCurrentDC());
    if (!list) {
        auto* ext = reinterpret_cast<PFN_wglGetExtensionsStringEXT>(
            glProc("wglGetExtensionsStringEXT"));
        if (ext) list = ext();
    }

    if (!list || !*list) {
        section.fields.push_back({i18n::tr(L"状态"), i18n::tr(L"无 WGL 扩展字符串")});
        return;
    }
    for (const std::string& e : split(list)) {
        section.fields.push_back({viewer::toWide(e), i18n::tr(L"支持")});
    }
}

}  // namespace

namespace viewer {

Report collectOpenGL() {
    Report report;

    RenderContext ctx;
    if (!ctx.create()) {
        report.description = i18n::tr(L"OpenGL - 无法创建上下文");
        report.addSection(L"OpenGL");
        report.add(i18n::tr(L"状态"), i18n::tr(L"无法在当前系统上创建 OpenGL 上下文。"));
        return report;
    }

    const char* vendor = glString(GL_VENDOR);
    const char* renderer = glString(GL_RENDERER);
    const char* version = glString(GL_VERSION);
    const char* glsl = glString(GL_SHADING_LANGUAGE_VERSION);

    int major = 0, minor = 0;
    parseVersion(version, &major, &minor);

    report.description = L"OpenGL " + toWide(version) + L" - " + toWide(renderer);

    Section& impl = report.addSection(i18n::tr(L"实现信息"));
    impl.fields.push_back({i18n::tr(L"厂商"), toWide(vendor)});
    impl.fields.push_back({i18n::tr(L"渲染器"), toWide(renderer)});
    impl.fields.push_back({i18n::tr(L"OpenGL 版本"), toWide(version)});
    impl.fields.push_back({i18n::tr(L"着色语言版本"), toWide(glsl)});

    Section& limits = report.addSection(i18n::tr(L"驱动限制"));
    for (const LimitEntry& entry : kLimits) {
        if (entry.pname == GL_MAX_VIEWPORT_DIMS) {
            GLint dims[2] = {0, 0};
            while (glGetError() != GL_NO_ERROR) {
            }
            glGetIntegerv(GL_MAX_VIEWPORT_DIMS, dims);
            if (glGetError() == GL_NO_ERROR) {
                limits.fields.push_back({entry.name, formatInt(dims[0]) + L" x " + formatInt(dims[1])});
            }
            continue;
        }
        if (major < entry.minMajor) continue;
        GLint value = 0;
        if (glTryGetInt(entry.pname, &value)) {
            limits.fields.push_back({entry.name, formatInt(value)});
        }
    }

    Section& extensions = report.addSection(i18n::tr(L"OpenGL 扩展"));
    if (major >= 3) {
        auto glGetStringi = reinterpret_cast<PFN_glGetStringi>(glProc("glGetStringi"));
        GLint count = 0;
        if (glGetStringi && glTryGetInt(GL_NUM_EXTENSIONS, &count)) {
            for (GLint i = 0; i < count; ++i) {
                const GLubyte* e = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i));
                if (e) extensions.fields.push_back({toWide(reinterpret_cast<const char*>(e)), i18n::tr(L"支持")});
            }
        }
    } else {
        for (const std::string& e : split(glString(GL_EXTENSIONS))) {
            extensions.fields.push_back({toWide(e), i18n::tr(L"支持")});
        }
    }
    extensions.fields.insert(extensions.fields.begin(),
                             {i18n::tr(L"数量"), formatUInt(extensions.fields.size())});

    GLint numFormats = 0;
    if (glTryGetInt(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &numFormats) && numFormats > 0) {
        std::vector<GLint> formats(static_cast<size_t>(numFormats), 0);
        while (glGetError() != GL_NO_ERROR) {
        }
        glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, formats.data());
        if (glGetError() == GL_NO_ERROR) {
            Section& section = report.addSection(i18n::tr(L"压缩纹理格式"));
            for (GLint f : formats) {
                section.fields.push_back({formatHex(static_cast<unsigned long long>(f)), L""});
            }
        }
    }

    collectWglExtensions(report);
    return report;
}

}  // namespace viewer

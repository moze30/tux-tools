/*
 * Tux-Tools - OpenGL driver test
 *
 * The classic three meshing gears, the same content glxgears has drawn since
 * 1999 (Brian Paul, MIT licence), on Win32 through WGL. The port is taken from
 * the Win32/GLX port of glxgears itself rather than reimplemented, so the
 * geometry and the animation match what people already recognise as the
 * standard OpenGL test.
 *
 * Why this exercises a different path from the Direct3D backends:
 *
 *   - the draw calls go through the fixed-function pipeline (glBegin/glEnd,
 *     glMatrixMode, glLight, display lists), not through shaders
 *   - it is also the only backend that reaches the driver through WGL: pixel
 *     format selection, a legacy context, and SwapBuffers rather than Present
 *
 * Geometry is generated in code; there is no image asset and no image decoder
 * involved. Only OpenGL 1.1 entry points are used, all of which live in
 * opengl32.dll, so no extension loader and no third-party DLL is required.
 */

#include "render_tool.h"

#include <windows.h>
#include <GL/gl.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace render {

namespace {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// One gear wheel. Kept verbatim from glxgears: the tooth profile, the normal
// directions and the winding here are all load-bearing, and re-deriving them
// is how gears end up inside-out.
void gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width,
          GLint teeth, GLfloat tooth_depth) {
    GLint i;
    GLfloat r0, r1, r2;
    GLfloat angle, da;
    GLfloat u, v, len;

    r0 = inner_radius;
    r1 = outer_radius - tooth_depth / 2.0f;
    r2 = outer_radius + tooth_depth / 2.0f;

    da = 2.0f * (GLfloat)M_PI / teeth / 4.0f;

    glShadeModel(GL_FLAT);
    glNormal3f(0.0f, 0.0f, 1.0f);

    /* draw front face */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        angle = i * 2.0f * (GLfloat)M_PI / teeth;
        glVertex3f(r0 * cosf(angle), r0 * sinf(angle), width * 0.5f);
        glVertex3f(r1 * cosf(angle), r1 * sinf(angle), width * 0.5f);
        if (i < teeth) {
            glVertex3f(r0 * cosf(angle), r0 * sinf(angle), width * 0.5f);
            glVertex3f(r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da),
                       width * 0.5f);
        }
    }
    glEnd();

    /* draw front sides of teeth */
    glBegin(GL_QUADS);
    da = 2.0f * (GLfloat)M_PI / teeth / 4.0f;
    for (i = 0; i < teeth; i++) {
        angle = i * 2.0f * (GLfloat)M_PI / teeth;
        glVertex3f(r1 * cosf(angle), r1 * sinf(angle), width * 0.5f);
        glVertex3f(r2 * cosf(angle + da), r2 * sinf(angle + da), width * 0.5f);
        glVertex3f(r2 * cosf(angle + 2 * da), r2 * sinf(angle + 2 * da),
                   width * 0.5f);
        glVertex3f(r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da),
                   width * 0.5f);
    }
    glEnd();

    glNormal3f(0.0f, 0.0f, -1.0f);

    /* draw back face */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        angle = i * 2.0f * (GLfloat)M_PI / teeth;
        glVertex3f(r1 * cosf(angle), r1 * sinf(angle), -width * 0.5f);
        glVertex3f(r0 * cosf(angle), r0 * sinf(angle), -width * 0.5f);
        if (i < teeth) {
            glVertex3f(r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da),
                       -width * 0.5f);
            glVertex3f(r0 * cosf(angle), r0 * sinf(angle), -width * 0.5f);
        }
    }
    glEnd();

    /* draw back sides of teeth */
    glBegin(GL_QUADS);
    da = 2.0f * (GLfloat)M_PI / teeth / 4.0f;
    for (i = 0; i < teeth; i++) {
        angle = i * 2.0f * (GLfloat)M_PI / teeth;
        glVertex3f(r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da),
                   -width * 0.5f);
        glVertex3f(r2 * cosf(angle + 2 * da), r2 * sinf(angle + 2 * da),
                   -width * 0.5f);
        glVertex3f(r2 * cosf(angle + da), r2 * sinf(angle + da),
                   -width * 0.5f);
        glVertex3f(r1 * cosf(angle), r1 * sinf(angle), -width * 0.5f);
    }
    glEnd();

    /* draw outward faces of teeth */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i < teeth; i++) {
        angle = i * 2.0f * (GLfloat)M_PI / teeth;

        glVertex3f(r1 * cosf(angle), r1 * sinf(angle), width * 0.5f);
        glVertex3f(r1 * cosf(angle), r1 * sinf(angle), -width * 0.5f);
        u = r2 * cosf(angle + da) - r1 * cosf(angle);
        v = r2 * sinf(angle + da) - r1 * sinf(angle);
        len = sqrtf(u * u + v * v);
        u /= len;
        v /= len;
        glNormal3f(v, -u, 0.0f);
        glVertex3f(r2 * cosf(angle + da), r2 * sinf(angle + da), width * 0.5f);
        glVertex3f(r2 * cosf(angle + da), r2 * sinf(angle + da),
                   -width * 0.5f);
        glNormal3f(cosf(angle), sinf(angle), 0.0f);
        glVertex3f(r2 * cosf(angle + 2 * da), r2 * sinf(angle + 2 * da),
                   width * 0.5f);
        glVertex3f(r2 * cosf(angle + 2 * da), r2 * sinf(angle + 2 * da),
                   -width * 0.5f);
        u = r1 * cosf(angle + 3 * da) - r2 * cosf(angle + 2 * da);
        v = r1 * sinf(angle + 3 * da) - r2 * sinf(angle + 2 * da);
        glNormal3f(v, -u, 0.0f);
        glVertex3f(r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da),
                   width * 0.5f);
        glVertex3f(r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da),
                   -width * 0.5f);
        glNormal3f(cosf(angle), sinf(angle), 0.0f);
    }
    glVertex3f(r1, 0.0f, width * 0.5f);
    glVertex3f(r1, 0.0f, -width * 0.5f);
    glEnd();

    glShadeModel(GL_SMOOTH);

    /* draw inside radius cylinder */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        angle = i * 2.0f * (GLfloat)M_PI / teeth;
        glNormal3f(-cosf(angle), -sinf(angle), 0.0f);
        glVertex3f(r0 * cosf(angle), r0 * sinf(angle), -width * 0.5f);
        glVertex3f(r0 * cosf(angle), r0 * sinf(angle), width * 0.5f);
    }
    glEnd();
}

void reportGlError(const char* where) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        char msg[128];
        std::snprintf(msg, sizeof(msg), "OpenGL error 0x%04X in %s\n",
                      static_cast<unsigned>(err), where);
        OutputDebugStringA(msg);
    }
}

}  // namespace

class OpenGlTool : public Tool {
public:
    bool init(HWND host) override;
    void resize(int width, int height) override;
    void render(double elapsed) override;
    void shutdown() override;
    std::wstring deviceInfo() const override { return deviceInfo_; }

private:
    bool makeContext(HWND host);
    void configurePipeline();

    HWND host_ = nullptr;
    HDC dc_ = nullptr;
    HGLRC rc_ = nullptr;
    int width_ = 1;
    int height_ = 1;
    std::wstring deviceInfo_;

    GLint gear1_ = 0;
    GLint gear2_ = 0;
    GLint gear3_ = 0;

    // The gears are lit from a fixed corner. Kept identical to glxgears so the
    // shading matches the picture people associate with the test.
    float angle_ = 0.0f;
};

bool OpenGlTool::makeContext(HWND host) {
    dc_ = GetDC(host);
    if (!dc_) return false;

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    // SetPixelFormat can only ever succeed once per window, so the host window
    // has to be one that no other backend has claimed. The shell hands us a
    // fresh one for exactly this reason.
    const int format = ChoosePixelFormat(dc_, &pfd);
    if (!format) return false;
    if (!SetPixelFormat(dc_, format, &pfd)) return false;

    rc_ = wglCreateContext(dc_);
    if (!rc_) return false;
    if (!wglMakeCurrent(dc_, rc_)) return false;
    return true;
}

void OpenGlTool::configurePipeline() {
    static const GLfloat pos[4] = {5.0f, 5.0f, 10.0f, 0.0f};
    static const GLfloat red[4] = {0.8f, 0.1f, 0.0f, 1.0f};
    static const GLfloat green[4] = {0.0f, 0.8f, 0.2f, 1.0f};
    static const GLfloat blue[4] = {0.2f, 0.2f, 1.0f, 1.0f};

    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);

    // Display lists keep the per-frame cost to three calls; building the gear
    // teeth every frame would measure the trigonometry rather than the driver.
    gear1_ = glGenLists(1);
    glNewList(gear1_, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
    gear(1.0f, 4.0f, 1.0f, 20, 0.7f);
    glEndList();

    gear2_ = glGenLists(1);
    glNewList(gear2_, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green);
    gear(0.5f, 2.0f, 2.0f, 10, 0.7f);
    glEndList();

    gear3_ = glGenLists(1);
    glNewList(gear3_, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, blue);
    gear(1.3f, 2.0f, 0.5f, 10, 0.7f);
    glEndList();

    glEnable(GL_NORMALIZE);
    reportGlError("configurePipeline");
}

bool OpenGlTool::init(HWND host) {
    RECT rc;
    GetClientRect(host, &rc);
    width_ = rc.right - rc.left;
    height_ = rc.bottom - rc.top;
    if (width_ <= 0) width_ = 1;
    if (height_ <= 0) height_ = 1;
    host_ = host;

    if (!makeContext(host)) return false;

    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    deviceInfo_ = std::wstring(L"OpenGL - ") + L"WGL";
    if (renderer && *renderer) {
        std::wstring wide(renderer, renderer + std::strlen(renderer));
        deviceInfo_ += L" - " + wide;
    }
    if (version && *version) {
        std::wstring wide(version, version + std::strlen(version));
        deviceInfo_ += L" (" + wide + L")";
    }

    configurePipeline();
    resize(width_, height_);
    return true;
}

void OpenGlTool::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    width_ = width;
    height_ = height;
    if (!rc_) return;

    wglMakeCurrent(dc_, rc_);
    const GLfloat h = static_cast<GLfloat>(height) / static_cast<GLfloat>(width);
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -h, h, 5.0, 60.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -40.0f);
}

void OpenGlTool::render(double elapsed) {
    if (!rc_ || !dc_) return;
    wglMakeCurrent(dc_, rc_);

    // The gears are advanced by elapsed time rather than one fixed step per
    // frame, so the animation runs at the same speed no matter what frame rate
    // the driver manages. 0.55 puts a full turn at roughly four seconds, close
    // to the original's 2 degrees per frame at 60 Hz.
    angle_ = static_cast<float>(std::fmod(elapsed * 33.0, 360.0));

    glClearColor(23.0f / 255.0f, 28.0f / 255.0f, 41.0f / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glPushMatrix();
    glRotatef(20.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(30.0f, 0.0f, 1.0f, 0.0f);
    glRotatef(0.0f, 0.0f, 0.0f, 1.0f);

    glPushMatrix();
    glTranslatef(-3.0f, -2.0f, 0.0f);
    glRotatef(angle_, 0.0f, 0.0f, 1.0f);
    glCallList(gear1_);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(3.1f, -2.0f, 0.0f);
    glRotatef(-2.0f * angle_ - 9.0f, 0.0f, 0.0f, 1.0f);
    glCallList(gear2_);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(-3.1f, 4.2f, 0.0f);
    glRotatef(-2.0f * angle_ - 25.0f, 0.0f, 0.0f, 1.0f);
    glCallList(gear3_);
    glPopMatrix();

    glPopMatrix();

    SwapBuffers(dc_);
}

void OpenGlTool::shutdown() {
    if (rc_) {
        wglMakeCurrent(dc_, rc_);
        if (gear1_) glDeleteLists(gear1_, 1);
        if (gear2_) glDeleteLists(gear2_, 1);
        if (gear3_) glDeleteLists(gear3_, 1);
        gear1_ = gear2_ = gear3_ = 0;
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(rc_);
        rc_ = nullptr;
    }
    if (dc_ && host_) {
        ReleaseDC(host_, dc_);
        dc_ = nullptr;
    }
}

Tool* createOpenGl() { return new OpenGlTool(); }

}  // namespace render

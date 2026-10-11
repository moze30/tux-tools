/*
 * Tux-Tools - Direct3D 9 driver test
 *
 * A ringed gas giant with a moon riding its ring, floating in a starfield,
 * driven through Direct3D 9's fixed-function pipeline: transforms, lighting,
 * texture stages, sampler states, cull mode, depth write and blending are all
 * set through render states rather than shaders. That is the point of having a
 * separate Direct3D 9 backend - it exercises a completely different part of the
 * driver than the Direct3D 10 and 11 backends do.
 *
 * Geometry and textures are generated in code; there is no image asset and no
 * image decoder involved.
 *
 * Nothing draws a sky: the starfield is additive points of light over the
 * renderer's clear colour, which is deliberate.
 */

#include "render_tool.h"
#include "test_content.h"

#include <d3d9.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace render {

namespace {

// The generated textures are RGBA byte order; Direct3D 9's A8R8G8B8 wants
// BGRA in memory, so red and blue are swapped while uploading.
const DWORD kFvf = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1;

// The camera never moves, which lets the starfield be built once and left
// alone: every quad can be oriented to face this point up front.
const float kCameraX = 0.0f;
const float kCameraY = 4.6f;
const float kCameraZ = -12.5f;
const float kLookAtY = 2.5f;

}  // namespace

class Dx9Tool : public Tool {
public:
    bool init(HWND host) override;
    void resize(int width, int height) override;
    void render(double elapsed) override;
    void shutdown() override;
    std::wstring deviceInfo() const override { return deviceInfo_; }

private:
    struct GpuMesh {
        IDirect3DVertexBuffer9* vb = nullptr;
        IDirect3DIndexBuffer9* ib = nullptr;
        UINT vertexCount = 0;
        UINT primitiveCount = 0;
    };

    bool createDevice(HWND host);
    void releaseDevice();
    bool uploadMesh(const Mesh& mesh, GpuMesh& out);
    bool createGeometry();
    bool createTextures();
    IDirect3DTexture9* uploadTexture(const Texture& source);
    void setupLighting();
    void drawMesh(const GpuMesh& mesh, const Mat4& world, IDirect3DTexture9* texture,
                  bool wrap, const float tint[4]);

    HWND host_ = nullptr;
    int width_ = 1;
    int height_ = 1;
    std::wstring deviceInfo_;

    IDirect3D9* d3d9_ = nullptr;
    IDirect3DDevice9* device_ = nullptr;

    GpuMesh planet_;
    GpuMesh band_;
    GpuMesh stars_;

    IDirect3DTexture9* planetTexture_ = nullptr;
    IDirect3DTexture9* ringTexture_ = nullptr;
    IDirect3DTexture9* moonTexture_ = nullptr;
    IDirect3DTexture9* starSprite_ = nullptr;
};

bool Dx9Tool::createDevice(HWND host) {
    RECT rc;
    GetClientRect(host, &rc);
    width_ = rc.right - rc.left;
    height_ = rc.bottom - rc.top;
    if (width_ <= 0) width_ = 1;
    if (height_ <= 0) height_ = 1;
    host_ = host;

    d3d9_ = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d9_) return false;

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = static_cast<UINT>(width_);
    pp.BackBufferHeight = static_cast<UINT>(height_);
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.hDeviceWindow = host;
    // No vsync, matching the other backends.
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    const DWORD behaviour[] = {
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
        D3DCREATE_MIXED_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
    };
    const wchar_t* behaviourName[] = {L"hardware vertex processing",
                                      L"mixed vertex processing",
                                      L"software vertex processing"};

    HRESULT hr = E_FAIL;
    const wchar_t* used = behaviourName[0];
    for (int i = 0; i < 3; ++i) {
        hr = d3d9_->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, host,
                                 behaviour[i], &pp, &device_);
        used = behaviourName[i];
        if (SUCCEEDED(hr)) break;
    }
    if (FAILED(hr)) return false;

    D3DADAPTER_IDENTIFIER9 id = {};
    if (SUCCEEDED(d3d9_->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id))) {
        // D3DADAPTER_IDENTIFIER9::Description is a fixed-width ANSI string even
        // in a Unicode build, so it needs converting.
        std::wstring name;
        const int needed = MultiByteToWideChar(CP_ACP, 0, id.Description, -1, nullptr, 0);
        if (needed > 1) {
            name.resize(static_cast<size_t>(needed - 1));
            MultiByteToWideChar(CP_ACP, 0, id.Description, -1, &name[0], needed);
        }
        if (name.empty()) name = L"(unknown adapter)";
        deviceInfo_ = std::wstring(L"Direct3D 9 - ") + used + L" - " + name;
    } else {
        deviceInfo_ = std::wstring(L"Direct3D 9 - ") + used;
    }
    return true;
}

void Dx9Tool::releaseDevice() {
    if (device_) { device_->Release(); device_ = nullptr; }
    if (d3d9_) { d3d9_->Release(); d3d9_ = nullptr; }
}

bool Dx9Tool::uploadMesh(const Mesh& mesh, GpuMesh& out) {
    const UINT vbytes = static_cast<UINT>(mesh.vertices.size() * sizeof(Vertex));
    const UINT ibytes = static_cast<UINT>(mesh.indices.size() * sizeof(uint16_t));

    if (FAILED(device_->CreateVertexBuffer(vbytes, 0, kFvf, D3DPOOL_MANAGED,
                                           &out.vb, nullptr))) {
        return false;
    }
    void* dst = nullptr;
    if (FAILED(out.vb->Lock(0, vbytes, &dst, 0))) return false;
    std::memcpy(dst, mesh.vertices.data(), vbytes);
    out.vb->Unlock();

    if (FAILED(device_->CreateIndexBuffer(ibytes, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED,
                                          &out.ib, nullptr))) {
        return false;
    }
    if (FAILED(out.ib->Lock(0, ibytes, &dst, 0))) return false;
    std::memcpy(dst, mesh.indices.data(), ibytes);
    out.ib->Unlock();

    out.vertexCount = mesh.vertexCount();
    out.primitiveCount = mesh.indexCount() / 3;
    return true;
}

bool Dx9Tool::createGeometry() {
    // Direct3D 9 gets a ringed gas giant in open space - nothing like the
    // Direct3D 10 cube lattice or the Direct3D 11 wave scene. The planet and
    // the moon share one sphere mesh at different scales, and every star is the
    // same single quad, just moved and sized differently.
    return uploadMesh(makeSphere(1.0f, 48, 32), planet_) &&
           uploadMesh(makeAnnulus(2.45f, 3.95f, 96), band_) &&
           uploadMesh(makeBillboard(1.0f), stars_);
}

IDirect3DTexture9* Dx9Tool::uploadTexture(const Texture& source) {
    IDirect3DTexture9* texture = nullptr;
    if (FAILED(device_->CreateTexture(static_cast<UINT>(source.width),
                                      static_cast<UINT>(source.height), 1, 0,
                                      D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture,
                                      nullptr))) {
        return nullptr;
    }

    D3DLOCKED_RECT locked = {};
    if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) {
        texture->Release();
        return nullptr;
    }
    for (int y = 0; y < source.height; ++y) {
        const uint8_t* src = source.pixels.data() + static_cast<size_t>(y) * source.width * 4;
        uint8_t* dst = static_cast<uint8_t*>(locked.pBits) + static_cast<size_t>(y) * locked.Pitch;
        for (int x = 0; x < source.width; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];  // blue
            dst[x * 4 + 1] = src[x * 4 + 1];  // green
            dst[x * 4 + 2] = src[x * 4 + 0];  // red
            dst[x * 4 + 3] = src[x * 4 + 3];  // alpha
        }
    }
    texture->UnlockRect(0);
    return texture;
}

bool Dx9Tool::createTextures() {
    // Banded gas-giant surface with a storm oval, radial banding for the ring,
    // a plain gradient for the moon, and one soft dot reused for every star.
    planetTexture_ = uploadTexture(makePlanetTexture(256));
    ringTexture_ = uploadTexture(makePlanetRingTexture(256));
    moonTexture_ = uploadTexture(
        makeVerticalGradient(64, rgba(0xD8, 0xD2, 0xCA), rgba(0x54, 0x4E, 0x48)));
    starSprite_ = uploadTexture(makeStarSprite(64));
    return planetTexture_ != nullptr && ringTexture_ != nullptr &&
           moonTexture_ != nullptr && starSprite_ != nullptr;
}

void Dx9Tool::setupLighting() {
    D3DLIGHT9 light = {};
    light.Type = D3DLIGHT_DIRECTIONAL;
    // Comes from over the viewer's shoulder rather than straight down, so the
    // sides of the rings that face the camera are lit instead of shadowed.
    light.Direction.x = 0.30f;
    light.Direction.y = -0.50f;
    light.Direction.z = 0.81f;
    light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = 1.0f;
    light.Diffuse.a = 1.0f;
    light.Ambient.r = light.Ambient.g = light.Ambient.b = 0.10f;
    light.Ambient.a = 1.0f;
    device_->SetLight(0, &light);
    device_->LightEnable(0, TRUE);

    device_->SetRenderState(D3DRS_LIGHTING, TRUE);
    device_->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
    device_->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_COLORVALUE(0.28f, 0.26f, 0.24f, 1.0f));
    device_->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
    device_->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
    device_->SetRenderState(D3DRS_SPECULARENABLE, FALSE);

    // texture * material diffuse, for colour and for alpha.
    device_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    device_->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    device_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

    device_->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device_->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device_->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
}

bool Dx9Tool::init(HWND host) {
    if (!createDevice(host)) return false;
    if (!createGeometry() || !createTextures()) return false;
    setupLighting();
    return true;
}

void Dx9Tool::resize(int width, int height) {
    if (!device_ || width <= 0 || height <= 0) return;
    width_ = width;
    height_ = height;

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = static_cast<UINT>(width_);
    pp.BackBufferHeight = static_cast<UINT>(height_);
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.hDeviceWindow = host_;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    if (SUCCEEDED(device_->Reset(&pp))) {
        setupLighting();
    }
}

void Dx9Tool::drawMesh(const GpuMesh& mesh, const Mat4& world,
                       IDirect3DTexture9* texture, bool wrap, const float tint[4]) {
    D3DMATERIAL9 material = {};
    material.Diffuse.r = tint[0];
    material.Diffuse.g = tint[1];
    material.Diffuse.b = tint[2];
    material.Diffuse.a = tint[3];
    // D3DRS_AMBIENTMATERIALSOURCE points at the material, so leaving Ambient at
    // zero kills every ambient contribution and surfaces facing away from the
    // light come out black. Give it the same colour, at full strength.
    material.Ambient.r = tint[0];
    material.Ambient.g = tint[1];
    material.Ambient.b = tint[2];
    material.Ambient.a = 1.0f;
    device_->SetMaterial(&material);

    const D3DTEXTUREADDRESS address = wrap ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP;
    device_->SetSamplerState(0, D3DSAMP_ADDRESSU, address);
    device_->SetSamplerState(0, D3DSAMP_ADDRESSV, address);

    device_->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(&world.m));
    device_->SetTexture(0, texture);
    device_->SetStreamSource(0, mesh.vb, 0, sizeof(Vertex));
    device_->SetIndices(mesh.ib);
    device_->SetFVF(kFvf);
    device_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, mesh.vertexCount, 0,
                                  mesh.primitiveCount);
}

void Dx9Tool::render(double elapsed) {
    if (!device_) return;

    if (device_->TestCooperativeLevel() == D3DERR_DEVICELOST) return;

    device_->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                   D3DCOLOR_XRGB(23, 28, 41), 1.0f, 0);
    if (FAILED(device_->BeginScene())) return;

    const Mat4 view = lookAtLH(kCameraX, kCameraY, kCameraZ, 0.0f, kLookAtY, 0.0f);
    const float aspect = height_ > 0 ? static_cast<float>(width_) / height_ : 1.0f;
    const Mat4 proj = perspectiveFovLH(0.85f, aspect, 0.1f, 100.0f);
    device_->SetTransform(D3DTS_VIEW, reinterpret_cast<const D3DMATRIX*>(&view.m));
    device_->SetTransform(D3DTS_PROJECTION, reinterpret_cast<const D3DMATRIX*>(&proj));

    device_->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    device_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    device_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);

    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    const float planetTint[4] = {1.02f, 0.98f, 0.94f, 1.0f};
    const float moonTint[4] = {0.95f, 0.93f, 0.90f, 1.0f};
    const float t = static_cast<float>(elapsed);

    // Six stars drifting through the scene. Each is the same single quad, which
    // already lies edge-on to the camera, so it only needs moving and scaling -
    // no rebuild, and far less clutter than a full sky layer.
    static const float kStarHome[6][3] = {
        {-6.6f, 6.8f, -1.0f}, {7.0f, 5.4f, -2.4f}, {-5.8f, 1.0f, 2.8f},
        {6.2f, 0.4f, 2.0f}, {-7.8f, 3.8f, 4.3f}, {7.6f, 3.0f, 3.6f},
    };
    // Sizes are chosen so the dots land around 10-20 pixels across from this
    // camera: a hard-edged disc the size of the old glow would read as a white
    // blob rather than a point of light.
    static const float kStarSize[6] = {0.24f, 0.17f, 0.12f, 0.27f, 0.11f, 0.19f};
    static const float kStarGlow[6] = {1.00f, 0.82f, 0.62f, 1.00f, 0.55f, 0.90f};

    device_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device_->SetRenderState(D3DRS_LIGHTING, FALSE);
    device_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    device_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

    for (int i = 0; i < 6; ++i) {
        const float phase = static_cast<float>(i) * 1.7f;
        const float drift = 0.55f;
        const float px = kStarHome[i][0] + drift * std::sin(t * 0.42f + phase);
        const float py = kStarHome[i][1] + drift * std::cos(t * 0.33f + phase * 1.3f);
        const float pz = kStarHome[i][2] + drift * std::sin(t * 0.27f + phase * 0.7f);
        const float size = kStarSize[i];
        const Mat4 world = multiply(scaling(size, size, size),
                                    translation(px, py, pz));
        const float tint[4] = {kStarGlow[i], kStarGlow[i], kStarGlow[i], 1.0f};
        drawMesh(stars_, world, starSprite_, false, tint);
    }

    // Back to ordinary opaque rendering for the bodies.
    device_->SetRenderState(D3DRS_LIGHTING, TRUE);
    device_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);

    const float kPlanetY = 2.7f;
    const float kTilt = 0.16f;      // the ring plane leans a little
    const float kMoonOrbit = 3.30f;
    const float spin = t * 0.42f;   // the ring's turn carries the moon with it

    // The gas giant, spinning on its axis.
    const Mat4 planetWorld =
        multiply(rotationY(t * 0.30f), translation(0.0f, kPlanetY, 0.0f));
    drawMesh(planet_, planetWorld, planetTexture_, false, planetTint);

    // The moon rides the ring: scale, out to the orbit radius, turn with the
    // ring, lean with it, then move to the planet. Drawn opaque so it reads as
    // a solid body rather than a smear.
    const Mat4 moonWorld = multiply(
        multiply(multiply(multiply(scaling(0.46f, 0.46f, 0.46f),
                                   translation(kMoonOrbit, 0.0f, 0.0f)),
                          rotationY(spin)),
                 rotationZ(kTilt)),
        translation(0.0f, kPlanetY, 0.0f));
    drawMesh(planet_, moonWorld, moonTexture_, false, moonTint);

    // Ring last: blended, no depth writes, no culling, so the planet shows
    // through the gaps in the ring instead of being punched out by it.
    device_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

    const Mat4 ringWorld = multiply(multiply(rotationY(spin), rotationZ(kTilt)),
                                    translation(0.0f, kPlanetY, 0.0f));
    drawMesh(band_, ringWorld, ringTexture_, false, white);

    device_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    device_->SetTexture(0, nullptr);

    device_->EndScene();
    device_->Present(nullptr, nullptr, nullptr, nullptr);
}

void Dx9Tool::shutdown() {
    for (IDirect3DTexture9** tex : {&planetTexture_, &ringTexture_, &moonTexture_,
                                    &starSprite_}) {
        if (*tex) { (*tex)->Release(); *tex = nullptr; }
    }
    GpuMesh* meshes[3] = {&planet_, &band_, &stars_};
    for (GpuMesh* m : meshes) {
        if (m->ib) { m->ib->Release(); m->ib = nullptr; }
        if (m->vb) { m->vb->Release(); m->vb = nullptr; }
    }
    releaseDevice();
}

Tool* createDx9() { return new Dx9Tool(); }

}  // namespace render

/*
 * Tux-Tools - Direct3D 10 driver test
 *
 * Saturn: a banded gas giant inside a layered ring system, with a small moon
 * riding the outer ring. Geometry and textures are generated in code - there is
 * no image asset and no image decoder involved.
 *
 * The rings and the planet shade each other. That two-way cast is the point of
 * this scene: the ring lays a dark band across the globe, and the globe drops a
 * shadow across the far side of the rings. Getting both requires the shadow map
 * to hold a closed sphere *and* a flat, single-sided sheet, which is why the
 * shadow pass culls front faces for the solids and none for the sheet.
 *
 * The shading is meant to read as solid metal rather than as a handful of flat
 * cards: a hemisphere ambient term separates faces by orientation, a key light
 * adds a broad Blinn-Phong highlight, the Fresnel term lifts the grazing edges,
 * and a reflect-vector lookup against the sky/ground colours stands in for an
 * environment map. A 1024x1024 shadow map of the structural mesh supplies the
 * self-shadowing, which is what gives the junctions between parts their weight.
 *
 * Direct3D 10 notes: D3D10CreateDeviceAndSwapChain hands back a D3D10.0
 * device, whose texture formats are restricted - B8G8R8A8_UNORM can only be a
 * display format there, so textures use R8G8B8A8_UNORM and the generated pixel
 * data is already in that order.
 */

#include "render_tool.h"
#include "test_content.h"

#include <d3d10.h>
#include <d3d10misc.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace render {

namespace {

const int kShadowSize = 1024;

struct Constants {
    float wvp[16];
    float world[16];
    float lightWVP[16];
    float light[4];    // xyz = direction the light travels
    float tint[4];     // rgb = tint, a = opacity
    float spec[4];     // rgb = specular colour, a = shininess
    float rim[4];      // rgb = rim colour, a = rim strength
    float sky[4];      // rgb = ambient sky colour, a = ambient intensity
    float ground[4];   // rgb = ambient bounce from below
    float eye[4];      // xyz = eye position in world space
    float params[4];   // x = shadow texel, y = depth bias, z = unlit, w = shadow strength
};

// Per-draw surface description. Everything that used to be a bare tint lives
// here now, so a part can be brushed metal, matte photovoltaic glass or a dark
// composite without any change to the geometry path.
struct Material {
    float tint[4];
    float spec[4];
    float rim[4];
    float sky[4];
    float ground[4];
    float params[4];
};

const char kShaderSource[] = R"(
cbuffer Constants : register(b0)
{
    row_major float4x4 gWVP;
    row_major float4x4 gWorld;
    row_major float4x4 gLightWVP;
    float4 gLight;
    float4 gTint;
    float4 gSpec;
    float4 gRim;
    float4 gSky;
    float4 gGround;
    float4 gEye;
    float4 gParams;
};

struct VSInput
{
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv  : TEXCOORD0;
};

struct PSInput
{
    float4 pos  : SV_POSITION;
    float3 nrm  : NORMAL;
    float2 uv   : TEXCOORD0;
    float3 wpos : TEXCOORD1;
    float4 lpos : TEXCOORD2;
};

PSInput VSMain(VSInput input)
{
    PSInput o;
    o.pos  = mul(float4(input.pos, 1.0f), gWVP);
    o.nrm  = mul(float4(input.nrm, 0.0f), gWorld).xyz;
    o.uv   = input.uv;
    o.wpos = mul(float4(input.pos, 1.0f), gWorld).xyz;
    o.lpos = mul(float4(input.pos, 1.0f), gLightWVP);
    return o;
}

Texture2D       gTex    : register(t0);
Texture2D<float> gShadow : register(t1);
SamplerState    gSmp    : register(s0);
SamplerState    gShadowSmp : register(s1);

// Nine taps of raw depth, compared by hand. Point sampling plus an explicit
// comparison keeps the contact edges crisp while still softening them a little.
float shadowVisibility(float4 lpos)
{
    if (gParams.w <= 0.0f)
        return 1.0f;

    float3 p = lpos.xyz / lpos.w;
    float2 uv = float2(p.x * 0.5f + 0.5f, 0.5f - p.y * 0.5f);
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
        return 1.0f;

    float reference = p.z - gParams.y;
    float2 texel = float2(gParams.x, gParams.x);
    float sum = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float stored = gShadow.SampleLevel(gShadowSmp, uv + float2(x, y) * texel, 0);
            sum += (reference <= stored) ? 1.0f : 0.0f;
        }
    }
    return sum / 9.0f;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 texel = gTex.Sample(gSmp, input.uv);
    float4 surface = float4(texel.rgb * gTint.rgb, texel.a * gTint.a);

    // Additive sprites are pure light: no normal, no shadow, no ambient.
    if (gParams.z > 0.5f)
        return surface;

    float3 N = normalize(input.nrm);
    float3 V = normalize(gEye.xyz - input.wpos);
    float3 L = normalize(-gLight.xyz);
    float3 H = normalize(L + V);

    float visibility = lerp(1.0f - gParams.w, 1.0f, shadowVisibility(input.lpos));

    float3 albedo = texel.rgb * gTint.rgb;

    // Ambient from the sky above and bounce from below, scaled by the surface
    // colour. Without that last factor a dark material would be washed towards
    // grey and a white one would blow out - both of which flatten the scene.
    float up = N.y * 0.5f + 0.5f;
    float3 ambient = lerp(gGround.rgb, gSky.rgb, up) * gSky.a * albedo;
    // Pockets the key light cannot reach also sit in less ambient, which is
    // what makes a junction read as a junction.
    ambient *= lerp(0.55f, 1.0f, visibility);

    float ndl = saturate(dot(N, L));
    const float3 keyColour = float3(1.06f, 1.03f, 0.98f);
    float3 diffuse = albedo * keyColour * ndl * visibility;

    float3 reflected = reflect(-V, N);
    float fresnel = pow(1.0f - saturate(dot(N, V)), 5.0f);

    // A cheap environment map: reflect the eye vector and read the vertical
    // component as sky versus ground. Metal picks up a horizon gradient
    // instead of one flat tint, which is most of what sells its solidity.
    float3 env = lerp(gGround.rgb, gSky.rgb, saturate(reflected.y * 0.5f + 0.5f));
    float3 environment = env * gSpec.rgb * 0.85f * (0.30f + 0.70f * fresnel);

    float blinn = pow(saturate(dot(N, H)), max(gSpec.a, 1.0f));
    // Weaker where the surface faces away from the eye, stronger at grazing
    // angles - the signature of a smooth curved surface.
    float3 specular = gSpec.rgb * blinn * visibility * (0.35f + 0.65f * fresnel);

    float rim = pow(1.0f - saturate(dot(N, V)), 3.0f) * gRim.a;

    float3 lit = ambient + diffuse + specular + environment + gRim.rgb * rim;
    return float4(lit, surface.a);
}
)";

Material makeMaterial(float tr, float tg, float tb, float opacity,
                      float sr, float sg, float sb, float shininess,
                      float rr, float rg, float rb, float rimStrength,
                      float ambient, float shadowStrength, float unlit) {
    Material m = {};
    m.tint[0] = tr; m.tint[1] = tg; m.tint[2] = tb; m.tint[3] = opacity;
    m.spec[0] = sr; m.spec[1] = sg; m.spec[2] = sb; m.spec[3] = shininess;
    m.rim[0] = rr;  m.rim[1] = rg;  m.rim[2] = rb;  m.rim[3] = rimStrength;
    // A cool key from above with a warmish bounce from the dark ground plane.
    m.sky[0] = 0.40f; m.sky[1] = 0.48f; m.sky[2] = 0.66f; m.sky[3] = ambient;
    m.ground[0] = 0.15f; m.ground[1] = 0.14f; m.ground[2] = 0.16f; m.ground[3] = 0.0f;
    m.params[0] = 0.0f;
    m.params[1] = 0.0f;
    m.params[2] = unlit;
    m.params[3] = shadowStrength;
    return m;
}

}  // namespace

class Dx10Tool : public Tool {
public:
    bool init(HWND host) override;
    void resize(int width, int height) override;
    void render(double elapsed) override;
    void shutdown() override;
    std::wstring deviceInfo() const override { return deviceInfo_; }


private:
    struct GpuMesh {
        ID3D10Buffer* vb = nullptr;
        ID3D10Buffer* ib = nullptr;
        UINT indexCount = 0;
    };

    bool createDevice(HWND host);
    bool createShaders();
    bool createGeometry();
    bool createTextures();
    bool createStates();
    bool createShadowMap();
    bool uploadMesh(const Mesh& mesh, GpuMesh& out);
    void createSizeDependent();
    void releaseSizeDependent();
    void setConstants(const Mat4& wvp, const Mat4& world, const Mat4& lightWVP,
                      const Material& material);
    void bindMesh(const GpuMesh& mesh);
    void drawMesh(const GpuMesh& mesh, const Mat4& world,
                  ID3D10ShaderResourceView* texture, ID3D10SamplerState* sampler,
                  const Material& material);
    void drawShadow(const GpuMesh& mesh, const Mat4& world);

    int width_ = 1;
    int height_ = 1;
    std::wstring deviceInfo_;
    Mat4 lightViewProj_;

    ID3D10Device* device_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D10RenderTargetView* rtv_ = nullptr;
    ID3D10DepthStencilView* dsv_ = nullptr;
    ID3D10Texture2D* depthTex_ = nullptr;

    ID3D10VertexShader* vs_ = nullptr;
    ID3D10PixelShader* ps_ = nullptr;
    ID3D10InputLayout* inputLayout_ = nullptr;
    ID3D10Buffer* constantBuffer_ = nullptr;

    ID3D10Texture2D* shadowTex_ = nullptr;
    ID3D10DepthStencilView* shadowDsv_ = nullptr;
    ID3D10ShaderResourceView* shadowSrv_ = nullptr;
    ID3D10SamplerState* shadowSampler_ = nullptr;

    GpuMesh frame_;
    GpuMesh panel_;
    GpuMesh shuttle_;
    GpuMesh stars_;

    ID3D10ShaderResourceView* frameTexture_ = nullptr;
    ID3D10ShaderResourceView* panelTexture_ = nullptr;
    ID3D10ShaderResourceView* starSprite_ = nullptr;
    ID3D10SamplerState* clampSampler_ = nullptr;

    ID3D10RasterizerState* rasterCullBack_ = nullptr;
    ID3D10RasterizerState* rasterCullNone_ = nullptr;
    ID3D10RasterizerState* rasterShadow_ = nullptr;
    ID3D10RasterizerState* rasterShadowSheet_ = nullptr;
    ID3D10DepthStencilState* depthWrite_ = nullptr;
    ID3D10DepthStencilState* depthNoWrite_ = nullptr;
    ID3D10BlendState* blendOpaque_ = nullptr;
    ID3D10BlendState* blendAlpha_ = nullptr;
    ID3D10BlendState* blendAdditive_ = nullptr;
};

bool Dx10Tool::createDevice(HWND host) {
    RECT rc;
    GetClientRect(host, &rc);
    width_ = rc.right - rc.left;
    height_ = rc.bottom - rc.top;
    if (width_ <= 0) width_ = 1;
    if (height_ <= 0) height_ = 1;

    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 1;
    desc.BufferDesc.Width = static_cast<UINT>(width_);
    desc.BufferDesc.Height = static_cast<UINT>(height_);
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = host;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    HRESULT hr = D3D10CreateDeviceAndSwapChain(
        nullptr, D3D10_DRIVER_TYPE_HARDWARE, nullptr, 0, D3D10_SDK_VERSION,
        &desc, &swapChain_, &device_);
    D3D10_DRIVER_TYPE used = D3D10_DRIVER_TYPE_HARDWARE;
    if (FAILED(hr)) {
        used = D3D10_DRIVER_TYPE_WARP;
        hr = D3D10CreateDeviceAndSwapChain(
            nullptr, D3D10_DRIVER_TYPE_WARP, nullptr, 0, D3D10_SDK_VERSION,
            &desc, &swapChain_, &device_);
    }
    if (FAILED(hr)) return false;

    std::wstring adapterName = L"(unknown adapter)";
    IDXGIDevice* dxgiDevice = nullptr;
    if (SUCCEEDED(device_->QueryInterface(__uuidof(IDXGIDevice),
                                          reinterpret_cast<void**>(&dxgiDevice)))) {
        IDXGIAdapter* adapter = nullptr;
        if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
            DXGI_ADAPTER_DESC ad = {};
            if (SUCCEEDED(adapter->GetDesc(&ad))) adapterName = ad.Description;
            adapter->Release();
        }
        dxgiDevice->Release();
    }
    deviceInfo_ = std::wstring(L"Direct3D 10") +
                  (used == D3D10_DRIVER_TYPE_WARP ? L" (WARP)" : L"") +
                  L" - " + adapterName;
    return true;
}

bool Dx10Tool::createShaders() {
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errors = nullptr;

    HRESULT hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "driver_test",
                            nullptr, nullptr, "VSMain", "vs_4_0",
                            D3D10_SHADER_ENABLE_STRICTNESS, 0, &vsBlob, &errors);
    if (FAILED(hr)) {
        if (errors) errors->Release();
        return false;
    }
    hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "driver_test",
                    nullptr, nullptr, "PSMain", "ps_4_0",
                    D3D10_SHADER_ENABLE_STRICTNESS, 0, &psBlob, &errors);
    if (FAILED(hr)) {
        if (errors) errors->Release();
        vsBlob->Release();
        return false;
    }
    if (errors) errors->Release();

    device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                &vs_);
    device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                               &ps_);

    D3D10_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D10_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
         D3D10_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
         D3D10_INPUT_PER_VERTEX_DATA, 0},
    };
    device_->CreateInputLayout(layout, 3, vsBlob->GetBufferPointer(),
                               vsBlob->GetBufferSize(), &inputLayout_);

    D3D10_BUFFER_DESC cbd = {};
    cbd.Usage = D3D10_USAGE_DYNAMIC;
    cbd.ByteWidth = sizeof(Constants);
    cbd.BindFlags = D3D10_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;
    device_->CreateBuffer(&cbd, nullptr, &constantBuffer_);

    vsBlob->Release();
    psBlob->Release();
    return vs_ && ps_ && inputLayout_ && constantBuffer_;
}

bool Dx10Tool::uploadMesh(const Mesh& mesh, GpuMesh& out) {
    D3D10_BUFFER_DESC vbd = {};
    vbd.Usage = D3D10_USAGE_IMMUTABLE;
    vbd.ByteWidth = static_cast<UINT>(mesh.vertices.size() * sizeof(Vertex));
    vbd.BindFlags = D3D10_BIND_VERTEX_BUFFER;
    D3D10_SUBRESOURCE_DATA vinit = {};
    vinit.pSysMem = mesh.vertices.data();
    if (FAILED(device_->CreateBuffer(&vbd, &vinit, &out.vb))) return false;

    D3D10_BUFFER_DESC ibd = {};
    ibd.Usage = D3D10_USAGE_IMMUTABLE;
    ibd.ByteWidth = static_cast<UINT>(mesh.indices.size() * sizeof(uint16_t));
    ibd.BindFlags = D3D10_BIND_INDEX_BUFFER;
    D3D10_SUBRESOURCE_DATA iinit = {};
    iinit.pSysMem = mesh.indices.data();
    if (FAILED(device_->CreateBuffer(&ibd, &iinit, &out.ib))) return false;

    out.indexCount = mesh.indexCount();
    return true;
}

bool Dx10Tool::createGeometry() {
    // Direct3D 10 gets Saturn. A single sphere plus one annulus is all the
    // silhouette it needs; the detail lives in the two generated textures and
    // in the shadows the two bodies throw at each other.
    const float pi = 3.14159265358979f;

    const Mesh planet = makeSphere(2.00f, 64, 48);
    const Mesh ring = makeAnnulus(2.45f, 3.95f, 160);
    const Mesh moon = makeSphere(0.34f, 32, 24);

    (void)pi;
    return uploadMesh(planet, frame_) &&
           uploadMesh(ring, panel_) &&
           uploadMesh(moon, shuttle_) &&
           uploadMesh(makeBillboard(1.0f), stars_);
}

bool Dx10Tool::createTextures() {
    // Smooth gradients and soft bands only - no checkerboard, no concentric
    // bands, so nothing in the frame gives the eye a repeating pattern to lock
    // onto. The ring's structure runs across v, which is the one direction a
    // viewer does not read as a repeating tile.
    const Texture globe = makeSaturnPlanetTexture(512);
    const Texture rings = makeSaturnRingTexture(512);
    const Texture star = makeStarSprite(64);

    D3D10_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(globe.width);
    desc.Height = static_cast<UINT>(globe.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D10_USAGE_IMMUTABLE;
    desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;

    const Texture* sources[3] = {&globe, &rings, &star};
    ID3D10ShaderResourceView** targets[3] = {&frameTexture_, &panelTexture_, &starSprite_};
    for (int i = 0; i < 3; ++i) {
        D3D10_SUBRESOURCE_DATA init = {};
        init.pSysMem = sources[i]->pixels.data();
        init.SysMemPitch = static_cast<UINT>(sources[i]->width) * 4;
        ID3D10Texture2D* texture = nullptr;
        if (FAILED(device_->CreateTexture2D(&desc, &init, &texture))) return false;
        const HRESULT hr =
            device_->CreateShaderResourceView(texture, nullptr, targets[i]);
        texture->Release();
        if (FAILED(hr)) return false;
    }

    D3D10_SAMPLER_DESC sd = {};
    sd.Filter = D3D10_FILTER_MIN_MAG_MIP_LINEAR;
    // Clamp only: every texture here is a single smooth sheet meant to be
    // stretched across the object once, not tiled.
    sd.AddressU = sd.AddressV = sd.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D10_COMPARISON_NEVER;
    sd.MaxLOD = D3D10_FLOAT32_MAX;
    if (FAILED(device_->CreateSamplerState(&sd, &clampSampler_))) return false;

    // The shadow map is read with raw depth fetches, so point sampling and an
    // explicit comparison in the shader is what is wanted here.
    D3D10_SAMPLER_DESC sp = {};
    sp.Filter = D3D10_FILTER_MIN_MAG_MIP_POINT;
    sp.AddressU = sp.AddressV = sp.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
    sp.ComparisonFunc = D3D10_COMPARISON_NEVER;
    sp.MaxLOD = D3D10_FLOAT32_MAX;
    if (FAILED(device_->CreateSamplerState(&sp, &shadowSampler_))) return false;

    return true;
}

bool Dx10Tool::createStates() {
    D3D10_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D10_FILL_SOLID;
    rd.CullMode = D3D10_CULL_BACK;
    rd.DepthClipEnable = TRUE;
    if (FAILED(device_->CreateRasterizerState(&rd, &rasterCullBack_))) return false;

    rd.CullMode = D3D10_CULL_NONE;
    if (FAILED(device_->CreateRasterizerState(&rd, &rasterCullNone_))) return false;

    // Shadow caster for the closed solids (planet, moon): cull front faces so
    // the depth stored is the far side of the body. That trades a little light
    // leaking for the complete absence of self-shadow acne, and on a convex
    // body in empty space the leaking is invisible while acne would not be.
    D3D10_RASTERIZER_DESC rs = {};
    rs.FillMode = D3D10_FILL_SOLID;
    rs.CullMode = D3D10_CULL_FRONT;
    rs.DepthClipEnable = TRUE;
    rs.DepthBias = 900;
    rs.SlopeScaledDepthBias = 3.0f;
    rs.DepthBiasClamp = 0.0f;
    if (FAILED(device_->CreateRasterizerState(&rs, &rasterShadow_))) return false;

    // The ring is a single-sided sheet. Culling front faces would erase it from
    // the shadow map entirely, so it is culled not at all - and because a flat
    // sheet seen from the side stores a clean depth, the acne the front-cull
    // trick was there to avoid does not arise here.
    rs.CullMode = D3D10_CULL_NONE;
    rs.DepthBias = 1200;
    rs.SlopeScaledDepthBias = 3.5f;
    if (FAILED(device_->CreateRasterizerState(&rs, &rasterShadowSheet_))) return false;

    D3D10_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = TRUE;
    dsd.DepthWriteMask = D3D10_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc = D3D10_COMPARISON_LESS_EQUAL;
    if (FAILED(device_->CreateDepthStencilState(&dsd, &depthWrite_))) return false;

    dsd.DepthWriteMask = D3D10_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(device_->CreateDepthStencilState(&dsd, &depthNoWrite_))) return false;

    D3D10_BLEND_DESC bd = {};
    bd.BlendEnable[0] = FALSE;
    bd.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device_->CreateBlendState(&bd, &blendOpaque_))) return false;

    bd.BlendEnable[0] = TRUE;
    bd.SrcBlend = D3D10_BLEND_SRC_ALPHA;
    bd.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
    bd.BlendOp = D3D10_BLEND_OP_ADD;
    bd.SrcBlendAlpha = D3D10_BLEND_ONE;
    bd.DestBlendAlpha = D3D10_BLEND_INV_SRC_ALPHA;
    bd.BlendOpAlpha = D3D10_BLEND_OP_ADD;
    if (FAILED(device_->CreateBlendState(&bd, &blendAlpha_))) return false;

    // Additive: the stars are points of light added to whatever is behind them.
    bd.SrcBlend = D3D10_BLEND_ONE;
    bd.DestBlend = D3D10_BLEND_ONE;
    bd.SrcBlendAlpha = D3D10_BLEND_ONE;
    bd.DestBlendAlpha = D3D10_BLEND_ONE;
    if (FAILED(device_->CreateBlendState(&bd, &blendAdditive_))) return false;

    return true;
}

bool Dx10Tool::createShadowMap() {
    // Typeless so the same texture can be a depth target while the shadow pass
    // writes it and a plain R32_FLOAT resource while the main pass reads it.
    D3D10_TEXTURE2D_DESC sd = {};
    sd.Width = static_cast<UINT>(kShadowSize);
    sd.Height = static_cast<UINT>(kShadowSize);
    sd.MipLevels = 1;
    sd.ArraySize = 1;
    sd.Format = DXGI_FORMAT_R32_TYPELESS;
    sd.SampleDesc.Count = 1;
    sd.BindFlags = D3D10_BIND_DEPTH_STENCIL | D3D10_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&sd, nullptr, &shadowTex_))) return false;

    D3D10_DEPTH_STENCIL_VIEW_DESC dvd = {};
    dvd.Format = DXGI_FORMAT_D32_FLOAT;
    dvd.ViewDimension = D3D10_DSV_DIMENSION_TEXTURE2D;
    if (FAILED(device_->CreateDepthStencilView(shadowTex_, &dvd, &shadowDsv_))) {
        return false;
    }

    D3D10_SHADER_RESOURCE_VIEW_DESC svd = {};
    svd.Format = DXGI_FORMAT_R32_FLOAT;
    svd.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
    svd.Texture2D.MipLevels = 1;
    if (FAILED(device_->CreateShaderResourceView(shadowTex_, &svd, &shadowSrv_))) {
        return false;
    }
    return true;
}

bool Dx10Tool::init(HWND host) {
    if (!createDevice(host)) return false;
    if (!createShaders() || !createGeometry() || !createTextures() ||
        !createStates() || !createShadowMap()) {
        return false;
    }
    createSizeDependent();
    return rtv_ != nullptr;
}

void Dx10Tool::releaseSizeDependent() {
    if (rtv_) { rtv_->Release(); rtv_ = nullptr; }
    if (dsv_) { dsv_->Release(); dsv_ = nullptr; }
    if (depthTex_) { depthTex_->Release(); depthTex_ = nullptr; }
}

void Dx10Tool::createSizeDependent() {
    ID3D10Texture2D* backBuffer = nullptr;
    swapChain_->GetBuffer(0, __uuidof(ID3D10Texture2D),
                          reinterpret_cast<void**>(&backBuffer));
    if (backBuffer) {
        device_->CreateRenderTargetView(backBuffer, nullptr, &rtv_);
        backBuffer->Release();
    }

    D3D10_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = static_cast<UINT>(width_);
    depthDesc.Height = static_cast<UINT>(height_);
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.BindFlags = D3D10_BIND_DEPTH_STENCIL;
    if (SUCCEEDED(device_->CreateTexture2D(&depthDesc, nullptr, &depthTex_))) {
        device_->CreateDepthStencilView(depthTex_, nullptr, &dsv_);
    }
}

void Dx10Tool::resize(int width, int height) {
    if (!swapChain_ || width <= 0 || height <= 0) return;
    width_ = width;
    height_ = height;
    releaseSizeDependent();
    swapChain_->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height),
                              DXGI_FORMAT_UNKNOWN, 0);
    createSizeDependent();
}

void Dx10Tool::setConstants(const Mat4& wvp, const Mat4& world, const Mat4& lightWVP,
                            const Material& material) {
    void* mapped = nullptr;
    if (FAILED(constantBuffer_->Map(D3D10_MAP_WRITE_DISCARD, 0, &mapped))) return;
    Constants* cb = static_cast<Constants*>(mapped);
    std::memcpy(cb->wvp, wvp.m, sizeof(wvp.m));
    std::memcpy(cb->world, world.m, sizeof(world.m));
    std::memcpy(cb->lightWVP, lightWVP.m, sizeof(lightWVP.m));
    // Light from high on the left, and far enough round to the front that the
    // surfaces facing the camera are still lit. Arriving from the left is what
    // throws the hub's shadow across the right-hand side of the ring, where it
    // can actually be seen, instead of hiding it behind the object.
    cb->light[0] = 0.60f; cb->light[1] = -0.68f;
    cb->light[2] = 0.42f;  cb->light[3] = 0.0f;
    std::memcpy(cb->tint, material.tint, sizeof(material.tint));
    std::memcpy(cb->spec, material.spec, sizeof(material.spec));
    std::memcpy(cb->rim, material.rim, sizeof(material.rim));
    std::memcpy(cb->sky, material.sky, sizeof(material.sky));
    std::memcpy(cb->ground, material.ground, sizeof(material.ground));
    cb->eye[0] = 0.0f; cb->eye[1] = 3.6f; cb->eye[2] = -12.6f; cb->eye[3] = 0.0f;
    cb->params[0] = 1.0f / static_cast<float>(kShadowSize);
    cb->params[1] = 0.0018f;
    cb->params[2] = material.params[2];
    cb->params[3] = material.params[3];
    constantBuffer_->Unmap();
}

void Dx10Tool::bindMesh(const GpuMesh& mesh) {
    UINT stride = sizeof(Vertex), offset = 0;
    device_->IASetVertexBuffers(0, 1, &mesh.vb, &stride, &offset);
    device_->IASetIndexBuffer(mesh.ib, DXGI_FORMAT_R16_UINT, 0);
}

void Dx10Tool::drawMesh(const GpuMesh& mesh, const Mat4& world,
                        ID3D10ShaderResourceView* texture,
                        ID3D10SamplerState* sampler, const Material& material) {
    const Mat4 view = lookAtLH(0.0f, 3.6f, -12.6f, 0.0f, 0.1f, 0.0f);
    const float aspect = height_ > 0 ? static_cast<float>(width_) / height_ : 1.0f;
    const Mat4 proj = perspectiveFovLH(0.78f, aspect, 0.1f, 100.0f);
    const Mat4 wvp = multiply(multiply(world, view), proj);

    setConstants(wvp, world, multiply(world, lightViewProj_), material);

    bindMesh(mesh);
    device_->PSSetShaderResources(0, 1, &texture);
    device_->PSSetSamplers(0, 1, &sampler);
    device_->DrawIndexed(mesh.indexCount, 0, 0);
}

void Dx10Tool::drawShadow(const GpuMesh& mesh, const Mat4& world) {
    // The depth pass only needs a position, so the model is pushed through the
    // light's view-projection in the same WVP slot the main pass uses.
    setConstants(multiply(world, lightViewProj_), world, lightViewProj_,
                 makeMaterial(1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0));
    bindMesh(mesh);
    device_->DrawIndexed(mesh.indexCount, 0, 0);
}

void Dx10Tool::render(double elapsed) {
    if (!device_ || !rtv_) return;

    const float t = static_cast<float>(elapsed);

    // Scene layout. The planet is tilted so the rings are seen at a shallow
    // angle - that is what makes them read as a disc rather than a line, and it
    // is also the angle at which the two bodies shadow each other most legibly.
    const Mat4 planetTilt = multiply(rotationZ(0.468f), rotationX(0.0f));
    const Mat4 planetWorld = multiply(rotationY(t * 0.22f), planetTilt);
    const Mat4 ringWorld = multiply(rotationY(t * 0.40f), planetTilt);
    // The moon rides just outside the ring, on its own orbit.
    const float moonAngle = t * 0.55f;
    const Mat4 moonWorld = multiply(
        multiply(translation(4.35f, 0.0f, 0.0f), rotationY(moonAngle)),
        multiply(planetTilt, translation(0.0f, 0.0f, 0.0f)));

    // Key light from the upper left and slightly behind the camera plane, so
    // the ring's shadow falls across the globe's face and the globe's own
    // shadow lands on the far limb of the rings - both on the visible side.
    const float lightDir[3] = {0.55f, -0.62f, 0.56f};
    const float lightDistance = 20.0f;
    const Mat4 lightView = lookAtLH(-lightDir[0] * lightDistance,
                                    -lightDir[1] * lightDistance,
                                    -lightDir[2] * lightDistance,
                                    0.0f, 0.0f, 0.0f);
    lightViewProj_ = multiply(lightView, orthographicLH(12.0f, 12.0f, 1.0f, 34.0f));

    // --- Shadow pass ------------------------------------------------------
    // Solid bodies first, with front-face culling, then the ring sheet with no
    // culling. Both write into the same map, so the ring receives the planet's
    // shadow and the planet receives the ring's.
    ID3D10ShaderResourceView* noTexture = nullptr;
    device_->PSSetShaderResources(0, 1, &noTexture);
    device_->PSSetShaderResources(1, 1, &noTexture);
    device_->OMSetRenderTargets(0, nullptr, shadowDsv_);
    device_->ClearDepthStencilView(shadowDsv_, D3D10_CLEAR_DEPTH, 1.0f, 0);
    D3D10_VIEWPORT shadowVp = {};
    shadowVp.Width = static_cast<UINT>(kShadowSize);
    shadowVp.Height = static_cast<UINT>(kShadowSize);
    shadowVp.MaxDepth = 1.0f;
    device_->RSSetViewports(1, &shadowVp);
    device_->OMSetDepthStencilState(depthWrite_, 0);
    device_->PSSetShader(nullptr);
    device_->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    device_->VSSetShader(vs_);
    device_->VSSetConstantBuffers(0, 1, &constantBuffer_);

    device_->RSSetState(rasterShadow_);
    drawShadow(frame_, planetWorld);
    drawShadow(shuttle_, moonWorld);
    device_->RSSetState(rasterShadowSheet_);
    drawShadow(panel_, ringWorld);

    // --- Main pass --------------------------------------------------------
    const float clear[4] = {0.055f, 0.065f, 0.10f, 1.0f};
    device_->OMSetRenderTargets(1, &rtv_, dsv_);
    device_->ClearRenderTargetView(rtv_, clear);
    device_->ClearDepthStencilView(dsv_, D3D10_CLEAR_DEPTH | D3D10_CLEAR_STENCIL,
                                   1.0f, 0);

    D3D10_VIEWPORT vp = {};
    vp.Width = static_cast<UINT>(width_);
    vp.Height = static_cast<UINT>(height_);
    vp.MaxDepth = 1.0f;
    device_->RSSetViewports(1, &vp);

    device_->IASetInputLayout(inputLayout_);
    device_->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    device_->VSSetShader(vs_);
    device_->VSSetConstantBuffers(0, 1, &constantBuffer_);
    // gLight / gTint / gWorld are read by the pixel shader as well, so the
    // constant buffer has to be bound to the pixel stage too.
    device_->PSSetConstantBuffers(0, 1, &constantBuffer_);
    device_->PSSetShader(ps_);
    device_->PSSetShaderResources(1, 1, &shadowSrv_);
    device_->PSSetSamplers(1, 1, &shadowSampler_);

    // Stars first: unlit, additive, no depth writes. Sparse on purpose - they
    // are punctuation, not a background layer.
    static const float kStarHome[6][3] = {
        {-8.6f, 5.9f, -2.4f}, {8.9f, 4.4f, -3.6f}, {-7.2f, -1.2f, 3.4f},
        {7.8f, -2.0f, 2.9f},  {-9.4f, 2.4f, 5.1f}, {9.2f, 1.1f, 4.6f},
    };
    static const float kStarSize[6] = {0.20f, 0.15f, 0.10f, 0.23f, 0.12f, 0.17f};
    static const float kStarGlow[6] = {1.00f, 0.82f, 0.58f, 0.95f, 0.66f, 0.88f};

    device_->RSSetState(rasterCullNone_);
    device_->OMSetDepthStencilState(depthNoWrite_, 0);
    device_->OMSetBlendState(blendAdditive_, nullptr, 0xFFFFFFFF);
    const Material starMaterial =
        makeMaterial(1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1);
    for (int i = 0; i < 6; ++i) {
        const float phase = static_cast<float>(i) * 2.1f;
        const float drift = 0.45f;
        const float px = kStarHome[i][0] + drift * std::sin(t * 0.38f + phase);
        const float py = kStarHome[i][1] + drift * std::cos(t * 0.29f + phase * 1.3f);
        const float pz = kStarHome[i][2] + drift * std::sin(t * 0.23f + phase * 0.7f);
        const float s = kStarSize[i];
        const Mat4 world = multiply(scaling(s, s, s), translation(px, py, pz));
        Material star = starMaterial;
        star.tint[0] = star.tint[1] = star.tint[2] = kStarGlow[i];
        drawMesh(stars_, world, starSprite_, clampSampler_, star);
    }

    // The planet: a lit sphere. Its shading is mostly Lambert plus a broad
    // specular sheen, because a gas giant is a matte body with a soft highlight
    // and no hard reflections.
    device_->RSSetState(rasterCullBack_);
    device_->OMSetDepthStencilState(depthWrite_, 0);
    device_->OMSetBlendState(blendOpaque_, nullptr, 0xFFFFFFFF);
    const Material planetMaterial =
        makeMaterial(1.06f, 1.00f, 0.90f, 1.0f,
                     0.20f, 0.20f, 0.22f, 26.0f,
                     0.26f, 0.30f, 0.40f, 0.40f,
                     0.70f, 1.0f, 0.0f);
    drawMesh(frame_, planetWorld, frameTexture_, clampSampler_, planetMaterial);

    // The moon, further out and unshadowed by the rings.
    const Material moonMaterial =
        makeMaterial(1.00f, 0.99f, 0.96f, 1.0f,
                     0.16f, 0.16f, 0.17f, 20.0f,
                     0.22f, 0.26f, 0.34f, 0.45f,
                     0.65f, 1.0f, 0.0f);
    drawMesh(shuttle_, moonWorld, frameTexture_, clampSampler_, moonMaterial);

    // The rings. They are a single-sided sheet, so back-face culling would make
    // the far half vanish; they are drawn with alpha blending and depth writes
    // off, after the planet, and they read depth so the planet occludes the
    // part of the ring that passes behind it.
    device_->RSSetState(rasterCullNone_);
    device_->OMSetDepthStencilState(depthWrite_, 0);
    device_->OMSetBlendState(blendAlpha_, nullptr, 0xFFFFFFFF);
    const Material ringMaterial =
        makeMaterial(1.00f, 0.97f, 0.92f, 1.0f,
                     0.10f, 0.10f, 0.11f, 16.0f,
                     0.30f, 0.34f, 0.44f, 0.30f,
                     0.62f, 1.0f, 0.0f);
    drawMesh(panel_, ringWorld, panelTexture_, clampSampler_, ringMaterial);

    swapChain_->Present(0, 0);
}

void Dx10Tool::shutdown() {
    releaseSizeDependent();
    if (shadowSrv_) shadowSrv_->Release();
    if (shadowDsv_) shadowDsv_->Release();
    if (shadowTex_) shadowTex_->Release();
    if (shadowSampler_) shadowSampler_->Release();
    if (blendAdditive_) blendAdditive_->Release();
    if (blendAlpha_) blendAlpha_->Release();
    if (blendOpaque_) blendOpaque_->Release();
    if (depthNoWrite_) depthNoWrite_->Release();
    if (depthWrite_) depthWrite_->Release();
    if (rasterShadowSheet_) rasterShadowSheet_->Release();
    if (rasterShadow_) rasterShadow_->Release();
    if (rasterCullNone_) rasterCullNone_->Release();
    if (rasterCullBack_) rasterCullBack_->Release();
    if (clampSampler_) clampSampler_->Release();
    for (ID3D10ShaderResourceView** srv : {&starSprite_, &panelTexture_, &frameTexture_}) {
        if (*srv) { (*srv)->Release(); *srv = nullptr; }
    }
    GpuMesh* meshes[4] = {&frame_, &panel_, &shuttle_, &stars_};
    for (GpuMesh* m : meshes) {
        if (m->ib) m->ib->Release();
        if (m->vb) m->vb->Release();
    }
    if (constantBuffer_) constantBuffer_->Release();
    if (inputLayout_) inputLayout_->Release();
    if (ps_) ps_->Release();
    if (vs_) vs_->Release();
    if (swapChain_) swapChain_->Release();
    if (device_) device_->Release();
}

Tool* createDx10() { return new Dx10Tool(); }

}  // namespace render
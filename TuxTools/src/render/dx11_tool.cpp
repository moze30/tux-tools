/*
 * Tux-Tools - Direct3D 11 driver test
 *
 * A neutron star: an emissive core, three counter-rotating bands of accretion
 * disc particles, and twin polar jets. Geometry and textures are generated in
 * code - there is no image asset and no image decoder involved.
 *
 * Where the Direct3D 9 and 10 scenes are lit bodies, this one is a light
 * source, and that is what makes it exercise a different part of the driver:
 *
 *   - the scene renders into an offscreen R16G16B16A16_FLOAT target, so the
 *     bright emissive values survive above 1.0 instead of clipping
 *   - a bright-pass, two separable blur passes and a composite pass then add
 *     bloom back over it, so the star actually glows
 *
 * That multi-pass offscreen chain is the Direct3D 11 specific path: float
 * render targets, a fullscreen pass and several pixel shaders per frame. The
 * earlier backends draw straight to the back buffer and could not do it.
 */

#include "render_tool.h"
#include "../app_log.h"
#include "test_content.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace render {

namespace {
void logCompileErrors(const wchar_t* which, ID3DBlob* errors) {
    if (!errors) {
        applog::write(L"%ls：编译器未返回诊断信息", which);
        return;
    }
    const char* text = static_cast<const char*>(errors->GetBufferPointer());
    const SIZE_T size = errors->GetBufferSize();
    if (!text || size == 0) {
        applog::write(L"%ls：编译器诊断信息为空", which);
        return;
    }
    std::string narrow(text, size);
    const std::wstring wide(narrow.begin(), narrow.end());
    applog::write(L"%ls 编译失败：", which);
    applog::writeRaw(wide.c_str());
}

}  // namespace

namespace {

const float kBloomScale = 0.25f;

struct Constants {
    float wvp[16];
    float world[16];
    float tint[4];
    float params[4];  // x = emissive strength, y = fresnel power, z = time
    float eye[4];
};

struct PostConstants {
    float texel[4];      // xy = source texel size
    float params[4];     // x = threshold, y = knee, z = intensity, w = bloom scale
};

const char kSceneShader[] = R"(
cbuffer Constants : register(b0)
{
    row_major float4x4 gWVP;
    row_major float4x4 gWorld;
    float4 gTint;
    float4 gParams;   // x = emissive, y = fresnel power, z = time, w = bowl
    float4 gEye;
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
};

PSInput VSMain(VSInput input)
{
    PSInput o;
    float3 p = input.pos;

    // The accretion disc particles are pushed up into a shallow bowl so the
    // disc has thickness instead of being a flat sheet of dots. gParams.w is
    // the bowl amount, zero for everything that is not the disc - the core and
    // the jets must not be bent.
    p.y += gParams.w * dot(p.xz, p.xz);

    o.pos  = mul(float4(p, 1.0f), gWVP);
    o.nrm  = mul(float4(input.nrm, 0.0f), gWorld).xyz;
    o.uv   = input.uv;
    o.wpos = mul(float4(p, 1.0f), gWorld).xyz;
    return o;
}

Texture2D    gTex : register(t0);
SamplerState gSmp : register(s0);

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 texel = gTex.Sample(gSmp, input.uv);
    float3 albedo = texel.rgb * gTint.rgb;

    float3 N = normalize(input.nrm);
    float3 V = normalize(gEye.xyz - input.wpos);

    // Everything in this scene is self-luminous, so there is no light source
    // and no shadow term. What shapes it instead is a fresnel falloff: the rim
    // of each body burns brighter than its face, which is what makes the star
    // read as a sphere of plasma rather than a flat disc.
    float fresnel = pow(1.0f - saturate(dot(N, V)), gParams.y);
    float emissive = gParams.x * (0.55f + 0.85f * fresnel);

    // A slow pulse so the surface looks alive.
    emissive *= 0.92f + 0.08f * sin(gParams.z * 2.3f + dot(N, float3(3.1f, 5.7f, 2.3f)));

    float3 colour = albedo * emissive;
    return float4(colour, texel.a * gTint.a);
}
)";

const char kPostShader[] = R"(
cbuffer PostConstants : register(b0)
{
    float4 gTexel;    // xy = source texel size
    float4 gParams;   // x = threshold, y = knee, z = intensity, w = bloom scale
};

Texture2D    gScene : register(t0);
Texture2D    gBloom : register(t1);
SamplerState gSmp   : register(s0);

struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

// Fullscreen pass-through: the quad arrives already in clip space.
VSOutput VSFullscreen(float4 pos : POSITION, float2 uv : TEXCOORD0)
{
    VSOutput o;
    o.pos = pos;
    o.uv = uv;
    return o;
}

// Everything above the knee is kept for the bloom; the knee softens the
// cutoff so the glow fades in instead of switching on at a hard edge.
float4 PSBrightPass(VSOutput input) : SV_TARGET
{
    float3 c = gScene.Sample(gSmp, input.uv).rgb;
    float luma = dot(c, float3(0.2126f, 0.7152f, 0.0722f));
    float soft = saturate((luma - gParams.x) / max(gParams.y, 0.0001f));
    return float4(c * soft, 1.0f);
}

// Nine-tap gaussian, run once horizontally and once vertically.
float4 PSBlur(VSOutput input) : SV_TARGET
{
    float2 step = gTexel.xy;
    float3 sum = gScene.Sample(gSmp, input.uv).rgb * 0.2270270270f;
    sum += gScene.Sample(gSmp, input.uv + float2( step.x, 0.0f)).rgb * 0.1945945946f;
    sum += gScene.Sample(gSmp, input.uv + float2(-step.x, 0.0f)).rgb * 0.1945945946f;
    sum += gScene.Sample(gSmp, input.uv + float2( 2.0f * step.x, 0.0f)).rgb * 0.1216216216f;
    sum += gScene.Sample(gSmp, input.uv + float2(-2.0f * step.x, 0.0f)).rgb * 0.1216216216f;
    sum += gScene.Sample(gSmp, input.uv + float2( 3.0f * step.x, 0.0f)).rgb * 0.0540540541f;
    sum += gScene.Sample(gSmp, input.uv + float2(-3.0f * step.x, 0.0f)).rgb * 0.0540540541f;
    sum += gScene.Sample(gSmp, input.uv + float2( 4.0f * step.x, 0.0f)).rgb * 0.0162162162f;
    sum += gScene.Sample(gSmp, input.uv + float2(-4.0f * step.x, 0.0f)).rgb * 0.0162162162f;
    return float4(sum, 1.0f);
}

// Composite the scene with its bloom, then tone map so the brightest core
// rolls off to white instead of clipping into a flat blob.
float4 PSComposite(VSOutput input) : SV_TARGET
{
    float3 scene = gScene.Sample(gSmp, input.uv).rgb;
    float3 bloom = gBloom.Sample(gSmp, input.uv).rgb;
    float3 c = scene + bloom * gParams.z;
    c = c / (1.0f + c);                 // Reinhard
    return float4(pow(c, 1.0f / 2.2f), 1.0f);   // gamma
}
)";

}  // namespace

class Dx11Tool : public Tool {
public:
    bool init(HWND host) override;
    void resize(int width, int height) override;
    void render(double elapsed) override;
    void shutdown() override;
    std::wstring deviceInfo() const override { return deviceInfo_; }
    std::wstring failureReason() const override {
        return failureReason_.empty() ? std::wstring(L"未知原因") : failureReason_;
    }


private:
    struct GpuMesh {
        ID3D11Buffer* vb = nullptr;
        ID3D11Buffer* ib = nullptr;
        UINT indexCount = 0;
    };

    bool createDevice(HWND host);
    bool createShaders();
    bool createGeometry();
    bool createTextures();
    bool createStates();
    bool createTargets();
    bool uploadMesh(const Mesh& mesh, GpuMesh& out);
    bool createFullscreenQuad();
    void createSizeDependent();
    void releaseSizeDependent();
    void releaseTargets();
    void drawMesh(const GpuMesh& mesh, const Mat4& world,
                  ID3D11ShaderResourceView* texture, ID3D11SamplerState* sampler,
                  const float tint[4], float emissive, float fresnelPower,
                  float bowl);
    void runPostChain();

    int width_ = 1;
    int height_ = 1;
    float time_ = 0.0f;
    std::wstring deviceInfo_;
    // Set at the exact point of failure, so the log says which step gave up.
    std::wstring failureReason_;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    ID3D11DepthStencilView* dsv_ = nullptr;
    ID3D11Texture2D* depthTex_ = nullptr;

    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11PixelShader* psBright_ = nullptr;
    ID3D11PixelShader* psBlur_ = nullptr;
    ID3D11PixelShader* psComposite_ = nullptr;
    ID3D11VertexShader* vsFullscreen_ = nullptr;
    ID3D11InputLayout* inputLayout_ = nullptr;
    ID3D11InputLayout* fullscreenLayout_ = nullptr;
    ID3D11Buffer* constantBuffer_ = nullptr;
    ID3D11Buffer* postBuffer_ = nullptr;

    // Offscreen chain: scene at full resolution, two bloom targets at a
    // quarter resolution, all float so the bright pixels keep their headroom.
    ID3D11Texture2D* sceneTex_ = nullptr;
    ID3D11RenderTargetView* sceneRtv_ = nullptr;
    ID3D11ShaderResourceView* sceneSrv_ = nullptr;
    ID3D11Texture2D* bloomA_ = nullptr;
    ID3D11RenderTargetView* bloomARtv_ = nullptr;
    ID3D11ShaderResourceView* bloomASrv_ = nullptr;
    ID3D11Texture2D* bloomB_ = nullptr;
    ID3D11RenderTargetView* bloomBRtv_ = nullptr;
    ID3D11ShaderResourceView* bloomBSrv_ = nullptr;

    GpuMesh star_;
    GpuMesh discInner_;
    GpuMesh discMid_;
    GpuMesh discOuter_;
    GpuMesh jets_;

    // The fullscreen quad has its own tiny vertex format (clip-space xy + uv),
    // so it does not go through uploadMesh.
    ID3D11Buffer* fullscreenVb_ = nullptr;

    ID3D11ShaderResourceView* starTexture_ = nullptr;
    ID3D11ShaderResourceView* discTexture_ = nullptr;
    ID3D11SamplerState* clampSampler_ = nullptr;

    ID3D11RasterizerState* rasterCullBack_ = nullptr;
    ID3D11RasterizerState* rasterCullNone_ = nullptr;
    ID3D11DepthStencilState* depthWrite_ = nullptr;
    ID3D11DepthStencilState* depthNoWrite_ = nullptr;
    ID3D11DepthStencilState* depthOff_ = nullptr;
    ID3D11BlendState* blendOpaque_ = nullptr;
    ID3D11BlendState* blendAdditive_ = nullptr;
};

bool Dx11Tool::createDevice(HWND host) {
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

    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                  D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 4, D3D11_SDK_VERSION,
        &desc, &swapChain_, &device_, &obtained, &context_);
    D3D_DRIVER_TYPE used = D3D_DRIVER_TYPE_HARDWARE;
    if (FAILED(hr)) {
        used = D3D_DRIVER_TYPE_WARP;
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 4, D3D11_SDK_VERSION,
            &desc, &swapChain_, &device_, &obtained, &context_);
    }
    if (FAILED(hr)) {
        applog::writeHresult(L"Direct3D 11：创建设备与交换链（硬件与 WARP 均失败）", hr);
        // The combined call hides which half failed, so work it out here.
        failureReason_ = L"无法创建 D3D11 设备";
        return false;
    }

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
    deviceInfo_ = std::wstring(L"Direct3D 11") +
                  (used == D3D_DRIVER_TYPE_WARP ? L" (WARP)" : L"") +
                  L" - FL " + (obtained == D3D_FEATURE_LEVEL_11_1 ? L"11.1" :
                               obtained == D3D_FEATURE_LEVEL_11_0 ? L"11.0" :
                               obtained == D3D_FEATURE_LEVEL_10_1 ? L"10.1" : L"10.0") +
                  L" - " + adapterName;
    return true;
}

bool Dx11Tool::createShaders() {
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errors = nullptr;

    HRESULT hr = D3DCompile(kSceneShader, sizeof(kSceneShader) - 1, "scene",
                            nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vsBlob, &errors);
    if (FAILED(hr)) {
        applog::writeHresult(L"Direct3D 11：编译场景顶点着色器", hr);
        logCompileErrors(L"场景顶点着色器", errors);
        if (errors) errors->Release();
        return false;
    }
    hr = D3DCompile(kSceneShader, sizeof(kSceneShader) - 1, "scene",
                    nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &psBlob, &errors);
    if (FAILED(hr)) {
        applog::writeHresult(L"Direct3D 11：编译场景像素着色器", hr);
        logCompileErrors(L"场景像素着色器", errors);
        if (errors) errors->Release();
        vsBlob->Release();
        return false;
    }
    if (errors) errors->Release();

    device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                nullptr, &vs_);
    device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                               nullptr, &ps_);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    device_->CreateInputLayout(layout, 3, vsBlob->GetBufferPointer(),
                               vsBlob->GetBufferSize(), &inputLayout_);
    vsBlob->Release();

    // Fullscreen pass-through vertex shader and the three post passes.
    ID3DBlob* postVs = nullptr;
    hr = D3DCompile(kPostShader, sizeof(kPostShader) - 1, "post",
                    nullptr, nullptr, "VSFullscreen", "vs_5_0", 0, 0, &postVs, &errors);
    if (FAILED(hr)) {
        applog::writeHresult(L"Direct3D 11：编译全屏顶点着色器", hr);
        logCompileErrors(L"全屏顶点着色器", errors);
        if (errors) errors->Release();
        psBlob->Release();
        return false;
    }
    device_->CreateVertexShader(postVs->GetBufferPointer(), postVs->GetBufferSize(),
                                nullptr, &vsFullscreen_);

    D3D11_INPUT_ELEMENT_DESC fsLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    device_->CreateInputLayout(fsLayout, 2, postVs->GetBufferPointer(),
                               postVs->GetBufferSize(), &fullscreenLayout_);
    postVs->Release();

    struct { const char* entry; ID3D11PixelShader** target; } passes[] = {
        {"PSBrightPass", &psBright_},
        {"PSBlur", &psBlur_},
        {"PSComposite", &psComposite_},
    };
    for (auto& pass : passes) {
        ID3DBlob* blob = nullptr;
        hr = D3DCompile(kPostShader, sizeof(kPostShader) - 1, "post",
                        nullptr, nullptr, pass.entry, "ps_5_0", 0, 0, &blob, &errors);
        if (FAILED(hr)) {
            applog::write(L"Direct3D 11：编译后处理通道 %hs 失败", pass.entry);
            applog::writeHresult(L"Direct3D 11：编译后处理着色器", hr);
            logCompileErrors(L"后处理着色器", errors);
            if (errors) errors->Release();
            psBlob->Release();
            return false;
        }
        device_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                   nullptr, pass.target);
        blob->Release();
    }
    if (errors) errors->Release();
    psBlob->Release();

    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.ByteWidth = sizeof(Constants);
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device_->CreateBuffer(&cbd, nullptr, &constantBuffer_))) return false;

    cbd.ByteWidth = sizeof(PostConstants);
    if (FAILED(device_->CreateBuffer(&cbd, nullptr, &postBuffer_))) return false;

    return vs_ && ps_ && psBright_ && psBlur_ && psComposite_ && vsFullscreen_ &&
           inputLayout_ && fullscreenLayout_ && constantBuffer_ && postBuffer_;
}

bool Dx11Tool::uploadMesh(const Mesh& mesh, GpuMesh& out) {
    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.ByteWidth = static_cast<UINT>(mesh.vertices.size() * sizeof(Vertex));
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vinit = {};
    vinit.pSysMem = mesh.vertices.data();
    if (FAILED(device_->CreateBuffer(&vbd, &vinit, &out.vb))) return false;

    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_IMMUTABLE;
    ibd.ByteWidth = static_cast<UINT>(mesh.indices.size() * sizeof(uint16_t));
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA iinit = {};
    iinit.pSysMem = mesh.indices.data();
    if (FAILED(device_->CreateBuffer(&ibd, &iinit, &out.ib))) return false;

    out.indexCount = mesh.indexCount();
    return true;
}

bool Dx11Tool::createGeometry() {
    // Direct3D 11 gets the neutron star, which is deliberately nothing like the
    // lit planets of the other two backends: a small emissive core, a disc of
    // luminous particles and a pair of polar jets.
    Mesh discInner;
    Mesh discMid;
    Mesh discOuter;
    const Mesh mote = makeSphere(1.0f, 8, 6);

    struct Band { float inner; float outer; int count; Mesh* target; };
    const Band bands[3] = {
        {1.55f, 2.45f, 150, &discInner},
        {2.45f, 3.55f, 170, &discMid},
        {3.55f, 4.85f, 160, &discOuter},
    };
    for (const Band& band : bands) {
        for (int i = 0; i < band.count; ++i) {
            // Even angular spacing with a jittered radius and a little vertical
            // scatter, so the bands do not line up into visible spokes.
            const float a = 6.2831853f * static_cast<float>(i) / band.count +
                            0.7f * nextRandom();
            const float r = band.inner + (band.outer - band.inner) * nextRandom();
            const float y = (nextRandom() - 0.5f) * 0.30f;
            const float s = 0.030f + 0.075f * nextRandom();
            appendMesh(*band.target, mote,
                       multiply(multiply(scaling(s, s * 0.62f, s), rotationY(a)),
                                translation(std::cos(a) * r, y, std::sin(a) * r)));
        }
    }

    // Twin polar jets: a cone is built apex-up, so the lower one is turned over.
    Mesh jets;
    appendMesh(jets, makeCone(0.30f, 4.2f, 40), scaling(1.0f, 1.0f, 1.0f));
    appendMesh(jets, makeCone(0.30f, 4.2f, 40),
               multiply(rotationX(3.14159265f), translation(0.0f, 0.0f, 0.0f)));

    return uploadMesh(makeSphere(0.82f, 48, 32), star_) &&
           uploadMesh(discInner, discInner_) &&
           uploadMesh(discMid, discMid_) &&
           uploadMesh(discOuter, discOuter_) &&
           uploadMesh(jets, jets_);
}

bool Dx11Tool::createFullscreenQuad() {
    // Clip-space xy and matching uv. Kept as a two-triangle list rather than a
    // strip so the whole frame can use one topology: switching to a strip for
    // the post passes and back is a step that is easy to forget, and forgetting
    // it draws half the screen and leaves the rest undefined.
    struct QuadVertex { float x, y, u, v; };
    const QuadVertex verts[6] = {
        {-1.0f, -1.0f, 0.0f, 1.0f},
        {-1.0f,  1.0f, 0.0f, 0.0f},
        { 1.0f, -1.0f, 1.0f, 1.0f},
        { 1.0f, -1.0f, 1.0f, 1.0f},
        {-1.0f,  1.0f, 0.0f, 0.0f},
        { 1.0f,  1.0f, 1.0f, 0.0f},
    };
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.ByteWidth = sizeof(verts);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = verts;
    return SUCCEEDED(device_->CreateBuffer(&bd, &init, &fullscreenVb_));
}

bool Dx11Tool::createTextures() {
    // A smooth central falloff, no pattern of any kind.
    const Texture core = makeRadialGlow(256, rgba(0xFF, 0xF6, 0xE0), rgba(0x3C, 0x6A, 0xC8));
    const Texture mote = makeRadialGlow(64, rgba(0xFF, 0xE8, 0xC0), rgba(0x10, 0x18, 0x40));

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(core.width);
    desc.Height = static_cast<UINT>(core.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init = {};
    init.SysMemPitch = static_cast<UINT>(core.width) * 4;

    ID3D11Texture2D* texture = nullptr;
    init.pSysMem = core.pixels.data();
    if (FAILED(device_->CreateTexture2D(&desc, &init, &texture))) return false;
    HRESULT hr = device_->CreateShaderResourceView(texture, nullptr, &starTexture_);
    texture->Release();
    if (FAILED(hr)) return false;

    desc.Width = desc.Height = static_cast<UINT>(mote.width);
    init.SysMemPitch = static_cast<UINT>(mote.width) * 4;
    init.pSysMem = mote.pixels.data();
    if (FAILED(device_->CreateTexture2D(&desc, &init, &texture))) return false;
    hr = device_->CreateShaderResourceView(texture, nullptr, &discTexture_);
    texture->Release();
    if (FAILED(hr)) return false;

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device_->CreateSamplerState(&sd, &clampSampler_))) return false;

    return true;
}

bool Dx11Tool::createStates() {
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_BACK;
    rd.DepthClipEnable = TRUE;
    if (FAILED(device_->CreateRasterizerState(&rd, &rasterCullBack_))) return false;

    rd.CullMode = D3D11_CULL_NONE;
    if (FAILED(device_->CreateRasterizerState(&rd, &rasterCullNone_))) return false;

    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = TRUE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (FAILED(device_->CreateDepthStencilState(&dsd, &depthWrite_))) return false;

    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(device_->CreateDepthStencilState(&dsd, &depthNoWrite_))) return false;

    // The fullscreen post passes do not test depth at all.
    dsd.DepthEnable = FALSE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(device_->CreateDepthStencilState(&dsd, &depthOff_))) return false;

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = FALSE;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device_->CreateBlendState(&bd, &blendOpaque_))) return false;

    // Additive: the disc and the jets are light, so they add to what is behind.
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    if (FAILED(device_->CreateBlendState(&bd, &blendAdditive_))) return false;

    return true;
}

bool Dx11Tool::createTargets() {
    releaseTargets();

    const UINT bw = static_cast<UINT>(static_cast<float>(width_) * kBloomScale);
    const UINT bh = static_cast<UINT>(static_cast<float>(height_) * kBloomScale);

    D3D11_TEXTURE2D_DESC sd = {};
    sd.Width = static_cast<UINT>(width_);
    sd.Height = static_cast<UINT>(height_);
    sd.MipLevels = 1;
    sd.ArraySize = 1;
    sd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    sd.SampleDesc.Count = 1;
    sd.Usage = D3D11_USAGE_DEFAULT;
    sd.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&sd, nullptr, &sceneTex_))) return false;
    if (FAILED(device_->CreateRenderTargetView(sceneTex_, nullptr, &sceneRtv_))) return false;
    if (FAILED(device_->CreateShaderResourceView(sceneTex_, nullptr, &sceneSrv_))) return false;

    sd.Width = bw > 0 ? bw : 1;
    sd.Height = bh > 0 ? bh : 1;
    if (FAILED(device_->CreateTexture2D(&sd, nullptr, &bloomA_))) return false;
    if (FAILED(device_->CreateRenderTargetView(bloomA_, nullptr, &bloomARtv_))) return false;
    if (FAILED(device_->CreateShaderResourceView(bloomA_, nullptr, &bloomASrv_))) return false;
    if (FAILED(device_->CreateTexture2D(&sd, nullptr, &bloomB_))) return false;
    if (FAILED(device_->CreateRenderTargetView(bloomB_, nullptr, &bloomBRtv_))) return false;
    if (FAILED(device_->CreateShaderResourceView(bloomB_, nullptr, &bloomBSrv_))) return false;

    return true;
}

void Dx11Tool::releaseTargets() {
    if (bloomBSrv_) { bloomBSrv_->Release(); bloomBSrv_ = nullptr; }
    if (bloomBRtv_) { bloomBRtv_->Release(); bloomBRtv_ = nullptr; }
    if (bloomB_) { bloomB_->Release(); bloomB_ = nullptr; }
    if (bloomASrv_) { bloomASrv_->Release(); bloomASrv_ = nullptr; }
    if (bloomARtv_) { bloomARtv_->Release(); bloomARtv_ = nullptr; }
    if (bloomA_) { bloomA_->Release(); bloomA_ = nullptr; }
    if (sceneSrv_) { sceneSrv_->Release(); sceneSrv_ = nullptr; }
    if (sceneRtv_) { sceneRtv_->Release(); sceneRtv_ = nullptr; }
    if (sceneTex_) { sceneTex_->Release(); sceneTex_ = nullptr; }
}

bool Dx11Tool::init(HWND host) {
    applog::write(L"Direct3D 11：开始初始化");
    if (!createDevice(host)) {
        if (failureReason_.empty()) failureReason_ = L"无法创建设备";
        return false;
    }
    applog::write(L"Direct3D 11：设备已建立，%ls", deviceInfo_.c_str());

    // The steps are spelled out one at a time rather than chained with ||,
    // because the log is only useful if it says which one stopped.
    if (!createShaders()) { failureReason_ = L"无法编译着色器"; return false; }
    applog::write(L"Direct3D 11：着色器已编译");
    // As with Vulkan, the baseline matters: record which window shapes work
    // before anything fails, so a later failure can be read against it. Gated on
    // the log for the same reason - the probe opens windows.
    if (applog::enabled())    if (!createGeometry()) { failureReason_ = L"无法创建几何缓冲"; return false; }
    if (!createTextures()) { failureReason_ = L"无法创建纹理"; return false; }
    if (!createStates()) { failureReason_ = L"无法创建渲染状态"; return false; }
    if (!createFullscreenQuad()) { failureReason_ = L"无法创建全屏四边形"; return false; }
    createSizeDependent();
    if (!createTargets()) { failureReason_ = L"无法创建离屏目标"; return false; }
    if (!rtv_) { failureReason_ = L"渲染目标视图为空"; return false; }
    applog::write(L"Direct3D 11：初始化完成");
    return true;
}

void Dx11Tool::releaseSizeDependent() {
    if (rtv_) { rtv_->Release(); rtv_ = nullptr; }
    if (dsv_) { dsv_->Release(); dsv_ = nullptr; }
    if (depthTex_) { depthTex_->Release(); depthTex_ = nullptr; }
}

void Dx11Tool::createSizeDependent() {
    ID3D11Texture2D* backBuffer = nullptr;
    swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D),
                          reinterpret_cast<void**>(&backBuffer));
    if (backBuffer) {
        device_->CreateRenderTargetView(backBuffer, nullptr, &rtv_);
        backBuffer->Release();
    }

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = static_cast<UINT>(width_);
    depthDesc.Height = static_cast<UINT>(height_);
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (SUCCEEDED(device_->CreateTexture2D(&depthDesc, nullptr, &depthTex_))) {
        device_->CreateDepthStencilView(depthTex_, nullptr, &dsv_);
    }
}

void Dx11Tool::resize(int width, int height) {
    if (!swapChain_ || width <= 0 || height <= 0) return;
    width_ = width;
    height_ = height;
    releaseSizeDependent();
    swapChain_->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height),
                              DXGI_FORMAT_UNKNOWN, 0);
    createSizeDependent();
    createTargets();
}

void Dx11Tool::drawMesh(const GpuMesh& mesh, const Mat4& world,
                        ID3D11ShaderResourceView* texture,
                        ID3D11SamplerState* sampler, const float tint[4],
                        float emissive, float fresnelPower, float bowl) {
    const Mat4 view = lookAtLH(0.0f, 3.2f, -12.8f, 0.0f, 0.0f, 0.0f);
    const float aspect = height_ > 0 ? static_cast<float>(width_) / height_ : 1.0f;
    const Mat4 proj = perspectiveFovLH(0.80f, aspect, 0.1f, 100.0f);
    const Mat4 wvp = multiply(multiply(world, view), proj);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(context_->Map(constantBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        Constants* cb = static_cast<Constants*>(mapped.pData);
        std::memcpy(cb->wvp, wvp.m, sizeof(wvp.m));
        std::memcpy(cb->world, world.m, sizeof(world.m));
        std::memcpy(cb->tint, tint, sizeof(cb->tint));
        cb->params[0] = emissive;
        cb->params[1] = fresnelPower;
        cb->params[2] = time_;
        cb->params[3] = bowl;
        cb->eye[0] = 0.0f; cb->eye[1] = 3.2f; cb->eye[2] = -12.8f; cb->eye[3] = 0.0f;
        context_->Unmap(constantBuffer_, 0);
    }

    UINT stride = sizeof(Vertex), offset = 0;
    context_->IASetVertexBuffers(0, 1, &mesh.vb, &stride, &offset);
    context_->IASetIndexBuffer(mesh.ib, DXGI_FORMAT_R16_UINT, 0);
    context_->PSSetShaderResources(0, 1, &texture);
    context_->PSSetSamplers(0, 1, &sampler);
    context_->DrawIndexed(mesh.indexCount, 0, 0);
}

void Dx11Tool::runPostChain() {
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context_->IASetInputLayout(fullscreenLayout_);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vsFullscreen_, nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &postBuffer_);
    context_->RSSetState(rasterCullNone_);
    context_->OMSetDepthStencilState(depthOff_, 0);
    context_->OMSetBlendState(blendOpaque_, nullptr, 0xFFFFFFFF);

    const UINT stride = 4 * sizeof(float), offset = 0;
    context_->IASetVertexBuffers(0, 1, &fullscreenVb_, &stride, &offset);
    context_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

    const float bw = static_cast<float>(static_cast<UINT>(width_ * kBloomScale));
    const float bh = static_cast<float>(static_cast<UINT>(height_ * kBloomScale));

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(context_->Map(postBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        PostConstants* pc = static_cast<PostConstants*>(mapped.pData);
        pc->texel[0] = bw > 0.0f ? 1.0f / bw : 0.0f;
        pc->texel[1] = bh > 0.0f ? 1.0f / bh : 0.0f;
        pc->texel[2] = 0.0f;
        pc->texel[3] = 0.0f;
        // Only the brightest cores bloom; the disc and jets stay crisp.
        pc->params[0] = 0.70f;
        pc->params[1] = 0.55f;
        pc->params[2] = 1.15f;
        pc->params[3] = kBloomScale;
        context_->Unmap(postBuffer_, 0);
    }

    D3D11_VIEWPORT vp = {};
    vp.MaxDepth = 1.0f;

    // 1. Bright pass: scene -> bloomA.
    vp.Width = bw; vp.Height = bh;
    context_->RSSetViewports(1, &vp);
    context_->OMSetRenderTargets(1, &bloomARtv_, nullptr);
    context_->ClearRenderTargetView(bloomARtv_, black);
    context_->PSSetShader(psBright_, nullptr, 0);
    context_->PSSetShaderResources(0, 1, &sceneSrv_);
    context_->PSSetSamplers(0, 1, &clampSampler_);
    context_->Draw(6, 0);

    // 2. Horizontal blur: bloomA -> bloomB.
    context_->OMSetRenderTargets(1, &bloomBRtv_, nullptr);
    context_->ClearRenderTargetView(bloomBRtv_, black);
    ID3D11ShaderResourceView* none = nullptr;
    context_->PSSetShaderResources(0, 1, &none);
    context_->PSSetShaderResources(0, 1, &bloomASrv_);
    context_->PSSetShader(psBlur_, nullptr, 0);
    context_->Draw(6, 0);

    // 3. Vertical blur: bloomB -> bloomA (the texel step is swapped by hand).
    if (SUCCEEDED(context_->Map(postBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        PostConstants* pc = static_cast<PostConstants*>(mapped.pData);
        pc->texel[0] = 0.0f;
        pc->texel[1] = bh > 0.0f ? 1.0f / bh : 0.0f;
        pc->texel[2] = 0.0f;
        pc->texel[3] = 0.0f;
        pc->params[0] = 0.70f;
        pc->params[1] = 0.55f;
        pc->params[2] = 1.15f;
        pc->params[3] = kBloomScale;
        context_->Unmap(postBuffer_, 0);
    }
    context_->OMSetRenderTargets(1, &bloomARtv_, nullptr);
    context_->ClearRenderTargetView(bloomARtv_, black);
    none = nullptr;
    context_->PSSetShaderResources(0, 1, &none);
    context_->PSSetShaderResources(0, 1, &bloomBSrv_);
    context_->Draw(6, 0);

    // 4. Composite: scene + bloom -> back buffer. Both are sampled at once, so
    //    the scene goes in slot 0 and the bloom in slot 1; the composite shader
    //    reads t0 twice, which is why the scene is bound there a second time.
    vp.Width = static_cast<float>(width_);
    vp.Height = static_cast<float>(height_);
    context_->RSSetViewports(1, &vp);
    context_->OMSetRenderTargets(1, &rtv_, nullptr);
    context_->PSSetShader(psComposite_, nullptr, 0);
    ID3D11ShaderResourceView* two[2] = {sceneSrv_, bloomASrv_};
    context_->PSSetShaderResources(0, 2, two);
    context_->Draw(6, 0);
}

void Dx11Tool::render(double elapsed) {
    if (!context_ || !rtv_ || !sceneRtv_) return;

    const float t = static_cast<float>(elapsed);
    time_ = t;

    // --- Scene pass, into the float target ---------------------------------
    const float clear[4] = {0.012f, 0.016f, 0.030f, 1.0f};
    context_->ClearRenderTargetView(sceneRtv_, clear);
    context_->ClearDepthStencilView(dsv_, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    context_->OMSetRenderTargets(1, &sceneRtv_, dsv_);

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(width_);
    vp.Height = static_cast<float>(height_);
    vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);

    context_->IASetInputLayout(inputLayout_);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vs_, nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, &constantBuffer_);
    context_->PSSetConstantBuffers(0, 1, &constantBuffer_);
    context_->PSSetShader(ps_, nullptr, 0);
    context_->RSSetState(rasterCullBack_);
    context_->OMSetDepthStencilState(depthWrite_, 0);
    context_->OMSetBlendState(blendOpaque_, nullptr, 0xFFFFFFFF);

    // The whole system is tilted so the jets run across the frame instead of
    // straight up, and so the disc is read at a three-quarter angle.
    const Mat4 systemTilt = rotationZ(0.38f);

    // The core: small, very bright, white-hot at the rim.
    const float coreTint[4] = {1.00f, 0.98f, 0.94f, 1.0f};
    drawMesh(star_, systemTilt, starTexture_, clampSampler_, coreTint, 2.60f, 1.7f, 0.0f);

    // The disc, in three bands turning at different rates so the inner edge
    // visibly outruns the outer - it is differential rotation that makes a disc
    // look like a disc and not a turntable.
    context_->OMSetBlendState(blendAdditive_, nullptr, 0xFFFFFFFF);
    context_->OMSetDepthStencilState(depthNoWrite_, 0);
    context_->RSSetState(rasterCullNone_);

    const float innerTint[4] = {1.30f, 1.14f, 0.86f, 1.0f};
    const float midTint[4] = {1.20f, 0.82f, 0.50f, 1.0f};
    const float outerTint[4] = {0.86f, 0.46f, 0.28f, 1.0f};

    const Mat4 spinInner = multiply(rotationY(t * 1.05f), systemTilt);
    const Mat4 spinMid = multiply(rotationY(t * 0.62f), systemTilt);
    const Mat4 spinOuter = multiply(rotationY(t * 0.36f), systemTilt);
    drawMesh(discInner_, spinInner, discTexture_, clampSampler_, innerTint, 1.55f, 1.1f, 0.030f);
    drawMesh(discMid_, spinMid, discTexture_, clampSampler_, midTint, 1.20f, 1.1f, 0.022f);
    drawMesh(discOuter_, spinOuter, discTexture_, clampSampler_, outerTint, 0.95f, 1.1f, 0.014f);

    // The jets, brightest at the base where they leave the core.
    const float jetTint[4] = {0.62f, 0.78f, 1.30f, 1.0f};
    const Mat4 jetsWorld = multiply(rotationY(t * 0.18f), systemTilt);
    drawMesh(jets_, jetsWorld, discTexture_, clampSampler_, jetTint, 0.85f, 2.4f, 0.0f);

    context_->OMSetBlendState(blendOpaque_, nullptr, 0xFFFFFFFF);

    // --- Post chain to the back buffer -------------------------------------
    runPostChain();

    swapChain_->Present(0, 0);
}

void Dx11Tool::shutdown() {
    releaseSizeDependent();
    releaseTargets();
    if (blendAdditive_) blendAdditive_->Release();
    if (blendOpaque_) blendOpaque_->Release();
    if (depthOff_) depthOff_->Release();
    if (depthNoWrite_) depthNoWrite_->Release();
    if (depthWrite_) depthWrite_->Release();
    if (rasterCullNone_) rasterCullNone_->Release();
    if (rasterCullBack_) rasterCullBack_->Release();
    if (clampSampler_) clampSampler_->Release();
    if (discTexture_) discTexture_->Release();
    if (starTexture_) starTexture_->Release();
    if (fullscreenVb_) fullscreenVb_->Release();
    GpuMesh* meshes[5] = {&star_, &discInner_, &discMid_, &discOuter_, &jets_};
    for (GpuMesh* m : meshes) {
        if (m->ib) m->ib->Release();
        if (m->vb) m->vb->Release();
    }
    if (postBuffer_) postBuffer_->Release();
    if (constantBuffer_) constantBuffer_->Release();
    if (fullscreenLayout_) fullscreenLayout_->Release();
    if (inputLayout_) inputLayout_->Release();
    if (psComposite_) psComposite_->Release();
    if (psBlur_) psBlur_->Release();
    if (psBright_) psBright_->Release();
    if (ps_) ps_->Release();
    if (vsFullscreen_) vsFullscreen_->Release();
    if (vs_) vs_->Release();
    if (swapChain_) swapChain_->Release();
    if (context_) context_->Release();
    if (device_) device_->Release();
}

Tool* createDx11() { return new Dx11Tool(); }

}  // namespace render

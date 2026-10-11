/*
 * Tux-Tools - Direct3D 12 driver test
 *
 * A black hole seen from close by: a black event horizon, an accretion disc
 * whose far side is bent up and over the hole into the classic photon ring,
 * and a starfield behind it. Geometry and textures are generated in code -
 * there is no image asset and no image decoder involved.
 *
 * Direct3D 12 is not just "Direct3D 11 with a new coat of paint", and this
 * backend is shaped by what is actually different about it:
 *
 *   - an explicit command queue, allocator and command list, recorded by hand
 *     and executed once per frame with a fence-based wait between frames
 *   - one root signature and one pipeline state object, created up front,
 *     since nothing is implicit here
 *   - explicit resource barriers: the back buffer is transitioned out of
 *     PRESENT before it is drawn to, and back before it is presented
 *   - a 256-byte-aligned constant buffer, because a constant buffer view is
 *     required to sit on that boundary
 *
 * Unlike the earlier backends there is no mesh at all. The whole frame is one
 * full-screen triangle, and every pixel traces a photon backwards from the eye
 * through the curved space around the hole. A vertex-displaced disc - the
 * obvious first approach - cannot produce the photon ring: the ring is light
 * that loops the hole, and no single placement of a flat vertex can express
 * that. Integrating the deflection per pixel can, and it also removes the
 * faceted shearing that a coarse displaced grid shows at the inner edge.
 */

#include "render_tool.h"
#include "../app_log.h"
#include "test_content.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace render {

namespace {

const int kFrameCount = 2;
// Direct3D 12 requires the constant buffer view to begin on a 256-byte
// boundary, so every draw uses its own aligned block.
const UINT kConstantStride = 256;

struct Constants {
    float eye[4];      // xyz = camera position, w = time in seconds
    float target[4];   // xyz = look-at point, w = aspect ratio
    float up[4];       // xyz = camera up, w = tan(fovY / 2)
    float disc[4];     // x = inner radius, y = outer, z = exposure, w = spin
};

const char kShaderSource[] = R"(
cbuffer Constants : register(b0)
{
    float4 gEye;
    float4 gTarget;
    float4 gUp;
    float4 gDisc;
};

static const float kHorizon = 1.0;
static const int   kSteps   = 128;
// Angular rate of the spiral density wave, radians per second. It sets the
// tempo of the whole animation: one full turn every 2*pi/kPatternSpeed
// seconds. Fast enough to be unmistakable at a glance, slow enough still to
// read as a massive object.
static const float kPatternSpeed = 0.95;

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

// One oversized triangle, no vertex or index buffer: SV_VertexID supplies the
// three corners and the rasteriser clips the overhang. Cheaper than a quad and
// it avoids the diagonal seam a two-triangle quad produces under interpolation.
PSInput VSMain(uint id : SV_VertexID)
{
    PSInput o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    o.pos = float4(o.ndc, 0.0, 1.0);
    return o;
}

// A cheap hash, used for the starfield behind the hole. It has to be stable per
// direction, so it is fed the quantised ray direction rather than a coordinate.
float hash13(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

// Value noise on a 2D lattice. Used to give the disc turbulent structure:
// a perfectly smooth emission ramp reads as a featureless sheet of caramel.
float vnoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash13(float3(i, 0.0));
    float b = hash13(float3(i + float2(1.0, 0.0), 0.0));
    float c = hash13(float3(i + float2(0.0, 1.0), 0.0));
    float d = hash13(float3(i + float2(1.0, 1.0), 0.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float3 background(float3 dir)
{
    // Sparse points of light. Quantising the direction into cells and putting a
    // soft round dot inside each lit cell keeps the field stable under motion.
    // A bare threshold on the cell hash instead draws thin filaments wherever
    // neighbouring cells disagree, and those read as scratches across the
    // picture rather than as stars.
    float3 d = dir * 170.0;
    float3 cell = floor(d);
    float3 off = d - cell - 0.5;
    float star = smoothstep(0.988, 1.0, hash13(cell)) *
                 smoothstep(0.30, 0.02, length(off));
    float3 tint = lerp(float3(0.72, 0.80, 1.00), float3(1.00, 0.90, 0.78),
                       hash13(cell + 7.1));
    // A faint cold haze over the deep blue the rest of the tool clears to, so
    // the sky is neither flat black nor a different colour from the other
    // backends.
    float neb = 0.012 * pow(saturate(0.5 + 0.5 * dir.y), 1.6);
    return tint * star * 2.6 + float3(0.30, 0.42, 0.85) * neb +
           float3(0.010, 0.013, 0.022);
}

// Blackbody-ish colour ramp for the disc: white-hot at the inner edge, cooling
// to deep orange and then nearly red at the rim.
float3 discColour(float t)
{
    float3 hot  = float3(1.00, 0.90, 0.72);
    float3 mid  = float3(1.00, 0.48, 0.14);
    float3 cool = float3(0.58, 0.13, 0.04);
    return t < 0.5 ? lerp(hot, mid, t * 2.0) : lerp(mid, cool, (t - 0.5) * 2.0);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 eye = gEye.xyz;
    float3 forward = normalize(gTarget.xyz - eye);
    float3 right = normalize(cross(gUp.xyz, forward));
    float3 up = cross(forward, right);

    float aspect = gTarget.w;
    float tanHalf = gUp.w;
    float3 dir = normalize(forward +
                           right * input.ndc.x * tanHalf * aspect +
                           up    * input.ndc.y * tanHalf);

    // Integrate the photon path. In the weak-field limit the acceleration of a
    // light ray is -1.5 h^2 r / |r|^5, where h = r x v is the conserved
    // angular momentum. That single term already reproduces the deflection
    // angle and, crucially, the loop around the hole that makes the ring.
    float3 pos = eye;
    float3 vel = dir;
    float3 h = cross(pos, vel);
    float h2 = dot(h, h);

    float3 accum = float3(0.0, 0.0, 0.0);
    float trans = 1.0;          // transmittance, so the disc occludes properly
    bool captured = false;

    float rIn = gDisc.x;
    float rOut = gDisc.y;

    for (int i = 0; i < kSteps; ++i)
    {
        float r = length(pos);
        if (r < kHorizon)
        {
            captured = true;
            break;
        }

        // The disc is a thin slab around y = 0. Detect the plane crossing by
        // watching the sign of y between two samples.
        float3 next = pos + vel * (0.06 * r);
        if (pos.y * next.y < 0.0)
        {
            float f = pos.y / (pos.y - next.y);
            float3 hit = lerp(pos, next, f);
            float rc = length(hit.xz);
            if (rc > rIn && rc < rOut)
            {
                float t = saturate((rc - rIn) / (rOut - rIn));
                // Temperature falls with radius, so brightness does too.
                float3 emis = discColour(t) * (1.9 / (0.35 + t * 1.6));

                // Turbulent filaments, advected by the local orbital rate so
                // the noise shears into spiral streaks. The two octaves turn
                // at different rates, so the pattern tears and rebuilds
                // instead of rotating rigidly like a painted disc.
                float om = 1.85 / max(pow(rc, 0.85), 0.70);
                float spinA = om * gEye.w * 0.75;
                float spinB = om * gEye.w * 1.35;
                float2 q = hit.xz * (1.55 / max(rc, 1.0));
                float ca = cos(spinA), sa = sin(spinA);
                float2 rpA = float2(q.x * ca - q.y * sa, q.x * sa + q.y * ca);
                float cb = cos(spinB), sb = sin(spinB);
                float2 rpB = float2(q.x * cb - q.y * sb, q.x * sb + q.y * cb);
                float turb = vnoise(rpA * 1.7) * 0.55 + vnoise(rpB * 3.9 + 5.3) * 0.45;
                emis *= 0.30 + 1.55 * turb;

                // --- The one cue that survives the tone map ---------------
                // A compact bright blob is useless here: the inner disc is
                // already mapped to white, so anything *added* on top of it is
                // destroyed by the tone map. That is why the earlier orbiting
                // hot spots measured as almost nothing on screen. What does
                // survive is a broad *multiplicative* pattern, because it can
                // still bite a fifth out of a fully saturated pixel.
                //
                // The pattern is a two-armed spiral density wave of the sort a
                // real accretion disc develops: the phase advances with the
                // logarithm of the radius, so the arms trail, and the whole
                // pattern turns at a single constant rate. Advecting it at the
                // local orbital rate instead would wind it tighter and tighter
                // until after a minute the disc had become a set of concentric
                // rings; a rigidly rotating spiral is what density-wave theory
                // predicts and it never degenerates.
                float phi = atan2(hit.z, hit.x);
                float arm = phi - 2.10 * log(max(rc, 1.0)) -
                            kPatternSpeed * gEye.w;
                float wave = 0.70 * sin(arm) + 0.22 * sin(2.0 * arm + 0.9);
                // Fade the wave in from the inner edge and back out at the rim,
                // so the bright crescent nearest the hole keeps its clean edge.
                float env = smoothstep(0.02, 0.28, t) *
                            lerp(1.0, 0.45, smoothstep(0.55, 1.0, t));
                emis *= saturate(1.0 + 1.05 * wave * env);

                // A dark lane trailing the crest. Brightening on its own only
                // pushes further into the clip; a gap moves the luminance the
                // other way, so the bright crest and the dark lane behind it
                // read as one object travelling round the disc with a definite
                // direction of travel.
                float lane = pow(saturate(0.5 + 0.5 * sin(arm - 1.05)), 3.0);
                emis *= 1.0 - 0.52 * lane * env;

                // A denser knot riding the crest, so there is one discrete
                // thing for the eye to follow rather than only a broad
                // gradient. It sits well outside the saturated inner disc.
                float knot = pow(saturate(0.5 + 0.5 * sin(arm + 0.35)), 6.0);
                float kr = (rc - 3.60) / 1.45;
                emis += knot * exp(-kr * kr) * float3(1.30, 1.02, 0.76) *
                        1.45 * env;

                // Fade the outer third *towards a dim floor* rather than to
                // black. Multiplying all the way down leaves a black rim
                // painted on top of the sky, which reads as a cut-out ellipse
                // with a hard edge all the way around it.
                emis *= lerp(1.0, 0.09, smoothstep(0.50, 1.0, t));

                // Doppler beaming: the side of the disc turning towards the eye
                // is brighter and bluer, the other side dimmer and redder.
                float3 vdir = normalize(cross(float3(0.0, 1.0, 0.0), hit));
                float speed = 0.45 / sqrt(max(rc, 1.0));
                float beta = dot(vdir * speed, -normalize(vel));
                float beam = pow(1.0 / max(1.0 - beta, 0.25), 2.4);
                // Gravitational redshift near the hole.
                float grav = sqrt(saturate(1.0 - kHorizon / max(rc, kHorizon)));
                emis *= beam * lerp(0.55, 1.0, grav) * gDisc.z;

                accum += trans * emis;
                // One crossing is essentially opaque, so the near side of the
                // disc really does hide the far side and the sky behind it.
                // The opacity taper at the inner edge matters as much as the
                // emission one: without it a ray that clips the disc at rc a
                // hair above rIn drops straight to 0.12 while its neighbour
                // just below rIn keeps 1.0, and that one-pixel opacity cliff
                // cuts a rectangle across the shadow.
                float innerOp = smoothstep(0.0, 0.06, t);
                trans *= lerp(1.0, 0.12, innerOp);
                if (trans < 0.02) break;
            }
        }

        // Deflection. The step grows with r so the far field is cheap.
        float3 acc = -1.5 * h2 * pos / pow(max(r, 0.2), 5.0);
        float dt = 0.06 * r;
        vel += acc * dt;
        pos = next;
    }

    float3 colour = accum;
    // The sky only shows where the ray never crossed the disc at all;
    // otherwise the leftover transmittance would leak stars through the disc.
    if (!captured && trans > 0.5) colour += trans * background(normalize(vel));

    // Photon ring, placed by the impact parameter rather than by marching. Rays
    // that skim the hole at b just above 3*sqrt(3)/2 * r_s loop it repeatedly,
    // and those extra images stack up into a thin hot ring on the shadow's rim.
    if (!captured)
    {
        float b = length(h);
        float bCrit = 2.59807621;
        float ring = exp(-pow((b - bCrit) / 0.075, 2.0));
        // A second, fainter arc just outside it, from the next image order.
        float ring2 = exp(-pow((b - bCrit - 0.16) / 0.10, 2.0));
        // The ring is a stack of strongly lensed images of the disc, so it
        // cannot be constant: as the density wave sweeps through the disc the
        // same pattern turns round the rim of the shadow. Modulating it here
        // is what finally puts motion into the brightest, most saturated part
        // of the picture - the part the eye is actually looking at. sin() of
        // the unwrapped angle is used, so there is no seam where atan2 wraps
        // behind the hole.
        float pa = atan2(input.ndc.y, input.ndc.x);
        float ringMod = 1.0 + 0.72 * sin(pa - kPatternSpeed * gEye.w) +
                              0.16 * sin(2.0 * (pa - kPatternSpeed * gEye.w) + 1.1);
        colour += (ring * 3.2 + ring2 * 0.7) * saturate(ringMod) *
                  float3(1.00, 0.82, 0.56) * gDisc.z;
    }

    // Reinhard tone map and gamma, so the bright inner disc rolls off instead
    // of clipping to a flat white blob.
    colour = colour / (1.0 + colour);
    colour = pow(saturate(colour), 1.0 / 2.2);
    return float4(colour, 1.0);
}
)";

}  // namespace

class Dx12Tool : public Tool {
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
    bool createDevice(HWND host);
    bool createRootSignature();
    bool createPipeline();
    bool createTargets();
    bool createFrameResources();
    void createSizeDependent();
    void releaseSizeDependent();
    void transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                    D3D12_RESOURCE_STATES after);
    void waitForGpu();
    void drawFrame();

    int width_ = 1;
    int height_ = 1;
    float time_ = 0.0f;
    std::wstring deviceInfo_;
    // Set at the exact point of failure, so the log names the step that gave up.
    std::wstring failureReason_;

    IDXGISwapChain3* swapChain_ = nullptr;
    ID3D12Device* device_ = nullptr;
    ID3D12CommandQueue* queue_ = nullptr;
    ID3D12CommandAllocator* allocators_[kFrameCount] = {};
    ID3D12GraphicsCommandList* commandList_ = nullptr;
    ID3D12Fence* fence_ = nullptr;
    HANDLE fenceEvent_ = nullptr;
    UINT64 fenceValues_[kFrameCount] = {};

    ID3D12DescriptorHeap* rtvHeap_ = nullptr;
    UINT rtvDescriptorSize_ = 0;
    ID3D12Resource* renderTargets_[kFrameCount] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[kFrameCount] = {};

    ID3D12RootSignature* rootSignature_ = nullptr;
    ID3D12PipelineState* pipeline_ = nullptr;

    ID3D12Resource* constantBuffer_ = nullptr;
    void* constantMapped_ = nullptr;
};

// --- device ---------------------------------------------------------------

bool Dx12Tool::createDevice(HWND host) {
    RECT rc;
    GetClientRect(host, &rc);
    width_ = rc.right - rc.left;
    height_ = rc.bottom - rc.top;
    if (width_ <= 0) width_ = 1;
    if (height_ <= 0) height_ = 1;

    // Direct3D 12 has no "create device and swap chain" convenience call, so
    // the factory, the device and the swap chain are three separate steps.
    IDXGIFactory4* factory = nullptr;
    HRESULT hrFactory = CreateDXGIFactory1(__uuidof(IDXGIFactory4),
                                           reinterpret_cast<void**>(&factory));
    if (FAILED(hrFactory)) {
        applog::writeHresult(L"Direct3D 12：CreateDXGIFactory1(IDXGIFactory4)", hrFactory);
        return false;
    }

    std::wstring adapterName = L"(unknown adapter)";
    IDXGIDevice* dxgiDevice = nullptr;
    if (SUCCEEDED(device_->QueryInterface(__uuidof(IDXGIDevice),
                                          reinterpret_cast<void**>(&dxgiDevice)))) {
        IDXGIAdapter* owner = nullptr;
        if (SUCCEEDED(dxgiDevice->GetAdapter(&owner))) {
            DXGI_ADAPTER_DESC ad = {};
            if (SUCCEEDED(owner->GetDesc(&ad))) adapterName = ad.Description;
            owner->Release();
        }
        dxgiDevice->Release();
    }
    if (adapterName == L"(unknown adapter)") {
        IDXGIAdapter1* first = nullptr;
        if (SUCCEEDED(factory->EnumAdapters1(0, &first)) && first) {
            DXGI_ADAPTER_DESC1 ad = {};
            if (SUCCEEDED(first->GetDesc1(&ad))) adapterName = ad.Description;
            first->Release();
        }
    }

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width = static_cast<UINT>(width_);
    scd.Height = static_cast<UINT>(height_);
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = kFrameCount;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    IDXGISwapChain1* swap1 = nullptr;
    // This is the step that matters on a translation layer: the device can build
    // fine and the swap chain still fail, because presenting to a window is the
    // part that has to be carried across to the host. The code says which way.
    const HRESULT hrSwap = factory->CreateSwapChainForHwnd(queue_, host, &scd, nullptr,
                                                           nullptr, &swap1);
    if (FAILED(hrSwap)) {
        applog::writeHresult(L"Direct3D 12：CreateSwapChainForHwnd（flip 模型）", hrSwap);
        // The window, the same way the Vulkan path reports it. Whatever the
        // translation layer objects to is most likely common to both.
        {
            LONG_PTR style = GetWindowLongPtrW(host, GWL_STYLE);
            HWND parent = GetParent(host);
            HWND root = GetAncestor(host, GA_ROOT);
            applog::write(L"Direct3D 12：窗口 style=0x%08lX (WS_CHILD=%ls) parent=%p root=%p",
                          static_cast<unsigned long>(style),
                          (style & WS_CHILD) ? L"是" : L"否",
                          reinterpret_cast<void*>(parent), reinterpret_cast<void*>(root));
        }
        applog::write(L"Direct3D 12：交换链参数 width=%u height=%u format=RGBA8 "
                      L"buffers=%d swapeffect=FLIP_DISCARD 适配器=%ls",
                      scd.Width, scd.Height, kFrameCount, adapterName.c_str());
        factory->Release();
        return false;
    }
    factory->Release();
    swap1->QueryInterface(__uuidof(IDXGISwapChain3),
                          reinterpret_cast<void**>(&swapChain_));
    swap1->Release();
    if (!swapChain_) return false;

    deviceInfo_ = std::wstring(L"Direct3D 12 - FL 11.0 - ") + adapterName;
    return true;
}

// --- descriptors, root signature, pipeline --------------------------------

void Dx12Tool::transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                          D3D12_RESOURCE_STATES after) {
    // In Direct3D 11 the driver inferred the state of every resource. Here the
    // code has to say it, and getting it wrong is a debug-layer error and a
    // corrupt frame, so each transition is spelled out at the point it happens.
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    commandList_->ResourceBarrier(1, &barrier);
}

bool Dx12Tool::createRootSignature() {
    // The scene is entirely procedural, so there is no texture and no sampler:
    // a single constant buffer is the whole binding model.
    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    param.Descriptor.ShaderRegister = 0;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &param;
    desc.NumStaticSamplers = 0;
    desc.pStaticSamplers = nullptr;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                           &blob, &errors))) {
        if (errors) errors->Release();
        return false;
    }
    const HRESULT hr = device_->CreateRootSignature(0, blob->GetBufferPointer(),
                                                    blob->GetBufferSize(),
                                                    __uuidof(ID3D12RootSignature),
                                                    reinterpret_cast<void**>(&rootSignature_));
    blob->Release();
    if (errors) errors->Release();
    return SUCCEEDED(hr);
}

bool Dx12Tool::createPipeline() {
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errors = nullptr;

    if (FAILED(D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "d3d12",
                          nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vsBlob, &errors))) {
        if (errors) errors->Release();
        return false;
    }
    if (FAILED(D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "d3d12",
                          nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &psBlob, &errors))) {
        if (errors) errors->Release();
        vsBlob->Release();
        return false;
    }
    if (errors) errors->Release();

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = rootSignature_;
    pd.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    pd.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
    // No input layout: the vertex shader reads only SV_VertexID.
    pd.InputLayout = {nullptr, 0};
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.NumRenderTargets = 1;
    pd.DSVFormat = DXGI_FORMAT_UNKNOWN;
    pd.SampleDesc.Count = 1;
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    const HRESULT hr = device_->CreateGraphicsPipelineState(
        &pd, __uuidof(ID3D12PipelineState),
        reinterpret_cast<void**>(&pipeline_));

    vsBlob->Release();
    psBlob->Release();
    return SUCCEEDED(hr);
}

// --- targets, frames, sync -------------------------------------------------

bool Dx12Tool::createTargets() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = kFrameCount;
    if (FAILED(device_->CreateDescriptorHeap(&rtvDesc,
                                             __uuidof(ID3D12DescriptorHeap),
                                             reinterpret_cast<void**>(&rtvHeap_)))) {
        return false;
    }
    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < kFrameCount; ++i) {
        if (FAILED(swapChain_->GetBuffer(i, __uuidof(ID3D12Resource),
                                         reinterpret_cast<void**>(&renderTargets_[i])))) {
            return false;
        }
        device_->CreateRenderTargetView(renderTargets_[i], nullptr, rtvHandle);
        rtvHandles_[i] = rtvHandle;
        rtvHandle.ptr += rtvDescriptorSize_;
    }
    return true;
}

bool Dx12Tool::createFrameResources() {
    if (FAILED(device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void**>(&allocators_[0])))) {
        return false;
    }
    if (FAILED(device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void**>(&allocators_[1])))) {
        return false;
    }
    if (FAILED(device_->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[0], nullptr,
            __uuidof(ID3D12GraphicsCommandList),
            reinterpret_cast<void**>(&commandList_)))) {
        return false;
    }
    // Recording is explicit, so the list is closed when it is not being filled.
    commandList_->Close();

    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                    __uuidof(ID3D12Fence),
                                    reinterpret_cast<void**>(&fence_)))) {
        return false;
    }
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) return false;

    // A buffer's resource description is not "width and nothing else": the
    // runtime rejects Height = DepthOrArraySize = MipLevels = 0, so every one
    // of those has to be spelled out as 1 and the format as UNKNOWN.
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = kConstantStride;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device_->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, __uuidof(ID3D12Resource),
            reinterpret_cast<void**>(&constantBuffer_)))) {
        return false;
    }
    if (FAILED(constantBuffer_->Map(0, nullptr, &constantMapped_))) return false;
    return true;
}

void Dx12Tool::waitForGpu() {
    const UINT frame = swapChain_->GetCurrentBackBufferIndex();
    const UINT64 value = ++fenceValues_[frame];
    queue_->Signal(fence_, value);
    if (fence_->GetCompletedValue() < value) {
        fence_->SetEventOnCompletion(value, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void Dx12Tool::createSizeDependent() {}

void Dx12Tool::releaseSizeDependent() {}

bool Dx12Tool::init(HWND host) {
    applog::write(L"Direct3D 12：开始初始化");

    // The command queue has to exist before the swap chain is created for it.
    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                   __uuidof(ID3D12Device),
                                   reinterpret_cast<void**>(&device_));
    if (FAILED(hr)) {
        applog::writeHresult(L"Direct3D 12：D3D12CreateDevice", hr);
        failureReason_ = L"无法创建 D3D12 设备";
        return false;
    }
    hr = device_->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue),
                                     reinterpret_cast<void**>(&queue_));
    if (FAILED(hr)) {
        applog::writeHresult(L"Direct3D 12：CreateCommandQueue", hr);
        failureReason_ = L"无法创建命令队列";
        return false;
    }
    applog::write(L"Direct3D 12：设备与队列已建立");

    if (!createDevice(host)) { failureReason_ = L"无法创建交换链"; return false; }
    if (!createRootSignature()) { failureReason_ = L"无法创建根签名"; return false; }
    if (!createPipeline()) { failureReason_ = L"无法创建管线"; return false; }
    if (!createFrameResources()) { failureReason_ = L"无法创建帧资源"; return false; }
    if (!createTargets()) { failureReason_ = L"无法创建渲染目标"; return false; }
    applog::write(L"Direct3D 12：初始化完成");
    return true;
}

void Dx12Tool::resize(int width, int height) {
    if (!swapChain_ || width <= 0 || height <= 0) return;
    waitForGpu();
    width_ = width;
    height_ = height;
    for (int i = 0; i < kFrameCount; ++i) {
        if (renderTargets_[i]) { renderTargets_[i]->Release(); renderTargets_[i] = nullptr; }
    }
    releaseSizeDependent();
    swapChain_->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height),
                              DXGI_FORMAT_UNKNOWN, 0);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < kFrameCount; ++i) {
        swapChain_->GetBuffer(i, __uuidof(ID3D12Resource),
                              reinterpret_cast<void**>(&renderTargets_[i]));
        device_->CreateRenderTargetView(renderTargets_[i], nullptr, rtvHandle);
        rtvHandles_[i] = rtvHandle;
        rtvHandle.ptr += rtvDescriptorSize_;
    }
    createSizeDependent();
}

void Dx12Tool::drawFrame() {
    Constants constants = {};
    // Almost in the plane of the disc - only about four degrees above it. A
    // steeper view opens the disc into a broad ellipse that sits under the hole
    // and reads as a heap, whereas from just off the plane the near edge
    // collapses to a thin bar straight through the middle and the far side is
    // lensed up and over the top into the ring. That silhouette is the one the
    // hole is recognised by.
    constants.eye[0] = 0.0f;
    constants.eye[1] = 1.20f;
    constants.eye[2] = -16.5f;
    constants.eye[3] = time_;
    constants.target[0] = 0.0f;
    constants.target[1] = 0.0f;
    constants.target[2] = 0.0f;
    constants.target[3] = height_ > 0 ? static_cast<float>(width_) / height_ : 1.0f;
    constants.up[0] = 0.0f;
    constants.up[1] = 1.0f;
    constants.up[2] = 0.0f;
    constants.up[3] = 0.52f;          // tan(fovY / 2): a fairly wide lens
    constants.disc[0] = 2.05f;        // inner edge, close to the photon sphere
    constants.disc[1] = 9.50f;        // outer edge
    constants.disc[2] = 1.00f;        // exposure
    constants.disc[3] = 0.0f;         // spin

    std::memcpy(constantMapped_, &constants, sizeof(constants));
    commandList_->SetGraphicsRootConstantBufferView(
        0, constantBuffer_->GetGPUVirtualAddress());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList_->DrawInstanced(3, 1, 0, 0);
}

void Dx12Tool::render(double elapsed) {
    if (!commandList_ || !swapChain_) return;

    time_ = static_cast<float>(elapsed);
    const UINT frame = swapChain_->GetCurrentBackBufferIndex();

    // Start the frame only once the GPU has finished with this slot's buffer.
    waitForGpu();
    allocators_[frame]->Reset();
    commandList_->Reset(allocators_[frame], pipeline_);

    // Back buffer: PRESENT -> RENDER_TARGET. Explicit, every frame.
    transition(renderTargets_[frame], D3D12_RESOURCE_STATE_PRESENT,
               D3D12_RESOURCE_STATE_RENDER_TARGET);

    commandList_->OMSetRenderTargets(1, &rtvHandles_[frame], FALSE, nullptr);

    D3D12_VIEWPORT vp = {};
    vp.Width = static_cast<float>(width_);
    vp.Height = static_cast<float>(height_);
    vp.MaxDepth = 1.0f;
    D3D12_RECT scissor = {0, 0, width_, height_};
    commandList_->RSSetViewports(1, &vp);
    commandList_->RSSetScissorRects(1, &scissor);

    // The root signature is bound on the command list as well as inside the
    // pipeline state. Omitting this leaves the list with no root signature, so
    // the very first SetGraphicsRootConstantBufferView indexes past the end of
    // an empty layout and faults inside the driver.
    commandList_->SetGraphicsRootSignature(rootSignature_);
    commandList_->SetPipelineState(pipeline_);
    drawFrame();

    // Back buffer: RENDER_TARGET -> PRESENT.
    transition(renderTargets_[frame], D3D12_RESOURCE_STATE_RENDER_TARGET,
               D3D12_RESOURCE_STATE_PRESENT);

    commandList_->Close();
    ID3D12CommandList* lists[] = {commandList_};
    queue_->ExecuteCommandLists(1, lists);
    swapChain_->Present(0, 0);
}

void Dx12Tool::shutdown() {
    if (queue_ && fence_ && fenceEvent_) waitForGpu();
    releaseSizeDependent();
    if (constantBuffer_ && constantMapped_) {
        constantBuffer_->Unmap(0, nullptr);
        constantMapped_ = nullptr;
    }
    if (constantBuffer_) constantBuffer_->Release();
    if (pipeline_) pipeline_->Release();
    if (rootSignature_) rootSignature_->Release();
    for (ID3D12Resource*& rt : renderTargets_) {
        if (rt) { rt->Release(); rt = nullptr; }
    }
    if (rtvHeap_) rtvHeap_->Release();
    if (fenceEvent_) CloseHandle(fenceEvent_);
    if (fence_) fence_->Release();
    if (commandList_) commandList_->Release();
    if (allocators_[1]) allocators_[1]->Release();
    if (allocators_[0]) allocators_[0]->Release();
    if (queue_) queue_->Release();
    if (swapChain_) swapChain_->Release();
    if (device_) device_->Release();
}

Tool* createDx12() { return new Dx12Tool(); }

}  // namespace render


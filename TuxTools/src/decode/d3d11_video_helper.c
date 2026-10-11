/*
 * D3D11 Video Acceleration Helper
 * Manages D3D11 device, video decoder, and video processor.
 */
#include "d3d11_video_helper.h"
#include "log.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* D3D11 device state */
static ID3D11Device           *pD3D11Device   = NULL;
static ID3D11DeviceContext    *pD3D11Context   = NULL;
static IDXGISwapChain1        *pSwapChain     = NULL;
static ID3D11RenderTargetView *pRenderTargetView = NULL;
static HWND                    g_hwnd         = NULL;
static int                     g_width        = 0;
static int                     g_height       = 0;

/* D3D11 Video Decoder state */
static ID3D11VideoDevice       *pVideoDevice   = NULL;
static ID3D11VideoContext      *pVideoContext  = NULL;
static ID3D11VideoDecoder      *pVideoDecoder  = NULL;
static ID3D11Texture2D        **ppDecodeTextures = NULL;
static ID3D11VideoDecoderOutputView **ppDecodeViews = NULL;
static UINT                     g_numTextures  = 0;
static UINT                     g_nextTexture  = 0;
static GUID                     g_decoderProfile = {0};

/* D3D11 Video Processor state */
static ID3D11VideoProcessorEnumerator *pProcessorEnum = NULL;
static ID3D11VideoProcessor           *pVideoProcessor = NULL;
static ID3D11VideoProcessorInputView  *pInputView = NULL;
static ID3D11VideoProcessorOutputView *pOutputView = NULL;

/* Maximum number of decode textures */
#define MAX_DECODE_TEXTURES 16

/* Helper macro for safe release */
#define SAFE_RELEASE(p) do { if ((p)) { (p)->lpVtbl->Release((p)); (p) = NULL; } } while(0)

int d3d11_video_init(HWND hwnd, int width, int height)
{
    HRESULT hr;
    DXGI_SWAP_CHAIN_DESC1 scd;
    ID3D11Texture2D *backBuffer = NULL;

    d3d11_video_cleanup();

    if (!hwnd || width <= 0 || height <= 0)
        return -1;

    /* Create D3D11 device */
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL featureLevel;
    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    hr = D3D11CreateDevice(
        NULL,                           /* Adapter */
        D3D_DRIVER_TYPE_HARDWARE,       /* Driver type */
        NULL,                           /* Software module */
        createFlags,                    /* Flags */
        featureLevels,                  /* Feature levels */
        sizeof(featureLevels) / sizeof(featureLevels[0]),
        D3D11_SDK_VERSION,              /* SDK version */
        &pD3D11Device,
        &featureLevel,
        &pD3D11Context
    );

    if (FAILED(hr)) {
        Log_Printf(L"D3D11: CreateDevice failed: 0x%08lx", hr);
        return -1;
    }

    /* Get DXGI factory */
    IDXGIDevice *pDXGIDevice = NULL;
    hr = ID3D11Device_QueryInterface(pD3D11Device, &IID_IDXGIDevice, (void **)&pDXGIDevice);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: QueryInterface IDXGIDevice failed: 0x%08lx", hr);
        d3d11_video_cleanup();
        return -1;
    }

    IDXGIAdapter *pAdapter = NULL;
    hr = IDXGIDevice_GetAdapter(pDXGIDevice, &pAdapter);
    SAFE_RELEASE(pDXGIDevice);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: GetAdapter failed: 0x%08lx", hr);
        d3d11_video_cleanup();
        return -1;
    }

    IDXGIFactory2 *pFactory = NULL;
    hr = IDXGIAdapter_GetParent(pAdapter, &IID_IDXGIFactory2, (void **)&pFactory);
    SAFE_RELEASE(pAdapter);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: GetParent IDXGIFactory2 failed: 0x%08lx", hr);
        d3d11_video_cleanup();
        return -1;
    }

    /* Create swap chain */
    ZeroMemory(&scd, sizeof(scd));
    scd.Width = width;
    scd.Height = height;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    scd.Scaling = DXGI_SCALING_STRETCH;

    hr = IDXGIFactory2_CreateSwapChainForHwnd(pFactory, (IUnknown *)pD3D11Device, hwnd, &scd, NULL, NULL, &pSwapChain);
    SAFE_RELEASE(pFactory);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: CreateSwapChainForHwnd failed: 0x%08lx", hr);
        d3d11_video_cleanup();
        return -1;
    }

    /* Get back buffer */
    hr = IDXGISwapChain1_GetBuffer(pSwapChain, 0, &IID_ID3D11Texture2D, (void **)&backBuffer);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: GetBuffer failed: 0x%08lx", hr);
        d3d11_video_cleanup();
        return -1;
    }

    /* Create render target view */
    hr = ID3D11Device_CreateRenderTargetView(pD3D11Device, (ID3D11Resource *)backBuffer, NULL, &pRenderTargetView);
    SAFE_RELEASE(backBuffer);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: CreateRenderTargetView failed: 0x%08lx", hr);
        d3d11_video_cleanup();
        return -1;
    }

    g_hwnd = hwnd;
    g_width = width;
    g_height = height;

    Log_Printf(L"D3D11: Device created successfully (%dx%d, feature level %d.%d", width, height, (featureLevel >> 12) & 0xF, (featureLevel >> 8) & 0xF);
    return 0;
}

int d3d11_video_check_support(void)
{
    /* Check if D3D11 is available */
    ID3D11Device *device = NULL;
    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr;

    hr = D3D11CreateDevice(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
        NULL, 0, D3D11_SDK_VERSION,
        &device, &featureLevel, NULL
    );

    if (SUCCEEDED(hr) && device) {
        device->lpVtbl->Release(device);
#ifdef __MINGW32__
        Log_Printf(L"D3D11: Basic device available in MinGW build (video decode/processor may be limited");
#endif
        return 1;
    }

    return 0;
}

const wchar_t *d3d11_video_get_device_info(void)
{
    static wchar_t info[256] = {0};

    if (!pD3D11Device) {
        swprintf(info, 256, L"D3D11: Not initialized");
        return info;
    }

    /* Get DXGI device */
    IDXGIDevice *pDXGIDevice = NULL;
    HRESULT hr = ID3D11Device_QueryInterface(pD3D11Device, &IID_IDXGIDevice, (void **)&pDXGIDevice);
    if (SUCCEEDED(hr)) {
        IDXGIAdapter *pAdapter = NULL;
        hr = IDXGIDevice_GetAdapter(pDXGIDevice, &pAdapter);
        if (SUCCEEDED(hr)) {
            DXGI_ADAPTER_DESC desc;
            hr = IDXGIAdapter_GetDesc(pAdapter, &desc);
            if (SUCCEEDED(hr)) {
                swprintf(info, 256, L"D3D11: %s (Vendor: 0x%04X, Device: 0x%04X)",
                         desc.Description, desc.VendorId, desc.DeviceId);
            }
            SAFE_RELEASE(pAdapter);
        }
        SAFE_RELEASE(pDXGIDevice);
    }

    if (info[0] == 0) {
        swprintf(info, 256, L"D3D11: Device info unavailable");
    }
    return info;
}

void d3d11_video_cleanup(void)
{
    /* Present a black frame before cleanup to clear residual content */
    if (pD3D11Context && pRenderTargetView && pSwapChain) {
        float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        ID3D11DeviceContext_ClearRenderTargetView(pD3D11Context, pRenderTargetView, black);
        IDXGISwapChain1_Present(pSwapChain, 1, 0);
    }

    /* Cleanup video processor */
    d3d11_video_processor_cleanup();

    /* Cleanup video decoder */
    d3d11_video_decoder_cleanup();

    SAFE_RELEASE(pRenderTargetView);
    SAFE_RELEASE(pSwapChain);
    SAFE_RELEASE(pD3D11Context);
    SAFE_RELEASE(pD3D11Device);

    g_hwnd = NULL;
    g_width = 0;
    g_height = 0;
}

ID3D11Device *d3d11_video_get_device(void)
{
    return pD3D11Device;
}

ID3D11DeviceContext *d3d11_video_get_context(void)
{
    return pD3D11Context;
}

int d3d11_video_is_initialized(void)
{
    return (pD3D11Device != NULL) ? 1 : 0;
}

int d3d11_video_present(void)
{
    if (!pSwapChain) return -1;

    HRESULT hr = IDXGISwapChain1_Present(pSwapChain, 1, 0);
    return SUCCEEDED(hr) ? 0 : -1;
}

/* ==================== D3D11 Video Decoder Functions ==================== */

int d3d11_video_decoder_init(const D3D11VideoDecoderConfig *config)
{
    HRESULT hr;

    if (!pD3D11Device || !config) return -1;

#ifdef __MINGW32__
    Log_Printf(L"D3D11: Video decoder init in MinGW build (may have limited support");
#endif

    /* Cleanup previous decoder */
    d3d11_video_decoder_cleanup();

    /* Query video device interface */
    hr = ID3D11Device_QueryInterface(pD3D11Device, &IID_ID3D11VideoDevice, (void **)&pVideoDevice);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: QueryInterface ID3D11VideoDevice failed: 0x%08lx", hr);
        return -1;
    }

    /* Query video context interface */
    hr = ID3D11DeviceContext_QueryInterface(pD3D11Context, &IID_ID3D11VideoContext, (void **)&pVideoContext);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: QueryInterface ID3D11VideoContext failed: 0x%08lx", hr);
        d3d11_video_decoder_cleanup();
        return -1;
    }

    /* Find decoder profile (H.264 VLD) */
    GUID profiles[] = {
        {0x1b81be68, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}}, /* H.264 VLD */
        {0x1b81be69, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}}  /* HEVC VLD */
    };
    int num_profiles = sizeof(profiles) / sizeof(profiles[0]);

    g_decoderProfile = GUID_NULL;
    for (int i = 0; i < num_profiles; i++) {
        BOOL supported = FALSE;
        hr = ID3D11VideoDevice_CheckVideoDecoderFormat(pVideoDevice, &profiles[i], DXGI_FORMAT_NV12, &supported);
        if (SUCCEEDED(hr) && supported) {
            g_decoderProfile = profiles[i];
            Log_Printf(L"D3D11: Using decoder profile %", i);
            break;
        }
    }

    if (IsEqualGUID(&g_decoderProfile, &GUID_NULL)) {
        Log_Printf(L"D3D11: No supported decoder profile foun");
        d3d11_video_decoder_cleanup();
        return -1;
    }

    /* Create decoder */
    D3D11_VIDEO_DECODER_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Guid = g_decoderProfile;
    desc.SampleWidth = config->width;
    desc.SampleHeight = config->height;
    desc.OutputFormat = DXGI_FORMAT_NV12;

    /* CreateVideoDecoder requires a config parameter */
    D3D11_VIDEO_DECODER_CONFIG config_desc;
    ZeroMemory(&config_desc, sizeof(config_desc));
    config_desc.guidConfigBitstreamEncryption = GUID_NULL;
    config_desc.guidConfigMBcontrolEncryption = GUID_NULL;
    config_desc.guidConfigResidDiffEncryption = GUID_NULL;
    config_desc.ConfigBitstreamRaw = 1;

    hr = ID3D11VideoDevice_CreateVideoDecoder(pVideoDevice, &desc, &config_desc, &pVideoDecoder);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: CreateVideoDecoder failed: 0x%08lx", hr);
        d3d11_video_decoder_cleanup();
        return -1;
    }

    /* Create decode textures */
    g_numTextures = config->num_surfaces;
    if (g_numTextures > MAX_DECODE_TEXTURES) g_numTextures = MAX_DECODE_TEXTURES;
    if (g_numTextures < 4) g_numTextures = 4;

    ppDecodeTextures = (ID3D11Texture2D **)malloc(g_numTextures * sizeof(ID3D11Texture2D *));
    ppDecodeViews = (ID3D11VideoDecoderOutputView **)malloc(g_numTextures * sizeof(ID3D11VideoDecoderOutputView *));

    if (!ppDecodeTextures || !ppDecodeViews) {
        Log_Printf(L"D3D11: Memory allocation faile");
        d3d11_video_decoder_cleanup();
        return -1;
    }

    ZeroMemory(ppDecodeTextures, g_numTextures * sizeof(ID3D11Texture2D *));
    ZeroMemory(ppDecodeViews, g_numTextures * sizeof(ID3D11VideoDecoderOutputView *));

    /* Create textures and output views */
    D3D11_TEXTURE2D_DESC texDesc;
    ZeroMemory(&texDesc, sizeof(texDesc));
    texDesc.Width = config->width;
    texDesc.Height = config->height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_NV12;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_DECODER;

    for (UINT i = 0; i < g_numTextures; i++) {
        hr = ID3D11Device_CreateTexture2D(pD3D11Device, &texDesc, NULL, &ppDecodeTextures[i]);
        if (FAILED(hr)) {
            Log_Printf(L"D3D11: CreateTexture2D failed for surface %u: 0x%08lx", i, hr);
            d3d11_video_decoder_cleanup();
            return -1;
        }

        D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC viewDesc;
        ZeroMemory(&viewDesc, sizeof(viewDesc));
        viewDesc.DecodeProfile = g_decoderProfile;
        viewDesc.ViewDimension = D3D11_VDOV_DIMENSION_TEXTURE2D;
        viewDesc.Texture2D.ArraySlice = 0;

        hr = ID3D11VideoDevice_CreateVideoDecoderOutputView(pVideoDevice,
            (ID3D11Resource *)ppDecodeTextures[i], &viewDesc, &ppDecodeViews[i]);
        if (FAILED(hr)) {
            Log_Printf(L"D3D11: CreateVideoDecoderOutputView failed for surface %u: 0x%08lx", i, hr);
            d3d11_video_decoder_cleanup();
            return -1;
        }
    }

    g_nextTexture = 0;
    Log_Printf(L"D3D11: Video decoder initialized (%dx%d, %d surfaces", config->width, config->height, g_numTextures);
    return 0;
}

int d3d11_video_decoder_decode(const BYTE *buffer, DWORD size, LONGLONG pts)
{
    if (!pVideoDecoder || !pVideoContext || !ppDecodeViews || !buffer || size == 0)
        return -1;

#ifdef __MINGW32__
    /* D3D11 video decode may not work correctly in MinGW due to structure differences */
    (void)buffer; (void)size; (void)pts;
    Log_Printf(L"D3D11: Video decode not reliable in MinGW build, skipping fram");
    return -1;
#endif

    HRESULT hr;
    UINT viewIndex = g_nextTexture;
    g_nextTexture = (g_nextTexture + 1) % g_numTextures;

    /* Begin frame */
    hr = ID3D11VideoContext_DecoderBeginFrame(pVideoContext, pVideoDecoder,
        ppDecodeViews[viewIndex], 0, NULL);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: DecoderBeginFrame failed: 0x%08lx", hr);
        return -1;
    }

    /* Submit bitstream buffer */
    D3D11_VIDEO_DECODER_BUFFER_DESC buffers[1];
    ZeroMemory(buffers, sizeof(buffers));
    buffers[0].BufferType = D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
    buffers[0].DataSize = size;
    buffers[0].DataOffset = 0;

    hr = ID3D11VideoContext_SubmitDecoderBuffers(pVideoContext, pVideoDecoder, 1, buffers);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: SubmitDecoderBuffers failed: 0x%08lx", hr);
        ID3D11VideoContext_DecoderEndFrame(pVideoContext, pVideoDecoder);
        return -1;
    }

    /* End frame */
    hr = ID3D11VideoContext_DecoderEndFrame(pVideoContext, pVideoDecoder);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: DecoderEndFrame failed: 0x%08lx", hr);
        return -1;
    }

    return 0;
}

ID3D11Texture2D *d3d11_video_decoder_get_texture(void)
{
    if (!ppDecodeTextures || g_numTextures == 0) return NULL;

    /* Return the most recently decoded texture */
    UINT index = (g_nextTexture > 0) ? g_nextTexture - 1 : g_numTextures - 1;
    ID3D11Texture2D *texture = ppDecodeTextures[index];
    if (texture) {
        texture->lpVtbl->AddRef(texture);
    }
    return texture;
}

void d3d11_video_decoder_cleanup(void)
{
    if (ppDecodeViews) {
        for (UINT i = 0; i < g_numTextures; i++) {
            SAFE_RELEASE(ppDecodeViews[i]);
        }
        free(ppDecodeViews);
        ppDecodeViews = NULL;
    }

    if (ppDecodeTextures) {
        for (UINT i = 0; i < g_numTextures; i++) {
            SAFE_RELEASE(ppDecodeTextures[i]);
        }
        free(ppDecodeTextures);
        ppDecodeTextures = NULL;
    }

    SAFE_RELEASE(pVideoDecoder);
    SAFE_RELEASE(pVideoContext);
    SAFE_RELEASE(pVideoDevice);

    g_numTextures = 0;
    g_nextTexture = 0;
    g_decoderProfile = GUID_NULL;
}

/* ==================== D3D11 Video Processor Functions ==================== */

int d3d11_video_processor_init(void)
{
    HRESULT hr;

    if (!pD3D11Device || !pD3D11Context) return -1;

#ifdef __MINGW32__
    Log_Printf(L"D3D11: Video processor init in MinGW build (may have limited support");
#endif

    /* Cleanup previous processor */
    d3d11_video_processor_cleanup();

    /* Query video device if not already done */
    if (!pVideoDevice) {
        hr = ID3D11Device_QueryInterface(pD3D11Device, &IID_ID3D11VideoDevice, (void **)&pVideoDevice);
        if (FAILED(hr)) {
            Log_Printf(L"D3D11: QueryInterface ID3D11VideoDevice failed: 0x%08lx", hr);
            return -1;
        }
    }

    if (!pVideoContext) {
        hr = ID3D11DeviceContext_QueryInterface(pD3D11Context, &IID_ID3D11VideoContext, (void **)&pVideoContext);
        if (FAILED(hr)) {
            Log_Printf(L"D3D11: QueryInterface ID3D11VideoContext failed: 0x%08lx", hr);
            return -1;
        }
    }

    /* Create video processor enumerator */
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    desc.InputWidth = g_width;
    desc.InputHeight = g_height;
    desc.OutputWidth = g_width;
    desc.OutputHeight = g_height;

    hr = ID3D11VideoDevice_CreateVideoProcessorEnumerator(pVideoDevice, &desc, &pProcessorEnum);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: CreateVideoProcessorEnumerator failed: 0x%08lx", hr);
        return -1;
    }

    /* Create video processor */
    hr = ID3D11VideoDevice_CreateVideoProcessor(pVideoDevice, pProcessorEnum, 0, &pVideoProcessor);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: CreateVideoProcessor failed: 0x%08lx", hr);
        d3d11_video_processor_cleanup();
        return -1;
    }

    Log_Printf(L"D3D11: Video processor initialize");
    return 0;
}

int d3d11_video_processor_render(ID3D11Texture2D *src_texture,
                                 const RECT *src_rect, const RECT *dst_rect,
                                 int video_w, int video_h)
{
    HRESULT hr;
    RECT centered_src, centered_dst;

    if (!pVideoProcessor || !pVideoContext || !src_texture || !pRenderTargetView)
        return -1;

    /* Calculate centered destination rect with aspect ratio preservation */
    if (!dst_rect && video_w > 0 && video_h > 0 && g_width > 0 && g_height > 0) {
        float scale_x = (float)g_width / (float)video_w;
        float scale_y = (float)g_height / (float)video_h;
        float scale = (scale_x < scale_y) ? scale_x : scale_y;
        int draw_w = (int)(video_w * scale);
        int draw_h = (int)(video_h * scale);
        if (draw_w < 1) draw_w = 1;
        if (draw_h < 1) draw_h = 1;
        centered_dst.left   = (g_width - draw_w) / 2;
        centered_dst.top    = (g_height - draw_h) / 2;
        centered_dst.right  = centered_dst.left + draw_w;
        centered_dst.bottom = centered_dst.top + draw_h;
        dst_rect = &centered_dst;
    }

    /* Set source rect (full video frame) */
    if (!src_rect && video_w > 0 && video_h > 0) {
        centered_src.left = 0;
        centered_src.top = 0;
        centered_src.right = video_w;
        centered_src.bottom = video_h;
        src_rect = &centered_src;
    }

    /* Clear render target to black for letterboxing/pillarboxing */
    {
        float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        ID3D11DeviceContext_ClearRenderTargetView(pD3D11Context, pRenderTargetView, black);
    }

    /* Create input view from source texture */
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc;
    ZeroMemory(&inputDesc, sizeof(inputDesc));
    inputDesc.FourCC = 0;
    inputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inputDesc.Texture2D.MipSlice = 0;
    inputDesc.Texture2D.ArraySlice = 0;

    ID3D11VideoProcessorInputView *inputView = NULL;
    hr = ID3D11VideoDevice_CreateVideoProcessorInputView(pVideoDevice,
        (ID3D11Resource *)src_texture, pProcessorEnum, &inputDesc, &inputView);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: CreateVideoProcessorInputView failed: 0x%08lx", hr);
        return -1;
    }

    /* Get back buffer for output */
    ID3D11Texture2D *backBuffer = NULL;
    hr = IDXGISwapChain1_GetBuffer(pSwapChain, 0, &IID_ID3D11Texture2D, (void **)&backBuffer);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: GetBuffer failed: 0x%08lx", hr);
        SAFE_RELEASE(inputView);
        return -1;
    }

    /* Create output view */
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc;
    ZeroMemory(&outputDesc, sizeof(outputDesc));
    outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outputDesc.Texture2D.MipSlice = 0;

    ID3D11VideoProcessorOutputView *outputView = NULL;
    hr = ID3D11VideoDevice_CreateVideoProcessorOutputView(pVideoDevice,
        (ID3D11Resource *)backBuffer, pProcessorEnum, &outputDesc, &outputView);
    SAFE_RELEASE(backBuffer);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: CreateVideoProcessorOutputView failed: 0x%08lx", hr);
        SAFE_RELEASE(inputView);
        return -1;
    }

    /* Set source and destination rectangles */
    if (src_rect) {
        ID3D11VideoContext_VideoProcessorSetStreamSourceRect(pVideoContext,
            pVideoProcessor, 0, TRUE, src_rect);
    }
    if (dst_rect) {
        ID3D11VideoContext_VideoProcessorSetStreamDestRect(pVideoContext,
            pVideoProcessor, 0, TRUE, dst_rect);
    }

    /* Process video */
    D3D11_VIDEO_PROCESSOR_STREAM streams[1];
    ZeroMemory(streams, sizeof(streams));
    streams[0].Enable = TRUE;
    streams[0].OutputIndex = 0;
    streams[0].InputFrameOrField = 0;
    streams[0].PastFrames = 0;
    streams[0].FutureFrames = 0;
    streams[0].pInputSurface = inputView;

    hr = ID3D11VideoContext_VideoProcessorBlt(pVideoContext, pVideoProcessor,
        outputView, 0, 1, streams);

    SAFE_RELEASE(inputView);
    SAFE_RELEASE(outputView);

    if (FAILED(hr)) {
        Log_Printf(L"D3D11: VideoProcessorBlt failed: 0x%08lx", hr);
        return -1;
    }

    /* Present */
    return d3d11_video_present();
}

void d3d11_video_processor_cleanup(void)
{
    SAFE_RELEASE(pOutputView);
    SAFE_RELEASE(pInputView);
    SAFE_RELEASE(pVideoProcessor);
    SAFE_RELEASE(pProcessorEnum);
}

/* ==================== D3D11 Texture Helper Functions ==================== */

ID3D11Texture2D *d3d11_video_create_texture(UINT width, UINT height, DXGI_FORMAT format)
{
    HRESULT hr;
    ID3D11Texture2D *texture = NULL;

    if (!pD3D11Device) return NULL;

    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DECODER;

    hr = ID3D11Device_CreateTexture2D(pD3D11Device, &desc, NULL, &texture);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: CreateTexture2D failed: 0x%08lx", hr);
        return NULL;
    }

    return texture;
}

int d3d11_video_upload_texture(ID3D11Texture2D *texture, const BYTE *data, int stride, DXGI_FORMAT format)
{
    HRESULT hr;

    if (!pD3D11Device || !pD3D11Context || !texture || !data) return -1;

    /* Get texture description */
    D3D11_TEXTURE2D_DESC desc;
    texture->lpVtbl->GetDesc(texture, &desc);

    /* Create staging texture for upload */
    D3D11_TEXTURE2D_DESC stagingDesc;
    ZeroMemory(&stagingDesc, sizeof(stagingDesc));
    stagingDesc.Width = desc.Width;
    stagingDesc.Height = desc.Height;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = desc.Format;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    ID3D11Texture2D *stagingTexture = NULL;
    hr = ID3D11Device_CreateTexture2D(pD3D11Device, &stagingDesc, NULL, &stagingTexture);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: Create staging texture failed: 0x%08lx", hr);
        return -1;
    }

    /* Map staging texture */
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = ID3D11DeviceContext_Map(pD3D11Context, (ID3D11Resource *)stagingTexture, 0, D3D11_MAP_WRITE, 0, &mapped);
    if (FAILED(hr)) {
        Log_Printf(L"D3D11: Map staging texture failed: 0x%08lx", hr);
        SAFE_RELEASE(stagingTexture);
        return -1;
    }

    /* Copy data to staging texture based on format */
    if (format == DXGI_FORMAT_NV12) {
        /* NV12 has two planes: Y (height) and UV (height/2) */
        const BYTE *srcY = data;
        const BYTE *srcUV = data + stride * desc.Height;
        BYTE *dstY = (BYTE *)mapped.pData;
        BYTE *dstUV = (BYTE *)mapped.pData + mapped.RowPitch * desc.Height;

        /* Copy Y plane */
        for (UINT y = 0; y < desc.Height; y++) {
            memcpy(dstY + y * mapped.RowPitch, srcY + y * stride, desc.Width);
        }

        /* Copy UV plane */
        for (UINT y = 0; y < desc.Height / 2; y++) {
            memcpy(dstUV + y * mapped.RowPitch, srcUV + y * stride, desc.Width);
        }
    } else if (format == DXGI_FORMAT_YUY2) {
        /* YUY2 is packed format */
        UINT rowSize = desc.Width * 2;
        for (UINT y = 0; y < desc.Height; y++) {
            memcpy((BYTE *)mapped.pData + y * mapped.RowPitch, data + y * stride, rowSize);
        }
    } else if (format == DXGI_FORMAT_B8G8R8A8_UNORM) {
        /* BGRA is packed format */
        UINT rowSize = desc.Width * 4;
        for (UINT y = 0; y < desc.Height; y++) {
            memcpy((BYTE *)mapped.pData + y * mapped.RowPitch, data + y * stride, rowSize);
        }
    } else {
        Log_Printf(L"D3D11: Unsupported format for upload: %", format);
        ID3D11DeviceContext_Unmap(pD3D11Context, (ID3D11Resource *)stagingTexture, 0);
        SAFE_RELEASE(stagingTexture);
        return -1;
    }

    /* Unmap staging texture */
    ID3D11DeviceContext_Unmap(pD3D11Context, (ID3D11Resource *)stagingTexture, 0);

    /* Copy from staging texture to target texture */
    ID3D11DeviceContext_CopySubresourceRegion(pD3D11Context,
        (ID3D11Resource *)texture, 0, 0, 0, 0,
        (ID3D11Resource *)stagingTexture, 0, NULL);

    SAFE_RELEASE(stagingTexture);
    return 0;
}

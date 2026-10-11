/*
 * DXVA2 Hardware Acceleration Helper
 * Manages Direct3D9 device, DXVA2 decoder, and video processor.
 */
#include "dxva2_helper.h"
#include "log.h"

#include <d3d9.h>
#include <dxva2api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* D3DFMT_NV12 may not be available in MinGW */
#ifndef D3DFMT_NV12
#define D3DFMT_NV12 ((D3DFORMAT)MAKEFOURCC('N','V','1','2'))
#endif

/* IID for IDirectXVideoProcessorService - use standard GUID from dxva2api.h */
static const GUID IID_IDXVPS = {
    0xfc51a552, 0xd5e7, 0x11d9, {0xaf, 0x55, 0x00, 0x05, 0x4e, 0x43, 0xff, 0x02}
};

/* IID for IDirectXVideoDecoderService - use standard GUID from dxva2api.h */
static const GUID IID_IDDS = {
    0xfc51a551, 0xd5e7, 0x11d9, {0xaf, 0x55, 0x00, 0x05, 0x4e, 0x43, 0xff, 0x02}
};

static IDirect3D9            *pD3D        = NULL;
static IDirect3DDevice9      *pDevice     = NULL;
static IDirect3DSurface9     *pRT         = NULL;
static D3DPRESENT_PARAMETERS  d3dpp;
static HWND                   g_hwnd      = NULL;
static int                    g_width     = 0;
static int                    g_height    = 0;

/* DXVA2 Decoder state */
static IDirectXVideoDecoderService *pDecoderService = NULL;
static IDirectXVideoDecoder        *pDecoder        = NULL;
static IDirect3DSurface9          **ppDecodeSurfaces = NULL;
static UINT                        *pSurfaceIndices  = NULL;
static UINT                         g_numSurfaces    = 0;
static UINT                         g_nextSurface    = 0;
static GUID                         g_decoderGuid    = {0};
static DXVA2_ConfigPictureDecode    g_decoderConfig  = {0};

/* DXVA2 Video Processor state */
static IDirectXVideoProcessorService *pProcessorService = NULL;
static IDirectXVideoProcessor        *pProcessor        = NULL;

/* Maximum number of decode surfaces */
#define MAX_DECODE_SURFACES 16

IDirect3DDevice9 *dxva2_init(HWND hwnd, int width, int height)
{
    D3DDISPLAYMODE dmode;

    dxva2_cleanup();

    if (!hwnd || width <= 0 || height <= 0)
        return NULL;

    pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) {
        Log_Printf(L"DXVA2: Direct3DCreate9 faile");
        return NULL;
    }

    /* Get current display mode */
    if (IDirect3D9_GetAdapterDisplayMode(pD3D, D3DADAPTER_DEFAULT, &dmode) != D3D_OK) {
        Log_Printf(L"DXVA2: GetAdapterDisplayMode faile");
        dxva2_cleanup();
        return NULL;
    }

    /* Setup present parameters */
    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.Windowed               = TRUE;
    d3dpp.hDeviceWindow          = hwnd;
    d3dpp.BackBufferWidth        = width;
    d3dpp.BackBufferHeight       = height;
    d3dpp.BackBufferFormat       = dmode.Format;
    d3dpp.BackBufferCount        = 1;
    d3dpp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
    d3dpp.PresentationInterval   = D3DPRESENT_INTERVAL_ONE;
    d3dpp.Flags                  = D3DPRESENTFLAG_VIDEO;

    /* Create device with hardware vertex processing */
    HRESULT hr = IDirect3D9_CreateDevice(pD3D, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
        &d3dpp, &pDevice);

    if (FAILED(hr)) {
        /* Fallback to software vertex processing */
        hr = IDirect3D9_CreateDevice(pD3D, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
            &d3dpp, &pDevice);
    }

    if (FAILED(hr)) {
        Log_Printf(L"DXVA2: CreateDevice failed: 0x%08lx", hr);
        dxva2_cleanup();
        return NULL;
    }

    /* Create render target surface */
    hr = IDirect3DDevice9_CreateRenderTarget(pDevice, width, height,
        dmode.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &pRT, NULL);
    if (FAILED(hr)) {
        Log_Printf(L"DXVA2: CreateRenderTarget failed: 0x%08lx", hr);
        /* Non-fatal: we can still use the back buffer */
        pRT = NULL;
    }

    g_hwnd   = hwnd;
    g_width  = width;
    g_height = height;

    Log_Printf(L"DXVA2: D3D9 device created successfully (%dx%d", width, height);
    return pDevice;
}

int dxva2_check_support(void)
{
    IDirectXVideoProcessorService *processor = NULL;
    GUID *guids = NULL;
    UINT count = 0;
    HRESULT hr;
    int supported = 0;

    /* Need a device first - try a temporary one */
    if (!pDevice) {
        /* Just check if D3D9 can be created */
        IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (!d3d) return 0;
        IDirect3D9_Release(d3d);
        return 1;  /* D3D9 available, DXVA2 likely supported */
    }

    /* Create DXVA2 video processor service */
    hr = DXVA2CreateVideoService(pDevice, &IID_IDXVPS,
                                 (void **)&processor);
    if (FAILED(hr)) {
        Log_Printf(L"DXVA2: DXVA2CreateVideoService failed: 0x%08lx", hr);
        return 0;
    }

    /* Enumerate video processor GUIDs */
    hr = IDirectXVideoProcessorService_GetVideoProcessorDeviceGuids(processor, NULL, &count, &guids);
    if (SUCCEEDED(hr) && count > 0) {
        supported = 1;
        CoTaskMemFree(guids);
    }

    IDirectXVideoProcessorService_Release(processor);
    return supported;
}

const wchar_t *dxva2_get_device_info(void)
{
    static wchar_t info[256] = {0};
    D3DADAPTER_IDENTIFIER9 id;

    if (!pD3D) {
        swprintf(info, 256, L"DXVA2: Not initialized");
        return info;
    }

    if (IDirect3D9_GetAdapterIdentifier(pD3D, D3DADAPTER_DEFAULT, 0, &id) == D3D_OK) {
        /* Convert description to wide string */
        wchar_t desc[128] = {0};
        MultiByteToWideChar(CP_ACP, 0, id.Description, -1, desc, 128);
        swprintf(info, 256, L"DXVA2: %s (Vendor: 0x%04X, Device: 0x%04X)",
                 desc, id.VendorId, id.DeviceId);
    } else {
        swprintf(info, 256, L"DXVA2: Device info unavailable");
    }
    return info;
}

IDirect3DDevice9 *dxva2_get_device(void)
{
    return pDevice;
}

int dxva2_present(void)
{
    if (!pDevice) return -1;

    HRESULT hr = IDirect3DDevice9_TestCooperativeLevel(pDevice);
    if (hr == D3DERR_DEVICELOST) return -1;
    if (hr == D3DERR_DEVICENOTRESET) {
        hr = IDirect3DDevice9_Reset(pDevice, &d3dpp);
        if (FAILED(hr)) return -1;
    }

    hr = IDirect3DDevice9_Present(pDevice, NULL, NULL, g_hwnd, NULL);
    return SUCCEEDED(hr) ? 0 : -1;
}

IDirect3DSurface9 *dxva2_get_render_target(void)
{
    if (pRT) {
        IDirect3DSurface9_AddRef(pRT);
        return pRT;
    }
    if (!pDevice) return NULL;

    /* Get back buffer */
    IDirect3DSurface9 *bb = NULL;
    IDirect3DDevice9_GetBackBuffer(pDevice, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    return bb;  /* Caller should release */
}

void dxva2_cleanup(void)
{
    /* Cleanup decoder and processor first */
    dxva2_decoder_cleanup();
    dxva2_processor_cleanup();

    if (pRT)    { IDirect3DSurface9_Release(pRT);    pRT    = NULL; }
    if (pDevice){ IDirect3DDevice9_Release(pDevice);  pDevice = NULL; }
    if (pD3D)   { IDirect3D9_Release(pD3D);           pD3D   = NULL; }
    g_hwnd   = NULL;
    g_width  = 0;
    g_height = 0;
}

/* ==================== DXVA2 Decoder Functions ==================== */

int dxva2_decoder_init(const DXVA2DecoderConfig *config)
{
    HRESULT hr;
    UINT count = 0;
    GUID *guids = NULL;
    UINT i;
    D3DFORMAT *formats = NULL;
    UINT num_formats = 0;
    DXVA2_VideoDesc desc;
    UINT num_configs = 0;
    DXVA2_ConfigPictureDecode *configs = NULL;

    if (!pDevice || !config) return -1;

    /* Cleanup previous decoder if any */
    dxva2_decoder_cleanup();

    /* Create decoder service */
    hr = DXVA2CreateVideoService(pDevice, &IID_IDDS, (void **)&pDecoderService);
    if (FAILED(hr)) {
        Log_Printf(L"DXVA2: Failed to create decoder service: 0x%08lx", hr);
        return -1;
    }

    /* Get decoder device GUIDs */
    hr = IDirectXVideoDecoderService_GetDecoderDeviceGuids(pDecoderService, &count, &guids);
    if (FAILED(hr) || count == 0) {
        Log_Printf(L"DXVA2: No decoder GUIDs availabl");
        dxva2_decoder_cleanup();
        return -1;
    }

    /* Find a suitable decoder GUID (prefer H.264, then HEVC, then any) */
    g_decoderGuid = GUID_NULL;
    for (i = 0; i < count; i++) {
        /* H.264 DXVA2 decoder */
        GUID h264_guid = {0x1b81be68, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}};
        /* HEVC DXVA2 decoder */
        GUID hevc_guid = {0x1b81be69, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}};

        if (IsEqualGUID(&guids[i], &h264_guid)) {
            g_decoderGuid = guids[i];
            Log_Printf(L"DXVA2: Using H.264 decode");
            break;
        } else if (IsEqualGUID(&guids[i], &hevc_guid)) {
            g_decoderGuid = guids[i];
            Log_Printf(L"DXVA2: Using HEVC decode");
            break;
        }
    }

    /* If no specific decoder found, use first available */
    if (IsEqualGUID(&g_decoderGuid, &GUID_NULL) && count > 0) {
        g_decoderGuid = guids[0];
        Log_Printf(L"DXVA2: Using default decode");
    }

    CoTaskMemFree(guids);

    if (IsEqualGUID(&g_decoderGuid, &GUID_NULL)) {
        Log_Printf(L"DXVA2: No suitable decoder foun");
        dxva2_decoder_cleanup();
        return -1;
    }

    /* Check if NV12 format is supported (most common for H.264/HEVC) */
    hr = IDirectXVideoDecoderService_GetDecoderRenderTargets(pDecoderService, &g_decoderGuid, &num_formats, &formats);
    if (FAILED(hr) || num_formats == 0) {
        Log_Printf(L"DXVA2: No render targets available for decode");
        dxva2_decoder_cleanup();
        return -1;
    }

    /* Check for NV12 support */
    int nv12_supported = 0;
    for (i = 0; i < num_formats; i++) {
        if (formats[i] == D3DFMT_NV12) {
            nv12_supported = 1;
            break;
        }
    }
    CoTaskMemFree(formats);

    if (!nv12_supported) {
        Log_Printf(L"DXVA2: NV12 format not supported by decode");
        dxva2_decoder_cleanup();
        return -1;
    }

    /* Create video description */
    ZeroMemory(&desc, sizeof(desc));
    desc.SampleWidth = config->width;
    desc.SampleHeight = config->height;
    desc.Format = D3DFMT_NV12;
    desc.InputSampleFreq.Numerator = 30;
    desc.InputSampleFreq.Denominator = 1;
    desc.OutputFrameFreq.Numerator = 30;
    desc.OutputFrameFreq.Denominator = 1;

    /* Get decoder configurations */
    hr = IDirectXVideoDecoderService_GetDecoderConfigurations(pDecoderService, &g_decoderGuid, &desc, NULL, &num_configs, &configs);
    if (FAILED(hr) || num_configs == 0) {
        Log_Printf(L"DXVA2: No decoder configurations availabl");
        dxva2_decoder_cleanup();
        return -1;
    }

    /* Use first configuration (usually the best) */
    g_decoderConfig = configs[0];
    CoTaskMemFree(configs);

    /* Create decode surfaces */
    g_numSurfaces = config->num_surfaces;
    if (g_numSurfaces > MAX_DECODE_SURFACES) g_numSurfaces = MAX_DECODE_SURFACES;
    if (g_numSurfaces < 4) g_numSurfaces = 4;  /* Minimum for H.264 */

    ppDecodeSurfaces = (IDirect3DSurface9 **)malloc(g_numSurfaces * sizeof(IDirect3DSurface9 *));
    pSurfaceIndices = (UINT *)malloc(g_numSurfaces * sizeof(UINT));
    if (!ppDecodeSurfaces || !pSurfaceIndices) {
        Log_Printf(L"DXVA2: Memory allocation faile");
        dxva2_decoder_cleanup();
        return -1;
    }

    ZeroMemory(ppDecodeSurfaces, g_numSurfaces * sizeof(IDirect3DSurface9 *));
    ZeroMemory(pSurfaceIndices, g_numSurfaces * sizeof(UINT));

    /* Create surfaces through decoder service */
    hr = IDirectXVideoDecoderService_CreateSurface(pDecoderService,
        config->width, config->height, g_numSurfaces - 1,
        D3DFMT_NV12, D3DPOOL_DEFAULT, 0, DXVA2_VideoDecoderRenderTarget,
        ppDecodeSurfaces, (void **)pSurfaceIndices);
    if (FAILED(hr)) {
        Log_Printf(L"DXVA2: Failed to create decode surfaces: 0x%08lx", hr);
        dxva2_decoder_cleanup();
        return -1;
    }

    /* Create decoder */
    hr = IDirectXVideoDecoderService_CreateVideoDecoder(pDecoderService, &g_decoderGuid, &desc, &g_decoderConfig, ppDecodeSurfaces, g_numSurfaces, &pDecoder);
    if (FAILED(hr)) {
        Log_Printf(L"DXVA2: Failed to create decoder: 0x%08lx", hr);
        dxva2_decoder_cleanup();
        return -1;
    }

    g_nextSurface = 0;
    Log_Printf(L"DXVA2: Decoder initialized (%dx%d, %d surfaces", config->width, config->height, g_numSurfaces);
    return 0;
}

int dxva2_decoder_get_surface(IDirect3DSurface9 **surface)
{
    if (!pDecoder || !ppDecodeSurfaces || !surface) return -1;

    /* Find a free surface (simple round-robin for now) */
    UINT index = g_nextSurface;
    g_nextSurface = (g_nextSurface + 1) % g_numSurfaces;

    *surface = ppDecodeSurfaces[index];
    if (*surface) {
        IDirect3DSurface9_AddRef(*surface);
    }
    return (int)index;
}

void dxva2_decoder_release_surface(int index)
{
    /* In a real implementation, you would track surface usage */
    /* For now, we just do nothing as surfaces are managed in a pool */
    (void)index;
}

int dxva2_decoder_decode(const BYTE *buffer, DWORD size, LONGLONG pts)
{
    if (!pDecoder || !ppDecodeSurfaces || !buffer || size == 0) return -1;

    HRESULT hr;
    UINT surface_index;
    IDirect3DSurface9 *surface = NULL;
    void *pbBuffer = NULL;
    UINT cbBuffer = 0;

    /* Select a surface for decoding (round-robin) */
    surface_index = g_nextSurface;
    g_nextSurface = (g_nextSurface + 1) % g_numSurfaces;
    surface = ppDecodeSurfaces[surface_index];

    if (!surface) {
        Log_Printf(L"DXVA2: No decode surface availabl");
        return -1;
    }

    /* Begin decode frame */
    hr = IDirectXVideoDecoder_BeginFrame(pDecoder, surface, NULL);
    if (FAILED(hr)) {
        Log_Printf(L"DXVA2: BeginFrame failed: 0x%08lx", hr);
        return -1;
    }

    /* Get bitstream buffer */
    hr = IDirectXVideoDecoder_GetBuffer(pDecoder, DXVA2_BitStreamDateBufferType, &pbBuffer, &cbBuffer);
    if (FAILED(hr)) {
        Log_Printf(L"DXVA2: GetBuffer failed: 0x%08lx", hr);
        IDirectXVideoDecoder_EndFrame(pDecoder, NULL);
        return -1;
    }

    /* Copy bitstream data to buffer */
    if (cbBuffer < size) {
        Log_Printf(L"DXVA2: Buffer too small (%u < %lu", cbBuffer, size);
        IDirectXVideoDecoder_ReleaseBuffer(pDecoder, DXVA2_BitStreamDateBufferType);
        IDirectXVideoDecoder_EndFrame(pDecoder, NULL);
        return -1;
    }
    memcpy(pbBuffer, buffer, size);

    /* Release buffer */
    hr = IDirectXVideoDecoder_ReleaseBuffer(pDecoder, DXVA2_BitStreamDateBufferType);
    if (FAILED(hr)) {
        Log_Printf(L"DXVA2: ReleaseBuffer failed: 0x%08lx", hr);
        IDirectXVideoDecoder_EndFrame(pDecoder, NULL);
        return -1;
    }

    /* End decode frame */
    hr = IDirectXVideoDecoder_EndFrame(pDecoder, NULL);
    if (FAILED(hr)) {
        Log_Printf(L"DXVA2: EndFrame failed: 0x%08lx", hr);
        return -1;
    }

    return 0;
}

IDirect3DSurface9 *dxva2_decoder_get_decoded_surface(void)
{
    if (!ppDecodeSurfaces || g_numSurfaces == 0) return NULL;

    /* Return the most recently decoded surface */
    UINT index = (g_nextSurface > 0) ? g_nextSurface - 1 : g_numSurfaces - 1;
    IDirect3DSurface9 *surface = ppDecodeSurfaces[index];
    if (surface) {
        IDirect3DSurface9_AddRef(surface);
    }
    return surface;
}

void dxva2_decoder_cleanup(void)
{
    if (pDecoder) {
        IDirectXVideoDecoder_Release(pDecoder);
        pDecoder = NULL;
    }

    if (ppDecodeSurfaces) {
        for (UINT i = 0; i < g_numSurfaces; i++) {
            if (ppDecodeSurfaces[i]) {
                IDirect3DSurface9_Release(ppDecodeSurfaces[i]);
                ppDecodeSurfaces[i] = NULL;
            }
        }
        free(ppDecodeSurfaces);
        ppDecodeSurfaces = NULL;
    }

    if (pSurfaceIndices) {
        free(pSurfaceIndices);
        pSurfaceIndices = NULL;
    }

    if (pDecoderService) {
        IDirectXVideoDecoderService_Release(pDecoderService);
        pDecoderService = NULL;
    }

    g_numSurfaces = 0;
    g_nextSurface = 0;
    g_decoderGuid = GUID_NULL;
    ZeroMemory(&g_decoderConfig, sizeof(g_decoderConfig));
}

/* ==================== DXVA2 Video Processor Functions ==================== */

int dxva2_processor_init(DXVA2_VideoFormat input_format, DXVA2_VideoFormat output_format)
{
    HRESULT hr;
    GUID *proc_guids = NULL;
    UINT num_guids = 0;

    if (!pDevice) return -1;

    /* Cleanup previous processor if any */
    dxva2_processor_cleanup();

    /* Create processor service */
    hr = DXVA2CreateVideoService(pDevice, &IID_IDXVPS, (void **)&pProcessorService);
    if (FAILED(hr)) {
        Log_Printf(L"DXVA2: Failed to create processor service: 0x%08lx", hr);
        return -1;
    }

    /* Get video processor GUIDs */
    hr = IDirectXVideoProcessorService_GetVideoProcessorDeviceGuids(pProcessorService, NULL, &num_guids, &proc_guids);
    if (FAILED(hr) || num_guids == 0) {
        Log_Printf(L"DXVA2: No video processor GUIDs availabl");
        dxva2_processor_cleanup();
        return -1;
    }

    /* Use the first available processor (usually DXVA2_VideoProcProgressiveDevice) */
    GUID proc_guid = proc_guids[0];
    CoTaskMemFree(proc_guids);

    /* Create video processor with simplified description */
    DXVA2_VideoDesc desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.SampleWidth = g_width;
    desc.SampleHeight = g_height;
    desc.SampleFormat.SampleFormat = DXVA2_SampleProgressiveFrame;
    desc.Format = D3DFMT_NV12;
    desc.InputSampleFreq.Numerator = 30;
    desc.InputSampleFreq.Denominator = 1;
    desc.OutputFrameFreq.Numerator = 30;
    desc.OutputFrameFreq.Denominator = 1;

    /* CreateVideoProcessor(This, VideoProcDeviceGuid, pVideoDesc, RenderTargetFormat, MaxNumSubStreams, ppVidProcess) */
    hr = IDirectXVideoProcessorService_CreateVideoProcessor(pProcessorService, &proc_guid, &desc, D3DFMT_X8R8G8B8, 0, &pProcessor);
    if (FAILED(hr)) {
        Log_Printf(L"DXVA2: Failed to create video processor: 0x%08lx", hr);
        dxva2_processor_cleanup();
        return -1;
    }

    Log_Printf(L"DXVA2: Video processor initialize");
    return 0;
}

int dxva2_processor_render(IDirect3DSurface9 *src_surface,
                           const RECT *src_rect, const RECT *dst_rect)
{
    HRESULT hr;
    DXVA2_VideoSample sample;
    DXVA2_VideoProcessBltParams params;

    if (!pProcessor || !pDevice || !src_surface) return -1;

    /* Get render target */
    IDirect3DSurface9 *rt = dxva2_get_render_target();
    if (!rt) return -1;

    /* Setup video sample */
    ZeroMemory(&sample, sizeof(sample));
    sample.SampleFormat.SampleFormat = DXVA2_SampleProgressiveFrame;
    sample.Start = 0;
    sample.End = 1;
    sample.SrcSurface = src_surface;

    if (src_rect) {
        sample.SrcRect = *src_rect;
    } else {
        sample.SrcRect.left = 0;
        sample.SrcRect.top = 0;
        sample.SrcRect.right = g_width;
        sample.SrcRect.bottom = g_height;
    }

    if (dst_rect) {
        sample.DstRect = *dst_rect;
    } else {
        sample.DstRect.left = 0;
        sample.DstRect.top = 0;
        sample.DstRect.right = g_width;
        sample.DstRect.bottom = g_height;
    }

    /* Setup blt params */
    ZeroMemory(&params, sizeof(params));
    params.TargetFrame = 0;
    params.TargetRect = sample.DstRect;
    /* BackgroundColor is DXVA2_AYUVSample16 structure - set to black */
    params.BackgroundColor.Cr = 0;
    params.BackgroundColor.Cb = 0;
    params.BackgroundColor.Y = 0;
    params.BackgroundColor.Alpha = 0xFFFF;
    /* Use default ProcAmp values (zeroed out) */
    params.ProcAmpValues.Brightness.ll = 0;
    params.ProcAmpValues.Contrast.ll = 0;
    params.ProcAmpValues.Saturation.ll = 0;

    /* Process and render */
    hr = IDirectXVideoProcessor_VideoProcessBlt(pProcessor, rt, &params, &sample, 1, NULL);
    if (FAILED(hr)) {
        Log_Printf(L"DXVA2: VideoProcessBlt failed: 0x%08lx", hr);
        IDirect3DSurface9_Release(rt);
        return -1;
    }

    /* Present to screen */
    int ret = dxva2_present();

    IDirect3DSurface9_Release(rt);
    return ret;
}

void dxva2_processor_cleanup(void)
{
    if (pProcessor) {
        IDirectXVideoProcessor_Release(pProcessor);
        pProcessor = NULL;
    }

    if (pProcessorService) {
        IDirectXVideoProcessorService_Release(pProcessorService);
        pProcessorService = NULL;
    }
}

/* ==================== DXVA2 Surface Helper Functions ==================== */

IDirect3DSurface9 *dxva2_create_surface(UINT width, UINT height, D3DFORMAT format)
{
    HRESULT hr;
    IDirect3DSurface9 *surface = NULL;

    if (!pDevice) return NULL;

    hr = IDirect3DDevice9_CreateOffscreenPlainSurface(pDevice, width, height, format, D3DPOOL_DEFAULT, &surface, NULL);
    if (FAILED(hr)) {
        Log_Printf(L"DXVA2: CreateOffscreenPlainSurface failed: 0x%08lx", hr);
        return NULL;
    }

    return surface;
}

int dxva2_upload_surface(IDirect3DSurface9 *surface, const BYTE *data, int stride, D3DFORMAT format)
{
    HRESULT hr;
    D3DLOCKED_RECT locked;
    UINT width, height;
    D3DSURFACE_DESC desc;

    if (!surface || !data) return -1;

    /* Get surface description */
    hr = IDirect3DSurface9_GetDesc(surface, &desc);
    if (FAILED(hr)) return -1;

    width = desc.Width;
    height = desc.Height;

    /* Lock surface */
    hr = IDirect3DSurface9_LockRect(surface, &locked, NULL, D3DLOCK_DISCARD);
    if (FAILED(hr)) {
        Log_Printf(L"DXVA2: LockRect failed: 0x%08lx", hr);
        return -1;
    }

    /* Copy data based on format */
    if (format == D3DFMT_NV12) {
        /* NV12: Y plane + interleaved UV plane */
        BYTE *dst = (BYTE *)locked.pBits;
        const BYTE *src = data;

        /* Copy Y plane */
        for (UINT y = 0; y < height; y++) {
            memcpy(dst, src, width);
            dst += locked.Pitch;
            src += stride;
        }

        /* Copy UV plane */
        src = data + stride * height;
        for (UINT y = 0; y < height / 2; y++) {
            memcpy(dst, src, width);
            dst += locked.Pitch;
            src += stride;
        }
    } else if (format == D3DFMT_YUY2) {
        /* YUY2: interleaved YUYV */
        BYTE *dst = (BYTE *)locked.pBits;
        const BYTE *src = data;

        for (UINT y = 0; y < height; y++) {
            memcpy(dst, src, width * 2);
            dst += locked.Pitch;
            src += stride;
        }
    } else if (format == D3DFMT_X8R8G8B8 || format == D3DFMT_A8R8G8B8) {
        /* RGB32 */
        BYTE *dst = (BYTE *)locked.pBits;
        const BYTE *src = data;

        for (UINT y = 0; y < height; y++) {
            memcpy(dst, src, width * 4);
            dst += locked.Pitch;
            src += stride;
        }
    } else {
        Log_Printf(L"DXVA2: Unsupported surface forma");
        IDirect3DSurface9_UnlockRect(surface);
        return -1;
    }

    IDirect3DSurface9_UnlockRect(surface);
    return 0;
}
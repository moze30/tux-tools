/*
 * D3D12 Video Acceleration Helper
 * Manages D3D12 device, video decoder, and video processor.
 * Based on Direct3D 12 Video APIs documentation from medfound/.
 */
#define INITGUID
#include "d3d12_video_helper.h"
#include "log.h"

#include <d3d12.h>
#include <d3d12video.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* D3D12 device state */
static ID3D12Device           *pD3D12Device      = NULL;
static ID3D12CommandQueue      *pVideoDecodeQueue = NULL;
static ID3D12CommandQueue      *pVideoProcessQueue = NULL;
static IDXGISwapChain3         *pSwapChain        = NULL;
static ID3D12Resource         **ppBackBuffers     = NULL;
static UINT                     g_numBackBuffers  = 0;
static UINT                     g_frameIndex      = 0;
static HWND                     g_hwnd            = NULL;
static int                      g_width           = 0;
static int                      g_height          = 0;

/* D3D12 Video Decoder state */
static ID3D12VideoDevice       *pVideoDevice      = NULL;
static ID3D12VideoDecoder      *pVideoDecoder     = NULL;
static ID3D12VideoDecoderHeap  *pVideoDecoderHeap = NULL;
static ID3D12CommandAllocator  *pDecodeAllocator  = NULL;
static ID3D12VideoDecodeCommandList *pDecodeCommandList = NULL;
static ID3D12Resource         **ppDecodeTextures  = NULL;
static ID3D12Resource         **ppDecodeSurfaces  = NULL; /* DPB surfaces */
static UINT                     g_numDecodeTextures = 0;
static UINT                     g_numDecodeSurfaces = 0;
static UINT                     g_nextTexture     = 0;
static GUID                     g_decoderProfile  = {0};
static UINT                     g_texWidth        = 0;
static UINT                     g_texHeight       = 0;

/* D3D12 Video Processor state.
 *
 * MinGW's d3d12video.h does not declare ID3D12VideoProcessor or
 * ID3D12VideoProcessCommandList at all, so on that toolchain these three
 * pointers - and the four functions that use them - cannot be compiled. The
 * upstream source already returns early from those functions under
 * __MINGW32__, but it did so with a plain #ifdef whose body was still parsed;
 * turning it into #ifdef/#else is what makes the exclusion real. Nothing is
 * lost on MinGW: mf_decoder.c only calls d3d12_video_processor_init/_render
 * from its own #else (MSVC) branch, and decodes in software otherwise. */
#ifndef __MINGW32__
static ID3D12VideoProcessor    *pVideoProcessor   = NULL;
static ID3D12CommandAllocator  *pProcessAllocator = NULL;
static ID3D12VideoProcessCommandList *pProcessCommandList = NULL;
#endif

/* Fence for synchronization */
static ID3D12Fence            *pFence            = NULL;
static HANDLE                  hFenceEvent       = NULL;
static UINT64                  g_fenceValue      = 0;

/* Maximum number of decode textures */
#define MAX_DECODE_TEXTURES 16
#define MAX_DECODE_SURFACES 16

/* Helper macro for safe release */
#define SAFE_RELEASE(p) do { if ((p)) { (p)->lpVtbl->Release((p)); (p) = NULL; } } while(0)

/* Wait for GPU to finish */
static void WaitForGpu(void)
{
    if (!pVideoDecodeQueue || !pFence || !hFenceEvent) return;

    g_fenceValue++;
    ID3D12CommandQueue_Signal(pVideoDecodeQueue, pFence, g_fenceValue);
    if (ID3D12Fence_GetCompletedValue(pFence) < g_fenceValue) {
        ID3D12Fence_SetEventOnCompletion(pFence, g_fenceValue, hFenceEvent);
        WaitForSingleObject(hFenceEvent, INFINITE);
    }
}

int d3d12_video_check_support(void)
{
    ID3D12Device *device = NULL;
    HRESULT hr;

#ifdef __MINGW32__
    Log_Printf(L"D3D12: Video decode check skipped in MinGW build (d3d12video.h structures may differ");
    return 0;
#else

    /* Try to create a D3D12 device */
    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device);
    if (FAILED(hr) || !device) {
        return 0;
    }

    /* Check if video decode is supported */
    ID3D12VideoDevice *videoDevice = NULL;
    hr = ID3D12Device_QueryInterface(device, &IID_ID3D12VideoDevice, (void **)&videoDevice);
    if (FAILED(hr) || !videoDevice) {
        SAFE_RELEASE(device);
        return 0;
    }

    /* Check H.264 decode support */
    D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT support;
    memset(&support, 0, sizeof(support));
    support.Configuration.DecodeProfile = D3D12_VIDEO_DECODE_PROFILE_H264;
    support.Configuration.BitstreamEncryption = D3D12_BITSTREAM_ENCRYPTION_TYPE_NONE;
    support.Configuration.InterlaceType = D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_NONE;
    support.Width = 1920;
    support.Height = 1080;
    support.DecodeFormat = DXGI_FORMAT_NV12;

    hr = ID3D12VideoDevice_CheckFeatureSupport(videoDevice,
        D3D12_FEATURE_VIDEO_DECODE_SUPPORT, &support, sizeof(support));

    SAFE_RELEASE(videoDevice);
    SAFE_RELEASE(device);

    return (SUCCEEDED(hr) && (support.SupportFlags & D3D12_VIDEO_DECODE_SUPPORT_FLAG_SUPPORTED)) ? 1 : 0;
#endif
}

int d3d12_video_init(HWND hwnd, int width, int height)
{
    HRESULT hr;
    IDXGIFactory4 *pFactory = NULL;

#ifdef __MINGW32__
    Log_Printf(L"D3D12: Video init skipped in MinGW build (d3d12video.h structures may differ)");
    return -1;
#else

    d3d12_video_cleanup();

    if (!hwnd || width <= 0 || height <= 0)
        return -1;

    /* Create DXGI factory */
    hr = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&pFactory);
    if (FAILED(hr)) {
        Log_Printf(L"D3D12: CreateDXGIFactory1 failed: 0x%08lx", hr);
        return -1;
    }

    /* Create D3D12 device */
    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&pD3D12Device);
    if (FAILED(hr)) {
        Log_Printf(L"D3D12: CreateDevice failed: 0x%08lx", hr);
        SAFE_RELEASE(pFactory);
        return -1;
    }

    /* Create video decode command queue */
    D3D12_COMMAND_QUEUE_DESC queueDesc;
    memset(&queueDesc, 0, sizeof(queueDesc));
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    hr = ID3D12Device_CreateCommandQueue(pD3D12Device, &queueDesc, &IID_ID3D12CommandQueue, (void **)&pVideoDecodeQueue);
    if (FAILED(hr)) {
        Log_Printf(L"D3D12: CreateCommandQueue (decode) failed: 0x%08lx", hr);
        d3d12_video_cleanup();
        SAFE_RELEASE(pFactory);
        return -1;
    }

    /* Create video process command queue */
    memset(&queueDesc, 0, sizeof(queueDesc));
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    hr = ID3D12Device_CreateCommandQueue(pD3D12Device, &queueDesc, &IID_ID3D12CommandQueue, (void **)&pVideoProcessQueue);
    if (FAILED(hr)) {
        Log_Printf(L"D3D12: CreateCommandQueue (process) failed: 0x%08lx", hr);
        d3d12_video_cleanup();
        SAFE_RELEASE(pFactory);
        return -1;
    }

    /* Create swap chain */
    DXGI_SWAP_CHAIN_DESC1 scd;
    memset(&scd, 0, sizeof(scd));
    scd.Width = width;
    scd.Height = height;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    scd.Scaling = DXGI_SCALING_STRETCH;

    IDXGISwapChain1 *pSwapChain1 = NULL;
    hr = IDXGIFactory4_CreateSwapChainForHwnd(pFactory, (IUnknown *)pVideoDecodeQueue, hwnd, &scd, NULL, NULL, &pSwapChain1);
    SAFE_RELEASE(pFactory);
    if (FAILED(hr)) {
        Log_Printf(L"D3D12: CreateSwapChainForHwnd failed: 0x%08lx", hr);
        d3d12_video_cleanup();
        return -1;
    }

    hr = IDXGISwapChain1_QueryInterface(pSwapChain1, &IID_IDXGISwapChain3, (void **)&pSwapChain);
    SAFE_RELEASE(pSwapChain1);
    if (FAILED(hr)) {
        Log_Printf(L"D3D12: QueryInterface IDXGISwapChain3 failed: 0x%08lx", hr);
        d3d12_video_cleanup();
        return -1;
    }

    /* Get back buffers */
    g_numBackBuffers = 2;
    ppBackBuffers = (ID3D12Resource **)calloc(g_numBackBuffers, sizeof(ID3D12Resource *));
    if (!ppBackBuffers) {
        d3d12_video_cleanup();
        return -1;
    }

    for (UINT i = 0; i < g_numBackBuffers; i++) {
        hr = IDXGISwapChain3_GetBuffer(pSwapChain, i, &IID_ID3D12Resource, (void **)&ppBackBuffers[i]);
        if (FAILED(hr)) {
            Log_Printf(L"D3D12: GetBuffer %u failed: 0x%08lx", i, hr);
            d3d12_video_cleanup();
            return -1;
        }
    }

    /* Create fence for synchronization */
    hr = ID3D12Device_CreateFence(pD3D12Device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&pFence);
    if (FAILED(hr)) {
        Log_Printf(L"D3D12: CreateFence failed: 0x%08lx", hr);
        d3d12_video_cleanup();
        return -1;
    }

    hFenceEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!hFenceEvent) {
        Log_Printf(L"D3D12: CreateEvent faile");
        d3d12_video_cleanup();
        return -1;
    }

    g_hwnd = hwnd;
    g_width = width;
    g_height = height;
    g_frameIndex = 0;

    Log_Printf(L"D3D12: Device created successfully (%dx%d", width, height);
    return 0;
#endif
}

const wchar_t *d3d12_video_get_device_info(void)
{
    static wchar_t info[256] = {0};

    if (!pD3D12Device) {
        swprintf(info, 256, L"D3D12: Not initialized");
        return info;
    }

    /* Get DXGI device */
    IDXGIDevice *pDXGIDevice = NULL;
    HRESULT hr = ID3D12Device_QueryInterface(pD3D12Device, &IID_IDXGIDevice, (void **)&pDXGIDevice);
    if (SUCCEEDED(hr)) {
        IDXGIAdapter *pAdapter = NULL;
        hr = IDXGIDevice_GetAdapter(pDXGIDevice, &pAdapter);
        if (SUCCEEDED(hr)) {
            DXGI_ADAPTER_DESC desc;
            hr = IDXGIAdapter_GetDesc(pAdapter, &desc);
            if (SUCCEEDED(hr)) {
                swprintf(info, 256, L"D3D12: %s (Vendor: 0x%04X, Device: 0x%04X)",
                         desc.Description, desc.VendorId, desc.DeviceId);
            }
            SAFE_RELEASE(pAdapter);
        }
        SAFE_RELEASE(pDXGIDevice);
    }

    if (info[0] == 0) {
        swprintf(info, 256, L"D3D12: Device info unavailable");
    }
    return info;
}

void d3d12_video_cleanup(void)
{
    WaitForGpu();

    d3d12_video_processor_cleanup();
    d3d12_video_decoder_cleanup();

    if (hFenceEvent) {
        CloseHandle(hFenceEvent);
        hFenceEvent = NULL;
    }
    SAFE_RELEASE(pFence);

    if (ppBackBuffers) {
        for (UINT i = 0; i < g_numBackBuffers; i++) {
            SAFE_RELEASE(ppBackBuffers[i]);
        }
        free(ppBackBuffers);
        ppBackBuffers = NULL;
    }
    g_numBackBuffers = 0;

    SAFE_RELEASE(pSwapChain);
    SAFE_RELEASE(pVideoProcessQueue);
    SAFE_RELEASE(pVideoDecodeQueue);
    SAFE_RELEASE(pD3D12Device);

    g_hwnd = NULL;
    g_width = 0;
    g_height = 0;
}

int d3d12_video_decoder_init(int width, int height)
{
    HRESULT hr;

    if (!pD3D12Device) {
        Log_Printf(L"D3D12: Device not initialize");
        return -1;
    }

#ifdef __MINGW32__
    Log_Printf(L"D3D12: Video decoder not supported in MinGW build (d3d12video.h structures may differ");
    return -1;
#else

    d3d12_video_decoder_cleanup();

    /* Query video device interface */
    hr = ID3D12Device_QueryInterface(pD3D12Device, &IID_ID3D12VideoDevice, (void **)&pVideoDevice);
    if (FAILED(hr)) {
        Log_Printf(L"D3D12: QueryInterface ID3D12VideoDevice failed: 0x%08lx", hr);
        return -1;
    }

    /* Check H.264 decode support */
    D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT support;
    memset(&support, 0, sizeof(support));
    support.Configuration.DecodeProfile = D3D12_VIDEO_DECODE_PROFILE_H264;
    support.Configuration.BitstreamEncryption = D3D12_BITSTREAM_ENCRYPTION_TYPE_NONE;
    support.Configuration.InterlaceType = D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_NONE;
    support.Width = width;
    support.Height = height;
    support.DecodeFormat = DXGI_FORMAT_NV12;

    hr = ID3D12VideoDevice_CheckFeatureSupport(pVideoDevice,
        D3D12_FEATURE_VIDEO_DECODE_SUPPORT, &support, sizeof(support));
    if (FAILED(hr) || !(support.SupportFlags & D3D12_VIDEO_DECODE_SUPPORT_FLAG_SUPPORTED)) {
        Log_Printf(L"D3D12: H.264 decode not supporte");
        d3d12_video_decoder_cleanup();
        return -1;
    }

    g_decoderProfile = D3D12_VIDEO_DECODE_PROFILE_H264;

    /* Create video decoder */
    D3D12_VIDEO_DECODER_DESC decoderDesc;
    memset(&decoderDesc, 0, sizeof(decoderDesc));
    decoderDesc.NodeMask = 0;
    decoderDesc.Configuration.DecodeProfile = g_decoderProfile;
    decoderDesc.Configuration.BitstreamEncryption = D3D12_BITSTREAM_ENCRYPTION_TYPE_NONE;
    decoderDesc.Configuration.InterlaceType = D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_NONE;

    hr = ID3D12VideoDevice_CreateVideoDecoder(pVideoDevice, &decoderDesc, &IID_ID3D12VideoDecoder, (void **)&pVideoDecoder);
    if (FAILED(hr)) {
        Log_Printf(L"D3D12: CreateVideoDecoder failed: 0x%08lx", hr);
        d3d12_video_decoder_cleanup();
        return -1;
    }

    /* Create video decoder heap */
    D3D12_VIDEO_DECODER_HEAP_DESC heapDesc;
    memset(&heapDesc, 0, sizeof(heapDesc));
    heapDesc.NodeMask = 0;
    heapDesc.Configuration.DecodeProfile = g_decoderProfile;
    heapDesc.Configuration.BitstreamEncryption = D3D12_BITSTREAM_ENCRYPTION_TYPE_NONE;
    heapDesc.Configuration.InterlaceType = D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_NONE;
    heapDesc.DecodeWidth = width;
    heapDesc.DecodeHeight = height;
    heapDesc.MaxDecodePictureBufferCount = MAX_DECODE_SURFACES;

    hr = ID3D12VideoDevice_CreateVideoDecoderHeap(pVideoDevice, &heapDesc, &IID_ID3D12VideoDecoderHeap, (void **)&pVideoDecoderHeap);
    if (FAILED(hr)) {
        Log_Printf(L"D3D12: CreateVideoDecoderHeap failed: 0x%08lx", hr);
        d3d12_video_decoder_cleanup();
        return -1;
    }

    /* Create command allocator for decode */
    hr = ID3D12Device_CreateCommandAllocator(pD3D12Device, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE,
        &IID_ID3D12CommandAllocator, (void **)&pDecodeAllocator);
    if (FAILED(hr)) {
        Log_Printf(L"D3D12: CreateCommandAllocator failed: 0x%08lx", hr);
        d3d12_video_decoder_cleanup();
        return -1;
    }

    /* Create video decode command list */
    hr = ID3D12Device_CreateCommandList(pD3D12Device, 0, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE,
        pDecodeAllocator, NULL, &IID_ID3D12VideoDecodeCommandList, (void **)&pDecodeCommandList);
    if (FAILED(hr)) {
        Log_Printf(L"D3D12: CreateCommandList failed: 0x%08lx", hr);
        d3d12_video_decoder_cleanup();
        return -1;
    }

    /* Create decode output textures (NV12 format) */
    g_numDecodeTextures = MAX_DECODE_TEXTURES;
    g_texWidth = width;
    g_texHeight = height;
    ppDecodeTextures = (ID3D12Resource **)calloc(g_numDecodeTextures, sizeof(ID3D12Resource *));
    if (!ppDecodeTextures) {
        d3d12_video_decoder_cleanup();
        return -1;
    }

    D3D12_RESOURCE_DESC texDesc;
    memset(&texDesc, 0, sizeof(texDesc));
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_NV12;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps;
    memset(&heapProps, 0, sizeof(heapProps));
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    for (UINT i = 0; i < g_numDecodeTextures; i++) {
        hr = ID3D12Device_CreateCommittedResource(pD3D12Device, &heapProps,
            D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&ppDecodeTextures[i]);
        if (FAILED(hr)) {
            Log_Printf(L"D3D12: CreateCommittedResource for decode texture %u failed: 0x%08lx", i, hr);
            d3d12_video_decoder_cleanup();
            return -1;
        }
    }

    /* Create DPB (Decode Picture Buffer) surfaces */
    g_numDecodeSurfaces = MAX_DECODE_SURFACES;
    ppDecodeSurfaces = (ID3D12Resource **)calloc(g_numDecodeSurfaces, sizeof(ID3D12Resource *));
    if (!ppDecodeSurfaces) {
        d3d12_video_decoder_cleanup();
        return -1;
    }

    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    for (UINT i = 0; i < g_numDecodeSurfaces; i++) {
        hr = ID3D12Device_CreateCommittedResource(pD3D12Device, &heapProps,
            D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&ppDecodeSurfaces[i]);
        if (FAILED(hr)) {
            Log_Printf(L"D3D12: CreateCommittedResource for DPB surface %u failed: 0x%08lx", i, hr);
            d3d12_video_decoder_cleanup();
            return -1;
        }
    }

    Log_Printf(L"D3D12: Video decoder initialized (H.264, %dx%d, %u textures, %u DPB surfaces", width, height, g_numDecodeTextures, g_numDecodeSurfaces);
    return 0;
#endif
}

void d3d12_video_decoder_cleanup(void)
{
    if (ppDecodeTextures) {
        for (UINT i = 0; i < g_numDecodeTextures; i++) {
            SAFE_RELEASE(ppDecodeTextures[i]);
        }
        free(ppDecodeTextures);
        ppDecodeTextures = NULL;
    }
    g_numDecodeTextures = 0;

    if (ppDecodeSurfaces) {
        for (UINT i = 0; i < g_numDecodeSurfaces; i++) {
            SAFE_RELEASE(ppDecodeSurfaces[i]);
        }
        free(ppDecodeSurfaces);
        ppDecodeSurfaces = NULL;
    }
    g_numDecodeSurfaces = 0;

    SAFE_RELEASE(pDecodeCommandList);
    SAFE_RELEASE(pDecodeAllocator);
    SAFE_RELEASE(pVideoDecoderHeap);
    SAFE_RELEASE(pVideoDecoder);
    SAFE_RELEASE(pVideoDevice);

    memset(&g_decoderProfile, 0, sizeof(g_decoderProfile));
}

int d3d12_video_upload_texture(void *data, int stride, int format)
{
    if (!pD3D12Device || !ppDecodeTextures || g_numDecodeTextures == 0) {
        return -1;
    }

#ifdef __MINGW32__
    (void)data; (void)stride; (void)format;
    Log_Printf(L"D3D12: Video texture upload not supported in MinGW buil");
    return -1;
#else

    /* Get next available texture */
    UINT texIndex = g_nextTexture % g_numDecodeTextures;
    ID3D12Resource *pTexture = ppDecodeTextures[texIndex];
    if (!pTexture) return -1;

    /* Create upload buffer - use stored texture dimensions */
    D3D12_RESOURCE_DESC texDesc;
    memset(&texDesc, 0, sizeof(texDesc));
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = g_texWidth;
    texDesc.Height = g_texHeight;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_NV12;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    UINT64 uploadSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT numRows;
    UINT64 rowSize;

    ID3D12Device_GetCopyableFootprints(pD3D12Device, &texDesc, 0, 1, 0, &footprint, &numRows, &rowSize, &uploadSize);

    D3D12_RESOURCE_DESC uploadDesc;
    memset(&uploadDesc, 0, sizeof(uploadDesc));
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES uploadHeapProps;
    memset(&uploadHeapProps, 0, sizeof(uploadHeapProps));
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    ID3D12Resource *pUploadBuffer = NULL;
    HRESULT hr = ID3D12Device_CreateCommittedResource(pD3D12Device, &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
        &IID_ID3D12Resource, (void **)&pUploadBuffer);
    if (FAILED(hr)) {
        Log_Printf(L"D3D12: CreateCommittedResource for upload buffer failed: 0x%08lx", hr);
        return -1;
    }

    /* Map and copy data */
    void *pMappedData = NULL;
    D3D12_RANGE readRange = {0, 0};
    hr = ID3D12Resource_Map(pUploadBuffer, 0, &readRange, &pMappedData);
    if (FAILED(hr)) {
        Log_Printf(L"D3D12: Map upload buffer failed: 0x%08lx", hr);
        SAFE_RELEASE(pUploadBuffer);
        return -1;
    }

    /* Copy data row by row */
    BYTE *pDst = (BYTE *)pMappedData + footprint.Offset;
    BYTE *pSrc = (BYTE *)data;
    for (UINT row = 0; row < numRows; row++) {
        memcpy(pDst + row * footprint.Footprint.RowPitch,
               pSrc + row * stride,
               (size_t)rowSize);
    }

    ID3D12Resource_Unmap(pUploadBuffer, 0, NULL);

    /* Record copy command */
    ID3D12VideoDecodeCommandList_Reset(pDecodeCommandList, pDecodeAllocator);

    /* Transition texture to copy destination */
    D3D12_RESOURCE_BARRIER barrier;
    memset(&barrier, 0, sizeof(barrier));
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = pTexture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    ID3D12VideoDecodeCommandList_ResourceBarrier(pDecodeCommandList, 1, &barrier);

    /* Note: Texture copy requires ID3D12GraphicsCommandList, not available in video decode command list.
     * For now, we'll use a simplified approach - just copy the data to the upload buffer.
     * The actual texture upload would need a separate graphics command list. */
    /* TODO: Implement proper texture copy using ID3D12GraphicsCommandList */

    /* Transition texture back to common */
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    ID3D12VideoDecodeCommandList_ResourceBarrier(pDecodeCommandList, 1, &barrier);

    ID3D12VideoDecodeCommandList_Close(pDecodeCommandList);

    /* Execute command list */
    ID3D12CommandQueue_ExecuteCommandLists(pVideoDecodeQueue, 1, (ID3D12CommandList **)&pDecodeCommandList);

    /* Wait for completion */
    WaitForGpu();

    SAFE_RELEASE(pUploadBuffer);

    g_nextTexture++;
    return 0;
#endif
}

int d3d12_video_processor_init(void)
{
    HRESULT hr;

    if (!pD3D12Device) {
        Log_Printf(L"D3D12: Device not initialized for video processo");
        return -1;
    }

#ifdef __MINGW32__
    Log_Printf(L"D3D12: Video processor not supported in MinGW build (d3d12video.h structures may differ");
    return -1;
#else

    if (!pVideoDevice) {
        Log_Printf(L"D3D12: Video device not initialized for video processo");
        return -1;
    }

    d3d12_video_processor_cleanup();

    /* Create command allocator for process */
    hr = ID3D12Device_CreateCommandAllocator(pD3D12Device, D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS,
        &IID_ID3D12CommandAllocator, (void **)&pProcessAllocator);
    if (FAILED(hr)) {
        Log_Printf(L"D3D12: CreateCommandAllocator (process) failed: 0x%08lx", hr);
        return -1;
    }

    /* Create video process command list */
    hr = ID3D12Device_CreateCommandList(pD3D12Device, 0, D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS,
        pProcessAllocator, NULL, &IID_ID3D12VideoProcessCommandList, (void **)&pProcessCommandList);
    if (FAILED(hr)) {
        Log_Printf(L"D3D12: CreateCommandList (process) failed: 0x%08lx", hr);
        d3d12_video_processor_cleanup();
        return -1;
    }

    /* Create video processor using correct MinGW structures */
    D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC outputStreamDesc;
    memset(&outputStreamDesc, 0, sizeof(outputStreamDesc));
    outputStreamDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    outputStreamDesc.ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    outputStreamDesc.AlphaFillMode = D3D12_VIDEO_PROCESS_ALPHA_FILL_MODE_OPAQUE;
    outputStreamDesc.FrameRate.Numerator = 30;
    outputStreamDesc.FrameRate.Denominator = 1;

    D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC inputStreamDesc;
    memset(&inputStreamDesc, 0, sizeof(inputStreamDesc));
    inputStreamDesc.Format = DXGI_FORMAT_NV12;
    inputStreamDesc.ColorSpace = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
    inputStreamDesc.SourceAspectRatio.Numerator = g_width;
    inputStreamDesc.SourceAspectRatio.Denominator = g_height;
    inputStreamDesc.DestinationAspectRatio.Numerator = g_width;
    inputStreamDesc.DestinationAspectRatio.Denominator = g_height;
    inputStreamDesc.FrameRate.Numerator = 30;
    inputStreamDesc.FrameRate.Denominator = 1;
    inputStreamDesc.SourceSizeRange.MaxWidth = g_width;
    inputStreamDesc.SourceSizeRange.MaxHeight = g_height;
    inputStreamDesc.DestinationSizeRange.MaxWidth = g_width;
    inputStreamDesc.DestinationSizeRange.MaxHeight = g_height;
    inputStreamDesc.EnableAutoProcessing = TRUE;

    hr = ID3D12VideoDevice_CreateVideoProcessor(pVideoDevice, 0,
        &outputStreamDesc, 1, &inputStreamDesc,
        &IID_ID3D12VideoProcessor, (void **)&pVideoProcessor);
    if (FAILED(hr)) {
        Log_Printf(L"D3D12: CreateVideoProcessor failed: 0x%08lx", hr);
        d3d12_video_processor_cleanup();
        return -1;
    }

    Log_Printf(L"D3D12: Video processor initialized (%dx%d", g_width, g_height);
    return 0;
#endif
}

void d3d12_video_processor_cleanup(void)
{
#ifndef __MINGW32__
    SAFE_RELEASE(pProcessCommandList);
    SAFE_RELEASE(pProcessAllocator);
    SAFE_RELEASE(pVideoProcessor);
#endif
}

int d3d12_video_processor_render(void *input_texture, void *output_texture)
{
#ifdef __MINGW32__
    (void)input_texture; (void)output_texture;
    Log_Printf(L"D3D12: Video processor render not supported in MinGW buil");
    return -1;
#else

    if (!pVideoProcessor || !pProcessCommandList || !pVideoProcessQueue) {
        return -1;
    }

    ID3D12Resource *pInput = (ID3D12Resource *)input_texture;
    ID3D12Resource *pOutput = (ID3D12Resource *)output_texture;

    if (!pInput || !pOutput) return -1;

    /* Record process command */
    ID3D12VideoProcessCommandList_Reset(pProcessCommandList, pProcessAllocator);

    /* Transition input to shader resource */
    D3D12_RESOURCE_BARRIER barriers[2];
    memset(barriers, 0, sizeof(barriers));

    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = pInput;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = pOutput;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    ID3D12VideoProcessCommandList_ResourceBarrier(pProcessCommandList, 2, barriers);

    /* Process frames using correct MinGW structures */
    D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS inputArgs;
    memset(&inputArgs, 0, sizeof(inputArgs));
    inputArgs.InputStream[0].pTexture2D = pInput;
    inputArgs.InputStream[0].Subresource = 0;
    inputArgs.Transform.SourceRectangle.left = 0;
    inputArgs.Transform.SourceRectangle.top = 0;
    inputArgs.Transform.SourceRectangle.right = g_width;
    inputArgs.Transform.SourceRectangle.bottom = g_height;
    inputArgs.Transform.DestinationRectangle = inputArgs.Transform.SourceRectangle;

    D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS outputArgs;
    memset(&outputArgs, 0, sizeof(outputArgs));
    outputArgs.OutputStream[0].pTexture2D = pOutput;
    outputArgs.OutputStream[0].Subresource = 0;
    outputArgs.TargetRectangle.left = 0;
    outputArgs.TargetRectangle.top = 0;
    outputArgs.TargetRectangle.right = g_width;
    outputArgs.TargetRectangle.bottom = g_height;

    ID3D12VideoProcessCommandList_ProcessFrames(pProcessCommandList,
        pVideoProcessor, &outputArgs, 1, &inputArgs);

    /* Transition back to common */
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;

    ID3D12VideoProcessCommandList_ResourceBarrier(pProcessCommandList, 2, barriers);

    ID3D12VideoProcessCommandList_Close(pProcessCommandList);

    /* Execute command list */
    ID3D12CommandQueue_ExecuteCommandLists(pVideoProcessQueue, 1, (ID3D12CommandList **)&pProcessCommandList);

    /* Wait for completion */
    WaitForGpu();

    return 0;
#endif
}

int d3d12_video_present(void)
{
    if (!pSwapChain) return -1;

    HRESULT hr = IDXGISwapChain3_Present(pSwapChain, 1, 0);
    if (FAILED(hr)) {
        Log_Printf(L"D3D12: Present failed: 0x%08lx", hr);
        return -1;
    }

    g_frameIndex = IDXGISwapChain3_GetCurrentBackBufferIndex(pSwapChain);
    return 0;
}

int d3d12_video_is_initialized(void)
{
    return (pD3D12Device != NULL) ? 1 : 0;
}
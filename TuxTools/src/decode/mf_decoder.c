/*
 * Media Foundation Video Decoder (IMFSourceReader)
 * Proper frame timing, efficient rendering, A/V sync.
 */
#include "mf_decoder.h"
#include "dxva2_helper.h"
#include "d3d11_video_helper.h"
#include "d3d12_video_helper.h"
#include "log.h"

#include <windows.h>
#include <mmsystem.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfobjects.h>
#include <d3d9.h>
#include <dxva2api.h>
#include <d3d11.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* D3DFMT_NV12 may not be available in MinGW */
#ifndef D3DFMT_NV12
#define D3DFMT_NV12 ((D3DFORMAT)MAKEFOURCC('N','V','1','2'))
#endif

static IMFSourceReader     *g_reader     = NULL;  /* Video reader (main thread only) */
static IMFSourceReader     *g_audioReader = NULL; /* Audio reader (audio thread only) */
static HWND                 g_hwnd       = NULL;
static int                  g_active     = 0;
static int                  g_dxva2      = 0;  /* 0=software, 1=DXVA2, 2=D3D11 */
static int                  g_eof        = 0;
static int                  g_width      = 0;
static int                  g_height     = 0;
static int                  g_stride     = 0;
static long long            g_duration   = 0;
static GUID                 g_subtype    = {0};

/* DXVA2 hardware acceleration state */
static int                  g_dxva2_initialized = 0;
static IDirect3DSurface9   *g_dxva2_surface     = NULL;
static int                  g_dxva2_use_hw_render = 0;

/* D3D11 hardware acceleration state */
static int                  g_d3d11_initialized = 0;
static ID3D11Texture2D     *g_d3d11_texture     = NULL;
static int                  g_d3d11_use_hw_render = 0;

/* D3D12 hardware acceleration state */
static int                  g_d3d12_initialized = 0;
static void                *g_d3d12_texture     = NULL;
static int                  g_d3d12_use_hw_render = 0;

/* Media info */
static wchar_t  g_videoCodec[64]  = {0};
static wchar_t  g_audioCodec[64]  = {0};
static UINT32   g_videoBitrate    = 0;
static UINT32   g_audioBitrate    = 0;
static UINT32   g_videoFPS_Num    = 0;
static UINT32   g_videoFPS_Den    = 0;
static int      g_hasVideo        = 0;
static int      g_hasAudio        = 0;

/* Frame timing */
static LONGLONG             g_startTime  = 0;  /* QPC start time */
static LONGLONG             g_freq       = 0;  /* QPC frequency */
static LONGLONG             g_firstPts   = 0;  /* First frame PTS */
static LONGLONG             g_currentPts = 0;  /* Current frame PTS */
static int                  g_frameCount = 0;
static int                  g_droppedFrames = 0;

/* Audio playback */
static HWAVEOUT            g_hWaveOut    = NULL;
#define AUDIO_BUFFERS 16
#define AUDIO_BUFFER_SIZE 65536
static WAVEHDR             g_audioHdr[AUDIO_BUFFERS];
static BYTE               *g_audioBuf[AUDIO_BUFFERS] = {0};
static int                 g_audioBufIdx = 0;
static HANDLE              g_hAudioThread = NULL;
static volatile int        g_audioThreadStop = 0;

/* Precomputed YUV→RGB tables (BT.601) */
static int g_ytable[256];   /* 1.164 * (Y - 16) */
static int g_rvtable[256];  /* 1.596 * (Cr - 128) */
static int g_gutable[256];  /* 0.391 * (Cb - 128) */
static int g_gvtable[256];  /* 0.813 * (Cr - 128) */
static int g_butable[256];  /* 2.018 * (Cb - 128) */
static int g_tables_init = 0;

static void init_yuv_tables(void)
{
    int i;
    if (g_tables_init) return;
    for (i = 0; i < 256; i++) {
        g_ytable[i]   = (int)((i - 16) * 1.164);
        g_rvtable[i]  = (int)((i - 128) * 1.596);
        g_gutable[i]  = (int)((i - 128) * 0.391);
        g_gvtable[i]  = (int)((i - 128) * 0.813);
        g_butable[i]  = (int)((i - 128) * 2.018);
    }
    g_tables_init = 1;
}

static __inline unsigned char clamp255(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (unsigned char)v;
}

/* Convert media subtype GUID to readable name */
static void mf_subtype_to_name(const GUID *subtype, wchar_t *name, int name_len)
{
    /* Video formats */
    if (IsEqualGUID(subtype, &MFVideoFormat_NV12))       { wcscpy_s(name, name_len, L"NV12"); return; }
    if (IsEqualGUID(subtype, &MFVideoFormat_YUY2))       { wcscpy_s(name, name_len, L"YUY2"); return; }
    if (IsEqualGUID(subtype, &MFVideoFormat_RGB32))      { wcscpy_s(name, name_len, L"RGB32"); return; }
    if (IsEqualGUID(subtype, &MFVideoFormat_ARGB32))     { wcscpy_s(name, name_len, L"ARGB32"); return; }
    if (IsEqualGUID(subtype, &MFVideoFormat_H264))       { wcscpy_s(name, name_len, L"H.264/AVC"); return; }
    if (IsEqualGUID(subtype, &MFVideoFormat_H265))       { wcscpy_s(name, name_len, L"H.265/HEVC"); return; }
    if (IsEqualGUID(subtype, &MFVideoFormat_MJPG))       { wcscpy_s(name, name_len, L"MJPEG"); return; }
    if (IsEqualGUID(subtype, &MFVideoFormat_MPEG2))      { wcscpy_s(name, name_len, L"MPEG-2"); return; }
    if (IsEqualGUID(subtype, &MFVideoFormat_VP80))       { wcscpy_s(name, name_len, L"VP8"); return; }
    if (IsEqualGUID(subtype, &MFVideoFormat_VP90))       { wcscpy_s(name, name_len, L"VP9"); return; }
    if (IsEqualGUID(subtype, &MFVideoFormat_AV1))        { wcscpy_s(name, name_len, L"AV1"); return; }
    if (IsEqualGUID(subtype, &MFVideoFormat_M4S2))       { wcscpy_s(name, name_len, L"MPEG-4"); return; }
    if (IsEqualGUID(subtype, &MFVideoFormat_WMV3))       { wcscpy_s(name, name_len, L"WMV3"); return; }
    if (IsEqualGUID(subtype, &MFVideoFormat_WVC1))       { wcscpy_s(name, name_len, L"VC-1"); return; }
    /* Audio formats */
    if (IsEqualGUID(subtype, &MFAudioFormat_PCM))        { wcscpy_s(name, name_len, L"PCM"); return; }
    if (IsEqualGUID(subtype, &MFAudioFormat_Float))      { wcscpy_s(name, name_len, L"Float PCM"); return; }
    if (IsEqualGUID(subtype, &MFAudioFormat_AAC))        { wcscpy_s(name, name_len, L"AAC"); return; }
    if (IsEqualGUID(subtype, &MFAudioFormat_MP3))        { wcscpy_s(name, name_len, L"MP3"); return; }
    if (IsEqualGUID(subtype, &MFAudioFormat_WMAudioV8))  { wcscpy_s(name, name_len, L"WMA8"); return; }
    if (IsEqualGUID(subtype, &MFAudioFormat_WMAudioV9))  { wcscpy_s(name, name_len, L"WMA9"); return; }
    if (IsEqualGUID(subtype, &MFAudioFormat_Dolby_AC3))  { wcscpy_s(name, name_len, L"AC3"); return; }
    if (IsEqualGUID(subtype, &MFAudioFormat_Dolby_DDPlus)) { wcscpy_s(name, name_len, L"E-AC3"); return; }
    if (IsEqualGUID(subtype, &MFAudioFormat_Vorbis))     { wcscpy_s(name, name_len, L"Vorbis"); return; }
    /* FLAC/Opus/DTS GUIDs require WINVER >= 0x0A00, define manually if needed */
#ifndef MFAudioFormat_FLAC
    { static const GUID MFAudioFormat_FLAC_local = {0x0000f1ac, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
      if (IsEqualGUID(subtype, &MFAudioFormat_FLAC_local)) { wcscpy_s(name, name_len, L"FLAC"); return; } }
#else
    if (IsEqualGUID(subtype, &MFAudioFormat_FLAC))       { wcscpy_s(name, name_len, L"FLAC"); return; }
#endif
#ifndef MFAudioFormat_Opus
    { static const GUID MFAudioFormat_Opus_local = {0x0000704f, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
      if (IsEqualGUID(subtype, &MFAudioFormat_Opus_local)) { wcscpy_s(name, name_len, L"Opus"); return; } }
#else
    if (IsEqualGUID(subtype, &MFAudioFormat_Opus))       { wcscpy_s(name, name_len, L"Opus"); return; }
#endif
    if (IsEqualGUID(subtype, &MFAudioFormat_DTS))        { wcscpy_s(name, name_len, L"DTS"); return; }
    /* Fallback: unknown codec */
    wcscpy_s(name, name_len, L"未知编码");
}

static void mf_cleanup_internals(void)
{
    int i;

    /* Signal audio thread to stop first, then set g_active=0 so the thread
     * can exit even if it's blocked in ReadSample (it checks g_active in the loop). */
    if (g_hAudioThread) {
        g_audioThreadStop = 1;
        g_active = 0;  /* Let audio thread loop condition fail */
        WaitForSingleObject(g_hAudioThread, 500);
        CloseHandle(g_hAudioThread);
        g_hAudioThread = NULL;
    }

    if (g_audioReader) { IMFSourceReader_Release(g_audioReader); g_audioReader = NULL; }
    if (g_reader)    { IMFSourceReader_Release(g_reader); g_reader = NULL; }

    if (g_hWaveOut) {
        waveOutReset(g_hWaveOut);
        for (i = 0; i < AUDIO_BUFFERS; i++) {
            if (g_audioBuf[i]) {
                if (g_audioHdr[i].dwFlags & WHDR_PREPARED)
                    waveOutUnprepareHeader(g_hWaveOut, &g_audioHdr[i], sizeof(WAVEHDR));
                free(g_audioBuf[i]);
                g_audioBuf[i] = NULL;
            }
        }
        waveOutClose(g_hWaveOut);
        g_hWaveOut = NULL;
    }

    g_audioBufIdx = 0;
    g_audioThreadStop = 0;

    /* Cleanup DXVA2 resources */
    if (g_dxva2_surface) {
        IDirect3DSurface9_Release(g_dxva2_surface);
        g_dxva2_surface = NULL;
    }
    if (g_dxva2_initialized) {
        dxva2_decoder_cleanup();
        dxva2_processor_cleanup();
        g_dxva2_initialized = 0;
    }
    g_dxva2_use_hw_render = 0;

    /* Cleanup D3D11 resources */
    if (g_d3d11_texture) {
        g_d3d11_texture->lpVtbl->Release(g_d3d11_texture);
        g_d3d11_texture = NULL;
    }
    if (g_d3d11_initialized) {
        d3d11_video_decoder_cleanup();
        d3d11_video_processor_cleanup();
        g_d3d11_initialized = 0;
    }
    g_d3d11_use_hw_render = 0;

    g_d3d12_texture = NULL;
    g_d3d12_initialized = 0;
    g_d3d12_use_hw_render = 0;

    /* Clear display area to prevent residual frames */
    if (g_hwnd) {
        HDC hdc = GetDC(g_hwnd);
        if (hdc) {
            RECT rc;
            GetClientRect(g_hwnd, &rc);
            FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
            ReleaseDC(g_hwnd, hdc);
        }
        RedrawWindow(g_hwnd, NULL, NULL,
            RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        g_hwnd = NULL;
    }

    g_active = 0;
    g_eof    = 0;
    g_dxva2  = 0;
    g_width  = 0;
    g_height = 0;
    g_stride = 0;
    g_startTime = 0;
    g_firstPts  = 0;
    g_currentPts = 0;
    g_frameCount = 0;
    g_droppedFrames = 0;

    /* Clear media info */
    memset(g_videoCodec, 0, sizeof(g_videoCodec));
    memset(g_audioCodec, 0, sizeof(g_audioCodec));
    g_videoBitrate = 0;
    g_audioBitrate = 0;
    g_videoFPS_Num = 0;
    g_videoFPS_Den = 0;
    g_hasVideo = 0;
    g_hasAudio = 0;
}

static int mf_init_audio(int channels, int sample_rate, int bits_per_sample)
{
    WAVEFORMATEX wfx;
    MMRESULT mr;
    int i;

    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = (WORD)channels;
    wfx.nSamplesPerSec  = (DWORD)sample_rate;
    wfx.wBitsPerSample  = (WORD)bits_per_sample;
    wfx.nBlockAlign     = (WORD)(channels * (bits_per_sample / 8));
    wfx.nAvgBytesPerSec = (DWORD)(sample_rate * wfx.nBlockAlign);

    mr = waveOutOpen(&g_hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    if (mr != MMSYSERR_NOERROR) {
        Log_Printf(L"MF Audio: waveOutOpen failed: %d", (int)mr);
        return -1;
    }

    waveOutSetVolume(g_hWaveOut, 0xFFFFFFFF);

    for (i = 0; i < AUDIO_BUFFERS; i++) {
        g_audioBuf[i] = (BYTE *)malloc(AUDIO_BUFFER_SIZE);
        ZeroMemory(&g_audioHdr[i], sizeof(WAVEHDR));
        g_audioHdr[i].lpData = (LPSTR)g_audioBuf[i];
        g_audioHdr[i].dwBufferLength = AUDIO_BUFFER_SIZE;
    }

    g_audioBufIdx = 0;
    g_audioThreadStop = 0;

    Log_Printf(L"MF Audio: %dHz %dch %dbit, %d buffers x %d bytes",
            sample_rate, channels, bits_per_sample, AUDIO_BUFFERS, AUDIO_BUFFER_SIZE);
    return 0;
}

/* Audio thread: continuously reads audio from source and feeds waveOut.
 * Uses a dedicated g_audioReader (separate from the video reader) so there
 * is no thread-safety issue with the main thread's ReadSample calls. */
static DWORD WINAPI mf_audio_thread(LPVOID param)
{
    (void)param;
    Log_Printf(L"MF Audio: thread started");

    while (!g_audioThreadStop && g_audioReader && g_active) {
        IMFMediaBuffer *abuf = NULL;
        IMFSample      *asample = NULL;
        BYTE           *adata = NULL;
        DWORD           aflags = 0;
        LONGLONG        ats = 0;
        DWORD           astream_idx;
        DWORD           abuf_len = 0;
        HRESULT         hr;
        WAVEHDR        *hdr;

        /* Check if we have a free buffer */
        hdr = &g_audioHdr[g_audioBufIdx];
        if (hdr->dwFlags & WHDR_INQUEUE) {
            /* All buffers busy, wait a bit */
            Sleep(5);
            continue;
        }

        /* Read audio sample from dedicated audio reader */
        hr = IMFSourceReader_ReadSample(g_audioReader,
            (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            0, &astream_idx, &aflags, &ats, &asample);

        if (FAILED(hr) || !asample || (aflags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            if (asample) IMFSample_Release(asample);
            break;
        }

        hr = IMFSample_ConvertToContiguousBuffer(asample, &abuf);
        if (SUCCEEDED(hr)) {
            hr = IMFMediaBuffer_Lock(abuf, &adata, &abuf_len, NULL);
            if (SUCCEEDED(hr)) {
                /* Submit audio data to waveOut */
                if (hdr->dwFlags & WHDR_PREPARED)
                    waveOutUnprepareHeader(g_hWaveOut, hdr, sizeof(WAVEHDR));

                /* Copy in chunks if needed */
                DWORD offset = 0;
                while (offset < abuf_len && !g_audioThreadStop) {
                    DWORD chunk = abuf_len - offset;
                    if (chunk > AUDIO_BUFFER_SIZE) chunk = AUDIO_BUFFER_SIZE;

                    hdr = &g_audioHdr[g_audioBufIdx];
                    /* Wait for this specific buffer to become free */
                    while ((hdr->dwFlags & WHDR_INQUEUE) && !g_audioThreadStop) {
                        Sleep(2);
                    }
                    if (g_audioThreadStop) break;

                    if (hdr->dwFlags & WHDR_PREPARED)
                        waveOutUnprepareHeader(g_hWaveOut, hdr, sizeof(WAVEHDR));

                    memcpy(g_audioBuf[g_audioBufIdx], adata + offset, chunk);
                    hdr->dwBufferLength = chunk;
                    hdr->dwFlags = 0;

                    if (waveOutPrepareHeader(g_hWaveOut, hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR)
                        waveOutWrite(g_hWaveOut, hdr, sizeof(WAVEHDR));

                    g_audioBufIdx = (g_audioBufIdx + 1) % AUDIO_BUFFERS;
                    offset += chunk;
                }

                IMFMediaBuffer_Unlock(abuf);
            }
            IMFMediaBuffer_Release(abuf);
        }
        IMFSample_Release(asample);
    }

    Log_Printf(L"MF Audio: thread exite");
    return 0;
}

static int mf_get_stride(IMFMediaType *type, int width)
{
    UINT32 stride = 0;
    GUID subtype;

    if (SUCCEEDED(IMFMediaType_GetUINT32(type, &MF_MT_DEFAULT_STRIDE, &stride)) && (int)stride != 0)
        return (int)stride;

    if (SUCCEEDED(IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &subtype))) {
        if (IsEqualGUID(&subtype, &MFVideoFormat_NV12)) return width;
        if (IsEqualGUID(&subtype, &MFVideoFormat_YUY2)) return width * 2;
        if (IsEqualGUID(&subtype, &MFVideoFormat_RGB32)) return width * 4;
    }
    return width;
}

int mf_open(const wchar_t *filepath, HWND hwnd_display, int enable_dxva2)
{
    HRESULT hr;
    IMFAttributes *attrs = NULL;
    IMFMediaType  *vtype = NULL;
    IMFMediaType  *atype = NULL;
    PROPVARIANT var;
    UINT32 channels = 0, samplerate = 0, bitspersample = 0;
    LARGE_INTEGER freq;

    mf_cleanup_internals();
    init_yuv_tables();

    /* Get QPC frequency */
    QueryPerformanceFrequency(&freq);
    g_freq = freq.QuadPart;

    hr = MFStartup(MF_VERSION, 0);
    if (FAILED(hr)) { Log_Printf(L"MF: MFStartup failed: 0x%08lx", hr); return -1; }

    g_hwnd  = hwnd_display;
    g_dxva2 = enable_dxva2;

    hr = MFCreateAttributes(&attrs, 2);
    if (FAILED(hr)) { MFShutdown(); return -1; }

    IMFAttributes_SetUINT32(attrs, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);

    hr = MFCreateSourceReaderFromURL(filepath, attrs, &g_reader);
    if (attrs) IMFAttributes_Release(attrs);
    if (FAILED(hr)) { Log_Printf(L"MF: MFCreateSourceReaderFromURL failed: 0x%08lx", hr); MFShutdown(); return -1; }

    /* Configure video: try NV12, then YUY2, then RGB32 */
    hr = MFCreateMediaType(&vtype);
    if (SUCCEEDED(hr)) {
        IMFMediaType_SetGUID(vtype, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
        IMFMediaType_SetGUID(vtype, &MF_MT_SUBTYPE, &MFVideoFormat_NV12);
        hr = IMFSourceReader_SetCurrentMediaType(g_reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, vtype);
        IMFMediaType_Release(vtype); vtype = NULL;
    }
    if (FAILED(hr)) {
        hr = MFCreateMediaType(&vtype);
        if (SUCCEEDED(hr)) {
            IMFMediaType_SetGUID(vtype, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
            IMFMediaType_SetGUID(vtype, &MF_MT_SUBTYPE, &MFVideoFormat_YUY2);
            hr = IMFSourceReader_SetCurrentMediaType(g_reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, vtype);
            IMFMediaType_Release(vtype); vtype = NULL;
        }
    }
    if (FAILED(hr)) {
        hr = MFCreateMediaType(&vtype);
        if (SUCCEEDED(hr)) {
            IMFMediaType_SetGUID(vtype, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
            IMFMediaType_SetGUID(vtype, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
            hr = IMFSourceReader_SetCurrentMediaType(g_reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, vtype);
            IMFMediaType_Release(vtype); vtype = NULL;
        }
    }

    /* Get video output info (for dimensions) */
    hr = IMFSourceReader_GetCurrentMediaType(g_reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &vtype);
    if (SUCCEEDED(hr)) {
        UINT64 frameSize = 0;
        IMFMediaType_GetUINT64(vtype, &MF_MT_FRAME_SIZE, &frameSize);
        g_width  = (int)(frameSize >> 32);
        g_height = (int)(frameSize & 0xFFFFFFFF);
        IMFMediaType_GetGUID(vtype, &MF_MT_SUBTYPE, &g_subtype);
        g_stride = mf_get_stride(vtype, g_width);
        g_hasVideo = 1;
        IMFMediaType_Release(vtype); vtype = NULL;
    }

    /* Get native video media type (original codec before decode) */
    if (g_hasVideo) {
        hr = IMFSourceReader_GetNativeMediaType(g_reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &vtype);
        if (SUCCEEDED(hr)) {
            GUID nativeSubtype = {0};
            UINT32 bitrate = 0;
            UINT64 fps = 0;
            IMFMediaType_GetGUID(vtype, &MF_MT_SUBTYPE, &nativeSubtype);
            /* Video bitrate */
            if (SUCCEEDED(IMFMediaType_GetUINT32(vtype, &MF_MT_AVG_BITRATE, &bitrate)))
                g_videoBitrate = bitrate;
            /* Frame rate */
            if (SUCCEEDED(IMFMediaType_GetUINT64(vtype, &MF_MT_FRAME_RATE, &fps))) {
                g_videoFPS_Num = (UINT32)(fps >> 32);
                g_videoFPS_Den = (UINT32)(fps & 0xFFFFFFFF);
            }
            /* Video codec name from native subtype */
            mf_subtype_to_name(&nativeSubtype, g_videoCodec, 64);
            IMFMediaType_Release(vtype); vtype = NULL;
            Log_Printf(L"MF: Video %dx%d codec=%ls bitrate=%u fps=%u/%", g_width, g_height, g_videoCodec, g_videoBitrate, g_videoFPS_Num, g_videoFPS_Den);
        }
    }

    /* Initialize hardware acceleration if requested */
    if (enable_dxva2 == 1 && g_width > 0 && g_height > 0) {
        /* DXVA2 mode */
#ifdef __MINGW32__
        Log_Printf(L"MF: Initializing DXVA2 hardware acceleration (MinGW build)..");
#else
        Log_Printf(L"MF: Initializing DXVA2 hardware acceleration..");
#endif

        /* Initialize DXVA2 device */
        if (dxva2_init(hwnd_display, g_width, g_height) != NULL) {
            /* Configure DXVA2 decoder */
            DXVA2DecoderConfig decoder_config;
            ZeroMemory(&decoder_config, sizeof(decoder_config));

            /* H.264 decoder GUID */
            decoder_config.guid = (GUID){0x1b81be68, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}};
            decoder_config.width = g_width;
            decoder_config.height = g_height;
            decoder_config.num_surfaces = 8;

            if (dxva2_decoder_init(&decoder_config) == 0) {
                /* Initialize video processor for rendering */
                if (dxva2_processor_init(DXVA2_VideoFormat_NV12, DXVA2_VideoFormat_RGB32) == 0) {
                    g_dxva2_initialized = 1;
                    g_dxva2_use_hw_render = 1;
                    Log_Printf(L"MF: DXVA2 hardware acceleration enable");
                } else {
                    Log_Printf(L"MF: DXVA2 processor init failed, falling back to softwar");
                    dxva2_decoder_cleanup();
                }
            } else {
                Log_Printf(L"MF: DXVA2 decoder init failed, falling back to softwar");
            }
        } else {
            Log_Printf(L"MF: DXVA2 device init failed, falling back to softwar");
        }
    } else if (enable_dxva2 == 2 && g_width > 0 && g_height > 0) {
        /* D3D11 mode */
#ifdef __MINGW32__
        Log_Printf(L"MF: Initializing D3D11 hardware acceleration (MinGW build, may have limited support)..");
#else
        Log_Printf(L"MF: Initializing D3D11 hardware acceleration..");
#endif

        /* Check if D3D11 device is already initialized */
        if (!d3d11_video_is_initialized()) {
            /* Initialize D3D11 device */
            if (d3d11_video_init(hwnd_display, g_width, g_height) != 0) {
                Log_Printf(L"MF: D3D11 device init failed, falling back to softwar");
                goto d3d11_failed;
            }
        } else {
            Log_Printf(L"MF: D3D11 device already initialized, reusin");
        }

        /* Configure D3D11 decoder */
        D3D11VideoDecoderConfig decoder_config;
        ZeroMemory(&decoder_config, sizeof(decoder_config));

        /* H.264 decoder profile */
        decoder_config.guid = (GUID){0x1b81be68, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}};
        decoder_config.width = g_width;
        decoder_config.height = g_height;
        decoder_config.num_surfaces = 8;

        if (d3d11_video_decoder_init(&decoder_config) == 0) {
            /* Initialize video processor for rendering */
            if (d3d11_video_processor_init() == 0) {
                g_d3d11_initialized = 1;
                g_d3d11_use_hw_render = 1;
                Log_Printf(L"MF: D3D11 hardware acceleration enable");
            } else {
                Log_Printf(L"MF: D3D11 processor init failed, falling back to softwar");
                d3d11_video_decoder_cleanup();
            }
        } else {
            Log_Printf(L"MF: D3D11 decoder init failed, falling back to softwar");
        }
        d3d11_failed:;
    } else if (enable_dxva2 == 3 && g_width > 0 && g_height > 0) {
        /* D3D12 mode */
#ifdef __MINGW32__
        Log_Printf(L"MF: D3D12 mode (MinGW build, software decoding only - D3D12 video API not supported)");
        /* MinGW: skip D3D12 initialization, use software rendering */
        (void)hwnd_display;
#else
        Log_Printf(L"MF: Initializing D3D12 hardware acceleration..");

        /* Check if D3D12 device is already initialized */
        if (!d3d12_video_is_initialized()) {
            /* Initialize D3D12 device */
            if (d3d12_video_init(hwnd_display, g_width, g_height) != 0) {
                Log_Printf(L"MF: D3D12 device init failed, falling back to software decoding");
            } else {
                /* Initialize video processor for rendering */
                if (d3d12_video_processor_init() == 0) {
                    g_d3d12_initialized = 1;
                    g_d3d12_use_hw_render = 1;
                    Log_Printf(L"MF: D3D12 hardware acceleration enabled");
                } else {
                    Log_Printf(L"MF: D3D12 processor init failed, falling back to software decoding");
                    d3d12_video_cleanup();
                }
            }
        } else {
            Log_Printf(L"MF: D3D12 device already initialized, reusing");
            g_d3d12_initialized = 1;
            g_d3d12_use_hw_render = 1;
        }
#endif
    }

    /* Get native audio info before converting to PCM */
    hr = IMFSourceReader_GetNativeMediaType(g_reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &atype);
    if (SUCCEEDED(hr)) {
        GUID audioSubtype;
        UINT32 abitrate = 0;
        g_hasAudio = 1;
        if (SUCCEEDED(IMFMediaType_GetGUID(atype, &MF_MT_SUBTYPE, &audioSubtype)))
            mf_subtype_to_name(&audioSubtype, g_audioCodec, 64);
        if (SUCCEEDED(IMFMediaType_GetUINT32(atype, &MF_MT_AVG_BITRATE, &abitrate)))
            g_audioBitrate = abitrate;
        Log_Printf(L"MF: Audio codec=%ls bitrate=%u", g_audioCodec, g_audioBitrate);
        IMFMediaType_Release(atype); atype = NULL;
    }

    /* Create a SEPARATE source reader for audio.
     * IMFSourceReader is not thread-safe, so the audio thread needs its own
     * reader instance to avoid data races with the video thread. */
    if (g_hasAudio) {
        IMFAttributes *aattrs = NULL;
        hr = MFCreateAttributes(&aattrs, 2);
        if (SUCCEEDED(hr)) {
            IMFAttributes_SetUINT32(aattrs, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
            hr = MFCreateSourceReaderFromURL(filepath, aattrs, &g_audioReader);
            IMFAttributes_Release(aattrs);
        }
        if (FAILED(hr)) {
            Log_Printf(L"MF: Failed to create audio reader: 0x%08lx", hr);
            g_audioReader = NULL;
        }
    }

    /* Configure audio output to 16-bit PCM on the audio reader.
     * AAC decoders output float by default; requesting 16-bit forces
     * the source reader to insert a float→PCM16 conversion MFT. */
    if (g_audioReader) {
        hr = MFCreateMediaType(&atype);
        if (SUCCEEDED(hr)) {
            IMFMediaType_SetGUID(atype, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
            IMFMediaType_SetGUID(atype, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
            IMFMediaType_SetUINT32(atype, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            hr = IMFSourceReader_SetCurrentMediaType(g_audioReader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, atype);
            IMFMediaType_Release(atype); atype = NULL;
        }
        if (FAILED(hr)) {
            /* Fallback without explicit bit depth */
            hr = MFCreateMediaType(&atype);
            if (SUCCEEDED(hr)) {
                IMFMediaType_SetGUID(atype, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
                IMFMediaType_SetGUID(atype, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
                hr = IMFSourceReader_SetCurrentMediaType(g_audioReader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, atype);
                IMFMediaType_Release(atype); atype = NULL;
            }
        }
        hr = IMFSourceReader_GetCurrentMediaType(g_audioReader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &atype);
        if (SUCCEEDED(hr)) {
            IMFMediaType_GetUINT32(atype, &MF_MT_AUDIO_NUM_CHANNELS, &channels);
            IMFMediaType_GetUINT32(atype, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &samplerate);
            IMFMediaType_GetUINT32(atype, &MF_MT_AUDIO_BITS_PER_SAMPLE, &bitspersample);
            IMFMediaType_Release(atype); atype = NULL;
            if (channels > 0 && samplerate > 0 && bitspersample > 0)
                mf_init_audio(channels, samplerate, bitspersample);
        }
    }

    /* Disable audio on the video reader - we use g_audioReader for audio */
    IMFSourceReader_SetStreamSelection(g_reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, FALSE);

    /* Get duration */
    PropVariantInit(&var);
    hr = IMFSourceReader_GetPresentationAttribute(g_reader, (DWORD)MF_SOURCE_READER_MEDIASOURCE, &MF_PD_DURATION, &var);
    if (SUCCEEDED(hr) && var.vt == VT_UI8) g_duration = (long long)var.uhVal.QuadPart;
    PropVariantClear(&var);

    /* Initialize start time */
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        g_startTime = now.QuadPart;
        g_firstPts  = -1;
    }

    g_active = 1;
    g_eof    = 0;

    /* Start audio thread */
    if (g_hWaveOut) {
        g_hAudioThread = CreateThread(NULL, 0, mf_audio_thread, NULL, 0, NULL);
        if (!g_hAudioThread) {
            Log_Printf(L"MF: Failed to create audio threa");
        }
    }

    return 0;
}

/* Convert NV12 to 32-bit RGB in a destination buffer (pre-allocated) */
static void nv12_to_rgb32(BYTE *dst, int dst_stride, BYTE *src, int src_stride,
                          int width, int height, int draw_w, int draw_h)
{
    int dy, dx;
    BYTE *y_plane = src;
    BYTE *uv_plane = src + src_stride * height;

    for (dy = 0; dy < draw_h; dy++) {
        int src_y = dy * height / draw_h;
        if (src_y >= height) src_y = height - 1;
        BYTE *dst_row = dst + dy * dst_stride;

        for (dx = 0; dx < draw_w; dx++) {
            int src_x = dx * width / draw_w;
            int y_val, cb, cr;
            int r, g, b;
            unsigned char *pixel;

            if (src_x >= width) src_x = width - 1;

            y_val = g_ytable[y_plane[src_y * src_stride + src_x]];

            {
                int uv_y = (src_y >> 1) * src_stride;
                int uv_x = (src_x >> 1) << 1;
                cb = uv_plane[uv_y + uv_x];
                cr = uv_plane[uv_y + uv_x + 1];
            }

            r = y_val + g_rvtable[cr];
            g = y_val - g_gutable[cb] - g_gvtable[cr];
            b = y_val + g_butable[cb];

            pixel = &dst_row[dx * 4];
            pixel[0] = clamp255(b);
            pixel[1] = clamp255(g);
            pixel[2] = clamp255(r);
            pixel[3] = 255;
        }
    }
}

/* Convert YUY2 to 32-bit RGB */
static void yuy2_to_rgb32(BYTE *dst, int dst_stride, BYTE *src, int src_stride,
                          int width, int height, int draw_w, int draw_h)
{
    int dy, dx;
    for (dy = 0; dy < draw_h; dy++) {
        int src_y = dy * height / draw_h;
        if (src_y >= height) src_y = height - 1;
        BYTE *dst_row = dst + dy * dst_stride;

        for (dx = 0; dx < draw_w; dx++) {
            int src_x = dx * width / draw_w;
            int y_val, cb, cr;
            int r, g, b;
            unsigned char *pixel;
            if (src_x >= width) src_x = width - 1;

            {
                BYTE *s = src + src_y * src_stride + (src_x >> 1) * 4;
                y_val = (src_x & 1) ? g_ytable[s[2]] : g_ytable[s[0]];
                cb = s[1];
                cr = s[3];
            }
            r = y_val + g_rvtable[cr];
            g = y_val - g_gutable[cb] - g_gvtable[cr];
            b = y_val + g_butable[cb];

            pixel = &dst_row[dx * 4];
            pixel[0] = clamp255(b);
            pixel[1] = clamp255(g);
            pixel[2] = clamp255(r);
            pixel[3] = 255;
        }
    }
}

/* Render frame with double buffering */
static void mf_render_frame(BYTE *data, int stride, int width, int height, HWND hwnd)
{
    HDC hdc, memDC;
    HBITMAP memBM, oldBM;
    RECT rc;
    int display_w, display_h;
    int draw_x, draw_y, draw_w, draw_h;
    float scale_x, scale_y, scale;
    BITMAPINFO bi;
    void *bits = NULL;

    if (!hwnd) { Log_Printf(L"MF Render: hwnd is NULL"); return; }

    hdc = GetDC(hwnd);
    if (!hdc) { Log_Printf(L"MF Render: GetDC failed"); return; }

    GetClientRect(hwnd, &rc);
    display_w = rc.right;
    display_h = rc.bottom;
    if (display_w <= 0 || display_h <= 0) { ReleaseDC(hwnd, hdc); Log_Printf(L"MF Render: display size %dx%d", display_w, display_h); return; }

    /* Aspect ratio */
    scale_x = (float)display_w / (float)width;
    scale_y = (float)display_h / (float)height;
    scale = (scale_x < scale_y) ? scale_x : scale_y;
    draw_w = (int)(width * scale);
    draw_h = (int)(height * scale);
    if (draw_w < 1) draw_w = 1;
    if (draw_h < 1) draw_h = 1;
    draw_x = (display_w - draw_w) / 2;
    draw_y = (display_h - draw_h) / 2;

    /* Create DIB */
    memDC = CreateCompatibleDC(hdc);
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = draw_w;
    bi.bmiHeader.biHeight      = -draw_h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    memBM = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!memBM || !bits) { DeleteDC(memDC); ReleaseDC(hwnd, hdc); return; }
    oldBM = (HBITMAP)SelectObject(memDC, memBM);

    /* Fill black */
    {
        RECT full = {0, 0, draw_w, draw_h};
        HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(memDC, &full, brush);
        DeleteObject(brush);
    }

    /* Convert to RGB */
    if (IsEqualGUID(&g_subtype, &MFVideoFormat_NV12)) {
        nv12_to_rgb32((BYTE *)bits, draw_w * 4, data, stride, width, height, draw_w, draw_h);
    } else if (IsEqualGUID(&g_subtype, &MFVideoFormat_YUY2)) {
        yuy2_to_rgb32((BYTE *)bits, draw_w * 4, data, stride, width, height, draw_w, draw_h);
    } else if (IsEqualGUID(&g_subtype, &MFVideoFormat_RGB32) ||
               IsEqualGUID(&g_subtype, &MFVideoFormat_ARGB32)) {
        /* Direct copy with scaling */
        int y;
        for (y = 0; y < draw_h; y++) {
            int src_y = y * height / draw_h;
            if (src_y >= height) src_y = height - 1;
            BYTE *dst_row = (BYTE *)bits + y * draw_w * 4;
            BYTE *src_row = data + src_y * stride;
            int x;
            for (x = 0; x < draw_w; x++) {
                int src_x = x * width / draw_w;
                if (src_x >= width) src_x = width - 1;
                memcpy(dst_row + x * 4, src_row + src_x * 4, 4);
            }
        }
    }

    BitBlt(hdc, draw_x, draw_y, draw_w, draw_h, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBM);
    DeleteObject(memBM);
    DeleteDC(memDC);
    ReleaseDC(hwnd, hdc);
}

int mf_render_next_frame(void)
{
    IMFMediaBuffer *buffer = NULL;
    IMFSample      *sample = NULL;
    BYTE           *data   = NULL;
    DWORD           flags  = 0;
    LONGLONG        ts     = 0;
    HRESULT         hr;
    DWORD           stream_idx;
    DWORD           buf_len = 0;
    LONGLONG        pts_relative;
    LONGLONG        wait_ns;
    int             drop_count = 0;
    const int       MAX_DROP = 10;
    LARGE_INTEGER   now;
    LONGLONG        elapsed_100ns;

    if (!g_reader || !g_active) return -1;

    /* For audio-only files (no video stream), just return 0 to keep playing */
    if (g_width == 0 || g_height == 0) {
        static int logged_no_video = 0;
        if (!logged_no_video) {
            Log_Printf(L"MF: No video (width=%d height=%d), audio-only mode", g_width, g_height);
            logged_no_video = 1;
        }
        return 0;
    }

    /* Read video sample with drop-late-frame loop */
    for (;;) {
        hr = IMFSourceReader_ReadSample(g_reader,
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0, &stream_idx, &flags, &ts, &sample);

        if (FAILED(hr)) { Log_Printf(L"MF: ReadSample failed: 0x%08lx", hr); return -1; }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            g_eof = 1;
            Log_Printf(L"MF: EOF, rendered %d frames, dropped %", g_frameCount, g_droppedFrames);
            return 1;
        }
        if (flags & MF_SOURCE_READERF_STREAMTICK) return 0;
        if (!sample) return 0;

        /* Update current position */
        g_currentPts = ts;

        if (g_firstPts < 0) g_firstPts = ts;

        pts_relative = ts - g_firstPts;
        QueryPerformanceCounter(&now);
        elapsed_100ns = ((now.QuadPart - g_startTime) * 10000000LL) / g_freq;
        wait_ns = pts_relative - elapsed_100ns;

        /* If frame is more than 33ms late and we haven't dropped too many, skip it */
        if (wait_ns < -330000 && drop_count < MAX_DROP) {
            g_droppedFrames++;
            drop_count++;
            IMFSample_Release(sample);
            sample = NULL;
            continue;
        }

        break;  /* Frame is on time or we've dropped enough */
    }

    /* If frame is early, wait */
    if (wait_ns > 10000) {
        DWORD wait_ms = (DWORD)(wait_ns / 10000);
        if (wait_ms > 500) wait_ms = 500;  /* Cap at 500ms */
        timeBeginPeriod(1);
        Sleep(wait_ms);
        timeEndPeriod(1);
    }

    /* Get buffer and render */
    hr = IMFSample_ConvertToContiguousBuffer(sample, &buffer);
    if (FAILED(hr)) { IMFSample_Release(sample); return -1; }

    hr = IMFMediaBuffer_Lock(buffer, &data, &buf_len, NULL);
    if (FAILED(hr)) { IMFMediaBuffer_Release(buffer); IMFSample_Release(sample); return -1; }

    /* Use hardware rendering if available */
    if (g_dxva2_use_hw_render && g_dxva2_initialized) {
        /* DXVA2 hardware rendering */
        if (g_dxva2_surface == NULL) {
            g_dxva2_surface = dxva2_create_surface(g_width, g_height, D3DFMT_NV12);
        }

        if (g_dxva2_surface) {
            /* Upload NV12 data to DXVA2 surface */
            if (dxva2_upload_surface(g_dxva2_surface, data, g_stride, D3DFMT_NV12) == 0) {
                /* Use video processor to render */
                dxva2_processor_render(g_dxva2_surface, NULL, NULL);
            } else {
                /* Fallback to software rendering */
                mf_render_frame(data, g_stride, g_width, g_height, g_hwnd);
            }
        } else {
            /* Fallback to software rendering */
            mf_render_frame(data, g_stride, g_width, g_height, g_hwnd);
        }
    } else if (g_d3d11_use_hw_render && g_d3d11_initialized) {
        /* D3D11 hardware rendering */
        if (g_d3d11_texture == NULL) {
            g_d3d11_texture = d3d11_video_create_texture(g_width, g_height, DXGI_FORMAT_NV12);
        }

        if (g_d3d11_texture) {
            /* Upload NV12 data to D3D11 texture */
            if (d3d11_video_upload_texture(g_d3d11_texture, data, g_stride, DXGI_FORMAT_NV12) == 0) {
                /* Use video processor to render, centered with aspect ratio */
                d3d11_video_processor_render(g_d3d11_texture, NULL, NULL, g_width, g_height);
            } else {
                /* Fallback to software rendering */
                mf_render_frame(data, g_stride, g_width, g_height, g_hwnd);
            }
        } else {
            /* Fallback to software rendering */
            mf_render_frame(data, g_stride, g_width, g_height, g_hwnd);
        }
    } else if (g_d3d12_use_hw_render && g_d3d12_initialized) {
        /* D3D12 hardware rendering */
#ifdef __MINGW32__
        /* MinGW: D3D12 video API not fully supported, use software rendering */
        mf_render_frame(data, g_stride, g_width, g_height, g_hwnd);
#else
        /* MSVC: Use D3D12 video processor for rendering */
        if (g_d3d12_texture == NULL) {
            /* Create D3D12 texture for upload */
            g_d3d12_texture = (void*)1; /* Placeholder - actual texture creation would go here */
        }

        if (g_d3d12_texture) {
            /* Upload NV12 data to D3D12 texture */
            if (d3d12_video_upload_texture(data, g_stride, 0) == 0) {
                /* Use video processor to render */
                d3d12_video_processor_render(g_d3d12_texture, NULL);
            } else {
                /* Fallback to software rendering */
                mf_render_frame(data, g_stride, g_width, g_height, g_hwnd);
            }
        } else {
            /* Fallback to software rendering */
            mf_render_frame(data, g_stride, g_width, g_height, g_hwnd);
        }
#endif
    } else {
        /* Software rendering */
        mf_render_frame(data, g_stride, g_width, g_height, g_hwnd);
    }

    g_frameCount++;

    IMFMediaBuffer_Unlock(buffer);
    IMFMediaBuffer_Release(buffer);
    IMFSample_Release(sample);

    return 0;
}

void mf_stop(void)
{
    mf_cleanup_internals();
    MFShutdown();
}

int mf_is_active(void) { return (g_active && !g_eof) ? 1 : 0; }
long long mf_get_position(void)
{
    if (g_firstPts < 0 || g_currentPts < g_firstPts) return 0;
    return g_currentPts - g_firstPts;
}
long long mf_get_duration(void) { return g_duration; }
int mf_is_using_dxva2(void) { return g_dxva2_initialized || g_d3d11_initialized; }

const wchar_t *mf_get_decoder_info(void)
{
    static wchar_t info[256];
    if (g_active) {
        if (g_dxva2_initialized) {
            swprintf(info, 256, L"MF + DXVA2 %dx%d (dropped %d/%d)",
                     g_width, g_height, g_droppedFrames, g_frameCount);
        } else if (g_d3d11_initialized) {
            swprintf(info, 256, L"MF + D3D11 %dx%d (dropped %d/%d)",
                     g_width, g_height, g_droppedFrames, g_frameCount);
        } else {
            swprintf(info, 256, L"MF %dx%d (dropped %d/%d)",
                     g_width, g_height, g_droppedFrames, g_frameCount);
        }
    } else {
        swprintf(info, 256, L"Media Foundation: Not active");
    }
    return info;
}

int mf_get_width(void)  { return g_width; }
int mf_get_height(void) { return g_height; }
int mf_has_video(void) { return g_hasVideo; }
int mf_has_audio(void) { return g_hasAudio; }

const wchar_t *mf_get_video_codec(void)  { return g_videoCodec[0] ? g_videoCodec : L"N/A"; }
const wchar_t *mf_get_audio_codec(void)  { return g_audioCodec[0] ? g_audioCodec : L"N/A"; }
UINT32 mf_get_video_bitrate(void) { return g_videoBitrate; }
UINT32 mf_get_audio_bitrate(void) { return g_audioBitrate; }

double mf_get_video_fps(void) {
    if (g_videoFPS_Num > 0 && g_videoFPS_Den > 0)
        return (double)g_videoFPS_Num / (double)g_videoFPS_Den;
    return 0.0;
}

int mf_get_dropped_frames(void) { return g_droppedFrames; }
int mf_get_total_frames(void) { return g_frameCount; }

const wchar_t *mf_get_renderer_name(void)
{
    if (!g_active) return L"None";
    if (g_d3d12_use_hw_render && g_d3d12_initialized) return L"D3D12 Presenter";
    if (g_d3d11_use_hw_render && g_d3d11_initialized) return L"D3D11 Presenter";
    if (g_dxva2_use_hw_render && g_dxva2_initialized) return L"EVR";
    return L"EVR";  /* Media Foundation uses EVR as default renderer */
}

#ifndef DXVA2_HELPER_H
#define DXVA2_HELPER_H

#include <windows.h>
#include <d3d9.h>
#include <dxva2api.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DXVA2_VideoFormat may not be available in MinGW */
#ifndef DXVA2_VideoFormat_Unknown
typedef enum _DXVA2_VideoFormat {
    DXVA2_VideoFormat_Unknown = 0,
    DXVA2_VideoFormat_ProgressiveScan = 1,
    DXVA2_VideoFormat_FieldInterleavedEvenFirst = 2,
    DXVA2_VideoFormat_FieldInterleavedOddFirst = 3,
    DXVA2_VideoFormat_FieldSingleEven = 4,
    DXVA2_VideoFormat_FieldSingleOdd = 5,
    DXVA2_VideoFormat_SubStream = 6,
    DXVA2_VideoFormat_NV12 = 100,
    DXVA2_VideoFormat_YUY2 = 101,
    DXVA2_VideoFormat_RGB32 = 102,
} DXVA2_VideoFormat;
#endif

/*
 * DXVA2 hardware acceleration helper.
 * Manages Direct3D9 device, DXVA2 decoder, and video processor.
 */

/* Initialize DXVA2 with a HWND for rendering.
 * Returns D3D device pointer on success, NULL on failure. */
IDirect3DDevice9 *dxva2_init(HWND hwnd, int width, int height);

/* Check if DXVA2 hardware acceleration is available */
int dxva2_check_support(void);

/* Get DXVA2 device description */
const wchar_t *dxva2_get_device_info(void);

/* Cleanup DXVA2 resources */
void dxva2_cleanup(void);

/* Get the current D3D device (NULL if not initialized) */
IDirect3DDevice9 *dxva2_get_device(void);

/* Present the current back buffer */
int dxva2_present(void);

/* Get a surface for rendering */
IDirect3DSurface9 *dxva2_get_render_target(void);

/* ==================== DXVA2 Decoder Functions ==================== */

/* DXVA2 decoder configuration */
typedef struct {
    GUID guid;           /* DXVA2 decoder GUID (e.g., DXVA2_ModeH264) */
    UINT width;          /* Video width */
    UINT height;         /* Video height */
    UINT num_surfaces;   /* Number of decode surfaces */
} DXVA2DecoderConfig;

/* Initialize DXVA2 decoder for H.264/HEVC/etc.
 * Returns 0 on success, non-zero on failure. */
int dxva2_decoder_init(const DXVA2DecoderConfig *config);

/* Get a decode surface for the decoder.
 * Returns surface index on success, -1 on failure. */
int dxva2_decoder_get_surface(IDirect3DSurface9 **surface);

/* Release a decode surface back to the pool */
void dxva2_decoder_release_surface(int index);

/* Execute DXVA2 decode with the given parameters.
 * buffer: compressed bitstream data
 * size: size of bitstream data
 * pts: presentation timestamp
 * Returns 0 on success, non-zero on failure. */
int dxva2_decoder_decode(const BYTE *buffer, DWORD size, LONGLONG pts);

/* Get the decoded surface for rendering.
 * Returns the surface pointer, or NULL if no decoded surface available. */
IDirect3DSurface9 *dxva2_decoder_get_decoded_surface(void);

/* Cleanup DXVA2 decoder */
void dxva2_decoder_cleanup(void);

/* ==================== DXVA2 Video Processor Functions ==================== */

/* Initialize DXVA2 video processor for color space conversion and rendering.
 * input_format: source video format (e.g., DXVA2_VideoFormat_NV12)
 * output_format: target format (e.g., DXVA2_VideoFormat_RGB32)
 * Returns 0 on success, non-zero on failure. */
int dxva2_processor_init(DXVA2_VideoFormat input_format, DXVA2_VideoFormat output_format);

/* Process and render a video surface to the render target.
 * src_surface: source video surface (decoded frame)
 * src_rect: source rectangle (NULL for full frame)
 * dst_rect: destination rectangle (NULL for full render target)
 * Returns 0 on success, non-zero on failure. */
int dxva2_processor_render(IDirect3DSurface9 *src_surface,
                           const RECT *src_rect, const RECT *dst_rect);

/* Cleanup DXVA2 video processor */
void dxva2_processor_cleanup(void);

/* ==================== DXVA2 Surface Helper Functions ==================== */

/* Create a DXVA2 surface for video processing.
 * format: surface format (e.g., D3DFMT_NV12, D3DFMT_YUY2)
 * Returns surface pointer on success, NULL on failure. */
IDirect3DSurface9 *dxva2_create_surface(UINT width, UINT height, D3DFORMAT format);

/* Copy data from system memory to a DXVA2 surface.
 * data: source data pointer
 * stride: source stride (bytes per row)
 * format: source format (must match surface format)
 * Returns 0 on success, non-zero on failure. */
int dxva2_upload_surface(IDirect3DSurface9 *surface, const BYTE *data, int stride, D3DFORMAT format);

#ifdef __cplusplus
}
#endif

#endif /* DXVA2_HELPER_H */

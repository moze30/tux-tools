#ifndef D3D11_VIDEO_HELPER_H
#define D3D11_VIDEO_HELPER_H

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * D3D11 Video Acceleration Helper
 * Manages D3D11 device, video decoder, and video processor.
 */

/* Initialize D3D11 device for video processing.
 * Returns 0 on success, non-zero on failure. */
int d3d11_video_init(HWND hwnd, int width, int height);

/* Check if D3D11 video acceleration is available */
int d3d11_video_check_support(void);

/* Get D3D11 device description */
const wchar_t *d3d11_video_get_device_info(void);

/* Cleanup D3D11 resources */
void d3d11_video_cleanup(void);

/* Get the current D3D11 device (NULL if not initialized) */
ID3D11Device *d3d11_video_get_device(void);

/* Get the D3D11 device context */
ID3D11DeviceContext *d3d11_video_get_context(void);

/* Check if D3D11 device is already initialized */
int d3d11_video_is_initialized(void);

/* Present the current frame */
int d3d11_video_present(void);

/* ==================== D3D11 Video Decoder Functions ==================== */

/* D3D11 video decoder configuration */
typedef struct {
    GUID guid;           /* DXVA decoder GUID (e.g., D3D11_DECODER_PROFILE_H264_VLD_NOFGT) */
    UINT width;          /* Video width */
    UINT height;         /* Video height */
    UINT num_surfaces;   /* Number of decode surfaces */
} D3D11VideoDecoderConfig;

/* Initialize D3D11 video decoder for H.264/HEVC/etc.
 * Returns 0 on success, non-zero on failure. */
int d3d11_video_decoder_init(const D3D11VideoDecoderConfig *config);

/* Decode a frame using D3D11 video decoder.
 * buffer: compressed bitstream data
 * size: size of bitstream data
 * pts: presentation timestamp
 * Returns 0 on success, non-zero on failure. */
int d3d11_video_decoder_decode(const BYTE *buffer, DWORD size, LONGLONG pts);

/* Get the decoded texture for rendering.
 * Returns the texture pointer, or NULL if no decoded texture available. */
ID3D11Texture2D *d3d11_video_decoder_get_texture(void);

/* Cleanup D3D11 video decoder */
void d3d11_video_decoder_cleanup(void);

/* ==================== D3D11 Video Processor Functions ==================== */

/* Initialize D3D11 video processor for color space conversion and rendering.
 * Returns 0 on success, non-zero on failure. */
int d3d11_video_processor_init(void);

/* Process and render a video texture to the render target.
 * src_texture: source video texture (decoded frame)
 * src_rect: source rectangle (NULL for full frame)
 * dst_rect: destination rectangle (NULL = auto center with aspect ratio)
 * video_w, video_h: source video dimensions for aspect ratio calculation
 * Returns 0 on success, non-zero on failure. */
int d3d11_video_processor_render(ID3D11Texture2D *src_texture,
                                 const RECT *src_rect, const RECT *dst_rect,
                                 int video_w, int video_h);

/* Cleanup D3D11 video processor */
void d3d11_video_processor_cleanup(void);

/* ==================== D3D11 Texture Helper Functions ==================== */

/* Create a D3D11 texture for video processing.
 * format: texture format (e.g., DXGI_FORMAT_NV12, DXGI_FORMAT_YUY2)
 * Returns texture pointer on success, NULL on failure. */
ID3D11Texture2D *d3d11_video_create_texture(UINT width, UINT height, DXGI_FORMAT format);

/* Copy data from system memory to a D3D11 texture.
 * data: source data pointer
 * stride: source stride (bytes per row)
 * format: source format (must match texture format)
 * Returns 0 on success, non-zero on failure. */
int d3d11_video_upload_texture(ID3D11Texture2D *texture, const BYTE *data, int stride, DXGI_FORMAT format);

#ifdef __cplusplus
}
#endif

#endif /* D3D11_VIDEO_HELPER_H */
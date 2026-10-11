#ifndef D3D12_VIDEO_HELPER_H
#define D3D12_VIDEO_HELPER_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* D3D12 Video Acceleration Helper
 * Manages D3D12 device, video decoder, and video processor for hardware-accelerated decoding.
 */

/* Initialize D3D12 video device and swap chain.
 * Returns 0 on success, -1 on failure.
 */
int d3d12_video_init(HWND hwnd, int width, int height);

/* Check if D3D12 video acceleration is available.
 * Returns 1 if available, 0 otherwise.
 */
int d3d12_video_check_support(void);

/* Get device info string */
const wchar_t *d3d12_video_get_device_info(void);

/* Cleanup all D3D12 resources */
void d3d12_video_cleanup(void);

/* Initialize D3D12 video decoder for H.264/HEVC.
 * Returns 0 on success, -1 on failure.
 */
int d3d12_video_decoder_init(int width, int height);

/* Cleanup video decoder resources */
void d3d12_video_decoder_cleanup(void);

/* Upload decoded frame data to D3D12 texture.
 * Returns 0 on success, -1 on failure.
 */
int d3d12_video_upload_texture(void *data, int stride, int format);

/* Initialize D3D12 video processor for color conversion/scaling.
 * Returns 0 on success, -1 on failure.
 */
int d3d12_video_processor_init(void);

/* Cleanup video processor resources */
void d3d12_video_processor_cleanup(void);

/* Render frame using D3D12 video processor.
 * Returns 0 on success, -1 on failure.
 */
int d3d12_video_processor_render(void *input_texture, void *output_texture);

/* Present the swap chain */
int d3d12_video_present(void);

/* Check if D3D12 is initialized */
int d3d12_video_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* D3D12_VIDEO_HELPER_H */

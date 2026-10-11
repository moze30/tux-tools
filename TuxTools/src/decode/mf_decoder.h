#ifndef MF_DECODER_H
#define MF_DECODER_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Media Foundation video decoder.
 * Uses IMFSourceReader to decode video frames.
 *
 * enable_dxva2: if non-zero, attempt to use DXVA2 hardware acceleration.
 * Returns 0 on success, non-zero on failure.
 */
int mf_open(const wchar_t *filepath, HWND hwnd_display, int enable_dxva2);

/* Decode and render next frame. Returns 0 on success, 1 on EOF, -1 on error */
int mf_render_next_frame(void);

/* Stop and release all resources */
void mf_stop(void);

/* Check if decoder is active */
int mf_is_active(void);

/* Get current position in 100-ns units */
long long mf_get_position(void);

/* Get duration in 100-ns units */
long long mf_get_duration(void);

/* Check if DXVA2 is being used */
int mf_is_using_dxva2(void);

/* Get decoder description */
const wchar_t *mf_get_decoder_info(void);

/* Get video dimensions (0 for audio-only) */
int mf_get_width(void);
int mf_get_height(void);

/* Media info queries */
int mf_has_video(void);
int mf_has_audio(void);
const wchar_t *mf_get_video_codec(void);
const wchar_t *mf_get_audio_codec(void);
UINT32 mf_get_video_bitrate(void);
UINT32 mf_get_audio_bitrate(void);
double mf_get_video_fps(void);

/* Get dropped frames count */
int mf_get_dropped_frames(void);

/* Get total frames rendered */
int mf_get_total_frames(void);

/* Get the name of the current video renderer being used.
 * Returns a string like "EVR", "D3D11 Presenter", "D3D12 Presenter", "Software", or "None". */
const wchar_t *mf_get_renderer_name(void);

#ifdef __cplusplus
}
#endif

#endif /* MF_DECODER_H */

#ifndef DIRECTSHOW_DECODER_H
#define DIRECTSHOW_DECODER_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DirectShow-based video decoder.
 * Uses IGraphBuilder + IMediaControl + IVideoWindow.
 * Returns 0 on success, non-zero on failure.
 */
int ds_open(const wchar_t *filepath, HWND hwnd_display);

/* Open with DXVA2 hardware acceleration (uses VMR-9 or EVR renderer).
 * enable_dxva2: 0=software, 1=DXVA2 hardware acceleration
 * Returns 0 on success, non-zero on failure. */
int ds_open_dxva2(const wchar_t *filepath, HWND hwnd_display, int enable_dxva2);

/* Start playback */
int ds_play(void);

/* Stop playback and release all resources */
void ds_stop(void);

/* Check if currently playing */
int ds_is_playing(void);

/* Get current position in seconds */
double ds_get_position(void);

/* Get duration in seconds */
double ds_get_duration(void);

/* Set volume (0.0 - 1.0) */
void ds_set_volume(float vol);

/* Resize the video window to fit the given client area */
void ds_resize(int x, int y, int w, int h);

/* Check if DXVA2 is being used */
int ds_is_using_dxva2(void);

/* Media info queries */
int ds_has_video(void);
int ds_get_video_width(void);
int ds_get_video_height(void);

/* Enumerate registered DirectShow filters and log them.
 * category: 0=all, 1=video decoders, 2=audio decoders, 3=video compressors, 4=audio compressors
 * Returns the number of filters found. */
int ds_enum_filters(int category);

/* Get the name of the current video renderer being used.
 * Returns a string like "VMR-9", "EVR", "Default", or "None" if not playing. */
const wchar_t *ds_get_renderer_name(void);

/* Set Wine compatibility fix mode.
 * When enabled, DirectShow + DXVA2 will use default renderer instead of VMR-9/EVR.
 * This fixes video display issues in Wine environment. */
void ds_set_wine_fix(int enable);

#ifdef __cplusplus
}
#endif

#endif /* DIRECTSHOW_DECODER_H */

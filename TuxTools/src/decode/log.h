#ifndef TUX_DECODE_LOG_H
#define TUX_DECODE_LOG_H

/*
 * Logging for the decoder cores.
 *
 * The upstream project backed these two calls with an EDIT control, and the
 * decoder sources only ever call the functions below - never the control - so
 * replacing the sink is enough to route the same messages into this project's
 * own drawn log panel. The decoder files are therefore used unmodified.
 */

#include <windows.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Receives every line the decoder cores log. Set from the UI; when it is null
 * the messages are dropped. */
typedef void (*DecodeLogSink)(const wchar_t *text);
void DecodeLog_SetSink(DecodeLogSink sink);

void Log_Append(const wchar_t *text);
void Log_Printf(const wchar_t *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* TUX_DECODE_LOG_H */

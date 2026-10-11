/*
 * Sink for the decoder cores' log calls.
 *
 * Upstream appended into an EDIT control; here the text is handed to whichever
 * sink the UI installed, so it lands in this project's own drawn log panel
 * instead. Keeping the sink behind a function pointer is what lets the five
 * decoder sources be used exactly as they are.
 */

#include "log.h"

#include <stdio.h>
#include <string.h>

static DecodeLogSink g_sink = NULL;

void DecodeLog_SetSink(DecodeLogSink sink)
{
    g_sink = sink;
}

void Log_Append(const wchar_t *text)
{
    if (!g_sink || !text) return;
    g_sink(text);
}

void Log_Printf(const wchar_t *fmt, ...)
{
    if (!g_sink) return;

    wchar_t msg[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(msg, _countof(msg), _TRUNCATE, fmt, args);
    va_end(args);

    /* The caller's messages carry no newline of their own. */
    size_t len = wcslen(msg);
    if (len + 2 < _countof(msg)) {
        msg[len] = L'\r';
        msg[len + 1] = L'\n';
        msg[len + 2] = 0;
    }
    g_sink(msg);
}

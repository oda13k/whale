
#include <whale/log.h>
#include <stdio.h>
#include <stdarg.h>

static const char* lvl_translations[] = {
    [FATAL] = "fatal",
    [ERR] = "error",
    [WARN] = "warning",
    [INFO] = "info",
    [DEBUG] = "debug"
};

int wh_log(LogLevel lvl, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    printf("[%s] ", lvl_translations[lvl]);
    vprintf(fmt, va);
    printf("\n");
    va_end(va);

    return 0;
}


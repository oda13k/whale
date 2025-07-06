
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <whale/log.h>

static const char* lvl_translations[] = {
    [FATAL] = "fatal",
    [ERR] = "error",
    [WARN] = "warn ",
    [INFO] = "info ",
    [DEBUG] = "debug"
};

static const char* lvl_colors[] = {
    [FATAL] = "\e[0;35m",
    [ERR] = "\e[0;31m",
    [WARN] = "\e[0;33m",
    [INFO] = "\e[0;97m",
    [DEBUG] = "\e[0;36m"
};

int wh_log(LogLevel lvl, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);

    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    char date[64];
    strftime(date, sizeof(date), "%Y/%m/%d %H:%M:%S", tm);

    bool term = true;
    if (term)
        printf("%s %s%s\e[0m | ", date, lvl_colors[lvl], lvl_translations[lvl]);
    else
        printf("%s %s | ", date, lvl_translations[lvl]);

    vprintf(fmt, va);
    printf("\n");
    va_end(va);
    return 0;
}

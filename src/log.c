
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
    [FATAL] = "\033[0;35m",
    [ERR] = "\033[0;31m",
    [WARN] = "\033[0;33m",
    [INFO] = "\033[0;97m",
    [DEBUG] = "\033[0;36m"
};

int wh_vlog(LogLevel lvl, const char* fmt, va_list vargs)
{
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    char date[64];
    strftime(date, sizeof(date), "%Y/%m/%d %H:%M:%S", tm);

    bool term = true;
    if (term)
        printf(
            "%s %s%s\033[0m | ", date, lvl_colors[lvl], lvl_translations[lvl]
        );
    else
        printf("%s %s | ", date, lvl_translations[lvl]);

    vprintf(fmt, vargs);
    printf("\n");

    return 0;
}

int wh_log(LogLevel lvl, const char* fmt, ...)
{
    va_list vargs;
    va_start(vargs, fmt);

    int rc = wh_vlog(lvl, fmt, vargs);

    va_end(vargs);

    return rc;
}

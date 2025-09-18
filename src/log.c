
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <whale/compositor.h>
#include <whale/debug.h>
#include <whale/log.h>

static const char* g_lvl_translations[] = {
    [FATAL] = "fatal",
    [ERR] = "error",
    [WARN] = "warn ",
    [INFO] = "info ",
    [DEBUG] = "debug"
};

static const char* g_lvl_colors[] = {
    [FATAL] = "\033[0;35m",
    [ERR] = "\033[0;31m",
    [WARN] = "\033[0;33m",
    [INFO] = "\033[0;97m",
    [DEBUG] = "\033[0;36m"
};

static FILE* g_logfile;

int wh_log_init()
{
    const char* logfile_path = "/home/oda/.whale.log";

    if (wh_compositor_running_on_bare_metal())
    {
        g_logfile = fopen(logfile_path, "at");
        if (!g_logfile)
            wh_log(ERR, "log: Failed to open log file for writing.");
        else
            fprintf(g_logfile, "\n");
    }

    wh_log(INFO, "Whale v0.0.0");

    return 0;
}

void wh_log_flush_buffers()
{
    if (g_logfile)
        fflush(g_logfile);

    fflush(stdout);
}

int wh_vlog(LogLevel lvl, const char* fmt, va_list vargs)
{
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    char date[64];
    strftime(date, sizeof(date), "%Y/%m/%d %H:%M:%S", tm);

    if (g_logfile)
    {
        va_list vargs2;
        va_copy(vargs2, vargs);

        fprintf(g_logfile, "%s %s | ", date, g_lvl_translations[lvl]);
        WH_NOWARN("-Wformat-nonliteral", vfprintf(g_logfile, fmt, vargs2);)
        fprintf(g_logfile, "\n");

        va_end(vargs2);
    }

    printf(
        "%s %s%s\033[0m | ", date, g_lvl_colors[lvl], g_lvl_translations[lvl]
    );
    WH_NOWARN("-Wformat-nonliteral", vprintf(fmt, vargs);)
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

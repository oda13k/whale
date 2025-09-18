
#ifndef WHALE_LOG_H
#define WHALE_LOG_H

#include <stdarg.h>

typedef enum
{
    FATAL,
    ERR,
    WARN,
    INFO,
    DEBUG
} LogLevel;

#define TODO_LOG(_msg)                                                         \
    wh_log(DEBUG, "TODO @ %s:%d: %s", __FILE__, __LINE__, _msg)

int wh_log_init();

void wh_log_flush_buffers();

[[gnu::format(printf, 2, 3)]] int wh_log(LogLevel lvl, const char* fmt, ...);

int wh_vlog(LogLevel lvl, const char* fmt, va_list vargs);

#endif // !WHALE_LOG_H

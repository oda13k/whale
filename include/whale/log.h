
#ifndef WHALE_LOG_H
#define WHALE_LOG_H

typedef enum
{
    FATAL,
    ERR,
    WARN,
    INFO,
    DEBUG
} LogLevel;

#define TODO_LOG(_msg)                                                         \
    wh_log(DEBUG, "TODO @ %s:%d: %s", __FILE__, __LINE__, _msg);

int wh_log(LogLevel lvl, const char* fmt, ...);

#endif // !WHALE_LOG_H
